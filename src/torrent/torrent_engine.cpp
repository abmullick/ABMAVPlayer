#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <iptypes.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include <cctype>
#include <limits>
#include <cwctype>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/info_hash.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/announce_entry.hpp>
#include "torrent_engine.h"
namespace lt=libtorrent;
namespace fs=std::filesystem;
namespace {
std::string ToUtf8(const std::wstring &s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.c_str(),-1,nullptr,0,nullptr,nullptr);
    if(n<=1) throw std::runtime_error("Invalid Unicode input");
    std::string out((size_t)n,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.c_str(),-1,out.data(),n,nullptr,nullptr);
    out.resize((size_t)n-1);
    return out;
}
std::wstring FromUtf8(const std::string &s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,nullptr,0);
    if(n<=1) return {};
    std::wstring out((size_t)n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,out.data(),n);
    out.resize((size_t)n-1);
    return out;
}
void CopyText(wchar_t *dst,size_t cap,const std::wstring &s) {
    if(dst&&cap) wcsncpy_s(dst,cap,s.c_str(),_TRUNCATE);
}
struct Task {
    uint64_t id=0;
    lt::torrent_handle handle;
    std::wstring name=L"Retrieving metadata...";
    std::wstring error;
    fs::path cache_path;
    bool metadata_applied=false;
    bool fallback_trackers_added=false;
    int file_count=0;
    std::atomic<bool> removed{false};
};
class Engine {
public:
    bool Init(const wchar_t *directory) {
        std::lock_guard<std::mutex> g(m_);
        if(session_) return true;
        if(!directory||!*directory) return false;
        try {
            data_=(fs::absolute(directory)/L"torrent-cache").lexically_normal();
            downloads_=(fs::absolute(directory)/L"Torrent Downloads").lexically_normal();
            fs::create_directories(data_);
            fs::create_directories(downloads_);
            cache_used_bytes_.store(DirectoryBytes(data_));
            {
                std::ifstream config(data_/L"cache_limit.txt");
                uint64_t persisted=0;
                if(config>>persisted&&persisted>=1024ULL*1024*1024&&persisted<=4096ULL*1024*1024*1024)
                    cache_limit_bytes_=persisted;
            }
            lt::settings_pack s;
            s.set_str(lt::settings_pack::user_agent,"ABM-AV-Player/1.0");
            s.set_str(lt::settings_pack::listen_interfaces,"0.0.0.0:0");
            /* Use several independent bootstrap routers. Relying on a single
               default hostname can leave DHT effectively offline on networks
               where that DNS name or endpoint is unreachable. */
            s.set_str(lt::settings_pack::dht_bootstrap_nodes,
                "dht.libtorrent.org:25401,router.bittorrent.com:6881,"
                "dht.transmissionbt.com:6881,router.bt.ouinet.work:6881");
            s.set_bool(lt::settings_pack::enable_dht,true);
            s.set_bool(lt::settings_pack::enable_lsd,true);
            /* Do not wait for every tracker in a tier to fail before trying
               alternatives. This is important for torrents carrying stale
               tracker lists. DHT remains active alongside tracker announces. */
            s.set_bool(lt::settings_pack::announce_to_all_trackers,true);
            s.set_bool(lt::settings_pack::announce_to_all_tiers,true);
            s.set_bool(lt::settings_pack::use_dht_as_fallback,false);
            s.set_bool(lt::settings_pack::enable_upnp,true);
            s.set_bool(lt::settings_pack::enable_natpmp,true);
            s.set_bool(lt::settings_pack::enable_outgoing_tcp,true);
            s.set_bool(lt::settings_pack::enable_outgoing_utp,true);
            s.set_int(lt::settings_pack::connection_speed,50);
            s.set_int(lt::settings_pack::connections_limit,200);
            WSADATA wsa{};if(WSAStartup(MAKEWORD(2,2),&wsa)!=0)return false;
            winsock_ready_=true;
            session_=std::make_unique<lt::session>(s);
            /* enable_dht is a session setting, but explicitly start the DHT
               service as well. This avoids reporting DHT OFF indefinitely on
               builds/configurations where the session is created before the
               DHT subsystem has been started. */
            try { session_->start_dht(); } catch(...) {}
            /* Explicit bootstrap probes supplement dht_bootstrap_nodes and
               make the initial routing-table join less dependent on one DNS
               result. Failures here are non-fatal; Monitor retries startup. */
            try{
                session_->add_dht_node({"router.bittorrent.com",6881});
                session_->add_dht_node({"dht.transmissionbt.com",6881});
                session_->add_dht_node({"router.utorrent.com",6881});
            }catch(...){}
            stopping_=false;
            worker_=std::thread([this]{Monitor();});
            return true;
        } catch(...) { session_.reset();if(winsock_ready_){WSACleanup();winsock_ready_=false;}return false; }
    }
    void Stop() {
        StopHttpStream();
        stopping_=true;
        if(worker_.joinable()) worker_.join();
        std::lock_guard<std::mutex> g(m_);
        for(auto &entry:tasks_)entry.second->removed.store(true);
        tasks_.clear();
        session_.reset();
        if(winsock_ready_){WSACleanup();winsock_ready_=false;}
    }
    bool AddMagnet(const wchar_t *uri,uint64_t *id,std::wstring &err) {
        if(!uri||!id) {err=L"Magnet URI is empty.";return false;}
        std::wstring w(uri);
        if(w.size()>8192||w.rfind(L"magnet:?",0)!=0) {err=L"Only a valid magnet URI is accepted.";return false;}
        try {
            lt::error_code ec;
            auto p=lt::parse_magnet_uri(ToUtf8(w),ec);
            if(ec){err=FromUtf8(ec.message());return false;}
            p.save_path=(downloads_/HashFolder(p.info_hashes)).u8string();
            return Add(std::move(p),id,err);
        } catch(const std::exception &e){err=FromUtf8(e.what());return false;}
        catch(...){err=L"Unable to parse magnet URI.";return false;}
    }
    bool AddFile(const wchar_t *path,uint64_t *id,std::wstring &err) {
        if(!path||!*path||!id){err=L"Torrent file path is empty.";return false;}
        try {
            fs::path p=fs::canonical(fs::path(path));
            if(!fs::is_regular_file(p)||fs::file_size(p)>16*1024*1024){err=L"Torrent file is missing or exceeds the 16 MiB metadata limit.";return false;}
            lt::error_code ec;
            lt::load_torrent_limits limits;
            limits.max_buffer_size=16*1024*1024;
            auto atp=lt::load_torrent_file(p.u8string(),ec,limits);
            if(ec||!atp.ti||!atp.ti->is_valid()){err=ec?FromUtf8(ec.message()):L"Invalid torrent metadata.";return false;}
            atp.save_path=(downloads_/HashFolder(atp.ti->info_hashes())).u8string();
            return Add(std::move(atp),id,err);
        } catch(const std::exception &e){err=FromUtf8(e.what());return false;}
        catch(...){err=L"Unable to load torrent file.";return false;}
    }
    bool Get(uint64_t id,ABM_TorrentTaskInfo *out) {
        if(!out)return false;
        std::lock_guard<std::mutex> g(m_);
        auto i=tasks_.find(id); if(i==tasks_.end()||!i->second->handle.is_valid())return false;
        auto s=i->second->handle.status();
        ZeroMemory(out,sizeof(*out));
        out->task_id=id; out->progress=s.progress; out->total_bytes=s.total_wanted;
        out->downloaded_bytes=s.total_wanted_done; out->uploaded_bytes=s.total_upload;
        out->download_rate=s.download_rate; out->upload_rate=s.upload_rate; out->peer_count=s.num_peers;
        out->known_peers=s.list_peers;out->connect_candidates=s.connect_candidates;
        out->connections=s.num_connections;out->seeds=s.num_seeds;
        out->announcing_to_dht=s.announcing_to_dht?1:0;
        out->announcing_to_trackers=s.announcing_to_trackers?1:0;
        out->dht_running=(session_&&session_->is_dht_running())?1:0;
        { auto ti=i->second->handle.torrent_file();out->is_private=(ti&&ti->priv())?1:0; }
        out->has_metadata=s.has_metadata?1:0; out->file_count=i->second->file_count;
        CopyText(out->name,_countof(out->name),i->second->name);
        CopyText(out->error,_countof(out->error),i->second->error);
        try {
            const auto trackers=i->second->handle.trackers();
            out->tracker_count=(int)trackers.size();
            std::wstring trackerDiag;
            for(const auto &tr:trackers){
                if(tr.verified)out->tracker_verified++;
                for(const auto &endpoint:tr.endpoints)for(const auto &ih:endpoint.info_hashes){
                    out->tracker_failures+=(int)ih.fails;
                    if(!ih.last_error && !ih.message.empty() && trackerDiag.empty())
                        trackerDiag=FromUtf8(ih.message);
                    else if(ih.last_error && trackerDiag.empty())
                        trackerDiag=FromUtf8(ih.last_error.message());
                }
            }
            CopyText(out->tracker_diagnostic,_countof(out->tracker_diagnostic),trackerDiag);
        } catch(...) { }

        if(!i->second->error.empty())out->state=ABM_TORRENT_ERROR;
        else if(s.flags & lt::torrent_flags::paused)out->state=ABM_TORRENT_PAUSED;
        else switch(s.state){
            case lt::torrent_status::checking_files:
            case lt::torrent_status::checking_resume_data:out->state=ABM_TORRENT_CHECKING;break;
            case lt::torrent_status::downloading:
            case lt::torrent_status::downloading_metadata:out->state=ABM_TORRENT_DOWNLOADING;break;
            case lt::torrent_status::finished:out->state=ABM_TORRENT_FINISHED;break;
            case lt::torrent_status::seeding:out->state=ABM_TORRENT_SEEDING;break;
            default:out->state=ABM_TORRENT_UNKNOWN;break;
        }
        return true;
    }
    bool GetFile(uint64_t id,int index,wchar_t *path,unsigned cap,int64_t *size) {
        if(!path||!cap||!size)return false;
        std::lock_guard<std::mutex> g(m_);
        auto i=tasks_.find(id);if(i==tasks_.end())return false;
        auto ti=i->second->handle.torrent_file();
        if(!ti||index<0||index>=ti->layout().num_files())return false;
        CopyText(path,cap,FromUtf8(ti->layout().file_path(lt::file_index_t(index))));
        *size=ti->layout().file_size(lt::file_index_t(index));return true;
    }
    bool Select(uint64_t id,int index) {
        std::lock_guard<std::mutex> g(m_);
        auto i=tasks_.find(id);if(i==tasks_.end())return false;
        auto ti=i->second->handle.torrent_file();
        if(!ti||index<0||index>=ti->layout().num_files())return false;
        const uint64_t fileBytes=static_cast<uint64_t>(ti->layout().file_size(lt::file_index_t(index)));
        const uint64_t existing=DirectoryBytes(i->second->cache_path);
        const uint64_t additional=fileBytes>existing?fileBytes-existing:0;
        ULARGE_INTEGER freeAvailable{},totalBytes{},totalFree{};
        if(!GetDiskFreeSpaceExW(i->second->cache_path.c_str(),&freeAvailable,&totalBytes,&totalFree)||
           freeAvailable.QuadPart<additional) {
            i->second->error=L"Not enough free disk space in the permanent Torrent Downloads folder for this file.";
            return false;
        }
        i->second->error.clear();
        std::vector<lt::download_priority_t> p((size_t)ti->layout().num_files(),lt::dont_download);
        p[(size_t)index]=lt::default_priority;i->second->handle.prioritize_files(p);return true;
    }
    bool CacheInfo(uint64_t *used,uint64_t *limit) {
        if(!used||!limit)return false;
        std::lock_guard<std::mutex> g(m_);
        *used=cache_used_bytes_.load();*limit=cache_limit_bytes_;return true;
    }
    bool SetCacheLimit(uint64_t limit,uint64_t *used) {
        if(limit<1024ULL*1024*1024||limit>4096ULL*1024*1024*1024)return false;
        std::lock_guard<std::mutex> g(m_);
        cache_limit_bytes_=limit;
        {
            const fs::path temporary=data_/L"cache_limit.tmp";
            const fs::path config=data_/L"cache_limit.txt";
            std::ofstream output(temporary,std::ios::trunc);
            if(!output)return false;
            output<<cache_limit_bytes_;
            output.flush();
            if(!output)return false;
            output.close();
            if(!MoveFileExW(temporary.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                return false;
        }
        CleanupCacheToLimit();
        const uint64_t current=DirectoryBytes(data_);
        cache_used_bytes_.store(current);
        if(used)*used=current;
        return current<=cache_limit_bytes_;
    }
    bool ClearCache(uint64_t *freed,uint64_t *remaining) {
        std::lock_guard<std::mutex> g(m_);
        const uint64_t before=DirectoryBytes(data_);
        auto candidates=EvictableCacheFolders();
        for(const auto &path:candidates){std::error_code ec;fs::remove_all(path,ec);}
        const uint64_t after=DirectoryBytes(data_);
        cache_used_bytes_.store(after);
        if(freed)*freed=before>after?before-after:0;
        if(remaining)*remaining=after;
        return true;
    }
    bool Pause(uint64_t id,bool pause) {
        std::lock_guard<std::mutex> g(m_);auto i=tasks_.find(id);
        if(i==tasks_.end())return false;
        if(pause)i->second->handle.pause();else i->second->handle.resume();return true;
    }
    bool Remove(uint64_t id,bool delete_data) {
        if(http_task_id_.load()==id)StopHttpStream();
        std::lock_guard<std::mutex> g(m_);auto i=tasks_.find(id);
        if(i==tasks_.end()||!session_)return false;
        lt::remove_flags_t flags{};
        if(delete_data)flags=lt::session_handle::delete_files;
        i->second->removed.store(true);
        session_->remove_torrent(i->second->handle,flags);tasks_.erase(i);return true;
    }
    bool Limits(int down,int up) {
        if(down<0||up<0)return false;
        std::lock_guard<std::mutex> g(m_);if(!session_)return false;
        lt::settings_pack s;s.set_int(lt::settings_pack::download_rate_limit,down);
        s.set_int(lt::settings_pack::upload_rate_limit,up);session_->apply_settings(s);return true;
    }

    bool SetReadWindow(uint64_t id,int index,int64_t offset,int64_t length,int ahead) {
        std::shared_ptr<Task> task; 
        { std::lock_guard<std::mutex> g(m_); auto it=tasks_.find(id); if(it==tasks_.end())return false; task=it->second; }
        if(task->removed.load())return false;
        auto ti=task->handle.torrent_file();
        if(!ti||index<0||index>=ti->layout().num_files())return false;
        const int64_t size=ti->layout().file_size(lt::file_index_t(index));
        if(offset<0||length<=0||offset>=size)return false;
        if(ahead<0)ahead=0;if(ahead>64*1024*1024)ahead=64*1024*1024;
        const int pieceLen=ti->piece_length();if(pieceLen<=0)return false;
        const int64_t fileStart=ti->layout().file_offset(lt::file_index_t(index));
        const int64_t firstGlobal=fileStart+offset;
        const int64_t requestedEnd=std::min<int64_t>(size,offset+std::min<int64_t>(length,size-offset));
        const int64_t windowEnd=requestedEnd+std::min<int64_t>(size-requestedEnd,ahead);
        const int firstPiece=static_cast<int>(firstGlobal/pieceLen);
        const int lastPiece=static_cast<int>((fileStart+std::max<int64_t>(offset,windowEnd-1))/pieceLen);
        std::lock_guard<std::mutex> schedule(schedule_mutex_);
        try {
            task->handle.clear_piece_deadlines();
            int ordinal=0;
            for(int p=firstPiece;p<=lastPiece && ordinal<4096;++p,++ordinal) {
                int deadline=100+std::min(ordinal,120)*150;
                task->handle.set_piece_deadline(lt::piece_index_t(p),deadline);
            }
            return true;
        } catch(...) { return false; }
    }

    bool RangeVerified(uint64_t id,int index,int64_t offset,int64_t length,
                       std::shared_ptr<Task> *taskOut=nullptr,
                       std::shared_ptr<const lt::torrent_info> *infoOut=nullptr) {
        std::shared_ptr<Task> task;
        { std::lock_guard<std::mutex> g(m_); auto it=tasks_.find(id); if(it==tasks_.end())return false; task=it->second; }
        if(task->removed.load())return false;
        auto ti=task->handle.torrent_file();
        if(!ti||index<0||index>=ti->layout().num_files())return false;
        const int64_t size=ti->layout().file_size(lt::file_index_t(index));
        if(offset<0||length<0||offset>size||length>size-offset)return false;
        if(taskOut)*taskOut=task;if(infoOut)*infoOut=ti;
        if(length==0)return true;
        const int pieceLen=ti->piece_length();if(pieceLen<=0)return false;
        const int64_t start=ti->layout().file_offset(lt::file_index_t(index))+offset;
        const int64_t end=start+length-1;
        const int first=static_cast<int>(start/pieceLen);
        const int last=static_cast<int>(end/pieceLen);
        try {
            for(int p=first;p<=last;++p)
                if(!task->handle.have_piece(lt::piece_index_t(p)))return false;
        } catch(...) { return false; }
        return true;
    }

    bool WaitForRange(uint64_t id,int index,int64_t offset,int64_t length,
                      unsigned timeout,const std::atomic<bool> *cancel=nullptr) {
        std::shared_ptr<Task> task;
        {std::lock_guard<std::mutex> g(m_);auto it=tasks_.find(id);if(it==tasks_.end())return false;task=it->second;}
        const uint64_t generation=wait_cancel_generation_.load();
        if(length>0&&!SetReadWindow(id,index,offset,length,8*1024*1024))return false;
        const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
        do {
            if(task->removed.load()||wait_cancel_generation_.load()!=generation||
               (cancel&&cancel->load())||stopping_.load())return false;
            if(RangeVerified(id,index,offset,length))return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } while(std::chrono::steady_clock::now()<until);
        if(task->removed.load()||wait_cancel_generation_.load()!=generation||
           (cancel&&cancel->load())||stopping_.load())return false;
        return RangeVerified(id,index,offset,length);
    }

    bool ReadVerified(uint64_t id,int index,int64_t offset,char *buffer,size_t count,
                      unsigned timeout,const std::atomic<bool> *cancel=nullptr) {
        if(!buffer||count==0||count>1024*1024)return false;
        std::shared_ptr<Task> task;std::shared_ptr<const lt::torrent_info> ti;
        if(!RangeVerified(id,index,offset,static_cast<int64_t>(count),&task,&ti)) {
            if(!task) {
                { std::lock_guard<std::mutex> g(m_); auto it=tasks_.find(id);if(it!=tasks_.end())task=it->second; }
            }
            if(!task||task->removed.load())return false;
            auto metadata=task->handle.torrent_file();
            if(!metadata||index<0||index>=metadata->layout().num_files())return false;
            const int64_t size=metadata->layout().file_size(lt::file_index_t(index));
            if(offset<0||offset>size||static_cast<int64_t>(count)>size-offset)return false;
            if(!WaitForRange(id,index,offset,static_cast<int64_t>(count),timeout,cancel))return false;
            ti=std::move(metadata);
        }
        if(!task||task->removed.load()||!ti)return false;
        if(ti->layout().symlink(lt::file_index_t(index)).size()!=0||ti->layout().pad_file_at(lt::file_index_t(index)))return false;
        try {
            const std::string fullUtf8=ti->layout().file_path(lt::file_index_t(index),task->cache_path.u8string());
            fs::path target=fs::weakly_canonical(fs::u8path(fullUtf8));
            fs::path root=fs::weakly_canonical(task->cache_path);
            auto ri=root.begin(),tiPath=target.begin();
            for(;ri!=root.end();++ri,++tiPath)
                if(tiPath==target.end()||_wcsicmp(ri->c_str(),tiPath->c_str())!=0)return false;
            if(tiPath==target.end())return false;
            std::ifstream input(target,std::ios::binary);
            if(!input)return false;
            input.seekg(static_cast<std::streamoff>(offset),std::ios::beg);
            input.read(buffer,static_cast<std::streamsize>(count));
            return input.gcount()==static_cast<std::streamsize>(count);
        } catch(...) { return false; }
    }

    bool StartHttpStream(uint64_t id,int index,wchar_t *url,unsigned cap,std::wstring &err) {
        return StartHttpStreamInternal(id,index,url,cap,err,false);
    }
    bool StartLanHttpStream(uint64_t id,int index,wchar_t *url,unsigned cap,std::wstring &err) {
        return StartHttpStreamInternal(id,index,url,cap,err,true);
    }
    bool StartHttpStreamInternal(uint64_t id,int index,wchar_t *url,unsigned cap,std::wstring &err,bool lan) {
        if(!url||cap==0){err=L"Output URL buffer is unavailable.";return false;}
        StopHttpStream();
        if(!Select(id,index)){err=L"Select a valid torrent file after metadata is available.";return false;}
        std::shared_ptr<Task> task;
        {std::lock_guard<std::mutex> g(m_);auto it=tasks_.find(id);if(it==tasks_.end()){err=L"Torrent task no longer exists.";return false;}task=it->second;}
        auto ti=task->handle.torrent_file();
        if(!ti||index<0||index>=ti->layout().num_files()){err=L"Torrent file metadata is not ready.";return false;}
        if(ti->layout().symlink(lt::file_index_t(index)).size()!=0||ti->layout().pad_file_at(lt::file_index_t(index))){err=L"Symbolic-link and padding entries cannot be streamed.";return false;}
        const int64_t size=ti->layout().file_size(lt::file_index_t(index));
        if(size<=0){err=L"Empty files cannot be streamed.";return false;}
        unsigned char randomToken[32]{};
        if(BCryptGenRandom(nullptr,randomToken,sizeof(randomToken),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0){
            err=L"Windows could not create a secure stream token.";return false;
        }
        static const wchar_t hex[]=L"0123456789abcdef";
        std::wstring token;token.reserve(64);
        for(unsigned char b:randomToken){token.push_back(hex[b>>4]);token.push_back(hex[b&15]);}
        SecureZeroMemory(randomToken,sizeof(randomToken));
        sockaddr_in lanAddress{};ULONG lanMask=0;
        if(lan&&!FindPrivateLanIPv4(lanAddress,lanMask)) {
            err=L"No private IPv4 network adapter was found. Connect this PC to the same Wi-Fi/LAN as the TV.";
            return false;
        }
        SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(listener==INVALID_SOCKET){err=L"Could not create the HTTP stream socket.";return false;}
        BOOL exclusive=TRUE;setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&exclusive,sizeof(exclusive));
        sockaddr_in address{};address.sin_family=AF_INET;
        address.sin_addr.s_addr=lan?lanAddress.sin_addr.s_addr:htonl(INADDR_LOOPBACK);address.sin_port=0;
        if(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR||listen(listener,8)==SOCKET_ERROR){
            closesocket(listener);err=lan?L"Could not bind the TV stream to the private LAN interface. Check Windows Firewall permissions.":L"Could not bind the loopback-only HTTP stream.";return false;
        }
        int addressLen=sizeof(address);
        if(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&addressLen)==SOCKET_ERROR){
            closesocket(listener);err=L"Could not determine the local stream port.";return false;
        }
        const unsigned short port=ntohs(address.sin_port);
        std::wstring host=L"127.0.0.1";
        if(lan) {
            char ip[INET_ADDRSTRLEN]={0};
            if(!inet_ntop(AF_INET,&address.sin_addr,ip,sizeof(ip))) {
                closesocket(listener);err=L"Could not format the PC's private LAN address.";return false;
            }
            host=FromUtf8(ip);
        }
        std::wstring streamUrl=L"http://"+host+L":"+std::to_wstring(port)+L"/stream/"+token;
        if(streamUrl.size()+1>cap){closesocket(listener);err=L"Output URL buffer is too small.";return false;}
        http_stop_.store(false);
        http_task_id_.store(id);http_file_index_=index;http_file_size_=size;
        http_token_=token;http_socket_=listener;
        url[0]=0;wcsncpy_s(url,cap,streamUrl.c_str(),_TRUNCATE);
        try {
            const uint32_t lanIp=lan?ntohl(address.sin_addr.s_addr):0;
            http_accept_thread_=std::thread([this,listener,token,id,index,size,lan,lanIp,lanMask]{HttpAcceptLoop(listener,token,id,index,size,lan,lanIp,lanMask);});
        } catch(...) {
            closesocket(listener);http_socket_=INVALID_SOCKET;http_task_id_.store(0);http_token_.clear();
            err=L"Could not start the local HTTP stream worker.";return false;
        }
        return true;
    }

    void StopHttpStream() {
        std::lock_guard<std::mutex> life(http_lifecycle_mutex_);
        http_stop_.store(true);
        wait_cancel_generation_.fetch_add(1);
        if(http_socket_!=INVALID_SOCKET)shutdown(http_socket_,SD_BOTH);
        if(http_accept_thread_.joinable())http_accept_thread_.join();
        if(http_socket_!=INVALID_SOCKET){closesocket(http_socket_);http_socket_=INVALID_SOCKET;}
        http_task_id_.store(0);http_file_index_=-1;http_file_size_=0;
        if(!http_token_.empty())SecureZeroMemory(http_token_.data(),http_token_.size()*sizeof(wchar_t));
        http_token_.clear();
    }

private:

    static bool SendAll(SOCKET client,const char *data,size_t size) {
        while(size) {
            int part=static_cast<int>(std::min<size_t>(size,64*1024));
            int sent=send(client,data,part,0);
            if(sent<=0)return false;
            data+=sent;size-=static_cast<size_t>(sent);
        }
        return true;
    }
    static std::string Lower(std::string v) {
        std::transform(v.begin(),v.end(),v.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        return v;
    }
    static std::string MimeType(const std::wstring &path) {
        std::wstring ext=fs::path(path).extension().wstring();
        for(auto &c:ext)c=static_cast<wchar_t>(towlower(c));
        if(ext==L".mp4"||ext==L".m4v")return "video/mp4";
        if(ext==L".mkv")return "video/x-matroska";
        if(ext==L".webm")return "video/webm";
        if(ext==L".mov")return "video/quicktime";
        if(ext==L".avi")return "video/x-msvideo";
        if(ext==L".mp3")return "audio/mpeg";
        if(ext==L".flac")return "audio/flac";
        if(ext==L".wav")return "audio/wav";
        if(ext==L".ogg"||ext==L".ogv")return "application/ogg";
        if(ext==L".m4a")return "audio/mp4";
        return "application/octet-stream";
    }
    static bool ParseUnsigned(const std::string &v,uint64_t &out) {
        if(v.empty())return false;
        out=0;
        for(unsigned char c:v){if(c<'0'||c>'9')return false;unsigned d=c-'0';if(out>(UINT64_MAX-d)/10)return false;out=out*10+d;}
        return true;
    }
    struct HttpClientWorker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    static bool IsPrivateIPv4(uint32_t ip) {
        return (ip>>24)==10 || (ip>>20)==0xAC1 || (ip>>16)==0xC0A8;
    }
    static bool FindPrivateLanIPv4(sockaddr_in &selected,ULONG &maskOut) {
        ULONG bytes=16*1024;
        std::vector<unsigned char> storage(bytes);
        ULONG rc=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,
                                      nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()),&bytes);
        if(rc==ERROR_BUFFER_OVERFLOW){storage.resize(bytes);rc=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,
                                      nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()),&bytes);}
        if(rc!=NO_ERROR)return false;
        for(auto adapter=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());adapter;adapter=adapter->Next) {
            if(adapter->OperStatus!=IfOperStatusUp||adapter->IfType==IF_TYPE_SOFTWARE_LOOPBACK)continue;
            for(auto unicast=adapter->FirstUnicastAddress;unicast;unicast=unicast->Next) {
                if(!unicast->Address.lpSockaddr||unicast->Address.lpSockaddr->sa_family!=AF_INET)continue;
                auto *sin=reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
                uint32_t ip=ntohl(sin->sin_addr.s_addr);
                if(!IsPrivateIPv4(ip))continue;
                ULONG prefix=unicast->OnLinkPrefixLength;
                if(prefix>32)continue;
                maskOut=prefix==0?0u:(0xFFFFFFFFu<<(32-prefix));
                selected=*sin;selected.sin_port=0;
                return true;
            }
        }
        return false;
    }
    void HttpAcceptLoop(SOCKET listener,const std::wstring &token,uint64_t id,int index,int64_t size,
                        bool lan,uint32_t lanIp,ULONG lanMask) {
        while(!http_stop_.load()) {
            fd_set readSet;FD_ZERO(&readSet);FD_SET(listener,&readSet);
            timeval tv{};tv.tv_sec=0;tv.tv_usec=250000;
            int ready=select(0,&readSet,nullptr,nullptr,&tv);
            if(ready==SOCKET_ERROR)break;
            if(ready==0)continue;
            sockaddr_in remote{};int remoteLen=sizeof(remote);
            SOCKET client=accept(listener,reinterpret_cast<sockaddr*>(&remote),&remoteLen);
            if(client==INVALID_SOCKET)continue;
            const uint32_t remoteIp=ntohl(remote.sin_addr.s_addr);
            if(lan) {
                if(!IsPrivateIPv4(remoteIp)||(remoteIp&lanMask)!=(lanIp&lanMask)){closesocket(client);continue;}
            } else if(remote.sin_addr.s_addr!=htonl(INADDR_LOOPBACK)){closesocket(client);continue;}
            std::lock_guard<std::mutex> g(http_clients_mutex_);
            for(auto it=http_clients_.begin();it!=http_clients_.end();) {
                if(it->done&&it->done->load()) {
                    if(it->thread.joinable())it->thread.join();
                    it=http_clients_.erase(it);
                } else ++it;
            }
            if(http_clients_.size()>=4){closesocket(client);continue;}
            try {
                auto done=std::make_shared<std::atomic<bool>>(false);
                http_clients_.emplace_back();
                HttpClientWorker &worker=http_clients_.back();
                worker.done=done;
                worker.thread=std::thread([this,client,token,id,index,size,done]{
                    try { HandleHttpClient(client,token,id,index,size); }
                    catch(...) { closesocket(client); }
                    done->store(true);
                });
            } catch(...) {
                if(!http_clients_.empty()&&!http_clients_.back().thread.joinable())http_clients_.pop_back();
                closesocket(client);
            }
        }
        std::vector<HttpClientWorker> clients;
        {std::lock_guard<std::mutex> g(http_clients_mutex_);clients.swap(http_clients_);}
        for(auto &worker:clients)if(worker.thread.joinable())worker.thread.join();
    }
    void HandleHttpClient(SOCKET client,const std::wstring &token,uint64_t id,int index,int64_t size) {
        DWORD timeout=5000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout));
        timeout=15000;setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,(const char*)&timeout,sizeof(timeout));
        std::string request;char buf[1024];bool complete=false;
        while(request.size()<8192&&!http_stop_.load()) {
            int n=recv(client,buf,sizeof(buf),0);if(n<=0)break;
            request.append(buf,static_cast<size_t>(n));
            if(request.find("\r\n\r\n")!=std::string::npos){complete=true;break;}
        }
        auto respond=[&](int code,const char *reason,const char *extra="") {
            char h[1024];int n=snprintf(h,sizeof(h),"HTTP/1.1 %d %s\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n%sContent-Length: 0\r\n\r\n",code,reason,extra);
            if(n>0)SendAll(client,h,static_cast<size_t>(n));
        };
        if(!complete){closesocket(client);return;}
        std::istringstream lines(request);
        std::string method,path,version;lines>>method>>path>>version;
        if((method!="GET"&&method!="HEAD")||version.rfind("HTTP/1.",0)!=0){respond(405,"Method Not Allowed","Allow: GET, HEAD\r\n");closesocket(client);return;}
        const std::string expected="/stream/"+ToUtf8(token);
        if(path!=expected){respond(404,"Not Found");closesocket(client);return;}
        std::string rangeHeader;
        std::string line;std::getline(lines,line);
        while(std::getline(lines,line)){
            if(line=="\r"||line.empty())break;
            auto colon=line.find(':');if(colon==std::string::npos)continue;
            if(Lower(line.substr(0,colon))=="range"){
                rangeHeader=line.substr(colon+1);
                auto first=rangeHeader.find_first_not_of(" \t");if(first!=std::string::npos)rangeHeader=rangeHeader.substr(first);
                while(!rangeHeader.empty()&&(rangeHeader.back()=='\r'||rangeHeader.back()==' '||rangeHeader.back()=='\t'))rangeHeader.pop_back();
            }
        }
        uint64_t start=0,end=static_cast<uint64_t>(size-1);
        bool partial=false;
        if(!rangeHeader.empty()){
            if(rangeHeader.rfind("bytes=",0)!=0||rangeHeader.find(',')!=std::string::npos){
                respond(416,"Range Not Satisfiable",("Content-Range: bytes */"+std::to_string(size)+"\r\n").c_str());closesocket(client);return;
            }
            std::string spec=rangeHeader.substr(6);auto dash=spec.find('-');
            if(dash==std::string::npos){respond(416,"Range Not Satisfiable");closesocket(client);return;}
            std::string left=spec.substr(0,dash),right=spec.substr(dash+1);uint64_t a=0,b=0;
            if(left.empty()){
                uint64_t suffix=0;if(!ParseUnsigned(right,suffix)||suffix==0){respond(416,"Range Not Satisfiable");closesocket(client);return;}
                start=suffix>=static_cast<uint64_t>(size)?0:static_cast<uint64_t>(size)-suffix;
            } else {
                if(!ParseUnsigned(left,a)||a>=static_cast<uint64_t>(size)){respond(416,"Range Not Satisfiable");closesocket(client);return;}
                start=a;
                if(!right.empty()){
                    if(!ParseUnsigned(right,b)||b<a){respond(416,"Range Not Satisfiable");closesocket(client);return;}
                    end=std::min<uint64_t>(b,static_cast<uint64_t>(size-1));
                }
            }
            partial=true;
        }
        const uint64_t length=end-start+1;
        std::string mime=MimeType([&](){std::shared_ptr<Task> t;{std::lock_guard<std::mutex> g(m_);auto it=tasks_.find(id);if(it!=tasks_.end())t=it->second;}if(!t)return std::wstring();auto ti=t->handle.torrent_file();if(!ti||index>=ti->layout().num_files())return std::wstring();return FromUtf8(ti->layout().file_path(lt::file_index_t(index)));}());
        std::string header="HTTP/1.1 "+std::string(partial?"206 Partial Content":"200 OK")+"\r\n";
        header+="Connection: close\r\nAccept-Ranges: bytes\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
        header+="Content-Type: "+mime+"\r\nContent-Length: "+std::to_string(length)+"\r\n";
        if(partial)header+="Content-Range: bytes "+std::to_string(start)+"-"+std::to_string(end)+"/"+std::to_string(size)+"\r\n";
        header+="\r\n";
        if(!SendAll(client,header.data(),header.size())){closesocket(client);return;}
        if(method=="HEAD"){closesocket(client);return;}
        uint64_t cursor=start,remaining=length;
        std::vector<char> data(256*1024);
        while(remaining&&!http_stop_.load()){
            size_t chunk=static_cast<size_t>(std::min<uint64_t>(remaining,data.size()));
            SetReadWindow(id,index,static_cast<int64_t>(cursor),static_cast<int64_t>(chunk),8*1024*1024);
            if(!ReadVerified(id,index,static_cast<int64_t>(cursor),data.data(),chunk,30000,&http_stop_))break;
            if(!SendAll(client,data.data(),chunk))break;
            cursor+=chunk;remaining-=chunk;
        }
        closesocket(client);
    }
    bool Add(lt::add_torrent_params p,uint64_t *id,std::wstring &err) {
        std::lock_guard<std::mutex> g(m_);
        if(!session_){err=L"Torrent engine is not initialized.";return false;}
        if(p.save_path.empty())p.save_path=(downloads_/HashFolder(p.info_hashes)).u8string();
        fs::path taskCachePath=fs::absolute(fs::u8path(p.save_path)).lexically_normal();
        std::error_code dirError;fs::create_directories(taskCachePath,dirError);
        if(dirError){err=L"Could not create the torrent's cache folder.";return false;}
        if(p.ti) p.file_priorities.assign((size_t)p.ti->layout().num_files(),lt::dont_download);
        lt::error_code ec;
        p.flags &= ~lt::torrent_flags::auto_managed;
        p.flags &= ~lt::torrent_flags::paused;
        /* Public torrents participate in DHT/LSD by default. Explicitly clear
           per-torrent disable flags: imported .torrent metadata may carry them. */
        p.flags &= ~lt::torrent_flags::disable_dht;
        p.flags &= ~lt::torrent_flags::disable_lsd;
        auto h=session_->add_torrent(std::move(p),ec);
        if(ec||!h.is_valid()){err=FromUtf8(ec.message());return false;}
        /* Do not leave a newly-added magnet/.torrent waiting for a long
           automatic announce interval. Ask its supplied trackers immediately
           and join the DHT swarm for public torrents. */
        try { h.force_reannounce(); } catch(...) {}
        try {
            if(session_->is_dht_running()) h.force_dht_announce();
            else session_->start_dht();
        } catch(...) {}
        auto t=std::make_shared<Task>();t->id=++next_;t->handle=std::move(h);t->cache_path=std::move(taskCachePath);
        tasks_.emplace(t->id,t);*id=t->id;return true;
    }
    static std::wstring HashFolder(const lt::info_hash_t &hashes) {
        const std::string bytes=hashes.get_best().to_string();
        static const wchar_t hex[]=L"0123456789abcdef";
        std::wstring out;out.reserve(bytes.size()*2);
        for(unsigned char c:bytes){out.push_back(hex[c>>4]);out.push_back(hex[c&15]);}
        return out.empty()?L"unknown":out;
    }
    static uint64_t DirectoryBytes(const fs::path &root) {
        std::error_code ec;
        if(!fs::exists(root,ec)||ec)return 0;
        uint64_t total=0;
        fs::recursive_directory_iterator it(root,fs::directory_options::skip_permission_denied,ec),end;
        for(;it!=end;it.increment(ec)) {
            if(ec){ec.clear();continue;}
            auto st=it->symlink_status(ec);if(ec){ec.clear();continue;}
            if(fs::is_regular_file(st)) {
                const auto size=it->file_size(ec);
                if(!ec&&UINT64_MAX-total>=size)total+=size;
                ec.clear();
            } else if(fs::is_symlink(st))it.disable_recursion_pending();
        }
        return total;
    }
    std::vector<fs::path> EvictableCacheFolders() const {
        std::vector<std::pair<fs::file_time_type,fs::path>> ordered;
        std::error_code ec;
        if(!fs::exists(data_,ec)||ec)return {};
        for(fs::directory_iterator it(data_,fs::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)) {
            if(ec){ec.clear();continue;}
            auto st=it->symlink_status(ec);if(ec){ec.clear();continue;}
            if(!fs::is_directory(st)||fs::is_symlink(st))continue;
            fs::path candidate=fs::weakly_canonical(it->path(),ec);if(ec){ec.clear();continue;}
            bool active=false;
            for(const auto &entry:tasks_) {
                fs::path protectedPath=fs::weakly_canonical(entry.second->cache_path,ec);ec.clear();
                if(candidate==protectedPath){active=true;break;}
            }
            if(active)continue;
            auto modified=fs::last_write_time(candidate,ec);if(ec){ec.clear();continue;}
            ordered.emplace_back(modified,candidate);
        }
        std::sort(ordered.begin(),ordered.end(),[](const auto &a,const auto &b){return a.first<b.first;});
        std::vector<fs::path> result;for(auto &entry:ordered)result.push_back(entry.second);
        return result;
    }
    bool EnsureCacheCapacity(uint64_t additional,const fs::path &protectedPath) {
        if(additional>cache_limit_bytes_)return false;
        uint64_t used=DirectoryBytes(data_);
        if(additional<=cache_limit_bytes_&&used<=cache_limit_bytes_-additional)return true;
        auto candidates=EvictableCacheFolders();std::error_code ec;
        for(const auto &path:candidates) {
            fs::remove_all(path,ec);ec.clear();
            used=DirectoryBytes(data_);
            if(additional<=cache_limit_bytes_&&used<=cache_limit_bytes_-additional)return true;
        }
        (void)protectedPath;
        return additional<=cache_limit_bytes_&&used<=cache_limit_bytes_-additional;
    }
    void CleanupCacheToLimit() {
        uint64_t used=DirectoryBytes(data_);
        if(used<=cache_limit_bytes_)return;
        auto candidates=EvictableCacheFolders();std::error_code ec;
        for(const auto &path:candidates) {
            fs::remove_all(path,ec);ec.clear();
            used=DirectoryBytes(data_);if(used<=cache_limit_bytes_)break;
        }
    }
    void Monitor() {
        auto lastCacheScan=std::chrono::steady_clock::now();
        auto lastDhtRetry=std::chrono::steady_clock::now()-std::chrono::seconds(10);
        while(!stopping_) {
            {
                std::lock_guard<std::mutex> g(m_);
                /* Keep DHT startup observable and self-healing. A session can
                   be alive while its DHT subsystem remains stopped (for
                   example after bootstrap/DNS startup trouble). Retry the
                   service and bootstrap nodes without blocking torrent UI. */
                const auto dhtNow=std::chrono::steady_clock::now();
                if(session_ && !session_->is_dht_running() &&
                   dhtNow-lastDhtRetry>=std::chrono::seconds(10)) {
                    try {
                        session_->start_dht();
                        session_->add_dht_node({"router.bittorrent.com",6881});
                        session_->add_dht_node({"dht.transmissionbt.com",6881});
                        session_->add_dht_node({"router.utorrent.com",6881});
                    } catch(...) {}
                    lastDhtRetry=dhtNow;
                }
                for(auto &e:tasks_) {
                    Task &t=*e.second;
                    if(!t.handle.is_valid())continue;
                    try {
                        auto snapshot=t.handle.status();
                        if(snapshot.errc)t.error=FromUtf8(snapshot.errc.message());
                        if(t.metadata_applied)continue;
                        auto ti=t.handle.torrent_file();if(!ti)continue;
                        auto const &files=ti->layout();
                        std::vector<lt::download_priority_t> p((size_t)files.num_files(),lt::dont_download);
                        t.handle.prioritize_files(p);
                        t.file_count=files.num_files();t.name=FromUtf8(ti->name());t.metadata_applied=true;
                        /* Public torrents often ship with stale or WebRTC-only trackers.
                           Add a small maintained UDP fallback set for conventional peers.
                           Never inject public trackers into a private torrent. */
                        if(!t.fallback_trackers_added && !ti->priv()){
                            static const char *fallback[]={
                                "udp://open.demonii.com:1337/announce",
                                "udp://tracker.opentrackr.org:1337/announce",
                                "udp://open.stealth.si:80/announce",
                                "udp://tracker.torrent.eu.org:451/announce"
                            };
                            auto existing=t.handle.trackers();
                            for(const char *url:fallback){
                                bool found=false;
                                for(const auto &entry:existing)if(entry.url==url){found=true;break;}
                                if(!found){lt::announce_entry tracker(url);tracker.tier=0;t.handle.add_tracker(tracker);}
                            }
                            t.fallback_trackers_added=true;
                            try{t.handle.force_reannounce();}catch(...){}
                            try{if(session_&&session_->is_dht_running())t.handle.force_dht_announce();}catch(...){}
                        }
                    } catch(const std::exception &e) {
                        t.error=FromUtf8(e.what());
                    } catch(...) {
                        t.error=L"Unexpected torrent engine error.";
                    }
                }
            }
            const auto now=std::chrono::steady_clock::now();
            if(now-lastCacheScan>=std::chrono::seconds(10)) {
                {
                    std::lock_guard<std::mutex> g(m_);
                    std::error_code ec;
                    for(const auto &entry:tasks_) {
                        fs::last_write_time(entry.second->cache_path,fs::file_time_type::clock::now(),ec);
                        ec.clear();
                    }
                }
                cache_used_bytes_.store(DirectoryBytes(data_));
                lastCacheScan=now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    std::mutex m_;
    std::unique_ptr<lt::session> session_;
    std::map<uint64_t,std::shared_ptr<Task>> tasks_;
    fs::path data_;
    fs::path downloads_;
    uint64_t cache_limit_bytes_=20ULL*1024*1024*1024;
    std::atomic<uint64_t> cache_used_bytes_{0};
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    std::mutex schedule_mutex_;
    std::mutex http_lifecycle_mutex_,http_clients_mutex_;
    std::vector<HttpClientWorker> http_clients_;
    std::thread http_accept_thread_;
    std::atomic<bool> http_stop_{true};
    std::atomic<uint64_t> wait_cancel_generation_{0};
    SOCKET http_socket_=INVALID_SOCKET;
    std::atomic<uint64_t> http_task_id_{0};
    int http_file_index_=-1;
    int64_t http_file_size_=0;
    std::wstring http_token_;
    bool winsock_ready_=false;
    uint64_t next_=0;
};
Engine g_engine;
void ErrorOut(wchar_t *p,unsigned cap,const std::wstring &s){if(p&&cap)wcsncpy_s(p,cap,s.c_str(),_TRUNCATE);}
}
BOOL WINAPI ABM_Torrent_Initialize(const wchar_t *p){return g_engine.Init(p)?TRUE:FALSE;}
void WINAPI ABM_Torrent_Shutdown(void){try{g_engine.Stop();}catch(...){}}
BOOL WINAPI ABM_Torrent_AddMagnet(const wchar_t *u,uint64_t *id,wchar_t *e,unsigned c){std::wstring s;bool ok=g_engine.AddMagnet(u,id,s);ErrorOut(e,c,s);return ok?TRUE:FALSE;}
BOOL WINAPI ABM_Torrent_AddFile(const wchar_t *p,uint64_t *id,wchar_t *e,unsigned c){std::wstring s;bool ok=g_engine.AddFile(p,id,s);ErrorOut(e,c,s);return ok?TRUE:FALSE;}
BOOL WINAPI ABM_Torrent_GetTask(uint64_t id,ABM_TorrentTaskInfo *p){try{return g_engine.Get(id,p)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_GetFile(uint64_t id,int i,wchar_t *p,unsigned c,int64_t *s){try{return g_engine.GetFile(id,i,p,c,s)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_SelectFile(uint64_t id,int i){try{return g_engine.Select(id,i)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_Pause(uint64_t id){try{return g_engine.Pause(id,true)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_Resume(uint64_t id){try{return g_engine.Pause(id,false)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_Remove(uint64_t id,BOOL d){try{return g_engine.Remove(id,d!=FALSE)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_SetRateLimits(int d,int u){try{return g_engine.Limits(d,u)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_GetCacheInfo(uint64_t *used,uint64_t *limit){try{return g_engine.CacheInfo(used,limit)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_SetCacheLimit(uint64_t limit,uint64_t *used){try{return g_engine.SetCacheLimit(limit,used)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_ClearCache(uint64_t *freed,uint64_t *remaining){try{return g_engine.ClearCache(freed,remaining)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_SetReadWindow(uint64_t id,int i,int64_t o,int64_t n,int a){try{return g_engine.SetReadWindow(id,i,o,n,a)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_WaitForRange(uint64_t id,int i,int64_t o,int64_t n,unsigned t){try{return g_engine.WaitForRange(id,i,o,n,t)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_StartHttpStream(uint64_t id,int i,wchar_t *u,unsigned uc,wchar_t *e,unsigned ec){std::wstring err;bool ok=false;try{ok=g_engine.StartHttpStream(id,i,u,uc,err);}catch(...){err=L"Unexpected error while starting loopback stream.";}ErrorOut(e,ec,err);return ok?TRUE:FALSE;}
BOOL WINAPI ABM_Torrent_StartLanHttpStream(uint64_t id,int i,wchar_t *u,unsigned uc,wchar_t *e,unsigned ec){std::wstring err;bool ok=false;try{ok=g_engine.StartLanHttpStream(id,i,u,uc,err);}catch(...){err=L"Unexpected error while starting the private-LAN torrent stream.";}ErrorOut(e,ec,err);return ok?TRUE:FALSE;}
void WINAPI ABM_Torrent_StopHttpStream(void){try{g_engine.StopHttpStream();}catch(...){}}

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
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
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
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
    bool metadata_applied=false;
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
            data_=fs::absolute(directory).lexically_normal();
            fs::create_directories(data_);
            lt::settings_pack s;
            s.set_str(lt::settings_pack::user_agent,"ABM-AV-Player/1.0");
            s.set_str(lt::settings_pack::listen_interfaces,"0.0.0.0:0");
            s.set_bool(lt::settings_pack::enable_dht,true);
            s.set_bool(lt::settings_pack::enable_lsd,true);
            s.set_bool(lt::settings_pack::enable_upnp,true);
            s.set_bool(lt::settings_pack::enable_natpmp,true);
            WSADATA wsa{};if(WSAStartup(MAKEWORD(2,2),&wsa)!=0)return false;
            winsock_ready_=true;
            session_=std::make_unique<lt::session>(s);
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
            p.save_path=data_.u8string();
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
            auto ti=std::make_shared<lt::torrent_info>(p.u8string(),ec);
            if(ec||!ti||!ti->is_valid()){err=ec?FromUtf8(ec.message()):L"Invalid torrent metadata.";return false;}
            lt::add_torrent_params atp;
            atp.ti=std::move(ti);
            atp.save_path=data_.u8string();
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
        out->has_metadata=s.has_metadata?1:0; out->file_count=i->second->file_count;
        CopyText(out->name,_countof(out->name),i->second->name);
        CopyText(out->error,_countof(out->error),i->second->error);
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
        if(!ti||index<0||index>=ti->files().num_files())return false;
        CopyText(path,cap,FromUtf8(ti->files().file_path(index)));
        *size=ti->files().file_size(index);return true;
    }
    bool Select(uint64_t id,int index) {
        std::lock_guard<std::mutex> g(m_);
        auto i=tasks_.find(id);if(i==tasks_.end())return false;
        auto ti=i->second->handle.torrent_file();
        if(!ti||index<0||index>=ti->files().num_files())return false;
        std::vector<lt::download_priority_t> p((size_t)ti->files().num_files(),lt::dont_download);
        p[(size_t)index]=lt::default_priority;i->second->handle.prioritize_files(p);return true;
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
        if(!ti||index<0||index>=ti->files().num_files())return false;
        const int64_t size=ti->files().file_size(index);
        if(offset<0||length<=0||offset>=size)return false;
        if(ahead<0)ahead=0;if(ahead>64*1024*1024)ahead=64*1024*1024;
        const int pieceLen=ti->piece_length();if(pieceLen<=0)return false;
        const int64_t fileStart=ti->files().file_offset(index);
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
        if(!ti||index<0||index>=ti->files().num_files())return false;
        const int64_t size=ti->files().file_size(index);
        if(offset<0||length<0||offset>size||length>size-offset)return false;
        if(taskOut)*taskOut=task;if(infoOut)*infoOut=ti;
        if(length==0)return true;
        const int pieceLen=ti->piece_length();if(pieceLen<=0)return false;
        const int64_t start=ti->files().file_offset(index)+offset;
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
            if(!metadata||index<0||index>=metadata->files().num_files())return false;
            const int64_t size=metadata->files().file_size(index);
            if(offset<0||offset>size||static_cast<int64_t>(count)>size-offset)return false;
            if(!WaitForRange(id,index,offset,static_cast<int64_t>(count),timeout,cancel))return false;
            ti=std::move(metadata);
        }
        if(!task||task->removed.load()||!ti)return false;
        if(ti->files().symlink(index).size()!=0)return false;
        try {
            const std::string fullUtf8=ti->files().file_path(index,data_.u8string());
            fs::path target=fs::weakly_canonical(fs::u8path(fullUtf8));
            fs::path root=fs::weakly_canonical(data_);
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
        if(!url||cap==0){err=L"Output URL buffer is unavailable.";return false;}
        StopHttpStream();
        if(!Select(id,index)){err=L"Select a valid torrent file after metadata is available.";return false;}
        std::shared_ptr<Task> task;
        {std::lock_guard<std::mutex> g(m_);auto it=tasks_.find(id);if(it==tasks_.end()){err=L"Torrent task no longer exists.";return false;}task=it->second;}
        auto ti=task->handle.torrent_file();
        if(!ti||index<0||index>=ti->files().num_files()){err=L"Torrent file metadata is not ready.";return false;}
        if(ti->files().symlink(index).size()!=0){err=L"Symbolic-link entries cannot be streamed.";return false;}
        const int64_t size=ti->files().file_size(index);
        if(size<=0){err=L"Empty files cannot be streamed.";return false;}
        if(BCryptGenRandom(nullptr,token_bytes_,sizeof(token_bytes_),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0){
            err=L"Windows could not create a secure stream token.";return false;
        }
        static const wchar_t hex[]=L"0123456789abcdef";
        std::wstring token;token.reserve(64);
        for(unsigned char b:token_bytes_){token.push_back(hex[b>>4]);token.push_back(hex[b&15]);}
        SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(listener==INVALID_SOCKET){err=L"Could not create the loopback HTTP socket.";return false;}
        BOOL exclusive=TRUE;setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&exclusive,sizeof(exclusive));
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
        if(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR||listen(listener,8)==SOCKET_ERROR){
            closesocket(listener);err=L"Could not bind the loopback-only HTTP stream.";return false;
        }
        int addressLen=sizeof(address);
        if(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&addressLen)==SOCKET_ERROR){
            closesocket(listener);err=L"Could not determine the local stream port.";return false;
        }
        const unsigned short port=ntohs(address.sin_port);
        std::wstring streamUrl=L"http://127.0.0.1:"+std::to_wstring(port)+L"/stream/"+token;
        if(streamUrl.size()+1>cap){closesocket(listener);err=L"Output URL buffer is too small.";return false;}
        http_stop_.store(false);
        http_task_id_.store(id);http_file_index_=index;http_file_size_=size;
        http_token_=token;http_socket_=listener;
        url[0]=0;wcsncpy_s(url,cap,streamUrl.c_str(),_TRUNCATE);
        try {
            http_accept_thread_=std::thread([this,listener,token,id,index,size]{HttpAcceptLoop(listener,token,id,index,size);});
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
        http_task_id_.store(0);http_file_index_=-1;http_file_size_=0;http_token_.clear();
        SecureZeroMemory(token_bytes_,sizeof(token_bytes_));
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
    void HttpAcceptLoop(SOCKET listener,const std::wstring &token,uint64_t id,int index,int64_t size) {
        while(!http_stop_.load()) {
            fd_set readSet;FD_ZERO(&readSet);FD_SET(listener,&readSet);
            timeval tv{};tv.tv_sec=0;tv.tv_usec=250000;
            int ready=select(0,&readSet,nullptr,nullptr,&tv);
            if(ready==SOCKET_ERROR)break;
            if(ready==0)continue;
            sockaddr_in remote{};int remoteLen=sizeof(remote);
            SOCKET client=accept(listener,reinterpret_cast<sockaddr*>(&remote),&remoteLen);
            if(client==INVALID_SOCKET)continue;
            if(remote.sin_addr.s_addr!=htonl(INADDR_LOOPBACK)){closesocket(client);continue;}
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
        const std::string expected="/stream/"+std::string(token.begin(),token.end());
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
        std::string mime=MimeType([&](){std::shared_ptr<Task> t;{std::lock_guard<std::mutex> g(m_);auto it=tasks_.find(id);if(it!=tasks_.end())t=it->second;}if(!t)return std::wstring();auto ti=t->handle.torrent_file();if(!ti||index>=ti->files().num_files())return std::wstring();return FromUtf8(ti->files().file_path(index));}());
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
        if(p.ti) p.file_priorities.assign((size_t)p.ti->files().num_files(),lt::dont_download);
        lt::error_code ec;p.flags &= ~lt::torrent_flags::auto_managed;
        auto h=session_->add_torrent(std::move(p),ec);
        if(ec||!h.is_valid()){err=FromUtf8(ec.message());return false;}
        auto t=std::make_shared<Task>();t->id=++next_;t->handle=std::move(h);
        tasks_.emplace(t->id,t);*id=t->id;return true;
    }
    void Monitor() {
        while(!stopping_) {
            {
                std::lock_guard<std::mutex> g(m_);
                for(auto &e:tasks_) {
                    Task &t=*e.second;
                    if(!t.handle.is_valid())continue;
                    try {
                        auto snapshot=t.handle.status();
                        if(snapshot.errc)t.error=FromUtf8(snapshot.errc.message());
                        if(t.metadata_applied)continue;
                        auto ti=t.handle.torrent_file();if(!ti)continue;
                        auto const &files=ti->files();
                        std::vector<lt::download_priority_t> p((size_t)files.num_files(),lt::dont_download);
                        t.handle.prioritize_files(p);
                        t.file_count=files.num_files();t.name=FromUtf8(ti->name());t.metadata_applied=true;
                    } catch(const std::exception &e) {
                        t.error=FromUtf8(e.what());
                    } catch(...) {
                        t.error=L"Unexpected torrent engine error.";
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    std::mutex m_;
    std::unique_ptr<lt::session> session_;
    std::map<uint64_t,std::shared_ptr<Task>> tasks_;
    fs::path data_;
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
    unsigned char token_bytes_[32]{};
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
BOOL WINAPI ABM_Torrent_SetReadWindow(uint64_t id,int i,int64_t o,int64_t n,int a){try{return g_engine.SetReadWindow(id,i,o,n,a)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_WaitForRange(uint64_t id,int i,int64_t o,int64_t n,unsigned t){try{return g_engine.WaitForRange(id,i,o,n,t)?TRUE:FALSE;}catch(...){return FALSE;}}
BOOL WINAPI ABM_Torrent_StartHttpStream(uint64_t id,int i,wchar_t *u,unsigned uc,wchar_t *e,unsigned ec){std::wstring err;bool ok=false;try{ok=g_engine.StartHttpStream(id,i,u,uc,err);}catch(...){err=L"Unexpected error while starting loopback stream.";}ErrorOut(e,ec,err);return ok?TRUE:FALSE;}
void WINAPI ABM_Torrent_StopHttpStream(void){try{g_engine.StopHttpStream();}catch(...){}}

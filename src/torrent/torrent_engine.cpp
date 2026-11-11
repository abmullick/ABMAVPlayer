#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
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
            session_=std::make_unique<lt::session>(s);
            stopping_=false;
            worker_=std::thread([this]{Monitor();});
            return true;
        } catch(...) { session_.reset(); return false; }
    }
    void Stop() {
        stopping_=true;
        if(worker_.joinable()) worker_.join();
        std::lock_guard<std::mutex> g(m_);
        tasks_.clear();
        session_.reset();
    }
    bool AddMagnet(const wchar_t *uri,uint64_t *id,std::wstring &err) {
        if(!uri||!id) {err=L"Magnet URI is empty.";return false;}
        std::wstring w(uri);
        if(w.size()>8192||w.rfind(L"magnet:?",0)!=0) {err=L"Only a valid magnet URI is accepted.";return false;}
        try {
            lt::error_code ec;
            auto p=lt::parse_magnet_uri(ToUtf8(w),ec);
            if(ec){err=FromUtf8(ec.message());return false;}
            p.save_path=data_.string();
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
            auto ti=std::make_shared<lt::torrent_info>(p.string(),ec);
            if(ec||!ti||!ti->is_valid()){err=ec?FromUtf8(ec.message()):L"Invalid torrent metadata.";return false;}
            lt::add_torrent_params atp;
            atp.ti=std::move(ti);
            atp.save_path=data_.string();
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
        else if(s.paused)out->state=ABM_TORRENT_PAUSED;
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
        std::lock_guard<std::mutex> g(m_);auto i=tasks_.find(id);
        if(i==tasks_.end()||!session_)return false;
        lt::remove_flags_t flags{};
        if(delete_data)flags=lt::session::delete_files;
        session_->remove_torrent(i->second->handle,flags);tasks_.erase(i);return true;
    }
    bool Limits(int down,int up) {
        if(down<0||up<0)return false;
        std::lock_guard<std::mutex> g(m_);if(!session_)return false;
        lt::settings_pack s;s.set_int(lt::settings_pack::download_rate_limit,down);
        s.set_int(lt::settings_pack::upload_rate_limit,up);session_->apply_settings(s);return true;
    }
private:
    bool Add(lt::add_torrent_params p,uint64_t *id,std::wstring &err) {
        std::lock_guard<std::mutex> g(m_);
        if(!session_){err=L"Torrent engine is not initialized.";return false;}
        if(p.ti) p.file_priorities.assign((size_t)p.ti->files().num_files(),lt::dont_download);
        lt::error_code ec;auto h=session_->add_torrent(std::move(p),ec);
        if(ec||!h.is_valid()){err=FromUtf8(ec.message());return false;}
        auto t=std::make_shared<Task>();t->id=++next_;t->handle=std::move(h);
        tasks_.emplace(t->id,t);*id=t->id;return true;
    }
    void Monitor() {
        while(!stopping_) {
            {
                std::lock_guard<std::mutex> g(m_);
                for(auto &e:tasks_) {
                    Task &t=*e.second;if(t.metadata_applied||!t.handle.is_valid())continue;
                    auto ti=t.handle.torrent_file();if(!ti)continue;
                    auto const &files=ti->files();
                    std::vector<lt::download_priority_t> p((size_t)files.num_files(),lt::dont_download);
                    t.handle.prioritize_files(p);
                    t.file_count=files.num_files();t.name=FromUtf8(ti->name());t.metadata_applied=true;
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
    uint64_t next_=0;
};
Engine g_engine;
void ErrorOut(wchar_t *p,unsigned cap,const std::wstring &s){if(p&&cap)wcsncpy_s(p,cap,s.c_str(),_TRUNCATE);}
}
BOOL WINAPI ABM_Torrent_Initialize(const wchar_t *p){return g_engine.Init(p)?TRUE:FALSE;}
void WINAPI ABM_Torrent_Shutdown(void){g_engine.Stop();}
BOOL WINAPI ABM_Torrent_AddMagnet(const wchar_t *u,uint64_t *id,wchar_t *e,unsigned c){std::wstring s;bool ok=g_engine.AddMagnet(u,id,s);ErrorOut(e,c,s);return ok?TRUE:FALSE;}
BOOL WINAPI ABM_Torrent_AddFile(const wchar_t *p,uint64_t *id,wchar_t *e,unsigned c){std::wstring s;bool ok=g_engine.AddFile(p,id,s);ErrorOut(e,c,s);return ok?TRUE:FALSE;}
BOOL WINAPI ABM_Torrent_GetTask(uint64_t id,ABM_TorrentTaskInfo *p){return g_engine.Get(id,p)?TRUE:FALSE;}
BOOL WINAPI ABM_Torrent_GetFile(uint64_t id,int i,wchar_t *p,unsigned c,int64_t *s){return g_engine.GetFile(id,i,p,c,s)?TRUE:FALSE;}
BOOL WINAPI ABM_Torrent_SelectFile(uint64_t id,int i){return g_engine.Select(id,i)?TRUE:FALSE;}
BOOL WINAPI ABM_Torrent_Pause(uint64_t id){return g_engine.Pause(id,true)?TRUE:FALSE;}
BOOL WINAPI ABM_Torrent_Resume(uint64_t id){return g_engine.Pause(id,false)?TRUE:FALSE;}
BOOL WINAPI ABM_Torrent_Remove(uint64_t id,BOOL d){return g_engine.Remove(id,d!=FALSE)?TRUE:FALSE;}
BOOL WINAPI ABM_Torrent_SetRateLimits(int d,int u){return g_engine.Limits(d,u)?TRUE:FALSE;}

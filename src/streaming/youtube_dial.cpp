#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "youtube_dial.h"
#include <winhttp.h>
#include <strsafe.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <cctype>

namespace {
struct DialDevice { std::wstring name, appUrl; };
std::mutex g_lock;
std::vector<DialDevice> g_devices;
std::thread g_worker;
std::atomic<bool> g_stop{false};
std::wstring g_lastLaunchError;

std::wstring Trim(std::wstring s) {
    while(!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while(!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}
std::wstring XmlValue(const std::wstring &xml,const wchar_t *tag) {
    std::wstring a=L"<";a+=tag;a+=L">";
    std::wstring b=L"</";b+=tag;b+=L">";
    size_t p=xml.find(a);if(p==std::wstring::npos)return {};
    p+=a.size();size_t e=xml.find(b,p);if(e==std::wstring::npos)return {};
    return Trim(xml.substr(p,e-p));
}
bool IsPrivateIPv4(const std::wstring &host) {
    IN_ADDR a{};if(InetPtonW(AF_INET,host.c_str(),&a)!=1)return false;
    unsigned long n=ntohl(a.S_un.S_addr);
    return (n>>24)==10 || (n>>20)==0xAC1 || (n>>16)==0xC0A8;
}
bool ParseHttpUrl(const std::wstring &url,std::wstring &host,INTERNET_PORT &port,std::wstring &path) {
    URL_COMPONENTSW c{};c.dwStructSize=sizeof(c);c.dwSchemeLength=(DWORD)-1;c.dwHostNameLength=(DWORD)-1;c.dwUrlPathLength=(DWORD)-1;c.dwExtraInfoLength=(DWORD)-1;
    if(!WinHttpCrackUrl(url.c_str(),0,0,&c)||c.nScheme!=INTERNET_SCHEME_HTTP)return false;
    host.assign(c.lpszHostName,c.dwHostNameLength);port=c.nPort;
    path.assign(c.lpszUrlPath,c.dwUrlPathLength);if(c.dwExtraInfoLength)path.append(c.lpszExtraInfo,c.dwExtraInfoLength);
    if(path.empty())path=L"/";
    return IsPrivateIPv4(host);
}
bool HttpRequest(const std::wstring &url,const wchar_t *verb,const std::wstring &body,std::wstring *response,std::wstring *applicationUrl,DWORD *statusOut) {
    std::wstring host,path;INTERNET_PORT port=0;if(!ParseHttpUrl(url,host,port,path))return false;
    HINTERNET session=WinHttpOpen(L"ABM-AV-Player/1.0",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session)return false;
    WinHttpSetTimeouts(session,1500,1500,2000,2000);
    HINTERNET connect=WinHttpConnect(session,host.c_str(),port,0);
    HINTERNET request=connect?WinHttpOpenRequest(connect,verb,path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0):nullptr;
    bool ok=false;
    if(request) {
        const bool isPost=_wcsicmp(verb,L"POST")==0;
        const wchar_t *headers=isPost?L"Content-Type: text/plain; charset=\"utf-8\"\r\nOrigin: https://www.youtube.com\r\n":WINHTTP_NO_ADDITIONAL_HEADERS;
        std::string bodyUtf8;
        if(isPost&&!body.empty()){
            int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,body.data(),(int)body.size(),nullptr,0,nullptr,nullptr);
            if(bytes>0){bodyUtf8.resize(bytes);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,body.data(),(int)body.size(),bodyUtf8.data(),bytes,nullptr,nullptr);}
        }
        BOOL sent=WinHttpSendRequest(request,headers,isPost?(DWORD)-1:0,
            bodyUtf8.empty()?WINHTTP_NO_REQUEST_DATA:(LPVOID)bodyUtf8.data(),(DWORD)bodyUtf8.size(),(DWORD)bodyUtf8.size(),0);
        if(sent&&WinHttpReceiveResponse(request,nullptr)) {
            DWORD status=0,size=sizeof(status);WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX);
            if(statusOut)*statusOut=status;
            if(applicationUrl){wchar_t value[2048]{};DWORD bytes=sizeof(value);if(WinHttpQueryHeaders(request,WINHTTP_QUERY_CUSTOM,L"Application-URL",value,&bytes,WINHTTP_NO_HEADER_INDEX))*applicationUrl=value;}
            if(response){std::string data;DWORD available=0;while(WinHttpQueryDataAvailable(request,&available)&&available){std::string chunk(available,'\0');DWORD read=0;if(!WinHttpReadData(request,chunk.data(),available,&read)||!read)break;chunk.resize(read);data+=chunk;if(data.size()>65536)break;}int count=MultiByteToWideChar(CP_UTF8,0,data.data(),(int)data.size(),nullptr,0);if(count>0){response->resize(count);MultiByteToWideChar(CP_UTF8,0,data.data(),(int)data.size(),response->data(),count);}}
            ok=true;
        }
        WinHttpCloseHandle(request);
    }
    if(connect)WinHttpCloseHandle(connect);WinHttpCloseHandle(session);return ok;
}
void Discover() {
    WSADATA w{};if(WSAStartup(MAKEWORD(2,2),&w)!=0)return;
    SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s==INVALID_SOCKET){WSACleanup();return;}
    DWORD timeout=2500;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout));
    BOOL reuse=TRUE;setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(const char*)&reuse,sizeof(reuse));
    sockaddr_in dst{};dst.sin_family=AF_INET;dst.sin_port=htons(1900);InetPtonA(AF_INET,"239.255.255.250",&dst.sin_addr);
    const char *msg="M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: urn:dial-multiscreen-org:service:dial:1\r\n\r\n";
    sendto(s,msg,(int)strlen(msg),0,(sockaddr*)&dst,sizeof(dst));
    char buf[8192];
    while(!g_stop.load()) {
        int n=recvfrom(s,buf,sizeof(buf)-1,0,nullptr,nullptr);if(n<=0)break;buf[n]=0;
        std::string raw(buf,n),lower=raw;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return (char)tolower(c);});
        size_t p=lower.find("\r\nlocation:");if(p==std::string::npos)continue;p+=11;while(p<raw.size()&&(raw[p]==' '||raw[p]=='\t'))++p;
        size_t e=raw.find("\r\n",p);if(e==std::string::npos)continue;
        int chars=MultiByteToWideChar(CP_UTF8,0,raw.data()+p,(int)(e-p),nullptr,0);if(chars<=0)continue;
        std::wstring location(chars,L'\0');MultiByteToWideChar(CP_UTF8,0,raw.data()+p,(int)(e-p),location.data(),chars);
        std::wstring xml,app;DWORD status=0;
        if(!HttpRequest(location,L"GET",L"",&xml,&app,&status)||status!=200||app.empty())continue;
        std::wstring name=XmlValue(xml,L"friendlyName");if(name.empty())name=L"YouTube TV";
        if(app.back()!=L'/')app+=L'/';
        std::lock_guard<std::mutex> lock(g_lock);
        auto it=std::find_if(g_devices.begin(),g_devices.end(),[&](const DialDevice &d){return _wcsicmp(d.name.c_str(),name.c_str())==0;});
        if(it==g_devices.end())g_devices.push_back({name,app});
    }
    g_stop=true;
    closesocket(s);WSACleanup();
}
}
extern "C" void ABM_Dial_StartDiscovery(void) {
    if(g_worker.joinable()&&!g_stop.load())return;
    ABM_Dial_StopDiscovery();{std::lock_guard<std::mutex> l(g_lock);g_devices.clear();}
    g_stop=false;g_worker=std::thread(Discover);
}
extern "C" void ABM_Dial_StopDiscovery(void) {
    g_stop=true;if(g_worker.joinable())g_worker.join();
}
extern "C" unsigned int ABM_Dial_DeviceCount(void) {
    std::lock_guard<std::mutex> l(g_lock);return (unsigned int)g_devices.size();
}
extern "C" BOOL ABM_Dial_DeviceName(unsigned int i,wchar_t *b,unsigned int c) {
    if(!b||!c)return FALSE;b[0]=0;std::lock_guard<std::mutex> l(g_lock);
    if(i>=g_devices.size())return FALSE;return wcsncpy_s(b,c,g_devices[i].name.c_str(),_TRUNCATE)==0;
}

namespace {
std::wstring ToWide(const std::string &v) { if(v.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,v.c_str(),-1,nullptr,0);if(n<=1)return {};std::wstring w((size_t)n,L'\0');MultiByteToWideChar(CP_UTF8,0,v.c_str(),-1,w.data(),n);w.resize((size_t)n-1);return w; }
std::string ToUtf8(const std::wstring &v) {
    if(v.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v.data(),(int)v.size(),nullptr,0,nullptr,nullptr);
    if(n<=0)return {};
    std::string out((size_t)n,'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v.data(),(int)v.size(),out.data(),n,nullptr,nullptr)<=0)return {};
    return out;
}
std::string FormEncode(const std::string &v) {
    static const char h[]="0123456789ABCDEF";std::string o;
    for(unsigned char c:v) {
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')o.push_back((char)c);
        else if(c==' ')o.push_back('+');
        else{o.push_back('%');o.push_back(h[c>>4]);o.push_back(h[c&15]);}
    } return o;
}
bool LoungePost(const std::wstring &path,const std::string &body,std::string &response,DWORD *statusOut=nullptr) {
    HINTERNET s=WinHttpOpen(L"ABM-AV-Player/1.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!s)return false;WinHttpSetTimeouts(s,5000,5000,7000,7000);
    HINTERNET c=WinHttpConnect(s,L"www.youtube.com",INTERNET_DEFAULT_HTTPS_PORT,0);
    HINTERNET r=c?WinHttpOpenRequest(c,L"POST",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
    bool ok=false;
    if(r) {
        const wchar_t *headers=L"Content-Type: application/x-www-form-urlencoded\r\nOrigin: https://www.youtube.com\r\n";
        BOOL sent=WinHttpSendRequest(r,headers,(DWORD)-1,body.empty()?WINHTTP_NO_REQUEST_DATA:(LPVOID)body.data(),(DWORD)body.size(),(DWORD)body.size(),0);
        if(sent&&WinHttpReceiveResponse(r,nullptr)) {
            DWORD status=0,z=sizeof(status);WinHttpQueryHeaders(r,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&z,WINHTTP_NO_HEADER_INDEX);
            if(statusOut)*statusOut=status;
            std::string data;DWORD available=0;
            while(WinHttpQueryDataAvailable(r,&available)&&available) {
                if(data.size()+available>1024*1024)break;
                std::string chunk(available,'\0');DWORD got=0;
                if(!WinHttpReadData(r,chunk.data(),available,&got)||!got)break;
                chunk.resize(got);data+=chunk;
            }
            response=std::move(data);ok=status>=200&&status<300;
        }
        WinHttpCloseHandle(r);
    }
    if(c)WinHttpCloseHandle(c);WinHttpCloseHandle(s);return ok;
}
std::string JsonString(const std::string &j,const char *key) {
    std::string m="\"";m+=key;m+="\"";size_t p=j.find(m);if(p==std::string::npos)return {};
    p=j.find(':',p+m.size());if(p==std::string::npos)return {};
    p=j.find('"',p+1);if(p==std::string::npos)return {};
    size_t e=j.find('"',p+1);return e==std::string::npos?std::string():j.substr(p+1,e-p-1);
}
std::string FrameString(const std::string &j,const char *prefix) {
    size_t p=j.find(prefix);if(p==std::string::npos)return {};
    p+=strlen(prefix);size_t e=j.find('"',p);return e==std::string::npos?std::string():j.substr(p,e-p);
}
bool GetScreenId(const std::wstring &base,std::string &id) {
    for(int n=0;n<6;n++) {
        std::wstring state;DWORD status=0;
        if(HttpRequest(base+L"YouTube",L"GET",L"",&state,nullptr,&status)&&status==200) {
            size_t p=state.find(L"<screenId>");
            if(p!=std::wstring::npos){p+=10;size_t e=state.find(L"</screenId>",p);if(e!=std::wstring::npos)id=ToUtf8(state.substr(p,e-p));}
            if(id.empty()) {
                p=state.find(L"screenId");
                if(p!=std::wstring::npos) {
                    size_t q=state.find_first_of(L"\":>",p+8);
                    size_t a=q==std::wstring::npos?q:state.find_first_not_of(L" \t\r\n\"'>",q+1);
                    size_t e=a==std::wstring::npos?a:state.find_first_of(L"\"'<& \t\r\n",a);
                    if(a!=std::wstring::npos&&e!=std::wstring::npos)id=ToUtf8(state.substr(a,e-a));
                }
            }
            if(!id.empty())return true;
        }
        Sleep(500);
    } return false;
}
bool PlayLounge(const std::string &screen,const std::string &video,std::string &error) {
    std::string response;DWORD status=0;
    if(!LoungePost(L"/api/lounge/pairing/get_lounge_token_batch","screen_ids="+FormEncode(screen),response,&status)) {
        error="YouTube token service failed (HTTP "+std::to_string(status)+").";return false;
    }
    std::string token=JsonString(response,"loungeToken");
    if(token.empty()){error="YouTube did not provide a remote-control token for this TV.";return false;}
    std::wstring path=L"/api/lounge/bc/bind?CVER=1&RID=1&VER=8&app=youtube-desktop&device=REMOTE_CONTROL&id=remote&loungeIdToken=";
    for(char ch:FormEncode(token))path+=(wchar_t)ch;
    path+=L"&name=ABM%20AV%20Player";
    if(!LoungePost(path,"",response,&status)){error="YouTube remote session failed (HTTP "+std::to_string(status)+").";return false;}
    std::string sid=FrameString(response,"[\"c\",\"");
    std::string gs=FrameString(response,"[\"S\",\"");
    std::string aid; size_t sidPos=response.find("[\"c\",\""); if(sidPos!=std::string::npos){size_t first=response.find('"',sidPos+6);if(first!=std::string::npos){size_t second=response.find('"',first+1);if(second!=std::string::npos){size_t third=response.find('"',second+1);if(third!=std::string::npos){size_t fourth=response.find('"',third+1);aid=response.substr(second+1,third-second-1);}}}}
    if(sid.empty()||gs.empty()){error="YouTube did not return a valid remote-control session.";return false;}
    std::string body="count=1&ofs=1&req0__sc=setPlaylist&req0_currentIndex=0&req0_currentTime=0&req0_videoId="+FormEncode(video)+"&req0_videoIds="+FormEncode(video);
    std::wstring cmd=L"/api/lounge/bc/bind?CVER=1&RID=2&VER=8&SID=";
    for(char ch:FormEncode(sid))cmd+=(wchar_t)ch;cmd+=L"&gsessionid=";
    for(char ch:FormEncode(gs))cmd+=(wchar_t)ch;cmd+=L"&loungeIdToken=";
    for(char ch:FormEncode(token))cmd+=(wchar_t)ch;
    if(!aid.empty()){cmd+=L"&AID=";for(char ch:FormEncode(aid))cmd+=(wchar_t)ch;}
    cmd+=L"&app=youtube-desktop&device=REMOTE_CONTROL&name=ABM%20AV%20Player&v=2";
    if(!LoungePost(cmd,body,response,&status)){error="YouTube rejected the video command (HTTP "+std::to_string(status)+").";return false;}
    if(response.find("error")!=std::string::npos||response.find("ERROR")!=std::string::npos){error="YouTube reported an error while accepting the video.";return false;}
    return true;
}
}
extern "C" BOOL ABM_Dial_LaunchPrimeVideo(unsigned int i) {
    std::wstring base;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if(i>=g_devices.size()) {
            g_lastLaunchError=L"No DIAL receiver is selected for Prime Video.";
            return FALSE;
        }
        base=g_devices[i].appUrl;
    }

    // DIAL only launches an installed TV application. It does not transfer
    // Prime Video account credentials, protected media, or playback sessions.
    const std::wstring appUrl=base+L"PrimeVideo";
    DWORD status=0;
    std::wstring response;
    if(!HttpRequest(appUrl,L"GET",L"",&response,nullptr,&status) || status!=200) {
        g_lastLaunchError=L"This TV does not advertise the PrimeVideo DIAL application. Use the TV's Prime Video app or Amazon's official Android/iOS Cast button.";
        return FALSE;
    }

    if(!HttpRequest(appUrl,L"POST",L"",&response,nullptr,&status) ||
       !(status==200||status==201||status==202)) {
        g_lastLaunchError=L"The TV found PrimeVideo through DIAL but rejected the app launch (HTTP "+std::to_wstring(status)+L").";
        return FALSE;
    }

    g_lastLaunchError.clear();
    return TRUE;
}

extern "C" BOOL ABM_Dial_LaunchYouTube(unsigned int i,const wchar_t *videoId) {
    if(!videoId||wcslen(videoId)!=11)return FALSE;
    for(const wchar_t *p=videoId;*p;++p)if(!((*p>=L'a'&&*p<=L'z')||(*p>=L'A'&&*p<=L'Z')||(*p>=L'0'&&*p<=L'9')||*p==L'_'||*p==L'-'))return FALSE;
    std::wstring base;{std::lock_guard<std::mutex> l(g_lock);if(i>=g_devices.size())return FALSE;base=g_devices[i].appUrl;}
    DWORD status=0;std::wstring ignored;
    std::wstring activeBase=base;
    bool launched=HttpRequest(activeBase+L"YouTube",L"POST",L"",&ignored,nullptr,&status);
    if(!launched||!(status==200||status==201||status==202)) {
        /* VIDAA firmware commonly exposes its DIAL REST service separately
           from the UPnP description endpoint, on TCP 56789 under /apps/. */
        std::wstring host,path;INTERNET_PORT port=0;
        if(ParseHttpUrl(base,host,port,path)) {
            std::wstring vidaaBase=L"http://"+host+L":56789/apps/";
            if(_wcsicmp(vidaaBase.c_str(),activeBase.c_str())!=0) {
                DWORD fallbackStatus=0;
                if(HttpRequest(vidaaBase+L"YouTube",L"POST",L"",&ignored,nullptr,&fallbackStatus) &&
                   (fallbackStatus==200||fallbackStatus==201||fallbackStatus==202)) {
                    activeBase=vidaaBase;status=fallbackStatus;launched=true;
                } else status=fallbackStatus;
            }
        }
    }
    if(!launched||!(status==200||status==201||status==202)) {
        g_lastLaunchError=L"The TV rejected the YouTube DIAL launch (HTTP "+std::to_wstring(status)+L"). Its DIAL service may not expose YouTube.";
        return FALSE;
    }
    std::string screen,error;
    if(!GetScreenId(activeBase,screen)){g_lastLaunchError=L"YouTube started, but the TV did not expose a YouTube screen ID.";return FALSE;}
    if(!PlayLounge(screen,ToUtf8(videoId),error)){g_lastLaunchError=ToWide(error);return FALSE;}
    g_lastLaunchError.clear();return TRUE;
}


extern "C" BOOL ABM_Dial_PlayLoungeScreenId(const char *screenId,const wchar_t *videoId) {
    if(!screenId||!*screenId||!videoId||wcslen(videoId)!=11)return FALSE;
    for(const wchar_t *p=videoId;*p;++p)
        if(!((*p>=L'a'&&*p<=L'z')||(*p>=L'A'&&*p<=L'Z')||(*p>=L'0'&&*p<=L'9')||*p==L'_'||*p==L'-'))return FALSE;
    std::string error;
    if(!PlayLounge(screenId,ToUtf8(videoId),error)){g_lastLaunchError=ToWide(error);return FALSE;}
    g_lastLaunchError.clear();
    return TRUE;
}

extern "C" BOOL ABM_Dial_LastLaunchError(wchar_t *buffer,unsigned int capacity) { if(!buffer||!capacity)return FALSE;buffer[0]=0;return SUCCEEDED(StringCchCopyW(buffer,capacity,g_lastLaunchError.c_str()))?TRUE:FALSE; }

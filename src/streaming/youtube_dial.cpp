#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "youtube_dial.h"
#include <winhttp.h>
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
std::atomic<DWORD> g_lastLaunchHttpStatus{0};

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
        const wchar_t *headers=isPost?L"Content-Type: text/plain; charset=\"utf-8\"\r\n":WINHTTP_NO_ADDITIONAL_HEADERS;
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
extern "C" unsigned int ABM_Dial_LastLaunchHttpStatus(void) { return (unsigned int)g_lastLaunchHttpStatus.load(); }
extern "C" BOOL ABM_Dial_LaunchYouTube(unsigned int i,const wchar_t *videoId) {
    g_lastLaunchHttpStatus=0;
    if(!videoId||wcslen(videoId)!=11)return FALSE;
    for(const wchar_t *p=videoId;*p;++p)if(!((*p>=L'a'&&*p<=L'z')||(*p>=L'A'&&*p<=L'Z')||(*p>=L'0'&&*p<=L'9')||*p==L'_'||*p==L'-'))return FALSE;
    std::wstring base;{std::lock_guard<std::mutex> l(g_lock);if(i>=g_devices.size())return FALSE;base=g_devices[i].appUrl;}
    DWORD status=0;std::wstring instance;
    bool ok=HttpRequest(base+L"YouTube",L"POST",std::wstring(L"v=")+videoId,nullptr,nullptr,&status);
    if(ok)g_lastLaunchHttpStatus=status;
    return ok&&(status==200||status==201||status==202)?TRUE:FALSE;
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#include <urlmon.h>
#include <shellapi.h>
#include <strsafe.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include "updater.h"

#pragma comment(lib,"wininet.lib")
#pragma comment(lib,"urlmon.lib")
#pragma comment(lib,"shell32.lib")

namespace {
const wchar_t *kReleaseApi=L"https://api.github.com/repos/abmullick/ABMAVPlayer/releases/latest";
const wchar_t *kInstallerName=L"ABM-AV-Player-Setup.exe";

bool ReadUrl(const wchar_t *url,char **out,DWORD *length) {
    *out=nullptr;*length=0;
    HINTERNET session=InternetOpenW(L"ABM-AV-Player-Updater/1.0",INTERNET_OPEN_TYPE_PRECONFIG,nullptr,nullptr,0);
    if(!session)return false;
    HINTERNET request=InternetOpenUrlW(session,url,nullptr,0,INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_SECURE|INTERNET_FLAG_NO_COOKIES,0);
    if(!request){InternetCloseHandle(session);return false;}
    DWORD capacity=16384,used=0;char *buffer=(char*)malloc(capacity);
    if(!buffer){InternetCloseHandle(request);InternetCloseHandle(session);return false;}
    char chunk[8192];DWORD got=0;bool ok=true;
    while(InternetReadFile(request,chunk,sizeof(chunk),&got)&&got){
        if(used+got+1>2*1024*1024){ok=false;break;}
        if(used+got+1>capacity){DWORD next=capacity*2;while(next<used+got+1)next*=2;char *p=(char*)realloc(buffer,next);if(!p){ok=false;break;}buffer=p;capacity=next;}
        memcpy(buffer+used,chunk,got);used+=got;
    }
    InternetCloseHandle(request);InternetCloseHandle(session);
    if(!ok){free(buffer);return false;}
    buffer[used]=0;*out=buffer;*length=used;return true;
}
bool JsonString(const char *json,const char *key,wchar_t *out,size_t count) {
    char token[96];if(sprintf_s(token,"\"%s\"",key)<0)return false;
    const char *p=strstr(json,token);if(!p)return false;
    p=strchr(p+strlen(token),':');if(!p)return false;
    while(*++p&&(*p==' '||*p=='\r'||*p=='\n'||*p=='\t')){}
    if(*p!='"')return false;++p;size_t n=0;
    while(*p&&*p!='"'&&n+1<count){
        unsigned char c=(unsigned char)*p++;
        if(c=='\\'&&*p){char e=*p++;if(e=='n')c=' ';else if(e=='r'||e=='t')c=' ';else c=(unsigned char)e;}
        out[n++]=(wchar_t)c;
    }
    out[n]=0;return *p=='"';
}
bool VersionNewer(const wchar_t *remote,const wchar_t *local) {
    const wchar_t *a=remote,*b=local;
    if(*a==L'v'||*a==L'V')++a;if(*b==L'v'||*b==L'V')++b;
    for(int i=0;i<4;i++){
        wchar_t *ae=nullptr,*be=nullptr;
        unsigned long av=wcstoul(a,&ae,10),bv=wcstoul(b,&be,10);
        if(av!=bv)return av>bv;
        if(ae==a||be==b)return false;
        a=(*ae==L'.')?ae+1:ae;b=(*be==L'.')?be+1:be;
        if(!*a&&!*b)return false;
    }
    return false;
}
void ExplainFailure(HWND owner,const wchar_t *message) {
    MessageBoxW(owner,message,L"ABM AV Player — Updates",MB_OK|MB_ICONWARNING);
}
}

bool ABM_CheckForUpdates(HWND owner,const wchar_t *currentVersion) {
    SetCursor(LoadCursorW(nullptr,IDC_WAIT));
    char *json=nullptr;DWORD bytes=0;
    bool fetched=ReadUrl(kReleaseApi,&json,&bytes);
    SetCursor(LoadCursorW(nullptr,IDC_ARROW));
    if(!fetched){ExplainFailure(owner,L"Could not contact GitHub Releases. Check your internet connection and try again.");return false;}
    wchar_t apiMessage[256]{};
    if(JsonString(json,"message",apiMessage,_countof(apiMessage)) && !_wcsicmp(apiMessage,L"Not Found")) {
        free(json);
        ExplainFailure(owner,L"No public release has been published yet. The update checker will work after the first Windows installer is published on GitHub Releases.");
        return false;
    }
    wchar_t tag[64]{},assetUrl[2048]{},notes[1024]{};
    bool gotTag=JsonString(json,"tag_name",tag,_countof(tag));
    JsonString(json,"body",notes,_countof(notes));
    const char *asset=strstr(json,"\"name\":");
    while(asset){
        wchar_t name[256]{};
        if(JsonString(asset,"name",name,_countof(name))&&_wcsicmp(name,kInstallerName)==0){
            JsonString(asset,"browser_download_url",assetUrl,_countof(assetUrl));break;
        }
        asset=strstr(asset+7,"\"name\":");
    }
    free(json);
    if(!gotTag||!tag[0]){ExplainFailure(owner,L"GitHub returned release information in an unexpected format.");return false;}
    if(!VersionNewer(tag,currentVersion)){
        wchar_t msg[256];StringCchPrintfW(msg,_countof(msg),L"You are using the latest version (%s).",currentVersion);
        MessageBoxW(owner,msg,L"ABM AV Player — Updates",MB_OK|MB_ICONINFORMATION);return true;
    }
    if(wcsncmp(assetUrl,L"https://github.com/",19)!=0&&wcsncmp(assetUrl,L"https://objects.githubusercontent.com/",38)!=0){
        ExplainFailure(owner,L"A newer release was found, but its official Windows installer asset is missing. Please visit the GitHub Releases page.");return false;
    }
    wchar_t prompt[1600];StringCchPrintfW(prompt,_countof(prompt),
        L"A newer version of ABM AV Player is available.\n\nInstalled: %s\nAvailable: %s\n\n%s\n\nDownload and install this update now?",
        currentVersion,tag,notes[0]?notes:L"Release notes were not provided.");
    if(MessageBoxW(owner,prompt,L"ABM AV Player — Update available",MB_YESNO|MB_ICONINFORMATION)!=IDYES)return true;

    wchar_t temp[MAX_PATH],installer[MAX_PATH];
    if(!GetTempPathW(_countof(temp),temp)||FAILED(StringCchPrintfW(installer,_countof(installer),L"%s%s",temp,L"ABM-AV-Player-Setup-latest.exe"))){
        ExplainFailure(owner,L"Could not create a temporary download path.");return false;
    }
    DeleteFileW(installer);
    SetCursor(LoadCursorW(nullptr,IDC_WAIT));
    HRESULT hr=URLDownloadToFileW(nullptr,assetUrl,installer,0,nullptr);
    SetCursor(LoadCursorW(nullptr,IDC_ARROW));
    if(FAILED(hr)){
        DeleteFileW(installer);ExplainFailure(owner,L"The installer could not be downloaded. Please try again later.");return false;
    }
    SHELLEXECUTEINFOW exec{};exec.cbSize=sizeof(exec);exec.fMask=SEE_MASK_NOCLOSEPROCESS;
    exec.hwnd=owner;exec.lpVerb=L"runas";exec.lpFile=installer;exec.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&exec)){
        DWORD error=GetLastError();DeleteFileW(installer);
        if(error!=ERROR_CANCELLED)ExplainFailure(owner,L"Windows could not start the installer. You can download it manually from GitHub Releases.");
        return false;
    }
    if(exec.hProcess)CloseHandle(exec.hProcess);
    PostMessageW(owner,WM_CLOSE,0,0);
    return true;
}

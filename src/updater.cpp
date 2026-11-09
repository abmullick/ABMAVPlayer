#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#include <urlmon.h>
#include <bcrypt.h>
#include <objbase.h>
#include <shellapi.h>
#include <strsafe.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include "updater.h"

#pragma comment(lib,"wininet.lib")
#pragma comment(lib,"urlmon.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"ole32.lib")

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
bool IsHexDigest(const wchar_t *value) {
    if(!value || wcslen(value)!=71 || wcsncmp(value,L"sha256:",7)!=0)return false;
    for(size_t i=7;i<71;i++)if(!((value[i]>=L'0'&&value[i]<=L'9')||(value[i]>=L'a'&&value[i]<=L'f')||(value[i]>=L'A'&&value[i]<=L'F')))return false;
    return true;
}
bool VerifySha256(const wchar_t *path,const wchar_t *expected) {
    if(!IsHexDigest(expected))return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    DWORD objectSize=0,hashSize=0,cb=0;PUCHAR object=nullptr;BYTE digest[32]{};
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    bool ok=false;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)goto done;
    if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,(PUCHAR)&objectSize,sizeof(objectSize),&cb,0)<0)goto done;
    if(BCryptGetProperty(algorithm,BCRYPT_HASH_LENGTH,(PUCHAR)&hashSize,sizeof(hashSize),&cb,0)<0||hashSize!=32)goto done;
    object=(PUCHAR)HeapAlloc(GetProcessHeap(),0,objectSize);if(!object)goto done;
    if(BCryptCreateHash(algorithm,&hash,object,objectSize,nullptr,0,0)<0)goto done;
    { BYTE buffer[65536];DWORD got=0;
      for(;;) {
          if(!ReadFile(file,buffer,sizeof(buffer),&got,nullptr))goto done;
          if(got==0)break;
          if(BCryptHashData(hash,buffer,got,0)<0)goto done;
      }
    }
    if(BCryptFinishHash(hash,digest,sizeof(digest),0)<0)goto done;
    for(size_t i=0;i<32;i++) {
        unsigned int byte=0;
        wchar_t pair[3]={expected[7+i*2],expected[8+i*2],0};
        if(swscanf_s(pair,L"%2x",&byte)!=1||digest[i]!=(BYTE)byte)goto done;
    }
    ok=true;
done:
    if(hash)BCryptDestroyHash(hash);
    if(object)HeapFree(GetProcessHeap(),0,object);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    CloseHandle(file);
    return ok;
}
bool CreatePrivateUpdatePath(wchar_t *directory,size_t directoryCount,wchar_t *installer,size_t installerCount) {
    wchar_t base[MAX_PATH]{};
    if(!GetEnvironmentVariableW(L"LOCALAPPDATA",base,_countof(base)))return false;
    GUID id{};if(CoCreateGuid(&id)!=S_OK)return false;
    wchar_t suffix[64]{};
    if(FAILED(StringCchPrintfW(suffix,_countof(suffix),L"\\ABMAVPlayer-Update-{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
       id.Data1,id.Data2,id.Data3,id.Data4[0],id.Data4[1],id.Data4[2],id.Data4[3],
       id.Data4[4],id.Data4[5],id.Data4[6],id.Data4[7])))return false;
    if(FAILED(StringCchPrintfW(directory,directoryCount,L"%s%s",base,suffix)))return false;
    if(!CreateDirectoryW(directory,nullptr))return false;
    return SUCCEEDED(StringCchPrintfW(installer,installerCount,L"%s\\%s",directory,kInstallerName));
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
    wchar_t tag[64]{},assetUrl[2048]{},notes[1024]{},assetDigest[96]{};
    bool gotTag=JsonString(json,"tag_name",tag,_countof(tag));
    JsonString(json,"body",notes,_countof(notes));
    const char *asset=strstr(json,"\"name\":");
    while(asset){
        wchar_t name[256]{};
        if(JsonString(asset,"name",name,_countof(name))&&_wcsicmp(name,kInstallerName)==0){
            JsonString(asset,"browser_download_url",assetUrl,_countof(assetUrl));
            JsonString(asset,"digest",assetDigest,_countof(assetDigest));break;
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

    if(!IsHexDigest(assetDigest)){
        ExplainFailure(owner,L"This release does not publish a SHA-256 asset digest. For your protection, automatic installation has been blocked. Please download the installer manually from the official GitHub Releases page.");return false;
    }
    wchar_t updateDirectory[MAX_PATH]{},installer[MAX_PATH]{};
    if(!CreatePrivateUpdatePath(updateDirectory,_countof(updateDirectory),installer,_countof(installer))){
        ExplainFailure(owner,L"Could not create a unique update directory under your Local AppData folder.");return false;
    }
    SetCursor(LoadCursorW(nullptr,IDC_WAIT));
    HRESULT hr=URLDownloadToFileW(nullptr,assetUrl,installer,0,nullptr);
    SetCursor(LoadCursorW(nullptr,IDC_ARROW));
    if(FAILED(hr)){
        DeleteFileW(installer);RemoveDirectoryW(updateDirectory);ExplainFailure(owner,L"The installer could not be downloaded. Please try again later.");return false;
    }
    if(!VerifySha256(installer,assetDigest)){
        DeleteFileW(installer);RemoveDirectoryW(updateDirectory);
        ExplainFailure(owner,L"The downloaded installer failed its SHA-256 integrity check. It has not been run.");return false;
    }
    /* GitHub's release-asset SHA-256 digest is verified above. No Authenticode
       certificate is required; HTTPS metadata and the digest protect download
       integrity, while Windows still shows the normal UAC elevation prompt. */
    SHELLEXECUTEINFOW exec{};exec.cbSize=sizeof(exec);exec.fMask=SEE_MASK_NOCLOSEPROCESS;
    exec.hwnd=owner;exec.lpVerb=L"runas";exec.lpFile=installer;exec.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&exec)){
        DWORD error=GetLastError();DeleteFileW(installer);RemoveDirectoryW(updateDirectory);
        if(error!=ERROR_CANCELLED)ExplainFailure(owner,L"Windows could not start the installer. You can download it manually from GitHub Releases.");
        return false;
    }
    if(exec.hProcess)CloseHandle(exec.hProcess);
    PostMessageW(owner,WM_CLOSE,0,0);
    return true;
}

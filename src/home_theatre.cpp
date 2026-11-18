#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <strsafe.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <new>
#include <cwchar>
#include <cstdlib>
#include <cstdio>
#include <dwmapi.h>
#include <uxtheme.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#include "home_theatre.h"
#include "metadata_engine.h"
#include "ui_d2d.h"
#include "ui_theme.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace {
enum { HT_EDIT=5101, HT_SEARCH=5102, HT_RESULTS=5103, HT_PLAY=5104, HT_SCAN=5105, HT_MATCH=5106, HT_ARTWORK=5107, HT_RECOMMEND=5108, HT_SEASONS=5109, HT_PICKER=5110, HT_WATCHED=5111 };
enum { HT_SEARCH_DONE=WM_APP+0x5A1, HT_DETAILS_DONE=WM_APP+0x5A2, HT_SCAN_DONE=WM_APP+0x5A3, HT_SEASON_DONE=WM_APP+0x5A4, HT_RECOMMEND_DONE=WM_APP+0x5A5 };
static const COLORREF BG=ABM_UI_COLOR_BACKGROUND, PANEL=ABM_UI_COLOR_SURFACE, PANEL2=ABM_UI_COLOR_INPUT;
static const COLORREF TEXT=ABM_UI_COLOR_TEXT, MUTED=ABM_UI_COLOR_TEXT_MUTED, ACCENT=ABM_UI_COLOR_ACCENT_BRIGHT;
static HWND g_window=nullptr, g_edit=nullptr, g_results=nullptr, g_play=nullptr, g_status=nullptr, g_localPicker=nullptr, g_scanButton=nullptr, g_matchButton=nullptr, g_artButton=nullptr, g_recommendButton=nullptr, g_seasonButton=nullptr, g_watchedButton=nullptr;
static ABM_D2D g_canvas={};
static BOOL g_canvasReady=FALSE;
static ABM_D2D_CANVAS g_controlCanvas={};
static BOOL g_controlCanvasReady=FALSE;
static HFONT g_controlFont=nullptr;
static HBRUSH g_darkBrush=nullptr;
static std::vector<std::wstring> g_mediaPaths;
static ABM_HomeTheatrePlayFn g_playCallback=nullptr;
static struct MovieItem {
    int id=0, type=1, localIndex=-1, seasonNumber=-1, episodeNumber=-1;
    wchar_t title[256]={}, date[32]={}, overview[1400]={}, poster[1024]={}, backdrop[1024]={};
} g_items[100];
static int g_itemCount=0, g_selected=-1, g_localMatch=-1;
static MovieItem g_recommendations[100],g_seasons[24];
static int g_recommendationCount=0,g_seasonCount=0,g_tvId=0,g_tvType=1;
static wchar_t g_overview[1800]={}, g_year[32]={}, g_runtime[64]={}, g_genres[256]={}, g_cast[512]={}, g_director[128]={}, g_posterFile[MAX_PATH]={}, g_backdropFile[MAX_PATH]={};
static BOOL g_showBackdrop=FALSE, g_scanMode=FALSE, g_recommendMode=FALSE, g_seasonMode=FALSE, g_episodeMode=FALSE;
static BOOL g_detailsReady=FALSE, g_busy=FALSE, g_selectedWatched=FALSE;
static wchar_t g_statusText[256]=L"Search movies and television using keyless metadata providers.";

static void CopyJsonString(const wchar_t *object,const wchar_t *end,const wchar_t *key,wchar_t *out,size_t cap) {
    if(!out||!cap)return;out[0]=0;
    wchar_t needle[96];if(FAILED(StringCchPrintfW(needle,_countof(needle),L"\"%s\"",key)))return;
    const wchar_t *p=object;
    while(p&&p<end) {
        p=wcsstr(p,needle);if(!p||p>=end)return;p+=wcslen(needle);
        while(p<end&&iswspace(*p))p++;if(p>=end||*p!=L':')continue;p++;
        while(p<end&&iswspace(*p))p++;if(p>=end||*p!=L'\"')return;p++;
        size_t n=0;
        while(p<end&&*p&&*p!=L'\"'&&n+1<cap) {
            wchar_t c=*p++;
            if(c==L'\\'&&p<end) {
                wchar_t e=*p++;
                if(e==L'n')c=L' ';else if(e==L'r'||e==L't')c=L' ';
                else if(e==L'u'&&end-p>=4) {
                    wchar_t hex[5]={p[0],p[1],p[2],p[3],0};wchar_t *tail=nullptr;
                    unsigned long v=wcstoul(hex,&tail,16);if(tail&&*tail==0)c=(wchar_t)v;p+=4;
                } else c=e;
            }
            out[n++]=c;
        }
        out[n]=0;return;
    }
}
static int JsonInt(const wchar_t *object,const wchar_t *end,const wchar_t *key) {
    wchar_t needle[96];if(FAILED(StringCchPrintfW(needle,_countof(needle),L"\"%s\"",key)))return 0;
    const wchar_t *p=wcsstr(object,needle);if(!p||p>=end)return 0;p+=wcslen(needle);
    while(p<end&&(*p==L' '||*p==L'\t'||*p==L'\r'||*p==L'\n'))p++;
    if(p>=end||*p++!=L':')return 0;while(p<end&&iswspace(*p))p++;
    return p<end?_wtoi(p):0;
}
static void ParseSearchJson(const wchar_t *json,int type,MovieItem *items,int *count) {
    const wchar_t *p=wcsstr(json,L"\"results\"");if(!p)return;
    p=wcschr(p,L'[');if(!p)return;p++;
    while(*p&&*count<100) {
        while(*p&&(iswspace(*p)||*p==L','))p++;if(*p==L']'||!*p)break;
        if(*p!=L'{'){p++;continue;}
        const wchar_t *start=p;BOOL quoted=FALSE,escaped=FALSE;int depth=0;
        for(;*p;p++) {
            if(quoted){if(escaped)escaped=FALSE;else if(*p==L'\\')escaped=TRUE;else if(*p==L'\"')quoted=FALSE;continue;}
            if(*p==L'\"'){quoted=TRUE;continue;}if(*p==L'{')depth++;else if(*p==L'}'&&--depth==0){p++;break;}
        }
        const wchar_t *finish=p;if(finish<=start)break;
        int id=JsonInt(start,finish,L"id");if(id<=0){p=finish;continue;}
        MovieItem item;item.id=id;item.type=type;
        if(type==0){CopyJsonString(start,finish,L"title",item.title,_countof(item.title));if(!item.title[0])CopyJsonString(start,finish,L"name",item.title,_countof(item.title));}
        else CopyJsonString(start,finish,type==1?L"title":L"name",item.title,_countof(item.title));
        CopyJsonString(start,finish,type==1?L"release_date":L"first_air_date",item.date,_countof(item.date));if(type==0&&!item.date[0]){CopyJsonString(start,finish,L"release_date",item.date,_countof(item.date));if(!item.date[0])CopyJsonString(start,finish,L"first_air_date",item.date,_countof(item.date));}
        CopyJsonString(start,finish,L"overview",item.overview,_countof(item.overview));
        CopyJsonString(start,finish,L"poster_path",item.poster,_countof(item.poster));
        CopyJsonString(start,finish,L"backdrop_path",item.backdrop,_countof(item.backdrop));
        if(type==0){wchar_t mediaType[16]={};CopyJsonString(start,finish,L"media_type",mediaType,_countof(mediaType));if(!_wcsicmp(mediaType,L"person")){p=finish;continue;}if(_wcsicmp(mediaType,L"movie")&&_wcsicmp(mediaType,L"tv")){p=finish;continue;}item.type=!_wcsicmp(mediaType,L"tv")?2:1;}
        if(item.title[0])items[(*count)++]=item;
    }
}
static std::wstring Normalized(const wchar_t *s) {
    std::wstring out;for(;s&&*s;s++)if(iswalnum(*s))out.push_back((wchar_t)towlower(*s));return out;
}
static BOOL IsVideoMediaPath(const wchar_t *path) {
    const wchar_t *dot=path?wcsrchr(path,L'.'):nullptr;if(!dot)return FALSE;
    static const wchar_t *extensions[]={L".mp4",L".mkv",L".avi",L".mov",L".wmv",L".m4v",L".webm",L".mpg",L".mpeg",L".ts",L".m2ts",L".flv",L".ogv",L".3gp",L".vob",L".mts"};
    for(const wchar_t *ext:extensions)if(!_wcsicmp(dot,ext))return TRUE;
    return FALSE;
}
static void MakeMediaSearchTitle(const wchar_t *path,wchar_t *out,size_t cap) {
    if(!out||!cap)return;out[0]=0;
    const wchar_t *leaf=path?wcsrchr(path,L'\\'):nullptr;leaf=leaf?leaf+1:(path?path:L"");
    StringCchCopyW(out,cap,leaf);
    wchar_t *dot=wcsrchr(out,L'.');if(dot)*dot=0;
    for(wchar_t *p=out;*p;p++)if(*p==L'_'||*p==L'.')*p=L' ';
    std::wstring lower=out;
    for(wchar_t &c:lower)c=(wchar_t)towlower(c);
    static const wchar_t *noise[]={L" 2160p",L" 1080p",L" 720p",L" 480p",L" bluray",L" blu-ray",L" web-dl",L" webdl",L" webrip",L" hdtv",L" x264",L" x265",L" h264",L" hevc",L" hdr",L" proper",L" remux"};
    size_t cut=std::wstring::npos;for(const wchar_t *token:noise){size_t at=lower.find(token);if(at!=std::wstring::npos&&(cut==std::wstring::npos||at<cut))cut=at;}
    if(cut!=std::wstring::npos)out[cut]=0;
    size_t len=wcslen(out);while(len&&iswspace(out[len-1]))out[--len]=0;
}
static int FindLocalMatch(const wchar_t *title) {
    std::wstring wanted=Normalized(title);if(wanted.empty())return -1;
    for(size_t i=0;i<g_mediaPaths.size();i++) {
        if(!IsVideoMediaPath(g_mediaPaths[i].c_str())||GetFileAttributesW(g_mediaPaths[i].c_str())==INVALID_FILE_ATTRIBUTES)continue;
        const wchar_t *leaf=wcsrchr(g_mediaPaths[i].c_str(),L'\\');leaf=leaf?leaf+1:g_mediaPaths[i].c_str();
        std::wstring file=leaf;size_t dot=file.find_last_of(L'.');if(dot!=std::wstring::npos)file.resize(dot);
        std::wstring normalized=Normalized(file.c_str());
        if(normalized==wanted || (wanted.size()>4&&normalized.find(wanted)!=std::wstring::npos))return (int)i;
    }
    return -1;
}
static BOOL ImageFolder(wchar_t *folder,size_t cap);
static int LoadManualMatch(int providerId,int type) {
    wchar_t folder[MAX_PATH]={};if(!ImageFolder(folder,_countof(folder)))return -1;
    wchar_t file[MAX_PATH]={};StringCchPrintfW(file,_countof(file),L"%s\\matches-v2.tsv",folder);
    FILE *fp=nullptr;if(_wfopen_s(&fp,file,L"rt, ccs=UTF-8")!=0||!fp)return -1;
    wchar_t line[MAX_PATH+64];int found=-1;
    while(fgetws(line,_countof(line),fp)) {
        line[wcscspn(line,L"\r\n")]=0;
        wchar_t *a=wcschr(line,L'\t');if(!a)continue;*a++=0;
        wchar_t *b=wcschr(a,L'\t');if(!b)continue;*b++=0;
        if(_wtoi(line)!=providerId||_wtoi(a)!=type)continue;
        for(size_t i=0;i<g_mediaPaths.size();i++)if(!_wcsicmp(g_mediaPaths[i].c_str(),b)){found=(int)i;break;}
    }
    fclose(fp);return found;
}
static void WriteCatalogField(FILE *fp,const wchar_t *value) {
    for(const wchar_t *p=value?value:L"";*p;p++) {
        wchar_t ch=*p;
        if(ch==L'\t'||ch==L'\r'||ch==L'\n')ch=L' ';
        fputwc(ch,fp);
    }
}
static void SaveLibraryCatalogEntry(int pathIndex) {
    if(pathIndex<0||pathIndex>=(int)g_mediaPaths.size()||g_selected<0||g_selected>=g_itemCount)return;
    wchar_t folder[MAX_PATH]={};if(!ImageFolder(folder,_countof(folder)))return;
    wchar_t file[MAX_PATH]={},temp[MAX_PATH]={};
    if(FAILED(StringCchPrintfW(file,_countof(file),L"%s\\metadata-catalog.tsv",folder))||
       FAILED(StringCchPrintfW(temp,_countof(temp),L"%s\\metadata-catalog.tmp",folder)))return;
    const std::wstring selectedPath=g_mediaPaths[(size_t)pathIndex];
    std::vector<std::wstring> retained;
    FILE *old=nullptr;
    if(_wfopen_s(&old,file,L"rt, ccs=UTF-8")==0&&old) {
        wchar_t line[8192];
        while(fgetws(line,_countof(line),old)) {
            line[wcscspn(line,L"\r\n")]=0;
            std::wstring fullLine=line;
            wchar_t *tab=wcschr(line,L'\t');
            if(tab)*tab=0;
            if(line[0]&&_wcsicmp(line,selectedPath.c_str())!=0)retained.emplace_back(fullLine);
        }
        fclose(old);
    }
    FILE *fp=nullptr;
    if(_wfopen_s(&fp,temp,L"wt, ccs=UTF-8")!=0||!fp)return;
    for(const std::wstring &row:retained)fwprintf(fp,L"%s\n",row.c_str());
    const MovieItem &item=g_items[g_selected];
    WriteCatalogField(fp,selectedPath.c_str());fputwc(L'\t',fp);
    WriteCatalogField(fp,item.title);fputwc(L'\t',fp);
    WriteCatalogField(fp,g_year);fputwc(L'\t',fp);
    WriteCatalogField(fp,g_genres);fputwc(L'\t',fp);
    WriteCatalogField(fp,g_overview);fputwc(L'\t',fp);
    WriteCatalogField(fp,g_cast);fputwc(L'\t',fp);
    WriteCatalogField(fp,g_director);fputwc(L'\t',fp);
    WriteCatalogField(fp,g_posterFile);fputwc(L'\t',fp);
    WriteCatalogField(fp,g_backdropFile);fputwc(L'\n',fp);
    BOOL writeOk=ferror(fp)==0;
    fclose(fp);
    if(!writeOk||!MoveFileExW(temp,file,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp);return;
    }
    if(g_window&&GetParent(g_window))PostMessageW(GetParent(g_window),WM_APP+0x5A6,0,0);
}
static void SaveManualMatch(int providerId,int type,int pathIndex) {
    if(providerId<=0||pathIndex<0||pathIndex>=(int)g_mediaPaths.size())return;
    wchar_t folder[MAX_PATH]={};if(!ImageFolder(folder,_countof(folder)))return;
    wchar_t file[MAX_PATH]={},temp[MAX_PATH]={};
    if(FAILED(StringCchPrintfW(file,_countof(file),L"%s\\matches-v2.tsv",folder))||
       FAILED(StringCchPrintfW(temp,_countof(temp),L"%s\\matches-v2.tmp",folder)))return;
    std::vector<std::wstring> retained;
    FILE *old=nullptr;
    if(_wfopen_s(&old,file,L"rt, ccs=UTF-8")==0&&old) {
        wchar_t line[MAX_PATH+128];
        while(fgetws(line,_countof(line),old)) {
            line[wcscspn(line,L"\r\n")]=0;
            wchar_t *a=wcschr(line,L'\t');if(!a){retained.emplace_back(line);continue;}*a++=0;
            wchar_t *b=wcschr(a,L'\t');if(!b){retained.emplace_back(line);continue;}*b++=0;
            if(_wtoi(line)==providerId&&_wtoi(a)==type)continue;
            wchar_t row[MAX_PATH+128]={};
            StringCchPrintfW(row,_countof(row),L"%d\t%d\t%s",_wtoi(line),_wtoi(a),b);
            retained.emplace_back(row);
        }
        fclose(old);
    }
    FILE *fp=nullptr;if(_wfopen_s(&fp,temp,L"wt, ccs=UTF-8")!=0||!fp)return;
    for(const std::wstring &row:retained)fwprintf(fp,L"%s\n",row.c_str());
    fwprintf(fp,L"%d\t%d\t%s\n",providerId,type,g_mediaPaths[(size_t)pathIndex].c_str());
    BOOL writeOk=ferror(fp)==0;fclose(fp);
    if(!writeOk||!MoveFileExW(temp,file,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp);return;}
    SaveLibraryCatalogEntry(pathIndex);
}
static BOOL ImageFolder(wchar_t *folder,size_t cap) {
    wchar_t app[MAX_PATH]={};if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA|CSIDL_FLAG_CREATE,nullptr,SHGFP_TYPE_CURRENT,app)))return FALSE;
    if(FAILED(StringCchPrintfW(folder,cap,L"%s\\ABMAVPlayer\\HomeTheatre",app)))return FALSE;
    wchar_t root[MAX_PATH]={};StringCchPrintfW(root,_countof(root),L"%s\\ABMAVPlayer",app);
    if(!CreateDirectoryW(root,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return FALSE;
    if(!CreateDirectoryW(folder,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return FALSE;
    return TRUE;
}

static BOOL IsWatchedPath(const wchar_t *path) {
 if(!path||!path[0])return FALSE;wchar_t folder[MAX_PATH]={},file[MAX_PATH]={};if(!ImageFolder(folder,_countof(folder)))return FALSE;
 if(FAILED(StringCchPrintfW(file,_countof(file),L"%s\\watched.tsv",folder)))return FALSE;
 FILE *fp=nullptr;if(_wfopen_s(&fp,file,L"rt, ccs=UTF-8")!=0||!fp)return FALSE;
 wchar_t line[MAX_PATH+8]={};BOOL found=FALSE;while(fgetws(line,_countof(line),fp)){line[wcscspn(line,L"\r\n")]=0;if(!_wcsicmp(line,path)){found=TRUE;break;}}fclose(fp);return found;
}
static BOOL SetWatchedPath(const wchar_t *path,BOOL watched) {
 if(!path||!path[0])return FALSE;wchar_t folder[MAX_PATH]={},file[MAX_PATH]={},temp[MAX_PATH]={};
 if(!ImageFolder(folder,_countof(folder))||FAILED(StringCchPrintfW(file,_countof(file),L"%s\\watched.tsv",folder))||FAILED(StringCchPrintfW(temp,_countof(temp),L"%s\\watched.tmp",folder)))return FALSE;
 std::vector<std::wstring> entries;FILE *old=nullptr;if(_wfopen_s(&old,file,L"rt, ccs=UTF-8")==0&&old){wchar_t line[MAX_PATH+8];while(fgetws(line,_countof(line),old)){line[wcscspn(line,L"\r\n")]=0;if(line[0]&&_wcsicmp(line,path)!=0)entries.emplace_back(line);}fclose(old);}
 if(watched)entries.emplace_back(path);FILE *fp=nullptr;if(_wfopen_s(&fp,temp,L"wt, ccs=UTF-8")!=0||!fp)return FALSE;
 for(const auto &entry:entries)fwprintf(fp,L"%s\n",entry.c_str());BOOL ok=ferror(fp)==0;fclose(fp);
 if(!ok||!MoveFileExW(temp,file,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp);return FALSE;}return TRUE;
}
static void RefreshWatchedButton(void) {
 g_selectedWatched=FALSE;if(g_localMatch>=0&&g_localMatch<(int)g_mediaPaths.size())g_selectedWatched=IsWatchedPath(g_mediaPaths[(size_t)g_localMatch].c_str());
 if(g_watchedButton){SetWindowTextW(g_watchedButton,g_selectedWatched?L"Mark unwatched":L"Mark watched");EnableWindow(g_watchedButton,g_localMatch>=0&&g_localMatch<(int)g_mediaPaths.size());}
}
struct Work {
    HWND target=nullptr;int kind=0,index=-1;
    MovieItem item;
    wchar_t query[256]={};
    std::vector<std::wstring> paths;
    int seasonNumber=-1;
    int tvId=0;
};
struct Result {
    int kind=0,count=0,index=-1,providerId=0;
    BOOL ok=FALSE;
    MovieItem items[100], recommendations[100], seasons[24], episodes[64];
    int recommendationCount=0, seasonCount=0, episodeCount=0;
    wchar_t overview[1800]={},year[32]={},runtime[64]={},genres[256]={},cast[512]={},director[128]={},posterFile[MAX_PATH]={},backdropFile[MAX_PATH]={},error[256]={};
};
static void ParseDetails(const wchar_t *json,Result *r,const wchar_t *fallbackPoster,int providerId,const wchar_t *fallbackBackdrop,int mediaType) {
    const wchar_t *end=json+wcslen(json);
    CopyJsonString(json,end,L"overview",r->overview,_countof(r->overview));
    CopyJsonString(json,end,L"release_date",r->year,_countof(r->year));
    if(!r->year[0])CopyJsonString(json,end,L"first_air_date",r->year,_countof(r->year));
    int runtime=JsonInt(json,end,L"runtime");
    if(runtime>0)StringCchPrintfW(r->runtime,_countof(r->runtime),L"%d min",runtime);
    else {
        int episodes=JsonInt(json,end,L"number_of_episodes");
        if(episodes>0)StringCchPrintfW(r->runtime,_countof(r->runtime),L"%d episodes",episodes);
    }
    const wchar_t *g=wcsstr(json,L"\"genres\"");if(g){const wchar_t *a=wcschr(g,L'['),*b=a?wcschr(a,L']'):nullptr;if(a&&b){const wchar_t *p=a;while((p=wcsstr(p,L"\"name\""))&&p<b){wchar_t name[64]={};CopyJsonString(p,b,L"name",name,_countof(name));if(name[0]){if(r->genres[0])StringCchCatW(r->genres,_countof(r->genres),L"  ·  ");StringCchCatW(r->genres,_countof(r->genres),name);}p+=6;}}}
    const wchar_t *credits=wcsstr(json,L"\"credits\"");
    if(credits) {
        const wchar_t *cast=wcsstr(credits,L"\"cast\"");if(cast){const wchar_t *p=cast;for(int n=0;n<4&&(p=wcsstr(p,L"\"name\""));n++){wchar_t name[96]={};CopyJsonString(p,end,L"name",name,_countof(name));if(name[0]){if(r->cast[0])StringCchCatW(r->cast,_countof(r->cast),L", ");StringCchCatW(r->cast,_countof(r->cast),name);}p+=6;}}
        const wchar_t *crew=wcsstr(credits,L"\"crew\"");if(crew){const wchar_t *p=crew;while((p=wcsstr(p,L"\"job\""))){wchar_t job[64]={};CopyJsonString(p,end,L"job",job,_countof(job));if(!_wcsicmp(job,L"Director")){CopyJsonString(p,end,L"name",r->director,_countof(r->director));break;}p+=5;}}
    }
    wchar_t poster[1024]={};CopyJsonString(json,end,L"poster_path",poster,_countof(poster));
    if(!poster[0]&&fallbackPoster)StringCchCopyW(poster,_countof(poster),fallbackPoster);
    wchar_t folder[MAX_PATH]={};if(ImageFolder(folder,_countof(folder))&&poster[0]){
        StringCchPrintfW(r->posterFile,_countof(r->posterFile),L"%s\\metadata-%d-%d.jpg",folder,mediaType,providerId);
        if(GetFileAttributesW(r->posterFile)==INVALID_FILE_ATTRIBUTES && !ABM_Metadata_DownloadImage(poster,r->posterFile,12000))r->posterFile[0]=0;
    }
    wchar_t backdrop[1024]={};CopyJsonString(json,end,L"backdrop_path",backdrop,_countof(backdrop));
    if(!backdrop[0]&&fallbackBackdrop)StringCchCopyW(backdrop,_countof(backdrop),fallbackBackdrop);
    if(ImageFolder(folder,_countof(folder))&&backdrop[0]) {
        StringCchPrintfW(r->backdropFile,_countof(r->backdropFile),L"%s\\metadata-%d-%d-backdrop.jpg",folder,mediaType,providerId);
        if(GetFileAttributesW(r->backdropFile)==INVALID_FILE_ATTRIBUTES&&!ABM_Metadata_DownloadImage(backdrop,r->backdropFile,12000))r->backdropFile[0]=0;
    }
    const wchar_t *rec=wcsstr(json,L"\"recommendations\"");
    if(rec)ParseSearchJson(rec,mediaType==2?2:1,r->recommendations,&r->recommendationCount);
    const wchar_t *season=wcsstr(json,L"\"seasons\"");
    if(season) {
        const wchar_t *p=wcschr(season,L'[');if(p)p++;
        while(p&&*p&&r->seasonCount<24) {
            while(*p&&(iswspace(*p)||*p==L','))p++;if(*p==L']'||!*p)break;
            if(*p!=L'{'){p++;continue;}
            const wchar_t *start=p;BOOL quoted=FALSE,escaped=FALSE;int depth=0;
            for(;*p;p++){if(quoted){if(escaped)escaped=FALSE;else if(*p==L'\\')escaped=TRUE;else if(*p==L'"')quoted=FALSE;continue;}if(*p==L'"'){quoted=TRUE;continue;}if(*p==L'{')depth++;else if(*p==L'}'&&--depth==0){p++;break;}}
            MovieItem item;item.id=providerId;item.type=2;item.seasonNumber=JsonInt(start,p,L"season_number");
            CopyJsonString(start,p,L"name",item.title,_countof(item.title));CopyJsonString(start,p,L"air_date",item.date,_countof(item.date));
            if(item.title[0])r->seasons[r->seasonCount++]=item;
        }
    }
}
static DWORD WINAPI Worker(LPVOID parameter);
static void SetStatus(const wchar_t *s);
static BOOL IsWatchedPath(const wchar_t *path);
static BOOL SetWatchedPath(const wchar_t *path,BOOL watched);
static void RefreshWatchedButton(void);
static void ParseEpisodes(const wchar_t *json,Result *r,int tvId,int seasonNumber) {
    const wchar_t *p=wcsstr(json,L"\"episodes\"");if(!p)return;
    p=wcschr(p,L'[');if(!p)return;p++;
    while(*p&&r->episodeCount<64) {
        while(*p&&(iswspace(*p)||*p==L','))p++;if(*p==L']'||!*p)break;
        if(*p!=L'{'){p++;continue;}
        const wchar_t *start=p;BOOL quoted=FALSE,escaped=FALSE;int depth=0;
        for(;*p;p++){if(quoted){if(escaped)escaped=FALSE;else if(*p==L'\\')escaped=TRUE;else if(*p==L'"')quoted=FALSE;continue;}if(*p==L'"'){quoted=TRUE;continue;}if(*p==L'{')depth++;else if(*p==L'}'&&--depth==0){p++;break;}}
        MovieItem item;item.id=tvId;item.type=2;item.seasonNumber=seasonNumber;item.episodeNumber=JsonInt(start,p,L"episode_number");
        CopyJsonString(start,p,L"name",item.title,_countof(item.title));CopyJsonString(start,p,L"air_date",item.date,_countof(item.date));CopyJsonString(start,p,L"overview",item.overview,_countof(item.overview));CopyJsonString(start,p,L"still_path",item.backdrop,_countof(item.backdrop));
        if(item.title[0])r->episodes[r->episodeCount++]=item;
    }
}
/* Keep episode links separate from show-level links in matches-v2.tsv.
   The composite type key remains within a positive integer for supported seasons. */
static int EpisodeMatchType(int season,int episode) {
    if(season<0||season>999||episode<0||episode>999)return -1;
    return 1000000+season*1000+episode;
}
static int FindEpisodeMatch(int season,int episode) {
    // Accept common release naming conventions with both padded and unpadded
    // season/episode numbers: S01E02, S1E2, 1x02, 1x2 and Season 1 Episode 2.
    wchar_t variants[5][64]={};
    StringCchPrintfW(variants[0],_countof(variants[0]),L"s%02de%02d",season,episode);
    StringCchPrintfW(variants[1],_countof(variants[1]),L"s%de%d",season,episode);
    StringCchPrintfW(variants[2],_countof(variants[2]),L"%dx%02d",season,episode);
    StringCchPrintfW(variants[3],_countof(variants[3]),L"%dx%d",season,episode);
    StringCchPrintfW(variants[4],_countof(variants[4]),L"season%depisode%d",season,episode);
    std::wstring wanted[5];
    for(int n=0;n<5;n++)wanted[n]=Normalized(variants[n]);
    for(size_t i=0;i<g_mediaPaths.size();i++) {
        const wchar_t *path=g_mediaPaths[i].c_str();
        if(!IsVideoMediaPath(path)||GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)continue;
        const wchar_t *leaf=wcsrchr(path,L'\\');leaf=leaf?leaf+1:path;
        std::wstring name=Normalized(leaf);
        for(const std::wstring &pattern:wanted) {
            if(pattern.empty())continue;
            size_t at=name.find(pattern);
            while(at!=std::wstring::npos) {
                size_t after=at+pattern.size();
                BOOL qualitySuffix=name.compare(after,5,L"1080p")==0||name.compare(after,5,L"2160p")==0||name.compare(after,4,L"720p")==0||name.compare(after,4,L"480p")==0||name.compare(after,2,L"4k")==0;
                if(after>=name.size()||!iswdigit(name[after])||qualitySuffix)return (int)i;
                at=name.find(pattern,at+1);
            }
        }
    }
    return -1;
}
static void StartLibraryScan() {
    if(g_busy)return;
    Work *w=new(std::nothrow) Work();if(!w){SetStatus(L"Could not allocate library scan.");return;}
    w->target=g_window;w->kind=3;w->paths=g_mediaPaths;g_itemCount=0;g_selected=-1;g_localMatch=-1;g_recommendationCount=g_seasonCount=0;EnableWindow(g_recommendButton,FALSE);EnableWindow(g_seasonButton,FALSE);EnableWindow(g_artButton,FALSE);EnableWindow(g_matchButton,FALSE);EnableWindow(g_localPicker,FALSE);SendMessageW(g_results,LB_RESETCONTENT,0,0);ABM_D2D_ClearImage(&g_canvas);InvalidateRect(g_window,nullptr,FALSE);
    HANDLE thread=CreateThread(nullptr,0,Worker,w,0,nullptr);
    if(thread){CloseHandle(thread);g_busy=TRUE;EnableWindow(g_scanButton,FALSE);EnableWindow(g_edit,FALSE);EnableWindow(GetDlgItem(g_window,HT_SEARCH),FALSE);EnableWindow(g_results,FALSE);SetStatus(L"Identifying local movies and shows with keyless providers. Large libraries may take a while…");}
    else {delete w;SetStatus(L"Could not start library identification.");}
}
static void StartSeasonLoad(int seasonNumber) {
    if(g_busy||g_tvId<=0)return;
    Work *w=new(std::nothrow) Work();if(!w)return;w->target=g_window;w->kind=4;w->seasonNumber=seasonNumber;w->tvId=g_tvId;
    HANDLE thread=CreateThread(nullptr,0,Worker,w,0,nullptr);
    if(thread){CloseHandle(thread);g_busy=TRUE;EnableWindow(g_results,FALSE);SetStatus(L"Loading season episodes…");}else delete w;
}
static void StartRecommendations(int index) {
    if(index<0||index>=g_itemCount||g_items[index].type!=1)return;
    Work *w=new(std::nothrow) Work();if(!w)return;
    w->target=g_window;w->kind=5;w->index=index;w->item=g_items[index];
    HANDLE thread=CreateThread(nullptr,0,Worker,w,0,nullptr);
    if(thread)CloseHandle(thread);else delete w;
}
static DWORD WINAPI Worker(LPVOID parameter) {
    Work *w=(Work*)parameter;Result *r=new(std::nothrow) Result();
    if(!r){delete w;return 0;}r->kind=w->kind;r->index=w->index;
    if(w->kind==1) {
        std::vector<wchar_t> json(512*1024);
        if(ABM_Metadata_Search(w->query,ABM_METADATA_MOVIE,json.data(),json.size()))
            ParseSearchJson(json.data(),1,r->items,&r->count);
        json.assign(512*1024,0);
        if(ABM_Metadata_Search(w->query,ABM_METADATA_TV,json.data(),json.size()))
            ParseSearchJson(json.data(),2,r->items,&r->count);
        r->ok=r->count>0;
        if(!r->ok)StringCchCopyW(r->error,_countof(r->error),L"No matches found. Check the title or try another search.");
    } else if(w->kind==2&&w->item.id>0) {
        MovieItem item=w->item;std::vector<wchar_t> json(512*1024);
        r->ok=ABM_Metadata_GetDetails((ABM_METADATA_MEDIA_TYPE)item.type,item.id,json.data(),json.size());
        if(r->ok){
            ParseDetails(json.data(),r,item.poster,item.id,item.backdrop,item.type);
        }
        else StringCchCopyW(r->error,_countof(r->error),L"Could not retrieve details. Check your network connection.");
    } else if(w->kind==3) {
        size_t limit=w->paths.size();
        for(size_t i=0;i<limit&&r->count<100;i++) {
            const wchar_t *path=w->paths[i].c_str();
            if(!IsVideoMediaPath(path)||GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)continue;
            wchar_t query[256]={};MakeMediaSearchTitle(path,query,_countof(query));
            wchar_t *year=wcsstr(query,L" (");if(year&&wcslen(year)>=6)year[0]=0;
            if(!query[0])continue;
            std::vector<wchar_t> json(512*1024);MovieItem found[100];int count=0;
            if(ABM_Metadata_Search(query,ABM_METADATA_MOVIE,json.data(),json.size()))
                ParseSearchJson(json.data(),1,found,&count);
            if(count==0) {
                json.assign(512*1024,0);
                if(ABM_Metadata_Search(query,ABM_METADATA_TV,json.data(),json.size()))
                    ParseSearchJson(json.data(),2,found,&count);
            }
            if(count>0){found[0].localIndex=(int)i;r->items[r->count++]=found[0];}
            Sleep(1100);
        }
        r->ok=r->count>0;
        if(!r->ok)StringCchCopyW(r->error,_countof(r->error),L"No library titles were identified. Try a more descriptive filename.");
    } else if(w->kind==4&&w->tvId>0&&w->seasonNumber>=0) {
        std::vector<wchar_t> json(512*1024);
        r->ok=ABM_Metadata_GetSeasonDetails(w->tvId,w->seasonNumber,json.data(),json.size());
        if(r->ok){ParseEpisodes(json.data(),r,w->tvId,w->seasonNumber);r->ok=r->episodeCount>0;}
        if(!r->ok)StringCchCopyW(r->error,_countof(r->error),L"No episode information was returned for this season.");
    } else if(w->kind==5&&w->item.id>0&&w->item.type==1) {
        r->providerId=w->item.id;
        std::vector<wchar_t> json(512*1024);
        r->ok=ABM_Metadata_GetRecommendations(ABM_METADATA_MOVIE,w->item.id,json.data(),json.size());
        if(r->ok)ParseSearchJson(json.data(),1,r->recommendations,&r->recommendationCount);
    }
    UINT done=w->kind==1?HT_SEARCH_DONE:w->kind==3?HT_SCAN_DONE:w->kind==4?HT_SEASON_DONE:w->kind==5?HT_RECOMMEND_DONE:HT_DETAILS_DONE;
    if(!IsWindow(w->target)||!PostMessageW(w->target,done,0,(LPARAM)r))delete r;
    delete w;return 0;
}
static void StartWork(int kind,int index,const wchar_t *query) {
    Work *w=new(std::nothrow) Work();if(!w)return;w->target=g_window;w->kind=kind;w->index=index;
    if(query)StringCchCopyW(w->query,_countof(w->query),query);
    if(kind==2&&index>=0&&index<g_itemCount)w->item=g_items[index];
    HANDLE thread=CreateThread(nullptr,0,Worker,w,0,nullptr);
    if(thread){CloseHandle(thread);g_busy=TRUE;EnableWindow(g_edit,FALSE);EnableWindow(GetDlgItem(g_window,HT_SEARCH),FALSE);EnableWindow(g_results,FALSE);SetWindowTextW(g_status,kind==1?L"Searching metadata providers…":L"Loading title information…");}
    else {delete w;SetStatus(L"Could not start the metadata worker. Try again.");EnableWindow(g_edit,TRUE);EnableWindow(GetDlgItem(g_window,HT_SEARCH),TRUE);EnableWindow(g_results,TRUE);}
}
static void SetStatus(const wchar_t *s){if(g_status)SetWindowTextW(g_status,s?s:L"");}
static void SetLocalMatchStatus(const wchar_t *prefix) {
    if(g_localMatch<0||g_localMatch>=(int)g_mediaPaths.size()){SetStatus(prefix);return;}
    const wchar_t *path=g_mediaPaths[(size_t)g_localMatch].c_str();
    const wchar_t *leaf=wcsrchr(path,L'\\');leaf=leaf?leaf+1:path;
    wchar_t message[256]={};
    StringCchPrintfW(message,_countof(message),L"%s: %s",prefix?prefix:L"Local match",leaf);
    SetStatus(message);
}
static void DrawAbmButton(const DRAWITEMSTRUCT *d) {
    if(!d||!d->hwndItem)return;
    RECT r=d->rcItem;int width=r.right-r.left,height=r.bottom-r.top;
    if(width<=0||height<=0)return;
    const int id=GetDlgCtrlID(d->hwndItem);
    const BOOL disabled=(d->itemState&ODS_DISABLED)!=0;
    const BOOL pressed=(d->itemState&ODS_SELECTED)!=0;
    const BOOL hot=(d->itemState&ODS_HOTLIGHT)!=0;
    const BOOL focused=(d->itemState&ODS_FOCUS)!=0;
    const BOOL primary=(id==HT_SEARCH||id==HT_PLAY||id==HT_SCAN);
    COLORREF fill=disabled?ABM_UI_COLOR_SURFACE_RAISED:
        (primary?(pressed?ABM_THEME_COLOR_197065:ABM_THEME_COLOR_18706F):
        (pressed?ABM_THEME_COLOR_305E6F:(hot?ABM_THEME_COLOR_1F3648:ABM_UI_COLOR_SURFACE_RAISED)));
    COLORREF text=disabled?ABM_UI_COLOR_TEXT_DISABLED:(primary?ABM_UI_COLOR_TEXT:ABM_UI_COLOR_TEXT_SECONDARY);
    COLORREF edge=focused?ABM_UI_COLOR_FOCUS:(primary?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_BORDER_STRONG);
    wchar_t caption[128]={};GetWindowTextW(d->hwndItem,caption,_countof(caption));
    if(g_controlCanvasReady&&ABM_D2D_CanvasBegin(&g_controlCanvas,d->hDC,&r)){
        ABM_D2D_CanvasFill(&g_controlCanvas,0,0,(float)width,(float)height,ABM_THEME_COLOR_0A1725);
        ABM_D2D_CanvasFillRound(&g_controlCanvas,1,1,(float)width-1,(float)height-1,(float)ABM_UI_RADIUS_BUTTON,fill);
        ABM_D2D_CanvasStrokeRound(&g_controlCanvas,0.5f,0.5f,(float)width-0.5f,(float)height-0.5f,(float)ABM_UI_RADIUS_BUTTON,edge,focused?2.0f:1.0f);
        RECT label={5,1,width-5,height-1};
        ABM_D2D_CanvasText(&g_controlCanvas,caption,label,text,ABM_D2D_TEXT_SMALL,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        ABM_D2D_CanvasEnd(&g_controlCanvas);return;
    }
    HBRUSH b=CreateSolidBrush(fill);FillRect(d->hDC,&r,b);DeleteObject(b);
    SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,text);
    HGDIOBJ old=SelectObject(d->hDC,g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT));
    RECT label={r.left+5,r.top+1,r.right-5,r.bottom-1};
    DrawTextW(d->hDC,caption,-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SelectObject(d->hDC,old);
}
static void DrawAbmResultItem(const DRAWITEMSTRUCT *d) {
    if(!d||d->itemID==(UINT)-1)return;
    RECT r=d->rcItem;int width=r.right-r.left,height=r.bottom-r.top;
    BOOL selected=(d->itemState&ODS_SELECTED)!=0;
    if(g_controlCanvasReady&&ABM_D2D_CanvasBegin(&g_controlCanvas,d->hDC,&r)){
        COLORREF bg=selected?ABM_UI_COLOR_ACCENT_SOFT:((d->itemID&1)?ABM_UI_COLOR_SURFACE:ABM_UI_COLOR_SURFACE_BLUE);
        ABM_D2D_CanvasFill(&g_controlCanvas,0,0,(float)width,(float)height,bg);
        if(selected){
            ABM_D2D_CanvasFillRound(&g_controlCanvas,3,3,(float)width-3,(float)height-3,(float)ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_SURFACE_RAISED);
            ABM_D2D_CanvasStrokeRound(&g_controlCanvas,3,3,(float)width-3,(float)height-3,(float)ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_ACCENT_BRIGHT,1.0f);
            ABM_D2D_CanvasFill(&g_controlCanvas,5,8,8,(float)height-8,ABM_UI_COLOR_ACCENT_BRIGHT);
        }
        wchar_t line[320]={};SendMessageW(d->hwndItem,LB_GETTEXT,d->itemID,(LPARAM)line);
        RECT tr={14,0,width-10,height};
        ABM_D2D_CanvasText(&g_controlCanvas,line,tr,selected?ABM_UI_COLOR_TEXT:ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(d->itemState&ODS_FOCUS)ABM_D2D_CanvasStrokeRound(&g_controlCanvas,1,1,(float)width-1,(float)height-1,(float)ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_FOCUS,1.5f);
        ABM_D2D_CanvasEnd(&g_controlCanvas);return;
    }
    HBRUSH b=CreateSolidBrush(selected?ABM_UI_COLOR_ACCENT_SOFT:ABM_UI_COLOR_SURFACE);FillRect(d->hDC,&r,b);DeleteObject(b);
    wchar_t line[320]={};SendMessageW(d->hwndItem,LB_GETTEXT,d->itemID,(LPARAM)line);
    SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,selected?ABM_UI_COLOR_TEXT:ABM_UI_COLOR_TEXT_SECONDARY);
    HGDIOBJ old=SelectObject(d->hDC,g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT));
    RECT tr={r.left+14,r.top,r.right-10,r.bottom};DrawTextW(d->hDC,line,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(d->hDC,old);
}
static void DrawAbmPickerItem(const DRAWITEMSTRUCT *d) {
    if(!d)return;
    RECT r=d->rcItem;int width=r.right-r.left,height=r.bottom-r.top;
    BOOL selected=(d->itemState&ODS_SELECTED)!=0;
    if(g_controlCanvasReady&&ABM_D2D_CanvasBegin(&g_controlCanvas,d->hDC,&r)){
        ABM_D2D_CanvasFill(&g_controlCanvas,0,0,(float)width,(float)height,selected?ABM_UI_COLOR_ACCENT_SOFT:ABM_UI_COLOR_INPUT);
        if(d->itemID!=(UINT)-1){
            wchar_t item[512]={};SendMessageW(d->hwndItem,CB_GETLBTEXT,d->itemID,(LPARAM)item);
            RECT tr={12,0,width-38,height};
            ABM_D2D_CanvasText(&g_controlCanvas,item,tr,selected?ABM_UI_COLOR_TEXT:ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        ABM_D2D_CanvasEnd(&g_controlCanvas);return;
    }
    HBRUSH b=CreateSolidBrush(selected?ABM_UI_COLOR_ACCENT_SOFT:ABM_UI_COLOR_INPUT);FillRect(d->hDC,&r,b);DeleteObject(b);
    if(d->itemID!=(UINT)-1){wchar_t item[512]={};SendMessageW(d->hwndItem,CB_GETLBTEXT,d->itemID,(LPARAM)item);
        SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,selected?ABM_UI_COLOR_TEXT:ABM_UI_COLOR_TEXT_SECONDARY);
        HGDIOBJ old=SelectObject(d->hDC,g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT));
        RECT tr={r.left+12,r.top,r.right-38,r.bottom};DrawTextW(d->hDC,item,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(d->hDC,old);}
}
static void DrawAbmControlFrame(HWND hwnd,HDC dc) {
    RECT r={};GetClientRect(hwnd,&r);if(r.right<2||r.bottom<2)return;
    POINT p={};GetCursorPos(&p);ScreenToClient(hwnd,&p);
    COLORREF edge=GetFocus()==hwnd?ABM_UI_COLOR_FOCUS:(PtInRect(&r,p)?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_BORDER_STRONG);
    if(g_controlCanvasReady&&ABM_D2D_CanvasBegin(&g_controlCanvas,dc,&r)){
        ABM_D2D_CanvasStrokeRound(&g_controlCanvas,0.5f,0.5f,(float)r.right-0.5f,(float)r.bottom-0.5f,(float)ABM_UI_RADIUS_CONTROL,edge,GetFocus()==hwnd?2.0f:1.0f);
        ABM_D2D_CanvasEnd(&g_controlCanvas);
    }
}
static LRESULT CALLBACK HomePremiumControlProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR ref) {
    (void)id;(void)ref;
    if(msg==WM_NCDESTROY){RemoveWindowSubclass(hwnd,HomePremiumControlProc,1);return DefSubclassProc(hwnd,msg,wp,lp);}
    if(msg==WM_SIZE){LRESULT z=DefSubclassProc(hwnd,msg,wp,lp);RECT r={};GetClientRect(hwnd,&r);HRGN region=CreateRoundRectRgn(0,0,r.right+1,r.bottom+1,12,12);if(region)SetWindowRgn(hwnd,region,TRUE);return z;}
    if(msg==WM_NCPAINT)return 0;
    if(msg==WM_PAINT){
        wchar_t cls[32]={};GetClassNameW(hwnd,cls,_countof(cls));
        if(!_wcsicmp(cls,L"ComboBox")){
            PAINTSTRUCT ps={};HDC dc=BeginPaint(hwnd,&ps);RECT r={};GetClientRect(hwnd,&r);
            if(dc&&g_controlCanvasReady&&ABM_D2D_CanvasBegin(&g_controlCanvas,dc,&r)){
                ABM_D2D_CanvasFill(&g_controlCanvas,0,0,(float)r.right,(float)r.bottom,ABM_UI_COLOR_INPUT);
                ABM_D2D_CanvasStrokeRound(&g_controlCanvas,0.5f,0.5f,(float)r.right-0.5f,(float)r.bottom-0.5f,(float)ABM_UI_RADIUS_CONTROL,GetFocus()==hwnd?ABM_UI_COLOR_FOCUS:ABM_UI_COLOR_BORDER_STRONG,GetFocus()==hwnd?2.0f:1.0f);
                int arrowW=34;RECT ar={r.right-arrowW,1,r.right-1,r.bottom-1};
                ABM_D2D_CanvasFillRound(&g_controlCanvas,(float)ar.left,(float)ar.top,(float)ar.right,(float)ar.bottom,(float)ABM_UI_RADIUS_CONTROL,ABM_THEME_COLOR_113041);
                int selected=(int)SendMessageW(hwnd,CB_GETCURSEL,0,0);wchar_t label[512]={};
                if(selected!=CB_ERR)SendMessageW(hwnd,CB_GETLBTEXT,(WPARAM)selected,(LPARAM)label);
                RECT tr={12,0,r.right-arrowW-8,r.bottom};
                ABM_D2D_CanvasText(&g_controlCanvas,label,tr,ABM_UI_COLOR_TEXT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                float cx=(float)(ar.left+arrowW/2),cy=(float)((ar.top+ar.bottom)/2);
                ABM_D2D_CanvasLine(&g_controlCanvas,cx-5,cy-2,cx,cy+3,ABM_UI_COLOR_ACCENT_BRIGHT,2);
                ABM_D2D_CanvasLine(&g_controlCanvas,cx,cy+3,cx+5,cy-2,ABM_UI_COLOR_ACCENT_BRIGHT,2);
                ABM_D2D_CanvasEnd(&g_controlCanvas);
            }
            EndPaint(hwnd,&ps);return 0;
        }
        LRESULT z=DefSubclassProc(hwnd,msg,wp,lp);HDC dc=GetDC(hwnd);
        if(dc){
            RECT r={};GetClientRect(hwnd,&r);DrawAbmControlFrame(hwnd,dc);
            ReleaseDC(hwnd,dc);
        }return z;
    }
    if(msg==WM_SETFOCUS||msg==WM_KILLFOCUS||msg==WM_MOUSEMOVE||msg==WM_MOUSELEAVE){LRESULT z=DefSubclassProc(hwnd,msg,wp,lp);InvalidateRect(hwnd,NULL,FALSE);return z;}
    return DefSubclassProc(hwnd,msg,wp,lp);
}
static void HomePremiumizeControl(HWND hwnd) {
    if(!hwnd)return;
    LONG_PTR style=GetWindowLongPtrW(hwnd,GWL_STYLE);SetWindowLongPtrW(hwnd,GWL_STYLE,style&~(WS_BORDER|WS_DLGFRAME));
    LONG_PTR ex=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);SetWindowLongPtrW(hwnd,GWL_EXSTYLE,ex&~WS_EX_CLIENTEDGE);
    SetWindowSubclass(hwnd,HomePremiumControlProc,1,0);
    RECT r={};GetClientRect(hwnd,&r);HRGN region=CreateRoundRectRgn(0,0,r.right+1,r.bottom+1,12,12);if(region)SetWindowRgn(hwnd,region,TRUE);
}
static void Layout(HWND w) {
    RECT c;GetClientRect(w,&c);int width=c.right,height=c.bottom;
    if(g_edit)MoveWindow(g_edit,28,103,width-190,36,TRUE);
    if(GetDlgItem(w,HT_SEARCH))MoveWindow(GetDlgItem(w,HT_SEARCH),width-150,103,122,36,TRUE);
    if(g_scanButton)MoveWindow(g_scanButton,width-282,150,126,28,TRUE);
    
    if(g_seasonButton)MoveWindow(g_seasonButton,28,150,100,28,TRUE);
    if(g_results)MoveWindow(g_results,28,186,300,height-272,TRUE);
    if(g_localPicker)MoveWindow(g_localPicker,380,height-136,360,250,TRUE);
    if(g_matchButton)MoveWindow(g_matchButton,748,height-136,150,30,TRUE);
    if(g_artButton)MoveWindow(g_artButton,380,height-82,150,30,TRUE);
    if(g_status)MoveWindow(g_status,28,height-70,320,24,TRUE);
    if(g_play)MoveWindow(g_play,width-176,height-82,148,42,TRUE);
    if(g_watchedButton)MoveWindow(g_watchedButton,width-340,height-82,150,30,TRUE);
}
static RECT MakeRect(LONG l,LONG t,LONG r,LONG b) { RECT value={l,t,r,b};return value; }
static void Draw(HWND w) {
    PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT c;GetClientRect(w,&c);
    if(!g_canvasReady){FillRect(dc,&c,(HBRUSH)GetStockObject(BLACK_BRUSH));EndPaint(w,&ps);return;}
    ABM_D2D_Begin(&g_canvas,BG);
    /* Use the exact main-player brand geometry and monogram. Keep the
       product wordmark, accent rule and subtitle on separate baselines. */
    ABM_D2D_Fill(&g_canvas,0,0,(float)c.right,96,ABM_THEME_COLOR_0D1B2D);
    ABM_D2D_Fill(&g_canvas,0,82,(float)c.right,96,ABM_THEME_COLOR_08111D);
    ABM_D2D_Fill(&g_canvas,0,95,(float)c.right,96,ABM_THEME_COLOR_273D53);
    ABM_D2D_Fill(&g_canvas,0,92,(float)c.right,95,ABM_THEME_COLOR_0D1827);
    ABM_D2D_Round(&g_canvas,20,7,102,87,22,ABM_THEME_COLOR_0D1C2D);
    ABM_D2D_StrokeRound(&g_canvas,20,7,102,87,22,ABM_THEME_COLOR_3F97B4,1.0f);
    ABM_D2D_GradientRound(&g_canvas,28,14,94,80,17,ABM_THEME_COLOR_1C9D9D,ABM_THEME_COLOR_34777E);
    ABM_D2D_StrokeRound(&g_canvas,33,19,89,75,14,ABM_THEME_COLOR_BEE5FF,0.8f);
    ABM_D2D_Line(&g_canvas,42,63,59,30,ABM_THEME_COLOR_FFFFFF,3.0f);
    ABM_D2D_Line(&g_canvas,59,30,76,63,ABM_THEME_COLOR_FFFFFF,3.0f);
    ABM_D2D_Line(&g_canvas,49,49,68,49,ABM_THEME_COLOR_E2F5FF,2.0f);
    ABM_D2D_Line(&g_canvas,62,40,75,48,ABM_THEME_COLOR_FFFFFF,2.0f);
    ABM_D2D_Line(&g_canvas,75,48,62,56,ABM_THEME_COLOR_FFFFFF,2.0f);
    ABM_D2D_Line(&g_canvas,62,56,62,40,ABM_THEME_COLOR_FFFFFF,2.0f);
    ABM_D2D_Text(&g_canvas,L"ABM",MakeRect(116,8,420,49),ABM_THEME_COLOR_F7FAFF,ABM_D2D_TEXT_HEADING,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    ABM_D2D_Text(&g_canvas,L"AV  PLAYER",MakeRect(188,20,390,48),ABM_THEME_COLOR_84CDC2,ABM_D2D_TEXT_BODY,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    ABM_D2D_GradientRound(&g_canvas,116,52,354,54,0,ABM_THEME_COLOR_36B1A8,ABM_THEME_COLOR_70BE9D);
    ABM_D2D_Text(&g_canvas,L"HOME THEATRE  ·  DISCOVER MOVIES & SHOWS",MakeRect(116,60,510,83),ABM_THEME_COLOR_9DB1C9,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    ABM_D2D_Round(&g_canvas,16,150,344,(float)c.bottom-88,14,PANEL);
    ABM_D2D_StrokeRound(&g_canvas,16,150,344,(float)c.bottom-88,14,ABM_UI_COLOR_BORDER,0.8f);
    ABM_D2D_Round(&g_canvas,22,102,(float)c.right-164,140,9,PANEL2);
    ABM_D2D_StrokeRound(&g_canvas,22,102,(float)c.right-164,140,9,ABM_UI_COLOR_BORDER_STRONG,0.9f);
    ABM_D2D_Round(&g_canvas,26,186,334,(float)c.bottom-94,9,PANEL2);
    ABM_D2D_StrokeRound(&g_canvas,26,186,334,(float)c.bottom-94,9,ABM_UI_COLOR_BORDER,0.8f);
    ABM_D2D_Round(&g_canvas,380,(float)c.bottom-136,742,(float)c.bottom-96,8,PANEL2);
    ABM_D2D_StrokeRound(&g_canvas,380,(float)c.bottom-136,742,(float)c.bottom-96,8,ABM_UI_COLOR_BORDER_STRONG,0.8f);
    ABM_D2D_Round(&g_canvas,356,130,(float)c.right-16,(float)c.bottom-40,14,PANEL);
    ABM_D2D_StrokeRound(&g_canvas,356,130,(float)c.right-16,(float)c.bottom-40,14,ABM_UI_COLOR_BORDER,0.8f);
    if(g_selected>=0&&g_selected<g_itemCount) {
        MovieItem &m=g_items[g_selected];
        ABM_D2D_Text(&g_canvas,m.title,MakeRect(380,150,c.right-390,196),TEXT,ABM_D2D_TEXT_HEADING,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        wchar_t meta[160];StringCchPrintfW(meta,_countof(meta),L"%s  ·  %s%s%s",m.type==1?L"MOVIE":L"TELEVISION",g_year[0]?g_year:(m.date[0]?m.date:L"Year unavailable"),g_runtime[0]?L"  ·  ":L"",g_runtime);
        ABM_D2D_Text(&g_canvas,meta,MakeRect(380,198,c.right-390,224),ACCENT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if((g_showBackdrop&&g_backdropFile[0])||g_posterFile[0]) {
            ABM_D2D_Round(&g_canvas,380,244,590,550,12,ABM_UI_COLOR_BACKGROUND);
            if(g_showBackdrop)ABM_D2D_DrawImageCover(&g_canvas,386,250,584,544,0.92f);else ABM_D2D_DrawImage(&g_canvas,386,250,584,544);
        } else {
            ABM_D2D_GradientRound(&g_canvas,380,244,590,550,12,RGB(24,59,69),RGB(12,26,40));
            ABM_D2D_Text(&g_canvas,L"ABM",MakeRect(390,330,580,410),MUTED,ABM_D2D_TEXT_HEADING,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        int tx=612,tw=c.right-tx-42;
        ABM_D2D_Text(&g_canvas,L"OVERVIEW",MakeRect(tx,244,tx+tw,270),ACCENT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        ABM_D2D_Text(&g_canvas,g_overview[0]?g_overview:(m.overview[0]?m.overview:L"No synopsis supplied by the metadata provider."),MakeRect(tx,274,tx+tw,420),TEXT,ABM_D2D_TEXT_BODY,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_END_ELLIPSIS);
        ABM_D2D_Text(&g_canvas,L"GENRES",MakeRect(tx,432,tx+tw,454),ACCENT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        ABM_D2D_Text(&g_canvas,g_genres[0]?g_genres:L"Not available",MakeRect(tx,456,tx+tw,480),TEXT,ABM_D2D_TEXT_BODY,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        ABM_D2D_Text(&g_canvas,L"CAST",MakeRect(tx,490,tx+tw,512),ACCENT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        ABM_D2D_Text(&g_canvas,g_cast[0]?g_cast:L"Not available",MakeRect(tx,514,tx+tw,556),TEXT,ABM_D2D_TEXT_BODY,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_END_ELLIPSIS);
        if(g_director[0])ABM_D2D_Text(&g_canvas,g_director,MakeRect(tx,566,tx+tw,590),MUTED,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        ABM_D2D_Text(&g_canvas,m.type==2?L"TVmaze  ·  CC BY-SA 4.0 data  ·  tvmaze.com":L"Wikidata  ·  CC0 structured data",MakeRect(tx,594,tx+tw,616),MUTED,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    } else {
        ABM_D2D_Text(&g_canvas,L"Select a title to explore its details",MakeRect(380,170,c.right-40,215),TEXT,ABM_D2D_TEXT_HEADING,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        ABM_D2D_Text(&g_canvas,L"Search by movie or television title. Select a result to load its synopsis, artwork, genres and credits. ABM will offer playback only when a matching local file is found in your current library.",MakeRect(380,224,c.right-60,340),MUTED,ABM_D2D_TEXT_BODY,DT_LEFT|DT_TOP|DT_WORDBREAK);
    }
    ABM_D2D_End(&g_canvas);EndPaint(w,&ps);
}
static void SearchNow() {
    wchar_t query[256]={};GetWindowTextW(g_edit,query,_countof(query));
    if(!query[0]){SetStatus(L"Enter a movie or television title to search.");return;}
    g_itemCount=0;g_selected=-1;g_localMatch=-1;g_detailsReady=FALSE;g_posterFile[0]=g_backdropFile[0]=0;g_showBackdrop=FALSE;g_recommendationCount=g_seasonCount=0;EnableWindow(g_recommendButton,FALSE);EnableWindow(g_seasonButton,FALSE);EnableWindow(g_artButton,FALSE);EnableWindow(g_matchButton,FALSE);EnableWindow(g_localPicker,FALSE);ABM_D2D_ClearImage(&g_canvas);
    SendMessageW(g_results,LB_RESETCONTENT,0,0);SetStatus(L"Searching metadata providers…");StartWork(1,-1,query);InvalidateRect(g_window,nullptr,FALSE);
}
static LRESULT CALLBACK HomeWndProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE:
        g_window=w;g_darkBrush=CreateSolidBrush(ABM_UI_COLOR_INPUT);
        g_controlCanvasReady=ABM_D2D_CanvasInit(&g_controlCanvas);
        g_controlFont=CreateFontW(ABM_UI_FONT_HEIGHT(ABM_UI_FONT_SIZE_D2D_BODY),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,ABM_UI_FONT_FAMILY_TEXT);
        g_edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,28,83,700,36,w,(HMENU)HT_EDIT,GetModuleHandleW(nullptr),nullptr);
        g_results=CreateWindowExW(0,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_VSCROLL|LBS_NOINTEGRALHEIGHT,28,180,300,450,w,(HMENU)HT_RESULTS,GetModuleHandleW(nullptr),nullptr);
        g_status=CreateWindowExW(0,L"STATIC",g_statusText,WS_CHILD|WS_VISIBLE|SS_LEFT,28,650,700,24,w,nullptr,GetModuleHandleW(nullptr),nullptr);
        g_play=CreateWindowExW(0,L"BUTTON",L"Play local match",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,800,650,148,42,w,(HMENU)HT_PLAY,GetModuleHandleW(nullptr),nullptr);
        g_watchedButton=CreateWindowExW(0,L"BUTTON",L"Mark watched",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,640,650,150,30,w,(HMENU)HT_WATCHED,GetModuleHandleW(nullptr),nullptr);
        g_scanButton=CreateWindowExW(0,L"BUTTON",L"Identify library",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,800,130,126,28,w,(HMENU)HT_SCAN,GetModuleHandleW(nullptr),nullptr);
        g_recommendButton=CreateWindowExW(0,L"BUTTON",L"Related movies",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,136,150,120,28,w,(HMENU)HT_RECOMMEND,GetModuleHandleW(nullptr),nullptr);
        
        g_seasonButton=CreateWindowExW(0,L"BUTTON",L"Seasons",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,28,150,100,28,w,(HMENU)HT_SEASONS,GetModuleHandleW(nullptr),nullptr);
        g_localPicker=CreateWindowExW(WS_EX_CLIENTEDGE,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,380,602,360,260,w,(HMENU)HT_PICKER,GetModuleHandleW(nullptr),nullptr);
        g_matchButton=CreateWindowExW(0,L"BUTTON",L"Use selected file",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,748,602,150,30,w,(HMENU)HT_MATCH,GetModuleHandleW(nullptr),nullptr);
        g_artButton=CreateWindowExW(0,L"BUTTON",L"Show backdrop",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,380,642,150,30,w,(HMENU)HT_ARTWORK,GetModuleHandleW(nullptr),nullptr);
        CreateWindowExW(0,L"BUTTON",L"Search",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,800,83,122,36,w,(HMENU)HT_SEARCH,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(g_edit,WM_SETFONT,(WPARAM)(g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(g_results,WM_SETFONT,(WPARAM)(g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(g_results,LB_SETITEMHEIGHT,0,48);
        SetWindowTheme(g_edit,L"DarkMode_Explorer",nullptr);SetWindowTheme(g_results,L"DarkMode_Explorer",nullptr);
        SendMessageW(g_play,WM_SETFONT,(WPARAM)(g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(g_watchedButton,WM_SETFONT,(WPARAM)(g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(g_scanButton,WM_SETFONT,(WPARAM)(g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(g_recommendButton,WM_SETFONT,(WPARAM)(g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(g_seasonButton,WM_SETFONT,(WPARAM)(g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(g_localPicker,WM_SETFONT,(WPARAM)(g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(g_matchButton,WM_SETFONT,(WPARAM)(g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(g_artButton,WM_SETFONT,(WPARAM)(g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SendMessageW(g_status,WM_SETFONT,(WPARAM)(g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        SetWindowTheme(g_localPicker,L"DarkMode_Explorer",nullptr);
        HomePremiumizeControl(g_edit);HomePremiumizeControl(g_localPicker);
        SetWindowTheme(g_edit,L"DarkMode_Explorer",nullptr);
        EnableWindow(g_recommendButton,FALSE);EnableWindow(g_seasonButton,FALSE);EnableWindow(g_artButton,FALSE);
        SendMessageW(GetDlgItem(w,HT_SEARCH),WM_SETFONT,(WPARAM)(g_controlFont?g_controlFont:GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        EnableWindow(g_play,FALSE);EnableWindow(g_watchedButton,FALSE);EnableWindow(g_localPicker,FALSE);EnableWindow(g_matchButton,FALSE);Layout(w);return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO *m=(MINMAXINFO*)lp;if(m){m->ptMinTrackSize.x=900;m->ptMinTrackSize.y=720;}return 0; }
    case WM_SIZE:Layout(w);if(g_canvasReady)ABM_D2D_Resize(&g_canvas,LOWORD(lp),HIWORD(lp));InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_COMMAND:
        if(LOWORD(wp)==HT_SEARCH&&HIWORD(wp)==BN_CLICKED){g_scanMode=g_recommendMode=g_seasonMode=g_episodeMode=FALSE;SearchNow();return 0;}
        if(LOWORD(wp)==HT_SCAN&&HIWORD(wp)==BN_CLICKED){g_scanMode=TRUE;g_recommendMode=g_seasonMode=g_episodeMode=FALSE;StartLibraryScan();return 0;}
        if(LOWORD(wp)==HT_RECOMMEND&&HIWORD(wp)==BN_CLICKED&&g_recommendationCount){g_scanMode=FALSE;g_recommendMode=TRUE;g_seasonMode=FALSE;g_episodeMode=FALSE;g_selected=-1;g_itemCount=g_recommendationCount;for(int i=0;i<g_itemCount;i++)g_items[i]=g_recommendations[i];SendMessageW(g_results,LB_RESETCONTENT,0,0);for(int i=0;i<g_itemCount;i++){wchar_t line[320];StringCchPrintfW(line,_countof(line),L"%s   ·   %s",g_items[i].title,g_items[i].type==1?L"MOVIE":L"TV");SendMessageW(g_results,LB_ADDSTRING,0,(LPARAM)line);}SetStatus(L"Select a recommendation to explore its details.");return 0;}
        if(LOWORD(wp)==HT_SEASONS&&HIWORD(wp)==BN_CLICKED&&g_seasonCount){g_scanMode=FALSE;g_recommendMode=FALSE;g_seasonMode=TRUE;g_episodeMode=FALSE;g_selected=-1;g_itemCount=g_seasonCount;for(int i=0;i<g_itemCount;i++)g_items[i]=g_seasons[i];SendMessageW(g_results,LB_RESETCONTENT,0,0);for(int i=0;i<g_itemCount;i++){wchar_t line[320];StringCchPrintfW(line,_countof(line),L"%s   ·   %s",g_items[i].title,g_items[i].date);SendMessageW(g_results,LB_ADDSTRING,0,(LPARAM)line);}SetWindowTextW(g_seasonButton,L"Seasons");SetStatus(L"Select a season to browse its episodes.");return 0;}
        if(LOWORD(wp)==HT_ARTWORK&&HIWORD(wp)==BN_CLICKED){g_showBackdrop=!g_showBackdrop;const wchar_t *art=(g_showBackdrop&&g_backdropFile[0])?g_backdropFile:g_posterFile;if(art[0])ABM_D2D_LoadImage(&g_canvas,art);SetWindowTextW(g_artButton,g_showBackdrop?L"Show poster":L"Show backdrop");InvalidateRect(w,nullptr,FALSE);return 0;}
        if(LOWORD(wp)==HT_MATCH&&HIWORD(wp)==BN_CLICKED&&g_localPicker){int pick=(int)SendMessageW(g_localPicker,CB_GETCURSEL,0,0);if(pick>=0){g_localMatch=(int)SendMessageW(g_localPicker,CB_GETITEMDATA,pick,0);if(g_selected>=0&&g_selected<g_itemCount){if(g_episodeMode&&g_items[g_selected].episodeNumber>=0){int key=EpisodeMatchType(g_items[g_selected].seasonNumber,g_items[g_selected].episodeNumber);if(key>0)SaveManualMatch(g_tvId,key,g_localMatch);}else SaveManualMatch(g_items[g_selected].id,g_items[g_selected].type,g_localMatch);}EnableWindow(g_play,g_localMatch>=0);RefreshWatchedButton();SetLocalMatchStatus(L"Manual local match saved");}return 0;}
        if(LOWORD(wp)==HT_EDIT&&HIWORD(wp)==EN_UPDATE)return 0;
        if(LOWORD(wp)==HT_EDIT&&HIWORD(wp)==EN_MAXTEXT)return 0;
        if(LOWORD(wp)==HT_EDIT&&HIWORD(wp)==EN_SETFOCUS)return 0;
        if(LOWORD(wp)==HT_EDIT&&HIWORD(wp)==EN_KILLFOCUS)return 0;
        if(LOWORD(wp)==HT_EDIT&&HIWORD(wp)==0)return 0;
        if(LOWORD(wp)==HT_RESULTS&&HIWORD(wp)==LBN_SELCHANGE) {
            int selected=(int)SendMessageW(g_results,LB_GETCURSEL,0,0);
            if(selected>=0&&selected<g_itemCount) {
                g_selected=selected;
                if(g_seasonMode&&!g_episodeMode&&g_items[selected].seasonNumber>=0){StartSeasonLoad(g_items[selected].seasonNumber);return 0;}
                if(g_episodeMode&&g_items[selected].episodeNumber>=0){
                    MovieItem episode=g_items[selected];
                    const int episodeKey=EpisodeMatchType(episode.seasonNumber,episode.episodeNumber);
                    g_localMatch=episodeKey>0?LoadManualMatch(g_tvId,episodeKey):-1;
                    if(g_localMatch<0)g_localMatch=FindEpisodeMatch(episode.seasonNumber,episode.episodeNumber);
                    if(g_localMatch>=0&&episodeKey>0)SaveManualMatch(g_tvId,episodeKey,g_localMatch);
                    StringCchCopyW(g_overview,_countof(g_overview),episode.overview);StringCchCopyW(g_year,_countof(g_year),episode.date);g_runtime[0]=g_genres[0]=g_cast[0]=g_director[0]=g_posterFile[0]=g_backdropFile[0]=0;g_showBackdrop=FALSE;ABM_D2D_ClearImage(&g_canvas);
                    // TVmaze supplies landscape episode stills. Cache them separately
                    // from show posters and display them as a wide image in the details pane.
                    if(episode.backdrop[0]) {
                        wchar_t folder[MAX_PATH]={},cache[MAX_PATH]={};
                        if(ImageFolder(folder,_countof(folder))&&SUCCEEDED(StringCchPrintfW(cache,_countof(cache),L"%s\\episode-%d-s%02d-e%02d.jpg",folder,episode.id,episode.seasonNumber,episode.episodeNumber))) {
                            if(GetFileAttributesW(cache)==INVALID_FILE_ATTRIBUTES)ABM_Metadata_DownloadImage(episode.backdrop,cache,12000);
                            if(GetFileAttributesW(cache)!=INVALID_FILE_ATTRIBUTES) {
                                StringCchCopyW(g_posterFile,_countof(g_posterFile),cache);
                                StringCchCopyW(g_backdropFile,_countof(g_backdropFile),cache);
                                g_showBackdrop=TRUE;ABM_D2D_LoadImage(&g_canvas,cache);
                            }
                        }
                    }
                    if(g_localPicker){SendMessageW(g_localPicker,CB_RESETCONTENT,0,0);for(size_t i=0;i<g_mediaPaths.size();i++){const wchar_t *path=g_mediaPaths[i].c_str();if(!IsVideoMediaPath(path)||GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)continue;const wchar_t *leaf=wcsrchr(path,L'\\');leaf=leaf?leaf+1:path;int n=(int)SendMessageW(g_localPicker,CB_ADDSTRING,0,(LPARAM)leaf);SendMessageW(g_localPicker,CB_SETITEMDATA,n,(LPARAM)i);if((int)i==g_localMatch)SendMessageW(g_localPicker,CB_SETCURSEL,n,0);}}
                    EnableWindow(g_artButton,FALSE);EnableWindow(g_localPicker,TRUE);EnableWindow(g_matchButton,TRUE);
                    RefreshWatchedButton();
                    EnableWindow(g_play,g_localMatch>=0);SetStatus(g_localMatch>=0?L"Episode matched. Its local file link is saved for future sessions.":L"No episode filename match. Choose the correct local file to link this episode.");InvalidateRect(w,nullptr,FALSE);return 0;
                }
                g_localMatch=LoadManualMatch(g_items[selected].id,g_items[selected].type);if(g_localMatch<0)g_localMatch=g_items[selected].localIndex>=0?g_items[selected].localIndex:FindLocalMatch(g_items[selected].title);
                g_overview[0]=g_year[0]=g_runtime[0]=g_genres[0]=g_cast[0]=g_director[0]=g_posterFile[0]=g_backdropFile[0]=0;
                g_showBackdrop=FALSE;g_detailsReady=FALSE;g_recommendationCount=g_seasonCount=0;EnableWindow(g_matchButton,FALSE);EnableWindow(g_localPicker,FALSE);EnableWindow(g_artButton,FALSE);EnableWindow(g_recommendButton,FALSE);EnableWindow(g_seasonButton,FALSE);ABM_D2D_ClearImage(&g_canvas);
                EnableWindow(g_play,g_localMatch>=0);
                if(g_localMatch>=0)SetLocalMatchStatus(L"Local file match · details loading");else SetStatus(L"No filename match found. Details are loading…");
                StartWork(2,selected,nullptr);InvalidateRect(w,nullptr,FALSE);
            }return 0;
        }
        if(LOWORD(wp)==HT_PLAY&&HIWORD(wp)==BN_CLICKED&&g_localMatch>=0&&g_playCallback){int index=g_localMatch;ABM_HomeTheatrePlayFn callback=g_playCallback;DestroyWindow(w);callback(index);return 0;}
        if(LOWORD(wp)==HT_WATCHED&&HIWORD(wp)==BN_CLICKED&&g_localMatch>=0&&g_localMatch<(int)g_mediaPaths.size()) {
            BOOL next=!g_selectedWatched;
            if(SetWatchedPath(g_mediaPaths[(size_t)g_localMatch].c_str(),next)){RefreshWatchedButton();SetStatus(next?L"Marked as watched.":L"Marked as unwatched.");InvalidateRect(w,nullptr,FALSE);}
            else SetStatus(L"Could not save watched status.");return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp,TRANSPARENT);SetTextColor((HDC)wp,ABM_UI_COLOR_TEXT_MUTED);return (LRESULT)g_darkBrush;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetBkColor((HDC)wp,ABM_UI_COLOR_INPUT);SetTextColor((HDC)wp,ABM_UI_COLOR_TEXT);return (LRESULT)g_darkBrush;
    case WM_MEASUREITEM: { MEASUREITEMSTRUCT *m=(MEASUREITEMSTRUCT*)lp;if(m&&m->CtlID==HT_RESULTS){m->itemHeight=48;return TRUE;}if(m&&m->CtlID==HT_PICKER){m->itemHeight=36;return TRUE;}break; }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)lp;if(!d)break;
        if(d->CtlType==ODT_BUTTON){DrawAbmButton(d);return TRUE;}
        if(d->CtlID==HT_PICKER){DrawAbmPickerItem(d);return TRUE;}
        if(d->CtlID==HT_RESULTS){DrawAbmResultItem(d);return TRUE;}
        break;
    }
    case HT_SEARCH_DONE: {
        Result *r=(Result*)lp;if(!r)return 0;g_busy=FALSE;
        if(r->ok){g_itemCount=r->count;for(int i=0;i<g_itemCount;i++){g_items[i]=r->items[i];wchar_t line[320];StringCchPrintfW(line,_countof(line),L"%s   ·   %s",g_items[i].title,g_items[i].type==1?L"MOVIE":L"TV");SendMessageW(g_results,LB_ADDSTRING,0,(LPARAM)line);}SetStatus(L"Select a result to load its cinematic details.");}
        else SetStatus(r->error[0]?r->error:L"No results found.");
        EnableWindow(g_edit,TRUE);EnableWindow(GetDlgItem(w,HT_SEARCH),TRUE);EnableWindow(g_results,TRUE);
        delete r;InvalidateRect(w,nullptr,FALSE);return 0;
    }
    case HT_SCAN_DONE: {
        Result *r=(Result*)lp;if(!r)return 0;g_busy=FALSE;EnableWindow(g_scanButton,TRUE);EnableWindow(g_edit,TRUE);EnableWindow(GetDlgItem(w,HT_SEARCH),TRUE);EnableWindow(g_results,TRUE);
        if(r->ok){g_scanMode=TRUE;g_recommendMode=g_seasonMode=g_episodeMode=FALSE;g_selected=-1;g_itemCount=r->count;SendMessageW(g_results,LB_RESETCONTENT,0,0);for(int i=0;i<g_itemCount;i++){g_items[i]=r->items[i];const int local=g_items[i].localIndex;const wchar_t *leaf=L"";if(local>=0&&local<(int)g_mediaPaths.size()){leaf=wcsrchr(g_mediaPaths[(size_t)local].c_str(),L'\\');leaf=leaf?leaf+1:g_mediaPaths[(size_t)local].c_str();}wchar_t line[480];StringCchPrintfW(line,_countof(line),L"%s   ·   %s   ·   %s",g_items[i].title,g_items[i].type==1?L"MOVIE":L"TV",leaf);SendMessageW(g_results,LB_ADDSTRING,0,(LPARAM)line);}wchar_t status[160];StringCchPrintfW(status,_countof(status),L"Identified %d local titles. Select one to view details and play its matched file.",g_itemCount);SetStatus(status);}
        else SetStatus(r->error[0]?r->error:L"No library matches found.");
        delete r;InvalidateRect(w,nullptr,FALSE);return 0;
    }
    case HT_RECOMMEND_DONE: {
        Result *r=(Result*)lp;if(!r)return 0;
        if(r->ok&&g_selected>=0&&g_selected<g_itemCount&&g_items[g_selected].type==1&&g_items[g_selected].id==r->providerId) {
            g_recommendationCount=r->recommendationCount;
            for(int i=0;i<g_recommendationCount;i++)g_recommendations[i]=r->recommendations[i];
            EnableWindow(g_recommendButton,g_recommendationCount>0);
        }
        delete r;return 0;
    }
    case HT_SEASON_DONE: {
        Result *r=(Result*)lp;if(!r)return 0;g_busy=FALSE;EnableWindow(g_results,TRUE);
        if(r->ok){g_episodeMode=TRUE;g_selected=-1;EnableWindow(g_localPicker,FALSE);EnableWindow(g_matchButton,FALSE);g_itemCount=r->episodeCount;SendMessageW(g_results,LB_RESETCONTENT,0,0);for(int i=0;i<g_itemCount;i++){g_items[i]=r->episodes[i];wchar_t line[320];StringCchPrintfW(line,_countof(line),L"%02d  ·  %s",g_items[i].episodeNumber,g_items[i].title);SendMessageW(g_results,LB_ADDSTRING,0,(LPARAM)line);}SetWindowTextW(g_seasonButton,L"Back to seasons");SetStatus(L"Select an episode to view its synopsis and check local filename matching.");}
        else SetStatus(r->error[0]?r->error:L"Episode list unavailable.");
        delete r;InvalidateRect(w,nullptr,FALSE);return 0;
    }
    case HT_DETAILS_DONE: {
        Result *r=(Result*)lp;if(!r)return 0;g_busy=FALSE;
        if(r->ok&&r->index==g_selected){
            StringCchCopyW(g_overview,_countof(g_overview),r->overview);
            StringCchCopyW(g_year,_countof(g_year),r->year);
            StringCchCopyW(g_runtime,_countof(g_runtime),r->runtime);
            StringCchCopyW(g_genres,_countof(g_genres),r->genres);
            StringCchCopyW(g_cast,_countof(g_cast),r->cast);
            StringCchCopyW(g_director,_countof(g_director),r->director);
            StringCchCopyW(g_posterFile,_countof(g_posterFile),r->posterFile);
            StringCchCopyW(g_backdropFile,_countof(g_backdropFile),r->backdropFile);g_showBackdrop=FALSE;
            if(g_posterFile[0])ABM_D2D_LoadImage(&g_canvas,g_posterFile);else if(g_backdropFile[0]){g_showBackdrop=TRUE;ABM_D2D_LoadImage(&g_canvas,g_backdropFile);SetWindowTextW(g_artButton,L"Show poster");}
            g_recommendationCount=r->recommendationCount;for(int i=0;i<g_recommendationCount;i++)g_recommendations[i]=r->recommendations[i];
            g_seasonCount=r->seasonCount;for(int i=0;i<g_seasonCount;i++)g_seasons[i]=r->seasons[i];
            g_tvId=g_items[g_selected].id;g_tvType=g_items[g_selected].type;
            /* A library-identification result already carries the exact source
               playlist index. Persist that association automatically so the
               next Home Theatre session does not require a manual relink. */
            if(g_localMatch>=0&&g_localMatch<(int)g_mediaPaths.size()&&
               GetFileAttributesW(g_mediaPaths[(size_t)g_localMatch].c_str())!=INVALID_FILE_ATTRIBUTES) {
                SaveManualMatch(g_items[g_selected].id,g_items[g_selected].type,g_localMatch);
            }
            if(g_localPicker){SendMessageW(g_localPicker,CB_RESETCONTENT,0,0);for(size_t i=0;i<g_mediaPaths.size();i++){const wchar_t *path=g_mediaPaths[i].c_str();if(!IsVideoMediaPath(path)||GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)continue;const wchar_t *leaf=wcsrchr(path,L'\\');leaf=leaf?leaf+1:path;int n=(int)SendMessageW(g_localPicker,CB_ADDSTRING,0,(LPARAM)leaf);SendMessageW(g_localPicker,CB_SETITEMDATA,n,(LPARAM)i);if((int)i==g_localMatch)SendMessageW(g_localPicker,CB_SETCURSEL,n,0);}}
            EnableWindow(g_artButton,g_backdropFile[0]!=0);EnableWindow(g_recommendButton,g_recommendationCount>0);EnableWindow(g_seasonButton,g_tvType==2&&g_seasonCount>0);EnableWindow(g_localPicker,TRUE);EnableWindow(g_matchButton,TRUE);
            RefreshWatchedButton();
            if(g_localMatch>=0)SetLocalMatchStatus(L"Ready to play local file");else SetStatus(L"Metadata ready. No matching local file was found in the current library.");
            if(g_tvType==1)StartRecommendations(g_selected);
        } else SetStatus(r->error[0]?r->error:L"Details unavailable.");
        EnableWindow(g_edit,TRUE);EnableWindow(GetDlgItem(w,HT_SEARCH),TRUE);EnableWindow(g_results,TRUE);
        delete r;InvalidateRect(w,nullptr,FALSE);return 0;
    }
    case WM_PAINT:Draw(w);return 0;
    case WM_CLOSE:DestroyWindow(w);return 0;
    case WM_DESTROY:
        if(g_canvasReady){ABM_D2D_Destroy(&g_canvas);g_canvasReady=FALSE;}
        if(g_controlCanvasReady){ABM_D2D_CanvasDestroy(&g_controlCanvas);g_controlCanvasReady=FALSE;}
        if(g_controlFont){DeleteObject(g_controlFont);g_controlFont=nullptr;}
        if(g_darkBrush){DeleteObject(g_darkBrush);g_darkBrush=nullptr;}
        g_window=g_edit=g_results=g_play=g_status=g_localPicker=g_scanButton=g_matchButton=g_artButton=g_recommendButton=g_seasonButton=g_watchedButton=nullptr;g_itemCount=0;g_selected=-1;g_localMatch=-1;
        HWND owner=GetWindow(w,GW_OWNER);if(owner&&IsWindow(owner)){EnableWindow(owner,TRUE);SetActiveWindow(owner);}
        return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
}
extern "C" void ABM_HomeTheatre_Show(HWND owner,const wchar_t *const *mediaPaths,int mediaCount,ABM_HomeTheatrePlayFn playCallback) {
    if(g_window&&IsWindow(g_window)){ShowWindow(g_window,SW_SHOWNORMAL);SetForegroundWindow(g_window);return;}
    g_mediaPaths.clear();for(int i=0;mediaPaths&&i<mediaCount;i++)if(mediaPaths[i]&&mediaPaths[i][0])g_mediaPaths.emplace_back(mediaPaths[i]);
    g_playCallback=playCallback;g_itemCount=0;g_selected=-1;g_localMatch=-1;
    static BOOL registered=FALSE;HINSTANCE instance=GetModuleHandleW(nullptr);
    if(!registered){WNDCLASSEXW wc={};wc.cbSize=sizeof(wc);wc.lpfnWndProc=HomeWndProc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP_ICON));wc.hIconSm=wc.hIcon;wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);wc.lpszClassName=L"ABMAVPlayerHomeTheatre";if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;registered=TRUE;}
    RECT wr={0,0,1080,720};DWORD style=WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN;
    AdjustWindowRectEx(&wr,style,FALSE,0);RECT o={};GetWindowRect(owner,&o);
    int x=o.left+(o.right-o.left-(wr.right-wr.left))/2,y=o.top+(o.bottom-o.top-(wr.bottom-wr.top))/2;
    g_window=CreateWindowExW(WS_EX_APPWINDOW,L"ABMAVPlayerHomeTheatre",L"ABM AV Player — Home Theatre",style,x,y,wr.right-wr.left,wr.bottom-wr.top,owner,nullptr,instance,nullptr);
    if(!g_window)return;
    HICON appIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP_ICON));
    if(appIcon){SendMessageW(g_window,WM_SETICON,ICON_BIG,(LPARAM)appIcon);SendMessageW(g_window,WM_SETICON,ICON_SMALL,(LPARAM)appIcon);}
    BOOL dark=TRUE;DwmSetWindowAttribute(g_window, DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    g_canvasReady=ABM_D2D_Init(&g_canvas,g_window);
    EnableWindow(owner,FALSE);
    ShowWindow(g_window,SW_SHOW);UpdateWindow(g_window);
}

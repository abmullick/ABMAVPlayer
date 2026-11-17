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
#include <dwmapi.h>
#include <uxtheme.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#include "tmdb_home_theatre.h"
#include "tmdb_metadata.h"
#include "ui_d2d.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
enum { HT_EDIT=5101, HT_SEARCH=5102, HT_RESULTS=5103, HT_PLAY=5104 };
enum { HT_SEARCH_DONE=WM_APP+0x5A1, HT_DETAILS_DONE=WM_APP+0x5A2 };
static const COLORREF BG=RGB(7,15,25), PANEL=RGB(15,29,44), PANEL2=RGB(10,21,34);
static const COLORREF TEXT=RGB(239,246,252), MUTED=RGB(145,169,190), ACCENT=RGB(73,199,181);
static HWND g_window=nullptr, g_edit=nullptr, g_results=nullptr, g_play=nullptr, g_status=nullptr;
static ABM_D2D g_canvas={};
static BOOL g_canvasReady=FALSE;
static HBRUSH g_darkBrush=nullptr;
static std::vector<std::wstring> g_mediaPaths;
static ABM_HomeTheatrePlayFn g_playCallback=nullptr;
static struct MovieItem {
    int id=0, type=1;
    wchar_t title[256]={}, date[32]={}, overview[1400]={}, poster[256]={};
} g_items[24];
static int g_itemCount=0, g_selected=-1, g_localMatch=-1;
static wchar_t g_overview[1800]={}, g_year[32]={}, g_runtime[64]={}, g_genres[256]={}, g_cast[512]={}, g_director[128]={}, g_posterFile[MAX_PATH]={};
static BOOL g_detailsReady=FALSE, g_busy=FALSE;
static wchar_t g_statusText[256]=L"Search TMDB for a movie or television series in your local collection.";

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
    while(*p&&*count<24) {
        while(*p&&(iswspace(*p)||*p==L','))p++;if(*p==L']'||!*p)break;
        if(*p!=L'{'){p++;continue;}
        const wchar_t *start=p;BOOL quoted=FALSE,escaped=FALSE;int depth=0;
        for(;*p;p++) {
            if(quoted){if(escaped)escaped=FALSE;else if(*p==L'\\')escaped=TRUE;else if(*p==L'\"')quoted=FALSE;continue;}
            if(*p==L'\"'){quoted=TRUE;continue;}if(*p==L'{')depth++;else if(*p==L'}'&&--depth==0){p++;break;}
        }
        const wchar_t *finish=p;if(finish<=start)break;
        int id=JsonInt(start,finish,L"id");if(id<=0)continue;
        MovieItem item;item.id=id;item.type=type;
        CopyJsonString(start,finish,type==1?L"title":L"name",item.title,_countof(item.title));
        CopyJsonString(start,finish,type==1?L"release_date":L"first_air_date",item.date,_countof(item.date));
        CopyJsonString(start,finish,L"overview",item.overview,_countof(item.overview));
        CopyJsonString(start,finish,L"poster_path",item.poster,_countof(item.poster));
        if(item.title[0])items[(*count)++]=item;
    }
}
static std::wstring Normalized(const wchar_t *s) {
    std::wstring out;for(;s&&*s;s++)if(iswalnum(*s))out.push_back((wchar_t)towlower(*s));return out;
}
static int FindLocalMatch(const wchar_t *title) {
    std::wstring wanted=Normalized(title);if(wanted.empty())return -1;
    for(size_t i=0;i<g_mediaPaths.size();i++) {
        const wchar_t *leaf=wcsrchr(g_mediaPaths[i].c_str(),L'\\');leaf=leaf?leaf+1:g_mediaPaths[i].c_str();
        std::wstring file=leaf;size_t dot=file.find_last_of(L'.');if(dot!=std::wstring::npos)file.resize(dot);
        std::wstring normalized=Normalized(file.c_str());
        if(normalized==wanted || (wanted.size()>4&&normalized.find(wanted)!=std::wstring::npos))return (int)i;
    }
    return -1;
}
static BOOL ImageFolder(wchar_t *folder,size_t cap) {
    wchar_t app[MAX_PATH]={};if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA|CSIDL_FLAG_CREATE,nullptr,SHGFP_TYPE_CURRENT,app)))return FALSE;
    if(FAILED(StringCchPrintfW(folder,cap,L"%s\\ABMAVPlayer\\HomeTheatre",app)))return FALSE;
    wchar_t root[MAX_PATH]={};StringCchPrintfW(root,_countof(root),L"%s\\ABMAVPlayer",app);
    if(!CreateDirectoryW(root,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return FALSE;
    if(!CreateDirectoryW(folder,nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return FALSE;
    return TRUE;
}
struct Work {
    HWND target=nullptr;int kind=0,index=-1;
    MovieItem item;
    wchar_t query[256]={};
};
struct Result {
    int kind=0,count=0,index=-1;
    BOOL ok=FALSE;
    MovieItem items[24];
    wchar_t overview[1800]={},year[32]={},runtime[64]={},genres[256]={},cast[512]={},director[128]={},posterFile[MAX_PATH]={},error[256]={};
};
static void ParseDetails(const wchar_t *json,Result *r,const wchar_t *fallbackPoster,int tmdbId) {
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
    wchar_t poster[256]={};CopyJsonString(json,end,L"poster_path",poster,_countof(poster));
    if(!poster[0]&&fallbackPoster)StringCchCopyW(poster,_countof(poster),fallbackPoster);
    wchar_t folder[MAX_PATH]={};if(ImageFolder(folder,_countof(folder))&&poster[0]){
        StringCchPrintfW(r->posterFile,_countof(r->posterFile),L"%s\\tmdb-%d.jpg",folder,tmdbId);
        if(GetFileAttributesW(r->posterFile)==INVALID_FILE_ATTRIBUTES && !ABM_TMDB_DownloadImage(poster,r->posterFile,12000))r->posterFile[0]=0;
    }
}
static DWORD WINAPI Worker(LPVOID parameter) {
    Work *w=(Work*)parameter;Result *r=new(std::nothrow) Result();
    if(!r){delete w;return 0;}r->kind=w->kind;r->index=w->index;
    if(w->kind==1) {
        if(!ABM_TMDB_IsConfigured())StringCchCopyW(r->error,_countof(r->error),L"Configure your TMDB Read Access Token in Settings first.");
        else {
            std::vector<wchar_t> json(512*1024);
            if(ABM_TMDB_Search(w->query,ABM_TMDB_MOVIE,json.data(),json.size()))ParseSearchJson(json.data(),1,r->items,&r->count);
            json.assign(512*1024,0);
            if(ABM_TMDB_Search(w->query,ABM_TMDB_TV,json.data(),json.size()))ParseSearchJson(json.data(),2,r->items,&r->count);
            r->ok=r->count>0;
            if(!r->ok)StringCchCopyW(r->error,_countof(r->error),L"No TMDB matches found. Check the title or try another search.");
        }
    } else if(w->kind==2&&w->item.id>0) {
        MovieItem item=w->item;std::vector<wchar_t> json(512*1024);
        r->ok=ABM_TMDB_GetDetails(item.type==1?ABM_TMDB_MOVIE:ABM_TMDB_TV,item.id,json.data(),json.size());
        if(r->ok)ParseDetails(json.data(),r,item.poster,item.id);
        else StringCchCopyW(r->error,_countof(r->error),L"Could not retrieve details. Check your connection and TMDB token.");
    }
    if(!IsWindow(w->target)||!PostMessageW(w->target,w->kind==1?HT_SEARCH_DONE:HT_DETAILS_DONE,0,(LPARAM)r))delete r;
    delete w;return 0;
}
static void StartWork(int kind,int index,const wchar_t *query) {
    Work *w=new(std::nothrow) Work();if(!w)return;w->target=g_window;w->kind=kind;w->index=index;
    if(query)StringCchCopyW(w->query,_countof(w->query),query);
    if(kind==2&&index>=0&&index<g_itemCount)w->item=g_items[index];
    HANDLE thread=CreateThread(nullptr,0,Worker,w,0,nullptr);
    if(thread){CloseHandle(thread);g_busy=TRUE;EnableWindow(g_edit,FALSE);EnableWindow(GetDlgItem(g_window,HT_SEARCH),FALSE);EnableWindow(g_results,FALSE);SetWindowTextW(g_status,kind==1?L"Searching TMDB…":L"Loading movie information…");}
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
static void Layout(HWND w) {
    RECT c;GetClientRect(w,&c);int width=c.right,height=c.bottom;
    if(g_edit)MoveWindow(g_edit,28,83,width-190,36,TRUE);
    if(GetDlgItem(w,HT_SEARCH))MoveWindow(GetDlgItem(w,HT_SEARCH),width-150,83,122,36,TRUE);
    if(g_results)MoveWindow(g_results,28,144,300,height-230,TRUE);
    if(g_status)MoveWindow(g_status,28,height-70,width-240,24,TRUE);
    if(g_play)MoveWindow(g_play,width-176,height-78,148,42,TRUE);
}
static RECT MakeRect(LONG l,LONG t,LONG r,LONG b) { RECT value={l,t,r,b};return value; }
static void Draw(HWND w) {
    PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT c;GetClientRect(w,&c);
    if(!g_canvasReady){FillRect(dc,&c,(HBRUSH)GetStockObject(BLACK_BRUSH));EndPaint(w,&ps);return;}
    ABM_D2D_Begin(&g_canvas,BG);
    ABM_D2D_GradientRound(&g_canvas,16,14,(float)c.right-16,70,16,RGB(15,42,57),RGB(10,25,40));
    ABM_D2D_StrokeRound(&g_canvas,16,14,(float)c.right-16,70,16,RGB(40,94,111),0.9f);
    ABM_D2D_Text(&g_canvas,L"ABM  /  HOME THEATRE",MakeRect(34,22,c.right-34,52),TEXT,ABM_D2D_TEXT_HEADING,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    ABM_D2D_Text(&g_canvas,L"DISCOVER MOVIES & SHOWS  ·  MATCH TO LOCAL MEDIA",MakeRect(36,50,c.right-34,66),ACCENT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    ABM_D2D_Round(&g_canvas,16,130,344,(float)c.bottom-88,14,PANEL);
    ABM_D2D_StrokeRound(&g_canvas,16,130,344,(float)c.bottom-88,14,RGB(37,65,85),0.8f);
    ABM_D2D_Text(&g_canvas,L"DISCOVER",MakeRect(34,142,320,170),ACCENT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    ABM_D2D_Round(&g_canvas,356,130,(float)c.right-16,(float)c.bottom-88,14,PANEL);
    ABM_D2D_StrokeRound(&g_canvas,356,130,(float)c.right-16,(float)c.bottom-88,14,RGB(37,65,85),0.8f);
    if(g_selected>=0&&g_selected<g_itemCount) {
        MovieItem &m=g_items[g_selected];
        ABM_D2D_Text(&g_canvas,m.title,MakeRect(380,150,c.right-390,196),TEXT,ABM_D2D_TEXT_HEADING,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        wchar_t meta[160];StringCchPrintfW(meta,_countof(meta),L"%s  ·  %s%s%s",m.type==1?L"MOVIE":L"TELEVISION",g_year[0]?g_year:(m.date[0]?m.date:L"Year unavailable"),g_runtime[0]?L"  ·  ":L"",g_runtime);
        ABM_D2D_Text(&g_canvas,meta,MakeRect(380,198,c.right-390,224),ACCENT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if(g_posterFile[0]) {
            ABM_D2D_Round(&g_canvas,380,244,590,550,12,RGB(7,12,20));
            ABM_D2D_DrawImage(&g_canvas,386,250,584,544);
        } else {
            ABM_D2D_GradientRound(&g_canvas,380,244,590,550,12,RGB(24,59,69),RGB(12,26,40));
            ABM_D2D_Text(&g_canvas,L"ABM",MakeRect(390,330,580,410),MUTED,ABM_D2D_TEXT_HEADING,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        int tx=612,tw=c.right-tx-42;
        ABM_D2D_Text(&g_canvas,L"OVERVIEW",MakeRect(tx,244,tx+tw,270),ACCENT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        ABM_D2D_Text(&g_canvas,g_overview[0]?g_overview:(m.overview[0]?m.overview:L"No synopsis supplied by TMDB."),MakeRect(tx,274,tx+tw,420),TEXT,ABM_D2D_TEXT_BODY,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_END_ELLIPSIS);
        ABM_D2D_Text(&g_canvas,L"GENRES",MakeRect(tx,432,tx+tw,454),ACCENT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        ABM_D2D_Text(&g_canvas,g_genres[0]?g_genres:L"Not available",MakeRect(tx,456,tx+tw,480),TEXT,ABM_D2D_TEXT_BODY,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        ABM_D2D_Text(&g_canvas,L"CAST",MakeRect(tx,490,tx+tw,512),ACCENT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        ABM_D2D_Text(&g_canvas,g_cast[0]?g_cast:L"Not available",MakeRect(tx,514,tx+tw,556),TEXT,ABM_D2D_TEXT_BODY,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_END_ELLIPSIS);
        if(g_director[0])ABM_D2D_Text(&g_canvas,g_director,MakeRect(tx,566,tx+tw,590),MUTED,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    } else {
        ABM_D2D_Text(&g_canvas,L"Select a title to explore its details",MakeRect(380,170,c.right-40,215),TEXT,ABM_D2D_TEXT_HEADING,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        ABM_D2D_Text(&g_canvas,L"Search by movie or television title. Select a result to load its synopsis, artwork, genres and credits. ABM will offer playback only when a matching local file is found in your current library.",MakeRect(380,224,c.right-60,340),MUTED,ABM_D2D_TEXT_BODY,DT_LEFT|DT_TOP|DT_WORDBREAK);
    }
    ABM_D2D_End(&g_canvas);EndPaint(w,&ps);
}
static void SearchNow() {
    wchar_t query[256]={};GetWindowTextW(g_edit,query,_countof(query));
    if(!query[0]){SetStatus(L"Enter a movie or television title to search.");return;}
    g_itemCount=0;g_selected=-1;g_localMatch=-1;g_detailsReady=FALSE;g_posterFile[0]=0;ABM_D2D_ClearImage(&g_canvas);
    SendMessageW(g_results,LB_RESETCONTENT,0,0);SetStatus(L"Searching TMDB…");StartWork(1,-1,query);InvalidateRect(g_window,nullptr,FALSE);
}
static LRESULT CALLBACK HomeWndProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE:
        g_window=w;g_darkBrush=CreateSolidBrush(PANEL2);
        g_edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,28,83,700,36,w,(HMENU)HT_EDIT,GetModuleHandleW(nullptr),nullptr);
        g_results=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_VSCROLL|LBS_NOINTEGRALHEIGHT,28,180,300,450,w,(HMENU)HT_RESULTS,GetModuleHandleW(nullptr),nullptr);
        g_status=CreateWindowExW(0,L"STATIC",g_statusText,WS_CHILD|WS_VISIBLE|SS_LEFT,28,650,700,24,w,nullptr,GetModuleHandleW(nullptr),nullptr);
        g_play=CreateWindowExW(0,L"BUTTON",L"Play local match",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,800,650,148,42,w,(HMENU)HT_PLAY,GetModuleHandleW(nullptr),nullptr);
        CreateWindowExW(0,L"BUTTON",L"Search",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,800,83,122,36,w,(HMENU)HT_SEARCH,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(g_edit,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        SendMessageW(g_results,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        SendMessageW(g_results,LB_SETITEMHEIGHT,0,42);
        SetWindowTheme(g_edit,L"DarkMode_Explorer",nullptr);SetWindowTheme(g_results,L"DarkMode_Explorer",nullptr);
        SendMessageW(g_play,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        SendMessageW(GetDlgItem(w,HT_SEARCH),WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        EnableWindow(g_play,FALSE);Layout(w);return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO *m=(MINMAXINFO*)lp;if(m){m->ptMinTrackSize.x=900;m->ptMinTrackSize.y=720;}return 0; }
    case WM_SIZE:Layout(w);if(g_canvasReady)ABM_D2D_Resize(&g_canvas,LOWORD(lp),HIWORD(lp));InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_COMMAND:
        if(LOWORD(wp)==HT_SEARCH&&HIWORD(wp)==BN_CLICKED){SearchNow();return 0;}
        if(LOWORD(wp)==HT_EDIT&&HIWORD(wp)==EN_UPDATE)return 0;
        if(LOWORD(wp)==HT_EDIT&&HIWORD(wp)==EN_MAXTEXT)return 0;
        if(LOWORD(wp)==HT_EDIT&&HIWORD(wp)==EN_SETFOCUS)return 0;
        if(LOWORD(wp)==HT_EDIT&&HIWORD(wp)==EN_KILLFOCUS)return 0;
        if(LOWORD(wp)==HT_EDIT&&HIWORD(wp)==0)return 0;
        if(LOWORD(wp)==HT_RESULTS&&HIWORD(wp)==LBN_SELCHANGE) {
            int selected=(int)SendMessageW(g_results,LB_GETCURSEL,0,0);
            if(selected>=0&&selected<g_itemCount) {
                g_selected=selected;g_localMatch=FindLocalMatch(g_items[selected].title);
                g_overview[0]=g_year[0]=g_runtime[0]=g_genres[0]=g_cast[0]=g_director[0]=g_posterFile[0]=0;
                g_detailsReady=FALSE;ABM_D2D_ClearImage(&g_canvas);
                EnableWindow(g_play,g_localMatch>=0);
                if(g_localMatch>=0)SetLocalMatchStatus(L"Local file match · details loading");else SetStatus(L"No filename match found. Details are loading…");
                StartWork(2,selected,nullptr);InvalidateRect(w,nullptr,FALSE);
            }return 0;
        }
        if(LOWORD(wp)==HT_PLAY&&HIWORD(wp)==BN_CLICKED&&g_localMatch>=0&&g_playCallback){int index=g_localMatch;ABM_HomeTheatrePlayFn callback=g_playCallback;DestroyWindow(w);callback(index);return 0;}
        break;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp,TRANSPARENT);SetTextColor((HDC)wp,MUTED);return (LRESULT)g_darkBrush;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetBkColor((HDC)wp,PANEL2);SetTextColor((HDC)wp,TEXT);return (LRESULT)g_darkBrush;
    case WM_MEASUREITEM: { MEASUREITEMSTRUCT *m=(MEASUREITEMSTRUCT*)lp;if(m&&m->CtlID==HT_RESULTS){m->itemHeight=42;return TRUE;}break; }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)lp;
        if(d&&d->CtlID==HT_RESULTS&&d->itemID!=(UINT)-1) {
            MovieItem &m=g_items[d->itemID];BOOL selected=(d->itemState&ODS_SELECTED)!=0;
            HBRUSH b=CreateSolidBrush(selected?RGB(25,73,77):PANEL);FillRect(d->hDC,&d->rcItem,b);DeleteObject(b);
            SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,selected?TEXT:MUTED);
            wchar_t line[320];StringCchPrintfW(line,_countof(line),L"%s   ·   %s",m.title,m.type==1?L"MOVIE":L"TV");
            RECT r=d->rcItem;InflateRect(&r,-10,-2);DrawTextW(d->hDC,line,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            if(d->itemState&ODS_FOCUS)DrawFocusRect(d->hDC,&d->rcItem);return TRUE;
        }break;
    }
    case HT_SEARCH_DONE: {
        Result *r=(Result*)lp;if(!r)return 0;g_busy=FALSE;
        if(r->ok){g_itemCount=r->count;for(int i=0;i<g_itemCount;i++){g_items[i]=r->items[i];wchar_t line[320];StringCchPrintfW(line,_countof(line),L"%s   ·   %s",g_items[i].title,g_items[i].type==1?L"MOVIE":L"TV");SendMessageW(g_results,LB_ADDSTRING,0,(LPARAM)line);}SetStatus(L"Select a result to load its cinematic details.");}
        else SetStatus(r->error[0]?r->error:L"No results found.");
        EnableWindow(g_edit,TRUE);EnableWindow(GetDlgItem(w,HT_SEARCH),TRUE);EnableWindow(g_results,TRUE);
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
            if(g_posterFile[0])ABM_D2D_LoadImage(&g_canvas,g_posterFile);
            if(g_localMatch>=0)SetLocalMatchStatus(L"Ready to play local file");else SetStatus(L"Metadata ready. No matching local file was found in the current library.");
        } else SetStatus(r->error[0]?r->error:L"Details unavailable.");
        EnableWindow(g_edit,TRUE);EnableWindow(GetDlgItem(w,HT_SEARCH),TRUE);
        delete r;InvalidateRect(w,nullptr,FALSE);return 0;
    }
    case WM_PAINT:Draw(w);return 0;
    case WM_CLOSE:DestroyWindow(w);return 0;
    case WM_DESTROY:
        if(g_canvasReady){ABM_D2D_Destroy(&g_canvas);g_canvasReady=FALSE;}
        if(g_darkBrush){DeleteObject(g_darkBrush);g_darkBrush=nullptr;}
        g_window=g_edit=g_results=g_play=g_status=nullptr;g_itemCount=0;g_selected=-1;g_localMatch=-1;
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
    if(!registered){WNDCLASSEXW wc={};wc.cbSize=sizeof(wc);wc.lpfnWndProc=HomeWndProc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);wc.lpszClassName=L"ABMAVPlayerHomeTheatre";if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;registered=TRUE;}
    RECT wr={0,0,1080,720};DWORD style=WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN;
    AdjustWindowRectEx(&wr,style,FALSE,0);RECT o={};GetWindowRect(owner,&o);
    int x=o.left+(o.right-o.left-(wr.right-wr.left))/2,y=o.top+(o.bottom-o.top-(wr.bottom-wr.top))/2;
    g_window=CreateWindowExW(WS_EX_APPWINDOW,L"ABMAVPlayerHomeTheatre",L"ABM AV Player — Home Theatre",style,x,y,wr.right-wr.left,wr.bottom-wr.top,owner,nullptr,instance,nullptr);
    if(!g_window)return;
    BOOL dark=TRUE;DwmSetWindowAttribute(g_window, DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    g_canvasReady=ABM_D2D_Init(&g_canvas,g_window);
    EnableWindow(owner,FALSE);
    ShowWindow(g_window,SW_SHOW);UpdateWindow(g_window);
}

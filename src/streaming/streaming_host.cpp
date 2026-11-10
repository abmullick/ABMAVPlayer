#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shellapi.h>
#include <wininet.h>
#include <cwctype>
#include <cstdarg>
#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <wchar.h>
#include <string>
#include <dwmapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include <wrl.h>
#include <WebView2.h>
#include <gdiplus.h>
#include "streaming_host.h"
#include "dlna_cast.h"
#include "youtube_dial.h"
#include "cast_v2.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
const wchar_t *kClassName=L"ABMAVPlayer.Streaming";
constexpr UINT WM_APP_WEBVIEW_RECOVER=WM_APP+20;
constexpr UINT WM_APP_DIAL_APPS_READY=WM_APP+21;
constexpr UINT WM_APP_DIAL_APP_LAUNCH=WM_APP+22;
constexpr UINT WM_APP_CAST_VIDEO_RESULT=WM_APP+23;
constexpr int kMaxWebViewRecoveries=3;
constexpr int kServiceCount=6;
const wchar_t *kServices[kServiceCount]={
    L"YouTube",L"Netflix",L"Prime Video",L"JioHotstar",L"Sony LIV",L"ZEE5"
};
const wchar_t *kUrls[kServiceCount]={
    L"https://www.youtube.com/",
    L"https://www.netflix.com/",
    L"https://www.primevideo.com/",
    L"https://www.hotstar.com/in",
    L"https://www.sonyliv.com/",
    L"https://www.zee5.com/"
};
HWND g_window=nullptr;
HWND g_mainWindow=nullptr;
HWND g_infoWindow=nullptr;
Gdiplus::Bitmap *g_infoPortrait=nullptr;
bool g_infoAbout=false;
ComPtr<ICoreWebView2Environment> g_environment;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webView;
RECT g_serviceRects[kServiceCount]{};
HWND g_devicePanel=nullptr,g_deviceList=nullptr,g_deviceTitle=nullptr,g_deviceHint=nullptr;
HWND g_deviceRefresh=nullptr,g_deviceSelect=nullptr,g_deviceClose=nullptr,g_deviceApps=nullptr,g_deviceAppCombo=nullptr,g_deviceLaunch=nullptr,g_deviceAppStatus=nullptr,g_deviceAppStop=nullptr;
bool g_devicePanelVisible=false;
std::atomic<bool> g_appProbeRunning{false};
struct StreamAppChoice { std::wstring label; std::wstring dialApp; bool castV2=false; std::wstring castAppId; };
struct DialAppProbePayload { std::wstring report; std::vector<StreamAppChoice> choices; };
std::vector<StreamAppChoice> g_streamAppChoices;
int g_streamSelectedIndex=-1;
bool g_streamSelectedIsDlna=false;
struct StreamReceiverRow { int castIndex; int dlnaIndex; int dialIndex; int castV2Index; };
int g_streamSelectedDialIndex=-1;
int g_streamSelectedCastV2Index=-1;
std::wstring g_streamSelectedReceiverName;
std::vector<StreamReceiverRow> g_streamReceiverRows;
bool IsStreamReceiverReady(){
    if(g_streamSelectedIndex<0 || (size_t)g_streamSelectedIndex>=g_streamReceiverRows.size())return false;
    const StreamReceiverRow &row=g_streamReceiverRows[(size_t)g_streamSelectedIndex];
    return row.castIndex>=0 || row.dlnaIndex>=0 || row.dialIndex>=0 || row.castV2Index>=0;
}

constexpr UINT ID_STREAM_DEVICE_TIMER=0x7A10;
constexpr int ID_STREAM_DEVICE_LIST=0x7A11,ID_STREAM_DEVICE_REFRESH=0x7A12,ID_STREAM_DEVICE_SELECT=0x7A13,ID_STREAM_DEVICE_CLOSE=0x7A14,ID_STREAM_DEVICE_APPS=0x7A15,ID_STREAM_DEVICE_APP_COMBO=0x7A16,ID_STREAM_DEVICE_LAUNCH=0x7A17,ID_STREAM_DEVICE_APP_STATUS=0x7A18,ID_STREAM_DEVICE_APP_STOP=0x7A19;
RECT g_backRect{},g_forwardRect{},g_reloadRect{},g_homeRect{},g_fullscreenRect{},g_externalRect{},g_helpRect{},g_aboutRect{},g_castRect{},g_castVideoRect{};
int g_activeService=-1;
int g_hoverService=-1;
int g_hoverControl=-1;
bool g_trackingMouseLeave=false;
bool g_comInitialized=false;
EventRegistrationToken g_navigationToken{};
EventRegistrationToken g_navigationStartingToken{};
EventRegistrationToken g_popupToken{};
EventRegistrationToken g_fullscreenToken{};
EventRegistrationToken g_processFailedToken{};
int g_webViewRecoveryAttempts=0;
bool g_webViewRecoveryPending=false;
bool g_webFullscreen=false;
LONG_PTR g_savedWindowStyle=0;
LONG_PTR g_savedWindowExStyle=0;
RECT g_savedWindowRect{};
wchar_t g_status[256]=L"Preparing secure browser profile…";
ULONG_PTR g_gdiplusToken=0;
Gdiplus::Bitmap *g_serviceLogos[kServiceCount]{};
const wchar_t *kLogoFiles[kServiceCount]={L"youtube.png",L"netflix.png",L"primevideo.png",L"jiohotstar.png",L"sonyliv.png",L"zee5.png"};
struct CachedTextFont{int height;int weight;HFONT font;};
CachedTextFont g_textFonts[24]{};

/* Shared diagnostics for the Streaming Centre and GoCast/DIAL investigation. */
void StreamingDiag(const wchar_t *format,...){
    wchar_t message[1200]{};
    va_list args;va_start(args,format);
    StringCchVPrintfW(message,_countof(message),format,args);
    va_end(args);
    SYSTEMTIME now{};GetLocalTime(&now);
    wchar_t line[1500]{};
    StringCchPrintfW(line,_countof(line),L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] Streaming: %s\r\n",
        now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,message);
    wchar_t path[MAX_PATH]{};
    if(GetTempPathW(_countof(path),path)){
        StringCchCatW(path,_countof(path),L"ABMAVPlayer-GoCast-diagnostics.log");
        HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE){
            char utf8[6000]{};int bytes=WideCharToMultiByte(CP_UTF8,0,line,-1,utf8,(int)sizeof(utf8),nullptr,nullptr);
            if(bytes>1){DWORD written=0;WriteFile(file,utf8,(DWORD)(bytes-1),&written,nullptr);}
            CloseHandle(file);
        }
    }
    OutputDebugStringW(line);
}
/* Used by the native device panel before its implementation below. */
void SetStatus(const wchar_t *message);

void LoadServiceLogos(){
    if(!g_gdiplusToken){
        Gdiplus::GdiplusStartupInput input;
        if(Gdiplus::GdiplusStartup(&g_gdiplusToken,&input,nullptr)!=Gdiplus::Ok){
            StringCchCopyW(g_status,_countof(g_status),L"Could not initialise PNG image support.");
            return;
        }
    }
    wchar_t exePath[MAX_PATH]{};
    DWORD length=GetModuleFileNameW(nullptr,exePath,_countof(exePath));
    if(!length||length>=_countof(exePath))return;
    wchar_t *slash=wcsrchr(exePath,L'\\');
    if(!slash)return;
    *(slash+1)=L'\0';

    wchar_t assetPath[MAX_PATH]{};
    wchar_t fallbackPath[MAX_PATH]{};
    int loaded=0;
    for(int i=0;i<kServiceCount;i++){
        if(FAILED(StringCchPrintfW(assetPath,_countof(assetPath),L"%sassets\\%s",exePath,kLogoFiles[i])))continue;
        Gdiplus::Bitmap *bitmap=Gdiplus::Bitmap::FromFile(assetPath,FALSE);
        if(!bitmap||bitmap->GetLastStatus()!=Gdiplus::Ok){
            delete bitmap;
            /* Development builds run from build\\; also check the repository assets folder. */
            if(SUCCEEDED(StringCchPrintfW(fallbackPath,_countof(fallbackPath),L"%s..\\assets\\%s",exePath,kLogoFiles[i]))){
                bitmap=Gdiplus::Bitmap::FromFile(fallbackPath,FALSE);
            }
        }
        if(bitmap&&bitmap->GetLastStatus()==Gdiplus::Ok){
            g_serviceLogos[i]=bitmap;
            ++loaded;
        }else{
            delete bitmap;
        }
    }
    if(loaded==kServiceCount)StringCchCopyW(g_status,_countof(g_status),L"All six streaming service logos loaded.");
    else{
        wchar_t message[128];
        StringCchPrintfW(message,_countof(message),L"Loaded %d of 6 service logos. Check the assets folder beside the application.",loaded);
        StringCchCopyW(g_status,_countof(g_status),message);
    }
}
void ReleaseServiceLogos(){
    for(auto &logo:g_serviceLogos){delete logo;logo=nullptr;}
    if(g_gdiplusToken){Gdiplus::GdiplusShutdown(g_gdiplusToken);g_gdiplusToken=0;}
}

COLORREF RGBc(int r,int g,int b){return RGB(r,g,b);}
void FillRectColor(HDC dc,const RECT &r,COLORREF color){
    HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);
}
void Rounded(HDC dc,const RECT &r,COLORREF color,int radius){
    HBRUSH b=CreateSolidBrush(color);HPEN p=CreatePen(PS_SOLID,1,color);
    HGDIOBJ ob=SelectObject(dc,b),op=SelectObject(dc,p);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
    SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(b);DeleteObject(p);
}
HFONT GetCachedTextFont(int height,int weight){
    for(auto &entry:g_textFonts)if(entry.font&&entry.height==height&&entry.weight==weight)return entry.font;
    for(auto &entry:g_textFonts)if(!entry.font){
        entry.height=height;entry.weight=weight;
        entry.font=CreateFontW(-height,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        return entry.font;
    }
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}
void ReleaseCachedTextFonts(){
    for(auto &entry:g_textFonts){if(entry.font)DeleteObject(entry.font);entry={};}
}
void Text(HDC dc,const wchar_t *value,RECT r,COLORREF color,int height,int weight,UINT flags){
    HFONT f=GetCachedTextFont(height,weight);
    HGDIOBJ old=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    DrawTextW(dc,value,-1,&r,flags);SelectObject(dc,old);
}

void DrawDeviceButton(const DRAWITEMSTRUCT *item,const wchar_t *caption,bool primary){
    if(!item||!item->hwndItem)return;
    HDC dc=item->hDC;RECT r=item->rcItem;
    bool pressed=(item->itemState&ODS_SELECTED)!=0;
    bool focused=(item->itemState&ODS_FOCUS)!=0;
    COLORREF fill=primary?(pressed?RGBc(25,112,101):RGBc(24,91,82)):
        (pressed?RGBc(35,75,94):RGBc(20,42,60));
    COLORREF edge=primary?RGBc(65,190,165):RGBc(48,91,113);
    Rounded(dc,r,fill,9);
    HPEN pen=CreatePen(PS_SOLID,focused?2:1,focused?RGBc(99,225,205):edge);
    HGDIOBJ op=SelectObject(dc,pen),ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,r.left,r.top,r.right,r.bottom,9,9);
    SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(pen);
    InflateRect(&r,-5,-2);
    Text(dc,caption,r,RGBc(229,244,250),11,600,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}
void DrawDeviceListItem(const DRAWITEMSTRUCT *item){
    if(!item||item->itemID==(UINT)-1)return;
    wchar_t name[256]{};
    SendMessageW(item->hwndItem,LB_GETTEXT,item->itemID,(LPARAM)name);
    bool selected=(item->itemState&ODS_SELECTED)!=0;
    RECT r=item->rcItem;
    FillRectColor(item->hDC,r,selected?RGBc(29,91,103):RGBc(15,31,47));
    RECT textRect=r;InflateRect(&textRect,-9,0);
    Text(item->hDC,name,textRect,selected?RGBc(245,255,255):RGBc(207,225,237),11,selected?700:500,
        DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if(item->itemState&ODS_FOCUS){
        HPEN pen=CreatePen(PS_SOLID,1,RGBc(75,190,184));
        HGDIOBJ old=SelectObject(item->hDC,pen),brush=SelectObject(item->hDC,GetStockObject(HOLLOW_BRUSH));
        Rectangle(item->hDC,r.left+1,r.top+1,r.right-1,r.bottom-1);
        SelectObject(item->hDC,brush);SelectObject(item->hDC,old);DeleteObject(pen);
    }
}

void LayoutDevicePanel(){
    if(!g_window||!g_devicePanel)return;
    RECT rc{};GetClientRect(g_window,&rc);
    int x=rc.right-530;if(x<12)x=12;
    int y=64;
    /* The app selector was laid out below the old 430px panel and was
       clipped/covered by the browser. Keep the full app-control row inside. */
    SetWindowPos(g_devicePanel,HWND_TOP,x,y,516,520,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    /* Controls share the Streaming Centre parent so WM_COMMAND is handled here. */
    SetWindowPos(g_deviceTitle,nullptr,x+18,y+12,480,24,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(g_deviceHint,nullptr,x+18,y+38,480,118,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(g_deviceList,nullptr,x+18,y+166,310,160,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(g_deviceRefresh,nullptr,x+342,y+166,156,34,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(g_deviceSelect,nullptr,x+342,y+209,156,34,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(g_deviceApps,nullptr,x+342,y+252,156,34,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(g_deviceAppCombo,nullptr,x+18,y+340,310,160,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(g_deviceLaunch,nullptr,x+342,y+340,156,34,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(g_deviceClose,nullptr,x+342,y+383,156,34,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(g_deviceAppStatus,nullptr,x+342,y+426,156,34,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(g_deviceAppStop,nullptr,x+342,y+469,156,34,SWP_NOZORDER|SWP_NOACTIVATE);
}
bool SameReceiverName(const std::wstring &left,const std::wstring &right){
    auto canonical=[](std::wstring value){
        size_t suffix=value.find_last_of(L'(');
        if(suffix!=std::wstring::npos && !value.empty() && value.back()==L')' && suffix>0)
            value.erase(suffix);
        while(!value.empty() && iswspace(value.back()))value.pop_back();
        while(!value.empty() && iswspace(value.front()))value.erase(value.begin());
        return value;
    };
    const std::wstring a=canonical(left),b=canonical(right);
    return !a.empty() && !b.empty() && _wcsicmp(a.c_str(),b.c_str())==0;
}
void RefreshDevicePanel(bool force){
    if(!g_deviceList)return;
    int oldSelection=(int)SendMessageW(g_deviceList,LB_GETCURSEL,0,0);
    unsigned int castCount=ABM_Cast_StreamDeviceCount();
    unsigned int dlnaCount=ABM_Dlna_DeviceCount();
    std::vector<std::wstring> castNames(castCount),dlnaNames(dlnaCount);
    std::vector<StreamReceiverRow> rows;
    rows.reserve(castCount+dlnaCount+ABM_CastV2_DeviceCount());
    for(unsigned int i=0;i<castCount;i++){
        wchar_t name[256]=L"Google Cast receiver";
        if(ABM_Cast_StreamDeviceName(i,name,_countof(name))){
            castNames[i]=name;
            rows.push_back({(int)i,-1,-1,-1});
        }
    }
    for(unsigned int i=0;i<dlnaCount;i++){
        wchar_t name[256]=L"DLNA receiver";
        if(!ABM_Dlna_DeviceName(i,name,_countof(name)))continue;
        dlnaNames[i]=name;
        auto match=std::find_if(rows.begin(),rows.end(),[&](const StreamReceiverRow &row){
            return row.castIndex>=0 && SameReceiverName(castNames[(size_t)row.castIndex],name);
        });
        if(match!=rows.end())match->dlnaIndex=(int)i;
        else rows.push_back({-1,(int)i,-1,-1});
    }
    unsigned int dialCount=ABM_Dial_DeviceCount();
    std::vector<std::wstring> dialNames(dialCount);
    for(unsigned int i=0;i<dialCount;i++){
        wchar_t name[256]=L"YouTube receiver";
        if(!ABM_Dial_DeviceName(i,name,_countof(name)))continue;
        dialNames[i]=name;
        auto match=std::find_if(rows.begin(),rows.end(),[&](const StreamReceiverRow &row){
            if(row.castIndex>=0)return SameReceiverName(castNames[(size_t)row.castIndex],name);
            if(row.dlnaIndex>=0)return SameReceiverName(dlnaNames[(size_t)row.dlnaIndex],name);
            return false;
        });
        if(match!=rows.end())match->dialIndex=(int)i;
        else rows.push_back({-1,-1,(int)i,-1});
    }
    unsigned int castV2Count=ABM_CastV2_DeviceCount();
    std::vector<std::wstring> castV2Names(castV2Count);
    for(unsigned int i=0;i<castV2Count;i++){
        wchar_t name[256]=L"Google Cast TV";
        if(!ABM_CastV2_DeviceName(i,name,_countof(name)))continue;
        castV2Names[i]=name;
        auto match=std::find_if(rows.begin(),rows.end(),[&](const StreamReceiverRow &row){
            if(row.castIndex>=0)return SameReceiverName(castNames[(size_t)row.castIndex],name);
            if(row.dlnaIndex>=0)return SameReceiverName(dlnaNames[(size_t)row.dlnaIndex],name);
            if(row.dialIndex>=0)return SameReceiverName(dialNames[(size_t)row.dialIndex],name);
            return false;
        });
        if(match!=rows.end())match->castV2Index=(int)i;
        else rows.push_back({-1,-1,-1,(int)i});
    }
    /* Keep the chosen TV bound to its identity, not to discovery-array
       indices. Cast/DIAL discovery can append or reorder receivers while the
       panel is open; stale indices otherwise launch against the wrong TV. */
    if(!g_streamSelectedReceiverName.empty()){
        auto selectedRow=std::find_if(rows.begin(),rows.end(),[&](const StreamReceiverRow &row){
            if(row.castIndex>=0 && SameReceiverName(castNames[(size_t)row.castIndex],g_streamSelectedReceiverName))return true;
            if(row.dlnaIndex>=0 && SameReceiverName(dlnaNames[(size_t)row.dlnaIndex],g_streamSelectedReceiverName))return true;
            if(row.dialIndex>=0 && SameReceiverName(dialNames[(size_t)row.dialIndex],g_streamSelectedReceiverName))return true;
            if(row.castV2Index>=0 && SameReceiverName(castV2Names[(size_t)row.castV2Index],g_streamSelectedReceiverName))return true;
            return false;
        });
        if(selectedRow!=rows.end()){
            g_streamSelectedIndex=(int)std::distance(rows.begin(),selectedRow);
            g_streamSelectedDialIndex=selectedRow->dialIndex;
            g_streamSelectedCastV2Index=selectedRow->castV2Index;
            if(selectedRow->castV2Index>=0)ABM_CastV2_SelectDevice((unsigned int)selectedRow->castV2Index);
        }else{
            g_streamSelectedIndex=-1;
            g_streamSelectedDialIndex=-1;
            g_streamSelectedCastV2Index=-1;
            g_streamSelectedIsDlna=false;
        }
    }
    if(!force && rows.size()==g_streamReceiverRows.size() &&
       std::equal(rows.begin(),rows.end(),g_streamReceiverRows.begin(),
           [](const StreamReceiverRow &a,const StreamReceiverRow &b){
               return a.castIndex==b.castIndex && a.dlnaIndex==b.dlnaIndex && a.dialIndex==b.dialIndex && a.castV2Index==b.castV2Index;
           })) return;
    g_streamReceiverRows=rows;
    SendMessageW(g_deviceList,LB_RESETCONTENT,0,0);
    for(const auto &row:rows){
        std::wstring name;
        if(row.castIndex>=0)name=castNames[(size_t)row.castIndex];
        else if(row.dlnaIndex>=0)name=dlnaNames[(size_t)row.dlnaIndex];
        else if(row.dialIndex>=0)name=dialNames[(size_t)row.dialIndex];
        else if(row.castV2Index>=0)name=castV2Names[(size_t)row.castV2Index];
        std::wstring label=row.castIndex>=0&&row.dlnaIndex>=0?L"[Google Cast + DLNA] ":
            row.castIndex>=0?L"[Google Cast] ":row.dlnaIndex>=0?L"[DLNA] ":
            row.dialIndex>=0?L"[YouTube DIAL] ":L"[Google Cast V2] ";
        if(row.dialIndex>=0 && (row.castIndex>=0||row.dlnaIndex>=0||row.castV2Index>=0))label=L"[YouTube + "+label.substr(1);
        else if(row.castV2Index>=0 && row.castIndex>=0)label=L"[Google Cast + V2] ";
        label+=name;
        SendMessageW(g_deviceList,LB_ADDSTRING,0,(LPARAM)label.c_str());
    }
    if(!rows.empty()){
        int pick=g_streamSelectedIndex>=0&&g_streamSelectedIndex<(int)rows.size()?g_streamSelectedIndex:
                 (oldSelection>=0&&oldSelection<(int)rows.size()?oldSelection:0);
        SendMessageW(g_deviceList,LB_SETCURSEL,pick,0);
        SetWindowTextW(g_deviceHint,L"Each TV appears once; available casting protocols are shown beside its name.");
    }else{
        SetWindowTextW(g_deviceHint,L"Searching for TVs… keep this panel open for discovery.");
    }
}
void CreateDevicePanel(HWND parent){
    if(g_devicePanel)return;
    g_devicePanel=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"",
        WS_CHILD|SS_OWNERDRAW,0,0,516,430,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!g_devicePanel)return;
    g_deviceTitle=CreateWindowExW(0,L"STATIC",L"CAST TO A DEVICE",
        WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,10,10,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    g_deviceHint=CreateWindowExW(0,L"STATIC",L"Searching for compatible receivers…",
        WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,10,10,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    g_deviceList=CreateWindowExW(0,L"LISTBOX",L"",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_LIST,GetModuleHandleW(nullptr),nullptr);
    g_deviceRefresh=CreateWindowExW(0,L"BUTTON",L"Refresh devices",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_REFRESH,GetModuleHandleW(nullptr),nullptr);
    g_deviceSelect=CreateWindowExW(0,L"BUTTON",L"Use selected TV",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_SELECT,GetModuleHandleW(nullptr),nullptr);
    g_deviceApps=CreateWindowExW(0,L"BUTTON",L"Check TV apps",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_APPS,GetModuleHandleW(nullptr),nullptr);
    g_deviceAppCombo=CreateWindowExW(0,L"LISTBOX",L"",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_TABSTOP,0,0,10,160,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_APP_COMBO,GetModuleHandleW(nullptr),nullptr);
    g_deviceLaunch=CreateWindowExW(0,L"BUTTON",L"Launch app",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_LAUNCH,GetModuleHandleW(nullptr),nullptr);
    g_deviceAppStatus=CreateWindowExW(0,L"BUTTON",L"Refresh app status",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_APP_STATUS,GetModuleHandleW(nullptr),nullptr);
    g_deviceAppStop=CreateWindowExW(0,L"BUTTON",L"Stop TV app",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_APP_STOP,GetModuleHandleW(nullptr),nullptr);
    g_deviceClose=CreateWindowExW(0,L"BUTTON",L"Close",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_CLOSE,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(g_deviceTitle,WM_SETFONT,(WPARAM)GetCachedTextFont(12,700),TRUE);
    SendMessageW(g_deviceHint,WM_SETFONT,(WPARAM)GetCachedTextFont(10,500),TRUE);
    SendMessageW(g_deviceList,WM_SETFONT,(WPARAM)GetCachedTextFont(11,500),TRUE);
    SendMessageW(g_deviceAppCombo,WM_SETFONT,(WPARAM)GetCachedTextFont(11,500),TRUE);
    SendMessageW(g_deviceList,LB_SETITEMHEIGHT,0,27);
    SendMessageW(g_deviceAppCombo,LB_SETITEMHEIGHT,0,30);
    EnableWindow(g_deviceAppCombo,FALSE);EnableWindow(g_deviceLaunch,FALSE);
    HWND controls[]={g_deviceTitle,g_deviceHint,g_deviceList,g_deviceRefresh,g_deviceSelect,g_deviceApps,g_deviceAppCombo,g_deviceLaunch,g_deviceClose,g_deviceAppStatus,g_deviceAppStop};
    ShowWindow(g_devicePanel,SW_HIDE);
    for(HWND control:controls)if(control)ShowWindow(control,SW_HIDE);
}
void ShowDevicePanel(){
    if(!g_window)return;
    if(!g_devicePanel)CreateDevicePanel(g_window);
    if(!g_devicePanel)return;
    g_devicePanelVisible=true;
    ShowWindow(g_devicePanel,SW_SHOW);
    HWND controls[]={g_deviceTitle,g_deviceHint,g_deviceList,g_deviceRefresh,g_deviceSelect,g_deviceApps,g_deviceAppCombo,g_deviceLaunch,g_deviceClose,g_deviceAppStatus,g_deviceAppStop};
    for(HWND control:controls)if(control)ShowWindow(control,SW_SHOW);
    LayoutDevicePanel();
    ABM_Cast_StreamDiscoveryStart();
    ABM_Dlna_StartDiscovery();
    ABM_Dial_StartDiscovery();
    ABM_CastV2_StartDiscovery();
    RefreshDevicePanel(true);
    SetTimer(g_window,ID_STREAM_DEVICE_TIMER,800,nullptr);
    SetStatus(L"Searching for TVs. Select a receiver in this panel; local-media casting remains separate.");
    InvalidateRect(g_window,nullptr,FALSE);
}
void HideDevicePanel(){
    g_devicePanelVisible=false;
    KillTimer(g_window,ID_STREAM_DEVICE_TIMER);
    HWND controls[]={g_devicePanel,g_deviceTitle,g_deviceHint,g_deviceList,g_deviceRefresh,g_deviceSelect,g_deviceApps,g_deviceAppCombo,g_deviceLaunch,g_deviceClose,g_deviceAppStatus,g_deviceAppStop};
    for(HWND control:controls)if(control)ShowWindow(control,SW_HIDE);
}

void SetStatus(const wchar_t *message){
    StringCchCopyW(g_status,_countof(g_status),message?message:L"");
    if(g_window)InvalidateRect(g_window,nullptr,FALSE);
}
void LayoutBrowser(){
    if(!g_controller||!g_window)return;
    RECT rc{};GetClientRect(g_window,&rc);
    /* Normal mode reserves the native chrome; true provider fullscreen uses the whole monitor. */
    RECT bounds=g_webFullscreen?RECT{0,0,rc.right,rc.bottom}:RECT{0,260,rc.right,rc.bottom-34};
    if(bounds.bottom<bounds.top)bounds.bottom=bounds.top;
    g_controller->put_Bounds(bounds);
}
void SetPlatformFullscreen(BOOL enabled){
    if(!g_window||!IsWindow(g_window)||g_webFullscreen==(enabled!=FALSE))return;
    if(enabled){
        GetWindowRect(g_window,&g_savedWindowRect);
        g_savedWindowStyle=GetWindowLongPtrW(g_window,GWL_STYLE);
        g_savedWindowExStyle=GetWindowLongPtrW(g_window,GWL_EXSTYLE);
        HMONITOR monitor=MonitorFromWindow(g_window,MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};info.cbSize=sizeof(info);
        if(GetMonitorInfoW(monitor,&info)){
            g_webFullscreen=true;
            SetWindowLongPtrW(g_window,GWL_STYLE,(g_savedWindowStyle&~WS_OVERLAPPEDWINDOW)|WS_POPUP);
            SetWindowLongPtrW(g_window,GWL_EXSTYLE,g_savedWindowExStyle&~(WS_EX_WINDOWEDGE|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE|WS_EX_DLGMODALFRAME));
            SetWindowPos(g_window,HWND_TOP,info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right-info.rcMonitor.left,info.rcMonitor.bottom-info.rcMonitor.top,SWP_FRAMECHANGED|SWP_NOOWNERZORDER|SWP_NOACTIVATE);
            LayoutBrowser();InvalidateRect(g_window,nullptr,FALSE);
        }
    }else{
        g_webFullscreen=false;
        SetWindowLongPtrW(g_window,GWL_STYLE,g_savedWindowStyle);
        SetWindowLongPtrW(g_window,GWL_EXSTYLE,g_savedWindowExStyle);
        SetWindowPos(g_window,nullptr,g_savedWindowRect.left,g_savedWindowRect.top,g_savedWindowRect.right-g_savedWindowRect.left,g_savedWindowRect.bottom-g_savedWindowRect.top,SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOACTIVATE);
        LayoutBrowser();InvalidateRect(g_window,nullptr,FALSE);
    }
}
/* Web content is untrusted. Permit HTTPS navigation only and reject
   loopback/private destinations; streaming providers can still use their
   HTTPS login, CDN and playback subdomains. */
bool IsSafeStreamingUrl(const wchar_t *url) {
    if(!url || !url[0] || wcslen(url)>8192)return false;
    URL_COMPONENTSW parts{};parts.dwStructSize=sizeof(parts);
    parts.dwSchemeLength=(DWORD)-1;parts.dwHostNameLength=(DWORD)-1;
    parts.dwUserNameLength=(DWORD)-1;parts.dwPasswordLength=(DWORD)-1;
    if(!InternetCrackUrlW(url,0,0,&parts) || parts.nScheme!=INTERNET_SCHEME_HTTPS ||
       !parts.dwHostNameLength || parts.dwUserNameLength || parts.dwPasswordLength)
        return false;
    std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
    for(auto &ch:host)ch=(wchar_t)towlower(ch);
    if(host==L"localhost" || host==L"localhost.localdomain" ||
       host.size()>=10 && host.compare(host.size()-10,10,L".localhost")==0)
        return false;
    IN_ADDR ipv4{};
    if(InetPtonW(AF_INET,host.c_str(),&ipv4)==1) {
        const BYTE *a=(const BYTE*)&ipv4.S_un.S_addr;
        if(a[0]==0 || a[0]==10 || a[0]==127 || (a[0]==169&&a[1]==254) ||
           (a[0]==172&&a[1]>=16&&a[1]<=31) || (a[0]==192&&a[1]==168) || a[0]>=224)
            return false;
    }
    IN6_ADDR ipv6{};
    if(InetPtonW(AF_INET6,host.c_str(),&ipv6)==1) {
        const BYTE *a=(const BYTE*)&ipv6;
        static const BYTE loopback[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        if((a[0]&0xfe)==0xfc || (a[0]==0xfe&&(a[1]&0xc0)==0x80) ||
           memcmp(a,loopback,16)==0 ||
           (a[0]==0&&a[1]==0&&a[2]==0&&a[3]==0&&a[4]==0&&a[5]==0&&a[6]==0&&a[7]==0&&a[8]==0&&a[9]==0&&a[10]==0xff&&a[11]==0xff))
            return false;
    }
    return true;
}
void OpenExternalUrl(const wchar_t *url) {
    if(IsSafeStreamingUrl(url))ShellExecuteW(nullptr,L"open",url,nullptr,nullptr,SW_SHOWNORMAL);
    else SetStatus(L"Blocked an unsafe or non-HTTPS destination.");
}

void NavigateHistory(BOOL forward){
    if(!g_webView){SetStatus(L"Streaming browser is still starting.");return;}
    BOOL available=FALSE;
    HRESULT hr=forward?g_webView->get_CanGoForward(&available):g_webView->get_CanGoBack(&available);
    if(FAILED(hr)||!available){
        SetStatus(forward?L"No forward page in browser history.":L"No previous page in browser history.");
        return;
    }
    if(forward)g_webView->GoForward();else g_webView->GoBack();
    SetStatus(forward?L"Going forward…":L"Going back…");
}

const wchar_t *kInfoClassName=L"ABMAVPlayer.StreamingInfo";
void LoadInfoPortrait(){
    delete g_infoPortrait;g_infoPortrait=nullptr;
    wchar_t exePath[MAX_PATH]{},candidate[MAX_PATH]{};
    DWORD n=GetModuleFileNameW(nullptr,exePath,_countof(exePath));
    if(!n||n>=_countof(exePath))return;
    wchar_t *slash=wcsrchr(exePath,L'\\');if(!slash)return;
    *slash=L'\0';
    auto tryPath=[](const wchar_t *file)->bool {
        Gdiplus::Bitmap *bmp=Gdiplus::Bitmap::FromFile(file,FALSE);
        if(!bmp)return false;
        if(bmp->GetLastStatus()!=Gdiplus::Ok){delete bmp;return false;}
        g_infoPortrait=bmp;return true;
    };
    if(SUCCEEDED(StringCchPrintfW(candidate,_countof(candidate),L"%s\\assets\\owner_portrait.bmp",exePath))&&tryPath(candidate))return;
    slash=wcsrchr(exePath,L'\\');
    if(slash&&slash>exePath){
        *slash=L'\0';
        if(SUCCEEDED(StringCchPrintfW(candidate,_countof(candidate),L"%s\\assets\\owner_portrait.bmp",exePath))&&tryPath(candidate))return;
    }
    DWORD cwd=GetCurrentDirectoryW(_countof(candidate),candidate);
    if(cwd&&cwd<_countof(candidate)){
        wchar_t path[MAX_PATH]{};
        if(SUCCEEDED(StringCchPrintfW(path,_countof(path),L"%s\\assets\\owner_portrait.bmp",candidate)))tryPath(path);
    }
}
LRESULT CALLBACK StreamingInfoProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:
        LoadInfoPortrait();
        return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{
        PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT rc{};GetClientRect(hwnd,&rc);
        FillRectColor(dc,rc,RGBc(8,17,30));
        RECT accent={0,0,rc.right,7};FillRectColor(dc,accent,RGBc(43,178,181));
        RECT brand={28,20,rc.right-28,54};
        Text(dc,L"ABM AV PLAYER",brand,RGBc(244,249,255),21,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT subtitle={29,49,rc.right-28,72};
        Text(dc,g_infoAbout?L"THE CREATOR":L"STREAMING CENTRE  •  USER GUIDE",subtitle,RGBc(91,224,218),12,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if(g_infoAbout){
            RECT photoFrame={28,94,286,362};
            Rounded(dc,photoFrame,RGBc(18,38,55),10);
            if(g_infoPortrait){
                Gdiplus::Graphics graphics(dc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(g_infoPortrait,Gdiplus::Rect(36,102,242,242));
            }else{
                RECT fallback={36,102,278,344};
                Rounded(dc,fallback,RGBc(27,143,145),8);
                Text(dc,L"ABM",fallback,RGBc(245,255,255),42,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
            RECT product={322,98,rc.right-28,138};
            Text(dc,L"ABM AV Player",product,RGBc(255,255,255),25,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT creatorLabel={322,157,rc.right-28,180};
            Text(dc,L"THE CREATOR",creatorLabel,RGBc(68,204,209),11,600,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT creator={322,187,rc.right-28,220};
            Text(dc,L"Abhisek Basu Mullick",creator,RGBc(255,255,255),19,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT role={322,227,rc.right-28,254};
            Text(dc,L"Founder & Developer",role,RGBc(232,241,250),15,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            RECT contactLabel={322,275,rc.right-28,298};
            Text(dc,L"CONTACT",contactLabel,RGBc(68,204,209),11,600,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT email={322,306,rc.right-28,333};
            Text(dc,L"abmullick@gmail.com",email,RGBc(246,250,255),15,600,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT description={322,347,rc.right-28,397};
            Text(dc,L"A modern desktop media player built around\nLibVLC, Direct2D and DirectWrite.",description,RGBc(226,238,249),14,400,DT_LEFT|DT_TOP|DT_WORDBREAK);
            RECT version={322,410,rc.right-28,436};
            Text(dc,L"Version 1.0.1   |   © 2026 Abhisek Basu Mullick",version,RGBc(174,198,219),12,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT divider={28,455,rc.right-28,456};FillRectColor(dc,divider,RGBc(35,62,82));
            RECT tributeTitle={32,470,rc.right-32,495};
            Text(dc,L"IN TRIBUTE TO DENNIS RITCHIE",tributeTitle,RGBc(68,204,209),11,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT tribute={32,501,rc.right-32,555};
            Text(dc,L"My homage to Dennis Ritchie, creator of the C programming language and co-creator of Unix. His ideas and work helped shape modern computing and generations of computer science.",tribute,RGBc(235,243,250),14,400,DT_LEFT|DT_TOP|DT_WORDBREAK);
            RECT stackTitle={32,575,rc.right-32,600};
            Text(dc,L"BUILT WITH",stackTitle,RGBc(68,204,209),11,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT stack={32,606,rc.right-32,640};
            Text(dc,L"C  •  C++  •  Win32 API  •  LibVLC  •  Direct2D  •  DirectWrite  •  Microsoft WebView2",stack,RGBc(221,233,245),13,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        }else{
            const int left=32,right=rc.right-32;
            RECT intro={left,91,right,125};
            Text(dc,L"Everything you need to use the ABM Streaming Centre.",intro,RGBc(238,245,252),16,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            const wchar_t *heads[6]={
                L"1. CHOOSE A SERVICE",L"2. SIGN IN & BROWSER PROFILE",L"3. NAVIGATION CONTROLS",
                L"4. FULLSCREEN & EXTERNAL BROWSER",L"5. FIND TVs & SELECT A RECEIVER",L"6. CAST VIDEO & TROUBLESHOOT"
            };
            const wchar_t *body[6]={
                L"Choose YouTube, Netflix, Prime Video, JioHotstar, Sony LIV or ZEE5 from the service cards. Each opens the provider's official website in the embedded browser.",
                L"Sign in directly on the provider's website with your own account. Supported cookies and sessions persist in the current Windows profile at %LOCALAPPDATA%\\ABMAVPlayer\\WebView2. The player does not collect or proxy passwords.",
                L"Back and Forward navigate browser history. Reload refreshes the current page; Home returns to the service start page. Previous/Next are intended for supported YouTube playlist navigation.",
                L"Use the Fullscreen toolbar control, F11 or the provider's own video control. Press Esc to leave fullscreen. Open in browser launches the current page in your default desktop browser.",
                L"Choose Find TVs, select a receiver and click Use selected TV. Check TV apps performs read-only Google Cast YouTube availability and DIAL app-endpoint checks; only confirmed launch targets appear in the app selector.",
                L"Select a confirmed app and click Launch app to open it on the TV; use the remote to choose content. Cast video supports YouTube handoff and directly addressable media URLs. Desktop Prime Video title handoff is not supported."
            };
            int y=140;
            for(int i=0;i<6;i++){
                RECT h={left,y,right,y+21};
                Text(dc,heads[i],h,RGBc(91,231,220),13,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
                RECT b={left,y+22,right,y+70};
                Text(dc,body[i],b,RGBc(235,242,250),14,400,DT_LEFT|DT_TOP|DT_WORDBREAK);
                y+=88;
            }
            RECT note={left,rc.bottom-62,right,rc.bottom-18};
            Text(dc,L"Streaming compatibility, DRM, subscriptions and protected playback are controlled by each provider. ABM AV Player does not host content or bypass provider protections.",note,RGBc(190,210,228),12,400,DT_LEFT|DT_TOP|DT_WORDBREAK);
        }
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_NCDESTROY:
        delete g_infoPortrait;g_infoPortrait=nullptr;g_infoWindow=nullptr;
        break;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void ShowStreamingInfo(bool about){
    if(g_infoWindow&&IsWindow(g_infoWindow)){
        if(g_infoAbout==about){ShowWindow(g_infoWindow,SW_RESTORE);SetForegroundWindow(g_infoWindow);return;}
        DestroyWindow(g_infoWindow);
    }
    g_infoAbout=about;
    HINSTANCE instance=GetModuleHandleW(nullptr);
    static bool registered=false;
    if(!registered){
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=StreamingInfoProc;wc.hInstance=instance;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,0,0,LR_DEFAULTSIZE);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);wc.lpszClassName=kInfoClassName;
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
        registered=true;
    }
    int width=about?900:900,height=about?760:760;
    RECT owner{};if(g_window)GetWindowRect(g_window,&owner);
    int x=owner.left+(owner.right-owner.left-width)/2;
    int y=owner.top+(owner.bottom-owner.top-height)/2;
    const wchar_t *caption=about?L"About ABM AV Player":L"ABM AV Player - Streaming Help";
    g_infoWindow=CreateWindowExW(WS_EX_DLGMODALFRAME,kInfoClassName,caption,
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,x,y,width,height,g_window,nullptr,instance,nullptr);
    if(!g_infoWindow)return;
    BOOL dark=TRUE;DwmSetWindowAttribute(g_infoWindow,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    COLORREF captionColor=RGB(10,15,25),borderColor=RGB(46,70,104);
    DwmSetWindowAttribute(g_infoWindow,DWMWA_CAPTION_COLOR,&captionColor,sizeof(captionColor));
    DwmSetWindowAttribute(g_infoWindow,DWMWA_BORDER_COLOR,&borderColor,sizeof(borderColor));
    ShowWindow(g_infoWindow,SW_SHOW);UpdateWindow(g_infoWindow);SetForegroundWindow(g_infoWindow);
}
void ShowStreamingHelp(){ShowStreamingInfo(false);}
void ShowStreamingAbout(){ShowStreamingInfo(true);}
void RequestProviderFullscreen(){
    if(!g_webView||!g_controller){SetStatus(L"Streaming browser is still starting.");return;}
    if(g_webFullscreen){
        g_webView->ExecuteScript(L"if(document.fullscreenElement){document.exitFullscreen();}",nullptr);
        SetPlatformFullscreen(FALSE);
        return;
    }
    /* Prefer the active HTML video element. WebView2's fullscreen event
       changes the top-level window to the monitor-sized popup. */
    HRESULT hr=g_webView->ExecuteScript(
        L"(()=>{const v=document.querySelector('video');if(v){if(v.requestFullscreen){v.requestFullscreen();return 'requested';}if(v.webkitEnterFullscreen){v.webkitEnterFullscreen();return 'requested';}}const e=document.querySelector('[data-fullscreen],.ytp-fullscreen-button');if(e){e.click();return 'clicked';}return 'unavailable';})()",
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [](HRESULT result,LPCWSTR value)->HRESULT{
                if(SUCCEEDED(result)&&value&&wcsstr(value,L"requested"))SetStatus(L"Entering fullscreen… Press Esc to exit.");
                else if(SUCCEEDED(result)&&value&&wcsstr(value,L"clicked"))SetStatus(L"Requesting provider fullscreen… Press Esc to exit.");
                else SetStatus(L"Use the streaming service's own fullscreen control if this page does not expose one.");
                return S_OK;
            }).Get());
    if(FAILED(hr))SetStatus(L"Could not request fullscreen from the current page.");
}

void CastCurrentBrowserVideo(){
    StreamingDiag(L"Cast video clicked; selectedRow=%d dialIndex=%d castV2Index=%d dlnaOnly=%d",
        g_streamSelectedIndex,g_streamSelectedDialIndex,g_streamSelectedCastV2Index,g_streamSelectedIsDlna?1:0);
    if(!g_webView){StreamingDiag(L"Cast aborted: WebView2 is null");SetStatus(L"Streaming browser is still starting.");return;}
    if(g_streamSelectedIndex<0){
        StreamingDiag(L"Cast aborted: no selected receiver");
        ShowDevicePanel();
        SetStatus(L"Select a receiver in this Streaming Centre panel first. The main player will not be opened.");
        return;
    }
    SetStatus(L"Checking the current service for a supported TV handoff…");
    HRESULT hr=g_webView->ExecuteScript(
        L"(()=>{const host=location.hostname.toLowerCase().replace(/^www\\./,'');if(host==='primevideo.com'||host.endsWith('.primevideo.com'))return 'PRIMEVIDEO';if(host==='netflix.com'||host.endsWith('.netflix.com'))return 'NETFLIX';if(host==='zee5.com'||host.endsWith('.zee5.com'))return 'ZEE5';if(host==='sonyliv.com'||host.endsWith('.sonyliv.com'))return 'SONYLIV';const valid=s=>typeof s==='string'&&/^[A-Za-z0-9_-]{11}$/.test(s);const ids=[];const addUrl=raw=>{try{const u=new URL(raw,location.href);const h=u.hostname.toLowerCase().replace(/^www\\./,'');if(h==='youtu.be'){ids.push(u.pathname.split('/').filter(Boolean)[0]||'');return;}if(h==='youtube.com'||h.endsWith('.youtube.com')||h==='youtube-nocookie.com'||h.endsWith('.youtube-nocookie.com')){ids.push(u.searchParams.get('v')||'');const m=u.pathname.match(/^\\/(?:shorts|embed|live|v)\\/([A-Za-z0-9_-]{11})(?:[/?]|$)/);if(m)ids.push(m[1]);}}catch(e){}};addUrl(location.href);const canonical=document.querySelector('link[rel=canonical]');if(canonical)addUrl(canonical.href);const og=document.querySelector('meta[property=\\'og:url\\']');if(og)addUrl(og.content);const flexy=document.querySelector('ytd-watch-flexy');if(flexy)ids.push(flexy.getAttribute('video-id')||'');try{ids.push(window.ytInitialPlayerResponse?.videoDetails?.videoId||'');}catch(e){}try{ids.push(window.ytplayer?.config?.args?.video_id||'');}catch(e){}const id=ids.find(valid);if(id)return 'YT:'+id;const vs=[...document.querySelectorAll('video')].filter(v=>v.currentSrc&&v.readyState>=2&&!/^blob:|^data:/i.test(v.currentSrc));if(!vs.length)return 'NONE';vs.sort((a,b)=>(b.videoWidth*b.videoHeight)-(a.videoWidth*a.videoHeight));return 'URL:'+encodeURIComponent(vs[0].currentSrc);})()",
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [](HRESULT result,LPCWSTR value)->HRESULT{
                StreamingDiag(L"Page inspection callback HRESULT=0x%08X valuePresent=%d",(unsigned)result,value?1:0);
                if(FAILED(result)||!value||value[0]!=L'"'){
                    SetStatus(L"Could not inspect the current page for a playable video.");return S_OK;
                }
                std::wstring payload=value+1;size_t quote=payload.find(L'"');if(quote!=std::wstring::npos)payload.resize(quote);
                StreamingDiag(L"Page inspection classification=%s",
                    payload==L"PRIMEVIDEO"?L"PRIMEVIDEO":payload.rfind(L"YT:",0)==0?L"YOUTUBE":
                    payload.rfind(L"URL:",0)==0?L"DIRECT_URL":L"NO_VIDEO");
                if(payload==L"NETFLIX"){
                    SetStatus(L"Netflix desktop-browser title casting is not supported. Launch Netflix on the TV, then use its remote or the official Netflix mobile app to control playback. DRM-protected browser playback is not relayed.");
                    return S_OK;
                }
                if(payload==L"ZEE5"||payload==L"SONYLIV"){
                    const int dialIndex=g_streamSelectedDialIndex;
                    const std::wstring service=payload==L"ZEE5"?L"ZEE5":L"SonyLIV";
                    if(dialIndex<0){
                        SetStatus((service+L" desktop title handoff is not integrated. This TV has no DIAL app-control endpoint; use the provider's official mobile Cast button or open its TV app.").c_str());
                        return S_OK;
                    }
                    SetStatus((L"Opening "+service+L" on the selected TV… The desktop browser's title/session is not transferred.").c_str());
                    const HWND target=g_window;
                    try{
                        std::thread([target,dialIndex,service](){
                            const wchar_t *aliases[3]={
                                service==L"ZEE5"?L"ZEE5":L"SonyLIV",
                                service==L"ZEE5"?L"Zee5":L"SonyLiv",
                                service==L"ZEE5"?L"zee5":L"sonyliv"
                            };
                            std::wstring message;
                            bool attempted=false,launched=false;
                            for(const wchar_t *app:aliases){
                                int capability=ABM_Dial_QueryApplication((unsigned int)dialIndex,app);
                                if(capability==1||capability==2||capability==3){
                                    attempted=true;
                                    int code=ABM_Dial_LaunchApplication((unsigned int)dialIndex,app);
                                    if(code==1){
                                        launched=true;
                                        message=service+L" launch request accepted by the TV. Choose the title with the TV remote. ABM does not transfer the desktop browser's provider session or protected video; use the official mobile Cast button for title casting.";
                                    }else{
                                        message=L"The TV recognises "+service+L" through DIAL but rejected the launch request. Open the app from the TV home screen.";
                                    }
                                    break;
                                }
                            }
                            if(!attempted)message=L"The selected TV does not expose "+service+L" through any supported DIAL app-name variant. Open it from the Android/Google TV home screen. Title handoff from this desktop browser is not implemented.";
                            auto *result=new std::wstring(std::move(message));
                            if(!IsWindow(target)||!PostMessageW(target,WM_APP_CAST_VIDEO_RESULT,0,(LPARAM)result))delete result;
                            StreamingDiag(L"%s browser Cast action DIAL launch=%d",service.c_str(),launched?1:0);
                        }).detach();
                    }catch(...){SetStatus((L"Could not start the "+service+L" TV-app launch request.").c_str());}
                    return S_OK;
                }
                if(payload==L"PRIMEVIDEO"){
                    /* Prime Video's desktop WebView does not expose Amazon's
                       mobile sender/session handoff. Make the main Cast action
                       useful and explicit: open the TV app, but never claim
                       that the browser title was transferred. */
                    const int castIndex=g_streamSelectedCastV2Index;
                    const int dialIndex=g_streamSelectedDialIndex;
                    const HWND target=g_window;
                    if(castIndex>=0){
                        SetStatus(L"Opening Prime Video on the selected TV… Title transfer from the desktop browser is not supported.");
                        try{
                            std::thread([target,castIndex](){
                                std::wstring message;
                                if(ABM_CastV2_LaunchApplication((unsigned int)castIndex,L"17608BC8"))
                                    message=L"Prime Video opened on the selected TV. Choose the title using the TV remote. ABM cannot transfer the desktop browser's Amazon session or protected video; use Prime Video's official mobile Cast button for phone-to-TV playback.";
                                else
                                    message=L"Could not open Prime Video on the selected TV. Refresh Find TVs, select the receiver and check that the experimental Prime Video app endpoint is available.";
                                auto *result=new std::wstring(std::move(message));
                                if(!IsWindow(target)||!PostMessageW(target,WM_APP_CAST_VIDEO_RESULT,0,(LPARAM)result))delete result;
                            }).detach();
                        }catch(...){SetStatus(L"Could not start the Prime Video TV-app launch request.");}
                    }else if(dialIndex>=0){
                        SetStatus(L"This TV exposes DIAL but not the experimental Google Cast Prime Video endpoint. Prime Video title handoff from the desktop browser is not supported; use the official mobile app.");
                    }else{
                        SetStatus(L"The selected TV has no supported Prime Video app-launch endpoint. Reopen Find TVs and check TV apps. Desktop-browser title transfer is not supported.");
                    }
                    return S_OK;
                }
                if(payload.rfind(L"YT:",0)==0){
                    const std::wstring id=payload.substr(3);
                    const int castIndex=g_streamSelectedCastV2Index;
                    const int dialIndex=g_streamSelectedDialIndex;
                    const bool dlnaOnly=g_streamSelectedIsDlna;
                    const HWND target=g_window;
                    if(castIndex<0&&dialIndex<0){
                        SetStatus(dlnaOnly?
                            L"This TV is selected through DLNA only. YouTube needs Google Cast or DIAL app control.":
                            L"The selected TV has no usable YouTube app-control endpoint. Reopen Find TVs and select the receiver again.");
                        return S_OK;
                    }
                    SetStatus(L"Connecting to the selected TV and transferring the YouTube video…");
                    try{
                        std::thread([target,id,castIndex,dialIndex](){
                            std::wstring message;
                            if(castIndex>=0){
                                StreamingDiag(L"Starting asynchronous Google Cast YouTube handoff videoId=%s receiverIndex=%d",id.c_str(),castIndex);
                                if(ABM_CastV2_LaunchYouTube(id.c_str()))
                                    message=L"YouTube video handoff completed. Check the TV for playback.";
                                else{
                                    wchar_t reason[256]{};
                                    if(ABM_CastV2_LastError(reason,_countof(reason))&&reason[0])message=reason;
                                    else message=L"Google Cast could not complete the YouTube video handoff. Check the TV connection and retry.";
                                }
                            }else{
                                StreamingDiag(L"Starting asynchronous DIAL YouTube handoff videoId=%s receiverIndex=%d",id.c_str(),dialIndex);
                                if(ABM_Dial_LaunchYouTube((unsigned int)dialIndex,id.c_str()))
                                    message=L"YouTube video handoff completed. Check the TV for playback.";
                                else{
                                    wchar_t reason[256]{};
                                    if(ABM_Dial_LastLaunchError(reason,_countof(reason))&&reason[0])message=reason;
                                    else message=L"The TV did not complete the YouTube app launch or video handoff.";
                                }
                            }
                            auto *result=new std::wstring(std::move(message));
                            if(!IsWindow(target)||!PostMessageW(target,WM_APP_CAST_VIDEO_RESULT,0,(LPARAM)result))delete result;
                        }).detach();
                    }catch(...){SetStatus(L"Could not start the YouTube casting operation.");}
                    return S_OK;
                }
                if(payload.rfind(L"URL:",0)!=0){
                    SetStatus(L"No direct video URL was found. Protected or segmented browser streams cannot be handed off as ordinary URLs.");return S_OK;
                }
                const wchar_t *p=payload.c_str()+4;char encoded[32768]{};size_t out=0;
                while(*p&&out+1<sizeof(encoded)){
                    if(*p==L'%'&&p[1]&&p[2]){
                        auto hex=[](wchar_t c)->int{if(c>=L'0'&&c<=L'9')return c-L'0';if(c>=L'a'&&c<=L'f')return c-L'a'+10;if(c>=L'A'&&c<=L'F')return c-L'A'+10;return -1;};
                        int hi=hex(p[1]),lo=hex(p[2]);if(hi<0||lo<0){SetStatus(L"The browser returned an invalid video URL.");return S_OK;}
                        encoded[out++]=(char)((hi<<4)|lo);p+=3;
                    }else{if(*p>0x7f){SetStatus(L"The browser returned an invalid encoded video URL.");return S_OK;}encoded[out++]=(char)*p++;}
                }
                encoded[out]=0;wchar_t url[8192]{};
                int chars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,encoded,-1,url,(int)_countof(url));
                if(chars<=0){SetStatus(L"Could not decode the browser video URL.");return S_OK;}
                if(g_streamSelectedIsDlna){
                    if(ABM_Dlna_PlayUrl(url))SetStatus(L"DLNA receiver accepted the media URL and Play command.");
                    else SetStatus(L"DLNA receiver rejected the media URL or did not accept SetAVTransportURI/Play.");
                }else if(ABM_Cast_StreamUrl(url))
                    SetStatus(L"Google Cast request handed to the selected receiver. Waiting for receiver playback…");
                return S_OK;
            }).Get());
    if(FAILED(hr))SetStatus(L"Could not inspect the current page for a video element.");
}
void Navigate(int service){
    if(service<0||service>=kServiceCount)return;
    g_activeService=service;
    if(g_webView){
        HRESULT hr=g_webView->Navigate(kUrls[service]);
        if(FAILED(hr))SetStatus(L"Could not open this service in the embedded browser.");
        else {
            wchar_t message[160];
            StringCchPrintfW(message,_countof(message),L"Opening %s  •  Sign in with your own subscription",kServices[service]);
            SetStatus(message);
        }
    }else{
        SetStatus(L"Browser is still starting. Please try again in a moment.");
    }
    if(g_window)InvalidateRect(g_window,nullptr,FALSE);
}
void NavigateHome(){
    g_activeService=-1;
    if(g_webView){
        static const wchar_t html[]=LR"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<style>
*{box-sizing:border-box}body{margin:0;background:radial-gradient(ellipse at 50% 0%,#163b48 0,#0b1728 48%,#080f1c 100%);color:#eff6ff;font:16px 'Segoe UI',sans-serif;min-height:100vh;display:grid;place-items:center}
main{max-width:780px;padding:52px;text-align:center} .mark{width:62px;height:62px;border-radius:18px;margin:0 auto 24px;background:linear-gradient(135deg,#239e9b,#397c89);display:grid;place-items:center;font-size:28px;font-weight:700;box-shadow:0 12px 35px #02081188}
h1{font-size:34px;margin:0 0 12px;letter-spacing:-.7px}p{color:#a9bfd2;line-height:1.7;margin:0 auto 28px;max-width:650px}.tag{display:inline-block;border:1px solid #34566b;background:#102337;border-radius:999px;padding:8px 14px;color:#9bd4d0;font-size:12px;letter-spacing:1px}
</style></head><body><main><div class="mark">A</div><div class="tag">ABM  /  STREAMING CENTRE</div><h1>Your streaming universe</h1><p>Choose a service from the navigation above. Sign in directly on its official website using your own account. Your browser session is stored locally on this Windows profile.</p><p style="font-size:13px;color:#7893aa">Playback availability and protected-video support depend on each streaming provider and the installed Microsoft Edge WebView2 Runtime.</p></main></body></html>)HTML";
        g_webView->NavigateToString(html);
        SetStatus(L"Choose a streaming service above.");
    }
    if(g_window)InvalidateRect(g_window,nullptr,FALSE);
}
void PaintChrome(HWND hwnd){
    PAINTSTRUCT ps{};HDC paintDc=BeginPaint(hwnd,&ps);RECT rc{};GetClientRect(hwnd,&rc);
    if(g_webFullscreen){EndPaint(hwnd,&ps);return;}
    int width=rc.right-rc.left,height=rc.bottom-rc.top;
    HDC dc=CreateCompatibleDC(paintDc);
    HBITMAP buffer=(dc&&width>0&&height>0)?CreateCompatibleBitmap(paintDc,width,height):nullptr;
    HGDIOBJ previousBitmap=(dc&&buffer)?SelectObject(dc,buffer):nullptr;
    if(!dc||!buffer||!previousBitmap){
        if(buffer)DeleteObject(buffer);
        if(dc)DeleteDC(dc);
        dc=paintDc;buffer=nullptr;previousBitmap=nullptr;
    }
    FillRectColor(dc,rc,RGBc(7,12,21));

    /* A taller, clearly separated native chrome keeps the service selector prominent. */
    RECT header={0,0,rc.right,260};FillRectColor(dc,header,RGBc(11,24,39));
    RECT topGlow={0,0,rc.right,2};FillRectColor(dc,topGlow,RGBc(31,91,105));
    RECT accent={0,257,rc.right,260};FillRectColor(dc,accent,RGBc(48,166,161));

    RECT logo={22,13,62,53};Rounded(dc,logo,RGBc(31,143,145),12);
    Text(dc,L"A",logo,RGBc(255,255,255),25,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RECT title={76,8,306,34};
    Text(dc,L"ABM AV PLAYER",title,RGBc(244,249,255),18,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT sub={77,34,306,54};
    Text(dc,L"STREAMING CENTRE",sub,RGBc(108,193,190),10,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    g_helpRect=RECT{318,15,390,55};
    Rounded(dc,g_helpRect,g_hoverControl==5?RGBc(36,75,94):RGBc(20,42,60),9);
    Text(dc,L"Help",g_helpRect,RGBc(210,232,244),11,600,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    g_aboutRect=RECT{398,15,470,55};
    Rounded(dc,g_aboutRect,g_hoverControl==6?RGBc(36,75,94):RGBc(20,42,60),9);
    Text(dc,L"About",g_aboutRect,RGBc(210,232,244),11,600,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    g_castRect=RECT{482,15,612,55};
    const bool receiverReady=IsStreamReceiverReady();
    const bool pulseOn=receiverReady && ((GetTickCount64()/400)%2==0);
    const COLORREF castFill=receiverReady?(pulseOn?RGBc(190,35,48):RGBc(112,22,34)):
        (g_hoverControl==8?RGBc(34,112,112):RGBc(24,75,83));
    const COLORREF castEdge=receiverReady?(pulseOn?RGBc(255,105,112):RGBc(220,65,76)):
        (g_hoverControl==8?RGBc(93,225,207):RGBc(48,137,137));
    Rounded(dc,g_castRect,castFill,10);
    HPEN castPen=CreatePen(PS_SOLID,(receiverReady||g_hoverControl==8)?2:1,castEdge);
    HGDIOBJ oldCastPen=SelectObject(dc,castPen);
    HGDIOBJ oldCastBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,g_castRect.left,g_castRect.top,g_castRect.right,g_castRect.bottom,10,10);
    SelectObject(dc,oldCastBrush);SelectObject(dc,oldCastPen);DeleteObject(castPen);
    Text(dc,L"Find TVs",g_castRect,receiverReady?RGBc(255,255,255):RGBc(224,250,245),11,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    g_castVideoRect=RECT{620,15,750,55};
    Rounded(dc,g_castVideoRect,g_hoverControl==9?RGBc(35,126,111):RGBc(25,91,80),10);
    HPEN videoPen=CreatePen(PS_SOLID,g_hoverControl==9?2:1,g_hoverControl==9?RGBc(113,237,198):RGBc(57,164,132));
    HGDIOBJ oldVideoPen=SelectObject(dc,videoPen);
    HGDIOBJ oldVideoBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,g_castVideoRect.left,g_castVideoRect.top,g_castVideoRect.right,g_castVideoRect.bottom,10,10);
    SelectObject(dc,oldVideoBrush);SelectObject(dc,oldVideoPen);DeleteObject(videoPen);
    Text(dc,L"Cast video",g_castVideoRect,RGBc(226,255,241),11,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    g_externalRect=RECT{rc.right-190,15,rc.right-22,55};
    Rounded(dc,g_externalRect,g_hoverControl==7?RGBc(34,91,101):RGBc(22,49,64),10);
    Text(dc,L"Open in browser",g_externalRect,RGBc(183,235,229),11,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    /* Dedicated second-row toolbar: larger targets with both icon and readable label. */
    const int navY=68,navH=52,navW=104,navGap=10;
    const int navStart=(rc.right-(5*navW+4*navGap))/2;
    g_backRect=RECT{navStart,navY,navStart+navW,navY+navH};
    g_forwardRect=RECT{navStart+(navW+navGap),navY,navStart+2*navW+navGap,navY+navH};
    g_reloadRect=RECT{navStart+2*(navW+navGap),navY,navStart+3*navW+2*navGap,navY+navH};
    g_homeRect=RECT{navStart+3*(navW+navGap),navY,navStart+4*navW+3*navGap,navY+navH};
    g_fullscreenRect=RECT{navStart+4*(navW+navGap),navY,navStart+5*navW+4*navGap,navY+navH};
    const RECT navButtons[5]={g_backRect,g_forwardRect,g_reloadRect,g_homeRect,g_fullscreenRect};
    const wchar_t *glyphs[5]={L"‹",L"›",L"↻",L"⌂",L"⛶"};
    const wchar_t *labels[5]={L"Back",L"Forward",L"Reload",L"Home",L"Fullscreen"};
    for(int i=0;i<5;i++){
        COLORREF fill=(g_hoverControl==i)?RGBc(43,91,113):(i==4?RGBc(31,103,111):RGBc(24,52,73));
        Rounded(dc,navButtons[i],fill,12);
        HPEN navPen=CreatePen(PS_SOLID,g_hoverControl==i?2:1,
            g_hoverControl==i?RGBc(83,211,199):(i==4?RGBc(62,151,153):RGBc(43,76,99)));
        HGDIOBJ oldNavPen=SelectObject(dc,navPen);
        HGDIOBJ oldNavBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        RoundRect(dc,navButtons[i].left,navButtons[i].top,navButtons[i].right,navButtons[i].bottom,12,12);
        SelectObject(dc,oldNavBrush);SelectObject(dc,oldNavPen);DeleteObject(navPen);
        RECT glyph=navButtons[i];glyph.top+=1;glyph.bottom-=19;
        Text(dc,glyphs[i],glyph,RGBc(239,248,255),i==4?23:25,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT label=navButtons[i];label.top=navButtons[i].bottom-21;label.bottom-=3;
        Text(dc,labels[i],label,RGBc(204,226,238),10,600,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT underline={navButtons[i].left+18,navButtons[i].bottom-2,navButtons[i].right-18,navButtons[i].bottom};
        if(g_hoverControl==i)FillRectColor(dc,underline,RGBc(72,210,199));
    }

    const int serviceLeft=22,serviceRight=rc.right-22,serviceY=132,serviceH=116,gap=12;
    const int serviceW=(serviceRight-serviceLeft-(kServiceCount-1)*gap)/kServiceCount;
    for(int i=0;i<kServiceCount;i++){
        int left=serviceLeft+i*(serviceW+gap);
        g_serviceRects[i]=RECT{left,serviceY,left+serviceW,serviceY+serviceH};
        bool active=(i==g_activeService),hover=(i==g_hoverService);
        COLORREF baseFill=active?RGBc(25,91,91):RGBc(17,35,53);
        COLORREF hoverFill=active?RGBc(34,119,113):RGBc(28,54,76);
        COLORREF fill=hover?hoverFill:baseFill;
        Rounded(dc,g_serviceRects[i],fill,14);
        RECT border=g_serviceRects[i];
        COLORREF baseBorder=active?RGBc(63,201,190):RGBc(30,53,73);
        COLORREF hoverBorder=active?RGBc(104,232,213):RGBc(61,119,145);
        HPEN pen=CreatePen(PS_SOLID,active?2:1,hover?hoverBorder:baseBorder);
        HGDIOBJ oldPen=SelectObject(dc,pen);
        HGDIOBJ oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        RoundRect(dc,border.left,border.top,border.right,border.bottom,14,14);
        SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);

        if(g_serviceLogos[i]){
            UINT iw=g_serviceLogos[i]->GetWidth(),ih=g_serviceLogos[i]->GetHeight();
            if(iw&&ih){
                const int maxLogoW=(serviceW>145?112:serviceW-30),maxLogoH=76;
                float scale=(float)maxLogoW/(float)iw;
                float scaleH=(float)maxLogoH/(float)ih;
                if(scaleH<scale)scale=scaleH;
                int drawW=(int)(iw*scale),drawH=(int)(ih*scale);
                int iconX=left+(serviceW-drawW)/2;
                int iconY=serviceY+8+(maxLogoH-drawH)/2;
                Gdiplus::Graphics graphics(dc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                graphics.DrawImage(g_serviceLogos[i],Gdiplus::Rect(iconX,iconY,drawW,drawH));
            }
        }else{
            RECT missing={left+8,serviceY+18,left+serviceW-8,serviceY+82};
            Text(dc,kServices[i],missing,RGBc(154,184,205),15,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        RECT label=g_serviceRects[i];
        label.top=serviceY+88;label.bottom=serviceY+109;
        label.left+=5;label.right-=5;
        Text(dc,kServices[i],label,active?RGBc(248,255,255):RGBc(214,228,240),
            13,active||hover?700:600,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(active){
            RECT activeMark={left+serviceW/2-18,serviceY+112,left+serviceW/2+18,serviceY+114};
            FillRectColor(dc,activeMark,RGBc(89,224,205));
        }
    }
    RECT footer={0,rc.bottom-34,rc.right,rc.bottom};FillRectColor(dc,footer,RGBc(10,20,33));
    RECT line={0,rc.bottom-35,rc.right,rc.bottom-34};FillRectColor(dc,line,RGBc(39,61,79));
    const wchar_t *footerText=g_hoverControl==0?L"Go back to the previous page":
        g_hoverControl==1?L"Go forward to the next page":
        g_hoverControl==2?L"Reload the current streaming page":
        g_hoverControl==3?L"Return to the Streaming Centre home page":
        g_hoverControl==4?L"Request fullscreen from the streaming player":
        g_hoverControl==5?L"Open Streaming Centre help":
        g_hoverControl==6?L"About ABM AV Player":
        g_hoverControl==7?L"Open the current streaming service in your default browser":
        g_hoverControl==8?L"Discover TVs using the main player's Google Cast / network receiver engine":
        g_hoverControl==9?L"Send a directly addressable HTML5 video URL to the selected TV; DRM and blob streams are not supported":
        g_hoverService>=0?kServices[g_hoverService]:g_status;
    RECT status={22,rc.bottom-29,rc.right-18,rc.bottom-5};
    Text(dc,footerText,status,g_hoverControl>=0||g_hoverService>=0?RGBc(173,211,220):RGBc(133,164,188),11,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if(g_devicePanelVisible)LayoutDevicePanel();
    if(buffer){
        BitBlt(paintDc,0,0,width,height,dc,0,0,SRCCOPY);
        SelectObject(dc,previousBitmap);
        DeleteObject(buffer);
        DeleteDC(dc);
    }
    EndPaint(hwnd,&ps);
}
void OnWebViewReady(HRESULT result,ICoreWebView2Controller *controller);
void CreateWebViewController(){
    if(!g_environment||!g_window||!IsWindow(g_window))return;
    HRESULT hr=g_environment->CreateCoreWebView2Controller(g_window,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [](HRESULT controllerResult,ICoreWebView2Controller *controller)->HRESULT{
                OnWebViewReady(controllerResult,controller);
                return S_OK;
            }).Get());
    if(FAILED(hr))SetStatus(L"Could not recreate the embedded browser. Close and reopen the Streaming Centre.");
}
void OnWebViewReady(HRESULT result,ICoreWebView2Controller *controller){
    if(!g_window||!IsWindow(g_window)){if(controller)controller->Close();return;}
    if(FAILED(result)||!controller){
        SetStatus(L"WebView2 Runtime is unavailable. Install the Microsoft Edge WebView2 Evergreen Runtime.");
        return;
    }
    g_controller=controller;
    g_controller->get_CoreWebView2(&g_webView);
    if(!g_webView){
        SetStatus(L"Could not initialise the embedded browser.");
        return;
    }
    ComPtr<ICoreWebView2Settings> settings;
    if(SUCCEEDED(g_webView->get_Settings(&settings))&&settings){
        settings->put_AreDefaultContextMenusEnabled(TRUE);
        settings->put_IsZoomControlEnabled(TRUE);
        settings->put_AreDevToolsEnabled(FALSE);
    }
    g_webView->add_ContainsFullScreenElementChanged(
        Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
            [](ICoreWebView2 *sender,IUnknown*)->HRESULT{
                BOOL contains=FALSE;
                if(sender&&SUCCEEDED(sender->get_ContainsFullScreenElement(&contains)))SetPlatformFullscreen(contains);
                return S_OK;
            }).Get(),&g_fullscreenToken);
    g_webView->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs *args)->HRESULT{
                LPWSTR uri=nullptr;
                if(args&&SUCCEEDED(args->get_Uri(&uri))&&uri){
                    if(!IsSafeStreamingUrl(uri)){
                        args->put_Cancel(TRUE);
                        SetStatus(L"Blocked a non-HTTPS, local-network or otherwise unsafe page destination.");
                    }else{
                        SetStatus(L"Loading secure streaming page…");
                    }
                    CoTaskMemFree(uri);
                }else if(args){
                    args->put_Cancel(TRUE);
                    SetStatus(L"Blocked navigation because its destination could not be validated.");
                }
                return S_OK;
            }).Get(),&g_navigationStartingToken);
    g_webView->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs *args)->HRESULT{
                BOOL success=FALSE;
                COREWEBVIEW2_WEB_ERROR_STATUS error=COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                if(args){args->get_IsSuccess(&success);args->get_WebErrorStatus(&error);}
                SetStatus(success?L"Streaming page loaded. Sign in using the service's official login.":L"Page navigation failed. Check your connection or open the service in your browser.");
                if(g_window)InvalidateRect(g_window,nullptr,FALSE);
                return S_OK;
            }).Get(),&g_navigationToken);
    /* Some providers open authentication or account pages in a new window.
       Keep navigation inside the same trusted WebView rather than spawning
       an unmanaged browser surface. */
    g_webView->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
            [](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*)->HRESULT{
                if(g_window&&IsWindow(g_window)&&!g_webViewRecoveryPending){
                    g_webViewRecoveryPending=true;
                    PostMessageW(g_window,WM_APP_WEBVIEW_RECOVER,0,0);
                }
                return S_OK;
            }).Get(),&g_processFailedToken);
    g_webView->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2 *sender,ICoreWebView2NewWindowRequestedEventArgs *args)->HRESULT{
                LPWSTR target=nullptr;
                if(args)args->put_Handled(TRUE);
                if(args&&SUCCEEDED(args->get_Uri(&target))&&target){
                    if(IsSafeStreamingUrl(target)){
                        sender->Navigate(target);
                        SetStatus(L"Opening a secure sign-in or service page in the Streaming Centre…");
                    }else{
                        SetStatus(L"Blocked a pop-up with a non-HTTPS or local-network destination.");
                    }
                    CoTaskMemFree(target);
                }
                return S_OK;
            }).Get(),&g_popupToken);
    g_controller->put_IsVisible(TRUE);
    LayoutBrowser();
    NavigateHome();
}
LRESULT CALLBACK StreamingProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_APP_DIAL_APPS_READY:{
        std::unique_ptr<DialAppProbePayload> report((DialAppProbePayload*)lp);
        if(report&&g_devicePanelVisible){
            /* Detailed endpoint diagnostics go to the log, not a second
               competing on-screen app list. The selectable list below is the
               single source of truth for what can actually be launched. */
            StreamingDiag(L"%s",report->report.c_str());
            SetWindowTextW(g_deviceHint,L"Select an available TV app below, then choose Launch app.");
            g_streamAppChoices=std::move(report->choices);
            SendMessageW(g_deviceAppCombo,LB_RESETCONTENT,0,0);
            for(size_t i=0;i<g_streamAppChoices.size();++i){
                const auto &choice=g_streamAppChoices[i];
                LRESULT item=SendMessageW(g_deviceAppCombo,LB_ADDSTRING,0,(LPARAM)choice.label.c_str());
                if(item==CB_ERR||item==CB_ERRSPACE){
                    StreamingDiag(L"App selector insertion failed choiceIndex=%zu label=%s",i,choice.label.c_str());
                    continue;
                }
                SendMessageW(g_deviceAppCombo,LB_SETITEMDATA,(WPARAM)item,(LPARAM)i);
                StreamingDiag(L"App selector item=%ld choiceIndex=%zu label=%s dialApp=%s castV2=%d castAppId=%s",
                    (long)item,i,choice.label.c_str(),choice.dialApp.c_str(),choice.castV2?1:0,choice.castAppId.c_str());
            }
            /* Require an explicit user selection; never default to the first app. */
            SendMessageW(g_deviceAppCombo,LB_SETCURSEL,(WPARAM)-1,0);
            EnableWindow(g_deviceAppCombo,!g_streamAppChoices.empty());
            EnableWindow(g_deviceLaunch,!g_streamAppChoices.empty());
            SetStatus(L"TV app checks completed. Select an available app in the list and choose Launch app.");
        }
        return 0;
    }
    case WM_APP_DIAL_APP_LAUNCH:{
        std::unique_ptr<std::wstring> message((std::wstring*)lp);
        if(message){SetStatus(message->c_str());if(g_devicePanelVisible)SetWindowTextW(g_deviceHint,message->c_str());}
        return 0;
    }
    case WM_APP_CAST_VIDEO_RESULT:{
        std::unique_ptr<std::wstring> message((std::wstring*)lp);
        if(message)SetStatus(message->c_str());
        return 0;
    }
    case WM_DRAWITEM:{
        const DRAWITEMSTRUCT *item=(const DRAWITEMSTRUCT*)lp;
        if(!item)return TRUE;
        if(item->hwndItem==g_devicePanel){
            RECT r=item->rcItem;
            FillRectColor(item->hDC,r,RGBc(12,29,45));
            HPEN pen=CreatePen(PS_SOLID,1,RGBc(43,91,112));
            HGDIOBJ oldPen=SelectObject(item->hDC,pen),oldBrush=SelectObject(item->hDC,GetStockObject(HOLLOW_BRUSH));
            RoundRect(item->hDC,r.left,r.top,r.right,r.bottom,14,14);
            SelectObject(item->hDC,oldBrush);SelectObject(item->hDC,oldPen);DeleteObject(pen);
            RECT accent={r.left+16,r.top+157,r.right-16,r.top+158};FillRectColor(item->hDC,accent,RGBc(38,112,119));
            RECT appDivider={r.left+16,r.top+326,r.right-16,r.top+327};FillRectColor(item->hDC,appDivider,RGBc(38,112,119));
            return TRUE;
        }
        if(item->hwndItem==g_deviceRefresh){DrawDeviceButton(item,L"Refresh devices",false);return TRUE;}
        if(item->hwndItem==g_deviceSelect){DrawDeviceButton(item,L"Use selected TV",true);return TRUE;}
        if(item->hwndItem==g_deviceApps){DrawDeviceButton(item,L"Check TV apps",false);return TRUE;}
        if(item->hwndItem==g_deviceLaunch){DrawDeviceButton(item,L"Launch app",true);return TRUE;}
        if(item->hwndItem==g_deviceClose){DrawDeviceButton(item,L"Close",false);return TRUE;}
        if(item->hwndItem==g_deviceAppStatus){DrawDeviceButton(item,L"Refresh app status",false);return TRUE;}
        if(item->hwndItem==g_deviceAppStop){DrawDeviceButton(item,L"Stop TV app",false);return TRUE;}
        if(item->hwndItem==g_deviceList){DrawDeviceListItem(item);return TRUE;}
        if(item->hwndItem==g_deviceAppCombo){
            if(item->itemID==(UINT)-1)return TRUE;
            wchar_t label[256]{};SendMessageW(item->hwndItem,LB_GETTEXT,item->itemID,(LPARAM)label);
            const bool selected=(item->itemState&ODS_SELECTED)!=0;
            FillRectColor(item->hDC,item->rcItem,selected?RGBc(29,91,103):RGBc(15,31,47));
            RECT textRect=item->rcItem;InflateRect(&textRect,-8,0);
            Text(item->hDC,label,textRect,selected?RGBc(245,255,255):RGBc(207,225,237),10,selected?700:500,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:{
        HDC dc=(HDC)wp;HWND control=(HWND)lp;
        if(control==g_deviceTitle||control==g_deviceHint){
            SetBkMode(dc,TRANSPARENT);
            SetTextColor(dc,control==g_deviceTitle?RGBc(242,249,255):RGBc(150,190,207));
            static HBRUSH panelBrush=CreateSolidBrush(RGBc(12,29,45));
            return (LRESULT)panelBrush;
        }
        break;
    }
    case WM_CREATE:{
        g_window=hwnd;
        CreateDevicePanel(hwnd);
        SetTimer(hwnd,ID_STREAM_DEVICE_TIMER,400,nullptr);
        LoadServiceLogos();
        HRESULT comResult=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        if(SUCCEEDED(comResult))g_comInitialized=true;
        else {SetStatus(L"Could not initialise the Windows browser COM apartment.");return 0;}
        LPWSTR runtimeVersion=nullptr;
        HRESULT runtimeHr=GetAvailableCoreWebView2BrowserVersionString(nullptr,&runtimeVersion);
        if(FAILED(runtimeHr)||!runtimeVersion||!runtimeVersion[0]){
            if(runtimeVersion)CoTaskMemFree(runtimeVersion);
            SetStatus(L"Microsoft Edge WebView2 Evergreen Runtime is not installed or cannot be found. Install the official WebView2 Runtime, then reopen the Streaming Centre.");
            return 0;
        }
        CoTaskMemFree(runtimeVersion);
        g_webViewRecoveryAttempts=0;g_webViewRecoveryPending=false;
        SetStatus(L"Starting Microsoft Edge WebView2…");
        wchar_t local[MAX_PATH]{},profile[MAX_PATH]{};
        if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local))||
           FAILED(StringCchPrintfW(profile,_countof(profile),L"%s\\ABMAVPlayer\\WebView2",local))){
            SetStatus(L"Could not create the persistent browser profile path.");
            return 0;
        }
        wchar_t parent[MAX_PATH]{};
        StringCchPrintfW(parent,_countof(parent),L"%s\\ABMAVPlayer",local);
        CreateDirectoryW(parent,nullptr);CreateDirectoryW(profile,nullptr);
        HRESULT hr=CreateCoreWebView2EnvironmentWithOptions(nullptr,profile,nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [hwnd](HRESULT envResult,ICoreWebView2Environment *environment)->HRESULT{
                    if(FAILED(envResult)||!environment||!IsWindow(hwnd)){
                        SetStatus(L"WebView2 Runtime is missing or could not start.");
                        return S_OK;
                    }
                    g_environment=environment;
                    CreateWebViewController();
                    return S_OK;
                }).Get());
        if(FAILED(hr))SetStatus(L"WebView2 initialisation failed. Check the Evergreen Runtime installation.");
        return 0;
    }
    case WM_APP_WEBVIEW_RECOVER:{
        g_webViewRecoveryPending=false;
        if(!g_window||!IsWindow(g_window))return 0;
        if(g_webViewRecoveryAttempts>=kMaxWebViewRecoveries){
            SetStatus(L"The streaming browser stopped repeatedly. Close and reopen the Streaming Centre.");
            return 0;
        }
        ++g_webViewRecoveryAttempts;
        LPWSTR current=nullptr;
        std::wstring restoreUrl;
        if(g_webView&&SUCCEEDED(g_webView->get_Source(&current))&&current){
            if(IsSafeStreamingUrl(current))restoreUrl=current;
            CoTaskMemFree(current);
        }
        SetStatus(L"Recovering the streaming browser after an unexpected process failure…");
        if(g_controller)g_controller->Close();
        g_webView.Reset();g_controller.Reset();
        if(!g_environment){SetStatus(L"Browser recovery is unavailable. Reopen the Streaming Centre.");return 0;}
        HRESULT hr=g_environment->CreateCoreWebView2Controller(hwnd,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [restoreUrl](HRESULT controllerResult,ICoreWebView2Controller *controller)->HRESULT{
                    OnWebViewReady(controllerResult,controller);
                    if(SUCCEEDED(controllerResult)&&controller&&!restoreUrl.empty()){
                        controller->get_CoreWebView2(&g_webView);
                        if(g_webView)g_webView->Navigate(restoreUrl.c_str());
                    }
                    return S_OK;
                }).Get());
        if(FAILED(hr))SetStatus(L"Browser recovery could not be started. Reopen the Streaming Centre.");
        return 0;
    }
    case WM_TIMER:
        if(wp==ID_STREAM_DEVICE_TIMER){
            if(g_devicePanelVisible)RefreshDevicePanel(false);
            if(IsStreamReceiverReady())InvalidateRect(hwnd,&g_castRect,FALSE);
            return 0;
        }
        break;
    case WM_SIZE:LayoutBrowser();if(g_devicePanelVisible)LayoutDevicePanel();InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_MOUSEMOVE:{
        if(!g_trackingMouseLeave){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};if(TrackMouseEvent(&track))g_trackingMouseLeave=true;}
        POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        int service=-1,control=-1;
        for(int i=0;i<kServiceCount;i++)if(PtInRect(&g_serviceRects[i],pt)){service=i;break;}
        if(PtInRect(&g_backRect,pt))control=0;
        else if(PtInRect(&g_forwardRect,pt))control=1;
        else if(PtInRect(&g_reloadRect,pt))control=2;
        else if(PtInRect(&g_homeRect,pt))control=3;
        else if(PtInRect(&g_fullscreenRect,pt))control=4;
        else if(PtInRect(&g_helpRect,pt))control=5;
        else if(PtInRect(&g_aboutRect,pt))control=6;
        else if(PtInRect(&g_externalRect,pt))control=7;
        else if(PtInRect(&g_castRect,pt))control=8;
        else if(PtInRect(&g_castVideoRect,pt))control=9;
        if(service!=g_hoverService||control!=g_hoverControl){g_hoverService=service;g_hoverControl=control;InvalidateRect(hwnd,nullptr,FALSE);}
        SetCursor((service>=0||control>=0)?LoadCursorW(nullptr,IDC_HAND):LoadCursorW(nullptr,IDC_ARROW));
        return 0;
    }
    case WM_MOUSELEAVE:
        g_trackingMouseLeave=false;g_hoverService=-1;g_hoverControl=-1;SetCursor(LoadCursorW(nullptr,IDC_ARROW));InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_GETMINMAXINFO:{MINMAXINFO *info=(MINMAXINFO*)lp;info->ptMinTrackSize.x=960;info->ptMinTrackSize.y=720;return 0;}
    case WM_PAINT:PaintChrome(hwnd);return 0;
    case WM_LBUTTONUP:{
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        for(int i=0;i<kServiceCount;i++)if(PtInRect(&g_serviceRects[i],POINT{x,y})){Navigate(i);return 0;}
        if(PtInRect(&g_backRect,POINT{x,y})){NavigateHistory(FALSE);return 0;}
        if(PtInRect(&g_forwardRect,POINT{x,y})){NavigateHistory(TRUE);return 0;}
        if(PtInRect(&g_reloadRect,POINT{x,y})){if(g_webView)g_webView->Reload();else SetStatus(L"Browser is still starting.");return 0;}
        if(PtInRect(&g_homeRect,POINT{x,y})){NavigateHome();return 0;}
        if(PtInRect(&g_fullscreenRect,POINT{x,y})){RequestProviderFullscreen();return 0;}
        if(PtInRect(&g_helpRect,POINT{x,y})){ShowStreamingHelp();return 0;}
        if(PtInRect(&g_aboutRect,POINT{x,y})){ShowStreamingAbout();return 0;}
        if(PtInRect(&g_castRect,POINT{x,y})){
            if(g_devicePanelVisible)HideDevicePanel();else ShowDevicePanel();
            return 0;
        }
        if(PtInRect(&g_castVideoRect,POINT{x,y})){CastCurrentBrowserVideo();return 0;}
        if(PtInRect(&g_externalRect,POINT{x,y})){
            LPWSTR current=nullptr;
            if(g_webView&&SUCCEEDED(g_webView->get_Source(&current))&&current){
                OpenExternalUrl(current);
                CoTaskMemFree(current);
            }else if(g_activeService>=0)ShellExecuteW(nullptr,L"open",kUrls[g_activeService],nullptr,nullptr,SW_SHOWNORMAL);
            return 0;
        }
        return 0;
    }
    case WM_KEYDOWN:{
        const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        if(wp==VK_ESCAPE){
            if(g_webFullscreen){
                if(g_webView)g_webView->ExecuteScript(L"if(document.fullscreenElement)document.exitFullscreen();",nullptr);
                SetPlatformFullscreen(FALSE);
            }else if(g_controller)g_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            else ShowWindow(hwnd,SW_MINIMIZE);
            return 0;
        }
        if(ctrl&&wp=='L'){if(g_activeService>=0)OpenExternalUrl(kUrls[g_activeService]);return 0;}
        if(ctrl&&wp=='R'){if(g_webView)g_webView->Reload();return 0;}
        if(ctrl&&wp=='W'){ShowWindow(hwnd,SW_MINIMIZE);return 0;}
        if(wp==VK_F5){if(g_webView)g_webView->Reload();return 0;}
        if(wp==VK_BROWSER_BACK){NavigateHistory(FALSE);return 0;}
        if(wp==VK_BROWSER_FORWARD){NavigateHistory(TRUE);return 0;}
        if(wp==VK_F11){RequestProviderFullscreen();return 0;}
        break;
    }
    case WM_COMMAND:{
        switch(LOWORD(wp)){
        case ID_STREAM_DEVICE_REFRESH:
            if(g_appProbeRunning.load()){SetStatus(L"Wait for the TV application check to finish before refreshing discovery.");return 0;}
            g_streamAppChoices.clear();SendMessageW(g_deviceAppCombo,CB_RESETCONTENT,0,0);EnableWindow(g_deviceAppCombo,FALSE);EnableWindow(g_deviceLaunch,FALSE);
            ABM_Cast_StreamDiscoveryStop();ABM_Dlna_StopDiscovery();ABM_Dial_StopDiscovery();ABM_CastV2_StopDiscovery();g_streamSelectedIndex=-1;g_streamSelectedDialIndex=-1;g_streamSelectedCastV2Index=-1;g_streamSelectedIsDlna=false;g_streamSelectedReceiverName.clear();
            ABM_Cast_StreamDiscoveryStart();ABM_Dlna_StartDiscovery();ABM_Dial_StartDiscovery();ABM_CastV2_StartDiscovery();RefreshDevicePanel(true);
            SetStatus(L"Refreshing network receiver discovery…");return 0;
        case ID_STREAM_DEVICE_SELECT:{
            if(g_appProbeRunning.load()){SetStatus(L"Wait for the TV application check to finish before changing the selected receiver.");return 0;}
            LRESULT selected=SendMessageW(g_deviceList,LB_GETCURSEL,0,0);
            if(selected==LB_ERR){SetStatus(L"Select a TV from the device list first.");return 0;}
            if((size_t)selected>=g_streamReceiverRows.size()){
                SetStatus(L"That receiver is no longer available. Refresh the list.");return 0;
            }
            const StreamReceiverRow row=g_streamReceiverRows[(size_t)selected];
            g_streamSelectedReceiverName.clear();
            if(row.castIndex>=0){wchar_t n[256]{};if(ABM_Cast_StreamDeviceName((unsigned int)row.castIndex,n,_countof(n)))g_streamSelectedReceiverName=n;}
            if(g_streamSelectedReceiverName.empty()&&row.dlnaIndex>=0){wchar_t n[256]{};if(ABM_Dlna_DeviceName((unsigned int)row.dlnaIndex,n,_countof(n)))g_streamSelectedReceiverName=n;}
            if(g_streamSelectedReceiverName.empty()&&row.dialIndex>=0){wchar_t n[256]{};if(ABM_Dial_DeviceName((unsigned int)row.dialIndex,n,_countof(n)))g_streamSelectedReceiverName=n;}
            if(g_streamSelectedReceiverName.empty()&&row.castV2Index>=0){wchar_t n[256]{};if(ABM_CastV2_DeviceName((unsigned int)row.castV2Index,n,_countof(n)))g_streamSelectedReceiverName=n;}
            g_streamAppChoices.clear();SendMessageW(g_deviceAppCombo,CB_RESETCONTENT,0,0);EnableWindow(g_deviceAppCombo,FALSE);EnableWindow(g_deviceLaunch,FALSE);
            StreamingDiag(L"Receiver selection row=%d cast=%d dlna=%d dial=%d castV2=%d",(int)selected,row.castIndex,row.dlnaIndex,row.dialIndex,row.castV2Index);
            bool selectedOk=false;
            g_streamSelectedDialIndex=row.dialIndex;
            g_streamSelectedCastV2Index=row.castV2Index;
            if(row.castV2Index>=0)ABM_CastV2_SelectDevice((unsigned int)row.castV2Index);
            /* Prefer Google Cast when the same TV advertises both protocols.
               DLNA remains selectable automatically when it is the only route. */
            if(row.castIndex>=0){
                selectedOk=ABM_Cast_StreamSelectDevice((unsigned int)row.castIndex)!=FALSE;
                g_streamSelectedIsDlna=false;
            }else if(row.dlnaIndex>=0){
                selectedOk=ABM_Dlna_SelectDevice((unsigned int)row.dlnaIndex)!=FALSE;
                g_streamSelectedIsDlna=true;
            }else if(row.dialIndex>=0 || row.castV2Index>=0){
                selectedOk=true;
                g_streamSelectedIsDlna=false;
            }
            if(selectedOk){
                g_streamSelectedIndex=(int)selected;
                wchar_t name[256]=L"TV";
                SendMessageW(g_deviceList,LB_GETTEXT,selected,(LPARAM)name);
                SetStatus((std::wstring(L"Streaming Centre selected: ")+name+L". Now choose Cast video.").c_str());
                SetWindowTextW(g_deviceHint,(std::wstring(L"Selected: ")+name).c_str());
            }else {g_streamSelectedDialIndex=-1;g_streamSelectedCastV2Index=-1;SetStatus(L"That receiver is no longer available. Refresh the list.");}
            return 0;
        }
        case ID_STREAM_DEVICE_APPS:{
            if(g_streamSelectedDialIndex<0&&g_streamSelectedCastV2Index<0){
                SetStatus(L"The selected TV has no DIAL or Google Cast V2 app-control endpoint.");
                return 0;
            }
            if(g_appProbeRunning.exchange(true)){
                SetStatus(L"TV application checks are already running…");
                return 0;
            }
            const int dialIndex=g_streamSelectedDialIndex;
            const int castV2Index=g_streamSelectedCastV2Index;
            HWND target=hwnd;
            SetWindowTextW(g_deviceHint,L"Checking TV application endpoints. This is read-only; no app will be launched.");
            SetStatus(L"Checking which streaming app endpoints this TV exposes…");
            try{
                std::thread([target,dialIndex,castV2Index](){
                    auto *payload=new DialAppProbePayload();
                    std::wstring &report=payload->report;
                    report=L"TV app checks (read-only):\r\n";
                    if(castV2Index>=0){
                        int castResult=ABM_CastV2_CheckYouTubeAvailability((unsigned int)castV2Index);
                        report+=L"Cast YouTube (233637DE): ";
                        if(castResult==1){
                            report+=L"APP_AVAILABLE";
                            payload->choices.push_back({L"YouTube — Google Cast",L"",true,L"233637DE"});
                        }else if(castResult==0)report+=L"APP_UNAVAILABLE";
                        else report+=L"query failed";
                        report+=L"\r\n";
                        int primeCastResult=ABM_CastV2_CheckApplicationAvailability((unsigned int)castV2Index,L"17608BC8");
                        report+=L"Cast Prime Video (17608BC8, experimental): ";
                        if(primeCastResult==1){
                            report+=L"APP_AVAILABLE";
                            payload->choices.push_back({L"Prime Video — Google Cast (experimental)",L"",true,L"17608BC8"});
                        }else if(primeCastResult==0)report+=L"APP_UNAVAILABLE";
                        else report+=L"query failed";
                        report+=L"\r\n";
                    }else report+=L"Google Cast app checks: no Cast V2 endpoint\r\n";
                    if(dialIndex>=0){
                        /* DIAL application resource names are case-sensitive on
                           some TV firmwares. Probe common provider spellings,
                           but expose only the first endpoint the receiver
                           actually recognises. Never guess a Cast receiver ID. */
                        struct DialServiceAliases{
                            const wchar_t *label;
                            const wchar_t *names[3];
                        };
                        const DialServiceAliases services[]={
                            {L"YouTube",{L"YouTube",nullptr,nullptr}},
                            {L"Netflix",{L"Netflix",nullptr,nullptr}},
                            {L"Prime Video",{L"PrimeVideo",L"Prime Video",nullptr}},
                            {L"JioHotstar",{L"JioHotstar",L"Hotstar",nullptr}},
                            {L"SonyLIV",{L"SonyLIV",L"SonyLiv",L"sonyliv"}},
                            {L"ZEE5",{L"ZEE5",L"Zee5",L"zee5"}}
                        };
                        for(const auto &service:services){
                            bool recognised=false;
                            for(const wchar_t *app:service.names){
                                if(!app)continue;
                                int result=ABM_Dial_QueryApplication((unsigned int)dialIndex,app);
                                report+=L"DIAL ";report+=app;report+=L": ";
                                if(result==1||result==2||result==3){
                                    report+=result==1?L"recognized — running":result==2?L"recognized — stopped":L"recognized — state unknown";
                                    if(!recognised){
                                        payload->choices.push_back({std::wstring(service.label)+L" — DIAL",app,false});
                                        recognised=true;
                                    }
                                }else if(result==0)report+=L"not exposed";
                                else if(result==-1)report+=L"query failed";
                                else report+=L"unexpected response";
                                report+=L"\r\n";
                                if(recognised)break;
                            }
                        }
                        /* Keep explicit manual launch attempts visible even
                           when DIAL GET reports 404. Some receiver firmware
                           implements POST launch but does not provide a useful
                           GET state document. The launch result remains
                           authoritative; these entries are clearly labelled
                           as unverified attempts. */
                        payload->choices.push_back({L"SonyLIV — try DIAL launch",L"SonyLIV",false});
                        payload->choices.push_back({L"SonyLiv — try DIAL launch",L"SonyLiv",false});
                        payload->choices.push_back({L"ZEE5 — try DIAL launch",L"ZEE5",false});
                        payload->choices.push_back({L"Zee5 — try DIAL launch",L"Zee5",false});
                    }else report+=L"DIAL apps: no DIAL endpoint; manual SonyLIV/ZEE5 launch requires a DIAL endpoint.\r\n";
                    report+=L"DIAL 404 = this app-name variant is not exposed by DIAL; manual entries below try POST anyway.";
                    g_appProbeRunning=false;
                    if(!IsWindow(target)||!PostMessageW(target,WM_APP_DIAL_APPS_READY,0,(LPARAM)payload))delete payload;
                }).detach();
            }catch(...){
                g_appProbeRunning=false;
                SetStatus(L"Could not start the TV application check.");
            }
            return 0;
        }
        case ID_STREAM_DEVICE_LAUNCH:{
            StreamingDiag(L"Launch button clicked");
            if(g_appProbeRunning.exchange(true)){SetStatus(L"Wait for the current TV operation to finish.");return 0;}
            LRESULT selected=SendMessageW(g_deviceAppCombo,LB_GETCURSEL,0,0);
            if(selected==CB_ERR){
                g_appProbeRunning=false;SetStatus(L"Run Check TV apps and select a confirmed application first.");return 0;
            }
            LRESULT choiceIndex=SendMessageW(g_deviceAppCombo,LB_GETITEMDATA,(WPARAM)selected,0);
            if(choiceIndex==CB_ERR||choiceIndex<0||(size_t)choiceIndex>=g_streamAppChoices.size()){
                g_appProbeRunning=false;SetStatus(L"The selected app entry is invalid. Run Check TV apps again.");return 0;
            }
            if(g_streamSelectedIndex<0){g_appProbeRunning=false;SetStatus(L"Select a TV first.");return 0;}
            const StreamAppChoice choice=g_streamAppChoices[(size_t)choiceIndex];
            StreamingDiag(L"App launch selection comboIndex=%ld choiceIndex=%ld label=%s dialApp=%s castV2=%d castAppId=%s",
                (long)selected,(long)choiceIndex,choice.label.c_str(),choice.dialApp.c_str(),choice.castV2?1:0,choice.castAppId.c_str());
            const int dialIndex=g_streamSelectedDialIndex;
            const int castV2Index=g_streamSelectedCastV2Index;
            HWND target=hwnd;
            SetStatus((std::wstring(L"Requesting launch of ")+choice.label+L"…").c_str());
            try{
                std::thread([target,choice,dialIndex,castV2Index](){
                    std::wstring result;
                    if(choice.castV2){
                        StreamingDiag(L"Dispatching Google Cast launch label=%s appId=%s receiverIndex=%d",
                            choice.label.c_str(),choice.castAppId.c_str(),castV2Index);
                        bool ok=castV2Index>=0&&ABM_CastV2_LaunchApplication((unsigned int)castV2Index,choice.castAppId.c_str())!=FALSE;
                        if(!ok)result=L"Google Cast could not confirm the requested receiver app launch. Check GoCast diagnostics.";
                        else if(choice.castAppId==L"17608BC8")
                            result=L"Prime Video Cast receiver app launch confirmed. This is only an app-launch test; Amazon's title/session handoff is not yet implemented. Use Prime Video's official mobile Cast control to start a title.";
                        else
                            result=L"YouTube app launch confirmed by Google Cast. Use the TV remote to choose content.";
                    }else{
                        int code=dialIndex>=0?ABM_Dial_LaunchApplication((unsigned int)dialIndex,choice.dialApp.c_str()):-1;
                        if(code==1)result=L"The TV accepted the "+choice.dialApp+L" app launch. Use Refresh app status to check the DIAL app state or Stop TV app to close it. These are app-level controls; playback pause/resume/seek and browser-title transfer require a provider-supported sender session.";
                        else if(code==0)result=L"The TV no longer exposes the "+choice.dialApp+L" app through DIAL.";
                        else if(code==-1)result=L"Could not reach the TV's DIAL app endpoint.";
                        else result=L"The TV rejected the "+choice.dialApp+L" app launch request.";
                    }
                    g_appProbeRunning=false;
                    auto *payload=new std::wstring(std::move(result));
                    if(!IsWindow(target)||!PostMessageW(target,WM_APP_DIAL_APP_LAUNCH,0,(LPARAM)payload))delete payload;
                }).detach();
            }catch(...){g_appProbeRunning=false;SetStatus(L"Could not start the TV app launch request.");}
            return 0;
        }
        case ID_STREAM_DEVICE_APP_STATUS:
        case ID_STREAM_DEVICE_APP_STOP:{
            if(g_appProbeRunning.exchange(true)){SetStatus(L"Wait for the current TV operation to finish.");return 0;}
            LRESULT selected=SendMessageW(g_deviceAppCombo,LB_GETCURSEL,0,0);
            LRESULT choiceIndex=selected==LB_ERR?LB_ERR:SendMessageW(g_deviceAppCombo,LB_GETITEMDATA,(WPARAM)selected,0);
            if(choiceIndex==LB_ERR||choiceIndex<0||(size_t)choiceIndex>=g_streamAppChoices.size()){
                g_appProbeRunning=false;SetStatus(L"Select a TV app first.");return 0;
            }
            const StreamAppChoice choice=g_streamAppChoices[(size_t)choiceIndex];
            if(choice.castV2||choice.dialApp.empty()||g_streamSelectedDialIndex<0){
                g_appProbeRunning=false;
                SetStatus(L"This app was launched through Google Cast or has no DIAL control endpoint. DIAL app status/stop controls are unavailable; use the TV remote or provider mobile app.");
                return 0;
            }
            const int dialIndex=g_streamSelectedDialIndex;
            const bool stop=(LOWORD(wp)==ID_STREAM_DEVICE_APP_STOP);
            const HWND target=hwnd;
            SetStatus(stop?L"Sending standard DIAL stop request to the TV…":L"Checking TV application state…");
            try{
                std::thread([target,dialIndex,choice,stop](){
                    std::wstring message;
                    if(stop){
                        int code=ABM_Dial_StopApplication((unsigned int)dialIndex,choice.dialApp.c_str());
                        if(code==1)message=L"The TV accepted the DIAL stop request for "+choice.label+L". This stops the TV app; it is not a pause command.";
                        else if(code==0)message=L"The TV no longer exposes "+choice.label+L" through DIAL.";
                        else if(code==-1)message=L"Could not reach the TV's DIAL application endpoint.";
                        else message=L"The TV rejected the DIAL stop request for "+choice.label+L". Use the TV remote to close the app.";
                    }else{
                        int code=ABM_Dial_QueryApplication((unsigned int)dialIndex,choice.dialApp.c_str());
                        if(code==1)message=choice.label+L" is reported RUNNING by the TV's DIAL endpoint. Playback state (playing/paused/buffering) is not exposed by standard DIAL.";
                        else if(code==2)message=choice.label+L" is reported STOPPED by the TV's DIAL endpoint.";
                        else if(code==3)message=choice.label+L" is recognised by DIAL, but the TV did not report a known app state.";
                        else if(code==0)message=choice.label+L" is no longer exposed by the TV's DIAL endpoint.";
                        else message=L"Could not read the TV app state from its DIAL endpoint.";
                    }
                    g_appProbeRunning=false;
                    auto *payload=new std::wstring(std::move(message));
                    if(!IsWindow(target)||!PostMessageW(target,WM_APP_DIAL_APP_LAUNCH,0,(LPARAM)payload))delete payload;
                }).detach();
            }catch(...){g_appProbeRunning=false;SetStatus(L"Could not start the TV app control request.");}
            return 0;
        }
        case ID_STREAM_DEVICE_CLOSE:HideDevicePanel();return 0;
        case ID_STREAM_DEVICE_LIST:
            if(HIWORD(wp)==LBN_DBLCLK)SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(ID_STREAM_DEVICE_SELECT,0),0);
            return 0;
        case ID_STREAM_DEVICE_APP_COMBO:
            if(HIWORD(wp)==LBN_SELCHANGE){
                LRESULT appSel=SendMessageW(g_deviceAppCombo,LB_GETCURSEL,0,0);
                LRESULT appData=appSel==LB_ERR?LB_ERR:SendMessageW(g_deviceAppCombo,LB_GETITEMDATA,(WPARAM)appSel,0);
                if(appData>=0&&(size_t)appData<g_streamAppChoices.size()){
                    const auto &choice=g_streamAppChoices[(size_t)appData];
                    StreamingDiag(L"App list selection index=%ld choiceIndex=%ld label=%s appId=%s dialApp=%s",
                        (long)appSel,(long)appData,choice.label.c_str(),choice.castAppId.c_str(),choice.dialApp.c_str());
                    SetStatus((std::wstring(L"Selected TV app: ")+choice.label+L". Click Launch app.").c_str());
                }
            }
            return 0;
        }
        break;
    }
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:
        ABM_Cast_StreamDiscoveryStop();ABM_Dlna_StopDiscovery();ABM_Dial_StopDiscovery();
        KillTimer(hwnd,ID_STREAM_DEVICE_TIMER);
        HideDevicePanel();
        g_window=nullptr;
        if(g_infoWindow&&IsWindow(g_infoWindow))DestroyWindow(g_infoWindow);
        if(g_controller)g_controller->Close();
        if(g_webView)g_webView->remove_ProcessFailed(g_processFailedToken);
        if(g_webView)g_webView->remove_NavigationCompleted(g_navigationToken);
        if(g_webView)g_webView->remove_NavigationStarting(g_navigationStartingToken);
        if(g_webView)g_webView->remove_NewWindowRequested(g_popupToken);
        if(g_webView)g_webView->remove_ContainsFullScreenElementChanged(g_fullscreenToken);
        g_webView.Reset();g_controller.Reset();g_environment.Reset();
        g_webViewRecoveryPending=false;g_webViewRecoveryAttempts=0;
        ReleaseServiceLogos();
        ReleaseCachedTextFonts();
        if(g_comInitialized){CoUninitialize();g_comInitialized=false;}
        if(g_mainWindow&&IsWindow(g_mainWindow)){ShowWindow(g_mainWindow,SW_SHOW);SetForegroundWindow(g_mainWindow);}
        g_mainWindow=nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
} // namespace

extern "C" void ABM_Streaming_Show(HWND mainWindow){
    g_mainWindow=mainWindow;
    if(g_window&&IsWindow(g_window)){ShowWindow(g_window,SW_RESTORE);SetForegroundWindow(g_window);return;}
    static bool registered=false;
    HINSTANCE instance=GetModuleHandleW(nullptr);
    if(!registered){
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=StreamingProc;
        wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hIcon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,0,0,LR_DEFAULTSIZE);
        wc.hIconSm=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,16,16,0);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);wc.lpszClassName=kClassName;
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
        registered=true;
    }
    HWND window=CreateWindowExW(0,kClassName,L"ABM AV Player - Streaming Centre",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1160,820,
        nullptr,nullptr,instance,nullptr);
    if(!window){SetStatus(L"Could not create the Streaming Centre window.");return;}
    BOOL dark=TRUE;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    int corners=2;COLORREF border=RGB(46,70,104),caption=RGB(10,15,25);
    DwmSetWindowAttribute(window,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
    DwmSetWindowAttribute(window,DWMWA_BORDER_COLOR,&border,sizeof(border));
    DwmSetWindowAttribute(window,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
    ShowWindow(window,SW_SHOW);UpdateWindow(window);
}
extern "C" void ABM_Streaming_Close(void){
    g_mainWindow=nullptr;
    if(g_window&&IsWindow(g_window))DestroyWindow(g_window);
}

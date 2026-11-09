#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shellapi.h>
#include <wchar.h>
#include <string>
#include <dwmapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include <wrl.h>
#include <WebView2.h>
#include <gdiplus.h>
#include "streaming_host.h"

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
constexpr int kMaxWebViewRecoveries=3;
const wchar_t *kServices[5]={
    L"YouTube",L"Netflix",L"Prime Video",L"JioHotstar",L"Sony LIV"
};
const wchar_t *kUrls[5]={
    L"https://www.youtube.com/",
    L"https://www.netflix.com/",
    L"https://www.primevideo.com/",
    L"https://www.hotstar.com/in",
    L"https://www.sonyliv.com/"
};
HWND g_window=nullptr;
HWND g_mainWindow=nullptr;
HWND g_infoWindow=nullptr;
Gdiplus::Bitmap *g_infoPortrait=nullptr;
bool g_infoAbout=false;
ComPtr<ICoreWebView2Environment> g_environment;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webView;
RECT g_serviceRects[5]{};
RECT g_backRect{},g_forwardRect{},g_reloadRect{},g_homeRect{},g_fullscreenRect{},g_externalRect{},g_helpRect{},g_aboutRect{};
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
Gdiplus::Bitmap *g_serviceLogos[5]{};
const wchar_t *kLogoFiles[5]={L"youtube.png",L"netflix.png",L"primevideo.png",L"jiohotstar.png",L"sonyliv.png"};
struct CachedTextFont{int height;int weight;HFONT font;};
CachedTextFont g_textFonts[24]{};

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
    for(int i=0;i<5;i++){
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
    if(loaded==5)StringCchCopyW(g_status,_countof(g_status),L"All five streaming service logos loaded.");
    else{
        wchar_t message[128];
        StringCchPrintfW(message,_countof(message),L"Loaded %d of 5 service logos. Check the assets folder beside the application.",loaded);
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
        entry.font=CreateFontW(height,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Variable Text");
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
        Text(dc,g_infoAbout?L"THE CREATOR":L"STREAMING CENTRE  •  USER GUIDE",subtitle,RGBc(68,204,209),11,600,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if(g_infoAbout){
            RECT photoFrame={28,91,286,349};
            Rounded(dc,photoFrame,RGBc(18,38,55),10);
            if(g_infoPortrait){
                Gdiplus::Graphics graphics(dc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(g_infoPortrait,Gdiplus::Rect(36,99,242,242));
            }else{
                RECT fallback={36,99,278,341};
                Rounded(dc,fallback,RGBc(27,143,145),8);
                Text(dc,L"ABM",fallback,RGBc(245,255,255),42,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
            RECT product={322,96,rc.right-28,134};
            Text(dc,L"ABM AV Player",product,RGBc(246,249,255),22,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT creatorLabel={322,151,rc.right-28,173};
            Text(dc,L"THE CREATOR",creatorLabel,RGBc(68,204,209),11,600,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT creator={322,180,rc.right-28,212};
            Text(dc,L"Abhisek Basu Mullick",creator,RGBc(246,249,255),16,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT role={322,219,rc.right-28,245};
            Text(dc,L"Founder & Developer",role,RGBc(207,220,235),13,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            RECT contactLabel={322,263,rc.right-28,284};
            Text(dc,L"CONTACT",contactLabel,RGBc(68,204,209),11,600,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT email={322,287,rc.right-28,310};
            Text(dc,L"abmullick@gmail.com",email,RGBc(228,238,249),13,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT description={322,316,rc.right-28,361};
            Text(dc,L"A modern desktop media player built around\nLibVLC, Direct2D and DirectWrite.",description,RGBc(207,220,235),12,400,DT_LEFT|DT_TOP|DT_WORDBREAK);
            RECT version={322,382,rc.right-28,404};
            Text(dc,L"Version 0.6.0   |   © 2026 Abhisek Basu Mullick",version,RGBc(125,157,185),11,400,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT divider={28,423,rc.right-28,424};FillRectColor(dc,divider,RGBc(35,62,82));
            RECT tributeTitle={32,436,rc.right-32,457};
            Text(dc,L"IN TRIBUTE TO DENNIS RITCHIE",tributeTitle,RGBc(68,204,209),11,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT tribute={32,460,rc.right-32,510};
            Text(dc,L"My homage to Dennis Ritchie, creator of the C programming language and co-creator of Unix. His ideas and work helped shape modern computing and generations of computer science.",tribute,RGBc(210,222,234),12,400,DT_LEFT|DT_TOP|DT_WORDBREAK);
            RECT stackTitle={32,516,rc.right-32,537};
            Text(dc,L"BUILT WITH",stackTitle,RGBc(68,204,209),11,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT stack={32,539,rc.right-32,562};
            Text(dc,L"C  •  C++  •  Win32 API  •  LibVLC  •  Direct2D  •  DirectWrite  •  Microsoft WebView2",stack,RGBc(192,207,220),11,400,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        }else{
            const int left=32,right=rc.right-32;
            RECT intro={left,91,right,125};
            Text(dc,L"Everything you need to use the ABM Streaming Centre.",intro,RGBc(221,234,247),14,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            const wchar_t *heads[5]={L"1. CHOOSE A SERVICE",L"2. SIGN IN SECURELY",L"3. NAVIGATION & PLAYBACK",L"4. FULLSCREEN",L"5. OPEN IN BROWSER"};
            const wchar_t *body[5]={
                L"Select YouTube, Netflix, Prime Video, JioHotstar or Sony LIV from the large service cards.",
                L"Sign in on the provider's official website using your own account. Browser sessions are stored locally in your Windows profile.",
                L"Back and Forward navigate browser history. Previous and Next are intended for YouTube playlists and mixes. Reload refreshes the page; Home returns to the Streaming Centre.",
                L"Use the fullscreen button beside Home, or the provider's own fullscreen control. Fullscreen behaviour depends on the service and WebView2 support. Press Esc to exit.",
                L"Open in browser launches the current service in your default desktop browser."
            };
            int y=143;
            for(int i=0;i<5;i++){
                RECT h={left,y,right,y+22};
                Text(dc,heads[i],h,RGBc(68,204,209),11,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
                RECT b={left,y+23,right,y+59};
                Text(dc,body[i],b,RGBc(205,220,235),12,400,DT_LEFT|DT_TOP|DT_WORDBREAK);
                y+=83;
            }
            RECT note={left,rc.bottom-57,right,rc.bottom-26};
            Text(dc,L"ABM AV Player does not host streaming content or provide subscriptions. Playback, protected content and available features are controlled by each provider.",note,RGBc(125,157,185),10,400,DT_LEFT|DT_TOP|DT_WORDBREAK);
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
    int width=about?720:760,height=about?630:640;
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

void Navigate(int service){
    if(service<0||service>=5)return;
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
        RECT glyph=navButtons[i];glyph.top+=1;glyph.bottom-=19;
        Text(dc,glyphs[i],glyph,RGBc(239,248,255),i==4?23:25,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT label=navButtons[i];label.top=navButtons[i].bottom-21;label.bottom-=3;
        Text(dc,labels[i],label,RGBc(204,226,238),10,600,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT underline={navButtons[i].left+18,navButtons[i].bottom-2,navButtons[i].right-18,navButtons[i].bottom};
        if(g_hoverControl==i)FillRectColor(dc,underline,RGBc(72,210,199));
    }

    const int serviceLeft=22,serviceRight=rc.right-22,serviceY=132,serviceH=116,gap=12;
    const int serviceW=(serviceRight-serviceLeft-4*gap)/5;
    for(int i=0;i<5;i++){
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
        g_hoverService>=0?kServices[g_hoverService]:g_status;
    RECT status={22,rc.bottom-29,rc.right-18,rc.bottom-5};
    Text(dc,footerText,status,g_hoverControl>=0||g_hoverService>=0?RGBc(173,211,220):RGBc(133,164,188),11,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
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
                    SetStatus(L"Loading streaming page…");
                    CoTaskMemFree(uri);
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
                if(args&&SUCCEEDED(args->get_Uri(&target))&&target){
                    sender->Navigate(target);
                    CoTaskMemFree(target);
                    args->put_Handled(TRUE);
                }
                return S_OK;
            }).Get(),&g_popupToken);
    g_controller->put_IsVisible(TRUE);
    LayoutBrowser();
    NavigateHome();
}
LRESULT CALLBACK StreamingProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        g_window=hwnd;
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
            if(wcsncmp(current,L"https://",8)==0||wcsncmp(current,L"http://",7)==0)restoreUrl=current;
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
    case WM_SIZE:LayoutBrowser();InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_MOUSEMOVE:{
        if(!g_trackingMouseLeave){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};if(TrackMouseEvent(&track))g_trackingMouseLeave=true;}
        POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        int service=-1,control=-1;
        for(int i=0;i<5;i++)if(PtInRect(&g_serviceRects[i],pt)){service=i;break;}
        if(PtInRect(&g_backRect,pt))control=0;
        else if(PtInRect(&g_forwardRect,pt))control=1;
        else if(PtInRect(&g_reloadRect,pt))control=2;
        else if(PtInRect(&g_homeRect,pt))control=3;
        else if(PtInRect(&g_fullscreenRect,pt))control=4;
        else if(PtInRect(&g_helpRect,pt))control=5;
        else if(PtInRect(&g_aboutRect,pt))control=6;
        else if(PtInRect(&g_externalRect,pt))control=7;
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
        for(int i=0;i<5;i++)if(PtInRect(&g_serviceRects[i],POINT{x,y})){Navigate(i);return 0;}
        if(PtInRect(&g_backRect,POINT{x,y})){NavigateHistory(FALSE);return 0;}
        if(PtInRect(&g_forwardRect,POINT{x,y})){NavigateHistory(TRUE);return 0;}
        if(PtInRect(&g_reloadRect,POINT{x,y})){if(g_webView)g_webView->Reload();else SetStatus(L"Browser is still starting.");return 0;}
        if(PtInRect(&g_homeRect,POINT{x,y})){NavigateHome();return 0;}
        if(PtInRect(&g_fullscreenRect,POINT{x,y})){RequestProviderFullscreen();return 0;}
        if(PtInRect(&g_helpRect,POINT{x,y})){ShowStreamingHelp();return 0;}
        if(PtInRect(&g_aboutRect,POINT{x,y})){ShowStreamingAbout();return 0;}
        if(PtInRect(&g_externalRect,POINT{x,y})){
            LPWSTR current=nullptr;
            if(g_webView&&SUCCEEDED(g_webView->get_Source(&current))&&current){
                if(wcsncmp(current,L"https://",8)==0||wcsncmp(current,L"http://",7)==0)
                    ShellExecuteW(nullptr,L"open",current,nullptr,nullptr,SW_SHOWNORMAL);
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
        if(ctrl&&wp=='L'){if(g_activeService>=0)ShellExecuteW(nullptr,L"open",kUrls[g_activeService],nullptr,nullptr,SW_SHOWNORMAL);return 0;}
        if(ctrl&&wp=='R'){if(g_webView)g_webView->Reload();return 0;}
        if(ctrl&&wp=='W'){ShowWindow(hwnd,SW_MINIMIZE);return 0;}
        if(wp==VK_F5){if(g_webView)g_webView->Reload();return 0;}
        if(wp==VK_BROWSER_BACK){NavigateHistory(FALSE);return 0;}
        if(wp==VK_BROWSER_FORWARD){NavigateHistory(TRUE);return 0;}
        if(wp==VK_F11){RequestProviderFullscreen();return 0;}
        break;
    }
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shellapi.h>
#include <wchar.h>
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
ComPtr<ICoreWebView2Environment> g_environment;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webView;
RECT g_serviceRects[5]{};
RECT g_backRect{},g_forwardRect{},g_reloadRect{},g_homeRect{},g_fullscreenRect{},g_externalRect{},g_helpRect{},g_aboutRect{};
int g_activeService=-1;
bool g_comInitialized=false;
EventRegistrationToken g_navigationToken{};
EventRegistrationToken g_popupToken{};
EventRegistrationToken g_fullscreenToken{};
bool g_webFullscreen=false;
LONG_PTR g_savedWindowStyle=0;
LONG_PTR g_savedWindowExStyle=0;
RECT g_savedWindowRect{};
wchar_t g_status[256]=L"Preparing secure browser profile…";
ULONG_PTR g_gdiplusToken=0;
Gdiplus::Bitmap *g_serviceLogos[5]{};
const wchar_t *kLogoFiles[5]={L"youtube.png",L"netflix.png",L"primevideo.png",L"jiohotstar.png",L"sonyliv.png"};

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
void Text(HDC dc,const wchar_t *value,RECT r,COLORREF color,int height,int weight,UINT flags){
    HFONT f=CreateFontW(height,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Variable Text");
    HGDIOBJ old=SelectObject(dc,f);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    DrawTextW(dc,value,-1,&r,flags);SelectObject(dc,old);DeleteObject(f);
}
void SetStatus(const wchar_t *message){
    StringCchCopyW(g_status,_countof(g_status),message?message:L"");
    if(g_window)InvalidateRect(g_window,nullptr,FALSE);
}
void LayoutBrowser(){
    if(!g_controller||!g_window)return;
    RECT rc{};GetClientRect(g_window,&rc);
    /* Normal mode reserves the native chrome; true provider fullscreen uses the whole monitor. */
    RECT bounds=g_webFullscreen?RECT{0,0,rc.right,rc.bottom}:RECT{0,174,rc.right,rc.bottom-34};
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
void MoveVideoInPlaylist(BOOL next){
    if(!g_webView){SetStatus(L"Streaming browser is still starting.");return;}
    if(g_activeService!=0){SetStatus(L"Previous/Next video controls are currently supported on YouTube playlists and mixes.");return;}
    LPCWSTR script=next
        ? L"(()=>{const b=document.querySelector('.ytp-next-button');if(b&&!b.disabled){b.click();return 'clicked';}return 'unavailable';})()"
        : L"(()=>{const b=document.querySelector('.ytp-prev-button');if(b&&!b.disabled){b.click();return 'clicked';}return 'unavailable';})()";
    HRESULT hr=g_webView->ExecuteScript(script,Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
        [next](HRESULT result,LPCWSTR value)->HRESULT{
            if(FAILED(result)||!value){SetStatus(L"Could not control the current YouTube playlist.");return S_OK;}
            if(wcsstr(value,L"clicked"))SetStatus(next?L"Skipped to the next video.":L"Moved to the previous video.");
            else SetStatus(L"No previous/next video is available in this YouTube playlist.");
            return S_OK;
        }).Get());
    if(FAILED(hr))SetStatus(L"Could not send the playlist command to YouTube.");
}

void ShowStreamingHelp(){
    MessageBoxW(g_window,
        L"STREAMING CENTRE\n\n"
        L"1. Choose YouTube, Netflix, Prime Video, JioHotstar or Sony LIV from the service cards.\n"
        L"2. Sign in directly with your own provider account. Your session is stored in this Windows user profile.\n"
        L"3. Use Back, Forward, Reload and Home for browser navigation. Previous/Next are intended for YouTube playlists and mixes.\n"
        L"4. Select the Fullscreen button beside Home, or use the streaming site's own fullscreen control. The video should enter monitor-level fullscreen when the provider supports it. Press Esc to exit.\n"
        L"5. Use Open in browser to continue on the provider's normal desktop browser.\n\n"
        L"Playback, protected content and fullscreen behaviour are controlled by each streaming provider and may vary by service.",
        L"ABM AV Player - Streaming Help",MB_OK|MB_ICONINFORMATION);
}
void ShowStreamingAbout(){
    MessageBoxW(g_window,
        L"ABM AV PLAYER\nSTREAMING CENTRE\n\n"
        L"A dedicated WebView2 experience for accessing your streaming services from ABM AV Player.\n\n"
        L"Sign in directly with each provider. ABM AV Player does not provide streaming subscriptions or host provider content.\n\n"
        L"Created by Abhisek Basu Mullick\n"
        L"abmullick@gmail.com",
        L"About ABM AV Player",MB_OK|MB_ICONINFORMATION);
}
void RequestProviderFullscreen(){
    if(!g_webView||!g_controller){SetStatus(L"Streaming browser is still starting.");return;}
    if(g_webFullscreen){
        if(g_webView)g_webView->ExecuteScript(L"document.exitFullscreen&&document.exitFullscreen();",nullptr);
        SetPlatformFullscreen(FALSE);
        return;
    }
    /* Send the provider's standard fullscreen shortcut as real keyboard input.
       This works with the native player controls on services that bind F to fullscreen. */
    g_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    INPUT keys[2]{};
    keys[0].type=INPUT_KEYBOARD;keys[0].ki.wVk='F';
    keys[1].type=INPUT_KEYBOARD;keys[1].ki.wVk='F';keys[1].ki.dwFlags=KEYEVENTF_KEYUP;
    UINT sent=SendInput(2,keys,sizeof(INPUT));
    if(sent==2)SetStatus(L"Requesting fullscreen from the streaming player. Press Esc to exit.");
    else SetStatus(L"Could not send the fullscreen command. Use the provider's fullscreen control.");
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
    PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT rc{};GetClientRect(hwnd,&rc);
    FillRectColor(dc,rc,RGBc(7,12,21));
    if(g_webFullscreen){EndPaint(hwnd,&ps);return;}
    RECT header={0,0,rc.right,174};FillRectColor(dc,header,RGBc(13,27,43));
    RECT accent={0,171,rc.right,174};FillRectColor(dc,accent,RGBc(48,166,161));

    RECT logo={22,13,62,53};Rounded(dc,logo,RGBc(31,143,145),12);
    Text(dc,L"A",logo,RGBc(255,255,255),25,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RECT title={76,8,306,34};
    Text(dc,L"ABM AV PLAYER",title,RGBc(244,249,255),18,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT sub={77,34,306,54};
    Text(dc,L"STREAMING CENTRE",sub,RGBc(108,193,190),10,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    g_helpRect=RECT{312,15,372,55};
    Rounded(dc,g_helpRect,RGBc(20,42,60),8);
    Text(dc,L"Help",g_helpRect,RGBc(190,218,235),11,600,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    g_aboutRect=RECT{378,15,438,55};
    Rounded(dc,g_aboutRect,RGBc(20,42,60),8);
    Text(dc,L"About",g_aboutRect,RGBc(190,218,235),11,600,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    g_externalRect=RECT{rc.right-190,16,rc.right-22,50};
    Rounded(dc,g_externalRect,RGBc(22,49,64),9);
    Text(dc,L"Open in browser",g_externalRect,RGBc(160,220,216),11,600,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    /* Large navigation controls occupy their own lane, clear of the browser button. */
    int navRight=rc.right-212;
    g_fullscreenRect=RECT{navRight-48,12,navRight,58};navRight-=58;
    g_homeRect=RECT{navRight-48,12,navRight,58};navRight-=58;
    g_reloadRect=RECT{navRight-48,12,navRight,58};navRight-=58;
    g_forwardRect=RECT{navRight-58,12,navRight,58};navRight-=68;
    g_backRect=RECT{navRight-58,12,navRight,58};
    const RECT navButtons[5]={g_backRect,g_forwardRect,g_reloadRect,g_homeRect,g_fullscreenRect};
    const wchar_t *glyphs[5]={L"‹",L"›",L"↻",L"⌂",L"⛶"};
    for(int i=0;i<5;i++){
        Rounded(dc,navButtons[i],i==4?RGBc(31,103,111):RGBc(28,57,78),10);
        Text(dc,glyphs[i],navButtons[i],RGBc(232,245,255),i==4?24:27,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    const int serviceLeft=22,serviceRight=rc.right-22,serviceY=76,serviceH=82,gap=12;
    const int serviceW=(serviceRight-serviceLeft-4*gap)/5;
    for(int i=0;i<5;i++){
        int left=serviceLeft+i*(serviceW+gap);
        g_serviceRects[i]=RECT{left,serviceY,left+serviceW,serviceY+serviceH};
        bool active=(i==g_activeService);
        Rounded(dc,g_serviceRects[i],active?RGBc(27,104,103):RGBc(20,38,56),12);
        RECT label=g_serviceRects[i];
        if(g_serviceLogos[i]){
            UINT iw=g_serviceLogos[i]->GetWidth(),ih=g_serviceLogos[i]->GetHeight();
            if(iw&&ih){
                const int maxIcon=48;
                float scale=(float)maxIcon/(float)(iw>ih?iw:ih);
                int drawW=(int)(iw*scale),drawH=(int)(ih*scale);
                int iconX=left+(serviceW-drawW)/2;
                int iconY=serviceY+7+(maxIcon-drawH)/2;
                Gdiplus::Graphics graphics(dc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(g_serviceLogos[i],Gdiplus::Rect(iconX,iconY,drawW,drawH));
            }
        }
        label.top=serviceY+57;
        label.bottom=serviceY+78;
        label.left+=5;label.right-=5;
        Text(dc,kServices[i],label,active?RGBc(248,255,255):RGBc(205,220,234),
            12,active?700:600,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    RECT footer={0,rc.bottom-34,rc.right,rc.bottom};FillRectColor(dc,footer,RGBc(10,20,33));
    RECT line={0,rc.bottom-35,rc.right,rc.bottom-34};FillRectColor(dc,line,RGBc(39,61,79));
    RECT status={22,rc.bottom-29,rc.right-18,rc.bottom-5};
    Text(dc,g_status,status,RGBc(133,164,188),11,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    EndPaint(hwnd,&ps);
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
    g_webView->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs *args)->HRESULT{
                BOOL success=FALSE;
                if(args)args->get_IsSuccess(&success);
                SetStatus(success?L"Streaming page loaded. Sign in using the service's official login.":L"Page navigation failed. The service may restrict embedded playback.");
                return S_OK;
            }).Get(),&g_navigationToken);
    /* Some providers open authentication or account pages in a new window.
       Keep navigation inside the same trusted WebView rather than spawning
       an unmanaged browser surface. */
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
                    environment->CreateCoreWebView2Controller(hwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [](HRESULT controllerResult,ICoreWebView2Controller *controller)->HRESULT{
                                OnWebViewReady(controllerResult,controller);
                                return S_OK;
                            }).Get());
                    return S_OK;
                }).Get());
        if(FAILED(hr))SetStatus(L"WebView2 initialisation failed. Check the Evergreen Runtime installation.");
        return 0;
    }
    case WM_SIZE:LayoutBrowser();InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_GETMINMAXINFO:{MINMAXINFO *info=(MINMAXINFO*)lp;info->ptMinTrackSize.x=960;info->ptMinTrackSize.y=700;return 0;}
    case WM_PAINT:PaintChrome(hwnd);return 0;
    case WM_LBUTTONUP:{
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        for(int i=0;i<5;i++)if(PtInRect(&g_serviceRects[i],POINT{x,y})){Navigate(i);return 0;}
        if(PtInRect(&g_backRect,POINT{x,y})){MoveVideoInPlaylist(FALSE);return 0;}
        if(PtInRect(&g_forwardRect,POINT{x,y})){MoveVideoInPlaylist(TRUE);return 0;}
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
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE){
            if(g_webFullscreen){
                if(g_webView)g_webView->ExecuteScript(L"document.exitFullscreen && document.exitFullscreen();",nullptr);
                SetPlatformFullscreen(FALSE);
            }else ShowWindow(hwnd,SW_MINIMIZE);
            return 0;
        }
        break;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:
        g_window=nullptr;
        if(g_controller)g_controller->Close();
        g_webView.Reset();g_controller.Reset();g_environment.Reset();
        ReleaseServiceLogos();
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

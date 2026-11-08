#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include <wrl.h>
#include <WebView2.h>
#include "streaming_host.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
const wchar_t *kClassName=L"ABMAVPlayer.Streaming";
const wchar_t *kServices[5]={
    L"YouTube",L"Netflix",L"Prime Video",L"Disney+ Hotstar",L"Sony LIV"
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
RECT g_backRect{},g_forwardRect{},g_reloadRect{},g_homeRect{};
int g_activeService=-1;
bool g_ready=false;
EventRegistrationToken g_navigationToken{};
wchar_t g_status[256]=L"Preparing secure browser profile…";

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
    RECT bounds={0,124,rc.right,rc.bottom-34};
    if(bounds.bottom<bounds.top)bounds.bottom=bounds.top;
    g_controller->put_Bounds(bounds);
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
    RECT header={0,0,rc.right,124};FillRectColor(dc,header,RGBc(13,27,43));
    RECT accent={0,121,rc.right,124};FillRectColor(dc,accent,RGBc(48,166,161));
    RECT logo={22,15,62,55};Rounded(dc,logo,RGBc(31,143,145),12);
    Text(dc,L"A",logo,RGBc(255,255,255),25,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RECT title={76,12,rc.right-220,39};
    Text(dc,L"ABM AV PLAYER",title,RGBc(244,249,255),18,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT sub={77,38,rc.right-220,58};
    Text(dc,L"STREAMING CENTRE",sub,RGBc(108,193,190),10,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    const int serviceLeft=22,serviceY=72,serviceH=34,serviceW=148,gap=9;
    for(int i=0;i<5;i++){
        g_serviceRects[i]=(RECT){serviceLeft+i*(serviceW+gap),serviceY,
            serviceLeft+i*(serviceW+gap)+serviceW,serviceY+serviceH};
        bool active=(i==g_activeService);
        Rounded(dc,g_serviceRects[i],active?RGBc(27,104,103):RGBc(20,38,56),9);
        RECT label=g_serviceRects[i];label.left+=5;label.right-=5;
        Text(dc,kServices[i],label,active?RGBc(248,255,255):RGBc(185,205,222),
            12,active?700:600,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    int right=rc.right-22;
    g_homeRect=(RECT){right-42,73,right-4,105};right-=50;
    g_reloadRect=(RECT){right-42,73,right-4,105};right-=50;
    g_forwardRect=(RECT){right-42,73,right-4,105};right-=50;
    g_backRect=(RECT){right-42,73,right-4,105};
    const RECT buttons[4]={g_backRect,g_forwardRect,g_reloadRect,g_homeRect};
    const wchar_t *glyphs[4]={L"‹",L"›",L"↻",L"⌂"};
    for(int i=0;i<4;i++){
        Rounded(dc,buttons[i],RGBc(23,43,62),8);
        Text(dc,glyphs[i],buttons[i],RGBc(205,224,239),19,600,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    RECT footer={0,rc.bottom-34,rc.right,rc.bottom};FillRectColor(dc,footer,RGBc(10,20,33));
    RECT line={0,rc.bottom-35,rc.right,rc.bottom-34};FillRectColor(dc,line,RGBc(39,61,79));
    RECT status={22,rc.bottom-29,rc.right-18,rc.bottom-5};
    Text(dc,g_status,status,RGBc(133,164,188),11,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    EndPaint(hwnd,&ps);
}
void OnWebViewReady(HRESULT result,ICoreWebView2Controller *controller){
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
    g_webView->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs *args)->HRESULT{
                BOOL success=FALSE;
                if(args)args->get_IsSuccess(&success);
                SetStatus(success?L"Streaming page loaded. Sign in using the service's official login.":L"Page navigation failed. The service may restrict embedded playback.");
                return S_OK;
            }).Get(),&g_navigationToken);
    g_controller->put_IsVisible(TRUE);
    LayoutBrowser();
    g_ready=true;
    NavigateHome();
}
LRESULT CALLBACK StreamingProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{
        g_window=hwnd;
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
    case WM_PAINT:PaintChrome(hwnd);return 0;
    case WM_LBUTTONUP:{
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        for(int i=0;i<5;i++)if(PtInRect(&g_serviceRects[i],POINT{x,y})){Navigate(i);return 0;}
        if(PtInRect(&g_backRect,POINT{x,y})&&g_webView){g_webView->GoBack();return 0;}
        if(PtInRect(&g_forwardRect,POINT{x,y})&&g_webView){g_webView->GoForward();return 0;}
        if(PtInRect(&g_reloadRect,POINT{x,y})&&g_webView){g_webView->Reload();return 0;}
        if(PtInRect(&g_homeRect,POINT{x,y})){NavigateHome();return 0;}
        return 0;
    }
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE){ShowWindow(hwnd,SW_MINIMIZE);return 0;}
        break;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:
        g_window=nullptr;g_webView.Reset();g_controller.Reset();g_environment.Reset();
        g_ready=false;
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
    if(g_window&&IsWindow(g_window))DestroyWindow(g_window);
}

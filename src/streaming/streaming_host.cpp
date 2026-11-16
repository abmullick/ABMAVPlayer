#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <objbase.h>
#include <shellapi.h>
#include <wininet.h>
#include <cwctype>
#include <cstdarg>
#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <functional>
#include <mutex>
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
#include "../ui_theme.h"
#include "../ui_d2d.h"
#include "../ui_motion.h"
#include "../ui_motion_win.h"
#include "version.h"
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
ABM_D2D_CANVAS g_streamButtonCanvas{};
bool g_streamButtonCanvasReady=false;
ABMMotionValue g_streamHoverMotion{};
bool g_streamHoverMotionReady=false;
double g_streamHoverLevel=0.0;
constexpr UINT ID_STREAM_MOTION_TIMER=0x7A20;
COLORREF StreamHoverColor(COLORREF base,COLORREF hot) {
    double t=g_streamHoverLevel/100.0;
    if(t<0.0)t=0.0;if(t>1.0)t=1.0;
    BYTE r=(BYTE)(GetRValue(base)+(GetRValue(hot)-GetRValue(base))*t);
    BYTE g=(BYTE)(GetGValue(base)+(GetGValue(hot)-GetGValue(base))*t);
    BYTE b=(BYTE)(GetBValue(base)+(GetBValue(hot)-GetBValue(base))*t);
    return RGB(r,g,b);
}

struct ManagedWorker {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> finished;
    std::shared_ptr<std::atomic<bool>> cancel;
};
std::mutex g_workerMutex;
std::vector<ManagedWorker> g_workers;
std::atomic<bool> g_workersStopping{false};

/* Streaming Centre workers are cancellable and are drained before their window
   releases WebView2/COM state. A worker must check its token before beginning
   another network action and before posting a completion message. */
void StartManagedWorker(std::function<void(const std::shared_ptr<std::atomic<bool>>&)> work) {
    std::lock_guard<std::mutex> lock(g_workerMutex);
    if(g_workersStopping.load()) throw std::runtime_error("Streaming Centre is closing");
    for(auto it=g_workers.begin();it!=g_workers.end();) {
        if(it->finished && it->finished->load()) {
            if(it->thread.joinable()) it->thread.join();
            it=g_workers.erase(it);
        } else ++it;
    }
    auto finished=std::make_shared<std::atomic<bool>>(false);
    auto cancel=std::make_shared<std::atomic<bool>>(false);
    g_workers.emplace_back();
    ManagedWorker &worker=g_workers.back();
    worker.finished=finished;
    worker.cancel=cancel;
    try {
        worker.thread=std::thread([work=std::move(work),finished,cancel]() mutable {
            try { work(cancel); }
            catch(...) { OutputDebugStringW(L"ABM AV Player: Streaming Centre worker threw an exception.\\n"); }
            finished->store(true);
        });
    } catch(...) { g_workers.pop_back(); throw; }
}
void StopManagedWorkers() {
    std::vector<ManagedWorker> workers;
    {
        std::lock_guard<std::mutex> lock(g_workerMutex);
        g_workersStopping.store(true);
        for(auto &worker:g_workers) if(worker.cancel) worker.cancel->store(true);
        workers.swap(g_workers);
    }
    /* Interrupt an in-flight GoCast helper process before joining workers. */
    ABM_CastV2_CancelOperations();
    for(auto &worker:workers) if(worker.thread.joinable()) worker.thread.join();
}
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
ABM_D2D_CANVAS g_infoCanvas{};
bool g_infoCanvasReady=false;

bool g_infoAbout=false;
HWND g_infoSearch=nullptr,g_infoTopics=nullptr,g_infoArticle=nullptr,g_infoFind=nullptr,g_infoArticleTitle=nullptr;
HBRUSH g_infoSurfaceBrush=nullptr,g_infoInputBrush=nullptr,g_infoArticleBrush=nullptr;
HFONT g_infoTextFont=nullptr,g_infoHeadingFont=nullptr;
HMODULE g_infoRichEditModule=nullptr;
int g_infoTopicIndex=0,g_infoSearchTopic=0,g_infoSearchOffset=0;
const wchar_t *g_streamHelpTopics[]={
 L"OVERVIEW",L"SUPPORTED SERVICES",L"BROWSER SESSIONS & SIGN-IN",
 L"NAVIGATION CONTROLS",L"FULLSCREEN & EXTERNAL BROWSER",L"TV DISCOVERY & RECEIVERS",
 L"TV APP CHECKS & LAUNCH",L"CAST VIDEO & LIMITS",
 L"PRIVACY, SECURITY & DRM",L"TROUBLESHOOTING",L"KEYBOARD SHORTCUTS"
};
const wchar_t *g_streamHelpArticles[]={
 L"The Streaming Centre brings six provider websites into a dedicated WebView2 window. Choose a service card to navigate to that provider's official website. The embedded browser is separate from the Main Player's local-media playback and local renderer controls. Streaming Centre receiver discovery and casting controls operate in this window; opening Help does not start playback, discover devices or change the Main Player's casting state.\r\n\r\nWhat it does: provides an integrated browser, persistent provider sessions, browser navigation, fullscreen support, an optional external-browser handoff, TV discovery, app availability checks and supported video handoff.\r\n\r\nWhat it does not do: host a streaming catalogue, provide subscriptions, proxy provider credentials, remove advertisements, decrypt protected streams or guarantee that every provider feature works in an embedded browser.",
 L"YouTube — Opens the official YouTube website. YouTube video identifiers can be handed off to compatible Google Cast receivers when the current page exposes a recognised video. Playlist Previous/Next controls are intended for supported YouTube playlist navigation.\r\n\r\nNetflix — Opens the official Netflix website. Desktop-browser title/session transfer to a TV is not supported by this implementation. For TV playback, open Netflix on the television and use its remote or the official Netflix mobile app.\r\n\r\nPrime Video — Opens the official Prime Video website. Desktop Prime Video title handoff is not supported. Launch the Prime Video app on the television and select content there.\r\n\r\nJioHotstar — Opens the official JioHotstar website. Provider playback, account features and any supported casting workflow remain controlled by JioHotstar. Do not assume that browser playback can be relayed to a receiver.\r\n\r\nSony LIV and ZEE5 — Open their official websites. The TV app check can identify supported DIAL app endpoints on a selected receiver and request an app launch where the television advertises that capability. Launching an app does not transfer the desktop browser's sign-in session or selected title.\r\n\r\nAvailability, catalogue, regional access, subscription tiers, browser compatibility and playback restrictions are determined by each provider and can change independently of ABM AV Player.",
 L"Sign in on the provider's own website inside the embedded browser. The Streaming Centre uses a dedicated WebView2 user-data profile under the current Windows user's local application data, normally %LOCALAPPDATA%\\ABMAVPlayer\\\\WebView2. Supported cookies and website storage can preserve sessions between launches.\r\n\r\nThe player does not ask you to enter provider passwords into an ABM form and does not intentionally collect or proxy those credentials. Only sign in when the address bar/page belongs to the provider's genuine HTTPS domain. Use the provider's sign-out option if you share the Windows account. Clearing or removing the WebView2 profile can sign you out and remove provider cookies/site data.\r\n\r\nA provider may require additional verification, a supported browser version, a location check or a fresh sign-in. Session persistence is subject to provider policies, cookie expiry and WebView2 behaviour; it is not guaranteed.",
 L"Back and Forward move through the current embedded browser's navigation history. They are unavailable when there is no page in the requested direction. Reload refreshes the current provider page and may cause the provider to revalidate the session or restart playback. Home navigates to the selected service's start page.\r\n\r\nThe service cards switch the embedded browser to the selected provider's official start page. Previous and Next are intended for supported YouTube playlist navigation; they are not universal previous/next episode controls for every service.\r\n\r\nOpen in browser launches the current page in the Windows default browser. The external browser may use a different profile and may not share the Streaming Centre's cookies or signed-in session. Browser history and session state are not guaranteed to transfer.",
 L"Use the Streaming Centre Fullscreen toolbar control or F11 to request fullscreen. The provider may also expose its own fullscreen button inside the video player. Press Esc to leave fullscreen. Some providers implement fullscreen differently, and the embedded browser can only request modes exposed by the current page.\r\n\r\nIf fullscreen does not start, try the provider's own control, click the video once to reveal its controls, or leave fullscreen with Esc and reload the page. Multiple monitors and display scaling can affect the available fullscreen area.\r\n\r\nOpen in browser moves the current page to the Windows default browser. This is useful when a provider requires a browser feature that is unavailable in WebView2. The external browser may require a separate sign-in.",
 L"Choose Find TVs to start receiver discovery in the Streaming Centre. Wait for discovery to complete, select the intended receiver in the device panel and choose Use selected TV. The selected receiver is used by Streaming Centre app checks and supported Cast video operations.\r\n\r\nKeep the PC and television on the same local network. Guest Wi-Fi, client isolation, VLAN rules, VPNs and Windows Firewall can prevent discovery or communication. A receiver may appear but still lack the protocol or application endpoint needed for a particular operation.\r\n\r\nStreaming Centre discovery is isolated from the Main Player's local playback-output discovery. Selecting a receiver here does not change the Main Player's local renderer selection or start local-player casting.",
 L"Check TV apps performs read-only capability checks against supported Google Cast YouTube availability and DIAL application endpoints. The app selector should list only launch targets confirmed by those checks; results depend on what the television advertises and which protocols it supports.\r\n\r\nChoose a confirmed app and select Launch app to send the launch request. A successful request means the television accepted the request; it does not mean a title or browser session was transferred. Use the television remote to browse and select content unless the provider's official mobile app supplies its own casting control.\r\n\r\nDIAL support varies by TV model, firmware and installed apps. A service can be installed but not expose a compatible endpoint. A failed or missing app check is not proof that the service cannot be opened manually from the TV home screen.",
 L"Cast video inspects the current provider page for a supported handoff. YouTube video IDs can be sent to a compatible Google Cast receiver. A directly addressable media URL may be handed off when the receiver can access and play that URL. Network reachability, codecs, URL lifetime and receiver support affect success.\r\n\r\nProtected or DRM-backed browser streams are not captured, decrypted or relayed by ABM AV Player. Netflix desktop title casting is not supported. Prime Video desktop title handoff is not supported. For Sony LIV and ZEE5, the available DIAL workflow can launch the TV app but does not transfer the desktop browser title or signed-in session. Other services may require their official mobile Cast workflow or native TV app.\r\n\r\nIf a handoff fails, open the service directly on the television or use the provider's official mobile application. Do not interpret app launch as video casting.",
 L"Provider websites control their own accounts, subscriptions, catalogue, advertising, regional availability, content protection and playback policies. ABM AV Player does not host provider content and does not bypass DRM, paywalls, geographic restrictions or subscription checks.\r\n\r\nProvider credentials are entered on the provider website. Supported WebView2 cookies and site data are stored in the current Windows user's Streaming Centre profile. Protect access to your Windows account, especially on shared computers. Use provider sign-out and account security controls when needed.\r\n\r\nNetwork discovery sends local-network protocol traffic to identify compatible receivers and app endpoints. Only perform discovery on networks and devices you are authorised to use.",
 L"Blank or incomplete provider page — Check the network connection, confirm the provider website is reachable in a normal browser, then use Reload. Some websites may block embedded browsers or require an updated WebView2 Runtime.\r\n\r\nRepeated sign-in — Provider cookies may have expired, been cleared or been rejected. Sign in again on the provider's official page. The external browser may have a separate profile.\r\n\r\nVideo will not play — Check provider subscription and regional availability, update Windows/WebView2 Runtime, reload the page and try the provider's own playback controls. Protected playback and hardware/DRM requirements are provider-controlled.\r\n\r\nTV not found — Confirm the PC and TV share a non-guest LAN, disable client isolation where appropriate, check Windows Firewall permissions and run Find TVs again.\r\n\r\nTV app missing — Run Check TV apps again. The receiver must expose a compatible Cast or DIAL endpoint and advertise the application. You can still open the app manually from the TV home screen.\r\n\r\nCast video unavailable — Confirm a receiver is selected and the current page exposes a supported YouTube identifier or directly addressable media URL. DRM-protected streams and unsupported provider handoffs cannot be relayed.\r\n\r\nFullscreen stuck — Press Esc, use the provider's fullscreen button, or close and reopen the Streaming Centre if the page stops responding.",
 L"F11 — Request or leave Streaming Centre fullscreen. Esc — Leave fullscreen or dismiss a provider fullscreen mode. Ctrl+F — Focus the Help search field. Enter — Find the next occurrence of the current search text. F3 — Repeat the search from the next occurrence.\r\n\r\nThe browser's own keyboard shortcuts and provider player shortcuts may also apply while the WebView2 page has focus. Their behaviour is determined by the provider website and may differ between services."
};

ComPtr<ICoreWebView2Environment> g_environment;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webView;
RECT g_serviceRects[kServiceCount]{};
HWND g_devicePanel=nullptr,g_deviceList=nullptr,g_deviceTitle=nullptr,g_deviceHint=nullptr;
HWND g_deviceRefresh=nullptr,g_deviceSelect=nullptr,g_deviceClose=nullptr,g_deviceApps=nullptr,g_deviceAppCombo=nullptr,g_deviceLaunch=nullptr,g_deviceAppStatus=nullptr,g_deviceAppStop=nullptr;
bool g_devicePanelVisible=false;
bool g_deviceSelectPulse=false,g_deviceLaunchPulse=false,g_devicePulsePhase=false;
constexpr UINT ID_STREAM_DEVICE_PULSE_TIMER=0x7A1B;
std::atomic<bool> g_appProbeRunning{false};
struct StreamAppChoice { std::wstring label; std::wstring dialApp; bool castV2=false; std::wstring castAppId; };
struct DialAppProbePayload { std::wstring report; std::vector<StreamAppChoice> choices; };
/* Worker completion messages transfer heap ownership to StreamingProc.
   Once workers are joined no producer can post, so reclaim payloads Windows
   would otherwise discard when the HWND is destroyed with messages queued. */
void DrainPendingWorkerMessages(HWND hwnd) {
    MSG message{};
    while(PeekMessageW(&message,hwnd,WM_APP_DIAL_APPS_READY,WM_APP_CAST_VIDEO_RESULT,PM_REMOVE)) {
        switch(message.message) {
            case WM_APP_DIAL_APPS_READY:
                delete (DialAppProbePayload*)message.lParam;
                break;
            case WM_APP_DIAL_APP_LAUNCH:
            case WM_APP_CAST_VIDEO_RESULT:
                delete (std::wstring*)message.lParam;
                break;
            default:
                break;
        }
    }
}
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
RECT g_backRect{},g_forwardRect{},g_reloadRect{},g_homeRect{},g_fullscreenRect{},g_externalRect{},g_helpRect{},g_aboutRect{},g_castRect{},g_castVideoRect{},g_stopTvRect{};
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
HFONT g_streamIconFont16=NULL;
HFONT g_streamIconFont17=NULL;

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
            DEFAULT_PITCH|FF_DONTCARE,ABM_UI_FONT_FAMILY_TEXT);
        return entry.font;
    }
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}
void ReleaseCachedTextFonts(){
    for(auto &entry:g_textFonts){if(entry.font)DeleteObject(entry.font);entry={};}
    if(g_streamIconFont16){DeleteObject(g_streamIconFont16);g_streamIconFont16=NULL;}
    if(g_streamIconFont17){DeleteObject(g_streamIconFont17);g_streamIconFont17=NULL;}
}
void Text(HDC dc,const wchar_t *value,RECT r,COLORREF color,int height,int weight,UINT flags){
    (void)weight;
    if(!value||!value[0]||!g_streamButtonCanvasReady)return;
    const int width=r.right-r.left,heightPx=r.bottom-r.top;
    if(width<=0||heightPx<=0)return;
    RECT local={0,0,width,heightPx};
    if(!ABM_D2D_CanvasBegin(&g_streamButtonCanvas,dc,&r))return;
    const int role=(height<=13)?ABM_D2D_TEXT_SMALL:ABM_D2D_TEXT_BODY;
    ABM_D2D_CanvasText(&g_streamButtonCanvas,value,local,color,role,flags);
    ABM_D2D_CanvasEnd(&g_streamButtonCanvas);
}

void BrandText(HDC dc,const wchar_t *value,RECT r,COLORREF color,int role,UINT flags){
    if(!value||!value[0]||!g_streamButtonCanvasReady)return;
    const int width=r.right-r.left,heightPx=r.bottom-r.top;
    if(width<=0||heightPx<=0)return;
    RECT local={0,0,width,heightPx};
    if(!ABM_D2D_CanvasBegin(&g_streamButtonCanvas,dc,&r))return;
    ABM_D2D_CanvasText(&g_streamButtonCanvas,value,local,color,role,flags);
    ABM_D2D_CanvasEnd(&g_streamButtonCanvas);
}

HFONT GetCachedIconFont(int px){
    HFONT *slot=(px<=16)?&g_streamIconFont16:&g_streamIconFont17;
    if(!*slot)*slot=CreateFontW(-px,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,
        L"Segoe MDL2 Assets");
    return *slot?*slot:(HFONT)GetStockObject(DEFAULT_GUI_FONT);
}
void DrawFluentGlyph(HDC dc,RECT r,wchar_t glyph,COLORREF color,int px=17){
    (void)px;
    if(!glyph||!g_streamButtonCanvasReady)return;
    const int width=r.right-r.left,heightPx=r.bottom-r.top;
    if(width<=0||heightPx<=0)return;
    RECT local={0,0,width,heightPx};
    if(!ABM_D2D_CanvasBegin(&g_streamButtonCanvas,dc,&r))return;
    wchar_t s[2]={glyph,0};
    ABM_D2D_CanvasText(&g_streamButtonCanvas,s,local,color,ABM_D2D_TEXT_ICON,
        DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    ABM_D2D_CanvasEnd(&g_streamButtonCanvas);
}
wchar_t DeviceButtonGlyph(HWND button){
    if(button==g_deviceRefresh||button==g_deviceAppStatus)return 0xE72C; /* Refresh */
    if(button==g_deviceSelect)return 0xE73E; /* Confirm */
    if(button==g_deviceApps)return 0xE71D; /* Apps */
    if(button==g_deviceLaunch)return 0xE768; /* Launch */
    if(button==g_deviceAppStop)return 0xE71A; /* Stop */
    if(button==g_deviceClose)return 0xE711; /* Close */
    return 0xE710;
}
void DrawDeviceButton(const DRAWITEMSTRUCT *item,const wchar_t *caption,bool primary){
    if(!item||!item->hwndItem)return;
    HDC dc=item->hDC;RECT r=item->rcItem;
    bool pressed=(item->itemState&ODS_SELECTED)!=0;
    bool focused=(item->itemState&ODS_FOCUS)!=0;
    const bool pulse=(item->hwndItem==g_deviceSelect&&g_deviceSelectPulse)||
                     (item->hwndItem==g_deviceLaunch&&g_deviceLaunchPulse);
    COLORREF fill=pulse?(g_devicePulsePhase?ABM_THEME_COLOR_FF911C:ABM_THEME_COLOR_C25310):
        primary?(pressed?ABM_THEME_COLOR_197065:ABM_THEME_COLOR_185B52):
        (pressed?ABM_THEME_COLOR_234B5E:ABM_THEME_COLOR_142A3C);
    COLORREF edge=pulse?ABM_THEME_COLOR_FFBE52:primary?ABM_THEME_COLOR_41BEA5:ABM_THEME_COLOR_305B71;
    if(g_streamButtonCanvasReady&&ABM_D2D_CanvasBegin(&g_streamButtonCanvas,dc,&r)){
        const float width=(float)(r.right-r.left),height=(float)(r.bottom-r.top);
        ABM_D2D_CanvasFill(&g_streamButtonCanvas,0,0,width,height,ABM_THEME_COLOR_0C1D2D);
        RECT face={1,1,(LONG)width-1,(LONG)height-1};
        ABM_D2D_CanvasFillRound(&g_streamButtonCanvas,(float)face.left,(float)face.top,(float)face.right,(float)face.bottom,(float)ABM_UI_RADIUS_BUTTON,fill);
        ABM_D2D_CanvasStrokeRound(&g_streamButtonCanvas,0.5f,0.5f,width-0.5f,height-0.5f,(float)ABM_UI_RADIUS_BUTTON,
            focused?ABM_THEME_COLOR_63E1CD:edge,focused?2.0f:1.0f);
        wchar_t glyph[2]={DeviceButtonGlyph(item->hwndItem),0};
        RECT icon={7,2,31,(LONG)height-2};
        RECT label={31,2,(LONG)width-6,(LONG)height-2};
        ABM_D2D_CanvasText(&g_streamButtonCanvas,glyph,icon,ABM_THEME_COLOR_91E5D5,ABM_D2D_TEXT_ICON,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        ABM_D2D_CanvasText(&g_streamButtonCanvas,caption,label,ABM_THEME_COLOR_E5F4FA,ABM_D2D_TEXT_SMALL,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        ABM_D2D_CanvasEnd(&g_streamButtonCanvas);
        return;
    }
    Rounded(dc,r,fill,ABM_UI_RADIUS_BUTTON);
    HPEN pen=CreatePen(PS_SOLID,focused?2:1,focused?ABM_THEME_COLOR_63E1CD:edge);
    HGDIOBJ op=SelectObject(dc,pen),ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,r.left,r.top,r.right,r.bottom,ABM_UI_RADIUS_BUTTON*2,ABM_UI_RADIUS_BUTTON*2);
    SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(pen);
    RECT icon={r.left+7,r.top+2,r.left+31,r.bottom-2};
    DrawFluentGlyph(dc,icon,DeviceButtonGlyph(item->hwndItem),ABM_THEME_COLOR_91E5D5,16);
    RECT label={r.left+31,r.top+2,r.right-6,r.bottom-2};
    Text(dc,caption,label,ABM_THEME_COLOR_E5F4FA,13,600,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
void DrawDeviceListItem(const DRAWITEMSTRUCT *item){
    if(!item||item->itemID==(UINT)-1)return;
    wchar_t name[256]{};
    SendMessageW(item->hwndItem,LB_GETTEXT,item->itemID,(LPARAM)name);
    bool selected=(item->itemState&ODS_SELECTED)!=0;
    RECT r=item->rcItem;
    if(g_streamButtonCanvasReady&&ABM_D2D_CanvasBegin(&g_streamButtonCanvas,item->hDC,&r)){
        const float width=(float)(r.right-r.left),height=(float)(r.bottom-r.top);
        ABM_D2D_CanvasFill(&g_streamButtonCanvas,0,0,width,height,selected?ABM_THEME_COLOR_1D5B67:ABM_THEME_COLOR_0F1F2F);
        RECT textRect={9,0,(LONG)width-9,(LONG)height};
        ABM_D2D_CanvasText(&g_streamButtonCanvas,name,textRect,selected?ABM_THEME_COLOR_F5FFFF:ABM_THEME_COLOR_CFE1ED,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(item->itemState&ODS_FOCUS)ABM_D2D_CanvasStrokeRound(&g_streamButtonCanvas,1,1,width-1,height-1,ABM_UI_RADIUS_CONTROL,ABM_THEME_COLOR_4BBEB8,1.0f);
        ABM_D2D_CanvasEnd(&g_streamButtonCanvas);
        return;
    }
    FillRectColor(item->hDC,r,selected?ABM_THEME_COLOR_1D5B67:ABM_THEME_COLOR_0F1F2F);
    RECT textRect=r;InflateRect(&textRect,-9,0);
    Text(item->hDC,name,textRect,selected?ABM_THEME_COLOR_F5FFFF:ABM_THEME_COLOR_CFE1ED,11,selected?700:500,
        DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if(item->itemState&ODS_FOCUS){
        HPEN pen=CreatePen(PS_SOLID,1,ABM_THEME_COLOR_4BBEB8);
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
    SetWindowPos(g_deviceAppStop,nullptr,x+342,y+383,156,34,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(g_deviceAppStatus,nullptr,x+342,y+426,156,34,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowPos(g_deviceClose,nullptr,x+342,y+469,156,34,SWP_NOZORDER|SWP_NOACTIVATE);
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
        WS_CHILD|WS_VISIBLE|SS_OWNERDRAW,0,0,10,10,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    g_deviceHint=CreateWindowExW(0,L"STATIC",L"Searching for compatible receivers…",
        WS_CHILD|WS_VISIBLE|SS_OWNERDRAW,0,0,10,10,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    g_deviceList=CreateWindowExW(0,L"LISTBOX",L"",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_LIST,GetModuleHandleW(nullptr),nullptr);
    g_deviceRefresh=CreateWindowExW(0,L"BUTTON",L"Refresh devices",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_REFRESH,GetModuleHandleW(nullptr),nullptr);
    g_deviceSelect=CreateWindowExW(0,L"BUTTON",L"Use selected TV",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_SELECT,GetModuleHandleW(nullptr),nullptr);
    g_deviceApps=CreateWindowExW(0,L"BUTTON",L"Check TV apps",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_APPS,GetModuleHandleW(nullptr),nullptr);
    g_deviceAppCombo=CreateWindowExW(0,L"LISTBOX",L"",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_TABSTOP,0,0,10,160,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_APP_COMBO,GetModuleHandleW(nullptr),nullptr);
    g_deviceLaunch=CreateWindowExW(0,L"BUTTON",L"Launch app",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_LAUNCH,GetModuleHandleW(nullptr),nullptr);
    g_deviceAppStatus=CreateWindowExW(0,L"BUTTON",L"Refresh app status",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_APP_STATUS,GetModuleHandleW(nullptr),nullptr);
    g_deviceAppStop=CreateWindowExW(0,L"BUTTON",L"Stop TV",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_APP_STOP,GetModuleHandleW(nullptr),nullptr);
    g_deviceClose=CreateWindowExW(0,L"BUTTON",L"Close",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,parent,
        (HMENU)(INT_PTR)ID_STREAM_DEVICE_CLOSE,GetModuleHandleW(nullptr),nullptr);
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
    g_deviceSelectPulse=false;g_deviceLaunchPulse=false;
    KillTimer(g_window,ID_STREAM_DEVICE_PULSE_TIMER);
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
void InfoText(HDC dc,const wchar_t *value,RECT rect,COLORREF color,int height,int weight,UINT flags){
    if(g_infoCanvasReady&&g_infoCanvas.drawing){
        int style=height>=24?ABM_D2D_TEXT_HEADING:height<=13?ABM_D2D_TEXT_SMALL:ABM_D2D_TEXT_BODY;
        ABM_D2D_CanvasText(&g_infoCanvas,value,rect,color,style,flags);
    }else Text(dc,value,rect,color,height,weight,flags);
}
void InfoFill(HDC dc,RECT rect,COLORREF color){
    if(g_infoCanvasReady&&g_infoCanvas.drawing)ABM_D2D_CanvasFill(&g_infoCanvas,(float)rect.left,(float)rect.top,(float)rect.right,(float)rect.bottom,color);
    else FillRectColor(dc,rect,color);
}
void InfoRound(HDC dc,RECT rect,COLORREF color,int radius){
    if(g_infoCanvasReady&&g_infoCanvas.drawing)ABM_D2D_CanvasFillRound(&g_infoCanvas,(float)rect.left,(float)rect.top,(float)rect.right,(float)rect.bottom,(float)radius,color);
    else Rounded(dc,rect,color,radius);
}
void StreamingHelpLayout(HWND hwnd){
    RECT rc{};GetClientRect(hwnd,&rc);
    const int w=rc.right,h=rc.bottom;
    const int margin=24,buttonW=(w<620?104:132),searchY=74,searchH=40;
    int searchW=w-margin*2-buttonW-12;if(searchW<120)searchW=120;
    if(g_infoSearch)MoveWindow(g_infoSearch,margin,searchY,searchW,searchH,TRUE);
    if(g_infoFind)MoveWindow(g_infoFind,w-margin-buttonW,searchY,buttonW,searchH,TRUE);
    if(w<760){
        int topicH=h<620?116:156,topicY=146,articleY=topicY+topicH+12,articleH=h-articleY-58;
        if(articleH<120)articleH=120;
        if(g_infoTopics)MoveWindow(g_infoTopics,margin,topicY,w-margin*2,topicH,TRUE);
        if(g_infoArticleTitle)MoveWindow(g_infoArticleTitle,margin,articleY,w-margin,26,TRUE);
        if(g_infoArticle)MoveWindow(g_infoArticle,margin,articleY+30,w-margin*2,articleH,TRUE);
    }else{
        int contentH=h-226;if(contentH<150)contentH=150;
        int top=146,topicW=(w<900?220:270),gap=16;
        if(g_infoTopics)MoveWindow(g_infoTopics,margin,top,topicW,contentH,TRUE);
        if(g_infoArticleTitle)MoveWindow(g_infoArticleTitle,margin+topicW+gap,top,w-(margin+topicW+gap)-margin,26,TRUE);
        if(g_infoArticle)MoveWindow(g_infoArticle,margin+topicW+gap,top+30,w-(margin+topicW+gap)-margin,contentH-30,TRUE);
    }
}
void StreamingHelpShowTopic(int index){
    if(index<0||index>=(int)_countof(g_streamHelpTopics)||!g_infoArticle)return;
    g_infoTopicIndex=index;
    if(g_infoTopics)SendMessageW(g_infoTopics,LB_SETCURSEL,index,0);
    if(g_infoArticleTitle)SetWindowTextW(g_infoArticleTitle,g_streamHelpTopics[index]);
    SetWindowTextW(g_infoArticle,g_streamHelpArticles[index]);
    SendMessageW(g_infoArticle,EM_SETBKGNDCOLOR,0,ABM_UI_COLOR_SURFACE_RAISED);
    SendMessageW(g_infoArticle,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELONG(26,26));
    SendMessageW(g_infoArticle,EM_SETOPTIONS,ECOOP_OR,ECO_AUTOWORDSELECTION);
    CHARFORMAT2W cf{};cf.cbSize=sizeof(cf);cf.dwMask=CFM_FACE|CFM_SIZE|CFM_COLOR|CFM_CHARSET|CFM_BOLD;
    wcscpy_s(cf.szFaceName,_countof(cf.szFaceName),ABM_UI_FONT_FAMILY_TEXT);
    cf.yHeight=(LONG)(ABM_UI_FONT_SIZE_D2D_BODY*15.0f);cf.crTextColor=ABM_UI_COLOR_TEXT_SECONDARY;cf.bCharSet=DEFAULT_CHARSET;
    SendMessageW(g_infoArticle,EM_SETSEL,0,-1);SendMessageW(g_infoArticle,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&cf);
    PARAFORMAT2 pf{};pf.cbSize=sizeof(pf);pf.dwMask=PFM_SPACEAFTER;pf.dySpaceAfter=180;
    SendMessageW(g_infoArticle,EM_SETPARAFORMAT,0,(LPARAM)&pf);
    SendMessageW(g_infoArticle,EM_SETSEL,0,0);
    SendMessageW(g_infoArticle,EM_SCROLLCARET,0,0);
}
BOOL StreamingHelpFindNext(){
    if(!g_infoSearch)return FALSE;
    wchar_t query[256]{};GetWindowTextW(g_infoSearch,query,(int)_countof(query));
    const size_t qlen=wcslen(query);if(!qlen)return FALSE;
    const int count=(int)_countof(g_streamHelpTopics);
    for(int pass=0;pass<count;pass++){
        int topic=(g_infoSearchTopic+pass)%count;
        const wchar_t *article=g_streamHelpArticles[topic];
        const int len=(int)wcslen(article);
        int begin=(topic==g_infoSearchTopic)?g_infoSearchOffset:0;
        if(begin<0||begin>=len)begin=0;
        const wchar_t *found=nullptr;
        for(int i=begin;i+qlen<=(size_t)len;i++){
            if(_wcsnicmp(article+i,query,qlen)==0){found=article+i;break;}
        }
        if(!found&&topic==g_infoSearchTopic){
            for(int i=0;i<begin&&i+qlen<=(size_t)len;i++){
                if(_wcsnicmp(article+i,query,qlen)==0){found=article+i;break;}
            }
        }
        if(found){
            StreamingHelpShowTopic(topic);
            int pos=(int)(found-article);
            SendMessageW(g_infoArticle,EM_SETSEL,pos,pos+(LPARAM)qlen);
            SendMessageW(g_infoArticle,EM_SCROLLCARET,0,0);
            g_infoSearchTopic=topic;g_infoSearchOffset=pos+(int)qlen;
            return TRUE;
        }
    }
    MessageBeep(MB_ICONASTERISK);
    SetWindowTextW(g_infoArticleTitle,L"No further matches — try another search");
    return FALSE;
}
LRESULT CALLBACK StreamingHelpSearchSubclass(HWND edit,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR ref){
    (void)id;(void)ref;
    if(msg==WM_KEYDOWN&&(wp==VK_RETURN||wp==VK_F3)){
        StreamingHelpFindNext();return 0;
    }
    if(msg==WM_KEYDOWN&&wp==VK_ESCAPE){SetFocus(GetParent(edit));return 0;}
    return DefSubclassProc(edit,msg,wp,lp);
}
LRESULT CALLBACK StreamingInfoProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:
        if(g_infoAbout)LoadInfoPortrait();
        g_infoCanvasReady=ABM_D2D_CanvasInit(&g_infoCanvas)!=FALSE;
        if(!g_infoAbout){
            g_infoRichEditModule=LoadLibraryW(L"Msftedit.dll");
            g_infoSurfaceBrush=CreateSolidBrush(ABM_UI_COLOR_SURFACE);
            g_infoInputBrush=CreateSolidBrush(ABM_UI_COLOR_INPUT);
            g_infoArticleBrush=CreateSolidBrush(ABM_UI_COLOR_SURFACE_RAISED);
            g_infoSearch=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|WS_TABSTOP,
                0,0,0,0,hwnd,(HMENU)8101,GetModuleHandleW(nullptr),nullptr);
            g_infoFind=CreateWindowExW(0,L"BUTTON",L"Find next",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                0,0,0,0,hwnd,(HMENU)8102,GetModuleHandleW(nullptr),nullptr);
            g_infoTopics=CreateWindowExW(0,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT|WS_VSCROLL,
                0,0,0,0,hwnd,(HMENU)8103,GetModuleHandleW(nullptr),nullptr);
            g_infoArticleTitle=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT|SS_NOPREFIX,
                0,0,0,0,hwnd,(HMENU)8104,GetModuleHandleW(nullptr),nullptr);
            g_infoArticle=CreateWindowExW(0,g_infoRichEditModule?L"RICHEDIT50W":L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|ES_NOHIDESEL,
                0,0,0,0,hwnd,(HMENU)8105,GetModuleHandleW(nullptr),nullptr);
            g_infoTextFont=CreateFontW(ABM_UI_FONT_HEIGHT(ABM_UI_FONT_SIZE_D2D_BODY),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,ABM_UI_FONT_FAMILY_TEXT);
            g_infoHeadingFont=CreateFontW(ABM_UI_FONT_HEIGHT(ABM_UI_FONT_SIZE_SECTION),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,ABM_UI_FONT_FAMILY_DISPLAY);
            for(HWND c:{g_infoSearch,g_infoFind,g_infoTopics,g_infoArticleTitle,g_infoArticle})if(c)SendMessageW(c,WM_SETFONT,(WPARAM)(c==g_infoArticleTitle?g_infoHeadingFont:g_infoTextFont),TRUE);
            if(g_infoSearch)SetWindowSubclass(g_infoSearch,StreamingHelpSearchSubclass,1,0);
            if(g_infoTopics){SendMessageW(g_infoTopics,LB_SETITEMHEIGHT,0,40);SetWindowTheme(g_infoTopics,L"DarkMode_Explorer",NULL);for(const wchar_t *topic:g_streamHelpTopics)SendMessageW(g_infoTopics,LB_ADDSTRING,0,(LPARAM)topic);}
            if(g_infoSearch){SendMessageW(g_infoSearch,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELONG(16,16));SendMessageW(g_infoSearch,EM_SETCUEBANNER,TRUE,(LPARAM)L"Search all streaming topics  ·  e.g. YouTube, DRM, DIAL, receiver");SetWindowTheme(g_infoSearch,L"DarkMode_Explorer",NULL);}
            if(g_infoArticle)SetWindowTheme(g_infoArticle,L"DarkMode_Explorer",NULL);
            RegisterHotKey(hwnd,1,MOD_CONTROL,'F');RegisterHotKey(hwnd,2,0,VK_F3);
            StreamingHelpLayout(hwnd);StreamingHelpShowTopic(0);
        }
        return 0;
    case WM_SIZE:
        if(!g_infoAbout)StreamingHelpLayout(hwnd);
        return 0;
    case WM_HOTKEY:
        if(!g_infoAbout&&wp==1){SetFocus(g_infoSearch);SendMessageW(g_infoSearch,EM_SETSEL,0,-1);return 0;}
        if(!g_infoAbout&&wp==2){StreamingHelpFindNext();return 0;}
        break;
    case WM_COMMAND:
        if(!g_infoAbout&&LOWORD(wp)==8102&&HIWORD(wp)==BN_CLICKED){StreamingHelpFindNext();return 0;}
        if(!g_infoAbout&&LOWORD(wp)==8103&&HIWORD(wp)==LBN_SELCHANGE){int i=(int)SendMessageW(g_infoTopics,LB_GETCURSEL,0,0);StreamingHelpShowTopic(i);return 0;}
        if(!g_infoAbout&&LOWORD(wp)==8101&&HIWORD(wp)==EN_CHANGE){g_infoSearchTopic=0;g_infoSearchOffset=0;return 0;}
        break;
    case WM_ERASEBKGND:return 1;
    case WM_MEASUREITEM:{
        MEASUREITEMSTRUCT *m=(MEASUREITEMSTRUCT*)lp;
        if(!g_infoAbout&&m&&m->CtlID==8103){m->itemHeight=40;return TRUE;}
        break;
    }
    case WM_DRAWITEM:{
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)lp;
        if(!g_infoAbout&&wp==8102&&d&&g_infoCanvasReady){
            BOOL pressed=(d->itemState&ODS_SELECTED)!=0,focused=(d->itemState&ODS_FOCUS)!=0;
            RECT bounds={0,0,d->rcItem.right-d->rcItem.left,d->rcItem.bottom-d->rcItem.top};
            if(ABM_D2D_CanvasBegin(&g_infoCanvas,d->hDC,&d->rcItem)){
                COLORREF fill=pressed?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_ACCENT;
                COLORREF edge=focused?ABM_UI_COLOR_FOCUS:ABM_UI_COLOR_ACCENT_BRIGHT;
                ABM_D2D_CanvasFill(&g_infoCanvas,0,0,(float)bounds.right,(float)bounds.bottom,ABM_THEME_COLOR_0C1D2D);
                RECT face=bounds;InflateRect(&face,-1,-1);
                ABM_D2D_CanvasFillRound(&g_infoCanvas,(float)face.left,(float)face.top,(float)face.right,(float)face.bottom,(float)ABM_UI_RADIUS_BUTTON,fill);
                ABM_D2D_CanvasStrokeRound(&g_infoCanvas,0.5f,0.5f,(float)bounds.right-0.5f,(float)bounds.bottom-0.5f,(float)ABM_UI_RADIUS_BUTTON,edge,focused?2.0f:1.0f);
                RECT label=bounds;InflateRect(&label,-8,-2);
                ABM_D2D_CanvasText(&g_infoCanvas,L"FIND NEXT",label,ABM_UI_COLOR_TEXT,ABM_D2D_TEXT_SMALL,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                ABM_D2D_CanvasEnd(&g_infoCanvas);return TRUE;
            }
        }
        if(!g_infoAbout&&wp==8103&&d&&d->itemID!=(UINT)-1&&g_infoCanvasReady){
            wchar_t label[128]{};SendMessageW(d->hwndItem,LB_GETTEXT,d->itemID,(LPARAM)label);
            BOOL selected=(d->itemState&ODS_SELECTED)!=0,hot=(d->itemState&ODS_HOTLIGHT)!=0;
            RECT bounds={0,0,d->rcItem.right-d->rcItem.left,d->rcItem.bottom-d->rcItem.top};
            if(ABM_D2D_CanvasBegin(&g_infoCanvas,d->hDC,&d->rcItem)){
                RECT row=bounds;InflateRect(&row,-6,-2);
                ABM_D2D_CanvasFillRound(&g_infoCanvas,(float)row.left,(float)row.top,(float)row.right,(float)row.bottom,(float)ABM_UI_RADIUS_CONTROL,selected?ABM_THEME_COLOR_163B48:(hot?ABM_UI_COLOR_SURFACE_BLUE:ABM_UI_COLOR_SURFACE));
                RECT textRect=row;textRect.left+=12;textRect.right-=8;
                ABM_D2D_CanvasText(&g_infoCanvas,label,textRect,selected?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                if(d->itemState&ODS_FOCUS)ABM_D2D_CanvasStrokeRound(&g_infoCanvas,(float)row.left+0.5f,(float)row.top+0.5f,(float)row.right-0.5f,(float)row.bottom-0.5f,(float)ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_FOCUS,1.0f);
                ABM_D2D_CanvasEnd(&g_infoCanvas);return TRUE;
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:{
        if(!g_infoAbout){
            HDC dc=(HDC)wp;HWND control=(HWND)lp;
            SetBkMode(dc,OPAQUE);
            if(control==g_infoSearch){SetTextColor(dc,ABM_UI_COLOR_TEXT);SetBkColor(dc,ABM_UI_COLOR_INPUT);return (LRESULT)g_infoInputBrush;}
            if(control==g_infoArticle){SetTextColor(dc,ABM_UI_COLOR_TEXT_SECONDARY);SetBkColor(dc,ABM_UI_COLOR_SURFACE_RAISED);return (LRESULT)g_infoArticleBrush;}
            if(control==g_infoArticleTitle){SetTextColor(dc,ABM_UI_COLOR_ACCENT_BRIGHT);SetBkColor(dc,ABM_UI_COLOR_BACKGROUND);return (LRESULT)g_infoSurfaceBrush;}
            SetTextColor(dc,ABM_UI_COLOR_TEXT_SECONDARY);SetBkColor(dc,ABM_UI_COLOR_SURFACE);return (LRESULT)g_infoSurfaceBrush;
        }break;
    }
    case WM_PAINT:{
        PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT rc{};GetClientRect(hwnd,&rc);
        if(g_infoCanvasReady&&ABM_D2D_CanvasBegin(&g_infoCanvas,dc,&rc)){
            ABM_D2D_CanvasFill(&g_infoCanvas,0,0,(float)rc.right,(float)rc.bottom,ABM_UI_COLOR_BACKGROUND);
            ABM_D2D_CanvasFill(&g_infoCanvas,0,0,(float)rc.right,6,ABM_UI_COLOR_ACCENT);
            if(g_infoAbout){RECT frame={28,94,286,362};ABM_D2D_CanvasFillRound(&g_infoCanvas,(float)frame.left,(float)frame.top,(float)frame.right,(float)frame.bottom,(float)ABM_UI_RADIUS_CARD,ABM_THEME_COLOR_122637);}
            ABM_D2D_CanvasEnd(&g_infoCanvas);
        }else{FillRectColor(dc,rc,ABM_UI_COLOR_BACKGROUND);RECT accent={0,0,rc.right,6};FillRectColor(dc,accent,ABM_UI_COLOR_ACCENT);}
        if(g_infoAbout&&g_infoPortrait){Gdiplus::Graphics graphics(dc);graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);graphics.DrawImage(g_infoPortrait,Gdiplus::Rect(36,102,242,242));}
        if(g_infoCanvasReady)ABM_D2D_CanvasBegin(&g_infoCanvas,dc,&rc);
        if(g_infoAbout){
            RECT brand={28,17,rc.right-28,48};
            InfoText(dc,L"ABM AV PLAYER",brand,ABM_THEME_COLOR_F4F9FF,21,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT subtitle={29,47,rc.right-28,70};
            InfoText(dc,L"THE CREATOR",subtitle,ABM_THEME_COLOR_5BE0DA,12,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        }else{
            RECT kicker={34,7,rc.right-28,29};
            InfoText(dc,L"ABM AV PLAYER   /   STREAMING CENTRE HELP",kicker,ABM_UI_COLOR_ACCENT_BRIGHT,12,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT intro={34,31,rc.right-28,57};
            InfoText(dc,L"Provider-specific guidance for streaming services, browser sessions and TV receivers.",intro,ABM_UI_COLOR_TEXT_SECONDARY,16,400,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            RECT accentLine={0,60,rc.right,63};InfoFill(dc,accentLine,ABM_UI_COLOR_ACCENT);
        }
        if(g_infoAbout){
            if(!g_infoPortrait){
                RECT fallback={36,102,278,344};InfoRound(dc,fallback,ABM_THEME_COLOR_1B8F91,ABM_UI_RADIUS_CARD);
                InfoText(dc,L"ABM",fallback,ABM_THEME_COLOR_F5FFFF,42,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
            RECT product={322,98,rc.right-28,138};
            InfoText(dc,L"ABM AV Player",product,ABM_THEME_COLOR_FFFFFF,25,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT tagline={322,140,rc.right-28,163};
            InfoText(dc,L"Capture the feeling. Relive every moment.",tagline,ABM_THEME_COLOR_70D3CA,13,600,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT creatorLabel={322,174,rc.right-28,197};
            InfoText(dc,L"THE CREATOR",creatorLabel,ABM_THEME_COLOR_44CCD1,11,600,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT creator={322,204,rc.right-28,237};
            InfoText(dc,L"Abhisek Basu Mullick",creator,ABM_THEME_COLOR_FFFFFF,19,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT role={322,244,rc.right-28,271};
            InfoText(dc,L"Founder & Developer",role,ABM_THEME_COLOR_E8F1FA,15,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            RECT contactLabel={322,290,rc.right-28,313};
            InfoText(dc,L"CONTACT",contactLabel,ABM_THEME_COLOR_44CCD1,11,600,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT email={322,321,rc.right-28,348};
            InfoText(dc,L"abmullick@gmail.com",email,ABM_THEME_COLOR_F6FAFF,15,600,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT description={322,362,rc.right-28,412};
            InfoText(dc,L"A unified Windows media platform for local playback, online streaming and torrent streaming. Enjoy your media on your PC or cast it to your television.",description,ABM_THEME_COLOR_E2EEF9,14,400,DT_LEFT|DT_TOP|DT_WORDBREAK);
            RECT version={322,420,rc.right-28,446};
            InfoText(dc,L"Version " ABM_VERSION_W L"   |   © 2026 Abhisek Basu Mullick",version,ABM_THEME_COLOR_AEC6DB,12,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT divider={28,455,rc.right-28,456};InfoFill(dc,divider,ABM_THEME_COLOR_233E52);
            RECT featuresTitle={32,470,rc.right-32,495};
            InfoText(dc,L"KEY FEATURES",featuresTitle,ABM_THEME_COLOR_44CCD1,11,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT features={32,500,rc.right-32,592};
            InfoText(dc,L"Local audio and video playback  •  Streaming Centre for YouTube, Netflix, Prime Video, JioHotstar, Sony LIV and ZEE5\nTorrent streaming with persistent caching and background downloading\nGoogle Cast and Android TV casting, including torrent media casting\nIntegrated playlists and audio equalizer",features,ABM_THEME_COLOR_EBF3FA,13,400,DT_LEFT|DT_TOP|DT_WORDBREAK);
            RECT tributeTitle={32,610,rc.right-32,635};
            InfoText(dc,L"IN TRIBUTE TO DENNIS RITCHIE",tributeTitle,ABM_THEME_COLOR_44CCD1,11,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT tribute={32,640,rc.right-32,700};
            InfoText(dc,L"My homage to Dennis Ritchie, creator of the C programming language and co-creator of Unix. His ideas and work helped shape modern computing and generations of computer science.",tribute,ABM_THEME_COLOR_EBF3FA,14,400,DT_LEFT|DT_TOP|DT_WORDBREAK);
            RECT stackTitle={32,720,rc.right-32,745};
            InfoText(dc,L"BUILT WITH",stackTitle,ABM_THEME_COLOR_44CCD1,11,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT stack={32,750,rc.right-32,790};
            InfoText(dc,L"C  •  C++  •  Win32 API  •  LibVLC  •  libtorrent  •  Direct2D  •  DirectWrite  •  Microsoft WebView2",stack,ABM_THEME_COLOR_DDE9F5,13,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }else{
            RECT navLabel={24,124,294,142};InfoText(dc,L"TOPICS",navLabel,ABM_UI_COLOR_TEXT_MUTED,12,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT articleLabel={310,124,rc.right-24,142};InfoText(dc,L"ARTICLE",articleLabel,ABM_UI_COLOR_TEXT_MUTED,12,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            HWND panels[3]={g_infoSearch,g_infoTopics,g_infoArticle};
            for(int pi=0;pi<3;pi++)if(panels[pi]&&IsWindowVisible(panels[pi])){
                RECT pr;GetWindowRect(panels[pi],&pr);MapWindowPoints(HWND_DESKTOP,hwnd,(POINT*)&pr,2);
                InflateRect(&pr,8,8);
                ABM_D2D_CanvasFillRound(&g_infoCanvas,(float)pr.left,(float)pr.top,(float)pr.right,(float)pr.bottom,(float)ABM_UI_RADIUS_CARD,ABM_UI_COLOR_SURFACE);
                ABM_D2D_CanvasStrokeRound(&g_infoCanvas,(float)pr.left+0.5f,(float)pr.top+0.5f,(float)pr.right-0.5f,(float)pr.bottom-0.5f,(float)ABM_UI_RADIUS_CARD,ABM_UI_COLOR_BORDER,1.0f);
            }
        }
        if(g_infoCanvasReady&&g_infoCanvas.drawing)ABM_D2D_CanvasEnd(&g_infoCanvas);
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_NCDESTROY:
        if(g_infoSearch)RemoveWindowSubclass(g_infoSearch,StreamingHelpSearchSubclass,1);
        UnregisterHotKey(hwnd,1);UnregisterHotKey(hwnd,2);
        if(g_infoCanvasReady){g_infoCanvasReady=false;ABM_D2D_CanvasDestroy(&g_infoCanvas);}
        if(g_infoSurfaceBrush){DeleteObject(g_infoSurfaceBrush);g_infoSurfaceBrush=nullptr;}
        if(g_infoInputBrush){DeleteObject(g_infoInputBrush);g_infoInputBrush=nullptr;}
        if(g_infoArticleBrush){DeleteObject(g_infoArticleBrush);g_infoArticleBrush=nullptr;}
        if(g_infoTextFont){DeleteObject(g_infoTextFont);g_infoTextFont=nullptr;}
        if(g_infoHeadingFont){DeleteObject(g_infoHeadingFont);g_infoHeadingFont=nullptr;}
        if(g_infoRichEditModule){FreeLibrary(g_infoRichEditModule);g_infoRichEditModule=nullptr;}
        g_infoSearch=g_infoTopics=g_infoArticle=g_infoFind=g_infoArticleTitle=nullptr;
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
    int width=1120,height=about?900:840;
    RECT owner{};if(g_window)GetWindowRect(g_window,&owner);
    int x=owner.left+(owner.right-owner.left-width)/2;
    int y=owner.top+(owner.bottom-owner.top-height)/2;
    const wchar_t *caption=about?L"About ABM AV Player":L"ABM AV Player - Streaming Help";
    g_infoWindow=CreateWindowExW(WS_EX_DLGMODALFRAME,kInfoClassName,caption,
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|(about?0:WS_THICKFRAME),x,y,width,height,g_window,nullptr,instance,nullptr);
    if(!g_infoWindow)return;
    BOOL dark=TRUE;DwmSetWindowAttribute(g_infoWindow,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    if(!about){int corners=2;DwmSetWindowAttribute(g_infoWindow,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));}
    COLORREF captionColor=ABM_UI_COLOR_SURFACE,borderColor=ABM_UI_COLOR_BORDER_STRONG;
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
                        StartManagedWorker([target,dialIndex,service](const std::shared_ptr<std::atomic<bool>>& cancel){
                    if(cancel->load())return;
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
                            if(cancel->load()||!IsWindow(target)||!PostMessageW(target,WM_APP_CAST_VIDEO_RESULT,0,(LPARAM)result))delete result;
                            StreamingDiag(L"%s browser Cast action DIAL launch=%d",service.c_str(),launched?1:0);
                        });
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
                            StartManagedWorker([target,castIndex](const std::shared_ptr<std::atomic<bool>>& cancel){
                    if(cancel->load())return;
                                std::wstring message;
                                if(ABM_CastV2_LaunchApplication((unsigned int)castIndex,L"17608BC8"))
                                    message=L"Prime Video opened on the selected TV. Choose the title using the TV remote. ABM cannot transfer the desktop browser's Amazon session or protected video; use Prime Video's official mobile Cast button for phone-to-TV playback.";
                                else
                                    message=L"Could not open Prime Video on the selected TV. Refresh Find TVs, select the receiver and check that the experimental Prime Video app endpoint is available.";
                                auto *result=new std::wstring(std::move(message));
                                if(cancel->load()||!IsWindow(target)||!PostMessageW(target,WM_APP_CAST_VIDEO_RESULT,0,(LPARAM)result))delete result;
                            });
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
                        StartManagedWorker([target,id,castIndex,dialIndex](const std::shared_ptr<std::atomic<bool>>& cancel){
                    if(cancel->load())return;
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
                            if(cancel->load()||!IsWindow(target)||!PostMessageW(target,WM_APP_CAST_VIDEO_RESULT,0,(LPARAM)result))delete result;
                        });
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

std::wstring ThemeCssColor(COLORREF color) {
    wchar_t value[16]{};
    StringCchPrintfW(value, _countof(value), L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return value;
}
std::wstring ThemeCssColorAlpha(COLORREF color, BYTE alpha) {
    wchar_t value[64]{};
    StringCchPrintfW(value, _countof(value), L"rgba(%u,%u,%u,%.3f)", GetRValue(color), GetGValue(color), GetBValue(color), (double)alpha / 255.0);
    return value;
}
void ReplaceThemeCssColor(std::wstring &html, const wchar_t *marker, const std::wstring &value) {
    const size_t at = html.find(marker);
    if (at != std::wstring::npos) html.replace(at, wcslen(marker), value);
}

void NavigateHome(){
    g_activeService=-1;
    if(g_webView){
        std::wstring html=LR"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<style>
*{box-sizing:border-box}body{margin:0;background:radial-gradient(ellipse at 50% 0%,@BG_TOP@ 0,@BG_MID@ 48%,@BG_BOTTOM@ 100%);color:@TEXT@;font:16px 'Segoe UI',sans-serif;min-height:100vh;display:grid;place-items:center}
main{max-width:780px;padding:52px;text-align:center} .mark{width:62px;height:62px;border-radius:18px;margin:0 auto 24px;background:linear-gradient(135deg,@ACCENT@,@ACCENT_2@);display:grid;place-items:center;font-size:28px;font-weight:700;box-shadow:0 12px 35px @SHADOW@}
h1{font-size:34px;margin:0 0 12px;letter-spacing:-.7px}p{color:@TEXT_SECONDARY@;line-height:1.7;margin:0 auto 28px;max-width:650px}.tag{display:inline-block;border:1px solid @BORDER@;background:@INPUT@;border-radius:999px;padding:8px 14px;color:@ACCENT_BRIGHT@;font-size:12px;letter-spacing:1px}
</style></head><body><main><div class="mark">A</div><div class="tag">ABM  /  STREAMING CENTRE</div><h1>Your streaming universe</h1><p>Choose a service from the navigation above. Sign in directly on its official website using your own account. Your browser session is stored locally on this Windows profile.</p><p style="font-size:13px;color:@TEXT_MUTED@">Playback availability and protected-video support depend on each streaming provider and the installed Microsoft Edge WebView2 Runtime.</p></main></body></html>)HTML";

        ReplaceThemeCssColor(html,L"@BG_TOP@",ThemeCssColor(ABM_THEME_COLOR_163B48));
        ReplaceThemeCssColor(html,L"@BG_MID@",ThemeCssColor(ABM_THEME_COLOR_0B1728));
        ReplaceThemeCssColor(html,L"@BG_BOTTOM@",ThemeCssColor(ABM_THEME_COLOR_080F1C));
        ReplaceThemeCssColor(html,L"@TEXT@",ThemeCssColor(ABM_THEME_COLOR_EFF6FF));
        ReplaceThemeCssColor(html,L"@ACCENT@",ThemeCssColor(ABM_THEME_COLOR_239E9B));
        ReplaceThemeCssColor(html,L"@ACCENT_2@",ThemeCssColor(ABM_THEME_COLOR_397C89));
        ReplaceThemeCssColor(html,L"@SHADOW@",ThemeCssColorAlpha(ABM_THEME_COLOR_020811,0x88));
        ReplaceThemeCssColor(html,L"@TEXT_SECONDARY@",ThemeCssColor(ABM_THEME_COLOR_A9BFD2));
        ReplaceThemeCssColor(html,L"@BORDER@",ThemeCssColor(ABM_THEME_COLOR_34566B));
        ReplaceThemeCssColor(html,L"@INPUT@",ThemeCssColor(ABM_THEME_COLOR_102337));
        ReplaceThemeCssColor(html,L"@ACCENT_BRIGHT@",ThemeCssColor(ABM_THEME_COLOR_9BD4D0));
        ReplaceThemeCssColor(html,L"@TEXT_MUTED@",ThemeCssColor(ABM_THEME_COLOR_7893AA));
        g_webView->NavigateToString(html.c_str());
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
    FillRectColor(dc,rc,ABM_THEME_COLOR_070C15);

    /* A taller, clearly separated native chrome keeps the service selector prominent. */
    RECT header={0,0,rc.right,260};FillRectColor(dc,header,ABM_THEME_COLOR_0B1827);
    RECT topGlow={0,0,rc.right,2};FillRectColor(dc,topGlow,ABM_THEME_COLOR_1F5B69);
    RECT accent={0,257,rc.right,260};FillRectColor(dc,accent,ABM_THEME_COLOR_30A6A1);

    /* Use the same signature logo and split wordmark as the main player.
       ABM is the large display face; AV PLAYER is deliberately smaller and
       teal, with the same baseline/offset and palette as the main masthead. */
    RECT logoHalo={20,7,102,87};Rounded(dc,logoHalo,ABM_THEME_COLOR_0D1C2D,22);
    HPEN haloPen=CreatePen(PS_SOLID,1,ABM_THEME_COLOR_3F97B4);
    HGDIOBJ oldLogoPen=SelectObject(dc,haloPen);
    HGDIOBJ oldLogoBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,logoHalo.left,logoHalo.top,logoHalo.right,logoHalo.bottom,22,22);
    SelectObject(dc,oldLogoBrush);SelectObject(dc,oldLogoPen);DeleteObject(haloPen);
    RECT logo={28,14,94,80};Rounded(dc,logo,ABM_THEME_COLOR_1C9D9D,17);
    RECT logoInset={33,19,89,75};
    HPEN insetPen=CreatePen(PS_SOLID,1,ABM_THEME_COLOR_BEE5FF);oldLogoPen=SelectObject(dc,insetPen);
    oldLogoBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,logoInset.left,logoInset.top,logoInset.right,logoInset.bottom,14,14);
    SelectObject(dc,oldLogoBrush);SelectObject(dc,oldLogoPen);DeleteObject(insetPen);
    HPEN markPen=CreatePen(PS_SOLID,3,ABM_THEME_COLOR_FFFFFF);oldLogoPen=SelectObject(dc,markPen);
    MoveToEx(dc,42,63,nullptr);LineTo(dc,59,30);LineTo(dc,76,63);
    SelectObject(dc,oldLogoPen);DeleteObject(markPen);
    HPEN crossPen=CreatePen(PS_SOLID,2,ABM_THEME_COLOR_E2F5FF);oldLogoPen=SelectObject(dc,crossPen);
    MoveToEx(dc,49,49,nullptr);LineTo(dc,68,49);
    SelectObject(dc,oldLogoPen);DeleteObject(crossPen);
    HPEN playPen=CreatePen(PS_SOLID,2,ABM_THEME_COLOR_FFFFFF);oldLogoPen=SelectObject(dc,playPen);
    MoveToEx(dc,62,40,nullptr);LineTo(dc,75,48);LineTo(dc,62,56);LineTo(dc,62,40);
    SelectObject(dc,oldLogoPen);DeleteObject(playPen);

    /* Brand wordmark uses the canonical display/text roles. Both layout
       rectangles share the same bottom edge so the two parts sit on one
       visual baseline rather than stepping down. */
    RECT brandMain={116,8,188,49};
    BrandText(dc,L"ABM",brandMain,ABM_THEME_COLOR_F7FAFF,
        ABM_D2D_TEXT_BRAND_MAIN,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    /* Keep both DirectWrite formats in the exact same layout box. Their
       different font metrics already provide the intended size hierarchy;
       a second top offset incorrectly shifts the suffix down. */
    RECT brandSuffix={188,8,306,49};
    BrandText(dc,L"AV  PLAYER",brandSuffix,ABM_THEME_COLOR_84CDC2,
        ABM_D2D_TEXT_BRAND_SUFFIX,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT brandRule={116,52,280,54};FillRectColor(dc,brandRule,ABM_THEME_COLOR_36B1A8);
    RECT sub={116,60,306,83};
    Text(dc,L"STREAMING CENTRE",sub,ABM_THEME_COLOR_9DB1C9,13,600,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    g_helpRect=RECT{318,15,390,55};
    Rounded(dc,g_helpRect,g_hoverControl==5?StreamHoverColor(ABM_THEME_COLOR_142A3C,ABM_THEME_COLOR_244B5E):ABM_THEME_COLOR_142A3C,9);
    DrawFluentGlyph(dc,RECT{g_helpRect.left+3,g_helpRect.top+2,g_helpRect.left+31,g_helpRect.bottom-2},0xE897,ABM_THEME_COLOR_8EE2D3,17);
    RECT helpLabel={g_helpRect.left+29,g_helpRect.top,g_helpRect.right-4,g_helpRect.bottom};
    Text(dc,L"Help",helpLabel,ABM_THEME_COLOR_D2E8F4,11,600,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    g_aboutRect=RECT{398,15,470,55};
    Rounded(dc,g_aboutRect,g_hoverControl==6?StreamHoverColor(ABM_THEME_COLOR_142A3C,ABM_THEME_COLOR_244B5E):ABM_THEME_COLOR_142A3C,9);
    DrawFluentGlyph(dc,RECT{g_aboutRect.left+3,g_aboutRect.top+2,g_aboutRect.left+31,g_aboutRect.bottom-2},0xE946,ABM_THEME_COLOR_8EE2D3,17);
    RECT aboutLabel={g_aboutRect.left+29,g_aboutRect.top,g_aboutRect.right-4,g_aboutRect.bottom};
    Text(dc,L"About",aboutLabel,ABM_THEME_COLOR_D2E8F4,11,600,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    g_castRect=RECT{482,15,612,55};
    const bool receiverReady=IsStreamReceiverReady();
    const bool pulseOn=receiverReady && ((GetTickCount64()/400)%2==0);
    const COLORREF castFill=receiverReady?(pulseOn?ABM_THEME_COLOR_BE2330:ABM_THEME_COLOR_701622):
        (g_hoverControl==8?ABM_THEME_COLOR_227070:ABM_THEME_COLOR_184B53);
    const COLORREF castEdge=receiverReady?(pulseOn?ABM_THEME_COLOR_FF6970:ABM_THEME_COLOR_DC414C):
        (g_hoverControl==8?ABM_THEME_COLOR_5DE1CF:ABM_THEME_COLOR_308989);
    Rounded(dc,g_castRect,castFill,10);
    HPEN castPen=CreatePen(PS_SOLID,(receiverReady||g_hoverControl==8)?2:1,castEdge);
    HGDIOBJ oldCastPen=SelectObject(dc,castPen);
    HGDIOBJ oldCastBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,g_castRect.left,g_castRect.top,g_castRect.right,g_castRect.bottom,10,10);
    SelectObject(dc,oldCastBrush);SelectObject(dc,oldCastPen);DeleteObject(castPen);
    DrawFluentGlyph(dc,RECT{g_castRect.left+3,g_castRect.top+2,g_castRect.left+31,g_castRect.bottom-2},0xE721,ABM_THEME_COLOR_8EE2D3,17);
    RECT findTvLabel={g_castRect.left+29,g_castRect.top,g_castRect.right-5,g_castRect.bottom};
    Text(dc,L"Find TVs",findTvLabel,receiverReady?ABM_THEME_COLOR_FFFFFF:ABM_THEME_COLOR_E0FAF5,11,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    g_castVideoRect=RECT{620,15,730,55};
    Rounded(dc,g_castVideoRect,g_hoverControl==9?StreamHoverColor(ABM_THEME_COLOR_195B50,ABM_THEME_COLOR_237E6F):ABM_THEME_COLOR_195B50,10);
    HPEN videoPen=CreatePen(PS_SOLID,g_hoverControl==9?2:1,g_hoverControl==9?ABM_THEME_COLOR_71EDC6:ABM_THEME_COLOR_39A484);
    HGDIOBJ oldVideoPen=SelectObject(dc,videoPen);
    HGDIOBJ oldVideoBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,g_castVideoRect.left,g_castVideoRect.top,g_castVideoRect.right,g_castVideoRect.bottom,10,10);
    SelectObject(dc,oldVideoBrush);SelectObject(dc,oldVideoPen);DeleteObject(videoPen);
    DrawFluentGlyph(dc,RECT{g_castVideoRect.left+3,g_castVideoRect.top+2,g_castVideoRect.left+31,g_castVideoRect.bottom-2},0xE768,ABM_THEME_COLOR_8EE2D3,17);
    RECT castVideoLabel={g_castVideoRect.left+29,g_castVideoRect.top,g_castVideoRect.right-5,g_castVideoRect.bottom};
    Text(dc,L"Cast video",castVideoLabel,ABM_THEME_COLOR_E2FFF1,11,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    g_stopTvRect=RECT{740,15,850,55};
    Rounded(dc,g_stopTvRect,g_hoverControl==10?ABM_THEME_COLOR_963722:ABM_THEME_COLOR_642B20,10);
    HPEN stopPen=CreatePen(PS_SOLID,g_hoverControl==10?2:1,ABM_THEME_COLOR_DC7753);
    HGDIOBJ oldStopPen=SelectObject(dc,stopPen);
    HGDIOBJ oldStopBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,g_stopTvRect.left,g_stopTvRect.top,g_stopTvRect.right,g_stopTvRect.bottom,10,10);
    SelectObject(dc,oldStopBrush);SelectObject(dc,oldStopPen);DeleteObject(stopPen);
    DrawFluentGlyph(dc,RECT{g_stopTvRect.left+3,g_stopTvRect.top+2,g_stopTvRect.left+31,g_stopTvRect.bottom-2},0xE71A,ABM_THEME_COLOR_FFD2C4,17);
    RECT stopTvLabel={g_stopTvRect.left+29,g_stopTvRect.top,g_stopTvRect.right-5,g_stopTvRect.bottom};
    Text(dc,L"Stop TV",stopTvLabel,ABM_THEME_COLOR_FFEFE8,11,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    g_externalRect=RECT{rc.right-190,15,rc.right-22,55};
    Rounded(dc,g_externalRect,g_hoverControl==7?StreamHoverColor(ABM_THEME_COLOR_163140,ABM_THEME_COLOR_225B65):ABM_THEME_COLOR_163140,10);
    DrawFluentGlyph(dc,RECT{g_externalRect.left+3,g_externalRect.top+2,g_externalRect.left+31,g_externalRect.bottom-2},0xE774,ABM_THEME_COLOR_8EE2D3,17);
    RECT externalLabel={g_externalRect.left+29,g_externalRect.top,g_externalRect.right-6,g_externalRect.bottom};
    Text(dc,L"Open in browser",externalLabel,ABM_THEME_COLOR_B7EBE5,11,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    /* Dedicated second-row toolbar: larger targets with both icon and readable label. */
    const int navY=68,navH=52,navW=104,navGap=10;
    const int navStart=(rc.right-(5*navW+4*navGap))/2;
    g_backRect=RECT{navStart,navY,navStart+navW,navY+navH};
    g_forwardRect=RECT{navStart+(navW+navGap),navY,navStart+2*navW+navGap,navY+navH};
    g_reloadRect=RECT{navStart+2*(navW+navGap),navY,navStart+3*navW+2*navGap,navY+navH};
    g_homeRect=RECT{navStart+3*(navW+navGap),navY,navStart+4*navW+3*navGap,navY+navH};
    g_fullscreenRect=RECT{navStart+4*(navW+navGap),navY,navStart+5*navW+4*navGap,navY+navH};
    const RECT navButtons[5]={g_backRect,g_forwardRect,g_reloadRect,g_homeRect,g_fullscreenRect};
    const wchar_t glyphs[5]={0xE72B,0xE72A,0xE72C,0xE80F,0xE740};
    const wchar_t *labels[5]={L"Back",L"Forward",L"Reload",L"Home",L"Fullscreen"};
    for(int i=0;i<5;i++){
        COLORREF base=(i==4?ABM_THEME_COLOR_1F676F:ABM_THEME_COLOR_183449);COLORREF fill=(g_hoverControl==i)?StreamHoverColor(base,ABM_THEME_COLOR_2B5B71):base;
        Rounded(dc,navButtons[i],fill,12);
        HPEN navPen=CreatePen(PS_SOLID,g_hoverControl==i?2:1,
            g_hoverControl==i?ABM_THEME_COLOR_53D3C7:(i==4?ABM_THEME_COLOR_3E9799:ABM_THEME_COLOR_2B4C63));
        HGDIOBJ oldNavPen=SelectObject(dc,navPen);
        HGDIOBJ oldNavBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        RoundRect(dc,navButtons[i].left,navButtons[i].top,navButtons[i].right,navButtons[i].bottom,12,12);
        SelectObject(dc,oldNavBrush);SelectObject(dc,oldNavPen);DeleteObject(navPen);
        RECT glyph=navButtons[i];glyph.top+=1;glyph.bottom-=19;
        DrawFluentGlyph(dc,glyph,glyphs[i],ABM_THEME_COLOR_EFF8FF,22);
        RECT label=navButtons[i];label.top=navButtons[i].bottom-21;label.bottom-=3;
        Text(dc,labels[i],label,ABM_THEME_COLOR_CCE2EE,10,600,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT underline={navButtons[i].left+18,navButtons[i].bottom-2,navButtons[i].right-18,navButtons[i].bottom};
        if(g_hoverControl==i)FillRectColor(dc,underline,ABM_THEME_COLOR_48D2C7);
    }

    /* Give the provider row a named section and a consistent card baseline. */
    RECT serviceHeading={24,126,420,148};
    Text(dc,L"YOUR STREAMING SERVICES",serviceHeading,ABM_THEME_COLOR_7EC7BE,11,700,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT serviceHint={rc.right-420,126,rc.right-24,148};
    Text(dc,L"OFFICIAL PROVIDER SITES  ·  SIGN IN WITH YOUR ACCOUNT",serviceHint,ABM_THEME_COLOR_708FA6,10,500,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    RECT serviceRule={24,149,rc.right-24,150};FillRectColor(dc,serviceRule,ABM_THEME_COLOR_1E3546);
    const int serviceLeft=22,serviceRight=rc.right-22,serviceY=154,serviceH=95,gap=12;
    const int serviceW=(serviceRight-serviceLeft-(kServiceCount-1)*gap)/kServiceCount;
    for(int i=0;i<kServiceCount;i++){
        int left=serviceLeft+i*(serviceW+gap);
        g_serviceRects[i]=RECT{left,serviceY,left+serviceW,serviceY+serviceH};
        bool active=(i==g_activeService),hover=(i==g_hoverService);
        COLORREF baseFill=active?ABM_THEME_COLOR_195B5B:ABM_THEME_COLOR_112335;
        COLORREF hoverFill=active?ABM_THEME_COLOR_227771:ABM_THEME_COLOR_1C364C;
        COLORREF fill=hover?StreamHoverColor(baseFill,hoverFill):baseFill;
        Rounded(dc,g_serviceRects[i],fill,14);
        RECT border=g_serviceRects[i];
        COLORREF baseBorder=active?ABM_THEME_COLOR_3FC9BE:ABM_THEME_COLOR_1E3549;
        COLORREF hoverBorder=active?ABM_THEME_COLOR_68E8D5:ABM_THEME_COLOR_3D7791;
        HPEN pen=CreatePen(PS_SOLID,active?2:1,hover?hoverBorder:baseBorder);
        HGDIOBJ oldPen=SelectObject(dc,pen);
        HGDIOBJ oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        RoundRect(dc,border.left,border.top,border.right,border.bottom,14,14);
        SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);

        if(g_serviceLogos[i]){
            UINT iw=g_serviceLogos[i]->GetWidth(),ih=g_serviceLogos[i]->GetHeight();
            if(iw&&ih){
                const int maxLogoW=(serviceW>145?112:serviceW-30),maxLogoH=54;
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
            Text(dc,kServices[i],missing,ABM_THEME_COLOR_9AB8CD,15,700,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        RECT label=g_serviceRects[i];
        label.top=serviceY+66;label.bottom=serviceY+87;
        label.left+=5;label.right-=5;
        Text(dc,kServices[i],label,active?ABM_THEME_COLOR_F8FFFF:ABM_THEME_COLOR_D6E4F0,
            13,active||hover?700:600,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(active){
            RECT activeMark={left+serviceW/2-18,serviceY+91,left+serviceW/2+18,serviceY+93};
            FillRectColor(dc,activeMark,ABM_THEME_COLOR_59E0CD);
        }
    }
    RECT footer={0,rc.bottom-34,rc.right,rc.bottom};FillRectColor(dc,footer,ABM_THEME_COLOR_0A1421);
    RECT line={0,rc.bottom-35,rc.right,rc.bottom-34};FillRectColor(dc,line,ABM_THEME_COLOR_273D4F);
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
    Text(dc,footerText,status,g_hoverControl>=0||g_hoverService>=0?ABM_THEME_COLOR_ADD3DC:ABM_THEME_COLOR_85A4BC,11,500,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
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
        if(wp){g_deviceLaunchPulse=true;g_devicePulsePhase=true;SetTimer(hwnd,ID_STREAM_DEVICE_PULSE_TIMER,180,nullptr);InvalidateRect(g_deviceLaunch,nullptr,FALSE);}
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
            if(g_streamButtonCanvasReady&&ABM_D2D_CanvasBegin(&g_streamButtonCanvas,item->hDC,&r)){
                ABM_D2D_CanvasFillRound(&g_streamButtonCanvas,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,ABM_UI_RADIUS_PANEL,ABM_THEME_COLOR_0C1D2D);
                ABM_D2D_CanvasStrokeRound(&g_streamButtonCanvas,(float)r.left+0.5f,(float)r.top+0.5f,(float)r.right-0.5f,(float)r.bottom-0.5f,ABM_UI_RADIUS_PANEL,ABM_THEME_COLOR_2B5B70,1.0f);
                ABM_D2D_CanvasFill(&g_streamButtonCanvas,(float)r.left+16.0f,(float)r.top+157.0f,(float)r.right-16.0f,(float)r.top+158.0f,ABM_THEME_COLOR_267077);
                ABM_D2D_CanvasFill(&g_streamButtonCanvas,(float)r.left+16.0f,(float)r.top+326.0f,(float)r.right-16.0f,(float)r.top+327.0f,ABM_THEME_COLOR_267077);
                ABM_D2D_CanvasEnd(&g_streamButtonCanvas);
                return TRUE;
            }
            FillRectColor(item->hDC,r,ABM_THEME_COLOR_0C1D2D);
            HPEN pen=CreatePen(PS_SOLID,1,ABM_THEME_COLOR_2B5B70);
            HGDIOBJ oldPen=SelectObject(item->hDC,pen),oldBrush=SelectObject(item->hDC,GetStockObject(HOLLOW_BRUSH));
            RoundRect(item->hDC,r.left,r.top,r.right,r.bottom,ABM_UI_RADIUS_PANEL*2,ABM_UI_RADIUS_PANEL*2);
            SelectObject(item->hDC,oldBrush);SelectObject(item->hDC,oldPen);DeleteObject(pen);
            RECT accent={r.left+16,r.top+157,r.right-16,r.top+158};FillRectColor(item->hDC,accent,ABM_THEME_COLOR_267077);
            RECT appDivider={r.left+16,r.top+326,r.right-16,r.top+327};FillRectColor(item->hDC,appDivider,ABM_THEME_COLOR_267077);
            return TRUE;
        }
        if(item->hwndItem==g_deviceTitle||item->hwndItem==g_deviceHint){
            wchar_t label[512]{};GetWindowTextW(item->hwndItem,label,_countof(label));
            if(g_streamButtonCanvasReady&&ABM_D2D_CanvasBegin(&g_streamButtonCanvas,item->hDC,&item->rcItem)){
                ABM_D2D_CanvasText(&g_streamButtonCanvas,label,item->rcItem,
                    item->hwndItem==g_deviceTitle?ABM_THEME_COLOR_F2F9FF:ABM_THEME_COLOR_96BECF,
                    ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                ABM_D2D_CanvasEnd(&g_streamButtonCanvas);
            }else Text(item->hDC,label,item->rcItem,item->hwndItem==g_deviceTitle?ABM_THEME_COLOR_F2F9FF:ABM_THEME_COLOR_96BECF,12,600,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            return TRUE;
        }
        if(item->hwndItem==g_deviceRefresh){DrawDeviceButton(item,L"Refresh devices",false);return TRUE;}
        if(item->hwndItem==g_deviceSelect){DrawDeviceButton(item,L"Use selected TV",true);return TRUE;}
        if(item->hwndItem==g_deviceApps){DrawDeviceButton(item,L"Check TV apps",false);return TRUE;}
        if(item->hwndItem==g_deviceLaunch){DrawDeviceButton(item,L"Launch app",true);return TRUE;}
        if(item->hwndItem==g_deviceClose){DrawDeviceButton(item,L"Close",false);return TRUE;}
        if(item->hwndItem==g_deviceAppStatus){DrawDeviceButton(item,L"Refresh app status",false);return TRUE;}
        if(item->hwndItem==g_deviceAppStop){DrawDeviceButton(item,L"Stop TV",false);return TRUE;}
        if(item->hwndItem==g_deviceList){DrawDeviceListItem(item);return TRUE;}
        if(item->hwndItem==g_deviceAppCombo){
            if(item->itemID==(UINT)-1)return TRUE;
            wchar_t label[256]{};SendMessageW(item->hwndItem,LB_GETTEXT,item->itemID,(LPARAM)label);
            const bool selected=(item->itemState&ODS_SELECTED)!=0;
            if(g_streamButtonCanvasReady&&ABM_D2D_CanvasBegin(&g_streamButtonCanvas,item->hDC,&item->rcItem)){
                ABM_D2D_CanvasFill(&g_streamButtonCanvas,(float)item->rcItem.left,(float)item->rcItem.top,(float)item->rcItem.right,(float)item->rcItem.bottom,selected?ABM_THEME_COLOR_1D5B67:ABM_THEME_COLOR_0F1F2F);
                RECT textRect=item->rcItem;InflateRect(&textRect,-8,0);
                ABM_D2D_CanvasText(&g_streamButtonCanvas,label,textRect,selected?ABM_THEME_COLOR_F5FFFF:ABM_THEME_COLOR_CFE1ED,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                ABM_D2D_CanvasEnd(&g_streamButtonCanvas);
            }else{
                FillRectColor(item->hDC,item->rcItem,selected?ABM_THEME_COLOR_1D5B67:ABM_THEME_COLOR_0F1F2F);
                RECT textRect=item->rcItem;InflateRect(&textRect,-8,0);
                Text(item->hDC,label,textRect,selected?ABM_THEME_COLOR_F5FFFF:ABM_THEME_COLOR_CFE1ED,10,selected?700:500,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            }
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:{
        HDC dc=(HDC)wp;HWND control=(HWND)lp;
        if(control==g_deviceTitle||control==g_deviceHint){
            SetBkMode(dc,TRANSPARENT);
            SetTextColor(dc,control==g_deviceTitle?ABM_THEME_COLOR_F2F9FF:ABM_THEME_COLOR_96BECF);
            static HBRUSH panelBrush=CreateSolidBrush(ABM_THEME_COLOR_0C1D2D);
            return (LRESULT)panelBrush;
        }
        break;
    }
    case WM_CREATE:{
        g_window=hwnd;
        ABM_MotionInit(&g_streamHoverMotion,0.0);g_streamHoverMotionReady=true;
        g_streamButtonCanvasReady=ABM_D2D_CanvasInit(&g_streamButtonCanvas)!=FALSE;
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
        if(wp==ID_STREAM_MOTION_TIMER){
            double value=0.0;ABMMotionValue *motions[1]={&g_streamHoverMotion};
            if(ABM_MotionWinSample(hwnd,ID_STREAM_MOTION_TIMER,motions,&value,1))
                g_streamHoverLevel=value;
            else g_streamHoverLevel=g_streamHoverMotion.target_value;
            InvalidateRect(hwnd,nullptr,FALSE);
            return 0;
        }
        if(wp==ID_STREAM_DEVICE_TIMER){
            if(g_devicePanelVisible)RefreshDevicePanel(false);
            if(IsStreamReceiverReady())InvalidateRect(hwnd,&g_castRect,FALSE);
            return 0;
        }
        if(wp==ID_STREAM_DEVICE_PULSE_TIMER){
            if(!g_deviceSelectPulse&&!g_deviceLaunchPulse){KillTimer(hwnd,ID_STREAM_DEVICE_PULSE_TIMER);return 0;}
            g_devicePulsePhase=!g_devicePulsePhase;
            if(g_deviceSelectPulse&&g_deviceSelect)InvalidateRect(g_deviceSelect,nullptr,FALSE);
            if(g_deviceLaunchPulse&&g_deviceLaunch)InvalidateRect(g_deviceLaunch,nullptr,FALSE);
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
        else if(PtInRect(&g_stopTvRect,pt))control=10;
        if(service!=g_hoverService||control!=g_hoverControl){
            g_hoverService=service;g_hoverControl=control;
            if(g_streamHoverMotionReady){
                ABM_MotionStart(&g_streamHoverMotion,(service>=0||control>=0)?100.0:0.0,
                    ABM_MOTION_DURATION_QUICK,ABM_MOTION_EASE_OUT);
                if(!ABM_MotionWinSchedule(hwnd,ID_STREAM_MOTION_TIMER))g_streamHoverLevel=(service>=0||control>=0)?100.0:0.0;
            }
            InvalidateRect(hwnd,nullptr,FALSE);
        }
        SetCursor((service>=0||control>=0)?LoadCursorW(nullptr,IDC_HAND):LoadCursorW(nullptr,IDC_ARROW));
        return 0;
    }
    case WM_MOUSELEAVE:
        g_trackingMouseLeave=false;g_hoverService=-1;g_hoverControl=-1;
        if(g_streamHoverMotionReady){ABM_MotionStart(&g_streamHoverMotion,0.0,ABM_MOTION_DURATION_QUICK,ABM_MOTION_EASE_OUT);if(!ABM_MotionWinSchedule(hwnd,ID_STREAM_MOTION_TIMER))g_streamHoverLevel=0.0;}
        SetCursor(LoadCursorW(nullptr,IDC_ARROW));InvalidateRect(hwnd,nullptr,FALSE);return 0;
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
        if(PtInRect(&g_stopTvRect,POINT{x,y})){
            if(!IsStreamReceiverReady()){SetStatus(L"Find TVs and select a receiver before stopping its app.");return 0;}
            if(g_streamAppChoices.empty()){SetStatus(L"Open Find TVs, select the TV app to stop, then use Stop TV here.");ShowDevicePanel();return 0;}
            LRESULT appSel=SendMessageW(g_deviceAppCombo,LB_GETCURSEL,0,0);
            LRESULT choiceIndex=appSel==LB_ERR?LB_ERR:SendMessageW(g_deviceAppCombo,LB_GETITEMDATA,(WPARAM)appSel,0);
            if(choiceIndex==LB_ERR||choiceIndex<0||(size_t)choiceIndex>=g_streamAppChoices.size()){
                SetStatus(L"Select the running TV app in Find TVs before pressing Stop TV.");ShowDevicePanel();return 0;
            }
            SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(ID_STREAM_DEVICE_APP_STOP,0),0);
            return 0;
        }
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
                g_deviceSelectPulse=true;g_devicePulsePhase=true;
                SetTimer(hwnd,ID_STREAM_DEVICE_PULSE_TIMER,180,nullptr);
                InvalidateRect(g_deviceSelect,nullptr,FALSE);
                wchar_t name[256]=L"TV";
                SendMessageW(g_deviceList,LB_GETTEXT,selected,(LPARAM)name);
                g_deviceSelectPulse=true;g_devicePulsePhase=true;
                SetTimer(hwnd,ID_STREAM_DEVICE_PULSE_TIMER,180,nullptr);
                InvalidateRect(g_deviceSelect,nullptr,FALSE);
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
                StartManagedWorker([target,dialIndex,castV2Index](const std::shared_ptr<std::atomic<bool>>& cancel){
                    if(cancel->load())return;
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
                        /* Keep Netflix available as a clearly labelled DIAL POST
                           attempt when the TV does not expose a readable GET state. */
                        bool hasNetflix=false;
                        for(const auto &choice:payload->choices)
                            if(choice.dialApp==L"Netflix")hasNetflix=true;
                        if(!hasNetflix)
                            payload->choices.push_back({L"Netflix — try DIAL launch",L"Netflix",false});
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
                    if(cancel->load()||!IsWindow(target)||!PostMessageW(target,WM_APP_DIAL_APPS_READY,0,(LPARAM)payload))delete payload;
                });
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
                StartManagedWorker([target,choice,dialIndex,castV2Index](const std::shared_ptr<std::atomic<bool>>& cancel){
                    if(cancel->load())return;
                    std::wstring result;
                    bool launchConfirmed=false;
                    if(choice.castV2){
                        StreamingDiag(L"Dispatching Google Cast launch label=%s appId=%s receiverIndex=%d",
                            choice.label.c_str(),choice.castAppId.c_str(),castV2Index);
                        bool ok=castV2Index>=0&&ABM_CastV2_LaunchApplication((unsigned int)castV2Index,choice.castAppId.c_str())!=FALSE;
                        if(!ok)result=L"Google Cast could not confirm the requested receiver app launch. Check GoCast diagnostics.";
                        else if(choice.castAppId==L"17608BC8")
                            result=L"Prime Video Cast receiver app launch confirmed. This is only an app-launch test; Amazon's title/session handoff is not yet implemented. Use Prime Video's official mobile Cast control to start a title.";
                        else
                            result=L"YouTube app launch confirmed by Google Cast. Use the TV remote to choose content.";
                        launchConfirmed=ok;
                    }else{
                        int code=dialIndex>=0?ABM_Dial_LaunchApplication((unsigned int)dialIndex,choice.dialApp.c_str()):-1;
                        launchConfirmed=(code==1);
                        if(code==1)result=L"The TV accepted the "+choice.dialApp+L" app launch. Use Refresh app status to check the DIAL app state or Stop TV app to close it. These are app-level controls; playback pause/resume/seek and browser-title transfer require a provider-supported sender session.";
                        else if(code==0)result=L"The TV no longer exposes the "+choice.dialApp+L" app through DIAL.";
                        else if(code==-1)result=L"Could not reach the TV's DIAL app endpoint.";
                        else result=L"The TV rejected the "+choice.dialApp+L" app launch request.";
                    }
                    g_appProbeRunning=false;
                    auto *payload=new std::wstring(std::move(result));
                    if(cancel->load()||!IsWindow(target)||!PostMessageW(target,WM_APP_DIAL_APP_LAUNCH,launchConfirmed?1:0,(LPARAM)payload))delete payload;
                });
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
            const bool stop=(LOWORD(wp)==ID_STREAM_DEVICE_APP_STOP);
            if(choice.castV2){
                const int castIndex=g_streamSelectedCastV2Index;
                const HWND target=hwnd;
                SetStatus(stop?L"Requesting Google Cast to stop the selected TV application…":L"Refreshing Google Cast application status…");
                try{
                    StartManagedWorker([target,castIndex,choice,stop](const std::shared_ptr<std::atomic<bool>>& cancel){
                    if(cancel->load())return;
                        int code=-1;
                        if(castIndex>=0){
                            code=stop?ABM_CastV2_StopApplication((unsigned int)castIndex,choice.castAppId.c_str())
                                     :ABM_CastV2_QueryApplicationStatus((unsigned int)castIndex,choice.castAppId.c_str());
                        }
                        std::wstring message;
                        if(stop){
                            message=code==1?L"Google Cast confirmed that "+choice.label+L" stopped on the TV."
                                :code==0?L"The selected Google Cast app is not currently running on the TV."
                                :L"The TV did not confirm that the Google Cast app stopped. Use the TV remote if it remains open.";
                        }else{
                            message=code==1?choice.label+L" is RUNNING on the TV (Google Cast receiver status). This does not indicate whether a movie is playing or paused."
                                :code==0?choice.label+L" is NOT RUNNING on the TV."
                                :L"Could not refresh Google Cast app status. Check the TV connection and GoCast diagnostics.";
                        }
                        g_appProbeRunning=false;
                        auto *payload=new std::wstring(std::move(message));
                        if(cancel->load()||!IsWindow(target)||!PostMessageW(target,WM_APP_DIAL_APP_LAUNCH,0,(LPARAM)payload))delete payload;
                    });
                }catch(...){g_appProbeRunning=false;SetStatus(stop?L"Could not start the Google Cast stop request.":L"Could not start the app status query.");}
                return 0;
            }
            if(choice.dialApp.empty()||g_streamSelectedDialIndex<0){
                g_appProbeRunning=false;
                SetStatus(L"This app has no supported receiver-control endpoint. Use the TV remote or provider mobile app.");
                return 0;
            }
            const int dialIndex=g_streamSelectedDialIndex;
            const HWND target=hwnd;
            SetStatus(stop?L"Sending standard DIAL stop request to the TV…":L"Checking TV application state…");
            try{
                StartManagedWorker([target,dialIndex,choice,stop](const std::shared_ptr<std::atomic<bool>>& cancel){
                    if(cancel->load())return;
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
                    if(cancel->load()||!IsWindow(target)||!PostMessageW(target,WM_APP_DIAL_APP_LAUNCH,0,(LPARAM)payload))delete payload;
                });
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
        g_appProbeRunning.store(false);
        StopManagedWorkers();
        DrainPendingWorkerMessages(hwnd);
        ABM_Cast_StreamDiscoveryStop();ABM_Dlna_StopDiscovery();ABM_Dial_StopDiscovery();
        KillTimer(hwnd,ID_STREAM_DEVICE_TIMER);ABM_MotionWinStop(hwnd,ID_STREAM_MOTION_TIMER);
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
        g_streamButtonCanvasReady=false;
        ABM_D2D_CanvasDestroy(&g_streamButtonCanvas);
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
    g_workersStopping.store(false);
    ABM_CastV2_ResetOperationCancellation();
    HWND window=CreateWindowExW(0,kClassName,L"ABM AV Player - Streaming Centre",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1160,820,
        nullptr,nullptr,instance,nullptr);
    if(!window){SetStatus(L"Could not create the Streaming Centre window.");return;}
    BOOL dark=TRUE;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    int corners=2;COLORREF border=ABM_UI_COLOR_BORDER_STRONG,caption=ABM_UI_COLOR_SURFACE;
    DwmSetWindowAttribute(window,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
    DwmSetWindowAttribute(window,DWMWA_BORDER_COLOR,&border,sizeof(border));
    DwmSetWindowAttribute(window,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
    ShowWindow(window,SW_SHOW);UpdateWindow(window);
}
extern "C" void ABM_Streaming_RefreshTheme(void){
    if(!g_window || !IsWindow(g_window)) return;
    /* Refresh only ABM-owned chrome; never navigate a provider page. */
    if(g_activeService < 0) NavigateHome();
    BOOL dark=TRUE; DwmSetWindowAttribute(g_window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    int corners=2; COLORREF border=ABM_UI_COLOR_BORDER_STRONG, caption=ABM_UI_COLOR_SURFACE;
    DwmSetWindowAttribute(g_window,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
    DwmSetWindowAttribute(g_window,DWMWA_BORDER_COLOR,&border,sizeof(border));
    DwmSetWindowAttribute(g_window,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
    InvalidateRect(g_window,nullptr,FALSE);
    if(g_devicePanel && IsWindow(g_devicePanel)) InvalidateRect(g_devicePanel,nullptr,FALSE);
}

extern "C" void ABM_Streaming_Close(void){
    g_mainWindow=nullptr;
    if(g_window&&IsWindow(g_window))DestroyWindow(g_window);
}

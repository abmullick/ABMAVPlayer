#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <vlc/vlc.h>
#include <vlc/libvlc_renderer_discoverer.h>
#include <vlc/libvlc_events.h>
#include "ui_d2d.h"
#include "resource.h"
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <shlobj.h>
#include <urlmon.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define APP_NAME L"ABM AV Player"
#define OWNER_TEXT L"Product Owner: Abhisek Basu Mullick  |  © 2026 Abhisek Basu Mullick. All rights reserved."
#define ID_RENDERERS 1010
#define WM_RENDERERS_CHANGED (WM_APP + 10)
#define WM_ARTWORK_READY (WM_APP + 11)
#define WM_ARTWORK_DOWNLOAD_READY (WM_APP + 12)
#define TIMER_ID 1
#define MAX_RENDERERS 32
#define MAX_DISCOVERERS 16
#define MAX_PLAYLIST 256
#define PATH_CAP 1024

static libvlc_instance_t *g_vlc;
static libvlc_media_player_t *g_player;
static HWND g_video, g_renderers, g_main, g_fullscreenOverlay;
static ABM_D2D g_ui;
static HFONT g_font, g_smallFont, g_headingFont;
static HBRUSH g_videoBrush, g_comboBrush;
static CRITICAL_SECTION g_rendererLock;
static libvlc_renderer_discoverer_t *g_discoverers[MAX_DISCOVERERS];
static size_t g_discovererCount;
static libvlc_renderer_item_t *g_rendererItems[MAX_RENDERERS], *g_comboItems[MAX_RENDERERS];
static size_t g_rendererCount, g_comboCount;
static libvlc_renderer_item_t *g_selectedRenderer;
static BOOL g_rendererApplied = FALSE;
static wchar_t g_playlist[MAX_PLAYLIST][PATH_CAP];
static int g_playlistCount, g_current = -1, g_selected = -1;
static int g_volume = 70, g_dragSeek = 0, g_dragVolume = 0, g_hover = 0;
static BOOL g_shuffle = FALSE, g_repeat = FALSE;
static float g_playbackRate = 1.0f;
static BOOL g_endHandled = FALSE;
#define MAX_RECENT 12
static wchar_t g_recent[MAX_RECENT][PATH_CAP];
static int g_recentCount=0;
static wchar_t g_dataDir[PATH_CAP];
static BOOL g_libraryReady=FALSE;
static unsigned int g_visualPulse=0;
static BOOL g_receiverLost=FALSE;
static DWORD g_discoveryStartedTick=0;
static BOOL g_discoveryTimedOut=FALSE;
static wchar_t g_statusText[256]=L"";
static BOOL g_pendingResume=FALSE;
static BOOL g_pendingResumePause=FALSE;
static int64_t g_pendingResumeMs=0;
static int64_t g_forcedResumeMs=0;
static BOOL g_useForcedResume=FALSE;
static unsigned int g_pendingResumeTicks=0;
static BOOL g_visualizerActive=FALSE;
static BOOL g_visualizerTried=FALSE;
static BOOL g_replayGain=TRUE;
static BOOL g_compressor=FALSE;
static int g_eqProfile=0;
static libvlc_media_t *g_artMedia=NULL;
static volatile LONG g_artworkGeneration=0;
static BOOL g_artworkLoaded=FALSE;
static int g_artworkFade=100;
static wchar_t g_trackTitle[256]=L"";
static wchar_t g_trackArtist[256]=L"";
static wchar_t g_trackAlbum[256]=L"";
static wchar_t g_trackGenre[256]=L"";
static int g_playlistScroll = 0, g_hoverButton = 0, g_hoverLevel = 0;
static DWORD g_lastClickTick = 0;
static int g_duration = 0;
static unsigned int g_videoWidth = 0, g_videoHeight = 0;
static RECT g_videoBounds = {0,0,0,0};
static BOOL g_videoBoundsValid = FALSE;
static unsigned int g_videoGeometryPoll = 0;
static int64_t g_lastUiTime = -1;
static int64_t g_overlayLength = 0;
static BOOL g_overlayPlaying = FALSE;
static RECT R_VIDEO, R_SIDEBAR, R_LIST, R_SEEK, R_PLAY, R_PREV, R_NEXT, R_STOP, R_OPEN, R_SCAN, R_VOLUME, R_RECEIVER, R_RECEIVER_LIST;
static RECT R_ROW[MAX_PLAYLIST];
static RECT R_SHUFFLE, R_REPEAT, R_SPEED, R_RECENT;
static RECT R_SEARCH, R_TAB_QUEUE, R_TAB_LIBRARY, R_FILTER_ALL, R_FILTER_AUDIO, R_FILTER_VIDEO, R_GROUP;
static wchar_t g_searchText[128]=L"";
static BOOL g_searchFocused=FALSE;
static int g_libraryTab=0; /* 0 = Queue, 1 = Library */
static int g_mediaFilter=0; /* 0 = All, 1 = Audio, 2 = Video */
static int g_groupMode=0; /* 0 = Folder, 1 = Artist, 2 = Album */
static BOOL g_hasMedia = FALSE;
static BOOL g_receiverPopup = FALSE;
static BOOL g_drawerOpen = FALSE;
static int g_drawerProgress = 0;
static int g_hoverPlaylistIndex = -1;
static BOOL g_drawerSettingsOpen = FALSE;
static BOOL g_menuOpen = FALSE;
static BOOL g_fullscreen = FALSE;
static BOOL g_fullscreenControlsVisible = TRUE;
static DWORD g_fullscreenLastInputTick = 0;
static POINT g_fullscreenLastMousePos = {0,0};
static BOOL g_fullscreenMousePosValid = FALSE;
#define FULLSCREEN_CONTROLS_TIMEOUT_MS 10000u
#define FULLSCREEN_OVERLAY_HEIGHT 160
static DWORD_PTR g_savedStyle = 0;
static WINDOWPLACEMENT g_savedPlacement = { sizeof(WINDOWPLACEMENT) };
static int g_menuProgress = 0;
static int g_hoverMenuItem = -1;
static RECT R_MENU_BUTTON, R_DRAWER_BUTTON, R_SETTINGS_BUTTON, R_MENU_PANEL, R_MENU_ITEMS[13], R_FULLSCREEN;

enum { HIT_NONE, HIT_OPEN, HIT_PLAY, HIT_STOP, HIT_PREV, HIT_NEXT, HIT_SEEK, HIT_VOLUME, HIT_SCAN, HIT_ROW_BASE = 1000 };

static COLORREF C_BG = RGB(10,13,20);
static COLORREF C_PANEL = RGB(18,23,34);
static COLORREF C_PANEL2 = RGB(25,33,48);
static COLORREF C_TEXT = RGB(242,246,255);
static COLORREF C_MUTED = RGB(144,157,179);
static COLORREF C_ACCENT = RGB(91,160,255);
static COLORREF C_LINE = RGB(42,54,73);

static void PlayIndex(int index);
static void SaveAudioPreferences(void);
static void ApplyEqProfile(void);
static void ArtworkParsedEvent(const libvlc_event_t *ev, void *opaque);
static BOOL EnsureDataDirectory(void);
static COLORREF Blend(COLORREF a,COLORREF b,int amount);
static void RoundBox(HDC dc, RECT r, COLORREF c, int radius);
static void Fill(HDC dc, RECT r, COLORREF c) { (void)dc;ABM_D2D_Fill(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,c); }
static void GradientRect(HDC dc, RECT r, COLORREF leftColor, COLORREF rightColor) {
    (void)dc;
    int width = r.right - r.left;
    if (width <= 0) return;
    const int steps = 36;
    for (int i = 0; i < steps; ++i) {
        int x1 = r.left + width * i / steps;
        int x2 = r.left + width * (i + 1) / steps + 1;
        RECT strip = { x1, r.top, x2, r.bottom };
        Fill(NULL, strip, Blend(leftColor, rightColor, i * 100 / (steps - 1)));
    }
}
static void RoundBox(HDC dc, RECT r, COLORREF c, int radius) { (void)dc;ABM_D2D_Round(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,(float)radius,c); }
static void GradientRound(HDC dc,RECT r,COLORREF a,COLORREF b,int radius) { (void)dc;ABM_D2D_GradientRound(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,(float)radius,a,b); }
static void Panel(HDC dc,RECT r,COLORREF fill,COLORREF edge,int radius) {
    (void)dc;
    ABM_D2D_Round(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,(float)radius,fill);
    COLORREF hairline=Blend(fill,edge,22);
    ABM_D2D_StrokeRound(&g_ui,(float)r.left+0.5f,(float)r.top+0.5f,(float)r.right-0.5f,(float)r.bottom-0.5f,(float)radius,hairline,0.75f);
}
static void DrawLine(HDC dc,int x1,int y1,int x2,int y2,COLORREF color,int width) { (void)dc;ABM_D2D_Line(&g_ui,(float)x1,(float)y1,(float)x2,(float)y2,color,(float)width); }
static void Txt(HDC dc, const wchar_t *s, RECT r, COLORREF c, HFONT f, UINT flags) { (void)dc;int style=(f==g_smallFont)?1:((f==g_headingFont)?2:0);ABM_D2D_Text(&g_ui,s,r,c,style,flags); }
static BOOL Inside(RECT r,int x,int y) { return x>=r.left && x<r.right && y>=r.top && y<r.bottom; }
static BOOL ContainsTextI(const wchar_t *text,const wchar_t *query) { if(!query||!query[0])return TRUE;if(!text)return FALSE;size_t n=wcslen(query);for(const wchar_t *s=text;*s;s++)if(!_wcsnicmp(s,query,n))return TRUE;return FALSE; }
static BOOL IsVideoPath(const wchar_t *path) { const wchar_t *d=wcsrchr(path?path:L"",L'.');return d&&(!_wcsicmp(d,L".mp4")||!_wcsicmp(d,L".mkv")||!_wcsicmp(d,L".avi")||!_wcsicmp(d,L".mov")||!_wcsicmp(d,L".wmv")||!_wcsicmp(d,L".webm")||!_wcsicmp(d,L".mpeg")||!_wcsicmp(d,L".mpg")||!_wcsicmp(d,L".m4v")); }
static int CountFilteredMedia(void) {
    int count=0;
    for(int i=0;i<g_playlistCount;i++){
        BOOL video=IsVideoPath(g_playlist[i]);
        if((g_mediaFilter==1&&video)||(g_mediaFilter==2&&!video))continue;
        const wchar_t *leaf=wcsrchr(g_playlist[i],L'\\');leaf=leaf?leaf+1:g_playlist[i];
        if(g_searchText[0]&&!ContainsTextI(leaf,g_searchText)&&
           !(i==g_current&&((g_trackArtist[0]&&ContainsTextI(g_trackArtist,g_searchText))||
                            (g_trackAlbum[0]&&ContainsTextI(g_trackAlbum,g_searchText))||
                            (g_trackTitle[0]&&ContainsTextI(g_trackTitle,g_searchText)))))continue;
        count++;
    }
    return count;
}
static int g_visibleRows[MAX_PLAYLIST],g_visibleRowCount=0;
static COLORREF Blend(COLORREF a,COLORREF b,int amount) {
    if(amount<0)amount=0;if(amount>100)amount=100;
    return RGB((GetRValue(a)*(100-amount)+GetRValue(b)*amount)/100,
               (GetGValue(a)*(100-amount)+GetGValue(b)*amount)/100,
               (GetBValue(a)*(100-amount)+GetBValue(b)*amount)/100);
}
static void SetStatus(const wchar_t *s) { if(s&&s[0]) wcsncpy_s(g_statusText,_countof(g_statusText),s,_TRUNCATE); if(g_main) InvalidateRect(g_main,NULL,FALSE); }
/* Frame-rate-independent easing for the 30 Hz interface animation loop.
   Exponential convergence feels softer than fixed-size jumps and snaps cleanly
   to the target so hit-testing and layout never remain fractionally displaced. */
static int EaseToward(int current,int target,int responsePercent) {
    int delta=target-current;
    if(delta==0)return target;
    if(delta>=-1&&delta<=1)return target;
    int step=(delta*responsePercent)/100;
    if(step==0)step=delta>0?1:-1;
    int next=current+step;
    if((delta>0&&next>target)||(delta<0&&next<target))return target;
    return next;
}
static void ResetArtwork(void) {
    if (g_artMedia) {
        libvlc_event_manager_t *em=libvlc_media_event_manager(g_artMedia);
        libvlc_event_detach(em,libvlc_MediaParsedChanged,ArtworkParsedEvent,NULL);
        libvlc_media_parse_stop(g_artMedia);
        libvlc_media_release(g_artMedia); g_artMedia=NULL;
    }
    InterlockedIncrement(&g_artworkGeneration);
    g_artworkLoaded=FALSE; g_artworkFade=100;
    g_trackTitle[0]=0; g_trackArtist[0]=0; g_trackAlbum[0]=0; g_trackGenre[0]=0;
    ABM_D2D_ClearImage(&g_ui);
}
static void BeginArtworkParse(libvlc_media_t *media) {
    ResetArtwork(); if (!media) return;
    g_artMedia=media; libvlc_media_retain(g_artMedia);
    libvlc_event_manager_t *em=libvlc_media_event_manager(g_artMedia);
    if (libvlc_event_attach(em,libvlc_MediaParsedChanged,ArtworkParsedEvent,NULL)!=0) return;
    libvlc_media_parse_with_options(g_artMedia,libvlc_media_parse_local,5000);
}
static void ArtworkParsedEvent(const libvlc_event_t *ev, void *opaque) {
    (void)opaque;
    if (ev && ev->type==libvlc_MediaParsedChanged && g_main)
        PostMessageW(g_main,WM_ARTWORK_READY,(WPARAM)ev->p_obj,0);
}
static void CopyVlcMeta(libvlc_media_t *media,libvlc_meta_t key,wchar_t *out,size_t cap) {
    char *value=libvlc_media_get_meta(media,key);
    if (!value) { out[0]=0; return; }
    MultiByteToWideChar(CP_UTF8,0,value,-1,out,(int)cap); libvlc_free(value);
}
typedef struct ArtworkDownloadRequest {
    HWND target;
    LONG generation;
    wchar_t url[PATH_CAP*4];
    wchar_t path[PATH_CAP];
} ArtworkDownloadRequest;

static DWORD WINAPI ArtworkDownloadThread(LPVOID parameter) {
    ArtworkDownloadRequest *request=(ArtworkDownloadRequest *)parameter;
    HRESULT com=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    HRESULT result=URLDownloadToFileW(NULL,request->url,request->path,0,NULL);
    if(SUCCEEDED(com))CoUninitialize();
    if(IsWindow(request->target))
        PostMessageW(request->target,WM_ARTWORK_DOWNLOAD_READY,
                     (WPARAM)request->generation,(LPARAM)SUCCEEDED(result));
    free(request);
    return 0;
}

static void LoadParsedArtwork(libvlc_media_t *media) {
    if (!media || media!=g_artMedia) return;
    CopyVlcMeta(media,libvlc_meta_Title,g_trackTitle,256);
    CopyVlcMeta(media,libvlc_meta_Artist,g_trackArtist,256);
    CopyVlcMeta(media,libvlc_meta_Album,g_trackAlbum,256);
    CopyVlcMeta(media,libvlc_meta_Genre,g_trackGenre,256);
    char *art=libvlc_media_get_meta(media,libvlc_meta_ArtworkURL);
    if (art) {
        wchar_t url[PATH_CAP*4];
        int n=MultiByteToWideChar(CP_UTF8,0,art,-1,url,(int)(sizeof(url)/sizeof(url[0])));
        libvlc_free(art);
        if (n>0 && EnsureDataDirectory() && g_main) {
            ArtworkDownloadRequest *request=(ArtworkDownloadRequest *)calloc(1,sizeof(*request));
            if(request) {
                request->target=g_main;
                request->generation=InterlockedCompareExchange(&g_artworkGeneration,0,0);
                wcsncpy_s(request->url,_countof(request->url),url,_TRUNCATE);
                swprintf_s(request->path,PATH_CAP,L"%s\\artwork-%ld.bin",g_dataDir,(long)request->generation);
                HANDLE thread=CreateThread(NULL,0,ArtworkDownloadThread,request,0,NULL);
                if(thread)CloseHandle(thread);else free(request);
            }
        }
    }
    InvalidateRect(g_main,NULL,FALSE);
}
static void FormatTime(int ms,wchar_t *out,size_t n) {
    if(ms<0) ms=0; int sec=ms/1000;
    swprintf_s(out,n,L"%02d:%02d",sec/60,sec%60);
}
static void PositionVideoWindow(void) {
    if (!g_video || !IsWindow(g_video)) return;

    int areaW = R_VIDEO.right - R_VIDEO.left;
    int areaH = R_VIDEO.bottom - R_VIDEO.top;
    if (areaW <= 0 || areaH <= 0) return;

    /* Always fit the complete video frame inside the available area.
       This preserves the source aspect ratio in both windowed and fullscreen
       modes; the surrounding video surface remains black (letterbox/pillarbox). */
    double aspect = (g_videoWidth > 0 && g_videoHeight > 0)
        ? (double)g_videoWidth / (double)g_videoHeight : (16.0 / 9.0);
    int videoW = areaW;
    int videoH = (int)((double)videoW / aspect + 0.5);
    if (videoH > areaH) {
        videoH = areaH;
        videoW = (int)((double)videoH * aspect + 0.5);
    }
    if (videoW < 1 || videoH < 1) return;

    int x = R_VIDEO.left + (areaW - videoW) / 2;
    int y = R_VIDEO.top + (areaH - videoH) / 2;
    RECT next = {x, y, x + videoW, y + videoH};
    if (!g_videoBoundsValid ||
        next.left != g_videoBounds.left || next.top != g_videoBounds.top ||
        next.right != g_videoBounds.right || next.bottom != g_videoBounds.bottom) {
        SetWindowPos(g_video, NULL, x, y, videoW, videoH,
            SWP_NOZORDER | SWP_NOACTIVATE);
        g_videoBounds = next;
        g_videoBoundsValid = TRUE;
        InvalidateRect(g_main, &R_VIDEO, FALSE);
    }
}
static void RefreshVideoGeometry(void) {
    if (!g_player || !g_hasMedia || g_selectedRenderer) return;
    unsigned int width = 0, height = 0;
    if (libvlc_video_get_size(g_player, 0, &width, &height) == 0 &&
        width > 0 && height > 0 &&
        (width != g_videoWidth || height != g_videoHeight)) {
        g_videoWidth = width;
        g_videoHeight = height;
        g_videoBoundsValid = FALSE;
        PositionVideoWindow();
    }
}
static void UpdateLayout(HWND hwnd) {
    RECT c; GetClientRect(hwnd,&c); int w=c.right,h=c.bottom;
    int left=28,top=108,bottom=164,gap=22;
    int side=w>=1700?420:(w>=1400?390:(w>=1180?350:320));
    int drawerWidth=side*g_drawerProgress/100;
    int videoW=w-drawerWidth-left*2-(g_drawerProgress?gap*g_drawerProgress/100:0); if(videoW<260)videoW=260;
    int videoH=h-top-bottom; if(videoH<140)videoH=140;
    R_VIDEO=(RECT){left,top,left+videoW,top+videoH};
    int drawerLeft=R_VIDEO.right+(gap*g_drawerProgress/100)+side-drawerWidth;
    R_SIDEBAR=(RECT){drawerLeft,top,drawerLeft+side,h-18};
    int settingsShift=g_drawerSettingsOpen?44:0;
    int queueShift=g_drawerSettingsOpen?44:-120;
    R_RECEIVER=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+66+settingsShift,R_SIDEBAR.right-14,R_SIDEBAR.top+98+settingsShift};
    R_MENU_BUTTON=(RECT){w-190,17,w-146,61};
    R_DRAWER_BUTTON=(RECT){w-132,17,w-18,61};
    R_MENU_PANEL=(RECT){w-350,66,w-18,66+580};
    /* Reserve a dedicated 54 px header zone above the first command row. */
    for(int i=0;i<13;i++)R_MENU_ITEMS[i]=(RECT){R_MENU_PANEL.left+10,R_MENU_PANEL.top+54+i*39,R_MENU_PANEL.right-10,R_MENU_PANEL.top+90+i*39};
    R_FULLSCREEN=(RECT){R_VIDEO.right-48,R_VIDEO.top+14,R_VIDEO.right-14,R_VIDEO.top+48};
    if(g_fullscreen) {
        R_VIDEO=(RECT){0,0,w,h};
        R_SEEK=(RECT){82,h-112,w-310,h-108};
        R_PREV=(RECT){w/2-112,h-60,w/2-64,h-12};
        R_PLAY=(RECT){w/2-30,h-66,w/2+30,h-6};
        R_NEXT=(RECT){w/2+64,h-60,w/2+112,h-12};
        R_STOP=(RECT){w/2+132,h-60,w/2+180,h-12};
        R_VOLUME=(RECT){w-148,h-43,w-38,h-25};
        PositionVideoWindow();
        ShowWindow(g_renderers,SW_HIDE);
        return;
    }
    PositionVideoWindow();
    if(g_renderers)ShowWindow(g_renderers,SW_HIDE);
    R_SEEK=(RECT){left+42,h-145,R_VIDEO.right-4,h-141};
    R_PREV=(RECT){w/2-108,h-91,w/2-64,h-47};
    R_PLAY=(RECT){w/2-32,h-103,w/2+32,h-39};
    R_NEXT=(RECT){w/2+64,h-91,w/2+108,h-47};
    R_STOP=(RECT){w/2+126,h-91,w/2+170,h-47};
    R_OPEN=(RECT){left+8,h-94,left+142,h-48};
    R_SETTINGS_BUTTON=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+12,R_SIDEBAR.right-14,R_SIDEBAR.top+48};
    R_SCAN=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+32+settingsShift,R_SIDEBAR.right-14,R_SIDEBAR.top+62+settingsShift};
    R_VOLUME=(RECT){R_SIDEBAR.left+92,h-59,R_SIDEBAR.right-30,h-51};
    R_SHUFFLE=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+124+settingsShift,R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2-4,R_SIDEBAR.top+152+settingsShift};
    R_REPEAT=(RECT){R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2+4,R_SIDEBAR.top+124+settingsShift,R_SIDEBAR.right-14,R_SIDEBAR.top+152+settingsShift};
    R_SPEED=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+156+settingsShift,R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2-4,R_SIDEBAR.top+184+settingsShift};
    R_RECENT=(RECT){R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2+4,R_SIDEBAR.top+156+settingsShift,R_SIDEBAR.right-14,R_SIDEBAR.top+184+settingsShift};
    R_SEARCH=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+214+queueShift,R_SIDEBAR.right-14,R_SIDEBAR.top+250+queueShift};
    R_TAB_QUEUE=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+256+queueShift,R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2-3,R_SIDEBAR.top+288+queueShift};
    R_TAB_LIBRARY=(RECT){R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2+3,R_SIDEBAR.top+256+queueShift,R_SIDEBAR.right-14,R_SIDEBAR.top+288+queueShift};
    R_FILTER_ALL=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+294+queueShift,R_SIDEBAR.left+72,R_SIDEBAR.top+318+queueShift};
    R_FILTER_AUDIO=(RECT){R_SIDEBAR.left+78,R_SIDEBAR.top+294+queueShift,R_SIDEBAR.left+145,R_SIDEBAR.top+318+queueShift};
    R_FILTER_VIDEO=(RECT){R_SIDEBAR.left+151,R_SIDEBAR.top+294+queueShift,R_SIDEBAR.left+218,R_SIDEBAR.top+318+queueShift};
    R_GROUP=(RECT){R_SIDEBAR.right-102,R_SIDEBAR.top+294+queueShift,R_SIDEBAR.right-14,R_SIDEBAR.top+318+queueShift};
    R_LIST=(RECT){R_SIDEBAR.left+10,R_SIDEBAR.top+326+queueShift,R_SIDEBAR.right-10,R_SIDEBAR.bottom-94};
    for(int i=0;i<MAX_PLAYLIST;i++) R_ROW[i]=(RECT){R_LIST.left,R_LIST.top+(i-g_playlistScroll)*68,R_LIST.right,R_LIST.top+(i-g_playlistScroll+1)*68};
}
static void DrawIconButton(HDC dc,RECT r,int kind,BOOL hot,BOOL active) {
    (void)dc;
    int cx=(r.left+r.right)/2,cy=(r.top+r.bottom)/2;
    int diameter=(r.right-r.left<r.bottom-r.top)?r.right-r.left:r.bottom-r.top;
    int radius=diameter/2;
    COLORREF glyph=RGB(224,231,240);
    if(active) {
        int pulse=(int)(5.0+5.0*(0.5+0.5*sin((double)g_visualPulse*0.11)));
        RECT halo=r; InflateRect(&halo,3,3);
        RoundBox(NULL,halo,Blend(RGB(16,37,65),RGB(34,88,151),pulse*2),radius+4);
        GradientRound(NULL,r,RGB(38,133,224),RGB(105,91,239),radius);
        ABM_D2D_StrokeRound(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,(float)radius,RGB(142,196,255),1.15f);
        glyph=RGB(255,255,255);
    } else {
        COLORREF base=Blend(RGB(22,31,47),RGB(42,61,88),hot?g_hoverLevel:0);
        RoundBox(NULL,r,base,radius);
        if(hot) {
            RECT sheen={r.left+3,r.top+2,r.right-3,r.top+5};
            GradientRect(NULL,sheen,RGB(52,153,225),RGB(126,105,241));
        }
        ABM_D2D_StrokeRound(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,(float)radius,hot?RGB(91,143,205):RGB(48,65,91),1.0f);
    }
    if(kind==1) {
        DrawLine(NULL,cx-6,cy-11,cx+10,cy,glyph,2);DrawLine(NULL,cx+10,cy,cx-6,cy+11,glyph,2);DrawLine(NULL,cx-6,cy+11,cx-6,cy-11,glyph,2);
    } else if(kind==2) {
        DrawLine(NULL,cx-5,cy-9,cx-5,cy+9,glyph,2);DrawLine(NULL,cx+5,cy-9,cx+5,cy+9,glyph,2);
    } else if(kind==3) {
        RECT stop={cx-6,cy-6,cx+6,cy+6};RoundBox(NULL,stop,glyph,2);
    } else if(kind==4) {
        DrawLine(NULL,cx-8,cy-8,cx+4,cy,glyph,2);DrawLine(NULL,cx+4,cy,cx-8,cy+8,glyph,2);DrawLine(NULL,cx+7,cy-9,cx+7,cy+9,glyph,2);
    } else if(kind==5) {
        DrawLine(NULL,cx+8,cy-8,cx-4,cy,glyph,2);DrawLine(NULL,cx-4,cy,cx+8,cy+8,glyph,2);DrawLine(NULL,cx-7,cy-9,cx-7,cy+9,glyph,2);
    }
}
static void DrawVisualizer(HDC dc,RECT area) {
    float levels[24]={0};
    ABM_AudioVisualizer_GetBands(levels,24);
    Fill(dc,area,RGB(7,11,18));
    RECT inset=area; InflateRect(&inset,-22,-24);
    int width=inset.right-inset.left;
    int gap=5;
    int barWidth=(width-gap*23)/24;
    if(barWidth<2)barWidth=2;
    int base=inset.bottom-8;
    int maxHeight=inset.bottom-inset.top-34;
    for(int i=0;i<24;i++) {
        float level=levels[i];
        if(level<0.015f)level=0.015f;
        if(level>1.0f)level=1.0f;
        int height=(int)(level*(float)maxHeight);
        int x=inset.left+i*(barWidth+gap);
        RECT bar={x,base-height,x+barWidth,base};
        COLORREF color=Blend(RGB(38,105,181),RGB(74,210,190),(i*100)/23);
        ABM_D2D_Round(&g_ui,(float)bar.left,(float)bar.top,(float)bar.right,(float)bar.bottom,(float)(barWidth/2),color);
    }
    RECT label={area.left+20,area.top+14,area.right-20,area.top+38};
    Txt(dc,L"NOW PLAYING  •  LIVE AUDIO SPECTRUM",label,RGB(153,174,201),g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RECT baseline={inset.left,base+8,inset.right,base+9};
    Fill(dc,baseline,RGB(36,54,76));
}
static void DrawUI(HDC dc,RECT client) {
    if(g_fullscreen) {
        /* Immersive now-playing view: artwork, track metadata and transport
           controls are composed as one screen rather than a centered cover. */
        Fill(dc,client,RGB(10,11,13));
        if(g_artworkLoaded) {
            int artSize=client.bottom-270;
            if(artSize>500)artSize=500;
            if(artSize>client.right/2-90)artSize=client.right/2-90;
            if(artSize<250)artSize=250;
            int artX=(client.right/2-artSize)/2;
            int artY=(client.bottom-170-artSize)/2+12;
            RECT tile={artX,artY,artX+artSize,artY+artSize};
            Panel(dc,tile,RGB(20,22,25),RGB(66,70,76),18);
            ABM_D2D_DrawImageCover(&g_ui,(float)tile.left+7,(float)tile.top+7,
                (float)tile.right-7,(float)tile.bottom-7,(float)g_artworkFade/100.0f);

            int infoX=client.right/2+34;
            int infoW=client.right-infoX-62;
            RECT kicker={infoX,artY+8,infoX+infoW,artY+34};
            RECT title={infoX,artY+50,infoX+infoW,artY+105};
            RECT artist={infoX,artY+112,infoX+infoW,artY+145};
            Txt(dc,L"NOW PLAYING",kicker,RGB(174,179,186),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            Txt(dc,g_trackTitle[0]?g_trackTitle:L"Untitled track",title,C_TEXT,g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            Txt(dc,g_trackArtist[0]?g_trackArtist:L"Unknown artist",artist,RGB(194,198,204),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

            int cardY=artY+184;
            RECT artistCard={infoX,cardY,infoX+infoW,cardY+112};
            Panel(dc,artistCard,RGB(26,28,31),RGB(53,56,61),15);
            RECT artistHead={infoX+18,cardY+12,infoX+infoW-18,cardY+37};
            RECT artistName={infoX+18,cardY+45,infoX+infoW-18,cardY+72};
            RECT artistRole={infoX+18,cardY+75,infoX+infoW-18,cardY+98};
            Txt(dc,L"ABOUT THE ARTIST",artistHead,RGB(185,190,197),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            Txt(dc,g_trackArtist[0]?g_trackArtist:L"Artist information unavailable",artistName,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            Txt(dc,L"Artist",artistRole,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

            RECT credits={infoX,cardY+128,infoX+infoW,cardY+242};
            Panel(dc,credits,RGB(26,28,31),RGB(53,56,61),15);
            RECT creditsHead={infoX+18,credits.top+12,infoX+infoW-18,credits.top+37};
            Txt(dc,L"TRACK DETAILS",creditsHead,RGB(185,190,197),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT albumLabel={infoX+18,credits.top+45,infoX+110,credits.top+67};
            RECT albumValue={infoX+118,credits.top+45,infoX+infoW-18,credits.top+67};
            RECT genreLabel={infoX+18,credits.top+73,infoX+110,credits.top+95};
            RECT genreValue={infoX+118,credits.top+73,infoX+infoW-18,credits.top+95};
            Txt(dc,L"Album",albumLabel,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            Txt(dc,g_trackAlbum[0]?g_trackAlbum:L"Not provided",albumValue,C_TEXT,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            Txt(dc,L"Genre",genreLabel,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            Txt(dc,g_trackGenre[0]?g_trackGenre:L"Not provided",genreValue,C_TEXT,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        } else if(g_visualizerActive) {
            DrawVisualizer(dc,client);
        } else {
            RECT empty={client.right/2-260,client.bottom/2-30,client.right/2+260,client.bottom/2+20};
            Txt(dc,L"Nothing is playing",empty,C_TEXT,g_headingFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }

        return;
    }
    Fill(dc,client,C_BG);
    RECT header={0,0,client.right,96};
    GradientRect(dc,header,RGB(13,21,35),RGB(10,14,23));
    Fill(dc,(RECT){0,95,client.right,96},RGB(43,45,49));
    /* A restrained moving light sweep gives the top bar a little life without
       competing with the video or increasing the media-rendering workload. */
    Fill(dc,(RECT){0,92,client.right,95},RGB(23,31,43));
    int sweepWidth=156;
    int sweepX=(int)((g_visualPulse*3u)%(unsigned int)(client.right+sweepWidth))-sweepWidth;
    RECT sweep={sweepX,92,sweepX+sweepWidth,95};
    GradientRect(dc,sweep,RGB(36,118,166),RGB(91,83,190));
    /* Signature mark: a luminous A-monogram fused with a play glyph. */
    RECT logoHalo={24,9,96,81};
    Panel(dc,logoHalo,RGB(17,29,49),RGB(64,125,190),22);
    RECT logo={31,16,89,74};
    GradientRound(dc,logo,RGB(27,164,207),RGB(111,82,224),17);
    RECT logoInset={35,20,85,70};
    ABM_D2D_StrokeRound(&g_ui,(float)logoInset.left,(float)logoInset.top,
        (float)logoInset.right,(float)logoInset.bottom,14.0f,RGB(190,229,255),0.8f);
    /* Geometric A: crisp strokes remain legible at small window sizes. */
    DrawLine(dc,43,59,57,31,RGB(255,255,255),3);
    DrawLine(dc,57,31,70,59,RGB(255,255,255),3);
    DrawLine(dc,49,48,64,48,RGB(226,245,255),2);
    /* Play triangle, offset into the A to make the mark distinctive. */
    DrawLine(dc,62,40,75,48,RGB(255,255,255),2);
    DrawLine(dc,75,48,62,56,RGB(255,255,255),2);
    DrawLine(dc,62,56,62,40,RGB(255,255,255),2);

    RECT brandMain={111,8,420,49};
    Txt(dc,L"ABM",brandMain,RGB(247,250,255),g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT brandSuffix={180,20,390,48};
    Txt(dc,L"AV  PLAYER",brandSuffix,RGB(130,190,255),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT brandRule={112,51,294,53};
    GradientRect(dc,brandRule,RGB(43,188,224),RGB(125,101,237));
    RECT sub={112,57,540,80};
    Txt(dc,L"IMMERSIVE AUDIO  •  VIDEO",sub,RGB(165,184,208),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT menuBtn=R_MENU_BUTTON;
    POINT chromeCursor={0,0}; BOOL menuHot=GetCursorPos(&chromeCursor)&&ScreenToClient(g_main,&chromeCursor)&&Inside(menuBtn,chromeCursor.x,chromeCursor.y);
    BOOL drawerHot=GetCursorPos(&chromeCursor)&&ScreenToClient(g_main,&chromeCursor)&&Inside(R_DRAWER_BUTTON,chromeCursor.x,chromeCursor.y);
    RoundBox(dc,menuBtn,g_menuOpen?RGB(34,76,125):Blend(RGB(19,31,49),RGB(38,58,84),menuHot?g_hoverLevel:0),13);
    ABM_D2D_StrokeRound(&g_ui,(float)menuBtn.left,(float)menuBtn.top,(float)menuBtn.right,(float)menuBtn.bottom,13.0f,menuHot?RGB(84,147,218):RGB(46,69,99),1.0f);
    for(int i=0;i<3;i++)DrawLine(dc,menuBtn.left+12,menuBtn.top+13+i*7,menuBtn.right-12,menuBtn.top+13+i*7,RGB(226,236,249),2);
    RECT drawerBtn=R_DRAWER_BUTTON;
    Panel(dc,drawerBtn,g_drawerOpen?RGB(32,78,132):Blend(RGB(17,29,47),RGB(36,57,84),drawerHot?g_hoverLevel:0),g_drawerOpen?RGB(92,165,242):(drawerHot?RGB(80,132,191):RGB(47,69,98)),15);
    DrawLine(dc,drawerBtn.left+13,drawerBtn.top+15,drawerBtn.left+29,drawerBtn.top+15,RGB(220,235,252),2);
    DrawLine(dc,drawerBtn.left+13,drawerBtn.top+22,drawerBtn.left+29,drawerBtn.top+22,RGB(220,235,252),2);
    DrawLine(dc,drawerBtn.left+13,drawerBtn.top+29,drawerBtn.left+29,drawerBtn.top+29,RGB(220,235,252),2);
    RECT drawerLabel={drawerBtn.left+36,drawerBtn.top+3,drawerBtn.right-8,drawerBtn.bottom-3};
    Txt(dc,L"MEDIA",drawerLabel,RGB(224,235,249),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT status={client.right-470,28,client.right-278,59};
    Panel(dc,status,RGB(17,30,47),RGB(55,91,137),16);
    RECT led={status.left+13,status.top+14,status.left+21,status.top+22};RoundBox(dc,led,g_hasMedia?RGB(63,205,145):RGB(100,117,139),5);
    RECT statusText={status.left+29,status.top+2,status.right-8,status.bottom-2};
    Txt(dc,g_statusText[0]?g_statusText:(g_hasMedia?L"READY TO PLAY":L"LOCAL PLAYER"),statusText,C_TEXT,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

    /* Video surface is a separate LibVLC child window. Keep the surrounding frame elegant. */
    RECT frame=R_VIDEO;InflateRect(&frame,3,3);
    int glow=(int)(18.0+10.0*(0.5+0.5*sin((double)g_visualPulse*0.08)));
    COLORREF edge=Blend(RGB(35,53,78),C_ACCENT,glow);
    Panel(dc,frame,RGB(8,11,16),edge,13);
    Fill(dc,R_VIDEO,RGB(0,0,0));
    if(g_artworkLoaded) {
        int areaW=R_VIDEO.right-R_VIDEO.left, areaH=R_VIDEO.bottom-R_VIDEO.top;
        ABM_D2D_DrawImageCover(&g_ui,(float)R_VIDEO.left,(float)R_VIDEO.top,
            (float)R_VIDEO.right,(float)R_VIDEO.bottom,0.16f*(float)g_artworkFade/100.0f);
        int cover=areaH-92;
        if(cover>420)cover=420;
        if(cover>areaW/2)cover=areaW/2;
        if(cover<190)cover=190;
        int tileX=R_VIDEO.left+42;
        int tileY=R_VIDEO.top+(areaH-cover)/2;
        RECT tile={tileX,tileY,tileX+cover,tileY+cover};
        Panel(dc,tile,RGB(13,20,32),RGB(72,119,178),19);
        ABM_D2D_DrawImageCover(&g_ui,(float)tile.left+8,(float)tile.top+8,
            (float)tile.right-8,(float)tile.bottom-8,(float)g_artworkFade/100.0f);

        int infoX=tile.right+54;
        RECT kicker={infoX,R_VIDEO.top+areaH/2-116,R_VIDEO.right-42,R_VIDEO.top+areaH/2-88};
        RECT title={infoX,R_VIDEO.top+areaH/2-76,R_VIDEO.right-42,R_VIDEO.top+areaH/2-25};
        RECT artist={infoX,R_VIDEO.top+areaH/2-17,R_VIDEO.right-42,R_VIDEO.top+areaH/2+16};
        RECT detail={infoX,R_VIDEO.top+areaH/2+38,R_VIDEO.right-42,R_VIDEO.top+areaH/2+64};
        Txt(dc,L"NOW PLAYING  /  ABM AUDIO",kicker,RGB(92,184,239),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if(g_trackTitle[0]) Txt(dc,g_trackTitle,title,C_TEXT,g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        else Txt(dc,L"Untitled track",title,C_TEXT,g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if(g_trackArtist[0]) Txt(dc,g_trackArtist,artist,RGB(191,207,229),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        else Txt(dc,L"Your personal listening session",artist,RGB(191,207,229),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        int64_t trackLength=g_player?libvlc_media_player_get_length(g_player):0;
        wchar_t duration[48]; FormatTime((int)trackLength,duration,48);
        Txt(dc,duration,detail,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        int waveX=infoX, waveY=detail.bottom+32, waveW=R_VIDEO.right-infoX-48;
        int bars=36, gap=5, bw=(waveW-gap*(bars-1))/bars;
        if(bw<2)bw=2;
        for(int i=0;i<bars;i++) {
            double pulse=0.22+0.78*(0.5+0.5*sin((double)g_visualPulse*0.095+i*0.47));
            int bh=5+(int)(pulse*25.0);
            RECT bar={waveX+i*(bw+gap),waveY-bh/2,waveX+i*(bw+gap)+bw,waveY+bh/2};
            COLORREF col=Blend(RGB(44,169,218),RGB(125,91,229),i*100/(bars-1));
            RoundBox(dc,bar,col,3);
        }
    } else if(g_visualizerActive) DrawVisualizer(dc,R_VIDEO);
    if(!g_hasMedia) {
        int cx=(R_VIDEO.left+R_VIDEO.right)/2;
        int cy=(R_VIDEO.top+R_VIDEO.bottom)/2-28;
        int breathe=(int)(3.0+3.0*(0.5+0.5*sin((double)g_visualPulse*0.075)));
        RECT outer={cx-48-breathe,cy-48-breathe,cx+48+breathe,cy+48+breathe};
        ABM_D2D_StrokeRound(&g_ui,(float)outer.left,(float)outer.top,(float)outer.right,(float)outer.bottom,27.0f,Blend(RGB(34,83,132),RGB(87,91,206),(g_visualPulse%80u)*100/79),1.5f);
        RECT badge={cx-39,cy-39,cx+39,cy+39};
        GradientRect(dc,badge,RGB(27,112,164),RGB(77,73,173));
        RoundBox(dc,badge,Blend(RGB(27,112,164),RGB(77,73,173),48),23);
        DrawLine(dc,cx-8,cy-16,cx-8,cy+16,RGB(245,250,255),3);
        DrawLine(dc,cx-8,cy-16,cx+17,cy,RGB(245,250,255),3);
        DrawLine(dc,cx+17,cy,cx-8,cy+16,RGB(245,250,255),3);
        RECT empty={R_VIDEO.left+20,cy+57,R_VIDEO.right-20,cy+86};
        Txt(dc,L"Your next favourite starts here",empty,RGB(232,241,255),g_headingFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT hint={R_VIDEO.left+20,empty.bottom+2,R_VIDEO.right-20,empty.bottom+26};
        Txt(dc,L"Drop a track or add media to build your listening space",hint,C_MUTED,g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        int bars=25, gap=7, barW=5;
        int total=bars*barW+(bars-1)*gap;
        int start=cx-total/2, base=hint.bottom+25;
        for(int i=0;i<bars;i++) {
            double wave=0.5+0.5*sin((double)g_visualPulse*0.12+(double)i*0.52);
            int bh=5+(int)(wave*19.0);
            RECT bar={start+i*(barW+gap),base-bh,start+i*(barW+gap)+barW,base};
            COLORREF bc=Blend(RGB(47,153,207),RGB(125,94,232),i*100/(bars-1));
            RoundBox(dc,bar,bc,3);
        }
    }

    RECT fs=R_FULLSCREEN;
    RoundBox(dc,fs,RGB(20,29,42),8);
    DrawLine(dc,fs.left+9,fs.top+13,fs.left+9,fs.top+7,C_TEXT,2);
    DrawLine(dc,fs.left+9,fs.top+7,fs.left+15,fs.top+7,C_TEXT,2);
    DrawLine(dc,fs.right-9,fs.top+13,fs.right-9,fs.top+7,C_TEXT,2);
    DrawLine(dc,fs.right-9,fs.top+7,fs.right-15,fs.top+7,C_TEXT,2);
    DrawLine(dc,fs.left+9,fs.bottom-13,fs.left+9,fs.bottom-7,C_TEXT,2);
    DrawLine(dc,fs.left+9,fs.bottom-7,fs.left+15,fs.bottom-7,C_TEXT,2);
    DrawLine(dc,fs.right-9,fs.bottom-13,fs.right-9,fs.bottom-7,C_TEXT,2);
    DrawLine(dc,fs.right-9,fs.bottom-7,fs.right-15,fs.bottom-7,C_TEXT,2);

    if(g_drawerProgress>0) {
    int settingsShift=g_drawerSettingsOpen?44:0;
    int queueShift=g_drawerSettingsOpen?44:-120;
    RECT receiver=R_RECEIVER;
    int sel=(int)SendMessageW(g_renderers,CB_GETCURSEL,0,0);
    /* Slide-out media drawer: output, quick controls, search and queue. */
    /* Premium media-library surface with layered navy and an inset rim. */
    GradientRound(dc,R_SIDEBAR,RGB(13,24,41),RGB(20,29,49),24);
    ABM_D2D_StrokeRound(&g_ui,(float)R_SIDEBAR.left,(float)R_SIDEBAR.top,(float)R_SIDEBAR.right,(float)R_SIDEBAR.bottom,24.0f,RGB(62,100,151),1.0f);
    RECT innerGlow={R_SIDEBAR.left+1,R_SIDEBAR.top+1,R_SIDEBAR.right-1,R_SIDEBAR.bottom-1};
    ABM_D2D_StrokeRound(&g_ui,(float)innerGlow.left,(float)innerGlow.top,(float)innerGlow.right,(float)innerGlow.bottom,23.0f,RGB(36,56,82),0.9f);
    RECT drawerInset={R_SIDEBAR.left+1,R_SIDEBAR.top+1,R_SIDEBAR.right-1,R_SIDEBAR.bottom-1};
    ABM_D2D_StrokeRound(&g_ui,(float)drawerInset.left,(float)drawerInset.top,(float)drawerInset.right,(float)drawerInset.bottom,23.0f,RGB(39,57,79),0.8f);
    int accentX=R_SIDEBAR.left+18+(int)((g_visualPulse*2u)%(unsigned int)((R_SIDEBAR.right-R_SIDEBAR.left)-72));
    RECT accent={accentX,R_SIDEBAR.top+1,accentX+54,R_SIDEBAR.top+3};
    GradientRect(dc,accent,RGB(48,180,230),RGB(112,105,255));
    if(g_drawerSettingsOpen) {
    RECT sideTitle={R_SIDEBAR.left+17,R_SIDEBAR.top+54,R_SIDEBAR.right-16,R_SIDEBAR.top+73};
    Txt(dc,L"PLAYBACK OUTPUT",sideTitle,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT scan=R_SCAN;
    RoundBox(dc,scan,Blend(RGB(43,46,51),RGB(61,65,71),(g_hoverButton==HIT_SCAN)?g_hoverLevel:0),12);
    ABM_D2D_StrokeRound(&g_ui,(float)scan.left,(float)scan.top,(float)scan.right,(float)scan.bottom,12.0f,RGB(67,71,77),0.75f);
    RECT scanIcon={scan.left+14,scan.top+8,scan.left+34,scan.bottom-8};
    DrawLine(dc,scanIcon.left+2,scanIcon.top+8,scanIcon.left+2,scanIcon.bottom-7,RGB(220,236,255),2);
    DrawLine(dc,scanIcon.left+2,scanIcon.top+8,scanIcon.right-2,scanIcon.top+8,RGB(220,236,255),2);
    DrawLine(dc,scanIcon.right-2,scanIcon.top+8,scanIcon.right-2,scanIcon.bottom-7,RGB(220,236,255),2);
    RECT scanText={scan.left+42,scan.top+2,scan.right-10,scan.bottom-2};
    Txt(dc,L"Discover devices",scanText,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    /* Fully custom Direct2D output selector. */
    COLORREF receiverFill=g_receiverPopup?RGB(42,45,50):RGB(30,32,36);
    Panel(dc,receiver,g_receiverPopup?RGB(30,54,82):RGB(20,34,53),g_receiverPopup?RGB(91,151,211):RGB(52,82,119),12);
    RECT deviceIcon={receiver.left+12,receiver.top+7,receiver.left+34,receiver.bottom-7};
    RoundBox(dc,deviceIcon,RGB(43,47,53),6);
    DrawLine(dc,deviceIcon.left+4,deviceIcon.top+5,deviceIcon.right-4,deviceIcon.top+5,RGB(197,205,214),2);
    DrawLine(dc,deviceIcon.left+4,deviceIcon.bottom-5,deviceIcon.right-4,deviceIcon.bottom-5,RGB(197,205,214),2);
    wchar_t deviceName[256]=L"Local playback";
    if(sel>0)SendMessageW(g_renderers,CB_GETLBTEXT,sel,(LPARAM)deviceName);
    RECT deviceText={receiver.left+44,receiver.top+2,receiver.right-48,receiver.bottom-2};
    Txt(dc,deviceName,deviceText,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    int arrowX=receiver.right-20,arrowY=(receiver.top+receiver.bottom)/2;
    DrawLine(dc,arrowX-4,arrowY-2,arrowX,arrowY+2,RGB(190,196,204),1.8f);
    DrawLine(dc,arrowX,arrowY+2,arrowX+4,arrowY-2,RGB(190,196,204),1.8f);

    RECT toolsHead={R_SIDEBAR.left+17,R_SIDEBAR.top+144,R_SIDEBAR.right-17,R_SIDEBAR.top+164};
    Txt(dc,L"QUICK CONTROLS",toolsHead,RGB(133,151,174),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT actions[4]={R_SHUFFLE,R_REPEAT,R_SPEED,R_RECENT};
    const wchar_t *actionLabels[4]={g_shuffle?L"Shuffle on":L"Shuffle",g_repeat?L"Repeat on":L"Repeat",L"Speed",L"Recent"};
    const wchar_t *actionIcons[4]={L"S",L"R",L"1x",L"..."};
    BOOL actionActive[4]={g_shuffle,g_repeat,g_playbackRate!=1.0f,FALSE};
    for(int ai=0;ai<4;ai++) {
        RECT ar=actions[ai];
        POINT cursor; BOOL actionHot=FALSE;
        if(GetCursorPos(&cursor)){ScreenToClient(g_main,&cursor);actionHot=PtInRect(&ar,cursor);}
        COLORREF fill=actionActive[ai]?RGB(29,61,99):(actionHot?RGB(30,48,72):RGB(19,32,50));
        COLORREF edge=actionActive[ai]?RGB(79,148,224):(actionHot?RGB(67,111,164):RGB(42,64,94));
        if(actionActive[ai])GradientRound(dc,ar,RGB(24,68,112),RGB(42,55,112),10);
        else RoundBox(dc,ar,fill,10);
        ABM_D2D_StrokeRound(&g_ui,(float)ar.left,(float)ar.top,(float)ar.right,(float)ar.bottom,10.0f,edge,0.9f);
        RECT glyph={ar.left+8,ar.top+3,ar.left+31,ar.bottom-3};
        Txt(dc,actionIcons[ai],glyph,actionActive[ai]?RGB(126,184,255):RGB(157,177,202),g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT label={ar.left+34,ar.top+2,ar.right-5,ar.bottom-2};
        wchar_t speedLabel[32];
        const wchar_t *shown=actionLabels[ai];
        if(ai==2){swprintf_s(speedLabel,32,L"Speed  %.2gx",g_playbackRate);shown=speedLabel;}
        Txt(dc,shown,label,actionActive[ai]?RGB(232,241,255):RGB(210,220,233),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }

    }
    Panel(dc,R_SETTINGS_BUTTON,g_drawerSettingsOpen?RGB(35,58,86):RGB(25,34,47),g_drawerSettingsOpen?RGB(77,132,194):RGB(49,66,88),11);
    RECT settingsLabel={R_SETTINGS_BUTTON.left+14,R_SETTINGS_BUTTON.top+2,R_SETTINGS_BUTTON.right-38,R_SETTINGS_BUTTON.bottom-2};
    Txt(dc,g_drawerSettingsOpen?L"Hide playback settings":L"Playback & output",settingsLabel,RGB(231,240,252),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    int chevronX=R_SETTINGS_BUTTON.right-22,chevronY=(R_SETTINGS_BUTTON.top+R_SETTINGS_BUTTON.bottom)/2;
    DrawLine(dc,chevronX-4,chevronY-2,chevronX,chevronY+2,RGB(190,210,234),2);
    DrawLine(dc,chevronX,chevronY+2,chevronX+4,chevronY-2,RGB(190,210,234),2);
    RECT listHead={R_SIDEBAR.left+17,R_SIDEBAR.top+190+queueShift,R_SIDEBAR.right-17,R_SIDEBAR.top+212+queueShift};
    Txt(dc,g_libraryTab?L"YOUR LIBRARY":L"UP NEXT",listHead,RGB(169,190,218),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    wchar_t count[48];swprintf_s(count,48,L"%02d TRACKS",g_playlistCount);
    RECT countR={R_SIDEBAR.right-112,listHead.top,R_SIDEBAR.right-17,listHead.bottom};
    Txt(dc,count,countR,C_MUTED,g_smallFont,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    DrawLine(dc,R_SIDEBAR.left+16,listHead.bottom+4,R_SIDEBAR.right-16,listHead.bottom+4,RGB(43,53,69),1);

    /* Search and filters are drawn in Direct2D; keyboard input is handled by
       the main window so no blurry native EDIT control is layered over it. */
    Panel(dc,R_SEARCH,g_searchFocused?RGB(16,39,65):RGB(11,23,39),g_searchFocused?RGB(86,164,244):RGB(45,75,112),12);
    RECT searchGlyph={R_SEARCH.left+12,R_SEARCH.top+10,R_SEARCH.left+26,R_SEARCH.bottom-10};
    ABM_D2D_StrokeRound(&g_ui,(float)searchGlyph.left,(float)searchGlyph.top,(float)searchGlyph.right,(float)searchGlyph.bottom,7,RGB(139,163,193),1.5f);
    DrawLine(dc,searchGlyph.right-1,searchGlyph.bottom-1,searchGlyph.right+4,searchGlyph.bottom+4,RGB(139,163,193),1.5f);
    RECT searchText={R_SEARCH.left+36,R_SEARCH.top+2,R_SEARCH.right-10,R_SEARCH.bottom-2};
    Txt(dc,g_searchText[0]?g_searchText:(g_searchFocused?L"Type to search":L"Search tracks, artists, albums"),searchText,
        g_searchText[0]?C_TEXT:RGB(135,151,172),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

    RECT tabs[2]={R_TAB_QUEUE,R_TAB_LIBRARY};
    const wchar_t *tabNames[2]={L"Queue",L"Library"};
    for(int ti=0;ti<2;ti++){
        BOOL active=(g_libraryTab==ti);
        if(active)GradientRound(dc,tabs[ti],RGB(31,112,172),RGB(80,82,184),11);
        else RoundBox(dc,tabs[ti],RGB(17,29,47),11);
        ABM_D2D_StrokeRound(&g_ui,(float)tabs[ti].left,(float)tabs[ti].top,(float)tabs[ti].right,(float)tabs[ti].bottom,11.0f,active?RGB(104,174,241):RGB(39,62,92),0.9f);
        RECT label={tabs[ti].left+3,tabs[ti].top+1,tabs[ti].right-3,tabs[ti].bottom-1};
        Txt(dc,tabNames[ti],label,active?RGB(238,246,255):RGB(171,187,207),g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    RECT filters[3]={R_FILTER_ALL,R_FILTER_AUDIO,R_FILTER_VIDEO};
    const wchar_t *filterNames[3]={L"All",L"Audio",L"Video"};
    for(int fi=0;fi<3;fi++){
        BOOL active=g_mediaFilter==fi;
        if(active)GradientRound(dc,filters[fi],RGB(35,112,173),RGB(77,82,177),8);
        else RoundBox(dc,filters[fi],RGB(18,30,47),8);
        RECT label={filters[fi].left+2,filters[fi].top,filters[fi].right-2,filters[fi].bottom};
        Txt(dc,filterNames[fi],label,active?RGB(244,248,255):RGB(158,174,194),g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    RoundBox(dc,R_GROUP,RGB(25,32,42),8);
    wchar_t groupLabel[32];swprintf_s(groupLabel,32,L"Group: %s",g_groupMode==1?L"Artist":(g_groupMode==2?L"Album":L"Folder"));
    Txt(dc,groupLabel,R_GROUP,RGB(174,195,220),g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    int rowHeight=68;
    int listHeight=R_LIST.bottom-R_LIST.top;
    /* Render complete rows only; partial final rows previously bled into
       the volume area and made adjacent titles appear superimposed. */
    int visible=listHeight>0?listHeight/rowHeight:0;
    if(visible>MAX_PLAYLIST)visible=MAX_PLAYLIST;
    int matched=0,shown=0;g_visibleRowCount=0;
    for(int i=0;i<g_playlistCount;i++){
        const wchar_t *leaf=wcsrchr(g_playlist[i],L'\\');leaf=leaf?leaf+1:g_playlist[i];
        BOOL isVideo=IsVideoPath(g_playlist[i]);
        if((g_mediaFilter==1&&isVideo)||(g_mediaFilter==2&&!isVideo))continue;
        if(g_searchText[0]&&!ContainsTextI(leaf,g_searchText)&&
           !(i==g_current&&((g_trackArtist[0]&&ContainsTextI(g_trackArtist,g_searchText))||
                            (g_trackAlbum[0]&&ContainsTextI(g_trackAlbum,g_searchText))||
                            (g_trackTitle[0]&&ContainsTextI(g_trackTitle,g_searchText)))))continue;
        if(matched++<g_playlistScroll)continue;
        if(shown>=visible)break;
        RECT row={R_LIST.left,R_LIST.top+shown*rowHeight,R_LIST.right,R_LIST.top+(shown+1)*rowHeight};
        if(row.bottom>R_LIST.bottom)row.bottom=R_LIST.bottom;
        R_ROW[shown]=row;g_visibleRows[shown]=i;g_visibleRowCount=shown+1;
        BOOL current=(i==g_current),selected=(i==g_selected),hot=(i==g_hoverPlaylistIndex);
        if(current||selected||hot){
            if(current)GradientRound(dc,row,RGB(19,49,81),RGB(32,45,86),12);
            else RoundBox(dc,row,selected?RGB(22,42,67):RGB(20,36,57),12);
            ABM_D2D_StrokeRound(&g_ui,(float)row.left,(float)row.top,(float)row.right,(float)row.bottom,12.0f,current?RGB(69,151,220):(hot?RGB(62,111,165):RGB(45,75,111)),current?1.25f:0.85f);
            if(current){RECT activeRail={row.left+1,row.top+12,row.left+4,row.bottom-12};GradientRect(dc,activeRail,RGB(47,190,231),RGB(126,103,245));}
        }
        RECT thumb={row.left+8,row.top+9,row.left+56,row.bottom-9};
        GradientRect(dc,thumb,current?RGB(34,120,184):RGB(28+(i*7)%14,45+(i*5)%14,69+(i*3)%18),current?RGB(91,93,190):RGB(38+(i*5)%16,57+(i*4)%16,84+(i*3)%18));
        RoundBox(dc,thumb,current?RGB(49,103,166):RGB(32+(i*5)%14,49+(i*4)%15,72+(i*3)%18),9);
        if(current&&g_artworkLoaded)ABM_D2D_DrawImage(&g_ui,(float)thumb.left+2,(float)thumb.top+2,(float)thumb.right-2,(float)thumb.bottom-2);
        else {
            int cx=(thumb.left+thumb.right)/2,cy=(thumb.top+thumb.bottom)/2;
            COLORREF iconColor=current?RGB(255,255,255):RGB(171,198,229);
            if(isVideo){DrawLine(dc,cx-5,cy-7,cx+7,cy,iconColor,2);DrawLine(dc,cx+7,cy,cx-5,cy+7,iconColor,2);DrawLine(dc,cx-5,cy+7,cx-5,cy-7,iconColor,2);}
            else {DrawLine(dc,cx-7,cy-6,cx-7,cy+6,RGB(182,205,232),2);DrawLine(dc,cx+7,cy-6,cx+7,cy+6,RGB(182,205,232),2);}
        }
        RECT name={row.left+67,row.top+9,row.right-14,row.top+34};
        RECT subline={row.left+67,row.top+35,row.right-14,row.bottom-7};
        Txt(dc,leaf,name,current?RGB(255,255,255):RGB(225,232,242),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        const wchar_t *group=L"Local file";
        if(g_groupMode==1)group=current&&g_trackArtist[0]?g_trackArtist:L"Artist details appear when track is played";
        else if(g_groupMode==2)group=current&&g_trackAlbum[0]?g_trackAlbum:L"Album details appear when track is played";
        Txt(dc,group,subline,RGB(139,157,180),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(current){int cx=row.right-22,base=row.top+rowHeight/2;for(int k=0;k<3;k++){int bh=5+(int)(5.0*(0.5+0.5*sin((double)g_visualPulse*0.18+k*1.5)));RECT wave={cx+k*4,base-bh,cx+k*4+2,base+bh};RoundBox(dc,wave,RGB(94,183,244),2);}}
        shown++;
    }
    if(shown==0){
        RECT empty={R_LIST.left+8,R_LIST.top+18,R_LIST.right-8,R_LIST.top+76};
        Txt(dc,g_searchText[0]?L"No matching media":L"Add media to start your library",empty,RGB(151,168,191),g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    if(g_receiverPopup) {
        int n=(int)SendMessageW(g_renderers,CB_GETCOUNT,0,0);if(n>MAX_RENDERERS+1)n=MAX_RENDERERS+1;
        int rows=n>6?6:n;
        R_RECEIVER_LIST=(RECT){receiver.left,receiver.bottom+7,receiver.right,receiver.bottom+7+rows*36};
        Panel(dc,R_RECEIVER_LIST,RGB(27,29,32),RGB(62,66,72),13);
        for(int i=0;i<rows;i++) {
            RECT item={R_RECEIVER_LIST.left+6,R_RECEIVER_LIST.top+4+i*36,R_RECEIVER_LIST.right-6,R_RECEIVER_LIST.top+4+(i+1)*36};
            if(i==sel)RoundBox(dc,item,RGB(53,57,64),8);
            wchar_t label[256]=L"";
            SendMessageW(g_renderers,CB_GETLBTEXT,i,(LPARAM)label);
            RECT tx={item.left+13,item.top,item.right-12,item.bottom};
            Txt(dc,label,tx,i==sel?RGB(248,249,250):RGB(202,207,214),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
    }

    }

    /* Timeline: keep the seek bar strictly inside the video canvas; never underlap the drawer. */
    RECT timeline={R_SEEK.left,R_SEEK.top-15,R_SEEK.right,R_SEEK.bottom+16};Fill(dc,timeline,C_BG);
    int pos=0;int64_t len=g_player?libvlc_media_player_get_length(g_player):0;
    int64_t now=g_player?libvlc_media_player_get_time(g_player):0;
    if(len>0){pos=(int)((now*1000)/len);if(pos<0)pos=0;if(pos>1000)pos=1000;}
    RECT track={R_SEEK.left,R_SEEK.top-2,R_SEEK.right,R_SEEK.bottom+2};RoundBox(dc,track,RGB(35,49,70),6);
    RECT prog=track;prog.right=prog.left+(prog.right-prog.left)*pos/1000;if(prog.right>prog.left)GradientRect(dc,prog,RGB(42,181,231),RGB(125,101,245));
    int thumb=R_SEEK.left+(R_SEEK.right-R_SEEK.left)*pos/1000;
    RECT halo={thumb-10,R_SEEK.top-10,thumb+10,R_SEEK.bottom+10};RoundBox(dc,halo,RGB(36,66,104),12);
    RECT dot={thumb-5,R_SEEK.top-5,thumb+5,R_SEEK.bottom+5};RoundBox(dc,dot,RGB(235,244,255),10);
    wchar_t t1[24],t2[24],times[64];FormatTime((int)now,t1,24);FormatTime((int)len,t2,24);swprintf_s(times,64,L"%s  /  %s",t1,t2);
    RECT timeR={24,client.bottom-44,180,client.bottom-20};Txt(dc,times,timeR,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    /* Control dock */
    RECT dock={client.right/2-174,client.bottom-105,client.right/2+174,client.bottom-29};
    RECT dockShadow=dock; OffsetRect(&dockShadow,0,7);
    RoundBox(dc,dockShadow,RGB(5,8,14),28);
    Panel(dc,dock,RGB(19,28,43),RGB(61,91,132),25);
    RECT dockRim=dock; InflateRect(&dockRim,-2,-2);
    ABM_D2D_StrokeRound(&g_ui,(float)dockRim.left,(float)dockRim.top,(float)dockRim.right,(float)dockRim.bottom,23.0f,RGB(38,57,82),0.8f);
    DrawIconButton(dc,R_PREV,5,g_hoverButton==HIT_PREV,FALSE);
    DrawIconButton(dc,R_PLAY,libvlc_media_player_get_state(g_player)==libvlc_Playing?2:1,g_hoverButton==HIT_PLAY,TRUE);
    DrawIconButton(dc,R_NEXT,4,g_hoverButton==HIT_NEXT,FALSE);
    DrawIconButton(dc,R_STOP,3,g_hoverButton==HIT_STOP,FALSE);
    if(g_drawerOpen) {
    RECT volPanel={R_SIDEBAR.left+10,R_SIDEBAR.bottom-78,R_SIDEBAR.right-10,R_SIDEBAR.bottom-12};
    Panel(dc,volPanel,RGB(23,31,43),RGB(57,83,116),16);
    RECT volLabel={volPanel.left+14,volPanel.top+3,volPanel.left+78,volPanel.bottom-3};
    Txt(dc,L"VOLUME",volLabel,RGB(161,183,211),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
    RECT vr={R_VOLUME.left,R_VOLUME.top-3,R_VOLUME.right,R_VOLUME.bottom+3};RoundBox(dc,vr,RGB(39,54,76),6);
    vr.right=vr.left+(vr.right-vr.left)*g_volume/100;GradientRect(dc,vr,RGB(44,174,226),RGB(119,105,245));
    int vx=R_VOLUME.left+(R_VOLUME.right-R_VOLUME.left)*g_volume/100;
    RECT vd={vx-6,R_VOLUME.top-6,vx+6,R_VOLUME.bottom+6};RoundBox(dc,vd,RGB(245,248,252),8);
    ABM_D2D_StrokeRound(&g_ui,(float)vd.left,(float)vd.top,(float)vd.right,(float)vd.bottom,8.0f,RGB(110,165,242),0.8f);

    RECT open=R_OPEN;
    GradientRound(dc,open,(g_hoverButton==HIT_OPEN)?RGB(36,137,203):RGB(23,43,67),(g_hoverButton==HIT_OPEN)?RGB(104,91,218):RGB(37,65,99),14);
    ABM_D2D_StrokeRound(&g_ui,(float)open.left,(float)open.top,(float)open.right,(float)open.bottom,14.0f,(g_hoverButton==HIT_OPEN)?RGB(112,174,241):RGB(62,100,148),1.0f);
    RECT plus={open.left+14,open.top+4,open.left+37,open.bottom-4};
    Txt(dc,L"+",plus,RGB(230,242,255),g_headingFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RECT openText={open.left+39,open.top+2,open.right-8,open.bottom-2};
    Txt(dc,L"Add media",openText,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    RECT footer={client.right-430,client.bottom-25,client.right-24,client.bottom-8};
    Txt(dc,L"ABHISEK BASU MULLICK  |  © 2026 ALL RIGHTS RESERVED",footer,RGB(128,143,162),g_smallFont,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

    /* Animated drop-down menu is painted last so it floats above the player. */
    if(g_menuProgress>0) {
        RECT menu=R_MENU_PANEL;
        menu.bottom=menu.top+((menu.bottom-menu.top)*g_menuProgress)/100;
        /* Floating command surface with a soft shadow, refined rim and section label. */
        RECT shadow=menu;OffsetRect(&shadow,0,7);RoundBox(dc,shadow,RGB(7,11,18),19);
        RECT menuGlow=menu; InflateRect(&menuGlow,2,2); RoundBox(dc,menuGlow,RGB(7,12,22),21);
        Panel(dc,menu,RGB(17,27,44),RGB(75,125,184),18);
        RECT menuTop={menu.left+18,menu.top+2,menu.right-18,menu.top+5};
        GradientRect(dc,menuTop,RGB(43,179,230),RGB(127,102,244));
        RECT menuHeader={menu.left+14,menu.top+10,menu.right-14,menu.top+43};
        Txt(dc,L"PLAYER CONTROLS",menuHeader,RGB(112,174,239),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawLine(dc,menu.left+14,menu.top+45,menu.right-14,menu.top+45,RGB(48,68,94),1);
        const wchar_t *labels[13]={
            L"Open media",L"Find network devices",L"Cast to TV",L"Play / Pause",
            L"Fullscreen",L"Load playlist",L"Save playlist",L"Help",
            L"About ABM AV Player",L"Exit player",L"Shuffle: Off",
            L"Repeat current: Off",L"Recent media"
        };
        const wchar_t *shortcuts[13]={
            L"Ctrl+O",L"Ctrl+D",L"Ctrl+Shift+C",L"Space",
            L"F11",L"Ctrl+L",L"Ctrl+S",L"F1",
            L"Ctrl+I",L"Alt+F4",L"Ctrl+Shift+H",
            L"Ctrl+R",L"Ctrl+J"
        };
        const wchar_t *icons[13]={L"+",L"N",L"TV",L"▶",L"⛶",L"↓",L"↑",L"?",L"i",L"×",L"⇄",L"↻",L"◷"};
        for(int i=0;i<13;i++) {
            RECT item=R_MENU_ITEMS[i];
            if(i==10)labels[10]=g_shuffle?L"Shuffle: On":L"Shuffle: Off";
            if(i==11)labels[11]=g_repeat?L"Repeat current: On":L"Repeat current: Off";
            if(item.bottom>menu.bottom)item.bottom=menu.bottom;
            if(item.top>=menu.bottom)continue;
            BOOL hot=(g_hoverMenuItem==i);
            RECT row=item;InflateRect(&row,-1,-1);
            if(hot){GradientRound(dc,row,RGB(31,66,105),RGB(48,56,112),11);ABM_D2D_StrokeRound(&g_ui,(float)row.left,(float)row.top,(float)row.right,(float)row.bottom,11.0f,RGB(91,153,220),1.0f);}
            else if(i==3||i==9)RoundBox(dc,row,RGB(22,38,59),10);
            RECT glyph={item.left+9,item.top+5,item.left+39,item.bottom-5};
            RoundBox(dc,glyph,hot?RGB(58,104,159):RGB(32,49,72),8);
            Txt(dc,icons[i],glyph,hot?RGB(244,249,255):RGB(154,190,232),g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            RECT label={item.left+49,item.top+2,item.right-104,item.bottom-2};
            Txt(dc,labels[i],label,hot?RGB(255,255,255):RGB(230,238,249),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            RECT shortcut={item.right-100,item.top+2,item.right-10,item.bottom-2};
            Txt(dc,shortcuts[i],shortcut,hot?RGB(190,216,247):RGB(137,160,190),g_smallFont,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        }
    }
}
/* The native renderer combo is a child HWND, so it always paints above
   the parent's Direct2D menu. Hide it while the custom menu overlaps it. */
static void SyncRendererComboVisibility(void) {
    /* Hidden combo retained only as a data store for renderer selection. */
    if (g_renderers && IsWindow(g_renderers)) ShowWindow(g_renderers,SW_HIDE);
}

static void RefreshRendererList(void) {
    libvlc_renderer_item_t *previous=g_selectedRenderer;
    int restored=0;
    for(size_t i=0;i<g_comboCount;i++) libvlc_renderer_item_release(g_comboItems[i]);
    g_comboCount=0; SendMessageW(g_renderers,CB_RESETCONTENT,0,0);
    SendMessageW(g_renderers,CB_ADDSTRING,0,(LPARAM)L"Local playback");
    EnterCriticalSection(&g_rendererLock);
    for(size_t i=0;i<g_rendererCount && g_comboCount<MAX_RENDERERS;i++) {
        libvlc_renderer_item_t *it=libvlc_renderer_item_hold(g_rendererItems[i]);
        const char *name=libvlc_renderer_item_name(it); wchar_t wide[256]=L"Network receiver";
        if(name) MultiByteToWideChar(CP_UTF8,0,name,-1,wide,256);
        int ix=(int)SendMessageW(g_renderers,CB_ADDSTRING,0,(LPARAM)wide);
        if(ix!=CB_ERR && ix!=CB_ERRSPACE) {
            g_comboItems[g_comboCount]=it;
            if(previous==it) restored=(int)g_comboCount+1;
            g_comboCount++;
        } else libvlc_renderer_item_release(it);
    }
    LeaveCriticalSection(&g_rendererLock);
    SendMessageW(g_renderers,CB_SETCURSEL,(WPARAM)restored,0);
}
static void RendererEvent(const libvlc_event_t *ev,void *opaque) {
    (void)opaque; libvlc_renderer_item_t *it=ev->type==libvlc_RendererDiscovererItemAdded?ev->u.renderer_discoverer_item_added.item:ev->u.renderer_discoverer_item_deleted.item;
    if(!it)return; EnterCriticalSection(&g_rendererLock);
    if(ev->type==libvlc_RendererDiscovererItemAdded) {
        BOOL found=FALSE; for(size_t i=0;i<g_rendererCount;i++)if(g_rendererItems[i]==it)found=TRUE;
        if(!found&&g_rendererCount<MAX_RENDERERS)g_rendererItems[g_rendererCount++]=libvlc_renderer_item_hold(it);
    } else for(size_t i=0;i<g_rendererCount;i++)if(g_rendererItems[i]==it){libvlc_renderer_item_release(g_rendererItems[i]);for(size_t j=i+1;j<g_rendererCount;j++)g_rendererItems[j-1]=g_rendererItems[j];--g_rendererCount;break;}
    LeaveCriticalSection(&g_rendererLock); if(g_main)PostMessageW(g_main,WM_RENDERERS_CHANGED,0,0);
}
static void StartDiscovery(void) {
    if(g_discovererCount){
        g_discoveryStartedTick=GetTickCount();g_discoveryTimedOut=FALSE;
        SetStatus(L"Searching for network receivers...");
        return;
    }
    libvlc_rd_description_t **services=NULL;
    size_t n=libvlc_renderer_discoverer_list_get(g_vlc,&services);
    if(!n||!services){
        SetStatus(L"No VLC receiver discovery plugins are available");
        MessageBoxW(g_main,L"LibVLC did not report any renderer-discovery services. The bundled VLC plugins may be missing or could not be loaded. Verify the plugins\\services_discovery directory in the application folder.",APP_NAME,MB_ICONWARNING);
        return;
    }
    wchar_t available[256]=L"";
    for(size_t i=0;i<n;i++){
        wchar_t name[64]=L"";
        if(services[i]->psz_name)MultiByteToWideChar(CP_UTF8,0,services[i]->psz_name,-1,name,_countof(name));
        if(name[0] && wcslen(available)<170){if(available[0])wcscat_s(available,_countof(available),L", ");wcscat_s(available,_countof(available),name);}
        libvlc_renderer_discoverer_t *d=services[i]->psz_name?libvlc_renderer_discoverer_new(g_vlc,services[i]->psz_name):NULL;
        if(!d)continue;
        libvlc_event_manager_t *em=libvlc_renderer_discoverer_event_manager(d);
        libvlc_event_attach(em,libvlc_RendererDiscovererItemAdded,RendererEvent,NULL);
        libvlc_event_attach(em,libvlc_RendererDiscovererItemDeleted,RendererEvent,NULL);
        if(libvlc_renderer_discoverer_start(d)==0)g_discoverers[g_discovererCount++]=d;
        else {libvlc_event_detach(em,libvlc_RendererDiscovererItemAdded,RendererEvent,NULL);libvlc_event_detach(em,libvlc_RendererDiscovererItemDeleted,RendererEvent,NULL);libvlc_renderer_discoverer_release(d);}
        if(g_discovererCount>=MAX_DISCOVERERS)break;
    }
    libvlc_renderer_discoverer_list_release(services,n);
    if(!g_discovererCount){
        wchar_t message[512];swprintf_s(message,_countof(message),L"VLC found these discovery services but could not start any of them:\n%s\n\nCheck that the VLC renderer discovery plugins and their dependent DLLs are present.",available[0]?available:L"(no service names returned)");
        SetStatus(L"Receiver discovery could not start");
        MessageBoxW(g_main,message,APP_NAME,MB_ICONWARNING);
        return;
    }
    g_discoveryStartedTick=GetTickCount();g_discoveryTimedOut=FALSE;
    wchar_t status[256];swprintf_s(status,_countof(status),L"Searching for receivers (%s)",available[0]?available:L"VLC discovery");
    SetStatus(status);
}
static void SelectRenderer(void) {
    int s=(int)SendMessageW(g_renderers,CB_GETCURSEL,0,0);
    libvlc_renderer_item_t *next=NULL;
    if(s>0&&(size_t)(s-1)<g_comboCount)next=libvlc_renderer_item_hold(g_comboItems[s-1]);
    if(g_selectedRenderer)libvlc_renderer_item_release(g_selectedRenderer);
    g_selectedRenderer=next;

    /* set_renderer only takes effect before play(). Recreate the media player
       when changing the output so an already-playing item can move to the TV. */
    if(g_hasMedia&&g_current>=0&&g_current<g_playlistCount){
        int64_t resumeAt=g_useForcedResume?g_forcedResumeMs:libvlc_media_player_get_time(g_player);
        libvlc_state_t oldState=libvlc_media_player_get_state(g_player);
        BOOL wasPlaying=g_useForcedResume || (oldState!=libvlc_Paused && oldState!=libvlc_Stopped && oldState!=libvlc_Ended && oldState!=libvlc_Error);
        if(resumeAt<0)resumeAt=0;
        libvlc_media_player_stop(g_player);
        libvlc_media_player_release(g_player);
        g_player=libvlc_media_player_new(g_vlc);
        if(!g_player){g_hasMedia=FALSE;g_pendingResume=FALSE;MessageBoxW(g_main,L"Could not recreate the media player.",APP_NAME,MB_ICONERROR);return;}
        libvlc_media_player_set_hwnd(g_player,g_video);
        g_rendererApplied=FALSE;
        if(g_selectedRenderer && libvlc_media_player_set_renderer(g_player,g_selectedRenderer)!=0){
            libvlc_renderer_item_release(g_selectedRenderer);
            g_selectedRenderer=NULL;
            SendMessageW(g_renderers,CB_SETCURSEL,0,0);
            SetStatus(L"Receiver setup failed; continuing with local playback");
        } else if(g_selectedRenderer) g_rendererApplied=TRUE;
        char utf8[PATH_CAP*4];
        if(WideCharToMultiByte(CP_UTF8,0,g_playlist[g_current],-1,utf8,sizeof(utf8),NULL,NULL)>0){
            libvlc_media_t *m=libvlc_media_new_path(g_vlc,utf8);
            if(m){
                if(g_replayGain)libvlc_media_add_option(m,":audio-replay-gain-mode=track");
                if(g_compressor){libvlc_media_add_option(m,":audio-filter=compressor");libvlc_media_add_option(m,":compressor-threshold=-18");libvlc_media_add_option(m,":compressor-ratio=4");libvlc_media_add_option(m,":compressor-attack=20");libvlc_media_add_option(m,":compressor-release=250");libvlc_media_add_option(m,":compressor-makeup-gain=0");}
                libvlc_media_player_set_media(g_player,m);ApplyEqProfile();
                if(libvlc_media_player_play(g_player)==0){
                    g_pendingResume=TRUE;g_pendingResumeMs=resumeAt;
                    g_pendingResumePause=!wasPlaying;g_pendingResumeTicks=0;
                }else{
                    g_pendingResume=FALSE;
                    if(g_selectedRenderer){
                        libvlc_media_player_stop(g_player);
                        libvlc_media_player_release(g_player);
                        libvlc_renderer_item_release(g_selectedRenderer);
                        g_selectedRenderer=NULL;g_rendererApplied=FALSE;
                        SendMessageW(g_renderers,CB_SETCURSEL,0,0);
                        g_player=libvlc_media_player_new(g_vlc);
                        if(g_player){
                            libvlc_media_player_set_hwnd(g_player,g_video);
                            libvlc_media_t *local=libvlc_media_new_path(g_vlc,utf8);
                            if(local){
                                if(g_replayGain)libvlc_media_add_option(local,":audio-replay-gain-mode=track");
                                if(g_compressor){libvlc_media_add_option(local,":audio-filter=compressor");libvlc_media_add_option(local,":compressor-threshold=-18");libvlc_media_add_option(local,":compressor-ratio=4");libvlc_media_add_option(local,":compressor-attack=20");libvlc_media_add_option(local,":compressor-release=250");libvlc_media_add_option(local,":compressor-makeup-gain=0");}
                                libvlc_media_player_set_media(g_player,local);libvlc_media_release(local);ApplyEqProfile();
                                if(libvlc_media_player_play(g_player)==0){
                                    g_pendingResume=TRUE;g_pendingResumeMs=resumeAt;
                                    g_pendingResumePause=!wasPlaying;g_pendingResumeTicks=0;
                                    SetStatus(L"Cast failed; resumed on this PC");
                                }else{g_hasMedia=FALSE;MessageBoxW(g_main,L"Playback failed on the receiver and local output.",APP_NAME,MB_ICONERROR);}
                            }else g_hasMedia=FALSE;
                        }else g_hasMedia=FALSE;
                    }else{g_hasMedia=FALSE;MessageBoxW(g_main,L"Playback could not restart on the selected output.",APP_NAME,MB_ICONERROR);}
                }
                libvlc_media_release(m);
            }
        }
    }
    InvalidateRect(g_main,NULL,FALSE);
}
static void StopDiscovery(void) {
    for(size_t i=0;i<g_discovererCount;i++){libvlc_event_manager_t *em=libvlc_renderer_discoverer_event_manager(g_discoverers[i]);libvlc_event_detach(em,libvlc_RendererDiscovererItemAdded,RendererEvent,NULL);libvlc_event_detach(em,libvlc_RendererDiscovererItemDeleted,RendererEvent,NULL);libvlc_renderer_discoverer_stop(g_discoverers[i]);libvlc_renderer_discoverer_release(g_discoverers[i]);}
    g_discovererCount=0; for(size_t i=0;i<g_comboCount;i++)libvlc_renderer_item_release(g_comboItems[i]);g_comboCount=0;
    if(g_selectedRenderer)libvlc_renderer_item_release(g_selectedRenderer);g_selectedRenderer=NULL;
    EnterCriticalSection(&g_rendererLock);for(size_t i=0;i<g_rendererCount;i++)libvlc_renderer_item_release(g_rendererItems[i]);g_rendererCount=0;LeaveCriticalSection(&g_rendererLock);
}
static BOOL EnsureDataDirectory(void) {
    if(g_dataDir[0])return TRUE;
    wchar_t base[PATH_CAP];
    if(FAILED(SHGetFolderPathW(NULL,CSIDL_APPDATA|CSIDL_FLAG_CREATE,NULL,SHGFP_TYPE_CURRENT,base)))return FALSE;
    if(swprintf_s(g_dataDir,PATH_CAP,L"%s\\ABMAVPlayer",base)<0)return FALSE;
    if(!CreateDirectoryW(g_dataDir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return FALSE;
    return TRUE;
}
static void SavePersistentState(void) {
    if(!EnsureDataDirectory())return;
    wchar_t path[PATH_CAP];swprintf_s(path,PATH_CAP,L"%s\\library.m3u",g_dataDir);
    FILE *fp=NULL;if(_wfopen_s(&fp,path,L"wb")==0&&fp){
        fputs("#EXTM3U\n",fp);
        for(int i=0;i<g_playlistCount;i++){int n=WideCharToMultiByte(CP_UTF8,0,g_playlist[i],-1,NULL,0,NULL,NULL);if(n>1){char *s=(char*)malloc((size_t)n);if(s){WideCharToMultiByte(CP_UTF8,0,g_playlist[i],-1,s,n,NULL,NULL);fputs(s,fp);fputc('\n',fp);free(s);}}}
        fclose(fp);
    }
    swprintf_s(path,PATH_CAP,L"%s\\recent.m3u",g_dataDir);
    fp=NULL;if(_wfopen_s(&fp,path,L"wb")==0&&fp){
        fputs("#EXTM3U\n",fp);
        for(int i=0;i<g_recentCount;i++){int n=WideCharToMultiByte(CP_UTF8,0,g_recent[i],-1,NULL,0,NULL,NULL);if(n>1){char *s=(char*)malloc((size_t)n);if(s){WideCharToMultiByte(CP_UTF8,0,g_recent[i],-1,s,n,NULL,NULL);fputs(s,fp);fputc('\n',fp);free(s);}}}
        fclose(fp);
    }
    swprintf_s(path,PATH_CAP,L"%s\\settings.ini",g_dataDir);
    wchar_t settings[256];swprintf_s(settings,256,L"Volume=%d\r\nShuffle=%d\r\nRepeat=%d\r\nRate=%.2f\r\n",g_volume,g_shuffle,g_repeat,g_playbackRate);
    wchar_t value[32];
    swprintf_s(value,32,L"%d",g_volume);WritePrivateProfileStringW(L"Player",L"Volume",value,path);
    swprintf_s(value,32,L"%d",g_shuffle?1:0);WritePrivateProfileStringW(L"Player",L"Shuffle",value,path);
    swprintf_s(value,32,L"%d",g_repeat?1:0);WritePrivateProfileStringW(L"Player",L"Repeat",value,path);
    swprintf_s(value,32,L"%d",(int)(g_playbackRate*100.0f));WritePrivateProfileStringW(L"Player",L"RatePercent",value,path);
}
static void LoadPersistentState(void) {
    if(!EnsureDataDirectory())return;
    wchar_t path[PATH_CAP];swprintf_s(path,PATH_CAP,L"%s\\settings.ini",g_dataDir);
    g_volume=GetPrivateProfileIntW(L"Player",L"Volume",70,path);
    g_shuffle=GetPrivateProfileIntW(L"Player",L"Shuffle",0,path)!=0;
    g_repeat=GetPrivateProfileIntW(L"Player",L"Repeat",0,path)!=0;
    g_playbackRate=(float)GetPrivateProfileIntW(L"Player",L"RatePercent",100,path)/100.0f;
    if(g_volume<0||g_volume>100)g_volume=70;
    if(g_playbackRate<0.5f||g_playbackRate>2.0f)g_playbackRate=1.0f;
    const wchar_t *files[2]={L"library.m3u",L"recent.m3u"};
    for(int which=0;which<2;which++){
        swprintf_s(path,PATH_CAP,L"%s\\%s",g_dataDir,files[which]);
        FILE *fp=NULL;if(_wfopen_s(&fp,path,L"rt, ccs=UTF-8")!=0||!fp)continue;
        wchar_t line[PATH_CAP];int count=0,limit=which?MAX_RECENT:MAX_PLAYLIST;
        while(count<limit&&fgetws(line,PATH_CAP,fp)){
            line[wcscspn(line,L"\r\n")]=0;if(!line[0]||line[0]==L'#')continue;
            if(which)wcsncpy_s(g_recent[count],PATH_CAP,line,_TRUNCATE);
            else wcsncpy_s(g_playlist[count],PATH_CAP,line,_TRUNCATE);
            count++;
        }
        fclose(fp);
        if(which)g_recentCount=count;else g_playlistCount=count;
    }
    if(g_playlistCount)g_selected=0;
    g_libraryReady=TRUE;
}
static void ShowRecentMenu(HWND hwnd,int x,int y) {
    if(!g_recentCount){MessageBoxW(hwnd,L"No recently played media yet.",APP_NAME,MB_OK|MB_ICONINFORMATION);return;}
    HMENU menu=CreatePopupMenu();if(!menu)return;
    for(int i=0;i<g_recentCount;i++){
        wchar_t label[PATH_CAP+16];wchar_t *name=wcsrchr(g_recent[i],L'\\');name=name?name+1:g_recent[i];
        swprintf_s(label,PATH_CAP+16,L"%d. %s",i+1,name);
        AppendMenuW(menu,MF_STRING,24000+i,label);
    }
    POINT pt={x,y};ClientToScreen(hwnd,&pt);
    int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd,NULL);
    DestroyMenu(menu);
    if(cmd>=24000&&cmd<24000+g_recentCount){
        const wchar_t *path=g_recent[cmd-24000];int found=-1;
        for(int i=0;i<g_playlistCount;i++)if(!_wcsicmp(g_playlist[i],path)){found=i;break;}
        if(found<0&&g_playlistCount<MAX_PLAYLIST){found=g_playlistCount++;wcsncpy_s(g_playlist[found],PATH_CAP,path,_TRUNCATE);}
        if(found>=0){SavePersistentState();PlayIndex(found);}
    }
}
static void AddRecent(const wchar_t *path) {
    for(int i=0;i<g_recentCount;i++)if(!_wcsicmp(g_recent[i],path)){for(int j=i;j>0;j--)wcsncpy_s(g_recent[j],PATH_CAP,g_recent[j-1],_TRUNCATE);wcsncpy_s(g_recent[0],PATH_CAP,path,_TRUNCATE);SavePersistentState();return;}
    int end=g_recentCount<MAX_RECENT?g_recentCount++:MAX_RECENT-1;
    for(int i=end;i>0;i--)wcsncpy_s(g_recent[i],PATH_CAP,g_recent[i-1],_TRUNCATE);
    wcsncpy_s(g_recent[0],PATH_CAP,path,_TRUNCATE);SavePersistentState();
}

static void SaveAudioPreferences(void) {
    if(!EnsureDataDirectory())return;
    wchar_t path[PATH_CAP],value[24];
    swprintf_s(path,PATH_CAP,L"%s\\audio.ini",g_dataDir);
    swprintf_s(value,24,L"%d",g_replayGain?1:0);
    WritePrivateProfileStringW(L"Audio",L"ReplayGain",value,path);
    swprintf_s(value,24,L"%d",g_compressor?1:0);
    WritePrivateProfileStringW(L"Audio",L"Compressor",value,path);
    swprintf_s(value,24,L"%d",g_eqProfile);
    WritePrivateProfileStringW(L"Audio",L"EqProfile",value,path);
}
static void LoadAudioPreferences(void) {
    if(!EnsureDataDirectory())return;
    wchar_t path[PATH_CAP];
    swprintf_s(path,PATH_CAP,L"%s\\audio.ini",g_dataDir);
    g_replayGain=GetPrivateProfileIntW(L"Audio",L"ReplayGain",1,path)!=0;
    g_compressor=GetPrivateProfileIntW(L"Audio",L"Compressor",0,path)!=0;
    g_eqProfile=GetPrivateProfileIntW(L"Audio",L"EqProfile",0,path);
    if(!((g_eqProfile>=0&&g_eqProfile<=3)||(g_eqProfile>=10&&g_eqProfile<=19)))g_eqProfile=0;
}
static void ApplyEqProfile(void) {
    if(!g_player)return;
    if(g_eqProfile>=10&&g_eqProfile<=19){
        libvlc_equalizer_t *preset=libvlc_audio_equalizer_new_from_preset(g_eqProfile-10);
        if(preset){libvlc_audio_equalizer_set_preamp(preset,0.0f);libvlc_media_player_set_equalizer(g_player,preset);libvlc_audio_equalizer_release(preset);}
        return;
    }
    libvlc_equalizer_t *eq=libvlc_audio_equalizer_new();
    if(!eq)return;
    int n=libvlc_audio_equalizer_get_band_count();
    for(int i=0;i<n;i++){
        float gain=0.0f;
        if(g_eqProfile==1){static const float v[10]={5,4,3,2,1,0,0,0,0,0};gain=v[i<10?i:9];}
        else if(g_eqProfile==2){static const float v[10]={-2,-1,0,2,3,3,2,1,0,-1};gain=v[i<10?i:9];}
        else if(g_eqProfile==3){static const float v[10]={0,0,0,0,1,2,3,4,5,5};gain=v[i<10?i:9];}
        libvlc_audio_equalizer_set_amp_at_index(eq,gain,(unsigned)i);
    }
    libvlc_audio_equalizer_set_preamp(eq,0.0f);
    libvlc_media_player_set_equalizer(g_player,eq);
    libvlc_audio_equalizer_release(eq);
}
static void PlayIndex(int index) {
    if(index<0||index>=g_playlistCount)return;
    DWORD attributes=GetFileAttributesW(g_playlist[index]);
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_DIRECTORY)){
        wchar_t message[PATH_CAP+128];
        swprintf_s(message,_countof(message),L"The selected media file is unavailable:\n%s",g_playlist[index]);
        MessageBoxW(g_main,message,APP_NAME,MB_ICONWARNING);
        return;
    }
    char utf8[PATH_CAP*4]; int n=WideCharToMultiByte(CP_UTF8,0,g_playlist[index],-1,utf8,sizeof(utf8),NULL,NULL); if(n<=0)return;
    libvlc_media_t *m=libvlc_media_new_path(g_vlc,utf8); if(!m){MessageBoxW(g_main,L"LibVLC could not open this file.",APP_NAME,MB_ICONERROR);return;}
    libvlc_media_player_stop(g_player);
    if(g_selectedRenderer&&!g_rendererApplied){
        if(libvlc_media_player_set_renderer(g_player,g_selectedRenderer)!=0){
            libvlc_media_release(m);
            libvlc_renderer_item_release(g_selectedRenderer);g_selectedRenderer=NULL;g_rendererApplied=FALSE;
            SendMessageW(g_renderers,CB_SETCURSEL,0,0);
            SetStatus(L"Receiver setup failed; using local playback");
            PlayIndex(index);
            return;
        }
        g_rendererApplied=TRUE;
    }
    if(g_replayGain)libvlc_media_add_option(m,":audio-replay-gain-mode=track");
    if(g_compressor){
        libvlc_media_add_option(m,":audio-filter=compressor");
        libvlc_media_add_option(m,":compressor-threshold=-18");
        libvlc_media_add_option(m,":compressor-ratio=4");
        libvlc_media_add_option(m,":compressor-attack=20");
        libvlc_media_add_option(m,":compressor-release=250");
        libvlc_media_add_option(m,":compressor-makeup-gain=0");
    }
    BeginArtworkParse(m);
    libvlc_media_player_set_media(g_player,m);libvlc_media_release(m);
    ApplyEqProfile();
    if(libvlc_media_player_play(g_player)!=0){
        if(g_selectedRenderer){
            libvlc_media_player_stop(g_player);
            libvlc_media_player_release(g_player);
            libvlc_renderer_item_release(g_selectedRenderer);g_selectedRenderer=NULL;g_rendererApplied=FALSE;
            SendMessageW(g_renderers,CB_SETCURSEL,0,0);
            g_player=libvlc_media_player_new(g_vlc);
            if(g_player){
                libvlc_media_player_set_hwnd(g_player,g_video);
                libvlc_media_t *local=libvlc_media_new_path(g_vlc,utf8);
                if(local){
                    if(g_replayGain)libvlc_media_add_option(local,":audio-replay-gain-mode=track");
                    if(g_compressor){libvlc_media_add_option(local,":audio-filter=compressor");libvlc_media_add_option(local,":compressor-threshold=-18");libvlc_media_add_option(local,":compressor-ratio=4");libvlc_media_add_option(local,":compressor-attack=20");libvlc_media_add_option(local,":compressor-release=250");libvlc_media_add_option(local,":compressor-makeup-gain=0");}
                    libvlc_media_player_set_media(g_player,local);libvlc_media_release(local);ApplyEqProfile();
                }
            }
        }
        if(!g_player || libvlc_media_player_play(g_player)!=0){
            g_hasMedia=FALSE;g_pendingResume=FALSE;ResetArtwork();
            MessageBoxW(g_main,L"Playback could not start locally or on the selected receiver. Check the media file and VLC plugins.",APP_NAME,MB_ICONERROR);
            return;
        }
        SetStatus(L"Cast could not start; playing locally");
    }
    g_videoWidth=0;g_videoHeight=0;g_videoBoundsValid=FALSE;
    g_current=index;g_selected=index;g_hasMedia=TRUE;g_lastUiTime=-1;g_overlayLength=0;g_overlayPlaying=TRUE;g_endHandled=FALSE;
    g_pendingResume=FALSE;g_pendingResumeTicks=0;
    PositionVideoWindow();
    AddRecent(g_playlist[index]);
    libvlc_media_player_set_rate(g_player,g_playbackRate);
    InvalidateRect(g_main,NULL,FALSE);
}
static int GetNextPlaylistIndex(void) {
    if(g_playlistCount<=0)return -1;
    if(g_shuffle&&g_playlistCount>1) {
        int next=g_current;
        while(next==g_current)next=rand()%g_playlistCount;
        return next;
    }
    return (g_current+1)%g_playlistCount;
}
static void AddDroppedFiles(HDROP drop) {
    UINT count=DragQueryFileW(drop,0xFFFFFFFF,NULL,0);
    for(UINT i=0;i<count&&g_playlistCount<MAX_PLAYLIST;i++) {
        wchar_t path[PATH_CAP];
        if(DragQueryFileW(drop,i,path,PATH_CAP)>0)
            wcsncpy_s(g_playlist[g_playlistCount++],PATH_CAP,path,_TRUNCATE);
    }
    DragFinish(drop);
    if(g_selected<0&&g_playlistCount>0)g_selected=0;
    SavePersistentState();InvalidateRect(g_main,NULL,FALSE);
}
static void AddFiles(void) {
    OPENFILENAMEW ofn; wchar_t buf[32768]=L""; ZeroMemory(&ofn,sizeof(ofn));
    ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=g_main;ofn.lpstrFile=buf;ofn.nMaxFile=32768;
    ofn.lpstrFilter=L"Media files\0*.mp3;*.wav;*.flac;*.m4a;*.aac;*.ogg;*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.webm\0All files\0*.*\0";
    ofn.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_ALLOWMULTISELECT|OFN_HIDEREADONLY;ofn.lpstrTitle=L"Add media to playlist";
    if(!GetOpenFileNameW(&ofn))return;
    wchar_t *p=buf; size_t first=wcslen(p); p+=first+1;
    if(!*p) { if(g_playlistCount<MAX_PLAYLIST) wcsncpy_s(g_playlist[g_playlistCount++],PATH_CAP,buf,_TRUNCATE); }
    else {
        wchar_t dir[PATH_CAP]; wcsncpy_s(dir,PATH_CAP,buf,_TRUNCATE);
        while(*p&&g_playlistCount<MAX_PLAYLIST){wchar_t full[PATH_CAP];swprintf_s(full,PATH_CAP,L"%s\\%s",dir,p);wcsncpy_s(g_playlist[g_playlistCount++],PATH_CAP,full,_TRUNCATE);p+=wcslen(p)+1;}
    }
    if(g_selected<0&&g_playlistCount>0)g_selected=0;
    InvalidateRect(g_main,NULL,FALSE);
}
static void RemovePlaylistItem(int index) {
    if(index<0||index>=g_playlistCount)return;
    if(index==g_current){libvlc_media_player_stop(g_player);g_hasMedia=FALSE;g_current=-1;}
    else if(index<g_current)g_current--;
    for(int i=index+1;i<g_playlistCount;i++)
        wcsncpy_s(g_playlist[i-1],PATH_CAP,g_playlist[i],_TRUNCATE);
    g_playlistCount--;SavePersistentState();
    if(g_playlistCount==0){g_selected=-1;g_current=-1;g_playlistScroll=0;}
    else {
        if(index==g_selected)g_selected=index<g_playlistCount?index:g_playlistCount-1;
        else if(index<g_selected)g_selected--;
        if(g_selected<0)g_selected=0;
    }
    InvalidateRect(g_main,NULL,FALSE);
}
static void MovePlaylistItem(int from,int to) {
    if(from<0||from>=g_playlistCount||to<0||to>=g_playlistCount||from==to)return;
    wchar_t temp[PATH_CAP];wcsncpy_s(temp,PATH_CAP,g_playlist[from],_TRUNCATE);
    if(from<to)for(int i=from;i<to;i++)wcsncpy_s(g_playlist[i],PATH_CAP,g_playlist[i+1],_TRUNCATE);
    else for(int i=from;i>to;i--)wcsncpy_s(g_playlist[i],PATH_CAP,g_playlist[i-1],_TRUNCATE);
    wcsncpy_s(g_playlist[to],PATH_CAP,temp,_TRUNCATE);
    if(g_current==from)g_current=to;else if(from<g_current&&to>=g_current)g_current--;else if(from>g_current&&to<=g_current)g_current++;
    g_selected=to;SavePersistentState();InvalidateRect(g_main,NULL,FALSE);
}
static void SavePlaylist(void) {
    OPENFILENAMEW ofn={0};wchar_t path[PATH_CAP]=L"Playlist.m3u";
    ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=g_main;ofn.lpstrFile=path;ofn.nMaxFile=PATH_CAP;
    ofn.lpstrFilter=L"M3U playlists (*.m3u)\0*.m3u\0All files (*.*)\0*.*\0";
    ofn.lpstrDefExt=L"m3u";ofn.lpstrTitle=L"Save playlist";
    ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
    if(!GetSaveFileNameW(&ofn))return;
    FILE *fp=NULL;if(_wfopen_s(&fp,path,L"wb")!=0||!fp){MessageBoxW(g_main,L"Could not save the playlist.",APP_NAME,MB_ICONERROR);return;}
    const unsigned char bom[3]={0xEF,0xBB,0xBF};fwrite(bom,1,3,fp);
    fputs("#EXTM3U\n",fp);
    for(int i=0;i<g_playlistCount;i++){
        int bytes=WideCharToMultiByte(CP_UTF8,0,g_playlist[i],-1,NULL,0,NULL,NULL);
        if(bytes>1){char *utf8=(char*)malloc((size_t)bytes);if(utf8){WideCharToMultiByte(CP_UTF8,0,g_playlist[i],-1,utf8,bytes,NULL,NULL);fputs(utf8,fp);fputc('\n',fp);free(utf8);}}
    }
    fclose(fp);
}
static void LoadPlaylist(void) {
    OPENFILENAMEW ofn={0};wchar_t path[PATH_CAP]=L"";
    ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=g_main;ofn.lpstrFile=path;ofn.nMaxFile=PATH_CAP;
    ofn.lpstrFilter=L"M3U playlists (*.m3u;*.m3u8)\0*.m3u;*.m3u8\0All files (*.*)\0*.*\0";
    ofn.lpstrTitle=L"Load playlist";ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if(!GetOpenFileNameW(&ofn))return;
    FILE *fp=NULL;if(_wfopen_s(&fp,path,L"rb")!=0||!fp){MessageBoxW(g_main,L"Could not open the playlist.",APP_NAME,MB_ICONERROR);return;}
    if(g_hasMedia){libvlc_media_player_stop(g_player);g_hasMedia=FALSE;}
    g_playlistCount=0;g_current=-1;g_selected=-1;g_playlistScroll=0;
    char line[PATH_CAP*4];
    while(g_playlistCount<MAX_PLAYLIST&&fgets(line,sizeof(line),fp)){
        size_t n=strcspn(line,"\r\n");line[n]=0;
        if(g_playlistCount==0&&strlen(line)>=3&&(unsigned char)line[0]==0xEF&&(unsigned char)line[1]==0xBB&&(unsigned char)line[2]==0xBF)
            memmove(line,line+3,strlen(line+3)+1);
        if(!line[0]||line[0]=='#')continue;
        UINT cp=CP_UTF8;
        int chars=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,line,-1,NULL,0);
        if(chars<=0){cp=CP_ACP;chars=MultiByteToWideChar(cp,0,line,-1,NULL,0);}
        if(chars>0&&chars<=PATH_CAP){
            MultiByteToWideChar(cp,0,line,-1,g_playlist[g_playlistCount],PATH_CAP);
            g_playlistCount++;
        }
    }
    fclose(fp);if(g_playlistCount)g_selected=0;
    InvalidateRect(g_main,NULL,FALSE);
}
static void ShowPlaybackContextMenu(HWND hwnd,int x,int y) {
    if(!g_hasMedia)return;
    HMENU menu=CreatePopupMenu();
    HMENU audio=CreatePopupMenu(),subs=CreatePopupMenu(),speed=CreatePopupMenu(),eq=CreatePopupMenu();
    if(!menu||!audio||!subs||!speed||!eq){if(menu)DestroyMenu(menu);if(audio)DestroyMenu(audio);if(subs)DestroyMenu(subs);if(speed)DestroyMenu(speed);if(eq)DestroyMenu(eq);return;}
    libvlc_track_description_t *tracks=libvlc_audio_get_track_description(g_player);
    int n=0;
    for(libvlc_track_description_t *t=tracks;t;t=t->p_next){
        wchar_t trackName[256]=L"Audio track";
        if(t->psz_name)MultiByteToWideChar(CP_UTF8,0,t->psz_name,-1,trackName,256);
        AppendMenuW(audio,MF_STRING,t->i_id+20000,trackName);n++;
    }
    if(!n)AppendMenuW(audio,MF_STRING|MF_GRAYED,0,L"No audio tracks");
    if(tracks)libvlc_track_description_list_release(tracks);
    tracks=libvlc_video_get_spu_description(g_player);n=0;
    AppendMenuW(subs,MF_STRING,21000,L"Disable subtitles");
    for(libvlc_track_description_t *t=tracks;t;t=t->p_next){
        wchar_t trackName[256]=L"Subtitle";
        if(t->psz_name)MultiByteToWideChar(CP_UTF8,0,t->psz_name,-1,trackName,256);
        AppendMenuW(subs,MF_STRING,t->i_id+21000,trackName);n++;
    }
    if(tracks)libvlc_track_description_list_release(tracks);
    AppendMenuW(speed,MF_STRING,22005,L"0.5x");
    AppendMenuW(speed,MF_STRING,22007,L"0.75x");
    AppendMenuW(speed,MF_STRING,22010,L"1.0x (Normal)");
    AppendMenuW(speed,MF_STRING,22012,L"1.25x");
    AppendMenuW(speed,MF_STRING,22015,L"1.5x");
    AppendMenuW(speed,MF_STRING,22020,L"2.0x");
    AppendMenuW(menu,MF_POPUP,(UINT_PTR)audio,L"Audio track");
    AppendMenuW(menu,MF_POPUP,(UINT_PTR)subs,L"Subtitles");
    AppendMenuW(menu,MF_STRING,23001,L"Load external subtitle...");
    AppendMenuW(menu,MF_POPUP,(UINT_PTR)speed,L"Playback speed");
    AppendMenuW(eq,MF_STRING,25000,L"Flat");
    AppendMenuW(eq,MF_STRING,25001,L"Classical");
    AppendMenuW(eq,MF_STRING,25002,L"Club");
    AppendMenuW(eq,MF_STRING,25003,L"Dance");
    AppendMenuW(eq,MF_STRING,25004,L"Full bass");
    AppendMenuW(eq,MF_STRING,25005,L"Full treble");
    AppendMenuW(eq,MF_STRING,25006,L"Headphones");
    AppendMenuW(eq,MF_STRING,25007,L"Laptop / speakers");
    AppendMenuW(eq,MF_STRING,25008,L"Live");
    AppendMenuW(eq,MF_STRING,25009,L"Rock");
    AppendMenuW(menu,MF_POPUP,(UINT_PTR)eq,L"Equalizer presets");
    HMENU profiles=CreatePopupMenu();
    AppendMenuW(profiles,MF_STRING|(g_eqProfile==0?MF_CHECKED:0),25100,L"Flat");
    AppendMenuW(profiles,MF_STRING|(g_eqProfile==1?MF_CHECKED:0),25101,L"Bass boost");
    AppendMenuW(profiles,MF_STRING|(g_eqProfile==2?MF_CHECKED:0),25102,L"Vocal clarity");
    AppendMenuW(profiles,MF_STRING|(g_eqProfile==3?MF_CHECKED:0),25103,L"Treble boost");
    AppendMenuW(menu,MF_POPUP,(UINT_PTR)profiles,L"ABM graphic EQ");
    AppendMenuW(menu,MF_STRING|(g_replayGain?MF_CHECKED:0),23005,L"ReplayGain track normalisation (next track)");
    AppendMenuW(menu,MF_STRING|(g_compressor?MF_CHECKED:0),23006,L"Dynamic-range compressor (next track)");
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    AppendMenuW(menu,MF_STRING,23002,L"Media information...");
    AppendMenuW(menu,MF_STRING|(g_shuffle?MF_CHECKED:0),23003,L"Shuffle");
    AppendMenuW(menu,MF_STRING|(g_repeat?MF_CHECKED:0),23004,L"Repeat current");
    POINT pt={x,y};ClientToScreen(hwnd,&pt);
    int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd,NULL);
    if(cmd>=20000&&cmd<21000)libvlc_audio_set_track(g_player,cmd-20000);
    else if(cmd>=21000&&cmd<22000)libvlc_video_set_spu(g_player,cmd-21000);
    else if(cmd==23001){
        OPENFILENAMEW ofn={0};wchar_t path[PATH_CAP]=L"";
        ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=hwnd;ofn.lpstrFile=path;ofn.nMaxFile=PATH_CAP;
        ofn.lpstrFilter=L"Subtitle files (*.srt;*.ass;*.ssa;*.sub;*.vtt)\0*.srt;*.ass;*.ssa;*.sub;*.vtt\0All files\0*.*\0";
        ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        if(GetOpenFileNameW(&ofn)){char utf8[PATH_CAP*4];if(WideCharToMultiByte(CP_UTF8,0,path,-1,utf8,sizeof(utf8),NULL,NULL)>0)libvlc_video_set_subtitle_file(g_player,utf8);}
    } else if(cmd>=22000&&cmd<=22020){
        float rate=cmd==22005?0.5f:cmd==22007?0.75f:cmd==22010?1.0f:cmd==22012?1.25f:cmd==22015?1.5f:2.0f;
        g_playbackRate=rate;libvlc_media_player_set_rate(g_player,rate);
    } else if(cmd==23002){
        libvlc_media_t *m=libvlc_media_player_get_media(g_player);
        wchar_t info[2048]=L"";
        if(m){
            const char *title=libvlc_media_get_meta(m,libvlc_meta_Title);
            const char *artist=libvlc_media_get_meta(m,libvlc_meta_Artist);
            const char *album=libvlc_media_get_meta(m,libvlc_meta_Album);
            int64_t duration=libvlc_media_get_duration(m);
            wchar_t wt[512]=L"(unknown)",wa[512]=L"(unknown)",wal[512]=L"(unknown)";
            if(title){MultiByteToWideChar(CP_UTF8,0,title,-1,wt,512);libvlc_free((void*)title);}
            if(artist){MultiByteToWideChar(CP_UTF8,0,artist,-1,wa,512);libvlc_free((void*)artist);}
            if(album){MultiByteToWideChar(CP_UTF8,0,album,-1,wal,512);libvlc_free((void*)album);}
            swprintf_s(info,2048,L"Title: %s\nArtist: %s\nAlbum: %s\nDuration: %02lld:%02lld",wt,wa,wal,(long long)(duration/60000),(long long)((duration/1000)%60));
            libvlc_media_release(m);
        } else wcscpy_s(info,2048,L"Media information is not available yet.");
        MessageBoxW(hwnd,info,L"Media information - ABM AV Player",MB_OK|MB_ICONINFORMATION);
    } else if(cmd==23005){g_replayGain=!g_replayGain;SaveAudioPreferences();}
    else if(cmd==23006){g_compressor=!g_compressor;SaveAudioPreferences();}
    else if(cmd>=25100&&cmd<=25103){g_eqProfile=cmd-25100;ApplyEqProfile();SaveAudioPreferences();}
    else if(cmd==23003)g_shuffle=!g_shuffle;
    else if(cmd==23004)g_repeat=!g_repeat;
    else if(cmd>=25000&&cmd<=25009){
        libvlc_equalizer_t *eqp=libvlc_audio_equalizer_new_from_preset(cmd-25000);
        if(eqp){libvlc_audio_equalizer_set_preamp(eqp,0.0f);libvlc_media_player_set_equalizer(g_player,eqp);libvlc_audio_equalizer_release(eqp);g_eqProfile=10+(cmd-25000);SaveAudioPreferences();}
    }
    DestroyMenu(menu);
    InvalidateRect(hwnd,NULL,FALSE);
}

static void ShowPlaylistContextMenu(HWND hwnd,int x,int y,int index) {
    if(index>=0&&index<g_playlistCount)g_selected=index;
    else g_selected=-1;
    HMENU menu=CreatePopupMenu();
    if(!menu)return;
    AppendMenuW(menu,MF_STRING|(g_selected>=0?0:MF_GRAYED),1,L"Play");
    AppendMenuW(menu,MF_STRING|(g_selected>=0?0:MF_GRAYED),2,L"Remove from playlist");
    AppendMenuW(menu,MF_STRING|((g_selected>0)?0:MF_GRAYED),3,L"Move up");
    AppendMenuW(menu,MF_STRING|((g_selected>=0&&g_selected<g_playlistCount-1)?0:MF_GRAYED),4,L"Move down");
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    AppendMenuW(menu,MF_STRING|(g_playlistCount?0:MF_GRAYED),5,L"Clear playlist");
    AppendMenuW(menu,MF_STRING|(g_playlistCount?0:MF_GRAYED),6,L"Save playlist...");
    SetForegroundWindow(hwnd);
    POINT pt={x,y};ClientToScreen(hwnd,&pt);
    int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd,NULL);
    DestroyMenu(menu);
    switch(cmd){
        case 1:PlayIndex(g_selected);break;
        case 2:RemovePlaylistItem(g_selected);break;
        case 3:MovePlaylistItem(g_selected,g_selected-1);break;
        case 4:MovePlaylistItem(g_selected,g_selected+1);break;
        case 5:if(MessageBoxW(hwnd,L"Clear all items from the playlist?",APP_NAME,MB_YESNO|MB_ICONQUESTION)==IDYES){if(g_hasMedia)libvlc_media_player_stop(g_player);g_hasMedia=FALSE;g_playlistCount=0;g_current=g_selected=-1;g_playlistScroll=0;InvalidateRect(hwnd,NULL,FALSE);}break;
        case 6:SavePlaylist();break;
    }
}

static void SeekAt(int x) {
    int64_t len=libvlc_media_player_get_length(g_player);if(len<=0)return;
    int width=R_SEEK.right-R_SEEK.left;int p=x-R_SEEK.left;if(p<0)p=0;if(p>width)p=width;
    libvlc_media_player_set_time(g_player,(len*p)/width);
}
static void InvalidateButton(int hit) {
    if(!g_main)return;
    RECT r={0,0,0,0};
    switch(hit) {
        case HIT_OPEN:r=R_OPEN;break;
        case HIT_PLAY:r=R_PLAY;break;
        case HIT_STOP:r=R_STOP;break;
        case HIT_PREV:r=R_PREV;break;
        case HIT_NEXT:r=R_NEXT;break;
        case HIT_SCAN:r=R_SCAN;break;
        default:return;
    }
    InflateRect(&r,3,3);
    InvalidateRect(g_main,&r,FALSE);
}
static void UpdateUI(void) {
    if(!g_main || !g_player)return;
    /* VLC renders video frames independently. Refresh only the timeline while
       playback is advancing, and only when the reported position changes. */
    if(libvlc_media_player_get_state(g_player)!=libvlc_Playing)return;
    int64_t current=libvlc_media_player_get_time(g_player);
    if(current<0 || current==g_lastUiTime)return;
    g_lastUiTime=current;
    RECT timeline={R_SEEK.left,R_SEEK.top-14,R_SEEK.right,R_SEEK.bottom+16};
    RECT timeText={20,R_SEEK.top+76,190,R_SEEK.top+106};
    InvalidateRect(g_main,&timeline,FALSE);
    InvalidateRect(g_main,&timeText,FALSE);
}
static void OverlayButton(HDC dc,RECT r,int kind,BOOL primary) {
    HBRUSH b=CreateSolidBrush(primary?RGB(247,248,250):RGB(43,46,51));
    HPEN e=CreatePen(PS_SOLID,1,primary?RGB(247,248,250):RGB(70,74,80));
    HBRUSH ob=(HBRUSH)SelectObject(dc,b);HPEN oe=(HPEN)SelectObject(dc,e);
    Ellipse(dc,r.left,r.top,r.right,r.bottom);
    SelectObject(dc,ob);SelectObject(dc,oe);DeleteObject(b);DeleteObject(e);
    int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
    HPEN g=CreatePen(PS_SOLID,2,primary?RGB(20,22,25):RGB(232,235,239));oe=(HPEN)SelectObject(dc,g);
    if(kind==1){MoveToEx(dc,x-7,y-11,NULL);LineTo(dc,x+10,y);LineTo(dc,x-7,y+11);LineTo(dc,x-7,y-11);}
    else if(kind==2){MoveToEx(dc,x-5,y-10,NULL);LineTo(dc,x-5,y+10);MoveToEx(dc,x+5,y-10,NULL);LineTo(dc,x+5,y+10);}
    else if(kind==3){RECT s={x-6,y-6,x+6,y+6};HBRUSH sb=CreateSolidBrush(primary?RGB(20,22,25):RGB(232,235,239));FillRect(dc,&s,sb);DeleteObject(sb);}
    else if(kind==4){MoveToEx(dc,x+8,y-8,NULL);LineTo(dc,x-4,y);LineTo(dc,x+8,y+8);MoveToEx(dc,x-8,y-10,NULL);LineTo(dc,x-8,y+10);}
    else if(kind==5){MoveToEx(dc,x-8,y-8,NULL);LineTo(dc,x+4,y);LineTo(dc,x-8,y+8);MoveToEx(dc,x+8,y-10,NULL);LineTo(dc,x+8,y+10);}
    SelectObject(dc,oe);DeleteObject(g);
}
static void PaintFullscreenOverlay(HWND hwnd) {
    RECT wr;GetWindowRect(hwnd,&wr);
    int w=wr.right-wr.left,h=wr.bottom-wr.top;
    if(w<=0||h<=0)return;

    BITMAPINFO bmi={0};
    bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=w;
    bmi.bmiHeader.biHeight=-h; /* top-down 32-bit BGRA */
    bmi.bmiHeader.biPlanes=1;
    bmi.bmiHeader.biBitCount=32;
    bmi.bmiHeader.biCompression=BI_RGB;
    void *pixels=NULL;
    HDC screen=GetDC(NULL),mem=CreateCompatibleDC(screen);
    HBITMAP dib=CreateDIBSection(screen,&bmi,DIB_RGB_COLORS,&pixels,NULL,0);
    if(!dib||!mem||!pixels){if(dib)DeleteObject(dib);if(mem)DeleteDC(mem);if(screen)ReleaseDC(NULL,screen);return;}
    HBITMAP oldBmp=(HBITMAP)SelectObject(mem,dib);
    RECT canvas={0,0,w,h};
    HBRUSH black=CreateSolidBrush(RGB(0,0,0));FillRect(mem,&canvas,black);DeleteObject(black);
    SetBkMode(mem,TRANSPARENT);
    SetTextColor(mem,RGB(218,222,228));
    HFONT font=CreateFontW(13,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HFONT oldFont=(HFONT)SelectObject(mem,font);

    /* Timeline floats independently above the transport buttons. */
    RECT tr={R_SEEK.left,h-112,R_SEEK.right,h-108};
    HBRUSH track=CreateSolidBrush(RGB(82,87,94));FillRect(mem,&tr,track);DeleteObject(track);
    /* Keep the layered-window paint path independent of LibVLC locks. */
    int64_t len=g_overlayLength;
    int64_t now=g_lastUiTime>0?g_lastUiTime:0;
    int pos=len>0?(int)(now*1000/len):0;if(pos<0)pos=0;if(pos>1000)pos=1000;
    RECT progress=tr;progress.right=progress.left+(progress.right-progress.left)*pos/1000;
    HBRUSH played=CreateSolidBrush(RGB(238,241,245));FillRect(mem,&progress,played);DeleteObject(played);
    wchar_t elapsed[24],total[24];FormatTime((int)now,elapsed,24);FormatTime((int)len,total,24);
    RECT et={R_SEEK.left,h-139,R_SEEK.left+70,h-120};
    RECT tt={R_SEEK.right-70,h-139,R_SEEK.right,h-120};
    DrawTextW(mem,elapsed,-1,&et,DT_LEFT|DT_SINGLELINE|DT_VCENTER);
    DrawTextW(mem,total,-1,&tt,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);

    /* Each transport action is its own floating control: no dock or panel. */
    RECT prev={w/2-112,h-60,w/2-64,h-12};
    RECT play={w/2-30,h-66,w/2+30,h-6};
    RECT next={w/2+64,h-60,w/2+112,h-12};
    RECT stop={w/2+132,h-60,w/2+180,h-12};
    OverlayButton(mem,prev,4,FALSE);
    OverlayButton(mem,play,g_overlayPlaying?2:1,TRUE);
    OverlayButton(mem,next,5,FALSE);
    OverlayButton(mem,stop,3,FALSE);

    /* Minimal volume label and slider; no enclosing rectangle. */
    RECT vl={w-214,h-44,w-160,h-26};
    SetTextColor(mem,RGB(202,207,214));
    DrawTextW(mem,L"VOLUME",-1,&vl,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT vt={w-148,h-36,w-38,h-32};
    HBRUSH vtrack=CreateSolidBrush(RGB(86,91,98));FillRect(mem,&vt,vtrack);DeleteObject(vtrack);
    int volume=g_volume;if(volume<0)volume=0;if(volume>100)volume=100;
    RECT vf=vt;vf.right=vf.left+(vf.right-vf.left)*volume/100;
    HBRUSH vfill=CreateSolidBrush(RGB(238,241,245));FillRect(mem,&vf,vfill);DeleteObject(vfill);
    int knobX=vf.right;
    RECT knob={knobX-5,h-41,knobX+5,h-27};
    HBRUSH kb=CreateSolidBrush(RGB(248,249,250));FillRect(mem,&knob,kb);DeleteObject(kb);

    SelectObject(mem,oldFont);DeleteObject(font);

    /* UpdateLayeredWindow requires premultiplied BGRA. The overlay is only
       FULLSCREEN_OVERLAY_HEIGHT pixels tall, not monitor-sized, so this
       transparency pass touches a small control strip rather than millions
       of video-area pixels. Black canvas pixels become fully transparent. */
    DWORD *px=(DWORD*)pixels;
    size_t count=(size_t)w*(size_t)h;
    for(size_t i=0;i<count;i++){
        BYTE blue=(BYTE)(px[i]&0xff),green=(BYTE)((px[i]>>8)&0xff),red=(BYTE)((px[i]>>16)&0xff);
        BYTE maxc=red>green?red:green;if(blue>maxc)maxc=blue;
        if(maxc==0){px[i]=0;continue;}
        BYTE alpha=(maxc>220)?245:205;
        blue=(BYTE)((blue*alpha+127)/255);
        green=(BYTE)((green*alpha+127)/255);
        red=(BYTE)((red*alpha+127)/255);
        px[i]=((DWORD)alpha<<24)|((DWORD)red<<16)|((DWORD)green<<8)|blue;
    }
    POINT dst={wr.left,wr.top},src={0,0};SIZE size={w,h};
    BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd,screen,&dst,&size,mem,&src,0,&blend,ULW_ALPHA);
    SelectObject(mem,oldBmp);DeleteObject(dib);DeleteDC(mem);ReleaseDC(NULL,screen);
}
static LRESULT CALLBACK FullscreenOverlayProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    HWND owner=GetWindow(hwnd,GW_OWNER);
    if(msg==WM_PAINT){PAINTSTRUCT ps;BeginPaint(hwnd,&ps);EndPaint(hwnd,&ps);PaintFullscreenOverlay(hwnd);return 0;}
    if(msg==WM_MOUSEMOVE||msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP){
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};MapWindowPoints(hwnd,owner,&pt,1);
        SendMessageW(owner,msg,wp,MAKELPARAM(pt.x,pt.y));return 0;
    }
    if(msg==WM_SETCURSOR){SetCursor(LoadCursorW(NULL,IDC_ARROW));return TRUE;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static LRESULT CALLBACK VideoKeyboardSubclass(HWND video, UINT msg,
        WPARAM wp, LPARAM lp, UINT_PTR subclassId, DWORD_PTR refData) {
    (void)subclassId;
    HWND owner = (HWND)refData;
    if (msg == WM_MOUSEMOVE && IsWindow(owner) && g_fullscreen) {
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        MapWindowPoints(video,owner,&pt,1);
        SendMessageW(owner,WM_MOUSEMOVE,wp,MAKELPARAM(pt.x,pt.y));
    }
    if (msg == WM_SETCURSOR && g_fullscreen && !g_fullscreenControlsVisible) {
        SetCursor(NULL);
        return TRUE;
    }
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) &&
        (wp == VK_ESCAPE || wp == VK_F11)) {
        /* LibVLC's child HWND can own keyboard focus. Route fullscreen
           shortcuts to the player window so they are never swallowed by VLC. */
        PostMessageW(owner, WM_KEYDOWN, wp, lp);
        return 0;
    }
    return DefSubclassProc(video, msg, wp, lp);
}

static void ToggleFullscreen(HWND hwnd) {
    /* Hide the VLC child while the top-level window changes styles and bounds.
       This prevents a stale fullscreen child rectangle from covering the
       restored title/header area during the transition. */
    if (g_video) ShowWindow(g_video, SW_HIDE);

    if(!g_fullscreen) {
        g_savedStyle=(DWORD_PTR)GetWindowLongPtrW(hwnd,GWL_STYLE);
        g_savedPlacement.length=sizeof(g_savedPlacement);
        GetWindowPlacement(hwnd,&g_savedPlacement);
        MONITORINFO mi={sizeof(mi)};
        HMONITOR monitor=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST);
        GetMonitorInfoW(monitor,&mi);
        g_fullscreen=TRUE;
        g_fullscreenControlsVisible=TRUE;
        g_fullscreenLastInputTick=GetTickCount();
        g_fullscreenMousePosValid=GetCursorPos(&g_fullscreenLastMousePos);
        SetCursor(LoadCursorW(NULL,IDC_ARROW));
        /* Capture mouse input at the top-level window for the duration of
           fullscreen. VLC can otherwise consume WM_MOUSEMOVE in its child
           HWND, especially after the child is resized to cover the monitor. */
        g_menuOpen=FALSE;g_receiverPopup=FALSE;
        ShowWindow(g_renderers,SW_HIDE);
        SetWindowLongPtrW(hwnd,GWL_STYLE,(LONG_PTR)(WS_POPUP|WS_VISIBLE|WS_CLIPCHILDREN));
        SetWindowPos(hwnd,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,
            mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,
            SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    } else {
        g_fullscreen=FALSE;
        g_fullscreenControlsVisible=TRUE;
        if(GetCapture()==hwnd)ReleaseCapture();
        if(g_fullscreenOverlay)ShowWindow(g_fullscreenOverlay,SW_HIDE);
        SetCursor(LoadCursorW(NULL,IDC_ARROW));
        /* Restore a known normal overlapped-window style instead of trusting
           a potentially stale style snapshot from the fullscreen transition. */
        SetWindowLongPtrW(hwnd,GWL_STYLE,
            (LONG_PTR)(WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN));
        SetWindowPlacement(hwnd,&g_savedPlacement);
        SetWindowPos(hwnd,NULL,0,0,0,0,
            SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        ShowWindow(hwnd,SW_RESTORE);
        ShowWindow(g_renderers,SW_SHOW);
    }

    /* Force a fresh child-window placement even if its cached rectangle
       happens to match a previous layout. */
    g_videoBoundsValid = FALSE;
    UpdateLayout(hwnd);
    SyncRendererComboVisibility();
    if(g_fullscreen && g_fullscreenOverlay) {
        RECT wr;GetWindowRect(hwnd,&wr);
        SetWindowPos(g_fullscreenOverlay,HWND_TOP,wr.left,
            wr.bottom-FULLSCREEN_OVERLAY_HEIGHT,wr.right-wr.left,
            FULLSCREEN_OVERLAY_HEIGHT,SWP_NOACTIVATE);
        ShowWindow(g_fullscreenOverlay,g_fullscreenControlsVisible?SW_SHOWNOACTIVATE:SW_HIDE);
        InvalidateRect(g_fullscreenOverlay,NULL,FALSE);
    } else if(g_fullscreenOverlay) ShowWindow(g_fullscreenOverlay,SW_HIDE);

    if (g_video && IsWindow(g_video)) {
        RECT vr = g_videoBounds;
        int vw = vr.right - vr.left;
        int vh = vr.bottom - vr.top;
        if (vw > 0 && vh > 0) {
            SetWindowPos(g_video, HWND_BOTTOM, vr.left, vr.top, vw, vh,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
    /* Re-sync the Direct2D HWND target with the restored client size.
       WM_SIZE can arrive while the non-client style transition is still in
       progress, so do one final resize and synchronous full-window repaint. */
    RECT clientRect = {0};
    GetClientRect(hwnd, &clientRect);
    if (clientRect.right > 0 && clientRect.bottom > 0) {
        ABM_D2D_Resize(&g_ui, (UINT)clientRect.right, (UINT)clientRect.bottom);
    }
    RedrawWindow(hwnd, NULL, NULL,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    SetFocus(hwnd);
}
static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        g_main=hwnd; LoadPersistentState(); LoadAudioPreferences(); if(!ABM_D2D_Init(&g_ui,hwnd)){MessageBoxW(hwnd,L"Direct2D / DirectWrite initialization failed.",APP_NAME,MB_ICONERROR);return -1;} g_video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|SS_BLACKRECT,0,0,640,360,hwnd,NULL,GetModuleHandleW(NULL),NULL);
        DragAcceptFiles(hwnd,TRUE);
        /* Hidden item store; the visible selector and popup are Direct2D. */
        g_renderers=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWNLIST|CBS_HASSTRINGS,0,0,1,1,hwnd,(HMENU)ID_RENDERERS,GetModuleHandleW(NULL),NULL);
        SendMessageW(g_renderers,CB_ADDSTRING,0,(LPARAM)L"Local playback");SendMessageW(g_renderers,CB_SETCURSEL,0,0);
        ShowWindow(g_renderers,SW_HIDE);
        libvlc_media_player_set_hwnd(g_player,g_video);SetWindowSubclass(g_video,VideoKeyboardSubclass,1,(DWORD_PTR)hwnd);libvlc_audio_set_volume(g_player,g_volume);ShowWindow(g_video,SW_HIDE);UpdateLayout(hwnd);SetTimer(hwnd,TIMER_ID,33,NULL);return 0;
    }
    case WM_DROPFILES: AddDroppedFiles((HDROP)wp);return 0;
    case WM_ARTWORK_READY: if((libvlc_media_t *)wp==g_artMedia)LoadParsedArtwork(g_artMedia);return 0;
    case WM_ARTWORK_DOWNLOAD_READY: {
        LONG generation=(LONG)wp;
        if(generation==InterlockedCompareExchange(&g_artworkGeneration,0,0) && lp && EnsureDataDirectory()) {
            wchar_t path[PATH_CAP];
            swprintf_s(path,PATH_CAP,L"%s\\artwork-%ld.bin",g_dataDir,(long)generation);
            g_artworkLoaded=ABM_D2D_LoadImage(&g_ui,path);
            if(g_artworkLoaded)g_artworkFade=0;
            InvalidateRect(hwnd,NULL,FALSE);
        }
        return 0;
    }
    case WM_SIZE: {
        UpdateLayout(hwnd);
        if(wp!=SIZE_MINIMIZED)ABM_D2D_Resize(&g_ui,(UINT)LOWORD(lp),(UINT)HIWORD(lp));
        InvalidateRect(hwnd,NULL,FALSE);return 0;
    }
    case WM_GETMINMAXINFO: { MINMAXINFO *mi=(MINMAXINFO*)lp; mi->ptMinTrackSize.x=1000; mi->ptMinTrackSize.y=650; return 0; }
    case WM_MEASUREITEM:
        if(((LPMEASUREITEMSTRUCT)lp)->CtlID==ID_RENDERERS){
            ((LPMEASUREITEMSTRUCT)lp)->itemHeight=30;
            return TRUE;
        }
        break;
    case WM_DRAWITEM:
        if(((LPDRAWITEMSTRUCT)lp)->CtlID==ID_RENDERERS){
            LPDRAWITEMSTRUCT d=(LPDRAWITEMSTRUCT)lp;
            HBRUSH bg=CreateSolidBrush((d->itemState&ODS_SELECTED)?RGB(54,59,66):RGB(31,34,38));
            FillRect(d->hDC,&d->rcItem,bg);
            DeleteObject(bg);
            if(d->itemID!=(UINT)-1){
                wchar_t t[256]=L"";
                SendMessageW(g_renderers,CB_GETLBTEXT,d->itemID,(LPARAM)t);
                RECT textRect=d->rcItem;
                textRect.left+=13;
                textRect.right-=28;
                SetBkMode(d->hDC,TRANSPARENT);
                SetTextColor(d->hDC,RGB(235,238,242));
                DrawTextW(d->hDC,t,-1,&textRect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            }
            /* The native combo already paints its drop-down button.
               Do not draw a second chevron in the owner-draw edit item. */
            if(d->itemState&ODS_FOCUS)DrawFocusRect(d->hDC,&d->rcItem);
            return TRUE;
        }
        break;
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wp,RGB(235,238,242));
        SetBkColor((HDC)wp,RGB(31,34,38));
        return (LRESULT)g_comboBrush;
    case WM_COMMAND:if(LOWORD(wp)==ID_RENDERERS&&HIWORD(wp)==CBN_SELCHANGE){SelectRenderer();return 0;}break;
    case WM_RENDERERS_CHANGED:{
        RefreshRendererList();
        if(g_rendererCount>0){wchar_t found[128];swprintf_s(found,_countof(found),L"Found %u receiver(s)",(unsigned int)g_rendererCount);SetStatus(found);}
        if(g_selectedRenderer){
            BOOL present=FALSE;
            for(size_t i=0;i<g_comboCount;i++)if(g_comboItems[i]==g_selectedRenderer){present=TRUE;break;}
            if(!present){
                g_receiverLost=TRUE;
                libvlc_renderer_item_release(g_selectedRenderer);g_selectedRenderer=NULL;
                SendMessageW(g_renderers,CB_SETCURSEL,0,0);
                SelectRenderer();
                SetStatus(L"Receiver disconnected - returned to local playback");
            } else g_receiverLost=FALSE;
        }
        return 0;
    }
    case WM_TIMER:if(wp==TIMER_ID){
        if (g_fullscreen) {
            /* Do not depend on WM_MOUSEMOVE reaching a particular HWND.
               LibVLC owns the video child and the layered controls are a
               separate popup, so input messages can be routed differently
               as those windows appear/disappear. Poll the screen cursor
               position at the existing 30 Hz UI tick instead. */
            POINT mouseNow;
            if (GetCursorPos(&mouseNow)) {
                BOOL moved = !g_fullscreenMousePosValid ||
                    mouseNow.x != g_fullscreenLastMousePos.x ||
                    mouseNow.y != g_fullscreenLastMousePos.y;
                if (moved) {
                    g_fullscreenLastMousePos = mouseNow;
                    g_fullscreenMousePosValid = TRUE;
                    g_fullscreenLastInputTick = GetTickCount();
                    SetCursor(LoadCursorW(NULL,IDC_ARROW));
                    if (!g_fullscreenControlsVisible) {
                        g_fullscreenControlsVisible = TRUE;
                        if (g_fullscreenOverlay) {
                            RECT wr; GetWindowRect(hwnd,&wr);
                            SetWindowPos(g_fullscreenOverlay,HWND_TOP,
                                wr.left,wr.bottom-FULLSCREEN_OVERLAY_HEIGHT,
                                wr.right-wr.left,FULLSCREEN_OVERLAY_HEIGHT,
                                SWP_NOACTIVATE|SWP_SHOWWINDOW);
                            InvalidateRect(g_fullscreenOverlay,NULL,FALSE);
                        }
                    }
                }
            }
            if (g_fullscreenControlsVisible &&
                (DWORD)(GetTickCount()-g_fullscreenLastInputTick)>=FULLSCREEN_CONTROLS_TIMEOUT_MS) {
                g_fullscreenControlsVisible=FALSE;
                if(g_fullscreenOverlay)ShowWindow(g_fullscreenOverlay,SW_HIDE);
                SetCursor(NULL);
            }
            /* No periodic layered-window repaint while video decoding is active. */
        } else {
            g_fullscreenMousePosValid = FALSE;
        }
        if(g_discovererCount && !g_discoveryTimedOut && g_rendererCount==0 && g_discoveryStartedTick && (DWORD)(GetTickCount()-g_discoveryStartedTick)>=15000){g_discoveryTimedOut=TRUE;SetStatus(L"No receivers found yet - checking network and firewall");}
        int oldDrawerProgress=g_drawerProgress;
        g_drawerProgress=EaseToward(g_drawerProgress,g_drawerOpen?100:0,24);
        if(oldDrawerProgress!=g_drawerProgress){UpdateLayout(hwnd);InvalidateRect(hwnd,NULL,FALSE);}
        int oldMenuProgress=g_menuProgress;
        g_menuProgress=EaseToward(g_menuProgress,g_menuOpen?100:0,28);
        int oldLevel=g_hoverLevel;
        g_hoverLevel=EaseToward(g_hoverLevel,g_hoverButton?100:0,32);
        if(oldLevel!=g_hoverLevel) {
            InvalidateButton(g_hoverButton);
            if(g_hoverButton==HIT_NONE) {
                InvalidateButton(HIT_OPEN);InvalidateButton(HIT_PLAY);
                InvalidateButton(HIT_STOP);InvalidateButton(HIT_PREV);
                InvalidateButton(HIT_NEXT);InvalidateButton(HIT_SCAN);
            }
        }
        if(g_hasMedia && g_player && !g_pendingResume &&
           libvlc_media_player_get_state(g_player)==libvlc_Error){
            if(g_selectedRenderer){
                int64_t failedAt=libvlc_media_player_get_time(g_player);
                g_forcedResumeMs=failedAt>=0?failedAt:g_lastUiTime;
                if(g_forcedResumeMs<0)g_forcedResumeMs=0;
                g_useForcedResume=TRUE;
                SendMessageW(g_renderers,CB_SETCURSEL,0,0);
                SelectRenderer();
                g_useForcedResume=FALSE;
                SetStatus(L"Cast playback failed; attempting local playback");
            }else{
                g_hasMedia=FALSE;g_endHandled=TRUE;ResetArtwork();
                SetStatus(L"Playback failed. Check the media file and VLC codec plugins");
            }
        }
        if(g_pendingResume && g_player){
            libvlc_state_t resumeState=libvlc_media_player_get_state(g_player);
            if(resumeState==libvlc_Playing){
                if(g_pendingResumeMs>0)libvlc_media_player_set_time(g_player,g_pendingResumeMs);
                if(g_pendingResumePause)libvlc_media_player_set_pause(g_player,1);
                g_pendingResume=FALSE;
            }else if(resumeState==libvlc_Error || resumeState==libvlc_Ended || ++g_pendingResumeTicks>90){
                if(g_selectedRenderer && g_hasMedia && resumeState!=libvlc_Ended){
                    g_forcedResumeMs=g_pendingResumeMs;g_useForcedResume=TRUE;
                    SendMessageW(g_renderers,CB_SETCURSEL,0,0);
                    g_pendingResume=FALSE;
                    SelectRenderer();
                    g_useForcedResume=FALSE;
                    SetStatus(L"Cast connection did not recover; switched to local playback");
                }else{
                    g_pendingResume=FALSE;
                    SetStatus(L"Could not restore the previous playback position");
                }
            }
        }
        if(g_hasMedia&&!g_endHandled&&libvlc_media_player_get_state(g_player)==libvlc_Ended) {
            g_endHandled=TRUE;
            if(g_repeat)PlayIndex(g_current);
            else if(g_playlistCount>1)PlayIndex(GetNextPlaylistIndex());
            else {g_hasMedia=FALSE;ResetArtwork();InvalidateRect(hwnd,NULL,FALSE);}
        }
        g_visualPulse=(g_visualPulse+1)%10000;
        if (g_artworkLoaded && g_artworkFade < 100) {
            g_artworkFade += 5;
            if (g_artworkFade > 100) g_artworkFade = 100;
            InvalidateRect(hwnd,&R_VIDEO,FALSE);
        }
        /* Fullscreen video geometry is already established for the active
           output. Avoid extra synchronous LibVLC output queries on the UI
           thread while fullscreen input and playback controls are active. */
        if (!g_fullscreen && (++g_videoGeometryPoll % 5u) == 0u)
            RefreshVideoGeometry();
        BOOL audioOnlyLocal=!g_fullscreen && g_hasMedia && g_player &&
            !g_selectedRenderer && libvlc_media_player_has_vout(g_player)==0;
        if(audioOnlyLocal) {
            if(g_artworkLoaded) {
                if(g_visualizerActive) ABM_AudioVisualizer_Stop();
                g_visualizerActive=FALSE;
                g_visualizerTried=TRUE;
                if(g_video && !g_fullscreen) ShowWindow(g_video,SW_HIDE);
            } else {
                if(!g_visualizerTried) {
                    g_visualizerTried=TRUE;
                    g_visualizerActive=ABM_AudioVisualizer_Start();
                }
                if(g_visualizerActive) {
                    if(g_video && !g_fullscreen) ShowWindow(g_video,SW_HIDE);
                } else if(g_video && !g_fullscreen) ShowWindow(g_video,SW_SHOWNA);
            }
        } else {
            if(g_visualizerActive) ABM_AudioVisualizer_Stop();
            g_visualizerActive=FALSE; g_visualizerTried=FALSE;
            if(g_video && !g_fullscreen) {
                if (g_hasMedia) ShowWindow(g_video,SW_SHOWNA);
                else ShowWindow(g_video,SW_HIDE);
            }
        }
        if (!g_hasMedia || g_visualizerActive) {
            /* Keep the idle equalizer and audio spectrum fluid at the 30 Hz UI tick. */
            InvalidateRect(hwnd,&R_VIDEO,FALSE);
        } else if ((g_visualPulse % 3u) == 0u) {
            RECT glow=R_VIDEO;
            InflateRect(&glow,4,4);
            InvalidateRect(hwnd,&glow,FALSE);
        }
        if (g_hasMedia && libvlc_media_player_get_state(g_player)==libvlc_Playing) {
            g_overlayPlaying=TRUE;
            RECT playDirty=R_PLAY;
            InflateRect(&playDirty,8,8);
            InvalidateRect(hwnd,&playDirty,FALSE);
        } else {
            g_overlayPlaying=FALSE;
        }
        if (!g_fullscreen) {
            RECT headerDirty;
            GetClientRect(hwnd,&headerDirty);
            headerDirty.top=91;
            headerDirty.bottom=96;
            InvalidateRect(hwnd,&headerDirty,FALSE);
            RECT sidebarAccent={R_SIDEBAR.left+1,R_SIDEBAR.top+1,R_SIDEBAR.right-1,R_SIDEBAR.top+4};
            InvalidateRect(hwnd,&sidebarAccent,FALSE);
        }
        UpdateUI();
        /* Invalidate on every animation step, including the final transition
           from 20% to 0%, so no fragment of the menu is left on screen. */
        if(oldMenuProgress!=g_menuProgress) {
            RECT dirty=R_MENU_PANEL;
            InvalidateRect(hwnd,&dirty,FALSE);
        }
    }return 0;
    case WM_MOUSEWHEEL: {
        if(!g_drawerOpen)return 0;
        int delta=GET_WHEEL_DELTA_WPARAM(wp);
        int visible=(R_LIST.bottom>R_LIST.top)?(R_LIST.bottom-R_LIST.top)/64:0;
        int maxScroll=CountFilteredMedia()-visible;if(maxScroll<0)maxScroll=0;
        g_playlistScroll-=delta/WHEEL_DELTA;
        if(g_playlistScroll<0)g_playlistScroll=0;
        if(g_playlistScroll>maxScroll)g_playlistScroll=maxScroll;
        InvalidateRect(hwnd,NULL,FALSE);return 0;
    }
    case WM_LBUTTONDOWN: {
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        if(g_fullscreen) {
            if(Inside(R_PLAY,x,y)) {
                if(g_hasMedia) {
                    if(libvlc_media_player_get_state(g_player)==libvlc_Playing)libvlc_media_player_set_pause(g_player,1);
                    else libvlc_media_player_play(g_player);
                }
                InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;
            }
            if(Inside(R_PREV,x,y)){if(g_playlistCount)PlayIndex(g_current>0?g_current-1:0);ReleaseCapture();return 0;}
            if(Inside(R_NEXT,x,y)){if(g_playlistCount)PlayIndex(GetNextPlaylistIndex());ReleaseCapture();return 0;}
            if(Inside(R_STOP,x,y)){libvlc_media_player_stop(g_player);g_hasMedia=FALSE;ResetArtwork();InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
            if(y>=R_SEEK.top-12&&y<=R_SEEK.bottom+14){g_dragSeek=1;SeekAt(x);SetCapture(hwnd);return 0;}
            if(y>=R_VOLUME.top-12&&y<=R_VOLUME.bottom+12&&x>=R_VOLUME.left-8&&x<=R_VOLUME.right+8){
                g_dragVolume=1;
                int v=(x-R_VOLUME.left)*100/(R_VOLUME.right-R_VOLUME.left);
                if(v<0)v=0;if(v>100)v=100;
                if(v!=g_volume) {
                    g_volume=v;
                    libvlc_audio_set_volume(g_player,v);
                    if(g_fullscreenOverlay)InvalidateRect(g_fullscreenOverlay,NULL,FALSE);
                }
                SetCapture(hwnd);return 0;
            }
            ReleaseCapture();return 0;
        }
        if(!g_fullscreen&&Inside(R_DRAWER_BUTTON,x,y)){g_drawerOpen=!g_drawerOpen;g_drawerSettingsOpen=FALSE;g_receiverPopup=FALSE;g_searchFocused=FALSE;g_menuOpen=FALSE;SyncRendererComboVisibility();InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_SETTINGS_BUTTON,x,y)){g_drawerSettingsOpen=!g_drawerSettingsOpen;g_receiverPopup=FALSE;g_searchFocused=FALSE;UpdateLayout(hwnd);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&g_drawerSettingsOpen&&!g_fullscreen&&Inside(R_SEARCH,x,y)){g_searchFocused=TRUE;SetFocus(hwnd);InvalidateRect(hwnd,&R_SEARCH,FALSE);ReleaseCapture();return 0;}
        if(!g_fullscreen&&g_searchFocused){g_searchFocused=FALSE;InvalidateRect(hwnd,&R_SEARCH,FALSE);}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_TAB_QUEUE,x,y)){g_libraryTab=0;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_TAB_LIBRARY,x,y)){g_libraryTab=1;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_FILTER_ALL,x,y)){g_mediaFilter=0;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_FILTER_AUDIO,x,y)){g_mediaFilter=1;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_FILTER_VIDEO,x,y)){g_mediaFilter=2;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_GROUP,x,y)){g_groupMode=(g_groupMode+1)%3;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(!g_fullscreen&&Inside(R_MENU_BUTTON,x,y)){
            g_menuOpen=!g_menuOpen;g_receiverPopup=FALSE;
            SyncRendererComboVisibility();
            RECT dirty=R_MENU_PANEL;UnionRect(&dirty,&dirty,&R_MENU_BUTTON);
            InvalidateRect(hwnd,&dirty,FALSE);ReleaseCapture();return 0;
        }
        /* Clicking anywhere outside the open menu dismisses it. Continue
           processing the click so the underlying control still responds. */
        if(g_menuOpen && !Inside(R_MENU_PANEL,x,y)) {
            g_menuOpen=FALSE;
            SyncRendererComboVisibility();
            RECT dirty=R_MENU_PANEL;
            InvalidateRect(hwnd,&dirty,FALSE);
        }
        if(!g_fullscreen&&Inside(R_FULLSCREEN,x,y)){ToggleFullscreen(hwnd);ReleaseCapture();return 0;}
        if(g_menuOpen&&g_menuProgress>=80) {
            for(int mi=0;mi<13;mi++)if(Inside(R_MENU_ITEMS[mi],x,y)) {
                g_menuOpen=FALSE;
                SyncRendererComboVisibility();
                if(mi==0)AddFiles();
                else if(mi==1)StartDiscovery();
                else if(mi==2){StartDiscovery();MessageBoxW(hwnd,L"Choose your TV in the Playback Output selector. If media is already playing, the player will restart it on the selected receiver.",L"Cast to TV",MB_OK|MB_ICONINFORMATION);}
                else if(mi==3){if(g_hasMedia){if(libvlc_media_player_get_state(g_player)==libvlc_Playing)libvlc_media_player_set_pause(g_player,1);else libvlc_media_player_play(g_player);}}
                else if(mi==4)ToggleFullscreen(hwnd);
                else if(mi==5)LoadPlaylist();
                else if(mi==6)SavePlaylist();
                else if(mi==7)MessageBoxW(hwnd,
                    L"ABM AV Player - Quick Help\n\n"
                    L"Open media: Add audio or video files to the playlist.\n"
                    L"Double-click a playlist item to play it.\n"
                    L"Right-click the playlist for remove, reorder and clear options.\n"
                    L"Load / Save playlist: Use M3U playlist files.\n"
                    L"Seek: Drag the timeline. Volume: Drag the volume slider.\n"
                    L"Drag files into the player to add them. Space: play/pause, arrows: seek 10 seconds. +/-: speed. N/P: next/previous.\n"
                    L"Cast to TV: Discover receivers, then choose your TV in Playback Output.\n\n"
                    L"Keyboard shortcuts:\nF11 - Toggle fullscreen\nEsc - Exit fullscreen or close the menu\n"
                    L"Double-click the video - Toggle fullscreen\n"\
                    L"Space - Play/Pause | Left/Right - Seek 10 seconds\n"\
                    L"+/- - Playback speed | N/P - Next/Previous",
                    L"ABM AV Player Help",MB_OK|MB_ICONINFORMATION);
                else if(mi==8)MessageBoxW(hwnd,
                    L"ABM AV Player\nVersion 0.4\n\n"
                    L"A Windows desktop media player powered by LibVLC, Direct2D and DirectWrite.\n\n"
                    L"Product Owner: Abhisek Basu Mullick\n"
                    L"Copyright (c) 2026 Abhisek Basu Mullick. All rights reserved.",
                    L"About ABM AV Player",MB_OK|MB_ICONINFORMATION);
                else if(mi==9)PostMessageW(hwnd,WM_CLOSE,0,0);
                else if(mi==10){g_shuffle=!g_shuffle;}
                else if(mi==11){g_repeat=!g_repeat;}
                else if(mi==12){POINT rp={x,y};ShowRecentMenu(hwnd,rp.x,rp.y);}
                InvalidateRect(hwnd,NULL,FALSE);return 0;
            }
        }
        SetCapture(hwnd);
        if(Inside(R_OPEN,x,y)){AddFiles();ReleaseCapture();return 0;}
        if(Inside(R_PLAY,x,y)){if(g_hasMedia){if(libvlc_media_player_get_state(g_player)==libvlc_Playing)libvlc_media_player_set_pause(g_player,1);else libvlc_media_player_play(g_player);}else if(g_playlistCount)PlayIndex(g_selected<0?0:g_selected);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(Inside(R_STOP,x,y)){libvlc_media_player_stop(g_player);g_hasMedia=FALSE;ResetArtwork();InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(Inside(R_NEXT,x,y)){if(g_playlistCount)PlayIndex(GetNextPlaylistIndex());ReleaseCapture();return 0;}
        if(Inside(R_PREV,x,y)){if(g_playlistCount)PlayIndex(g_current>0?g_current-1:0);ReleaseCapture();return 0;}
        if(g_drawerOpen&&g_drawerSettingsOpen&&Inside(R_SCAN,x,y)){StartDiscovery();ReleaseCapture();return 0;}
        if(g_drawerOpen&&g_drawerSettingsOpen&&Inside(R_SHUFFLE,x,y)){g_shuffle=!g_shuffle;SavePersistentState();InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&g_drawerSettingsOpen&&Inside(R_REPEAT,x,y)){g_repeat=!g_repeat;SavePersistentState();InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&g_drawerSettingsOpen&&Inside(R_SPEED,x,y)){
            if(g_playbackRate<0.99f)g_playbackRate=1.0f;
            else if(g_playbackRate<1.24f)g_playbackRate=1.25f;
            else if(g_playbackRate<1.49f)g_playbackRate=1.5f;
            else if(g_playbackRate<1.99f)g_playbackRate=2.0f;
            else g_playbackRate=0.75f;
            libvlc_media_player_set_rate(g_player,g_playbackRate);
            SavePersistentState();InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;
        }
        if(g_drawerOpen&&g_drawerSettingsOpen&&Inside(R_RECENT,x,y)){ShowRecentMenu(hwnd,x,y);ReleaseCapture();return 0;}
        if(g_drawerOpen&&g_drawerSettingsOpen&&Inside(R_RECEIVER,x,y)){g_receiverPopup=!g_receiverPopup;InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&g_drawerSettingsOpen&&g_receiverPopup && Inside(R_RECEIVER_LIST,x,y)) {
            int item=(y-R_RECEIVER_LIST.top-3)/32;
            int count=(int)SendMessageW(g_renderers,CB_GETCOUNT,0,0);
            if(item>=0&&item<count) {
                SendMessageW(g_renderers,CB_SETCURSEL,item,0);
                SelectRenderer();
            }
            g_receiverPopup=FALSE;InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;
        }
        if(g_receiverPopup){g_receiverPopup=FALSE;InvalidateRect(hwnd,NULL,FALSE);}
        if(y>=R_SEEK.top-12&&y<=R_SEEK.bottom+14){g_dragSeek=1;SeekAt(x);ReleaseCapture();return 0;}
        if(y>=R_VOLUME.top-12&&y<=R_VOLUME.bottom+12){g_dragVolume=1;int v=(x-R_VOLUME.left)*100/(R_VOLUME.right-R_VOLUME.left);if(v<0)v=0;if(v>100)v=100;g_volume=v;libvlc_audio_set_volume(g_player,v);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        for(int ri=0;ri<g_visibleRowCount;ri++)if(Inside(R_ROW[ri],x,y)){g_selected=g_visibleRows[ri];InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        ReleaseCapture();return 0;
    }
    case WM_KEYDOWN: {
        BOOL ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        BOOL shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(ctrl && !shift && wp=='O'){AddFiles();return 0;}
        if(ctrl && !shift && wp=='D'){StartDiscovery();return 0;}
        if(ctrl && shift && wp=='C'){StartDiscovery();MessageBoxW(hwnd,L"Choose your TV in the Playback Output selector. If media is already playing, the player will restart it on the selected receiver.",L"Cast to TV",MB_OK|MB_ICONINFORMATION);return 0;}
        if(ctrl && !shift && wp=='L'){LoadPlaylist();return 0;}
        if(ctrl && !shift && wp=='S'){SavePlaylist();return 0;}
        if(ctrl && !shift && wp=='I'){
            MessageBoxW(hwnd,L"ABM AV Player\\nVersion 0.4\\n\\nA Windows desktop media player powered by LibVLC, Direct2D and DirectWrite.\\n\\nProduct Owner: Abhisek Basu Mullick\\nCopyright (c) 2026 Abhisek Basu Mullick. All rights reserved.",L"About ABM AV Player",MB_OK|MB_ICONINFORMATION);
            return 0;
        }
        if(ctrl && shift && wp=='H'){g_shuffle=!g_shuffle;SavePersistentState();InvalidateRect(hwnd,NULL,FALSE);return 0;}
        if(ctrl && !shift && wp=='R'){g_repeat=!g_repeat;SavePersistentState();InvalidateRect(hwnd,NULL,FALSE);return 0;}
        if(ctrl && !shift && wp=='J'){POINT p;GetCursorPos(&p);ShowRecentMenu(hwnd,p.x,p.y);return 0;}
        if(wp==VK_F1){
            MessageBoxW(hwnd,L"ABM AV Player - Quick Help\\n\\nCtrl+O Open media | Ctrl+D Find network devices | Ctrl+Shift+C Cast to TV\\nSpace Play/Pause | F11 Fullscreen | Esc Exit fullscreen\\nCtrl+L Load playlist | Ctrl+S Save playlist | Ctrl+J Recent media\\nCtrl+Shift+H Shuffle | Ctrl+R Repeat | Ctrl+I About | Alt+F4 Exit\\nLeft/Right Seek 10 seconds | +/- Playback speed | N/P Previous/Next",L"ABM AV Player Help",MB_OK|MB_ICONINFORMATION);
            return 0;
        }
        if(g_searchFocused&&wp==VK_ESCAPE){g_searchFocused=FALSE;InvalidateRect(hwnd,&R_SEARCH,FALSE);return 0;}
        if(g_searchFocused&&wp==VK_BACK){size_t n=wcslen(g_searchText);if(n)g_searchText[n-1]=0;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);return 0;}
        if(g_searchFocused&&wp==VK_RETURN){g_searchFocused=FALSE;InvalidateRect(hwnd,&R_SEARCH,FALSE);return 0;}
        if(g_searchFocused&&(wp==VK_SPACE||wp==VK_LEFT||wp==VK_RIGHT))return 0;
        if(wp==VK_F11){ToggleFullscreen(hwnd);return 0;}
        if(wp==VK_SPACE&&g_hasMedia){if(libvlc_media_player_get_state(g_player)==libvlc_Playing)libvlc_media_player_set_pause(g_player,1);else libvlc_media_player_play(g_player);return 0;}
        if(wp==VK_RIGHT&&g_hasMedia){int64_t t=libvlc_media_player_get_time(g_player);libvlc_media_player_set_time(g_player,t+10000);return 0;}
        if(wp==VK_LEFT&&g_hasMedia){int64_t t=libvlc_media_player_get_time(g_player);libvlc_media_player_set_time(g_player,t>10000?t-10000:0);return 0;}
        if((wp==VK_OEM_PLUS||wp==VK_ADD)&&g_hasMedia){if(g_playbackRate<2.0f)g_playbackRate+=0.25f;libvlc_media_player_set_rate(g_player,g_playbackRate);InvalidateRect(hwnd,NULL,FALSE);return 0;}
        if((wp==VK_OEM_MINUS||wp==VK_SUBTRACT)&&g_hasMedia){if(g_playbackRate>0.5f)g_playbackRate-=0.25f;libvlc_media_player_set_rate(g_player,g_playbackRate);InvalidateRect(hwnd,NULL,FALSE);return 0;}
        if(wp==VK_HOME&&g_hasMedia){libvlc_media_player_set_time(g_player,0);return 0;}
        if(wp=='N'&&g_playlistCount){PlayIndex(GetNextPlaylistIndex());return 0;}
        if(wp=='P'&&g_playlistCount){PlayIndex(g_current>0?g_current-1:g_playlistCount-1);return 0;}
        if(wp==VK_ESCAPE&&g_fullscreen){ToggleFullscreen(hwnd);return 0;}
        if(wp==VK_ESCAPE&&g_menuOpen){g_menuOpen=FALSE;SyncRendererComboVisibility();InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
        break;
    }
    case WM_CHAR:
        if(g_searchFocused){wchar_t ch=(wchar_t)wp;if(ch>=32&&ch!=127){size_t n=wcslen(g_searchText);if(n<127){g_searchText[n]=ch;g_searchText[n+1]=0;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);}}return 0;}
        break;
    case WM_RBUTTONUP: {
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp),index=-1;
        for(int ri=0;ri<g_visibleRowCount;ri++)if(Inside(R_ROW[ri],x,y)){index=g_visibleRows[ri];break;}
        if(Inside(R_SIDEBAR,x,y)&&y>=R_SIDEBAR.top+140){ShowPlaylistContextMenu(hwnd,x,y,index);return 0;}
        if(Inside(R_VIDEO,x,y)){ShowPlaybackContextMenu(hwnd,x,y);return 0;}
        break;
    }
    case WM_LBUTTONDBLCLK: { int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp); if(!g_fullscreen&&Inside(R_VIDEO,x,y)){ToggleFullscreen(hwnd);return 0;} for(int ri=0;ri<g_visibleRowCount;ri++)if(Inside(R_ROW[ri],x,y)){PlayIndex(g_visibleRows[ri]);return 0;} return 0; }
    case WM_MOUSEMOVE: {
        int mx=GET_X_LPARAM(lp),my=GET_Y_LPARAM(lp),hit=HIT_NONE;
        int playlistHot=-1;
        if(g_drawerProgress>=90&&Inside(R_LIST,mx,my)){for(int ri=0;ri<g_visibleRowCount;ri++)if(Inside(R_ROW[ri],mx,my)){playlistHot=g_visibleRows[ri];break;}}
        if(playlistHot!=g_hoverPlaylistIndex){g_hoverPlaylistIndex=playlistHot;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);}
        int menuHot=-1;
        if(g_menuOpen&&g_menuProgress>=35){for(int mi=0;mi<13;mi++)if(Inside(R_MENU_ITEMS[mi],mx,my)){menuHot=mi;break;}}
        if(menuHot!=g_hoverMenuItem){g_hoverMenuItem=menuHot;InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);}
        if (g_fullscreen) {
            g_fullscreenLastInputTick=GetTickCount();
            SetCursor(LoadCursorW(NULL,IDC_ARROW));
            if (!g_fullscreenControlsVisible) {
                g_fullscreenControlsVisible=TRUE;
                if(g_fullscreenOverlay)ShowWindow(g_fullscreenOverlay,SW_SHOWNOACTIVATE);
                if(g_fullscreenOverlay)InvalidateRect(g_fullscreenOverlay,NULL,FALSE);
                RedrawWindow(hwnd,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
            }
        }
        if(Inside(R_OPEN,mx,my))hit=HIT_OPEN;else if(Inside(R_SCAN,mx,my))hit=HIT_SCAN;
        else if(Inside(R_PLAY,mx,my))hit=HIT_PLAY;else if(Inside(R_STOP,mx,my))hit=HIT_STOP;
        else if(Inside(R_PREV,mx,my))hit=HIT_PREV;else if(Inside(R_NEXT,mx,my))hit=HIT_NEXT;
        if(hit!=g_hoverButton){
            int previous=g_hoverButton;
            g_hoverButton=hit;
            InvalidateButton(previous);
            InvalidateButton(g_hoverButton);
        }
        if(g_dragSeek)SeekAt(mx);
        if(g_dragVolume){
            int v=(mx-R_VOLUME.left)*100/(R_VOLUME.right-R_VOLUME.left);
            if(v<0)v=0;if(v>100)v=100;
            if(v!=g_volume) {
                g_volume=v;
                libvlc_audio_set_volume(g_player,v);
                if(g_fullscreen && g_fullscreenOverlay)
                    InvalidateRect(g_fullscreenOverlay,NULL,FALSE);
                else
                    InvalidateRect(hwnd,&R_VOLUME,FALSE);
            }
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (g_fullscreen && !g_fullscreenControlsVisible) {
            SetCursor(NULL);
            return TRUE;
        }
        if (g_fullscreen && g_fullscreenControlsVisible) {
            SetCursor(LoadCursorW(NULL,IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONUP:g_dragSeek=0;g_dragVolume=0;ReleaseCapture();return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);

        RECT r;
        GetClientRect(hwnd, &r);

        /* Paint only the invalidated area. A full-target Direct2D Clear on every
           menu animation frame was repainting the whole surface and causing flicker. */
        if (ABM_D2D_BeginClip(&g_ui, C_BG, &ps.rcPaint)) {
            DrawUI(NULL, r);

            if (!ABM_D2D_End(&g_ui)) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
        } else {
            /* The render target could not be created; retry on a subsequent paint. */
            InvalidateRect(hwnd, NULL, FALSE);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:return 1;
    case WM_CTLCOLORSTATIC:SetBkColor((HDC)wp,C_BG);SetTextColor((HDC)wp,C_TEXT);return (LRESULT)g_videoBrush;
    case WM_DESTROY:SavePersistentState();DragAcceptFiles(hwnd,FALSE);KillTimer(hwnd,TIMER_ID);ABM_AudioVisualizer_Stop();ResetArtwork();ABM_D2D_Destroy(&g_ui);g_main=NULL;StopDiscovery();libvlc_media_player_stop(g_player);libvlc_media_player_release(g_player);libvlc_release(g_vlc);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE prev,PWSTR cmd,int show) {
    (void)prev;(void)cmd;HRESULT comInit=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);srand((unsigned int)GetTickCount());INITCOMMONCONTROLSEX icc={sizeof(icc),ICC_STANDARD_CLASSES};InitCommonControlsEx(&icc);
    InitializeCriticalSection(&g_rendererLock);
    g_videoBrush=CreateSolidBrush(RGB(0,0,0));g_comboBrush=CreateSolidBrush(C_PANEL2);
    g_font=CreateFontW(18,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    g_smallFont=CreateFontW(15,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    g_headingFont=CreateFontW(25,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    /* Resolve the bundled LibVLC runtime relative to this executable. This lets
       an installed copy find its own plugins/data without a system VLC install. */
    wchar_t modulePath[PATH_CAP]={0}, appDir[PATH_CAP]={0}, pluginDir[PATH_CAP]={0};
    DWORD moduleLen=GetModuleFileNameW(NULL,modulePath,PATH_CAP);
    if(moduleLen>0 && moduleLen<PATH_CAP) {
        wchar_t *lastSlash=wcsrchr(modulePath,L'\\');
        if(lastSlash) {
            *lastSlash=0;
            wcscpy_s(appDir,PATH_CAP,modulePath);

            /* Prefer a self-contained runtime beside the executable. For a
               development build in build\\, fall back to the VLC installation
               supplied by run.bat instead of overriding it with build\\plugins. */
            swprintf_s(pluginDir,PATH_CAP,L"%s\\plugins",appDir);
            DWORD pluginAttrs=GetFileAttributesW(pluginDir);
            if(pluginAttrs!=INVALID_FILE_ATTRIBUTES && (pluginAttrs&FILE_ATTRIBUTE_DIRECTORY)) {
                _wputenv_s(L"VLC_PLUGIN_PATH",pluginDir);
            } else {
                DWORD envLen=GetEnvironmentVariableW(L"VLC_PLUGIN_PATH",pluginDir,PATH_CAP);
                if(envLen==0 || envLen>=PATH_CAP) {
                    wchar_t vlcDir[PATH_CAP]={0};
                    DWORD vlcLen=GetEnvironmentVariableW(L"VLC_DIR",vlcDir,PATH_CAP);
                    if(vlcLen>0 && vlcLen<PATH_CAP) {
                        swprintf_s(pluginDir,PATH_CAP,L"%s\\plugins",vlcDir);
                        pluginAttrs=GetFileAttributesW(pluginDir);
                        if(pluginAttrs!=INVALID_FILE_ATTRIBUTES && (pluginAttrs&FILE_ATTRIBUTE_DIRECTORY))
                            _wputenv_s(L"VLC_PLUGIN_PATH",pluginDir);
                        else
                            pluginDir[0]=0;
                    } else {
                        pluginDir[0]=0;
                    }
                }
            }

            /* Keep an explicit data path only when the bundled app has its
               own VLC data directory; otherwise retain the launcher's VLC path. */
            wchar_t bundledLua[PATH_CAP]={0};
            swprintf_s(bundledLua,PATH_CAP,L"%s\\lua",appDir);
            DWORD luaAttrs=GetFileAttributesW(bundledLua);
            if(luaAttrs!=INVALID_FILE_ATTRIBUTES && (luaAttrs&FILE_ATTRIBUTE_DIRECTORY)) {
                _wputenv_s(L"VLC_DATA_PATH",appDir);
            } else {
                wchar_t dataDir[PATH_CAP]={0};
                DWORD dataLen=GetEnvironmentVariableW(L"VLC_DATA_PATH",dataDir,PATH_CAP);
                if(dataLen==0 || dataLen>=PATH_CAP) {
                    wchar_t vlcDir[PATH_CAP]={0};
                    DWORD vlcLen=GetEnvironmentVariableW(L"VLC_DIR",vlcDir,PATH_CAP);
                    if(vlcLen>0 && vlcLen<PATH_CAP)
                        _wputenv_s(L"VLC_DATA_PATH",vlcDir);
                }
            }
        }
    }
    /* Pass the bundled plugin directory explicitly as well as through the
       environment. This is important for installed copies that must not depend
       on a separate system-wide VLC installation or its registry settings. */
    char pluginArg[PATH_CAP*4+32]={0};
    char pluginUtf8[PATH_CAP*4]={0};
    if(pluginDir[0] && WideCharToMultiByte(CP_UTF8,0,pluginDir,-1,pluginUtf8,(int)sizeof(pluginUtf8),NULL,NULL)>0)
        snprintf(pluginArg,sizeof(pluginArg),"--plugin-path=%s",pluginUtf8);

    /* Hardware decoding defaults to LibVLC automatic selection. Override with
       ABM_VLC_HW=none for software-only troubleshooting or d3d11va to request D3D11VA. */
    const char *args[6]={"--no-video-title-show","--quiet","--no-plugins-cache",NULL,NULL,NULL};
    int argCount=3;
    if(pluginArg[0])args[argCount++]=pluginArg;
    wchar_t hwMode[32]=L"";
    GetEnvironmentVariableW(L"ABM_VLC_HW",hwMode,(DWORD)_countof(hwMode));
    if(!_wcsicmp(hwMode,L"none"))args[argCount++]="--avcodec-hw=none";
    else if(!_wcsicmp(hwMode,L"d3d11va"))args[argCount++]="--avcodec-hw=d3d11va";
    g_vlc=libvlc_new(argCount,args);
    if(!g_vlc){MessageBoxW(NULL,L"Could not initialize LibVLC. Check the VLC installation and plugins.",APP_NAME,MB_ICONERROR);return 1;}
    g_player=libvlc_media_player_new(g_vlc);if(!g_player){libvlc_release(g_vlc);return 1;}
    WNDCLASSW wc={0};wc.lpfnWndProc=WndProc;wc.style=CS_DBLCLKS;wc.hInstance=inst;wc.lpszClassName=L"ABMAVPlayerWindow";wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=g_videoBrush;wc.hIcon=LoadIconW(inst,MAKEINTRESOURCEW(IDI_APP_ICON));
    if(!RegisterClassW(&wc))return 1;
    WNDCLASSW owc={0};owc.lpfnWndProc=FullscreenOverlayProc;owc.hInstance=inst;owc.lpszClassName=L"ABMFullscreenOverlay";owc.hCursor=LoadCursorW(NULL,IDC_ARROW);owc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    if(!RegisterClassW(&owc))return 1;
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"ABM AV Player",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1440,900,NULL,NULL,inst,NULL);
    if(!hwnd)return 1;
    if(wc.hIcon)SendMessageW(hwnd,WM_SETICON,ICON_SMALL,(LPARAM)wc.hIcon);
    g_fullscreenOverlay=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
        L"ABMFullscreenOverlay",L"",WS_POPUP,0,0,1,1,hwnd,NULL,inst,NULL);
    /* Per-pixel alpha is supplied by UpdateLayeredWindow; no colour-key
       transparency, which would leave opaque rectangular control panels. */
    /* Windows 11 native dark chrome: rounded corners, deep navy caption and
       Mica backdrop where supported. Every attribute is best-effort so older
       Windows builds retain the standard decorated window without regressions. */
    BOOL darkTitle=TRUE;
    DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&darkTitle,sizeof(darkTitle));
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWMWA_BORDER_COLOR 34
#define DWMWA_CAPTION_COLOR 35
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
    { int corners=2; COLORREF border=RGB(46,70,104),caption=RGB(10,15,25); int backdrop=2;
      DwmSetWindowAttribute(hwnd,(DWORD)DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
      DwmSetWindowAttribute(hwnd,(DWORD)DWMWA_BORDER_COLOR,&border,sizeof(border));
      DwmSetWindowAttribute(hwnd,(DWORD)DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
      DwmSetWindowAttribute(hwnd,(DWORD)DWMWA_SYSTEMBACKDROP_TYPE,&backdrop,sizeof(backdrop)); }
    ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG m;while(GetMessageW(&m,NULL,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
    if(g_headingFont)DeleteObject(g_headingFont);if(g_smallFont)DeleteObject(g_smallFont);if(g_font)DeleteObject(g_font);if(g_videoBrush)DeleteObject(g_videoBrush);if(g_comboBrush)DeleteObject(g_comboBrush);DeleteCriticalSection(&g_rendererLock);
    if(SUCCEEDED(comInit))CoUninitialize();
    return (int)m.wParam;
}

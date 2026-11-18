#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <winsock2.h>
#include <windows.h>
#include <psapi.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <richedit.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <vlc/vlc.h>
#include <vlc/libvlc_renderer_discoverer.h>
#include <vlc/libvlc_events.h>
#include "ui_d2d.h"
#include "ui_theme.h"
#include "projectm_visualizer.h"
#include "tmdb_metadata.h"
#include "tmdb_home_theatre.h"
#include "streaming/streaming_host.h"
#include "updater.h"
#include "licensing.h"
#include "resource.h"
#include "version.h"
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <wchar.h>
#include <wctype.h>
#include <string.h>
#include <strsafe.h>
#include <stdlib.h>
#include <limits.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <KnownFolders.h>
#include <urlmon.h>
#include <winhttp.h>
#include <wincodec.h>
#include <ws2tcpip.h>
#include <dwmapi.h>

static void ApplyAdvancedAspectRatio(void);
static void VlcCodecVerificationReset(void);
static void AddAdvancedCachingOptions(libvlc_media_t *media);
static void AddAdvancedPerformanceOptions(libvlc_media_t *media);
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define APP_NAME L"ABM AV Player"
#define APP_VERSION ABM_VERSION_W
#define OWNER_TEXT L"Product Owner: Abhisek Basu Mullick  |  © 2026 Abhisek Basu Mullick. All rights reserved."
#define ID_RENDERERS 1010
#define ID_SETTINGS_CACHE_PATH 6201
#define ID_SETTINGS_BROWSE 6202
#define ID_SETTINGS_OPEN_CACHE 6203
#define ID_SETTINGS_CACHE_QUOTA 6204
#define ID_SETTINGS_DOWN_LIMIT 6205
#define ID_SETTINGS_UP_LIMIT 6206
#define ID_SETTINGS_VOLUME 6207
#define ID_SETTINGS_RATE 6208
#define ID_SETTINGS_SHUFFLE 6209
#define ID_SETTINGS_REPEAT 6210
#define ID_SETTINGS_REPLAY_GAIN 6211
#define ID_SETTINGS_COMPRESSOR 6212
#define ID_SETTINGS_APPLY 6213
#define ID_SETTINGS_CANCEL 6214
#define WM_RENDERERS_CHANGED (WM_APP + 10)
#define WM_ARTWORK_READY (WM_APP + 11)
#define WM_ARTWORK_DOWNLOAD_READY (WM_APP + 12)
#define WM_TORRENT_INIT_DONE (WM_APP + 13)
#define WM_TORRENT_STREAM_READY (WM_APP + 14)
#define WM_TORRENT_PLAYER_EVENT (WM_APP + 15)
#define WM_TORRENT_TV_READY (WM_APP + 16)
#define WM_VIDEO_CONTEXT_REQUEST (WM_APP + 17)
#define TIMER_ID 1
#define VISUALIZER_TIMER_ID 2 /* dedicated main-window 60 Hz visualizer timer */
#define MAX_RENDERERS 32
#define MAX_DISCOVERERS 16
#define MAX_PLAYLIST 256
#define PATH_CAP 1024

static libvlc_instance_t *g_vlc;
static libvlc_media_player_t *g_player;
/* Dedicated browser-to-TV player; never stop or replace the local player. */
static libvlc_media_player_t *g_streamCastPlayer;
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
/* Streaming Centre has its own renderer-discovery session and item registry.
   It never shares the local player's discovery arrays or picker state. */
static libvlc_renderer_discoverer_t *g_streamDiscoverers[MAX_DISCOVERERS];
static size_t g_streamDiscovererCount;
static libvlc_renderer_item_t *g_streamRendererItems[MAX_RENDERERS];
static size_t g_streamRendererCount;
/* Streaming Centre owns an independent selection; local-player routing is untouched. */
static libvlc_renderer_item_t *g_streamSelectedRenderer;
static BOOL g_rendererApplied = FALSE;

/* Duration metadata is resolved off the UI thread. The paint path performs
   only a bounded cache lookup and a non-blocking request enqueue. */
#define DURATION_CACHE_SIZE 1024
typedef struct { wchar_t path[PATH_CAP]; int64_t milliseconds; BOOL occupied; BOOL pending; } ABM_DurationEntry;
static ABM_DurationEntry g_durationCache[DURATION_CACHE_SIZE];
static SRWLOCK g_durationLock=SRWLOCK_INIT;
static wchar_t g_durationRequests[MAX_PLAYLIST][PATH_CAP];
static int g_durationRead=0,g_durationWrite=0,g_durationCount=0;
static HANDLE g_durationWake=NULL,g_durationThread=NULL;
static volatile LONG g_durationStop=0;
static unsigned __int64 DurationPathHash(const wchar_t *path) {
    unsigned __int64 h=1469598103934665603ULL;
    for(;path&&*path;path++){wchar_t c=(wchar_t)towlower(*path);h^=(unsigned __int64)c;h*=1099511628211ULL;}
    return h;
}
static int DurationCacheSlot(const wchar_t *path,BOOL create) {
    unsigned int slot=(unsigned int)(DurationPathHash(path)%DURATION_CACHE_SIZE);
    for(unsigned int n=0;n<DURATION_CACHE_SIZE;n++,slot=(slot+1)%DURATION_CACHE_SIZE) {
        ABM_DurationEntry *e=&g_durationCache[slot];
        if(!e->occupied) {
            if(!create)return -1;
            e->occupied=TRUE;e->milliseconds=-1;e->pending=FALSE;
            wcsncpy_s(e->path,PATH_CAP,path,_TRUNCATE);return (int)slot;
        }
        if(!_wcsicmp(e->path,path))return (int)slot;
    }
    return -1;
}
static void DurationRequest(const wchar_t *path) {
    if(!path||!path[0]||!g_durationWake||InterlockedCompareExchange(&g_durationStop,0,0))return;
    AcquireSRWLockExclusive(&g_durationLock);
    int slot=DurationCacheSlot(path,TRUE);
    if(slot>=0&&!g_durationCache[slot].pending&&g_durationCache[slot].milliseconds<0&&g_durationCount<MAX_PLAYLIST) {
        wcsncpy_s(g_durationRequests[g_durationWrite],PATH_CAP,path,_TRUNCATE);
        g_durationWrite=(g_durationWrite+1)%MAX_PLAYLIST;g_durationCount++;
        g_durationCache[slot].pending=TRUE;
        SetEvent(g_durationWake);
    }
    ReleaseSRWLockExclusive(&g_durationLock);
}
static int64_t DurationLookup(const wchar_t *path) {
    int64_t value=-1;
    AcquireSRWLockShared(&g_durationLock);
    int slot=DurationCacheSlot(path,FALSE);
    if(slot>=0)value=g_durationCache[slot].milliseconds;
    ReleaseSRWLockShared(&g_durationLock);
    return value;
}
static DWORD WINAPI DurationMetadataThread(LPVOID unused) {
    (void)unused;
    for(;;) {
        WaitForSingleObject(g_durationWake,INFINITE);
        if(InterlockedCompareExchange(&g_durationStop,0,0))break;
        wchar_t path[PATH_CAP]={0};
        AcquireSRWLockExclusive(&g_durationLock);
        if(g_durationCount>0) {
            wcsncpy_s(path,PATH_CAP,g_durationRequests[g_durationRead],_TRUNCATE);
            g_durationRead=(g_durationRead+1)%MAX_PLAYLIST;g_durationCount--;
        }
        BOOL more=g_durationCount>0;
        ReleaseSRWLockExclusive(&g_durationLock);
        if(!path[0])continue;
        int64_t duration=-1;
        if(g_vlc&&!InterlockedCompareExchange(&g_durationStop,0,0)) {
            char utf8[PATH_CAP*4];
            if(WideCharToMultiByte(CP_UTF8,0,path,-1,utf8,sizeof(utf8),NULL,NULL)>0) {
                libvlc_media_t *media=libvlc_media_new_path(g_vlc,utf8);
                if(media) {
                    /* Synchronous parse is safe here: this worker is never the UI thread. */
                    libvlc_media_parse(media);
                    duration=libvlc_media_get_duration(media);
                    libvlc_media_release(media);
                }
            }
        }
        AcquireSRWLockExclusive(&g_durationLock);
        int slot=DurationCacheSlot(path,TRUE);
        if(slot>=0){g_durationCache[slot].milliseconds=duration>0?duration:0;g_durationCache[slot].pending=FALSE;}
        ReleaseSRWLockExclusive(&g_durationLock);
        if(g_main)PostMessageW(g_main,WM_APP+0x351,0,0);
        if(more)SetEvent(g_durationWake);
    }
    return 0;
}
static BOOL DurationMetadataStart(void) {
    if(g_durationThread)return TRUE;
    g_durationWake=CreateEventW(NULL,FALSE,FALSE,NULL);
    if(!g_durationWake)return FALSE;
    InterlockedExchange(&g_durationStop,0);
    g_durationThread=CreateThread(NULL,0,DurationMetadataThread,NULL,0,NULL);
    if(!g_durationThread){CloseHandle(g_durationWake);g_durationWake=NULL;return FALSE;}
    SetThreadPriority(g_durationThread,THREAD_PRIORITY_BELOW_NORMAL);
    return TRUE;
}
static void DurationMetadataStop(void) {
    if(!g_durationThread)return;
    InterlockedExchange(&g_durationStop,1);SetEvent(g_durationWake);
    WaitForSingleObject(g_durationThread,INFINITE);
    CloseHandle(g_durationThread);g_durationThread=NULL;
    CloseHandle(g_durationWake);g_durationWake=NULL;
}
static void FormatMediaDuration(int64_t milliseconds,wchar_t *out,size_t capacity) {
    if(!out||capacity==0)return;
    if(milliseconds<=0){StringCchCopyW(out,capacity,L"—");return;}
    unsigned long long seconds=(unsigned long long)(milliseconds/1000);
    if(seconds>=3600)StringCchPrintfW(out,capacity,L"%llu:%02llu:%02llu",seconds/3600,(seconds/60)%60,seconds%60);
    else StringCchPrintfW(out,capacity,L"%02llu:%02llu",seconds/60,seconds%60);
}

static wchar_t g_playlist[MAX_PLAYLIST][PATH_CAP];
static int g_playlistCount, g_current = -1, g_selected = -1;
#define MAX_FAVORITES MAX_PLAYLIST
static wchar_t g_favorites[MAX_FAVORITES][PATH_CAP];
static int g_favoriteCount=0;
static BOOL g_showFavorites=FALSE;
static BOOL g_showContinueWatching=FALSE;
static int g_volume = 70, g_dragSeek = 0, g_dragSeekX = 0, g_dragVolume = 0, g_hover = 0;
static BOOL g_shuffle = FALSE;
/* 0=Off, 1=Repeat current, 2=Repeat all. Preserves legacy Repeat=1 settings. */
static int g_repeat = 0;
static float g_playbackRate = 1.0f;
static BOOL g_endHandled = FALSE;
#define MAX_RECENT 12
static wchar_t g_recent[MAX_RECENT][PATH_CAP];
static int g_recentCount=0;
static HWND g_recentPopupWindow=NULL;
static int g_recentPopupHover=-1;
static BOOL g_recentOpenPending=FALSE;
static wchar_t g_dataDir[PATH_CAP];
static wchar_t g_torrentCacheBaseDir[PATH_CAP];
static wchar_t g_torrentDownloadDir[PATH_CAP];
static wchar_t g_torrentActiveCacheBaseDir[PATH_CAP];
static wchar_t g_torrentActiveDownloadDir[PATH_CAP];
static uint64_t g_torrentCacheQuotaGiB=20;
static int g_torrentDownloadLimitKiB=0,g_torrentUploadLimitKiB=0;

static BOOL g_libraryReady=FALSE;
static unsigned int g_visualPulse=0;
static unsigned int g_visualizerFrame=0;
static HHOOK g_videoMouseHook=NULL;
static BOOL g_visualizerTimerRunning=FALSE;
static BOOL g_receiverLost=FALSE;
static DWORD g_discoveryStartedTick=0;
static BOOL g_discoveryTimedOut=FALSE;
static wchar_t g_statusText[256]=L"";
static BOOL g_pendingResume=FALSE;
static BOOL g_advancedConfirmClearPlaylist=TRUE;
static BOOL g_pendingResumePause=FALSE;
static int64_t g_pendingResumeMs=0;
static int64_t g_forcedResumeMs=0;
static BOOL g_useForcedResume=FALSE;
static BOOL g_startFromBeginningNextPlay=FALSE;
static unsigned int g_pendingResumeTicks=0;
#define MAX_RESUME_ENTRIES MAX_PLAYLIST
typedef struct { wchar_t path[PATH_CAP]; int64_t positionMs; int64_t durationMs; } ABMResumeEntry;
static ABMResumeEntry g_resumeEntries[MAX_RESUME_ENTRIES];
static int g_resumeCount=0;
static HANDLE g_resumeWriteThread=NULL;
static DWORD g_lastResumeSaveTick=0;
static BOOL g_missingMedia[MAX_PLAYLIST];
static BOOL g_hasResume[MAX_PLAYLIST];
static BOOL g_visualizerActive=FALSE;
static BOOL g_visualizerTried=FALSE;
static BOOL g_projectmTried=FALSE;
static float g_visualizerBarPeaks[64];
static int g_visualizerMode=3; /* 0-3 Direct2D styles, 4 isolated ProjectM MilkDrop */
static BOOL g_replayGain=TRUE;
static BOOL g_compressor=FALSE;
static int g_eqProfile=0;
static int g_eqUiPreset=-1;
static libvlc_media_t *g_artMedia=NULL;
static volatile LONG g_artworkGeneration=0;
static LONG g_artworkCallbackGeneration=0;
static BOOL g_artworkLoaded=FALSE;
static int g_artworkFade=100;
static wchar_t g_trackTitle[256]=L"";
static wchar_t g_trackArtist[256]=L"";
static wchar_t g_trackAlbum[256]=L"";
static wchar_t g_trackGenre[256]=L"";
static int g_playlistScroll = 0, g_hoverButton = 0, g_hoverLevel = 0;
static int g_hoverChromeControl=0;
static BOOL g_dragPlaylistScroll = FALSE;
static int g_playlistScrollGrabY = 0, g_playlistScrollStart = 0;
static BOOL g_queueDragPending=FALSE,g_queueDragging=FALSE;
static int g_queueDragSource=-1,g_queueDropBoundary=-1,g_queueDragStartX=0,g_queueDragStartY=0;
static RECT R_PLAYLIST_SCROLL, R_PLAYLIST_THUMB;
static DWORD g_lastClickTick = 0;
static int g_duration = 0;
static unsigned int g_videoWidth = 0, g_videoHeight = 0;
static RECT g_videoBounds = {0,0,0,0};
static BOOL g_videoBoundsValid = FALSE;
static unsigned int g_videoGeometryPoll = 0;
static int64_t g_lastUiTime = -1;
static int64_t g_overlayLength = 0;
static BOOL g_overlayPlaying = FALSE;
static RECT R_VIDEO, R_SIDEBAR, R_LIST, R_SEEK, R_PLAY, R_PREV, R_NEXT, R_STOP, R_OPEN, R_SCAN, R_VOLUME, R_RECEIVER, R_RECEIVER_LIST, R_AUDIO_BUTTON, R_AUDIO_PANEL, R_AUDIO_ITEMS[12];
static RECT R_ROW[MAX_PLAYLIST],R_FAVORITE[MAX_PLAYLIST];
static RECT R_SHUFFLE, R_REPEAT, R_SPEED, R_RECENT, R_CLEAR_PLAYLIST;
static RECT R_HOME_OPEN, R_HOME_STREAM, R_HOME_TORRENT, R_HOME_RECENT;
static int g_homeCardMotion[4]={0,0,0,0};
static RECT R_SEARCH, R_TAB_QUEUE, R_TAB_LIBRARY, R_FILTER_ALL, R_FILTER_AUDIO, R_FILTER_VIDEO, R_GROUP;
static wchar_t g_searchText[128]=L"";
static BOOL g_searchFocused=FALSE;
static int g_libraryTab=0; /* 0 = Queue, 1 = Library */
static int g_mediaFilter=0; /* 0 = All, 1 = Audio, 2 = Video */
static int g_groupMode=0; /* 0 = Folder, 1 = Artist, 2 = Album */
static BOOL g_hasMedia = FALSE;
static libvlc_event_manager_t *g_vlcPlaybackEventManager=NULL;
static libvlc_event_type_t g_vlcPlaybackAttachedEvents[9];
static int g_vlcPlaybackAttachedCount=0;
static volatile LONG g_vlcPlaybackEventFlags=0;
#define ABM_VLC_EVT_PLAYING       0x0001
#define ABM_VLC_EVT_PAUSED        0x0002
#define ABM_VLC_EVT_STOPPED       0x0004
#define ABM_VLC_EVT_ENDED         0x0008
#define ABM_VLC_EVT_ERROR         0x0010
#define ABM_VLC_EVT_ES_ADDED      0x0020
#define ABM_VLC_EVT_ES_DELETED    0x0040
#define ABM_VLC_EVT_MEDIA_CHANGED 0x0080
#define ABM_VLC_EVT_ES_SELECTED  0x0100
static BOOL g_receiverPopup = FALSE;
static BOOL g_audioPopupOpen = FALSE;
static char g_audioDeviceId[256] = "";
static wchar_t g_audioDeviceLabel[256] = L"System default";
static char g_audioDeviceIds[12][256];
static wchar_t g_audioDeviceNames[12][256];
static int g_audioDeviceCount = 0;
static DWORD g_audioLastDeviceCheckTick = 0;
static BOOL g_drawerOpen = FALSE;
static int g_drawerProgress = 0;
static int g_hoverPlaylistIndex = -1;
static BOOL g_drawerSettingsOpen = FALSE;
static int64_t g_advancedSubtitleDelayUs=0;
static int64_t g_audioDelayUs=0;
static int g_advancedSubtitleFontSize=13; /* LibVLC relative subtitle size: 10, 13, 16, 19 */
static int g_advancedSubtitleColor=16777215; /* RGB white */
static BOOL g_advancedSubtitleBold=FALSE;
static int g_advancedSubtitleOutline=4; /* 0 none, 2 thin, 4 normal, 6 thick */
static int g_advancedSubtitleMargin=10; /* pixels from the bottom edge */
static void ApplySubtitleAppearanceOption(libvlc_media_t *media) {
    if(!media)return;
    char option[64];
    int size=g_advancedSubtitleFontSize;
    if(size!=10&&size!=13&&size!=16&&size!=19)size=13;
    snprintf(option,sizeof(option),":freetype-rel-fontsize=%d",size);
    libvlc_media_add_option(media,option);
    int color=g_advancedSubtitleColor;
    if(color!=16777215&&color!=16776960&&color!=65535&&color!=65280)color=16777215;
    snprintf(option,sizeof(option),":freetype-color=%d",color);
    libvlc_media_add_option(media,option);
    libvlc_media_add_option(media,g_advancedSubtitleBold?":freetype-bold":":no-freetype-bold");
    int outline=g_advancedSubtitleOutline;
    if(outline!=0&&outline!=2&&outline!=4&&outline!=6)outline=4;
    snprintf(option,sizeof(option),":freetype-outline-thickness=%d",outline);
    libvlc_media_add_option(media,option);
    int margin=g_advancedSubtitleMargin;
    if(margin!=0&&margin!=10&&margin!=25&&margin!=50)margin=10;
    snprintf(option,sizeof(option),":sub-margin=%d",margin);
    libvlc_media_add_option(media,option);
}
static BOOL g_menuOpen = FALSE;
static BOOL g_fullscreen = FALSE;
/* Suppress intermediate WM_SIZE layout while Win32 restores fullscreen placement.
   LibVLC's child HWND must be resized only once after the transition completes. */
static BOOL g_fullscreenTransition = FALSE;
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
static int g_menuSelection = 0;
static RECT R_MENU_BUTTON, R_DRAWER_BUTTON, R_SETTINGS_BUTTON, R_MENU_PANEL, R_MENU_ITEMS[19], R_MENU_SECTION_LABELS[5], R_MENU_GROUPS[5], R_FULLSCREEN;
static RECT R_ADVANCED_SETTINGS_BUTTON;
static HWND g_eqWindow=NULL;
static float g_eqBandGain[10]={0,0,0,0,0,0,0,0,0,0};
static float g_eqPreamp=0.0f;
static int g_eqDragBand=-1;

enum { HIT_NONE, HIT_OPEN, HIT_PLAY, HIT_STOP, HIT_PREV, HIT_NEXT, HIT_SEEK, HIT_VOLUME, HIT_SCAN, HIT_ROW_BASE = 1000 };

#define C_BG     ABM_UI_COLOR_BACKGROUND
#define C_PANEL  ABM_UI_COLOR_SURFACE
#define C_PANEL2 ABM_UI_COLOR_SURFACE_RAISED
#define C_TEXT   ABM_THEME_COLOR_F5F8FF
#define C_MUTED  ABM_UI_COLOR_TEXT_MUTED
#define C_ACCENT ABM_THEME_COLOR_52CDE0
#define C_LINE   ABM_UI_COLOR_BORDER


/* ABM-owned modal notification surface. Replaces legacy MessageBox UI while
   preserving the Win32 result codes expected by existing call sites. */
static ABM_D2D_CANVAS g_abmDialogCanvas;
static BOOL g_abmDialogCanvasReady=FALSE;
typedef struct ABMDialogState { const wchar_t *text,*caption; UINT flags; int result; int defaultButton; int focusedButton; RECT buttons[2]; int buttonCount; BOOL done; } ABMDialogState;
static LRESULT CALLBACK ABMDialogProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    ABMDialogState *d=(ABMDialogState*)GetWindowLongPtrW(w,GWLP_USERDATA);
    switch(m) {
    case WM_NCCREATE:SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);return TRUE;
    case WM_CREATE:g_abmDialogCanvasReady=ABM_D2D_CanvasInit(&g_abmDialogCanvas);return 0;
    case WM_DESTROY:if(g_abmDialogCanvasReady){g_abmDialogCanvasReady=FALSE;ABM_D2D_CanvasDestroy(&g_abmDialogCanvas);}return 0;
    case WM_ERASEBKGND:return 1;
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE){d->result=(d->flags&MB_YESNO)?IDNO:IDOK;d->done=TRUE;DestroyWindow(w);return 0;}
        if(wp==VK_TAB||wp==VK_LEFT||wp==VK_RIGHT){
            if(d->buttonCount>1){d->focusedButton=(d->focusedButton+1)%d->buttonCount;InvalidateRect(w,NULL,FALSE);}
            return 0;
        }
        if(wp==VK_RETURN||wp==VK_SPACE){
            int chosen=(d->focusedButton>=0&&d->focusedButton<d->buttonCount)?d->focusedButton:d->defaultButton;
            d->result=(d->flags&MB_YESNO)?(chosen==0?IDYES:IDNO):IDOK;d->done=TRUE;DestroyWindow(w);return 0;
        }
        break;
    case WM_LBUTTONUP: {
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        for(int i=0;i<d->buttonCount;i++)if(x>=d->buttons[i].left&&x<d->buttons[i].right&&y>=d->buttons[i].top&&y<d->buttons[i].bottom){
            d->focusedButton=i;d->result=(d->flags&MB_YESNO)?(i==0?IDYES:IDNO):IDOK;d->done=TRUE;DestroyWindow(w);return 0;
        } return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT rc;GetClientRect(w,&rc);
        if(g_abmDialogCanvasReady&&ABM_D2D_CanvasBegin(&g_abmDialogCanvas,dc,&rc)){
            ABM_D2D_CanvasFill(&g_abmDialogCanvas,0,0,(float)rc.right,(float)rc.bottom,ABM_THEME_COLOR_090F19);
            ABM_D2D_CanvasFill(&g_abmDialogCanvas,0,0,(float)rc.right,54,ABM_THEME_COLOR_0F2230);
            RECT cap={22,8,rc.right-20,46};ABM_D2D_CanvasText(&g_abmDialogCanvas,d->caption&&*d->caption?d->caption:L"ABM AV Player",cap,ABM_THEME_COLOR_F2F8FF,ABM_D2D_TEXT_HEADING,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            COLORREF accent=(d->flags&MB_ICONERROR)?ABM_THEME_COLOR_DC4B55:(d->flags&MB_ICONWARNING)?ABM_THEME_COLOR_F2B14F:ABM_THEME_COLOR_52CDBE;
            ABM_D2D_CanvasFillRound(&g_abmDialogCanvas,22,78,30,86,3.0f,accent);
            RECT msg={44,68,rc.right-24,rc.bottom-74};ABM_D2D_CanvasText(&g_abmDialogCanvas,d->text?d->text:L"",msg,ABM_THEME_COLOR_D7E4F1,ABM_D2D_TEXT_BODY,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
            for(int i=0;i<d->buttonCount;i++){
                RECT b=d->buttons[i];BOOL primary=(i==d->defaultButton),focused=(i==d->focusedButton);
                ABM_D2D_CanvasFillRound(&g_abmDialogCanvas,(float)b.left,(float)b.top,(float)b.right,(float)b.bottom,(float)ABM_UI_RADIUS_BUTTON,primary?ABM_THEME_COLOR_187E74:ABM_THEME_COLOR_192737);
                ABM_D2D_CanvasStrokeRound(&g_abmDialogCanvas,(float)b.left+0.5f,(float)b.top+0.5f,(float)b.right-0.5f,(float)b.bottom-0.5f,(float)ABM_UI_RADIUS_BUTTON,focused?ABM_UI_COLOR_FOCUS:(primary?ABM_THEME_COLOR_44B4A4:ABM_THEME_COLOR_394E63),focused?2.0f:1.0f);
                const wchar_t *label=(d->flags&MB_YESNO)?(i==0?L"Yes":L"No"):L"OK";
                ABM_D2D_CanvasText(&g_abmDialogCanvas,label,b,ABM_THEME_COLOR_F7FAFF,ABM_D2D_TEXT_SMALL,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
            ABM_D2D_CanvasEnd(&g_abmDialogCanvas);
        }else{
            HBRUSH bg=CreateSolidBrush(ABM_THEME_COLOR_090F19);FillRect(dc,&rc,bg);DeleteObject(bg);
            SetBkMode(dc,TRANSPARENT);HFONT old=(HFONT)SelectObject(dc,g_headingFont?g_headingFont:(HFONT)GetStockObject(DEFAULT_GUI_FONT));SetTextColor(dc,ABM_THEME_COLOR_F2F8FF);
            RECT cap={22,8,rc.right-20,46};DrawTextW(dc,d->caption&&*d->caption?d->caption:L"ABM AV Player",-1,&cap,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            SelectObject(dc,g_font?g_font:(HFONT)GetStockObject(DEFAULT_GUI_FONT));SetTextColor(dc,ABM_THEME_COLOR_D7E4F1);RECT msg={44,68,rc.right-24,rc.bottom-74};
            DrawTextW(dc,d->text?d->text:L"",-1,&msg,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
            for(int i=0;i<d->buttonCount;i++){RECT b=d->buttons[i];SetTextColor(dc,ABM_THEME_COLOR_F7FAFF);DrawTextW(dc,(d->flags&MB_YESNO)?(i==0?L"Yes":L"No"):L"OK",-1,&b,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
            SelectObject(dc,old);
        }
        EndPaint(w,&ps);return 0;
    }
    case WM_NCDESTROY:if(d)d->done=TRUE;break;
    }
    return DefWindowProcW(w,m,wp,lp);
}
static int ABM_MessageBoxW(HWND owner,const wchar_t *message,const wchar_t *caption,UINT flags) {
    static BOOL registered=FALSE;HINSTANCE inst=GetModuleHandleW(NULL);
    if(!registered){WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.lpfnWndProc=ABMDialogProc;wc.hInstance=inst;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);wc.lpszClassName=L"ABMAVPlayerModalDialog";if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return IDOK;registered=TRUE;}
    ABMDialogState d={0};d.text=message;d.caption=caption;d.flags=flags;d.result=(flags&MB_YESNO)?IDNO:IDOK;d.buttonCount=(flags&MB_YESNO)?2:1;d.defaultButton=(flags&MB_DEFBUTTON2)?1:0;d.focusedButton=d.defaultButton;
    int width=560,height=220;HDC measure=GetDC(NULL);HFONT measureFont=g_font?g_font:(HFONT)GetStockObject(DEFAULT_GUI_FONT);HGDIOBJ previousFont=measure?SelectObject(measure,measureFont):NULL;RECT measured={0,0,width-90,0};if(measure&&message)DrawTextW(measure,message,-1,&measured,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_CALCRECT|DT_NOPREFIX);if(previousFont)SelectObject(measure,previousFont);if(measure)ReleaseDC(NULL,measure);if(measured.bottom>80)height=measured.bottom+150;if(height>560)height=560;RECT wr={0,0,width,height};AdjustWindowRectEx(&wr,WS_POPUP|WS_BORDER,FALSE,WS_EX_TOOLWINDOW);width=wr.right-wr.left;height=wr.bottom-wr.top;
    RECT ownerRect={0};if(owner&&IsWindow(owner))GetWindowRect(owner,&ownerRect);else SystemParametersInfoW(SPI_GETWORKAREA,0,&ownerRect,0);
    int x=(ownerRect.left+ownerRect.right-width)/2,y=(ownerRect.top+ownerRect.bottom-height)/2;
    HWND w=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"ABMAVPlayerModalDialog",L"",WS_POPUP|WS_BORDER,x,y,width,height,owner,NULL,inst,&d);
    if(!w)return d.result;
    RECT client={0};GetClientRect(w,&client);int bw=104,bh=36,gap=12,right=client.right-22,bottom=client.bottom-20;
    if(d.buttonCount==2){d.buttons[1]=(RECT){right-bw,bottom-bh,right,bottom};d.buttons[0]=(RECT){right-bw*2-gap,bottom-bh,right-bw-gap,bottom};}
    else d.buttons[0]=(RECT){right-bw,bottom-bh,right,bottom};
    BOOL ownerWasEnabled=owner&&IsWindowEnabled(owner);if(ownerWasEnabled)EnableWindow(owner,FALSE);
    ShowWindow(w,SW_SHOW);UpdateWindow(w);SetForegroundWindow(w);SetFocus(w);
    MSG msg;while(!d.done&&IsWindow(w)){BOOL got=GetMessageW(&msg,NULL,0,0);if(got<=0)break;if(!IsDialogMessageW(w,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(IsWindow(w))DestroyWindow(w);if(ownerWasEnabled){EnableWindow(owner,TRUE);SetActiveWindow(owner);}return d.result;
}


typedef struct ABMMenuItem { int command; const wchar_t *label; BOOL enabled; BOOL heading; } ABMMenuItem;
typedef struct ABMMenuState { HWND owner; const ABMMenuItem *items; int count; int hover; int first; int result; BOOL done; } ABMMenuState;
static ABMMenuState *g_abmActiveMenu=NULL;
static ABM_D2D_CANVAS g_abmMenuCanvas;
static BOOL g_abmMenuCanvasReady=FALSE;
static LRESULT CALLBACK ABMMenuProc(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    ABMMenuState *d=(ABMMenuState*)GetWindowLongPtrW(w,GWLP_USERDATA);
    switch(m) {
    case WM_NCCREATE:SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);return TRUE;
    case WM_CREATE:g_abmMenuCanvasReady=ABM_D2D_CanvasInit(&g_abmMenuCanvas);return 0;
    case WM_DESTROY:if(g_abmMenuCanvasReady){g_abmMenuCanvasReady=FALSE;ABM_D2D_CanvasDestroy(&g_abmMenuCanvas);}return 0;
    case WM_ERASEBKGND:return 1;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT t={sizeof(t),TME_LEAVE,w,0};TrackMouseEvent(&t);
        int row=(GET_Y_LPARAM(lp)-54)/36+d->first;if(row<d->first||row>=d->count||d->items[row].heading||!d->items[row].enabled)row=-1;
        if(row!=d->hover){d->hover=row;InvalidateRect(w,NULL,FALSE);}return 0;
    }
    case WM_MOUSELEAVE:d->hover=-1;InvalidateRect(w,NULL,FALSE);return 0;
    case WM_MOUSEWHEEL:{int delta=GET_WHEEL_DELTA_WPARAM(wp);d->first-=delta/WHEEL_DELTA*3;if(d->first<0)d->first=0;if(d->first>d->count-1)d->first=d->count-1;InvalidateRect(w,NULL,FALSE);return 0;}
    case WM_LBUTTONUP: {
        int row=(GET_Y_LPARAM(lp)-54)/36+d->first;
        if(row>=d->first&&row<d->count&&!d->items[row].heading&&d->items[row].enabled){d->result=d->items[row].command;d->done=TRUE;DestroyWindow(w);}return 0;
    }
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE){d->done=TRUE;DestroyWindow(w);return 0;}
        if(wp==VK_DOWN||wp==VK_UP){
            int step=wp==VK_DOWN?1:-1;int i=d->hover<0?(step>0?-1:0):d->hover;
            for(int n=0;n<d->count;n++){i=(i+step+d->count)%d->count;if(!d->items[i].heading&&d->items[i].enabled){d->hover=i;break;}}
            RECT rc;GetClientRect(w,&rc);int visible=(rc.bottom-62)/36;if(d->hover<d->first)d->first=d->hover;if(d->hover>=d->first+visible)d->first=d->hover-visible+1;InvalidateRect(w,NULL,FALSE);return 0;
        }
        if(wp==VK_RETURN&&d->hover>=0){d->result=d->items[d->hover].command;d->done=TRUE;DestroyWindow(w);return 0;}
        break;
    case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE){d->done=TRUE;DestroyWindow(w);return 0;}break;
    case WM_PAINT:{
        PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT rc;GetClientRect(w,&rc);
        if(g_abmMenuCanvasReady&&ABM_D2D_CanvasBegin(&g_abmMenuCanvas,dc,&rc)){
            ABM_D2D_CanvasFill(&g_abmMenuCanvas,0,0,(float)rc.right,(float)rc.bottom,ABM_THEME_COLOR_09101B);
            ABM_D2D_CanvasFill(&g_abmMenuCanvas,0,0,(float)rc.right,48,ABM_THEME_COLOR_0F2533);
            RECT tr={18,7,rc.right-14,42};ABM_D2D_CanvasText(&g_abmMenuCanvas,L"ABM  /  ACTIONS",tr,ABM_THEME_COLOR_F3F9FF,ABM_D2D_TEXT_BODY,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            int visible=(rc.bottom-62)/36;if(visible<1)visible=1;
            for(int v=0;v<visible&&d->first+v<d->count;v++){
                int i=d->first+v,top=54+v*36;RECT row={8,top,rc.right-8,top+33};const ABMMenuItem *it=&d->items[i];
                if(it->heading){
                    RECT t={18,top+1,rc.right-12,top+32};
                    ABM_D2D_CanvasText(&g_abmMenuCanvas,it->label,t,ABM_THEME_COLOR_59B9AE,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                    continue;
                }
                if(i==d->hover)ABM_D2D_CanvasFillRound(&g_abmMenuCanvas,(float)row.left,(float)row.top,(float)row.right,(float)row.bottom,(float)ABM_UI_RADIUS_CONTROL,ABM_THEME_COLOR_18424C);
                RECT t={20,top+1,rc.right-14,top+32};
                ABM_D2D_CanvasText(&g_abmMenuCanvas,it->label,t,it->enabled?ABM_THEME_COLOR_E2EEF9:ABM_THEME_COLOR_5B6C80,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            }
            ABM_D2D_CanvasEnd(&g_abmMenuCanvas);
        }else{
            HBRUSH bg=CreateSolidBrush(ABM_THEME_COLOR_09101B);FillRect(dc,&rc,bg);DeleteObject(bg);
            SetBkMode(dc,TRANSPARENT);HFONT old=(HFONT)SelectObject(dc,g_font);SetTextColor(dc,ABM_THEME_COLOR_F3F9FF);
            RECT tr={18,7,rc.right-14,42};DrawTextW(dc,L"ABM  /  ACTIONS",-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            int visible=(rc.bottom-62)/36;if(visible<1)visible=1;
            for(int v=0;v<visible&&d->first+v<d->count;v++){
                int i=d->first+v,top=54+v*36;const ABMMenuItem *it=&d->items[i];
                RECT row={8,top,rc.right-8,top+33};
                if(i==d->hover){HBRUSH hot=CreateSolidBrush(ABM_THEME_COLOR_18424C);FillRect(dc,&row,hot);DeleteObject(hot);}
                SelectObject(dc,it->heading?g_smallFont:g_font);SetTextColor(dc,it->heading?ABM_THEME_COLOR_59B9AE:it->enabled?ABM_THEME_COLOR_E2EEF9:ABM_THEME_COLOR_5B6C80);
                RECT t={20,top+1,rc.right-14,top+32};DrawTextW(dc,it->label,-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            }
            SelectObject(dc,old);
        }
        EndPaint(w,&ps);return 0;
    }
    case WM_NCDESTROY:if(g_abmActiveMenu==d)g_abmActiveMenu=NULL;break;
    }
    return DefWindowProcW(w,m,wp,lp);
}
static int ABM_ShowMenu(HWND owner,int x,int y,const ABMMenuItem *items,int count) {
    if(!items||count<=0)return 0;static BOOL registered=FALSE;HINSTANCE inst=GetModuleHandleW(NULL);
    if(!registered){WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.lpfnWndProc=ABMMenuProc;wc.hInstance=inst;wc.hCursor=LoadCursorW(NULL,IDC_HAND);wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);wc.lpszClassName=L"ABMAVPlayerActionMenu";if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 0;registered=TRUE;}
    ABMMenuState d={owner,items,count,-1,0,0,FALSE};POINT pt={x,y};ClientToScreen(owner,&pt);
    int width=380,height=54+count*36+8;RECT work;SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    if(height>work.bottom-work.top-24)height=work.bottom-work.top-24;
    if(pt.x+width>work.right)pt.x=work.right-width;if(pt.y+height>work.bottom)pt.y=work.bottom-height;
    if(pt.x<work.left)pt.x=work.left;if(pt.y<work.top)pt.y=work.top;
    HWND w=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"ABMAVPlayerActionMenu",L"",WS_POPUP|WS_BORDER,pt.x,pt.y,width,height,owner,NULL,inst,&d);
    if(!w)return 0;g_abmActiveMenu=&d;ShowWindow(w,SW_SHOW);SetForegroundWindow(w);SetFocus(w);InvalidateRect(w,NULL,TRUE);
    MSG msg;while(!d.done&&IsWindow(w)){BOOL got=GetMessageW(&msg,NULL,0,0);if(got<=0)break;if(!IsDialogMessageW(w,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(IsWindow(w))DestroyWindow(w);return d.result;
}

int ABM_ShowMessageBox(HWND owner,const wchar_t *message,const wchar_t *caption,UINT flags) { return ABM_MessageBoxW(owner,message,caption,flags); }

static void PlayIndex(int index);
static void ResumeCaptureCurrent(void);
static int64_t ResumePositionForPath(const wchar_t *path);
static void RefreshMissingMediaFlags(void);
static void RefreshResumeFlags(void);
static void FormatResumeLabel(const wchar_t *path,wchar_t *out,size_t capacity);
static void TorrentStopPlayback(void);
static void SaveAudioPreferences(void);
static void RefreshAudioDevices(void);
static void ApplyAudioOutputDevice(void);
static void ApplyEqProfile(void);
static void ShowEqualizer(void);
static void ShowAdvancedSettingsAtPage(HWND owner,int page);
static void ApplyCustomEqualizer(void);
static LRESULT CALLBACK EqualizerWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp);
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
static void SavePersistentState(void);
static BOOL IsFavoritePath(const wchar_t *path) {
    if(!path||!path[0])return FALSE;
    for(int i=0;i<g_favoriteCount;i++)if(!_wcsicmp(g_favorites[i],path))return TRUE;
    return FALSE;
}
static void ToggleFavoritePath(const wchar_t *path) {
    if(!path||!path[0])return;
    int found=-1;for(int i=0;i<g_favoriteCount;i++)if(!_wcsicmp(g_favorites[i],path)){found=i;break;}
    if(found>=0){for(int i=found+1;i<g_favoriteCount;i++)wcsncpy_s(g_favorites[i-1],PATH_CAP,g_favorites[i],_TRUNCATE);g_favoriteCount--;}
    else if(g_favoriteCount<MAX_FAVORITES)wcsncpy_s(g_favorites[g_favoriteCount++],PATH_CAP,path,_TRUNCATE);
    SavePersistentState();if(g_main)InvalidateRect(g_main,&R_SIDEBAR,FALSE);
}
static void EnsureFavoritesInPlaylist(void) {
    BOOL changed=FALSE;
    for(int f=0;f<g_favoriteCount&&g_playlistCount<MAX_PLAYLIST;f++) {
        BOOL found=FALSE;
        for(int i=0;i<g_playlistCount;i++)if(!_wcsicmp(g_playlist[i],g_favorites[f])){found=TRUE;break;}
        if(!found){wcsncpy_s(g_playlist[g_playlistCount++],PATH_CAP,g_favorites[f],_TRUNCATE);changed=TRUE;}
    }
    if(g_selected<0&&g_playlistCount)g_selected=0;
    if(changed){RefreshMissingMediaFlags();SavePersistentState();}
}
static int CountFilteredMedia(void) {
    int count=0;
    for(int i=0;i<g_playlistCount;i++){
        if(g_libraryTab==1&&g_showFavorites&&!IsFavoritePath(g_playlist[i]))continue;
        if(g_libraryTab==1&&g_showContinueWatching&&!g_hasResume[i])continue;
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
/* Keep collection rows fully inside the list viewport. Never count a
   partially visible final row: its title and metadata rectangles would be
   compressed into one another at the bottom edge of the drawer. */
static int PlaylistRowHeight(void) {
    int h=R_LIST.bottom-R_LIST.top;
    if(h>=80)return 80;
    if(h>=56)return 56;
    if(h>=40)return 40;
    return h>0?h:40;
}
static int PlaylistVisibleRows(int rowHeight) {
    int h=R_LIST.bottom-R_LIST.top;
    if(h<=0||rowHeight<=0)return 0;
    /* Floor division is deliberate: only complete cards are drawable. */
    int n=h/rowHeight;
    return n>MAX_PLAYLIST?MAX_PLAYLIST:n;
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
    LONG previousGeneration=InterlockedCompareExchange(&g_artworkGeneration,0,0);
    if(g_dataDir[0] && previousGeneration>0) {
        wchar_t obsoletePath[PATH_CAP]={0};
        if(SUCCEEDED(StringCchPrintfW(obsoletePath,_countof(obsoletePath),
            L"%s\\artwork-%ld.bin",g_dataDir,(long)previousGeneration)))
            DeleteFileW(obsoletePath);
    }
    if (g_artMedia) {
        libvlc_event_manager_t *em=libvlc_media_event_manager(g_artMedia);
        libvlc_event_detach(em,libvlc_MediaParsedChanged,ArtworkParsedEvent,
                            (void *)(INT_PTR)g_artworkCallbackGeneration);
        g_artworkCallbackGeneration=0;
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
    LONG generation=InterlockedCompareExchange(&g_artworkGeneration,0,0);
    if (libvlc_event_attach(em,libvlc_MediaParsedChanged,ArtworkParsedEvent,(void *)(INT_PTR)generation)!=0) {
        libvlc_media_release(g_artMedia);g_artMedia=NULL;
        return;
    }
    g_artworkCallbackGeneration=generation;
    if(libvlc_media_parse_with_options(g_artMedia,libvlc_media_parse_local,5000)!=0) {
        libvlc_event_detach(em,libvlc_MediaParsedChanged,ArtworkParsedEvent,(void *)(INT_PTR)generation);
        g_artworkCallbackGeneration=0;
        libvlc_media_release(g_artMedia);g_artMedia=NULL;
    }
}
static void ArtworkParsedEvent(const libvlc_event_t *ev, void *opaque) {
    LONG generation=(LONG)(INT_PTR)opaque;
    HWND target=g_main;
    if (ev && ev->type==libvlc_MediaParsedChanged && target)
        PostMessageW(target,WM_ARTWORK_READY,(WPARAM)generation,0);
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

#define ARTWORK_MAX_BYTES (10u * 1024u * 1024u)
#define ARTWORK_MAX_PIXELS (16u * 1024u * 1024u)
#define ARTWORK_MAX_REDIRECTS 4

/* Metadata is untrusted input. Only public HTTPS image hosts are eligible.
   Reject local/private destinations before opening a connection; redirects
   are deliberately not followed so a public URL cannot redirect into LAN. */
static BOOL ArtworkHostIsPublic(const wchar_t *host) {
    ADDRINFOW hints={0}, *addresses=NULL, *it;
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype=SOCK_STREAM;
    if(GetAddrInfoW(host,NULL,&hints,&addresses)!=0 || !addresses) return FALSE;
    BOOL valid=TRUE;
    for(it=addresses;it;it=it->ai_next) {
        if(it->ai_family==AF_INET) {
            const unsigned char *a=(const unsigned char *)&((struct sockaddr_in *)it->ai_addr)->sin_addr;
            if(a[0]==0 || a[0]==10 || a[0]==127 || (a[0]==169&&a[1]==254) ||
               (a[0]==172&&a[1]>=16&&a[1]<=31) || (a[0]==192&&a[1]==168) ||
               (a[0]>=224)) valid=FALSE;
        } else if(it->ai_family==AF_INET6) {
            const unsigned char *a=(const unsigned char *)&((struct sockaddr_in6 *)it->ai_addr)->sin6_addr;
            static const unsigned char loopback[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
            if((a[0]&0xfe)==0xfc || (a[0]==0xfe&&(a[1]&0xc0)==0x80) ||
               memcmp(a,loopback,16)==0 ||
               (a[0]==0&&a[1]==0&&a[2]==0&&a[3]==0&&a[4]==0&&a[5]==0&&a[6]==0&&a[7]==0&&a[8]==0&&a[9]==0&&a[10]==0xff&&a[11]==0xff))
                valid=FALSE;
        }
        if(!valid) break;
    }
    FreeAddrInfoW(addresses);
    return valid;
}

static BOOL ArtworkUrlIsSafe(const wchar_t *url, URL_COMPONENTSW *parts,
                             wchar_t *host, DWORD hostCount) {
    ZeroMemory(parts,sizeof(*parts));
    parts->dwStructSize=sizeof(*parts);
    parts->dwHostNameLength=(DWORD)-1;
    parts->dwSchemeLength=(DWORD)-1;
    parts->dwUrlPathLength=(DWORD)-1;
    parts->dwExtraInfoLength=(DWORD)-1;
    if(!WinHttpCrackUrl(url,0,0,parts) || parts->nScheme!=INTERNET_SCHEME_HTTPS ||
       parts->dwHostNameLength==0 || parts->dwHostNameLength>=hostCount ||
       parts->dwUserNameLength!=0 || parts->dwPasswordLength!=0) return FALSE;
    if(parts->dwHostNameLength>=hostCount) return FALSE;
    wcsncpy_s(host,hostCount,parts->lpszHostName,parts->dwHostNameLength);
    if(!_wcsicmp(host,L"localhost") || !_wcsicmp(host,L"localhost.localdomain")) return FALSE;
    return ArtworkHostIsPublic(host);
}

static BOOL ValidateArtworkImage(const wchar_t *path) {
    IWICImagingFactory *factory=NULL;
    IWICBitmapDecoder *decoder=NULL;
    IWICBitmapFrameDecode *frame=NULL;
    BOOL valid=FALSE;
    UINT width=0,height=0;
    HRESULT hr=CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,
                                &IID_IWICImagingFactory,(void **)&factory);
    if(SUCCEEDED(hr) && factory)
        hr=factory->lpVtbl->CreateDecoderFromFilename(factory,path,NULL,GENERIC_READ,
                 WICDecodeMetadataCacheOnLoad,&decoder);
    if(SUCCEEDED(hr) && decoder)
        hr=decoder->lpVtbl->GetFrame(decoder,0,&frame);
    if(SUCCEEDED(hr) && frame)
        hr=frame->lpVtbl->GetSize(frame,&width,&height);
    if(SUCCEEDED(hr) && width>0 && height>0 &&
       (ULONGLONG)width*(ULONGLONG)height<=ARTWORK_MAX_PIXELS &&
       width<=8192 && height<=8192) {
        /* Force a full pixel decode before the file is accepted. */
        IWICFormatConverter *converter=NULL;
        hr=factory->lpVtbl->CreateFormatConverter(factory,&converter);
        if(SUCCEEDED(hr) && converter) {
            hr=converter->lpVtbl->Initialize(converter,(IWICBitmapSource *)frame,
                &GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,NULL,0.0,
                WICBitmapPaletteTypeCustom);
            if(SUCCEEDED(hr)) {
                UINT stride=width*4, bytes=stride*height;
                BYTE *pixels=(BYTE *)malloc(bytes);
                if(pixels) {
                    hr=converter->lpVtbl->CopyPixels(converter,NULL,stride,bytes,pixels);
                    free(pixels);
                    valid=SUCCEEDED(hr);
                }
            }
            converter->lpVtbl->Release(converter);
        }
    }
    if(frame)frame->lpVtbl->Release(frame);
    if(decoder)decoder->lpVtbl->Release(decoder);
    if(factory)factory->lpVtbl->Release(factory);
    return valid;
}

static BOOL DownloadArtworkSecurely(const wchar_t *url,const wchar_t *finalPath,
                                    LONG generation) {
    URL_COMPONENTSW parts;
    wchar_t host[256]={0}, tempPath[PATH_CAP]={0};
    HINTERNET session=NULL,connection=NULL,request=NULL;
    HANDLE file=INVALID_HANDLE_VALUE;
    BOOL ok=FALSE;
    DWORD status=0,statusSize=sizeof(status),read=0,written=0;
    BYTE buffer[16384];
    if(!ArtworkUrlIsSafe(url,&parts,host,_countof(host))) return FALSE;
    if(FAILED(StringCchPrintfW(tempPath,_countof(tempPath),L"%s.tmp-%ld",finalPath,(long)generation)))
        return FALSE;
    session=WinHttpOpen(L"ABM AV Player/Artwork",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session) goto cleanup;
    WinHttpSetTimeouts(session,5000,5000,7000,7000);
    connection=WinHttpConnect(session,host,parts.nPort,0);
    if(!connection) goto cleanup;
    wchar_t requestPath[PATH_CAP*4]={0};
    if(parts.dwUrlPathLength>=_countof(requestPath)) goto cleanup;
    if(parts.dwUrlPathLength) wcsncpy_s(requestPath,_countof(requestPath),parts.lpszUrlPath,parts.dwUrlPathLength);
    else wcscpy_s(requestPath,_countof(requestPath),L"/");
    if(parts.dwExtraInfoLength &&
       (wcslen(requestPath)+parts.dwExtraInfoLength)<_countof(requestPath))
        wcsncat_s(requestPath,_countof(requestPath),parts.lpszExtraInfo,parts.dwExtraInfoLength);
    request=WinHttpOpenRequest(connection,L"GET",requestPath,
        NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if(!request) goto cleanup;
    /* No automatic redirects: every redirect would need a fresh DNS and
       destination-policy check. Reject them rather than risk a private hop. */
    { DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
      WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy)); }
    if(!WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,
                           WINHTTP_NO_REQUEST_DATA,0,0,0) ||
       !WinHttpReceiveResponse(request,NULL)) goto cleanup;
    if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,&status,&statusSize,
                            WINHTTP_NO_HEADER_INDEX) || status!=HTTP_STATUS_OK) goto cleanup;
    file=CreateFileW(tempPath,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,
                     FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_SEQUENTIAL_SCAN,NULL);
    if(file==INVALID_HANDLE_VALUE) goto cleanup;
    ULONGLONG total=0;
    for(;;) {
        if(!WinHttpReadData(request,buffer,sizeof(buffer),&read)) goto cleanup;
        if(read==0) break;
        total+=read;
        if(total>ARTWORK_MAX_BYTES ||
           !WriteFile(file,buffer,read,&written,NULL) || written!=read) goto cleanup;
    }
    if(total<16 || !FlushFileBuffers(file)) goto cleanup;
    CloseHandle(file);file=INVALID_HANDLE_VALUE;
    if(!ValidateArtworkImage(tempPath)) goto cleanup;
    if(!MoveFileExW(tempPath,finalPath,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        goto cleanup;
    ok=TRUE;
cleanup:
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(!ok)DeleteFileW(tempPath);
    if(request)WinHttpCloseHandle(request);
    if(connection)WinHttpCloseHandle(connection);
    if(session)WinHttpCloseHandle(session);
    return ok;
}

static DWORD WINAPI ArtworkDownloadThread(LPVOID parameter) {
    ArtworkDownloadRequest *request=(ArtworkDownloadRequest *)parameter;
    HRESULT com=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    BOOL downloaded=DownloadArtworkSecurely(request->url,request->path,request->generation);
    if(SUCCEEDED(com))CoUninitialize();
    BOOL posted=FALSE;
    if(IsWindow(request->target))
        posted=PostMessageW(request->target,WM_ARTWORK_DOWNLOAD_READY,
                            (WPARAM)request->generation,(LPARAM)downloaded);
    if(!posted || !downloaded)DeleteFileW(request->path);
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
/* Writes timing diagnostics to %TEMP%\\ABMAVPlayer-fullscreen-diagnostics.log.
   This is intentionally lightweight and only records potentially blocking
   fullscreen/seek/stop operations. */
static void FullscreenDiag(const char *stage, ULONGLONG elapsedMs) {
    wchar_t path[MAX_PATH];
    DWORD n=GetTempPathW(_countof(path),path);
    if(n==0 || n>=_countof(path)) return;
    if(wcslen(path)+48>=_countof(path)) return;
    wcscat_s(path,_countof(path),L"ABMAVPlayer-fullscreen-diagnostics.log");
    FILE *f=NULL;
    if(_wfopen_s(&f,path,L"a, ccs=UTF-8")!=0 || !f) return;
    SYSTEMTIME now; GetLocalTime(&now);
    fwprintf(f,L"%04u-%02u-%02u %02u:%02u:%02u.%03u | %S | %llu ms\n",
        now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,
        now.wMilliseconds,stage,(unsigned long long)elapsedMs);
    fclose(f);
}
static void PositionVideoWindow(void) {
    if (!g_video || !IsWindow(g_video)) return;

    /* Audio-only playback is rendered by the main window's visualizer.
       The LibVLC child HWND is always above the owner's Direct2D surface;
       showing it, even with no video output, covers the fullscreen visuals. */
    if(g_visualizerActive) {
        ShowWindow(g_video,SW_HIDE);
        g_videoBoundsValid=FALSE;
        return;
    }

    int areaW = R_VIDEO.right - R_VIDEO.left;
    int areaH = R_VIDEO.bottom - R_VIDEO.top;
    /* The media drawer is an always-visible foreground workspace. LibVLC
       renders into a native child HWND, which is composited above the main
       window's GDI/Direct2D paint. Do not let that video HWND extend beneath
       the drawer: reserve the drawer's full footprint while it is open. */
    if (g_drawerOpen && !g_fullscreen) {
        int drawerSafeRight = R_SIDEBAR.left - 12;
        int availableW = drawerSafeRight - R_VIDEO.left;
        if (availableW < areaW) areaW = availableW;
    }
    /* LibVLC renders in a native child HWND, which always sits above the
       main window's Direct2D surface. Reserve the command-menu footprint
       instead of trying to paint the menu over that video HWND. */
    if ((g_menuOpen || g_menuProgress > 0) && !g_fullscreen) {
        int menuSafeRight = R_MENU_PANEL.left - 12;
        int availableW = menuSafeRight - R_VIDEO.left;
        if (availableW < areaW) areaW = availableW;
    }
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
        ULONGLONG positionStart=GetTickCount64();
        FullscreenDiag(g_fullscreenTransition?"video-child-resize-during-transition-begin":"video-child-resize-begin",0);
        SetWindowPos(g_video, HWND_TOP, x, y, videoW, videoH,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        FullscreenDiag(g_fullscreenTransition?"video-child-resize-during-transition":"video-child-resize",GetTickCount64()-positionStart);
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
    /* Keep the LibVLC child HWND above the seek row. The seek bar is painted
       by the main window at h-175; ending the video viewport at h-190 gives
       it a dedicated 15 px gap so the native video surface cannot cover it. */
    int left=28,top=104,bottom=190;
    int side=w>=1700?640:(w>=1400?600:(w>=1180?540:460));
    int drawerWidth=side*g_drawerProgress/100;
    int videoW=w-left*2; if(videoW<260)videoW=260;
    int videoH=h-top-bottom; if(videoH<140)videoH=140;
    R_VIDEO=(RECT){left,top,left+videoW,top+videoH};
    /* Detached overlay: preserve the video stage and slide the library over it. */
    int drawerRight=w-18;
    int drawerLeft=drawerRight-drawerWidth;
    /* Keep the drawer above the transport/seek region. The previous h-28
       bottom edge let its volume footer paint over the player controls. */
    /* Full-height floating library: it opens from the top edge and owns its
       complete vertical space, including the volume footer. */
    int drawerBottom=h-42;
    if(drawerBottom<520)drawerBottom=520;
    R_SIDEBAR=(RECT){drawerLeft,18,drawerLeft+side,drawerBottom};
    /* Collection-first drawer. Settings occupy a separate expandable region;
       all library controls are laid out below it with no negative offsets. */
    int collectionTop=g_drawerSettingsOpen?438:82;
    R_SETTINGS_BUTTON=(RECT){R_SIDEBAR.left+16,R_SIDEBAR.top+14,R_SIDEBAR.right-16,R_SIDEBAR.top+66};
    R_ADVANCED_SETTINGS_BUTTON=(RECT){R_SIDEBAR.left+16,R_SIDEBAR.top+310,R_SIDEBAR.left+212,R_SIDEBAR.top+350};
    R_SCAN=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+104,R_SIDEBAR.right-14,R_SIDEBAR.top+140};
    R_RECEIVER=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+148,R_SIDEBAR.right-14,R_SIDEBAR.top+188};
    R_SHUFFLE=(RECT){R_SIDEBAR.left+16,R_SIDEBAR.top+224,R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2-5,R_SIDEBAR.top+260};
    R_REPEAT=(RECT){R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2+5,R_SIDEBAR.top+224,R_SIDEBAR.right-16,R_SIDEBAR.top+260};
    R_SPEED=(RECT){R_SIDEBAR.left+16,R_SIDEBAR.top+266,R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2-5,R_SIDEBAR.top+302};
    R_RECENT=(RECT){R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2+5,R_SIDEBAR.top+266,R_SIDEBAR.right-16,R_SIDEBAR.top+302};
    R_SEARCH=(RECT){R_SIDEBAR.left+16,R_SIDEBAR.top+collectionTop+34,R_SIDEBAR.right-16,R_SIDEBAR.top+collectionTop+76};
    R_TAB_QUEUE=(RECT){R_SIDEBAR.left+16,R_SIDEBAR.top+collectionTop+86,R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2-4,R_SIDEBAR.top+collectionTop+126};
    R_TAB_LIBRARY=(RECT){R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2+4,R_SIDEBAR.top+collectionTop+86,R_SIDEBAR.right-16,R_SIDEBAR.top+collectionTop+126};
    R_FILTER_ALL=(RECT){R_SIDEBAR.left+16,R_SIDEBAR.top+collectionTop+136,R_SIDEBAR.left+80,R_SIDEBAR.top+collectionTop+168};
    R_FILTER_AUDIO=(RECT){R_SIDEBAR.left+86,R_SIDEBAR.top+collectionTop+136,R_SIDEBAR.left+158,R_SIDEBAR.top+collectionTop+168};
    R_FILTER_VIDEO=(RECT){R_SIDEBAR.left+164,R_SIDEBAR.top+collectionTop+136,R_SIDEBAR.left+236,R_SIDEBAR.top+collectionTop+168};
    R_GROUP=(RECT){R_SIDEBAR.right-112,R_SIDEBAR.top+collectionTop+136,R_SIDEBAR.right-16,R_SIDEBAR.top+collectionTop+168};
    R_CLEAR_PLAYLIST=(RECT){R_SIDEBAR.right-82,R_SIDEBAR.top+collectionTop-2,R_SIDEBAR.right-16,R_SIDEBAR.top+collectionTop+26};
    R_LIST=(RECT){R_SIDEBAR.left+12,R_SIDEBAR.top+collectionTop+176,R_SIDEBAR.right-12,R_SIDEBAR.bottom-112};
    R_PLAYLIST_SCROLL=(RECT){R_LIST.right-8,R_LIST.top+8,R_LIST.right-3,R_LIST.bottom-8};
    R_PLAYLIST_THUMB=R_PLAYLIST_SCROLL;
    R_VOLUME=(RECT){R_SIDEBAR.left+100,R_SIDEBAR.bottom-43,R_SIDEBAR.right-92,R_SIDEBAR.bottom-35};
    R_AUDIO_BUTTON=(RECT){R_SIDEBAR.right-70,R_SIDEBAR.bottom-65,R_SIDEBAR.right-18,R_SIDEBAR.bottom-15};
    R_MENU_BUTTON=(RECT){w-244,16,w-156,72};
    R_DRAWER_BUTTON=(RECT){w-144,16,w-20,72};
    /* Premium Command Centre: compact responsive groups, shared ABM spacing,
       typography and surfaces. Action indices remain unchanged for input routing. */
    R_MENU_PANEL=(RECT){w-430,74,w-20,74+690};
    if(R_MENU_PANEL.right>w-12)R_MENU_PANEL.right=w-12;
    if(R_MENU_PANEL.left<12)R_MENU_PANEL.left=12;
    if(R_MENU_PANEL.bottom>h-12)R_MENU_PANEL.bottom=h-12;
    const int menuStarts[5]={0,1,3,11,13};
    const int menuEnds[5]={1,3,11,13,19};
    int menuUsable=R_MENU_PANEL.bottom-R_MENU_PANEL.top-60;
    int showSectionLabels=(R_MENU_PANEL.bottom-R_MENU_PANEL.top)>=620;
    int sectionHeaderH=showSectionLabels?16:0;
    int sectionGap=showSectionLabels?5:3;
    int sectionSpace=5*sectionHeaderH+5*sectionGap;
    int menuUnit=(menuUsable-sectionSpace)/21;
    if(menuUnit<15)menuUnit=15;
    int menuY=R_MENU_PANEL.top+54;
    for(int group=0;group<5;group++){
        R_MENU_SECTION_LABELS[group]=(RECT){R_MENU_PANEL.left+16,menuY,R_MENU_PANEL.right-16,menuY+sectionHeaderH};
        menuY+=sectionHeaderH;
        int groupTop=menuY;
        for(int i=menuStarts[group];i<menuEnds[group];i++){
            int weight=(i==11||i==12)?2:1;
            int rowH=menuUnit*weight;
            R_MENU_ITEMS[i]=(RECT){R_MENU_PANEL.left+12,menuY,R_MENU_PANEL.right-12,menuY+rowH-3};
            menuY+=rowH;
        }
        R_MENU_GROUPS[group]=(RECT){R_MENU_PANEL.left+12,groupTop,R_MENU_PANEL.right-12,menuY-1};
        menuY+=sectionGap;
    }
    R_FULLSCREEN=(RECT){R_VIDEO.right-50,R_VIDEO.top+16,R_VIDEO.right-14,R_VIDEO.top+52};
    /* Keep the four dashboard actions strictly inside the green hero panel.
       The panel, not the full video stage, is the source of truth for their
       available width and height. Switch to one column only when two columns
       would make the cards too narrow to read. */
    {
        int hw=R_VIDEO.right-R_VIDEO.left, hh=R_VIDEO.bottom-R_VIDEO.top;
        RECT hero={
            R_VIDEO.left+hw*36/100,
            R_VIDEO.top+hh/6,
            R_VIDEO.right-hw/8,
            R_VIDEO.bottom-hh/7
        };
        int panelW=hero.right-hero.left, panelH=hero.bottom-hero.top;
        /* Balanced 2 x 2 shortcut grid, right-aligned within the hero panel
           with a deliberate inset so it does not touch the panel edge. */
        int cardGap=12, rowGap=12, footGap=12, footH=20;
        int cardH=66, x, y, cardW;
        int gridW=panelW*78/100;
        if(gridW>456)gridW=456;
        if(gridW>panelW-48)gridW=panelW-48;
        if(gridW<260)gridW=panelW-24;
        cardW=(gridW-cardGap)/2;
        int totalH=2*cardH+rowGap+footGap+footH;
        if(totalH>panelH-20) {
            cardH=(panelH-20-rowGap-footGap-footH)/2;
            if(cardH>66)cardH=66;
            if(cardH<44)cardH=44;
            totalH=2*cardH+rowGap+footGap+footH;
        }
        x=hero.right-28-gridW;
        y=hero.top+(panelH-totalH)/2;
        if(y<hero.top+8)y=hero.top+8;
        int rightX=x+cardW+cardGap;
        R_HOME_OPEN=(RECT){x,y,x+cardW,y+cardH};
        R_HOME_STREAM=(RECT){rightX,y,rightX+cardW,y+cardH};
        R_HOME_TORRENT=(RECT){x,y+cardH+rowGap,x+cardW,y+2*cardH+rowGap};
        R_HOME_RECENT=(RECT){rightX,y+cardH+rowGap,rightX+cardW,y+2*cardH+rowGap};
    }
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
    R_SEEK=(RECT){left+38,h-175,R_VIDEO.right-10,h-171};
    int dockLeft=R_VIDEO.left+((R_VIDEO.right-R_VIDEO.left)-720)/2;
    R_PREV=(RECT){dockLeft+366,h-112,dockLeft+422,h-56};
    R_PLAY=(RECT){dockLeft+438,h-126,dockLeft+510,h-54};
    R_NEXT=(RECT){dockLeft+526,h-112,dockLeft+582,h-56};
    R_STOP=(RECT){dockLeft+596,h-108,dockLeft+652,h-52};
    R_OPEN=(RECT){left+8,h-118,left+158,h-62};
    /* All drawer hit targets are assigned in the collection-first layout above. */
    int rowHeight=PlaylistRowHeight();
    for(int i=0;i<MAX_PLAYLIST;i++) R_ROW[i]=(RECT){R_LIST.left,R_LIST.top+(i-g_playlistScroll)*rowHeight,R_LIST.right,R_LIST.top+(i-g_playlistScroll+1)*rowHeight};
}
/* ABM-branded fallback artwork for library items without embedded art.
   These are vector-rendered locally, so they are crisp at every DPI and need
   no network access, external image assets or per-file cache entries. */
static void DrawDefaultMediaArtwork(HDC dc,RECT r,BOOL video,int variant) {
    int w=r.right-r.left,h=r.bottom-r.top;
    if(w<=2||h<=2)return;
    RECT inner={r.left+1,r.top+1,r.right-1,r.bottom-1};
    if(video) {
        COLORREF top=(variant%3==0)?ABM_THEME_COLOR_0D1825:
                     (variant%3==1)?ABM_THEME_COLOR_183449:ABM_THEME_COLOR_1C3445;
        COLORREF bottom=(variant%2)?ABM_THEME_COLOR_1B7771:ABM_THEME_COLOR_1E6F77;
        GradientRect(dc,inner,top,bottom);
        /* Cinematic horizon, distant ridgelines and a luminous play motif. */
        int horizon=inner.top+h*64/100;
        DrawLine(dc,inner.left+3,horizon,inner.right-3,horizon,ABM_THEME_COLOR_63E1CD,1);
        DrawLine(dc,inner.left+w/10,horizon,inner.left+w/3,inner.top+h/3,ABM_THEME_COLOR_5EB7F4,1);
        DrawLine(dc,inner.left+w/3,inner.top+h/3,inner.left+w*55/100,horizon,ABM_THEME_COLOR_5EB7F4,1);
        DrawLine(dc,inner.left+w*42/100,horizon,inner.left+w*68/100,inner.top+h/4,ABM_THEME_COLOR_74D7D3,1);
        DrawLine(dc,inner.left+w*68/100,inner.top+h/4,inner.right-w/12,horizon,ABM_THEME_COLOR_74D7D3,1);
        if(w>=48&&h>=48) {
            int cx=inner.left+w*72/100,cy=inner.top+h*30/100,rad=w/12;
            RECT sun={cx-rad,cy-rad,cx+rad,cy+rad};
            RoundBox(dc,sun,ABM_THEME_COLOR_A4C1EF,rad);
            int px=inner.left+w/2,py=inner.top+h/2,sz=w/10;
            DrawLine(dc,px-sz,py-sz,px+sz,py,ABM_THEME_COLOR_FFFFFF,2);
            DrawLine(dc,px+sz,py,px-sz,py+sz,ABM_THEME_COLOR_FFFFFF,2);
            DrawLine(dc,px-sz,py+sz,px-sz,py-sz,ABM_THEME_COLOR_FFFFFF,2);
        } else {
            int cx=(inner.left+inner.right)/2,cy=(inner.top+inner.bottom)/2,sz=(w<h?w:h)/5;
            DrawLine(dc,cx-sz,cy-sz,cx+sz,cy,ABM_THEME_COLOR_FFFFFF,2);
            DrawLine(dc,cx+sz,cy,cx-sz,cy+sz,ABM_THEME_COLOR_FFFFFF,2);
            DrawLine(dc,cx-sz,cy+sz,cx-sz,cy-sz,ABM_THEME_COLOR_FFFFFF,2);
        }
    } else {
        GradientRect(dc,inner,ABM_THEME_COLOR_0B1727,
                     (variant%2)?ABM_THEME_COLOR_1C3445:ABM_THEME_COLOR_1B7771);
        /* Equalizer-inspired cover: a balanced, repeatable waveform. */
        int cx=(inner.left+inner.right)/2,cy=(inner.top+inner.bottom)/2;
        int bars=w>=48?9:5;
        int step=(w-12)/bars;if(step<3)step=3;
        int barW=step>4?3:2;
        for(int b=0;b<bars;b++) {
            int wave=(b*7+variant*3)%11;
            int bh=(h>=48?h/5: h/7)+(wave*(h>=48?h/28:1));
            if(bh>h-8)bh=h-8;
            RECT bar={cx-(bars*step)/2+b*step,cy-bh/2,
                      cx-(bars*step)/2+b*step+barW,cy+bh/2};
            GradientRect(dc,bar,(b%2)?ABM_THEME_COLOR_28BED3:ABM_THEME_COLOR_63E1CD,
                         (b%3)?ABM_THEME_COLOR_5EB7F4:ABM_THEME_COLOR_6F69EF);
        }
        if(w>=48&&h>=48) {
            RECT frame={inner.left+5,inner.top+5,inner.right-5,inner.bottom-5};
            ABM_D2D_StrokeRound(&g_ui,(float)frame.left,(float)frame.top,
                (float)frame.right,(float)frame.bottom,7.0f,ABM_THEME_COLOR_63E1CD,0.55f);
        }
    }
    ABM_D2D_StrokeRound(&g_ui,(float)r.left,(float)r.top,(float)r.right,
        (float)r.bottom,8.0f,video?ABM_THEME_COLOR_5EB7F4:ABM_THEME_COLOR_63E1CD,0.8f);
}
static void DrawIconButton(HDC dc,RECT r,int kind,BOOL hot,BOOL active) {
    (void)dc;
    int cx=(r.left+r.right)/2,cy=(r.top+r.bottom)/2;
    int diameter=(r.right-r.left<r.bottom-r.top)?r.right-r.left:r.bottom-r.top;
    int radius=diameter/2;
    COLORREF glyph=ABM_THEME_COLOR_E0E7F0;
    if(active) {
        int pulse=(int)(2.0+2.0*(0.5+0.5*sin((double)g_visualPulse*0.07)));
        RECT halo=r; InflateRect(&halo,4,4);
        RoundBox(NULL,halo,Blend(ABM_THEME_COLOR_0C3039,ABM_THEME_COLOR_184B52,pulse*2),radius+5);
        GradientRound(NULL,r,ABM_THEME_COLOR_36B1A8,ABM_THEME_COLOR_1C7D77,radius);
        ABM_D2D_StrokeRound(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,(float)radius,ABM_THEME_COLOR_91E5D5,1.25f);
        glyph=ABM_THEME_COLOR_FFFFFF;
    } else {
        GradientRound(NULL,r,hot?ABM_THEME_COLOR_253D45:ABM_THEME_COLOR_1B2735,
            hot?ABM_THEME_COLOR_18303A:ABM_THEME_COLOR_141D2B,radius);
        if(hot) {
            RECT sheen={r.left+7,r.top+3,r.right-7,r.top+5};
            GradientRect(NULL,sheen,ABM_THEME_COLOR_3E9799,ABM_THEME_COLOR_5DB097);
        }
        ABM_D2D_StrokeRound(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,(float)radius,
            hot?ABM_THEME_COLOR_69BEB1:ABM_THEME_COLOR_394E63,hot?1.15f:0.8f);
        glyph=hot?ABM_THEME_COLOR_F5FFFF:ABM_THEME_COLOR_BED5E3;
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
static void EnsureProjectMRenderer(HWND hwnd) {
    if(g_projectmTried)return;
    g_projectmTried=TRUE;
    wchar_t exePath[PATH_CAP],presetPath[PATH_CAP],texturePath[PATH_CAP];
    DWORD exeLen=GetModuleFileNameW(NULL,exePath,_countof(exePath));
    if(!exeLen||exeLen>=_countof(exePath))return;
    wchar_t *slash=wcsrchr(exePath,L'\\');
    if(!slash)return;
    *slash=0;
    if(FAILED(StringCchCopyW(presetPath,_countof(presetPath),exePath))||
       FAILED(StringCchCatW(presetPath,_countof(presetPath),L"\\assets\\visualizer\\presets"))||
       FAILED(StringCchCopyW(texturePath,_countof(texturePath),exePath))||
       FAILED(StringCchCatW(texturePath,_countof(texturePath),L"\\assets\\visualizer\\textures")))return;
    ABM_ProjectM_Create(hwnd,presetPath,texturePath);
}
static void DrawVisualizer(HDC dc,RECT area) {
    float levels[24]={0};
    ABM_AudioVisualizer_GetBands(levels,24);
    int width=area.right-area.left,height=area.bottom-area.top;
    if(width<160||height<120)return;

    /* Each mode is a separate composition. Only the FFT snapshot, clock and
       drawing surface are shared; no common particle/ribbon/halo overlay is
       painted across modes. */
    const COLORREF ink=RGB(3,7,18);
    const COLORREF cyan=RGB(40,218,235);
    const COLORREF blue=RGB(55,112,255);
    const COLORREF violet=RGB(158,82,255);
    const COLORREF pink=RGB(255,74,164);
    const COLORREF mint=RGB(70,245,190);
    const COLORREF gold=RGB(255,190,76);
    const float scale=(float)height/1080.0f;
    const float s=scale<0.72f?0.72f:(scale>2.2f?2.2f:scale);
    const float time=(float)g_visualizerFrame;
    const float pi=3.14159265358979323846f;
    int cx=area.left+width/2,cy=area.top+height/2;
    float bass=(levels[0]+levels[1]+levels[2]+levels[3])*0.25f;
    float mid=0.0f,treble=0.0f;
    for(int i=4;i<16;i++)mid+=levels[i]/12.0f;
    for(int i=16;i<24;i++)treble+=levels[i]/8.0f;

    if(g_visualizerMode==0) {
        /* SPECTRUM: layered, elliptical 3-D frequency architecture. Four
           independently displaced spectral contours form a rotating crown. */
        GradientRect(dc,area,RGB(3,8,24),RGB(15,7,34));
        ABM_D2D_GradientRound(&g_ui,(float)cx-width*0.27f,(float)cy-height*0.28f,
            (float)cx+width*0.27f,(float)cy+height*0.28f,height*0.20f,
            RGB(15,54,103),ink);
        const int pointsCount=240;
        for(int ring=3;ring>=0;ring--) {
            float xy[512*2];
            float base=(float)(height<width?height:width)*(0.145f+ring*0.075f);
            float phase=time*(0.0012f+ring*0.00019f)+ring*0.42f;
            for(int n=0;n<=pointsCount;n++) {
                float a=(float)n*2.0f*pi/(float)pointsCount;
                float bandPos=(float)n*24.0f/(float)pointsCount;
                int band=(int)bandPos;if(band>23)band=23;
                int next=band<23?band+1:23;
                float frac=bandPos-(float)band;
                float v=levels[band]*(1.0f-frac)+levels[next]*frac;
                float harmonic=0.5f+0.5f*sinf(a*6.0f+phase*2.0f+ring);
                float r=base+v*(height*(0.105f+ring*0.012f))+
                    harmonic*height*(0.012f+treble*0.018f);
                float wobble=sinf(a*3.0f-phase)*height*(0.008f+bass*0.018f);
                xy[n*2]=(float)cx+cosf(a+phase*0.18f)*r;
                xy[n*2+1]=(float)cy+sinf(a+phase*0.18f)*r*0.62f+wobble;
            }
            COLORREF c=ring==0?cyan:(ring==1?blue:(ring==2?violet:pink));
            ABM_D2D_Polyline(&g_ui,xy,pointsCount+1,Blend(ink,c,ring==0?45:25),(ring==0?5.0f:3.0f)*s);
            ABM_D2D_Polyline(&g_ui,xy,pointsCount+1,c,(ring==0?1.9f:1.05f)*s);
        }
        /* 120 finely spaced spectral pillars. The capture engine supplies
           24 logarithmic FFT bands; interpolate between them for a smooth,
           dense radial spectrum without adding work to the audio thread. */
        for(int bar=0;bar<120;bar++) {
            float bandPos=(float)bar*23.0f/119.0f;
            int band=(int)bandPos;if(band>23)band=23;
            int next=band<23?band+1:23;
            float frac=bandPos-(float)band;
            float v=levels[band]*(1.0f-frac)+levels[next]*frac;
            float a=(float)bar*2.0f*pi/120.0f-time*0.0014f;
            float harmonic=0.5f+0.5f*sinf(a*18.0f+time*0.002f);
            float r0=(float)(height<width?height:width)*0.105f;
            float r1=r0+(float)(height<width?height:width)*
                (0.055f+v*0.31f+harmonic*treble*0.018f);
            float x0=(float)cx+cosf(a)*r0,y0=(float)cy+sinf(a)*r0*0.62f;
            float x1=(float)cx+cosf(a)*r1,y1=(float)cy+sinf(a)*r1*0.62f;
            COLORREF c=band<5?gold:(band<13?mint:(band<19?cyan:violet));
            ABM_D2D_Line(&g_ui,x0,y0,x1,y1,Blend(ink,c,24),2.6f*s);
            ABM_D2D_Line(&g_ui,x0,y0,x1,y1,c,(bar%5==0?1.35f:0.85f)*s);
        }
        /* Central optical core and fine elliptical depth guides. */
        for(int r=3;r>=0;r--) {
            float rw=(float)(height<width?height:width)*(0.075f+r*0.026f);
            ABM_D2D_StrokeRound(&g_ui,cx-rw,cy-rw*0.62f,cx+rw,cy+rw*0.62f,
                rw*0.55f,Blend(ink,r==0?cyan:blue,18+r*4), (r==0?2.0f:0.8f)*s);
        }
        ABM_D2D_GradientRound(&g_ui,cx-7*s,cy-7*s,cx+7*s,cy+7*s,7*s,
            RGB(205,255,255),RGB(36,150,255));
    } else if(g_visualizerMode==1) {
        /* FLUID WAVES: layered continuous liquid sheets. Each ribbon has its
           own phase, frequency response and vertical flow; no radial spectrum
           or particle field is reused from the other modes. */
        GradientRect(dc,area,RGB(2,13,25),RGB(7,8,29));
        ABM_D2D_GradientRound(&g_ui,(float)area.left+width*0.12f,(float)area.top+height*0.16f,
            (float)area.right-width*0.12f,(float)area.bottom-height*0.08f,height*0.30f,
            RGB(8,65,88),ink);
        const int samples=320;
        for(int layer=8;layer>=0;layer--) {
            float xy[512*2];
            float pLayer=(float)layer/8.0f;
            float baseline=(float)cy+(pLayer-0.5f)*height*0.50f;
            float amp=height*(0.045f+pLayer*0.025f);
            for(int n=0;n<=samples;n++) {
                float p=(float)n/(float)samples;
                float bandPos=p*23.0f;
                int band=(int)bandPos;if(band>23)band=23;
                int next=band<23?band+1:23;
                float frac=bandPos-(float)band;
                float v=levels[band]*(1.0f-frac)+levels[next]*frac;
                float envelope=0.24f+0.76f*sinf(p*pi);
                float flow=p*(7.0f+layer*0.31f)-time*(0.008f+layer*0.00065f)+layer*0.83f;
                float swell=sinf(flow)*amp*envelope*(0.30f+mid*0.75f+v*1.75f);
                float detail=sinf(flow*2.35f+time*0.002f)*amp*0.22f*(0.25f+treble);
                float undertow=cosf(p*18.0f-time*0.006f+layer)*height*(0.012f+bass*0.026f);
                xy[n*2]=(float)area.left+p*(float)width;
                xy[n*2+1]=baseline+swell+detail+undertow;
            }
            COLORREF c=layer<3?mint:(layer<6?cyan:(layer==6?blue:violet));
            ABM_D2D_Polyline(&g_ui,xy,samples+1,Blend(ink,c,layer<3?35:20),(layer<3?6.0f:3.4f)*s);
            ABM_D2D_Polyline(&g_ui,xy,samples+1,c,(layer<3?1.8f:0.95f)*s);
        }
        /* Cross-current filaments create a woven, fluid interference field. */
        for(int strand=0;strand<4;strand++) {
            float xy[512*2];
            for(int n=0;n<=samples;n++) {
                float p=(float)n/(float)samples;
                float a=p*10.0f+time*(0.005f+strand*0.0008f)+strand*1.7f;
                float band=(float)((strand*5+(int)(p*23.0f))%24);
                float v=levels[(int)band];
                float y=(float)cy+sinf(a)*height*(0.10f+v*0.14f)+
                    cosf(a*0.43f-time*0.003f)*height*0.08f;
                xy[n*2]=(float)area.left+p*(float)width;
                xy[n*2+1]=y;
            }
            ABM_D2D_Polyline(&g_ui,xy,samples+1,Blend(ink,strand&1?violet:blue,22),1.15f*s);
        }
        /* A quiet luminous horizon anchors the liquid volume. */
        ABM_D2D_Line(&g_ui,(float)area.left,(float)cy+height*0.31f,
            (float)area.right,(float)cy+height*0.31f,Blend(ink,cyan,18),1.0f*s);
    } else if(g_visualizerMode==2) {
        /* PARTICLE UNIVERSE: a perspective star tunnel with a fixed vanishing
           point. Depth, apparent size, trails and turbulence are derived from
           each particle's Z position and the live frequency bands. */
        GradientRect(dc,area,RGB(1,3,13),RGB(7,8,27));
        ABM_D2D_GradientRound(&g_ui,cx-width*0.19f,cy-height*0.19f,
            cx+width*0.19f,cy+height*0.19f,height*0.16f,
            RGB(30,38,112),ink);
        const float vanishX=(float)cx;
        const float vanishY=(float)cy-height*0.025f;
        /* Long perspective streaks converge into the centre before the
           particles cross the near plane. */
        for(int ray=0;ray<36;ray++) {
            float a=(float)ray*2.0f*pi/36.0f;
            float xy[4];
            float r0=(float)(height<width?height:width)*0.035f;
            float r1=(float)(height<width?height:width)*0.73f;
            xy[0]=vanishX+cosf(a)*r0;xy[1]=vanishY+sinf(a)*r0;
            xy[2]=vanishX+cosf(a)*r1;xy[3]=vanishY+sinf(a)*r1;
            ABM_D2D_Polyline(&g_ui,xy,2,Blend(ink,(ray%3==0)?violet:blue,12),0.8f*s);
        }
        /* A compact set of audio-modulated trails; particles themselves are
           projected independently so the scene reads as a 3-D volume. */
        for(int i=0;i<42;i++) {
            float seed=(float)((i*73+19)%997)/997.0f;
            float z=fmodf(seed+time*0.00072f,1.0f);
            float nx=(float)((i*193+41)%991)/991.0f-0.5f;
            float ny=(float)((i*317+83)%983)/983.0f-0.5f;
            int band=(i*7)%24;
            float persp=0.13f+z*1.95f;
            float x=vanishX+nx*width*0.34f*persp;
            float y=vanishY+ny*height*0.34f*persp;
            float tailZ=z>0.10f?z-0.10f:0.0f;
            float tailScale=0.13f+tailZ*1.95f;
            float xy[4]={vanishX+nx*width*0.34f*tailScale,
                vanishY+ny*height*0.34f*tailScale,x,y};
            COLORREF c=band<5?gold:(band<13?cyan:(band<19?violet:pink));
            ABM_D2D_Polyline(&g_ui,xy,2,Blend(ink,c,35), (1.0f+z*2.0f)*s);
        }
        for(int i=0;i<260;i++) {
            float seed=(float)((i*197+31)%997)/997.0f;
            float z=fmodf(seed+time*(0.00048f+(i%5)*0.000055f),1.0f);
            float nx=(float)((i*193+41)%991)/991.0f-0.5f;
            float ny=(float)((i*317+83)%983)/983.0f-0.5f;
            int band=(i*11)%24;float v=levels[band];
            float turbulence=sinf(time*0.006f+i*1.37f)*0.018f*(bass+v);
            float persp=0.13f+z*1.95f;
            float x=vanishX+(nx+turbulence)*width*0.34f*persp;
            float y=vanishY+(ny+cosf(time*0.005f+i)*0.018f*(mid+v))*height*0.34f*persp;
            float radius=(0.7f+z*3.1f+v*2.2f)*s;
            COLORREF c=band<5?gold:(band<13?cyan:(band<19?violet:pink));
            if(i%7==0)ABM_D2D_Round(&g_ui,x-radius*2.8f,y-radius*2.8f,
                x+radius*2.8f,y+radius*2.8f,radius*2.8f,Blend(ink,c,24));
            ABM_D2D_Round(&g_ui,x-radius,y-radius,x+radius,y+radius,radius,c);
        }
        /* Bass pulses distort the event horizon rather than adding another
           generic audio ring to the other two visualizer styles. */
        float pulse=(float)(height<width?height:width)*(0.025f+bass*0.035f);
        ABM_D2D_StrokeRound(&g_ui,cx-pulse,cy-pulse,cx+pulse,cy+pulse,
            pulse*0.8f,Blend(ink,cyan,34),1.5f*s);
    } else {
        /* NEON SPECTRUM: a high-density equalizer with frequency colour,
           reflected energy and slowly-decaying peak markers. This mode uses
           the same lightweight 24-band FFT snapshot as the other compositions. */
        GradientRect(dc,area,RGB(2,9,19),RGB(8,12,31));
        ABM_D2D_GradientRound(&g_ui,(float)area.left+width*0.08f,
            (float)area.top+height*0.08f,(float)area.right-width*0.08f,
            (float)area.bottom-height*0.05f,height*0.28f,
            RGB(10,58,77),ink);
        for(int grid=1;grid<=3;grid++) {
            float gy=(float)area.bottom-height*(0.16f+grid*0.19f);
            ABM_D2D_Line(&g_ui,(float)area.left+width*0.045f,gy,
                (float)area.right-width*0.045f,gy,Blend(ink,blue,10),0.7f*s);
        }
        const int bars=64;
        float slot=(float)width/(float)bars;
        float barW=slot*0.62f;
        float baseline=(float)area.bottom-height*0.16f;
        for(int i=0;i<bars;i++) {
            float bandPos=(float)i*23.0f/(float)(bars-1);
            int band=(int)bandPos;if(band>23)band=23;
            int next=band<23?band+1:23;
            float frac=bandPos-(float)band;
            float v=levels[band]*(1.0f-frac)+levels[next]*frac;
            v=sqrtf((v<0.0f)?0.0f:v);
            float h=height*(0.025f+v*0.57f);
            if(h>height*0.70f)h=height*0.70f;
            if(v>g_visualizerBarPeaks[i])g_visualizerBarPeaks[i]=v;
            else g_visualizerBarPeaks[i]=(g_visualizerBarPeaks[i]-0.012f)>0.0f?
                g_visualizerBarPeaks[i]-0.012f:0.0f;
            float x=(float)area.left+slot*(float)i+(slot-barW)*0.5f;
            float top=baseline-h;
            COLORREF col=band<4?gold:(band<10?mint:(band<17?cyan:(band<21?blue:violet)));
            ABM_D2D_Round(&g_ui,x-2.2f*s,top-2.2f*s,x+barW+2.2f*s,
                baseline+1.0f*s,4.0f*s,Blend(ink,col,16));
            ABM_D2D_GradientRound(&g_ui,x,top,x+barW,baseline,2.8f*s,
                Blend(ink,col,68),col);
            /* Soft mirror below the baseline adds depth without clutter. */
            float reflection=h*0.20f;
            ABM_D2D_GradientRound(&g_ui,x,baseline+3.0f*s,x+barW,
                baseline+3.0f*s+reflection,2.0f*s,
                Blend(ink,col,15),ink);
            float peakY=baseline-height*(0.025f+g_visualizerBarPeaks[i]*0.57f);
            ABM_D2D_Line(&g_ui,x-1.0f*s,peakY,x+barW+1.0f*s,peakY,
                Blend(ink,col,82),1.8f*s);
        }
        ABM_D2D_Line(&g_ui,(float)area.left+width*0.04f,baseline,
            (float)area.right-width*0.04f,baseline,Blend(ink,mint,30),1.0f*s);
    }

    /* Compact now-playing identity stays visible without competing with the
       full-bleed visual field. Artwork and metadata are already cached by the
       player; this overlay performs no file or network work during painting. */
    if(g_artworkLoaded || g_trackTitle[0] || g_trackArtist[0]) {
        int pad=(int)(22.0f*s), art=(int)(54.0f*s);
        int cardH=(int)(82.0f*s), cardW=(int)(width*0.38f);
        int maxCardW=(int)(590.0f*s);
        if(cardW>maxCardW)cardW=maxCardW;
        if(cardW>(width-pad*2))cardW=width-pad*2;
        if(cardW>art+pad*3 && cardH>0) {
            int left=area.left+pad, bottom=area.bottom-pad;
            int top=bottom-cardH;
            ABM_D2D_GradientRound(&g_ui,(float)left,(float)top,
                (float)(left+cardW),(float)bottom,12.0f*s,
                RGB(13,25,42),RGB(3,8,18));
            ABM_D2D_StrokeRound(&g_ui,(float)left,(float)top,
                (float)(left+cardW),(float)bottom,12.0f*s,
                Blend(ink,cyan,24),1.0f*s);
            int artX=left+pad,artY=top+(cardH-art)/2;
            if(g_artworkLoaded)
                ABM_D2D_DrawImageCover(&g_ui,(float)artX,(float)artY,
                    (float)(artX+art),(float)(artY+art),1.0f);
            else
                ABM_D2D_GradientRound(&g_ui,(float)artX,(float)artY,
                    (float)(artX+art),(float)(artY+art),7.0f*s,
                    RGB(28,106,126),RGB(39,48,108));
            int textX=artX+art+pad/2;
            RECT label={textX,top+pad/2,left+cardW-pad,bottom-pad/2};
            RECT kicker=label,title=label,artist=label;
            kicker.bottom=kicker.top+(int)(17.0f*s);
            title.top=kicker.bottom;title.bottom=title.top+(int)(27.0f*s);
            artist.top=title.bottom;artist.bottom=bottom-pad/2;
            Txt(dc,L"NOW PLAYING",kicker,Blend(ink,mint,90),g_smallFont,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            Txt(dc,g_trackTitle[0]?g_trackTitle:L"Untitled track",title,
                RGB(245,249,255),g_headingFont,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            Txt(dc,g_trackArtist[0]?g_trackArtist:L"Unknown artist",artist,
                RGB(165,184,204),g_smallFont,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
    }
}
static void DrawUI(HDC dc,RECT client) {
    if(!g_visualizerActive || g_visualizerMode!=4 || !ABM_ProjectM_IsReady()) ABM_ProjectM_SetBounds(NULL,FALSE);
    if(g_fullscreen) {
        /* When audio visualization is active it owns the entire client area.
           Artwork/metadata layouts must not constrain it to a short footer strip. */
        if(g_visualizerActive) {
            if(g_visualizerMode==4 && ABM_ProjectM_IsReady()) ABM_ProjectM_SetBounds(&client,TRUE);
            else { ABM_ProjectM_SetBounds(NULL,FALSE); DrawVisualizer(dc,client); }
            return;
        }
        /* Immersive now-playing view: artwork, track metadata and transport
           controls are composed as one screen rather than a centered cover. */
        GradientRect(dc,client,ABM_THEME_COLOR_070F15,ABM_THEME_COLOR_0C1419);
        RECT ambientTop={0,0,client.right,client.bottom/2};
        GradientRect(dc,ambientTop,ABM_THEME_COLOR_0A2327,ABM_THEME_COLOR_081017);
        if(g_artworkLoaded) {
            int artSize=client.bottom-270;
            if(artSize>500)artSize=500;
            if(artSize>client.right/2-90)artSize=client.right/2-90;
            if(artSize<250)artSize=250;
            int artX=(client.right/2-artSize)/2;
            int artY=(client.bottom-170-artSize)/2+12;
            RECT tile={artX,artY,artX+artSize,artY+artSize};
            RECT tileShadow=tile;OffsetRect(&tileShadow,0,14);RoundBox(dc,tileShadow,ABM_THEME_COLOR_03060D,22);
            GradientRound(dc,tile,ABM_THEME_COLOR_12373B,ABM_THEME_COLOR_22303B,22);
            ABM_D2D_StrokeRound(&g_ui,(float)tile.left,(float)tile.top,(float)tile.right,(float)tile.bottom,22.0f,ABM_THEME_COLOR_53978F,1.25f);
            ABM_D2D_DrawImageCover(&g_ui,(float)tile.left+7,(float)tile.top+7,
                (float)tile.right-7,(float)tile.bottom-7,(float)g_artworkFade/100.0f);

            int infoX=client.right/2+34;
            int infoW=client.right-infoX-62;
            RECT kicker={infoX,artY+4,infoX+infoW,artY+34};
            RECT title={infoX,artY+50,infoX+infoW,artY+105};
            RECT artist={infoX,artY+112,infoX+infoW,artY+145};
            Txt(dc,L"ABM  /  NOW PLAYING",kicker,ABM_THEME_COLOR_68C2B1,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            Txt(dc,g_trackTitle[0]?g_trackTitle:L"Untitled track",title,C_TEXT,g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            Txt(dc,g_trackArtist[0]?g_trackArtist:L"Unknown artist",artist,ABM_THEME_COLOR_C2C6CC,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

            int cardY=artY+184;
            RECT artistCard={infoX,cardY,infoX+infoW,cardY+112};
            Panel(dc,artistCard,ABM_THEME_COLOR_0D1D2E,ABM_THEME_COLOR_305B7D,16);
            RECT artistHead={infoX+18,cardY+12,infoX+infoW-18,cardY+37};
            RECT artistName={infoX+18,cardY+45,infoX+infoW-18,cardY+72};
            RECT artistRole={infoX+18,cardY+75,infoX+infoW-18,cardY+98};
            Txt(dc,L"ARTIST",artistHead,ABM_THEME_COLOR_66C0D1,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            Txt(dc,g_trackArtist[0]?g_trackArtist:L"Artist information unavailable",artistName,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            Txt(dc,L"Artist",artistRole,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

            RECT credits={infoX,cardY+128,infoX+infoW,cardY+242};
            Panel(dc,credits,ABM_THEME_COLOR_0D1D2E,ABM_THEME_COLOR_305B7D,16);
            RECT creditsHead={infoX+18,credits.top+12,infoX+infoW-18,credits.top+37};
            Txt(dc,L"TRACK INFORMATION",creditsHead,ABM_THEME_COLOR_66C0D1,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT albumLabel={infoX+18,credits.top+45,infoX+110,credits.top+67};
            RECT albumValue={infoX+118,credits.top+45,infoX+infoW-18,credits.top+67};
            RECT genreLabel={infoX+18,credits.top+73,infoX+110,credits.top+95};
            RECT genreValue={infoX+118,credits.top+73,infoX+infoW-18,credits.top+95};
            Txt(dc,L"Album",albumLabel,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            Txt(dc,g_trackAlbum[0]?g_trackAlbum:L"Not provided",albumValue,C_TEXT,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            Txt(dc,L"Genre",genreLabel,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            Txt(dc,g_trackGenre[0]?g_trackGenre:L"Not provided",genreValue,C_TEXT,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            if(g_visualizerActive) {
                RECT visual={client.right/2+34,client.bottom-178,client.right-42,client.bottom-72};
                DrawVisualizer(dc,visual);
            }
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
    GradientRect(dc,header,ABM_THEME_COLOR_0D1B2D,ABM_THEME_COLOR_050A13);
    RECT headerShade={0,82,client.right,96};
    GradientRect(dc,headerShade,ABM_THEME_COLOR_08111D,ABM_THEME_COLOR_050A12);
    Fill(dc,(RECT){0,95,client.right,96},ABM_THEME_COLOR_273D53);
    /* A restrained moving light sweep gives the top bar a little life without
       competing with the video or increasing the media-rendering workload. */
    Fill(dc,(RECT){0,92,client.right,95},ABM_THEME_COLOR_0D1827);
    int sweepWidth=112;
    int sweepX=(int)((g_visualPulse*3u)%(unsigned int)(client.right+sweepWidth))-sweepWidth;
    RECT sweep={sweepX,92,sweepX+sweepWidth,95};
    GradientRect(dc,sweep,ABM_THEME_COLOR_2DA9A6,ABM_THEME_COLOR_69BEA4);
    /* Signature mark: a luminous A-monogram fused with a play glyph. */
    RECT logoHalo={20,7,102,87};
    Panel(dc,logoHalo,ABM_THEME_COLOR_0D1C2D,ABM_THEME_COLOR_3F97B4,22);
    RECT logo={28,14,94,80};
    GradientRound(dc,logo,ABM_THEME_COLOR_1C9D9D,ABM_THEME_COLOR_34777E,17);
    RECT logoInset={33,19,89,75};
    ABM_D2D_StrokeRound(&g_ui,(float)logoInset.left,(float)logoInset.top,
        (float)logoInset.right,(float)logoInset.bottom,14.0f,ABM_THEME_COLOR_BEE5FF,0.8f);
    /* Geometric A: crisp strokes remain legible at small window sizes. */
    DrawLine(dc,42,63,59,30,ABM_THEME_COLOR_FFFFFF,3);
    DrawLine(dc,59,30,76,63,ABM_THEME_COLOR_FFFFFF,3);
    DrawLine(dc,49,49,68,49,ABM_THEME_COLOR_E2F5FF,2);
    /* Play triangle, offset into the A to make the mark distinctive. */
    DrawLine(dc,62,40,75,48,ABM_THEME_COLOR_FFFFFF,2);
    DrawLine(dc,75,48,62,56,ABM_THEME_COLOR_FFFFFF,2);
    DrawLine(dc,62,56,62,40,ABM_THEME_COLOR_FFFFFF,2);

    RECT brandMain={116,8,420,49};
    Txt(dc,L"ABM",brandMain,ABM_THEME_COLOR_F7FAFF,g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT brandSuffix={188,20,390,48};
    Txt(dc,L"AV  PLAYER",brandSuffix,ABM_THEME_COLOR_84CDC2,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT brandRule={116,52,354,54};
    GradientRect(dc,brandRule,ABM_THEME_COLOR_36B1A8,ABM_THEME_COLOR_70BE9D);
    RECT brandSubtitle={116,60,510,83};
    Txt(dc,L"YOUR MUSIC  ·  YOUR MOVIES  ·  YOUR SPACE",brandSubtitle,ABM_THEME_COLOR_9DB1C9,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT menuBtn=R_MENU_BUTTON;
    POINT chromeCursor={0,0}; BOOL menuHot=GetCursorPos(&chromeCursor)&&ScreenToClient(g_main,&chromeCursor)&&Inside(menuBtn,chromeCursor.x,chromeCursor.y);
    BOOL drawerHot=GetCursorPos(&chromeCursor)&&ScreenToClient(g_main,&chromeCursor)&&Inside(R_DRAWER_BUTTON,chromeCursor.x,chromeCursor.y);
    Panel(dc,menuBtn,g_menuOpen?ABM_THEME_COLOR_204E5B:Blend(ABM_THEME_COLOR_111D2F,ABM_THEME_COLOR_243954,menuHot?100:0),
        g_menuOpen?ABM_THEME_COLOR_5CA5B6:(menuHot?ABM_THEME_COLOR_5084BF:ABM_THEME_COLOR_2F4562),15);
    /* Keep the hamburger icon in its own compact column, clear of the label. */
    for(int i=0;i<3;i++)DrawLine(dc,menuBtn.left+12,menuBtn.top+13+i*7,menuBtn.left+29,menuBtn.top+13+i*7,ABM_THEME_COLOR_E2ECF9,2);
    RECT menuLabel={menuBtn.left+39,menuBtn.top+3,menuBtn.right-7,menuBtn.bottom-3};
    Txt(dc,L"MENU",menuLabel,ABM_THEME_COLOR_E0EBF9,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT drawerBtn=R_DRAWER_BUTTON;
    Panel(dc,drawerBtn,g_drawerOpen?ABM_THEME_COLOR_204E84:Blend(ABM_THEME_COLOR_111D2F,ABM_THEME_COLOR_243954,drawerHot?100:0),g_drawerOpen?ABM_THEME_COLOR_5CA5F2:(drawerHot?ABM_THEME_COLOR_5084BF:ABM_THEME_COLOR_2F4562),15);
    DrawLine(dc,drawerBtn.left+13,drawerBtn.top+15,drawerBtn.left+29,drawerBtn.top+15,ABM_THEME_COLOR_DCEBFC,2);
    DrawLine(dc,drawerBtn.left+13,drawerBtn.top+22,drawerBtn.left+29,drawerBtn.top+22,ABM_THEME_COLOR_DCEBFC,2);
    DrawLine(dc,drawerBtn.left+13,drawerBtn.top+29,drawerBtn.left+29,drawerBtn.top+29,ABM_THEME_COLOR_DCEBFC,2);
    RECT drawerLabel={drawerBtn.left+36,drawerBtn.top+3,drawerBtn.right-8,drawerBtn.bottom-3};
    Txt(dc,L"MEDIA",drawerLabel,ABM_THEME_COLOR_E0EBF9,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT status={client.right-540,26,client.right-300,64};
    Panel(dc,status,ABM_THEME_COLOR_0C1827,ABM_THEME_COLOR_35748F,18);
    RECT led={status.left+13,status.top+14,status.left+21,status.top+22};RoundBox(dc,led,g_hasMedia?ABM_THEME_COLOR_3FCD91:ABM_THEME_COLOR_64758B,5);
    RECT statusText={status.left+29,status.top+2,status.right-8,status.bottom-2};
    Txt(dc,g_statusText[0]?g_statusText:(g_hasMedia?L"READY TO PLAY":L"LOCAL PLAYER"),statusText,C_TEXT,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

    /* Video surface is a separate LibVLC child window. Keep the surrounding frame elegant. */
    RECT frame=R_VIDEO;InflateRect(&frame,3,3);
    int glow=(int)(14.0+5.0*(0.5+0.5*sin((double)g_visualPulse*0.045)));
    COLORREF frameEdge=Blend(ABM_THEME_COLOR_1E3035,ABM_THEME_COLOR_377F75,glow);
    Panel(dc,frame,ABM_THEME_COLOR_060A11,frameEdge,20);
    GradientRect(dc,R_VIDEO,ABM_THEME_COLOR_070F1A,ABM_THEME_COLOR_04080F);
    if(g_visualizerActive) {
        if(g_visualizerMode==4 && ABM_ProjectM_IsReady()) ABM_ProjectM_SetBounds(&R_VIDEO,TRUE);
        else { ABM_ProjectM_SetBounds(NULL,FALSE); DrawVisualizer(dc,R_VIDEO); }
    } else if(g_artworkLoaded) {
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
        Panel(dc,tile,ABM_THEME_COLOR_0D1420,ABM_THEME_COLOR_4877B2,19);
        ABM_D2D_DrawImageCover(&g_ui,(float)tile.left+8,(float)tile.top+8,
            (float)tile.right-8,(float)tile.bottom-8,(float)g_artworkFade/100.0f);

        int infoX=tile.right+54;
        RECT kicker={infoX,R_VIDEO.top+areaH/2-116,R_VIDEO.right-42,R_VIDEO.top+areaH/2-88};
        RECT title={infoX,R_VIDEO.top+areaH/2-76,R_VIDEO.right-42,R_VIDEO.top+areaH/2-25};
        RECT artist={infoX,R_VIDEO.top+areaH/2-17,R_VIDEO.right-42,R_VIDEO.top+areaH/2+16};
        RECT detail={infoX,R_VIDEO.top+areaH/2+38,R_VIDEO.right-42,R_VIDEO.top+areaH/2+64};
        Txt(dc,L"NOW PLAYING  /  ABM AUDIO",kicker,ABM_THEME_COLOR_69BEB1,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if(g_trackTitle[0]) Txt(dc,g_trackTitle,title,C_TEXT,g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        else Txt(dc,L"Untitled track",title,C_TEXT,g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if(g_trackArtist[0]) Txt(dc,g_trackArtist,artist,ABM_THEME_COLOR_BFCFE5,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        else Txt(dc,L"Your personal listening session",artist,ABM_THEME_COLOR_BFCFE5,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        int64_t trackLength=g_player?libvlc_media_player_get_length(g_player):0;
        wchar_t duration[48]; FormatTime((int)trackLength,duration,48);
        Txt(dc,duration,detail,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        int waveX=infoX, waveY=detail.bottom+32, waveW=R_VIDEO.right-infoX-48;
        RECT visualStrip={waveX,waveY-42,waveX+waveW,waveY+46};
        if(g_visualizerActive)DrawVisualizer(dc,visualStrip);
        else { int bars=36,gap=5,bw=(waveW-gap*(bars-1))/bars;if(bw<2)bw=2;
            for(int i=0;i<bars;i++){double pulse=0.22+0.78*(0.5+0.5*sin((double)g_visualPulse*0.095+i*0.47));int bh=5+(int)(pulse*25.0);RECT bar={waveX+i*(bw+gap),waveY-bh/2,waveX+i*(bw+gap)+bw,waveY+bh/2};COLORREF col=Blend(ABM_THEME_COLOR_2B9791,ABM_THEME_COLOR_6CB897,i*100/(bars-1));RoundBox(dc,bar,col,3);} }
    }
    if(!g_hasMedia) {
        int areaW=R_VIDEO.right-R_VIDEO.left, areaH=R_VIDEO.bottom-R_VIDEO.top;
        int cy=(R_VIDEO.top+R_VIDEO.bottom)/2;
        /* Ambient colour fields make the stage feel like a listening space,
           not an empty black video rectangle. */
        /* Keep the copy in a dedicated left column. The teal hero panel
           begins farther right so the text never crosses its rounded edge. */
        RECT ambient={R_VIDEO.left+areaW*36/100,R_VIDEO.top+areaH/6,R_VIDEO.right-areaW/8,R_VIDEO.bottom-areaH/7};
        GradientRound(dc,ambient,ABM_THEME_COLOR_0A2327,ABM_THEME_COLOR_111B22,34);
        RECT glowLine={ambient.left+26,ambient.top+1,ambient.right-26,ambient.top+3};
        GradientRect(dc,glowLine,ABM_THEME_COLOR_2FAB9E,ABM_THEME_COLOR_70BB96);
        int leftX=R_VIDEO.left+areaW*7/100;
        int textW=areaW*27/100;
        int topY=cy-112;
        RECT eyebrowPill={leftX,topY+5,leftX+8,topY+13};
        RoundBox(dc,eyebrowPill,ABM_THEME_COLOR_4BD3BE,4);
        Txt(dc,L"THE ABM LISTENING EXPERIENCE",(RECT){leftX+18,topY,leftX+textW,topY+25},ABM_THEME_COLOR_78BECE,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT headline1={leftX,topY+42,leftX+textW,topY+91};
        RECT headline2={leftX,topY+88,leftX+textW,topY+137};
        Txt(dc,L"Capture the feeling.",headline1,ABM_THEME_COLOR_F7F9FF,g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        Txt(dc,L"Relive every moment.",headline2,ABM_THEME_COLOR_7EC7B5,g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT detail={leftX,topY+151,leftX+textW,topY+194};
        Txt(dc,L"A refined home for music, movies and",detail,ABM_THEME_COLOR_B0C0D5,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT detail2={leftX,topY+177,leftX+textW,topY+218};
        Txt(dc,L"everything you love to watch and hear.",detail2,ABM_THEME_COLOR_B0C0D5,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT hint={leftX,topY+237,leftX+textW,topY+263};
        Txt(dc,L"ADD MEDIA TO BEGIN  →",hint,ABM_THEME_COLOR_70C3B3,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        /* Action-led home dashboard: four real destinations replace the
           decorative album stack. Each card has a generous hit target and
           remains visible only while the player is idle. */
        {
            RECT cards[4]={R_HOME_OPEN,R_HOME_STREAM,R_HOME_TORRENT,R_HOME_RECENT};
            const wchar_t *titles[4]={L"Open media",L"Streaming Centre",L"Torrent Stream",L"Home Theatre"};
            const wchar_t *subs[4]={L"Browse files and folders",L"Your streaming services",L"Play from a torrent",L"Movies, shows and artwork"};
            const wchar_t *glyphs[4]={L"+",L"TV",L"↗",L"▶"};
            for(int i=0;i<4;i++) {
                int motion=g_homeCardMotion[i];if(motion<0)motion=0;if(motion>100)motion=100;
                RECT card=cards[i];OffsetRect(&card,0,-(motion*3)/100);
                COLORREF cardEdge=Blend((i==0)?ABM_THEME_COLOR_449A8F:ABM_THEME_COLOR_334F65,ABM_THEME_COLOR_68C9B8,motion);
                RECT shadow=card;OffsetRect(&shadow,0,5);RoundBox(dc,shadow,ABM_THEME_COLOR_03080E,13);
                if(i==0)GradientRound(dc,card,Blend(ABM_THEME_COLOR_197770,ABM_THEME_COLOR_25998B,motion),ABM_THEME_COLOR_184B52,13);
                else GradientRound(dc,card,Blend(ABM_THEME_COLOR_142535,ABM_THEME_COLOR_1D3A4C,motion),ABM_THEME_COLOR_0D1927,13);
                ABM_D2D_StrokeRound(&g_ui,(float)card.left,(float)card.top,(float)card.right,(float)card.bottom,13.0f,cardEdge,0.85f+(float)motion*0.005f);
                int cardW=card.right-card.left, cardH=card.bottom-card.top;
                int iconSize=cardW<180?30:36, iconLeft=card.left+(cardW<180?10:12);
                RECT icon={iconLeft,card.top+(cardH-iconSize)/2,iconLeft+iconSize,card.top+(cardH+iconSize)/2};
                RoundBox(dc,icon,Blend((i==0)?ABM_THEME_COLOR_279D8F:ABM_THEME_COLOR_1D3847,ABM_THEME_COLOR_2CAB9A,motion),9);
                Txt(dc,glyphs[i],icon,ABM_THEME_COLOR_E2F7F5,g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                int textLeft=icon.right+10;
                int titleTop=cardH>=70?10:6, titleBottom=cardH>=70?39:cardH/2+2;
                int subTop=cardH>=70?38:cardH/2, subBottom=cardH>=70?cardH-8:cardH-5;
                RECT title={textLeft,card.top+titleTop,card.right-8,card.top+titleBottom};
                RECT cardSubtitle={textLeft,card.top+subTop,card.right-8,card.top+subBottom};
                Txt(dc,titles[i],title,ABM_THEME_COLOR_F4F9FF,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                Txt(dc,subs[i],cardSubtitle,ABM_THEME_COLOR_9EB5C9,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            }
            int footGap=12, footHeight=20;
            RECT foot={R_HOME_OPEN.left,R_HOME_RECENT.bottom+footGap,R_HOME_RECENT.right,R_HOME_RECENT.bottom+footGap+footHeight};
            Txt(dc,L"LOCAL MEDIA   •   TMDB METADATA   •   HOME THEATRE",foot,ABM_THEME_COLOR_77A9A9,g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
    }
    RECT fs=R_FULLSCREEN;
    RoundBox(dc,fs,ABM_THEME_COLOR_141D2A,8);
    DrawLine(dc,fs.left+9,fs.top+13,fs.left+9,fs.top+7,C_TEXT,2);
    DrawLine(dc,fs.left+9,fs.top+7,fs.left+15,fs.top+7,C_TEXT,2);
    DrawLine(dc,fs.right-9,fs.top+13,fs.right-9,fs.top+7,C_TEXT,2);
    DrawLine(dc,fs.right-9,fs.top+7,fs.right-15,fs.top+7,C_TEXT,2);
    DrawLine(dc,fs.left+9,fs.bottom-13,fs.left+9,fs.bottom-7,C_TEXT,2);
    DrawLine(dc,fs.left+9,fs.bottom-7,fs.left+15,fs.bottom-7,C_TEXT,2);
    DrawLine(dc,fs.right-9,fs.bottom-13,fs.right-9,fs.bottom-7,C_TEXT,2);
    DrawLine(dc,fs.right-9,fs.bottom-7,fs.right-15,fs.bottom-7,C_TEXT,2);

    {
    int collectionTop=g_drawerSettingsOpen?438:82;
    RECT receiver=R_RECEIVER;
    int sel=(int)SendMessageW(g_renderers,CB_GETCURSEL,0,0);
    /* Timeline: a dedicated, readable seek row above the transport dock.
       The seek rectangle and its hit-testing remain unchanged. */
    RECT timeline={R_SEEK.left-8,R_SEEK.top-37,R_SEEK.right+8,R_SEEK.bottom+17};
    Fill(dc,timeline,C_BG);
    int pos=0;int64_t len=g_player?libvlc_media_player_get_length(g_player):0;
    int64_t now=g_player?libvlc_media_player_get_time(g_player):0;
    if(len>0){pos=(int)((now*1000)/len);if(pos<0)pos=0;if(pos>1000)pos=1000;}
    wchar_t t1[24],t2[24];
    FormatTime((int)now,t1,24);FormatTime((int)len,t2,24);
    RECT elapsedLabel={R_SEEK.left,R_SEEK.top-32,R_SEEK.left+92,R_SEEK.top-10};
    RECT totalLabel={R_SEEK.right-92,R_SEEK.top-32,R_SEEK.right,R_SEEK.top-10};
    Txt(dc,t1,elapsedLabel,ABM_THEME_COLOR_E7F4F8,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    Txt(dc,t2,totalLabel,ABM_THEME_COLOR_8097AE,g_smallFont,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    /* A fine recessed rail, luminous progress and a crisp floating playhead. */
    RECT track={R_SEEK.left,R_SEEK.top-3,R_SEEK.right,R_SEEK.bottom+3};
    RoundBox(dc,track,ABM_THEME_COLOR_243544,6);
    RECT trackInner={track.left+1,track.top+1,track.right-1,track.bottom-1};
    RoundBox(dc,trackInner,ABM_THEME_COLOR_1B2735,5);
    RECT prog=trackInner;
    prog.right=prog.left+(prog.right-prog.left)*pos/1000;
    if(prog.right>prog.left)GradientRound(dc,prog,ABM_THEME_COLOR_36B1A8,ABM_THEME_COLOR_8FD2B8,5);
    int seekThumb=R_SEEK.left+(R_SEEK.right-R_SEEK.left)*pos/1000;
    RECT halo={seekThumb-10,R_SEEK.top-10,seekThumb+10,R_SEEK.bottom+10};
    RoundBox(dc,halo,ABM_THEME_COLOR_1B4C48,10);
    RECT dot={seekThumb-5,R_SEEK.top-6,seekThumb+5,R_SEEK.bottom+6};
    GradientRound(dc,dot,ABM_THEME_COLOR_FFFFFF,ABM_THEME_COLOR_D3F4EE,6);
    ABM_D2D_StrokeRound(&g_ui,(float)dot.left,(float)dot.top,(float)dot.right,(float)dot.bottom,6.0f,ABM_THEME_COLOR_70D3CA,1.0f);

    /* Control dock */
    int dockLeft=R_VIDEO.left+((R_VIDEO.right-R_VIDEO.left)-720)/2;
    RECT dock={dockLeft,client.bottom-144,dockLeft+720,client.bottom-42};
    RECT dockShadow=dock; OffsetRect(&dockShadow,0,8);
    RoundBox(dc,dockShadow,ABM_THEME_COLOR_03070D,32);
    GradientRound(dc,dock,ABM_THEME_COLOR_11222B,ABM_THEME_COLOR_0F1B27,28);
    /* Fine top-edge light gives the transport surface a clear visual boundary. */
    RECT dockTop={dock.left+30,dock.top+1,dock.right-30,dock.top+3};
    GradientRect(dc,dockTop,ABM_THEME_COLOR_309D97,ABM_THEME_COLOR_5DB097);
    ABM_D2D_StrokeRound(&g_ui,(float)dock.left,(float)dock.top,(float)dock.right,(float)dock.bottom,28.0f,ABM_THEME_COLOR_3B696D,1.0f);
    RECT dockRim=dock; InflateRect(&dockRim,-2,-2);
    ABM_D2D_StrokeRound(&g_ui,(float)dockRim.left,(float)dockRim.top,(float)dockRim.right,(float)dockRim.bottom,26.0f,ABM_THEME_COLOR_253D45,0.65f);
    /* Dedicated Now Playing identity block makes the transport dock a real
       player surface rather than a floating row of generic buttons. */
    RECT miniArt={dock.left+17,dock.top+17,dock.left+81,dock.bottom-17};
    GradientRound(dc,miniArt,ABM_THEME_COLOR_1C8F8B,ABM_THEME_COLOR_347077,14);
    RECT miniInner={miniArt.left+6,miniArt.top+6,miniArt.right-6,miniArt.bottom-6};
    ABM_D2D_StrokeRound(&g_ui,(float)miniInner.left,(float)miniInner.top,(float)miniInner.right,(float)miniInner.bottom,9.0f,ABM_THEME_COLOR_A5DACF,0.85f);
    if(g_artworkLoaded)ABM_D2D_DrawImage(&g_ui,(float)miniArt.left+3,(float)miniArt.top+3,(float)miniArt.right-3,(float)miniArt.bottom-3);
    else {
        int mx=(miniArt.left+miniArt.right)/2,my=(miniArt.top+miniArt.bottom)/2;
        DrawLine(dc,mx-12,my+13,mx,my-14,ABM_THEME_COLOR_F4FAFF,2);
        DrawLine(dc,mx,my-14,mx+13,my+13,ABM_THEME_COLOR_F4FAFF,2);
        DrawLine(dc,mx-7,my+3,mx+7,my+3,ABM_THEME_COLOR_F4FAFF,2);
    }
    RECT nowLabel={miniArt.right+14,dock.top+12,dock.left+346,dock.top+31};
    Txt(dc,g_hasMedia?L"NOW PLAYING  ·  ON THIS DEVICE":L"ABM  ·  YOUR MEDIA SPACE",nowLabel,ABM_THEME_COLOR_6FC2B4,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    RECT nowTitle={miniArt.right+14,dock.top+32,dock.left+346,dock.top+59};
    const wchar_t *dockTitle=g_hasMedia?(g_trackTitle[0]?g_trackTitle:L"Playing your selection"):L"Your entertainment, in one place";
    Txt(dc,dockTitle,nowTitle,ABM_THEME_COLOR_F6F9FF,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    RECT nowMeta={miniArt.right+14,dock.top+62,dock.left+346,dock.bottom-9};
    Txt(dc,g_hasMedia?(g_trackArtist[0]?g_trackArtist:L"LOCAL MEDIA"):L"Build a queue or explore your library",nowMeta,ABM_THEME_COLOR_97AEC7,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    RECT transportDivider={dock.left+360,dock.top+17,dock.left+361,dock.bottom-17};
    Fill(dc,transportDivider,ABM_THEME_COLOR_2D4447);
    DrawIconButton(dc,R_PREV,5,g_hoverButton==HIT_PREV,FALSE);
    DrawIconButton(dc,R_PLAY,libvlc_media_player_get_state(g_player)==libvlc_Playing?2:1,g_hoverButton==HIT_PLAY,TRUE);
    DrawIconButton(dc,R_NEXT,4,g_hoverButton==HIT_NEXT,FALSE);
    DrawIconButton(dc,R_STOP,3,g_hoverButton==HIT_STOP,FALSE);
    RECT open=R_OPEN;
    GradientRound(dc,open,(g_hoverButton==HIT_OPEN)?ABM_THEME_COLOR_2A9D94:ABM_THEME_COLOR_163034,(g_hoverButton==HIT_OPEN)?ABM_THEME_COLOR_4CAE97:ABM_THEME_COLOR_274B48,14);
    ABM_D2D_StrokeRound(&g_ui,(float)open.left,(float)open.top,(float)open.right,(float)open.bottom,14.0f,(g_hoverButton==HIT_OPEN)?ABM_THEME_COLOR_7ECDB9:ABM_THEME_COLOR_427069,1.0f);
    RECT plus={open.left+14,open.top+4,open.left+37,open.bottom-4};
    Txt(dc,L"+",plus,ABM_THEME_COLOR_E6F2FF,g_headingFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RECT openText={open.left+39,open.top+2,open.right-8,open.bottom-2};
    Txt(dc,L"Add media",openText,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);


    /* Playback controls are permanent. Only the drawer itself follows its slide animation. */
    if(g_drawerProgress>0) {
    /* Slide-out media drawer: output, quick controls, search and queue. */
    /* Premium media-library surface with layered navy and an inset rim. */
    RECT drawerShadow=R_SIDEBAR;OffsetRect(&drawerShadow,0,8);
    RoundBox(dc,drawerShadow,ABM_THEME_COLOR_04080E,26);
    /* Slightly lighter layered glass treatment while media is playing. */
    if(g_hasMedia && libvlc_media_player_get_state(g_player)==libvlc_Playing)
        GradientRound(dc,R_SIDEBAR,ABM_THEME_COLOR_141D2A,ABM_THEME_COLOR_101926,28);
    else
        GradientRound(dc,R_SIDEBAR,ABM_THEME_COLOR_131C29,ABM_THEME_COLOR_0F1723,28);
    ABM_D2D_StrokeRound(&g_ui,(float)R_SIDEBAR.left,(float)R_SIDEBAR.top,(float)R_SIDEBAR.right,(float)R_SIDEBAR.bottom,28.0f,ABM_THEME_COLOR_43586F,1.0f);
    RECT drawerInset={R_SIDEBAR.left+2,R_SIDEBAR.top+2,R_SIDEBAR.right-2,R_SIDEBAR.bottom-2};
    ABM_D2D_StrokeRound(&g_ui,(float)drawerInset.left,(float)drawerInset.top,(float)drawerInset.right,(float)drawerInset.bottom,26.0f,ABM_THEME_COLOR_2B3F4E,0.55f);
    int accentX=R_SIDEBAR.left+18+(int)((g_visualPulse*2u)%(unsigned int)((R_SIDEBAR.right-R_SIDEBAR.left)-72));
    RECT accent={accentX,R_SIDEBAR.top+1,accentX+54,R_SIDEBAR.top+3};
    GradientRect(dc,accent,ABM_THEME_COLOR_30A99D,ABM_THEME_COLOR_68BB97);
    if(g_drawerSettingsOpen) {
    RECT sideTitle={R_SIDEBAR.left+18,R_SIDEBAR.top+78,R_SIDEBAR.right-18,R_SIDEBAR.top+96};
    Txt(dc,L"OUTPUT ROUTING",sideTitle,ABM_THEME_COLOR_77B1A4,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT scan=R_SCAN;
    GradientRound(dc,scan,(g_hoverButton==HIT_SCAN)?ABM_THEME_COLOR_257B70:ABM_THEME_COLOR_143133,(g_hoverButton==HIT_SCAN)?ABM_THEME_COLOR_43977E:ABM_THEME_COLOR_1F4844,12);
    ABM_D2D_StrokeRound(&g_ui,(float)scan.left,(float)scan.top,(float)scan.right,(float)scan.bottom,12.0f,ABM_THEME_COLOR_428474,0.95f);
    RECT scanIcon={scan.left+14,scan.top+8,scan.left+34,scan.bottom-8};
    DrawLine(dc,scanIcon.left+2,scanIcon.top+8,scanIcon.left+2,scanIcon.bottom-7,ABM_THEME_COLOR_DCECFF,2);
    DrawLine(dc,scanIcon.left+2,scanIcon.top+8,scanIcon.right-2,scanIcon.top+8,ABM_THEME_COLOR_DCECFF,2);
    DrawLine(dc,scanIcon.right-2,scanIcon.top+8,scanIcon.right-2,scanIcon.bottom-7,ABM_THEME_COLOR_DCECFF,2);
    RECT scanText={scan.left+42,scan.top+2,scan.right-10,scan.bottom-2};
    Txt(dc,L"Discover devices",scanText,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    /* Fully custom Direct2D output selector. */
    COLORREF receiverFill=g_receiverPopup?ABM_THEME_COLOR_2A2D32:ABM_THEME_COLOR_1E2024; (void)receiverFill;
    Panel(dc,receiver,g_receiverPopup?ABM_THEME_COLOR_1B4944:ABM_THEME_COLOR_132B2B,g_receiverPopup?ABM_THEME_COLOR_52A689:ABM_THEME_COLOR_39685B,12);
    RECT deviceIcon={receiver.left+12,receiver.top+7,receiver.left+34,receiver.bottom-7};
    GradientRound(dc,deviceIcon,ABM_THEME_COLOR_1D8277,ABM_THEME_COLOR_306567,6);
    DrawLine(dc,deviceIcon.left+4,deviceIcon.top+5,deviceIcon.right-4,deviceIcon.top+5,ABM_THEME_COLOR_C5CDD6,2);
    DrawLine(dc,deviceIcon.left+4,deviceIcon.bottom-5,deviceIcon.right-4,deviceIcon.bottom-5,ABM_THEME_COLOR_C5CDD6,2);
    wchar_t deviceName[256]=L"Local playback";
    if(sel>0)SendMessageW(g_renderers,CB_GETLBTEXT,sel,(LPARAM)deviceName);
    RECT deviceText={receiver.left+44,receiver.top+2,receiver.right-48,receiver.bottom-2};
    Txt(dc,deviceName,deviceText,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    int arrowX=receiver.right-20,arrowY=(receiver.top+receiver.bottom)/2;
    DrawLine(dc,arrowX-4,arrowY-2,arrowX,arrowY+2,ABM_THEME_COLOR_BEC4CC,2);
    DrawLine(dc,arrowX,arrowY+2,arrowX+4,arrowY-2,ABM_THEME_COLOR_BEC4CC,2);

    RECT toolsHead={R_SIDEBAR.left+18,R_SIDEBAR.top+198,R_SIDEBAR.right-18,R_SIDEBAR.top+218};
    Txt(dc,L"QUICK CONTROLS",toolsHead,ABM_THEME_COLOR_77B1A4,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT actions[4]={R_SHUFFLE,R_REPEAT,R_SPEED,R_RECENT};
    const wchar_t *actionLabels[4]={g_shuffle?L"Shuffle on":L"Shuffle",g_repeat==2?L"Repeat all":(g_repeat==1?L"Repeat current":L"Repeat off"),L"Speed",L"Recent"};
    const wchar_t *actionIcons[4]={L"S",L"R",L"1x",L"..."};
    BOOL actionActive[4]={g_shuffle,g_repeat!=0,g_playbackRate!=1.0f,FALSE};
    for(int ai=0;ai<4;ai++) {
        RECT ar=actions[ai];
        POINT cursor; BOOL actionHot=FALSE;
        if(GetCursorPos(&cursor)){ScreenToClient(g_main,&cursor);actionHot=PtInRect(&ar,cursor);}
        COLORREF fill=actionActive[ai]?ABM_THEME_COLOR_1B3741:(actionHot?ABM_THEME_COLOR_1E303B:ABM_THEME_COLOR_17202C);
        COLORREF edge=actionActive[ai]?ABM_THEME_COLOR_41878F:(actionHot?ABM_THEME_COLOR_466F7D:ABM_THEME_COLOR_344556);
        if(actionActive[ai])GradientRound(dc,ar,ABM_THEME_COLOR_1B695B,ABM_THEME_COLOR_1E524E,10);
        else RoundBox(dc,ar,fill,10);
        ABM_D2D_StrokeRound(&g_ui,(float)ar.left,(float)ar.top,(float)ar.right,(float)ar.bottom,10.0f,edge,0.9f);
        RECT glyph={ar.left+8,ar.top+3,ar.left+31,ar.bottom-3};
        Txt(dc,actionIcons[ai],glyph,actionActive[ai]?ABM_THEME_COLOR_8FD2B8:ABM_THEME_COLOR_9DB8B4,g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT label={ar.left+34,ar.top+2,ar.right-5,ar.bottom-2};
        wchar_t speedLabel[32];
        const wchar_t *shown=actionLabels[ai];
        if(ai==2){swprintf_s(speedLabel,32,L"Speed  %.2gx",g_playbackRate);shown=speedLabel;}
        Txt(dc,shown,label,actionActive[ai]?ABM_THEME_COLOR_EBF6EF:ABM_THEME_COLOR_D2E1DB,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    RECT advancedButton=R_ADVANCED_SETTINGS_BUTTON;
    GradientRound(dc,advancedButton,ABM_THEME_COLOR_1D4B5B,ABM_THEME_COLOR_1C9D9D,10);
    ABM_D2D_StrokeRound(&g_ui,(float)advancedButton.left,(float)advancedButton.top,(float)advancedButton.right,(float)advancedButton.bottom,10.0f,ABM_THEME_COLOR_2B586F,1.0f);
    /* Match the settings glyph used by the Command Centre and settings windows. */
    RECT advancedGlyph={advancedButton.left+8,advancedButton.top+2,advancedButton.left+36,advancedButton.bottom-2};
    Txt(dc,L"⚙",advancedGlyph,ABM_THEME_COLOR_B7EBE5,g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RECT advancedLabel={advancedButton.left+42,advancedButton.top+2,advancedButton.right-8,advancedButton.bottom-2};
    Txt(dc,L"Advanced Settings",advancedLabel,ABM_THEME_COLOR_E5F5FA,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
    
    }
    Panel(dc,R_SETTINGS_BUTTON,g_drawerSettingsOpen?ABM_THEME_COLOR_1B433E:ABM_THEME_COLOR_17202C,g_drawerSettingsOpen?ABM_THEME_COLOR_4C977F:ABM_THEME_COLOR_344556,13);
    RECT headerIcon={R_SETTINGS_BUTTON.left+12,R_SETTINGS_BUTTON.top+11,R_SETTINGS_BUTTON.left+42,R_SETTINGS_BUTTON.bottom-11};
    GradientRound(dc,headerIcon,ABM_THEME_COLOR_1C7D77,ABM_THEME_COLOR_285C65,8);
    DrawLine(dc,headerIcon.left+6,headerIcon.top+8,headerIcon.left+6,headerIcon.bottom-8,ABM_THEME_COLOR_E5F5FA,2);
    DrawLine(dc,headerIcon.left+6,headerIcon.top+8,headerIcon.left+14,headerIcon.top+8,ABM_THEME_COLOR_E5F5FA,2);
    DrawLine(dc,headerIcon.left+6,headerIcon.bottom-8,headerIcon.left+14,headerIcon.bottom-8,ABM_THEME_COLOR_E5F5FA,2);
    RECT settingsTitle={R_SETTINGS_BUTTON.left+54,R_SETTINGS_BUTTON.top+6,R_SETTINGS_BUTTON.right-46,R_SETTINGS_BUTTON.top+29};
    Txt(dc,L"Playback & Output",settingsTitle,ABM_THEME_COLOR_F4F8FF,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    wchar_t outputSummary[256]=L"Local playback";
    int selectedOutput=(int)SendMessageW(g_renderers,CB_GETCURSEL,0,0);
    if(selectedOutput>0)SendMessageW(g_renderers,CB_GETLBTEXT,(WPARAM)selectedOutput,(LPARAM)outputSummary);
    RECT settingsSub={R_SETTINGS_BUTTON.left+55,R_SETTINGS_BUTTON.top+28,R_SETTINGS_BUTTON.right-48,R_SETTINGS_BUTTON.bottom-4};
    Txt(dc,outputSummary,settingsSub,ABM_THEME_COLOR_8BB4C2,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    /* Prominent but restrained drawer control: a larger inset capsule gives
       the chevron a clear visual anchor without changing the header hit target. */
    RECT chevronButton={R_SETTINGS_BUTTON.right-50,R_SETTINGS_BUTTON.top+7,
                        R_SETTINGS_BUTTON.right-8,R_SETTINGS_BUTTON.bottom-7};
    if(g_drawerSettingsOpen)
        GradientRound(dc,chevronButton,ABM_THEME_COLOR_1B695B,ABM_THEME_COLOR_1E524E,12);
    else
        RoundBox(dc,chevronButton,ABM_THEME_COLOR_1E303B,12);
    ABM_D2D_StrokeRound(&g_ui,(float)chevronButton.left,(float)chevronButton.top,
                        (float)chevronButton.right,(float)chevronButton.bottom,12.0f,
                        g_drawerSettingsOpen?ABM_THEME_COLOR_5BCDBC:ABM_THEME_COLOR_466F7D,1.1f);
    int chevronX=(chevronButton.left+chevronButton.right)/2;
    int chevronY=(chevronButton.top+chevronButton.bottom)/2;
    COLORREF chevronColor=g_drawerSettingsOpen?ABM_THEME_COLOR_E5F5FA:ABM_THEME_COLOR_BED2EA;
    if(g_drawerSettingsOpen) {
        DrawLine(dc,chevronX-8,chevronY+3,chevronX,chevronY-5,chevronColor,3);
        DrawLine(dc,chevronX,chevronY-5,chevronX+8,chevronY+3,chevronColor,3);
    } else {
        DrawLine(dc,chevronX-8,chevronY-3,chevronX,chevronY+5,chevronColor,3);
        DrawLine(dc,chevronX,chevronY+5,chevronX+8,chevronY-3,chevronColor,3);
    }
    RECT listHead={R_SIDEBAR.left+18,R_SIDEBAR.top+collectionTop,R_SIDEBAR.right-18,R_SIDEBAR.top+collectionTop+24};
    /* Collection identity: a compact layered-library mark and a clear section title. */
    int stackX=listHead.left+1,stackY=(listHead.top+listHead.bottom)/2;
    DrawLine(dc,stackX,stackY-5,stackX+10,stackY-10,ABM_THEME_COLOR_63E1CD,2);
    DrawLine(dc,stackX+10,stackY-10,stackX+20,stackY-5,ABM_THEME_COLOR_63E1CD,2);
    DrawLine(dc,stackX+20,stackY-5,stackX+10,stackY,ABM_THEME_COLOR_63E1CD,2);
    DrawLine(dc,stackX+10,stackY,stackX,stackY-5,ABM_THEME_COLOR_63E1CD,2);
    DrawLine(dc,stackX,stackY,stackX+10,stackY+5,ABM_THEME_COLOR_5EB7F4,2);
    DrawLine(dc,stackX+10,stackY+5,stackX+20,stackY,ABM_THEME_COLOR_5EB7F4,2);
    RECT collectionTitle={listHead.left+30,listHead.top,listHead.right-156,listHead.bottom};
    Txt(dc,L"MEDIA COLLECTION",collectionTitle,ABM_THEME_COLOR_D3F4EE,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    wchar_t count[48];swprintf_s(count,48,L"%02d TRACKS",g_libraryTab==1&&g_showFavorites?g_favoriteCount:(g_libraryTab==1&&g_showContinueWatching?CountFilteredMedia():g_playlistCount));
    RECT countR={R_SIDEBAR.right-166,listHead.top+1,R_CLEAR_PLAYLIST.left-8,listHead.bottom-1};
    RoundBox(dc,countR,ABM_THEME_COLOR_132B2B,8);
    ABM_D2D_StrokeRound(&g_ui,(float)countR.left,(float)countR.top,(float)countR.right,(float)countR.bottom,8.0f,ABM_THEME_COLOR_2B586F,0.75f);
    Txt(dc,count,countR,ABM_THEME_COLOR_A9BFD2,g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    GradientRound(dc,R_CLEAR_PLAYLIST,ABM_THEME_COLOR_143133,ABM_THEME_COLOR_1F4844,8);
    ABM_D2D_StrokeRound(&g_ui,(float)R_CLEAR_PLAYLIST.left,(float)R_CLEAR_PLAYLIST.top,(float)R_CLEAR_PLAYLIST.right,(float)R_CLEAR_PLAYLIST.bottom,8.0f,ABM_THEME_COLOR_428474,0.85f);
    Txt(dc,L"Manage",R_CLEAR_PLAYLIST,ABM_THEME_COLOR_8FD2B8,g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    DrawLine(dc,R_SIDEBAR.left+16,listHead.bottom+5,R_SIDEBAR.right-16,listHead.bottom+5,ABM_THEME_COLOR_2B4865,1);

    /* Search and filters are drawn in Direct2D; keyboard input is handled by
       the main window so no blurry native EDIT control is layered over it. */
    /* Search is a recessed tool surface with a quiet focus halo. */
    GradientRound(dc,R_SEARCH,g_searchFocused?ABM_THEME_COLOR_0D2F3A:ABM_THEME_COLOR_0B1727,
        ABM_THEME_COLOR_0D192A,12);
    ABM_D2D_StrokeRound(&g_ui,(float)R_SEARCH.left,(float)R_SEARCH.top,(float)R_SEARCH.right,
        (float)R_SEARCH.bottom,12.0f,g_searchFocused?ABM_THEME_COLOR_48D2C7:ABM_THEME_COLOR_2B4865,
        g_searchFocused?1.25f:0.8f);
    RECT searchGlyph={R_SEARCH.left+13,R_SEARCH.top+10,R_SEARCH.left+27,R_SEARCH.bottom-10};
    ABM_D2D_StrokeRound(&g_ui,(float)searchGlyph.left,(float)searchGlyph.top,(float)searchGlyph.right,
        (float)searchGlyph.bottom,7,ABM_THEME_COLOR_91B8DA,1.55f);
    DrawLine(dc,searchGlyph.right-1,searchGlyph.bottom-1,searchGlyph.right+4,searchGlyph.bottom+4,ABM_THEME_COLOR_91B8DA,2);
    RECT searchText={R_SEARCH.left+38,R_SEARCH.top+2,R_SEARCH.right-12,R_SEARCH.bottom-2};
    Txt(dc,g_searchText[0]?g_searchText:(g_searchFocused?L"Type to search":L"Search tracks, artists, albums"),searchText,
        g_searchText[0]?C_TEXT:ABM_THEME_COLOR_91A6BE,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

    RECT tabs[2]={R_TAB_QUEUE,R_TAB_LIBRARY};
    const wchar_t *tabNames[2]={L"Queue",L"Library"};
    for(int ti=0;ti<2;ti++){
        BOOL active=(g_libraryTab==ti);
        if(active)GradientRound(dc,tabs[ti],ABM_THEME_COLOR_1B7771,ABM_THEME_COLOR_1C3445,11);
        else GradientRound(dc,tabs[ti],ABM_THEME_COLOR_111D2C,ABM_THEME_COLOR_0D192A,11);
        ABM_D2D_StrokeRound(&g_ui,(float)tabs[ti].left,(float)tabs[ti].top,(float)tabs[ti].right,(float)tabs[ti].bottom,
            11.0f,active?ABM_THEME_COLOR_63E1CD:ABM_THEME_COLOR_2B4865,active?1.2f:0.75f);
        if(active){RECT tabGlow={tabs[ti].left+14,tabs[ti].bottom-3,tabs[ti].right-14,tabs[ti].bottom-1};
            GradientRect(dc,tabGlow,ABM_THEME_COLOR_63E1CD,ABM_THEME_COLOR_5EB7F4);}
        RECT label={tabs[ti].left+3,tabs[ti].top+1,tabs[ti].right-3,tabs[ti].bottom-1};
        Txt(dc,tabNames[ti],label,active?ABM_THEME_COLOR_F4FAFF:ABM_THEME_COLOR_AEC3DC,g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    RECT filters[3]={R_FILTER_ALL,R_FILTER_AUDIO,R_FILTER_VIDEO};
    const wchar_t *filterNames[3]={L"All",L"Audio",L"Video"};
    for(int fi=0;fi<3;fi++){
        BOOL active=g_mediaFilter==fi;
        if(active)GradientRound(dc,filters[fi],ABM_THEME_COLOR_1B7771,ABM_THEME_COLOR_1C3445,8);
        else RoundBox(dc,filters[fi],ABM_THEME_COLOR_0F1B27,8);
        ABM_D2D_StrokeRound(&g_ui,(float)filters[fi].left,(float)filters[fi].top,
            (float)filters[fi].right,(float)filters[fi].bottom,8.0f,
            active?ABM_THEME_COLOR_4CAE97:ABM_THEME_COLOR_264F5B,active?0.9f:0.55f);
        RECT label={filters[fi].left+2,filters[fi].top,filters[fi].right-2,filters[fi].bottom};
        Txt(dc,filterNames[fi],label,active?ABM_THEME_COLOR_F4FAFF:ABM_THEME_COLOR_9DB1C9,g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    wchar_t groupLabel[32];
    if(g_libraryTab==1)StringCchCopyW(groupLabel,_countof(groupLabel),g_showFavorites?L"♥ Favorites":(g_showContinueWatching?L"▶ Continue":L"All tracks"));
    else swprintf_s(groupLabel,32,L"Group: %s",g_groupMode==1?L"Artist":(g_groupMode==2?L"Album":L"Folder"));
    if(g_libraryTab==1&&g_showFavorites) {
        GradientRound(dc,R_GROUP,ABM_THEME_COLOR_311F2B,ABM_THEME_COLOR_1F2226,8);
        ABM_D2D_StrokeRound(&g_ui,(float)R_GROUP.left,(float)R_GROUP.top,(float)R_GROUP.right,(float)R_GROUP.bottom,8.0f,ABM_THEME_COLOR_FFBECD,0.9f);
    } else if(g_libraryTab==1&&g_showContinueWatching) {
        GradientRound(dc,R_GROUP,ABM_THEME_COLOR_12373B,ABM_THEME_COLOR_0C3039,8);
        ABM_D2D_StrokeRound(&g_ui,(float)R_GROUP.left,(float)R_GROUP.top,(float)R_GROUP.right,(float)R_GROUP.bottom,8.0f,ABM_THEME_COLOR_52CDBE,0.9f);
    } else {
        GradientRound(dc,R_GROUP,ABM_THEME_COLOR_122637,ABM_THEME_COLOR_142A3C,8);
        ABM_D2D_StrokeRound(&g_ui,(float)R_GROUP.left,(float)R_GROUP.top,(float)R_GROUP.right,(float)R_GROUP.bottom,8.0f,ABM_THEME_COLOR_2B586F,0.7f);
    }
    Txt(dc,groupLabel,R_GROUP,g_showContinueWatching?ABM_THEME_COLOR_63E1CD:(g_libraryTab==1?ABM_THEME_COLOR_FFBECD:ABM_THEME_COLOR_BBD2DC),g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    int rowHeight=PlaylistRowHeight();
    int visible=PlaylistVisibleRows(rowHeight);
    int filteredCount=CountFilteredMedia();
    int maxPlaylistScroll=filteredCount-visible;
    if(maxPlaylistScroll<0)maxPlaylistScroll=0;
    if(g_playlistScroll<0)g_playlistScroll=0;
    if(g_playlistScroll>maxPlaylistScroll)g_playlistScroll=maxPlaylistScroll;
    int matched=0,shown=0;g_visibleRowCount=0;
    for(int i=0;i<g_playlistCount;i++){
        const wchar_t *leaf=wcsrchr(g_playlist[i],L'\\');leaf=leaf?leaf+1:g_playlist[i];
        if(g_libraryTab==1&&g_showFavorites&&!IsFavoritePath(g_playlist[i]))continue;
        if(g_libraryTab==1&&g_showContinueWatching&&!g_hasResume[i])continue;
        BOOL isVideo=IsVideoPath(g_playlist[i]);
        if((g_mediaFilter==1&&isVideo)||(g_mediaFilter==2&&!isVideo))continue;
        if(g_searchText[0]&&!ContainsTextI(leaf,g_searchText)&&
           !(i==g_current&&((g_trackArtist[0]&&ContainsTextI(g_trackArtist,g_searchText))||
                            (g_trackAlbum[0]&&ContainsTextI(g_trackAlbum,g_searchText))||
                            (g_trackTitle[0]&&ContainsTextI(g_trackTitle,g_searchText)))))continue;
        if(matched++<g_playlistScroll)continue;
        if(shown>=visible)break;
        RECT row={R_LIST.left,R_LIST.top+shown*rowHeight,R_LIST.right,R_LIST.top+(shown+1)*rowHeight};
        /* PlaylistVisibleRows() guarantees this complete row fits in R_LIST. */
        if(row.bottom>R_LIST.bottom||row.bottom<=row.top)break;
        R_ROW[shown]=row;g_visibleRows[shown]=i;g_visibleRowCount=shown+1;
        BOOL current=(i==g_current),selected=(i==g_selected),hot=(i==g_hoverPlaylistIndex);
        /* Every queue entry is a deliberate media card, not bare text on a panel. */
        if(current)GradientRound(dc,row,ABM_THEME_COLOR_183947,ABM_THEME_COLOR_18303A,13);
        else if(selected)GradientRound(dc,row,ABM_THEME_COLOR_1B2F42,ABM_THEME_COLOR_17212E,13);
        else GradientRound(dc,row,hot?ABM_THEME_COLOR_1B2F42:ABM_THEME_COLOR_0F1F2F,
            hot?ABM_THEME_COLOR_18303A:ABM_THEME_COLOR_111D2C,13);
        ABM_D2D_StrokeRound(&g_ui,(float)row.left,(float)row.top,(float)row.right,(float)row.bottom,13.0f,
            current?ABM_THEME_COLOR_63E1CD:(selected?ABM_THEME_COLOR_45697C:(hot?ABM_THEME_COLOR_415E71:ABM_THEME_COLOR_2B4865)),
            current?1.25f:(hot?0.95f:0.7f));
        if(current){RECT activeRail={row.left+1,row.top+10,row.left+4,row.bottom-10};GradientRect(dc,activeRail,ABM_THEME_COLOR_63E1CD,ABM_THEME_COLOR_28BED3);}
        int compactRow=rowHeight<72;
        RECT rowThumb=compactRow
            ?(RECT){row.left+7,row.top+6,row.left+7+(rowHeight<48?rowHeight-12:44),row.top+6+(rowHeight<48?rowHeight-12:44)}
            :(RECT){row.left+10,row.top+8,row.left+72,row.bottom-8};
        RECT thumbFrame=rowThumb;InflateRect(&thumbFrame,2,2);
        RoundBox(dc,thumbFrame,ABM_THEME_COLOR_0C1420,11);
        GradientRound(dc,rowThumb,current?ABM_THEME_COLOR_1B7771:ABM_THEME_COLOR_183449,
            current?ABM_THEME_COLOR_1C3445:ABM_THEME_COLOR_0D1825,9);
        RECT coverInset={rowThumb.left+2,rowThumb.top+2,rowThumb.right-2,rowThumb.bottom-2};
        ABM_D2D_StrokeRound(&g_ui,(float)coverInset.left,(float)coverInset.top,(float)coverInset.right,
            (float)coverInset.bottom,7.0f,ABM_THEME_COLOR_91B8DA,0.45f);
        if(current&&g_artworkLoaded)
            ABM_D2D_DrawImage(&g_ui,(float)rowThumb.left+2,(float)rowThumb.top+2,
                (float)rowThumb.right-2,(float)rowThumb.bottom-2);
        else
            DrawDefaultMediaArtwork(dc,rowThumb,isVideo,i);
        int textLeft=compactRow?rowThumb.right+9:row.left+82;
        R_FAVORITE[shown]=(RECT){row.right-34,row.top+(rowHeight-28)/2,row.right-6,row.top+(rowHeight+28)/2};
        RECT name=compactRow
            ?(RECT){textLeft,row.top+3,row.right-106,row.top+(rowHeight/2)+3}
            :(RECT){row.left+82,row.top+9,row.right-108,row.top+38};
        RECT subline=compactRow
            ?(RECT){textLeft,row.top+(rowHeight/2)-1,row.right-106,row.bottom-2}
            :(RECT){row.left+82,row.top+40,row.right-108,row.bottom-7};
        Txt(dc,leaf,name,current?ABM_THEME_COLOR_FFFFFF:ABM_THEME_COLOR_E8EFF9,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        wchar_t metaLine[320];
        if(g_missingMedia[i])swprintf_s(metaLine,320,L"MISSING MEDIA  ·  %s",isVideo?L"VIDEO":L"AUDIO");
        else if(g_hasResume[i])FormatResumeLabel(g_playlist[i],metaLine,_countof(metaLine));
        else if(g_groupMode==1&&current&&g_trackArtist[0])swprintf_s(metaLine,320,L"%s  ·  %s",g_trackArtist,isVideo?L"VIDEO":L"AUDIO");
        else if(g_groupMode==2&&current&&g_trackAlbum[0])swprintf_s(metaLine,320,L"%s  ·  LOCAL FILE",g_trackAlbum);
        else swprintf_s(metaLine,320,L"%s  ·  LOCAL FILE",isVideo?L"VIDEO":L"AUDIO");
        Txt(dc,metaLine,subline,g_missingMedia[i]?ABM_THEME_COLOR_FFBECD:(g_hasResume[i]?ABM_THEME_COLOR_63E1CD:ABM_THEME_COLOR_91A6BE),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        DurationRequest(g_playlist[i]);
        wchar_t durationText[32];FormatMediaDuration(DurationLookup(g_playlist[i]),durationText,_countof(durationText));
        RECT durationRect={row.right-164,subline.top,row.right-112,subline.bottom};
        Txt(dc,durationText,durationRect,ABM_THEME_COLOR_91A6BE,g_smallFont,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        RECT typePill=compactRow
            ?(RECT){row.right-98,row.top+(row.bottom-row.top-20)/2,row.right-42,row.top+(row.bottom-row.top+20)/2}
            :(RECT){row.right-108,row.top+24,row.right-44,row.top+49};
        GradientRound(dc,typePill,isVideo?ABM_THEME_COLOR_1B2F42:ABM_THEME_COLOR_13414B,
            isVideo?ABM_THEME_COLOR_17273B:ABM_THEME_COLOR_153943,8);
        ABM_D2D_StrokeRound(&g_ui,(float)typePill.left,(float)typePill.top,(float)typePill.right,
            (float)typePill.bottom,8.0f,isVideo?ABM_THEME_COLOR_3D7791:ABM_THEME_COLOR_347077,0.65f);
        RECT typeText={typePill.left+3,typePill.top+1,typePill.right-3,typePill.bottom-1};
        Txt(dc,isVideo?L"VIDEO":L"AUDIO",typeText,isVideo?ABM_THEME_COLOR_A4C1EF:ABM_THEME_COLOR_74D7D3,g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        BOOL favorite=IsFavoritePath(g_playlist[i]);
        RECT heart=R_FAVORITE[shown];
        Txt(dc,favorite?L"♥":L"♡",heart,favorite?ABM_THEME_COLOR_FFBECD:(hot?ABM_THEME_COLOR_BBD2DC:ABM_THEME_COLOR_738297),g_headingFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(current){int cx=row.right-118,base=row.top+rowHeight/2;for(int k=0;k<3;k++){int bh=5+(int)(5.0*(0.5+0.5*sin((double)g_visualPulse*0.18+k*1.5)));RECT wave={cx+k*4,base-bh,cx+k*4+2,base+bh};RoundBox(dc,wave,ABM_THEME_COLOR_5EB7F4,2);}}
        shown++;
    }
    /* Queue drag insertion guide. The Library tab remains read-only. */
    if(g_queueDragging&&g_libraryTab==0&&g_queueDropBoundary>=0) {
        int guideY=R_LIST.bottom;
        for(int ri=0;ri<g_visibleRowCount;ri++) {
            if(g_visibleRows[ri]>=g_queueDropBoundary){guideY=R_ROW[ri].top;break;}
            guideY=R_ROW[ri].bottom;
        }
        if(guideY<R_LIST.top)guideY=R_LIST.top;
        if(guideY>R_LIST.bottom)guideY=R_LIST.bottom;
        RECT guide={R_LIST.left+7,guideY-2,R_LIST.right-9,guideY+2};
        GradientRect(dc,guide,ABM_THEME_COLOR_63E1CD,ABM_THEME_COLOR_5EB7F4);
    }
    /* Premium slim scrollbar: quiet rail, luminous gradient thumb and a
       generous hit target. It appears only when the filtered collection overflows. */
    if(maxPlaylistScroll>0 && R_PLAYLIST_SCROLL.bottom>R_PLAYLIST_SCROLL.top) {
        RECT rail=R_PLAYLIST_SCROLL;
        RoundBox(dc,rail,ABM_THEME_COLOR_17273B,3);
        int railH=rail.bottom-rail.top;
        int thumbH=(int)((long long)railH*visible/filteredCount);
        if(thumbH<42)thumbH=42;
        if(thumbH>railH)thumbH=railH;
        int thumbTravel=railH-thumbH;
        int thumbTop=rail.top+(maxPlaylistScroll?thumbTravel*g_playlistScroll/maxPlaylistScroll:0);
        R_PLAYLIST_THUMB=(RECT){rail.left-2,thumbTop,rail.right+2,thumbTop+thumbH};
        RECT thumbGlow=R_PLAYLIST_THUMB;InflateRect(&thumbGlow,2,1);
        RoundBox(dc,thumbGlow,ABM_THEME_COLOR_183D52,5);
        GradientRect(dc,R_PLAYLIST_THUMB,ABM_THEME_COLOR_28BED3,ABM_THEME_COLOR_6F69EF);
        RECT shine={R_PLAYLIST_THUMB.left+1,R_PLAYLIST_THUMB.top+4,R_PLAYLIST_THUMB.right-1,R_PLAYLIST_THUMB.bottom-4};
        ABM_D2D_StrokeRound(&g_ui,(float)shine.left,(float)shine.top,(float)shine.right,(float)shine.bottom,3.0f,ABM_THEME_COLOR_AADEFF,0.45f);
    } else {
        R_PLAYLIST_THUMB=R_PLAYLIST_SCROLL;
    }
    if(filteredCount==0){
        RECT empty={R_LIST.left+8,R_LIST.top+4,R_LIST.right-8,R_LIST.bottom-4};
        RECT emptyCard={empty.left+8,empty.top+(empty.bottom-empty.top)/2-42,empty.right-8,empty.top+(empty.bottom-empty.top)/2+42};
        GradientRound(dc,emptyCard,ABM_THEME_COLOR_111D2C,ABM_THEME_COLOR_0E1F31,14);
        ABM_D2D_StrokeRound(&g_ui,(float)emptyCard.left,(float)emptyCard.top,(float)emptyCard.right,
            (float)emptyCard.bottom,14.0f,ABM_THEME_COLOR_2B4865,0.8f);
        int ecx=(emptyCard.left+emptyCard.right)/2,ecy=emptyCard.top+22;
        DrawLine(dc,ecx-9,ecy-5,ecx,ecy-10,ABM_THEME_COLOR_63E1CD,2);
        DrawLine(dc,ecx,ecy-10,ecx+9,ecy-5,ABM_THEME_COLOR_63E1CD,2);
        DrawLine(dc,ecx-9,ecy-5,ecx,ecy,ABM_THEME_COLOR_5EB7F4,2);
        DrawLine(dc,ecx,ecy,ecx+9,ecy-5,ABM_THEME_COLOR_5EB7F4,2);
        const wchar_t *emptyText=g_playlistCount==0
            ?L"Add media to start your library"
            :(g_libraryTab==1&&g_showFavorites&&g_favoriteCount==0?L"Your favourites will appear here"
              :(g_libraryTab==1&&g_showContinueWatching?L"Nothing to continue watching right now"
              :(g_searchText[0]?L"No matching media":L"No media in this filter")));
        RECT emptyLabel={emptyCard.left+8,ecy+10,emptyCard.right-8,emptyCard.bottom-5};
        Txt(dc,emptyText,emptyLabel,ABM_THEME_COLOR_BBD2DC,g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    if(g_receiverPopup) {
        int n=(int)SendMessageW(g_renderers,CB_GETCOUNT,0,0);if(n>MAX_RENDERERS+1)n=MAX_RENDERERS+1;
        int rows=n>6?6:n;
        R_RECEIVER_LIST=(RECT){receiver.left,receiver.bottom+7,receiver.right,receiver.bottom+7+rows*36};
        Panel(dc,R_RECEIVER_LIST,ABM_THEME_COLOR_142526,ABM_THEME_COLOR_3B5B4F,13);
        for(int i=0;i<rows;i++) {
            RECT item={R_RECEIVER_LIST.left+6,R_RECEIVER_LIST.top+4+i*36,R_RECEIVER_LIST.right-6,R_RECEIVER_LIST.top+4+(i+1)*36};
            if(i==sel)RoundBox(dc,item,ABM_THEME_COLOR_22453B,8);
            wchar_t label[256]=L"";
            SendMessageW(g_renderers,CB_GETLBTEXT,i,(LPARAM)label);
            RECT tx={item.left+13,item.top,item.right-12,item.bottom};
            Txt(dc,label,tx,i==sel?ABM_THEME_COLOR_F8F9FA:ABM_THEME_COLOR_CACFD6,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
    }

    if(g_drawerOpen) {
    RECT volPanel={R_SIDEBAR.left+14,R_SIDEBAR.bottom-78,R_SIDEBAR.right-14,R_SIDEBAR.bottom-12};
    Panel(dc,volPanel,ABM_THEME_COLOR_172628,ABM_THEME_COLOR_37574D,16);
    RECT volLabel={volPanel.left+14,volPanel.top+3,volPanel.left+82,volPanel.bottom-3};
         Txt(dc,L"VOLUME",volLabel,ABM_THEME_COLOR_A1B7D3,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT audioBtn=R_AUDIO_BUTTON;BOOL audioActive=g_audioPopupOpen;
    GradientRound(dc,audioBtn,audioActive?ABM_THEME_COLOR_287E77:ABM_THEME_COLOR_18303A,audioActive?ABM_THEME_COLOR_44AE91:ABM_THEME_COLOR_264F5B,13);
    ABM_D2D_StrokeRound(&g_ui,(float)audioBtn.left,(float)audioBtn.top,(float)audioBtn.right,(float)audioBtn.bottom,13.0f,audioActive?ABM_THEME_COLOR_7BDCBC:ABM_THEME_COLOR_4C8996,1.0f);
    int acx=(audioBtn.left+audioBtn.right)/2,acy=(audioBtn.top+audioBtn.bottom)/2;
    /* Compact speaker + Bluetooth mark: the button still represents audio
       output selection, while the secondary glyph hints at wireless audio. */
    COLORREF speakerInk=ABM_THEME_COLOR_E5F5FA;
    COLORREF bluetoothInk=ABM_THEME_COLOR_85DCCC;
    DrawLine(dc,acx-15,acy-5,acx-9,acy-5,speakerInk,2);
    DrawLine(dc,acx-15,acy+5,acx-9,acy+5,speakerInk,2);
    DrawLine(dc,acx-9,acy-5,acx-1,acy-12,speakerInk,2);
    DrawLine(dc,acx-9,acy+5,acx-1,acy+12,speakerInk,2);
    DrawLine(dc,acx-1,acy-12,acx-1,acy+12,speakerInk,2);
    DrawLine(dc,acx+5,acy-12,acx+5,acy+12,bluetoothInk,2);
    DrawLine(dc,acx+5,acy-12,acx+13,acy-5,bluetoothInk,2);
    DrawLine(dc,acx+13,acy-5,acx+5,acy+2,bluetoothInk,2);
    DrawLine(dc,acx+5,acy+2,acx+13,acy+9,bluetoothInk,2);
    DrawLine(dc,acx+13,acy+9,acx+5,acy+12,bluetoothInk,2);
    DrawLine(dc,acx+5,acy+2,acx-1,acy-4,bluetoothInk,2);
    DrawLine(dc,acx+5,acy+2,acx-1,acy+8,bluetoothInk,2);
    if(g_audioPopupOpen) {
        int rows=g_audioDeviceCount+1;if(rows>7)rows=7;
        R_AUDIO_PANEL=(RECT){R_SIDEBAR.right-304,R_SIDEBAR.bottom-91-(rows*38+68),R_SIDEBAR.right-14,R_SIDEBAR.bottom-91};
        Panel(dc,R_AUDIO_PANEL,ABM_THEME_COLOR_0D1927,ABM_THEME_COLOR_467E82,17);
        RECT title={R_AUDIO_PANEL.left+16,R_AUDIO_PANEL.top+10,R_AUDIO_PANEL.right-14,R_AUDIO_PANEL.top+36};
        Txt(dc,L"AUDIO OUTPUT",title,ABM_THEME_COLOR_7ECFBE,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        for(int ai=0;ai<rows;ai++) {
            RECT item={R_AUDIO_PANEL.left+8,R_AUDIO_PANEL.top+42+ai*38,R_AUDIO_PANEL.right-8,R_AUDIO_PANEL.top+78+ai*38};R_AUDIO_ITEMS[ai]=item;
            BOOL chosen=(ai==0&&!g_audioDeviceId[0])||(ai>0&&ai-1<g_audioDeviceCount&&!strcmp(g_audioDeviceIds[ai-1],g_audioDeviceId));
            if(chosen)RoundBox(dc,item,ABM_THEME_COLOR_1B4C48,9);
            const wchar_t *name=ai==0?L"System default":g_audioDeviceNames[ai-1];
            RECT label={item.left+14,item.top,item.right-30,item.bottom};
            Txt(dc,name,label,chosen?ABM_THEME_COLOR_F4FFFB:ABM_THEME_COLOR_D1E0EF,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            if(chosen){int cy=(item.top+item.bottom)/2;RoundBox(dc,(RECT){item.right-22,cy-4,item.right-14,cy+4},ABM_THEME_COLOR_6AE1B9,5);}
        }
        RECT foot={R_AUDIO_PANEL.left+14,R_AUDIO_PANEL.bottom-26,R_AUDIO_PANEL.right-12,R_AUDIO_PANEL.bottom-7};
        Txt(dc,L"Bluetooth pairing is managed by Windows",foot,ABM_THEME_COLOR_8097AE,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
    }
    RECT vr={R_VOLUME.left,R_VOLUME.top-3,R_VOLUME.right,R_VOLUME.bottom+3};
    RoundBox(dc,vr,ABM_THEME_COLOR_243544,6);
    RECT volumeInner={vr.left+1,vr.top+1,vr.right-1,vr.bottom-1};
    RoundBox(dc,volumeInner,ABM_THEME_COLOR_111D2C,5);
    RECT volumeFill=volumeInner;
    volumeFill.right=volumeFill.left+(volumeFill.right-volumeFill.left)*g_volume/100;
    if(volumeFill.right>volumeFill.left)
        GradientRound(dc,volumeFill,ABM_THEME_COLOR_36B1A8,ABM_THEME_COLOR_8FD2B8,5);
    int vx=R_VOLUME.left+(R_VOLUME.right-R_VOLUME.left)*g_volume/100;
    RECT volumeHalo={vx-9,R_VOLUME.top-9,vx+9,R_VOLUME.bottom+9};
    RoundBox(dc,volumeHalo,ABM_THEME_COLOR_1B4C48,9);
    RECT vd={vx-5,R_VOLUME.top-6,vx+5,R_VOLUME.bottom+6};
    GradientRound(dc,vd,ABM_THEME_COLOR_FFFFFF,ABM_THEME_COLOR_D3F4EE,6);
    ABM_D2D_StrokeRound(&g_ui,(float)vd.left,(float)vd.top,(float)vd.right,(float)vd.bottom,6.0f,ABM_THEME_COLOR_70D3CA,1.0f);



    }
    } /* End permanent playback surface block. */

    /* Persistent contextual toolbar strip. Its hit target descriptions are
       concise and change only when the pointer enters a real command. */
    RECT commandStrip={0,client.bottom-30,client.right,client.bottom};
    GradientRect(dc,commandStrip,ABM_THEME_COLOR_09131F,ABM_THEME_COLOR_070D17);
    Fill(dc,(RECT){0,client.bottom-30,client.right,client.bottom-29},ABM_THEME_COLOR_2A4B5B);
    const wchar_t *commandHint=g_hoverChromeControl==1?L"COMMAND CENTRE  •  Browse playback, audio, library, streaming and application commands":
        g_hoverChromeControl==2?L"MEDIA LIBRARY  •  Open your queue, browse files, search media and choose a playback device":
        g_hoverButton==HIT_OPEN?L"ADD MEDIA  •  Choose audio or video files to play":
        g_hoverButton==HIT_PLAY?L"PLAY / PAUSE  •  Start or pause the current selection":
        g_hoverButton==HIT_PREV?L"PREVIOUS  •  Play the previous item in the queue":
        g_hoverButton==HIT_NEXT?L"NEXT  •  Play the next item in the queue":
        g_hoverButton==HIT_STOP?L"STOP  •  Stop current playback":
        g_hoverButton==HIT_SCAN?L"DISCOVER DEVICES  •  Search your network for compatible receivers":
        g_statusText[0]?g_statusText:L"ABM AV PLAYER  •  Capture the feeling. Relive every moment.";
    RECT hint={22,client.bottom-28,client.right-18,client.bottom-3};
    Txt(dc,commandHint,hint,ABM_THEME_COLOR_9FBECF,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

    /* Bottom-edge branding removed: playback and volume surfaces own this area. */

    /* Command Centre: restrained surfaces, consistent action hierarchy and
       clearly separated shortcut hints. Keep this overlay above the player. */
    if(g_menuProgress>0) {
        RECT menu=R_MENU_PANEL;
        menu.bottom=menu.top+((menu.bottom-menu.top)*g_menuProgress)/100;
        RECT shadow=menu;OffsetRect(&shadow,0,7);RoundBox(dc,shadow,ABM_THEME_COLOR_04080E,20);
        RECT menuGlow=menu;InflateRect(&menuGlow,2,2);RoundBox(dc,menuGlow,ABM_THEME_COLOR_070C14,22);
        Panel(dc,menu,ABM_THEME_COLOR_0C1420,ABM_THEME_COLOR_314C5E,18);
        RECT menuTop={menu.left+18,menu.top+2,menu.right-18,menu.top+4};
        GradientRect(dc,menuTop,ABM_THEME_COLOR_36B1A8,ABM_THEME_COLOR_52CDBE);
        RECT brandMark={menu.left+16,menu.top+10,menu.left+50,menu.top+44};
        RoundBox(dc,brandMark,ABM_THEME_COLOR_1B7771,ABM_UI_RADIUS_CONTROL);
        ABM_D2D_StrokeRound(&g_ui,(float)brandMark.left,(float)brandMark.top,(float)brandMark.right,(float)brandMark.bottom,
            (float)ABM_UI_RADIUS_CONTROL,ABM_THEME_COLOR_36B1A8,1.0f);
        Txt(dc,L"A",brandMark,ABM_THEME_COLOR_F2F8FD,g_headingFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT menuHeader={menu.left+62,menu.top+8,menu.right-16,menu.top+32};
        Txt(dc,L"COMMAND CENTRE",menuHeader,ABM_THEME_COLOR_F2F8FD,g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT menuSub={menu.left+63,menu.top+30,menu.right-16,menu.top+47};
        Txt(dc,L"PLAYBACK  ·  LIBRARY  ·  DEVICES  ·  APP",menuSub,ABM_THEME_COLOR_85A3B8,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawLine(dc,menu.left+16,menu.top+51,menu.right-16,menu.top+51,ABM_THEME_COLOR_273A4C,1);
        /* Group cards use the same ABM surface, border and radius tokens as
           the rest of the application. Labels collapse gracefully on short windows. */
        const wchar_t *sectionNames[5]={
            L"PLAYBACK & MEDIA",L"DEVICES & CASTING",L"PLAYBACK CONTROLS",
            L"STREAMING & TORRENT",L"APP & SUPPORT"
        };
        for(int group=0;group<5;group++){
            RECT groupBox=R_MENU_GROUPS[group];
            if(groupBox.top>=menu.bottom||groupBox.bottom<=groupBox.top)continue;
            if(groupBox.bottom>menu.bottom)groupBox.bottom=menu.bottom;
            RoundBox(dc,groupBox,ABM_THEME_COLOR_0D1825,ABM_UI_RADIUS_CARD);
            ABM_D2D_StrokeRound(&g_ui,(float)groupBox.left,(float)groupBox.top,(float)groupBox.right,(float)groupBox.bottom,
                (float)ABM_UI_RADIUS_CARD,ABM_THEME_COLOR_1C3445,0.8f);
            RECT section=R_MENU_SECTION_LABELS[group];
            if(section.bottom>section.top&&section.top<menu.bottom){
                if(section.bottom>menu.bottom)section.bottom=menu.bottom;
                RECT sectionText=section;sectionText.right=section.left+150;
                Txt(dc,sectionNames[group],sectionText,ABM_THEME_COLOR_63E1CD,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
                int lineX=sectionText.right+8;
                if(lineX<section.right)DrawLine(dc,lineX,(section.top+section.bottom)/2,section.right,(section.top+section.bottom)/2,ABM_THEME_COLOR_1C3445,1);
            }
        }
        const wchar_t *labels[19]={
            L"Open media",L"Find network devices",L"Cast to TV",L"Play / Pause",
            L"Fullscreen",L"Load playlist",L"Save playlist",L"Recent media",
            L"Shuffle: Off",L"Repeat current: Off",L"Advanced equalizer",
            L"Streaming services",L"Torrent Stream",L"Help & User Guide",
            L"Check for updates",L"License & Activation",L"Settings",L"About ABM AV Player",L"Exit player"
        };
        const wchar_t *shortcuts[19]={
            L"Ctrl+O",L"Ctrl+D",L"Ctrl+Shift+C",L"Space",
            L"F11",L"Ctrl+L",L"Ctrl+S",L"Ctrl+J",
            L"Ctrl+Shift+H",L"Ctrl+R",L"Ctrl+E",
            L"Ctrl+Shift+S",L"Ctrl+Shift+T",L"F1",
            L"Ctrl+Shift+U",L"Ctrl+Shift+L",L"Ctrl+,",L"Ctrl+I",L"Alt+F4"
        };
        const wchar_t *icons[19]={L"+",L"N",L"TV",L"▶",L"⛶",L"↓",L"↑",L"◷",L"⇄",L"↻",L"EQ",L"WEB",L"TOR",L"?",L"↻",L"KEY",L"⚙",L"i",L"×"};
        for(int i=0;i<19;i++) {
            RECT item=R_MENU_ITEMS[i];
            if(i==8)labels[8]=g_shuffle?L"Shuffle: On":L"Shuffle: Off";
            if(i==9)labels[9]=g_repeat==2?L"Repeat all: On":(g_repeat==1?L"Repeat current: On":L"Repeat: Off");
            if(item.bottom>menu.bottom)item.bottom=menu.bottom;
            if(item.top>=menu.bottom)continue;
            BOOL hot=(g_hoverMenuItem==i)||(g_menuOpen&&g_menuSelection==i);
            BOOL featured=(i==11||i==12);
            RECT row=item;InflateRect(&row,-1,-1);
            COLORREF fill=featured?(i==11?ABM_THEME_COLOR_0D373D:ABM_THEME_COLOR_3E271D):ABM_THEME_COLOR_193041;
            COLORREF edge=featured?(i==11?ABM_THEME_COLOR_2B7E7D:ABM_THEME_COLOR_8B5B3B):ABM_THEME_COLOR_1F3041;
            if(hot){fill=featured?(i==11?ABM_THEME_COLOR_145353:ABM_THEME_COLOR_5B3622):ABM_THEME_COLOR_1C3445;edge=ABM_THEME_COLOR_53B5AB;}
            if(featured||hot){
                RoundBox(dc,row,fill,featured?ABM_UI_RADIUS_CARD:ABM_UI_RADIUS_CONTROL);
                ABM_D2D_StrokeRound(&g_ui,(float)row.left,(float)row.top,(float)row.right,(float)row.bottom,
                    featured?(float)ABM_UI_RADIUS_CARD:(float)ABM_UI_RADIUS_CONTROL,hot?ABM_THEME_COLOR_69CFC0:edge,hot?1.25f:0.8f);
            }else if(i!=0){
                DrawLine(dc,item.left+46,item.bottom-1,item.right-8,item.bottom-1,ABM_THEME_COLOR_1C2F3D,1);
            }
            RECT glyph={item.left+(featured?14:9),item.top+(featured?8:4),
                        item.left+(featured?56:38),item.bottom-(featured?8:4)};
            COLORREF glyphFill=featured?(i==11?ABM_THEME_COLOR_1B7771:ABM_THEME_COLOR_89512D):ABM_THEME_COLOR_183449;
            if(hot)glyphFill=ABM_THEME_COLOR_257471;
            RoundBox(dc,glyph,glyphFill,featured?ABM_UI_RADIUS_CONTROL:ABM_UI_RADIUS_CONTROL);
            Txt(dc,icons[i],glyph,ABM_THEME_COLOR_E7F4F8,g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            RECT label={item.left+(featured?68:50),item.top+(featured?6:2),
                        item.right-112,item.bottom-(featured?6:2)};
            if(featured){
                RECT mainLabel=label;mainLabel.bottom=label.top+(item.bottom-item.top)/2+3;
                RECT subLabel=label;subLabel.top=mainLabel.bottom-1;
                Txt(dc,i==11?L"STREAMING TVs & SERVICES":L"TORRENT STREAMING",mainLabel,
                    ABM_THEME_COLOR_F9FCFF,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                Txt(dc,i==11?L"Open services, discover TVs and cast video":L"Stream torrents while they download",
                    subLabel,ABM_THEME_COLOR_BBD2DC,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            }else{
                COLORREF textColor=hot?ABM_THEME_COLOR_FFFFFF:ABM_THEME_COLOR_E2ECF6;
                Txt(dc,labels[i],label,textColor,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            }
            RECT shortcut={item.right-104,item.top+4,item.right-8,item.bottom-4};
            RoundBox(dc,shortcut,hot?ABM_THEME_COLOR_224B53:ABM_THEME_COLOR_0C1623,7);
            ABM_D2D_StrokeRound(&g_ui,(float)shortcut.left,(float)shortcut.top,(float)shortcut.right,
                (float)shortcut.bottom,7.0f,hot?ABM_THEME_COLOR_418383:ABM_THEME_COLOR_223343,0.7f);
            Txt(dc,shortcuts[i],shortcut,hot?ABM_THEME_COLOR_D3F4EE:ABM_THEME_COLOR_91ABBE,g_smallFont,
                DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            /* Quiet dividers reinforce the command groups without adding
               extra hit targets or changing menu order. */
            if(i==2||i==7||i==10||i==12||i==15||i==16){
                int y=item.bottom+1;
                if(y<menu.bottom-5)DrawLine(dc,item.left+8,y,item.right-8,y,ABM_THEME_COLOR_243544,1);
            }
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
        const char *name=libvlc_renderer_item_name(it);
        /* VLC can report the same physical TV through more than one renderer
           discovery service. Collapse identical friendly names in the UI,
           while retaining all underlying discovery items so removal of one
           service does not make the receiver disappear prematurely. */
        BOOL duplicateName=FALSE;
        if(name){
            for(size_t j=0;j<g_comboCount;j++){
                const char *existing=libvlc_renderer_item_name(g_comboItems[j]);
                if(existing && _stricmp(existing,name)==0){duplicateName=TRUE;break;}
            }
        }
        if(duplicateName){libvlc_renderer_item_release(it);continue;}
        wchar_t wide[256]=L"Network receiver";
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
        g_discoveryStartedTick=GetTickCount();
        g_discoveryTimedOut=FALSE;
        SetStatus(L"Searching for network receivers...");
        return;
    }

    EnterCriticalSection(&g_rendererLock);
    for(size_t i=0;i<g_rendererCount;i++)libvlc_renderer_item_release(g_rendererItems[i]);
    g_rendererCount=0;
    LeaveCriticalSection(&g_rendererLock);
    g_discoveryStartedTick=0;
    g_discoveryTimedOut=FALSE;

    libvlc_rd_description_t **services=NULL;
    size_t n=libvlc_renderer_discoverer_list_get(g_vlc,&services);
    if(!n||!services){
        SetStatus(L"No VLC receiver discovery plugins are available");
        ABM_MessageBoxW(g_main,
            L"LibVLC did not report any renderer-discovery services. Compatible VLC discovery plugins and their dependencies are required before network receivers can be discovered.",
            APP_NAME,MB_ICONWARNING);
        return;
    }

    wchar_t available[256]=L"", started[256]=L"", failed[256]=L"";
    unsigned int failedCount=0;
    BOOL hasMdnsRenderer=FALSE;
    BOOL mdnsRendererStarted=FALSE;
    for(size_t i=0;i<n && g_discovererCount<MAX_DISCOVERERS;i++){
        const char *serviceName=services[i]->psz_name;
        if(serviceName && _stricmp(serviceName,"microdns_renderer")==0)
            hasMdnsRenderer=TRUE;
        wchar_t name[64]=L"(unnamed)";
        if(serviceName)MultiByteToWideChar(CP_UTF8,0,serviceName,-1,name,_countof(name));
        if(name[0] && wcslen(available)<170){
            if(available[0])wcscat_s(available,_countof(available),L", ");
            wcscat_s(available,_countof(available),name);
        }

        libvlc_renderer_discoverer_t *d=serviceName?libvlc_renderer_discoverer_new(g_vlc,serviceName):NULL;
        if(!d){
            failedCount++;
            if(wcslen(failed)+wcslen(name)+3<_countof(failed)){
                if(failed[0])wcscat_s(failed,_countof(failed),L", ");
                wcscat_s(failed,_countof(failed),name);
            }
            continue;
        }

        libvlc_event_manager_t *em=libvlc_renderer_discoverer_event_manager(d);
        BOOL addAttached=(libvlc_event_attach(em,libvlc_RendererDiscovererItemAdded,RendererEvent,NULL)==0);
        BOOL delAttached=(libvlc_event_attach(em,libvlc_RendererDiscovererItemDeleted,RendererEvent,NULL)==0);
        if(addAttached && delAttached && libvlc_renderer_discoverer_start(d)==0){
            g_discoverers[g_discovererCount++]=d;
            if(serviceName && _stricmp(serviceName,"microdns_renderer")==0)
                mdnsRendererStarted=TRUE;
            if(wcslen(started)+wcslen(name)+3<_countof(started)){
                if(started[0])wcscat_s(started,_countof(started),L", ");
                wcscat_s(started,_countof(started),name);
            }
        }else{
            if(addAttached)libvlc_event_detach(em,libvlc_RendererDiscovererItemAdded,RendererEvent,NULL);
            if(delAttached)libvlc_event_detach(em,libvlc_RendererDiscovererItemDeleted,RendererEvent,NULL);
            libvlc_renderer_discoverer_release(d);
            failedCount++;
            if(wcslen(failed)+wcslen(name)+3<_countof(failed)){
                if(failed[0])wcscat_s(failed,_countof(failed),L", ");
                wcscat_s(failed,_countof(failed),name);
            }
        }
    }
    libvlc_renderer_discoverer_list_release(services,n);

    if(!g_discovererCount){
        wchar_t message[900];
        if(!hasMdnsRenderer){
            swprintf_s(message,_countof(message),
                L"Google Cast discovery is unavailable because LibVLC did not expose its mDNS renderer service (microdns_renderer).\n\nInstall a 64-bit VLC runtime that includes the services_discovery\\libmicrodns plugin, then rebuild the installer.\n\nAvailable services: %s",
                available[0]?available:L"(none)");
        }else{
            swprintf_s(message,_countof(message),
                L"No renderer-discovery service could be started.\n\nAvailable services: %s\nFailed services: %s\n\nCheck the VLC plugin dependencies and Windows network permissions.",
                available[0]?available:L"(none)",failed[0]?failed:L"(not reported)");
        }
        SetStatus(L"Receiver discovery could not start");
        ABM_MessageBoxW(g_main,message,APP_NAME,MB_ICONWARNING);
        return;
    }

    g_discoveryStartedTick=GetTickCount();
    g_discoveryTimedOut=FALSE;
    wchar_t status[512];
    if(!hasMdnsRenderer)
        swprintf_s(status,_countof(status),L"Searching with %s; Google Cast mDNS service is missing",started[0]?started:L"VLC discovery");
    else if(!mdnsRendererStarted)
        swprintf_s(status,_countof(status),L"Searching with %s; Google Cast service failed to start",started[0]?started:L"VLC discovery");
    else if(failedCount)
        swprintf_s(status,_countof(status),L"Searching for Google Cast and other receivers | %u service(s) failed",failedCount);
    else
        swprintf_s(status,_countof(status),L"Searching for Google Cast and network receivers...");
    SetStatus(status);
}
/* Central local-player command boundary. Keep all local playback state
   transitions on the Win32 UI thread; LibVLC's internal decoder threads only
   report events and must not mutate UI-owned playback state directly. */
static BOOL PlaybackPlayerAvailable(void) {
    return g_player != NULL;
}
static void PlaybackStopEngine(void) {
    if(PlaybackPlayerAvailable())libvlc_media_player_stop(g_player);
}
static BOOL PlaybackSetMedia(libvlc_media_t *media) {
    if(!PlaybackPlayerAvailable()||!media)return FALSE;
    VlcCodecVerificationReset();
    libvlc_media_player_set_media(g_player,media);
    return TRUE;
}
static int PlaybackStartEngine(void) {
    if(!PlaybackPlayerAvailable())return -1;
    return libvlc_media_player_play(g_player);
}
static BOOL PlaybackSetPaused(BOOL paused) {
    if(!PlaybackPlayerAvailable())return FALSE;
    if(paused)ResumeCaptureCurrent();
    libvlc_state_t state=libvlc_media_player_get_state(g_player);
    if(paused) {
        if(state!=libvlc_Playing&&state!=libvlc_Buffering)return FALSE;
        libvlc_media_player_set_pause(g_player,1);
        return TRUE;
    }
    if(state==libvlc_Paused) {
        libvlc_media_player_set_pause(g_player,0);
        return TRUE;
    }
    if(state==libvlc_Stopped||state==libvlc_Ended||state==libvlc_Error)return FALSE;
    return PlaybackStartEngine()==0;
}
static BOOL PlaybackTogglePause(void) {
    if(!PlaybackPlayerAvailable()||!g_hasMedia)return FALSE;
    libvlc_state_t state=libvlc_media_player_get_state(g_player);
    if(state==libvlc_Playing||state==libvlc_Buffering)return PlaybackSetPaused(TRUE);
    if(state==libvlc_Paused)return PlaybackSetPaused(FALSE);
    return FALSE;
}
static BOOL PlaybackSeekTo(int64_t requestedMs) {
    if(!PlaybackPlayerAvailable()||!g_hasMedia)return FALSE;
    libvlc_state_t state=libvlc_media_player_get_state(g_player);
    if(state==libvlc_Stopped||state==libvlc_Ended||state==libvlc_Error)return FALSE;
    int64_t length=libvlc_media_player_get_length(g_player);
    if(length<=0)return FALSE;
    if(requestedMs<0)requestedMs=0;
    if(requestedMs>length)requestedMs=length;
    libvlc_media_player_set_time(g_player,requestedMs);
    return TRUE;
}

static void SelectRenderer(void) {
    int s=(int)SendMessageW(g_renderers,CB_GETCURSEL,0,0);
    libvlc_renderer_item_t *next=NULL;
    if(s>0&&(size_t)(s-1)<g_comboCount)next=libvlc_renderer_item_hold(g_comboItems[s-1]);
    if(g_selectedRenderer)libvlc_renderer_item_release(g_selectedRenderer);
    g_selectedRenderer=next;
    g_rendererApplied=FALSE;

    if(g_selectedRenderer){
        wchar_t selectedName[256]=L"Selected receiver";
        if(s>0)SendMessageW(g_renderers,CB_GETLBTEXT,(WPARAM)s,(LPARAM)selectedName);
        wchar_t status[320];
        if(g_hasMedia && g_current>=0 && g_current<g_playlistCount)
            swprintf_s(status,_countof(status),L"Connecting to %s...",selectedName);
        else
            swprintf_s(status,_countof(status),L"Ready to cast to %s when playback starts",selectedName);
        SetStatus(status);
    }else{
        SetStatus(L"Local playback selected");
    }

    /* Renderer selection must happen before play(). Recreate the player to
       move an active item to a different output without changing its position. */
    if(g_hasMedia&&g_current>=0&&g_current<g_playlistCount){
        int64_t resumeAt=g_useForcedResume?g_forcedResumeMs:libvlc_media_player_get_time(g_player);
        libvlc_state_t oldState=libvlc_media_player_get_state(g_player);
        BOOL wasPlaying=g_useForcedResume || (oldState!=libvlc_Paused && oldState!=libvlc_Stopped && oldState!=libvlc_Ended && oldState!=libvlc_Error);
        if(resumeAt<0)resumeAt=0;

        PlaybackStopEngine();
        libvlc_media_player_release(g_player);
        g_player=libvlc_media_player_new(g_vlc);
        if(!g_player){
            g_hasMedia=FALSE;g_pendingResume=FALSE;g_rendererApplied=FALSE;
            SetStatus(L"Could not recreate playback engine");
            ABM_MessageBoxW(g_main,L"Could not recreate the playback engine. Select the media again to retry.",APP_NAME,MB_ICONERROR);
            return;
        }
        libvlc_media_player_set_hwnd(g_player,g_video);
        if(!g_selectedRenderer)ApplyAudioOutputDevice();

        if(g_selectedRenderer && libvlc_media_player_set_renderer(g_player,g_selectedRenderer)!=0){
            g_hasMedia=FALSE;g_pendingResume=FALSE;g_rendererApplied=FALSE;
            SetStatus(L"Receiver rejected the renderer connection");
            ABM_MessageBoxW(g_main,
                L"LibVLC could not attach the selected receiver. Playback was not redirected to this PC. Refresh discovery, select the receiver again, or check whether it supports this renderer protocol.",
                APP_NAME,MB_ICONERROR);
            InvalidateRect(g_main,NULL,FALSE);
            return;
        }
        if(g_selectedRenderer)g_rendererApplied=TRUE;

        char utf8[PATH_CAP*4];
        if(WideCharToMultiByte(CP_UTF8,0,g_playlist[g_current],-1,utf8,sizeof(utf8),NULL,NULL)>0){
            libvlc_media_t *m=libvlc_media_new_path(g_vlc,utf8);
            if(m){
                if(g_replayGain)libvlc_media_add_option(m,":audio-replay-gain-mode=track");
                if(g_compressor){
                    libvlc_media_add_option(m,":audio-filter=compressor");
                    libvlc_media_add_option(m,":compressor-threshold=-18");
                    libvlc_media_add_option(m,":compressor-ratio=4");
                    libvlc_media_add_option(m,":compressor-attack=20");
                    libvlc_media_add_option(m,":compressor-release=250");
                    libvlc_media_add_option(m,":compressor-makeup-gain=0");
                }
                ApplyAudioOutputDevice();
                ApplySubtitleAppearanceOption(m);AddAdvancedCachingOptions(m);AddAdvancedPerformanceOptions(m);
                PlaybackSetMedia(m);ApplyAdvancedAspectRatio();
                libvlc_video_set_spu_delay(g_player,g_advancedSubtitleDelayUs);
                libvlc_audio_set_delay(g_player,g_audioDelayUs);
                ApplyEqProfile();
                int playResult=PlaybackStartEngine();
                libvlc_media_release(m);
                if(playResult==0){
                    if(!g_selectedRenderer)libvlc_audio_set_delay(g_player,g_audioDelayUs);
                    g_pendingResume=TRUE;g_pendingResumeMs=resumeAt;
                    g_pendingResumePause=!wasPlaying;g_pendingResumeTicks=0;
                    if(g_selectedRenderer)SetStatus(L"Connecting to selected receiver...");
                }else{
                    /* A failed Cast request is never replayed locally without
                       an explicit user action to select local output. */
                    g_pendingResume=FALSE;g_hasMedia=FALSE;g_rendererApplied=FALSE;
                    SetStatus(g_selectedRenderer?
                        L"Cast request failed - local playback was not started":
                        L"Playback could not restart on the selected output");
                    ABM_MessageBoxW(g_main,
                        g_selectedRenderer?
                        L"LibVLC could not start playback on the selected receiver. No local fallback was attempted. Check receiver compatibility and network connectivity, then retry.":
                        L"Playback could not restart on the selected output. Select the media again to retry.",
                        APP_NAME,MB_ICONERROR);
                }
            }else{
                g_hasMedia=FALSE;g_pendingResume=FALSE;
                SetStatus(L"Could not create media for the selected output");
            }
        }else{
            g_hasMedia=FALSE;g_pendingResume=FALSE;
            SetStatus(L"Could not encode the media path");
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
/* Called by the Streaming Centre so it can reuse the main player's
   existing LibVLC renderer-discovery and receiver-selection workflow. */
void ABM_Cast_ShowPicker(HWND owner) {
    if(!g_main || !IsWindow(g_main) || (owner && owner!=g_main))return;
    g_drawerOpen=TRUE;
    g_drawerProgress=100;
    g_drawerSettingsOpen=TRUE;
    g_receiverPopup=FALSE;
    StartDiscovery();
    SetStatus(L"Choose Playback Output, then select your Google Cast / network receiver. The selection applies to LibVLC media playback.");
    InvalidateRect(g_main,NULL,FALSE);
    ShowWindow(g_main,SW_RESTORE);
    SetForegroundWindow(g_main);
}

/* Streaming Centre and the local player share one LibVLC renderer-discovery
   session. Starting duplicate discoverers for the same services on one LibVLC
   instance is unnecessary and can cause inconsistent discovery results.
   The two UIs keep independent selections and playback engines. */
void ABM_Cast_StreamDiscoveryStart(void) {
    if(!g_vlc)return;
    StartDiscovery();
}
void ABM_Cast_StreamDiscoveryStop(void) {
    /* The Streaming Centre must not stop the local player's discovery session. */
    if(g_streamSelectedRenderer) {
        libvlc_renderer_item_release(g_streamSelectedRenderer);
        g_streamSelectedRenderer=NULL;
    }
}
unsigned int ABM_Cast_StreamDeviceCount(void) {
    unsigned int count=0;
    char names[MAX_RENDERERS][256]={{0}};
    EnterCriticalSection(&g_rendererLock);
    for(size_t i=0;i<g_rendererCount&&count<MAX_RENDERERS;i++) {
        const char *name=libvlc_renderer_item_name(g_rendererItems[i]);
        if(!name||!name[0])continue;
        BOOL duplicate=FALSE;
        for(unsigned int j=0;j<count;j++)if(_stricmp(names[j],name)==0){duplicate=TRUE;break;}
        if(!duplicate){strncpy_s(names[count],_countof(names[count]),name,_TRUNCATE);count++;}
    }
    LeaveCriticalSection(&g_rendererLock);
    return count;
}
BOOL ABM_Cast_StreamDeviceName(unsigned int index,wchar_t *buffer,unsigned int capacity) {
    if(!buffer||capacity==0)return FALSE;
    buffer[0]=0;
    unsigned int count=0;
    BOOL found=FALSE;
    EnterCriticalSection(&g_rendererLock);
    for(size_t i=0;i<g_rendererCount;i++) {
        const char *name=libvlc_renderer_item_name(g_rendererItems[i]);
        if(!name||!name[0])continue;
        BOOL duplicate=FALSE;
        for(size_t j=0;j<i;j++) {
            const char *prior=libvlc_renderer_item_name(g_rendererItems[j]);
            if(prior&&_stricmp(prior,name)==0){duplicate=TRUE;break;}
        }
        if(duplicate)continue;
        if(count++==index){found=MultiByteToWideChar(CP_UTF8,0,name,-1,buffer,(int)capacity)>0;break;}
    }
    LeaveCriticalSection(&g_rendererLock);
    return found;
}
BOOL ABM_Cast_StreamSelectDevice(unsigned int index) {
    libvlc_renderer_item_t *selected=NULL;
    unsigned int count=0;
    EnterCriticalSection(&g_rendererLock);
    for(size_t i=0;i<g_rendererCount;i++) {
        const char *name=libvlc_renderer_item_name(g_rendererItems[i]);
        if(!name||!name[0])continue;
        BOOL duplicate=FALSE;
        for(size_t j=0;j<i;j++) {
            const char *prior=libvlc_renderer_item_name(g_rendererItems[j]);
            if(prior&&_stricmp(prior,name)==0){duplicate=TRUE;break;}
        }
        if(duplicate)continue;
        if(count++==index){selected=libvlc_renderer_item_hold(g_rendererItems[i]);break;}
    }
    LeaveCriticalSection(&g_rendererLock);
    if(!selected)return FALSE;
    if(g_streamSelectedRenderer)libvlc_renderer_item_release(g_streamSelectedRenderer);
    g_streamSelectedRenderer=selected;
    return TRUE;
}
void ABM_Cast_StreamClearSelection(void) {
    if(g_streamSelectedRenderer){libvlc_renderer_item_release(g_streamSelectedRenderer);g_streamSelectedRenderer=NULL;}
}

/* Resolve and reject local/private destinations before handing a browser URL
   to LibVLC. This intentionally does not disable redirects: providers commonly
   use CDN redirects, and LibVLC must retain that behavior for compatibility. */
static BOOL CastAddressIsPublic(const struct addrinfoW *entry) {
    if(entry->ai_family==AF_INET && entry->ai_addrlen>=sizeof(struct sockaddr_in)) {
        const unsigned char *a=(const unsigned char *)&((const struct sockaddr_in *)entry->ai_addr)->sin_addr;
        if(a[0]==0 || a[0]==10 || a[0]==127 || a[0]>=224 ||
           (a[0]==100&&a[1]>=64&&a[1]<=127) ||
           (a[0]==169&&a[1]==254) ||
           (a[0]==172&&a[1]>=16&&a[1]<=31) ||
           (a[0]==192&&(a[1]==0||a[1]==168)) ||
           (a[0]==198&&(a[1]==18||a[1]==19||a[1]==51)) ||
           (a[0]==203&&a[1]==0&&a[2]==113)) return FALSE;
        return TRUE;
    }
    if(entry->ai_family==AF_INET6 && entry->ai_addrlen>=sizeof(struct sockaddr_in6)) {
        const unsigned char *a=(const unsigned char *)&((const struct sockaddr_in6 *)entry->ai_addr)->sin6_addr;
        static const unsigned char zero[16]={0};
        static const unsigned char loopback[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        if(memcmp(a,zero,16)==0 || memcmp(a,loopback,16)==0 ||
           (a[0]&0xfe)==0xfc || (a[0]==0xfe&&(a[1]&0xc0)==0x80) || a[0]==0xff)
            return FALSE;
        /* IPv4-mapped IPv6 addresses must pass the IPv4 policy too. */
        if(a[0]==0&&a[1]==0&&a[2]==0&&a[3]==0&&a[4]==0&&a[5]==0&&
           a[6]==0&&a[7]==0&&a[8]==0&&a[9]==0&&a[10]==0xff&&a[11]==0xff) {
            unsigned char v4[4]={a[12],a[13],a[14],a[15]};
            if(v4[0]==0||v4[0]==10||v4[0]==127||v4[0]>=224||
               (v4[0]==100&&v4[1]>=64&&v4[1]<=127)||
               (v4[0]==169&&v4[1]==254)||
               (v4[0]==172&&v4[1]>=16&&v4[1]<=31)||
               (v4[0]==192&&(v4[1]==0||v4[1]==168))||
               (v4[0]==198&&(v4[1]==18||v4[1]==19||v4[1]==51))||
               (v4[0]==203&&v4[1]==0&&v4[2]==113)) return FALSE;
        }
        return TRUE;
    }
    return FALSE;
}
static BOOL CastUrlDestinationIsPublic(const wchar_t *url) {
    URL_COMPONENTSW parts={0};
    wchar_t host[256]={0};
    struct addrinfoW hints={0},*addresses=NULL,*it;
    BOOL valid=FALSE,haveAddress=FALSE;
    parts.dwStructSize=sizeof(parts);
    parts.dwSchemeLength=(DWORD)-1;
    parts.dwHostNameLength=(DWORD)-1;
    parts.dwUserNameLength=(DWORD)-1;
    parts.dwPasswordLength=(DWORD)-1;
    if(!WinHttpCrackUrl(url,0,0,&parts) ||
       (parts.nScheme!=INTERNET_SCHEME_HTTP&&parts.nScheme!=INTERNET_SCHEME_HTTPS) ||
       parts.dwHostNameLength==0 || parts.dwHostNameLength>=_countof(host) ||
       parts.dwUserNameLength!=0 || parts.dwPasswordLength!=0) return FALSE;
    wcsncpy_s(host,_countof(host),parts.lpszHostName,parts.dwHostNameLength);
    /* A trailing DNS root dot does not make a local name public. */
    size_t n=wcslen(host);
    while(n&&host[n-1]==L'.')host[--n]=0;
    if(!n || wcschr(host,L'%') || wcschr(host,L'/') || wcschr(host,L'\\') ||
       wcschr(host,L'@') || !_wcsicmp(host,L"localhost") ||
       !_wcsicmp(host,L"localhost.localdomain") ||
       (n>=10&&_wcsicmp(host+n-10,L".localhost")==0) ||
       (n>=5&&_wcsicmp(host+n-5,L".local")==0) ||
       !wcschr(host,L'.')) return FALSE;
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype=SOCK_STREAM;
    if(GetAddrInfoW(host,NULL,&hints,&addresses)!=0 || !addresses)return FALSE;
    valid=TRUE;
    for(it=addresses;it;it=it->ai_next) {
        if(it->ai_family!=AF_INET&&it->ai_family!=AF_INET6)continue;
        haveAddress=TRUE;
        if(!CastAddressIsPublic(it)){valid=FALSE;break;}
    }
    FreeAddrInfoW(addresses);
    return valid&&haveAddress;
}
/* Cast a directly-addressable browser video URL through LibVLC.
   Direct HTTP(S) URLs remain supported; DRM/MSE pages generally do not
   expose a reusable direct URL. */
BOOL ABM_Cast_StreamUrl(const wchar_t *url) {
    if(!g_main || !IsWindow(g_main) || !url)return FALSE;
    size_t length=wcslen(url);
    if(length<9 || length>=8192 ||
       (_wcsnicmp(url,L"https://",8)!=0 && _wcsnicmp(url,L"http://",7)!=0)){
        SetStatus(L"This page does not expose a directly castable HTTP(S) video URL. DRM, blob and segmented browser streams are not supported.");
        return FALSE;
    }
    for(size_t i=0;i<length;i++)if(url[i]<0x20 || url[i]==L'\\' || url[i]==L'"' || url[i]==L'<' || url[i]==L'>'){
        SetStatus(L"The browser returned an invalid media URL.");
        return FALSE;
    }
    if(!CastUrlDestinationIsPublic(url)){
        SetStatus(L"Cast blocked this URL because its destination is local, private, malformed or could not be safely resolved.");
        return FALSE;
    }
    if(!g_streamSelectedRenderer){
        SetStatus(L"No streaming receiver is selected. Open Find TVs in the Streaming Centre and select a receiver there.");
        return FALSE;
    }
    if(!g_streamCastPlayer){
        SetStatus(L"The independent streaming cast engine is unavailable. Local playback has not been changed.");
        return FALSE;
    }
    char *utf8=(char*)malloc(length*4+1);
    if(!utf8)return FALSE;
    int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,url,-1,utf8,(int)(length*4+1),NULL,NULL);
    if(bytes<=0){free(utf8);SetStatus(L"Could not encode the browser video URL.");return FALSE;}
    libvlc_media_t *media=libvlc_media_new_location(g_vlc,utf8);
    free(utf8);
    if(!media){SetStatus(L"LibVLC could not open the browser video URL.");return FALSE;}
    /* Do not touch g_player: local playback and local casting remain independent. */
    libvlc_media_player_stop(g_streamCastPlayer);
    if(libvlc_media_player_set_renderer(g_streamCastPlayer,g_streamSelectedRenderer)!=0){
        libvlc_media_release(media);
        SetStatus(L"The selected receiver rejected the browser video stream.");
        return FALSE;
    }
    /* Streaming output deliberately does not modify local renderer or artwork state. */
    libvlc_media_player_set_media(g_streamCastPlayer,media);
    int result=libvlc_media_player_play(g_streamCastPlayer);
    libvlc_media_release(media);
    if(result!=0){
        SetStatus(L"The receiver could not start this direct video URL. Local playback was left unchanged.");
        return FALSE;
    }
    SetStatus(L"Streaming cast handoff started. Receiver playback confirmation is not available from this direct-URL path.");
    return TRUE;
}
static BOOL EnsureDataDirectory(void) {
    if(g_dataDir[0])return TRUE;
    wchar_t base[PATH_CAP];
    if(FAILED(SHGetFolderPathW(NULL,CSIDL_APPDATA|CSIDL_FLAG_CREATE,NULL,SHGFP_TYPE_CURRENT,base)))return FALSE;
    if(swprintf_s(g_dataDir,PATH_CAP,L"%s\\ABMAVPlayer",base)<0)return FALSE;
    if(!CreateDirectoryW(g_dataDir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return FALSE;
    return TRUE;
}
static BOOL EnsureTorrentCacheBaseDir(void) {
    if(g_torrentCacheBaseDir[0]) {
        if(!CreateDirectoryW(g_torrentCacheBaseDir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return FALSE;
        return TRUE;
    }
    wchar_t settingsPath[PATH_CAP],legacyCache[PATH_CAP];
    if(g_dataDir[0]) {
        StringCchPrintfW(settingsPath,_countof(settingsPath),L"%s\\settings.ini",g_dataDir);
        GetPrivateProfileStringW(L"Torrent",L"CacheBaseDir",L"",g_torrentCacheBaseDir,_countof(g_torrentCacheBaseDir),settingsPath);
        if(g_torrentCacheBaseDir[0]) {
            if(!CreateDirectoryW(g_torrentCacheBaseDir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return FALSE;
            return TRUE;
        }
        StringCchPrintfW(legacyCache,_countof(legacyCache),L"%s\\torrent-cache",g_dataDir);
        DWORD legacy=GetFileAttributesW(legacyCache);
        if(legacy!=INVALID_FILE_ATTRIBUTES&&(legacy&FILE_ATTRIBUTE_DIRECTORY)) {
            wcsncpy_s(g_torrentCacheBaseDir,_countof(g_torrentCacheBaseDir),g_dataDir,_TRUNCATE);
            return TRUE;
        }
    }
    PWSTR downloads=NULL;
    if(SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Downloads,KF_FLAG_CREATE,NULL,&downloads))&&downloads) {
        StringCchPrintfW(g_torrentCacheBaseDir,_countof(g_torrentCacheBaseDir),L"%s\\ABM AV Player",downloads);
        CoTaskMemFree(downloads);
    } else {
        wchar_t profile[PATH_CAP]={0};
        if(FAILED(SHGetFolderPathW(NULL,CSIDL_PROFILE,NULL,SHGFP_TYPE_CURRENT,profile)))return FALSE;
        StringCchPrintfW(g_torrentCacheBaseDir,_countof(g_torrentCacheBaseDir),L"%s\\Downloads\\ABM AV Player",profile);
    }
    if(!CreateDirectoryW(g_torrentCacheBaseDir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return FALSE;
    return TRUE;
}
static BOOL EnsureTorrentDownloadDir(void) {
    if(!g_torrentDownloadDir[0]) {
        if(!EnsureTorrentCacheBaseDir())return FALSE;
        if(FAILED(StringCchPrintfW(g_torrentDownloadDir,_countof(g_torrentDownloadDir),L"%s\\Torrent Downloads",g_torrentCacheBaseDir)))return FALSE;
    }
    SHCreateDirectoryExW(NULL,g_torrentDownloadDir,NULL);
    DWORD attrs=GetFileAttributesW(g_torrentDownloadDir);
    return attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_DIRECTORY)!=0;
}
static void SavePersistentState(void);
/* Missing files remain in the library so users can relink them later. */
static void PruneUnavailablePlaylistItems(void) {
    for(int i=0;i<MAX_PLAYLIST;i++)g_missingMedia[i]=FALSE;
    for(int i=0;i<g_playlistCount;i++){
        DWORD attrs=GetFileAttributesW(g_playlist[i]);
        g_missingMedia[i]=(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_DIRECTORY));
    }
    RefreshResumeFlags();
}
static void SavePersistentState(void) {
    if(!EnsureDataDirectory())return;
    wchar_t path[PATH_CAP];swprintf_s(path,PATH_CAP,L"%s\\library.m3u",g_dataDir);
    FILE *fp=NULL;if(_wfopen_s(&fp,path,L"wb")==0&&fp){
        fputs("#EXTM3U\n",fp);
        for(int i=0;i<g_playlistCount;i++){int n=WideCharToMultiByte(CP_UTF8,0,g_playlist[i],-1,NULL,0,NULL,NULL);if(n>1){char *s=(char*)malloc((size_t)n);if(s){WideCharToMultiByte(CP_UTF8,0,g_playlist[i],-1,s,n,NULL,NULL);fputs(s,fp);fputc('\n',fp);free(s);}}}
        fclose(fp);
    }
    swprintf_s(path,PATH_CAP,L"%s\\favorites.m3u",g_dataDir);
    fp=NULL;if(_wfopen_s(&fp,path,L"wb")==0&&fp){fputs("#EXTM3U\n",fp);for(int i=0;i<g_favoriteCount;i++){int n=WideCharToMultiByte(CP_UTF8,0,g_favorites[i],-1,NULL,0,NULL,NULL);if(n>1){char *v=(char*)malloc((size_t)n);if(v){WideCharToMultiByte(CP_UTF8,0,g_favorites[i],-1,v,n,NULL,NULL);fputs(v,fp);fputc('\n',fp);free(v);}}}fclose(fp);}
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
    swprintf_s(value,32,L"%d",g_repeat);WritePrivateProfileStringW(L"Player",L"Repeat",value,path);
    swprintf_s(value,32,L"%d",(int)(g_playbackRate*100.0f));WritePrivateProfileStringW(L"Player",L"RatePercent",value,path);
    swprintf_s(value,32,L"%llu",(unsigned long long)g_torrentCacheQuotaGiB);WritePrivateProfileStringW(L"Torrent",L"CacheQuotaGiB",value,path);
    swprintf_s(value,32,L"%d",g_torrentDownloadLimitKiB);WritePrivateProfileStringW(L"Torrent",L"DownloadLimitKiB",value,path);
    swprintf_s(value,32,L"%d",g_torrentUploadLimitKiB);WritePrivateProfileStringW(L"Torrent",L"UploadLimitKiB",value,path);
    WritePrivateProfileStringW(L"Torrent",L"CacheBaseDir",g_torrentCacheBaseDir,path);
    WritePrivateProfileStringW(L"Torrent",L"DownloadDir",g_torrentDownloadDir,path);
}
static int ResumeFind(const wchar_t *path) {
    if(!path||!path[0])return -1;
    for(int i=0;i<g_resumeCount;i++)if(!_wcsicmp(g_resumeEntries[i].path,path))return i;
    return -1;
}
static int64_t ResumePositionForPath(const wchar_t *path) {
    int i=ResumeFind(path);return i>=0?g_resumeEntries[i].positionMs:0;
}
static void FormatResumeLabel(const wchar_t *path,wchar_t *out,size_t capacity) {
    if(!out||capacity==0)return;
    int i=ResumeFind(path);if(i<0){StringCchCopyW(out,capacity,L"");return;}
    int64_t duration=g_resumeEntries[i].durationMs,position=g_resumeEntries[i].positionMs;
    if(duration<=0){StringCchCopyW(out,capacity,L"CONTINUE WATCHING");return;}
    int percent=(int)((position*100)/duration);if(percent<0)percent=0;if(percent>99)percent=99;
    wchar_t remaining[32];FormatMediaDuration(duration-position,remaining,_countof(remaining));
    StringCchPrintfW(out,capacity,L"CONTINUE  %d%%  ·  %s LEFT",percent,remaining);
}
static void RefreshResumeFlags(void) {
    for(int i=0;i<MAX_PLAYLIST;i++)g_hasResume[i]=FALSE;
    for(int i=0;i<g_playlistCount;i++)g_hasResume[i]=(ResumeFind(g_playlist[i])>=0);
}
typedef struct { int count; ABMResumeEntry entries[MAX_RESUME_ENTRIES]; wchar_t file[PATH_CAP]; } ABMResumeWrite;
static DWORD WINAPI ResumeWriteThreadProc(LPVOID parameter) {
    ABMResumeWrite *job=(ABMResumeWrite*)parameter;
    wchar_t temp[PATH_CAP];swprintf_s(temp,_countof(temp),L"%s.tmp",job->file);
    FILE *fp=NULL;
    if(_wfopen_s(&fp,temp,L"wt, ccs=UTF-8")==0&&fp){
        for(int i=0;i<job->count;i++)
            fwprintf(fp,L"%lld\t%lld\t%s\n",(long long)job->entries[i].positionMs,
                (long long)job->entries[i].durationMs,job->entries[i].path);
        fflush(fp);fclose(fp);
        MoveFileExW(temp,job->file,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }
    free(job);return 0;
}
static void ResumeSaveAsync(BOOL waitForWrite) {
    if(!EnsureDataDirectory())return;
    if(g_resumeWriteThread){
        DWORD state=WaitForSingleObject(g_resumeWriteThread,waitForWrite?INFINITE:0);
        if(state==WAIT_TIMEOUT)return;
        CloseHandle(g_resumeWriteThread);g_resumeWriteThread=NULL;
    }
    ABMResumeWrite *job=(ABMResumeWrite*)calloc(1,sizeof(*job));if(!job)return;
    job->count=g_resumeCount;memcpy(job->entries,g_resumeEntries,sizeof(g_resumeEntries));
    swprintf_s(job->file,_countof(job->file),L"%s\\resume.tsv",g_dataDir);
    g_resumeWriteThread=CreateThread(NULL,0,ResumeWriteThreadProc,job,0,NULL);
    if(!g_resumeWriteThread){free(job);return;}
    if(waitForWrite){WaitForSingleObject(g_resumeWriteThread,INFINITE);CloseHandle(g_resumeWriteThread);g_resumeWriteThread=NULL;}
}
static void LoadResumeState(void) {
    wchar_t file[PATH_CAP];swprintf_s(file,_countof(file),L"%s\\resume.tsv",g_dataDir);
    FILE *fp=NULL;if(_wfopen_s(&fp,file,L"rt, ccs=UTF-8")!=0||!fp)return;
    wchar_t line[PATH_CAP+96];
    while(g_resumeCount<MAX_RESUME_ENTRIES&&fgetws(line,_countof(line),fp)){
        line[wcscspn(line,L"\r\n")]=0;
        wchar_t *a=wcschr(line,L'\t');if(!a)continue;*a++=0;
        wchar_t *b=wcschr(a,L'\t');if(!b)continue;*b++=0;
        wchar_t *end=NULL;long long pos=_wcstoi64(line,&end,10);if(end==line||pos<5000)continue;
        long long dur=_wcstoi64(a,&end,10);if(end==a||dur<=0||pos>=dur*95/100||!b[0]||ResumeFind(b)>=0)continue;
        ABMResumeEntry *e=&g_resumeEntries[g_resumeCount++];
        wcsncpy_s(e->path,PATH_CAP,b,_TRUNCATE);e->positionMs=pos;e->durationMs=dur;
    }
    fclose(fp);
}
static void ResumeCaptureCurrent(void) {
    if(!g_hasMedia||!g_player||g_current<0||g_current>=g_playlistCount)return;
    int64_t pos=libvlc_media_player_get_time(g_player),dur=libvlc_media_player_get_length(g_player);
    if(pos<0||dur<=0)return;
    int i=ResumeFind(g_playlist[g_current]);
    if(pos<5000||pos>=dur*95/100){
        if(i>=0){for(int j=i+1;j<g_resumeCount;j++)g_resumeEntries[j-1]=g_resumeEntries[j];g_resumeCount--;}
    }else{
        if(i<0){if(g_resumeCount>=MAX_RESUME_ENTRIES){for(int j=1;j<g_resumeCount;j++)g_resumeEntries[j-1]=g_resumeEntries[j];g_resumeCount--;}i=g_resumeCount++;}
        wcsncpy_s(g_resumeEntries[i].path,PATH_CAP,g_playlist[g_current],_TRUNCATE);
        g_resumeEntries[i].positionMs=pos;g_resumeEntries[i].durationMs=dur;
    }
    RefreshResumeFlags();g_lastResumeSaveTick=GetTickCount();ResumeSaveAsync(FALSE);
}
static void RelinkSavedCollections(const wchar_t *oldPath,const wchar_t *newPath) {
    wchar_t folder[PATH_CAP],pattern[PATH_CAP];
    if(FAILED(StringCchPrintfW(folder,_countof(folder),L"%s\\collections",g_dataDir)))return;
    if(FAILED(StringCchPrintfW(pattern,_countof(pattern),L"%s\\*.m3u",folder)))return;
    WIN32_FIND_DATAW fd;HANDLE find=FindFirstFileW(pattern,&fd);if(find==INVALID_HANDLE_VALUE)return;
    do {
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
        wchar_t file[PATH_CAP],temp[PATH_CAP];
        if(FAILED(StringCchPrintfW(file,_countof(file),L"%s\\%s",folder,fd.cFileName)))continue;
        if(FAILED(StringCchPrintfW(temp,_countof(temp),L"%s.tmp",file)))continue;
        FILE *in=NULL,*out=NULL;
        if(_wfopen_s(&in,file,L"rt, ccs=UTF-8")!=0||!in)continue;
        if(_wfopen_s(&out,temp,L"wt, ccs=UTF-8")!=0||!out){fclose(in);continue;}
        wchar_t line[PATH_CAP+8];
        while(fgetws(line,_countof(line),in)){
            size_t n=wcslen(line);while(n&&(line[n-1]==L'\r'||line[n-1]==L'\n'))line[--n]=0;
            fwprintf(out,L"%s\n",!_wcsicmp(line,oldPath)?newPath:line);
        }
        fclose(in);fflush(out);fclose(out);
        MoveFileExW(temp,file,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    } while(FindNextFileW(find,&fd));
    FindClose(find);
}
static void RelinkMissingMedia(int index) {
    if(index<0||index>=g_playlistCount)return;
    OPENFILENAMEW ofn={0};wchar_t replacement[PATH_CAP]=L"";
    ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=g_main;ofn.lpstrFile=replacement;ofn.nMaxFile=PATH_CAP;
    ofn.lpstrFilter=L"Media files\0*.*\0All files\0*.*\0";ofn.lpstrTitle=L"Relink missing media";
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER;
    if(!GetOpenFileNameW(&ofn))return;
    DWORD attrs=GetFileAttributesW(replacement);
    if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_DIRECTORY)){
        ABM_MessageBoxW(g_main,L"The selected replacement file is not accessible.",APP_NAME,MB_ICONWARNING);return;
    }
    wchar_t oldPath[PATH_CAP];wcsncpy_s(oldPath,PATH_CAP,g_playlist[index],_TRUNCATE);
    if(g_hasMedia&&g_current==index){ResumeCaptureCurrent();PlaybackStopEngine();g_hasMedia=FALSE;g_current=-1;}
    for(int i=0;i<g_playlistCount;i++)if(!_wcsicmp(g_playlist[i],oldPath))wcsncpy_s(g_playlist[i],PATH_CAP,replacement,_TRUNCATE);
    for(int i=0;i<g_favoriteCount;i++)if(!_wcsicmp(g_favorites[i],oldPath))wcsncpy_s(g_favorites[i],PATH_CAP,replacement,_TRUNCATE);
    for(int i=0;i<g_recentCount;i++)if(!_wcsicmp(g_recent[i],oldPath))wcsncpy_s(g_recent[i],PATH_CAP,replacement,_TRUNCATE);
    RelinkSavedCollections(oldPath,replacement);
    int ri=ResumeFind(oldPath);if(ri>=0)wcsncpy_s(g_resumeEntries[ri].path,PATH_CAP,replacement,_TRUNCATE);
    RefreshMissingMediaFlags();
    SavePersistentState();ResumeSaveAsync(FALSE);
    SetStatus(L"Media relinked. Library references and viewing progress were preserved.");
    InvalidateRect(g_main,NULL,FALSE);
}
static void RefreshMissingMediaFlags(void) {
    for(int i=0;i<g_playlistCount;i++){
        DWORD attrs=GetFileAttributesW(g_playlist[i]);
        g_missingMedia[i]=(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_DIRECTORY));
    }
    RefreshResumeFlags();
}
static void LoadPersistentState(void) {
    if(!EnsureDataDirectory())return;
    wchar_t path[PATH_CAP];swprintf_s(path,PATH_CAP,L"%s\\settings.ini",g_dataDir);
    g_volume=GetPrivateProfileIntW(L"Player",L"Volume",70,path);
    g_shuffle=GetPrivateProfileIntW(L"Player",L"Shuffle",0,path)!=0;
    g_repeat=GetPrivateProfileIntW(L"Player",L"Repeat",0,path);if(g_repeat<0||g_repeat>2)g_repeat=0;
    g_playbackRate=(float)GetPrivateProfileIntW(L"Player",L"RatePercent",100,path)/100.0f;
    g_torrentCacheQuotaGiB=(uint64_t)GetPrivateProfileIntW(L"Torrent",L"CacheQuotaGiB",20,path);
    if(g_torrentCacheQuotaGiB<1||g_torrentCacheQuotaGiB>4096)g_torrentCacheQuotaGiB=20;
    g_torrentDownloadLimitKiB=GetPrivateProfileIntW(L"Torrent",L"DownloadLimitKiB",0,path);
    g_torrentUploadLimitKiB=GetPrivateProfileIntW(L"Torrent",L"UploadLimitKiB",0,path);
    if(g_torrentDownloadLimitKiB<0)g_torrentDownloadLimitKiB=0;if(g_torrentUploadLimitKiB<0)g_torrentUploadLimitKiB=0;
    GetPrivateProfileStringW(L"Torrent",L"CacheBaseDir",L"",g_torrentCacheBaseDir,_countof(g_torrentCacheBaseDir),path);
    GetPrivateProfileStringW(L"Torrent",L"DownloadDir",L"",g_torrentDownloadDir,_countof(g_torrentDownloadDir),path);
    EnsureTorrentCacheBaseDir();EnsureTorrentDownloadDir();
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
    swprintf_s(path,PATH_CAP,L"%s\\favorites.m3u",g_dataDir);
    {FILE *fav=NULL;if(_wfopen_s(&fav,path,L"rt, ccs=UTF-8")==0&&fav){wchar_t line[PATH_CAP];while(g_favoriteCount<MAX_FAVORITES&&fgetws(line,PATH_CAP,fav)){line[wcscspn(line,L"\r\n")]=0;if(!line[0]||line[0]==L'#'||IsFavoritePath(line))continue;wcsncpy_s(g_favorites[g_favoriteCount++],PATH_CAP,line,_TRUNCATE);}fclose(fav);}}
    PruneUnavailablePlaylistItems();
    LoadResumeState();RefreshResumeFlags();
    /* Recent history is a list of playable local files, not a stale log. */
    {
        int write=0;
        for(int read=0;read<g_recentCount;read++) {
            /* Keep unavailable recent paths too: selecting one routes through
               PlayIndex, which flags it and offers the normal relink workflow. */
            BOOL duplicate=FALSE;
            for(int j=0;j<write;j++)if(!_wcsicmp(g_recent[j],g_recent[read])){duplicate=TRUE;break;}
            if(duplicate)continue;
            if(write!=read)wcsncpy_s(g_recent[write],PATH_CAP,g_recent[read],_TRUNCATE);
            write++;
        }
        if(write!=g_recentCount){g_recentCount=write;SavePersistentState();}
    }
    if(g_playlistCount)g_selected=0;
    g_libraryReady=TRUE;
}
static void RecentPlayIndex(HWND owner,int index) {
    if(index<0||index>=g_recentCount)return;
    wchar_t path[PATH_CAP];wcsncpy_s(path,PATH_CAP,g_recent[index],_TRUNCATE);
    if(g_recentPopupWindow&&IsWindow(g_recentPopupWindow))DestroyWindow(g_recentPopupWindow);
    int found=-1;
    for(int i=0;i<g_playlistCount;i++)if(!_wcsicmp(g_playlist[i],path)){found=i;break;}
    if(found<0&&g_playlistCount<MAX_PLAYLIST){found=g_playlistCount++;wcsncpy_s(g_playlist[found],PATH_CAP,path,_TRUNCATE);}
    if(found>=0){g_selected=found;RefreshMissingMediaFlags();SavePersistentState();PlayIndex(found);}
    if(IsWindow(owner))InvalidateRect(owner,NULL,FALSE);
}
static ABM_D2D_CANVAS g_recentPopupCanvas;
static BOOL g_recentPopupCanvasReady=FALSE;
static LRESULT CALLBACK RecentPopupWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE:g_recentPopupCanvasReady=ABM_D2D_CanvasInit(&g_recentPopupCanvas);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT t={sizeof(t),TME_LEAVE,hwnd,0};TrackMouseEvent(&t);
        POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        int row=(p.y-68)/42;
        int count=g_recentCount;
        if(row<0||row>=count||p.y<68)row=-1;
        if(row!=g_recentPopupHover){g_recentPopupHover=row;InvalidateRect(hwnd,NULL,FALSE);}
        return 0;
    }
    case WM_MOUSELEAVE:g_recentPopupHover=-1;InvalidateRect(hwnd,NULL,FALSE);return 0;
    case WM_LBUTTONUP: {
        int row=(GET_Y_LPARAM(lp)-68)/42;
        if(GET_Y_LPARAM(lp)>=68&&row>=0&&row<g_recentCount) {
            HWND owner=(HWND)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
            RecentPlayIndex(owner,row);
        }
        /* The popup is opened from the owner's mouse-down handler. Windows can
           route that same physical click's mouse-up to this newly shown popup;
           an outside-row release is not an outside dismissal. WM_ACTIVATE
           handles genuine clicks/focus changes outside the popup. */
        return 0;
    }
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE){DestroyWindow(hwnd);return 0;}
        if(wp==VK_DOWN||wp==VK_UP) {
            if(g_recentCount){if(g_recentPopupHover<0)g_recentPopupHover=0;else g_recentPopupHover=(g_recentPopupHover+(wp==VK_DOWN?1:g_recentCount-1))%g_recentCount;InvalidateRect(hwnd,NULL,FALSE);}
            return 0;
        }
        if(wp==VK_RETURN&&g_recentPopupHover>=0){HWND owner=(HWND)GetWindowLongPtrW(hwnd,GWLP_USERDATA);RecentPlayIndex(owner,g_recentPopupHover);return 0;}
        break;
    /* Do not dismiss on WM_ACTIVATE: the owner/popup activation hand-off
       can generate a transient inactive notification during initial display.
       The owner closes this surface on the next click outside it. */
    case WM_PAINT: {
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT rc;GetClientRect(hwnd,&rc);
        if(g_recentPopupCanvasReady&&ABM_D2D_CanvasBegin(&g_recentPopupCanvas,dc,&rc)){
            ABM_D2D_CanvasFill(&g_recentPopupCanvas,0,0,(float)rc.right,(float)rc.bottom,ABM_THEME_COLOR_0A111C);
            ABM_D2D_CanvasFillRound(&g_recentPopupCanvas,1,1,(float)rc.right-1,(float)rc.bottom-1,(float)ABM_UI_RADIUS_PANEL,ABM_THEME_COLOR_0A111C);
            ABM_D2D_CanvasStrokeRound(&g_recentPopupCanvas,0.5f,0.5f,(float)rc.right-0.5f,(float)rc.bottom-0.5f,(float)ABM_UI_RADIUS_PANEL,ABM_THEME_COLOR_365B70,1.0f);
            ABM_D2D_CanvasFill(&g_recentPopupCanvas,1,1,(float)rc.right-1,62,ABM_THEME_COLOR_102736);
            RECT tr={20,10,rc.right-18,36};
            ABM_D2D_CanvasText(&g_recentPopupCanvas,L"Recently played",tr,ABM_THEME_COLOR_F2F8FF,ABM_D2D_TEXT_HEADING,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            wchar_t countText[64];StringCchPrintfW(countText,_countof(countText),L"%d LOCAL FILE%s  ·  SELECT TO PLAY",g_recentCount,g_recentCount==1?L"":L"S");
            RECT mr={21,36,rc.right-18,56};
            ABM_D2D_CanvasText(&g_recentPopupCanvas,countText,mr,ABM_THEME_COLOR_8BB4C2,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            for(int i=0;i<g_recentCount;i++){
                int top=68+i*42;RECT row={10,top,rc.right-10,top+38};
                if(i==g_recentPopupHover)ABM_D2D_CanvasFillRound(&g_recentPopupCanvas,(float)row.left,(float)row.top,(float)row.right,(float)row.bottom,(float)ABM_UI_RADIUS_CONTROL,ABM_THEME_COLOR_18424C);
                else if((i&1)==0)ABM_D2D_CanvasFill(&g_recentPopupCanvas,(float)row.left,(float)row.top,(float)row.right,(float)row.bottom,ABM_THEME_COLOR_0D1825);
                RECT num={20,top+2,48,top+36};wchar_t n[12];StringCchPrintfW(n,_countof(n),L"%02d",i+1);
                ABM_D2D_CanvasText(&g_recentPopupCanvas,n,num,ABM_THEME_COLOR_5BBFB1,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
                const wchar_t *leaf=wcsrchr(g_recent[i],L'\\');if(!leaf)leaf=wcsrchr(g_recent[i],L'/');leaf=leaf?leaf+1:g_recent[i];
                RECT name={54,top+2,rc.right-22,top+36};
                ABM_D2D_CanvasText(&g_recentPopupCanvas,leaf,name,ABM_THEME_COLOR_E1EDF7,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            }
            ABM_D2D_CanvasEnd(&g_recentPopupCanvas);
        }else{
            HBRUSH bg=CreateSolidBrush(ABM_THEME_COLOR_0A111C);FillRect(dc,&rc,bg);DeleteObject(bg);
        }
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_NCDESTROY:if(g_recentPopupCanvasReady){g_recentPopupCanvasReady=FALSE;ABM_D2D_CanvasDestroy(&g_recentPopupCanvas);}g_recentPopupWindow=NULL;g_recentPopupHover=-1;break;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static void ShowRecentMenu(HWND hwnd,int x,int y) {
    if(!g_recentCount){ABM_MessageBoxW(hwnd,L"No recently played media yet.",APP_NAME,MB_OK|MB_ICONINFORMATION);return;}
    if(g_recentPopupWindow&&IsWindow(g_recentPopupWindow)){DestroyWindow(g_recentPopupWindow);return;}
    static BOOL registered=FALSE;
    HINSTANCE inst=GetModuleHandleW(NULL);
    if(!registered){
        WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.lpfnWndProc=RecentPopupWndProc;wc.hInstance=inst;
        wc.hCursor=LoadCursorW(NULL,IDC_HAND);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);wc.lpszClassName=L"ABMAVPlayerRecentPopup";
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;registered=TRUE;
    }

    /* Anchor to the actual Recent control, not the mouse-down point. The old
       fixed 520px popup could be pushed off-screen or detached from the control
       in compact/resized layouts. */
    RECT anchor=R_RECENT;
    POINT anchorTL={anchor.left,anchor.top},anchorBR={anchor.right,anchor.bottom};
    ClientToScreen(hwnd,&anchorTL);ClientToScreen(hwnd,&anchorBR);
    if(anchor.right<=anchor.left || anchor.bottom<=anchor.top) {
        POINT cursor={x,y};ClientToScreen(hwnd,&cursor);
        anchorTL=cursor;anchorBR=cursor;
    }

    RECT work;
    if(!SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0)) {
        HMONITOR monitor=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi={sizeof(mi)};
        if(GetMonitorInfoW(monitor,&mi))work=mi.rcWork;
        else {work.left=0;work.top=0;work.right=GetSystemMetrics(SM_CXSCREEN);work.bottom=GetSystemMetrics(SM_CYSCREEN);}
    }
    const int margin=12;
    int availableW=work.right-work.left-2*margin;
    int availableH=work.bottom-work.top-2*margin;
    if(availableW<240 || availableH<180)return;
    RECT client={0};GetClientRect(hwnd,&client);
    int ownerW=client.right-client.left;
    int width=520;
    /* Keep the Recent surface proportionate to compact player windows while
       retaining the full desktop width on larger layouts. */
    int ownerFit=ownerW-24;
    if(ownerFit<240)ownerFit=240;
    if(width>ownerFit)width=ownerFit;
    if(width>availableW)width=availableW;
    int height=68+g_recentCount*42+10;
    if(height>availableH)height=availableH;

    /* Prefer opening below the button, aligned to its right edge. If that
       would leave insufficient room, open above it; clamp both axes to the
       monitor work area. */
    int px=anchorBR.x-width;
    if(px<work.left+margin)px=anchorTL.x;
    if(px+width>work.right-margin)px=work.right-margin-width;
    int py=anchorBR.y+6;
    if(py+height>work.bottom-margin)py=anchorTL.y-height-6;
    if(py<work.top+margin)py=work.top+margin;
    if(px<work.left+margin)px=work.left+margin;
    if(px+width>work.right-margin)px=work.right-margin-width;

    HWND popup=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"ABMAVPlayerRecentPopup",L"Recently played",
        WS_POPUP,px,py,width,height,hwnd,NULL,inst,NULL);
    if(!popup)return;
    SetWindowLongPtrW(popup,GWLP_USERDATA,(LONG_PTR)hwnd);g_recentPopupWindow=popup;
    g_recentPopupHover=-1;
    ShowWindow(popup,SW_SHOW);
    SetWindowPos(popup,HWND_TOPMOST,px,py,width,height,SWP_SHOWWINDOW|SWP_NOACTIVATE);
    SetForegroundWindow(popup);SetFocus(popup);InvalidateRect(popup,NULL,TRUE);
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
    swprintf_s(value,24,L"%d",(int)(g_eqPreamp*10.0f));
    WritePrivateProfileStringW(L"Audio",L"EqPreamp",value,path);
    for(int i=0;i<10;i++){wchar_t key[24];swprintf_s(key,_countof(key),L"EqBand%d",i);swprintf_s(value,24,L"%d",(int)(g_eqBandGain[i]*10.0f));WritePrivateProfileStringW(L"Audio",key,value,path);}
    wchar_t device[256]={0};MultiByteToWideChar(CP_UTF8,0,g_audioDeviceId,-1,device,(int)_countof(device));
    WritePrivateProfileStringW(L"Audio",L"OutputDevice",device,path);
    swprintf_s(value,24,L"%lld",(long long)g_audioDelayUs);
    WritePrivateProfileStringW(L"Audio",L"AudioDelayUs",value,path);
}
static void LoadAudioPreferences(void) {
    if(!EnsureDataDirectory())return;
    wchar_t path[PATH_CAP];
    swprintf_s(path,PATH_CAP,L"%s\\audio.ini",g_dataDir);
    g_replayGain=GetPrivateProfileIntW(L"Audio",L"ReplayGain",1,path)!=0;
    g_compressor=GetPrivateProfileIntW(L"Audio",L"Compressor",0,path)!=0;
    wchar_t savedDevice[256]={0};GetPrivateProfileStringW(L"Audio",L"OutputDevice",L"",savedDevice,(DWORD)_countof(savedDevice),path);
    if(savedDevice[0])WideCharToMultiByte(CP_UTF8,0,savedDevice,-1,g_audioDeviceId,(int)sizeof(g_audioDeviceId),NULL,NULL);
    {int delay=GetPrivateProfileIntW(L"Audio",L"AudioDelayUs",0,path);if(delay<-10000000)delay=-10000000;if(delay>10000000)delay=10000000;g_audioDelayUs=delay;}
    g_eqProfile=GetPrivateProfileIntW(L"Audio",L"EqProfile",0,path);
    if(!((g_eqProfile>=0&&g_eqProfile<=4)||(g_eqProfile>=10&&g_eqProfile<=19)))g_eqProfile=0;
    g_eqPreamp=(float)GetPrivateProfileIntW(L"Audio",L"EqPreamp",0,path)/10.0f;
    for(int i=0;i<10;i++){wchar_t key[24];swprintf_s(key,_countof(key),L"EqBand%d",i);g_eqBandGain[i]=(float)GetPrivateProfileIntW(L"Audio",key,0,path)/10.0f;}
}

static void RefreshAudioDevices(void) {
    g_audioDeviceCount=0;
    if(!g_player)return;
    libvlc_audio_output_device_t *list=libvlc_audio_output_device_enum(g_player);
    for(libvlc_audio_output_device_t *d=list;d&&g_audioDeviceCount<11;d=d->p_next) {
        int i=g_audioDeviceCount++;
        g_audioDeviceIds[i][0]=0;g_audioDeviceNames[i][0]=0;
        if(d->psz_device)strncpy_s(g_audioDeviceIds[i],sizeof(g_audioDeviceIds[i]),d->psz_device,_TRUNCATE);
        if(d->psz_description)MultiByteToWideChar(CP_UTF8,0,d->psz_description,-1,g_audioDeviceNames[i],(int)_countof(g_audioDeviceNames[i]));
        if(!g_audioDeviceNames[i][0])wcscpy_s(g_audioDeviceNames[i],_countof(g_audioDeviceNames[i]),L"Audio device");
    }
    if(list)libvlc_audio_output_device_list_release(list);
    wcscpy_s(g_audioDeviceLabel,_countof(g_audioDeviceLabel),L"System default");
    if(g_audioDeviceId[0])for(int i=0;i<g_audioDeviceCount;i++)if(!strcmp(g_audioDeviceIds[i],g_audioDeviceId)){wcscpy_s(g_audioDeviceLabel,_countof(g_audioDeviceLabel),g_audioDeviceNames[i]);break;}
}
static void ApplyAudioOutputDevice(void) {
    if(!g_player||g_selectedRenderer)return;
    libvlc_audio_output_device_set(g_player,NULL,g_audioDeviceId[0]?g_audioDeviceId:NULL);
}

static void CheckAudioOutputAvailability(void) {
    DWORD now=GetTickCount();
    if(!g_player||!g_audioDeviceId[0]||(DWORD)(now-g_audioLastDeviceCheckTick)<2000)return;
    g_audioLastDeviceCheckTick=now;
    RefreshAudioDevices();
    BOOL found=FALSE;
    for(int i=0;i<g_audioDeviceCount;i++)if(!strcmp(g_audioDeviceIds[i],g_audioDeviceId)){found=TRUE;break;}
    if(!found) {
        g_audioDeviceId[0]=0;
        wcscpy_s(g_audioDeviceLabel,_countof(g_audioDeviceLabel),L"System default");
        /* LibVLC's NULL device ID selects the current Windows default endpoint.
           The same g_player is used for both local and torrent playback. */
        libvlc_audio_output_device_set(g_player,NULL,NULL);
        SaveAudioPreferences();
        SetStatus(L"Audio device disconnected — switched to System Default");
        if(g_main)InvalidateRect(g_main,NULL,FALSE);
    }
}

static void SelectAudioOutput(int index) {
    if(index<0){g_audioDeviceId[0]=0;wcscpy_s(g_audioDeviceLabel,_countof(g_audioDeviceLabel),L"System default");}
    else if(index<g_audioDeviceCount){strncpy_s(g_audioDeviceId,sizeof(g_audioDeviceId),g_audioDeviceIds[index],_TRUNCATE);wcscpy_s(g_audioDeviceLabel,_countof(g_audioDeviceLabel),g_audioDeviceNames[index]);}
    else return;
    ApplyAudioOutputDevice();SaveAudioPreferences();g_audioPopupOpen=FALSE;InvalidateRect(g_main,NULL,FALSE);
}

static void ApplyEqProfile(void) {
    if(!g_player)return;
    if(g_eqProfile==4){ApplyCustomEqualizer();return;}
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
static BOOL g_torrentPlaybackActive=FALSE;
static HANDLE g_torrentStreamThread=NULL;
/* LibVLC exposes a generic media-player Error state rather than a
   reliable public error code for every demux/decoder/output failure. Keep
   diagnoses evidence-based: classify by source context and never claim a
   specific codec or device failure without a corresponding signal. */
static void PlaybackReportFailure(int index,BOOL startupFailure) {
    if(g_selectedRenderer) {
        SetStatus(startupFailure?
            L"Receiver could not start this media. Check receiver availability and format; ABM AV Player has not switched playback to this PC.":
            L"Receiver playback failed. Choose another receiver or return to local playback; no automatic reroute was attempted");
        return;
    }
    if(index<0||index>=g_playlistCount) {
        SetStatus(L"Playback failed. Select the item again to retry or choose another media file.");
        return;
    }
    DWORD attributes=GetFileAttributesW(g_playlist[index]);
    if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY)) {
        SetStatus(L"Playback source is unavailable. Check that the file still exists and can be accessed.");
        return;
    }
    SetStatus(startupFailure?
        L"LibVLC could not start this file. It may be unsupported, damaged, truncated, or blocked by a decoder/output initialisation failure. Try another file or review Video & Decoding settings.":
        L"Playback stopped after a LibVLC error. The exact cause is not exposed; check the file, VLC plugins, decoder settings and audio/video output. Select Play to retry.");
}
static void PlaybackHandleEngineError(void) {
    if(!g_hasMedia||!g_player)return;
    int failedIndex=g_current;
    /* Stop once and leave the queue selection intact. Do not auto-retry a
       corrupt source or repeatedly cycle decoder/output initialisation. */
    PlaybackStopEngine();
    g_hasMedia=FALSE;g_endHandled=TRUE;g_pendingResume=FALSE;
    g_videoBoundsValid=FALSE;ResetArtwork();
    if(g_visualizerActive)ABM_AudioVisualizer_Stop();
    g_visualizerActive=FALSE;g_visualizerTried=FALSE;
    if(g_video&&!g_fullscreen)ShowWindow(g_video,SW_HIDE);
    PlaybackReportFailure(failedIndex,FALSE);
    InvalidateRect(g_main,NULL,FALSE);
}
static void QueueMoveItem(int source,int insertBoundary) {
    if(source<0||source>=g_playlistCount)return;
    int target=insertBoundary;
    if(target<0)target=0;
    if(target>g_playlistCount)target=g_playlistCount;
    if(source<target)target--;
    if(target<0)target=0;
    if(target>=g_playlistCount)target=g_playlistCount-1;
    if(target==source)return;

    wchar_t moving[PATH_CAP];
    wcsncpy_s(moving,_countof(moving),g_playlist[source],_TRUNCATE);
    if(source<target) {
        memmove(&g_playlist[source][0],&g_playlist[source+1][0],
            (size_t)(target-source)*sizeof(g_playlist[0]));
    } else {
        memmove(&g_playlist[target+1][0],&g_playlist[target][0],
            (size_t)(source-target)*sizeof(g_playlist[0]));
    }
    wcsncpy_s(g_playlist[target],PATH_CAP,moving,_TRUNCATE);

    int *indices[2]={&g_current,&g_selected};
    for(int n=0;n<2;n++) {
        int *idx=indices[n];
        if(*idx==source)*idx=target;
        else if(source<target&&*idx>source&&*idx<=target)(*idx)--;
        else if(source>target&&*idx>=target&&*idx<source)(*idx)++;
    }
    SavePersistentState();
    InvalidateRect(g_main,&R_SIDEBAR,FALSE);
}

static void PlayIndex(int index) {
    if(index<0||index>=g_playlistCount){g_startFromBeginningNextPlay=FALSE;return;}
    BOOL startFromBeginning=g_startFromBeginningNextPlay;
    g_startFromBeginningNextPlay=FALSE;
    if(g_torrentPlaybackActive||g_torrentStreamThread)TorrentStopPlayback();
    DWORD attributes=GetFileAttributesW(g_playlist[index]);
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_DIRECTORY)){
        g_missingMedia[index]=TRUE;
        SetStatus(L"Media file missing. Use the item menu to relink it.");
        InvalidateRect(g_main,&R_SIDEBAR,FALSE);
        wchar_t message[PATH_CAP+128];
        swprintf_s(message,_countof(message),L"The selected media file is unavailable:\n%s",g_playlist[index]);
        ABM_MessageBoxW(g_main,message,APP_NAME,MB_ICONWARNING);
        return;
    }
    char utf8[PATH_CAP*4]; int n=WideCharToMultiByte(CP_UTF8,0,g_playlist[index],-1,utf8,sizeof(utf8),NULL,NULL); if(n<=0)return;
    libvlc_media_t *m=libvlc_media_new_path(g_vlc,utf8); if(!m){ABM_MessageBoxW(g_main,L"LibVLC could not open this file.",APP_NAME,MB_ICONERROR);return;}
    ResumeCaptureCurrent();
    PlaybackStopEngine();
    if(g_selectedRenderer&&!g_rendererApplied){
        if(libvlc_media_player_set_renderer(g_player,g_selectedRenderer)!=0){
            libvlc_media_release(m);
            SetStatus(L"Receiver setup failed - playback cancelled, not sent locally");
            ABM_MessageBoxW(g_main,L"LibVLC rejected the selected receiver. No local playback was started. Try selecting the receiver again or choose another device.",APP_NAME,MB_ICONERROR);
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
    ApplySubtitleAppearanceOption(m);AddAdvancedCachingOptions(m);AddAdvancedPerformanceOptions(m);
    BeginArtworkParse(m);
    PlaybackSetMedia(m);libvlc_video_set_spu_delay(g_player,g_advancedSubtitleDelayUs);libvlc_audio_set_delay(g_player,g_audioDelayUs);libvlc_media_release(m);
    ApplyEqProfile();
    if(PlaybackStartEngine()!=0){
        g_hasMedia=FALSE;g_pendingResume=FALSE;ResetArtwork();
        PlaybackReportFailure(index,TRUE);
        if(g_selectedRenderer)
            ABM_MessageBoxW(g_main,L"LibVLC could not start playback on the selected receiver. The video was not redirected to local playback. Check that the TV is awake and supports this media format.",APP_NAME,MB_ICONERROR);
        else
            ABM_MessageBoxW(g_main,L"Playback could not start. The file may be unsupported or damaged, or a decoder/output component may have failed. Review Video & Decoding settings and check the VLC plugins.",APP_NAME,MB_ICONERROR);
        return;
    }
    if(!g_selectedRenderer)libvlc_audio_set_delay(g_player,g_audioDelayUs);
    g_videoWidth=0;g_videoHeight=0;g_videoBoundsValid=FALSE;
    g_current=index;g_selected=index;g_hasMedia=TRUE;g_lastUiTime=-1;g_overlayLength=0;g_overlayPlaying=TRUE;g_endHandled=FALSE;
    g_pendingResume=FALSE;g_pendingResumeTicks=0;
    int resumeIndex=ResumeFind(g_playlist[index]);
    if(startFromBeginning&&resumeIndex>=0){
        for(int ri=resumeIndex+1;ri<g_resumeCount;ri++)g_resumeEntries[ri-1]=g_resumeEntries[ri];
        g_resumeCount--;RefreshResumeFlags();ResumeSaveAsync(FALSE);
        SetStatus(L"Starting from the beginning.");
    }else{
        int64_t savedResume=ResumePositionForPath(g_playlist[index]);
        if(savedResume>0){g_pendingResume=TRUE;g_pendingResumeMs=savedResume;g_pendingResumePause=FALSE;g_pendingResumeTicks=0;SetStatus(L"Restoring your previous playback position...");}
    }
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
    RefreshMissingMediaFlags();SavePersistentState();InvalidateRect(g_main,NULL,FALSE);
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
    RefreshMissingMediaFlags();SavePersistentState();InvalidateRect(g_main,NULL,FALSE);
}
static void ClearPlaylist(void) {
    if(g_hasMedia)PlaybackStopEngine();
    g_hasMedia=FALSE;g_current=-1;g_selected=-1;g_playlistCount=0;g_playlistScroll=0;
    g_endHandled=FALSE;g_pendingResume=FALSE;memset(g_hasResume,0,sizeof(g_hasResume));ResetArtwork();SavePersistentState();
    SetStatus(L"Playlist cleared");InvalidateRect(g_main,NULL,FALSE);
}
static void RemovePlaylistItem(int index) {
    if(index<0||index>=g_playlistCount)return;
    if(index==g_current){PlaybackStopEngine();g_hasMedia=FALSE;g_current=-1;}
    else if(index<g_current)g_current--;
    for(int i=index+1;i<g_playlistCount;i++)
        wcsncpy_s(g_playlist[i-1],PATH_CAP,g_playlist[i],_TRUNCATE);
    g_playlistCount--;RefreshMissingMediaFlags();SavePersistentState();
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
    RefreshMissingMediaFlags();
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
    FILE *fp=NULL;if(_wfopen_s(&fp,path,L"wb")!=0||!fp){ABM_MessageBoxW(g_main,L"Could not save the playlist.",APP_NAME,MB_ICONERROR);return;}
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
    FILE *fp=NULL;if(_wfopen_s(&fp,path,L"rb")!=0||!fp){ABM_MessageBoxW(g_main,L"Could not open the playlist.",APP_NAME,MB_ICONERROR);return;}
    if(g_hasMedia){PlaybackStopEngine();g_hasMedia=FALSE;}
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
    fclose(fp);PruneUnavailablePlaylistItems();if(g_playlistCount)g_selected=0;
    InvalidateRect(g_main,NULL,FALSE);
}
static void LoadExternalSubtitle(HWND owner){
    if(!g_player||(!g_hasMedia&&!g_torrentPlaybackActive)){ABM_MessageBoxW(owner,L"Start playing a media file before loading an external subtitle.",APP_NAME,MB_ICONINFORMATION);return;}
    OPENFILENAMEW ofn={0};wchar_t path[PATH_CAP]=L"";
    ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=owner;ofn.lpstrFile=path;ofn.nMaxFile=PATH_CAP;
    ofn.lpstrFilter=L"Subtitle files (*.srt;*.ass;*.ssa;*.sub;*.vtt)\0*.srt;*.ass;*.ssa;*.sub;*.vtt\0All files (*.*)\0*.*\0";
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&ofn)){
        char utf8[PATH_CAP*4];
        if(WideCharToMultiByte(CP_UTF8,0,path,-1,utf8,(int)sizeof(utf8),NULL,NULL)>0){
            if(libvlc_video_set_subtitle_file(g_player,utf8)==0)SetStatus(L"External subtitle loaded.");
            else SetStatus(L"LibVLC could not load this subtitle file.");
        }
    }
}
static void ShowAudioTrackMenu(HWND owner){
    if(!g_player||(!g_hasMedia&&!g_torrentPlaybackActive)){ABM_MessageBoxW(owner,L"Start playing a media file before selecting an audio track.",APP_NAME,MB_ICONINFORMATION);return;}
    ABMMenuItem items[26];wchar_t labels[24][256];int count=0,n=0;
    items[count++]=(ABMMenuItem){27000,L"Disable audio track",TRUE,FALSE};
    libvlc_track_description_t *tracks=libvlc_audio_get_track_description(g_player);
    for(libvlc_track_description_t *t=tracks;t&&n<24;t=t->p_next){
        labels[n][0]=0;if(t->psz_name)MultiByteToWideChar(CP_UTF8,0,t->psz_name,-1,labels[n],256);
        if(!labels[n][0])StringCchPrintfW(labels[n],256,L"Audio track %d",n+1);
        items[count++]=(ABMMenuItem){27100+n,labels[n],TRUE,FALSE};n++;
    }
    if(tracks)libvlc_track_description_list_release(tracks);
    if(!n)items[count++]=(ABMMenuItem){0,L"No embedded audio tracks",FALSE,FALSE};
    POINT menuPoint;GetCursorPos(&menuPoint);ScreenToClient(owner,&menuPoint);
    int cmd=ABM_ShowMenu(owner,menuPoint.x,menuPoint.y,items,count);
    if(cmd==27000)libvlc_audio_set_track(g_player,-1);
    else if(cmd>=27100&&cmd<27124){
        int wanted=cmd-27100;
        libvlc_track_description_t *again=libvlc_audio_get_track_description(g_player);
        libvlc_track_description_t *selected=again;
        for(int i=0;selected&&i<wanted;i++)selected=selected->p_next;
        if(selected)libvlc_audio_set_track(g_player,selected->i_id);
        if(again)libvlc_track_description_list_release(again);
    }
}
static void ShowSubtitleTrackMenu(HWND owner){
    if(!g_player||(!g_hasMedia&&!g_torrentPlaybackActive)){ABM_MessageBoxW(owner,L"Start playing a media file before selecting an embedded subtitle track.",APP_NAME,MB_ICONINFORMATION);return;}
    ABMMenuItem items[66];wchar_t labels[64][256];int count=0,n=0;
    items[count++]=(ABMMenuItem){21000,L"Disable subtitles",TRUE,FALSE};
    libvlc_track_description_t *tracks=libvlc_video_get_spu_description(g_player);
    for(libvlc_track_description_t *t=tracks;t&&n<64;t=t->p_next){
        labels[n][0]=0;if(t->psz_name)MultiByteToWideChar(CP_UTF8,0,t->psz_name,-1,labels[n],256);
        if(!labels[n][0])StringCchPrintfW(labels[n],256,L"Subtitle %d",n+1);
        items[count++]=(ABMMenuItem){26000+n,labels[n],TRUE,FALSE};n++;
    }
    if(tracks)libvlc_track_description_list_release(tracks);
    if(!n)items[count++]=(ABMMenuItem){0,L"No embedded subtitle tracks",FALSE,FALSE};
    POINT menuPoint;GetCursorPos(&menuPoint);ScreenToClient(owner,&menuPoint);
    int cmd=ABM_ShowMenu(owner,menuPoint.x,menuPoint.y,items,count);
    if(cmd==21000)libvlc_video_set_spu(g_player,-1);
    else if(cmd>=26000&&cmd<26064){
        int wanted=cmd-26000;
        libvlc_track_description_t *again=libvlc_video_get_spu_description(g_player);
        libvlc_track_description_t *selected=again;
        for(int i=0;selected&&i<wanted;i++)selected=selected->p_next;
        if(selected)libvlc_video_set_spu(g_player,selected->i_id);
        if(again)libvlc_track_description_list_release(again);
    }
}
static void ShowPlaybackContextMenu(HWND hwnd,int x,int y) {
    if(!g_hasMedia)return;
    ABMMenuItem items[160];wchar_t trackLabels[64][256];int count=0,trackCount=0;
#define ABM_ADD(cmd,text,active) do{items[count++]=(ABMMenuItem){cmd,text,active,FALSE};}while(0)
#define ABM_HEAD(text) do{items[count++]=(ABMMenuItem){0,text,FALSE,TRUE};}while(0)
    ABM_ADD(23009,L"Home Theatre...",TRUE);
    ABM_ADD(23007,L"Subtitle Manager...",TRUE);
    ABM_ADD(23008,L"Advanced Audio Centre...",TRUE);
    ABM_HEAD(L"VIDEO PICTURE");
    ABM_ADD(24000,L"Aspect ratio: Automatic",TRUE);
    ABM_ADD(24001,L"Aspect ratio: 16:9",TRUE);
    ABM_ADD(24002,L"Aspect ratio: 4:3",TRUE);
    ABM_ADD(24003,L"Aspect ratio: 16:10",TRUE);
    ABM_ADD(24004,L"Aspect ratio: 1:1",TRUE);
    ABM_ADD(24010,L"Crop: Automatic",TRUE);
    ABM_ADD(24011,L"Crop: 16:9",TRUE);
    ABM_ADD(24012,L"Crop: 4:3",TRUE);
    ABM_ADD(24013,L"Crop: 1:1",TRUE);
    ABM_ADD(24020,L"Reset video scaling",TRUE);
    ABM_ADD(24021,L"Deinterlace: Toggle Yadif",TRUE);
    ABM_HEAD(L"AUDIO VISUALIZER STYLE");
    ABM_ADD(25210,g_visualizerMode==0?L"✓ Spectrum Orbit":L"Spectrum Orbit",TRUE);
    ABM_ADD(25211,g_visualizerMode==1?L"✓ Fluid Waves":L"Fluid Waves",TRUE);
    ABM_ADD(25212,g_visualizerMode==2?L"✓ Particle Universe":L"Particle Universe",TRUE);
    ABM_ADD(25213,g_visualizerMode==3?L"✓ Neon Spectrum":L"Neon Spectrum",TRUE);
    ABM_ADD(25214,g_visualizerMode==4?L"✓ ProjectM MilkDrop (OpenGL)":L"ProjectM MilkDrop (OpenGL)",TRUE);
    ABM_HEAD(L"AUDIO TRACK");
    libvlc_track_description_t *tracks=libvlc_audio_get_track_description(g_player);
    for(libvlc_track_description_t *t=tracks;t&&trackCount<24;t=t->p_next){
        wchar_t *label=trackLabels[trackCount];label[0]=0;
        if(t->psz_name)MultiByteToWideChar(CP_UTF8,0,t->psz_name,-1,label,256);
        if(!label[0])StringCchPrintfW(label,256,L"Audio track %d",trackCount+1);
        ABM_ADD(t->i_id+20000,label,TRUE);trackCount++;
    }
    if(tracks)libvlc_track_description_list_release(tracks);
    if(!trackCount)ABM_ADD(0,L"No audio tracks available",FALSE);
    ABM_HEAD(L"SUBTITLES");
    ABM_ADD(21000,L"Disable subtitles",TRUE);
    tracks=libvlc_video_get_spu_description(g_player);trackCount=0;
    for(libvlc_track_description_t *t=tracks;t&&trackCount<64;t=t->p_next){
        wchar_t *label=trackLabels[trackCount];label[0]=0;
        if(t->psz_name)MultiByteToWideChar(CP_UTF8,0,t->psz_name,-1,label,256);
        if(!label[0])StringCchPrintfW(label,256,L"Subtitle %d",trackCount+1);
        ABM_ADD(t->i_id+21000,label,TRUE);trackCount++;
    }
    if(tracks)libvlc_track_description_list_release(tracks);
    ABM_ADD(23001,L"Load external subtitle...",TRUE);
    ABM_HEAD(L"PLAYBACK SPEED");
    ABM_ADD(22005,L"0.5x",TRUE);ABM_ADD(22007,L"0.75x",TRUE);ABM_ADD(22010,L"1.0x  (Normal)",TRUE);
    ABM_ADD(22012,L"1.25x",TRUE);ABM_ADD(22015,L"1.5x",TRUE);ABM_ADD(22020,L"2.0x",TRUE);
    ABM_HEAD(L"EQUALIZER PRESETS");
    static const wchar_t *presets[10]={L"Flat",L"Classical",L"Club",L"Dance",L"Full bass",L"Full treble",L"Headphones",L"Laptop / speakers",L"Live",L"Rock"};
    for(int i=0;i<10;i++)ABM_ADD(25000+i,presets[i],TRUE);
    ABM_HEAD(L"ABM AUDIO OPTIONS");
    ABM_ADD(25100,L"Graphic EQ: Flat",TRUE);ABM_ADD(25101,L"Graphic EQ: Bass boost",TRUE);
    ABM_ADD(25102,L"Graphic EQ: Vocal clarity",TRUE);ABM_ADD(25103,L"Graphic EQ: Treble boost",TRUE);
    ABM_ADD(23005,g_replayGain?L"Disable ReplayGain normalisation":L"Enable ReplayGain normalisation",TRUE);
    ABM_ADD(23006,g_compressor?L"Disable dynamic-range compressor":L"Enable dynamic-range compressor",TRUE);
    ABM_ADD(23002,L"Media information...",TRUE);
    ABM_ADD(23003,g_shuffle?L"Disable shuffle":L"Enable shuffle",TRUE);
    ABM_ADD(23004,g_repeat==0?L"Repeat: Off (cycle mode)":(g_repeat==2?L"Repeat all (cycle mode)":L"Repeat current (cycle mode)"),TRUE);
    int cmd=ABM_ShowMenu(hwnd,x,y,items,count);
#undef ABM_ADD
#undef ABM_HEAD
    if(cmd==23009){EnsureFavoritesInPlaylist();const wchar_t *paths[MAX_PLAYLIST];for(int i=0;i<g_playlistCount;i++)paths[i]=g_playlist[i];ABM_HomeTheatre_Show(hwnd,paths,g_playlistCount,PlayIndex);}
    else if(cmd==23007)ShowAdvancedSettingsAtPage(hwnd,4);
    else if(cmd==23008)ShowAdvancedSettingsAtPage(hwnd,10);
    else if(cmd==24000)libvlc_video_set_aspect_ratio(g_player,NULL);
    else if(cmd==24001)libvlc_video_set_aspect_ratio(g_player,"16:9");
    else if(cmd==24002)libvlc_video_set_aspect_ratio(g_player,"4:3");
    else if(cmd==24003)libvlc_video_set_aspect_ratio(g_player,"16:10");
    else if(cmd==24004)libvlc_video_set_aspect_ratio(g_player,"1:1");
    else if(cmd==24010)libvlc_video_set_crop_geometry(g_player,NULL);
    else if(cmd==24011)libvlc_video_set_crop_geometry(g_player,"16:9");
    else if(cmd==24012)libvlc_video_set_crop_geometry(g_player,"4:3");
    else if(cmd==24013)libvlc_video_set_crop_geometry(g_player,"1:1");
    else if(cmd==24020)libvlc_video_set_scale(g_player,0.0f);
    else if(cmd==24021){static BOOL deinterlace=FALSE;deinterlace=!deinterlace;libvlc_video_set_deinterlace(g_player,deinterlace?"yadif":NULL);}
    else if(cmd>=25210&&cmd<=25214){
        int previousMode=g_visualizerMode;
        g_visualizerMode=cmd-25210;
        if(g_visualizerMode==4 && previousMode!=4)g_projectmTried=FALSE;
        for(int i=0;i<64;i++)g_visualizerBarPeaks[i]=0.0f;
        if(g_visualizerMode==4) {
            if(g_visualizerActive)EnsureProjectMRenderer(hwnd);
            SetStatus(L"Visualizer: ProjectM MilkDrop (isolated OpenGL renderer)");
        } else {
            ABM_ProjectM_SetBounds(NULL,FALSE);
            SetStatus(g_visualizerMode==0?L"Visualizer: Spectrum Orbit":
                (g_visualizerMode==1?L"Visualizer: Fluid Waves":
                (g_visualizerMode==2?L"Visualizer: Particle Universe":L"Visualizer: Neon Spectrum")));
        }
        InvalidateRect(hwnd,g_fullscreen?NULL:&R_VIDEO,FALSE);
    }
    else if(cmd>=20000&&cmd<21000)libvlc_audio_set_track(g_player,cmd-20000);
    else if(cmd>=21000&&cmd<22000)libvlc_video_set_spu(g_player,cmd-21000);
    else if(cmd==23001)LoadExternalSubtitle(hwnd); else if(cmd>=22000&&cmd<=22020){
        float rate=cmd==22005?0.5f:cmd==22007?0.75f:cmd==22010?1.0f:cmd==22012?1.25f:cmd==22015?1.5f:2.0f;
        g_playbackRate=rate;libvlc_media_player_set_rate(g_player,rate);
    } else if(cmd==23002){
        libvlc_media_t *m=libvlc_media_player_get_media(g_player);
        wchar_t info[4096]=L"",value[1024]=L"",path[PATH_CAP]=L"";
        int64_t duration=-1,position=libvlc_media_player_get_time(g_player);
        unsigned int videoW=0,videoH=0;
        BOOL haveVideoSize=(libvlc_video_get_size(g_player,0,&videoW,&videoH)==0&&videoW&&videoH);
        if(g_current>=0&&g_current<g_playlistCount)StringCchCopyW(path,_countof(path),g_playlist[g_current]);
        if(m){
            struct {libvlc_meta_t key;const wchar_t *label;} meta[]={
                {libvlc_meta_Title,L"Title"},{libvlc_meta_Artist,L"Artist"},{libvlc_meta_Album,L"Album"},
                {libvlc_meta_Genre,L"Genre"},{libvlc_meta_Date,L"Year / date"},{libvlc_meta_Description,L"Description"}
            };
            StringCchCatW(info,_countof(info),L"MEDIA OVERVIEW\r\n");
            for(unsigned int i=0;i<_countof(meta);i++){
                char *raw=libvlc_media_get_meta(m,meta[i].key);value[0]=0;
                if(raw){MultiByteToWideChar(CP_UTF8,0,raw,-1,value,(int)_countof(value));libvlc_free(raw);}
                if(!value[0])StringCchCopyW(value,_countof(value),L"—");
                StringCchCatW(info,_countof(info),meta[i].label);StringCchCatW(info,_countof(info),L": ");
                StringCchCatW(info,_countof(info),value);StringCchCatW(info,_countof(info),L"\r\n");
            }
            duration=libvlc_media_get_duration(m);
            libvlc_media_release(m);
        } else StringCchCatW(info,_countof(info),L"Media metadata is not available yet.\r\n");
        StringCchCatW(info,_countof(info),L"\r\nFILE\r\nName: ");
        const wchar_t *base=path[0]?wcsrchr(path,L'\\'):NULL;
        StringCchCatW(info,_countof(info),base?base+1:(path[0]?path:L"Unknown"));
        StringCchCatW(info,_countof(info),L"\r\nLocation: ");StringCchCatW(info,_countof(info),path[0]?path:L"Not available");
        if(path[0]){
            WIN32_FILE_ATTRIBUTE_DATA fd;
            if(GetFileAttributesExW(path,GetFileExInfoStandard,&fd)){
                ULONGLONG bytes=((ULONGLONG)fd.nFileSizeHigh<<32)|fd.nFileSizeLow;
                StringCchPrintfW(value,_countof(value),L"\r\nSize: %.2f MiB",(double)bytes/(1024.0*1024.0));
                StringCchCatW(info,_countof(info),value);
            }
        }
        StringCchCatW(info,_countof(info),L"\r\nDuration: ");
        if(duration>0)StringCchPrintfW(value,_countof(value),L"%02lld:%02lld:%02lld",(long long)(duration/3600000),(long long)((duration/60000)%60),(long long)((duration/1000)%60));
        else StringCchCopyW(value,_countof(value),L"Unknown");
        StringCchCatW(info,_countof(info),value);
        if(position>=0){StringCchPrintfW(value,_countof(value),L"\r\nPosition: %02lld:%02lld:%02lld",(long long)(position/3600000),(long long)((position/60000)%60),(long long)((position/1000)%60));StringCchCatW(info,_countof(info),value);}
        StringCchCatW(info,_countof(info),L"\r\n\r\nVIDEO / AUDIO STREAMS\r\n");
        if(haveVideoSize){StringCchPrintfW(value,_countof(value),L"Decoded video size: %u × %u\r\n",videoW,videoH);StringCchCatW(info,_countof(info),value);}
        else StringCchCatW(info,_countof(info),L"Decoded video size: Not available (audio-only or not yet decoded)\r\n");
        libvlc_media_t *trackMedia=libvlc_media_player_get_media(g_player);
        if(trackMedia){
            libvlc_media_track_t **streams=NULL;unsigned int streamCount=libvlc_media_tracks_get(trackMedia,&streams);
            unsigned int shown=0;
            for(unsigned int i=0;streams&&i<streamCount&&shown<12;i++){
                libvlc_media_track_t *t=streams[i];if(!t)continue;
                const char *desc=libvlc_media_get_codec_description(t->i_type,t->i_codec);
                value[0]=0;if(desc)MultiByteToWideChar(CP_UTF8,0,desc,-1,value,(int)_countof(value));
                if(!value[0])StringCchCopyW(value,_countof(value),L"Unknown codec");
                wchar_t line[512]=L"";
                const wchar_t *kind=t->i_type==libvlc_track_video?L"Video":t->i_type==libvlc_track_audio?L"Audio":t->i_type==libvlc_track_text?L"Subtitle":L"Other";
                BOOL active=(t->i_type==libvlc_track_audio&&t->i_id==libvlc_audio_get_track(g_player))||
                    (t->i_type==libvlc_track_video&&t->i_id==libvlc_video_get_track(g_player))||
                    (t->i_type==libvlc_track_text&&t->i_id==libvlc_video_get_spu(g_player));
                StringCchPrintfW(line,_countof(line),L"%s: %s%s\r\n",kind,value,active?L"  [active]":L"");
                StringCchCatW(info,_countof(info),line);shown++;
            }
            if(streams)libvlc_media_tracks_release(streams,streamCount);
            libvlc_media_release(trackMedia);
            if(!shown)StringCchCatW(info,_countof(info),L"No stream details reported by LibVLC.\r\n");
        }
        StringCchCatW(info,_countof(info),L"\r\nCodec names and decoded dimensions are reported only when LibVLC exposes them; they do not certify hardware decoding or HDR support.");
        ABM_MessageBoxW(hwnd,info,L"Media information",MB_OK|MB_ICONINFORMATION);
    } else if(cmd==23005){g_replayGain=!g_replayGain;SaveAudioPreferences();}
    else if(cmd==23006){g_compressor=!g_compressor;SaveAudioPreferences();}
    else if(cmd>=25100&&cmd<=25103){g_eqProfile=cmd-25100;ApplyEqProfile();SaveAudioPreferences();}
    else if(cmd==23003)g_shuffle=!g_shuffle;
    else if(cmd==23004)g_repeat=(g_repeat==0)?2:(g_repeat==2?1:0);
    else if(cmd>=25000&&cmd<=25009){
        libvlc_equalizer_t *eqp=libvlc_audio_equalizer_new_from_preset(cmd-25000);
        if(eqp){libvlc_audio_equalizer_set_preamp(eqp,0.0f);libvlc_media_player_set_equalizer(g_player,eqp);libvlc_audio_equalizer_release(eqp);g_eqProfile=10+(cmd-25000);SaveAudioPreferences();}
    }
    InvalidateRect(hwnd,NULL,FALSE);
}

static void SavePersonalCollection(void) {
    if(!g_playlistCount||!EnsureDataDirectory())return;
    wchar_t folder[PATH_CAP],path[PATH_CAP];
    if(FAILED(StringCchPrintfW(folder,_countof(folder),L"%s\\collections",g_dataDir)))return;
    if(!CreateDirectoryW(folder,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return;
    if(FAILED(StringCchPrintfW(path,_countof(path),L"%s\\My Library.m3u",folder)))return;
    OPENFILENAMEW ofn={0};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=g_main;ofn.lpstrFile=path;ofn.nMaxFile=PATH_CAP;ofn.lpstrFilter=L"ABM personal libraries (*.m3u)\0*.m3u\0";ofn.lpstrDefExt=L"m3u";ofn.lpstrTitle=L"Save personal media library";ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_EXPLORER;
    if(!GetSaveFileNameW(&ofn))return;
    FILE *fp=NULL;if(_wfopen_s(&fp,path,L"wb")!=0||!fp){ABM_MessageBoxW(g_main,L"Could not save this personal library.",APP_NAME,MB_ICONERROR);return;}
    fputs("#EXTM3U\n#ABM_PERSONAL_LIBRARY\n",fp);
    for(int i=0;i<g_playlistCount;i++){int n=WideCharToMultiByte(CP_UTF8,0,g_playlist[i],-1,NULL,0,NULL,NULL);if(n>1){char *v=(char*)malloc((size_t)n);if(v){WideCharToMultiByte(CP_UTF8,0,g_playlist[i],-1,v,n,NULL,NULL);fputs(v,fp);fputc('\n',fp);free(v);}}}
    fclose(fp);SetStatus(L"Personal library saved. Your queue and source files were left unchanged.");
}
static void LoadPersonalCollection(void) {
    if(!EnsureDataDirectory())return;
    wchar_t folder[PATH_CAP],path[PATH_CAP]=L"";if(FAILED(StringCchPrintfW(folder,_countof(folder),L"%s\\collections",g_dataDir)))return;
    if(!CreateDirectoryW(folder,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return;
    OPENFILENAMEW ofn={0};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=g_main;ofn.lpstrFile=path;ofn.nMaxFile=PATH_CAP;ofn.lpstrInitialDir=folder;ofn.lpstrFilter=L"ABM personal libraries (*.m3u)\0*.m3u\0All playlists (*.m3u8)\0*.m3u8\0";ofn.lpstrTitle=L"Load saved personal library";ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER;
    if(!GetOpenFileNameW(&ofn))return;
    FILE *fp=NULL;if(_wfopen_s(&fp,path,L"rb")!=0||!fp){ABM_MessageBoxW(g_main,L"Could not open this personal library.",APP_NAME,MB_ICONERROR);return;}
    if(g_hasMedia){PlaybackStopEngine();g_hasMedia=FALSE;}g_playlistCount=0;g_current=-1;g_selected=-1;g_playlistScroll=0;
    char line[PATH_CAP*4];while(g_playlistCount<MAX_PLAYLIST&&fgets(line,sizeof(line),fp)){size_t n=strcspn(line,"\r\n");line[n]=0;if(!line[0]||line[0]=='#')continue;UINT cp=CP_UTF8;int chars=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,line,-1,NULL,0);if(chars<=0){cp=CP_ACP;chars=MultiByteToWideChar(cp,0,line,-1,NULL,0);}if(chars>0&&chars<=PATH_CAP)MultiByteToWideChar(cp,0,line,-1,g_playlist[g_playlistCount++],PATH_CAP);}
    fclose(fp);PruneUnavailablePlaylistItems();if(g_playlistCount)g_selected=0;g_libraryTab=1;g_showFavorites=FALSE;g_showContinueWatching=FALSE;SavePersistentState();SetStatus(L"Saved personal library loaded into the playback queue.");InvalidateRect(g_main,NULL,FALSE);
}
static void OpenMediaSourceFolder(int index) {
    if(index<0||index>=g_playlistCount)return;
    const wchar_t *path=g_playlist[index];
    DWORD attrs=GetFileAttributesW(path);
    if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_DIRECTORY)){
        SetStatus(L"This media file is currently unavailable. Relink it before opening its location.");
        return;
    }

    /* Ask the Shell to open the containing folder and select the exact file.
       This is more reliable than passing a quoted /select command line to
       Explorer, especially for paths containing spaces, commas or Unicode. */
    PIDLIST_ABSOLUTE itemPidl=NULL;
    HRESULT hr=SHParseDisplayName(path,NULL,&itemPidl,0,NULL);
    if(SUCCEEDED(hr)&&itemPidl){
        PIDLIST_ABSOLUTE folderPidl=ILClone(itemPidl);
        PCUITEMID_CHILD childPidl=ILFindLastID(itemPidl);
        if(folderPidl&&ILRemoveLastID(folderPidl)){
            hr=SHOpenFolderAndSelectItems(folderPidl,1,&childPidl,0);
            CoTaskMemFree(folderPidl);
            CoTaskMemFree(itemPidl);
            if(SUCCEEDED(hr))return;
        }else{
            if(folderPidl)CoTaskMemFree(folderPidl);
            CoTaskMemFree(itemPidl);
        }
    }

    /* Fallback for Shell namespace/PIDL failures. */
    wchar_t args[PATH_CAP+32]={0};
    if(FAILED(StringCchPrintfW(args,_countof(args),L"/select,\"%s\"",path)))return;
    HINSTANCE result=ShellExecuteW(NULL,L"open",L"explorer.exe",args,NULL,SW_SHOWNORMAL);
    if((INT_PTR)result<=32)SetStatus(L"Could not open the media file location.");
}
static void ShowPlaylistContextMenu(HWND hwnd,int x,int y,int index) {
    if(index>=0&&index<g_playlistCount)g_selected=index;else g_selected=-1;
    ABMMenuItem items[]={
        {1,L"Play selected media",g_selected>=0,FALSE},
        {13,L"Open file location",g_selected>=0&&GetFileAttributesW(g_playlist[g_selected])!=INVALID_FILE_ATTRIBUTES&&!(GetFileAttributesW(g_playlist[g_selected])&FILE_ATTRIBUTE_DIRECTORY),FALSE},
        {2,L"Remove from playlist",g_selected>=0,FALSE},
        {3,L"Move up",g_selected>0,FALSE},
        {4,L"Move down",g_selected>=0&&g_selected<g_playlistCount-1,FALSE},
        {0,L"PLAYLIST",FALSE,TRUE},
        {5,L"Clear playlist",g_playlistCount>0,FALSE},
        {6,L"Save playlist...",g_playlistCount>0,FALSE},
        {0,L"PERSONAL LIBRARIES",FALSE,TRUE},
        {7,L"Save as personal library...",g_playlistCount>0,FALSE},
        {8,L"Load saved library...",TRUE,FALSE},
        {9,IsFavoritePath(index>=0&&index<g_playlistCount?g_playlist[index]:L"")?L"Remove from favourites":L"Add to favourites",index>=0&&index<g_playlistCount,FALSE},
        {10,L"Relink missing media...",index>=0&&index<g_playlistCount&&(g_missingMedia[index]||GetFileAttributesW(g_playlist[index])==INVALID_FILE_ATTRIBUTES),FALSE},
        {11,L"Continue from checkpoint",index>=0&&index<g_playlistCount&&ResumePositionForPath(g_playlist[index])>0,FALSE},
        {12,L"Start from beginning",index>=0&&index<g_playlistCount&&!g_missingMedia[index],FALSE},
    };
    int cmd=ABM_ShowMenu(hwnd,x,y,items,(int)_countof(items));
    switch(cmd){
        case 1:PlayIndex(g_selected);break;
        case 2:RemovePlaylistItem(g_selected);break;
        case 3:MovePlaylistItem(g_selected,g_selected-1);break;
        case 4:MovePlaylistItem(g_selected,g_selected+1);break;
        case 5:if(!g_advancedConfirmClearPlaylist||ABM_MessageBoxW(hwnd,L"Remove every item from the playlist? The source files will not be deleted.",APP_NAME,MB_YESNO|MB_ICONWARNING)==IDYES)ClearPlaylist();break;
        case 6:SavePlaylist();break;
        case 7:SavePersonalCollection();break;
        case 8:LoadPersonalCollection();break;
        case 9:if(g_selected>=0&&g_selected<g_playlistCount)ToggleFavoritePath(g_playlist[g_selected]);break;
        case 10:if(g_selected>=0&&g_selected<g_playlistCount)RelinkMissingMedia(g_selected);break;
        case 11:if(g_selected>=0&&g_selected<g_playlistCount)PlayIndex(g_selected);break;
        case 12:if(g_selected>=0&&g_selected<g_playlistCount){g_startFromBeginningNextPlay=TRUE;PlayIndex(g_selected);}break;
        case 13:if(g_selected>=0&&g_selected<g_playlistCount)OpenMediaSourceFolder(g_selected);break;
    }
}

static void SeekAt(int x) {
    int width=R_SEEK.right-R_SEEK.left;if(width<=0)return;
    int p=x-R_SEEK.left;if(p<0)p=0;if(p>width)p=width;
    int64_t len=libvlc_media_player_get_length(g_player);if(len<=0)return;
    PlaybackSeekTo((len*p)/width);
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
static void OverlayRoundRect(HDC dc,RECT r,COLORREF color,int radius) {
    HBRUSH brush=CreateSolidBrush(color);
    HPEN pen=CreatePen(PS_SOLID,1,color);
    HGDIOBJ oldBrush=SelectObject(dc,brush);
    HGDIOBJ oldPen=SelectObject(dc,pen);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,radius*2,radius*2);
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);
    DeleteObject(brush);DeleteObject(pen);
}
static void OverlayButton(HDC dc,RECT r,int kind,BOOL primary) {
    HBRUSH b=CreateSolidBrush(primary?ABM_THEME_COLOR_F7F8FA:ABM_THEME_COLOR_2B2E33);
    HPEN e=CreatePen(PS_SOLID,1,primary?ABM_THEME_COLOR_F7F8FA:ABM_THEME_COLOR_464A50);
    HBRUSH ob=(HBRUSH)SelectObject(dc,b);HPEN oe=(HPEN)SelectObject(dc,e);
    Ellipse(dc,r.left,r.top,r.right,r.bottom);
    SelectObject(dc,ob);SelectObject(dc,oe);DeleteObject(b);DeleteObject(e);
    int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
    HPEN g=CreatePen(PS_SOLID,2,primary?ABM_THEME_COLOR_141619:ABM_THEME_COLOR_E8EBEF);oe=(HPEN)SelectObject(dc,g);
    if(kind==1){MoveToEx(dc,x-7,y-11,NULL);LineTo(dc,x+10,y);LineTo(dc,x-7,y+11);LineTo(dc,x-7,y-11);}
    else if(kind==2){MoveToEx(dc,x-5,y-10,NULL);LineTo(dc,x-5,y+10);MoveToEx(dc,x+5,y-10,NULL);LineTo(dc,x+5,y+10);}
    else if(kind==3){RECT s={x-6,y-6,x+6,y+6};HBRUSH sb=CreateSolidBrush(primary?ABM_THEME_COLOR_141619:ABM_THEME_COLOR_E8EBEF);FillRect(dc,&s,sb);DeleteObject(sb);}
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
    HBRUSH black=CreateSolidBrush(ABM_THEME_COLOR_000000);FillRect(mem,&canvas,black);DeleteObject(black);
    SetBkMode(mem,TRANSPARENT);
    SetTextColor(mem,ABM_THEME_COLOR_DADEE4);
    HFONT oldFont=(HFONT)SelectObject(mem,g_smallFont?g_smallFont:GetStockObject(DEFAULT_GUI_FONT));

    /* High-contrast fullscreen timeline: thick track, distinct played/unplayed
       segments and a large playhead marker so position is visible at a glance. */
    /* Read the live LibVLC clock. The old overlay used cached globals that
       were reset on every track change but never refreshed, leaving the
       fullscreen playhead stuck at the start while compact mode was correct. */
    int64_t len=(g_player&&g_hasMedia)?libvlc_media_player_get_length(g_player):0;
    int64_t now=(g_player&&g_hasMedia)?libvlc_media_player_get_time(g_player):0;
    if(len<=0)len=g_overlayLength;
    if(now<0)now=0;
    int pos=len>0?(int)(((double)now/(double)len)*1000.0):0;
    if(pos<0)pos=0;if(pos>1000)pos=1000;
    int timelineLeft=R_SEEK.left;
    int timelineRight=R_SEEK.right;
    if(timelineLeft<24)timelineLeft=24;
    if(timelineRight>w-24)timelineRight=w-24;
    if(timelineRight-timelineLeft<160){timelineLeft=24;timelineRight=w-24;}
    int playheadX=timelineLeft+(timelineRight-timelineLeft)*pos/1000;
    RECT tr={timelineLeft,h-115,timelineRight,h-107};
    OverlayRoundRect(mem,tr,ABM_THEME_COLOR_243544,4);
    RECT trInner={tr.left+1,tr.top+1,tr.right-1,tr.bottom-1};
    OverlayRoundRect(mem,trInner,ABM_THEME_COLOR_162434,3);
    RECT progress=trInner;progress.right=playheadX;
    if(progress.right>progress.left)OverlayRoundRect(mem,progress,ABM_THEME_COLOR_41BEA5,3);
    /* Floating pearl playhead with a dark halo for contrast on bright footage. */
    RECT haloRect={playheadX-10,h-126,playheadX+10,h-96};
    OverlayRoundRect(mem,haloRect,ABM_THEME_COLOR_1B4C48,9);
    RECT headRect={playheadX-5,h-122,playheadX+5,h-100};
    OverlayRoundRect(mem,headRect,ABM_THEME_COLOR_FFFFFF,5);
    wchar_t elapsed[24],total[24];FormatTime((int)now,elapsed,24);FormatTime((int)len,total,24);
    RECT et={timelineLeft,h-145,timelineLeft+90,h-124};
    RECT tt={timelineRight-90,h-145,timelineRight,h-124};
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
    SetTextColor(mem,ABM_THEME_COLOR_CACFD6);
    DrawTextW(mem,L"VOLUME",-1,&vl,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT vt={w-148,h-39,w-38,h-29};
    OverlayRoundRect(mem,vt,ABM_THEME_COLOR_243544,5);
    RECT vtInner={vt.left+1,vt.top+1,vt.right-1,vt.bottom-1};
    OverlayRoundRect(mem,vtInner,ABM_THEME_COLOR_162434,4);
    int volume=g_volume;if(volume<0)volume=0;if(volume>100)volume=100;
    RECT vf=vtInner;vf.right=vf.left+(vf.right-vf.left)*volume/100;
    if(vf.right>vf.left)OverlayRoundRect(mem,vf,ABM_THEME_COLOR_41BEA5,4);
    int knobX=vf.right;
    RECT knobHalo={knobX-8,h-46,knobX+8,h-22};
    OverlayRoundRect(mem,knobHalo,ABM_THEME_COLOR_1B4C48,7);
    RECT knob={knobX-4,h-42,knobX+4,h-26};
    OverlayRoundRect(mem,knob,ABM_THEME_COLOR_FFFFFF,4);

    SelectObject(mem,oldFont);

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
    if((msg==WM_KEYDOWN||msg==WM_SYSKEYDOWN) &&
       (wp==VK_ESCAPE||wp==VK_F11)) {
        FullscreenDiag(wp==VK_ESCAPE ? "overlay-escape-keydown" : "overlay-f11-keydown",0);
        if(IsWindow(owner)) {
            SendMessageW(owner,WM_KEYDOWN,wp,lp);
            return 0;
        }
    }
    if(msg==WM_SETCURSOR){SetCursor(LoadCursorW(NULL,IDC_ARROW));return TRUE;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static LRESULT CALLBACK VideoMouseHookProc(int code, WPARAM message, LPARAM data) {
    if(code==HC_ACTION && message==WM_RBUTTONDOWN && data && IsWindow(g_main)) {
        /* WH_MOUSE is thread-scoped and can miss clicks delivered to a
           LibVLC-created video-output window on another thread. WH_MOUSE_LL
           observes the physical click before it is dispatched to any HWND. */
        const MSLLHOOKSTRUCT *mouse=(const MSLLHOOKSTRUCT*)data;
        POINT pt=mouse->pt;
        HWND target=WindowFromPoint(pt);
        if(target && IsWindow(target)) {
            HWND root=GetAncestor(target,GA_ROOTOWNER);
            if(root==g_main || target==g_main || IsChild(g_main,target)) {
                ScreenToClient(g_main,&pt);
                if(PtInRect(&R_VIDEO,pt)) {
                    /* Never open a tracking popup inside the low-level hook:
                       post the request and return immediately to keep the
                       system input hook responsive. */
                    PostMessageW(g_main,WM_VIDEO_CONTEXT_REQUEST,0,MAKELPARAM(pt.x,pt.y));
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(g_videoMouseHook,code,message,data);
}

static LRESULT CALLBACK VideoKeyboardSubclass(HWND video, UINT msg,
        WPARAM wp, LPARAM lp, UINT_PTR subclassId, DWORD_PTR refData) {
    (void)subclassId;
    HWND owner = (HWND)refData;
    if (IsWindow(owner) && (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN ||
        msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_MOUSEWHEEL)) {
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if (msg == WM_MOUSEWHEEL) {
            /* Wheel coordinates are screen-relative, unlike client mouse messages. */
            ScreenToClient(owner,&pt);
        } else {
            MapWindowPoints(video,owner,&pt,1);
        }
        /* LibVLC owns the video child HWND and receives its mouse messages
           instead of the main window. Forward right-click in owner-client
           coordinates so the playback context menu is reachable on the
           actual rendered video surface, including fullscreen. */
        if (msg == WM_RBUTTONDOWN) {
            SendMessageW(owner,WM_RBUTTONDOWN,wp,MAKELPARAM(pt.x,pt.y));
            return 0;
        }
        /* The LibVLC child HWND can physically cover the drawer even though the
           drawer is painted as a foreground overlay by the owner. Forward pointer
           input over the drawer to the owner so its header/chevron and controls
           receive the click instead of VLC's video surface. */
        if ((g_drawerOpen && !g_fullscreen && PtInRect(&R_SIDEBAR,pt)) ||
            (g_fullscreen && msg == WM_MOUSEMOVE)) {
            /* WM_MOUSEWHEEL carries screen coordinates; preserve LPARAM so
               the owner can apply the standard ScreenToClient conversion. */
            SendMessageW(owner,msg,wp,msg==WM_MOUSEWHEEL?lp:MAKELPARAM(pt.x,pt.y));
            return 0;
        }
    }
    if (msg == WM_SETCURSOR && g_fullscreen && !g_fullscreenControlsVisible) {
        SetCursor(NULL);
        return TRUE;
    }
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) &&
        (wp == VK_ESCAPE || wp == VK_F11)) {
        /* Record the actual input route and deliver synchronously so the owner
           processes the shortcut before LibVLC's child consumes the key. */
        FullscreenDiag(wp == VK_ESCAPE ? "video-child-escape-keydown" :
                       "video-child-f11-keydown", 0);
        if (IsWindow(owner)) {
            SendMessageW(owner, WM_KEYDOWN, wp, lp);
            return 0;
        }
    }
    return DefSubclassProc(video, msg, wp, lp);
}

static BOOL CALLBACK SubclassVideoDescendant(HWND child, LPARAM ownerParam) {
    HWND owner=(HWND)ownerParam;
    if(IsWindow(child) && child!=g_video && IsWindow(owner))
        SetWindowSubclass(child,VideoKeyboardSubclass,2,(DWORD_PTR)owner);
    return TRUE;
}

static void ToggleFullscreen(HWND hwnd) {
    ULONGLONG transitionStart=GetTickCount64();
    FullscreenDiag(g_fullscreen?"fullscreen-exit-begin":"fullscreen-enter-begin",0);
    g_fullscreenTransition = TRUE;

    /* Hide the VLC child only when entering fullscreen. On exit, leave the
       renderer alive and let PositionVideoWindow move it once to the restored
       client rectangle; hiding and showing the child during restore can stall
       the UI thread on some video outputs. */
    if (!g_fullscreen && g_video) ShowWindow(g_video, SW_HIDE);

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
        if(g_eqWindow)ShowWindow(g_eqWindow,SW_HIDE);
        ShowWindow(g_renderers,SW_HIDE);
        SetWindowLongPtrW(hwnd,GWL_STYLE,(LONG_PTR)(WS_POPUP|WS_VISIBLE|WS_CLIPCHILDREN));
        ULONGLONG placementStart=GetTickCount64();
        SetWindowPos(hwnd,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,
            mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,
            SWP_FRAMECHANGED|SWP_SHOWWINDOW);
        FullscreenDiag("fullscreen-enter-window-setpos",GetTickCount64()-placementStart);
    } else {
        g_fullscreen=FALSE;
        g_fullscreenControlsVisible=TRUE;
        if(GetCapture()==hwnd)ReleaseCapture();
        if(g_fullscreenOverlay)ShowWindow(g_fullscreenOverlay,SW_HIDE);
        SetCursor(LoadCursorW(NULL,IDC_ARROW));
        /* Restore the saved normal-window placement in one operation. Avoid a
           second no-op SetWindowPos/frame recalculation after SetWindowPlacement. */
        SetWindowLongPtrW(hwnd,GWL_STYLE,
            (LONG_PTR)(WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN));
        ULONGLONG placementStart=GetTickCount64();
        FullscreenDiag("fullscreen-exit-setwindowplacement-begin",0);
        SetWindowPlacement(hwnd,&g_savedPlacement);
        FullscreenDiag("fullscreen-exit-setwindowplacement",GetTickCount64()-placementStart);
        ShowWindow(hwnd,SW_RESTORE);
        ShowWindow(g_renderers,SW_SHOW);
    }

    /* Recompute layout once. WM_SIZE is suppressed during SetWindowPlacement /
       SetWindowPos above; otherwise Windows invokes UpdateLayout synchronously
       during restoration and resizes the LibVLC child before this final pass. */
    g_fullscreenTransition = FALSE;

    /* Recompute layout once. PositionVideoWindow owns the single required
       LibVLC child move/show operation; the previous duplicate SetWindowPos,
       second UpdateLayout and synchronous RedrawWindow(UPDATENOW) made Escape
       block the message loop during the return to compact mode. */
    g_videoBoundsValid = FALSE;
    ULONGLONG layoutStart=GetTickCount64();
    FullscreenDiag(g_fullscreen?"fullscreen-enter-layout-begin":"fullscreen-exit-layout-begin",0);
    UpdateLayout(hwnd);
    FullscreenDiag(g_fullscreen?"fullscreen-enter-layout":"fullscreen-exit-layout",GetTickCount64()-layoutStart);
    SyncRendererComboVisibility();

    if(g_fullscreen && g_fullscreenOverlay) {
        RECT wr;GetWindowRect(hwnd,&wr);
        SetWindowPos(g_fullscreenOverlay,HWND_TOP,wr.left,
            wr.bottom-FULLSCREEN_OVERLAY_HEIGHT,wr.right-wr.left,
            FULLSCREEN_OVERLAY_HEIGHT,SWP_NOACTIVATE);
        ShowWindow(g_fullscreenOverlay,g_fullscreenControlsVisible?SW_SHOWNOACTIVATE:SW_HIDE);
        InvalidateRect(g_fullscreenOverlay,NULL,FALSE);
    } else if(g_fullscreenOverlay) {
        ShowWindow(g_fullscreenOverlay,SW_HIDE);
    }

    RECT clientRect={0};
    GetClientRect(hwnd,&clientRect);
    if(clientRect.right>0&&clientRect.bottom>0) {
        ULONGLONG d2dStart=GetTickCount64();
        FullscreenDiag("fullscreen-d2d-resize-begin",0);
        ABM_D2D_Resize(&g_ui,(UINT)clientRect.right,(UINT)clientRect.bottom);
        FullscreenDiag("fullscreen-d2d-resize",GetTickCount64()-d2dStart);
    }
    FullscreenDiag(g_fullscreen?"fullscreen-enter-total":"fullscreen-exit-total",GetTickCount64()-transitionStart);

    /* Queue a repaint instead of forcing all child windows to paint
       synchronously while handling Escape. */
    InvalidateRect(hwnd,NULL,FALSE);
    SetFocus(hwnd);
}

static void ApplyCustomEqualizer(void) {
    if(!g_player)return;
    libvlc_equalizer_t *eq=libvlc_audio_equalizer_new();
    if(!eq)return;
    int count=libvlc_audio_equalizer_get_band_count();
    if(count>10)count=10;
    for(int i=0;i<count;i++)
        libvlc_audio_equalizer_set_amp_at_index(eq,g_eqBandGain[i],(unsigned)i);
    libvlc_audio_equalizer_set_preamp(eq,g_eqPreamp);
    libvlc_media_player_set_equalizer(g_player,eq);
    libvlc_audio_equalizer_release(eq);
}
static void EqSetPreset(int preset) {
    static const float presets[5][10]={
        {0,0,0,0,0,0,0,0,0,0},
        {7,6,4,1,-1,-1,1,3,5,6},
        {-2,0,2,4,4,2,0,-1,-2,-2},
        {4,3,1,0,-1,1,3,4,4,3},
        {3,2,0,-2,-2,0,2,4,5,5}
    };
    if(preset<0||preset>4)return;
    for(int i=0;i<10;i++)g_eqBandGain[i]=presets[preset][i];
    g_eqPreamp=preset==1?-3.0f:0.0f;
    g_eqProfile=4;
    g_eqUiPreset=preset;
    ApplyCustomEqualizer();
    SaveAudioPreferences();
    if(g_eqWindow)InvalidateRect(g_eqWindow,NULL,FALSE);
}
static void EqSetFromPoint(HWND hwnd,int x,int y) {
    (void)hwnd;
    if(y>=102&&y<=132&&x>=145&&x<=555){
        g_eqPreamp=-12.0f+24.0f*(float)(x-145)/410.0f;
        if(g_eqPreamp<-12)g_eqPreamp=-12;if(g_eqPreamp>12)g_eqPreamp=12;
        g_eqProfile=4;g_eqUiPreset=-1;ApplyCustomEqualizer();SaveAudioPreferences();return;
    }
    for(int i=0;i<10;i++){
        int cx=58+i*62;
        if(x>=cx-23&&x<=cx+23&&y>=145&&y<=330){
            float gain=12.0f-24.0f*(float)(y-155)/160.0f;
            if(gain>12)gain=12;if(gain<-12)gain=-12;
            g_eqBandGain[i]=gain;g_eqProfile=4;g_eqUiPreset=-1;
            ApplyCustomEqualizer();SaveAudioPreferences();InvalidateRect(g_eqWindow,NULL,FALSE);
            return;
        }
    }
}
static ABM_D2D_CANVAS g_equalizerCanvas;
static BOOL g_equalizerCanvasReady=FALSE;
static LRESULT CALLBACK EqualizerWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg){
    case WM_ACTIVATE:
        if(LOWORD(wp)==WA_INACTIVE){SaveAudioPreferences();ShowWindow(hwnd,SW_HIDE);}
        return 0;
    case WM_ERASEBKGND:return 1;
    case WM_CREATE:
        g_equalizerCanvasReady=ABM_D2D_CanvasInit(&g_equalizerCanvas);
        return 0;
    case WM_DESTROY:
        if(g_equalizerCanvasReady){g_equalizerCanvasReady=FALSE;ABM_D2D_CanvasDestroy(&g_equalizerCanvas);}
        return 0;
    case WM_PAINT:{
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);
        RECT client;GetClientRect(hwnd,&client);
        if(g_equalizerCanvasReady&&ABM_D2D_CanvasBegin(&g_equalizerCanvas,dc,&client)){
            ABM_D2D_CanvasFill(&g_equalizerCanvas,0,0,(float)client.right,(float)client.bottom,ABM_UI_COLOR_BACKGROUND);
            ABM_D2D_CanvasFillRound(&g_equalizerCanvas,8,8,(float)client.right-8,(float)client.bottom-8,(float)ABM_UI_RADIUS_CARD,ABM_UI_COLOR_SURFACE);
            ABM_D2D_CanvasStrokeRound(&g_equalizerCanvas,8.5f,8.5f,(float)client.right-8.5f,(float)client.bottom-8.5f,(float)ABM_UI_RADIUS_CARD,ABM_UI_COLOR_BORDER_STRONG,1.0f);
            RECT r={28,20,650,48};ABM_D2D_CanvasText(&g_equalizerCanvas,L"ADVANCED EQUALIZER",r,ABM_UI_COLOR_TEXT,ABM_D2D_TEXT_HEADING,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r.top=49;r.bottom=73;ABM_D2D_CanvasText(&g_equalizerCanvas,L"SHAPE YOUR SOUND  /  REAL-TIME AUDIO PROCESSING",r,ABM_UI_COLOR_ACCENT_BRIGHT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r.left=30;r.top=94;r.right=130;r.bottom=126;ABM_D2D_CanvasText(&g_equalizerCanvas,L"PREAMP",r,ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            ABM_D2D_CanvasFill(&g_equalizerCanvas,145,116,555,120,ABM_UI_COLOR_BORDER);
            int preX=145+(int)((g_eqPreamp+12.0f)/24.0f*410.0f);
            ABM_D2D_CanvasLine(&g_equalizerCanvas,145,118,(float)preX,118,ABM_UI_COLOR_ACCENT,3.0f);
            ABM_D2D_CanvasFillRound(&g_equalizerCanvas,(float)preX-6,112,(float)preX+6,124,6.0f,ABM_UI_COLOR_ACCENT_BRIGHT);
            wchar_t preText[32];swprintf_s(preText,_countof(preText),L"%+.1f dB",g_eqPreamp);
            r.left=565;r.top=102;r.right=650;r.bottom=132;ABM_D2D_CanvasText(&g_equalizerCanvas,preText,r,ABM_UI_COLOR_TEXT,ABM_D2D_TEXT_SMALL,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
            for(int line=-12;line<=12;line+=6){
                int yy=155+(12-line)*160/24;
                ABM_D2D_CanvasLine(&g_equalizerCanvas,38,(float)yy,648,(float)yy,ABM_UI_COLOR_BORDER,1.0f);
                wchar_t db[20];swprintf_s(db,_countof(db),L"%+d",line);
                r.left=5;r.top=yy-9;r.right=35;r.bottom=yy+9;ABM_D2D_CanvasText(&g_equalizerCanvas,db,r,ABM_UI_COLOR_TEXT_MUTED,ABM_D2D_TEXT_SMALL,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
            }
            static const wchar_t *bands[10]={L"31",L"63",L"125",L"250",L"500",L"1k",L"2k",L"4k",L"8k",L"16k"};
            for(int i=0;i<10;i++){
                int cx=58+i*62,zeroY=235;
                int thumbY=155+(int)((12.0f-g_eqBandGain[i])*160.0f/24.0f);
                ABM_D2D_CanvasLine(&g_equalizerCanvas,(float)cx,155,(float)cx,315,ABM_UI_COLOR_SURFACE_RAISED,5.0f);
                ABM_D2D_CanvasLine(&g_equalizerCanvas,(float)cx,(float)zeroY,(float)cx,(float)thumbY,ABM_UI_COLOR_ACCENT_BRIGHT,5.0f);
                ABM_D2D_CanvasFillRound(&g_equalizerCanvas,(float)cx-8,(float)thumbY-8,(float)cx+8,(float)thumbY+8,8.0f,ABM_UI_COLOR_ACCENT_ORANGE);
                wchar_t val[24];swprintf_s(val,_countof(val),L"%+.1f",g_eqBandGain[i]);
                r.left=cx-27;r.top=thumbY-29;r.right=cx+27;r.bottom=thumbY-10;
                ABM_D2D_CanvasText(&g_equalizerCanvas,val,r,ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_SMALL,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                r.left=cx-28;r.top=321;r.right=cx+28;r.bottom=340;
                ABM_D2D_CanvasText(&g_equalizerCanvas,bands[i],r,ABM_UI_COLOR_TEXT_MUTED,ABM_D2D_TEXT_SMALL,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
            static const wchar_t *presetNames[5]={L"FLAT",L"BASS",L"VOCAL",L"ROCK",L"CLASSICAL"};
            for(int i=0;i<5;i++){
                RECT pr={30+i*126,354,145+i*126,383};
                COLORREF fill=(g_eqUiPreset==i)?ABM_UI_COLOR_ACCENT:ABM_UI_COLOR_SURFACE_RAISED;
                COLORREF edge=(g_eqUiPreset==i)?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_BORDER;
                ABM_D2D_CanvasFillRound(&g_equalizerCanvas,(float)pr.left,(float)pr.top,(float)pr.right,(float)pr.bottom,(float)ABM_UI_RADIUS_BUTTON,fill);
                ABM_D2D_CanvasStrokeRound(&g_equalizerCanvas,(float)pr.left+0.5f,(float)pr.top+0.5f,(float)pr.right-0.5f,(float)pr.bottom-0.5f,(float)ABM_UI_RADIUS_BUTTON,edge,1.0f);
                ABM_D2D_CanvasText(&g_equalizerCanvas,presetNames[i],pr,ABM_UI_COLOR_TEXT,ABM_D2D_TEXT_SMALL,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
            ABM_D2D_CanvasEnd(&g_equalizerCanvas);
        }else{
            HBRUSH fallbackBg=CreateSolidBrush(ABM_UI_COLOR_BACKGROUND);FillRect(dc,&client,fallbackBg);DeleteObject(fallbackBg);
        }
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_LBUTTONDOWN:{
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        if(y>=354&&y<=386&&x>=30&&x<=649){
            int preset=(x-30)/126;if(preset>4)preset=4;
            EqSetPreset(preset);InvalidateRect(hwnd,NULL,FALSE);return 0;
        }
        g_eqDragBand=-2;EqSetFromPoint(hwnd,x,y);SetCapture(hwnd);InvalidateRect(hwnd,NULL,FALSE);return 0;
    }
    case WM_MOUSEMOVE:
        if(g_eqDragBand==-2&&(wp&MK_LBUTTON)){EqSetFromPoint(hwnd,GET_X_LPARAM(lp),GET_Y_LPARAM(lp));InvalidateRect(hwnd,NULL,FALSE);}return 0;
    case WM_LBUTTONUP:g_eqDragBand=-1;ReleaseCapture();return 0;
    case WM_KEYDOWN:if(wp==VK_ESCAPE){SaveAudioPreferences();ShowWindow(hwnd,SW_HIDE);return 0;}break;
    case WM_CLOSE:SaveAudioPreferences();ShowWindow(hwnd,SW_HIDE);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static void ShowEqualizer(void) {
    if(!g_eqWindow){
        WNDCLASSEXW wc={sizeof(wc)};wc.lpfnWndProc=EqualizerWndProc;wc.hInstance=GetModuleHandleW(NULL);
        wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName=L"ABMAVPlayer.Equalizer";
        RegisterClassExW(&wc);
        g_eqWindow=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TOPMOST,
            wc.lpszClassName,L"ABM AV Player - Advanced Equalizer",WS_POPUP,
            CW_USEDEFAULT,CW_USEDEFAULT,680,405,g_main,NULL,wc.hInstance,NULL);
        if(!g_eqWindow){ABM_MessageBoxW(g_main,L"Could not create the equalizer window.",APP_NAME,MB_ICONERROR);return;}
        SetLayeredWindowAttributes(g_eqWindow,0,244,LWA_ALPHA);
    }
    RECT owner;GetWindowRect(g_main,&owner);
    int x=owner.left+(owner.right-owner.left-680)/2;
    int y=owner.top+(owner.bottom-owner.top-405)/2;
    SetWindowPos(g_eqWindow,HWND_TOPMOST,x,y,680,405,SWP_SHOWWINDOW);
    ShowWindow(g_eqWindow,SW_SHOWNORMAL);SetForegroundWindow(g_eqWindow);SetFocus(g_eqWindow);
    InvalidateRect(g_eqWindow,NULL,TRUE);
}



/* The optional torrent DLL is loaded only when the user explicitly opens
   Torrent Stream. Session initialization runs on a worker thread so DHT,
   tracker and peer setup can never block the Win32 UI thread. */
/* Keep the player independent of libtorrent headers: resolve the complete
   stable C ABI only after the user opens Torrent Stream. */
typedef struct ABM_TorrentTaskInfo {
    uint64_t task_id;
    double progress;
    int64_t total_bytes, downloaded_bytes, uploaded_bytes;
    int download_rate, upload_rate, peer_count, state, has_metadata, file_count;
    int known_peers, connect_candidates, connections, seeds;
    int tracker_count, tracker_verified, tracker_failures;
    int announcing_to_dht, announcing_to_trackers;
    int dht_running, is_private;
    wchar_t tracker_diagnostic[512];
    wchar_t name[512];
    wchar_t error[512];
} ABM_TorrentTaskInfo;
typedef BOOL (WINAPI *ABM_TorrentInitializeFn)(const wchar_t *,const wchar_t *);
typedef void (WINAPI *ABM_TorrentShutdownFn)(void);
typedef BOOL (WINAPI *ABM_TorrentAddMagnetFn)(const wchar_t *,uint64_t *,wchar_t *,unsigned int);
typedef BOOL (WINAPI *ABM_TorrentAddFileFn)(const wchar_t *,uint64_t *,wchar_t *,unsigned int);
typedef BOOL (WINAPI *ABM_TorrentGetTaskFn)(uint64_t,ABM_TorrentTaskInfo *);
typedef BOOL (WINAPI *ABM_TorrentGetFileFn)(uint64_t,int,wchar_t *,unsigned int,int64_t *);
typedef BOOL (WINAPI *ABM_TorrentSelectFileFn)(uint64_t,int);
typedef BOOL (WINAPI *ABM_TorrentTaskActionFn)(uint64_t);
typedef BOOL (WINAPI *ABM_TorrentRemoveFn)(uint64_t,BOOL);
typedef BOOL (WINAPI *ABM_TorrentSetRateLimitsFn)(int,int);
typedef BOOL (WINAPI *ABM_TorrentGetCacheInfoFn)(uint64_t *,uint64_t *);
typedef BOOL (WINAPI *ABM_TorrentSetCacheLimitFn)(uint64_t,uint64_t *);
typedef BOOL (WINAPI *ABM_TorrentClearCacheFn)(uint64_t *,uint64_t *);
typedef BOOL (WINAPI *ABM_TorrentSetReadWindowFn)(uint64_t,int,int64_t,int64_t,int);
typedef BOOL (WINAPI *ABM_TorrentWaitForRangeFn)(uint64_t,int,int64_t,int64_t,unsigned int);
typedef BOOL (WINAPI *ABM_TorrentStartHttpStreamFn)(uint64_t,int,wchar_t *,unsigned int,wchar_t *,unsigned int);
typedef BOOL (WINAPI *ABM_TorrentStartLanHttpStreamFn)(uint64_t,int,wchar_t *,unsigned int,wchar_t *,unsigned int);
void ABM_TorrentDlna_StartDiscovery(void);
void ABM_TorrentDlna_StopDiscovery(void);
unsigned int ABM_TorrentDlna_DeviceCount(void);
BOOL ABM_TorrentDlna_DeviceName(unsigned int,wchar_t *,unsigned int);
BOOL ABM_TorrentDlna_SelectDevice(unsigned int);
BOOL ABM_TorrentDlna_PlayUrl(const wchar_t *);
BOOL ABM_TorrentDlna_StopPlayback(void);
typedef void (WINAPI *ABM_TorrentStopHttpStreamFn)(void);
static HMODULE g_torrentModule=NULL;
static ABM_TorrentInitializeFn g_torrentInitialize=NULL;
static ABM_TorrentShutdownFn g_torrentShutdown=NULL;
static ABM_TorrentAddMagnetFn g_torrentAddMagnet=NULL;
static ABM_TorrentAddFileFn g_torrentAddFile=NULL;
static ABM_TorrentGetTaskFn g_torrentGetTask=NULL;
static ABM_TorrentGetFileFn g_torrentGetFile=NULL;
static ABM_TorrentSelectFileFn g_torrentSelectFile=NULL;
static ABM_TorrentTaskActionFn g_torrentPause=NULL,g_torrentResume=NULL;
static ABM_TorrentRemoveFn g_torrentRemove=NULL;
static ABM_TorrentSetRateLimitsFn g_torrentSetRateLimits=NULL;
static ABM_TorrentGetCacheInfoFn g_torrentGetCacheInfo=NULL;
static ABM_TorrentSetCacheLimitFn g_torrentSetCacheLimit=NULL;
static ABM_TorrentClearCacheFn g_torrentClearCache=NULL;
static ABM_TorrentSetReadWindowFn g_torrentSetReadWindow=NULL;
static ABM_TorrentWaitForRangeFn g_torrentWaitForRange=NULL;
static ABM_TorrentStartHttpStreamFn g_torrentStartHttpStream=NULL;
static ABM_TorrentStartLanHttpStreamFn g_torrentStartLanHttpStream=NULL;
static ABM_TorrentStopHttpStreamFn g_torrentStopHttpStream=NULL;
static HANDLE g_torrentInitThread=NULL;
static volatile LONG g_torrentInitResult=0; /* 0=pending, 1=ready, -1=failed */
static BOOL g_torrentInitPending=FALSE;
static BOOL g_torrentInitialized=FALSE;
static void ShowTorrentWindow(HWND owner);
static void ShowSettingsDialog(HWND owner);
static BOOL EnsureTorrentCacheBaseDir(void);
static volatile LONG g_torrentStreamCancel=0;
/* Scalar completion tokens keep queued WM_TORRENT_STREAM_READY messages independent
   of request memory, which remains owned by the UI thread. */
static volatile LONG g_torrentStreamGeneration=0;
static HWND g_torrentWindow=NULL,g_torrentMagnetEdit=NULL,g_torrentFileList=NULL,g_torrentInfoLabel=NULL,g_torrentStatusLabel=NULL;
static HWND g_torrentDownLimitEdit=NULL,g_torrentUpLimitEdit=NULL,g_torrentBufferBar=NULL;
static HWND g_torrentCacheLimitEdit=NULL,g_torrentCacheInfoLabel=NULL;
static HWND g_torrentNumericButtons[6]={0};
static HWND g_torrentTvCombo=NULL,g_torrentDownloadPathLabel=NULL,g_torrentOpenDownloadsButton=NULL;
static HWND g_torrentSourceHint=NULL,g_torrentDownLabel=NULL,g_torrentUpLabel=NULL,g_torrentCacheLabel=NULL;
static HWND g_torrentActionButtons[5]={0},g_torrentTvButtons[3]={0};
static ABM_D2D g_torrentUi;
static BOOL g_torrentUiReady=FALSE;
static ABM_D2D_CANVAS g_torrentButtonCanvas;
static BOOL g_torrentButtonCanvasReady=FALSE;
static int g_torrentTvKnownCount=-1;
static BOOL g_torrentTvPlaybackActive=FALSE;
static HANDLE g_torrentTvStreamThread=NULL;
static volatile LONG g_torrentTvStreamCancel=0;
typedef struct TorrentTvStreamRequest {
    HWND owner; uint64_t task_id; int file_index; int64_t file_size; BOOL ok;
    wchar_t url[2048]; wchar_t error[512];
} TorrentTvStreamRequest;
static TorrentTvStreamRequest *g_torrentTvStreamRequest=NULL;
static uint64_t g_torrentTaskId=0;
static int g_torrentListedFileCount=0,g_torrentListedForTask=-1,g_torrentSelectedFile=-1;
typedef enum TorrentPlaybackState {
    TORRENT_PLAYBACK_IDLE=0,
    TORRENT_PLAYBACK_BUFFERING_START,
    TORRENT_PLAYBACK_PLAYING,
    TORRENT_PLAYBACK_BUFFERING,
    TORRENT_PLAYBACK_RECOVERING,
    TORRENT_PLAYBACK_PAUSED,
    TORRENT_PLAYBACK_STOPPED,
    TORRENT_PLAYBACK_FAILED
} TorrentPlaybackState;
static volatile LONG g_torrentPlaybackState=TORRENT_PLAYBACK_IDLE;
static volatile LONG g_torrentBufferPercent=0;
static BOOL g_torrentEverPlayed=FALSE;
static int64_t g_torrentLastMediaTime=-1;
static ULONGLONG g_torrentLastProgressTick=0;
static ULONGLONG g_torrentBufferingSinceTick=0;
static libvlc_event_manager_t *g_torrentPlayerEventManager=NULL;
/* Every torrent playback session owns a generation. Queued LibVLC callbacks
   from an older session are discarded by the UI thread. */
static volatile LONG g_torrentPlaybackGeneration=0;
static LONG g_torrentAttachedGeneration=0;
static libvlc_event_type_t g_torrentAttachedEvents[7];
static int g_torrentAttachedEventCount=0;
typedef struct TorrentStreamRequest {
    HWND owner;
    LONG generation;
    uint64_t task_id;
    int file_index;
    int64_t file_size;
    BOOL ok;
    wchar_t url[2048];
    wchar_t error[512];
} TorrentStreamRequest;
static TorrentStreamRequest *g_torrentStreamRequest=NULL;
#define ID_TORRENT_MAGNET 5101
#define ID_TORRENT_ADD_MAGNET 5102
#define ID_TORRENT_BROWSE 5103
#define ID_TORRENT_FILES 5104
#define ID_TORRENT_PAUSE 5105
#define ID_TORRENT_STREAM 5106
#define ID_TORRENT_REMOVE 5107
#define ID_TORRENT_STOP 5108
#define ID_TORRENT_REFRESH 5109
#define ID_TORRENT_CACHE_LIMIT 5120
#define ID_TORRENT_CLEAR_CACHE 5121
#define ID_TORRENT_CACHE_APPLY 5122
#define ID_TORRENT_DOWN_LIMIT 5110
#define ID_TORRENT_UP_LIMIT 5111
#define ID_TORRENT_APPLY_LIMITS 5112
#define ID_TORRENT_FIND_TVS 5123
#define ID_TORRENT_TV_COMBO 5124
#define ID_TORRENT_CAST_TV 5125
#define ID_TORRENT_STOP_TV 5126
#define ID_TORRENT_OPEN_DOWNLOADS 5127
#define ID_TORRENT_DOWN_DEC 5130
#define ID_TORRENT_DOWN_INC 5131
#define ID_TORRENT_UP_DEC 5132
#define ID_TORRENT_UP_INC 5133
#define ID_TORRENT_CACHE_DEC 5134
#define ID_TORRENT_CACHE_INC 5135

static BOOL LoadTorrentEngine(HWND owner) {
    if(g_torrentModule && g_torrentInitialize && g_torrentShutdown) return TRUE;
    wchar_t exePath[PATH_CAP]={0}, dllPath[PATH_CAP]={0};
    DWORD n=GetModuleFileNameW(NULL,exePath,_countof(exePath));
    if(!n || n>=_countof(exePath)) return FALSE;
    wchar_t *slash=wcsrchr(exePath,L'\\');
    if(!slash) return FALSE;
    *slash=0;
    if(FAILED(StringCchPrintfW(dllPath,_countof(dllPath),
        L"%s\\torrent-runtime\\abm_torrent_engine.dll",exePath))) return FALSE;
    g_torrentModule=LoadLibraryExW(dllPath,NULL,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!g_torrentModule) {
        wchar_t message[PATH_CAP+256];
        StringCchPrintfW(message,_countof(message),
            L"The optional torrent engine was not found or could not load.\n\nExpected:\n%s\n\nBuild the Windows x64 torrent runtime and keep its dependency DLLs in the same torrent-runtime folder.",
            dllPath);
        ABM_MessageBoxW(owner,message,L"Torrent Stream",MB_OK|MB_ICONINFORMATION);
        return FALSE;
    }
    g_torrentInitialize=(ABM_TorrentInitializeFn)GetProcAddress(g_torrentModule,"ABM_Torrent_Initialize");
    g_torrentShutdown=(ABM_TorrentShutdownFn)GetProcAddress(g_torrentModule,"ABM_Torrent_Shutdown");
    g_torrentAddMagnet=(ABM_TorrentAddMagnetFn)GetProcAddress(g_torrentModule,"ABM_Torrent_AddMagnet");
    g_torrentAddFile=(ABM_TorrentAddFileFn)GetProcAddress(g_torrentModule,"ABM_Torrent_AddFile");
    g_torrentGetTask=(ABM_TorrentGetTaskFn)GetProcAddress(g_torrentModule,"ABM_Torrent_GetTask");
    g_torrentGetFile=(ABM_TorrentGetFileFn)GetProcAddress(g_torrentModule,"ABM_Torrent_GetFile");
    g_torrentSelectFile=(ABM_TorrentSelectFileFn)GetProcAddress(g_torrentModule,"ABM_Torrent_SelectFile");
    g_torrentPause=(ABM_TorrentTaskActionFn)GetProcAddress(g_torrentModule,"ABM_Torrent_Pause");
    g_torrentResume=(ABM_TorrentTaskActionFn)GetProcAddress(g_torrentModule,"ABM_Torrent_Resume");
    g_torrentRemove=(ABM_TorrentRemoveFn)GetProcAddress(g_torrentModule,"ABM_Torrent_Remove");
    g_torrentSetRateLimits=(ABM_TorrentSetRateLimitsFn)GetProcAddress(g_torrentModule,"ABM_Torrent_SetRateLimits");
    g_torrentGetCacheInfo=(ABM_TorrentGetCacheInfoFn)GetProcAddress(g_torrentModule,"ABM_Torrent_GetCacheInfo");
    g_torrentSetCacheLimit=(ABM_TorrentSetCacheLimitFn)GetProcAddress(g_torrentModule,"ABM_Torrent_SetCacheLimit");
    g_torrentClearCache=(ABM_TorrentClearCacheFn)GetProcAddress(g_torrentModule,"ABM_Torrent_ClearCache");
    g_torrentSetReadWindow=(ABM_TorrentSetReadWindowFn)GetProcAddress(g_torrentModule,"ABM_Torrent_SetReadWindow");
    g_torrentWaitForRange=(ABM_TorrentWaitForRangeFn)GetProcAddress(g_torrentModule,"ABM_Torrent_WaitForRange");
    g_torrentStartHttpStream=(ABM_TorrentStartHttpStreamFn)GetProcAddress(g_torrentModule,"ABM_Torrent_StartHttpStream");
    g_torrentStartLanHttpStream=(ABM_TorrentStartLanHttpStreamFn)GetProcAddress(g_torrentModule,"ABM_Torrent_StartLanHttpStream");
    g_torrentStopHttpStream=(ABM_TorrentStopHttpStreamFn)GetProcAddress(g_torrentModule,"ABM_Torrent_StopHttpStream");
    if(!g_torrentInitialize || !g_torrentShutdown || !g_torrentAddMagnet ||
       !g_torrentAddFile || !g_torrentGetTask || !g_torrentGetFile ||
       !g_torrentSelectFile || !g_torrentPause || !g_torrentResume ||
       !g_torrentRemove || !g_torrentSetRateLimits || !g_torrentGetCacheInfo ||
       !g_torrentSetCacheLimit || !g_torrentClearCache || !g_torrentSetReadWindow ||
       !g_torrentWaitForRange || !g_torrentStartHttpStream || !g_torrentStartLanHttpStream || !g_torrentStopHttpStream) {
        ABM_MessageBoxW(owner,L"The torrent engine DLL is missing one or more required C ABI exports. Rebuild and install the matching Windows x64 runtime.",
                    L"Torrent Stream",MB_OK|MB_ICONERROR);
        FreeLibrary(g_torrentModule);g_torrentModule=NULL;
        g_torrentInitialize=NULL;g_torrentShutdown=NULL;
        g_torrentAddMagnet=NULL;g_torrentAddFile=NULL;g_torrentGetTask=NULL;
        g_torrentGetFile=NULL;g_torrentSelectFile=NULL;g_torrentPause=NULL;
        g_torrentResume=NULL;g_torrentRemove=NULL;g_torrentSetRateLimits=NULL;
        g_torrentGetCacheInfo=NULL;g_torrentSetCacheLimit=NULL;g_torrentClearCache=NULL;
        g_torrentSetReadWindow=NULL;g_torrentWaitForRange=NULL;
        g_torrentStartHttpStream=NULL;g_torrentStartLanHttpStream=NULL;g_torrentStopHttpStream=NULL;
        return FALSE;
    }
    return TRUE;
}

static DWORD WINAPI TorrentInitializeWorker(void *parameter) {
    HWND owner=(HWND)parameter;
    BOOL ok=g_torrentInitialize && g_torrentInitialize(g_torrentActiveCacheBaseDir[0]?g_torrentActiveCacheBaseDir:(g_torrentCacheBaseDir[0]?g_torrentCacheBaseDir:g_dataDir),g_torrentActiveDownloadDir[0]?g_torrentActiveDownloadDir:g_torrentDownloadDir);
    InterlockedExchange(&g_torrentInitResult,ok?1:-1);
    if(owner)PostMessageW(owner,WM_TORRENT_INIT_DONE,0,0);
    return 0;
}

static void StartTorrentEngine(HWND owner) {
    if(g_torrentInitialized) { ShowTorrentWindow(owner); return; }
    if(g_torrentInitPending) {
        SetStatus(L"Torrent engine initialization is already in progress...");
        return;
    }
    if(!EnsureDataDirectory() || !EnsureTorrentCacheBaseDir()) {
        ABM_MessageBoxW(owner,L"ABM AV Player could not prepare its application data directory or torrent cache location.",
                    L"Torrent Stream",MB_OK|MB_ICONWARNING);
        return;
    }
    if(!LoadTorrentEngine(owner)) {
        SetStatus(L"Torrent engine runtime is unavailable");
        return;
    }
    InterlockedExchange(&g_torrentInitResult,0);
    g_torrentInitPending=TRUE;
    wcsncpy_s(g_torrentActiveCacheBaseDir,_countof(g_torrentActiveCacheBaseDir),g_torrentCacheBaseDir,_TRUNCATE);
    wcsncpy_s(g_torrentActiveDownloadDir,_countof(g_torrentActiveDownloadDir),g_torrentDownloadDir,_TRUNCATE);
    g_torrentInitThread=CreateThread(NULL,0,TorrentInitializeWorker,owner,0,NULL);
    if(!g_torrentInitThread) {
        g_torrentInitPending=FALSE;
        SetStatus(L"Could not start torrent engine initialization");
        ABM_MessageBoxW(owner,L"Windows could not start the torrent initialization worker.",
                    L"Torrent Stream",MB_OK|MB_ICONERROR);
        return;
    }
    SetStatus(L"Initializing optional libtorrent engine...");
}


static void TorrentSetLabel(HWND hwnd,const wchar_t *value) {
    if(hwnd&&IsWindow(hwnd)){
        SetWindowTextW(hwnd,value?value:L"");
        /* The Torrent labels are data holders; the parent paints their text
           through the canonical DirectWrite renderer. */
        if(g_torrentWindow)InvalidateRect(g_torrentWindow,NULL,FALSE);
    }
}
static const wchar_t *TorrentPlaybackStateText(void) {
    switch((TorrentPlaybackState)InterlockedCompareExchange(&g_torrentPlaybackState,0,0)) {
        case TORRENT_PLAYBACK_BUFFERING_START:return L"Buffering before playback starts…";
        case TORRENT_PLAYBACK_PLAYING:return L"Playing normally";
        case TORRENT_PLAYBACK_BUFFERING:return L"Buffering during playback…";
        case TORRENT_PLAYBACK_RECOVERING:return L"Recovering after a network interruption…";
        case TORRENT_PLAYBACK_PAUSED:return L"Playback paused";
        case TORRENT_PLAYBACK_STOPPED:return L"Stream stopped";
        case TORRENT_PLAYBACK_FAILED:return L"Stream failed";
        default:return NULL;
    }
}
static void TorrentSetBufferProgress(int percent) {
    if(percent<0)percent=0;if(percent>100)percent=100;
    if(g_torrentBufferBar&&IsWindow(g_torrentBufferBar)) {
        SendMessageW(g_torrentBufferBar,PBM_SETMARQUEE,FALSE,0);
        SendMessageW(g_torrentBufferBar,PBM_SETPOS,(WPARAM)percent,0);
    }
}
static void TorrentSetPlaybackState(TorrentPlaybackState state,const wchar_t *detail) {
    InterlockedExchange(&g_torrentPlaybackState,(LONG)state);
    if(state==TORRENT_PLAYBACK_BUFFERING_START||state==TORRENT_PLAYBACK_BUFFERING) {
        if(state==TORRENT_PLAYBACK_BUFFERING_START&&!g_torrentPlaybackActive&&g_torrentBufferBar&&IsWindow(g_torrentBufferBar))
            SendMessageW(g_torrentBufferBar,PBM_SETMARQUEE,TRUE,30);
        else
            TorrentSetBufferProgress((int)InterlockedCompareExchange(&g_torrentBufferPercent,0,0));
    }
    else if(state==TORRENT_PLAYBACK_PLAYING)TorrentSetBufferProgress(100);
    else if(state==TORRENT_PLAYBACK_RECOVERING||state==TORRENT_PLAYBACK_STOPPED||state==TORRENT_PLAYBACK_FAILED)
        TorrentSetBufferProgress(0);
    const wchar_t *label=detail?detail:TorrentPlaybackStateText();
    if(label) {
        TorrentSetLabel(g_torrentStatusLabel,label);
        SetStatus(label);
    }
}
static void TorrentPlayerEvent(const libvlc_event_t *event,void *opaque) {
    if(!event||!g_main)return;
    LONG generation=(LONG)(INT_PTR)opaque;
    if(generation==0 ||
       InterlockedCompareExchange(&g_torrentPlaybackGeneration,0,0)!=generation)return;
    LONG value=0;
    if(event->type==libvlc_MediaPlayerBuffering) {
        float percent=event->u.media_player_buffering.new_cache;
        if(percent<0.0f)percent=0.0f;if(percent>100.0f)percent=100.0f;
        value=(LONG)(percent+0.5f);
    } 
    /* x64 build: pack generation and the small buffering value into LPARAM.
       TimeChanged reads the current player clock on the UI thread, avoiding
       truncation of LibVLC's 64-bit media timestamp. */
    uint64_t payload=((uint64_t)(uint32_t)generation<<32)|(uint32_t)value;
    PostMessageW(g_main,WM_TORRENT_PLAYER_EVENT,(WPARAM)event->type,(LPARAM)(LONG_PTR)payload);
}
static BOOL TorrentAttachPlayerEvents(void) {
    if(g_torrentPlayerEventManager)return TRUE;
    if(!g_player)return FALSE;
    libvlc_event_manager_t *manager=libvlc_media_player_event_manager(g_player);
    if(!manager)return FALSE;
    LONG generation=InterlockedCompareExchange(&g_torrentPlaybackGeneration,0,0);
    if(generation==0)generation=InterlockedIncrement(&g_torrentPlaybackGeneration);
    void *callbackContext=(void*)(INT_PTR)generation;
    static const libvlc_event_type_t types[]={
        libvlc_MediaPlayerBuffering,libvlc_MediaPlayerPlaying,
        libvlc_MediaPlayerPaused,libvlc_MediaPlayerStopped,
        libvlc_MediaPlayerEndReached,libvlc_MediaPlayerEncounteredError,
        libvlc_MediaPlayerTimeChanged
    };
    g_torrentAttachedEventCount=0;
    for(int i=0;i<(int)_countof(types);i++) {
        if(libvlc_event_attach(manager,types[i],TorrentPlayerEvent,callbackContext)!=0) {
            for(int j=0;j<g_torrentAttachedEventCount;j++)
                libvlc_event_detach(manager,g_torrentAttachedEvents[j],TorrentPlayerEvent,callbackContext);
            g_torrentAttachedEventCount=0;
            InterlockedIncrement(&g_torrentPlaybackGeneration);
            return FALSE;
        }
        g_torrentAttachedEvents[g_torrentAttachedEventCount++]=types[i];
    }
    g_torrentPlayerEventManager=manager;
    g_torrentAttachedGeneration=generation;
    return TRUE;
}
static void TorrentDetachPlayerEvents(void) {
    /* Invalidate first: callbacks already in flight and messages already
       queued carry the previous generation and cannot mutate a later session. */
    InterlockedIncrement(&g_torrentPlaybackGeneration);
    if(g_torrentPlayerEventManager) {
        void *callbackContext=(void*)(INT_PTR)g_torrentAttachedGeneration;
        for(int i=0;i<g_torrentAttachedEventCount;i++)
            libvlc_event_detach(g_torrentPlayerEventManager,g_torrentAttachedEvents[i],TorrentPlayerEvent,callbackContext);
    }
    g_torrentPlayerEventManager=NULL;
    g_torrentAttachedEventCount=0;
    g_torrentAttachedGeneration=0;
}
static void TorrentCheckPlaybackStall(void) {
    if(!g_torrentPlaybackActive||!g_torrentPlayerEventManager)return;
    TorrentPlaybackState state=(TorrentPlaybackState)InterlockedCompareExchange(&g_torrentPlaybackState,0,0);
    ULONGLONG now=GetTickCount64();
    BOOL stalledPlaying=(state==TORRENT_PLAYBACK_PLAYING&&g_torrentLastProgressTick&&
        now-g_torrentLastProgressTick>=6000);
    BOOL stalledBuffering=(state==TORRENT_PLAYBACK_BUFFERING&&g_torrentBufferingSinceTick&&
        now-g_torrentBufferingSinceTick>=6000);
    if(stalledPlaying||stalledBuffering) {
        TorrentSetPlaybackState(TORRENT_PLAYBACK_RECOVERING,
            L"Recovering after a network interruption — waiting for verified torrent pieces…");
    }
}
static void TorrentFormatBytes(int64_t bytes,wchar_t *out,size_t cap) {
    if(bytes<0)bytes=0;
    if(bytes>=1073741824LL)StringCchPrintfW(out,cap,L"%.2f GiB",(double)bytes/1073741824.0);
    else if(bytes>=1048576LL)StringCchPrintfW(out,cap,L"%.1f MiB",(double)bytes/1048576.0);
    else if(bytes>=1024LL)StringCchPrintfW(out,cap,L"%.1f KiB",(double)bytes/1024.0);
    else StringCchPrintfW(out,cap,L"%lld B",(long long)bytes);
}
static BOOL TorrentIsPlayableVideo(const wchar_t *path){
    if(!path)return FALSE;
    const wchar_t *dot=wcsrchr(path,L'.');if(!dot)return FALSE;
    return !_wcsicmp(dot,L".mp4")||!_wcsicmp(dot,L".mkv")||!_wcsicmp(dot,L".avi")||
        !_wcsicmp(dot,L".mov")||!_wcsicmp(dot,L".wmv")||!_wcsicmp(dot,L".webm")||
        !_wcsicmp(dot,L".m4v")||!_wcsicmp(dot,L".mpg")||!_wcsicmp(dot,L".mpeg")||
        !_wcsicmp(dot,L".ts")||!_wcsicmp(dot,L".m2ts")||!_wcsicmp(dot,L".ogv")||
        !_wcsicmp(dot,L".3gp");
}
static void TorrentRefreshFiles(void) {
    if(!g_torrentWindow||!g_torrentTaskId||!g_torrentGetTask||!g_torrentGetFile)return;
    ABM_TorrentTaskInfo info={0};
    if(!g_torrentGetTask(g_torrentTaskId,&info))return;
    if(!info.has_metadata||info.file_count<=0)return;
    if(g_torrentListedForTask==(int)(g_torrentTaskId&0x7fffffff))return;
    SendMessageW(g_torrentFileList,LB_RESETCONTENT,0,0);
    g_torrentListedFileCount=0;g_torrentSelectedFile=-1;
    int preferredRow=-1;int64_t preferredSize=-1;int row=0;
    for(int i=0;i<info.file_count;i++) {
        wchar_t path[1024]=L"",sizeText[64]=L"",line[1200]=L"";
        int64_t size=0;
        if(!g_torrentGetFile(g_torrentTaskId,i,path,(unsigned)_countof(path),&size))continue;
        TorrentFormatBytes(size,sizeText,_countof(sizeText));
        StringCchPrintfW(line,_countof(line),L"%s   (%s)",path,sizeText);
        LRESULT item=SendMessageW(g_torrentFileList,LB_ADDSTRING,0,(LPARAM)line);
        if(item!=LB_ERR&&item!=LB_ERRSPACE){
            SendMessageW(g_torrentFileList,LB_SETITEMDATA,(WPARAM)item,(LPARAM)i);
            if(TorrentIsPlayableVideo(path)&&size>preferredSize){preferredRow=(int)item;preferredSize=size;}
        }
        g_torrentListedFileCount++;row++;
    }
    g_torrentListedForTask=(int)(g_torrentTaskId&0x7fffffff);
    if(g_torrentListedFileCount>0){
        int initial=preferredRow>=0?preferredRow:0;
        SendMessageW(g_torrentFileList,LB_SETCURSEL,(WPARAM)initial,0);
        if(preferredRow>=0)TorrentSetLabel(g_torrentStatusLabel,L"Video selected automatically. Press Play / Stream to begin buffering.");
        else TorrentSetLabel(g_torrentStatusLabel,L"No video file detected. Select a supported media file manually.");
    }
}
static void TorrentRefreshCacheStatus(void) {
    if(!g_torrentInitialized||!g_torrentGetCacheInfo||!g_torrentCacheInfoLabel)return;
    uint64_t used=0,limit=0;
    if(!g_torrentGetCacheInfo(&used,&limit))return;
    wchar_t usedText[64],limitText[64],line[256];
    TorrentFormatBytes((int64_t)(used>INT64_MAX?INT64_MAX:used),usedText,_countof(usedText));
    TorrentFormatBytes((int64_t)(limit>INT64_MAX?INT64_MAX:limit),limitText,_countof(limitText));
    StringCchPrintfW(line,_countof(line),L"Disk cache: %s used of %s  |  Location: %s\\torrent-cache",
        usedText,limitText,(g_torrentInitialized||g_torrentInitPending)?(g_torrentActiveCacheBaseDir[0]?g_torrentActiveCacheBaseDir:g_dataDir):(g_torrentCacheBaseDir[0]?g_torrentCacheBaseDir:g_dataDir));
    TorrentSetLabel(g_torrentCacheInfoLabel,line);
    if(g_torrentCacheLimitEdit&&GetFocus()!=g_torrentCacheLimitEdit) {
        wchar_t quota[32];StringCchPrintfW(quota,_countof(quota),L"%llu",(unsigned long long)(limit/(1024ULL*1024ULL*1024ULL)));
        SetWindowTextW(g_torrentCacheLimitEdit,quota);
    }
}
static void TorrentRefreshStatus(void) {
    if(!g_torrentWindow||!g_torrentTaskId||!g_torrentGetTask)return;
    ABM_TorrentTaskInfo info={0};
    if(!g_torrentGetTask(g_torrentTaskId,&info))return;
    wchar_t down[64],up[64],total[64],status[1024],diagnostic[512];
    TorrentFormatBytes(info.downloaded_bytes,down,_countof(down));
    TorrentFormatBytes(info.total_bytes,total,_countof(total));
    TorrentFormatBytes(info.uploaded_bytes,up,_countof(up));
    const wchar_t *state=L"Connecting";
    switch(info.state) {
        case 0:state=L"Queued";break;case 1:state=L"Checking";break;
        case 2:state=L"Downloading";break;case 3:state=L"Complete";break;
        case 4:state=L"Seeding";break;case 5:state=L"Paused";break;
        case 6:state=L"Error";break;
    }
    StringCchPrintfW(status,_countof(status),L"%s  |  %.1f%%  |  %s / %s  |  ↓ %.1f KiB/s  ↑ %.1f KiB/s  |  Peers: %d  |  Uploaded: %s",
        info.name,info.progress*100.0,down,total,(double)info.download_rate/1024.0,
        (double)info.upload_rate/1024.0,info.peer_count,up);
    StringCchPrintfW(diagnostic,_countof(diagnostic),
        L"Trackers %d (%d verified, %d failures)  |  DHT %s  |  Tracker announce %s  |  Known peers %d  |  Connecting %d  |  Connections %d  |  Seeds %d%s%s",
        info.tracker_count,info.tracker_verified,info.tracker_failures,
        info.announcing_to_dht?L"enabled":L"off",
        info.announcing_to_trackers?L"enabled":L"off",
        info.known_peers,info.connect_candidates,info.connections,info.seeds,
        info.tracker_diagnostic[0]?L"  |  Last tracker response: ":L"",
        info.tracker_diagnostic);
    TorrentSetLabel(g_torrentInfoLabel,status);
    if(info.error[0])TorrentSetLabel(g_torrentStatusLabel,info.error);
    else if(!info.has_metadata)TorrentSetLabel(g_torrentStatusLabel,L"Fetching torrent metadata…");
    else if(info.file_count>0&&info.total_bytes==0)TorrentSetLabel(g_torrentStatusLabel,L"Torrent metadata loaded. Select a video file to begin downloading.");
    else TorrentSetLabel(g_torrentStatusLabel,diagnostic);
    TorrentRefreshFiles();
    TorrentPlaybackState playbackState=(TorrentPlaybackState)InterlockedCompareExchange(&g_torrentPlaybackState,0,0);
    const wchar_t *playbackText=TorrentPlaybackStateText();
    if(playbackState==TORRENT_PLAYBACK_BUFFERING_START||playbackState==TORRENT_PLAYBACK_BUFFERING) {
        wchar_t buffering[768];
        StringCchPrintfW(buffering,_countof(buffering),
            L"%s (%ld%%) | Trackers %d/%d Fail %d | DHT %s/%s | %s | Known %d | Connecting %d | Connected %d",
            playbackState==TORRENT_PLAYBACK_BUFFERING_START?L"Buffering…":L"Buffering…",
            InterlockedCompareExchange(&g_torrentBufferPercent,0,0),
            info.tracker_verified,info.tracker_count,info.tracker_failures,
            info.dht_running?L"RUN":L"STOP",info.announcing_to_dht?L"ANNOUNCE":L"IDLE",
            info.is_private?L"PRIVATE":L"PUBLIC",info.known_peers,info.connect_candidates,
            info.connections);
        if(info.tracker_diagnostic[0]){
            StringCchCatW(buffering,_countof(buffering),L"  |  Tracker response: ");
            StringCchCatW(buffering,_countof(buffering),info.tracker_diagnostic);
        }
        TorrentSetLabel(g_torrentStatusLabel,buffering);
    } else if(playbackText)TorrentSetLabel(g_torrentStatusLabel,playbackText);
}
static void TorrentCancelStreamPreparation(void) {
    /* Invalidate already-queued completion messages before touching request memory. */
    InterlockedIncrement(&g_torrentStreamGeneration);
    InterlockedExchange(&g_torrentStreamCancel,1);
    if(g_torrentStreamThread) {
        if(g_torrentStopHttpStream)g_torrentStopHttpStream();
        WaitForSingleObject(g_torrentStreamThread,INFINITE);
        CloseHandle(g_torrentStreamThread);
        g_torrentStreamThread=NULL;
    }
    if(g_torrentStreamRequest) {
        free(g_torrentStreamRequest);
        g_torrentStreamRequest=NULL;
    }
}
static void TorrentStopPlayback(void) {
    BOOL hadLocalStream=(g_torrentStreamThread!=NULL)||g_torrentPlaybackActive;
    TorrentDetachPlayerEvents();
    if(g_torrentStreamThread||g_torrentStreamRequest)TorrentCancelStreamPreparation();
    if(g_torrentPlaybackActive) {
        if(g_player)PlaybackStopEngine();
        g_torrentPlaybackActive=FALSE;g_hasMedia=FALSE;ResetArtwork();
    }
    if(hadLocalStream&&g_torrentStopHttpStream&&g_torrentInitialized)g_torrentStopHttpStream();
    if(InterlockedCompareExchange(&g_torrentPlaybackState,0,0)!=TORRENT_PLAYBACK_IDLE)
        TorrentSetPlaybackState(TORRENT_PLAYBACK_STOPPED,NULL);
}
/* Torrent playback shares the local LibVLC player, but owns a different
   failure/teardown path. Never let the generic local-file error handler clear
   g_hasMedia first: that would make the queued torrent error callback stale
   and leave the torrent HTTP stream running. */
static void TorrentHandlePlayerError(void) {
    if(!g_torrentPlaybackActive)return;
    TorrentDetachPlayerEvents();
    PlaybackStopEngine();
    g_torrentPlaybackActive=FALSE;
    g_hasMedia=FALSE;
    g_current=-1;
    g_pendingResume=FALSE;
    g_endHandled=TRUE;
    ResetArtwork();
    if(g_visualizerActive)ABM_AudioVisualizer_Stop();
    g_visualizerActive=FALSE;
    g_visualizerTried=FALSE;
    if(g_torrentStopHttpStream&&g_torrentInitialized)g_torrentStopHttpStream();
    TorrentSetPlaybackState(TORRENT_PLAYBACK_FAILED,
        L"Stream failed — LibVLC could not continue reading the torrent source.");
    if(g_main)InvalidateRect(g_main,NULL,FALSE);
}
static void TorrentRefreshTvDevices(void) {
    if(!g_torrentTvCombo||!IsWindow(g_torrentTvCombo))return;
    unsigned int count=ABM_TorrentDlna_DeviceCount();
    if((int)count==g_torrentTvKnownCount)return;
    SendMessageW(g_torrentTvCombo,CB_RESETCONTENT,0,0);
    for(unsigned int i=0;i<count;i++) {
        wchar_t name[256]=L"";
        if(ABM_TorrentDlna_DeviceName(i,name,(unsigned int)_countof(name)))
            SendMessageW(g_torrentTvCombo,CB_ADDSTRING,0,(LPARAM)name);
    }
    if(count)SendMessageW(g_torrentTvCombo,CB_SETCURSEL,0,0);
    g_torrentTvKnownCount=(int)count;
}
static DWORD WINAPI TorrentTvStreamWorker(void *arg) {
    TorrentTvStreamRequest *r=(TorrentTvStreamRequest*)arg;
    if(!r)return 1;
    if(InterlockedCompareExchange(&g_torrentTvStreamCancel,0,0)) {
        StringCchCopyW(r->error,_countof(r->error),L"TV stream preparation cancelled.");r->ok=FALSE;
    } else if(!g_torrentStartLanHttpStream(r->task_id,r->file_index,r->url,(unsigned)_countof(r->url),r->error,(unsigned)_countof(r->error))) {
        r->ok=FALSE;
    } else if(InterlockedCompareExchange(&g_torrentTvStreamCancel,0,0)) {
        if(g_torrentStopHttpStream)g_torrentStopHttpStream();
        StringCchCopyW(r->error,_countof(r->error),L"TV stream preparation cancelled.");r->ok=FALSE;
    } else {
        int64_t initial=r->file_size<8LL*1024*1024?r->file_size:8LL*1024*1024;
        if(initial<=0)initial=1;
        r->ok=g_torrentWaitForRange(r->task_id,r->file_index,0,initial,180000);
        if(!r->ok&&!r->error[0])StringCchCopyW(r->error,_countof(r->error),L"Timed out waiting for the initial verified TV playback buffer.");
    }
    if(!PostMessageW(r->owner,WM_TORRENT_TV_READY,0,(LPARAM)r)) {
        if(g_torrentStopHttpStream)g_torrentStopHttpStream();
        if(g_torrentTvStreamRequest==r)g_torrentTvStreamRequest=NULL;
        free(r);
    }
    return 0;
}
static void TorrentBeginCastToTV(void) {
    if(!g_torrentInitialized||!g_torrentTaskId||!g_torrentStartLanHttpStream)return;
    if(g_torrentTvStreamThread){TorrentSetLabel(g_torrentStatusLabel,L"TV stream is already preparing.");return;}
    int sel=(int)SendMessageW(g_torrentFileList,LB_GETCURSEL,0,0);
    if(sel==LB_ERR){TorrentSetLabel(g_torrentStatusLabel,L"Select a video file before casting.");return;}
    int fileIndex=(int)SendMessageW(g_torrentFileList,LB_GETITEMDATA,(WPARAM)sel,0);
    wchar_t relativePath[1024]=L"";int64_t size=0;
    if(!g_torrentGetFile(g_torrentTaskId,fileIndex,relativePath,(unsigned)_countof(relativePath),&size)||size<=0) {
        TorrentSetLabel(g_torrentStatusLabel,L"Could not read the selected torrent file.");return;
    }
    if(!IsVideoPath(relativePath)) {
        TorrentSetLabel(g_torrentStatusLabel,L"Select a video file to cast to the TV.");return;
    }
    int tv=(int)SendMessageW(g_torrentTvCombo,CB_GETCURSEL,0,0);
    if(tv==CB_ERR||!ABM_TorrentDlna_SelectDevice((unsigned int)tv)) {
        TorrentSetLabel(g_torrentStatusLabel,L"Find TVs and select an available DLNA receiver first.");return;
    }
    if(!g_torrentSelectFile(g_torrentTaskId,fileIndex)) {
        ABM_TorrentTaskInfo info={0};
        if(g_torrentGetTask(g_torrentTaskId,&info)&&info.error[0])TorrentSetLabel(g_torrentStatusLabel,info.error);
        else TorrentSetLabel(g_torrentStatusLabel,L"Could not prioritize the selected video for download.");
        return;
    }
    TorrentStopPlayback();
    if(g_torrentTvPlaybackActive){ABM_TorrentDlna_StopPlayback();g_torrentTvPlaybackActive=FALSE;}
    TorrentTvStreamRequest *req=(TorrentTvStreamRequest*)calloc(1,sizeof(*req));
    if(!req){TorrentSetLabel(g_torrentStatusLabel,L"Not enough memory to prepare TV casting.");return;}
    req->owner=g_main;req->task_id=g_torrentTaskId;req->file_index=fileIndex;req->file_size=size;
    InterlockedExchange(&g_torrentTvStreamCancel,0);g_torrentTvStreamRequest=req;
    g_torrentTvStreamThread=CreateThread(NULL,0,TorrentTvStreamWorker,req,0,NULL);
    if(!g_torrentTvStreamThread){g_torrentTvStreamRequest=NULL;free(req);TorrentSetLabel(g_torrentStatusLabel,L"Could not start the TV stream worker.");return;}
    TorrentSetLabel(g_torrentStatusLabel,L"Preparing a secure LAN stream and buffering verified torrent pieces for your TV…");
}
static void TorrentStopTvCasting(void) {
    BOOL hadTvStream=(g_torrentTvStreamThread!=NULL)||g_torrentTvPlaybackActive;
    if(g_torrentTvStreamThread) {
        InterlockedExchange(&g_torrentTvStreamCancel,1);
        if(g_torrentStopHttpStream)g_torrentStopHttpStream();
    }
    if(g_torrentTvPlaybackActive)ABM_TorrentDlna_StopPlayback();
    g_torrentTvPlaybackActive=FALSE;
    if(hadTvStream&&g_torrentStopHttpStream&&g_torrentInitialized)g_torrentStopHttpStream();
    TorrentSetLabel(g_torrentStatusLabel,L"TV playback stopped. Torrent downloading continues to the Windows download folder.");
}
static DWORD WINAPI TorrentStreamWorker(void *arg) {
    TorrentStreamRequest *r=(TorrentStreamRequest*)arg;
    if(!r)return 1;
    if(InterlockedCompareExchange(&g_torrentStreamCancel,0,0)) {
        StringCchCopyW(r->error,_countof(r->error),L"Stream start cancelled.");
        r->ok=FALSE;
    } else if(!g_torrentStartHttpStream(r->task_id,r->file_index,r->url,(unsigned)_countof(r->url),r->error,(unsigned)_countof(r->error))) {
        r->ok=FALSE;
    } else if(InterlockedCompareExchange(&g_torrentStreamCancel,0,0)) {
        if(g_torrentStopHttpStream)g_torrentStopHttpStream();
        StringCchCopyW(r->error,_countof(r->error),L"Stream start cancelled.");
        r->ok=FALSE;
    } else {
        int64_t initial=r->file_size<8LL*1024*1024?r->file_size:8LL*1024*1024;
        if(initial<=0)initial=1;
        r->ok=g_torrentWaitForRange(r->task_id,r->file_index,0,initial,180000);
        if(!r->ok&&!r->error[0])StringCchCopyW(r->error,_countof(r->error),L"Timed out waiting for the initial verified playback buffer.");
    }
    if(!r->ok && !InterlockedCompareExchange(&g_torrentStreamCancel,0,0))
        InterlockedExchange(&g_torrentPlaybackState,TORRENT_PLAYBACK_FAILED);
    /* The message carries only a generation token, never a heap pointer. The UI
       thread owns and frees the request after joining this worker. */
    PostMessageW(r->owner,WM_TORRENT_STREAM_READY,(WPARAM)r->generation,0);
    return 0;
}
static void TorrentBeginStream(void) {
    if(g_torrentTvPlaybackActive||g_torrentTvStreamThread)TorrentStopTvCasting();
    if(!g_torrentInitialized||!g_torrentTaskId||!g_torrentGetFile||!g_torrentStartHttpStream)return;
    if(g_torrentStreamThread){TorrentSetLabel(g_torrentStatusLabel,L"A stream is already preparing.");return;}
    int sel=(int)SendMessageW(g_torrentFileList,LB_GETCURSEL,0,0);
    if(sel==LB_ERR){TorrentSetLabel(g_torrentStatusLabel,L"Select a media file first.");return;}
    int fileIndex=(int)SendMessageW(g_torrentFileList,LB_GETITEMDATA,(WPARAM)sel,0);
    wchar_t path[1024]=L"";int64_t size=0;
    if(!g_torrentGetFile(g_torrentTaskId,fileIndex,path,(unsigned)_countof(path),&size)||size<=0) {
        TorrentSetLabel(g_torrentStatusLabel,L"Could not read the selected file metadata.");return;
    }
    if(!TorrentIsPlayableVideo(path)){
        TorrentSetLabel(g_torrentStatusLabel,L"That file is not a supported video. Select the .mp4/.mkv movie file, not subtitles or artwork.");return;
    }
    if(!g_torrentSelectFile(g_torrentTaskId,fileIndex)) {
        ABM_TorrentTaskInfo cacheError={0};
        if(g_torrentGetTask&&g_torrentGetTask(g_torrentTaskId,&cacheError)&&cacheError.error[0])
            TorrentSetLabel(g_torrentStatusLabel,cacheError.error);
        else
            TorrentSetLabel(g_torrentStatusLabel,L"Could not prioritize the selected file.");
        return;
    }
    g_torrentEverPlayed=FALSE;g_torrentLastMediaTime=-1;g_torrentLastProgressTick=0;g_torrentBufferingSinceTick=0;
    InterlockedExchange(&g_torrentBufferPercent,0);
    TorrentSetPlaybackState(TORRENT_PLAYBACK_BUFFERING_START,
        L"Buffering before playback starts — collecting the initial verified pieces…");
    TorrentStreamRequest *req=(TorrentStreamRequest*)calloc(1,sizeof(*req));
    if(!req){TorrentSetPlaybackState(TORRENT_PLAYBACK_FAILED,L"Stream failed — not enough memory to start streaming.");return;}
    req->owner=g_main;req->generation=InterlockedIncrement(&g_torrentStreamGeneration);
    req->task_id=g_torrentTaskId;req->file_index=fileIndex;req->file_size=size;
    InterlockedExchange(&g_torrentStreamCancel,0);g_torrentStreamRequest=req;
    g_torrentStreamThread=CreateThread(NULL,0,TorrentStreamWorker,req,0,NULL);
    if(!g_torrentStreamThread){g_torrentStreamRequest=NULL;free(req);TorrentSetPlaybackState(TORRENT_PLAYBACK_FAILED,L"Stream failed — could not start the buffering worker.");}
}
static void TorrentAddSource(HWND hwnd,BOOL fromFile) {
    if(!g_torrentInitialized){TorrentSetLabel(g_torrentStatusLabel,L"Torrent engine is not initialized.");return;}
    wchar_t source[8192]=L"",error[512]=L"";
    uint64_t id=0;BOOL ok=FALSE;
    if(fromFile) {
        OPENFILENAMEW ofn={0};wchar_t path[PATH_CAP]=L"";
        ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=hwnd;ofn.lpstrFilter=L"Torrent files (*.torrent)\0*.torrent\0All files (*.*)\0*.*\0";
        ofn.lpstrFile=path;ofn.nMaxFile=(DWORD)_countof(path);ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER;
        if(!GetOpenFileNameW(&ofn))return;
        ok=g_torrentAddFile(path,&id,error,(unsigned)_countof(error));
    } else {
        GetWindowTextW(g_torrentMagnetEdit,source,(int)_countof(source));
        if(!source[0]){TorrentSetLabel(g_torrentStatusLabel,L"Paste a magnet URI first.");return;}
        ok=g_torrentAddMagnet(source,&id,error,(unsigned)_countof(error));
    }
    if(!ok){TorrentSetLabel(g_torrentStatusLabel,error[0]?error:L"Could not add this torrent source.");return;}
    if(g_torrentTaskId&&g_torrentTaskId!=id) {
        TorrentStopTvCasting();TorrentStopPlayback();
        g_torrentRemove(g_torrentTaskId,FALSE);
    }
    g_torrentTaskId=id;g_torrentListedForTask=-1;g_torrentListedFileCount=0;g_torrentSelectedFile=-1;
    InterlockedExchange(&g_torrentPlaybackState,TORRENT_PLAYBACK_IDLE);
    InterlockedExchange(&g_torrentBufferPercent,0);
    g_torrentEverPlayed=FALSE;g_torrentLastMediaTime=-1;g_torrentLastProgressTick=0;g_torrentBufferingSinceTick=0;
    TorrentSetBufferProgress(0);
    SendMessageW(g_torrentFileList,LB_RESETCONTENT,0,0);
    TorrentSetLabel(g_torrentStatusLabel,L"Torrent added. Waiting for metadata…");
    TorrentRefreshStatus();
}


typedef struct TorrentSliderState { int kind; int value; BOOL dragging; } TorrentSliderState;
static HWND g_torrentSliders[3]={0};
static int g_torrentSliderValues[3]={0,0,20};
static const wchar_t *g_torrentSliderClass=L"ABMTorrentPremiumSlider";
static int TorrentSliderToValue(int kind,int pos){
    if(kind==2){if(pos<=0)return 1;return 1+(int)((long long)pos*pos*4095LL/(1000LL*1000LL));}
    if(pos<=0)return 0;
    return 64+(int)((long long)(pos-1)*(pos-1)*2097087LL/(999LL*999LL));
}
static int TorrentSliderToPos(int kind,int value){
    if(kind==2){int lo=0,hi=1000;while(lo<hi){int mid=(lo+hi)/2;if(TorrentSliderToValue(kind,mid)<value)lo=mid+1;else hi=mid;}return lo;}
    if(value<=0)return 0;
    int lo=1,hi=1000;
    while(lo<hi){int mid=(lo+hi)/2;if(TorrentSliderToValue(kind,mid)<value)lo=mid+1;else hi=mid;}
    return lo;
}
static void TorrentSliderSetPoint(HWND hwnd,TorrentSliderState *s,int x){
    RECT r;GetClientRect(hwnd,&r);int left=14,right=r.right-104;if(right<=left)return;
    int pos=(x-left)*1000/(right-left);if(pos<0)pos=0;if(pos>1000)pos=1000;
    s->value=TorrentSliderToValue(s->kind,pos);g_torrentSliderValues[s->kind]=s->value;
    InvalidateRect(hwnd,NULL,FALSE);
}
static void TorrentSliderPaint(HWND hwnd,HDC dc,TorrentSliderState *s){
    RECT r;GetClientRect(hwnd,&r);
    if(!s||!g_torrentButtonCanvasReady||
       !ABM_D2D_CanvasBegin(&g_torrentButtonCanvas,dc,&r))return;
    int left=14,right=r.right-104,cy=(r.top+r.bottom)/2;
    if(right<=left+18){ABM_D2D_CanvasEnd(&g_torrentButtonCanvas);return;}
    int pos=TorrentSliderToPos(s->kind,s->value),thumb=left+(right-left)*pos/1000;
    /* Canonical Direct2D/DirectWrite canvas, shared palette and focus style. */
    ABM_D2D_CanvasFill(&g_torrentButtonCanvas,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,ABM_UI_COLOR_SURFACE_BLUE);
    ABM_D2D_CanvasFillRound(&g_torrentButtonCanvas,(float)left,(float)(cy-4),(float)right,(float)(cy+4),4.0f,ABM_UI_COLOR_BORDER);
    if(thumb>left)ABM_D2D_CanvasFillRound(&g_torrentButtonCanvas,(float)left,(float)(cy-4),(float)thumb,(float)(cy+4),4.0f,ABM_UI_COLOR_ACCENT);
    ABM_D2D_CanvasFillRound(&g_torrentButtonCanvas,(float)(thumb-12),(float)(cy-12),(float)(thumb+12),(float)(cy+12),12.0f,ABM_UI_COLOR_ACCENT_SOFT);
    ABM_D2D_CanvasFillRound(&g_torrentButtonCanvas,(float)(thumb-7),(float)(cy-7),(float)(thumb+7),(float)(cy+7),7.0f,ABM_UI_COLOR_ACCENT_BRIGHT);
    ABM_D2D_CanvasStrokeRound(&g_torrentButtonCanvas,(float)(thumb-7)+0.5f,(float)(cy-7)+0.5f,(float)(thumb+7)-0.5f,(float)(cy+7)-0.5f,7.0f,ABM_UI_COLOR_TEXT,1.0f);
    RECT pill={r.right-94,cy-16,r.right-2,cy+16};
    ABM_D2D_CanvasFillRound(&g_torrentButtonCanvas,(float)pill.left,(float)pill.top,(float)pill.right,(float)pill.bottom,ABM_UI_RADIUS_BUTTON,ABM_UI_COLOR_INPUT);
    ABM_D2D_CanvasStrokeRound(&g_torrentButtonCanvas,(float)pill.left+0.5f,(float)pill.top+0.5f,(float)pill.right-0.5f,(float)pill.bottom-0.5f,ABM_UI_RADIUS_BUTTON,ABM_UI_COLOR_BORDER_STRONG,1.0f);
    wchar_t valueText[48];
    if(s->kind<2){if(s->value==0)StringCchCopyW(valueText,_countof(valueText),L"Unlimited");
        else if(s->value>=1024)StringCchPrintfW(valueText,_countof(valueText),L"%.1f MiB/s",(double)s->value/1024.0);
        else StringCchPrintfW(valueText,_countof(valueText),L"%d KiB/s",s->value);}
    else StringCchPrintfW(valueText,_countof(valueText),L"%d GiB",s->value);
    RECT textRect={pill.left+3,pill.top+1,pill.right-3,pill.bottom-1};
    ABM_D2D_CanvasText(&g_torrentButtonCanvas,valueText,textRect,ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_SMALL,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if(GetFocus()==hwnd)ABM_D2D_CanvasStrokeRound(&g_torrentButtonCanvas,(float)r.left+0.5f,(float)r.top+0.5f,(float)r.right-0.5f,(float)r.bottom-0.5f,ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_FOCUS,1.5f);
    ABM_D2D_CanvasEnd(&g_torrentButtonCanvas);
}
static LRESULT CALLBACK TorrentSliderProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    TorrentSliderState *s=(TorrentSliderState*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(msg==WM_NCCREATE){CREATESTRUCTW *cs=(CREATESTRUCTW*)lp;
        s=(TorrentSliderState*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*s));if(!s)return FALSE;
        s->kind=(int)(INT_PTR)cs->lpCreateParams;s->value=g_torrentSliderValues[s->kind];
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)s);}
    if(!s)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg){
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);TorrentSliderPaint(hwnd,dc,s);EndPaint(hwnd,&ps);return 0;}
    case WM_LBUTTONDOWN:s->dragging=TRUE;SetCapture(hwnd);TorrentSliderSetPoint(hwnd,s,GET_X_LPARAM(lp));return 0;
    case WM_MOUSEMOVE:if(s->dragging&&(wp&MK_LBUTTON))TorrentSliderSetPoint(hwnd,s,GET_X_LPARAM(lp));return 0;
    case WM_LBUTTONUP:if(s->dragging){s->dragging=FALSE;ReleaseCapture();TorrentSliderSetPoint(hwnd,s,GET_X_LPARAM(lp));}return 0;
    case WM_CAPTURECHANGED:s->dragging=FALSE;return 0;
    case WM_SETFOCUS:case WM_KILLFOCUS:case WM_ENABLE:
        InvalidateRect(hwnd,NULL,FALSE);return 0;
    case WM_GETDLGCODE:return DLGC_WANTARROWS;
    case WM_KEYDOWN:if(wp==VK_LEFT||wp==VK_DOWN||wp==VK_RIGHT||wp==VK_UP){
        int p=TorrentSliderToPos(s->kind,s->value)+(wp==VK_LEFT||wp==VK_DOWN?-1:1);
        if(p<0)p=0;if(p>1000)p=1000;s->value=TorrentSliderToValue(s->kind,p);
        g_torrentSliderValues[s->kind]=s->value;InvalidateRect(hwnd,NULL,FALSE);return 0;}break;
    case WM_NCDESTROY:HeapFree(GetProcessHeap(),0,s);SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);break;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static BOOL TorrentRegisterSliderClass(HINSTANCE instance){
    WNDCLASSW wc={0};wc.lpfnWndProc=TorrentSliderProc;wc.hInstance=instance;
    wc.lpszClassName=g_torrentSliderClass;wc.hCursor=LoadCursorW(NULL,IDC_HAND);
    if(RegisterClassW(&wc)||GetLastError()==ERROR_CLASS_ALREADY_EXISTS)return TRUE;return FALSE;
}

static void TorrentLayout(HWND hwnd) {
    RECT rc={0};GetClientRect(hwnd,&rc);
    const int w=rc.right,h=rc.bottom,m=22,inner=w-2*m;
    /* Ignore minimized/intermediate zero-sized WM_SIZE notifications. The
       next restored/maximized size will perform a complete layout pass. */
    if(w<700||h<600)return;
    /* Larger 40px action targets match the Streaming Centre's receiver controls.
       The taller window keeps the content library spacious while preserving
       dedicated rows for playback, network, cache and TV casting. */
    const int headerH=94,sourceTop=headerH+14,sourceH=66;
    const int contentTop=sourceTop+sourceH+12;
    int contentBottom=h-410;
    if(contentBottom<contentTop+100)contentBottom=contentTop+100;
    const int playbackY=contentBottom+12,networkY=playbackY+112;
    const int cacheY=networkY+84,tvY=cacheY+84;
    MoveWindow(g_torrentMagnetEdit,m+14,sourceTop+27,inner-272,36,FALSE);
    MoveWindow(GetDlgItem(hwnd,ID_TORRENT_ADD_MAGNET),w-m-250,sourceTop+23,104,42,FALSE);
    MoveWindow(GetDlgItem(hwnd,ID_TORRENT_BROWSE),w-m-134,sourceTop+23,122,42,FALSE);
    /* Keep the live task summary below the content heading. */
    MoveWindow(g_torrentInfoLabel,m+14,contentTop+33,inner-28,20,FALSE);
    MoveWindow(g_torrentFileList,m+12,contentTop+58,inner-24,max(40,contentBottom-contentTop-66),FALSE);
    /* Playback title, status and progress each own a distinct baseline. */
    MoveWindow(g_torrentStatusLabel,m+14,playbackY+23,inner-28,20,FALSE);
    MoveWindow(g_torrentBufferBar,m+14,playbackY+47,inner-28,5,FALSE);
    /* Playback actions use a compact fixed width, like Streaming Centre
       controls; do not stretch them to consume the entire window. */
    const int actionWidths[5]={184,156,128,128,110},actionH=42,actionGap=10;
    int actionTotal=actionGap*4;
    for(int i=0;i<5;i++)actionTotal+=actionWidths[i];
    int x=m+(inner-actionTotal)/2;
    for(int i=0;i<5;i++){MoveWindow(g_torrentActionButtons[i],x,playbackY+54,actionWidths[i],actionH,FALSE);x+=actionWidths[i]+actionGap;}
    /* Two balanced bandwidth columns; reserve a dedicated action lane at right. */
    const int leftX=m+14,applyX=w-m-136,gap=18;
    const int sliderW=max(170,(applyX-leftX-gap)/2);
    const int secondX=leftX+sliderW+gap;
    MoveWindow(g_torrentDownLabel,leftX,networkY+3,sliderW,18,FALSE);
    MoveWindow(g_torrentUpLabel,secondX,networkY+3,sliderW,18,FALSE);
    ShowWindow(g_torrentDownLimitEdit,SW_HIDE);ShowWindow(g_torrentUpLimitEdit,SW_HIDE);
    for(int si=0;si<6;si++)if(g_torrentNumericButtons[si])ShowWindow(g_torrentNumericButtons[si],SW_HIDE);
    MoveWindow(g_torrentSliders[0],leftX,networkY+25,sliderW,46,FALSE);
    MoveWindow(g_torrentSliders[1],secondX,networkY+25,sliderW,46,FALSE);
    MoveWindow(GetDlgItem(hwnd,ID_TORRENT_APPLY_LIMITS),applyX,networkY+25,124,42,FALSE);
    MoveWindow(g_torrentCacheLabel,m+14,cacheY+3,210,18,FALSE);
    MoveWindow(g_torrentCacheInfoLabel,m+232,cacheY+3,max(180,inner-246),18,FALSE);
    ShowWindow(g_torrentCacheLimitEdit,SW_HIDE);
    for(int si=4;si<6;si++)if(g_torrentNumericButtons[si])ShowWindow(g_torrentNumericButtons[si],SW_HIDE);
    MoveWindow(g_torrentSliders[2],m+14,cacheY+25,260,46,FALSE);
    MoveWindow(GetDlgItem(hwnd,ID_TORRENT_CACHE_APPLY),m+290,cacheY+25,112,42,FALSE);
    MoveWindow(GetDlgItem(hwnd,ID_TORRENT_CLEAR_CACHE),m+418,cacheY+25,168,42,FALSE);
    MoveWindow(g_torrentOpenDownloadsButton,m+600,cacheY+25,190,42,FALSE);
    MoveWindow(g_torrentTvButtons[0],m+14,tvY+26,132,42,FALSE);
    MoveWindow(g_torrentTvCombo,m+158,tvY+26,max(120,inner-442),42,FALSE);
    MoveWindow(g_torrentTvButtons[1],w-m-270,tvY+26,130,42,FALSE);
    MoveWindow(g_torrentTvButtons[2],w-m-128,tvY+26,120,42,FALSE);
    /* Keep interactive buttons above overlapping static labels after relayout. */
    const int buttonIds[]={ID_TORRENT_APPLY_LIMITS,ID_TORRENT_CACHE_APPLY,
        ID_TORRENT_CLEAR_CACHE,ID_TORRENT_FIND_TVS,ID_TORRENT_CAST_TV,
        ID_TORRENT_STOP_TV,ID_TORRENT_OPEN_DOWNLOADS,ID_TORRENT_PAUSE,ID_TORRENT_STREAM,
        ID_TORRENT_STOP,ID_TORRENT_REMOVE,ID_TORRENT_REFRESH};
    for(int i=0;i<(int)_countof(buttonIds);++i){
        HWND b=GetDlgItem(hwnd,buttonIds[i]);
        if(b)SetWindowPos(b,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOREDRAW|SWP_SHOWWINDOW);
    }
    /* Batch all child geometry changes, then repaint once to prevent ghosted
       controls and stale slider/button fragments during maximize/restore. */
    RedrawWindow(hwnd,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE);
}
/* Preserve the existing RECT truncation semantics at the Win32 pixel boundary. */
static RECT TorrentMakeRect(float left,float top,float right,float bottom){
    RECT rect={(LONG)left,(LONG)top,(LONG)right,(LONG)bottom};
    return rect;
}
static void TorrentRoundRect(ABM_D2D *ui,RECT rect,float radius,COLORREF color){
    ABM_D2D_Round(ui,(float)rect.left,(float)rect.top,(float)rect.right,(float)rect.bottom,radius,color);
}
static void TorrentStrokeRoundRect(ABM_D2D *ui,RECT rect,float radius,COLORREF color,float width){
    ABM_D2D_StrokeRound(ui,(float)rect.left,(float)rect.top,(float)rect.right,(float)rect.bottom,radius,color,width);
}
static void TorrentGradientRoundRect(ABM_D2D *ui,RECT rect,float radius,COLORREF start,COLORREF end){
    ABM_D2D_GradientRound(ui,(float)rect.left,(float)rect.top,(float)rect.right,(float)rect.bottom,radius,start,end);
}
static void TorrentDrawStoredText(HWND control,RECT rect,COLORREF color,int style,UINT flags){
    if(!control||!IsWindow(control))return;
    wchar_t text[2048]=L"";
    GetWindowTextW(control,text,(int)_countof(text));
    if(text[0])ABM_D2D_Text(&g_torrentUi,text,rect,color,style,flags);
}
static BOOL TorrentPaint(HWND hwnd,const RECT *paintRect){
    RECT rc={0};GetClientRect(hwnd,&rc);
    const float w=(float)rc.right,h=(float)rc.bottom;
    const float m=22.0f,headerH=94.0f,sourceTop=108.0f,sourceH=66.0f;
    const float contentTop=sourceTop+sourceH+12.0f;
    float contentBottom=h-410.0f;
    if(contentBottom<contentTop+100.0f)contentBottom=contentTop+100.0f;
    float playbackY=contentBottom+12.0f,networkY=playbackY+112.0f;
    float cacheY=networkY+84.0f,tvY=cacheY+84.0f;
    const COLORREF bg=ABM_UI_COLOR_BACKGROUND,edge=ABM_UI_COLOR_BORDER_STRONG;
    const COLORREF muted=ABM_UI_COLOR_TEXT_MUTED,accent=ABM_UI_COLOR_ACCENT;
    if(!paintRect||!ABM_D2D_BeginClip(&g_torrentUi,bg,paintRect))return FALSE;

    /* Cinematic branded masthead */
    ABM_D2D_GradientRound(&g_torrentUi,0,0,w,headerH+18,0,ABM_THEME_COLOR_0D1B2D,ABM_THEME_COLOR_050A13);
    ABM_D2D_Fill(&g_torrentUi,0,0,w,2,ABM_THEME_COLOR_226F7A);
    /* Match the main player's signature ABM identity: luminous A/play
       monogram, split ABM + AV PLAYER wordmark, gradient rule and tagline. */
    ABM_D2D_Round(&g_torrentUi,20,7,102,87,22,ABM_THEME_COLOR_0D1C2D);
    ABM_D2D_StrokeRound(&g_torrentUi,20,7,102,87,22,ABM_THEME_COLOR_3F97B4,1.0f);
    ABM_D2D_GradientRound(&g_torrentUi,28,14,94,80,17,ABM_THEME_COLOR_1C9D9D,ABM_THEME_COLOR_34777E);
    ABM_D2D_StrokeRound(&g_torrentUi,33,19,89,75,14,ABM_THEME_COLOR_BEE5FF,0.8f);
    ABM_D2D_Line(&g_torrentUi,42,63,59,30,ABM_THEME_COLOR_FFFFFF,3.0f);
    ABM_D2D_Line(&g_torrentUi,59,30,76,63,ABM_THEME_COLOR_FFFFFF,3.0f);
    ABM_D2D_Line(&g_torrentUi,49,49,68,49,ABM_THEME_COLOR_E2F5FF,2.0f);
    ABM_D2D_Line(&g_torrentUi,62,40,75,48,ABM_THEME_COLOR_FFFFFF,2.0f);
    ABM_D2D_Line(&g_torrentUi,75,48,62,56,ABM_THEME_COLOR_FFFFFF,2.0f);
    ABM_D2D_Line(&g_torrentUi,62,56,62,40,ABM_THEME_COLOR_FFFFFF,2.0f);

    RECT r=TorrentMakeRect(116,8,420,49);ABM_D2D_Text(&g_torrentUi,L"ABM",r,ABM_THEME_COLOR_F7FAFF,ABM_D2D_TEXT_BRAND_MAIN,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    /* Match the Main Player's exact brand suffix role and baseline. */
    r=TorrentMakeRect(188,20,390,48);ABM_D2D_Text(&g_torrentUi,L"AV  PLAYER",r,ABM_THEME_COLOR_84CDC2,ABM_D2D_TEXT_BRAND_SUFFIX,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    ABM_D2D_GradientRound(&g_torrentUi,116,52,354,54,0,ABM_THEME_COLOR_36B1A8,ABM_THEME_COLOR_70BE9D);
    r=TorrentMakeRect(116,60,510,83);ABM_D2D_Text(&g_torrentUi,L"TORRENT STREAM  ·  PEER-TO-PEER MEDIA",r,ABM_THEME_COLOR_9DB1C9,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    /* Live task state is presented as a compact status chip. This is a
       read-only view of the existing engine task; it does not alter its state. */
    const wchar_t *engineState=L"ENGINE READY";
    COLORREF engineDot=ABM_UI_COLOR_SUCCESS;
    ABM_TorrentTaskInfo taskInfo={0};
    if(g_torrentTaskId&&g_torrentGetTask&&g_torrentGetTask(g_torrentTaskId,&taskInfo)){
        switch(taskInfo.state){
        case 0:engineState=L"QUEUED";engineDot=ABM_UI_COLOR_WARNING;break;
        case 1:engineState=L"CHECKING";engineDot=ABM_UI_COLOR_WARNING;break;
        case 2:engineState=L"DOWNLOADING";engineDot=ABM_UI_COLOR_ACCENT_BRIGHT;break;
        case 3:engineState=L"COMPLETE";engineDot=ABM_UI_COLOR_SUCCESS;break;
        case 4:engineState=L"SEEDING";engineDot=ABM_UI_COLOR_SUCCESS;break;
        case 5:engineState=L"PAUSED";engineDot=ABM_UI_COLOR_WARNING;break;
        case 6:engineState=L"ERROR";engineDot=ABM_UI_COLOR_DANGER;break;
        default:engineState=L"CONNECTED";break;
        }
    }
    r=TorrentMakeRect(w-244,23,w-22,57);TorrentRoundRect(&g_torrentUi,r,16,ABM_UI_COLOR_SURFACE);
    TorrentStrokeRoundRect(&g_torrentUi,r,16,ABM_UI_COLOR_BORDER_STRONG,0.9f);
    ABM_D2D_Round(&g_torrentUi,w-228,36,w-220,44,4,engineDot);
    r=TorrentMakeRect(w-211,25,w-34,55);ABM_D2D_Text(&g_torrentUi,engineState,r,ABM_THEME_COLOR_B4CDDB,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    /* Source card: label above the edit row; no painted text beneath controls. */
    r=TorrentMakeRect(m,sourceTop,w-m,sourceTop+sourceH);
    TorrentGradientRoundRect(&g_torrentUi,r,14,ABM_UI_COLOR_SURFACE_BLUE,ABM_UI_COLOR_SURFACE);
    TorrentStrokeRoundRect(&g_torrentUi,r,14,edge,1.0f);
    r=TorrentMakeRect(m+14,sourceTop+3,m+250,sourceTop+22);ABM_D2D_Text(&g_torrentUi,L"ADD A SOURCE",r,accent,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    /* Content library card */
    r=TorrentMakeRect(m,contentTop,w-m,contentBottom);
    TorrentGradientRoundRect(&g_torrentUi,r,16,ABM_UI_COLOR_SURFACE_BLUE,ABM_UI_COLOR_BACKGROUND);
    TorrentStrokeRoundRect(&g_torrentUi,r,16,ABM_UI_COLOR_BORDER,1.0f);
    r=TorrentMakeRect(m+14,contentTop+5,m+260,contentTop+25);ABM_D2D_Text(&g_torrentUi,L"TORRENT CONTENT",r,accent,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    /* Give the instruction the full header row after the section title.
       A width-relative left edge could make the right-aligned sentence clip
       at narrower window sizes or higher DPI settings. */
    r=TorrentMakeRect(m+270,contentTop+5,w-m-14,contentTop+25);
    ABM_D2D_Text(&g_torrentUi,L"SELECT A MEDIA FILE TO DOWNLOAD, PLAY OR CAST",r,muted,ABM_D2D_TEXT_BODY,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    ABM_D2D_Fill(&g_torrentUi,m+14,contentTop+29,m+62,contentTop+31,accent);
    r=TorrentMakeRect(m+14,contentTop+33,w-m-14,contentTop+53);
    TorrentDrawStoredText(g_torrentInfoLabel,r,ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

    /* Playback deck: status, progress and actions each have their own row. */
    r=TorrentMakeRect(m,playbackY,w-m,playbackY+100);
    TorrentGradientRoundRect(&g_torrentUi,r,14,ABM_UI_COLOR_SURFACE_BLUE,ABM_UI_COLOR_SURFACE);
    TorrentStrokeRoundRect(&g_torrentUi,r,14,edge,1.0f);
    r=TorrentMakeRect(m+14,playbackY+2,m+250,playbackY+21);ABM_D2D_Text(&g_torrentUi,L"PLAYBACK & BUFFER",r,accent,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    int progress=(int)InterlockedCompareExchange(&g_torrentBufferPercent,0,0);
    if(InterlockedCompareExchange(&g_torrentPlaybackState,0,0)==TORRENT_PLAYBACK_PLAYING)progress=100;
    float barL=m+14,barT=playbackY+47,barR=w-m-14;
    ABM_D2D_Round(&g_torrentUi,barL,barT,barR,barT+5,3,ABM_THEME_COLOR_193041);
    ABM_D2D_Fill(&g_torrentUi,barL+2,barT+1,barR-2,barT+2,ABM_THEME_COLOR_314B5B);
    if(progress>0)ABM_D2D_GradientRound(&g_torrentUi,barL,barT,barL+(barR-barL)*progress/100.0f,barT+5,3,ABM_UI_COLOR_ACCENT,ABM_UI_COLOR_ACCENT_BRIGHT);
    r=TorrentMakeRect(m+14,playbackY+23,w-m-14,playbackY+43);
    TorrentDrawStoredText(g_torrentStatusLabel,r,ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

    /* Matched utility cards */
    r=TorrentMakeRect(m,networkY,w-m,networkY+76);
    TorrentGradientRoundRect(&g_torrentUi,r,13,ABM_UI_COLOR_SURFACE_BLUE,ABM_UI_COLOR_BACKGROUND);
    TorrentStrokeRoundRect(&g_torrentUi,r,13,edge,0.9f);
    r=TorrentMakeRect(m,cacheY,w-m,cacheY+76);
    TorrentGradientRoundRect(&g_torrentUi,r,13,ABM_UI_COLOR_SURFACE_BLUE,ABM_UI_COLOR_BACKGROUND);
    TorrentStrokeRoundRect(&g_torrentUi,r,13,edge,0.9f);

    /* Receiver dock */
    r=TorrentMakeRect(m,tvY,w-m,h-12);
    TorrentGradientRoundRect(&g_torrentUi,r,13,ABM_UI_COLOR_SURFACE_BLUE,ABM_UI_COLOR_BACKGROUND);
    TorrentStrokeRoundRect(&g_torrentUi,r,13,edge,0.9f);
    r=TorrentMakeRect(m+14,tvY+2,m+220,tvY+22);ABM_D2D_Text(&g_torrentUi,L"TV CASTING",r,accent,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    /* Static child labels retain their text as state only. Render every
       application-owned label with the same DirectWrite formats as Main. */
    r=TorrentMakeRect(m+14,networkY+3,m+14+((w-2*m-14-18-136)/2),networkY+21);
    TorrentDrawStoredText(g_torrentDownLabel,r,muted,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    r=TorrentMakeRect(m+14+((w-2*m-14-18-136)/2)+18,networkY+3,w-m-136,networkY+21);
    TorrentDrawStoredText(g_torrentUpLabel,r,muted,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    r=TorrentMakeRect(m+14,cacheY+3,m+224,cacheY+21);
    TorrentDrawStoredText(g_torrentCacheLabel,r,accent,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    r=TorrentMakeRect(m+232,cacheY+3,w-m-14,cacheY+21);
    TorrentDrawStoredText(g_torrentCacheInfoLabel,r,muted,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    return ABM_D2D_End(&g_torrentUi);
}
/* Button renderer copied from Streaming Centre's DrawDeviceButton palette,
   radius, border, pressed/focus treatment and typography. */
static COLORREF TorrentBlend(COLORREF a,COLORREF b,int t,int maxT){
    return RGB((GetRValue(a)*(maxT-t)+GetRValue(b)*t)/maxT,
               (GetGValue(a)*(maxT-t)+GetGValue(b)*t)/maxT,
               (GetBValue(a)*(maxT-t)+GetBValue(b)*t)/maxT);
}
static wchar_t TorrentButtonGlyph(int id){
    switch(id){
    case ID_TORRENT_PAUSE:return 0xE769;
    case ID_TORRENT_STREAM:return 0xE768;
    case ID_TORRENT_STOP:return 0xE71A;
    case ID_TORRENT_REMOVE:case ID_TORRENT_CLEAR_CACHE:return 0xE74D;
    case ID_TORRENT_REFRESH:return 0xE72C;
    case ID_TORRENT_FIND_TVS:return 0xE721;
    case ID_TORRENT_CAST_TV:case ID_TORRENT_STOP_TV:return 0xE7F4;
    case ID_TORRENT_OPEN_DOWNLOADS:case ID_TORRENT_BROWSE:return 0xE8B7;
    case ID_TORRENT_APPLY_LIMITS:case ID_TORRENT_CACHE_APPLY:return 0xE73E;
    case ID_TORRENT_DOWN_DEC:case ID_TORRENT_UP_DEC:case ID_TORRENT_CACHE_DEC:return 0xE738;
    case ID_TORRENT_DOWN_INC:case ID_TORRENT_UP_INC:case ID_TORRENT_CACHE_INC:return 0xE710;
    case ID_TORRENT_ADD_MAGNET:return 0xE8D7;
    default:return 0xE710;
    }
}
static void TorrentDrawButton(const DRAWITEMSTRUCT *item){
    if(!item||!item->hwndItem)return;
    RECT r=item->rcItem;
    BOOL pressed=(item->itemState&ODS_SELECTED)!=0;
    BOOL hot=(item->itemState&ODS_HOTLIGHT)!=0;
    BOOL focused=(item->itemState&ODS_FOCUS)!=0;
    BOOL disabled=(item->itemState&ODS_DISABLED)!=0;
    int id=GetDlgCtrlID(item->hwndItem);
    BOOL primary=(id==ID_TORRENT_STREAM||id==ID_TORRENT_CAST_TV||id==ID_TORRENT_ADD_MAGNET);
    BOOL destructive=(id==ID_TORRENT_REMOVE||id==ID_TORRENT_CLEAR_CACHE);
    COLORREF fill,iconColor,textColor,borderColor;
    if(disabled){
        fill=ABM_UI_COLOR_SURFACE_RAISED;iconColor=ABM_UI_COLOR_TEXT_DISABLED;
        textColor=ABM_UI_COLOR_TEXT_DISABLED;borderColor=ABM_UI_COLOR_BORDER;
        hot=FALSE;pressed=FALSE;
    }else if(primary){
        fill=pressed?ABM_UI_COLOR_ACCENT:ABM_THEME_COLOR_18706F;
        iconColor=ABM_UI_COLOR_TEXT;textColor=ABM_UI_COLOR_TEXT;borderColor=ABM_UI_COLOR_ACCENT_BRIGHT;
    }else if(destructive){
        fill=pressed?ABM_THEME_COLOR_80393F:ABM_THEME_COLOR_41252F;
        iconColor=ABM_THEME_COLOR_FFAEA9;textColor=ABM_THEME_COLOR_FFE7E5;borderColor=ABM_UI_COLOR_DANGER;
    }else{
        fill=pressed?ABM_THEME_COLOR_305E6F:ABM_UI_COLOR_SURFACE_RAISED;
        iconColor=ABM_UI_COLOR_ACCENT_BRIGHT;textColor=ABM_UI_COLOR_TEXT_SECONDARY;borderColor=ABM_UI_COLOR_BORDER_STRONG;
    }
    if(hot&&!pressed&&!destructive&&!disabled)fill=TorrentBlend(fill,ABM_UI_COLOR_ACCENT_SOFT,18,100);
    BOOL numeric=(id==ID_TORRENT_DOWN_DEC||id==ID_TORRENT_UP_DEC||id==ID_TORRENT_CACHE_DEC||id==ID_TORRENT_DOWN_INC||id==ID_TORRENT_UP_INC||id==ID_TORRENT_CACHE_INC);
    wchar_t caption[128]=L"";if(!numeric)GetWindowTextW(item->hwndItem,caption,(int)_countof(caption));
    if(g_torrentButtonCanvasReady&&ABM_D2D_CanvasBegin(&g_torrentButtonCanvas,item->hDC,&r)){
        ABM_D2D_CanvasFill(&g_torrentButtonCanvas,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,ABM_THEME_COLOR_0A1725);
        RECT face=r;InflateRect(&face,-1,-1);
        ABM_D2D_CanvasFillRound(&g_torrentButtonCanvas,(float)face.left,(float)face.top,(float)face.right,(float)face.bottom,(float)ABM_UI_RADIUS_BUTTON,fill);
        ABM_D2D_CanvasStrokeRound(&g_torrentButtonCanvas,(float)r.left+0.5f,(float)r.top+0.5f,(float)r.right-0.5f,(float)r.bottom-0.5f,(float)ABM_UI_RADIUS_BUTTON,focused?ABM_THEME_COLOR_ABF6E2:borderColor,focused?2.0f:1.0f);
        wchar_t glyph[2]={TorrentButtonGlyph(id),0};RECT iconRect;
        if(numeric){iconRect.left=r.left+1;iconRect.top=r.top+1;iconRect.right=r.right-1;iconRect.bottom=r.bottom-1;}
        else{iconRect.left=r.left+2;iconRect.top=r.top+2;iconRect.right=r.left+20;iconRect.bottom=r.bottom-2;}
        ABM_D2D_CanvasText(&g_torrentButtonCanvas,glyph,iconRect,numeric?ABM_THEME_COLOR_8EE2D3:iconColor,ABM_D2D_TEXT_ICON,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(!numeric){RECT textRect={r.left+21,r.top+1,r.right-3,r.bottom-1};ABM_D2D_CanvasText(&g_torrentButtonCanvas,caption,textRect,textColor,ABM_D2D_TEXT_SMALL,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);}
        ABM_D2D_CanvasEnd(&g_torrentButtonCanvas);return;
    }
    HDC dc=item->hDC;HBRUSH outer=CreateSolidBrush(ABM_THEME_COLOR_0A1725);FillRect(dc,&r,outer);DeleteObject(outer);
    RECT face=r;InflateRect(&face,-1,-1);
    HBRUSH faceBrush=CreateSolidBrush(fill);HPEN edge=CreatePen(PS_SOLID,focused?2:1,focused?ABM_THEME_COLOR_ABF6E2:borderColor);
    HGDIOBJ oldBrush=SelectObject(dc,faceBrush),oldPen=SelectObject(dc,edge);
    RoundRect(dc,face.left,face.top,face.right,face.bottom,ABM_UI_RADIUS_BUTTON*2,ABM_UI_RADIUS_BUTTON*2);
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(faceBrush);DeleteObject(edge);
    if(!numeric){
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,textColor);
        RECT textRect={r.left+21,r.top+1,r.right-3,r.bottom-1};
        HGDIOBJ oldFont=SelectObject(dc,g_smallFont?g_smallFont:GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(dc,caption,-1,&textRect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        SelectObject(dc,oldFont);
    }
}
static HBRUSH g_torrentSurfaceBrush=NULL;
static HBRUSH g_torrentControlBrush=NULL;
static HFONT g_torrentUiFont=NULL;
static void TorrentApplyDarkControl(HWND control) { if(control)SetWindowTheme(control,L"DarkMode_Explorer",NULL); }

/* Clip legacy child HWNDs to rounded geometry and paint their chrome ourselves.
   Native EDIT/LISTBOX/COMBOBOX still own input, selection, accessibility and IME. */
static void TorrentShapeControl(HWND hwnd){
    if(!hwnd||!IsWindow(hwnd))return;
    RECT r={0};GetClientRect(hwnd,&r);
    int w=r.right-r.left,h=r.bottom-r.top;
    if(w<2||h<2)return;
    HRGN region=CreateRoundRectRgn(0,0,w+1,h+1,14,14);
    if(region)SetWindowRgn(hwnd,region,TRUE);
}
static void TorrentDrawControlFrame(HWND hwnd,HDC dc){
    RECT r={0};GetClientRect(hwnd,&r);
    if(r.right<=2||r.bottom<=2)return;
    BOOL focus=GetFocus()==hwnd;
    BOOL hot=FALSE;POINT pt;GetCursorPos(&pt);ScreenToClient(hwnd,&pt);
    hot=PtInRect(&r,pt);
    COLORREF edge=focus?ABM_UI_COLOR_FOCUS:(hot?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_BORDER_STRONG);
    if(g_torrentButtonCanvasReady&&ABM_D2D_CanvasBegin(&g_torrentButtonCanvas,dc,&r)){
        ABM_D2D_CanvasStrokeRound(&g_torrentButtonCanvas,0.5f,0.5f,(float)r.right-0.5f,(float)r.bottom-0.5f,ABM_UI_RADIUS_CONTROL,edge,focus?2.0f:1.0f);
        ABM_D2D_CanvasEnd(&g_torrentButtonCanvas);
        return;
    }
    HPEN pen=CreatePen(PS_SOLID,focus?2:1,edge);
    HGDIOBJ op=SelectObject(dc,pen),ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,r.left,r.top,r.right-1,r.bottom-1,ABM_UI_RADIUS_CONTROL*2,ABM_UI_RADIUS_CONTROL*2);
    SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(pen);
}
static LRESULT CALLBACK TorrentPremiumControlProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,
                                                   UINT_PTR id,DWORD_PTR ref){
    (void)id;(void)ref;
    if(msg==WM_NCDESTROY){RemoveWindowSubclass(hwnd,TorrentPremiumControlProc,1);return DefSubclassProc(hwnd,msg,wp,lp);}
    if(msg==WM_SIZE){LRESULT z=DefSubclassProc(hwnd,msg,wp,lp);TorrentShapeControl(hwnd);return z;}
    if(msg==WM_NCPAINT)return 0;
    if(msg==WM_ERASEBKGND)return DefSubclassProc(hwnd,msg,wp,lp);
    if(msg==WM_PAINT){
        LRESULT z=DefSubclassProc(hwnd,msg,wp,lp);
        HDC dc=GetDC(hwnd);
        if(dc){
            RECT r={0};GetClientRect(hwnd,&r);
            TorrentDrawControlFrame(hwnd,dc);
            wchar_t cls[32]=L"";GetClassNameW(hwnd,cls,_countof(cls));
            if(_wcsicmp(cls,L"ComboBox")==0){
                /* Fully replace the themed Win32 drop-button chrome. The native
                   combo remains responsible for input, keyboard and popup list,
                   but its grey arrow surface/glyph is covered by our own navy
                   rounded segment and teal chevron. */
                const int arrowW=34;
                RECT arrow={r.right-arrowW,r.top+1,r.right-1,r.bottom-1};
                if(g_torrentButtonCanvasReady&&ABM_D2D_CanvasBegin(&g_torrentButtonCanvas,dc,&r)){
                    ABM_D2D_CanvasFillRound(&g_torrentButtonCanvas,(float)arrow.left,(float)arrow.top,(float)arrow.right,(float)arrow.bottom,ABM_UI_RADIUS_CONTROL,ABM_THEME_COLOR_113041);
                    int cx=arrow.left+arrowW/2,cy=(arrow.top+arrow.bottom)/2-1;
                    ABM_D2D_CanvasLine(&g_torrentButtonCanvas,(float)cx-5.0f,(float)cy-2.0f,(float)cx,(float)cy+3.0f,ABM_THEME_COLOR_74DCCC,2.0f);
                    ABM_D2D_CanvasLine(&g_torrentButtonCanvas,(float)cx,(float)cy+3.0f,(float)cx+5.0f,(float)cy-2.0f,ABM_THEME_COLOR_74DCCC,2.0f);
                    ABM_D2D_CanvasEnd(&g_torrentButtonCanvas);
                }else{
                    HBRUSH fill=CreateSolidBrush(ABM_THEME_COLOR_113041);
                    HPEN edge=CreatePen(PS_SOLID,1,ABM_THEME_COLOR_2B6977);
                    HGDIOBJ ob=SelectObject(dc,fill),op=SelectObject(dc,edge);
                    RoundRect(dc,arrow.left,arrow.top,arrow.right,arrow.bottom,ABM_UI_RADIUS_CONTROL*2,ABM_UI_RADIUS_CONTROL*2);
                    SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(fill);DeleteObject(edge);
                    HPEN chevron=CreatePen(PS_SOLID,2,ABM_THEME_COLOR_74DCCC);
                    op=SelectObject(dc,chevron);
                    int cx=arrow.left+arrowW/2,cy=(arrow.top+arrow.bottom)/2-1;
                    MoveToEx(dc,cx-5,cy-2,NULL);LineTo(dc,cx,cy+3);LineTo(dc,cx+5,cy-2);
                    SelectObject(dc,op);DeleteObject(chevron);
                }
                if(g_torrentButtonCanvasReady&&ABM_D2D_CanvasBegin(&g_torrentButtonCanvas,dc,&r)){
                    ABM_D2D_CanvasStrokeRound(&g_torrentButtonCanvas,0.5f,0.5f,(float)r.right-0.5f,(float)r.bottom-0.5f,
                        ABM_UI_RADIUS_CONTROL,(GetFocus()==hwnd)?ABM_THEME_COLOR_5BCDBC:ABM_THEME_COLOR_235265,1.0f);
                    ABM_D2D_CanvasEnd(&g_torrentButtonCanvas);
                }else{
                    HPEN border=CreatePen(PS_SOLID,1,(GetFocus()==hwnd)?ABM_THEME_COLOR_5BCDBC:ABM_THEME_COLOR_235265);
                    HGDIOBJ op2=SelectObject(dc,border),ob2=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
                    RoundRect(dc,r.left,r.top,r.right-1,r.bottom-1,ABM_UI_RADIUS_CONTROL*2,ABM_UI_RADIUS_CONTROL*2);
                    SelectObject(dc,ob2);SelectObject(dc,op2);DeleteObject(border);
                }
            }
            ReleaseDC(hwnd,dc);
        }
        return z;
    }
    if(msg==WM_SETFOCUS||msg==WM_KILLFOCUS||msg==WM_MOUSEMOVE||msg==WM_MOUSELEAVE){
        LRESULT z=DefSubclassProc(hwnd,msg,wp,lp);InvalidateRect(hwnd,NULL,FALSE);return z;
    }
    return DefSubclassProc(hwnd,msg,wp,lp);
}
static void TorrentPremiumizeControl(HWND hwnd){
    if(!hwnd)return;
    LONG_PTR style=GetWindowLongPtrW(hwnd,GWL_STYLE);
    SetWindowLongPtrW(hwnd,GWL_STYLE,style&~(WS_BORDER|WS_DLGFRAME));
    LONG_PTR ex=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd,GWL_EXSTYLE,ex&~WS_EX_CLIENTEDGE);
    SetWindowSubclass(hwnd,TorrentPremiumControlProc,1,0);
    TorrentShapeControl(hwnd);
    InvalidateRect(hwnd,NULL,TRUE);
}

static void TorrentDrawFileListItem(const DRAWITEMSTRUCT *d){
    if(!d)return;
    HDC dc=d->hDC;RECT r=d->rcItem;
    if(g_torrentButtonCanvasReady&&ABM_D2D_CanvasBegin(&g_torrentButtonCanvas,dc,&r)){
        COLORREF bg=(d->itemState&ODS_SELECTED)?ABM_UI_COLOR_ACCENT_SOFT:((d->itemID&1)?ABM_UI_COLOR_SURFACE:ABM_UI_COLOR_SURFACE_BLUE);
        ABM_D2D_CanvasFill(&g_torrentButtonCanvas,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,bg);
        if(d->itemState&ODS_SELECTED){
            RECT pill=r;InflateRect(&pill,-3,-2);
            ABM_D2D_CanvasFillRound(&g_torrentButtonCanvas,(float)pill.left,(float)pill.top,(float)pill.right,(float)pill.bottom,ABM_UI_RADIUS_BUTTON,ABM_UI_COLOR_SURFACE_RAISED);
            ABM_D2D_CanvasStrokeRound(&g_torrentButtonCanvas,(float)pill.left,(float)pill.top,(float)pill.right,(float)pill.bottom,ABM_UI_RADIUS_BUTTON,ABM_UI_COLOR_ACCENT_BRIGHT,1.0f);
            ABM_D2D_CanvasFill(&g_torrentButtonCanvas,(float)r.left+5.0f,(float)r.top+6.0f,(float)r.left+8.0f,(float)r.bottom-6.0f,ABM_UI_COLOR_ACCENT_BRIGHT);
        }
        wchar_t line[1200]=L"";
        if(d->itemID!=(UINT)-1)SendMessageW(d->hwndItem,LB_GETTEXT,d->itemID,(LPARAM)line);
        RECT tx=r;tx.left+=12;tx.right-=10;
        ABM_D2D_CanvasText(&g_torrentButtonCanvas,line,tx,(d->itemState&ODS_SELECTED)?ABM_UI_COLOR_TEXT:ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(d->itemState&ODS_FOCUS)ABM_D2D_CanvasStrokeRound(&g_torrentButtonCanvas,(float)r.left+1,(float)r.top+1,(float)r.right-1,(float)r.bottom-1,ABM_UI_RADIUS_BUTTON,ABM_UI_COLOR_FOCUS,1.8f);
        ABM_D2D_CanvasEnd(&g_torrentButtonCanvas);
        return;
    }
    COLORREF bg=(d->itemState&ODS_SELECTED)?ABM_THEME_COLOR_144B53:((d->itemID&1)?ABM_THEME_COLOR_0C1C2C:ABM_THEME_COLOR_0A1827);
    HBRUSH b=CreateSolidBrush(bg);FillRect(dc,&r,b);DeleteObject(b);
    if(d->itemState&ODS_SELECTED){
        RECT pill=r;InflateRect(&pill,-3,-2);
        HBRUSH sb=CreateSolidBrush(ABM_THEME_COLOR_14444F);HPEN sp=CreatePen(PS_SOLID,1,ABM_THEME_COLOR_31848B);
        HGDIOBJ ob=SelectObject(dc,sb),op=SelectObject(dc,sp);
        RoundRect(dc,pill.left,pill.top,pill.right,pill.bottom,10,10);
        SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(sb);DeleteObject(sp);
        RECT marker={r.left+5,r.top+6,r.left+8,r.bottom-6};
        HBRUSH mb=CreateSolidBrush(ABM_THEME_COLOR_5DDABE);FillRect(dc,&marker,mb);DeleteObject(mb);
    }
    wchar_t line[1200]=L"";
    if(d->itemID!=(UINT)-1)SendMessageW(d->hwndItem,LB_GETTEXT,d->itemID,(LPARAM)line);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,(d->itemState&ODS_SELECTED)?ABM_THEME_COLOR_F7FDFF:ABM_THEME_COLOR_B2C9DA);
    RECT tx=r;tx.left+=12;tx.right-=10;
    HGDIOBJ old=SelectObject(dc,g_smallFont?g_smallFont:GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(dc,line,-1,&tx,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SelectObject(dc,old);
    if(d->itemState&ODS_FOCUS){HPEN pen=CreatePen(PS_SOLID,1,ABM_THEME_COLOR_41BEA5);old=SelectObject(dc,pen);
        HGDIOBJ ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));Rectangle(dc,r.left+1,r.top+1,r.right-1,r.bottom-1);
        SelectObject(dc,ob);SelectObject(dc,old);DeleteObject(pen);}
}
static void TorrentDrawTvCombo(const DRAWITEMSTRUCT *d){
    if(!d||!d->hwndItem)return;
    HDC dc=d->hDC;RECT r=d->rcItem;
    if(g_torrentButtonCanvasReady&&ABM_D2D_CanvasBegin(&g_torrentButtonCanvas,dc,&r)){
        COLORREF bg=(d->itemState&ODS_COMBOBOXEDIT)?ABM_UI_COLOR_INPUT:ABM_UI_COLOR_SURFACE_RAISED;
        ABM_D2D_CanvasFill(&g_torrentButtonCanvas,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,bg);
        if(d->itemID!=(UINT)-1){
            if(d->itemState&ODS_SELECTED){RECT sr=r;InflateRect(&sr,-2,-1);ABM_D2D_CanvasFillRound(&g_torrentButtonCanvas,(float)sr.left,(float)sr.top,(float)sr.right,(float)sr.bottom,ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_ACCENT_SOFT);}
            wchar_t name[256]=L"";SendMessageW(d->hwndItem,CB_GETLBTEXT,d->itemID,(LPARAM)name);
            RECT tr=r;tr.left+=13;tr.right-=36;
            ABM_D2D_CanvasText(&g_torrentButtonCanvas,name,tr,(d->itemState&ODS_SELECTED)?ABM_UI_COLOR_TEXT:ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        if(d->itemState&ODS_COMBOBOXEDIT)ABM_D2D_CanvasStrokeRound(&g_torrentButtonCanvas,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,ABM_UI_RADIUS_CONTROL,ABM_THEME_COLOR_1D4B5B,1.0f);
        ABM_D2D_CanvasEnd(&g_torrentButtonCanvas);
        return;
    }
    COLORREF bg=(d->itemState&ODS_COMBOBOXEDIT)?ABM_THEME_COLOR_0D1E2F:ABM_THEME_COLOR_0A1826;
    HBRUSH b=CreateSolidBrush(bg);FillRect(dc,&r,b);DeleteObject(b);
    if(d->itemID!=(UINT)-1){
        if(d->itemState&ODS_SELECTED){
            HBRUSH sb=CreateSolidBrush(ABM_THEME_COLOR_194653);RECT sr=r;InflateRect(&sr,-2,-1);
            FillRect(dc,&sr,sb);DeleteObject(sb);
        }
        wchar_t name[256]=L"";SendMessageW(d->hwndItem,CB_GETLBTEXT,d->itemID,(LPARAM)name);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,(d->itemState&ODS_SELECTED)?ABM_THEME_COLOR_F5FFFF:ABM_THEME_COLOR_BED5E3);
        RECT tr=r;tr.left+=13;tr.right-=36;
        HGDIOBJ old=SelectObject(dc,g_smallFont?g_smallFont:GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(dc,name,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        SelectObject(dc,old);
    }
    /* The combo's native drop button is covered and redrawn once by the
       control subclass after default painting. The edit item draws no arrow. */
    if(d->itemState&ODS_COMBOBOXEDIT){
        HPEN border=CreatePen(PS_SOLID,1,ABM_THEME_COLOR_1D4B5B);HGDIOBJ old=SelectObject(dc,border);
        HGDIOBJ ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));RoundRect(dc,r.left,r.top,r.right,r.bottom,10,10);
        SelectObject(dc,ob);SelectObject(dc,old);DeleteObject(border);
    }
}
static void TorrentApplyWindowChrome(HWND hwnd){
    if(!hwnd)return;
    BOOL dark=TRUE;
    DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    {int corners=2;COLORREF border=ABM_UI_COLOR_BORDER_STRONG,caption=ABM_THEME_COLOR_0A0F19;
     DwmSetWindowAttribute(hwnd,(DWORD)DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
     DwmSetWindowAttribute(hwnd,(DWORD)DWMWA_BORDER_COLOR,&border,sizeof(border));
     DwmSetWindowAttribute(hwnd,(DWORD)DWMWA_CAPTION_COLOR,&caption,sizeof(caption));}
    SetWindowPos(hwnd,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
}
static LRESULT CALLBACK TorrentWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_PAINT: { PAINTSTRUCT ps;BeginPaint(hwnd,&ps);if(g_torrentUiReady&&!TorrentPaint(hwnd,&ps.rcPaint))InvalidateRect(hwnd,NULL,FALSE);EndPaint(hwnd,&ps);return 0; }
    case WM_SIZE:
        if(g_torrentUiReady)ABM_D2D_Resize(&g_torrentUi,(UINT)LOWORD(lp),(UINT)HIWORD(lp));
        TorrentLayout(hwnd);
        RedrawWindow(hwnd,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE);
        return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO *mm=(MINMAXINFO*)lp;mm->ptMinTrackSize.x=900;mm->ptMinTrackSize.y=760;return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_THEMECHANGED:
        if(g_torrentSurfaceBrush){DeleteObject(g_torrentSurfaceBrush);g_torrentSurfaceBrush=NULL;}
        if(g_torrentControlBrush){DeleteObject(g_torrentControlBrush);g_torrentControlBrush=NULL;}
        TorrentApplyWindowChrome(hwnd);
        InvalidateRect(hwnd,NULL,TRUE);
        return 0;
    
    case WM_DRAWITEM: { const DRAWITEMSTRUCT *dis=(const DRAWITEMSTRUCT*)lp; if(dis&&dis->CtlType==ODT_BUTTON){TorrentDrawButton(dis);return TRUE;} if(dis&&dis->CtlType==ODT_COMBOBOX){TorrentDrawTvCombo(dis);return TRUE;} if(dis&&dis->CtlType==ODT_LISTBOX&&dis->CtlID==ID_TORRENT_FILES){TorrentDrawFileListItem(dis);return TRUE;} break; }
    case WM_MEASUREITEM: { MEASUREITEMSTRUCT *mi=(MEASUREITEMSTRUCT*)lp; if(mi&&mi->CtlType==ODT_COMBOBOX&&mi->CtlID==ID_TORRENT_TV_COMBO){mi->itemHeight=38;return TRUE;} break; }
    
    case WM_CTLCOLORDLG:
        if(!g_torrentSurfaceBrush)g_torrentSurfaceBrush=CreateSolidBrush(ABM_UI_COLOR_BACKGROUND);
        return (LRESULT)g_torrentSurfaceBrush;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc=(HDC)wp;
        SetTextColor(dc,ABM_UI_COLOR_TEXT_SECONDARY);
        SetBkMode(dc,OPAQUE);SetBkColor(dc,ABM_UI_COLOR_INPUT);
        if(!g_torrentControlBrush)g_torrentControlBrush=CreateSolidBrush(ABM_UI_COLOR_INPUT);
        return (LRESULT)g_torrentControlBrush;
    }
    case WM_CREATE: {
        /* Native EDIT retains caret, selection, IME and accessibility while
           using the Main Player body face and size. */
        g_torrentUiFont=CreateFontW(ABM_UI_FONT_HEIGHT(ABM_UI_FONT_SIZE_D2D_BODY),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,ABM_UI_FONT_FAMILY_TEXT);
        HFONT font=g_torrentUiFont?g_torrentUiFont:(HFONT)GetStockObject(DEFAULT_GUI_FONT);
        ZeroMemory(&g_torrentUi,sizeof(g_torrentUi));
        if(!ABM_D2D_Init(&g_torrentUi,hwnd)){if(g_torrentUiFont){DeleteObject(g_torrentUiFont);g_torrentUiFont=NULL;}return -1;}
        g_torrentUiReady=TRUE;
        if(!ABM_D2D_CanvasInit(&g_torrentButtonCanvas)){
            ABM_D2D_Destroy(&g_torrentUi);g_torrentUiReady=FALSE;
            if(g_torrentUiFont){DeleteObject(g_torrentUiFont);g_torrentUiFont=NULL;}
            return -1;
        }
        g_torrentButtonCanvasReady=TRUE;
        if(!TorrentRegisterSliderClass(GetModuleHandleW(NULL))){
            g_torrentButtonCanvasReady=FALSE;ABM_D2D_CanvasDestroy(&g_torrentButtonCanvas);
            ABM_D2D_Destroy(&g_torrentUi);g_torrentUiReady=FALSE;
            if(g_torrentUiFont){DeleteObject(g_torrentUiFont);g_torrentUiFont=NULL;}
            return -1;
        }
        HWND h=CreateWindowExW(0,L"STATIC",L"Magnet URI or local .torrent file",WS_CHILD|WS_VISIBLE,20,76,760,20,hwnd,NULL,NULL,NULL);
        g_torrentSourceHint=h;ShowWindow(h,SW_HIDE);SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentMagnetEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,20,62,570,30,hwnd,(HMENU)ID_TORRENT_MAGNET,NULL,NULL);
        SendMessageW(g_torrentMagnetEdit,WM_SETFONT,(WPARAM)font,TRUE);
        h=CreateWindowW(L"BUTTON",L"Add magnet",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,600,62,105,30,hwnd,(HMENU)ID_TORRENT_ADD_MAGNET,NULL,NULL);
        SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);
        h=CreateWindowW(L"BUTTON",L"Open .torrent",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,715,62,105,30,hwnd,(HMENU)ID_TORRENT_BROWSE,NULL,NULL);
        SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentInfoLabel=CreateWindowW(L"STATIC",L"No torrent loaded",WS_CHILD|WS_VISIBLE,20,108,800,42,hwnd,NULL,NULL,NULL);
        SendMessageW(g_torrentInfoLabel,WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentFileList=CreateWindowExW(0,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,20,158,800,300,hwnd,(HMENU)ID_TORRENT_FILES,NULL,NULL);
        SendMessageW(g_torrentFileList,WM_SETFONT,(WPARAM)font,TRUE);SendMessageW(g_torrentFileList,LB_SETITEMHEIGHT,0,26);
        g_torrentStatusLabel=CreateWindowW(L"STATIC",L"Engine ready. Add a source to begin.",WS_CHILD|WS_VISIBLE,20,466,800,28,hwnd,NULL,NULL,NULL);
        SendMessageW(g_torrentStatusLabel,WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentBufferBar=CreateWindowExW(0,PROGRESS_CLASSW,L"",WS_CHILD|WS_VISIBLE|PBS_SMOOTH|PBS_MARQUEE,20,540,770,14,hwnd,NULL,NULL,NULL);
        SendMessageW(g_torrentBufferBar,PBM_SETRANGE32,0,100);
        SendMessageW(g_torrentBufferBar,PBM_SETPOS,0,0);
        h=CreateWindowW(L"STATIC",L"DOWNLOAD KiB/s",WS_CHILD|WS_VISIBLE,20,505,130,22,hwnd,NULL,NULL,NULL);
        g_torrentDownLabel=h;SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentDownLimitEdit=CreateWindowExW(0,L"EDIT",L"0",WS_CHILD|WS_VISIBLE|ES_NUMBER|ES_READONLY|ES_CENTER,245,502,85,26,hwnd,(HMENU)ID_TORRENT_DOWN_LIMIT,NULL,NULL);
        SendMessageW(g_torrentDownLimitEdit,WM_SETFONT,(WPARAM)font,TRUE);
        h=CreateWindowW(L"STATIC",L"UPLOAD KiB/s",WS_CHILD|WS_VISIBLE,350,505,120,22,hwnd,NULL,NULL,NULL);
        g_torrentUpLabel=h;SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentUpLimitEdit=CreateWindowExW(0,L"EDIT",L"0",WS_CHILD|WS_VISIBLE|ES_NUMBER|ES_READONLY|ES_CENTER,570,502,85,26,hwnd,(HMENU)ID_TORRENT_UP_LIMIT,NULL,NULL);
        SendMessageW(g_torrentUpLimitEdit,WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentNumericButtons[0]=CreateWindowW(L"BUTTON",L"−",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,22,30,hwnd,(HMENU)ID_TORRENT_DOWN_DEC,NULL,NULL);
        g_torrentNumericButtons[1]=CreateWindowW(L"BUTTON",L"+",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,22,30,hwnd,(HMENU)ID_TORRENT_DOWN_INC,NULL,NULL);
        g_torrentNumericButtons[2]=CreateWindowW(L"BUTTON",L"−",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,22,30,hwnd,(HMENU)ID_TORRENT_UP_DEC,NULL,NULL);
        g_torrentNumericButtons[3]=CreateWindowW(L"BUTTON",L"+",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,22,30,hwnd,(HMENU)ID_TORRENT_UP_INC,NULL,NULL);
        g_torrentNumericButtons[4]=CreateWindowW(L"BUTTON",L"−",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,22,30,hwnd,(HMENU)ID_TORRENT_CACHE_DEC,NULL,NULL);
        g_torrentNumericButtons[5]=CreateWindowW(L"BUTTON",L"+",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,0,0,22,30,hwnd,(HMENU)ID_TORRENT_CACHE_INC,NULL,NULL);
        for(int ni=0;ni<6;ni++)SendMessageW(g_torrentNumericButtons[ni],WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentSliders[0]=CreateWindowExW(0,g_torrentSliderClass,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,300,46,hwnd,NULL,GetModuleHandleW(NULL),(LPVOID)(INT_PTR)0);
        g_torrentSliders[1]=CreateWindowExW(0,g_torrentSliderClass,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,300,46,hwnd,NULL,GetModuleHandleW(NULL),(LPVOID)(INT_PTR)1);
        g_torrentSliders[2]=CreateWindowExW(0,g_torrentSliderClass,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,250,46,hwnd,NULL,GetModuleHandleW(NULL),(LPVOID)(INT_PTR)2);
        h=CreateWindowW(L"BUTTON",L"Apply limits",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,670,500,120,30,hwnd,(HMENU)ID_TORRENT_APPLY_LIMITS,NULL,NULL);
        SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);
        h=CreateWindowW(L"STATIC",L"CACHE QUOTA GiB",WS_CHILD|WS_VISIBLE,20,612,130,22,hwnd,NULL,NULL,NULL);
        g_torrentCacheLabel=h;SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentCacheLimitEdit=CreateWindowExW(0,L"EDIT",L"20",WS_CHILD|WS_VISIBLE|ES_NUMBER|ES_READONLY|ES_CENTER,165,608,65,26,hwnd,(HMENU)ID_TORRENT_CACHE_LIMIT,NULL,NULL);
        SendMessageW(g_torrentCacheLimitEdit,WM_SETFONT,(WPARAM)font,TRUE);
        h=CreateWindowW(L"BUTTON",L"Apply quota",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,245,606,105,30,hwnd,(HMENU)ID_TORRENT_CACHE_APPLY,NULL,NULL);
        SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);
        h=CreateWindowW(L"BUTTON",L"Clear inactive cache",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,365,606,155,30,hwnd,(HMENU)ID_TORRENT_CLEAR_CACHE,NULL,NULL);
        SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentCacheInfoLabel=CreateWindowW(L"STATIC",L"Disk cache: calculating…",WS_CHILD|WS_VISIBLE,20,648,800,24,hwnd,NULL,NULL,NULL);
        g_torrentTvButtons[0]=CreateWindowW(L"BUTTON",L"Find TVs",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,20,684,130,38,hwnd,(HMENU)ID_TORRENT_FIND_TVS,NULL,NULL);
        SendMessageW(g_torrentTvButtons[0],WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentTvCombo=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,140,684,260,220,hwnd,(HMENU)ID_TORRENT_TV_COMBO,NULL,NULL);
        SendMessageW(g_torrentTvCombo,WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentTvButtons[1]=CreateWindowW(L"BUTTON",L"Cast to TV",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,415,684,130,38,hwnd,(HMENU)ID_TORRENT_CAST_TV,NULL,NULL);
        SendMessageW(g_torrentTvButtons[1],WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentTvButtons[2]=CreateWindowW(L"BUTTON",L"Stop TV",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,545,684,136,38,hwnd,(HMENU)ID_TORRENT_STOP_TV,NULL,NULL);
        SendMessageW(g_torrentTvButtons[2],WM_SETFONT,(WPARAM)font,TRUE);
        g_torrentOpenDownloadsButton=CreateWindowW(L"BUTTON",L"Open Downloads Folder",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,20,724,190,36,hwnd,(HMENU)ID_TORRENT_OPEN_DOWNLOADS,NULL,NULL);
        SendMessageW(g_torrentOpenDownloadsButton,WM_SETFONT,(WPARAM)font,TRUE);
        SendMessageW(g_torrentCacheInfoLabel,WM_SETFONT,(WPARAM)font,TRUE);
        const struct {int id;const wchar_t *label;} buttons[]={
            {ID_TORRENT_PAUSE,L"Pause / Resume Download"},{ID_TORRENT_STREAM,L"Play / Stream"},
            {ID_TORRENT_STOP,L"Stop stream"},{ID_TORRENT_REMOVE,L"Remove task"},{ID_TORRENT_REFRESH,L"Refresh"}
        };
        for(size_t i=0;i<_countof(buttons);i++){g_torrentActionButtons[i]=CreateWindowW(L"BUTTON",buttons[i].label,WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,20,568,125,40,hwnd,(HMENU)(INT_PTR)buttons[i].id,NULL,NULL);SendMessageW(g_torrentActionButtons[i],WM_SETFONT,(WPARAM)font,TRUE);}
        for(HWND child=GetWindow(hwnd,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
            TorrentApplyDarkControl(child);SendMessageW(child,WM_SETFONT,(WPARAM)font,TRUE);
        }
        /* Premium surfaces for editable fields, the TV selector and the file library. */
        TorrentPremiumizeControl(g_torrentMagnetEdit);
        TorrentPremiumizeControl(g_torrentDownLimitEdit);
        TorrentPremiumizeControl(g_torrentUpLimitEdit);
        TorrentPremiumizeControl(g_torrentCacheLimitEdit);
        TorrentPremiumizeControl(g_torrentTvCombo);
        TorrentPremiumizeControl(g_torrentFileList);
        /* Keep all interactive child controls visible until their Direct2D
           canvas replacements are wired. Never hide the command buttons. */
        ShowWindow(g_torrentFileList,SW_SHOW);
        /* Native STATIC controls are retained as accessible text/state
           holders. Their visible text is rendered by TorrentPaint/DirectWrite. */
        ShowWindow(g_torrentInfoLabel,SW_HIDE);
        ShowWindow(g_torrentStatusLabel,SW_HIDE);
        ShowWindow(g_torrentDownLabel,SW_HIDE);
        ShowWindow(g_torrentUpLabel,SW_HIDE);
        ShowWindow(g_torrentCacheLabel,SW_HIDE);
        ShowWindow(g_torrentCacheInfoLabel,SW_HIDE);
        ShowWindow(g_torrentBufferBar,SW_HIDE); /* replaced by the Direct2D progress rail */
        { const int ids[]={ID_TORRENT_DOWN_DEC,ID_TORRENT_DOWN_INC,ID_TORRENT_UP_DEC,ID_TORRENT_UP_INC,
            ID_TORRENT_CACHE_DEC,ID_TORRENT_CACHE_INC,ID_TORRENT_ADD_MAGNET,ID_TORRENT_BROWSE,ID_TORRENT_APPLY_LIMITS,
            ID_TORRENT_CACHE_APPLY,ID_TORRENT_CLEAR_CACHE,ID_TORRENT_FIND_TVS,
            ID_TORRENT_CAST_TV,ID_TORRENT_STOP_TV,ID_TORRENT_PAUSE,ID_TORRENT_STREAM,
            ID_TORRENT_STOP,ID_TORRENT_REMOVE,ID_TORRENT_REFRESH};
          for(int i=0;i<(int)_countof(ids);i++){
              HWND button=GetDlgItem(hwnd,ids[i]);if(button){
                  LONG_PTR style=GetWindowLongPtrW(button,GWL_STYLE);
                  SetWindowLongPtrW(button,GWL_STYLE,style|BS_OWNERDRAW);
                  ShowWindow(button,SW_SHOW);
              }
          } }
        TorrentLayout(hwnd);
        TorrentRefreshCacheStatus();
        g_torrentTvKnownCount=-1;TorrentRefreshTvDevices();
        SetTimer(hwnd,1,1000,NULL);return 0;
    }
    case WM_COMMAND:
        switch(LOWORD(wp)) {
            case ID_TORRENT_DOWN_DEC: case ID_TORRENT_DOWN_INC:
            case ID_TORRENT_UP_DEC: case ID_TORRENT_UP_INC:
            case ID_TORRENT_CACHE_DEC: case ID_TORRENT_CACHE_INC: {
                HWND edit=NULL;unsigned long long step=1,limit=0;BOOL inc=FALSE;
                switch(LOWORD(wp)){
                case ID_TORRENT_DOWN_DEC:edit=g_torrentDownLimitEdit;step=64;limit=2097151;break;
                case ID_TORRENT_DOWN_INC:edit=g_torrentDownLimitEdit;step=64;limit=2097151;inc=TRUE;break;
                case ID_TORRENT_UP_DEC:edit=g_torrentUpLimitEdit;step=64;limit=2097151;break;
                case ID_TORRENT_UP_INC:edit=g_torrentUpLimitEdit;step=64;limit=2097151;inc=TRUE;break;
                case ID_TORRENT_CACHE_DEC:edit=g_torrentCacheLimitEdit;step=1;limit=4096;break;
                case ID_TORRENT_CACHE_INC:edit=g_torrentCacheLimitEdit;step=1;limit=4096;inc=TRUE;break;
                }
                if(edit){wchar_t value[32]={0};GetWindowTextW(edit,value,_countof(value));unsigned long long n=_wcstoui64(value,NULL,10);
                    if(inc)n=(n>limit-step)?limit:n+step;else n=n>step?n-step:0;
                    wchar_t out[32];StringCchPrintfW(out,_countof(out),L"%llu",n);SetWindowTextW(edit,out);}
                return 0;
            }
            case ID_TORRENT_ADD_MAGNET:TorrentAddSource(hwnd,FALSE);return 0;
            case ID_TORRENT_BROWSE:TorrentAddSource(hwnd,TRUE);return 0;
            case ID_TORRENT_PAUSE:
                if(g_torrentTaskId&&g_torrentGetTask){ABM_TorrentTaskInfo info={0};if(g_torrentGetTask(g_torrentTaskId,&info)){if(info.state==5)g_torrentResume(g_torrentTaskId);else g_torrentPause(g_torrentTaskId);TorrentRefreshStatus();}}return 0;
            case ID_TORRENT_STREAM:TorrentBeginStream();return 0;
            case ID_TORRENT_FIND_TVS:
                ABM_TorrentDlna_StartDiscovery();g_torrentTvKnownCount=-1;
                TorrentSetLabel(g_torrentStatusLabel,L"Searching for Android/Google TVs with DLNA MediaRenderer support…");
                TorrentRefreshTvDevices();return 0;
            case ID_TORRENT_CAST_TV:TorrentBeginCastToTV();return 0;
            case ID_TORRENT_STOP_TV:TorrentStopTvCasting();return 0;
            case ID_TORRENT_OPEN_DOWNLOADS:
                if(EnsureTorrentDownloadDir()) {
                    const wchar_t *active=(g_torrentInitialized&&g_torrentActiveDownloadDir[0])?g_torrentActiveDownloadDir:g_torrentDownloadDir;
                    ShellExecuteW(hwnd,L"open",active,NULL,NULL,SW_SHOWNORMAL);
                }
                else ABM_MessageBoxW(hwnd,L"Could not open the configured permanent downloads folder.",L"Torrent Downloads",MB_OK|MB_ICONWARNING);
                return 0;
            case ID_TORRENT_STOP:TorrentStopPlayback();TorrentSetLabel(g_torrentStatusLabel,L"Stream stopped.");return 0;
            case ID_TORRENT_REMOVE:
                TorrentStopTvCasting();TorrentStopPlayback();
                if(g_torrentTaskId&&g_torrentRemove)g_torrentRemove(g_torrentTaskId,FALSE);
                g_torrentTaskId=0;g_torrentListedForTask=-1;g_torrentListedFileCount=0;
                SendMessageW(g_torrentFileList,LB_RESETCONTENT,0,0);
                TorrentSetLabel(g_torrentInfoLabel,L"No torrent loaded");
                TorrentSetLabel(g_torrentStatusLabel,L"Task removed. Downloaded data was retained.");
                return 0;
            case ID_TORRENT_REFRESH:TorrentRefreshStatus();TorrentRefreshCacheStatus();return 0;
            case ID_TORRENT_CACHE_APPLY: {
                unsigned long long gib=(unsigned long long)g_torrentSliderValues[2];
                if(gib<1||gib>4096) {
                    TorrentSetLabel(g_torrentStatusLabel,L"Cache quota must be from 1 to 4096 GiB.");return 0;
                }
                uint64_t used=0,limit=(uint64_t)gib*1024ULL*1024ULL*1024ULL;
                BOOL applied=g_torrentSetCacheLimit&&g_torrentSetCacheLimit(limit,&used);
                TorrentRefreshCacheStatus();
                if(applied)TorrentSetLabel(g_torrentStatusLabel,L"Cache quota applied. Oldest inactive torrent folders are evicted first when needed.");
                else TorrentSetLabel(g_torrentStatusLabel,L"Quota saved, but active torrent data prevents cleanup below this limit. Stop/remove active tasks or choose a larger quota.");
                return 0;
            }
            case ID_TORRENT_CLEAR_CACHE: {
                if(ABM_MessageBoxW(hwnd,L"Delete cached data for inactive torrents? Data belonging to the currently loaded torrent is protected. This cannot be undone.",L"Clear Torrent Cache",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return 0;
                uint64_t freed=0,remaining=0;
                if(g_torrentClearCache&&g_torrentClearCache(&freed,&remaining)) {
                    wchar_t a[64],b[64],statusMessage[192];TorrentFormatBytes((int64_t)freed,a,_countof(a));TorrentFormatBytes((int64_t)remaining,b,_countof(b));
                    StringCchPrintfW(statusMessage,_countof(statusMessage),L"Inactive cache cleared — freed %s. Remaining cache: %s.",a,b);
                    TorrentSetLabel(g_torrentStatusLabel,statusMessage);TorrentRefreshCacheStatus();
                } else TorrentSetLabel(g_torrentStatusLabel,L"Could not clear inactive torrent cache.");
                return 0;
            }
            case ID_TORRENT_APPLY_LIMITS: {
                unsigned long long down=(unsigned long long)g_torrentSliderValues[0];
                unsigned long long up=(unsigned long long)g_torrentSliderValues[1];
                wchar_t downText[32],upText[32];
                StringCchPrintfW(downText,_countof(downText),L"%llu",down);
                StringCchPrintfW(upText,_countof(upText),L"%llu",up);
                if(down>(unsigned long long)INT_MAX/1024ULL||up>(unsigned long long)INT_MAX/1024ULL) {
                    TorrentSetLabel(g_torrentStatusLabel,L"Bandwidth limit is outside the supported range.");return 0;
                }
                if(!g_torrentSetRateLimits||(g_torrentInitialized&&!g_torrentSetRateLimits((int)(down*1024ULL),(int)(up*1024ULL)))) {
                    TorrentSetLabel(g_torrentStatusLabel,L"Could not apply torrent bandwidth limits.");
                    return 0;
                }
                wchar_t applied[192];
                StringCchPrintfW(applied,_countof(applied),L"Limits applied — download: %s KiB/s, upload: %s KiB/s. 0 = unlimited.",downText,upText);
                TorrentSetLabel(g_torrentStatusLabel,applied);
                return 0;
            }
        }break;
    case WM_TIMER:if(wp==1){TorrentRefreshStatus();TorrentRefreshTvDevices();if((GetTickCount64()%15000)<1000)TorrentRefreshCacheStatus();return 0;}break;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:
        KillTimer(hwnd,1);
        /* Closing the manager while startup buffering is pending cancels that
           request; closing it after playback starts leaves playback running. */
        if(g_torrentStreamThread&&!g_torrentPlaybackActive)TorrentStopPlayback();
        g_torrentWindow=NULL;g_torrentMagnetEdit=NULL;g_torrentFileList=NULL;
        g_torrentUiReady=FALSE;
        g_torrentButtonCanvasReady=FALSE;
        ABM_D2D_CanvasDestroy(&g_torrentButtonCanvas);
        ABM_D2D_Destroy(&g_torrentUi);
        if(g_torrentUiFont){DeleteObject(g_torrentUiFont);g_torrentUiFont=NULL;}
        if(g_torrentSurfaceBrush){DeleteObject(g_torrentSurfaceBrush);g_torrentSurfaceBrush=NULL;}
        if(g_torrentControlBrush){DeleteObject(g_torrentControlBrush);g_torrentControlBrush=NULL;}
        g_torrentInfoLabel=NULL;g_torrentStatusLabel=NULL;g_torrentDownLimitEdit=NULL;g_torrentUpLimitEdit=NULL;g_torrentBufferBar=NULL;g_torrentOpenDownloadsButton=NULL;
        g_torrentCacheLimitEdit=NULL;g_torrentCacheInfoLabel=NULL;g_torrentTvCombo=NULL;g_torrentDownloadPathLabel=NULL;return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static void ShowTorrentWindow(HWND owner) {
    if(!g_torrentInitialized){StartTorrentEngine(owner);return;}
    if(g_torrentWindow&&IsWindow(g_torrentWindow)){ShowWindow(g_torrentWindow,SW_SHOWNORMAL);SetForegroundWindow(g_torrentWindow);return;}
    static BOOL registered=FALSE;HINSTANCE inst=GetModuleHandleW(NULL);
    if(!registered){WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.lpfnWndProc=TorrentWndProc;wc.hInstance=inst;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=CreateSolidBrush(ABM_THEME_COLOR_0A111D);wc.hIcon=LoadIconW(inst,MAKEINTRESOURCEW(IDI_APP_ICON));wc.lpszClassName=L"ABMAVPlayerTorrent";if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;registered=TRUE;}
    RECT wr;GetWindowRect(owner,&wr);MONITORINFO mi={sizeof(mi)};HMONITOR mon=MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST);GetMonitorInfoW(mon,&mi);int maxW=mi.rcWork.right-mi.rcWork.left-32,maxH=mi.rcWork.bottom-mi.rcWork.top-32;int w=maxW<1080?maxW:1080,h=maxH<980?maxH:980;if(w<760)w=760;if(h<620)h=620;
    int x=wr.left+(wr.right-wr.left-w)/2,y=wr.top+(wr.bottom-wr.top-h)/2;if(x<0)x=20;if(y<0)y=20;
    g_torrentWindow=CreateWindowExW(0,L"ABMAVPlayerTorrent",L"ABM AV Player — Torrent Stream",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,x,y,w,h,owner,NULL,inst,NULL);
    if(g_torrentWindow){
        HICON appIcon=LoadIconW(inst,MAKEINTRESOURCEW(IDI_APP_ICON));
        if(appIcon){SendMessageW(g_torrentWindow,WM_SETICON,ICON_BIG,(LPARAM)appIcon);SendMessageW(g_torrentWindow,WM_SETICON,ICON_SMALL,(LPARAM)appIcon);}
        TorrentApplyWindowChrome(g_torrentWindow);
        ShowWindow(g_torrentWindow,SW_SHOW);UpdateWindow(g_torrentWindow);TorrentRefreshStatus();
    }
}

static HBITMAP g_aboutPortrait = NULL;
static ABM_D2D_CANVAS g_aboutCanvas;
static BOOL g_aboutCanvasReady=FALSE;

static HBITMAP LoadAboutPortrait(void) {
    wchar_t exePath[MAX_PATH], candidate[MAX_PATH];
    DWORD n=GetModuleFileNameW(NULL,exePath,MAX_PATH);
    if(!n || n>=MAX_PATH) return NULL;

    wchar_t *slash=wcsrchr(exePath,L'\\');
    if(!slash) return NULL;
    *slash=0;

    if(SUCCEEDED(StringCchPrintfW(candidate,MAX_PATH,L"%s\\assets\\owner_portrait.bmp",exePath))) {
        HBITMAP bmp=(HBITMAP)LoadImageW(NULL,candidate,IMAGE_BITMAP,0,0,LR_LOADFROMFILE|LR_CREATEDIBSECTION);
        if(bmp) return bmp;
    }

    /* When launched from the build\\ directory, the asset lives one level up. */
    slash=wcsrchr(exePath,L'\\');
    if(slash && slash>exePath) {
        *slash=0;
        if(SUCCEEDED(StringCchPrintfW(candidate,MAX_PATH,L"%s\\assets\\owner_portrait.bmp",exePath))) {
            HBITMAP bmp=(HBITMAP)LoadImageW(NULL,candidate,IMAGE_BITMAP,0,0,LR_LOADFROMFILE|LR_CREATEDIBSECTION);
            if(bmp) return bmp;
        }
    }

    /* Also support launching from the repository root with a relative working directory. */
    DWORD cwd=GetCurrentDirectoryW(MAX_PATH,candidate);
    if(cwd && cwd<MAX_PATH) {
        wchar_t path[MAX_PATH];
        if(SUCCEEDED(StringCchPrintfW(path,MAX_PATH,L"%s\\assets\\owner_portrait.bmp",candidate))) {
            HBITMAP bmp=(HBITMAP)LoadImageW(NULL,path,IMAGE_BITMAP,0,0,LR_LOADFROMFILE|LR_CREATEDIBSECTION);
            if(bmp) return bmp;
        }
    }
    return NULL;
}

static LRESULT CALLBACK AboutWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        g_aboutPortrait=LoadAboutPortrait();
        g_aboutCanvasReady=ABM_D2D_CanvasInit(&g_aboutCanvas);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps);
        RECT rc; GetClientRect(hwnd,&rc);
        if(g_aboutCanvasReady&&ABM_D2D_CanvasBegin(&g_aboutCanvas,dc,&rc)){
            ABM_D2D_CanvasFill(&g_aboutCanvas,0,0,(float)rc.right,(float)rc.bottom,ABM_UI_COLOR_BACKGROUND);
            ABM_D2D_CanvasFill(&g_aboutCanvas,0,0,(float)rc.right,7.0f,ABM_UI_COLOR_ACCENT);
            ABM_D2D_CanvasFill(&g_aboutCanvas,28,405,(float)rc.right-28,407,ABM_UI_COLOR_BORDER);
            ABM_D2D_CanvasFillRound(&g_aboutCanvas,34,55,284,305,(float)ABM_UI_RADIUS_CARD,ABM_UI_COLOR_SURFACE_RAISED);
            ABM_D2D_CanvasEnd(&g_aboutCanvas);
        }else{
            HBRUSH bg=CreateSolidBrush(ABM_UI_COLOR_BACKGROUND);FillRect(dc,&rc,bg);DeleteObject(bg);
            RECT band={0,0,rc.right,7};HBRUSH accent=CreateSolidBrush(ABM_UI_COLOR_ACCENT);FillRect(dc,&band,accent);DeleteObject(accent);
        }
        if(g_aboutPortrait) {
            HDC mem=CreateCompatibleDC(dc);HBITMAP old=(HBITMAP)SelectObject(mem,g_aboutPortrait);
            BITMAP bm;GetObjectW(g_aboutPortrait,sizeof(bm),&bm);
            int sw=bm.bmWidth<bm.bmHeight?bm.bmWidth:bm.bmHeight;
            int sx=(bm.bmWidth-sw)/2,sy=(bm.bmHeight-sw)/2;
            SetStretchBltMode(dc,HALFTONE);StretchBlt(dc,34,55,250,250,mem,sx,sy,sw,sw,SRCCOPY);
            SelectObject(mem,old);DeleteDC(mem);
        }
        if(g_aboutCanvasReady&&ABM_D2D_CanvasBegin(&g_aboutCanvas,dc,&rc)){
            RECT r={320,58,rc.right-34,108};
            ABM_D2D_CanvasText(&g_aboutCanvas,L"ABM AV Player",r,ABM_UI_COLOR_TEXT,ABM_D2D_TEXT_HEADING,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r.top=116;r.bottom=145;ABM_D2D_CanvasText(&g_aboutCanvas,L"THE CREATOR",r,ABM_UI_COLOR_ACCENT_BRIGHT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r.top=151;r.bottom=192;ABM_D2D_CanvasText(&g_aboutCanvas,L"Abhisek Basu Mullick",r,ABM_THEME_COLOR_FFFFFF,ABM_D2D_TEXT_HEADING,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r.top=198;r.bottom=232;ABM_D2D_CanvasText(&g_aboutCanvas,L"Founder & Developer",r,ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_BODY,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r.top=245;r.bottom=274;ABM_D2D_CanvasText(&g_aboutCanvas,L"CONTACT",r,ABM_UI_COLOR_ACCENT_BRIGHT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r.top=278;r.bottom=310;ABM_D2D_CanvasText(&g_aboutCanvas,L"abmullick@gmail.com",r,ABM_THEME_COLOR_FAFCFF,ABM_D2D_TEXT_BODY,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r.top=320;r.bottom=399;ABM_D2D_CanvasText(&g_aboutCanvas,L"Play local media, access streaming services through the Streaming Centre, and watch torrent video while it downloads. Cast playback to your TV.",r,ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_BODY,DT_LEFT|DT_TOP|DT_WORDBREAK);
            r.left=34;r.right=rc.right-34;r.top=424;r.bottom=454;ABM_D2D_CanvasText(&g_aboutCanvas,L"IN TRIBUTE TO DENNIS RITCHIE",r,ABM_UI_COLOR_ACCENT_BRIGHT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r.top=461;r.bottom=535;ABM_D2D_CanvasText(&g_aboutCanvas,L"My homage to Dennis Ritchie, creator of the C programming language and co-creator of Unix. His ideas and work helped shape modern computing and generations of computer science.",r,ABM_UI_COLOR_TEXT,ABM_D2D_TEXT_BODY,DT_LEFT|DT_TOP|DT_WORDBREAK);
            r.top=550;r.bottom=580;ABM_D2D_CanvasText(&g_aboutCanvas,L"BUILT WITH",r,ABM_UI_COLOR_ACCENT_BRIGHT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r.top=585;r.bottom=616;ABM_D2D_CanvasText(&g_aboutCanvas,L"C  •  C++  •  Win32 API  •  LibVLC  •  libtorrent  •  Direct2D  •  DirectWrite  •  WebView2",r,ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            r.top=628;r.bottom=656;ABM_D2D_CanvasText(&g_aboutCanvas,L"Version " APP_VERSION L"  |  © 2026 Abhisek Basu Mullick",r,ABM_UI_COLOR_TEXT_MUTED,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
             r.top=662;r.bottom=700;ABM_D2D_CanvasText(&g_aboutCanvas,ABM_TMDB_AttributionNotice(),r,ABM_UI_COLOR_TEXT_MUTED,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_TOP|DT_WORDBREAK);
            ABM_D2D_CanvasEnd(&g_aboutCanvas);
        }else{
            SetBkMode(dc,TRANSPARENT);
            RECT r={320,58,rc.right-34,108};HFONT oldFont=(HFONT)SelectObject(dc,g_headingFont);
            SetTextColor(dc,ABM_UI_COLOR_TEXT);DrawTextW(dc,L"ABM AV Player",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            SelectObject(dc,g_smallFont);SetTextColor(dc,ABM_UI_COLOR_ACCENT_BRIGHT);
            r.top=116;r.bottom=145;DrawTextW(dc,L"THE CREATOR",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            SelectObject(dc,g_headingFont);SetTextColor(dc,ABM_THEME_COLOR_FFFFFF);
            r.top=151;r.bottom=192;DrawTextW(dc,L"Abhisek Basu Mullick",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            SelectObject(dc,g_font);SetTextColor(dc,ABM_UI_COLOR_TEXT_SECONDARY);
            r.top=198;r.bottom=232;DrawTextW(dc,L"Founder & Developer",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            SelectObject(dc,g_smallFont);SetTextColor(dc,ABM_UI_COLOR_ACCENT_BRIGHT);
            r.top=245;r.bottom=274;DrawTextW(dc,L"CONTACT",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            SelectObject(dc,g_font);SetTextColor(dc,ABM_THEME_COLOR_FAFCFF);
            r.top=278;r.bottom=310;DrawTextW(dc,L"abmullick@gmail.com",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r.top=320;r.bottom=399;SetTextColor(dc,ABM_UI_COLOR_TEXT_SECONDARY);
            DrawTextW(dc,L"Play local media, access streaming services through the Streaming Centre, and watch torrent video while it downloads. Cast playback to your TV.",-1,&r,DT_LEFT|DT_TOP|DT_WORDBREAK);
            r.left=34;r.right=rc.right-34;r.top=424;r.bottom=454;SelectObject(dc,g_smallFont);SetTextColor(dc,ABM_UI_COLOR_ACCENT_BRIGHT);
            DrawTextW(dc,L"IN TRIBUTE TO DENNIS RITCHIE",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            SelectObject(dc,g_font);SetTextColor(dc,ABM_UI_COLOR_TEXT);
            r.top=461;r.bottom=535;DrawTextW(dc,L"My homage to Dennis Ritchie, creator of the C programming language and co-creator of Unix. His ideas and work helped shape modern computing and generations of computer science.",-1,&r,DT_LEFT|DT_TOP|DT_WORDBREAK);
            SelectObject(dc,g_smallFont);SetTextColor(dc,ABM_UI_COLOR_ACCENT_BRIGHT);
            r.top=550;r.bottom=580;DrawTextW(dc,L"BUILT WITH",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            SelectObject(dc,g_smallFont);SetTextColor(dc,ABM_UI_COLOR_TEXT_SECONDARY);
            r.top=585;r.bottom=616;DrawTextW(dc,L"C  •  C++  •  Win32 API  •  LibVLC  •  libtorrent  •  Direct2D  •  DirectWrite  •  WebView2",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            r.top=628;r.bottom=656;SetTextColor(dc,ABM_UI_COLOR_TEXT_MUTED);
            DrawTextW(dc,L"Version " APP_VERSION L"  |  © 2026 Abhisek Basu Mullick",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            SelectObject(dc,oldFont);
        }
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_KEYDOWN: if(wp==VK_ESCAPE){DestroyWindow(hwnd);return 0;} break;
    case WM_DESTROY: if(g_aboutPortrait){DeleteObject(g_aboutPortrait);g_aboutPortrait=NULL;} if(g_aboutCanvasReady){g_aboutCanvasReady=FALSE;ABM_D2D_CanvasDestroy(&g_aboutCanvas);} return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

static HBRUSH g_helpBrush=NULL, g_helpInputBrush=NULL, g_helpArticleBrush=NULL, g_helpNavBrush=NULL;
static ABM_D2D_CANVAS g_helpCanvas;
static BOOL g_helpCanvasReady=FALSE;
static HFONT g_helpTitleFont=NULL, g_helpSectionFont=NULL, g_helpBodyFont=NULL, g_helpMetaFont=NULL;
static HWND g_helpEdit=NULL, g_helpSearch=NULL, g_helpTopics=NULL, g_helpFindButton=NULL;
static HMODULE g_helpRichEditModule=NULL;
static const wchar_t *g_helpDocument=NULL;
static int g_helpArticleStart=0,g_helpArticleLength=0,g_helpSearchCursor=0;
static const wchar_t *g_helpTopicNames[]={
 L"GETTING STARTED",L"PLAYBACK AND FULLSCREEN",L"PLAYLISTS AND RECENT MEDIA",
 L"LIBRARY, CONTINUE WATCHING AND RESUME",L"MISSING MEDIA AND RELINKING",
 L"LICENSE & ACTIVATION",L"APPLICATION SETTINGS",L"AUDIO, VIDEO AND SUBTITLES",
 L"ADVANCED EQUALIZER AND AUDIO PROCESSING",L"NOW PLAYING AND VISUALIZER",
 L"LOCAL PLAYBACK OUTPUT",L"STREAMING CENTRE",L"STREAMING BROWSER CONTROLS",
 L"UPDATES",L"TORRENT STREAM",L"INTERFACE MENUS",L"ACCESSIBILITY, DISPLAY AND MOTION",
 L"SUPPORTED MEDIA AND REQUIREMENTS",L"ADVANCED SETTINGS",L"KEYBOARD SHORTCUTS",L"TROUBLESHOOTING"
};
#define HELP_ID_SEARCH 7101
#define HELP_ID_FIND 7102
#define HELP_ID_TOPICS 7103
/* Convert both real and accidentally escaped line endings into real paragraphs.
   Also break long prose at sentence boundaries so every topic reads as an article. */
static wchar_t *HelpNormalizeArticle(const wchar_t *src) {
 if(!src)return NULL;
 size_t n=wcslen(src),cap=n*2+32,out=0;
 wchar_t *dst=(wchar_t*)calloc(cap,sizeof(wchar_t));if(!dst)return NULL;
 size_t lineStart=0;
 for(size_t i=0;i<n;i++) {
  if(src[i]==L'\\'&&src[i+1]==L'r') {
   i++;if(src[i+1]==L'\\'&&src[i+2]==L'n')i+=2;
   if(out&&dst[out-1]!=L'\r'&&dst[out-1]!=L'\n'){dst[out++]=L'\r';dst[out++]=L'\n';}
   lineStart=out;continue;
  }
  if(src[i]==L'\\'&&src[i+1]==L'n') {
   i++;if(out&&dst[out-1]!=L'\r'&&dst[out-1]!=L'\n'){dst[out++]=L'\r';dst[out++]=L'\n';}
   lineStart=out;continue;
  }
  if(src[i]==L'\r'||src[i]==L'\n') {
   if(src[i]==L'\r'&&src[i+1]==L'\n')i++;
   if(out&&dst[out-1]!=L'\r'&&dst[out-1]!=L'\n'){dst[out++]=L'\r';dst[out++]=L'\n';}
   lineStart=out;continue;
  }
  if((src[i]==L'.'||src[i]==L'?'||src[i]==L'!')&&src[i+1]==L' ') {
   size_t next=i+2;while(src[next]==L' ')next++;
   if((src[next]>=L'A'&&src[next]<=L'Z')||(src[next]>=L'0'&&src[next]<=L'9')) {
    dst[out++]=src[i];dst[out++]=L'\r';dst[out++]=L'\n';i++;while(src[i+1]==L' ')i++;lineStart=out;continue;
   }
  }
  if(src[i]==L':'&&src[i+1]==L' ') {
   size_t next=i+2;while(src[next]==L' ')next++;
   size_t prefix=out-lineStart;
   if(prefix>0&&prefix<48&&((src[next]>=L'A'&&src[next]<=L'Z')||(src[next]>=L'0'&&src[next]<=L'9'))) {
    dst[out++]=L':';dst[out++]=L'\r';dst[out++]=L'\n';i++;while(src[i+1]==L' ')i++;lineStart=out;continue;
   }
  }
  dst[out++]=src[i];
 }
 while(out>0&&(dst[out-1]==L'\r'||dst[out-1]==L'\n'||dst[out-1]==L' '))out--;
 dst[out]=0;return dst;
}
/* Format the guide as an actual article: distinct title/section hierarchy,
   readable paragraph rhythm and consistent application typography. */
static void HelpFormatArticle(HWND edit) {
 if(!edit)return;
 SendMessageW(edit,EM_SETBKGNDCOLOR,0,ABM_UI_COLOR_SURFACE_RAISED);
 int len=GetWindowTextLengthW(edit);if(len<=0)return;
 CHARFORMAT2W cf;ZeroMemory(&cf,sizeof(cf));cf.cbSize=sizeof(cf);
 cf.dwMask=CFM_FACE|CFM_SIZE|CFM_COLOR|CFM_CHARSET|CFM_BOLD;
 wcscpy_s(cf.szFaceName,_countof(cf.szFaceName),ABM_UI_FONT_FAMILY_TEXT);
 cf.yHeight=(LONG)(ABM_UI_FONT_SIZE_D2D_BODY*15.0f);cf.crTextColor=ABM_UI_COLOR_TEXT_SECONDARY;cf.bCharSet=DEFAULT_CHARSET;
 SendMessageW(edit,EM_SETSEL,0,-1);
 SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&cf);
 PARAFORMAT2 pf;ZeroMemory(&pf,sizeof(pf));pf.cbSize=sizeof(pf);
 pf.dwMask=PFM_SPACEAFTER;
 pf.dySpaceAfter=180;
 SendMessageW(edit,EM_SETPARAFORMAT,0,(LPARAM)&pf);
 wchar_t *text=(wchar_t*)calloc((size_t)len+1,sizeof(wchar_t));if(!text)return;
 GetWindowTextW(edit,text,len+1);
 int pos=0;
 while(pos<len) {
  int end=pos;while(end<len&&text[end]!=L'\r'&&text[end]!=L'\n')end++;
  int lineLen=end-pos;
  if(lineLen>0) {
   BOOL heading=FALSE;
   for(int i=0;i<(int)_countof(g_helpTopicNames);i++) {
    size_t n=wcslen(g_helpTopicNames[i]);
    if((size_t)lineLen>=n&&_wcsnicmp(text+pos,g_helpTopicNames[i],n)==0){heading=TRUE;break;}
   }
   if(heading) {
    SendMessageW(edit,EM_SETSEL,pos,end);
    ZeroMemory(&cf,sizeof(cf));cf.cbSize=sizeof(cf);
    cf.dwMask=CFM_FACE|CFM_SIZE|CFM_BOLD|CFM_COLOR;
    wcscpy_s(cf.szFaceName,_countof(cf.szFaceName),ABM_UI_FONT_FAMILY_DISPLAY);
    cf.yHeight=(LONG)(ABM_UI_FONT_SIZE_D2D_HEADING*15.0f);cf.dwEffects=CFE_BOLD;cf.crTextColor=ABM_UI_COLOR_ACCENT_BRIGHT;
    SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&cf);
    SendMessageW(edit,EM_SETSEL,pos,end);
    ZeroMemory(&pf,sizeof(pf));pf.cbSize=sizeof(pf);
    pf.dwMask=PFM_SPACEBEFORE|PFM_SPACEAFTER;
    pf.dySpaceBefore=260;pf.dySpaceAfter=130;
    SendMessageW(edit,EM_SETPARAFORMAT,0,(LPARAM)&pf);
   }
  }
  while(end<len&&(text[end]==L'\r'||text[end]==L'\n'))end++;
  pos=end;
 }
 free(text);
 SendMessageW(edit,EM_SETSEL,0,0);
}
static void HelpNavigateToTopic(HWND hwnd,int index) {
 (void)hwnd;
 if(index<0||index>=(int)_countof(g_helpTopicNames)||!g_helpEdit||!g_helpDocument)return;
 const wchar_t *start=wcsstr(g_helpDocument,g_helpTopicNames[index]);
 if(!start)return;
 const wchar_t *end=g_helpDocument+wcslen(g_helpDocument);
 for(int i=0;i<(int)_countof(g_helpTopicNames);i++) {
  const wchar_t *next=wcsstr(start+wcslen(g_helpTopicNames[index]),g_helpTopicNames[i]);
  if(next&&next<end&&next<end)end=next;
 }
 g_helpArticleStart=(int)(start-g_helpDocument);
 g_helpArticleLength=(int)(end-start);
 wchar_t *raw=(wchar_t*)calloc((size_t)g_helpArticleLength+1,sizeof(wchar_t));if(!raw)return;
 wmemcpy(raw,start,(size_t)g_helpArticleLength);raw[g_helpArticleLength]=0;
 wchar_t *article=HelpNormalizeArticle(raw);free(raw);if(!article)return;
 SetWindowTextW(g_helpEdit,article);
 free(article);
 HelpFormatArticle(g_helpEdit);
 SendMessageW(g_helpEdit,EM_SETSEL,0,0);
 SendMessageW(g_helpEdit,EM_SCROLLCARET,0,0);
}
static BOOL HelpFindNext(HWND hwnd);
static LRESULT CALLBACK HelpSearchSubclass(HWND edit,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR ref) {
 (void)id;(void)ref;
 if(msg==WM_KEYDOWN&&wp==VK_RETURN){HelpFindNext(GetParent(edit));return 0;}
 return DefSubclassProc(edit,msg,wp,lp);
}
static BOOL HelpFindNext(HWND hwnd) {
 (void)hwnd;
 if(!g_helpSearch||!g_helpEdit||!g_helpDocument)return FALSE;
 wchar_t query[256]={0};GetWindowTextW(g_helpSearch,query,(int)_countof(query));
 if(!query[0])return FALSE;
 int len=(int)wcslen(g_helpDocument);if(len<=0)return FALSE;
 int start=g_helpSearchCursor;if(start<0||start>=len)start=0;
 const wchar_t *found=NULL;size_t qlen=wcslen(query);
 for(int pass=0;pass<2&&!found;pass++) {
  int begin=pass?0:start,end=pass?start:len;
  for(int i=begin;i<end;i++)if((size_t)(end-i)>=qlen&&_wcsnicmp(g_helpDocument+i,query,qlen)==0){found=g_helpDocument+i;break;}
 }
 if(!found)return FALSE;
 int docPos=(int)(found-g_helpDocument),topic=-1,lastHeading=-1;
 for(int i=0;i<(int)_countof(g_helpTopicNames);i++) {
  const wchar_t *h=wcsstr(g_helpDocument,g_helpTopicNames[i]);
  if(h&&(int)(h-g_helpDocument)<=docPos&&(int)(h-g_helpDocument)>lastHeading){lastHeading=(int)(h-g_helpDocument);topic=i;}
 }
 if(topic>=0) {
  SendMessageW(g_helpTopics,LB_SETCURSEL,topic,0);
  HelpNavigateToTopic(hwnd,topic);
 }
 int articleLen=GetWindowTextLengthW(g_helpEdit);
 wchar_t *visible=(wchar_t*)calloc((size_t)articleLen+1,sizeof(wchar_t));
 if(visible) {
  GetWindowTextW(g_helpEdit,visible,articleLen+1);
  wchar_t *match=NULL;
  for(int i=0;i<articleLen;i++)if((size_t)(articleLen-i)>=qlen&&_wcsnicmp(visible+i,query,qlen)==0){match=visible+i;break;}
  if(match){int p=(int)(match-visible);SendMessageW(g_helpEdit,EM_SETSEL,p,p+(int)qlen);SendMessageW(g_helpEdit,EM_SCROLLCARET,0,0);}
  free(visible);
 }
 g_helpSearchCursor=docPos+(int)qlen;
 return TRUE;
}
static LRESULT CALLBACK HelpWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_THEMECHANGED:
        if(g_helpBrush){DeleteObject(g_helpBrush);g_helpBrush=CreateSolidBrush(ABM_UI_COLOR_SURFACE);}
        if(g_helpInputBrush){DeleteObject(g_helpInputBrush);g_helpInputBrush=CreateSolidBrush(ABM_UI_COLOR_INPUT);}
        if(g_helpArticleBrush){DeleteObject(g_helpArticleBrush);g_helpArticleBrush=CreateSolidBrush(ABM_UI_COLOR_SURFACE_RAISED);}
        if(g_helpNavBrush){DeleteObject(g_helpNavBrush);g_helpNavBrush=CreateSolidBrush(ABM_UI_COLOR_SURFACE);}
        if(g_helpEdit)SendMessageW(g_helpEdit,EM_SETBKGNDCOLOR,0,ABM_UI_COLOR_SURFACE_RAISED);
        InvalidateRect(hwnd,NULL,FALSE);return 0;
    case WM_CREATE: {
        g_helpCanvasReady=ABM_D2D_CanvasInit(&g_helpCanvas);
        const wchar_t *help=
            L"ABM AV PLAYER  |  COMPLETE USER GUIDE\r\n"
            L"\r\n"
            L"GETTING STARTED\r\n"
            L"Choose Open media or press Ctrl+O, then select one or more audio or video files. You can also drag files from File Explorer into the player. Added files appear in the playlist. Double-click a playlist row to play it. The playlist contains references to your files; removing an entry never deletes the original file.\r\n"
            L"\r\n"
            L"PLAYBACK AND FULLSCREEN\r\n"
            L"Play/Pause starts or pauses the selected item. Stop ends playback. Previous and Next move through the playlist. Drag the timeline to seek and the volume control to change loudness. Double-click the video or use F11 to toggle fullscreen. In fullscreen, move the pointer to reveal the playback overlay; drag the timeline to seek. Press Esc or F11 to return to the window.\r\n"
            L"\r\n"
            L"PLAYLISTS AND RECENT MEDIA\r\n"
            L"Add multiple files, double-click an item to play, and right-click a playlist row for item actions. Load Playlist and Save Playlist use M3U files. Recent media opens the recent-items list. Shuffle changes next-item order; Repeat current repeats the active item. The playlist supports up to 256 entries. Shuffle, repeat, playback speed and recent/playlist state are saved for your Windows user.\r\n"
            L"\r\n"
            L"LIBRARY, CONTINUE WATCHING AND RESUME\r\n"
            L"Use the Library view selector to cycle All tracks, Favourites and Continue Watching. Continue Watching lists items with a saved playback position and shows an approximate progress percentage and remaining time. Resume is automatic: when you play an item with a saved checkpoint, ABM AV Player seeks after LibVLC reports that playback has started. Right-click a media row to choose Continue from checkpoint or Start from beginning. Starting from the beginning clears the old checkpoint only after playback successfully starts.\r\n"
            L"Playback position is checkpointed periodically and around pause, stop, media changes and application shutdown. Very early positions are ignored; an item at approximately 95 percent completion is treated as finished and removed from Continue Watching. Checkpoints are stored locally for the current Windows user in %APPDATA%\\\\ABMAVPlayer\\\\resume.tsv; they are not written into the media file.\r\n"
            L"\r\n"
            L"MISSING MEDIA AND RELINKING\r\n"
            L"Playlist and saved-library entries are file-path references. If a file is moved, renamed, disconnected or deleted, its entry is retained and flagged MISSING MEDIA rather than silently removed. Open the Library/Queue drawer to refresh availability, then right-click the missing row and choose Relink missing media. Select the replacement file in the Windows file picker. Cancel leaves the original reference unchanged.\r\n"
            L"Relinking updates matching references in the active playlist, Favourites, Recent media, resume records and saved personal M3U collections. It changes the stored path; it does not copy, move, rename or repair the file. Choose the correct replacement, because a different edit or episode may not match the saved resume position. Reconnect removable drives or network shares and reopen the drawer to refresh their status.\r\n"            L"Right-click any media row in Queue or Library and choose Open file location to open its containing folder in Windows File Explorer with the media file selected. The action is disabled for missing files; use Relink missing media first.\r\n"
            L"\r\n"
            L"LICENSE & ACTIVATION\r\n"
            L"If you begin with the 10-day trial, you can activate at any time without reinstalling. Open the Command Centre and choose License & Activation, enter the universal 16-character key, and select Activate. The player confirms successful activation and remembers it for future launches.\r\n"
            L"\r\n"
            L"APPLICATION SETTINGS\r\n"
            L"Open Settings from the Command Centre to configure the player in one dedicated window. General includes volume, playback speed, shuffle and repeat defaults. Audio includes ReplayGain normalisation and the dynamic-range compressor. Torrent & Storage lets you choose the cache parent folder and set a cache quota from 1 to 4096 GiB. The player creates a torrent-cache subfolder inside the selected parent. Network controls set torrent download and upload limits in KiB/s; enter 0 for unlimited. Updates provides a manual check for the latest public GitHub release. Choose Save settings to persist your changes.\r\n"
            L"On a new installation, the cache parent defaults to the current Windows user Downloads known folder, inside an ABM AV Player folder. Redirected Downloads folders are respected. Existing installations with a legacy cache keep using that location unless you choose another. Changing the cache parent while Torrent Stream is running requires restarting ABM AV Player. Changing the location does not automatically move existing cache data; existing files remain in the old folder. Settings are stored in %APPDATA%\\ABMAVPlayer\\settings.ini; audio processing preferences are also stored in audio.ini.\r\n"
            L"\r\n"
            L"The Settings window is resizable and keeps its full virtual page reachable on shorter or narrower displays. When content extends beyond the visible area, use the slim ABM-styled scrollbars: drag a thumb or click its track to move vertically or horizontally. The mouse wheel scrolls vertically; Page Up and Page Down move by a page, and Ctrl+Home and Ctrl+End move to the beginning or end. Native text-entry fields retain normal Windows caret, selection and clipboard behaviour. The scrollbars are custom-painted with the shared ABM theme rather than native Windows scrollbar controls.\r\n"
            L"\r\n"
            L"AUDIO, VIDEO AND SUBTITLES\r\n"
            L"Right-click the video surface during playback for media actions. Subtitle Manager opens the subtitle controls: choose an embedded track, disable subtitles, load an external SRT, ASS, SSA, SUB or VTT file, adjust timing and personalize appearance. Advanced Audio Centre brings together embedded audio-track selection, the output-engine preference, ReplayGain, dynamic-range compression, audio delay and the 10-band equalizer. Track and processing availability depends on the media and VLC plugins.\r\n"
            L"\r\n"
            L"ADVANCED EQUALIZER AND AUDIO PROCESSING\r\n"
            L"Open Advanced Audio Centre from the playback surface right-click menu. It provides Automatic/WASAPI/DirectSound output-engine selection (restart required), ReplayGain and compressor toggles, saved audio synchronization in 50 ms steps, and a direct link to the 10-band graphic equalizer. The ABM EQ offers Flat, Bass boost, Vocal clarity and Treble boost profiles; these are fixed-band curves, not a parametric EQ. ReplayGain needs suitable track metadata. The compressor is a dynamic-range effect, not a guaranteed true-peak limiter. Audio preferences are saved in audio.ini. Ctrl+E still opens the standalone equalizer directly.\r\n"
            L"\r\n"
            L"NOW PLAYING AND VISUALIZER\r\n"
            L"When audio is playing, now-playing metadata and a live spectrum/visualizer can be shown when the media and playback data provide it. The visualizer currently captures the Windows default render mix, so audio from other applications may also affect the display. Process-specific capture is not implemented.\r\n"
            L"\r\n"
            L"LOCAL PLAYBACK OUTPUT — FIND TVs / CAST TO TV\r\n"
            L"Open the Media / Settings drawer, expand Playback Output, choose Find TVs / Find network devices and wait for discovery. Select a receiver before playback or while a file is playing. Choose Local playback to return to the PC. This local-player output uses LibVLC renderer discovery; compatible Google Cast / Chromecast receivers and other renderers exposed by the installed VLC build may appear. A full 64-bit VLC runtime with renderer-discovery and Chromecast plugins is required for Google Cast. Keep the PC and TV on the same non-guest LAN and allow ABM AV Player/VLC through Windows Defender Firewall on Private networks. VPNs, guest Wi-Fi, router client isolation or blocked mDNS can prevent discovery. Android TV branding alone does not guarantee Google Cast support.\r\n"
            L"\r\n"
            L"STREAMING CENTRE — ONLINE SERVICES AND TV CASTING\r\n"
            L"Open Streaming services from the Command Centre or press Ctrl+Shift+S. Choose YouTube, Netflix, Prime Video, JioHotstar, Sony LIV or ZEE5. These are the providers' official websites inside Microsoft Edge WebView2. Sign in on the provider's website; ABM AV Player does not collect or proxy passwords. The browser profile is stored locally under %LOCALAPPDATA%\\ABMAVPlayer\\WebView2, so supported sessions can persist.\r\n"
            L"The Streaming Centre has its own Find TVs device panel and Cast video action. It discovers Google Cast, DLNA/UPnP and DIAL receivers; it is separate from local-media playback and its receiver selection does not change the main player's output. Select a TV in the panel, then choose Cast video. YouTube video handoff uses a TV's Google Cast V2 or DIAL YouTube receiver; generic direct video URLs may be sent through a compatible Cast/DLNA route. Check TV apps probes Google Cast app IDs for YouTube (233637DE) and an experimental Prime Video receiver (17608BC8), plus DIAL application endpoints. The app selector also offers clearly labelled DIAL launch attempts for Netflix, SonyLIV and ZEE5 when the selected TV exposes DIAL, even if its GET status endpoint is unavailable; the TV may reject an unsupported app name. Launch app opens the selected TV app only and does not transfer a browser title or provider session. Netflix and Prime Video title handoff from the desktop browser is not supported. YouTube title handoff remains supported through Google Cast V2/DIAL. Protected, encrypted, segmented or otherwise non-direct streams are not extracted or relayed. Receiver compatibility varies.\r\n"
            L"\r\n"
            L"STREAMING BROWSER CONTROLS AND LIMITATIONS\r\n"
            L"Back/Forward navigate browser history; Reload refreshes the page; Home returns to the service start page; Fullscreen requests provider/browser fullscreen; Open in browser opens the current page in your default desktop browser. Previous/Next browser buttons are intended for supported YouTube playlist navigation. WebView2 can recover from some browser-process failures and attempt to restore the current page.\r\n"
            L"Protected playback depends on provider DRM, browser restrictions, device certification, region and WebView2 runtime. A subscription does not guarantee embedded playback or TV handoff. Use the provider's official browser/app if playback is blocked. The player does not extract protected streams or bypass DRM.\r\n"
            L"\r\n"
            L"UPDATES\r\n"
            L"Choose Check for updates in the Command Centre. The player checks the latest public GitHub Release, shows release notes and asks before downloading. The installer is checked against the SHA-256 digest supplied by GitHub; an absent or mismatched digest blocks installation. No GitHub account or code-signing certificate is required. Windows may still display SmartScreen warnings for an unsigned installer. Approve the normal Windows elevation prompt to install.\r\n"
            L"\r\n"
            L"TORRENT STREAM — DOWNLOAD, PLAY AND CAST\r\n"
            L"Open Torrent Stream from the main navigation or use Ctrl+Shift+T. Add a magnet URI or open a local .torrent file, wait for metadata, then select a media file. Play / Stream starts progressive playback while the selected file continues downloading to the configured Permanent downloads folder. The quota-managed torrent-cache is a separate working cache; changing its parent does not move existing data. Use Open Downloads Folder to inspect downloaded files. Find TVs discovers compatible DLNA MediaRenderer devices; select one and choose Cast to TV. The TV fetches the same persistent download over a temporary authenticated private-LAN connection while downloading continues. Stop TV stops the receiver/listener but does not delete the file or stop the torrent task. This is direct play: the TV must support the media container and codecs; PC-side transcoding is not provided. Only download content you have the right to access.\r\n"
            L"INTERFACE MENUS, DIALOGS AND KEYBOARD ACCESS\r\n"
            L"Application-owned action menus, recent-media lists, alerts and confirmations use ABM-styled dark surfaces rather than legacy-looking Windows popup menus or message boxes. Standard Windows file-open/save pickers and Windows security/UAC prompts remain system-owned. In custom action menus, use Up/Down to move, Enter to choose and Esc to close; long menus can be scrolled with the mouse wheel. In branded Yes/No dialogs, use Tab or Left/Right to change the focused choice, then Enter or Space to activate it. Esc cancels and follows the safe No/cancel path. The Streaming Centre device panel supports Tab/Shift+Tab navigation between its action buttons and lists; visible focus indicates the current action.\r\n"
            L"ACCESSIBILITY, DISPLAY AND MOTION\r\n"
            L"Keyboard focus and activation are implemented for branded modal buttons, custom action menus and Streaming Centre device actions. This is not yet a complete screen-reader/UI Automation or high-contrast certification; some custom-painted controls may not expose full accessible names and roles. Mixed-DPI monitor transitions also need additional validation. Home dashboard cards have restrained hover feedback while idle; this is visual feedback only and does not alter playback, decoding, casting or torrent transfers. See UI_QA_CHECKLIST.md in the project documentation for the Windows validation matrix.\r\n"
            L"\r\n"
            L"SUPPORTED MEDIA AND REQUIREMENTS\r\n"
            L"Common audio formats include MP3, WAV, FLAC, AAC, M4A and OGG. Common video formats include MP4, MKV, AVI, MOV, WMV and WebM. Actual codec support depends on the installed 64-bit VLC runtime and plugins. Streaming Centre requires Microsoft Edge WebView2 Evergreen Runtime.\r\n"
            L"\r\n"
            L"ADVANCED SETTINGS\r\n"
            L"Open Command Centre > Settings > Advanced. Overview is a live read-only snapshot; it does not change preferences. Video & Decoding: Automatic lets LibVLC choose; Software requests CPU decoding; D3D11VA requests Direct3D 11 hardware decoding. Apply and restart; ABM_VLC_HW environment override takes precedence. Audio Engine: Automatic, WASAPI or DirectSound selects the LibVLC output module, not a device picker or bit-perfect mode; restart after applying. Video Output: Original, 16:9, 4:3 or 21:9 changes display geometry, not the media file. Subtitles: +/-50 ms adjusts sync; size, colour, bold, outline and margin affect appearance on newly opened media, and embedded styling may override them. Streaming & Network: Network, File and Live cache targets cycle 300/1000/2000/5000 ms and apply to newly opened media; more cache may reduce jitter but adds startup delay and live latency. Torrent quota and bandwidth limits remain in Main Settings. Performance: Automatic/2/4/8 decoder threads, Drop late frames and Skip frames affect decoding workload and responsiveness; they are not a global CPU limit and may reduce visual continuity. Reopen media to compare. Controls & Behaviour: exit and clear-playlist confirmations add prompts; clearing a playlist never deletes source files; the double-click option controls fullscreen toggling. Diagnostics is read-only: it separates system CPU/RAM/load from this process's working set and page-file use, and shows playback state, video dimensions and playlist/recent counts. Generate report creates a timestamped local UTF-8 TXT file and opens it in a read-only preview; Open data folder opens its location. Reports are not uploaded. Review machine details, paths and playback/session information before sharing. VLC Runtime & Codecs is a separate read-only audit. File Found means a plugin DLL exists; Plugin Loaded means it is mapped in this process; Runtime Tested means LibVLC and the local player initialized; Playback Verified means the current media reached Playing when refreshed. VLC loads plugins on demand, so Not Loaded is not automatically an error. Playback Verified is session evidence, not certification of every codec/profile or hardware path. Play representative media and use Refresh scan. See USER_GUIDE.md and SETTINGS.md for the full reference.\r\n"
            L"\r\n"
            L"KEYBOARD SHORTCUTS\r\n"
            L"Ctrl+O  Open media     |     Ctrl+D  Find network devices\r\n"
            L"Ctrl+Shift+C  Start local-player receiver discovery     |     Ctrl+Shift+S  Open Streaming Centre\r\n"
            L"Space  Play/Pause     |     Left/Right  Seek 10 seconds     |     Home  Seek to beginning\r\n"
            L"N / P  Next/Previous item     |     + / -  Change playback speed     |     F11  Fullscreen\r\n"
            L"Ctrl+L  Load playlist     |     Ctrl+S  Save playlist     |     Ctrl+J  Recent media\r\n"
            L"Ctrl+E  Advanced equalizer     |     Ctrl+Shift+H  Shuffle     |     Ctrl+R  Repeat current\r\n"
            L"Ctrl+Shift+T  Torrent Stream     |     Ctrl+Shift+S  Streaming Centre\r\n"
            L"F1  This help     |     Ctrl+I  About     |     Alt+F4  Exit     |     Esc  Close menu/leave fullscreen\r\n"
            L"\r\n"
            L"TROUBLESHOOTING\r\n"
            L"If a file will not play, confirm it exists and that VLC includes its codec/plugin. If a TV is not discovered, check same-network connectivity, mDNS/multicast, VPN, router isolation and Windows Firewall. If a TV appears but casting fails, try a common MP4/H.264 file and confirm the receiver supports the selected protocol. Protected streaming services may block embedded playback or handoff; try Open in browser or the provider's official app.\r\n"
            L"\r\n"
            L"ABM AV Player is a desktop media player built on LibVLC. Streaming Centre web playback is separate from local media playback and does not provide subscriptions or bypass provider protections.";

        if(!g_helpBrush)g_helpBrush=CreateSolidBrush(ABM_UI_COLOR_SURFACE);
        if(!g_helpInputBrush)g_helpInputBrush=CreateSolidBrush(ABM_UI_COLOR_INPUT);
        if(!g_helpArticleBrush)g_helpArticleBrush=CreateSolidBrush(ABM_UI_COLOR_SURFACE_RAISED);
        if(!g_helpNavBrush)g_helpNavBrush=CreateSolidBrush(ABM_UI_COLOR_SURFACE);
        if(!g_helpTitleFont)g_helpTitleFont=g_headingFont;
        if(!g_helpSectionFont)g_helpSectionFont=g_smallFont;
        if(!g_helpBodyFont)g_helpBodyFont=g_font;
        if(!g_helpMetaFont)g_helpMetaFont=g_smallFont;
        /* Branded Help Centre workspace: borderless native text editing retains
           keyboard, selection and clipboard behaviour while the host paints the chrome. */
        g_helpSearch=CreateWindowExW(0,L"EDIT",L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|ES_LEFT,32,74,760,40,hwnd,
            (HMENU)HELP_ID_SEARCH,GetModuleHandleW(NULL),NULL);
        g_helpFindButton=CreateWindowExW(0,L"BUTTON",L"SEARCH",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,806,74,132,40,hwnd,
            (HMENU)HELP_ID_FIND,GetModuleHandleW(NULL),NULL);
        g_helpTopics=CreateWindowExW(0,L"LISTBOX",L"",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_HASSTRINGS|LBS_OWNERDRAWFIXED,
            32,146,286,500,hwnd,(HMENU)HELP_ID_TOPICS,GetModuleHandleW(NULL),NULL);
        g_helpDocument=help;
        if(!g_helpRichEditModule)g_helpRichEditModule=LoadLibraryW(L"Msftedit.dll");
        g_helpEdit=CreateWindowExW(0,L"RICHEDIT50W",L"",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_LEFT|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|ES_NOHIDESEL,
            338,146,600,500,hwnd,(HMENU)1,GetModuleHandleW(NULL),NULL);
        if(g_helpEdit){
            SendMessageW(g_helpEdit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELONG(26,26));
            SendMessageW(g_helpEdit,EM_SETOPTIONS,ECOOP_OR,ECO_AUTOWORDSELECTION);
            HelpFormatArticle(g_helpEdit);
        }
        if(g_helpSearch){SendMessageW(g_helpSearch,WM_SETFONT,(WPARAM)g_helpBodyFont,TRUE);SendMessageW(g_helpSearch,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELONG(16,16));SetWindowSubclass(g_helpSearch,HelpSearchSubclass,1,0);}
        if(g_helpSearch)SendMessageW(g_helpSearch,EM_SETCUEBANNER,TRUE,(LPARAM)L"Search all help topics  ·  e.g. subtitles, buffering, D3D11VA, torrent cache");
        if(g_helpTopics){
            SendMessageW(g_helpTopics,WM_SETFONT,(WPARAM)g_helpBodyFont,TRUE);
            SendMessageW(g_helpTopics,LB_SETITEMHEIGHT,0,40);
            SetWindowTheme(g_helpTopics,L"DarkMode_Explorer",NULL);
        }
        if(g_helpSearch)SetWindowTheme(g_helpSearch,L"DarkMode_Explorer",NULL);
        if(g_helpEdit)SetWindowTheme(g_helpEdit,L"DarkMode_Explorer",NULL);
        if(g_helpFindButton)SendMessageW(g_helpFindButton,WM_SETFONT,(WPARAM)g_helpMetaFont,TRUE);
        for(int ti=0;ti<(int)(sizeof(g_helpTopicNames)/sizeof(g_helpTopicNames[0]));ti++)
            SendMessageW(g_helpTopics,LB_ADDSTRING,0,(LPARAM)g_helpTopicNames[ti]);
        SendMessageW(g_helpTopics,LB_SETCURSEL,0,0);
        HelpNavigateToTopic(hwnd,0);
        RegisterHotKey(hwnd,1,MOD_CONTROL,'F');RegisterHotKey(hwnd,2,0,VK_F3);
        return 0;
    }
    case WM_SIZE: {
        int w=LOWORD(lp),h=HIWORD(lp);
        int margin=24,searchButtonW=(w<620?104:132),searchY=74,searchH=40;
        int searchWidth=w-(margin*2)-searchButtonW-12;if(searchWidth<120)searchWidth=120;
        if(g_helpSearch)MoveWindow(g_helpSearch,margin,searchY,searchWidth,searchH,TRUE);
        if(g_helpFindButton)MoveWindow(g_helpFindButton,w-margin-searchButtonW,searchY,searchButtonW,searchH,TRUE);
        if(w<760){int topicH=h<620?116:156,topicY=132,articleY=topicY+topicH+12,articleH=h-articleY-58;if(articleH<120)articleH=120;
            if(g_helpTopics)MoveWindow(g_helpTopics,margin,topicY,w-margin*2,topicH,TRUE);
            if(g_helpEdit)MoveWindow(g_helpEdit,margin,articleY,w-margin*2,articleH,TRUE);
        }else{int contentH=h-226;if(contentH<150)contentH=150;int top=146,topicW=(w<900?220:270),gap=16;
            if(g_helpTopics)MoveWindow(g_helpTopics,margin,top,topicW,contentH,TRUE);
            if(g_helpEdit)MoveWindow(g_helpEdit,margin+topicW+gap,top,w-(margin+topicW+gap)-margin,contentH,TRUE);
        }
        InvalidateRect(hwnd,NULL,FALSE);
        return 0;
    }
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT rc;GetClientRect(hwnd,&rc);
        if(g_helpCanvasReady&&ABM_D2D_CanvasBegin(&g_helpCanvas,dc,&rc)){
            ABM_D2D_CanvasFill(&g_helpCanvas,0,0,(float)rc.right,(float)rc.bottom,ABM_UI_COLOR_BACKGROUND);
            /* The window now uses the same native dark DWM caption as the
               Main Player. Keep only the compact branded context strip here. */
            ABM_D2D_CanvasFill(&g_helpCanvas,0,0,(float)rc.right,62,ABM_UI_COLOR_SURFACE_BLUE);
            RECT kicker={34,7,rc.right-28,29};ABM_D2D_CanvasText(&g_helpCanvas,L"ABM AV PLAYER   /   KNOWLEDGE & SUPPORT",kicker,ABM_UI_COLOR_ACCENT_BRIGHT,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT intro={34,31,rc.right-28,57};ABM_D2D_CanvasText(&g_helpCanvas,L"Find answers, learn features and understand the practical impact of settings.",intro,ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_BODY,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            ABM_D2D_CanvasFill(&g_helpCanvas,0,60,(float)rc.right,63,ABM_UI_COLOR_ACCENT);
            /* Draw panel chrome from actual child bounds so cards stay aligned
               in both side-by-side and narrow stacked layouts. */
            HWND panels[3]={g_helpSearch,g_helpTopics,g_helpEdit};
            for(int pi=0;pi<3;pi++)if(panels[pi]&&IsWindowVisible(panels[pi])){
                RECT pr;GetWindowRect(panels[pi],&pr);MapWindowPoints(HWND_DESKTOP,hwnd,(POINT*)&pr,2);
                InflateRect(&pr,8,8);
                ABM_D2D_CanvasFillRound(&g_helpCanvas,(float)pr.left,(float)pr.top,(float)pr.right,(float)pr.bottom,(float)ABM_UI_RADIUS_CARD,ABM_UI_COLOR_SURFACE);
                ABM_D2D_CanvasStrokeRound(&g_helpCanvas,(float)pr.left+0.5f,(float)pr.top+0.5f,(float)pr.right-0.5f,(float)pr.bottom-0.5f,(float)ABM_UI_RADIUS_CARD,ABM_UI_COLOR_BORDER,1.0f);
            }
            ABM_D2D_CanvasFill(&g_helpCanvas,0,(float)rc.bottom-48,(float)rc.right,(float)rc.bottom,ABM_UI_COLOR_SURFACE);
            ABM_D2D_CanvasLine(&g_helpCanvas,0,(float)rc.bottom-48,(float)rc.right,(float)rc.bottom-48,ABM_UI_COLOR_BORDER,1.0f);
            RECT foot={26,rc.bottom-42,rc.right-24,rc.bottom-12};ABM_D2D_CanvasText(&g_helpCanvas,L"TIP   Ctrl+F search   •   Find next / F3 next match   •   Select a topic to jump",foot,ABM_UI_COLOR_TEXT_MUTED,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            ABM_D2D_CanvasEnd(&g_helpCanvas);
        }else{HBRUSH bg=CreateSolidBrush(ABM_UI_COLOR_BACKGROUND);FillRect(dc,&rc,bg);DeleteObject(bg);}
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi=(MEASUREITEMSTRUCT*)lp;
        if(mi&&mi->CtlID==HELP_ID_TOPICS){mi->itemHeight=40;return TRUE;}
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di=(DRAWITEMSTRUCT*)lp;
        if(!di)break;
        if(di->CtlID==HELP_ID_FIND&&g_helpCanvasReady){
            BOOL pressed=(di->itemState&ODS_SELECTED)!=0,focused=(di->itemState&ODS_FOCUS)!=0;
            RECT bounds={0,0,di->rcItem.right-di->rcItem.left,di->rcItem.bottom-di->rcItem.top};
            if(ABM_D2D_CanvasBegin(&g_helpCanvas,di->hDC,&di->rcItem)){
                COLORREF fill=pressed?ABM_THEME_COLOR_197065:ABM_THEME_COLOR_185B52;
                COLORREF edge=focused?ABM_THEME_COLOR_63E1CD:ABM_THEME_COLOR_41BEA5;
                ABM_D2D_CanvasFill(&g_helpCanvas,0,0,(float)bounds.right,(float)bounds.bottom,ABM_THEME_COLOR_0C1D2D);
                RECT face=bounds;InflateRect(&face,-1,-1);
                ABM_D2D_CanvasFillRound(&g_helpCanvas,(float)face.left,(float)face.top,(float)face.right,(float)face.bottom,(float)ABM_UI_RADIUS_BUTTON,fill);
                ABM_D2D_CanvasStrokeRound(&g_helpCanvas,0.5f,0.5f,(float)bounds.right-0.5f,(float)bounds.bottom-0.5f,(float)ABM_UI_RADIUS_BUTTON,edge,focused?2.0f:1.0f);
                wchar_t caption[64]=L"SEARCH";if(g_helpFindButton)GetWindowTextW(g_helpFindButton,caption,(int)_countof(caption));
                RECT label=bounds;InflateRect(&label,-8,-2);
                ABM_D2D_CanvasText(&g_helpCanvas,caption,label,ABM_THEME_COLOR_E5F4FA,ABM_D2D_TEXT_SMALL,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                ABM_D2D_CanvasEnd(&g_helpCanvas);return TRUE;
            }
        }
        if(di->CtlID==HELP_ID_TOPICS&&g_helpCanvasReady){
            wchar_t label[256]={0};if(di->itemID!=(UINT)-1)SendMessageW(di->hwndItem,LB_GETTEXT,di->itemID,(LPARAM)label);
            BOOL selected=(di->itemState&ODS_SELECTED)!=0,hot=(di->itemState&ODS_HOTLIGHT)!=0;
            /* BindDC clips to rcItem; draw in item-local coordinates. Reusing
               rcItem.top here offsets each row twice, hiding every row after the first. */
            RECT bounds={0,0,di->rcItem.right-di->rcItem.left,di->rcItem.bottom-di->rcItem.top};
            if(ABM_D2D_CanvasBegin(&g_helpCanvas,di->hDC,&di->rcItem)){
                RECT row=bounds;InflateRect(&row,-6,-2);
                ABM_D2D_CanvasFillRound(&g_helpCanvas,(float)row.left,(float)row.top,(float)row.right,(float)row.bottom,(float)ABM_UI_RADIUS_CONTROL,selected?ABM_THEME_COLOR_163B48:(hot?ABM_UI_COLOR_SURFACE_BLUE:ABM_UI_COLOR_SURFACE));
                RECT textRect=row;textRect.left+=12;textRect.right-=8;
                ABM_D2D_CanvasText(&g_helpCanvas,label,textRect,selected?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_TEXT_SECONDARY,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                if(di->itemState&ODS_FOCUS)ABM_D2D_CanvasStrokeRound(&g_helpCanvas,(float)row.left+0.5f,(float)row.top+0.5f,(float)row.right-0.5f,(float)row.bottom-0.5f,(float)ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_FOCUS,1.0f);
                ABM_D2D_CanvasEnd(&g_helpCanvas);return TRUE;
            }
        }
        if(di->CtlID==HELP_ID_FIND){
            RECT r=di->rcItem;BOOL pressed=(di->itemState&ODS_SELECTED)!=0,focused=(di->itemState&ODS_FOCUS)!=0;
            COLORREF fill=pressed?ABM_THEME_COLOR_187E74:ABM_UI_COLOR_ACCENT;
            HBRUSH b=CreateSolidBrush(fill);HPEN p=CreatePen(PS_SOLID,focused?2:1,focused?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_BORDER_STRONG);
            HGDIOBJ ob=SelectObject(di->hDC,b),op=SelectObject(di->hDC,p);
            RoundRect(di->hDC,r.left+1,r.top+1,r.right-1,r.bottom-1,12,12);
            SelectObject(di->hDC,op);SelectObject(di->hDC,ob);DeleteObject(p);DeleteObject(b);
            SetBkMode(di->hDC,TRANSPARENT);SetTextColor(di->hDC,ABM_THEME_COLOR_F7FAFF);
            HFONT f=(HFONT)SelectObject(di->hDC,g_helpMetaFont);
            RECT label=r;InflateRect(&label,-8,-2);wchar_t caption[64]=L"SEARCH  →";
            if(g_helpFindButton)GetWindowTextW(g_helpFindButton,caption,(int)_countof(caption));
            DrawTextW(di->hDC,caption,-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            SelectObject(di->hDC,f);return TRUE;
        }
        if(di->CtlID==HELP_ID_TOPICS){
            wchar_t label[256]={0};
            if(di->itemID!=(UINT)-1)SendMessageW(di->hwndItem,LB_GETTEXT,di->itemID,(LPARAM)label);
            BOOL selected=(di->itemState&ODS_SELECTED)!=0;
            RECT row=di->rcItem;InflateRect(&row,-6,-2);
            HBRUSH b=CreateSolidBrush(selected?ABM_THEME_COLOR_163B48:ABM_UI_COLOR_SURFACE);
            FillRect(di->hDC,&row,b);DeleteObject(b);
            if(selected){RECT mark={row.left,row.top+7,row.left+3,row.bottom-7};b=CreateSolidBrush(ABM_THEME_COLOR_52CDE0);FillRect(di->hDC,&mark,b);DeleteObject(b);}
            SetBkMode(di->hDC,TRANSPARENT);
            SetTextColor(di->hDC,selected?ABM_THEME_COLOR_F5F8FF:ABM_UI_COLOR_TEXT_SECONDARY);
            HFONT f=(HFONT)SelectObject(di->hDC,g_helpBodyFont);
            RECT t=row;t.left+=18;t.right-=10;
            DrawTextW(di->hDC,label,-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            SelectObject(di->hDC,f);return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        int id=LOWORD(wp),code=HIWORD(wp);
        if(id==HELP_ID_FIND&&code==BN_CLICKED){BOOL found=HelpFindNext(hwnd);SetWindowTextW(g_helpFindButton,found?L"Find next":L"No match");SetFocus(g_helpSearch);return 0;}
        if(id==HELP_ID_SEARCH&&code==EN_CHANGE){g_helpSearchCursor=0;if(g_helpFindButton)SetWindowTextW(g_helpFindButton,L"Find next");return 0;}
        if(id==HELP_ID_TOPICS&&code==LBN_SELCHANGE){int sel=(int)SendMessageW(g_helpTopics,LB_GETCURSEL,0,0);HelpNavigateToTopic(hwnd,sel);return 0;}
        break;
    }
    case WM_HOTKEY:
        if(wp==1){SetFocus(g_helpSearch);SendMessageW(g_helpSearch,EM_SETSEL,0,-1);return 0;}
        if(wp==2){BOOL found=HelpFindNext(hwnd);if(g_helpFindButton)SetWindowTextW(g_helpFindButton,found?L"Find next":L"No match");return 0;}
        break;
    case WM_NCHITTEST:
        /* Use the real Windows caption hit-test and system close button,
           exactly as the Main Player does; no simulated caption in client area. */
        return DefWindowProcW(hwnd,msg,wp,lp);
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX: {
        HDC dc=(HDC)wp;
        if((HWND)lp==g_helpSearch){SetTextColor(dc,ABM_UI_COLOR_TEXT);SetBkColor(dc,ABM_UI_COLOR_INPUT);return (LRESULT)g_helpInputBrush;}
        if((HWND)lp==g_helpEdit){SetTextColor(dc,ABM_UI_COLOR_TEXT_SECONDARY);SetBkColor(dc,ABM_UI_COLOR_SURFACE_RAISED);return (LRESULT)g_helpArticleBrush;}
        SetTextColor(dc,ABM_UI_COLOR_TEXT_SECONDARY);SetBkColor(dc,ABM_UI_COLOR_SURFACE);
        return (LRESULT)g_helpNavBrush;
    }
    case WM_KEYDOWN: if(wp==VK_ESCAPE){DestroyWindow(hwnd);return 0;} break;
    case WM_DESTROY:
        if(g_helpCanvasReady){g_helpCanvasReady=FALSE;ABM_D2D_CanvasDestroy(&g_helpCanvas);}
        UnregisterHotKey(hwnd,1);UnregisterHotKey(hwnd,2);
        if(g_helpSearch)RemoveWindowSubclass(g_helpSearch,HelpSearchSubclass,1);
        if(g_helpBrush){DeleteObject(g_helpBrush);g_helpBrush=NULL;}
        if(g_helpInputBrush){DeleteObject(g_helpInputBrush);g_helpInputBrush=NULL;}
        if(g_helpArticleBrush){DeleteObject(g_helpArticleBrush);g_helpArticleBrush=NULL;}
        if(g_helpNavBrush){DeleteObject(g_helpNavBrush);g_helpNavBrush=NULL;}
        g_helpEdit=NULL;g_helpSearch=NULL;g_helpTopics=NULL;g_helpFindButton=NULL;
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static void ShowHelpDialog(HWND owner) {
    static BOOL registered=FALSE;HINSTANCE inst=GetModuleHandleW(NULL);
    if(!registered) {
        WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.lpfnWndProc=HelpWndProc;
        wc.hInstance=inst;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);
        wc.hbrBackground=CreateSolidBrush(ABM_THEME_COLOR_070C15);wc.lpszClassName=L"ABMAVPlayerHelp";
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
        registered=TRUE;
    }
    int w=1120,h=840;
    RECT ownerRect;GetWindowRect(owner,&ownerRect);
    HMONITOR mon=MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST);MONITORINFO mi={sizeof(mi)};
    if(GetMonitorInfoW(mon,&mi)){
        int maxW=mi.rcWork.right-mi.rcWork.left-48,maxH=mi.rcWork.bottom-mi.rcWork.top-48;
        if(w>maxW)w=maxW;if(h>maxH)h=maxH;
    }
    int x=ownerRect.left+(ownerRect.right-ownerRect.left-w)/2;
    int y=ownerRect.top+(ownerRect.bottom-ownerRect.top-h)/2;
    if(x<ownerRect.left)x=ownerRect.left+12;if(y<ownerRect.top)y=ownerRect.top+12;
    HWND dlg=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"ABMAVPlayerHelp",
        L"Help Centre",WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_POPUP,
        x,y,w,h,owner,NULL,inst,NULL);
    if(!dlg)return;
    BOOL dark=TRUE;DwmSetWindowAttribute(dlg,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    {int corners=2;COLORREF border=ABM_THEME_COLOR_2E4668,caption=ABM_THEME_COLOR_0A0F19;
     DwmSetWindowAttribute(dlg,(DWORD)DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
     DwmSetWindowAttribute(dlg,(DWORD)DWMWA_BORDER_COLOR,&border,sizeof(border));
     DwmSetWindowAttribute(dlg,(DWORD)DWMWA_CAPTION_COLOR,&caption,sizeof(caption));}
    SetWindowPos(dlg,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
    EnableWindow(owner,FALSE);ShowWindow(dlg,SW_SHOW);UpdateWindow(dlg);
    MSG m;while(IsWindow(dlg)&&GetMessageW(&m,NULL,0,0)>0) {
        if(!IsDialogMessageW(dlg,&m)){TranslateMessage(&m);DispatchMessageW(&m);}
    }
    EnableWindow(owner,TRUE);SetActiveWindow(owner);
}

static void ShowAboutDialog(HWND owner) {
    static BOOL registered=FALSE;
    HINSTANCE inst=GetModuleHandleW(NULL);
    if(!registered) {
        WNDCLASSEXW wc={0}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=AboutWndProc;
        wc.hInstance=inst; wc.hCursor=LoadCursorW(NULL,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName=L"ABMAVPlayerAbout";
        if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
        registered=TRUE;
    }
    RECT wr={0,0,900,760}; AdjustWindowRectEx(&wr,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
    int w=wr.right-wr.left,h=wr.bottom-wr.top;
    int x=(GetSystemMetrics(SM_CXSCREEN)-w)/2,y=(GetSystemMetrics(SM_CYSCREEN)-h)/2;
    HWND dlg=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"ABMAVPlayerAbout",
        L"About ABM AV Player",WS_CAPTION|WS_SYSMENU|WS_POPUP,
        x,y,w,h,owner,NULL,inst,NULL);
    if(!dlg)return;
    /* Match the dark caption, border and rounded frame used by the Main Player. */
    BOOL dark=TRUE;DwmSetWindowAttribute(dlg,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    {int corners=2;COLORREF border=ABM_THEME_COLOR_2E4668,caption=ABM_THEME_COLOR_0A0F19;
     DwmSetWindowAttribute(dlg,(DWORD)DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
     DwmSetWindowAttribute(dlg,(DWORD)DWMWA_BORDER_COLOR,&border,sizeof(border));
     DwmSetWindowAttribute(dlg,(DWORD)DWMWA_CAPTION_COLOR,&caption,sizeof(caption));}
    SetWindowPos(dlg,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
    EnableWindow(owner,FALSE); ShowWindow(dlg,SW_SHOW); UpdateWindow(dlg);
    MSG m; while(IsWindow(dlg) && GetMessageW(&m,NULL,0,0)>0) {
        if(!IsDialogMessageW(dlg,&m)){TranslateMessage(&m);DispatchMessageW(&m);}
    }
    EnableWindow(owner,TRUE); SetActiveWindow(owner);
}


/* Settings consumes the shared ABM design tokens; keep this window
   visually aligned with the main player and Torrent workspace. */
#define SETTINGS_BG ABM_UI_COLOR_BACKGROUND
#define SETTINGS_PANEL ABM_UI_COLOR_SURFACE_BLUE
#define SETTINGS_PANEL_ALT ABM_UI_COLOR_SURFACE_RAISED
#define SETTINGS_FG ABM_UI_COLOR_TEXT_SECONDARY
#define SETTINGS_MUTED ABM_UI_COLOR_TEXT_MUTED
#define SETTINGS_ACCENT ABM_UI_COLOR_ACCENT
#define SETTINGS_BORDER ABM_UI_COLOR_BORDER
static ABM_D2D g_settingsUi;
static BOOL g_settingsUiReady=FALSE;
static HBRUSH g_settingsEditBrush;
static HWND g_settingsWindow;
static BOOL g_settingsShuffle=FALSE;
static int g_settingsRepeat=0;
static BOOL g_settingsReplayGain=FALSE,g_settingsCompressor=FALSE;
static BOOL g_settingsRateOpen=FALSE;
static int g_settingsThemePreset=ABM_THEME_PRESET_MIDNIGHT_TEAL;
static int g_settingsRateIndex=2;
static wchar_t g_settingsMessage[256]=L"Settings are saved only when you choose Save settings.";

static const float g_settingsRates[7]={0.5f,0.75f,1.0f,1.25f,1.5f,1.75f,2.0f};
static const wchar_t *g_settingsRateLabels[7]={L"0.5x",L"0.75x",L"1.0x",L"1.25x",L"1.5x",L"1.75x",L"2.0x"};

static void SettingsText(const wchar_t *text,int l,int t,int r,int b,COLORREF color,int style,UINT flags) {
 RECT rc={l,t,r,b};
 int role=style==2?ABM_D2D_TEXT_HEADING:style==1?ABM_D2D_TEXT_SMALL:ABM_D2D_TEXT_BODY;
 ABM_D2D_Text(&g_settingsUi,text,rc,color,role,flags);
}
static void SettingsSection(int top,const wchar_t *title,const wchar_t *description,int bottom) {
 /* Premium layered card: restrained gradient, fine outline and a quiet
    header rule keep long settings pages visually grouped. */
 ABM_D2D_GradientRound(&g_settingsUi,20.0f,(float)top,800.0f,(float)bottom,
               (float)ABM_UI_RADIUS_CARD,ABM_UI_COLOR_SURFACE_RAISED,SETTINGS_PANEL);
 ABM_D2D_StrokeRound(&g_settingsUi,20.5f,(float)top+0.5f,799.5f,(float)bottom-0.5f,
                     (float)ABM_UI_RADIUS_CARD,SETTINGS_BORDER,0.9f);
 ABM_D2D_Round(&g_settingsUi,30.0f,(float)top+12.0f,33.5f,(float)top+35.0f,
               1.7f,ABM_UI_COLOR_ACCENT_BRIGHT);
 SettingsText(title,44,top+11,510,top+36,SETTINGS_ACCENT,1,
              DT_LEFT|DT_VCENTER|DT_SINGLELINE);
 if(description&&description[0])
  SettingsText(description,510,top+11,780,top+36,SETTINGS_MUTED,1,
               DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
 ABM_D2D_Line(&g_settingsUi,38.0f,(float)top+42.0f,782.0f,(float)top+42.0f,
              SETTINGS_BORDER,0.65f);
}
static void SettingsEditFrame(int l,int t,int r,int b) {
 /* The native EDIT remains fully interactive; Direct2D supplies its premium
    inset surface and focus ring without replacing Windows text behaviour. */
 ABM_D2D_GradientRound(&g_settingsUi,(float)l,(float)t,(float)r,(float)b,
               (float)ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_INPUT,ABM_UI_COLOR_SURFACE);
 ABM_D2D_StrokeRound(&g_settingsUi,(float)l+0.5f,(float)t+0.5f,
                     (float)r-0.5f,(float)b-0.5f,
                     (float)ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_BORDER_STRONG,0.9f);
}

static int g_settingsHoverX=-1,g_settingsHoverY=-1,g_advancedHoverX=-1,g_advancedHoverY=-1;
static BOOL g_settingsMousePressed=FALSE,g_advancedMousePressed=FALSE;
static BOOL ABM_PointInRect(int x,int y,int l,int t,int r,int b){return x>=l&&x<=r&&y>=t&&y<=b;}
static void SettingsButtonDraw(int l,int t,int r,int b,const wchar_t *label,BOOL primary) {
 BOOL hot=ABM_PointInRect(g_settingsHoverX,g_settingsHoverY,l,t,r,b),down=hot&&g_settingsMousePressed;
 COLORREF top=down?ABM_UI_COLOR_SURFACE_BLUE:hot?(primary?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_SURFACE_BLUE):(primary?ABM_UI_COLOR_ACCENT:ABM_UI_COLOR_SURFACE_RAISED);
 COLORREF bottom=primary&&!down&&!hot?ABM_THEME_COLOR_1C9D9D:ABM_UI_COLOR_SURFACE;
 COLORREF edge=hot?ABM_UI_COLOR_FOCUS:(primary?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_BORDER_STRONG);
 ABM_D2D_GradientRound(&g_settingsUi,(float)l,(float)t,(float)r,(float)b,
               (float)ABM_UI_RADIUS_BUTTON,top,bottom);
 ABM_D2D_StrokeRound(&g_settingsUi,(float)l+0.5f,(float)t+0.5f,
                     (float)r-0.5f,(float)b-0.5f,
                     (float)ABM_UI_RADIUS_BUTTON,edge,hot?1.25f:0.9f);
 SettingsText(label,l+9,t+1,r-9,b-1,ABM_UI_COLOR_TEXT,1,
              DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}

static void SettingsCheckDraw(int l,int t,const wchar_t *label,BOOL checked) {
 BOOL hot=ABM_PointInRect(g_settingsHoverX,g_settingsHoverY,l,t,l+310,t+22);
 ABM_D2D_Round(&g_settingsUi,(float)l,(float)t,(float)(l+18),(float)(t+18),
               (float)ABM_UI_RADIUS_CONTROL,
               checked?ABM_UI_COLOR_ACCENT:(hot?ABM_UI_COLOR_SURFACE_BLUE:ABM_UI_COLOR_INPUT));
 ABM_D2D_StrokeRound(&g_settingsUi,(float)l+0.5f,(float)t+0.5f,
                     (float)(l+17.5f),(float)(t+17.5f),
                     (float)ABM_UI_RADIUS_CONTROL,
                     hot?ABM_UI_COLOR_FOCUS:(checked?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_BORDER_STRONG),hot?1.6f:0.9f);
 if(checked){
  ABM_D2D_Line(&g_settingsUi,(float)(l+4),(float)(t+9),
               (float)(l+7),(float)(t+12),ABM_UI_COLOR_TEXT,1.8f);
  ABM_D2D_Line(&g_settingsUi,(float)(l+7),(float)(t+12),
               (float)(l+14),(float)(t+5),ABM_UI_COLOR_TEXT,1.8f);
 }
 SettingsText(label,l+26,t-3,l+310,t+21,SETTINGS_FG,0,
              DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
static BOOL ABM_PickFolder(HWND owner,const wchar_t *title,wchar_t *path,size_t pathCount) {
    IFileOpenDialog *dialog=NULL;IShellItem *item=NULL;PWSTR selected=NULL;BOOL ok=FALSE;
    HRESULT hr=CoCreateInstance(&CLSID_FileOpenDialog,NULL,CLSCTX_INPROC_SERVER,&IID_IFileOpenDialog,(void**)&dialog);
    if(FAILED(hr)||!dialog)return FALSE;
    DWORD options=0;
    if(SUCCEEDED(IFileOpenDialog_GetOptions(dialog,&options))&&
       SUCCEEDED(IFileOpenDialog_SetOptions(dialog,options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST))&&
       SUCCEEDED(IFileDialog_SetTitle((IFileDialog*)dialog,title))&&
       SUCCEEDED(IFileDialog_Show((IFileDialog*)dialog,owner))&&
       SUCCEEDED(IFileDialog_GetResult((IFileDialog*)dialog,&item))&&item&&
       SUCCEEDED(IShellItem_GetDisplayName(item,SIGDN_FILESYSPATH,&selected))&&selected&&
       SUCCEEDED(StringCchCopyW(path,pathCount,selected)))ok=TRUE;
    if(selected)CoTaskMemFree(selected);if(item)IShellItem_Release(item);IFileOpenDialog_Release(dialog);
    return ok;
}
static void SettingsBrowse(HWND w) {
 wchar_t path[PATH_CAP]={0};
 if(ABM_PickFolder(w,L"Select torrent cache parent folder",path,_countof(path)))SetWindowTextW(GetDlgItem(w,6201),path);
}
static void SettingsBrowseDownloads(HWND w) {
 wchar_t path[PATH_CAP]={0};
 if(ABM_PickFolder(w,L"Select permanent torrent downloads folder",path,_countof(path)))SetWindowTextW(GetDlgItem(w,6202),path);
}
static void LoadThemePreference(void) {
 if(!EnsureDataDirectory())return;
 wchar_t path[PATH_CAP];
 swprintf_s(path,_countof(path),L"%s\\theme.ini",g_dataDir);
 int preset=GetPrivateProfileIntW(L"Appearance",L"ColorPreset",ABM_THEME_PRESET_MIDNIGHT_TEAL,path);
 if(preset<0||preset>=ABM_THEME_PRESET_COUNT)preset=ABM_THEME_PRESET_MIDNIGHT_TEAL;
 ABMThemeApplyPreset((ABMThemePreset)preset);
 g_settingsThemePreset=preset;
}
static void SaveThemePreference(int preset) {
 if(!EnsureDataDirectory()||preset<0||preset>=ABM_THEME_PRESET_COUNT)return;
 wchar_t path[PATH_CAP],value[16];
 swprintf_s(path,_countof(path),L"%s\\theme.ini",g_dataDir);
 swprintf_s(value,_countof(value),L"%d",preset);
 WritePrivateProfileStringW(L"Appearance",L"ColorPreset",value,path);
}
static BOOL CALLBACK ThemeInvalidateWindow(HWND hwnd,LPARAM unused) {
 (void)unused;
 if(IsWindow(hwnd)){SendMessageW(hwnd,WM_THEMECHANGED,0,0);InvalidateRect(hwnd,NULL,FALSE);}
 return TRUE;
}
static void RefreshThemeWindows(void) {
 EnumThreadWindows(GetCurrentThreadId(),ThemeInvalidateWindow,0);
 ABM_Streaming_RefreshTheme();
}

static void SettingsApply(HWND w) {
 wchar_t path[PATH_CAP]={0},downloads[PATH_CAP]={0},q[32]={0},d[32]={0},u[32]={0},v[32]={0},tmdbToken[4096]={0};
 GetWindowTextW(GetDlgItem(w,6201),path,_countof(path));GetWindowTextW(GetDlgItem(w,6202),downloads,_countof(downloads));GetWindowTextW(GetDlgItem(w,6204),q,_countof(q));
 GetWindowTextW(GetDlgItem(w,6205),d,_countof(d));GetWindowTextW(GetDlgItem(w,6206),u,_countof(u));GetWindowTextW(GetDlgItem(w,6207),v,_countof(v));GetWindowTextW(GetDlgItem(w,6208),tmdbToken,_countof(tmdbToken));
 wchar_t *qe,*de,*ue,*ve;unsigned long long quota=_wcstoui64(q,&qe,10),down=_wcstoui64(d,&de,10),up=_wcstoui64(u,&ue,10),volume=_wcstoui64(v,&ve,10);
 if(!path[0]||!downloads[0]||!q[0]||*qe||quota<1||quota>4096||!d[0]||*de||down>(unsigned long long)INT_MAX/1024||!u[0]||*ue||up>(unsigned long long)INT_MAX/1024||!v[0]||*ve||volume>100) {
  StringCchCopyW(g_settingsMessage,_countof(g_settingsMessage),L"Invalid values. Quota 1–4096 GiB; bandwidth 0–2,097,151 KiB/s; volume 0–100.");InvalidateRect(w,NULL,FALSE);return;
 }
 DWORD downloadAttr=GetFileAttributesW(downloads);
 if(downloadAttr==INVALID_FILE_ATTRIBUTES){int mk=SHCreateDirectoryExW(NULL,downloads,NULL);if(mk!=ERROR_SUCCESS&&mk!=ERROR_ALREADY_EXISTS&&mk!=ERROR_FILE_EXISTS){StringCchCopyW(g_settingsMessage,_countof(g_settingsMessage),L"Could not create the permanent downloads folder.");InvalidateRect(w,NULL,FALSE);return;}}
 else if(!(downloadAttr&FILE_ATTRIBUTE_DIRECTORY)){StringCchCopyW(g_settingsMessage,_countof(g_settingsMessage),L"Permanent downloads location must be a folder.");InvalidateRect(w,NULL,FALSE);return;}
 DWORD attr=GetFileAttributesW(path);
 if(attr==INVALID_FILE_ATTRIBUTES) {if(!CreateDirectoryW(path,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS){StringCchCopyW(g_settingsMessage,_countof(g_settingsMessage),L"Could not create the selected folder.");InvalidateRect(w,NULL,FALSE);return;}}
 else if(!(attr&FILE_ATTRIBUTE_DIRECTORY)){StringCchCopyW(g_settingsMessage,_countof(g_settingsMessage),L"Cache location must be a folder.");InvalidateRect(w,NULL,FALSE);return;}
 BOOL changed=_wcsicmp(path,g_torrentCacheBaseDir)!=0;
 BOOL downloadsChanged=_wcsicmp(downloads,g_torrentDownloadDir)!=0;
 if((changed||downloadsChanged)&&(g_torrentInitialized||g_torrentInitPending)&&ABM_MessageBoxW(w,L"The torrent engine is running. The new location will take effect after restarting ABM AV Player. Continue?",L"Restart required",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
 wcsncpy_s(g_torrentCacheBaseDir,_countof(g_torrentCacheBaseDir),path,_TRUNCATE);
 wcsncpy_s(g_torrentDownloadDir,_countof(g_torrentDownloadDir),downloads,_TRUNCATE);
 if(!ABM_TMDB_SetReadToken(tmdbToken)){StringCchCopyW(g_settingsMessage,_countof(g_settingsMessage),L"Could not securely save the TMDB token.");InvalidateRect(w,NULL,FALSE);SecureZeroMemory(tmdbToken,sizeof(tmdbToken));return;} SecureZeroMemory(tmdbToken,sizeof(tmdbToken)); g_torrentCacheQuotaGiB=quota;g_torrentDownloadLimitKiB=(int)down;g_torrentUploadLimitKiB=(int)up;g_volume=(int)volume;
 g_shuffle=g_settingsShuffle;g_repeat=g_settingsRepeat;g_replayGain=g_settingsReplayGain;g_compressor=g_settingsCompressor;
 if(g_settingsRateIndex<0||g_settingsRateIndex>6)g_settingsRateIndex=2;
 g_playbackRate=g_settingsRates[g_settingsRateIndex];
 if(g_player)libvlc_audio_set_volume(g_player,g_volume);if(g_hasMedia)libvlc_media_player_set_rate(g_player,g_playbackRate);
 SavePersistentState();SaveAudioPreferences();ApplyEqProfile();
 if(g_torrentInitialized){uint64_t used=0;if(g_torrentSetCacheLimit)g_torrentSetCacheLimit(quota*1024ULL*1024ULL*1024ULL,&used);if(g_torrentSetRateLimits)g_torrentSetRateLimits((int)(down*1024),(int)(up*1024));}
 StringCchCopyW(g_settingsMessage,_countof(g_settingsMessage),(changed||downloadsChanged)&&(g_torrentInitialized||g_torrentInitPending)?L"Settings saved. Restart ABM AV Player to activate the new storage locations.":L"All settings saved successfully.");
 InvalidateRect(w,NULL,FALSE);InvalidateRect(g_main,NULL,FALSE);
}

/* Advanced Settings framework. This first stage establishes navigation and
   page surfaces only; engine controls are added in later, capability-backed steps. */
static ABM_D2D g_advancedSettingsUi;
static BOOL g_advancedSettingsUiReady=FALSE;
static BOOL g_advancedConfirmExit=TRUE;
static BOOL g_advancedDoubleClickFullscreen=TRUE;
static HWND g_advancedSettingsWindow=NULL;
static HWND g_advancedDiagnosticEdit=NULL;
static HFONT g_advancedDiagnosticFont=NULL;
static HBRUSH g_advancedDiagnosticBrush=NULL;
static int g_advancedSettingsPage=0;
static int g_advancedSettingsStartPage=0;
static int g_advancedAspectMode=0;
static int g_advancedDecoderThreads=0;
static BOOL g_advancedDropLateFrames=TRUE,g_advancedSkipFrames=FALSE;
static const int g_advancedThreadChoices[4]={0,2,4,8};
static const wchar_t *g_advancedThreadLabels[4]={L"Automatic",L"2 threads",L"4 threads",L"8 threads"};
static void AddAdvancedPerformanceOptions(libvlc_media_t *media){
 if(!media)return;
 char opt[64];
 if(g_advancedDecoderThreads>0){sprintf_s(opt,sizeof(opt),":avcodec-threads=%d",g_advancedDecoderThreads);libvlc_media_add_option(media,opt);}
 if(g_advancedDropLateFrames)libvlc_media_add_option(media,":drop-late-frames");
 else libvlc_media_add_option(media,":no-drop-late-frames");
 if(g_advancedSkipFrames)libvlc_media_add_option(media,":skip-frames");
 else libvlc_media_add_option(media,":no-skip-frames");
}
static int g_advancedNetworkCaching=1000,g_advancedFileCaching=300,g_advancedLiveCaching=1000;
static const int g_advancedCacheChoices[4]={300,1000,2000,5000};
static const wchar_t *g_advancedCacheLabels[4]={L"300 ms",L"1,000 ms",L"2,000 ms",L"5,000 ms"};
static void AddAdvancedCachingOptions(libvlc_media_t *media){
 if(!media)return;char opt[64];
 sprintf_s(opt,sizeof(opt),":network-caching=%d",g_advancedNetworkCaching);libvlc_media_add_option(media,opt);
 sprintf_s(opt,sizeof(opt),":file-caching=%d",g_advancedFileCaching);libvlc_media_add_option(media,opt);
 sprintf_s(opt,sizeof(opt),":live-caching=%d",g_advancedLiveCaching);libvlc_media_add_option(media,opt);
}
static const wchar_t *g_advancedAspectLabels[4]={L"Original (source)",L"16:9 widescreen",L"4:3 classic",L"21:9 ultrawide"};
static void ApplyAdvancedAspectRatio(void);
static void ApplyAdvancedAspectRatio(void){
 if(!g_player)return;
 const char *ratios[4]={NULL,"16:9","4:3","21:9"};
 int mode=g_advancedAspectMode;if(mode<0||mode>3)mode=0;
 libvlc_video_set_aspect_ratio(g_player,ratios[mode]);
}

static int g_advancedHwMode=0,g_advancedHwDraft=0;
static BOOL g_advancedHwEnvOverride=FALSE;
static int g_advancedAudioOutMode=0,g_advancedAudioOutDraft=0;
static const int g_subtitleFontSizes[4]={10,13,16,19};
static const wchar_t *g_subtitleFontSizeLabels[4]={L"Small",L"Normal",L"Large",L"Larger"};
static const int g_subtitleColors[4]={16777215,16776960,65535,65280};
static const wchar_t *g_subtitleColorLabels[4]={L"White",L"Yellow",L"Cyan",L"Green"};
static const int g_subtitleOutlineValues[4]={0,2,4,6};
static const wchar_t *g_subtitleOutlineLabels[4]={L"Off",L"Thin",L"Normal",L"Thick"};
static const int g_subtitleMarginValues[4]={0,10,25,50};
static const wchar_t *g_subtitleMarginLabels[4]={L"Default",L"10 px",L"25 px",L"50 px"};

static wchar_t g_advancedAudioOutMessage[256]=L"";
static wchar_t g_advancedHwMessage[256]=L"Choose a decoder mode, then apply and restart ABM AV Player.";
static const wchar_t *g_advancedSettingsPages[]={
 L"Overview",L"Video & Decoding",L"Audio Engine",L"Video Output",
 L"Subtitle Manager",L"Streaming & Network",L"Performance",L"Controls & Behaviour",
 L"Diagnostics",L"VLC Runtime & Codecs",L"Advanced Audio Centre"
};
static const wchar_t *g_advancedSettingsDescriptions[]={
 L"Advanced configuration for ABM AV Player.",
 L"Decoder selection, hardware acceleration and frame handling.",
 L"Output devices, channel layout, processing and synchronization.",
 L"Scaling, aspect ratio, colour handling and display options.",
 L"Select embedded or external subtitles, tune synchronization and personalize appearance.",
 L"Network caching, stream timeouts and connection behaviour.",
 L"Playback resource usage, buffering and performance controls.",
 L"Keyboard, mouse, playlist and playback behaviour.",
 L"System and player health, local reports and troubleshooting.",
 L"LibVLC runtime health, complete plugin inventory and evidence-based playback checks.",
 L"Output engine, audio processing, equalizer and synchronization controls."
};
#define ABM_VLC_PLUGIN_MAX 512
typedef struct ABM_VlcPluginEntry { wchar_t category[48]; wchar_t name[128]; BOOL fileFound; BOOL loadedInProcess; } ABM_VlcPluginEntry;
static ABM_VlcPluginEntry g_vlcPlugins[ABM_VLC_PLUGIN_MAX];
static unsigned int g_vlcPluginCount=0;
static BOOL g_vlcPluginInventoryTruncated=FALSE,g_vlcRuntimeTested=FALSE;
static wchar_t g_vlcRuntimeVersion[96]=L"Not detected",g_vlcRuntimePath[PATH_CAP]=L"Not detected",g_vlcPluginPath[PATH_CAP]=L"Not detected";
static BOOL g_vlcRuntimeLoaded=FALSE,g_vlcPluginDirectoryFound=FALSE;
static HWND g_advancedVlcReportEdit=NULL;
static HBRUSH g_advancedVlcReportBrush=NULL;
static wchar_t g_vlcRuntimeTestDetail[160]=L"Not tested";
static wchar_t g_vlcPlaybackTestDetail[192]=L"No successful playback observed in this session";

#define ABM_VLC_TRACK_MAX 32
typedef struct ABM_VlcTrackEvidence { int id; libvlc_track_type_t type; uint32_t codec; BOOL selected; wchar_t description[96]; } ABM_VlcTrackEvidence;
static ABM_VlcTrackEvidence g_vlcTrackEvidence[ABM_VLC_TRACK_MAX];
static unsigned int g_vlcTrackEvidenceCount=0;
static wchar_t g_vlcMediaPath[PATH_CAP]=L"",g_vlcContainer[32]=L"Unknown";
static BOOL g_vlcMediaPlayingObserved=FALSE,g_vlcMediaErrorObserved=FALSE,g_vlcTrackSnapshotReady=FALSE;
static BOOL g_vlcPlaybackSessionSuccessful=FALSE,g_vlcDecoderModuleIdentified=FALSE;
static int g_vlcSelectedAudioId=-1,g_vlcSelectedVideoId=-1,g_vlcSelectedSubtitleId=-1;
static int64_t g_vlcLastObservedTime=-1;
static wchar_t g_vlcDecoderModule[128]=L"Not exposed by LibVLC public API";
static void VlcPlaybackEvent(const libvlc_event_t *event,void *opaque) {
 (void)opaque;if(!event)return;LONG bit=0;
 switch(event->type){
  case libvlc_MediaPlayerPlaying:bit=ABM_VLC_EVT_PLAYING;break;
  case libvlc_MediaPlayerPaused:bit=ABM_VLC_EVT_PAUSED;break;
  case libvlc_MediaPlayerStopped:bit=ABM_VLC_EVT_STOPPED;break;
  case libvlc_MediaPlayerEndReached:bit=ABM_VLC_EVT_ENDED;break;
  case libvlc_MediaPlayerEncounteredError:bit=ABM_VLC_EVT_ERROR;break;
  case libvlc_MediaPlayerESAdded:bit=ABM_VLC_EVT_ES_ADDED;break;
  case libvlc_MediaPlayerESDeleted:bit=ABM_VLC_EVT_ES_DELETED;break;
  case libvlc_MediaPlayerESSelected:bit=ABM_VLC_EVT_ES_SELECTED;break;
  case libvlc_MediaPlayerMediaChanged:bit=ABM_VLC_EVT_MEDIA_CHANGED;break;
  default:break;
 }
 if(bit)InterlockedOr(&g_vlcPlaybackEventFlags,bit);
}
static BOOL VlcAttachPlaybackEvents(void) {
 if(g_vlcPlaybackEventManager)return TRUE;if(!g_player)return FALSE;
 libvlc_event_manager_t *manager=libvlc_media_player_event_manager(g_player);if(!manager)return FALSE;
 static const libvlc_event_type_t types[]={libvlc_MediaPlayerPlaying,libvlc_MediaPlayerPaused,libvlc_MediaPlayerStopped,libvlc_MediaPlayerEndReached,libvlc_MediaPlayerEncounteredError,libvlc_MediaPlayerESAdded,libvlc_MediaPlayerESDeleted,libvlc_MediaPlayerESSelected,libvlc_MediaPlayerMediaChanged};
 g_vlcPlaybackAttachedCount=0;
 for(int i=0;i<(int)_countof(types);i++){if(libvlc_event_attach(manager,types[i],VlcPlaybackEvent,NULL)!=0){for(int j=0;j<g_vlcPlaybackAttachedCount;j++)libvlc_event_detach(manager,g_vlcPlaybackAttachedEvents[j],VlcPlaybackEvent,NULL);g_vlcPlaybackAttachedCount=0;return FALSE;}g_vlcPlaybackAttachedEvents[g_vlcPlaybackAttachedCount++]=types[i];}
 g_vlcPlaybackEventManager=manager;return TRUE;
}
static void VlcDetachPlaybackEvents(void) {
 if(g_vlcPlaybackEventManager){for(int i=0;i<g_vlcPlaybackAttachedCount;i++)libvlc_event_detach(g_vlcPlaybackEventManager,g_vlcPlaybackAttachedEvents[i],VlcPlaybackEvent,NULL);}
 g_vlcPlaybackEventManager=NULL;g_vlcPlaybackAttachedCount=0;InterlockedExchange(&g_vlcPlaybackEventFlags,0);
}
static void VlcCodecVerificationReset(void) {
 g_vlcTrackEvidenceCount=0;g_vlcMediaPath[0]=0;StringCchCopyW(g_vlcContainer,_countof(g_vlcContainer),L"Unknown");
 g_vlcMediaPlayingObserved=FALSE;g_vlcMediaErrorObserved=FALSE;g_vlcTrackSnapshotReady=FALSE;g_vlcPlaybackSessionSuccessful=FALSE;
 g_vlcSelectedAudioId=g_vlcSelectedVideoId=g_vlcSelectedSubtitleId=-1;g_vlcLastObservedTime=-1;
 InterlockedExchange(&g_vlcPlaybackEventFlags,0);
 StringCchCopyW(g_vlcDecoderModule,_countof(g_vlcDecoderModule),L"Not exposed by LibVLC public API");g_vlcDecoderModuleIdentified=FALSE;
 StringCchCopyW(g_vlcPlaybackTestDetail,_countof(g_vlcPlaybackTestDetail),L"WAITING — start media to identify streams and active tracks");
}
static void VlcFourccText(uint32_t code,wchar_t *out,size_t count) {if(!out||count<5)return;for(int i=0;i<4;i++){unsigned char c=(unsigned char)((code>>(i*8))&0xff);out[i]=(c>=32&&c<=126)?(wchar_t)c:L'.';}out[4]=0;}
static void VlcCaptureTrackEvidence(void) {
 if(!g_player||!g_hasMedia)return;libvlc_media_t *media=libvlc_media_player_get_media(g_player);if(!media)return;
 libvlc_media_track_t **tracks=NULL;unsigned int count=libvlc_media_tracks_get(media,&tracks),limit=count>ABM_VLC_TRACK_MAX?ABM_VLC_TRACK_MAX:count;g_vlcTrackEvidenceCount=0;
 g_vlcSelectedAudioId=libvlc_audio_get_track(g_player);g_vlcSelectedVideoId=libvlc_video_get_track(g_player);g_vlcSelectedSubtitleId=libvlc_video_get_spu(g_player);
 for(unsigned int i=0;i<limit;i++){libvlc_media_track_t *t=tracks?tracks[i]:NULL;if(!t)continue;ABM_VlcTrackEvidence *e=&g_vlcTrackEvidence[g_vlcTrackEvidenceCount++];ZeroMemory(e,sizeof(*e));e->id=t->i_id;e->type=t->i_type;e->codec=t->i_codec;e->selected=(t->i_type==libvlc_track_audio&&e->id==g_vlcSelectedAudioId)||(t->i_type==libvlc_track_video&&e->id==g_vlcSelectedVideoId)||(t->i_type==libvlc_track_text&&e->id==g_vlcSelectedSubtitleId);const char *d=libvlc_media_get_codec_description(t->i_type,t->i_codec);if(d&&*d)MultiByteToWideChar(CP_UTF8,0,d,-1,e->description,(int)_countof(e->description));if(!e->description[0])VlcFourccText(t->i_codec,e->description,_countof(e->description));}
 if(tracks)libvlc_media_tracks_release(tracks,count);libvlc_media_release(media);
 if(g_current>=0&&g_current<g_playlistCount)StringCchCopyW(g_vlcMediaPath,_countof(g_vlcMediaPath),g_playlist[g_current]);const wchar_t *dot=wcsrchr(g_vlcMediaPath,L'.');if(dot){StringCchCopyW(g_vlcContainer,_countof(g_vlcContainer),dot+1);CharLowerBuffW(g_vlcContainer,(DWORD)wcslen(g_vlcContainer));}
 g_vlcTrackSnapshotReady=(count>0);
}
static void VlcRefreshSelectedTracks(void) {
 if(!g_player||!g_vlcTrackSnapshotReady)return;
 g_vlcSelectedAudioId=libvlc_audio_get_track(g_player);g_vlcSelectedVideoId=libvlc_video_get_track(g_player);g_vlcSelectedSubtitleId=libvlc_video_get_spu(g_player);
 for(unsigned int i=0;i<g_vlcTrackEvidenceCount;i++){ABM_VlcTrackEvidence *e=&g_vlcTrackEvidence[i];e->selected=(e->type==libvlc_track_audio&&e->id==g_vlcSelectedAudioId)||(e->type==libvlc_track_video&&e->id==g_vlcSelectedVideoId)||(e->type==libvlc_track_text&&e->id==g_vlcSelectedSubtitleId);}
}
static void VlcCodecVerificationPoll(void) {
 if(!g_player||!g_hasMedia)return;
 LONG events=InterlockedExchange(&g_vlcPlaybackEventFlags,0);
 if(events&ABM_VLC_EVT_ERROR)g_vlcMediaErrorObserved=TRUE;
 if(events&ABM_VLC_EVT_PLAYING){g_vlcMediaPlayingObserved=TRUE;if(!g_selectedRenderer)libvlc_audio_set_delay(g_player,g_audioDelayUs);}
 if(events&(ABM_VLC_EVT_ES_ADDED|ABM_VLC_EVT_ES_DELETED|ABM_VLC_EVT_ES_SELECTED))g_vlcTrackSnapshotReady=FALSE;
 if(events&ABM_VLC_EVT_MEDIA_CHANGED){g_vlcTrackSnapshotReady=FALSE;g_vlcTrackEvidenceCount=0;}
 libvlc_state_t state=libvlc_media_player_get_state(g_player);
 if(state==libvlc_Error)g_vlcMediaErrorObserved=TRUE;
 if(state==libvlc_Playing)g_vlcMediaPlayingObserved=TRUE;
 if(g_vlcMediaPlayingObserved&&!g_vlcTrackSnapshotReady)VlcCaptureTrackEvidence();
 VlcRefreshSelectedTracks();
 int64_t now=libvlc_media_player_get_time(g_player);
 if(state==libvlc_Playing&&g_vlcMediaPlayingObserved&&now>=0&&g_vlcLastObservedTime>=0&&now>g_vlcLastObservedTime)g_vlcPlaybackSessionSuccessful=TRUE;
 if(now>=0)g_vlcLastObservedTime=now;
 if((events&ABM_VLC_EVT_ENDED)&&g_vlcMediaPlayingObserved)g_vlcPlaybackSessionSuccessful=TRUE;
 if(g_vlcMediaErrorObserved)StringCchCopyW(g_vlcPlaybackTestDetail,_countof(g_vlcPlaybackTestDetail),L"PLAYBACK ERROR — LibVLC reported EncounteredError");
 else if(g_vlcPlaybackSessionSuccessful)StringCchCopyW(g_vlcPlaybackTestDetail,_countof(g_vlcPlaybackTestDetail),L"SESSION SUCCESS — Playing event/state and media time progression observed");
 else if(g_vlcMediaPlayingObserved&&g_vlcTrackSnapshotReady)StringCchCopyW(g_vlcPlaybackTestDetail,_countof(g_vlcPlaybackTestDetail),L"PLAYING — tracks detected; waiting for media time progression");
 else if(g_vlcMediaPlayingObserved)StringCchCopyW(g_vlcPlaybackTestDetail,_countof(g_vlcPlaybackTestDetail),L"PLAYING — waiting for stream metadata");
 else StringCchCopyW(g_vlcPlaybackTestDetail,_countof(g_vlcPlaybackTestDetail),L"PENDING — waiting for LibVLC Playing event");
}

static void ScanVlcPluginCategory(const wchar_t *pluginDir,const wchar_t *category){
 wchar_t pattern[PATH_CAP]={0};if(FAILED(StringCchPrintfW(pattern,_countof(pattern),L"%s\\%s\\*.dll",pluginDir,category)))return;
 WIN32_FIND_DATAW fd;HANDLE find=FindFirstFileW(pattern,&fd);if(find==INVALID_HANDLE_VALUE)return;
 do{if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;if(g_vlcPluginCount>=ABM_VLC_PLUGIN_MAX){g_vlcPluginInventoryTruncated=TRUE;break;}
  ABM_VlcPluginEntry *e=&g_vlcPlugins[g_vlcPluginCount++];StringCchCopyW(e->category,_countof(e->category),category);StringCchCopyW(e->name,_countof(e->name),fd.cFileName);e->fileFound=TRUE;e->loadedInProcess=(GetModuleHandleW(fd.cFileName)!=NULL);
 }while(FindNextFileW(find,&fd));FindClose(find);
}
static void ScanVlcRuntime(void){
 wchar_t modulePath[PATH_CAP]={0},directory[PATH_CAP]={0},pluginDir[PATH_CAP]={0};HMODULE mod=GetModuleHandleW(L"libvlc.dll");g_vlcRuntimeLoaded=(mod!=NULL);
 if(mod&&GetModuleFileNameW(mod,modulePath,(DWORD)_countof(modulePath))>0){StringCchCopyW(g_vlcRuntimePath,_countof(g_vlcRuntimePath),modulePath);StringCchCopyW(directory,_countof(directory),modulePath);wchar_t *slash=wcsrchr(directory,L'\\');if(slash)*slash=0;}
 else StringCchCopyW(g_vlcRuntimePath,_countof(g_vlcRuntimePath),L"LibVLC module is not loaded");
 const char *version=libvlc_get_version();if(version&&*version)MultiByteToWideChar(CP_UTF8,0,version,-1,g_vlcRuntimeVersion,(int)_countof(g_vlcRuntimeVersion));else StringCchCopyW(g_vlcRuntimeVersion,_countof(g_vlcRuntimeVersion),L"Version unavailable");
 if(directory[0]){StringCchPrintfW(pluginDir,_countof(pluginDir),L"%s\\plugins",directory);DWORD at=GetFileAttributesW(pluginDir);if(at==INVALID_FILE_ATTRIBUTES||!(at&FILE_ATTRIBUTE_DIRECTORY)){wchar_t env[PATH_CAP]={0};DWORD n=GetEnvironmentVariableW(L"VLC_PLUGIN_PATH",env,(DWORD)_countof(env));if(n>0&&n<_countof(env))StringCchCopyW(pluginDir,_countof(pluginDir),env);}}
 if(pluginDir[0]){StringCchCopyW(g_vlcPluginPath,_countof(g_vlcPluginPath),pluginDir);DWORD at=GetFileAttributesW(pluginDir);g_vlcPluginDirectoryFound=(at!=INVALID_FILE_ATTRIBUTES&&(at&FILE_ATTRIBUTE_DIRECTORY));}
 else{StringCchCopyW(g_vlcPluginPath,_countof(g_vlcPluginPath),L"Plugin directory not resolved");g_vlcPluginDirectoryFound=FALSE;}
 g_vlcPluginCount=0;g_vlcPluginInventoryTruncated=FALSE;
 if(g_vlcPluginDirectoryFound){wchar_t pattern[PATH_CAP]={0};StringCchPrintfW(pattern,_countof(pattern),L"%s\\*",pluginDir);WIN32_FIND_DATAW fd;HANDLE find=FindFirstFileW(pattern,&fd);if(find!=INVALID_HANDLE_VALUE){do{if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||fd.cFileName[0]==L'.')continue;ScanVlcPluginCategory(pluginDir,fd.cFileName);if(g_vlcPluginInventoryTruncated)break;}while(FindNextFileW(find,&fd));FindClose(find);}}
 g_vlcRuntimeTested=(g_vlc!=NULL&&g_player!=NULL&&version&&*version);
 StringCchCopyW(g_vlcRuntimeTestDetail,_countof(g_vlcRuntimeTestDetail),g_vlcRuntimeTested?L"PASS — LibVLC instance and local player initialized":L"NOT TESTED — runtime/player initialization unavailable");
 VlcCodecVerificationPoll();
}
static void UpdateVlcReportText(void){
 if(!g_advancedVlcReportEdit)return;wchar_t *r=(wchar_t*)calloc(65536,sizeof(wchar_t));if(!r)return;unsigned int loaded=0;for(unsigned int i=0;i<g_vlcPluginCount;i++)if(g_vlcPlugins[i].loadedInProcess)loaded++;
 StringCchPrintfW(r,65536,L"ABM VLC COMPATIBILITY AUDIT\r\nLibVLC: %s | Version: %s\r\nModule: %s\r\nPlugin directory: %s (%s)\r\n\r\nRUNTIME CHECKS\r\n1. File Found: %u plugin DLLs discovered\r\n2. DLL Mapped: %u DLLs mapped in this ABM process\r\n3. Runtime Tested: %s\r\n4. Playback Session: %s\r\n\r\nPLUGIN INVENTORY  [Category | DLL | File Found | DLL Mapped]\r\n",g_vlcRuntimeLoaded?L"Loaded":L"Not loaded",g_vlcRuntimeVersion,g_vlcRuntimePath,g_vlcPluginPath,g_vlcPluginDirectoryFound?L"Found":L"Not found",g_vlcPluginCount,loaded,g_vlcRuntimeTestDetail,g_vlcPlaybackTestDetail);
 size_t used=wcslen(r);for(unsigned int i=0;i<g_vlcPluginCount&&used<64000;i++){int n=_snwprintf_s(r+used,65536-used,_TRUNCATE,L"%-18s | %-42s | %s | %s\r\n",g_vlcPlugins[i].category,g_vlcPlugins[i].name,g_vlcPlugins[i].fileFound?L"FOUND":L"MISSING",g_vlcPlugins[i].loadedInProcess?L"LOADED":L"NOT LOADED");if(n<0)break;used+=(size_t)n;}
 if(g_vlcPluginInventoryTruncated&&used<65000)StringCchCatW(r,65536,L"\r\nWARNING: Inventory capped at 512 plugin DLLs.\r\n");
 if(used<65000){StringCchCatW(r,65536,L"\r\nCURRENT MEDIA — CODEC-AWARE EVIDENCE\r\nMedia: ");StringCchCatW(r,65536,g_vlcMediaPath[0]?g_vlcMediaPath:L"No local playlist path recorded");StringCchCatW(r,65536,L"\r\nContainer/extension hint: ");StringCchCatW(r,65536,g_vlcContainer);StringCchCatW(r,65536,L"\r\nStream detected: ");StringCchCatW(r,65536,g_vlcTrackSnapshotReady?L"YES":L"NOT YET");StringCchCatW(r,65536,L"\r\nPlayback session successful: ");StringCchCatW(r,65536,g_vlcPlaybackSessionSuccessful?L"YES — time progression/end reached":L"NOT CONFIRMED");StringCchCatW(r,65536,L"\r\nPlayback error: ");StringCchCatW(r,65536,g_vlcMediaErrorObserved?L"YES":L"NO OBSERVED ERROR");StringCchCatW(r,65536,L"\r\nTrack selected — audio ID: ");wchar_t selectedLine[160];StringCchPrintfW(selectedLine,_countof(selectedLine),L"%d | video ID: %d | subtitle ID: %d\r\n",g_vlcSelectedAudioId,g_vlcSelectedVideoId,g_vlcSelectedSubtitleId);StringCchCatW(r,65536,selectedLine);StringCchCatW(r,65536,L"Decoder module identified: ");StringCchCatW(r,65536,g_vlcDecoderModuleIdentified?g_vlcDecoderModule:L"UNKNOWN — not exposed by the LibVLC public API");StringCchCatW(r,65536,L"\r\nEvent correlation: ");StringCchCatW(r,65536,g_vlcPlaybackEventManager?L"Player lifecycle and elementary-stream events attached":L"Event attachment unavailable; state polling only");StringCchCatW(r,65536,L"\r\n\r\nTRACK EVIDENCE [Type | ID | Codec | Detected | Selected]\r\n");for(unsigned int ti=0;ti<g_vlcTrackEvidenceCount&&used<64000;ti++){ABM_VlcTrackEvidence *e=&g_vlcTrackEvidence[ti];const wchar_t *kind=e->type==libvlc_track_video?L"Video":e->type==libvlc_track_audio?L"Audio":e->type==libvlc_track_text?L"Subtitle":L"Other";wchar_t fourcc[8],line[256];VlcFourccText(e->codec,fourcc,_countof(fourcc));StringCchPrintfW(line,_countof(line),L"%s | %d | %s | YES | %s\r\n",kind,e->id,e->description,e->selected?L"SELECTED":L"not selected");StringCchCatW(r,65536,line);}StringCchCatW(r,65536,L"\r\nINTERPRETATION\r\nPlayback verification requires LibVLC to reach Playing and expose elementary-stream metadata. Selected IDs come from libvlc_audio_get_track, libvlc_video_get_track and libvlc_video_get_spu. Playback session success requires a Playing event/state followed by media-time progression or EndReached. Decoder module identification remains unknown because LibVLC public APIs do not expose the selected internal decoder module. Container/extension is a filename hint, not a validated demuxer result.\r\n");}
 SendMessageW(g_advancedVlcReportEdit,EM_SETLIMITTEXT,65535,0);SetWindowTextW(g_advancedVlcReportEdit,r);SendMessageW(g_advancedVlcReportEdit,EM_SETSEL,0,0);SendMessageW(g_advancedVlcReportEdit,EM_SCROLLCARET,0,0);free(r);
}
static void AdvancedSettingsText(const wchar_t *text,int l,int t,int r,int b,COLORREF color,int style,UINT flags){
 RECT rc={l,t,r,b};
 int role=style==2?ABM_D2D_TEXT_HEADING:style==1?ABM_D2D_TEXT_SMALL:ABM_D2D_TEXT_BODY;
 ABM_D2D_Text(&g_advancedSettingsUi,text,rc,color,role,flags);
}
static void AdvancedSettingsButtonDraw(int l,int t,int r,int b,const wchar_t *label,BOOL primary){
 BOOL hot=ABM_PointInRect(g_advancedHoverX,g_advancedHoverY,l,t,r,b),down=hot&&g_advancedMousePressed;
 COLORREF fill=down?ABM_UI_COLOR_SURFACE_BLUE:hot?(primary?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_SURFACE_BLUE):(primary?ABM_UI_COLOR_ACCENT:ABM_UI_COLOR_SURFACE_RAISED);
 COLORREF edge=hot?ABM_UI_COLOR_FOCUS:(primary?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_BORDER_STRONG);
 ABM_D2D_Round(&g_advancedSettingsUi,(float)l,(float)t,(float)r,(float)b,(float)ABM_UI_RADIUS_BUTTON,fill);
 ABM_D2D_StrokeRound(&g_advancedSettingsUi,(float)l+0.5f,(float)t+0.5f,(float)r-0.5f,(float)b-0.5f,(float)ABM_UI_RADIUS_BUTTON,edge,0.9f);
 AdvancedSettingsText(label,l+8,t+1,r-8,b-1,ABM_UI_COLOR_TEXT,1,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
static BOOL AdvancedSettingsPoint(int x,int y,int l,int t,int r,int b){
 return x>=l&&x<=r&&y>=t&&y<=b;
}
/* Advanced Settings is drawn in a fixed 1080x780 design coordinate space.
   Convert real client pixels back into that space for mouse input. Without
   this inverse transform, a window scaled to fit a smaller work area paints
   controls in one place but tests clicks against the unscaled coordinates. */
static void AdvancedSettingsClientToDesign(HWND w,int *x,int *y){
 RECT rc;GetClientRect(w,&rc);
 if(!x||!y||rc.right<=0||rc.bottom<=0)return;
 *x=(int)(((int64_t)*x*1080)/rc.right);
 *y=(int)(((int64_t)*y*780)/rc.bottom);
}
static BOOL GenerateAdvancedDiagnosticReport(wchar_t *reportPath,size_t reportPathCount){
 if(!EnsureDataDirectory())return FALSE;
 ScanVlcRuntime();
 SYSTEM_INFO si={0};GetNativeSystemInfo(&si);
 MEMORYSTATUSEX mem={0};mem.dwLength=sizeof(mem);GlobalMemoryStatusEx(&mem);
 PROCESS_MEMORY_COUNTERS pmc={0};pmc.cb=sizeof(pmc);GetProcessMemoryInfo(GetCurrentProcess(),&pmc,sizeof(pmc));
 wchar_t exe[PATH_CAP]={0};GetModuleFileNameW(NULL,exe,(DWORD)_countof(exe));
 const wchar_t *arch=si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64?L"x64":si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_ARM64?L"ARM64":L"x86";
 const wchar_t *playback=L"No media loaded";
 if(g_hasMedia&&g_player){libvlc_state_t st=libvlc_media_player_get_state(g_player);playback=st==libvlc_Playing?L"Playing":st==libvlc_Paused?L"Paused":st==libvlc_Buffering?L"Buffering":st==libvlc_Stopped?L"Stopped":L"Loaded / transitioning";}
 SYSTEMTIME now;GetLocalTime(&now);
 if(FAILED(StringCchPrintfW(reportPath,reportPathCount,L"%s\\ABM-Diagnostics-%04u%02u%02u-%02u%02u%02u.txt",g_dataDir,now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond)))return FALSE;
 FILE *fp=NULL;if(_wfopen_s(&fp,reportPath,L"wt, ccs=UTF-8")!=0||!fp)return FALSE;
 fwprintf(fp,L"ABM AV Player diagnostic report\r\nGenerated: %04u-%02u-%02u %02u:%02u:%02u\r\n\r\n",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);
 fwprintf(fp,L"[Application]\r\nVersion: %s\r\nProcess ID: %lu\r\nArchitecture: %s\r\nExecutable: %s\r\nData directory: %s\r\n",APP_VERSION,GetCurrentProcessId(),arch,exe,g_dataDir);
 fwprintf(fp,L"\r\n[VLC runtime]\r\nLoaded in process: %s\r\nLibVLC version: %s\r\nLibVLC module: %s\r\nPlugin directory: %s\r\nPlugin directory found: %s\r\n",g_vlcRuntimeLoaded?L"Yes":L"No",g_vlcRuntimeVersion,g_vlcRuntimePath,g_vlcPluginPath,g_vlcPluginDirectoryFound?L"Yes":L"No");
 {unsigned int loaded=0;for(unsigned int vi=0;vi<g_vlcPluginCount;vi++)if(g_vlcPlugins[vi].loadedInProcess)loaded++;fwprintf(fp,L"File Found: %u plugin DLLs\r\nDLL Mapped: %u DLLs mapped in process\r\nRuntime Tested: %s\r\nPlayback Session: %s\r\nCurrent media: %s\r\nContainer/extension hint: %s\r\nStream detected: %s\r\nPlayback session successful: %s\r\nPlayback error: %s\r\nSelected audio ID: %d\r\nSelected video ID: %d\r\nSelected subtitle ID: %d\r\nDecoder module identified: %s\r\nEvent monitoring: %s\r\n",g_vlcPluginCount,loaded,g_vlcRuntimeTestDetail,g_vlcPlaybackTestDetail,g_vlcMediaPath[0]?g_vlcMediaPath:L"Not recorded",g_vlcContainer,g_vlcTrackSnapshotReady?L"Yes":L"No",g_vlcPlaybackSessionSuccessful?L"Yes":L"No",g_vlcMediaErrorObserved?L"Yes":L"No",g_vlcSelectedAudioId,g_vlcSelectedVideoId,g_vlcSelectedSubtitleId,g_vlcDecoderModuleIdentified?g_vlcDecoderModule:L"Unknown — not exposed by public API",g_vlcPlaybackEventManager?L"Attached":L"Polling only");for(unsigned int ti=0;ti<g_vlcTrackEvidenceCount;ti++)fwprintf(fp,L"Track %d | type %d | codec %s | FourCC %08X | selected %s\r\n",g_vlcTrackEvidence[ti].id,(int)g_vlcTrackEvidence[ti].type,g_vlcTrackEvidence[ti].description,g_vlcTrackEvidence[ti].codec,g_vlcTrackEvidence[ti].selected?L"Yes":L"No");for(unsigned int vi=0;vi<g_vlcPluginCount;vi++)fwprintf(fp,L"Plugin [%s] %s: File %s; Loaded %s\r\n",g_vlcPlugins[vi].category,g_vlcPlugins[vi].name,g_vlcPlugins[vi].fileFound?L"Found":L"Missing",g_vlcPlugins[vi].loadedInProcess?L"Yes":L"No");}
 fwprintf(fp,L"\r\n[System]\r\nLogical processors: %lu\r\nPhysical memory: %llu MB\r\nAvailable physical memory: %llu MB\r\nMemory load: %lu%%\r\n",si.dwNumberOfProcessors,(unsigned long long)(mem.ullTotalPhys/(1024ULL*1024ULL)),(unsigned long long)(mem.ullAvailPhys/(1024ULL*1024ULL)),mem.dwMemoryLoad);
 fwprintf(fp,L"\r\n[ABM AV Player process]\r\nWorking set: %llu MB\r\nPage file usage: %llu MB\r\n", (unsigned long long)(pmc.WorkingSetSize/(1024ULL*1024ULL)),(unsigned long long)(pmc.PagefileUsage/(1024ULL*1024ULL)));
 fwprintf(fp,L"\r\n[Playback]\r\nState: %s\r\nVideo dimensions: %u x %u\r\nPlaylist entries: %d\r\nRecent entries: %d\r\nVolume: %d%%\r\nPlayback speed: %.2fx\r\nShuffle: %s\r\nRepeat mode: %s\r\n",playback,g_videoWidth,g_videoHeight,g_playlistCount,g_recentCount,g_volume,(double)g_playbackRate,g_shuffle?L"On":L"Off",g_repeat==2?L"All":(g_repeat==1?L"Current":L"Off"));
 fwprintf(fp,L"\r\n[Advanced settings]\r\nDecoder threads: %s\r\nDrop late frames: %s\r\nSkip frames: %s\r\nNetwork caching: %d ms\r\nFile caching: %d ms\r\nLive caching: %d ms\r\nHardware acceleration selection: %d\r\nAudio output selection: %d\r\nAspect ratio selection: %d\r\n",g_advancedDecoderThreads==0?L"Automatic":g_advancedDecoderThreads==2?L"2":g_advancedDecoderThreads==4?L"4":L"8",g_advancedDropLateFrames?L"Enabled":L"Disabled",g_advancedSkipFrames?L"Enabled":L"Disabled",g_advancedNetworkCaching,g_advancedFileCaching,g_advancedLiveCaching,g_advancedHwMode,g_advancedAudioOutMode,g_advancedAspectMode);
 fwprintf(fp,L"\r\n[Notes]\r\nThis report contains local application, system and playback configuration details. It does not include media file contents or upload anything. Review it before sharing.\r\n");
 fclose(fp);return TRUE;
}
static BOOL LoadAdvancedDiagnosticPreview(const wchar_t *path){
 if(!g_advancedDiagnosticEdit||!path)return FALSE;
 FILE *fp=NULL;if(_wfopen_s(&fp,path,L"rt, ccs=UTF-8")!=0||!fp)return FALSE;
 wchar_t *buffer=(wchar_t*)calloc(65536,sizeof(wchar_t));if(!buffer){fclose(fp);return FALSE;}
 size_t used=0;wchar_t chunk[2048];
 while(fgetws(chunk,(int)_countof(chunk),fp)){size_t n=wcslen(chunk);if(used+n>=65535)break;memcpy(buffer+used,chunk,n*sizeof(wchar_t));used+=n;}
 buffer[used]=L'\0';fclose(fp);SetWindowTextW(g_advancedDiagnosticEdit,buffer);free(buffer);
 SendMessageW(g_advancedDiagnosticEdit,EM_SETSEL,0,0);SendMessageW(g_advancedDiagnosticEdit,EM_SCROLLCARET,0,0);return TRUE;
}
static LRESULT CALLBACK AdvancedSettingsWndProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
 case WM_THEMECHANGED:
  if(g_advancedDiagnosticBrush){DeleteObject(g_advancedDiagnosticBrush);g_advancedDiagnosticBrush=CreateSolidBrush(ABM_UI_COLOR_INPUT);}
  if(g_advancedVlcReportBrush){DeleteObject(g_advancedVlcReportBrush);g_advancedVlcReportBrush=CreateSolidBrush(ABM_UI_COLOR_SURFACE_RAISED);}
  InvalidateRect(w,NULL,FALSE);return 0;
 case WM_CREATE:
  if(!ABM_D2D_Init(&g_advancedSettingsUi,w))return -1;
  g_advancedSettingsUiReady=TRUE;
  g_advancedSettingsPage=g_advancedSettingsStartPage;
  if(g_advancedSettingsPage<0||g_advancedSettingsPage>=11)g_advancedSettingsPage=0;
  g_advancedSettingsStartPage=0;
  SetWindowTextW(w,g_advancedSettingsPage==4?L"ABM AV Player — Subtitle Manager":g_advancedSettingsPage==10?L"ABM AV Player — Advanced Audio Centre":L"ABM AV Player — Advanced Settings");
  ScanVlcRuntime();
  g_advancedDiagnosticBrush=CreateSolidBrush(ABM_UI_COLOR_INPUT);
  g_advancedDiagnosticFont=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Consolas");
   g_advancedVlcReportBrush=CreateSolidBrush(ABM_UI_COLOR_SURFACE_RAISED);
   g_advancedVlcReportEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VSCROLL|ES_LEFT|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|ES_NOHIDESEL|ES_WANTRETURN,326,532,684,112,w,(HMENU)6409,GetModuleHandleW(NULL),NULL);
   if(g_advancedVlcReportEdit){SendMessageW(g_advancedVlcReportEdit,WM_SETFONT,(WPARAM)g_font,TRUE);SendMessageW(g_advancedVlcReportEdit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELONG(10,10));SetWindowTheme(g_advancedVlcReportEdit,L"DarkMode_Explorer",NULL);ShowWindow(g_advancedVlcReportEdit,SW_HIDE);UpdateVlcReportText();}
  g_advancedDiagnosticEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"Generate a diagnostic report to preview its contents here.",
   WS_CHILD|WS_VSCROLL|ES_LEFT|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|ES_NOHIDESEL,
   326,514,684,132,w,(HMENU)6408,GetModuleHandleW(NULL),NULL);
  if(g_advancedDiagnosticEdit){SendMessageW(g_advancedDiagnosticEdit,WM_SETFONT,(WPARAM)(g_advancedDiagnosticFont?g_advancedDiagnosticFont:GetStockObject(DEFAULT_GUI_FONT)),TRUE);ShowWindow(g_advancedDiagnosticEdit,SW_HIDE);}
  g_advancedHwDraft=g_advancedHwMode;g_advancedHwEnvOverride=FALSE;
  {wchar_t p[PATH_CAP]={0};StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);int v=GetPrivateProfileIntW(L"Video",L"AspectRatio",0,p);g_advancedAspectMode=(v>=0&&v<=3)?v:0;ApplyAdvancedAspectRatio();}
  {wchar_t p[PATH_CAP]={0};StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);int v=GetPrivateProfileIntW(L"Audio",L"OutputEngine",0,p);if(v<0||v>2)v=0;g_advancedAudioOutMode=v;g_advancedAudioOutDraft=v;}
  {wchar_t p[PATH_CAP]={0};StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);int v=GetPrivateProfileIntW(L"Performance",L"DecoderThreads",0,p);g_advancedDecoderThreads=(v==0||v==2||v==4||v==8)?v:0;g_advancedDropLateFrames=GetPrivateProfileIntW(L"Performance",L"DropLateFrames",1,p)!=0;g_advancedSkipFrames=GetPrivateProfileIntW(L"Performance",L"SkipFrames",0,p)!=0;}
  {wchar_t p[PATH_CAP]={0};StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);int v=GetPrivateProfileIntW(L"Streaming",L"NetworkCachingMs",1000,p);g_advancedNetworkCaching=(v==300||v==1000||v==2000||v==5000)?v:1000;v=GetPrivateProfileIntW(L"Streaming",L"FileCachingMs",300,p);g_advancedFileCaching=(v==300||v==1000||v==2000||v==5000)?v:300;v=GetPrivateProfileIntW(L"Streaming",L"LiveCachingMs",1000,p);g_advancedLiveCaching=(v==300||v==1000||v==2000||v==5000)?v:1000;}
  {wchar_t p[PATH_CAP]={0};StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);int v=GetPrivateProfileIntW(L"Subtitles",L"RelativeFontSize",13,p);if(v!=10&&v!=13&&v!=16&&v!=19)v=13;g_advancedSubtitleFontSize=v;}
  {wchar_t p[PATH_CAP]={0};StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);int v=GetPrivateProfileIntW(L"Subtitles",L"TextColor",16777215,p);if(v!=16777215&&v!=16776960&&v!=65535&&v!=65280)v=16777215;g_advancedSubtitleColor=v;}
  {wchar_t p[PATH_CAP]={0};StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);g_advancedSubtitleBold=GetPrivateProfileIntW(L"Subtitles",L"Bold",0,p)!=0;}
  {wchar_t p[PATH_CAP]={0};StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);int v=GetPrivateProfileIntW(L"Subtitles",L"OutlineThickness",4,p);if(v!=0&&v!=2&&v!=4&&v!=6)v=4;g_advancedSubtitleOutline=v;}
  {wchar_t p[PATH_CAP]={0};StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);int v=GetPrivateProfileIntW(L"Subtitles",L"Margin",10,p);if(v!=0&&v!=10&&v!=25&&v!=50)v=10;g_advancedSubtitleMargin=v;}
  {wchar_t env[32]={0};if(GetEnvironmentVariableW(L"ABM_VLC_HW",env,(DWORD)_countof(env))>0){
   if(!_wcsicmp(env,L"none")){g_advancedHwDraft=1;g_advancedHwEnvOverride=TRUE;}
   else if(!_wcsicmp(env,L"d3d11va")){g_advancedHwDraft=2;g_advancedHwEnvOverride=TRUE;}
  }}
  return 0;
 case WM_MOUSEMOVE:{int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);AdvancedSettingsClientToDesign(w,&x,&y);if(x!=g_advancedHoverX||y!=g_advancedHoverY){g_advancedHoverX=x;g_advancedHoverY=y;TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,w,0};TrackMouseEvent(&tme);InvalidateRect(w,NULL,FALSE);}return 0;}
 case WM_MOUSELEAVE:g_advancedHoverX=g_advancedHoverY=-1;g_advancedMousePressed=FALSE;InvalidateRect(w,NULL,FALSE);return 0;
 case WM_CANCELMODE:case WM_CAPTURECHANGED:g_advancedMousePressed=FALSE;if(GetCapture()==w)ReleaseCapture();InvalidateRect(w,NULL,FALSE);return 0;
 case WM_LBUTTONDOWN:{g_advancedMousePressed=TRUE;SetCapture(w);InvalidateRect(w,NULL,FALSE);
  int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);AdvancedSettingsClientToDesign(w,&x,&y);
  for(int i=0;i<(int)_countof(g_advancedSettingsPages);i++){
   /* Match the exact row origin used by the navigation renderer. */
   int top=151+i*50;
   if(AdvancedSettingsPoint(x,y,22,top,254,top+44)){
    g_advancedSettingsPage=i;if(g_advancedDiagnosticEdit)ShowWindow(g_advancedDiagnosticEdit,i==8?SW_SHOW:SW_HIDE);if(g_advancedVlcReportEdit)ShowWindow(g_advancedVlcReportEdit,i==9?SW_SHOW:SW_HIDE);if(i==9){ScanVlcRuntime();UpdateVlcReportText();}InvalidateRect(w,NULL,FALSE);return 0;
   }
  }
  if(g_advancedSettingsPage==3){
   for(int n=0;n<4;n++){
    int top=260+n*74;
    if(AdvancedSettingsPoint(x,y,320,top,1018,top+66)){
     g_advancedAspectMode=n;ApplyAdvancedAspectRatio();
     if(EnsureDataDirectory()){
      wchar_t p[PATH_CAP],v[16];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);StringCchPrintfW(v,_countof(v),L"%d",n);
      WritePrivateProfileStringW(L"Video",L"AspectRatio",v,p);
     }
     InvalidateRect(w,NULL,FALSE);return 0;
    }
   }
  }
  if(g_advancedSettingsPage==5){
   int *values[3]={&g_advancedNetworkCaching,&g_advancedFileCaching,&g_advancedLiveCaching};
   const wchar_t *keys[3]={L"NetworkCachingMs",L"FileCachingMs",L"LiveCachingMs"};
   for(int n=0;n<3;n++)if(AdvancedSettingsPoint(x,y,320,260+n*104,1018,344+n*104)){
    int current=0;for(int j=0;j<4;j++)if(g_advancedCacheChoices[j]==*values[n])current=j;
    *values[n]=g_advancedCacheChoices[(current+1)%4];
    if(EnsureDataDirectory()){wchar_t p[PATH_CAP],v[16];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);StringCchPrintfW(v,_countof(v),L"%d",*values[n]);WritePrivateProfileStringW(L"Streaming",keys[n],v,p);}
    InvalidateRect(w,NULL,FALSE);return 0;
   }
  }
  if(g_advancedSettingsPage==9){if(AdvancedSettingsPoint(x,y,326,466,520,502)){ScanVlcRuntime();UpdateVlcReportText();InvalidateRect(w,NULL,FALSE);return 0;}}
  if(g_advancedSettingsPage==8){
   if(AdvancedSettingsPoint(x,y,326,442,610,478)){wchar_t report[PATH_CAP];if(GenerateAdvancedDiagnosticReport(report,_countof(report))){if(!LoadAdvancedDiagnosticPreview(report))ABM_MessageBoxW(w,L"The report was saved, but its contents could not be loaded for preview.",APP_NAME,MB_ICONWARNING);}else ABM_MessageBoxW(w,L"Could not create the diagnostic report. Check that the ABM AV Player data folder is writable.",APP_NAME,MB_ICONERROR);return 0;}
   if(AdvancedSettingsPoint(x,y,626,442,850,478)){if(EnsureDataDirectory())ShellExecuteW(w,L"open",g_dataDir,NULL,NULL,SW_SHOWNORMAL);else ABM_MessageBoxW(w,L"Could not access the ABM AV Player data folder.",APP_NAME,MB_ICONERROR);return 0;}
  }
  if(g_advancedSettingsPage==7){
   BOOL *values[3]={&g_advancedConfirmExit,&g_advancedConfirmClearPlaylist,&g_advancedDoubleClickFullscreen};
   const wchar_t *keys[3]={L"ConfirmExit",L"ConfirmClearPlaylist",L"DoubleClickFullscreen"};
   for(int n=0;n<3;n++)if(AdvancedSettingsPoint(x,y,320,260+n*112,1018,356+n*112)){*values[n]=!*values[n];if(EnsureDataDirectory()){wchar_t p[PATH_CAP];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);WritePrivateProfileStringW(L"Controls",keys[n],*values[n]?L"1":L"0",p);}InvalidateRect(w,NULL,FALSE);return 0;}
  }
  if(g_advancedSettingsPage==6){
   if(AdvancedSettingsPoint(x,y,320,260,1018,326)){
    int current=0;for(int j=0;j<4;j++)if(g_advancedThreadChoices[j]==g_advancedDecoderThreads)current=j;
    g_advancedDecoderThreads=g_advancedThreadChoices[(current+1)%4];
    if(EnsureDataDirectory()){wchar_t p[PATH_CAP],v[16];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);StringCchPrintfW(v,_countof(v),L"%d",g_advancedDecoderThreads);WritePrivateProfileStringW(L"Performance",L"DecoderThreads",v,p);}
    InvalidateRect(w,NULL,FALSE);return 0;
   }
   if(AdvancedSettingsPoint(x,y,320,350,1018,416)){
    g_advancedDropLateFrames=!g_advancedDropLateFrames;
    if(EnsureDataDirectory()){wchar_t p[PATH_CAP];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);WritePrivateProfileStringW(L"Performance",L"DropLateFrames",g_advancedDropLateFrames?L"1":L"0",p);}
    InvalidateRect(w,NULL,FALSE);return 0;
   }
   if(AdvancedSettingsPoint(x,y,320,440,1018,506)){
    g_advancedSkipFrames=!g_advancedSkipFrames;
    if(EnsureDataDirectory()){wchar_t p[PATH_CAP];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);WritePrivateProfileStringW(L"Performance",L"SkipFrames",g_advancedSkipFrames?L"1":L"0",p);}
    InvalidateRect(w,NULL,FALSE);return 0;
   }
  }
  if(g_advancedSettingsPage==2){   if(AdvancedSettingsPoint(x,y,320,260,1018,326)){g_advancedAudioOutDraft=0;InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,320,334,1018,400)){g_advancedAudioOutDraft=1;InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,320,408,1018,474)){g_advancedAudioOutDraft=2;InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,870,594,1032,638)){
    if(EnsureDataDirectory()){
     wchar_t path[PATH_CAP],value[16];StringCchPrintfW(path,_countof(path),L"%s\\settings.ini",g_dataDir);
     StringCchPrintfW(value,_countof(value),L"%d",g_advancedAudioOutDraft);
     if(WritePrivateProfileStringW(L"Audio",L"OutputEngine",value,path)){
      g_advancedAudioOutMode=g_advancedAudioOutDraft;
      StringCchCopyW(g_advancedAudioOutMessage,_countof(g_advancedAudioOutMessage),L"Audio output preference saved.");
      InvalidateRect(w,NULL,FALSE);
      if(ABM_MessageBoxW(w,L"The audio output engine preference has been saved. Restart ABM AV Player now to activate it?",L"Restart ABM AV Player",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES){
       wchar_t exePath[PATH_CAP]={0};DWORD exeLen=GetModuleFileNameW(NULL,exePath,(DWORD)_countof(exePath));
       if(exeLen>0&&exeLen<_countof(exePath)){
        HINSTANCE launched=ShellExecuteW(NULL,L"open",exePath,NULL,NULL,SW_SHOWNORMAL);
        if((INT_PTR)launched>32){HWND mainWindow=g_main;DestroyWindow(w);if(mainWindow&&IsWindow(mainWindow))PostMessageW(mainWindow,WM_CLOSE,0,0);return 0;}
       }
       StringCchCopyW(g_advancedAudioOutMessage,_countof(g_advancedAudioOutMessage),L"Saved, but restart could not be started. Please close and reopen ABM AV Player.");
      }else StringCchCopyW(g_advancedAudioOutMessage,_countof(g_advancedAudioOutMessage),L"Saved. Restart ABM AV Player later to activate this output engine.");
     }else StringCchCopyW(g_advancedAudioOutMessage,_countof(g_advancedAudioOutMessage),L"Could not save the audio output preference.");
    }else StringCchCopyW(g_advancedAudioOutMessage,_countof(g_advancedAudioOutMessage),L"Could not access the ABM AV Player settings folder.");
    InvalidateRect(w,NULL,FALSE);return 0;
   }
  }
  if(g_advancedSettingsPage==10){
   if(AdvancedSettingsPoint(x,y,320,260,1018,326)){g_advancedAudioOutDraft=0;InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,320,334,1018,400)){g_advancedAudioOutDraft=1;InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,320,408,1018,474)){g_advancedAudioOutDraft=2;InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,320,486,660,530)){g_replayGain=!g_replayGain;SaveAudioPreferences();InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,676,486,1018,530)){g_compressor=!g_compressor;SaveAudioPreferences();InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,720,540,820,580)){if(g_audioDelayUs>-10000000)g_audioDelayUs-=50000;if(g_player)libvlc_audio_set_delay(g_player,g_audioDelayUs);SaveAudioPreferences();InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,830,540,930,580)){if(g_audioDelayUs<10000000)g_audioDelayUs+=50000;if(g_player)libvlc_audio_set_delay(g_player,g_audioDelayUs);SaveAudioPreferences();InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,940,540,1018,580)){g_audioDelayUs=0;if(g_player)libvlc_audio_set_delay(g_player,0);SaveAudioPreferences();InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,322,594,440,638)){ShowAudioTrackMenu(w);return 0;}
   if(AdvancedSettingsPoint(x,y,452,594,660,638)){ShowEqualizer();return 0;}
   if(AdvancedSettingsPoint(x,y,870,594,1032,638)){
    if(EnsureDataDirectory()){
     wchar_t path[PATH_CAP],value[16];StringCchPrintfW(path,_countof(path),L"%s\\settings.ini",g_dataDir);StringCchPrintfW(value,_countof(value),L"%d",g_advancedAudioOutDraft);
     if(WritePrivateProfileStringW(L"Audio",L"OutputEngine",value,path)){
      g_advancedAudioOutMode=g_advancedAudioOutDraft;StringCchCopyW(g_advancedAudioOutMessage,_countof(g_advancedAudioOutMessage),L"Audio output preference saved.");InvalidateRect(w,NULL,FALSE);
      if(ABM_MessageBoxW(w,L"The audio output engine preference has been saved. Restart ABM AV Player now to activate it?",L"Restart ABM AV Player",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES){
       wchar_t exePath[PATH_CAP]={0};DWORD exeLen=GetModuleFileNameW(NULL,exePath,(DWORD)_countof(exePath));
       if(exeLen>0&&exeLen<_countof(exePath)){HINSTANCE launched=ShellExecuteW(NULL,L"open",exePath,NULL,NULL,SW_SHOWNORMAL);if((INT_PTR)launched>32){HWND mainWindow=g_main;DestroyWindow(w);if(mainWindow&&IsWindow(mainWindow))PostMessageW(mainWindow,WM_CLOSE,0,0);return 0;}}
       StringCchCopyW(g_advancedAudioOutMessage,_countof(g_advancedAudioOutMessage),L"Saved, but restart could not be started. Please close and reopen ABM AV Player.");
      }else StringCchCopyW(g_advancedAudioOutMessage,_countof(g_advancedAudioOutMessage),L"Saved. Restart ABM AV Player later to activate this output engine.");
     }else StringCchCopyW(g_advancedAudioOutMessage,_countof(g_advancedAudioOutMessage),L"Could not save the audio output preference.");
    }else StringCchCopyW(g_advancedAudioOutMessage,_countof(g_advancedAudioOutMessage),L"Could not access the ABM AV Player settings folder.");
    InvalidateRect(w,NULL,FALSE);return 0;
   }
  }
  if(g_advancedSettingsPage==4){
   if(AdvancedSettingsPoint(x,y,320,624,540,662)){ShowSubtitleTrackMenu(w);return 0;}
   if(AdvancedSettingsPoint(x,y,552,624,772,662)){if(g_player)libvlc_video_set_spu(g_player,-1);InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,784,624,1004,662)){LoadExternalSubtitle(w);InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,770,264,850,308)){if(g_advancedSubtitleDelayUs>-10000000)g_advancedSubtitleDelayUs-=50000;if(g_player)libvlc_video_set_spu_delay(g_player,g_advancedSubtitleDelayUs);if(EnsureDataDirectory()){wchar_t p[PATH_CAP],v[32];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);StringCchPrintfW(v,_countof(v),L"%d",(int)(g_advancedSubtitleDelayUs/1000));WritePrivateProfileStringW(L"Subtitles",L"DelayMs",v,p);}InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,860,264,940,308)){if(g_advancedSubtitleDelayUs<10000000)g_advancedSubtitleDelayUs+=50000;if(g_player)libvlc_video_set_spu_delay(g_player,g_advancedSubtitleDelayUs);if(EnsureDataDirectory()){wchar_t p[PATH_CAP],v[32];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);StringCchPrintfW(v,_countof(v),L"%d",(int)(g_advancedSubtitleDelayUs/1000));WritePrivateProfileStringW(L"Subtitles",L"DelayMs",v,p);}InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,950,264,1032,308)){g_advancedSubtitleDelayUs=0;if(g_player)libvlc_video_set_spu_delay(g_player,0);if(EnsureDataDirectory()){wchar_t p[PATH_CAP];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);WritePrivateProfileStringW(L"Subtitles",L"DelayMs",L"0",p);}InvalidateRect(w,NULL,FALSE);return 0;}
   for(int i=0;i<4;i++){
    int left=320+i*176;
    if(AdvancedSettingsPoint(x,y,left,366,left+164,410)){
     g_advancedSubtitleFontSize=g_subtitleFontSizes[i];
     if(EnsureDataDirectory()){wchar_t p[PATH_CAP],v[16];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);StringCchPrintfW(v,_countof(v),L"%d",g_advancedSubtitleFontSize);WritePrivateProfileStringW(L"Subtitles",L"RelativeFontSize",v,p);}
     InvalidateRect(w,NULL,FALSE);return 0;
    }
    if(AdvancedSettingsPoint(x,y,left,440,left+164,484)){
     g_advancedSubtitleColor=g_subtitleColors[i];
     if(EnsureDataDirectory()){wchar_t p[PATH_CAP],v[16];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);StringCchPrintfW(v,_countof(v),L"%d",g_advancedSubtitleColor);WritePrivateProfileStringW(L"Subtitles",L"TextColor",v,p);}
     InvalidateRect(w,NULL,FALSE);return 0;
    }
    if(AdvancedSettingsPoint(x,y,left,578,left+164,622)){
     g_advancedSubtitleMargin=g_subtitleMarginValues[i];
     if(EnsureDataDirectory()){wchar_t p[PATH_CAP],v[16];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);StringCchPrintfW(v,_countof(v),L"%d",g_advancedSubtitleMargin);WritePrivateProfileStringW(L"Subtitles",L"Margin",v,p);}
     InvalidateRect(w,NULL,FALSE);return 0;
    }
    if(AdvancedSettingsPoint(x,y,520+i*126,510,640+i*126,552)){
     g_advancedSubtitleOutline=g_subtitleOutlineValues[i];
     if(EnsureDataDirectory()){wchar_t p[PATH_CAP],v[16];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);StringCchPrintfW(v,_countof(v),L"%d",g_advancedSubtitleOutline);WritePrivateProfileStringW(L"Subtitles",L"OutlineThickness",v,p);}
     InvalidateRect(w,NULL,FALSE);return 0;
    }
   }
   if(AdvancedSettingsPoint(x,y,320,510,500,552)){
    g_advancedSubtitleBold=!g_advancedSubtitleBold;
    if(EnsureDataDirectory()){wchar_t p[PATH_CAP];StringCchPrintfW(p,_countof(p),L"%s\\settings.ini",g_dataDir);WritePrivateProfileStringW(L"Subtitles",L"Bold",g_advancedSubtitleBold?L"1":L"0",p);}
    InvalidateRect(w,NULL,FALSE);return 0;
   }
  }
  if(g_advancedSettingsPage==1){
   if(AdvancedSettingsPoint(x,y,320,260,1018,326)){g_advancedHwDraft=0;InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,320,334,1018,400)){g_advancedHwDraft=1;InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,320,408,1018,474)){g_advancedHwDraft=2;InvalidateRect(w,NULL,FALSE);return 0;}
   if(AdvancedSettingsPoint(x,y,870,594,1032,638)){
    if(g_advancedHwEnvOverride)StringCchCopyW(g_advancedHwMessage,_countof(g_advancedHwMessage),L"ABM_VLC_HW environment override is active. Remove it to use the saved preference.");
    else if(EnsureDataDirectory()){
     wchar_t path[PATH_CAP],value[16];StringCchPrintfW(path,_countof(path),L"%s\\settings.ini",g_dataDir);
     StringCchPrintfW(value,_countof(value),L"%d",g_advancedHwDraft);
     if(WritePrivateProfileStringW(L"Video",L"HardwareAcceleration",value,path)){
      g_advancedHwMode=g_advancedHwDraft;
      StringCchCopyW(g_advancedHwMessage,_countof(g_advancedHwMessage),L"Decoder preference saved.");
      InvalidateRect(w,NULL,FALSE);
      if(ABM_MessageBoxW(w,L"The hardware-decoding preference has been saved. Restart ABM AV Player now to activate it?",L"Restart ABM AV Player",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES){
       wchar_t exePath[PATH_CAP]={0};
       DWORD exeLen=GetModuleFileNameW(NULL,exePath,(DWORD)_countof(exePath));
       if(exeLen>0&&exeLen<_countof(exePath)){
        HINSTANCE launched=ShellExecuteW(NULL,L"open",exePath,NULL,NULL,SW_SHOWNORMAL);
        if((INT_PTR)launched>32){
         HWND mainWindow=g_main;
         DestroyWindow(w);
         if(mainWindow&&IsWindow(mainWindow))PostMessageW(mainWindow,WM_CLOSE,0,0);
         return 0;
        }
       }
       StringCchCopyW(g_advancedHwMessage,_countof(g_advancedHwMessage),L"Saved, but restart could not be started. Please close and reopen ABM AV Player.");
      }else StringCchCopyW(g_advancedHwMessage,_countof(g_advancedHwMessage),L"Saved. Restart ABM AV Player later to activate this decoder mode.");
     }else StringCchCopyW(g_advancedHwMessage,_countof(g_advancedHwMessage),L"Could not save the decoder preference. Check folder permissions.");
    }else StringCchCopyW(g_advancedHwMessage,_countof(g_advancedHwMessage),L"Could not access the ABM AV Player settings folder.");
    InvalidateRect(w,NULL,FALSE);return 0;
   }
  }
  if(AdvancedSettingsPoint(x,y,914,664,1040,708)){DestroyWindow(w);return 0;}
  return 0;
 }
 case WM_LBUTTONUP:{
  g_advancedMousePressed=FALSE;if(GetCapture()==w)ReleaseCapture();InvalidateRect(w,NULL,FALSE);
  int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);AdvancedSettingsClientToDesign(w,&x,&y);
  if(g_advancedSettingsPage==9&&AdvancedSettingsPoint(x,y,326,466,520,502)){ScanVlcRuntime();UpdateVlcReportText();InvalidateRect(w,NULL,FALSE);return 0;}
  break;
 }
 case WM_KEYDOWN:
  if(wp==VK_ESCAPE){DestroyWindow(w);return 0;}
  if(wp==VK_UP||wp==VK_DOWN){
   int n=(int)_countof(g_advancedSettingsPages);
   g_advancedSettingsPage=(g_advancedSettingsPage+(wp==VK_DOWN?1:n-1))%n;
   if(g_advancedDiagnosticEdit)ShowWindow(g_advancedDiagnosticEdit,g_advancedSettingsPage==8?SW_SHOW:SW_HIDE);
   if(g_advancedVlcReportEdit)ShowWindow(g_advancedVlcReportEdit,g_advancedSettingsPage==9?SW_SHOW:SW_HIDE);
   InvalidateRect(w,NULL,FALSE);return 0;
  }
  break;
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{
  PAINTSTRUCT ps;BeginPaint(w,&ps);
  if(g_advancedSettingsUiReady){
   /* This window redraws a compact, fixed-coordinate settings surface.
      Clear and repaint the complete Direct2D target on every WM_PAINT.
      Partial dirty-rectangle clipping left stale horizontal strips visible
      across controls after invalidation/resizing on some DPI scales. */
   ABM_D2D_Begin(&g_advancedSettingsUi,SETTINGS_BG);
   ABM_D2D_GradientRound(&g_advancedSettingsUi,18,14,1062,92,16,
       ABM_THEME_COLOR_0F2739,ABM_THEME_COLOR_0D1E2F);
   ABM_D2D_StrokeRound(&g_advancedSettingsUi,18.5f,14.5f,1061.5f,91.5f,16,
       ABM_THEME_COLOR_2B586F,1.0f);
   ABM_D2D_Round(&g_advancedSettingsUi,34,29,78,73,12,ABM_THEME_COLOR_1C9D9D);
   ABM_D2D_StrokeRound(&g_advancedSettingsUi,34,29,78,73,12,ABM_THEME_COLOR_73D2CF,1.0f);
   AdvancedSettingsText(L"⚙",39,31,73,70,ABM_THEME_COLOR_FFFFFF,2,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
   AdvancedSettingsText(g_advancedSettingsPage==4?L"SUBTITLE MANAGER":g_advancedSettingsPage==10?L"ADVANCED AUDIO CENTRE":L"ADVANCED SETTINGS",94,24,660,57,ABM_THEME_COLOR_F7FAFF,2,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   AdvancedSettingsText(g_advancedSettingsPage==4?L"TRACKS  ·  SYNCHRONIZATION  ·  APPEARANCE":g_advancedSettingsPage==10?L"OUTPUT  ·  EQUALIZER  ·  AUDIO PROCESSING":L"PLAYBACK ENGINE  ·  OUTPUT  ·  PERFORMANCE",96,55,760,78,SETTINGS_ACCENT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   ABM_D2D_Round(&g_advancedSettingsUi,18,106,258,718,14,SETTINGS_PANEL);
   ABM_D2D_StrokeRound(&g_advancedSettingsUi,18.5f,106.5f,257.5f,717.5f,14,SETTINGS_BORDER,0.8f);
   AdvancedSettingsText(L"CONFIGURATION",34,119,238,145,SETTINGS_ACCENT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   for(int i=0;i<(int)_countof(g_advancedSettingsPages);i++){
    float top=(float)(151+i*50);
    BOOL selected=(i==g_advancedSettingsPage);
    if(selected){
     ABM_D2D_Round(&g_advancedSettingsUi,28,top,248,top+44,9,ABM_UI_COLOR_SURFACE_BLUE);
     ABM_D2D_Round(&g_advancedSettingsUi,28,top+8,31,top+36,1.5f,ABM_UI_COLOR_ACCENT_BRIGHT);
    }
    AdvancedSettingsText(g_advancedSettingsPages[i],42,(int)top+1,237,(int)top+43,
       selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_FG,selected?1:0,
       DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   }
   ABM_D2D_Round(&g_advancedSettingsUi,274,106,1062,718,14,SETTINGS_PANEL);
   ABM_D2D_StrokeRound(&g_advancedSettingsUi,274.5f,106.5f,1061.5f,717.5f,14,SETTINGS_BORDER,0.8f);
   AdvancedSettingsText(g_advancedSettingsPages[g_advancedSettingsPage],304,130,1028,174,
       ABM_THEME_COLOR_F7FAFF,2,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   AdvancedSettingsText(g_advancedSettingsDescriptions[g_advancedSettingsPage],306,178,1028,216,
       SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   if(g_advancedSettingsPage==2){
    AdvancedSettingsText(L"Audio output engine",322,232,1018,258,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    const wchar_t *outNames[3]={L"Automatic (recommended)",L"Windows Audio Session API (WASAPI)",L"DirectSound"};
    const wchar_t *outDesc[3]={L"Let LibVLC select the best available Windows audio output.",L"Use the Windows modern audio endpoint engine for device routing and low-latency playback.",L"Use the DirectSound output module for compatibility troubleshooting."};
    for(int i=0;i<3;i++){
     int top=260+i*74;BOOL selected=(g_advancedAudioOutDraft==i);
     ABM_D2D_Round(&g_advancedSettingsUi,320,(float)top,1018,(float)top+66,9,selected?ABM_UI_COLOR_SURFACE_BLUE:ABM_UI_COLOR_SURFACE_RAISED);
     ABM_D2D_StrokeRound(&g_advancedSettingsUi,320.5f,(float)top+0.5f,1017.5f,(float)top+65.5f,9,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_BORDER,selected?1.5f:0.8f);
     ABM_D2D_Round(&g_advancedSettingsUi,338,(float)top+22,356,(float)top+40,9,selected?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_SURFACE);
     if(selected)ABM_D2D_Round(&g_advancedSettingsUi,343,(float)top+27,351,(float)top+35,4,ABM_THEME_COLOR_0D1E2F);
     AdvancedSettingsText(outNames[i],372,top+8,990,top+32,SETTINGS_FG,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
     AdvancedSettingsText(outDesc[i],372,top+34,990,top+59,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
    AdvancedSettingsText(L"Your selected playback device remains managed by the existing Output selector.",322,492,1018,532,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    AdvancedSettingsButtonDraw(870,594,1032,638,L"Apply preference",TRUE);
    AdvancedSettingsText(g_advancedAudioOutMessage,322,548,840,636,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_WORDBREAK);
   }else if(g_advancedSettingsPage==10){
    AdvancedSettingsText(L"Audio output engine",322,232,1018,258,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    const wchar_t *outNames[3]={L"Automatic (recommended)",L"Windows Audio Session API (WASAPI)",L"DirectSound"};
    const wchar_t *outDesc[3]={L"Let LibVLC select the best available Windows audio output.",L"Use the Windows modern audio endpoint engine for device routing and low-latency playback.",L"Use the DirectSound output module for compatibility troubleshooting."};
    for(int i=0;i<3;i++){int top=260+i*74;BOOL selected=(g_advancedAudioOutDraft==i);ABM_D2D_Round(&g_advancedSettingsUi,320,(float)top,1018,(float)top+66,9,selected?ABM_UI_COLOR_SURFACE_BLUE:ABM_UI_COLOR_SURFACE_RAISED);ABM_D2D_StrokeRound(&g_advancedSettingsUi,320.5f,(float)top+0.5f,1017.5f,(float)top+65.5f,9,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_BORDER,selected?1.5f:0.8f);ABM_D2D_Round(&g_advancedSettingsUi,338,(float)top+22,356,(float)top+40,9,selected?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_SURFACE);if(selected)ABM_D2D_Round(&g_advancedSettingsUi,343,(float)top+27,351,(float)top+35,4,ABM_THEME_COLOR_0D1E2F);AdvancedSettingsText(outNames[i],372,top+8,990,top+32,SETTINGS_FG,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);AdvancedSettingsText(outDesc[i],372,top+34,990,top+59,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);}
    AdvancedSettingsButtonDraw(320,486,660,530,g_replayGain?L"ReplayGain: On":L"ReplayGain: Off",g_replayGain);
    AdvancedSettingsButtonDraw(676,486,1018,530,g_compressor?L"Dynamic-range compressor: On":L"Dynamic-range compressor: Off",g_compressor);
    AdvancedSettingsText(L"Audio synchronization",322,540,510,580,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    AdvancedSettingsButtonDraw(720,540,820,580,L"− 50 ms",FALSE);AdvancedSettingsButtonDraw(830,540,930,580,L"+ 50 ms",FALSE);AdvancedSettingsButtonDraw(940,540,1018,580,L"Reset",FALSE);
    AdvancedSettingsButtonDraw(322,594,440,638,L"Audio track",FALSE);
    AdvancedSettingsButtonDraw(452,594,660,638,L"Advanced equalizer",FALSE);
    AdvancedSettingsButtonDraw(870,594,1032,638,L"Apply output engine",TRUE);
    wchar_t audioDelayText[96];StringCchPrintfW(audioDelayText,_countof(audioDelayText),L"Offset: %+.0f ms",(double)g_audioDelayUs/1000.0);AdvancedSettingsText(audioDelayText,520,540,700,580,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    if(g_advancedAudioOutMessage[0])AdvancedSettingsText(g_advancedAudioOutMessage,322,642,1032,660,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
   }else if(g_advancedSettingsPage==5){
    AdvancedSettingsText(L"Stream buffering",322,232,1018,258,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    int cacheValues[3]={g_advancedNetworkCaching,g_advancedFileCaching,g_advancedLiveCaching};
    const wchar_t *cacheNames[3]={L"Network stream caching",L"Local file caching",L"Live stream caching"};
    const wchar_t *cacheDesc[3]={L"Buffer before playback for network URLs and remote streams.",L"Read-ahead buffer used when opening local media files.",L"Buffer target for live and time-sensitive streams."};
    for(int n=0;n<3;n++){
     int top=260+n*104;int selected=0;for(int j=0;j<4;j++)if(g_advancedCacheChoices[j]==cacheValues[n])selected=j;
     ABM_D2D_Round(&g_advancedSettingsUi,320,(float)top,1018,(float)top+92,9,ABM_UI_COLOR_SURFACE_RAISED);
     ABM_D2D_StrokeRound(&g_advancedSettingsUi,320.5f,(float)top+0.5f,1017.5f,(float)top+91.5f,9,SETTINGS_BORDER,0.8f);
     AdvancedSettingsText(cacheNames[n],340,top+8,720,top+34,SETTINGS_FG,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
     AdvancedSettingsText(cacheDesc[n],340,top+39,760,top+68,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
     AdvancedSettingsButtonDraw(820,top+22,996,top+68,g_advancedCacheLabels[selected],TRUE);
    }
    AdvancedSettingsText(L"Click a value to cycle through 300, 1,000, 2,000 and 5,000 ms. Applies when the next media item opens.",322,584,1018,624,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   }else if(g_advancedSettingsPage==3){
    AdvancedSettingsText(L"Video aspect ratio",322,232,1018,258,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    for(int n=0;n<4;n++){
     int top=260+n*74;BOOL selected=(g_advancedAspectMode==n);
     ABM_D2D_Round(&g_advancedSettingsUi,320,(float)top,1018,(float)top+66,9,selected?ABM_UI_COLOR_SURFACE_BLUE:ABM_UI_COLOR_SURFACE_RAISED);
     ABM_D2D_StrokeRound(&g_advancedSettingsUi,320.5f,(float)top+0.5f,1017.5f,(float)top+65.5f,9,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_BORDER,selected?1.5f:0.8f);
     AdvancedSettingsText(g_advancedAspectLabels[n],344,top+12,990,top+54,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_FG,selected?1:0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
    AdvancedSettingsText(L"Changes apply immediately and are saved for future playback.",322,580,1018,620,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   }else if(g_advancedSettingsPage==1){
    AdvancedSettingsText(L"Hardware acceleration",322,232,1018,258,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    const wchar_t *hwNames[3]={L"Automatic (recommended)",L"Software decoding",L"Direct3D 11 Video Acceleration"};
    const wchar_t *hwDesc[3]={L"Let LibVLC choose the available hardware decoder.",L"Decode on the CPU. Useful for diagnosing GPU or driver issues.",L"Request D3D11VA hardware decoding through the LibVLC video decoder."};
    for(int i=0;i<3;i++){
     int top=260+i*74;BOOL selected=(g_advancedHwDraft==i);
     ABM_D2D_Round(&g_advancedSettingsUi,320,(float)top,1018,(float)top+66,9,selected?ABM_UI_COLOR_SURFACE_BLUE:ABM_UI_COLOR_SURFACE_RAISED);
     ABM_D2D_StrokeRound(&g_advancedSettingsUi,320.5f,(float)top+0.5f,1017.5f,(float)top+65.5f,9,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_BORDER,selected?1.5f:0.8f);
     ABM_D2D_Round(&g_advancedSettingsUi,338,(float)top+22,356,(float)top+40,9,selected?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_SURFACE);
     if(selected)ABM_D2D_Round(&g_advancedSettingsUi,343,(float)top+27,351,(float)top+35,4,ABM_THEME_COLOR_0D1E2F);
     AdvancedSettingsText(hwNames[i],372,top+8,990,top+32,SETTINGS_FG,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
     AdvancedSettingsText(hwDesc[i],372,top+34,990,top+59,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
    AdvancedSettingsText(g_advancedHwEnvOverride?L"Environment override active: ABM_VLC_HW takes precedence over this saved setting.":L"Decoder changes require an application restart. Automatic is the default.",322,492,1018,532,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    AdvancedSettingsButtonDraw(870,594,1032,638,L"Apply preference",TRUE);
    AdvancedSettingsText(g_advancedHwMessage,322,548,840,636,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_WORDBREAK);
   }else if(g_advancedSettingsPage==7){
    AdvancedSettingsText(L"Confirmation and interaction",322,232,1018,258,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    const wchar_t *labels[3]={L"Confirm before exiting ABM AV Player",L"Confirm before clearing the playlist",L"Double-click video to toggle fullscreen"};
    const wchar_t *helps[3]={L"Show a confirmation prompt before closing the application.",L"Ask before removing all playlist entries. Source files are never deleted.",L"Use a double-click on the video area to enter or leave fullscreen."};
    BOOL values[3]={g_advancedConfirmExit,g_advancedConfirmClearPlaylist,g_advancedDoubleClickFullscreen};
    for(int n=0;n<3;n++){int top=260+n*112;ABM_D2D_Round(&g_advancedSettingsUi,320,(float)top,1018,(float)top+96,9,ABM_UI_COLOR_SURFACE_RAISED);ABM_D2D_StrokeRound(&g_advancedSettingsUi,320.5f,(float)top+0.5f,1017.5f,(float)top+95.5f,9,SETTINGS_BORDER,0.8f);AdvancedSettingsText(labels[n],340,top+10,790,top+38,SETTINGS_FG,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);AdvancedSettingsText(helps[n],340,top+43,790,top+80,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_WORDBREAK);AdvancedSettingsButtonDraw(830,top+24,996,top+68,values[n]?L"Enabled":L"Disabled",values[n]);}
    AdvancedSettingsText(L"Preferences are saved immediately and apply to the next matching interaction.",322,612,1018,642,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   }else if(g_advancedSettingsPage==4){
    AdvancedSettingsText(L"Subtitle timing",322,232,1018,254,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    ABM_D2D_Round(&g_advancedSettingsUi,320,258,1018,324,10,ABM_UI_COLOR_SURFACE);
    ABM_D2D_StrokeRound(&g_advancedSettingsUi,320.5f,258.5f,1017.5f,323.5f,10,SETTINGS_BORDER,0.8f);
    wchar_t delayText[64];StringCchPrintfW(delayText,_countof(delayText),L"Offset: %+.0f ms",(double)g_advancedSubtitleDelayUs/1000.0);
    AdvancedSettingsText(delayText,338,270,570,312,SETTINGS_FG,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    AdvancedSettingsButtonDraw(770,268,850,312,L"- 50 ms",FALSE);
    AdvancedSettingsButtonDraw(860,268,940,312,L"+ 50 ms",FALSE);
    AdvancedSettingsButtonDraw(950,268,1032,312,L"Reset",FALSE);
    AdvancedSettingsText(L"Font size",322,334,1018,358,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    for(int i=0;i<4;i++){int left=320+i*176;BOOL selected=(g_advancedSubtitleFontSize==g_subtitleFontSizes[i]);ABM_D2D_Round(&g_advancedSettingsUi,(float)left,366,(float)left+164,410,7,selected?ABM_UI_COLOR_SURFACE_BLUE:ABM_UI_COLOR_SURFACE_RAISED);ABM_D2D_StrokeRound(&g_advancedSettingsUi,(float)left+0.5f,366.5f,(float)left+163.5f,409.5f,7,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_BORDER,selected?1.4f:0.8f);AdvancedSettingsText(g_subtitleFontSizeLabels[i],left+5,367,left+159,409,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_FG,selected?1:0,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
    AdvancedSettingsText(L"Text color",322,414,1018,436,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    for(int i=0;i<4;i++){int left=320+i*176;BOOL selected=(g_advancedSubtitleColor==g_subtitleColors[i]);ABM_D2D_Round(&g_advancedSettingsUi,(float)left,440,(float)left+164,484,7,selected?ABM_UI_COLOR_SURFACE_BLUE:ABM_UI_COLOR_SURFACE_RAISED);ABM_D2D_StrokeRound(&g_advancedSettingsUi,(float)left+0.5f,440.5f,(float)left+163.5f,483.5f,7,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_BORDER,selected?1.4f:0.8f);AdvancedSettingsText(g_subtitleColorLabels[i],left+5,441,left+159,483,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_FG,selected?1:0,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
    AdvancedSettingsText(L"Text style and readability",322,486,1018,508,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    AdvancedSettingsButtonDraw(320,510,500,552,g_advancedSubtitleBold?L"Bold: On":L"Bold: Off",g_advancedSubtitleBold);
    for(int i=0;i<4;i++){int left=520+i*126;BOOL selected=(g_advancedSubtitleOutline==g_subtitleOutlineValues[i]);ABM_D2D_Round(&g_advancedSettingsUi,(float)left,510,(float)left+120,552,7,selected?ABM_UI_COLOR_SURFACE_BLUE:ABM_UI_COLOR_SURFACE_RAISED);ABM_D2D_StrokeRound(&g_advancedSettingsUi,(float)left+0.5f,510.5f,(float)left+119.5f,551.5f,7,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_BORDER,selected?1.4f:0.8f);AdvancedSettingsText(g_subtitleOutlineLabels[i],left+4,511,left+116,551,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_FG,selected?1:0,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
    AdvancedSettingsText(L"Subtitle vertical position",322,554,1018,576,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    for(int i=0;i<4;i++){int left=320+i*176;BOOL selected=(g_advancedSubtitleMargin==g_subtitleMarginValues[i]);ABM_D2D_Round(&g_advancedSettingsUi,(float)left,578,(float)left+164,622,7,selected?ABM_UI_COLOR_SURFACE_BLUE:ABM_UI_COLOR_SURFACE_RAISED);ABM_D2D_StrokeRound(&g_advancedSettingsUi,(float)left+0.5f,578.5f,(float)left+163.5f,621.5f,7,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_BORDER,selected?1.4f:0.8f);AdvancedSettingsText(g_subtitleMarginLabels[i],left+5,579,left+159,621,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_FG,selected?1:0,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
    AdvancedSettingsButtonDraw(320,624,540,662,L"Choose subtitle track",FALSE);
    AdvancedSettingsButtonDraw(552,624,772,662,L"Disable subtitles",FALSE);
    AdvancedSettingsButtonDraw(784,624,1004,662,L"Load external subtitle...",TRUE);
   }else{
    MEMORYSTATUSEX mem={0};mem.dwLength=sizeof(mem);GlobalMemoryStatusEx(&mem);
    SYSTEM_INFO si={0};GetNativeSystemInfo(&si);
    wchar_t row1[256]={0},row2[256]={0},row3[256]={0},row4[256]={0},row5[256]={0};
    const wchar_t *sectionTitle=L"Runtime overview";
    if(g_advancedSettingsPage==0){
     sectionTitle=L"Current playback session";
     StringCchPrintfW(row1,_countof(row1),L"Playback: %s",g_hasMedia?(g_overlayPlaying?L"Playing":L"Loaded / paused"):L"No media loaded");
     StringCchPrintfW(row2,_countof(row2),L"Video dimensions: %u × %u",g_videoWidth,g_videoHeight);
     StringCchPrintfW(row3,_countof(row3),L"Volume: %d%%     Speed: %.2fx",g_volume,(double)g_playbackRate);
     StringCchPrintfW(row4,_countof(row4),L"Playlist: %d item(s)     Current index: %d",g_playlistCount,g_current>=0?g_current+1:0);
     StringCchPrintfW(row5,_countof(row5),L"Shuffle: %s     Repeat: %s",g_shuffle?L"On":L"Off",g_repeat==2?L"All":(g_repeat==1?L"Current":L"Off"));
    }else if(g_advancedSettingsPage==6){
     int threadIndex=0;for(int j=0;j<4;j++)if(g_advancedThreadChoices[j]==g_advancedDecoderThreads)threadIndex=j;
     AdvancedSettingsText(L"Decoder thread count",322,232,1018,258,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
     ABM_D2D_Round(&g_advancedSettingsUi,320,260,1018,326,9,ABM_UI_COLOR_SURFACE_RAISED);
     ABM_D2D_StrokeRound(&g_advancedSettingsUi,320.5f,260.5f,1017.5f,325.5f,9,SETTINGS_BORDER,0.8f);
     AdvancedSettingsText(L"CPU threads used by the video decoder",340,270,730,314,SETTINGS_FG,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
     AdvancedSettingsButtonDraw(820,272,996,314,g_advancedThreadLabels[threadIndex],TRUE);
     ABM_D2D_Round(&g_advancedSettingsUi,320,350,1018,416,9,ABM_UI_COLOR_SURFACE_RAISED);
     ABM_D2D_StrokeRound(&g_advancedSettingsUi,320.5f,350.5f,1017.5f,415.5f,9,SETTINGS_BORDER,0.8f);
     AdvancedSettingsText(L"Drop late frames",340,360,730,406,SETTINGS_FG,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
     AdvancedSettingsButtonDraw(820,362,996,404,g_advancedDropLateFrames?L"Enabled":L"Disabled",g_advancedDropLateFrames);
     ABM_D2D_Round(&g_advancedSettingsUi,320,440,1018,506,9,ABM_UI_COLOR_SURFACE_RAISED);
     ABM_D2D_StrokeRound(&g_advancedSettingsUi,320.5f,440.5f,1017.5f,505.5f,9,SETTINGS_BORDER,0.8f);
     AdvancedSettingsText(L"Skip frames when decoding falls behind",340,450,730,496,SETTINGS_FG,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
     AdvancedSettingsButtonDraw(820,452,996,494,g_advancedSkipFrames?L"Enabled":L"Disabled",g_advancedSkipFrames);
     AdvancedSettingsText(L"Decoder threads: Automatic lets LibVLC choose; fixed counts cap decoder worker threads. Drop late frames discards frames that miss their display deadline to keep playback responsive. Skip frames reduces decoding work when the decoder falls behind; visual quality may be reduced. Changes apply to newly opened media.",322,536,1018,632,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_WORDBREAK);
    }else if(g_advancedSettingsPage==3){
     RECT vr={0};if(g_video&&IsWindow(g_video))GetClientRect(g_video,&vr);
     sectionTitle=L"Video output status";
     StringCchPrintfW(row1,_countof(row1),L"Output surface: %s",g_video&&IsWindow(g_video)?L"Video window available":L"Not currently available");
     StringCchPrintfW(row2,_countof(row2),L"Decoded video size: %u × %u",g_videoWidth,g_videoHeight);
     StringCchPrintfW(row3,_countof(row3),L"Video client area: %ld × %ld",vr.right-vr.left,vr.bottom-vr.top);
     StringCchCopyW(row4,_countof(row4),L"Scaling and aspect ratio are managed by the current LibVLC video output.");
     StringCchCopyW(row5,_countof(row5),L"Display-specific controls will appear here when the renderer exposes a safe runtime option.");
    }else if(g_advancedSettingsPage==5){
     sectionTitle=L"Network and streaming configuration";
     StringCchPrintfW(row1,_countof(row1),L"Torrent cache quota: %llu GiB",(unsigned long long)g_torrentCacheQuotaGiB);
     StringCchPrintfW(row2,_countof(row2),L"Torrent download limit: %s",g_torrentDownloadLimitKiB?L"Configured (KiB/s)":L"Unlimited");
     StringCchPrintfW(row3,_countof(row3),L"Torrent upload limit: %s",g_torrentUploadLimitKiB?L"Configured (KiB/s)":L"Unlimited");
     StringCchPrintfW(row4,_countof(row4),L"Download limit value: %d KiB/s     Upload limit value: %d KiB/s",g_torrentDownloadLimitKiB,g_torrentUploadLimitKiB);
     StringCchCopyW(row5,_countof(row5),L"Change bandwidth limits and cache location in Settings → Torrent & Storage.");
    }else if(g_advancedSettingsPage==6){
     PROCESS_MEMORY_COUNTERS pmc={0};pmc.cb=sizeof(pmc);GetProcessMemoryInfo(GetCurrentProcess(),&pmc,sizeof(pmc));
     sectionTitle=L"System and process performance";
     StringCchPrintfW(row1,_countof(row1),L"Logical processors: %lu",si.dwNumberOfProcessors);
     StringCchPrintfW(row2,_countof(row2),L"Physical memory: %llu GB total",(unsigned long long)(mem.ullTotalPhys/(1024ULL*1024ULL*1024ULL)));
     StringCchPrintfW(row3,_countof(row3),L"Available physical memory: %llu GB",(unsigned long long)(mem.ullAvailPhys/(1024ULL*1024ULL*1024ULL)));
     StringCchPrintfW(row4,_countof(row4),L"ABM AV Player working set: %llu MB",(unsigned long long)(pmc.WorkingSetSize/(1024ULL*1024ULL)));
     StringCchPrintfW(row5,_countof(row5),L"System memory load: %lu%%",mem.dwMemoryLoad);
    }else if(g_advancedSettingsPage==7){
     sectionTitle=L"Current controls and playback behaviour";
     StringCchPrintfW(row1,_countof(row1),L"Default volume: %d%%     Playback speed: %.2fx",g_volume,(double)g_playbackRate);
     StringCchPrintfW(row2,_countof(row2),L"Shuffle: %s     Repeat mode: %s",g_shuffle?L"Enabled":L"Disabled",g_repeat==2?L"All":(g_repeat==1?L"Current":L"Off"));
     StringCchPrintfW(row3,_countof(row3),L"Playlist entries: %d     Recent media entries: %d",g_playlistCount,g_recentCount);
     StringCchCopyW(row4,_countof(row4),L"Playback, seek, volume and playlist shortcuts are handled by the main player.");
     StringCchCopyW(row5,_countof(row5),L"Change default volume, speed, shuffle and repeat in Settings → General.");
    }else if(g_advancedSettingsPage==8){
     PROCESS_MEMORY_COUNTERS pmc={0};pmc.cb=sizeof(pmc);GetProcessMemoryInfo(GetCurrentProcess(),&pmc,sizeof(pmc));
     sectionTitle=L"Live diagnostic summary";
     StringCchPrintfW(row1,_countof(row1),L"ABM AV Player %s     PID: %lu",APP_VERSION,GetCurrentProcessId());
      StringCchPrintfW(row2,_countof(row2),L"SYSTEM RESOURCES  |  CPU: %lu logical processors",si.dwNumberOfProcessors);
      StringCchPrintfW(row3,_countof(row3),L"Installed RAM: %llu MB     Available RAM: %llu MB     Memory load: %lu%%",(unsigned long long)(mem.ullTotalPhys/(1024ULL*1024ULL)),(unsigned long long)(mem.ullAvailPhys/(1024ULL*1024ULL)),mem.dwMemoryLoad);
      StringCchPrintfW(row4,_countof(row4),L"ABM AV PLAYER PROCESS  |  Working set: %llu MB     Page file: %llu MB",(unsigned long long)(pmc.WorkingSetSize/(1024ULL*1024ULL)),(unsigned long long)(pmc.PagefileUsage/(1024ULL*1024ULL)));
      StringCchPrintfW(row5,_countof(row5),L"Playback: %s     Video: %u x %u     Playlist: %d     Recent: %d",g_hasMedia?(g_overlayPlaying?L"Playing":L"Loaded / paused"):L"No media",g_videoWidth,g_videoHeight,g_playlistCount,g_recentCount);
     }
    /* Page 6 paints its interactive performance controls above. Do not draw
       the generic runtime-status card over those controls. */
    if(g_advancedSettingsPage==0){
      ABM_D2D_Round(&g_advancedSettingsUi,304,244,1032,650,12,ABM_UI_COLOR_SURFACE);
      ABM_D2D_StrokeRound(&g_advancedSettingsUi,304.5f,244.5f,1031.5f,649.5f,12,SETTINGS_BORDER,0.8f);
      SYSTEM_INFO overviewSi={0};GetNativeSystemInfo(&overviewSi);
      MEMORYSTATUSEX overviewMem={0};overviewMem.dwLength=sizeof(overviewMem);GlobalMemoryStatusEx(&overviewMem);
      PROCESS_MEMORY_COUNTERS overviewPmc={0};overviewPmc.cb=sizeof(overviewPmc);GetProcessMemoryInfo(GetCurrentProcess(),&overviewPmc,sizeof(overviewPmc));
      wchar_t overviewA[256],overviewB[256],overviewC[256],overviewD[256],overviewE[256],overviewF[256],overviewG[256],overviewH[256];
      StringCchPrintfW(overviewA,_countof(overviewA),L"%s  ·  %u × %u",g_hasMedia?(g_overlayPlaying?L"Playing":L"Loaded / paused"):L"No media loaded",g_videoWidth,g_videoHeight);
      StringCchPrintfW(overviewB,_countof(overviewB),L"Volume %d%%  ·  Speed %.2fx  ·  %s",g_volume,(double)g_playbackRate,g_shuffle?L"Shuffle on":L"Shuffle off");
      StringCchPrintfW(overviewC,_countof(overviewC),L"Playlist %d items  ·  Recent %d  ·  Current #%d",g_playlistCount,g_recentCount,g_current>=0?g_current+1:0);
      StringCchPrintfW(overviewD,_countof(overviewD),L"Output engine: %s",g_advancedAudioOutMode==1?L"WASAPI":g_advancedAudioOutMode==2?L"DirectSound":L"Automatic");
      StringCchPrintfW(overviewE,_countof(overviewE),L"Cache targets: Network %d ms  ·  File %d ms  ·  Live %d ms",g_advancedNetworkCaching,g_advancedFileCaching,g_advancedLiveCaching);
      StringCchPrintfW(overviewF,_countof(overviewF),L"Decoder threads: %s  ·  Drop late: %s  ·  Skip: %s",g_advancedDecoderThreads==0?L"Auto":g_advancedDecoderThreads==2?L"2":g_advancedDecoderThreads==4?L"4":L"8",g_advancedDropLateFrames?L"On":L"Off",g_advancedSkipFrames?L"On":L"Off");
      StringCchPrintfW(overviewG,_countof(overviewG),L"Subtitles: %d px  ·  %s  ·  Delay %+.0f ms",g_advancedSubtitleFontSize,g_advancedSubtitleBold?L"Bold":L"Regular",(double)g_advancedSubtitleDelayUs/1000.0);
      StringCchPrintfW(overviewH,_countof(overviewH),L"System: %lu CPU threads  ·  %llu GB RAM  ·  ABM AV Player %llu MB",overviewSi.dwNumberOfProcessors,(unsigned long long)(overviewMem.ullTotalPhys/(1024ULL*1024ULL*1024ULL)),(unsigned long long)(overviewPmc.WorkingSetSize/(1024ULL*1024ULL)));
      const wchar_t *overviewTitles[8]={L"PLAYBACK SESSION",L"PLAYBACK CONTROLS",L"PLAYLIST & LIBRARY",L"AUDIO ENGINE",L"STREAMING & NETWORK",L"VIDEO PERFORMANCE",L"SUBTITLE CONFIGURATION",L"SYSTEM RESOURCES"};
      const wchar_t *overviewValues[8]={overviewA,overviewB,overviewC,overviewD,overviewE,overviewF,overviewG,overviewH};
      for(int oi=0;oi<8;oi++){
       int col=oi%2,row=oi/2,left=320+col*354,top=260+row*94;
       ABM_D2D_Round(&g_advancedSettingsUi,(float)left,(float)top,(float)(left+340),(float)(top+82),8,ABM_UI_COLOR_SURFACE_RAISED);
       ABM_D2D_StrokeRound(&g_advancedSettingsUi,(float)left+0.5f,(float)top+0.5f,(float)(left+339.5f),(float)(top+81.5f),8,SETTINGS_BORDER,0.7f);
       AdvancedSettingsText(overviewTitles[oi],left+14,top+8,left+326,top+30,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
       AdvancedSettingsText(overviewValues[oi],left+14,top+34,left+326,top+70,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_WORDBREAK|DT_END_ELLIPSIS);
      }
    }else if(g_advancedSettingsPage==9){
      ABM_D2D_Round(&g_advancedSettingsUi,304,244,1032,650,12,ABM_UI_COLOR_SURFACE);
      ABM_D2D_StrokeRound(&g_advancedSettingsUi,304.5f,244.5f,1031.5f,649.5f,12,SETTINGS_BORDER,0.8f);
      AdvancedSettingsText(L"Loaded LibVLC runtime",326,258,1008,284,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
      AdvancedSettingsText(g_vlcRuntimeLoaded?L"Loaded in this process":L"Not loaded",326,290,520,320,g_vlcRuntimeLoaded?ABM_UI_COLOR_SUCCESS:ABM_UI_COLOR_WARNING,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
      AdvancedSettingsText(L"LibVLC version",548,290,680,320,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
      AdvancedSettingsText(g_vlcRuntimeVersion,680,290,1008,320,SETTINGS_FG,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
      AdvancedSettingsText(L"Loaded library path",326,326,1008,348,SETTINGS_MUTED,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
      AdvancedSettingsText(g_vlcRuntimePath,326,350,1008,374,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
      AdvancedSettingsText(L"Plugin directory",326,380,1008,402,SETTINGS_MUTED,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
      AdvancedSettingsText(g_vlcPluginPath,326,404,1008,426,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
      AdvancedSettingsText(g_vlcPluginDirectoryFound?L"Plugin directory found":L"Plugin directory not found",326,430,620,454,g_vlcPluginDirectoryFound?ABM_UI_COLOR_SUCCESS:ABM_UI_COLOR_WARNING,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
      AdvancedSettingsButtonDraw(326,466,520,502,L"Refresh scan",TRUE);
      /* Four visible rows keep the inventory within the card. Scroll with the
         wheel or click the compact track to inspect all eight decoder entries. */
       AdvancedSettingsText(L"Four checks are independent. Plugins load on demand; play media, then Refresh scan. Ctrl+C copies the report.",326,506,1008,530,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
       ABM_D2D_Round(&g_advancedSettingsUi,320,530,1018,648,8,ABM_UI_COLOR_SURFACE_RAISED);
       ABM_D2D_StrokeRound(&g_advancedSettingsUi,320.5f,530.5f,1017.5f,647.5f,8,SETTINGS_BORDER,0.8f);
    }else if(g_advancedSettingsPage==8){
      ABM_D2D_Round(&g_advancedSettingsUi,304,244,1032,650,12,ABM_UI_COLOR_SURFACE);ABM_D2D_StrokeRound(&g_advancedSettingsUi,304.5f,244.5f,1031.5f,649.5f,12,SETTINGS_BORDER,0.8f);
      AdvancedSettingsText(sectionTitle,326,258,1008,290,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
      AdvancedSettingsText(row1,326,294,1008,320,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
      AdvancedSettingsText(row2,326,326,1008,352,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
      AdvancedSettingsText(row3,326,358,1008,384,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
      AdvancedSettingsText(row4,326,390,1008,416,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
      AdvancedSettingsText(L"Diagnostic report preview  |  Read-only",326,486,1008,510,ABM_UI_COLOR_TEXT_SECONDARY,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
      AdvancedSettingsButtonDraw(326,442,610,478,L"Generate report",TRUE);
      AdvancedSettingsButtonDraw(626,442,850,478,L"Open data folder",FALSE);
    }else if(g_advancedSettingsPage!=6&&g_advancedSettingsPage!=7){
     ABM_D2D_Round(&g_advancedSettingsUi,304,244,1032,548,12,ABM_UI_COLOR_SURFACE);
     ABM_D2D_StrokeRound(&g_advancedSettingsUi,304.5f,244.5f,1031.5f,547.5f,12,SETTINGS_BORDER,0.8f);
     AdvancedSettingsText(sectionTitle,326,260,1008,298,ABM_UI_COLOR_ACCENT_BRIGHT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
     AdvancedSettingsText(row1,326,312,1008,344,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
     AdvancedSettingsText(row2,326,354,1008,386,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
     AdvancedSettingsText(row3,326,396,1008,428,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
     AdvancedSettingsText(row4,326,438,1008,470,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
     AdvancedSettingsText(row5,326,480,1008,522,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
   }
   /* Keep the footer inside the content panel. The old divider at Y=664
      crossed the subtitle action row, and the old Close button extended below
      the panel's bottom edge. */
   AdvancedSettingsText(L"Auto-saved. Output-engine changes require a restart.",
       292,670,890,710,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
   AdvancedSettingsButtonDraw(914,664,1040,708,L"Close",FALSE);
   ABM_D2D_End(&g_advancedSettingsUi);
  }
  EndPaint(w,&ps);return 0;
 }
 case WM_CLOSE:DestroyWindow(w);return 0;
  case WM_CTLCOLOREDIT:{HDC dc=(HDC)wp;if((HWND)lp==g_advancedVlcReportEdit){SetTextColor(dc,ABM_UI_COLOR_TEXT_SECONDARY);SetBkColor(dc,ABM_UI_COLOR_SURFACE_RAISED);SetBkMode(dc,OPAQUE);if(!g_advancedVlcReportBrush)g_advancedVlcReportBrush=CreateSolidBrush(ABM_UI_COLOR_SURFACE_RAISED);return (LRESULT)g_advancedVlcReportBrush;}SetTextColor(dc,ABM_UI_COLOR_TEXT);SetBkColor(dc,ABM_UI_COLOR_INPUT);SetBkMode(dc,OPAQUE);if(!g_advancedDiagnosticBrush)g_advancedDiagnosticBrush=CreateSolidBrush(ABM_UI_COLOR_INPUT);return (LRESULT)g_advancedDiagnosticBrush;}
 case WM_DESTROY:
  g_advancedSettingsWindow=NULL;
   if(g_advancedVlcReportEdit){DestroyWindow(g_advancedVlcReportEdit);g_advancedVlcReportEdit=NULL;}if(g_advancedVlcReportBrush){DeleteObject(g_advancedVlcReportBrush);g_advancedVlcReportBrush=NULL;}
  if(g_advancedDiagnosticEdit){DestroyWindow(g_advancedDiagnosticEdit);g_advancedDiagnosticEdit=NULL;}
   if(g_advancedDiagnosticFont){DeleteObject(g_advancedDiagnosticFont);g_advancedDiagnosticFont=NULL;}
   if(g_advancedDiagnosticBrush){DeleteObject(g_advancedDiagnosticBrush);g_advancedDiagnosticBrush=NULL;}
  if(g_advancedSettingsUiReady){ABM_D2D_Destroy(&g_advancedSettingsUi);g_advancedSettingsUiReady=FALSE;}
  return 0;
 }
 return DefWindowProcW(w,msg,wp,lp);
}
static void ShowAdvancedSettingsDialog(HWND owner){
 if(g_advancedSettingsWindow&&IsWindow(g_advancedSettingsWindow)){
  g_advancedSettingsPage=g_advancedSettingsStartPage;
  if(g_advancedSettingsPage<0||g_advancedSettingsPage>=11)g_advancedSettingsPage=0;
  g_advancedSettingsStartPage=0;
  if(g_advancedDiagnosticEdit)ShowWindow(g_advancedDiagnosticEdit,g_advancedSettingsPage==8?SW_SHOW:SW_HIDE);
  if(g_advancedVlcReportEdit)ShowWindow(g_advancedVlcReportEdit,g_advancedSettingsPage==9?SW_SHOW:SW_HIDE);
  SetWindowTextW(g_advancedSettingsWindow,g_advancedSettingsPage==4?L"ABM AV Player — Subtitle Manager":g_advancedSettingsPage==10?L"ABM AV Player — Advanced Audio Centre":L"ABM AV Player — Advanced Settings");
  InvalidateRect(g_advancedSettingsWindow,NULL,FALSE);ShowWindow(g_advancedSettingsWindow,SW_SHOWNORMAL);SetForegroundWindow(g_advancedSettingsWindow);return;
 }
 static BOOL registered=FALSE;HINSTANCE inst=GetModuleHandleW(NULL);
 if(!registered){
  WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.lpfnWndProc=AdvancedSettingsWndProc;
  wc.hInstance=inst;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);
  wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);wc.lpszClassName=L"ABMAVPlayerAdvancedSettings";
  if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
  registered=TRUE;
 }
 RECT wr={0,0,1080,780};
 AdjustWindowRectEx(&wr,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
 int ww=wr.right-wr.left,hh=wr.bottom-wr.top;RECT o;GetWindowRect(owner,&o);
 int x=o.left+(o.right-o.left-ww)/2,y=o.top+(o.bottom-o.top-hh)/2;
 g_advancedSettingsWindow=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,
  L"ABMAVPlayerAdvancedSettings",L"ABM AV Player — Advanced Settings",
  WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_POPUP,x,y,ww,hh,owner,NULL,inst,NULL);
 if(!g_advancedSettingsWindow)return;
 BOOL dark=TRUE;DwmSetWindowAttribute(g_advancedSettingsWindow,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
 {int corners=2;COLORREF border=ABM_THEME_COLOR_2E4668,caption=ABM_THEME_COLOR_0A0F19;
  DwmSetWindowAttribute(g_advancedSettingsWindow,(DWORD)DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
  DwmSetWindowAttribute(g_advancedSettingsWindow,(DWORD)DWMWA_BORDER_COLOR,&border,sizeof(border));
  DwmSetWindowAttribute(g_advancedSettingsWindow,(DWORD)DWMWA_CAPTION_COLOR,&caption,sizeof(caption));}
 EnableWindow(owner,FALSE);ShowWindow(g_advancedSettingsWindow,SW_SHOW);UpdateWindow(g_advancedSettingsWindow);
 MSG m;while(IsWindow(g_advancedSettingsWindow)&&GetMessageW(&m,NULL,0,0)>0){
  if(!IsDialogMessageW(g_advancedSettingsWindow,&m)){TranslateMessage(&m);DispatchMessageW(&m);}
 }
 EnableWindow(owner,TRUE);SetActiveWindow(owner);
}
static void ShowAdvancedSettingsAtPage(HWND owner,int page){
 if(page<0||page>=11)page=0;
 g_advancedSettingsStartPage=page;
 ShowAdvancedSettingsDialog(owner);
}

static BOOL SettingsPoint(int x,int y,int l,int t,int r,int b){return x>=l&&x<=r&&y>=t&&y<=b;}
static int g_settingsScrollX=0,g_settingsScrollY=0;
#define SETTINGS_CONTENT_WIDTH 804
#define SETTINGS_CONTENT_HEIGHT 1120
static int SettingsClampInt(int v,int lo,int hi){return v<lo?lo:(v>hi?hi:v);}
static void SettingsMoveChildControls(HWND w){
 /* Move every native EDIT as one layout transaction. Moving them one at a
    time lets Windows paint intermediate positions over the translated
    Direct2D surface, which looks like controls and labels are scrambling
    while the user scrolls. */
 static const struct {int id,x,y,cx,cy;} items[]={{6207,166,157,92,29},{6201,39,397,600,30},{6202,39,458,600,30},{6204,174,498,100,29},{6205,206,602,130,29},{6206,558,602,130,29},{6208,39,730,600,30}};
 HDWP batch=BeginDeferWindowPos((int)_countof(items));
 for(int i=0;i<(int)_countof(items);i++){
  HWND child=GetDlgItem(w,items[i].id);
  if(!child)continue;
  int x=items[i].x-g_settingsScrollX,y=items[i].y-g_settingsScrollY;
  if(batch)batch=DeferWindowPos(batch,child,NULL,x,y,items[i].cx,items[i].cy,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
  else SetWindowPos(child,NULL,x,y,items[i].cx,items[i].cy,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW);
 }
 if(batch)EndDeferWindowPos(batch);
 for(int i=0;i<(int)_countof(items);i++){
  HWND child=GetDlgItem(w,items[i].id);
  if(child)InvalidateRect(child,NULL,TRUE);
 }
}
#define SETTINGS_SCROLLBAR_SIZE 12
#define SETTINGS_SCROLLBAR_MIN_THUMB 28
static void SettingsSetScroll(HWND w,int x,int y);
static BOOL g_settingsScrollDragging=FALSE,g_settingsScrollDragVertical=TRUE;
static int g_settingsScrollDragOffset=0;
static void SettingsScrollMetrics(HWND w,BOOL vertical,int *trackStart,int *trackLength,int *thumbStart,int *thumbLength,int *maxScroll){
 RECT rc;GetClientRect(w,&rc);
 int viewport=vertical?rc.bottom-SETTINGS_SCROLLBAR_SIZE:rc.right-SETTINGS_SCROLLBAR_SIZE;
 int content=vertical?SETTINGS_CONTENT_HEIGHT:SETTINGS_CONTENT_WIDTH;
 if(viewport<1)viewport=1;
 int track=viewport-8;if(track<1)track=1;
 int maximum=content-viewport;if(maximum<0)maximum=0;
 int thumb=maximum?((track*viewport)/content):track;
 if(thumb<SETTINGS_SCROLLBAR_MIN_THUMB&&track>SETTINGS_SCROLLBAR_MIN_THUMB)thumb=SETTINGS_SCROLLBAR_MIN_THUMB;
 if(thumb>track)thumb=track;
 int pos=vertical?g_settingsScrollY:g_settingsScrollX;
 int start=4;
 int thumbPos=start+(maximum?((track-thumb)*pos/maximum):0);
 if(trackStart)*trackStart=start;
 if(trackLength)*trackLength=track;
 if(thumbStart)*thumbStart=thumbPos;
 if(thumbLength)*thumbLength=thumb;
 if(maxScroll)*maxScroll=maximum;
}
static void SettingsDrawScrollBars(HWND w){
 RECT rc;GetClientRect(w,&rc);
 int ts,tl,thumb,th,maxScroll;
 SettingsScrollMetrics(w,TRUE,&ts,&tl,&thumb,&th,&maxScroll);
 if(maxScroll>0){
  ABM_D2D_Round(&g_settingsUi,(float)(rc.right-9),4.0f,(float)(rc.right-5),(float)(rc.bottom-SETTINGS_SCROLLBAR_SIZE-4),2.0f,ABM_THEME_COLOR_193041);
  ABM_D2D_Round(&g_settingsUi,(float)(rc.right-10),(float)thumb,(float)(rc.right-4),(float)(thumb+th),3.0f,
    g_settingsScrollDragging&&g_settingsScrollDragVertical?ABM_THEME_COLOR_52CDBE:ABM_THEME_COLOR_45697C);
 }
 SettingsScrollMetrics(w,FALSE,&ts,&tl,&thumb,&th,&maxScroll);
 if(maxScroll>0){
  ABM_D2D_Round(&g_settingsUi,4.0f,(float)(rc.bottom-9),(float)(rc.right-SETTINGS_SCROLLBAR_SIZE-4),(float)(rc.bottom-5),2.0f,ABM_THEME_COLOR_193041);
  ABM_D2D_Round(&g_settingsUi,(float)thumb,(float)(rc.bottom-10),(float)(thumb+th),(float)(rc.bottom-4),3.0f,
    g_settingsScrollDragging&&!g_settingsScrollDragVertical?ABM_THEME_COLOR_52CDBE:ABM_THEME_COLOR_45697C);
 }
}
static void SettingsUpdateScrollBars(HWND w){
 RECT rc;GetClientRect(w,&rc);
 int maxX=SETTINGS_CONTENT_WIDTH-(rc.right-SETTINGS_SCROLLBAR_SIZE);if(maxX<0)maxX=0;
 int maxY=SETTINGS_CONTENT_HEIGHT-(rc.bottom-SETTINGS_SCROLLBAR_SIZE);if(maxY<0)maxY=0;
 g_settingsScrollX=SettingsClampInt(g_settingsScrollX,0,maxX);
 g_settingsScrollY=SettingsClampInt(g_settingsScrollY,0,maxY);
 SettingsMoveChildControls(w);
}
static BOOL SettingsBeginScrollDrag(HWND w,int x,int y){
 RECT rc;GetClientRect(w,&rc);
 int ts,tl,thumb,th,maxScroll;
 SettingsScrollMetrics(w,TRUE,&ts,&tl,&thumb,&th,&maxScroll);
 if(maxScroll>0&&x>=rc.right-SETTINGS_SCROLLBAR_SIZE&&y>=4&&y<rc.bottom-SETTINGS_SCROLLBAR_SIZE){
  g_settingsScrollDragging=TRUE;g_settingsScrollDragVertical=TRUE;
  if(y>=thumb&&y<thumb+th)g_settingsScrollDragOffset=y-thumb;
  else {g_settingsScrollDragOffset=th/2;int p=y-g_settingsScrollDragOffset;int span=tl-th;SettingsSetScroll(w,g_settingsScrollX,span>0?(p-ts)*maxScroll/span:0);}
  SetCapture(w);return TRUE;
 }
 SettingsScrollMetrics(w,FALSE,&ts,&tl,&thumb,&th,&maxScroll);
 if(maxScroll>0&&y>=rc.bottom-SETTINGS_SCROLLBAR_SIZE&&x>=4&&x<rc.right-SETTINGS_SCROLLBAR_SIZE){
  g_settingsScrollDragging=TRUE;g_settingsScrollDragVertical=FALSE;
  if(x>=thumb&&x<thumb+th)g_settingsScrollDragOffset=x-thumb;
  else {g_settingsScrollDragOffset=th/2;int p=x-g_settingsScrollDragOffset;int span=tl-th;SettingsSetScroll(w,span>0?(p-ts)*maxScroll/span:g_settingsScrollX,g_settingsScrollY);}
  SetCapture(w);return TRUE;
 }
 return FALSE;
}
static void SettingsUpdateScrollDrag(HWND w,int x,int y){
 int ts,tl,thumb,th,maxScroll;
 SettingsScrollMetrics(w,g_settingsScrollDragVertical,&ts,&tl,&thumb,&th,&maxScroll);
 int span=tl-th;if(span<1||maxScroll<1)return;
 int p=(g_settingsScrollDragVertical?y:x)-g_settingsScrollDragOffset-ts;
 p=SettingsClampInt(p,0,span);
 int value=(p*maxScroll)/span;
 if(g_settingsScrollDragVertical)SettingsSetScroll(w,g_settingsScrollX,value);
 else SettingsSetScroll(w,value,g_settingsScrollY);
}
static void SettingsSetScroll(HWND w,int x,int y){
 RECT rc;GetClientRect(w,&rc);int maxX=SETTINGS_CONTENT_WIDTH-(rc.right-SETTINGS_SCROLLBAR_SIZE);if(maxX<0)maxX=0;int maxY=SETTINGS_CONTENT_HEIGHT-(rc.bottom-SETTINGS_SCROLLBAR_SIZE);if(maxY<0)maxY=0;
 x=SettingsClampInt(x,0,maxX);y=SettingsClampInt(y,0,maxY);if(x==g_settingsScrollX&&y==g_settingsScrollY)return;
 g_settingsScrollX=x;g_settingsScrollY=y;
 SettingsMoveChildControls(w);
 /* Repaint the complete content viewport after the atomic child-layout move.
    This keeps card surfaces, labels, edit frames and native EDIT controls on
    the same scroll origin instead of leaving stale pixels in exposed areas. */
 RedrawWindow(w,NULL,NULL,RDW_INVALIDATE|RDW_NOERASE|RDW_ALLCHILDREN);
}
static void SettingsClampToMonitor(HWND w,const RECT *suggested){
 RECT target=*suggested;HMONITOR mon=MonitorFromRect(&target,MONITOR_DEFAULTTONEAREST);MONITORINFO mi={sizeof(mi)};
 if(GetMonitorInfoW(mon,&mi)){int maxW=mi.rcWork.right-mi.rcWork.left,maxH=mi.rcWork.bottom-mi.rcWork.top,width=target.right-target.left,height=target.bottom-target.top;if(width>maxW)width=maxW;if(height>maxH)height=maxH;target.right=target.left+width;target.bottom=target.top+height;if(target.left<mi.rcWork.left){target.left=mi.rcWork.left;target.right=target.left+width;}if(target.top<mi.rcWork.top){target.top=mi.rcWork.top;target.bottom=target.top+height;}if(target.right>mi.rcWork.right){target.right=mi.rcWork.right;target.left=target.right-width;}if(target.bottom>mi.rcWork.bottom){target.bottom=mi.rcWork.bottom;target.top=target.bottom-height;}}
 SetWindowPos(w,NULL,target.left,target.top,target.right-target.left,target.bottom-target.top,SWP_NOZORDER|SWP_NOACTIVATE);
}

static LRESULT CALLBACK SettingsEditFocusSubclass(HWND edit,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR ref){
 (void)id;(void)ref;HWND parent=GetParent(edit);
 if(msg==WM_SETFOCUS){
  RECT r,client;GetWindowRect(edit,&r);MapWindowPoints(HWND_DESKTOP,parent,(POINT*)&r,2);GetClientRect(parent,&client);
  int sx=g_settingsScrollX,sy=g_settingsScrollY;
  if(r.left<0)sx+=r.left;else if(r.right>client.right)sx+=r.right-client.right;
  if(r.top<0)sy+=r.top;else if(r.bottom>client.bottom)sy+=r.bottom-client.bottom;
  SettingsSetScroll(parent,sx,sy);InvalidateRect(parent,NULL,FALSE);
 }
 if(msg==WM_KILLFOCUS)InvalidateRect(parent,NULL,FALSE);
 if(msg==WM_MOUSEWHEEL){SendMessageW(parent,WM_MOUSEWHEEL,wp,lp);return 0;}
 if(msg==WM_NCDESTROY)RemoveWindowSubclass(edit,SettingsEditFocusSubclass,1);
 return DefSubclassProc(edit,msg,wp,lp);
}
static LRESULT CALLBACK SettingsWndProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
 switch(msg) {
 case WM_CREATE: {
  if(!ABM_D2D_Init(&g_settingsUi,w))return -1;g_settingsUiReady=TRUE;
  g_settingsEditBrush=CreateSolidBrush(ABM_UI_COLOR_INPUT);
  g_settingsShuffle=g_shuffle;g_settingsRepeat=g_repeat;g_settingsReplayGain=g_replayGain;g_settingsCompressor=g_compressor;
  g_settingsThemePreset=(int)ABMThemeCurrentPreset();
  g_settingsRateIndex=2;for(int i=0;i<7;i++)if(fabsf(g_playbackRate-g_settingsRates[i])<0.01f)g_settingsRateIndex=i;
  wchar_t n[32];
  StringCchPrintfW(n,_countof(n),L"%d",g_volume);
  CreateWindowExW(0,L"EDIT",n,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,166,157,92,29,w,(HMENU)6207,NULL,NULL);
  CreateWindowExW(0,L"EDIT",g_torrentCacheBaseDir,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,39,397,600,30,w,(HMENU)6201,NULL,NULL);
  CreateWindowExW(0,L"EDIT",g_torrentDownloadDir,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,39,458,600,30,w,(HMENU)6202,NULL,NULL);
  StringCchPrintfW(n,_countof(n),L"%llu",(unsigned long long)g_torrentCacheQuotaGiB);
  CreateWindowExW(0,L"EDIT",n,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,174,498,100,29,w,(HMENU)6204,NULL,NULL);
  StringCchPrintfW(n,_countof(n),L"%d",g_torrentDownloadLimitKiB);
  CreateWindowExW(0,L"EDIT",n,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,206,602,130,29,w,(HMENU)6205,NULL,NULL);
  StringCchPrintfW(n,_countof(n),L"%d",g_torrentUploadLimitKiB);
  CreateWindowExW(0,L"EDIT",n,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,558,602,130,29,w,(HMENU)6206,NULL,NULL);
  wchar_t tmdbToken[4096]={0};ABM_TMDB_GetReadToken(tmdbToken,_countof(tmdbToken)); HWND tmdbEdit=CreateWindowExW(0,L"EDIT",tmdbToken,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|ES_PASSWORD,39,730,600,30,w,(HMENU)6208,NULL,NULL); if(tmdbEdit){SendMessageW(tmdbEdit,EM_SETLIMITTEXT,4096,0);SendMessageW(tmdbEdit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELONG(8,8));} SecureZeroMemory(tmdbToken,sizeof(tmdbToken)); for(int id=6201;id<=6208;id++){HWND edit=GetDlgItem(w,id);if(edit){SendMessageW(edit,WM_SETFONT,(WPARAM)g_font,TRUE);SetWindowSubclass(edit,SettingsEditFocusSubclass,1,0);}}
  return 0;
 }
 case WM_THEMECHANGED:
  if(g_settingsEditBrush){DeleteObject(g_settingsEditBrush);g_settingsEditBrush=CreateSolidBrush(ABM_UI_COLOR_INPUT);}
  InvalidateRect(w,NULL,FALSE);return 0;
 case WM_SIZE:if(g_settingsUiReady)ABM_D2D_Resize(&g_settingsUi,LOWORD(lp),HIWORD(lp));SettingsUpdateScrollBars(w);InvalidateRect(w,NULL,FALSE);return 0;
 case WM_ERASEBKGND:return 1;
 case WM_CTLCOLOREDIT:{
  HDC dc=(HDC)wp;
  SetTextColor(dc,ABM_UI_COLOR_TEXT);
  SetBkColor(dc,ABM_UI_COLOR_INPUT);
  SetBkMode(dc,OPAQUE);
  return (LRESULT)g_settingsEditBrush;
 }
 case WM_CANCELMODE:case WM_CAPTURECHANGED:g_settingsMousePressed=FALSE;g_settingsScrollDragging=FALSE;if(GetCapture()==w)ReleaseCapture();InvalidateRect(w,NULL,FALSE);return 0;
 case WM_MOUSEMOVE:{if(g_settingsScrollDragging){SettingsUpdateScrollDrag(w,GET_X_LPARAM(lp),GET_Y_LPARAM(lp));return 0;}int x=GET_X_LPARAM(lp)+g_settingsScrollX,y=GET_Y_LPARAM(lp)+g_settingsScrollY;if(x!=g_settingsHoverX||y!=g_settingsHoverY){g_settingsHoverX=x;g_settingsHoverY=y;TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,w,0};TrackMouseEvent(&tme);InvalidateRect(w,NULL,FALSE);}return 0;}
 case WM_MOUSELEAVE:g_settingsHoverX=g_settingsHoverY=-1;g_settingsMousePressed=FALSE;InvalidateRect(w,NULL,FALSE);return 0;
 case WM_LBUTTONDOWN:{if(SettingsBeginScrollDrag(w,GET_X_LPARAM(lp),GET_Y_LPARAM(lp)))return 0;g_settingsMousePressed=TRUE;SetCapture(w);InvalidateRect(w,NULL,FALSE);
  int x=GET_X_LPARAM(lp)+g_settingsScrollX,y=GET_Y_LPARAM(lp)+g_settingsScrollY;
  for(int preset=0;preset<ABM_THEME_PRESET_COUNT;preset++){
   int col=preset%4,row=preset/4;
   int left=40+col*185,top=918+row*46;
   if(SettingsPoint(x,y,left,top,left+177,top+44)){
    g_settingsThemePreset=preset;
    ABMThemeApplyPreset((ABMThemePreset)preset);
    SaveThemePreference(preset);
    StringCchCopyW(g_settingsMessage,_countof(g_settingsMessage),L"Theme applied and saved.");
    RefreshThemeWindows();
    InvalidateRect(w,NULL,FALSE);
    return 0;
   }
  }
  if(SettingsPoint(x,y,565,155,704,190)){g_settingsRateOpen=!g_settingsRateOpen;InvalidateRect(w,NULL,FALSE);return 0;}
  if(g_settingsRateOpen&&SettingsPoint(x,y,565,190,704,372)){int idx=(y-190)/26;if(idx>=0&&idx<7)g_settingsRateIndex=idx;g_settingsRateOpen=FALSE;InvalidateRect(w,NULL,FALSE);return 0;}
  if(SettingsPoint(x,y,38,198,300,224)){g_settingsShuffle=!g_settingsShuffle;InvalidateRect(w,NULL,FALSE);return 0;}
  if(SettingsPoint(x,y,340,198,610,224)){g_settingsRepeat=(g_settingsRepeat==0)?2:(g_settingsRepeat==2?1:0);InvalidateRect(w,NULL,FALSE);return 0;}
  if(SettingsPoint(x,y,38,280,340,306)){g_settingsReplayGain=!g_settingsReplayGain;InvalidateRect(w,NULL,FALSE);return 0;}
  if(SettingsPoint(x,y,350,280,680,306)){g_settingsCompressor=!g_settingsCompressor;InvalidateRect(w,NULL,FALSE);return 0;}
  if(SettingsPoint(x,y,655,396,780,428)){SettingsBrowse(w);return 0;}
  if(SettingsPoint(x,y,655,457,780,489)){SettingsBrowseDownloads(w);return 0;}
  if(SettingsPoint(x,y,655,496,780,528)){wchar_t p[PATH_CAP]={0};GetWindowTextW(GetDlgItem(w,6201),p,_countof(p));if(p[0])ShellExecuteW(w,L"open",p,NULL,NULL,SW_SHOWNORMAL);return 0;}
  /* Keep hit targets aligned with the current Direct2D-rendered button rectangles. */
  if(SettingsPoint(x,y,650,27,780,62)){ShowAdvancedSettingsDialog(w);return 0;}
  if(SettingsPoint(x,y,39,816,194,849)){ABM_CheckForUpdates(g_main,APP_VERSION);return 0;}
  if(SettingsPoint(x,y,610,1040,735,1078)){SettingsApply(w);return 0;}
  if(SettingsPoint(x,y,744,1040,800,1078)){DestroyWindow(w);return 0;}
  if(g_settingsRateOpen){g_settingsRateOpen=FALSE;InvalidateRect(w,NULL,FALSE);}
  break;
 }
 case WM_LBUTTONUP:g_settingsMousePressed=FALSE;g_settingsScrollDragging=FALSE;if(GetCapture()==w)ReleaseCapture();InvalidateRect(w,NULL,FALSE);break;
 case WM_MOUSEWHEEL:{int delta=GET_WHEEL_DELTA_WPARAM(wp);SettingsSetScroll(w,g_settingsScrollX,g_settingsScrollY-(delta/WHEEL_DELTA)*72);return 0;}
 case WM_DPICHANGED:{SettingsClampToMonitor(w,(const RECT*)lp);SettingsUpdateScrollBars(w);if(g_settingsUiReady){RECT cr;GetClientRect(w,&cr);ABM_D2D_Resize(&g_settingsUi,cr.right,cr.bottom);}InvalidateRect(w,NULL,TRUE);return 0;}
 case WM_KEYDOWN:
  if(wp==VK_ESCAPE){if(g_settingsRateOpen){g_settingsRateOpen=FALSE;InvalidateRect(w,NULL,FALSE);}else DestroyWindow(w);return 0;}
  if((GetKeyState(VK_CONTROL)&0x8000)&&wp=='S'){SettingsApply(w);return 0;}
  if(wp==VK_PRIOR){SettingsSetScroll(w,g_settingsScrollX,g_settingsScrollY-GetSystemMetrics(SM_CYSCREEN)/2);return 0;}
  if(wp==VK_NEXT){SettingsSetScroll(w,g_settingsScrollX,g_settingsScrollY+GetSystemMetrics(SM_CYSCREEN)/2);return 0;}
  if(wp==VK_HOME&&(GetKeyState(VK_CONTROL)&0x8000)){SettingsSetScroll(w,0,0);return 0;}
  if(wp==VK_END&&(GetKeyState(VK_CONTROL)&0x8000)){SettingsSetScroll(w,SETTINGS_CONTENT_WIDTH,SETTINGS_CONTENT_HEIGHT);return 0;}
  break;
 case WM_PAINT:{
  PAINTSTRUCT ps;BeginPaint(w,&ps);
  if(g_settingsUiReady){
   /* Match the Main Player's invalid-region rendering: preserve the rest of
      the window and repaint only what Windows invalidated. */
   ABM_D2D_BeginClip(&g_settingsUi,SETTINGS_BG,&ps.rcPaint);
   ABM_D2D_SetOffset(&g_settingsUi,(float)-g_settingsScrollX,(float)-g_settingsScrollY);
   ABM_D2D_GradientRound(&g_settingsUi,20,14,800,76,15,ABM_THEME_COLOR_0F2739,ABM_THEME_COLOR_0D1E2F);
   ABM_D2D_StrokeRound(&g_settingsUi,20,14,800,76,15,ABM_THEME_COLOR_2B586F,1.0f);
   ABM_D2D_Round(&g_settingsUi,34,27,76,69,12,ABM_THEME_COLOR_1C9D9D);
   ABM_D2D_StrokeRound(&g_settingsUi,34,27,76,69,12,ABM_THEME_COLOR_73D2CF,1.0f);
   SettingsText(L"⚙",39,29,72,66,ABM_THEME_COLOR_FFFFFF,2,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
   SettingsText(L"SETTINGS",92,20,420,53,ABM_THEME_COLOR_F7FAFF,2,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsButtonDraw(650,27,780,62,L"Advanced…",TRUE);
   SettingsText(L"PLAYBACK  ·  AUDIO  ·  STORAGE  ·  NETWORK",94,49,550,69,SETTINGS_ACCENT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsSection(88,L"GENERAL",L"Playback defaults",226);
   SettingsText(L"Volume (0–100)",40,140,155,180,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(160,155,264,188);if(GetFocus()==GetDlgItem(w,6207))ABM_D2D_StrokeRound(&g_settingsUi,159,154,265,189,ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_FOCUS,1.8f);
   SettingsText(L"Playback speed",430,140,550,180,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(565,155,704,190);
   ABM_D2D_Round(&g_settingsUi,572,160,697,185,5,ABM_THEME_COLOR_102737);
   SettingsText(g_settingsRateLabels[g_settingsRateIndex],579,160,670,185,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   ABM_D2D_Line(&g_settingsUi,681,169,686,175,SETTINGS_ACCENT,1.8f);ABM_D2D_Line(&g_settingsUi,686,175,691,169,SETTINGS_ACCENT,1.8f);
   SettingsCheckDraw(40,202,L"Shuffle by default",g_settingsShuffle);
   SettingsCheckDraw(342,202,g_settingsRepeat==0?L"Repeat mode: Off":(g_settingsRepeat==2?L"Repeat mode: All":L"Repeat mode: Current item"),g_settingsRepeat!=0);
   SettingsSection(238,L"AUDIO",L"Processing",324);
   SettingsCheckDraw(40,281,L"ReplayGain normalisation",g_settingsReplayGain);
   SettingsCheckDraw(352,281,L"Dynamic range compressor",g_settingsCompressor);
   SettingsSection(336,L"TORRENT & STORAGE",L"Cache, permanent downloads and quota",536);
   SettingsText(L"Cache parent folder  ·  torrent-cache is created inside",40,372,610,394,SETTINGS_MUTED,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(34,396,644,429);if(GetFocus()==GetDlgItem(w,6201))ABM_D2D_StrokeRound(&g_settingsUi,33,395,645,430,ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_FOCUS,1.8f);SettingsButtonDraw(655,396,780,429,L"Browse…",FALSE);
   SettingsText(L"Permanent downloads folder",40,434,610,456,SETTINGS_MUTED,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(34,457,644,490);if(GetFocus()==GetDlgItem(w,6202))ABM_D2D_StrokeRound(&g_settingsUi,33,456,645,491,ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_FOCUS,1.8f);SettingsButtonDraw(655,457,780,490,L"Browse…",FALSE);
   SettingsText(L"Cache quota (GiB)",40,495,166,526,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(169,496,279,529);if(GetFocus()==GetDlgItem(w,6204))ABM_D2D_StrokeRound(&g_settingsUi,168,495,280,530,ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_FOCUS,1.8f);SettingsButtonDraw(655,496,780,529,L"Open cache",FALSE);
   SettingsSection(548,L"NETWORK",L"Torrent bandwidth  ·  0 = unlimited",654);
   SettingsText(L"Download limit (KiB/s)",40,583,202,608,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(199,599,341,634);if(GetFocus()==GetDlgItem(w,6205))ABM_D2D_StrokeRound(&g_settingsUi,198,598,342,635,ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_FOCUS,1.8f);
   SettingsText(L"Upload limit (KiB/s)",400,583,552,608,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(551,599,693,634);if(GetFocus()==GetDlgItem(w,6206))ABM_D2D_StrokeRound(&g_settingsUi,550,598,694,635,ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_FOCUS,1.8f);
   SettingsSection(666,L"TMDB METADATA",L"Movie and television information",794); SettingsText(L"TMDB API Read Access Token",40,701,420,723,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE); SettingsEditFrame(34,729,644,763); if(GetFocus()==GetDlgItem(w,6208))ABM_D2D_StrokeRound(&g_settingsUi,33,728,645,764,ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_FOCUS,1.8f); SettingsText(ABM_TMDB_IsConfigured()?L"Token encrypted for this Windows user.":L"Enter your TMDB Read Access Token to enable metadata lookup.",40,768,780,793,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS); SettingsSection(806,L"UPDATES",L"Maintenance",878);
   SettingsButtonDraw(39,816,194,849,L"Check for updates",TRUE);
   SettingsText(L"Checks the latest published GitHub release.",210,817,770,848,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsSection(886,L"COLOR THEME",L"Changes apply immediately",1100);
   for(int preset=0;preset<ABM_THEME_PRESET_COUNT;preset++){
    int col=preset%4,row=preset/4;
    float left=(float)(40+col*185),top=(float)(918+row*46);
    BOOL selected=(preset==g_settingsThemePreset);
    COLORREF accent=ABMThemePresetAccent((ABMThemePreset)preset);
    ABM_D2D_Round(&g_settingsUi,left,top,left+177.0f,top+44.0f,8.0f,selected?ABM_UI_COLOR_SURFACE_RAISED:ABM_UI_COLOR_SURFACE);
    ABM_D2D_Round(&g_settingsUi,left+7.0f,top+4.0f,left+170.0f,top+19.0f,5.0f,accent);
    ABM_D2D_Round(&g_settingsUi,left+7.0f,top+22.0f,left+170.0f,top+39.0f,5.0f,selected?ABM_UI_COLOR_SURFACE_BLUE:ABM_UI_COLOR_SURFACE);
    SettingsText(ABMThemePresetName((ABMThemePreset)preset),(int)left+10,(int)top+22,(int)left+167,(int)top+39,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_FG,selected?1:0,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    ABM_D2D_StrokeRound(&g_settingsUi,left+0.5f,top+0.5f,left+176.5f,top+43.5f,8.0f,selected?ABM_UI_COLOR_ACCENT_BRIGHT:SETTINGS_BORDER,selected?1.8f:0.8f);
   }
   SettingsText(g_settingsMessage,40,1016,780,1034,SETTINGS_MUTED,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
   SettingsButtonDraw(610,1040,735,1078,L"Save settings",TRUE);
   SettingsButtonDraw(744,1040,800,1078,L"Close",FALSE);
   if(g_settingsRateOpen){
    ABM_D2D_Round(&g_settingsUi,565,190,704,374,8,ABM_THEME_COLOR_0C1B2B);
    ABM_D2D_StrokeRound(&g_settingsUi,565,190,704,374,8,ABM_THEME_COLOR_438999,1.0f);
    for(int i=0;i<7;i++){int y=191+i*26;if(i==g_settingsRateIndex)ABM_D2D_Round(&g_settingsUi,570,(float)y,699,(float)(y+25),5,ABM_THEME_COLOR_195B58);SettingsText(g_settingsRateLabels[i],580,y,694,y+25,i==g_settingsRateIndex?ABM_THEME_COLOR_FFFFFF:SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);}
   }
   ABM_D2D_SetOffset(&g_settingsUi,0.0f,0.0f);
   SettingsDrawScrollBars(w);
   ABM_D2D_End(&g_settingsUi);
  }
  EndPaint(w,&ps);return 0;
 }
 case WM_CLOSE:DestroyWindow(w);return 0;
 case WM_DESTROY:
  g_settingsWindow=NULL;
  if(g_settingsUiReady){ABM_D2D_Destroy(&g_settingsUi);g_settingsUiReady=FALSE;}
  if(g_settingsEditBrush){DeleteObject(g_settingsEditBrush);g_settingsEditBrush=NULL;}
  return 0;
 }
 return DefWindowProcW(w,msg,wp,lp);
}
static void ShowSettingsDialog(HWND owner) {
 if(g_settingsWindow&&IsWindow(g_settingsWindow)){RECT current;GetWindowRect(g_settingsWindow,&current);SettingsClampToMonitor(g_settingsWindow,&current);ShowWindow(g_settingsWindow,SW_SHOWNORMAL);SetForegroundWindow(g_settingsWindow);return;}
 static BOOL registered=FALSE;HINSTANCE inst=GetModuleHandleW(NULL);
 if(!registered){WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.lpfnWndProc=SettingsWndProc;wc.hInstance=inst;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);wc.lpszClassName=L"ABMAVPlayerSettings";if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;registered=TRUE;}
 DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN|WS_POPUP;
 RECT wr={0,0,820,760};AdjustWindowRectEx(&wr,style,FALSE,WS_EX_DLGMODALFRAME);int ww=wr.right-wr.left,hh=wr.bottom-wr.top;RECT o;GetWindowRect(owner,&o);
 HMONITOR mon=MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST);MONITORINFO mi={sizeof(mi)};int workL=0,workT=0,workW=GetSystemMetrics(SM_CXSCREEN),workH=GetSystemMetrics(SM_CYSCREEN);if(GetMonitorInfoW(mon,&mi)){workL=mi.rcWork.left;workT=mi.rcWork.top;workW=mi.rcWork.right-mi.rcWork.left;workH=mi.rcWork.bottom-mi.rcWork.top;}
 if(ww>workW)ww=workW;if(hh>workH)hh=workH;int x=o.left+(o.right-o.left-ww)/2,y=o.top+(o.bottom-o.top-hh)/2;if(x<workL)x=workL;if(y<workT)y=workT;if(x+ww>workL+workW)x=workL+workW-ww;if(y+hh>workT+workH)y=workT+workH-hh;g_settingsScrollX=0;g_settingsScrollY=0;
 g_settingsWindow=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"ABMAVPlayerSettings",L"ABM AV Player — Settings",style,x,y,ww,hh,owner,NULL,inst,NULL);
 if(!g_settingsWindow)return;
 BOOL dark=TRUE;DwmSetWindowAttribute(g_settingsWindow,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
 {int corners=2;COLORREF border=ABM_THEME_COLOR_2E4668,caption=ABM_THEME_COLOR_0A0F19;DwmSetWindowAttribute(g_settingsWindow,(DWORD)DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));DwmSetWindowAttribute(g_settingsWindow,(DWORD)DWMWA_BORDER_COLOR,&border,sizeof(border));DwmSetWindowAttribute(g_settingsWindow,(DWORD)DWMWA_CAPTION_COLOR,&caption,sizeof(caption));}
 EnableWindow(owner,FALSE);ShowWindow(g_settingsWindow,SW_SHOW);UpdateWindow(g_settingsWindow);
 MSG m;while(IsWindow(g_settingsWindow)&&GetMessageW(&m,NULL,0,0)>0){if(!IsDialogMessageW(g_settingsWindow,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
 EnableWindow(owner,TRUE);SetActiveWindow(owner);
}

static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_THEMECHANGED:
        if(g_comboBrush){DeleteObject(g_comboBrush);g_comboBrush=CreateSolidBrush(C_PANEL2);}
        InvalidateRect(hwnd,NULL,FALSE);return 0;
    case WM_APP+0x351:
        if(g_drawerOpen&&!g_fullscreen)InvalidateRect(hwnd,&R_SIDEBAR,FALSE);
        return 0;
    case WM_CREATE: {
        g_main=hwnd; LoadPersistentState(); LoadAudioPreferences(); ApplyAudioOutputDevice(); if(!ABM_D2D_Init(&g_ui,hwnd)){ABM_MessageBoxW(hwnd,L"Direct2D / DirectWrite initialization failed.",APP_NAME,MB_ICONERROR);return -1;} g_video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|SS_BLACKRECT,0,0,640,360,hwnd,NULL,GetModuleHandleW(NULL),NULL);
        DragAcceptFiles(hwnd,TRUE);
        /* Hidden item store; the visible selector and popup are Direct2D. */
        g_renderers=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWNLIST|CBS_HASSTRINGS,0,0,1,1,hwnd,(HMENU)ID_RENDERERS,GetModuleHandleW(NULL),NULL);
        SendMessageW(g_renderers,CB_ADDSTRING,0,(LPARAM)L"Local playback");SendMessageW(g_renderers,CB_SETCURSEL,0,0);
        ShowWindow(g_renderers,SW_HIDE);
        libvlc_media_player_set_hwnd(g_player,g_video);SetWindowSubclass(g_video,VideoKeyboardSubclass,1,(DWORD_PTR)hwnd);g_videoMouseHook=SetWindowsHookExW(WH_MOUSE_LL,VideoMouseHookProc,GetModuleHandleW(NULL),0);libvlc_audio_set_volume(g_player,g_volume);ShowWindow(g_video,SW_HIDE);UpdateLayout(hwnd);VlcAttachPlaybackEvents();SetTimer(hwnd,TIMER_ID,33,NULL);return 0;
    }
    case WM_CLOSE:
        if(g_advancedConfirmExit&&ABM_MessageBoxW(hwnd,L"Close ABM AV Player?",APP_NAME,MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;
        DestroyWindow(hwnd);return 0;
    case WM_DROPFILES: AddDroppedFiles((HDROP)wp);return 0;
    case WM_ARTWORK_READY: if((LONG)wp==InterlockedCompareExchange(&g_artworkGeneration,0,0) && g_artMedia)LoadParsedArtwork(g_artMedia);return 0;
    case WM_ARTWORK_DOWNLOAD_READY: {
        LONG generation=(LONG)wp;
        wchar_t path[PATH_CAP]={0};
        if(EnsureDataDirectory())
            swprintf_s(path,PATH_CAP,L"%s\\artwork-%ld.bin",g_dataDir,(long)generation);
        if(generation==InterlockedCompareExchange(&g_artworkGeneration,0,0) && lp && path[0]) {
            g_artworkLoaded=ABM_D2D_LoadImage(&g_ui,path);
            if(g_artworkLoaded)g_artworkFade=0;
            InvalidateRect(hwnd,NULL,FALSE);
        } else if(path[0]) {
            /* A newer track superseded this download before its completion
               message was handled. Do not accumulate obsolete cover files. */
            DeleteFileW(path);
        }
        return 0;
    }
    case WM_SYSKEYDOWN:
        if(wp==VK_ESCAPE && g_fullscreen) {
            FullscreenDiag("main-wndproc-escape-syskeydown",0);
            ToggleFullscreen(hwnd);
            return 0;
        }
        break;
    case WM_SIZE: {
        /* SetWindowPlacement/SetWindowPos generate intermediate size messages
           during fullscreen transitions. Do not let those messages resize
           LibVLC's video child multiple times while playback is active. */
        if(!g_fullscreenTransition) {
            UpdateLayout(hwnd);
            if(wp!=SIZE_MINIMIZED)ABM_D2D_Resize(&g_ui,(UINT)LOWORD(lp),(UINT)HIWORD(lp));
        }
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
            HBRUSH bg=CreateSolidBrush((d->itemState&ODS_SELECTED)?ABM_THEME_COLOR_363B42:ABM_THEME_COLOR_1F2226);
            FillRect(d->hDC,&d->rcItem,bg);
            DeleteObject(bg);
            if(d->itemID!=(UINT)-1){
                wchar_t t[256]=L"";
                SendMessageW(g_renderers,CB_GETLBTEXT,d->itemID,(LPARAM)t);
                RECT textRect=d->rcItem;
                textRect.left+=13;
                textRect.right-=28;
                SetBkMode(d->hDC,TRANSPARENT);
                SetTextColor(d->hDC,ABM_THEME_COLOR_EBEEF2);
                DrawTextW(d->hDC,t,-1,&textRect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            }
            /* The native combo already paints its drop-down button.
               Do not draw a second chevron in the owner-draw edit item. */
            if(d->itemState&ODS_FOCUS)DrawFocusRect(d->hDC,&d->rcItem);
            return TRUE;
        }
        break;
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wp,ABM_THEME_COLOR_EBEEF2);
        SetBkColor((HDC)wp,ABM_THEME_COLOR_1F2226);
        return (LRESULT)g_comboBrush;
    case WM_COMMAND:if(LOWORD(wp)==ID_RENDERERS&&HIWORD(wp)==CBN_SELCHANGE){SelectRenderer();return 0;}break;
    case WM_TORRENT_PLAYER_EVENT: {
        uint64_t payload=(uint64_t)(ULONG_PTR)lp;
        LONG generation=(LONG)(uint32_t)(payload>>32);
        LONG eventValue=(LONG)(uint32_t)payload;
        if(generation==0 ||
           InterlockedCompareExchange(&g_torrentPlaybackGeneration,0,0)!=generation ||
           !g_torrentPlaybackActive)return 0;
        switch((libvlc_event_type_t)wp) {
            case libvlc_MediaPlayerBuffering: {
                int percent=(int)eventValue;
                InterlockedExchange(&g_torrentBufferPercent,percent);
                TorrentSetBufferProgress(percent);
                if(percent<100) {
                    wchar_t buffering[160];
                    if(g_torrentEverPlayed) {
                        TorrentPlaybackState current=(TorrentPlaybackState)InterlockedCompareExchange(&g_torrentPlaybackState,0,0);
                        if(current!=TORRENT_PLAYBACK_RECOVERING) {
                            if(!g_torrentBufferingSinceTick)g_torrentBufferingSinceTick=GetTickCount64();
                            StringCchPrintfW(buffering,_countof(buffering),
                                L"Buffering during playback — waiting for verified pieces (%d%%)…",percent);
                            TorrentSetPlaybackState(TORRENT_PLAYBACK_BUFFERING,buffering);
                        }
                    } else {
                        StringCchPrintfW(buffering,_countof(buffering),
                            L"Buffering before playback starts (%d%%)…",percent);
                        TorrentSetPlaybackState(TORRENT_PLAYBACK_BUFFERING_START,buffering);
                    }
                }
                break;
            }
            case libvlc_MediaPlayerPlaying:
                g_torrentEverPlayed=TRUE;
                g_torrentLastProgressTick=GetTickCount64();
                g_torrentBufferingSinceTick=0;
                if(g_player)g_torrentLastMediaTime=libvlc_media_player_get_time(g_player);
                TorrentSetPlaybackState(TORRENT_PLAYBACK_PLAYING,NULL);
                break;
            case libvlc_MediaPlayerTimeChanged: {
                int64_t mediaTime=g_player?libvlc_media_player_get_time(g_player):-1;
                if(g_torrentLastMediaTime<0||mediaTime!=g_torrentLastMediaTime) {
                    g_torrentLastMediaTime=mediaTime;
                    g_torrentLastProgressTick=GetTickCount64();
                    TorrentSetBufferProgress(100);
                    if(g_torrentEverPlayed) {
                        g_torrentBufferingSinceTick=0;
                        TorrentSetPlaybackState(TORRENT_PLAYBACK_PLAYING,NULL);
                    }
                }
                break;
            }
            case libvlc_MediaPlayerPaused:
                TorrentSetPlaybackState(TORRENT_PLAYBACK_PAUSED,NULL);
                break;
            case libvlc_MediaPlayerStopped:
            case libvlc_MediaPlayerEndReached:
                g_torrentPlaybackActive=FALSE;
                g_hasMedia=FALSE;
                TorrentDetachPlayerEvents();
                if(g_torrentStopHttpStream&&g_torrentInitialized)g_torrentStopHttpStream();
                TorrentSetPlaybackState(TORRENT_PLAYBACK_STOPPED,NULL);
                break;
            case libvlc_MediaPlayerEncounteredError:
                TorrentHandlePlayerError();
                break;
        }
        return 0;
    }
    case WM_TORRENT_TV_READY: {
        TorrentTvStreamRequest *req=(TorrentTvStreamRequest*)lp;
        if(g_torrentTvStreamThread){CloseHandle(g_torrentTvStreamThread);g_torrentTvStreamThread=NULL;}
        if(g_torrentTvStreamRequest==req)g_torrentTvStreamRequest=NULL;
        BOOL cancelled=InterlockedCompareExchange(&g_torrentTvStreamCancel,0,0)!=0;
        if(req&&req->ok&&!cancelled) {
            if(ABM_TorrentDlna_PlayUrl(req->url)) {
                g_torrentTvPlaybackActive=TRUE;
                TorrentSetLabel(g_torrentStatusLabel,L"Casting the original torrent video to your TV. The complete file is also downloading to Windows Downloads\\ABM AV Player\\Torrent Downloads.");
            } else {
                if(g_torrentStopHttpStream)g_torrentStopHttpStream();
                TorrentSetLabel(g_torrentStatusLabel,L"The TV rejected the media URL or DLNA Play command. Check receiver compatibility and Windows Firewall.");
            }
        } else if(req&&!cancelled) {
            if(g_torrentStopHttpStream)g_torrentStopHttpStream();
            TorrentSetLabel(g_torrentStatusLabel,req->error[0]?req->error:L"Could not prepare the TV stream.");
        }
        if(req)free(req);
        return 0;
    }
    case WM_TORRENT_STREAM_READY: {
        LONG generation=(LONG)wp;
        TorrentStreamRequest *req=g_torrentStreamRequest;
        /* A stop/replacement may have invalidated this queued completion. */
        if(!req||req->generation!=generation||
           InterlockedCompareExchange(&g_torrentStreamGeneration,0,0)!=generation)
            return 0;
        if(g_torrentStreamThread){
            WaitForSingleObject(g_torrentStreamThread,INFINITE);
            CloseHandle(g_torrentStreamThread);g_torrentStreamThread=NULL;
        }
        g_torrentStreamRequest=NULL;
        BOOL cancelled=InterlockedCompareExchange(&g_torrentStreamCancel,0,0)!=0;
        if(req&&req->ok&&!cancelled) {
            int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,req->url,-1,NULL,0,NULL,NULL);
            char *url=bytes>0?(char*)malloc((size_t)bytes):NULL;
            if(url&&WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,req->url,-1,url,bytes,NULL,NULL)>0) {
                libvlc_media_t *media=libvlc_media_new_location(g_vlc,url);
                if(media) {
                    TorrentDetachPlayerEvents();
                    PlaybackStopEngine();
                    g_hasMedia=FALSE;g_current=-1;
                    ApplyAudioOutputDevice();
                    ApplySubtitleAppearanceOption(media);AddAdvancedCachingOptions(media);AddAdvancedPerformanceOptions(media);
                    PlaybackSetMedia(media);
                    libvlc_video_set_spu_delay(g_player,g_advancedSubtitleDelayUs);
                    BeginArtworkParse(media);
                    BOOL eventsAttached=TorrentAttachPlayerEvents();
                    int result=PlaybackStartEngine();
                    libvlc_media_release(media);
                    if(result==0) {
                        libvlc_audio_set_delay(g_player,g_audioDelayUs);
                        g_torrentPlaybackActive=TRUE;g_hasMedia=TRUE;g_current=-1;
                        g_torrentEverPlayed=FALSE;g_torrentLastMediaTime=-1;
                        g_torrentLastProgressTick=GetTickCount64();g_torrentBufferingSinceTick=0;
                        TorrentSetPlaybackState(TORRENT_PLAYBACK_BUFFERING_START,
                            L"Buffering before playback starts — waiting for LibVLC to begin playback…");
                        if(!eventsAttached)
                            TorrentSetPlaybackState(TORRENT_PLAYBACK_PLAYING,
                                L"Playing normally — LibVLC live event monitoring is unavailable.");
                    } else {
                        TorrentDetachPlayerEvents();
                        SetStatus(L"LibVLC could not start the torrent stream.");
                        TorrentSetLabel(g_torrentStatusLabel,L"LibVLC could not start playback.");
                        g_torrentPlaybackActive=FALSE;g_hasMedia=FALSE;g_current=-1;
                        TorrentSetPlaybackState(TORRENT_PLAYBACK_FAILED,NULL);
                        ResetArtwork();
                        g_torrentStopHttpStream();
                    }
                } else {
                    TorrentSetPlaybackState(TORRENT_PLAYBACK_FAILED,L"Stream failed — LibVLC rejected the local torrent stream URL.");
                    g_torrentStopHttpStream();
                }
            } else {
                TorrentSetPlaybackState(TORRENT_PLAYBACK_FAILED,L"Stream failed — could not encode the local stream URL.");
                g_torrentStopHttpStream();
            }
            free(url);
        } else if(req&&!cancelled) {
            TorrentSetPlaybackState(TORRENT_PLAYBACK_FAILED,req->error[0]?req->error:L"Stream failed — initial buffering failed.");
            g_torrentStopHttpStream();
        } else if(!cancelled) TorrentSetPlaybackState(TORRENT_PLAYBACK_FAILED,L"Stream failed — stream preparation failed.");
        if(req)free(req);
        return 0;
    }
    case WM_TORRENT_INIT_DONE: {
        if(g_torrentInitThread){CloseHandle(g_torrentInitThread);g_torrentInitThread=NULL;}
        g_torrentInitPending=FALSE;
        if(InterlockedCompareExchange(&g_torrentInitResult,0,0)==1) {
            g_torrentInitialized=TRUE;
            {uint64_t used=0;uint64_t quota=g_torrentCacheQuotaGiB*1024ULL*1024ULL*1024ULL;if(g_torrentSetCacheLimit)g_torrentSetCacheLimit(quota,&used);if(g_torrentSetRateLimits){int d=g_torrentDownloadLimitKiB>INT_MAX/1024?INT_MAX:g_torrentDownloadLimitKiB*1024;int u=g_torrentUploadLimitKiB>INT_MAX/1024?INT_MAX:g_torrentUploadLimitKiB*1024;g_torrentSetRateLimits(d,u);}}
            SetStatus(L"Torrent engine ready");
            ShowTorrentWindow(hwnd);
        } else {
            SetStatus(L"Torrent engine initialization failed");
            ABM_MessageBoxW(hwnd,L"The native torrent engine could not initialize. Normal playback and Streaming Centre remain available.",
                        L"Torrent Stream",MB_OK|MB_ICONWARNING);
        }
        return 0;
    }
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
    case WM_TIMER:
        if(wp==VISUALIZER_TIMER_ID) {
            if(g_visualizerActive) {
                ++g_visualizerFrame;
                if(g_visualizerMode==4 && ABM_ProjectM_IsReady()) {
                    float pcm[512];
                    ABM_AudioVisualizer_GetSamples(pcm,512);
                    ABM_ProjectM_SetAudioSamples(pcm,512);
                } else InvalidateRect(hwnd,g_fullscreen?NULL:&R_VIDEO,FALSE);
            } else {
                KillTimer(hwnd,VISUALIZER_TIMER_ID);
                g_visualizerTimerRunning=FALSE;
            }
            return 0;
        }
        if(wp==TIMER_ID){
        /* LibVLC may create an additional native child HWND for the actual
           video output after playback starts. Keep the input bridge attached
           to those descendants as well as the supplied drawable window. */
        if(IsWindow(g_video))EnumChildWindows(g_video,SubclassVideoDescendant,(LPARAM)hwnd);
        VlcCodecVerificationPoll();
        TorrentCheckPlaybackStall();
        CheckAudioOutputAvailability();
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
            /* Refresh only the small bottom overlay strip at the existing UI
               tick so the playhead follows playback continuously. */
            if(g_fullscreenControlsVisible && g_fullscreenOverlay)
                InvalidateRect(g_fullscreenOverlay,NULL,FALSE);
        } else {
            g_fullscreenMousePosValid = FALSE;
        }
        if(g_discovererCount && !g_discoveryTimedOut && g_rendererCount==0 && g_discoveryStartedTick && (DWORD)(GetTickCount()-g_discoveryStartedTick)>=15000){
            g_discoveryTimedOut=TRUE;
            SetStatus(L"No Google Cast receivers found - check Cast support, same Wi-Fi and mDNS");
        }
        int oldDrawerProgress=g_drawerProgress;
        g_drawerProgress=EaseToward(g_drawerProgress,g_drawerOpen?100:0,24);
        if(oldDrawerProgress!=g_drawerProgress){UpdateLayout(hwnd);InvalidateRect(hwnd,NULL,FALSE);}
        int oldMenuProgress=g_menuProgress;
        g_menuProgress=EaseToward(g_menuProgress,g_menuOpen?100:0,28);
        if(oldMenuProgress!=g_menuProgress){
            /* Menu animation is paint-only. Move the LibVLC child window
               once at open, and once after the closing animation completes. */
            if(oldMenuProgress>0 && g_menuProgress==0)
                PositionVideoWindow();
            InvalidateRect(hwnd,NULL,FALSE);
        }
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
        if(g_hasMedia&&g_player&&!g_torrentPlaybackActive&&
           (DWORD)(GetTickCount()-g_lastResumeSaveTick)>=10000)
            ResumeCaptureCurrent();
        if(g_hasMedia && g_player && !g_pendingResume &&
           libvlc_media_player_get_state(g_player)==libvlc_Error){
            if(g_torrentPlaybackActive)
                TorrentHandlePlayerError();
            else
                PlaybackHandleEngineError();
        }
        if(g_pendingResume && g_player){
            libvlc_state_t resumeState=libvlc_media_player_get_state(g_player);
            if(resumeState==libvlc_Playing){
                if(g_pendingResumeMs>0)PlaybackSeekTo(g_pendingResumeMs);
                if(g_pendingResumePause)PlaybackSetPaused(TRUE);
                g_pendingResume=FALSE;
            }else if(resumeState==libvlc_Error || resumeState==libvlc_Ended || ++g_pendingResumeTicks>90){
                g_pendingResume=FALSE;
                if(g_selectedRenderer && g_hasMedia){
                    g_hasMedia=FALSE;g_endHandled=TRUE;ResetArtwork();
                    SetStatus(L"Cast connection failed - playback was not switched to this PC");
                }else{
                    SetStatus(L"Could not restore the previous playback position");
                }
            }
        }
        if(g_hasMedia&&!g_endHandled&&libvlc_media_player_get_state(g_player)==libvlc_Ended) {
            g_endHandled=TRUE;
            if(g_repeat==1)PlayIndex(g_current);
            else if(g_playlistCount>1 && (g_repeat==2 || g_shuffle || g_current<g_playlistCount-1))PlayIndex(GetNextPlaylistIndex());
            else {g_hasMedia=FALSE;ResetArtwork();InvalidateRect(hwnd,NULL,FALSE);}
        }
        /* Lightweight 30 Hz hover easing for idle dashboard cards.
           This is paint-only: no video child movement, decoding or I/O. */
        if(!g_hasMedia&&!g_drawerOpen&&!g_menuOpen&&!g_fullscreen) {
            POINT homeMouse={-1,-1};BOOL haveHomeMouse=GetCursorPos(&homeMouse)&&ScreenToClient(hwnd,&homeMouse);
            RECT homeCards[4]={R_HOME_OPEN,R_HOME_STREAM,R_HOME_TORRENT,R_HOME_RECENT};
            BOOL changed=FALSE;
            for(int ci=0;ci<4;ci++) {
                int target=haveHomeMouse&&Inside(homeCards[ci],homeMouse.x,homeMouse.y)?100:0;
                int before=g_homeCardMotion[ci];
                g_homeCardMotion[ci]=EaseToward(before,target,34);
                if(before!=g_homeCardMotion[ci])changed=TRUE;
            }
            if(changed)InvalidateRect(hwnd,&R_VIDEO,FALSE);
        } else {
            BOOL changed=FALSE;for(int ci=0;ci<4;ci++){int before=g_homeCardMotion[ci];g_homeCardMotion[ci]=EaseToward(before,0,40);if(before!=g_homeCardMotion[ci])changed=TRUE;}
            if(changed)InvalidateRect(hwnd,&R_VIDEO,FALSE);
        }
        g_visualPulse=(g_visualPulse+1)%10000;
        if(g_visualizerActive&&!g_visualizerTimerRunning)++g_visualizerFrame;
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
        BOOL audioOnlyLocal=g_hasMedia && g_player &&
            !g_selectedRenderer && libvlc_media_player_has_vout(g_player)==0;
        if(audioOnlyLocal) {
            /* Keep capture independent from artwork; the visualizer owns the
               complete media stage whenever the local track is audio-only. */
            if(!g_visualizerTried) {
                g_visualizerTried=TRUE;
                g_visualizerActive=ABM_AudioVisualizer_Start();
            }
            if(g_visualizerActive) {
                if(g_visualizerMode==4)EnsureProjectMRenderer(hwnd);
                else ABM_ProjectM_SetBounds(NULL,FALSE);
                if(!g_visualizerTimerRunning) {
                    g_visualizerTimerRunning=(SetTimer(hwnd,VISUALIZER_TIMER_ID,16,NULL)!=0);
                }
                /* Hide the native VLC child in both windowed and fullscreen
                   modes; it would otherwise cover the owner-painted visuals. */
                if(g_video) ShowWindow(g_video,SW_HIDE);
                g_videoBoundsValid=FALSE;
            } else if(g_video && !g_fullscreen) ShowWindow(g_video,SW_SHOWNA);
        } else {
            if(g_visualizerActive) ABM_AudioVisualizer_Stop();
            if(g_visualizerTimerRunning) {
                KillTimer(hwnd,VISUALIZER_TIMER_ID);
                g_visualizerTimerRunning=FALSE;
            }
            g_visualizerActive=FALSE; g_visualizerTried=FALSE;
            if(g_projectmTried)ABM_ProjectM_Destroy();
            g_projectmTried=FALSE;
            ABM_ProjectM_SetBounds(NULL,FALSE);
            if(g_video && !g_fullscreen) {
                if (g_hasMedia) ShowWindow(g_video,SW_SHOWNA);
                else ShowWindow(g_video,SW_HIDE);
            }
        }
        if (!g_hasMedia || g_visualizerActive) {
            /* Fullscreen audio visualization is painted by the owner window,
               not the hidden LibVLC child. Invalidate the entire fullscreen
               client surface so every animation frame is presented. The
               windowed path retains its bounded video-stage invalidation. */
            if(g_fullscreen) {
                RECT fullClient;
                GetClientRect(hwnd,&fullClient);
                InvalidateRect(hwnd,&fullClient,FALSE);
            } else {
                InvalidateRect(hwnd,&R_VIDEO,FALSE);
            }
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
    case WM_LBUTTONDOWN: {
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        /* A click delivered to the main window is outside the separate Recent
           popup. Dismiss it here, while allowing the clicked main-window
           control to continue processing normally. */
        if(g_recentPopupWindow&&IsWindow(g_recentPopupWindow))DestroyWindow(g_recentPopupWindow);
        if(g_fullscreen) {
            if(Inside(R_PLAY,x,y)) {
                if(g_hasMedia) {
                    if(libvlc_media_player_get_state(g_player)==libvlc_Playing)PlaybackSetPaused(TRUE);
                    else PlaybackStartEngine();
                }
                InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;
            }
            if(Inside(R_PREV,x,y)){if(g_playlistCount)PlayIndex(g_current>0?g_current-1:0);ReleaseCapture();return 0;}
            if(Inside(R_NEXT,x,y)){if(g_playlistCount)PlayIndex(GetNextPlaylistIndex());ReleaseCapture();return 0;}
            if(Inside(R_STOP,x,y)){
                ULONGLONG stopStart=GetTickCount64();
                FullscreenDiag("fullscreen-stop-begin",0);
                if(g_torrentPlaybackActive||g_torrentStreamThread)TorrentStopPlayback();
                PlaybackStopEngine();
                FullscreenDiag("fullscreen-stop-call",GetTickCount64()-stopStart);
                g_hasMedia=FALSE;ResetArtwork();InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;
            }
            if(y>=R_SEEK.top-12&&y<=R_SEEK.bottom+14){g_dragSeek=1;g_dragSeekX=x;SetCapture(hwnd);return 0;}
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
        if(!g_fullscreen&&!g_drawerOpen&&Inside(R_DRAWER_BUTTON,x,y)){RefreshMissingMediaFlags();g_drawerOpen=TRUE;g_drawerSettingsOpen=FALSE;g_receiverPopup=FALSE;g_audioPopupOpen=FALSE;g_searchFocused=FALSE;g_menuOpen=FALSE;SyncRendererComboVisibility();InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&max(0,0)==0&&Inside(R_PLAYLIST_SCROLL,x,y)){
            g_dragPlaylistScroll=TRUE;
            g_playlistScrollGrabY=y;
            g_playlistScrollStart=g_playlistScroll;
            SetCapture(hwnd);
            return 0;
        }
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_PLAYLIST_THUMB,x,y)){
            g_dragPlaylistScroll=TRUE;g_playlistScrollGrabY=y;g_playlistScrollStart=g_playlistScroll;
            SetCapture(hwnd);return 0;
        }
        if(g_drawerOpen&&!g_fullscreen){RECT settingsHit=R_SETTINGS_BUTTON;InflateRect(&settingsHit,8,8);if(Inside(settingsHit,x,y)){g_drawerSettingsOpen=!g_drawerSettingsOpen;g_receiverPopup=FALSE;g_searchFocused=FALSE;UpdateLayout(hwnd);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}}
        if(g_drawerOpen&&g_drawerSettingsOpen&&!g_fullscreen&&Inside(R_ADVANCED_SETTINGS_BUTTON,x,y)){ShowAdvancedSettingsDialog(hwnd);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_SEARCH,x,y)){g_searchFocused=TRUE;SetFocus(hwnd);InvalidateRect(hwnd,&R_SEARCH,FALSE);ReleaseCapture();return 0;}
        if(!g_fullscreen&&g_searchFocused&&!Inside(R_SEARCH,x,y)){g_searchFocused=FALSE;InvalidateRect(hwnd,&R_SEARCH,FALSE);}
        if(g_audioPopupOpen) {
            int rows=g_audioDeviceCount+1;if(rows>12)rows=12;
            if(Inside(R_AUDIO_PANEL,x,y)) {if(y>=R_AUDIO_PANEL.top+42){int item=(y-(R_AUDIO_PANEL.top+42))/38;if(item>=0&&item<rows)SelectAudioOutput(item-1);}ReleaseCapture();return 0;}
            if(!Inside(R_AUDIO_BUTTON,x,y))g_audioPopupOpen=FALSE;
        }
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_CLEAR_PLAYLIST,x,y)){ShowPlaylistContextMenu(hwnd,x,y,-1);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_TAB_QUEUE,x,y)){g_libraryTab=0;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_TAB_LIBRARY,x,y)){g_libraryTab=1;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_FILTER_ALL,x,y)){g_mediaFilter=0;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_FILTER_AUDIO,x,y)){g_mediaFilter=1;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_FILTER_VIDEO,x,y)){g_mediaFilter=2;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_GROUP,x,y)){if(g_libraryTab==1){if(g_showFavorites){g_showFavorites=FALSE;g_showContinueWatching=TRUE;}else if(g_showContinueWatching){g_showContinueWatching=FALSE;}else{g_showFavorites=TRUE;EnsureFavoritesInPlaylist();}}else g_groupMode=(g_groupMode+1)%3;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(!g_fullscreen&&Inside(R_MENU_BUTTON,x,y)){
            g_menuOpen=!g_menuOpen;g_receiverPopup=FALSE;
            if(g_menuOpen){g_menuSelection=0;g_hoverMenuItem=-1;}
            PositionVideoWindow();
            SyncRendererComboVisibility();
            RECT dirty=R_MENU_PANEL;UnionRect(&dirty,&dirty,&R_MENU_BUTTON);
            InvalidateRect(hwnd,&dirty,FALSE);ReleaseCapture();return 0;
        }
        /* Clicking anywhere outside the open menu dismisses it. Continue
           processing the click so the underlying control still responds. */
        if(g_drawerOpen&&!g_fullscreen&&!Inside(R_SIDEBAR,x,y)&&!Inside(R_DRAWER_BUTTON,x,y)){
            g_drawerOpen=FALSE;g_drawerSettingsOpen=FALSE;g_receiverPopup=FALSE;g_audioPopupOpen=FALSE;g_searchFocused=FALSE;
            SyncRendererComboVisibility();InvalidateRect(hwnd,NULL,FALSE);
        }
        if(g_menuOpen && !Inside(R_MENU_PANEL,x,y)) {
            g_menuOpen=FALSE;
            PositionVideoWindow();
            SyncRendererComboVisibility();
            RECT dirty=R_MENU_PANEL;
            InvalidateRect(hwnd,&dirty,FALSE);
        }
        if(!g_fullscreen&&Inside(R_FULLSCREEN,x,y)){ToggleFullscreen(hwnd);ReleaseCapture();return 0;}
        if(!g_fullscreen&&!g_hasMedia&&!g_drawerOpen) {
            if(Inside(R_HOME_OPEN,x,y)){AddFiles();ReleaseCapture();return 0;}
            if(Inside(R_HOME_STREAM,x,y)){ABM_Streaming_Show(hwnd);ReleaseCapture();return 0;}
            if(Inside(R_HOME_TORRENT,x,y)){ShowTorrentWindow(hwnd);ReleaseCapture();return 0;}
            if(Inside(R_HOME_RECENT,x,y)){EnsureFavoritesInPlaylist();const wchar_t *paths[MAX_PLAYLIST];for(int i=0;i<g_playlistCount;i++)paths[i]=g_playlist[i];ABM_HomeTheatre_Show(hwnd,paths,g_playlistCount,PlayIndex);ReleaseCapture();return 0;}
        }
        if(g_menuOpen&&g_menuProgress>=80) {
            for(int mi=0;mi<19;mi++)if(Inside(R_MENU_ITEMS[mi],x,y)) {
                g_menuOpen=FALSE;
                PositionVideoWindow();
                SyncRendererComboVisibility();
                if(mi==0)AddFiles();
                else if(mi==1)StartDiscovery();
                else if(mi==2){StartDiscovery();ABM_MessageBoxW(hwnd,L"Choose your TV in the Playback Output selector. If media is already playing, the player will restart it on the selected receiver.",L"Cast to TV",MB_OK|MB_ICONINFORMATION);}
                else if(mi==3){if(g_hasMedia)PlaybackTogglePause();}
                else if(mi==4)ToggleFullscreen(hwnd);
                else if(mi==5)LoadPlaylist();
                else if(mi==6)SavePlaylist();
                else if(mi==7){POINT rp={x,y};ShowRecentMenu(hwnd,rp.x,rp.y);}
                else if(mi==8){g_shuffle=!g_shuffle;SavePersistentState();}
                else if(mi==9){g_repeat=(g_repeat==0)?2:(g_repeat==2?1:0);SavePersistentState();}
                else if(mi==10)ShowEqualizer();
                else if(mi==11)ABM_Streaming_Show(hwnd);
                else if(mi==12)ShowTorrentWindow(hwnd);
                else if(mi==13)ShowHelpDialog(hwnd);
                else if(mi==14)ABM_CheckForUpdates(hwnd,APP_VERSION);
                else if(mi==15){if(ABM_LicenseShowActivation(GetModuleHandleW(NULL),hwnd))ABM_MessageBoxW(hwnd,L"ABM AV Player is now activated. All features are unlocked.",L"Activation successful",MB_OK|MB_ICONINFORMATION);}
                else if(mi==16)ShowSettingsDialog(hwnd);
                else if(mi==17)ShowAboutDialog(hwnd);
                else if(mi==18)PostMessageW(hwnd,WM_CLOSE,0,0);
                InvalidateRect(hwnd,NULL,FALSE);return 0;
            }
        }
        SetCapture(hwnd);
        if(Inside(R_OPEN,x,y)){AddFiles();ReleaseCapture();return 0;}
        if(Inside(R_PLAY,x,y)){if(g_hasMedia)PlaybackTogglePause();else if(g_playlistCount)PlayIndex(g_selected<0?0:g_selected);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(Inside(R_STOP,x,y)){
            ULONGLONG stopStart=GetTickCount64();
            FullscreenDiag("windowed-stop-begin",0);
            if(g_torrentPlaybackActive||g_torrentStreamThread)TorrentStopPlayback();
            ResumeCaptureCurrent();
            PlaybackStopEngine();
            FullscreenDiag("windowed-stop-call",GetTickCount64()-stopStart);
            g_hasMedia=FALSE;ResetArtwork();InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;
        }
        if(Inside(R_NEXT,x,y)){if(g_playlistCount)PlayIndex(GetNextPlaylistIndex());ReleaseCapture();return 0;}
        if(Inside(R_PREV,x,y)){if(g_playlistCount)PlayIndex(g_current>0?g_current-1:0);ReleaseCapture();return 0;}
        if(g_drawerOpen&&g_drawerSettingsOpen&&Inside(R_SCAN,x,y)){StartDiscovery();ReleaseCapture();return 0;}
        /* The receiver popup is painted over the quick-action area. Its hit
           test must run before shuffle/speed/recent hit tests, otherwise the
           first receiver rows are consumed by the controls underneath. */
        if(g_drawerOpen&&g_drawerSettingsOpen&&g_receiverPopup && Inside(R_RECEIVER_LIST,x,y)) {
            int rowY=y-(R_RECEIVER_LIST.top+4);
            int item=rowY>=0?rowY/36:-1;
            int count=(int)SendMessageW(g_renderers,CB_GETCOUNT,0,0);
            int rows=count>6?6:count;
            if(item>=0&&item<rows && rowY%36<32) {
                SendMessageW(g_renderers,CB_SETCURSEL,(WPARAM)item,0);
                SelectRenderer();
            }
            g_receiverPopup=FALSE;
            InvalidateRect(hwnd,NULL,FALSE);
            ReleaseCapture();
            return 0;
        }
        if(g_drawerOpen&&g_drawerSettingsOpen&&Inside(R_SHUFFLE,x,y)){g_shuffle=!g_shuffle;SavePersistentState();InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&g_drawerSettingsOpen&&Inside(R_REPEAT,x,y)){g_repeat=(g_repeat==0)?2:(g_repeat==2?1:0);SavePersistentState();InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&g_drawerSettingsOpen&&Inside(R_SPEED,x,y)){
            if(g_playbackRate<0.99f)g_playbackRate=1.0f;
            else if(g_playbackRate<1.24f)g_playbackRate=1.25f;
            else if(g_playbackRate<1.49f)g_playbackRate=1.5f;
            else if(g_playbackRate<1.99f)g_playbackRate=2.0f;
            else g_playbackRate=0.75f;
            libvlc_media_player_set_rate(g_player,g_playbackRate);
            SavePersistentState();InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;
        }
        if(g_drawerOpen&&g_drawerSettingsOpen&&Inside(R_RECENT,x,y)){g_recentOpenPending=TRUE;ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_AUDIO_BUTTON,x,y)){RefreshAudioDevices();g_audioPopupOpen=!g_audioPopupOpen;g_receiverPopup=FALSE;InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&g_drawerSettingsOpen&&Inside(R_RECEIVER,x,y)){g_receiverPopup=!g_receiverPopup;InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(g_receiverPopup){g_receiverPopup=FALSE;InvalidateRect(hwnd,NULL,FALSE);}
        if(y>=R_SEEK.top-12&&y<=R_SEEK.bottom+14){g_dragSeek=1;SeekAt(x);ReleaseCapture();return 0;}
        if(y>=R_VOLUME.top-12&&y<=R_VOLUME.bottom+12){g_dragVolume=1;int v=(x-R_VOLUME.left)*100/(R_VOLUME.right-R_VOLUME.left);if(v<0)v=0;if(v>100)v=100;g_volume=v;libvlc_audio_set_volume(g_player,v);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_LIST,x,y)){for(int ri=0;ri<g_visibleRowCount;ri++)if(Inside(R_FAVORITE[ri],x,y)){ToggleFavoritePath(g_playlist[g_visibleRows[ri]]);ReleaseCapture();return 0;}}
        if(g_drawerOpen&&!g_fullscreen&&g_libraryTab==0&&Inside(R_LIST,x,y)) {
            for(int ri=0;ri<g_visibleRowCount;ri++)if(Inside(R_ROW[ri],x,y)) {
                g_queueDragPending=TRUE;g_queueDragging=FALSE;
                g_queueDragSource=g_visibleRows[ri];g_queueDropBoundary=g_queueDragSource;
                g_queueDragStartX=x;g_queueDragStartY=y;
                SetCapture(hwnd);return 0;
            }
        }
        for(int ri=0;ri<g_visibleRowCount;ri++)if(Inside(R_ROW[ri],x,y)){int mediaIndex=g_visibleRows[ri];g_selected=mediaIndex;PlayIndex(mediaIndex);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        ReleaseCapture();return 0;
    }
    case WM_KEYDOWN: {
        /* Fullscreen Escape has priority over drawer/search/menu shortcuts.
           Otherwise a stale focus state can consume Escape without restoring
           the compact player. */
        if(wp==VK_ESCAPE && g_fullscreen){
            FullscreenDiag("main-wndproc-escape-keydown",0);
            ToggleFullscreen(hwnd);
            return 0;
        }
        BOOL ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        BOOL shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(g_menuOpen){
            if(wp==VK_ESCAPE){g_menuOpen=FALSE;g_hoverMenuItem=-1;PositionVideoWindow();SyncRendererComboVisibility();InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
            if(wp==VK_DOWN||wp==VK_TAB){g_menuSelection=(g_menuSelection+1)%19;g_hoverMenuItem=-1;InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
            if(wp==VK_UP){g_menuSelection=(g_menuSelection+18)%19;g_hoverMenuItem=-1;InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
            if(wp==VK_HOME){g_menuSelection=0;g_hoverMenuItem=-1;InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
            if(wp==VK_END){g_menuSelection=18;g_hoverMenuItem=-1;InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
            if(wp==VK_RETURN){if(g_menuProgress<80)return 0;int chosen=g_menuSelection;POINT p={R_MENU_ITEMS[chosen].left+12,R_MENU_ITEMS[chosen].top+12};SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(p.x,p.y));SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(p.x,p.y));return 0;}
        }
        if(wp==VK_F10&&!shift){g_menuOpen=!g_menuOpen;if(g_menuOpen)g_menuSelection=0;g_hoverMenuItem=-1;PositionVideoWindow();SyncRendererComboVisibility();InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
        if(ctrl && !shift && wp=='O'){AddFiles();return 0;}
        if(ctrl && !shift && wp=='D'){StartDiscovery();return 0;}
        if(ctrl && !shift && wp=='E'){ShowEqualizer();return 0;}
        if(ctrl && shift && wp=='C'){StartDiscovery();ABM_MessageBoxW(hwnd,L"Choose your TV in the Playback Output selector. If media is already playing, the player will restart it on the selected receiver.",L"Cast to TV",MB_OK|MB_ICONINFORMATION);return 0;}
        if(ctrl && !shift && wp=='L'){LoadPlaylist();return 0;}
        if(ctrl && !shift && wp=='S'){SavePlaylist();return 0;}
        if(ctrl && !shift && wp=='I'){ShowAboutDialog(hwnd);return 0;}
        if(ctrl && shift && wp=='U'){ABM_CheckForUpdates(hwnd,APP_VERSION);return 0;}
        if(ctrl && shift && wp=='L'){if(ABM_LicenseShowActivation(GetModuleHandleW(NULL),hwnd))ABM_MessageBoxW(hwnd,L"ABM AV Player is now activated. All features are unlocked.",L"Activation successful",MB_OK|MB_ICONINFORMATION);return 0;}
        if(ctrl && !shift && wp==VK_OEM_COMMA){ShowSettingsDialog(hwnd);return 0;}
        if(ctrl && shift && wp=='S'){ABM_Streaming_Show(hwnd);return 0;}
        if(ctrl && shift && wp=='T'){ShowTorrentWindow(hwnd);return 0;}
        if(ctrl && shift && wp=='H'){g_shuffle=!g_shuffle;SavePersistentState();InvalidateRect(hwnd,NULL,FALSE);return 0;}
        if(ctrl && !shift && wp=='R'){g_repeat=(g_repeat==0)?2:(g_repeat==2?1:0);SavePersistentState();InvalidateRect(hwnd,NULL,FALSE);return 0;}
        if(ctrl && !shift && wp=='J'){POINT p;GetCursorPos(&p);ScreenToClient(hwnd,&p);ShowRecentMenu(hwnd,p.x,p.y);return 0;}
        if(wp==VK_F1){ShowHelpDialog(hwnd);return 0;}
        if(g_searchFocused&&wp==VK_ESCAPE){g_searchFocused=FALSE;InvalidateRect(hwnd,&R_SEARCH,FALSE);return 0;}
        if(wp==VK_ESCAPE&&g_drawerOpen){g_drawerOpen=FALSE;g_drawerSettingsOpen=FALSE;g_receiverPopup=FALSE;g_searchFocused=FALSE;SyncRendererComboVisibility();InvalidateRect(hwnd,NULL,FALSE);return 0;}
        if(g_searchFocused&&wp==VK_BACK){size_t n=wcslen(g_searchText);if(n)g_searchText[n-1]=0;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);return 0;}
        if(g_searchFocused&&wp==VK_RETURN){g_searchFocused=FALSE;InvalidateRect(hwnd,&R_SEARCH,FALSE);return 0;}
        if(g_searchFocused&&(wp==VK_SPACE||wp==VK_LEFT||wp==VK_RIGHT))return 0;
        if(wp==VK_F11){ToggleFullscreen(hwnd);return 0;}
        if(wp==VK_SPACE&&g_hasMedia){if(libvlc_media_player_get_state(g_player)==libvlc_Playing)PlaybackSetPaused(TRUE);else PlaybackStartEngine();return 0;}
        if(wp==VK_RIGHT&&g_hasMedia){int64_t t=libvlc_media_player_get_time(g_player);PlaybackSeekTo(t+10000);return 0;}
        if(wp==VK_LEFT&&g_hasMedia){int64_t t=libvlc_media_player_get_time(g_player);PlaybackSeekTo(t>10000?t-10000:0);return 0;}
        if((wp==VK_OEM_PLUS||wp==VK_ADD)&&g_hasMedia){if(g_playbackRate<2.0f)g_playbackRate+=0.25f;libvlc_media_player_set_rate(g_player,g_playbackRate);InvalidateRect(hwnd,NULL,FALSE);return 0;}
        if((wp==VK_OEM_MINUS||wp==VK_SUBTRACT)&&g_hasMedia){if(g_playbackRate>0.5f)g_playbackRate-=0.25f;libvlc_media_player_set_rate(g_player,g_playbackRate);InvalidateRect(hwnd,NULL,FALSE);return 0;}
        if(wp==VK_HOME&&g_hasMedia){PlaybackSeekTo(0);return 0;}
        if(wp=='N'&&g_playlistCount){PlayIndex(GetNextPlaylistIndex());return 0;}
        if(wp=='P'&&g_playlistCount){PlayIndex(g_current>0?g_current-1:g_playlistCount-1);return 0;}
        if(wp==VK_ESCAPE&&g_menuOpen){g_menuOpen=FALSE;PositionVideoWindow();SyncRendererComboVisibility();InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
        break;
    }
    case WM_CHAR:
        if(g_searchFocused){wchar_t ch=(wchar_t)wp;if(ch>=32&&ch!=127){size_t n=wcslen(g_searchText);if(n<127){g_searchText[n]=ch;g_searchText[n+1]=0;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);}}return 0;}
        break;
    case WM_MOUSEWHEEL: {
        if(g_drawerOpen&&!g_fullscreen) {
            /* Win32 wheel coordinates are screen-relative, including events
               forwarded by the LibVLC child window. */
            POINT wheelPoint={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd,&wheelPoint);
            if(Inside(R_LIST,wheelPoint.x,wheelPoint.y)) {
                int delta=GET_WHEEL_DELTA_WPARAM(wp);
                int notches=delta/WHEEL_DELTA;
                if(notches) {
                    int visible=PlaylistVisibleRows(PlaylistRowHeight());
                    int maxScroll=CountFilteredMedia()-visible;
                    if(maxScroll<0)maxScroll=0;
                    /* Three media rows per wheel notch, matching Windows list
                       control behaviour while preserving the existing scrollbar. */
                    g_playlistScroll-=notches*3;
                    if(g_playlistScroll<0)g_playlistScroll=0;
                    if(g_playlistScroll>maxScroll)g_playlistScroll=maxScroll;
                    InvalidateRect(hwnd,&R_SIDEBAR,FALSE);
                }
                return 0;
            }
        }
        break;
    }
    case WM_VIDEO_CONTEXT_REQUEST: {
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        if(Inside(R_VIDEO,x,y))ShowPlaybackContextMenu(hwnd,x,y);
        return 0;
    }
    case WM_RBUTTONDOWN: {
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp),index=-1;
        for(int ri=0;ri<g_visibleRowCount;ri++)if(Inside(R_ROW[ri],x,y)){index=g_visibleRows[ri];break;}
        /* Open on button-down so the menu does not depend on the video
           renderer delivering WM_RBUTTONUP to the parent. The LibVLC child
           input bridge forwards this message from the actual rendered frame. */
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_LIST,x,y)){
            ShowPlaylistContextMenu(hwnd,x,y,index);return 0;
        }
        if(Inside(R_VIDEO,x,y)){ShowPlaybackContextMenu(hwnd,x,y);return 0;}
        break;
    }
    case WM_CONTEXTMENU: {
        if(g_drawerOpen&&!g_fullscreen){
            int sx=GET_X_LPARAM(lp),sy=GET_Y_LPARAM(lp),x,y,index=-1;
            if(sx==-1&&sy==-1){
                index=g_selected;
                x=R_LIST.left+24;y=R_LIST.top+24;
                for(int ri=0;ri<g_visibleRowCount;ri++)if(g_visibleRows[ri]==index){
                    x=(R_ROW[ri].left+R_ROW[ri].right)/2;
                    y=(R_ROW[ri].top+R_ROW[ri].bottom)/2;break;
                }
                POINT pt={x,y};ClientToScreen(hwnd,&pt);sx=pt.x;sy=pt.y;
            }
            POINT pt={sx,sy};ScreenToClient(hwnd,&pt);x=pt.x;y=pt.y;
            if(Inside(R_LIST,x,y)){
                if(index<0)for(int ri=0;ri<g_visibleRowCount;ri++)if(Inside(R_ROW[ri],x,y)){index=g_visibleRows[ri];break;}
                ShowPlaylistContextMenu(hwnd,x,y,index);return 0;
            }
        }
        break;
    }
    case WM_LBUTTONDBLCLK: { g_queueDragPending=FALSE;g_queueDragging=FALSE;int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp); if(g_advancedDoubleClickFullscreen&&Inside(R_VIDEO,x,y)){ToggleFullscreen(hwnd);return 0;} for(int ri=0;ri<g_visibleRowCount;ri++)if(Inside(R_ROW[ri],x,y)){g_selected=g_visibleRows[ri];PlayIndex(g_visibleRows[ri]);return 0;} return 0; }
    case WM_MOUSEMOVE: {
        int mx=GET_X_LPARAM(lp),my=GET_Y_LPARAM(lp),hit=HIT_NONE;
        if(g_queueDragPending&&g_libraryTab==0&&g_drawerOpen&&!g_fullscreen&&(wp&MK_LBUTTON)) {
            int dx=abs(mx-g_queueDragStartX),dy=abs(my-g_queueDragStartY);
            if(!g_queueDragging&&(dx>=GetSystemMetrics(SM_CXDRAG)||dy>=GetSystemMetrics(SM_CYDRAG)))
                g_queueDragging=TRUE;
            if(g_queueDragging) {
                int boundary=g_queueDragSource;
                if(my>=R_LIST.bottom)boundary=g_playlistCount;
                else if(my>R_LIST.top) {
                    boundary=g_playlistCount;
                    for(int ri=0;ri<g_visibleRowCount;ri++) {
                        int mid=(R_ROW[ri].top+R_ROW[ri].bottom)/2;
                        if(my<mid){boundary=g_visibleRows[ri];break;}
                        boundary=g_visibleRows[ri]+1;
                    }
                }
                g_queueDropBoundary=boundary;
                InvalidateRect(hwnd,&R_SIDEBAR,FALSE);
                SetCursor(LoadCursorW(NULL,IDC_SIZEALL));
            }
        }
        int playlistHot=-1;
        if(g_drawerProgress>=90&&Inside(R_LIST,mx,my)){for(int ri=0;ri<g_visibleRowCount;ri++)if(Inside(R_ROW[ri],mx,my)){playlistHot=g_visibleRows[ri];break;}}
        if(playlistHot!=g_hoverPlaylistIndex){g_hoverPlaylistIndex=playlistHot;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);}
        int menuHot=-1;
        if(g_menuOpen&&g_menuProgress>=35){for(int mi=0;mi<19;mi++)if(Inside(R_MENU_ITEMS[mi],mx,my)){menuHot=mi;break;}}
        if(menuHot!=g_hoverMenuItem){g_hoverMenuItem=menuHot;if(menuHot>=0)g_menuSelection=menuHot;InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);}
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
        int chrome=Inside(R_MENU_BUTTON,mx,my)?1:(Inside(R_DRAWER_BUTTON,mx,my)?2:0);
        if(chrome!=g_hoverChromeControl){g_hoverChromeControl=chrome;InvalidateRect(hwnd,NULL,FALSE);}
        if(Inside(R_OPEN,mx,my))hit=HIT_OPEN;else if(Inside(R_SCAN,mx,my))hit=HIT_SCAN;
        else if(Inside(R_PLAY,mx,my))hit=HIT_PLAY;else if(Inside(R_STOP,mx,my))hit=HIT_STOP;
        else if(Inside(R_PREV,mx,my))hit=HIT_PREV;else if(Inside(R_NEXT,mx,my))hit=HIT_NEXT;
        if(hit!=g_hoverButton){
            int previous=g_hoverButton;
            g_hoverButton=hit;
            InvalidateButton(previous);
            InvalidateButton(g_hoverButton);
            RECT hintStrip={0,0,0,0};GetClientRect(hwnd,&hintStrip);
            hintStrip.top=hintStrip.bottom-30;
            InvalidateRect(hwnd,&hintStrip,FALSE);
        }
        if(g_dragPlaylistScroll){
            int visible=PlaylistVisibleRows(PlaylistRowHeight());
            int total=CountFilteredMedia(),maxScroll=total-visible;if(maxScroll<0)maxScroll=0;
            int railH=R_PLAYLIST_SCROLL.bottom-R_PLAYLIST_SCROLL.top;
            int thumbH=(total>0)?(int)((long long)railH*visible/total):railH;
            if(thumbH<42)thumbH=42;if(thumbH>railH)thumbH=railH;
            int travel=railH-thumbH;
            if(travel>0){int dy=my-g_playlistScrollGrabY;g_playlistScroll=g_playlistScrollStart+(int)((long long)dy*maxScroll/travel);}
            if(g_playlistScroll<0)g_playlistScroll=0;if(g_playlistScroll>maxScroll)g_playlistScroll=maxScroll;
            InvalidateRect(hwnd,&R_SIDEBAR,FALSE);
        }
        if(g_dragSeek)g_dragSeekX=mx;
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
        SetCursor((hit!=HIT_NONE||chrome!=0)?LoadCursorW(NULL,IDC_HAND):LoadCursorW(NULL,IDC_ARROW));
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
        if(!g_fullscreen){POINT pt;GetCursorPos(&pt);ScreenToClient(hwnd,&pt);
            if(Inside(R_AUDIO_BUTTON,pt.x,pt.y)||Inside(R_MENU_BUTTON,pt.x,pt.y)||Inside(R_DRAWER_BUTTON,pt.x,pt.y)||
               Inside(R_OPEN,pt.x,pt.y)||Inside(R_PLAY,pt.x,pt.y)||Inside(R_STOP,pt.x,pt.y)||
               Inside(R_PREV,pt.x,pt.y)||Inside(R_NEXT,pt.x,pt.y)||Inside(R_SCAN,pt.x,pt.y)){
                SetCursor(LoadCursorW(NULL,IDC_HAND));return TRUE;
            }
        }
        break;
    case WM_LBUTTONUP:
        /* Open Recent only after the initiating click has been fully released.
           Showing a separate popup during WM_LBUTTONDOWN lets Windows route
           that same release into the new popup; depending on compact-window
           overlap and z-order, it can immediately consume/dismiss the menu. */
        if(g_recentOpenPending) {
            g_recentOpenPending=FALSE;
            int rx=GET_X_LPARAM(lp),ry=GET_Y_LPARAM(lp);
            if(g_drawerOpen&&g_drawerSettingsOpen&&Inside(R_RECENT,rx,ry))
                ShowRecentMenu(hwnd,rx,ry);
            ReleaseCapture();
            return 0;
        }
        if(g_queueDragPending) {
            BOOL didDrag=g_queueDragging;
            int source=g_queueDragSource,boundary=g_queueDropBoundary;
            g_queueDragPending=FALSE;g_queueDragging=FALSE;
            g_queueDragSource=-1;g_queueDropBoundary=-1;
            if(didDrag&&g_libraryTab==0)QueueMoveItem(source,boundary);
            else if(!didDrag&&source>=0&&source<g_playlistCount) {
                g_selected=source;PlayIndex(source);InvalidateRect(hwnd,NULL,FALSE);
            }
            ReleaseCapture();return 0;
        }
        if(g_dragSeek){
            int seekX=g_dragSeekX;
            g_dragSeek=0;
            /* Submit one seek after the drag ends. Repeated set_time calls while
               the mouse moves can queue decoder seeks faster than they complete. */
            if(g_hasMedia) {
                ULONGLONG seekStart=GetTickCount64();
                FullscreenDiag(g_fullscreen?"fullscreen-seek-submit-begin":"windowed-seek-submit-begin",0);
                SeekAt(seekX);
                FullscreenDiag(g_fullscreen?"fullscreen-seek-submit":"windowed-seek-submit",GetTickCount64()-seekStart);
            }
        }
        g_dragVolume=0;g_dragPlaylistScroll=FALSE;ReleaseCapture();return 0;
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
    case WM_DESTROY:
        if(g_videoMouseHook){UnhookWindowsHookEx(g_videoMouseHook);g_videoMouseHook=NULL;}
        VlcDetachPlaybackEvents();
        TorrentDetachPlayerEvents();
        if(g_torrentStreamThread||g_torrentStreamRequest)TorrentCancelStreamPreparation();
        if(g_torrentTvStreamThread) {
            InterlockedExchange(&g_torrentTvStreamCancel,1);
            if(g_torrentStopHttpStream)g_torrentStopHttpStream();
            WaitForSingleObject(g_torrentTvStreamThread,INFINITE);
            CloseHandle(g_torrentTvStreamThread);g_torrentTvStreamThread=NULL;
            if(g_torrentTvStreamRequest){free(g_torrentTvStreamRequest);g_torrentTvStreamRequest=NULL;}
        }
        if(g_torrentInitThread){WaitForSingleObject(g_torrentInitThread,INFINITE);CloseHandle(g_torrentInitThread);g_torrentInitThread=NULL;}
        g_torrentInitPending=FALSE;
        if(g_torrentTvPlaybackActive)ABM_TorrentDlna_StopPlayback();
        ABM_TorrentDlna_StopDiscovery();
        if(g_torrentInitResult==1 && g_torrentShutdown)g_torrentShutdown();
        g_torrentInitialized=FALSE;
        if(g_torrentModule){
            FreeLibrary(g_torrentModule);g_torrentModule=NULL;
            g_torrentInitialize=NULL;g_torrentShutdown=NULL;
            g_torrentAddMagnet=NULL;g_torrentAddFile=NULL;g_torrentGetTask=NULL;
            g_torrentGetFile=NULL;g_torrentSelectFile=NULL;g_torrentPause=NULL;
            g_torrentResume=NULL;g_torrentRemove=NULL;g_torrentSetRateLimits=NULL;
            g_torrentSetReadWindow=NULL;g_torrentWaitForRange=NULL;
            g_torrentStartHttpStream=NULL;g_torrentStartLanHttpStream=NULL;g_torrentStopHttpStream=NULL;
        }
        ResumeCaptureCurrent();ResumeSaveAsync(TRUE);DurationMetadataStop();ABM_Streaming_Close();ABM_Cast_StreamDiscoveryStop();SavePersistentState();SaveAudioPreferences();if(g_eqWindow)DestroyWindow(g_eqWindow);DragAcceptFiles(hwnd,FALSE);KillTimer(hwnd,TIMER_ID);if(g_visualizerTimerRunning)KillTimer(hwnd,VISUALIZER_TIMER_ID);g_visualizerTimerRunning=FALSE;ABM_AudioVisualizer_Stop();ABM_ProjectM_Destroy();ResetArtwork();ABM_D2D_Destroy(&g_ui);g_main=NULL;StopDiscovery();if(g_streamCastPlayer){libvlc_media_player_stop(g_streamCastPlayer);libvlc_media_player_release(g_streamCastPlayer);g_streamCastPlayer=NULL;}if(g_streamSelectedRenderer){libvlc_renderer_item_release(g_streamSelectedRenderer);g_streamSelectedRenderer=NULL;}PlaybackStopEngine();libvlc_media_player_release(g_player);libvlc_release(g_vlc);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static void EnableDpiAwareness(void) {
    /* Declare awareness before creating any HWND. System-DPI awareness keeps
       Win32 coordinates and the hand-authored Direct2D layout in one stable
       coordinate space, while avoiding Windows bitmap-stretching the window
       on the primary display. Per-monitor scaling needs a full layout pass. */
    HMODULE user32=GetModuleHandleW(L"user32.dll");
    if(user32) {
        typedef BOOL (WINAPI *SetDpiContextFn)(DPI_AWARENESS_CONTEXT);
        SetDpiContextFn setContext=(SetDpiContextFn)GetProcAddress(user32,"SetProcessDpiAwarenessContext");
        if(setContext && setContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE))return;
    }
    SetProcessDPIAware();
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE prev,PWSTR cmd,int show) {
    EnableDpiAwareness();
    (void)prev;(void)cmd;HRESULT comInit=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);srand((unsigned int)GetTickCount());INITCOMMONCONTROLSEX icc={sizeof(icc),ICC_STANDARD_CLASSES};InitCommonControlsEx(&icc);
    /* Load the user's palette before the activation UI is shown. */
    LoadThemePreference();
    /* Licensing is an independent gate; no playback, torrent or casting state is initialized before access is granted. */
    if(!ABM_LicenseCheckAndPrompt(inst,NULL)){if(SUCCEEDED(comInit))CoUninitialize();return 0;}
    InitializeCriticalSection(&g_rendererLock);
    g_videoBrush=CreateSolidBrush(ABM_THEME_COLOR_000000);g_comboBrush=CreateSolidBrush(C_PANEL2);
    g_font=CreateFontW(ABM_UI_FONT_HEIGHT(ABM_UI_FONT_SIZE_D2D_BODY),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,ABM_UI_FONT_FAMILY_TEXT);
    g_smallFont=CreateFontW(ABM_UI_FONT_HEIGHT(ABM_UI_FONT_SIZE_D2D_SMALL),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,ABM_UI_FONT_FAMILY_TEXT);
    g_headingFont=CreateFontW(ABM_UI_FONT_HEIGHT(ABM_UI_FONT_SIZE_D2D_HEADING),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,ABM_UI_FONT_FAMILY_DISPLAY);
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
    const char *args[8]={"--no-video-title-show","--quiet",NULL,NULL,NULL,NULL,NULL,NULL};
    /* Keep LibVLC plugin caching enabled. Disabling its cache forces a full
       plugin scan on every launch and makes startup unnecessarily slow. */
    int argCount=2;
    if(pluginArg[0])args[argCount++]=pluginArg;
    /* Hardware decoding is an instance-level LibVLC startup option. */
    EnsureDataDirectory();
    wchar_t hwSettingsPath[PATH_CAP]={0};
    StringCchPrintfW(hwSettingsPath,_countof(hwSettingsPath),L"%s\\settings.ini",g_dataDir);
    int savedHwMode=GetPrivateProfileIntW(L"Video",L"HardwareAcceleration",0,hwSettingsPath);
    if(savedHwMode<0||savedHwMode>2)savedHwMode=0;
    g_advancedHwMode=savedHwMode;
    wchar_t hwMode[32]=L"";
    GetEnvironmentVariableW(L"ABM_VLC_HW",hwMode,(DWORD)_countof(hwMode));
    if(!_wcsicmp(hwMode,L"none"))args[argCount++]="--avcodec-hw=none";
    else if(!_wcsicmp(hwMode,L"d3d11va"))args[argCount++]="--avcodec-hw=d3d11va";
    else if(savedHwMode==1)args[argCount++]="--avcodec-hw=none";
    else if(savedHwMode==2)args[argCount++]="--avcodec-hw=d3d11va";
    int savedAudioOut=GetPrivateProfileIntW(L"Audio",L"OutputEngine",0,hwSettingsPath);
    if(savedAudioOut<0||savedAudioOut>2)savedAudioOut=0;
    g_advancedAudioOutMode=savedAudioOut;
    g_advancedConfirmExit=GetPrivateProfileIntW(L"Controls",L"ConfirmExit",1,hwSettingsPath)!=0;
  g_advancedConfirmClearPlaylist=GetPrivateProfileIntW(L"Controls",L"ConfirmClearPlaylist",1,hwSettingsPath)!=0;
  g_advancedDoubleClickFullscreen=GetPrivateProfileIntW(L"Controls",L"DoubleClickFullscreen",1,hwSettingsPath)!=0;
  {int savedSubtitleDelayMs=GetPrivateProfileIntW(L"Subtitles",L"DelayMs",0,hwSettingsPath);if(savedSubtitleDelayMs < -10000)savedSubtitleDelayMs=-10000;if(savedSubtitleDelayMs > 10000)savedSubtitleDelayMs=10000;g_advancedSubtitleDelayUs=(int64_t)savedSubtitleDelayMs*1000;}
    {int savedSubtitleSize=GetPrivateProfileIntW(L"Subtitles",L"RelativeFontSize",13,hwSettingsPath);if(savedSubtitleSize!=10&&savedSubtitleSize!=13&&savedSubtitleSize!=16&&savedSubtitleSize!=19)savedSubtitleSize=13;g_advancedSubtitleFontSize=savedSubtitleSize;}
    {int savedSubtitleColor=GetPrivateProfileIntW(L"Subtitles",L"TextColor",16777215,hwSettingsPath);if(savedSubtitleColor!=16777215&&savedSubtitleColor!=16776960&&savedSubtitleColor!=65535&&savedSubtitleColor!=65280)savedSubtitleColor=16777215;g_advancedSubtitleColor=savedSubtitleColor;}
    g_advancedSubtitleBold=GetPrivateProfileIntW(L"Subtitles",L"Bold",0,hwSettingsPath)!=0;
    {int v=GetPrivateProfileIntW(L"Subtitles",L"OutlineThickness",4,hwSettingsPath);if(v!=0&&v!=2&&v!=4&&v!=6)v=4;g_advancedSubtitleOutline=v;}
    {int v=GetPrivateProfileIntW(L"Subtitles",L"Margin",10,hwSettingsPath);if(v!=0&&v!=10&&v!=25&&v!=50)v=10;g_advancedSubtitleMargin=v;}
    if(savedAudioOut==1)args[argCount++]="--aout=mmdevice";
    else if(savedAudioOut==2)args[argCount++]="--aout=directsound";
    g_vlc=libvlc_new(argCount,args);
    if(!g_vlc){ABM_MessageBoxW(NULL,L"Could not initialize LibVLC. Check the VLC installation and plugins.",APP_NAME,MB_ICONERROR);return 1;}
    /* Optional metadata worker: failure to start it never blocks playback. */
    DurationMetadataStart();
    g_player=libvlc_media_player_new(g_vlc);if(!g_player){libvlc_release(g_vlc);return 1;}
    /* Failure of the optional streaming cast player must not affect local playback. */
    g_streamCastPlayer=libvlc_media_player_new(g_vlc);
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
    { int corners=2; COLORREF border=ABM_THEME_COLOR_2E4668,caption=ABM_THEME_COLOR_0A0F19; int backdrop=2;
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

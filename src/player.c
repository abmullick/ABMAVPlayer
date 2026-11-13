#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
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
#include "ui_theme.h"
#include "streaming/streaming_host.h"
#include "updater.h"
#include "resource.h"
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <strsafe.h>
#include <stdlib.h>
#include <limits.h>
#include <shlobj.h>
#include <KnownFolders.h>
#include <urlmon.h>
#include <winhttp.h>
#include <wincodec.h>
#include <ws2tcpip.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define APP_NAME L"ABM AV Player"
#define APP_VERSION L"1.0.3"
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
#define TIMER_ID 1
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
static wchar_t g_playlist[MAX_PLAYLIST][PATH_CAP];
static int g_playlistCount, g_current = -1, g_selected = -1;
static int g_volume = 70, g_dragSeek = 0, g_dragSeekX = 0, g_dragVolume = 0, g_hover = 0;
static BOOL g_shuffle = FALSE, g_repeat = FALSE;
static float g_playbackRate = 1.0f;
static BOOL g_endHandled = FALSE;
#define MAX_RECENT 12
static wchar_t g_recent[MAX_RECENT][PATH_CAP];
static int g_recentCount=0;
static wchar_t g_dataDir[PATH_CAP];
static wchar_t g_torrentCacheBaseDir[PATH_CAP];
static wchar_t g_torrentDownloadDir[PATH_CAP];
static wchar_t g_torrentActiveCacheBaseDir[PATH_CAP];
static wchar_t g_torrentActiveDownloadDir[PATH_CAP];
static uint64_t g_torrentCacheQuotaGiB=20;
static int g_torrentDownloadLimitKiB=0,g_torrentUploadLimitKiB=0;

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
static int g_hoverChromeControl=0;
static BOOL g_dragPlaylistScroll = FALSE;
static int g_playlistScrollGrabY = 0, g_playlistScrollStart = 0;
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
static RECT R_VIDEO, R_SIDEBAR, R_LIST, R_SEEK, R_PLAY, R_PREV, R_NEXT, R_STOP, R_OPEN, R_SCAN, R_VOLUME, R_RECEIVER, R_RECEIVER_LIST;
static RECT R_ROW[MAX_PLAYLIST];
static RECT R_SHUFFLE, R_REPEAT, R_SPEED, R_RECENT, R_CLEAR_PLAYLIST;
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
static RECT R_MENU_BUTTON, R_DRAWER_BUTTON, R_SETTINGS_BUTTON, R_MENU_PANEL, R_MENU_ITEMS[18], R_FULLSCREEN;
static HWND g_eqWindow=NULL;
static float g_eqBandGain[10]={0,0,0,0,0,0,0,0,0,0};
static float g_eqPreamp=0.0f;
static int g_eqDragBand=-1;

enum { HIT_NONE, HIT_OPEN, HIT_PLAY, HIT_STOP, HIT_PREV, HIT_NEXT, HIT_SEEK, HIT_VOLUME, HIT_SCAN, HIT_ROW_BASE = 1000 };

static COLORREF C_BG = RGB(6,9,16);
static COLORREF C_PANEL = RGB(12,18,29);
static COLORREF C_PANEL2 = RGB(20,29,43);
static COLORREF C_TEXT = RGB(245,248,255);
static COLORREF C_MUTED = RGB(145,160,181);
static COLORREF C_ACCENT = RGB(82,205,224);
static COLORREF C_LINE = RGB(39,51,70);

static void PlayIndex(int index);
static void TorrentStopPlayback(void);
static void SaveAudioPreferences(void);
static void ApplyEqProfile(void);
static void ShowEqualizer(void);
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
            hr=converter->lpVtbl->Initialize(converter,(IWICBitmapFrameDecode *)frame,
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
    if(IsWindow(request->target))
        PostMessageW(request->target,WM_ARTWORK_DOWNLOAD_READY,
                     (WPARAM)request->generation,(LPARAM)downloaded);
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
    int left=28,top=104,bottom=150,gap=22;
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
    int collectionTop=g_drawerSettingsOpen?286:82;
    R_SETTINGS_BUTTON=(RECT){R_SIDEBAR.left+16,R_SIDEBAR.top+16,R_SIDEBAR.right-16,R_SIDEBAR.top+58};
    R_SCAN=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+84,R_SIDEBAR.right-14,R_SIDEBAR.top+118};
    R_RECEIVER=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+126,R_SIDEBAR.right-14,R_SIDEBAR.top+162};
    R_SHUFFLE=(RECT){R_SIDEBAR.left+16,R_SIDEBAR.top+204,R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2-5,R_SIDEBAR.top+240};
    R_REPEAT=(RECT){R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2+5,R_SIDEBAR.top+204,R_SIDEBAR.right-16,R_SIDEBAR.top+240};
    R_SPEED=(RECT){R_SIDEBAR.left+16,R_SIDEBAR.top+246,R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2-5,R_SIDEBAR.top+282};
    R_RECENT=(RECT){R_SIDEBAR.left+(R_SIDEBAR.right-R_SIDEBAR.left)/2+5,R_SIDEBAR.top+246,R_SIDEBAR.right-16,R_SIDEBAR.top+282};
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
    R_VOLUME=(RECT){R_SIDEBAR.left+100,R_SIDEBAR.bottom-43,R_SIDEBAR.right-30,R_SIDEBAR.bottom-35};
    R_MENU_BUTTON=(RECT){w-244,16,w-156,72};
    R_DRAWER_BUTTON=(RECT){w-144,16,w-20,72};
    /* Wider command surface gives labels and shortcut hints room to breathe. */
    R_MENU_PANEL=(RECT){w-430,74,w-20,74+690};
    /* Keep the complete command menu inside the client area at supported sizes. */
    if(R_MENU_PANEL.right>w-12)R_MENU_PANEL.right=w-12;
    if(R_MENU_PANEL.left<12)R_MENU_PANEL.left=12;
    if(R_MENU_PANEL.bottom>h-12)R_MENU_PANEL.bottom=h-12;
    /* Streaming Centre and Torrent Stream remain featured two-line destinations.
       The weighted grid preserves all 18 actions at shorter window heights. */
    int menuUsable=R_MENU_PANEL.bottom-R_MENU_PANEL.top-60;
    int menuUnit=menuUsable/20;
    if(menuUnit<1)menuUnit=1;
    int menuY=R_MENU_PANEL.top+54;
    for(int i=0;i<18;i++){
        int weight=(i==11||i==12)?2:1;
        int rowH=menuUnit*weight;
        R_MENU_ITEMS[i]=(RECT){R_MENU_PANEL.left+12,menuY,R_MENU_PANEL.right-12,menuY+rowH-4};
        menuY+=rowH;
    }
    R_FULLSCREEN=(RECT){R_VIDEO.right-50,R_VIDEO.top+16,R_VIDEO.right-14,R_VIDEO.top+52};
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
    for(int i=0;i<MAX_PLAYLIST;i++) R_ROW[i]=(RECT){R_LIST.left,R_LIST.top+(i-g_playlistScroll)*80,R_LIST.right,R_LIST.top+(i-g_playlistScroll+1)*80};
}
static void DrawIconButton(HDC dc,RECT r,int kind,BOOL hot,BOOL active) {
    (void)dc;
    int cx=(r.left+r.right)/2,cy=(r.top+r.bottom)/2;
    int diameter=(r.right-r.left<r.bottom-r.top)?r.right-r.left:r.bottom-r.top;
    int radius=diameter/2;
    COLORREF glyph=RGB(224,231,240);
    if(active) {
        int pulse=(int)(3.0+3.0*(0.5+0.5*sin((double)g_visualPulse*0.08)));
        RECT halo=r; InflateRect(&halo,3,3);
        RoundBox(NULL,halo,Blend(RGB(12,48,57),RGB(24,91,99),pulse*2),radius+4);
        GradientRound(NULL,r,RGB(25,143,145),RGB(31,111,116),radius);
        ABM_D2D_StrokeRound(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,(float)radius,RGB(105,202,195),1.15f);
        glyph=RGB(255,255,255);
    } else {
        COLORREF base=Blend(RGB(22,32,43),RGB(38,61,68),hot?g_hoverLevel:0);
        RoundBox(NULL,r,base,radius);
        if(hot) {
            RECT sheen={r.left+3,r.top+2,r.right-3,r.top+5};
            GradientRect(NULL,sheen,RGB(47,157,157),RGB(80,177,164));
        }
        ABM_D2D_StrokeRound(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,(float)radius,hot?RGB(91,166,157):RGB(55,76,82),1.0f);
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
    Fill(dc,area,RGB(8,15,20));
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
        COLORREF color=Blend(RGB(35,116,122),RGB(100,202,166),(i*100)/23);
        ABM_D2D_Round(&g_ui,(float)bar.left,(float)bar.top,(float)bar.right,(float)bar.bottom,(float)(barWidth/2),color);
    }
    RECT label={area.left+20,area.top+14,area.right-20,area.top+38};
    Txt(dc,L"NOW PLAYING  •  LIVE AUDIO SPECTRUM",label,RGB(151,184,180),g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RECT baseline={inset.left,base+8,inset.right,base+9};
    Fill(dc,baseline,RGB(43,67,68));
}
static void DrawUI(HDC dc,RECT client) {
    if(g_fullscreen) {
        /* Immersive now-playing view: artwork, track metadata and transport
           controls are composed as one screen rather than a centered cover. */
        GradientRect(dc,client,RGB(7,15,21),RGB(12,20,25));
        RECT ambientTop={0,0,client.right,client.bottom/2};
        GradientRect(dc,ambientTop,RGB(10,35,39),RGB(8,16,23));
        if(g_artworkLoaded) {
            int artSize=client.bottom-270;
            if(artSize>500)artSize=500;
            if(artSize>client.right/2-90)artSize=client.right/2-90;
            if(artSize<250)artSize=250;
            int artX=(client.right/2-artSize)/2;
            int artY=(client.bottom-170-artSize)/2+12;
            RECT tile={artX,artY,artX+artSize,artY+artSize};
            RECT tileShadow=tile;OffsetRect(&tileShadow,0,14);RoundBox(dc,tileShadow,RGB(3,6,13),22);
            GradientRound(dc,tile,RGB(18,55,59),RGB(34,48,59),22);
            ABM_D2D_StrokeRound(&g_ui,(float)tile.left,(float)tile.top,(float)tile.right,(float)tile.bottom,22.0f,RGB(83,151,143),1.25f);
            ABM_D2D_DrawImageCover(&g_ui,(float)tile.left+7,(float)tile.top+7,
                (float)tile.right-7,(float)tile.bottom-7,(float)g_artworkFade/100.0f);

            int infoX=client.right/2+34;
            int infoW=client.right-infoX-62;
            RECT kicker={infoX,artY+4,infoX+infoW,artY+34};
            RECT title={infoX,artY+50,infoX+infoW,artY+105};
            RECT artist={infoX,artY+112,infoX+infoW,artY+145};
            Txt(dc,L"ABM  /  NOW PLAYING",kicker,RGB(104,194,177),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            Txt(dc,g_trackTitle[0]?g_trackTitle:L"Untitled track",title,C_TEXT,g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            Txt(dc,g_trackArtist[0]?g_trackArtist:L"Unknown artist",artist,RGB(194,198,204),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

            int cardY=artY+184;
            RECT artistCard={infoX,cardY,infoX+infoW,cardY+112};
            Panel(dc,artistCard,RGB(13,29,46),RGB(48,91,125),16);
            RECT artistHead={infoX+18,cardY+12,infoX+infoW-18,cardY+37};
            RECT artistName={infoX+18,cardY+45,infoX+infoW-18,cardY+72};
            RECT artistRole={infoX+18,cardY+75,infoX+infoW-18,cardY+98};
            Txt(dc,L"ARTIST",artistHead,RGB(102,192,209),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            Txt(dc,g_trackArtist[0]?g_trackArtist:L"Artist information unavailable",artistName,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            Txt(dc,L"Artist",artistRole,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

            RECT credits={infoX,cardY+128,infoX+infoW,cardY+242};
            Panel(dc,credits,RGB(13,29,46),RGB(48,91,125),16);
            RECT creditsHead={infoX+18,credits.top+12,infoX+infoW-18,credits.top+37};
            Txt(dc,L"TRACK INFORMATION",creditsHead,RGB(102,192,209),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
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
    GradientRect(dc,header,RGB(13,27,45),RGB(5,10,19));
    RECT headerShade={0,82,client.right,96};
    GradientRect(dc,headerShade,RGB(8,17,29),RGB(5,10,18));
    Fill(dc,(RECT){0,95,client.right,96},RGB(39,61,83));
    /* A restrained moving light sweep gives the top bar a little life without
       competing with the video or increasing the media-rendering workload. */
    Fill(dc,(RECT){0,92,client.right,95},RGB(13,24,39));
    int sweepWidth=112;
    int sweepX=(int)((g_visualPulse*3u)%(unsigned int)(client.right+sweepWidth))-sweepWidth;
    RECT sweep={sweepX,92,sweepX+sweepWidth,95};
    GradientRect(dc,sweep,RGB(45,169,166),RGB(105,190,164));
    /* Signature mark: a luminous A-monogram fused with a play glyph. */
    RECT logoHalo={20,7,102,87};
    Panel(dc,logoHalo,RGB(13,28,45),RGB(63,151,180),22);
    RECT logo={28,14,94,80};
    GradientRound(dc,logo,RGB(28,157,157),RGB(52,119,126),17);
    RECT logoInset={33,19,89,75};
    ABM_D2D_StrokeRound(&g_ui,(float)logoInset.left,(float)logoInset.top,
        (float)logoInset.right,(float)logoInset.bottom,14.0f,RGB(190,229,255),0.8f);
    /* Geometric A: crisp strokes remain legible at small window sizes. */
    DrawLine(dc,42,63,59,30,RGB(255,255,255),3);
    DrawLine(dc,59,30,76,63,RGB(255,255,255),3);
    DrawLine(dc,49,49,68,49,RGB(226,245,255),2);
    /* Play triangle, offset into the A to make the mark distinctive. */
    DrawLine(dc,62,40,75,48,RGB(255,255,255),2);
    DrawLine(dc,75,48,62,56,RGB(255,255,255),2);
    DrawLine(dc,62,56,62,40,RGB(255,255,255),2);

    RECT brandMain={116,8,420,49};
    Txt(dc,L"ABM",brandMain,RGB(247,250,255),g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT brandSuffix={188,20,390,48};
    Txt(dc,L"AV  PLAYER",brandSuffix,RGB(132,205,194),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT brandRule={116,52,354,54};
    GradientRect(dc,brandRule,RGB(54,177,168),RGB(112,190,157));
    RECT sub={116,60,510,83};
    Txt(dc,L"YOUR MUSIC  ·  YOUR MOVIES  ·  YOUR SPACE",sub,RGB(157,177,201),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT menuBtn=R_MENU_BUTTON;
    POINT chromeCursor={0,0}; BOOL menuHot=GetCursorPos(&chromeCursor)&&ScreenToClient(g_main,&chromeCursor)&&Inside(menuBtn,chromeCursor.x,chromeCursor.y);
    BOOL drawerHot=GetCursorPos(&chromeCursor)&&ScreenToClient(g_main,&chromeCursor)&&Inside(R_DRAWER_BUTTON,chromeCursor.x,chromeCursor.y);
    Panel(dc,menuBtn,g_menuOpen?RGB(32,78,91):Blend(RGB(17,29,47),RGB(36,57,84),menuHot?100:0),
        g_menuOpen?RGB(92,165,182):(menuHot?RGB(80,132,191):RGB(47,69,98)),15);
    /* Keep the hamburger icon in its own compact column, clear of the label. */
    for(int i=0;i<3;i++)DrawLine(dc,menuBtn.left+12,menuBtn.top+13+i*7,menuBtn.left+29,menuBtn.top+13+i*7,RGB(226,236,249),2);
    RECT menuLabel={menuBtn.left+39,menuBtn.top+3,menuBtn.right-7,menuBtn.bottom-3};
    Txt(dc,L"MENU",menuLabel,RGB(224,235,249),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT drawerBtn=R_DRAWER_BUTTON;
    Panel(dc,drawerBtn,g_drawerOpen?RGB(32,78,132):Blend(RGB(17,29,47),RGB(36,57,84),drawerHot?100:0),g_drawerOpen?RGB(92,165,242):(drawerHot?RGB(80,132,191):RGB(47,69,98)),15);
    DrawLine(dc,drawerBtn.left+13,drawerBtn.top+15,drawerBtn.left+29,drawerBtn.top+15,RGB(220,235,252),2);
    DrawLine(dc,drawerBtn.left+13,drawerBtn.top+22,drawerBtn.left+29,drawerBtn.top+22,RGB(220,235,252),2);
    DrawLine(dc,drawerBtn.left+13,drawerBtn.top+29,drawerBtn.left+29,drawerBtn.top+29,RGB(220,235,252),2);
    RECT drawerLabel={drawerBtn.left+36,drawerBtn.top+3,drawerBtn.right-8,drawerBtn.bottom-3};
    Txt(dc,L"MEDIA",drawerLabel,RGB(224,235,249),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT status={client.right-540,26,client.right-300,64};
    Panel(dc,status,RGB(12,24,39),RGB(53,116,143),18);
    RECT led={status.left+13,status.top+14,status.left+21,status.top+22};RoundBox(dc,led,g_hasMedia?RGB(63,205,145):RGB(100,117,139),5);
    RECT statusText={status.left+29,status.top+2,status.right-8,status.bottom-2};
    Txt(dc,g_statusText[0]?g_statusText:(g_hasMedia?L"READY TO PLAY":L"LOCAL PLAYER"),statusText,C_TEXT,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

    /* Video surface is a separate LibVLC child window. Keep the surrounding frame elegant. */
    RECT frame=R_VIDEO;InflateRect(&frame,3,3);
    int glow=(int)(14.0+5.0*(0.5+0.5*sin((double)g_visualPulse*0.045)));
    COLORREF edge=Blend(RGB(30,48,53),RGB(55,127,117),glow);
    Panel(dc,frame,RGB(6,10,17),edge,20);
    GradientRect(dc,R_VIDEO,RGB(7,15,26),RGB(4,8,15));
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
        Txt(dc,L"NOW PLAYING  /  ABM AUDIO",kicker,RGB(105,190,177),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
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
            COLORREF col=Blend(RGB(43,151,145),RGB(108,184,151),i*100/(bars-1));
            RoundBox(dc,bar,col,3);
        }
    } else if(g_visualizerActive) DrawVisualizer(dc,R_VIDEO);
    if(!g_hasMedia) {
        int areaW=R_VIDEO.right-R_VIDEO.left, areaH=R_VIDEO.bottom-R_VIDEO.top;
        int cx=(R_VIDEO.left+R_VIDEO.right)/2;
        int cy=(R_VIDEO.top+R_VIDEO.bottom)/2;
        /* Ambient colour fields make the stage feel like a listening space,
           not an empty black video rectangle. */
        /* Keep the copy in a dedicated left column. The teal hero panel
           begins farther right so the text never crosses its rounded edge. */
        RECT ambient={R_VIDEO.left+areaW*36/100,R_VIDEO.top+areaH/6,R_VIDEO.right-areaW/8,R_VIDEO.bottom-areaH/7};
        GradientRound(dc,ambient,RGB(10,35,39),RGB(17,27,34),34);
        RECT glowLine={ambient.left+26,ambient.top+1,ambient.right-26,ambient.top+3};
        GradientRect(dc,glowLine,RGB(47,171,158),RGB(112,187,150));
        int leftX=R_VIDEO.left+areaW*7/100;
        int textW=areaW*27/100;
        int topY=cy-112;
        RECT eyebrowPill={leftX,topY+5,leftX+8,topY+13};
        RoundBox(dc,eyebrowPill,RGB(75,211,190),4);
        Txt(dc,L"THE ABM LISTENING EXPERIENCE",(RECT){leftX+18,topY,leftX+textW,topY+25},RGB(120,190,206),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT headline1={leftX,topY+42,leftX+textW,topY+91};
        RECT headline2={leftX,topY+88,leftX+textW,topY+137};
        Txt(dc,L"Capture the feeling.",headline1,RGB(247,249,255),g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        Txt(dc,L"Relive every moment.",headline2,RGB(126,199,181),g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT detail={leftX,topY+151,leftX+textW,topY+194};
        Txt(dc,L"A refined home for music, movies and",detail,RGB(176,192,213),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT detail2={leftX,topY+177,leftX+textW,topY+218};
        Txt(dc,L"everything you love to watch and hear.",detail2,RGB(176,192,213),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT hint={leftX,topY+237,leftX+textW,topY+263};
        Txt(dc,L"ADD MEDIA TO BEGIN  →",hint,RGB(112,195,179),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        /* Abstract, layered album sleeves create a distinctive visual anchor.
           The cards are decorative only; playback remains on the real controls. */
        int cardW=areaW>900?190:150, cardH=cardW;
        int stackX=R_VIDEO.right-areaW/5-cardW;
        int stackY=cy-cardH/2-8;
        int drift=(int)(4.0*sin((double)g_visualPulse*0.045));
        RECT backCard={stackX+52,stackY-25+drift,stackX+52+cardW,stackY-25+drift+cardH};
        GradientRound(dc,backCard,RGB(34,62,75),RGB(25,104,96),22);
        RECT backInset={backCard.left+12,backCard.top+12,backCard.right-12,backCard.bottom-12};
        ABM_D2D_StrokeRound(&g_ui,(float)backInset.left,(float)backInset.top,(float)backInset.right,(float)backInset.bottom,16.0f,RGB(130,183,164),1.0f);
        RECT frontCard={stackX,stackY,stackX+cardW,stackY+cardH};
        RECT cardShadow=frontCard;OffsetRect(&cardShadow,0,10);
        RoundBox(dc,cardShadow,RGB(3,7,16),23);
        GradientRound(dc,frontCard,RGB(24,132,126),RGB(55,91,100),22);
        RECT inner={frontCard.left+12,frontCard.top+12,frontCard.right-12,frontCard.bottom-12};
        ABM_D2D_StrokeRound(&g_ui,(float)inner.left,(float)inner.top,(float)inner.right,(float)inner.bottom,16.0f,RGB(171,218,199),0.8f);
        int icx=(frontCard.left+frontCard.right)/2, icy=(frontCard.top+frontCard.bottom)/2;
        int ring=(int)(42.0+3.0*sin((double)g_visualPulse*0.06));
        RECT vinyl={icx-ring,icy-ring,icx+ring,icy+ring};
        ABM_D2D_StrokeRound(&g_ui,(float)vinyl.left,(float)vinyl.top,(float)vinyl.right,(float)vinyl.bottom,(float)ring,RGB(211,235,224),1.6f);
        RECT vinyl2={icx-23,icy-23,icx+23,icy+23};
        ABM_D2D_StrokeRound(&g_ui,(float)vinyl2.left,(float)vinyl2.top,(float)vinyl2.right,(float)vinyl2.bottom,23.0f,RGB(184,218,204),1.0f);
        RECT vinylDot={icx-5,icy-5,icx+5,icy+5};
        RoundBox(dc,vinylDot,RGB(240,247,255),5);
        int bars=19, gap=5, barW=4;
        int total=bars*barW+(bars-1)*gap;
        int base=frontCard.bottom+30;
        for(int i=0;i<bars;i++) {
            double wave=0.18+0.82*(0.5+0.5*sin((double)g_visualPulse*0.11+i*0.58));
            int bh=5+(int)(wave*24.0);
            RECT bar={icx-total/2+i*(barW+gap),base-bh,icx-total/2+i*(barW+gap)+barW,base};
            RoundBox(dc,bar,Blend(RGB(44,164,151),RGB(117,195,151),i*100/(bars-1)),2);
        }
        RECT caption={stackX-30,base+9,stackX+cardW+85,base+34};
        Txt(dc,L"MUSIC  •  CINEMA  •  YOU",caption,RGB(132,171,165),g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
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

    {
    int collectionTop=g_drawerSettingsOpen?286:82;
    RECT receiver=R_RECEIVER;
    int sel=(int)SendMessageW(g_renderers,CB_GETCURSEL,0,0);
    /* Timeline: a dedicated, readable seek row above the transport dock.
       The seek rectangle and its hit-testing remain unchanged. */
    RECT timeline={R_SEEK.left,R_SEEK.top-34,R_SEEK.right,R_SEEK.bottom+16};
    Fill(dc,timeline,C_BG);
    int pos=0;int64_t len=g_player?libvlc_media_player_get_length(g_player):0;
    int64_t now=g_player?libvlc_media_player_get_time(g_player):0;
    if(len>0){pos=(int)((now*1000)/len);if(pos<0)pos=0;if(pos>1000)pos=1000;}
    wchar_t t1[24],t2[24];
    FormatTime((int)now,t1,24);FormatTime((int)len,t2,24);
    RECT elapsedLabel={R_SEEK.left,R_SEEK.top-31,R_SEEK.left+92,R_SEEK.top-9};
    RECT totalLabel={R_SEEK.right-92,R_SEEK.top-31,R_SEEK.right,R_SEEK.top-9};
    Txt(dc,t1,elapsedLabel,RGB(205,220,230),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    Txt(dc,t2,totalLabel,RGB(135,157,177),g_smallFont,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    RECT track={R_SEEK.left,R_SEEK.top-2,R_SEEK.right,R_SEEK.bottom+2};
    RoundBox(dc,track,RGB(31,48,61),5);
    RECT prog=track;
    prog.right=prog.left+(prog.right-prog.left)*pos/1000;
    if(prog.right>prog.left)GradientRound(dc,prog,RGB(42,169,158),RGB(112,199,161),5);
    int thumb=R_SEEK.left+(R_SEEK.right-R_SEEK.left)*pos/1000;
    RECT halo={thumb-11,R_SEEK.top-11,thumb+11,R_SEEK.bottom+11};
    RoundBox(dc,halo,RGB(21,57,67),11);
    RECT dot={thumb-5,R_SEEK.top-5,thumb+5,R_SEEK.bottom+5};
    RoundBox(dc,dot,RGB(242,249,255),8);
    ABM_D2D_StrokeRound(&g_ui,(float)dot.left,(float)dot.top,(float)dot.right,(float)dot.bottom,8.0f,RGB(93,194,181),1.0f);

    /* Control dock */
    int dockLeft=R_VIDEO.left+((R_VIDEO.right-R_VIDEO.left)-720)/2;
    RECT dock={dockLeft,client.bottom-144,dockLeft+720,client.bottom-42};
    RECT dockShadow=dock; OffsetRect(&dockShadow,0,8);
    RoundBox(dc,dockShadow,RGB(3,7,13),32);
    GradientRound(dc,dock,RGB(17,34,43),RGB(15,27,39),28);
    /* Fine top-edge light gives the transport surface a clear visual boundary. */
    RECT dockTop={dock.left+30,dock.top+1,dock.right-30,dock.top+3};
    GradientRect(dc,dockTop,RGB(48,157,151),RGB(93,176,151));
    ABM_D2D_StrokeRound(&g_ui,(float)dock.left,(float)dock.top,(float)dock.right,(float)dock.bottom,28.0f,RGB(59,105,109),1.0f);
    RECT dockRim=dock; InflateRect(&dockRim,-2,-2);
    ABM_D2D_StrokeRound(&g_ui,(float)dockRim.left,(float)dockRim.top,(float)dockRim.right,(float)dockRim.bottom,26.0f,RGB(37,61,69),0.65f);
    /* Dedicated Now Playing identity block makes the transport dock a real
       player surface rather than a floating row of generic buttons. */
    RECT miniArt={dock.left+17,dock.top+17,dock.left+81,dock.bottom-17};
    GradientRound(dc,miniArt,RGB(28,143,139),RGB(52,112,119),14);
    RECT miniInner={miniArt.left+6,miniArt.top+6,miniArt.right-6,miniArt.bottom-6};
    ABM_D2D_StrokeRound(&g_ui,(float)miniInner.left,(float)miniInner.top,(float)miniInner.right,(float)miniInner.bottom,9.0f,RGB(165,218,207),0.85f);
    if(g_artworkLoaded)ABM_D2D_DrawImage(&g_ui,(float)miniArt.left+3,(float)miniArt.top+3,(float)miniArt.right-3,(float)miniArt.bottom-3);
    else {
        int mx=(miniArt.left+miniArt.right)/2,my=(miniArt.top+miniArt.bottom)/2;
        DrawLine(dc,mx-12,my+13,mx,my-14,RGB(244,250,255),2);
        DrawLine(dc,mx,my-14,mx+13,my+13,RGB(244,250,255),2);
        DrawLine(dc,mx-7,my+3,mx+7,my+3,RGB(244,250,255),2);
    }
    RECT nowLabel={miniArt.right+14,dock.top+12,dock.left+346,dock.top+31};
    Txt(dc,g_hasMedia?L"NOW PLAYING  ·  ON THIS DEVICE":L"ABM  ·  YOUR MEDIA SPACE",nowLabel,RGB(111,194,180),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    RECT nowTitle={miniArt.right+14,dock.top+32,dock.left+346,dock.top+59};
    const wchar_t *dockTitle=g_hasMedia?(g_trackTitle[0]?g_trackTitle:L"Playing your selection"):L"Your entertainment, in one place";
    Txt(dc,dockTitle,nowTitle,RGB(246,249,255),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    RECT nowMeta={miniArt.right+14,dock.top+62,dock.left+346,dock.bottom-9};
    Txt(dc,g_hasMedia?(g_trackArtist[0]?g_trackArtist:L"LOCAL MEDIA"):L"Build a queue or explore your library",nowMeta,RGB(151,174,199),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    RECT transportDivider={dock.left+360,dock.top+17,dock.left+361,dock.bottom-17};
    Fill(dc,transportDivider,RGB(45,68,71));
    DrawIconButton(dc,R_PREV,5,g_hoverButton==HIT_PREV,FALSE);
    DrawIconButton(dc,R_PLAY,libvlc_media_player_get_state(g_player)==libvlc_Playing?2:1,g_hoverButton==HIT_PLAY,TRUE);
    DrawIconButton(dc,R_NEXT,4,g_hoverButton==HIT_NEXT,FALSE);
    DrawIconButton(dc,R_STOP,3,g_hoverButton==HIT_STOP,FALSE);
    RECT open=R_OPEN;
    GradientRound(dc,open,(g_hoverButton==HIT_OPEN)?RGB(42,157,148):RGB(22,48,52),(g_hoverButton==HIT_OPEN)?RGB(76,174,151):RGB(39,75,72),14);
    ABM_D2D_StrokeRound(&g_ui,(float)open.left,(float)open.top,(float)open.right,(float)open.bottom,14.0f,(g_hoverButton==HIT_OPEN)?RGB(126,205,185):RGB(66,112,105),1.0f);
    RECT plus={open.left+14,open.top+4,open.left+37,open.bottom-4};
    Txt(dc,L"+",plus,RGB(230,242,255),g_headingFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RECT openText={open.left+39,open.top+2,open.right-8,open.bottom-2};
    Txt(dc,L"Add media",openText,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);


    /* Playback controls are permanent. Only the drawer itself follows its slide animation. */
    if(g_drawerProgress>0) {
    /* Slide-out media drawer: output, quick controls, search and queue. */
    /* Premium media-library surface with layered navy and an inset rim. */
    RECT drawerShadow=R_SIDEBAR;OffsetRect(&drawerShadow,0,8);
    RoundBox(dc,drawerShadow,RGB(4,8,14),26);
    /* Slightly lighter layered glass treatment while media is playing. */
    if(g_hasMedia && libvlc_media_player_get_state(g_player)==libvlc_Playing)
        GradientRound(dc,R_SIDEBAR,RGB(20,29,42),RGB(16,25,38),28);
    else
        GradientRound(dc,R_SIDEBAR,RGB(19,28,41),RGB(15,23,35),28);
    ABM_D2D_StrokeRound(&g_ui,(float)R_SIDEBAR.left,(float)R_SIDEBAR.top,(float)R_SIDEBAR.right,(float)R_SIDEBAR.bottom,28.0f,RGB(67,88,111),1.0f);
    RECT drawerInset={R_SIDEBAR.left+2,R_SIDEBAR.top+2,R_SIDEBAR.right-2,R_SIDEBAR.bottom-2};
    ABM_D2D_StrokeRound(&g_ui,(float)drawerInset.left,(float)drawerInset.top,(float)drawerInset.right,(float)drawerInset.bottom,26.0f,RGB(43,63,78),0.55f);
    int accentX=R_SIDEBAR.left+18+(int)((g_visualPulse*2u)%(unsigned int)((R_SIDEBAR.right-R_SIDEBAR.left)-72));
    RECT accent={accentX,R_SIDEBAR.top+1,accentX+54,R_SIDEBAR.top+3};
    GradientRect(dc,accent,RGB(48,169,157),RGB(104,187,151));
    if(g_drawerSettingsOpen) {
    RECT sideTitle={R_SIDEBAR.left+18,R_SIDEBAR.top+64,R_SIDEBAR.right-18,R_SIDEBAR.top+82};
    Txt(dc,L"PLAYBACK OUTPUT",sideTitle,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT scan=R_SCAN;
    GradientRound(dc,scan,(g_hoverButton==HIT_SCAN)?RGB(37,123,112):RGB(20,49,51),(g_hoverButton==HIT_SCAN)?RGB(67,151,126):RGB(31,72,68),12);
    ABM_D2D_StrokeRound(&g_ui,(float)scan.left,(float)scan.top,(float)scan.right,(float)scan.bottom,12.0f,RGB(66,132,116),0.95f);
    RECT scanIcon={scan.left+14,scan.top+8,scan.left+34,scan.bottom-8};
    DrawLine(dc,scanIcon.left+2,scanIcon.top+8,scanIcon.left+2,scanIcon.bottom-7,RGB(220,236,255),2);
    DrawLine(dc,scanIcon.left+2,scanIcon.top+8,scanIcon.right-2,scanIcon.top+8,RGB(220,236,255),2);
    DrawLine(dc,scanIcon.right-2,scanIcon.top+8,scanIcon.right-2,scanIcon.bottom-7,RGB(220,236,255),2);
    RECT scanText={scan.left+42,scan.top+2,scan.right-10,scan.bottom-2};
    Txt(dc,L"Discover devices",scanText,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    /* Fully custom Direct2D output selector. */
    COLORREF receiverFill=g_receiverPopup?RGB(42,45,50):RGB(30,32,36); (void)receiverFill;
    Panel(dc,receiver,g_receiverPopup?RGB(27,73,68):RGB(19,43,43),g_receiverPopup?RGB(82,166,137):RGB(57,104,91),12);
    RECT deviceIcon={receiver.left+12,receiver.top+7,receiver.left+34,receiver.bottom-7};
    GradientRound(dc,deviceIcon,RGB(29,130,119),RGB(48,101,103),6);
    DrawLine(dc,deviceIcon.left+4,deviceIcon.top+5,deviceIcon.right-4,deviceIcon.top+5,RGB(197,205,214),2);
    DrawLine(dc,deviceIcon.left+4,deviceIcon.bottom-5,deviceIcon.right-4,deviceIcon.bottom-5,RGB(197,205,214),2);
    wchar_t deviceName[256]=L"Local playback";
    if(sel>0)SendMessageW(g_renderers,CB_GETLBTEXT,sel,(LPARAM)deviceName);
    RECT deviceText={receiver.left+44,receiver.top+2,receiver.right-48,receiver.bottom-2};
    Txt(dc,deviceName,deviceText,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    int arrowX=receiver.right-20,arrowY=(receiver.top+receiver.bottom)/2;
    DrawLine(dc,arrowX-4,arrowY-2,arrowX,arrowY+2,RGB(190,196,204),1.8f);
    DrawLine(dc,arrowX,arrowY+2,arrowX+4,arrowY-2,RGB(190,196,204),1.8f);

    RECT toolsHead={R_SIDEBAR.left+18,R_SIDEBAR.top+178,R_SIDEBAR.right-18,R_SIDEBAR.top+198};
    Txt(dc,L"QUICK ACTIONS",toolsHead,RGB(119,177,164),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT actions[4]={R_SHUFFLE,R_REPEAT,R_SPEED,R_RECENT};
    const wchar_t *actionLabels[4]={g_shuffle?L"Shuffle on":L"Shuffle",g_repeat?L"Repeat on":L"Repeat",L"Speed",L"Recent"};
    const wchar_t *actionIcons[4]={L"S",L"R",L"1x",L"..."};
    BOOL actionActive[4]={g_shuffle,g_repeat,g_playbackRate!=1.0f,FALSE};
    for(int ai=0;ai<4;ai++) {
        RECT ar=actions[ai];
        POINT cursor; BOOL actionHot=FALSE;
        if(GetCursorPos(&cursor)){ScreenToClient(g_main,&cursor);actionHot=PtInRect(&ar,cursor);}
        COLORREF fill=actionActive[ai]?RGB(27,55,65):(actionHot?RGB(30,48,59):RGB(23,32,44));
        COLORREF edge=actionActive[ai]?RGB(65,135,143):(actionHot?RGB(70,111,125):RGB(52,69,86));
        if(actionActive[ai])GradientRound(dc,ar,RGB(27,105,91),RGB(30,82,78),10);
        else RoundBox(dc,ar,fill,10);
        ABM_D2D_StrokeRound(&g_ui,(float)ar.left,(float)ar.top,(float)ar.right,(float)ar.bottom,10.0f,edge,0.9f);
        RECT glyph={ar.left+8,ar.top+3,ar.left+31,ar.bottom-3};
        Txt(dc,actionIcons[ai],glyph,actionActive[ai]?RGB(143,210,184):RGB(157,184,180),g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT label={ar.left+34,ar.top+2,ar.right-5,ar.bottom-2};
        wchar_t speedLabel[32];
        const wchar_t *shown=actionLabels[ai];
        if(ai==2){swprintf_s(speedLabel,32,L"Speed  %.2gx",g_playbackRate);shown=speedLabel;}
        Txt(dc,shown,label,actionActive[ai]?RGB(235,246,239):RGB(210,225,219),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }

    }
    Panel(dc,R_SETTINGS_BUTTON,g_drawerSettingsOpen?RGB(27,67,62):RGB(25,38,45),g_drawerSettingsOpen?RGB(76,151,127):RGB(58,83,78),11);
    RECT settingsLabel={R_SETTINGS_BUTTON.left+14,R_SETTINGS_BUTTON.top+2,R_SETTINGS_BUTTON.right-38,R_SETTINGS_BUTTON.bottom-2};
    Txt(dc,g_drawerSettingsOpen?L"Playback settings  ·  expanded":L"Playback settings  ·  tap to expand",settingsLabel,RGB(231,240,252),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    int chevronX=R_SETTINGS_BUTTON.right-22,chevronY=(R_SETTINGS_BUTTON.top+R_SETTINGS_BUTTON.bottom)/2;
    DrawLine(dc,chevronX-4,chevronY-2,chevronX,chevronY+2,RGB(190,210,234),2);
    DrawLine(dc,chevronX,chevronY+2,chevronX+4,chevronY-2,RGB(190,210,234),2);
    RECT listHead={R_SIDEBAR.left+18,R_SIDEBAR.top+collectionTop,R_SIDEBAR.right-18,R_SIDEBAR.top+collectionTop+24};
    Txt(dc,g_libraryTab?L"YOUR COLLECTION":L"YOUR QUEUE",listHead,RGB(132,187,207),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    wchar_t count[48];swprintf_s(count,48,L"%02d TRACKS",g_playlistCount);
    RECT countR={R_SIDEBAR.right-164,listHead.top,R_CLEAR_PLAYLIST.left-8,listHead.bottom};
    Txt(dc,count,countR,RGB(153,174,200),g_smallFont,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    RoundBox(dc,R_CLEAR_PLAYLIST,g_playlistCount?RGB(49,31,43):RGB(25,30,40),8);
    ABM_D2D_StrokeRound(&g_ui,(float)R_CLEAR_PLAYLIST.left,(float)R_CLEAR_PLAYLIST.top,(float)R_CLEAR_PLAYLIST.right,(float)R_CLEAR_PLAYLIST.bottom,8.0f,g_playlistCount?RGB(139,74,99):RGB(48,58,72),0.8f);
    Txt(dc,L"Clear",R_CLEAR_PLAYLIST,g_playlistCount?RGB(255,190,205):RGB(115,130,151),g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    DrawLine(dc,R_SIDEBAR.left+16,listHead.bottom+4,R_SIDEBAR.right-16,listHead.bottom+4,RGB(43,53,69),1);

    /* Search and filters are drawn in Direct2D; keyboard input is handled by
       the main window so no blurry native EDIT control is layered over it. */
    Panel(dc,R_SEARCH,g_searchFocused?RGB(13,47,58):RGB(11,23,39),g_searchFocused?RGB(67,183,194):RGB(42,75,103),12);
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
        if(active)GradientRound(dc,tabs[ti],RGB(30,111,119),RGB(34,87,101),11);
        else RoundBox(dc,tabs[ti],RGB(13,25,42),11);
        ABM_D2D_StrokeRound(&g_ui,(float)tabs[ti].left,(float)tabs[ti].top,(float)tabs[ti].right,(float)tabs[ti].bottom,11.0f,active?RGB(104,190,224):RGB(43,72,101),0.95f);
        RECT label={tabs[ti].left+3,tabs[ti].top+1,tabs[ti].right-3,tabs[ti].bottom-1};
        Txt(dc,tabNames[ti],label,active?RGB(238,246,255):RGB(171,187,207),g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    RECT filters[3]={R_FILTER_ALL,R_FILTER_AUDIO,R_FILTER_VIDEO};
    const wchar_t *filterNames[3]={L"All",L"Audio",L"Video"};
    for(int fi=0;fi<3;fi++){
        BOOL active=g_mediaFilter==fi;
        if(active)GradientRound(dc,filters[fi],RGB(30,111,119),RGB(34,87,101),8);
        else RoundBox(dc,filters[fi],RGB(15,28,46),8);
        RECT label={filters[fi].left+2,filters[fi].top,filters[fi].right-2,filters[fi].bottom};
        Txt(dc,filterNames[fi],label,active?RGB(244,248,255):RGB(158,174,194),g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    RoundBox(dc,R_GROUP,RGB(18,34,51),8);
    wchar_t groupLabel[32];swprintf_s(groupLabel,32,L"Group: %s",g_groupMode==1?L"Artist":(g_groupMode==2?L"Album":L"Folder"));
    Txt(dc,groupLabel,R_GROUP,RGB(174,195,220),g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    int rowHeight=80;
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
        /* Every queue entry is a deliberate media card, not bare text on a panel. */
        if(current)GradientRound(dc,row,RGB(25,58,66),RGB(28,47,61),13);
        else if(selected)GradientRound(dc,row,RGB(31,45,59),RGB(27,39,53),13);
        else RoundBox(dc,row,hot?RGB(31,45,59):RGB(23,33,46),13);
        ABM_D2D_StrokeRound(&g_ui,(float)row.left,(float)row.top,(float)row.right,(float)row.bottom,13.0f,current?RGB(70,139,148):(selected?RGB(69,105,124):(hot?RGB(65,94,113):RGB(48,64,82))),current?1.25f:0.75f);
        if(current){RECT activeRail={row.left+1,row.top+12,row.left+4,row.bottom-12};GradientRect(dc,activeRail,RGB(57,178,176),RGB(77,145,160));}
        RECT thumb={row.left+10,row.top+8,row.left+72,row.bottom-8};
        GradientRect(dc,thumb,current?RGB(27,139,169):RGB(22+(i*13)%22,53+(i*9)%24,82+(i*7)%28),current?RGB(95,79,190):RGB(55+(i*11)%26,55+(i*7)%24,119+(i*5)%24));
        RoundBox(dc,thumb,current?RGB(49,103,166):RGB(32+(i*5)%14,49+(i*4)%15,72+(i*3)%18),10);
        RECT coverInset={thumb.left+5,thumb.top+5,thumb.right-5,thumb.bottom-5};
        ABM_D2D_StrokeRound(&g_ui,(float)coverInset.left,(float)coverInset.top,(float)coverInset.right,(float)coverInset.bottom,7.0f,RGB(145,184,218),0.65f);
        if(current&&g_artworkLoaded)ABM_D2D_DrawImage(&g_ui,(float)thumb.left+2,(float)thumb.top+2,(float)thumb.right-2,(float)thumb.bottom-2);
        else {
            int cx=(thumb.left+thumb.right)/2,cy=(thumb.top+thumb.bottom)/2;
            COLORREF iconColor=current?RGB(255,255,255):RGB(171,198,229);
            if(isVideo){DrawLine(dc,cx-5,cy-7,cx+7,cy,iconColor,2);DrawLine(dc,cx+7,cy,cx-5,cy+7,iconColor,2);DrawLine(dc,cx-5,cy+7,cx-5,cy-7,iconColor,2);}
            else {DrawLine(dc,cx-7,cy-6,cx-7,cy+6,RGB(182,205,232),2);DrawLine(dc,cx+7,cy-6,cx+7,cy+6,RGB(182,205,232),2);}
        }
        RECT name={row.left+82,row.top+9,row.right-84,row.top+38};
        RECT subline={row.left+82,row.top+40,row.right-84,row.bottom-7};
        Txt(dc,leaf,name,current?RGB(255,255,255):RGB(232,239,249),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        wchar_t metaLine[320];
        if(g_groupMode==1&&current&&g_trackArtist[0])swprintf_s(metaLine,320,L"%s  ·  %s",g_trackArtist,isVideo?L"VIDEO":L"AUDIO");
        else if(g_groupMode==2&&current&&g_trackAlbum[0])swprintf_s(metaLine,320,L"%s  ·  LOCAL FILE",g_trackAlbum);
        else swprintf_s(metaLine,320,L"%s  ·  LOCAL FILE",isVideo?L"VIDEO":L"AUDIO");
        Txt(dc,metaLine,subline,RGB(145,166,190),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        RECT typePill={row.right-74,row.top+24,row.right-10,row.top+49};
        RoundBox(dc,typePill,isVideo?RGB(38,54,83):RGB(19,65,75),8);
        RECT typeText={typePill.left+3,typePill.top+1,typePill.right-3,typePill.bottom-1};
        Txt(dc,isVideo?L"VIDEO":L"AUDIO",typeText,isVideo?RGB(164,193,239):RGB(116,215,211),g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(current){int cx=row.right-23,base=row.top+rowHeight/2;for(int k=0;k<3;k++){int bh=5+(int)(5.0*(0.5+0.5*sin((double)g_visualPulse*0.18+k*1.5)));RECT wave={cx+k*4,base-bh,cx+k*4+2,base+bh};RoundBox(dc,wave,RGB(94,183,244),2);}}
        shown++;
    }
    /* Premium slim scrollbar: quiet rail, luminous gradient thumb and a
       generous hit target. It appears only when the filtered collection overflows. */
    int filteredCount=CountFilteredMedia();
    int maxPlaylistScroll=filteredCount-visible;
    if(maxPlaylistScroll<0)maxPlaylistScroll=0;
    if(maxPlaylistScroll>0 && R_PLAYLIST_SCROLL.bottom>R_PLAYLIST_SCROLL.top) {
        RECT rail=R_PLAYLIST_SCROLL;
        RoundBox(dc,rail,RGB(23,39,59),3);
        int railH=rail.bottom-rail.top;
        int thumbH=(int)((long long)railH*visible/filteredCount);
        if(thumbH<42)thumbH=42;
        if(thumbH>railH)thumbH=railH;
        int thumbTravel=railH-thumbH;
        int thumbTop=rail.top+(maxPlaylistScroll?thumbTravel*g_playlistScroll/maxPlaylistScroll:0);
        R_PLAYLIST_THUMB=(RECT){rail.left-2,thumbTop,rail.right+2,thumbTop+thumbH};
        RECT thumbGlow=R_PLAYLIST_THUMB;InflateRect(&thumbGlow,2,1);
        RoundBox(dc,thumbGlow,RGB(24,61,82),5);
        GradientRect(dc,R_PLAYLIST_THUMB,RGB(40,190,211),RGB(111,105,239));
        RECT shine={R_PLAYLIST_THUMB.left+1,R_PLAYLIST_THUMB.top+4,R_PLAYLIST_THUMB.right-1,R_PLAYLIST_THUMB.bottom-4};
        ABM_D2D_StrokeRound(&g_ui,(float)shine.left,(float)shine.top,(float)shine.right,(float)shine.bottom,3.0f,RGB(170,222,255),0.45f);
    } else {
        R_PLAYLIST_THUMB=R_PLAYLIST_SCROLL;
    }
    if(shown==0){
        RECT empty={R_LIST.left+8,R_LIST.top+18,R_LIST.right-8,R_LIST.top+76};
        Txt(dc,g_searchText[0]?L"No matching media":L"Add media to start your library",empty,RGB(151,168,191),g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    if(g_receiverPopup) {
        int n=(int)SendMessageW(g_renderers,CB_GETCOUNT,0,0);if(n>MAX_RENDERERS+1)n=MAX_RENDERERS+1;
        int rows=n>6?6:n;
        R_RECEIVER_LIST=(RECT){receiver.left,receiver.bottom+7,receiver.right,receiver.bottom+7+rows*36};
        Panel(dc,R_RECEIVER_LIST,RGB(20,37,38),RGB(59,91,79),13);
        for(int i=0;i<rows;i++) {
            RECT item={R_RECEIVER_LIST.left+6,R_RECEIVER_LIST.top+4+i*36,R_RECEIVER_LIST.right-6,R_RECEIVER_LIST.top+4+(i+1)*36};
            if(i==sel)RoundBox(dc,item,RGB(34,69,59),8);
            wchar_t label[256]=L"";
            SendMessageW(g_renderers,CB_GETLBTEXT,i,(LPARAM)label);
            RECT tx={item.left+13,item.top,item.right-12,item.bottom};
            Txt(dc,label,tx,i==sel?RGB(248,249,250):RGB(202,207,214),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
    }

    if(g_drawerOpen) {
    RECT volPanel={R_SIDEBAR.left+14,R_SIDEBAR.bottom-78,R_SIDEBAR.right-14,R_SIDEBAR.bottom-12};
    Panel(dc,volPanel,RGB(23,38,40),RGB(55,87,77),16);
    RECT volLabel={volPanel.left+14,volPanel.top+3,volPanel.left+82,volPanel.bottom-3};
     RECT volTrack={R_VOLUME.left,R_VOLUME.top-3,R_VOLUME.right,R_VOLUME.bottom+3};
    Txt(dc,L"VOLUME",volLabel,RGB(161,183,211),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
    RECT vr={R_VOLUME.left,R_VOLUME.top-3,R_VOLUME.right,R_VOLUME.bottom+3};RoundBox(dc,vr,RGB(34,53,77),6);
    vr.right=vr.left+(vr.right-vr.left)*g_volume/100;GradientRect(dc,vr,RGB(43,184,220),RGB(117,105,237));
    int vx=R_VOLUME.left+(R_VOLUME.right-R_VOLUME.left)*g_volume/100;
    RECT vd={vx-6,R_VOLUME.top-6,vx+6,R_VOLUME.bottom+6};RoundBox(dc,vd,RGB(245,248,252),8);
    ABM_D2D_StrokeRound(&g_ui,(float)vd.left,(float)vd.top,(float)vd.right,(float)vd.bottom,8.0f,RGB(110,165,242),0.8f);



    }
    } /* End permanent playback surface block. */

    /* Persistent contextual toolbar strip. Its hit target descriptions are
       concise and change only when the pointer enters a real command. */
    RECT commandStrip={0,client.bottom-30,client.right,client.bottom};
    GradientRect(dc,commandStrip,RGB(9,19,31),RGB(7,13,23));
    Fill(dc,(RECT){0,client.bottom-30,client.right,client.bottom-29},RGB(42,75,91));
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
    Txt(dc,commandHint,hint,RGB(159,190,207),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

    /* Bottom-edge branding removed: playback and volume surfaces own this area. */

    /* Command Centre: restrained surfaces, consistent action hierarchy and
       clearly separated shortcut hints. Keep this overlay above the player. */
    if(g_menuProgress>0) {
        RECT menu=R_MENU_PANEL;
        menu.bottom=menu.top+((menu.bottom-menu.top)*g_menuProgress)/100;
        RECT shadow=menu;OffsetRect(&shadow,0,7);RoundBox(dc,shadow,RGB(4,8,14),20);
        RECT menuGlow=menu;InflateRect(&menuGlow,2,2);RoundBox(dc,menuGlow,RGB(7,12,20),22);
        Panel(dc,menu,RGB(12,20,32),RGB(49,76,94),18);
        RECT menuTop={menu.left+18,menu.top+2,menu.right-18,menu.top+4};
        GradientRect(dc,menuTop,RGB(54,177,168),RGB(82,205,190));
        RECT menuHeader={menu.left+18,menu.top+9,menu.right-18,menu.top+32};
        Txt(dc,L"COMMAND CENTRE",menuHeader,RGB(242,248,253),g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT menuSub={menu.left+18,menu.top+31,menu.right-18,menu.top+48};
        Txt(dc,L"PLAYBACK  ·  LIBRARY  ·  DEVICES  ·  APP",menuSub,RGB(133,163,184),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        DrawLine(dc,menu.left+16,menu.top+51,menu.right-16,menu.top+51,RGB(39,58,76),1);
        const wchar_t *labels[18]={
            L"Open media",L"Find network devices",L"Cast to TV",L"Play / Pause",
            L"Fullscreen",L"Load playlist",L"Save playlist",L"Recent media",
            L"Shuffle: Off",L"Repeat current: Off",L"Advanced equalizer",
            L"Streaming services",L"Torrent Stream",L"Help & User Guide",
            L"Check for updates",L"Settings",L"Exit player",L"About ABM AV Player"
        };
        const wchar_t *shortcuts[18]={
            L"Ctrl+O",L"Ctrl+D",L"Ctrl+Shift+C",L"Space",
            L"F11",L"Ctrl+L",L"Ctrl+S",L"Ctrl+J",
            L"Ctrl+Shift+H",L"Ctrl+R",L"Ctrl+E",
            L"Ctrl+Shift+S",L"Ctrl+Shift+T",L"F1",
            L"Ctrl+Shift+U",L"Ctrl+,",L"Alt+F4",L"Ctrl+I"
        };
        const wchar_t *icons[18]={L"+",L"N",L"TV",L"▶",L"⛶",L"↓",L"↑",L"◷",L"⇄",L"↻",L"EQ",L"WEB",L"TOR",L"?",L"↻",L"⚙",L"×",L"i"};
        for(int i=0;i<18;i++) {
            RECT item=R_MENU_ITEMS[i];
            if(i==8)labels[8]=g_shuffle?L"Shuffle: On":L"Shuffle: Off";
            if(i==9)labels[9]=g_repeat?L"Repeat current: On":L"Repeat current: Off";
            if(item.bottom>menu.bottom)item.bottom=menu.bottom;
            if(item.top>=menu.bottom)continue;
            BOOL hot=(g_hoverMenuItem==i)||(g_menuOpen&&g_menuSelection==i);
            BOOL featured=(i==11||i==12);
            RECT row=item;InflateRect(&row,-1,-1);
            COLORREF fill=featured?(i==11?RGB(13,55,61):RGB(62,39,29)):RGB(17,29,44);
            COLORREF edge=featured?(i==11?RGB(43,126,125):RGB(139,91,59)):RGB(31,48,65);
            if(hot){
                fill=featured?(i==11?RGB(20,83,83):RGB(91,54,34)):RGB(25,55,67);
                edge=RGB(83,181,171);
            }
            RoundBox(dc,row,fill,featured?13:9);
            ABM_D2D_StrokeRound(&g_ui,(float)row.left,(float)row.top,(float)row.right,(float)row.bottom,
                featured?13.0f:9.0f,hot?RGB(105,207,192):edge,hot?1.25f:0.8f);
            RECT glyph={item.left+(featured?14:10),item.top+(featured?9:5),
                        item.left+(featured?56:40),item.bottom-(featured?9:5)};
            COLORREF glyphFill=featured?(i==11?RGB(27,119,113):RGB(137,81,45)):RGB(27,47,66);
            if(hot)glyphFill=RGB(37,116,113);
            RoundBox(dc,glyph,glyphFill,featured?10:8);
            Txt(dc,icons[i],glyph,RGB(231,244,248),featured?g_headingFont:g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            RECT label={item.left+(featured?68:50),item.top+(featured?6:2),
                        item.right-112,item.bottom-(featured?6:2)};
            if(featured){
                RECT mainLabel=label;mainLabel.bottom=label.top+(item.bottom-item.top)/2+3;
                RECT subLabel=label;subLabel.top=mainLabel.bottom-1;
                Txt(dc,i==11?L"STREAMING TVs & SERVICES":L"TORRENT STREAMING",mainLabel,
                    RGB(249,252,255),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                Txt(dc,i==11?L"Open services, discover TVs and cast supported video":L"Add a magnet or torrent file and stream while downloading",
                    subLabel,RGB(187,210,220),g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            }else{
                COLORREF textColor=hot?RGB(255,255,255):RGB(226,236,246);
                Txt(dc,labels[i],label,textColor,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            }
            RECT shortcut={item.right-104,item.top+4,item.right-8,item.bottom-4};
            RoundBox(dc,shortcut,hot?RGB(34,75,83):RGB(12,22,35),7);
            ABM_D2D_StrokeRound(&g_ui,(float)shortcut.left,(float)shortcut.top,(float)shortcut.right,
                (float)shortcut.bottom,7.0f,hot?RGB(65,131,131):RGB(34,51,67),0.7f);
            Txt(dc,shortcuts[i],shortcut,hot?RGB(211,244,238):RGB(145,171,190),g_smallFont,
                DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            /* Quiet dividers reinforce the command groups without adding
               extra hit targets or changing menu order. */
            if(i==2||i==7||i==10||i==12||i==15||i==16){
                int y=item.bottom+1;
                if(y<menu.bottom-5)DrawLine(dc,item.left+8,y,item.right-8,y,RGB(36,53,68),1);
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
        MessageBoxW(g_main,
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
        MessageBoxW(g_main,message,APP_NAME,MB_ICONWARNING);
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

        libvlc_media_player_stop(g_player);
        libvlc_media_player_release(g_player);
        g_player=libvlc_media_player_new(g_vlc);
        if(!g_player){
            g_hasMedia=FALSE;g_pendingResume=FALSE;g_rendererApplied=FALSE;
            SetStatus(L"Could not recreate playback engine");
            MessageBoxW(g_main,L"Could not recreate the playback engine. Select the media again to retry.",APP_NAME,MB_ICONERROR);
            return;
        }
        libvlc_media_player_set_hwnd(g_player,g_video);

        if(g_selectedRenderer && libvlc_media_player_set_renderer(g_player,g_selectedRenderer)!=0){
            g_hasMedia=FALSE;g_pendingResume=FALSE;g_rendererApplied=FALSE;
            SetStatus(L"Receiver rejected the renderer connection");
            MessageBoxW(g_main,
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
                libvlc_media_player_set_media(g_player,m);
                ApplyEqProfile();
                int playResult=libvlc_media_player_play(g_player);
                libvlc_media_release(m);
                if(playResult==0){
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
                    MessageBoxW(g_main,
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
static void PruneUnavailablePlaylistItems(void) {
    int write=0,removed=0;
    for(int read=0;read<g_playlistCount;read++){
        DWORD attrs=GetFileAttributesW(g_playlist[read]);
        if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_DIRECTORY)){removed++;continue;}
        if(write!=read)wcsncpy_s(g_playlist[write],PATH_CAP,g_playlist[read],_TRUNCATE);
        write++;
    }
    if(removed){g_playlistCount=write;g_current=-1;g_selected=write?0:-1;g_playlistScroll=0;SavePersistentState();}
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
    swprintf_s(value,32,L"%llu",(unsigned long long)g_torrentCacheQuotaGiB);WritePrivateProfileStringW(L"Torrent",L"CacheQuotaGiB",value,path);
    swprintf_s(value,32,L"%d",g_torrentDownloadLimitKiB);WritePrivateProfileStringW(L"Torrent",L"DownloadLimitKiB",value,path);
    swprintf_s(value,32,L"%d",g_torrentUploadLimitKiB);WritePrivateProfileStringW(L"Torrent",L"UploadLimitKiB",value,path);
    WritePrivateProfileStringW(L"Torrent",L"CacheBaseDir",g_torrentCacheBaseDir,path);
    WritePrivateProfileStringW(L"Torrent",L"DownloadDir",g_torrentDownloadDir,path);
}
static void LoadPersistentState(void) {
    if(!EnsureDataDirectory())return;
    wchar_t path[PATH_CAP];swprintf_s(path,PATH_CAP,L"%s\\settings.ini",g_dataDir);
    g_volume=GetPrivateProfileIntW(L"Player",L"Volume",70,path);
    g_shuffle=GetPrivateProfileIntW(L"Player",L"Shuffle",0,path)!=0;
    g_repeat=GetPrivateProfileIntW(L"Player",L"Repeat",0,path)!=0;
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
    PruneUnavailablePlaylistItems();
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
    swprintf_s(value,24,L"%d",(int)(g_eqPreamp*10.0f));
    WritePrivateProfileStringW(L"Audio",L"EqPreamp",value,path);
    for(int i=0;i<10;i++){wchar_t key[24];swprintf_s(key,_countof(key),L"EqBand%d",i);swprintf_s(value,24,L"%d",(int)(g_eqBandGain[i]*10.0f));WritePrivateProfileStringW(L"Audio",key,value,path);}
}
static void LoadAudioPreferences(void) {
    if(!EnsureDataDirectory())return;
    wchar_t path[PATH_CAP];
    swprintf_s(path,PATH_CAP,L"%s\\audio.ini",g_dataDir);
    g_replayGain=GetPrivateProfileIntW(L"Audio",L"ReplayGain",1,path)!=0;
    g_compressor=GetPrivateProfileIntW(L"Audio",L"Compressor",0,path)!=0;
    g_eqProfile=GetPrivateProfileIntW(L"Audio",L"EqProfile",0,path);
    if(!((g_eqProfile>=0&&g_eqProfile<=4)||(g_eqProfile>=10&&g_eqProfile<=19)))g_eqProfile=0;
    g_eqPreamp=(float)GetPrivateProfileIntW(L"Audio",L"EqPreamp",0,path)/10.0f;
    for(int i=0;i<10;i++){wchar_t key[24];swprintf_s(key,_countof(key),L"EqBand%d",i);g_eqBandGain[i]=(float)GetPrivateProfileIntW(L"Audio",key,0,path)/10.0f;}
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
static void PlayIndex(int index) {
    if(index<0||index>=g_playlistCount)return;
    if(g_torrentPlaybackActive||g_torrentStreamThread)TorrentStopPlayback();
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
            SetStatus(L"Receiver setup failed - playback cancelled, not sent locally");
            MessageBoxW(g_main,L"LibVLC rejected the selected receiver. No local playback was started. Try selecting the receiver again or choose another device.",APP_NAME,MB_ICONERROR);
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
        g_hasMedia=FALSE;g_pendingResume=FALSE;ResetArtwork();
        if(g_selectedRenderer){
            SetStatus(L"Receiver rejected playback - check the TV and supported format");
            MessageBoxW(g_main,L"LibVLC could not start playback on the selected receiver. The video was not redirected to local playback. Check that the TV is awake and supports this media format.",APP_NAME,MB_ICONERROR);
        }else{
            MessageBoxW(g_main,L"Playback could not start. Check the media file and VLC plugins.",APP_NAME,MB_ICONERROR);
        }
        return;
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
    SavePersistentState();InvalidateRect(g_main,NULL,FALSE);
}
static void ClearPlaylist(void) {
    if(g_hasMedia)libvlc_media_player_stop(g_player);
    g_hasMedia=FALSE;g_current=-1;g_selected=-1;g_playlistCount=0;g_playlistScroll=0;
    g_endHandled=FALSE;g_pendingResume=FALSE;ResetArtwork();SavePersistentState();
    SetStatus(L"Playlist cleared");InvalidateRect(g_main,NULL,FALSE);
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
    fclose(fp);PruneUnavailablePlaylistItems();if(g_playlistCount)g_selected=0;
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
        case 5:if(MessageBoxW(hwnd,L"Clear all items from the playlist?",APP_NAME,MB_YESNO|MB_ICONQUESTION)==IDYES)ClearPlaylist();break;
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
    RECT tr={timelineLeft,h-116,timelineRight,h-106};
    HBRUSH track=CreateSolidBrush(RGB(105,116,130));FillRect(mem,&tr,track);DeleteObject(track);
    RECT progress=tr;progress.right=playheadX;
    HBRUSH played=CreateSolidBrush(RGB(40,211,185));FillRect(mem,&progress,played);DeleteObject(played);
    /* A dark halo keeps the bright playhead legible over any video frame. */
    HBRUSH halo=CreateSolidBrush(RGB(12,19,28));
    RECT haloRect={playheadX-10,h-126,playheadX+10,h-96};
    FillRect(mem,&haloRect,halo);DeleteObject(halo);
    HBRUSH head=CreateSolidBrush(RGB(255,255,255));
    RECT headRect={playheadX-5,h-123,playheadX+5,h-99};
    FillRect(mem,&headRect,head);DeleteObject(head);
    /* Add a fine vertical locator through the track so the position remains
       easy to distinguish even against the bright played segment. */
    RECT locator={playheadX-1,h-130,playheadX+1,h-94};
    HBRUSH locatorBrush=CreateSolidBrush(RGB(255,255,255));
    FillRect(mem,&locator,locatorBrush);DeleteObject(locatorBrush);
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
static LRESULT CALLBACK VideoKeyboardSubclass(HWND video, UINT msg,
        WPARAM wp, LPARAM lp, UINT_PTR subclassId, DWORD_PTR refData) {
    (void)subclassId;
    HWND owner = (HWND)refData;
    if (IsWindow(owner) && (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN ||
        msg == WM_LBUTTONUP || msg == WM_MOUSEWHEEL)) {
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if (msg == WM_MOUSEWHEEL) {
            /* Wheel coordinates are screen-relative, unlike client mouse messages. */
            ScreenToClient(owner,&pt);
        } else {
            MapWindowPoints(video,owner,&pt,1);
        }
        /* The LibVLC child HWND can physically cover the drawer even though the
           drawer is painted as a foreground overlay by the owner. Forward pointer
           input over the drawer to the owner so its header/chevron and controls
           receive the click instead of VLC's video surface. */
        if ((g_drawerOpen && !g_fullscreen && PtInRect(&R_SIDEBAR,pt)) ||
            (g_fullscreen && msg == WM_MOUSEMOVE)) {
            SendMessageW(owner,msg,wp,MAKELPARAM(pt.x,pt.y));
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
    ApplyCustomEqualizer();
    SaveAudioPreferences();
    if(g_eqWindow)InvalidateRect(g_eqWindow,NULL,FALSE);
}
static void EqSetFromPoint(HWND hwnd,int x,int y) {
    (void)hwnd;
    if(y>=102&&y<=132&&x>=145&&x<=555){
        g_eqPreamp=-12.0f+24.0f*(float)(x-145)/410.0f;
        if(g_eqPreamp<-12)g_eqPreamp=-12;if(g_eqPreamp>12)g_eqPreamp=12;
        g_eqProfile=4;ApplyCustomEqualizer();SaveAudioPreferences();return;
    }
    for(int i=0;i<10;i++){
        int cx=58+i*62;
        if(x>=cx-23&&x<=cx+23&&y>=145&&y<=330){
            float gain=12.0f-24.0f*(float)(y-155)/160.0f;
            if(gain>12)gain=12;if(gain<-12)gain=-12;
            g_eqBandGain[i]=gain;g_eqProfile=4;
            ApplyCustomEqualizer();SaveAudioPreferences();InvalidateRect(g_eqWindow,NULL,FALSE);
            return;
        }
    }
}
static LRESULT CALLBACK EqualizerWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg){
    case WM_ACTIVATE:
        if(LOWORD(wp)==WA_INACTIVE){SaveAudioPreferences();ShowWindow(hwnd,SW_HIDE);}
        return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);
        RECT client;GetClientRect(hwnd,&client);
        HDC mem=CreateCompatibleDC(dc);HBITMAP bmp=CreateCompatibleBitmap(dc,client.right,client.bottom);
        HBITMAP old=(HBITMAP)SelectObject(mem,bmp);
        HBRUSH bg=CreateSolidBrush(ABM_UI_COLOR_BACKGROUND);FillRect(mem,&client,bg);DeleteObject(bg);
        HPEN border=CreatePen(PS_SOLID,1,ABM_UI_COLOR_BORDER_STRONG);HGDIOBJ oldPen=SelectObject(mem,border);
        HBRUSH panel=CreateSolidBrush(ABM_UI_COLOR_SURFACE);HGDIOBJ oldBrush=SelectObject(mem,panel);
        RoundRect(mem,8,8,client.right-8,client.bottom-8,22,22);
        SelectObject(mem,oldBrush);DeleteObject(panel);SelectObject(mem,oldPen);DeleteObject(border);
        SetBkMode(mem,TRANSPARENT);
        HFONT title=CreateFontW(23,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,ABM_UI_FONT_FAMILY_DISPLAY);
        HFONT smallFont=CreateFontW(12,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,ABM_UI_FONT_FAMILY_TEXT);
        HFONT normal=CreateFontW(14,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Variable Text");
        HGDIOBJ oldFont=SelectObject(mem,title);SetTextColor(mem,ABM_UI_COLOR_TEXT);
        TextOutW(mem,28,23,L"ADVANCED EQUALIZER",18);
        SelectObject(mem,smallFont);SetTextColor(mem,ABM_UI_COLOR_ACCENT_BRIGHT);
        TextOutW(mem,30,52,L"SHAPE YOUR SOUND  /  REAL-TIME AUDIO PROCESSING",48);
        SelectObject(mem,normal);SetTextColor(mem,ABM_UI_COLOR_TEXT_SECONDARY);
        TextOutW(mem,30,105,L"PREAMP",6);
        RECT preTrack={145,116,555,120};HBRUSH track=CreateSolidBrush(ABM_UI_COLOR_BORDER);FillRect(mem,&preTrack,track);DeleteObject(track);
        int preX=145+(int)((g_eqPreamp+12.0f)/24.0f*410.0f);
        HPEN accent=CreatePen(PS_SOLID,3,ABM_UI_COLOR_ACCENT);oldPen=SelectObject(mem,accent);
        MoveToEx(mem,145,118,NULL);LineTo(mem,preX,118);SelectObject(mem,oldPen);DeleteObject(accent);
        HBRUSH knob=CreateSolidBrush(ABM_UI_COLOR_ACCENT_BRIGHT);HGDIOBJ ob=SelectObject(mem,knob);Ellipse(mem,preX-6,112,preX+6,124);SelectObject(mem,ob);DeleteObject(knob);
        wchar_t preText[32];swprintf_s(preText,_countof(preText),L"%+.1f dB",g_eqPreamp);
        RECT pr={565,102,650,132};DrawTextW(mem,preText,-1,&pr,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(mem,smallFont);SetTextColor(mem,ABM_UI_COLOR_TEXT_MUTED);
        for(int line=-12;line<=12;line+=6){
            int yy=155+(12-line)*160/24;
            HPEN grid=CreatePen(PS_DOT,1,ABM_UI_COLOR_BORDER);oldPen=SelectObject(mem,grid);
            MoveToEx(mem,32,yy,NULL);LineTo(mem,648,yy);SelectObject(mem,oldPen);DeleteObject(grid);
            wchar_t db[20];swprintf_s(db,_countof(db),L"%+d",line);
            RECT lr={15,yy-8,37,yy+8};DrawTextW(mem,db,-1,&lr,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        }
        static const wchar_t *fallback[10]={L"31",L"63",L"125",L"250",L"500",L"1k",L"2k",L"4k",L"8k",L"16k"};
        for(int i=0;i<10;i++){
            int cx=58+i*62,zeroY=235;
            int thumbY=155+(int)((12.0f-g_eqBandGain[i])*160.0f/24.0f);
            HPEN rail=CreatePen(PS_SOLID,5,ABM_UI_COLOR_SURFACE_RAISED);oldPen=SelectObject(mem,rail);
            MoveToEx(mem,cx,155,NULL);LineTo(mem,cx,315);SelectObject(mem,oldPen);DeleteObject(rail);
            HPEN active=CreatePen(PS_SOLID,5,ABM_UI_COLOR_ACCENT_BRIGHT);oldPen=SelectObject(mem,active);
            MoveToEx(mem,cx,zeroY,NULL);LineTo(mem,cx,thumbY);SelectObject(mem,oldPen);DeleteObject(active);
            HBRUSH dot=CreateSolidBrush(ABM_UI_COLOR_ACCENT_ORANGE);ob=SelectObject(mem,dot);
            Ellipse(mem,cx-8,thumbY-8,cx+8,thumbY+8);SelectObject(mem,ob);DeleteObject(dot);
            wchar_t val[24];swprintf_s(val,_countof(val),L"%+.1f",g_eqBandGain[i]);
            SelectObject(mem,normal);SetTextColor(mem,ABM_UI_COLOR_TEXT_SECONDARY);
            RECT vr={cx-26,thumbY-28,cx+26,thumbY-10};DrawTextW(mem,val,-1,&vr,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            SelectObject(mem,smallFont);SetTextColor(mem,ABM_UI_COLOR_TEXT_MUTED);
            RECT fr={cx-28,321,cx+28,340};DrawTextW(mem,fallback[i],-1,&fr,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        static const wchar_t *presetNames[5]={L"FLAT",L"BASS",L"VOCAL",L"ROCK",L"CLASSICAL"};
        for(int i=0;i<5;i++){
            RECT r={30+i*126,354,145+i*126,383};
            HBRUSH pb=CreateSolidBrush(ABM_UI_COLOR_SURFACE_RAISED);ob=SelectObject(mem,pb);
            RoundRect(mem,r.left,r.top,r.right,r.bottom,10,10);SelectObject(mem,ob);DeleteObject(pb);
            SelectObject(mem,smallFont);SetTextColor(mem,ABM_UI_COLOR_TEXT_SECONDARY);
            DrawTextW(mem,presetNames[i],-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
        SelectObject(mem,oldFont);DeleteObject(title);DeleteObject(smallFont);DeleteObject(normal);
        BitBlt(dc,0,0,client.right,client.bottom,mem,0,0,SRCCOPY);
        SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);EndPaint(hwnd,&ps);return 0;
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
        if(!g_eqWindow){MessageBoxW(g_main,L"Could not create the equalizer window.",APP_NAME,MB_ICONERROR);return;}
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
static HWND g_torrentWindow=NULL,g_torrentMagnetEdit=NULL,g_torrentFileList=NULL,g_torrentInfoLabel=NULL,g_torrentStatusLabel=NULL;
static HWND g_torrentDownLimitEdit=NULL,g_torrentUpLimitEdit=NULL,g_torrentBufferBar=NULL;
static HWND g_torrentCacheLimitEdit=NULL,g_torrentCacheInfoLabel=NULL;
static HWND g_torrentNumericButtons[6]={0};
static HWND g_torrentTvCombo=NULL,g_torrentDownloadPathLabel=NULL,g_torrentOpenDownloadsButton=NULL;
static HWND g_torrentSourceHint=NULL,g_torrentDownLabel=NULL,g_torrentUpLabel=NULL,g_torrentCacheLabel=NULL;
static HWND g_torrentActionButtons[5]={0},g_torrentTvButtons[3]={0};
static ABM_D2D g_torrentUi;
static BOOL g_torrentUiReady=FALSE;
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
static libvlc_event_type_t g_torrentAttachedEvents[7];
static int g_torrentAttachedEventCount=0;
typedef struct TorrentStreamRequest {
    HWND owner;
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
        MessageBoxW(owner,message,L"Torrent Stream",MB_OK|MB_ICONINFORMATION);
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
        MessageBoxW(owner,L"The torrent engine DLL is missing one or more required C ABI exports. Rebuild and install the matching Windows x64 runtime.",
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
        MessageBoxW(owner,L"ABM AV Player could not prepare its application data directory or torrent cache location.",
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
        MessageBoxW(owner,L"Windows could not start the torrent initialization worker.",
                    L"Torrent Stream",MB_OK|MB_ICONERROR);
        return;
    }
    SetStatus(L"Initializing optional libtorrent engine...");
}


static void TorrentSetLabel(HWND hwnd,const wchar_t *value) {
    if(hwnd&&IsWindow(hwnd))SetWindowTextW(hwnd,value?value:L"");
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
    (void)opaque;
    if(!event||!g_main)return;
    LONG_PTR value=0;
    if(event->type==libvlc_MediaPlayerBuffering) {
        float percent=event->u.media_player_buffering.new_cache;
        if(percent<0.0f)percent=0.0f;if(percent>100.0f)percent=100.0f;
        value=(LONG_PTR)(percent+0.5f);
    } else if(event->type==libvlc_MediaPlayerTimeChanged) {
        value=(LONG_PTR)event->u.media_player_time_changed.new_time;
    }
    PostMessageW(g_main,WM_TORRENT_PLAYER_EVENT,(WPARAM)event->type,(LPARAM)value);
}
static BOOL TorrentAttachPlayerEvents(void) {
    if(g_torrentPlayerEventManager)return TRUE;
    if(!g_player)return FALSE;
    libvlc_event_manager_t *manager=libvlc_media_player_event_manager(g_player);
    if(!manager)return FALSE;
    static const libvlc_event_type_t types[]={
        libvlc_MediaPlayerBuffering,libvlc_MediaPlayerPlaying,
        libvlc_MediaPlayerPaused,libvlc_MediaPlayerStopped,
        libvlc_MediaPlayerEndReached,libvlc_MediaPlayerEncounteredError,
        libvlc_MediaPlayerTimeChanged
    };
    g_torrentAttachedEventCount=0;
    for(int i=0;i<(int)_countof(types);i++) {
        if(libvlc_event_attach(manager,types[i],TorrentPlayerEvent,NULL)!=0) {
            for(int j=0;j<g_torrentAttachedEventCount;j++)
                libvlc_event_detach(manager,g_torrentAttachedEvents[j],TorrentPlayerEvent,NULL);
            g_torrentAttachedEventCount=0;
            return FALSE;
        }
        g_torrentAttachedEvents[g_torrentAttachedEventCount++]=types[i];
    }
    g_torrentPlayerEventManager=manager;
    return TRUE;
}
static void TorrentDetachPlayerEvents(void) {
    if(g_torrentPlayerEventManager) {
        for(int i=0;i<g_torrentAttachedEventCount;i++)
            libvlc_event_detach(g_torrentPlayerEventManager,g_torrentAttachedEvents[i],TorrentPlayerEvent,NULL);
    }
    g_torrentPlayerEventManager=NULL;
    g_torrentAttachedEventCount=0;
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
static void TorrentStopPlayback(void) {
    BOOL hadLocalStream=(g_torrentStreamThread!=NULL)||g_torrentPlaybackActive;
    TorrentDetachPlayerEvents();
    if(g_torrentStreamThread) {
        InterlockedExchange(&g_torrentStreamCancel,1);
        if(g_torrentStopHttpStream)g_torrentStopHttpStream();
    }
    if(g_torrentPlaybackActive) {
        if(g_player)libvlc_media_player_stop(g_player);
        g_torrentPlaybackActive=FALSE;g_hasMedia=FALSE;ResetArtwork();
    }
    if(hadLocalStream&&g_torrentStopHttpStream&&g_torrentInitialized)g_torrentStopHttpStream();
    if(InterlockedCompareExchange(&g_torrentPlaybackState,0,0)!=TORRENT_PLAYBACK_IDLE)
        TorrentSetPlaybackState(TORRENT_PLAYBACK_STOPPED,NULL);
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
    if(!PostMessageW(r->owner,WM_TORRENT_STREAM_READY,0,(LPARAM)r)) {
        if(g_torrentStopHttpStream)g_torrentStopHttpStream();
        if(g_torrentStreamRequest==r)g_torrentStreamRequest=NULL;
        free(r);
    }
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
    req->owner=g_main;req->task_id=g_torrentTaskId;req->file_index=fileIndex;req->file_size=size;
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
    HBRUSH bg=CreateSolidBrush(RGB(12,27,43));FillRect(dc,&r,bg);DeleteObject(bg);
    int left=14,right=r.right-104,cy=(r.top+r.bottom)/2;if(right<=left+18)return;
    int pos=TorrentSliderToPos(s->kind,s->value),thumb=left+(right-left)*pos/1000;
    /* Deep recessed rail with a luminous teal progress segment. */
    RECT track={left,cy-4,right,cy+4};
    HRGN region=CreateRoundRectRgn(track.left,track.top,track.right,track.bottom,8,8);
    HBRUSH rail=CreateSolidBrush(RGB(28,52,69));FillRgn(dc,region,rail);DeleteObject(region);DeleteObject(rail);
    if(thumb>left){
        RECT fill={left,cy-4,thumb,cy+4};region=CreateRoundRectRgn(fill.left,fill.top,fill.right,fill.bottom,8,8);
        HBRUSH accent=CreateSolidBrush(RGB(49,151,143));FillRgn(dc,region,accent);DeleteObject(region);DeleteObject(accent);
        HPEN sheen=CreatePen(PS_SOLID,1,RGB(103,218,190));HGDIOBJ op=SelectObject(dc,sheen);
        MoveToEx(dc,left+4,cy-2,NULL);LineTo(dc,max(left+4,thumb-3),cy-2);
        SelectObject(dc,op);DeleteObject(sheen);
    }
    /* Soft outer halo and a larger two-tone thumb. */
    HBRUSH halo=CreateSolidBrush(RGB(22,65,73));HGDIOBJ ob=SelectObject(dc,halo);
    HPEN none=(HPEN)GetStockObject(NULL_PEN);HGDIOBJ op=SelectObject(dc,none);
    Ellipse(dc,thumb-13,cy-13,thumb+13,cy+13);
    SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(halo);
    HBRUSH knob=CreateSolidBrush(RGB(115,220,199));HPEN rim=CreatePen(PS_SOLID,1,RGB(205,255,240));
    ob=SelectObject(dc,knob);op=SelectObject(dc,rim);
    Ellipse(dc,thumb-7,cy-7,thumb+7,cy+7);
    SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(rim);DeleteObject(knob);
    /* Value capsule is isolated from the track and always legible. */
    RECT pill={r.right-94,cy-16,r.right-2,cy+16};
    HBRUSH pillBrush=CreateSolidBrush(RGB(17,48,64));HPEN pillPen=CreatePen(PS_SOLID,1,RGB(67,151,151));
    ob=SelectObject(dc,pillBrush);op=SelectObject(dc,pillPen);
    RoundRect(dc,pill.left,pill.top,pill.right,pill.bottom,14,14);
    SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(pillPen);DeleteObject(pillBrush);
    wchar_t valueText[48];
    if(s->kind<2){if(s->value==0)StringCchCopyW(valueText,_countof(valueText),L"Unlimited");
        else if(s->value>=1024)StringCchPrintfW(valueText,_countof(valueText),L"%.1f MiB/s",(double)s->value/1024.0);
        else StringCchPrintfW(valueText,_countof(valueText),L"%d KiB/s",s->value);}
    else StringCchPrintfW(valueText,_countof(valueText),L"%d GiB",s->value);
    HFONT font=CreateFontW(-12,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HGDIOBJ old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(231,247,249));
    DrawTextW(dc,valueText,-1,&pill,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    SelectObject(dc,old);DeleteObject(font);
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
    MoveWindow(g_torrentMagnetEdit,m+14,sourceTop+27,inner-254,36,FALSE);
    MoveWindow(GetDlgItem(hwnd,ID_TORRENT_ADD_MAGNET),w-m-232,sourceTop+23,104,42,FALSE);
    MoveWindow(GetDlgItem(hwnd,ID_TORRENT_BROWSE),w-m-116,sourceTop+23,104,42,FALSE);
    MoveWindow(g_torrentInfoLabel,m+14,contentTop+7,inner-28,22,FALSE);
    MoveWindow(g_torrentFileList,m+12,contentTop+34,inner-24,max(40,contentBottom-contentTop-42),FALSE);
    MoveWindow(g_torrentStatusLabel,m+14,playbackY+5,inner-28,19,FALSE);
    MoveWindow(g_torrentBufferBar,m+14,playbackY+29,inner-28,5,FALSE);
    /* Playback actions use a compact fixed width, like Streaming Centre
       controls; do not stretch them to consume the entire window. */
    const int actionWidths[5]={128,156,128,128,110},actionH=42,actionGap=10;
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
    MoveWindow(GetDlgItem(hwnd,ID_TORRENT_CLEAR_CACHE),m+418,cacheY+25,142,42,FALSE);
    MoveWindow(g_torrentOpenDownloadsButton,m+574,cacheY+25,190,42,FALSE);
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
static void TorrentPaint(HWND hwnd){
    RECT rc={0};GetClientRect(hwnd,&rc);
    const float w=(float)rc.right,h=(float)rc.bottom;
    const float m=22.0f,headerH=94.0f,sourceTop=108.0f,sourceH=66.0f;
    const float contentTop=sourceTop+sourceH+12.0f;
    float contentBottom=h-410.0f;
    if(contentBottom<contentTop+100.0f)contentBottom=contentTop+100.0f;
    float playbackY=contentBottom+12.0f,networkY=playbackY+112.0f;
    float cacheY=networkY+84.0f,tvY=cacheY+84.0f;
    const COLORREF bg=ABM_UI_COLOR_BACKGROUND,edge=ABM_UI_COLOR_BORDER_STRONG;
    const COLORREF muted=ABM_UI_COLOR_TEXT_MUTED,accent=ABM_UI_COLOR_ACCENT,white=ABM_UI_COLOR_TEXT;
    ABM_D2D_Begin(&g_torrentUi,bg);

    /* Cinematic branded masthead */
    ABM_D2D_GradientRound(&g_torrentUi,0,0,w,headerH+18,0,RGB(15,37,57),RGB(6,17,29));
    ABM_D2D_Fill(&g_torrentUi,0,0,w,2,RGB(34,111,122));
    /* Match the main player's signature ABM identity: luminous A/play
       monogram, split ABM + AV PLAYER wordmark, gradient rule and tagline. */
    ABM_D2D_Round(&g_torrentUi,20,7,102,87,22,RGB(13,28,45));
    ABM_D2D_StrokeRound(&g_torrentUi,20,7,102,87,22,RGB(63,151,180),1.0f);
    ABM_D2D_GradientRound(&g_torrentUi,28,14,94,80,17,RGB(28,157,157),RGB(52,119,126));
    ABM_D2D_StrokeRound(&g_torrentUi,33,19,89,75,14,RGB(190,229,255),0.8f);
    ABM_D2D_Line(&g_torrentUi,42,63,59,30,RGB(255,255,255),3.0f);
    ABM_D2D_Line(&g_torrentUi,59,30,76,63,RGB(255,255,255),3.0f);
    ABM_D2D_Line(&g_torrentUi,49,49,68,49,RGB(226,245,255),2.0f);
    ABM_D2D_Line(&g_torrentUi,62,40,75,48,RGB(255,255,255),2.0f);
    ABM_D2D_Line(&g_torrentUi,75,48,62,56,RGB(255,255,255),2.0f);
    ABM_D2D_Line(&g_torrentUi,62,56,62,40,RGB(255,255,255),2.0f);

    RECT r={116,8,420,49};ABM_D2D_Text(&g_torrentUi,L"ABM",r,RGB(247,250,255),3,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    r=(RECT){188,20,390,48};ABM_D2D_Text(&g_torrentUi,L"AV  PLAYER",r,RGB(132,205,194),4,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    ABM_D2D_GradientRound(&g_torrentUi,116,52,354,54,0,RGB(54,177,168),RGB(112,190,157));
    r=(RECT){116,60,510,83};ABM_D2D_Text(&g_torrentUi,L"TORRENT STREAM  ·  PEER-TO-PEER MEDIA",r,RGB(157,177,201),1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
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
    r=(RECT){w-244,23,w-22,57};ABM_D2D_Round(&g_torrentUi,r.left,r.top,r.right,r.bottom,16,RGB(11,28,43));
    ABM_D2D_StrokeRound(&g_torrentUi,r.left,r.top,r.right,r.bottom,16,RGB(42,83,101),0.9f);
    ABM_D2D_Round(&g_torrentUi,w-228,36,w-220,44,4,engineDot);
    r=(RECT){w-211,25,w-34,55};ABM_D2D_Text(&g_torrentUi,engineState,r,RGB(180,205,219),1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    /* Source card: label above the edit row; no painted text beneath controls. */
    r=(RECT){m,sourceTop,w-m,sourceTop+sourceH};
    ABM_D2D_GradientRound(&g_torrentUi,r.left,r.top,r.right,r.bottom,14,RGB(17,39,58),RGB(10,25,40));
    ABM_D2D_StrokeRound(&g_torrentUi,r.left,r.top,r.right,r.bottom,14,edge,1.0f);
    r=(RECT){m+14,sourceTop+3,m+250,sourceTop+22};ABM_D2D_Text(&g_torrentUi,L"ADD A SOURCE",r,accent,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    /* Content library card */
    r=(RECT){m,contentTop,w-m,contentBottom};
    ABM_D2D_GradientRound(&g_torrentUi,r.left,r.top,r.right,r.bottom,16,RGB(14,31,49),RGB(9,22,36));
    ABM_D2D_StrokeRound(&g_torrentUi,r.left,r.top,r.right,r.bottom,16,RGB(34,64,83),1.0f);
    r=(RECT){m+14,contentTop+5,m+260,contentTop+25};ABM_D2D_Text(&g_torrentUi,L"TORRENT CONTENT",r,accent,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    r=(RECT){w-420,contentTop+5,w-m-14,contentTop+25};
    ABM_D2D_Text(&g_torrentUi,L"SELECT A MEDIA FILE TO DOWNLOAD, PLAY OR CAST",r,muted,0,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    ABM_D2D_Fill(&g_torrentUi,m+14,contentTop+29,m+62,contentTop+31,accent);

    /* Playback deck: status, progress and actions each have their own row. */
    r=(RECT){m,playbackY,w-m,playbackY+100};
    ABM_D2D_GradientRound(&g_torrentUi,r.left,r.top,r.right,r.bottom,14,RGB(17,38,56),RGB(10,25,40));
    ABM_D2D_StrokeRound(&g_torrentUi,r.left,r.top,r.right,r.bottom,14,edge,1.0f);
    r=(RECT){m+14,playbackY+2,m+250,playbackY+21};ABM_D2D_Text(&g_torrentUi,L"PLAYBACK & BUFFER",r,accent,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    int progress=(int)SendMessageW(g_torrentBufferBar,PBM_GETPOS,0,0);
    float barL=m+14,barT=playbackY+28,barR=w-m-14;
    ABM_D2D_Round(&g_torrentUi,barL,barT,barR,barT+5,3,RGB(25,48,65));
    ABM_D2D_Fill(&g_torrentUi,barL+2,barT+1,barR-2,barT+2,RGB(49,75,91));
    if(progress>0)ABM_D2D_GradientRound(&g_torrentUi,barL,barT,barL+(barR-barL)*progress/100.0f,barT+5,3,RGB(40,151,147),RGB(102,230,190));

    /* Matched utility cards */
    r=(RECT){m,networkY,w-m,networkY+76};
    ABM_D2D_GradientRound(&g_torrentUi,r.left,r.top,r.right,r.bottom,13,RGB(15,34,52),RGB(9,23,37));
    ABM_D2D_StrokeRound(&g_torrentUi,r.left,r.top,r.right,r.bottom,13,edge,0.9f);
    r=(RECT){m,cacheY,w-m,cacheY+76};
    ABM_D2D_GradientRound(&g_torrentUi,r.left,r.top,r.right,r.bottom,13,RGB(15,34,52),RGB(9,23,37));
    ABM_D2D_StrokeRound(&g_torrentUi,r.left,r.top,r.right,r.bottom,13,edge,0.9f);

    /* Receiver dock */
    r=(RECT){m,tvY,w-m,h-12};
    ABM_D2D_GradientRound(&g_torrentUi,r.left,r.top,r.right,r.bottom,13,RGB(15,34,52),RGB(9,23,37));
    ABM_D2D_StrokeRound(&g_torrentUi,r.left,r.top,r.right,r.bottom,13,edge,0.9f);
    r=(RECT){m+14,tvY+2,m+220,tvY+22};ABM_D2D_Text(&g_torrentUi,L"TV CASTING",r,accent,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    ABM_D2D_End(&g_torrentUi);
}
/* Button renderer copied from Streaming Centre's DrawDeviceButton palette,
   radius, border, pressed/focus treatment and typography. */
static void TorrentButtonRounded(HDC dc,RECT r,COLORREF color,int radius){
    HBRUSH brush=CreateSolidBrush(color);
    HPEN pen=CreatePen(PS_SOLID,1,color);
    HGDIOBJ oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);
    DeleteObject(brush);DeleteObject(pen);
}
static void TorrentButtonText(HDC dc,const wchar_t *value,RECT r,COLORREF color){
    /* Fit labels to the actual button width. Compact utility buttons such as
       Add magnet / Apply quota need a smaller caption than the wide playback
       actions; do not clip the first or last characters behind the icon. */
    int width=r.right-r.left;
    int fontHeight=width<88?-9:(width<116?-10:(width<150?-11:-12));
    HFONT font=CreateFontW(fontHeight,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HGDIOBJ old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    DrawTextW(dc,value,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,old);DeleteObject(font);
}
static COLORREF TorrentBlend(COLORREF a,COLORREF b,int t,int maxT){
    return RGB((GetRValue(a)*(maxT-t)+GetRValue(b)*t)/maxT,
               (GetGValue(a)*(maxT-t)+GetGValue(b)*t)/maxT,
               (GetBValue(a)*(maxT-t)+GetBValue(b)*t)/maxT);
}
static void TorrentDrawButtonIcon(HDC dc,int id,RECT r,COLORREF color){
    /* Use Windows' Segoe MDL2 Assets vector glyphs for crisp, consistent
       Fluent-style icons at every DPI. This avoids hand-drawn, clipped
       geometry and keeps the visual language consistent with the rest of ABM. */
    wchar_t glyph=0;
    switch(id){
    case ID_TORRENT_PAUSE: glyph=0xE769; break;             /* Pause */
    case ID_TORRENT_STREAM: glyph=0xE768; break;            /* Play */
    case ID_TORRENT_STOP: glyph=0xE71A; break;              /* Stop */
    case ID_TORRENT_REMOVE: case ID_TORRENT_CLEAR_CACHE: glyph=0xE74D; break; /* Delete */
    case ID_TORRENT_REFRESH: glyph=0xE72C; break;           /* Refresh */
    case ID_TORRENT_FIND_TVS: glyph=0xE721; break;          /* Search */
    case ID_TORRENT_CAST_TV: glyph=0xE7F4; break;            /* Display */
    case ID_TORRENT_STOP_TV: glyph=0xE7F4; break;            /* Display */
    case ID_TORRENT_OPEN_DOWNLOADS: glyph=0xE8B7; break;      /* Folder open */
    case ID_TORRENT_APPLY_LIMITS: case ID_TORRENT_CACHE_APPLY: glyph=0xE73E; break; /* Check */
    case ID_TORRENT_DOWN_DEC: case ID_TORRENT_UP_DEC: case ID_TORRENT_CACHE_DEC: glyph=0xE738; break;
    case ID_TORRENT_DOWN_INC: case ID_TORRENT_UP_INC: case ID_TORRENT_CACHE_INC: glyph=0xE710; break;
    case ID_TORRENT_ADD_MAGNET: glyph=0xE8D7; break;         /* Add */
    case ID_TORRENT_BROWSE: glyph=0xE8B7; break;             /* Folder */
    default: glyph=0xE710; break;                            /* Add */
    }
    HFONT font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,
        L"Segoe MDL2 Assets");
    HGDIOBJ old=SelectObject(dc,font);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    wchar_t text[2]={glyph,0};
    DrawTextW(dc,text,1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    SelectObject(dc,old);DeleteObject(font);
}
static void TorrentDrawButton(const DRAWITEMSTRUCT *item){
    if(!item||!item->hwndItem)return;
    HDC dc=item->hDC;RECT r=item->rcItem;
    BOOL pressed=(item->itemState&ODS_SELECTED)!=0;
    BOOL hot=(item->itemState&ODS_HOTLIGHT)!=0;
    BOOL focused=(item->itemState&ODS_FOCUS)!=0;
    HBRUSH background=CreateSolidBrush(RGB(10,23,37));
    FillRect(dc,&r,background);DeleteObject(background);
    RECT face=r;InflateRect(&face,-1,-1);
    HRGN clip=CreateRoundRectRgn(face.left,face.top,face.right,face.bottom,14,14);
    int saved=SaveDC(dc);SelectClipRgn(dc,clip);
    /* Button hierarchy: filled mint-teal for key actions, quiet slate for
       secondary controls, and a restrained red treatment for destructive actions. */
    int id=GetDlgCtrlID(item->hwndItem);
    BOOL primary=(id==ID_TORRENT_STREAM||id==ID_TORRENT_CAST_TV||id==ID_TORRENT_ADD_MAGNET);
    BOOL destructive=(id==ID_TORRENT_REMOVE||id==ID_TORRENT_CLEAR_CACHE);
    COLORREF top,bottom,iconColor,textColor,borderColor;
    if(primary){
        top=pressed?RGB(35,160,151):RGB(24,112,111);
        bottom=pressed?RGB(73,190,165):RGB(34,139,132);
        iconColor=RGB(222,255,246);textColor=RGB(250,255,253);borderColor=RGB(91,206,183);
    }else if(destructive){
        top=pressed?RGB(128,57,63):RGB(65,37,47);
        bottom=pressed?RGB(158,70,73):RGB(83,43,52);
        iconColor=RGB(255,174,169);textColor=RGB(255,231,229);borderColor=RGB(137,77,82);
    }else{
        top=pressed?RGB(48,94,111):RGB(17,42,59);
        bottom=pressed?RGB(57,111,119):RGB(24,57,71);
        iconColor=RGB(139,194,205);textColor=RGB(213,230,239);borderColor=RGB(53,91,110);
    }
    if(hot&&!pressed&&!destructive){top=TorrentBlend(top,RGB(40,92,101),1,4);bottom=TorrentBlend(bottom,RGB(50,108,113),1,4);}
    for(int y=face.top;y<face.bottom;y++){
        RECT stripe={face.left,y,face.right,y+1};
        HBRUSH line=CreateSolidBrush(TorrentBlend(top,bottom,y-face.top,max(1,face.bottom-face.top-1)));
        FillRect(dc,&stripe,line);DeleteObject(line);
    }
    RestoreDC(dc,saved);DeleteObject(clip);
    HPEN pen=CreatePen(PS_SOLID,focused?2:1,focused?RGB(171,246,226):borderColor);
    HGDIOBJ oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,face.left,face.top,face.right,face.bottom,14,14);
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
    /* Fine top highlight gives the button a glass-like edge without a white rim. */
    HPEN hi=CreatePen(PS_SOLID,1,primary?RGB(127,229,207):(destructive?RGB(132,76,81):RGB(53,91,110)));oldPen=SelectObject(dc,hi);
    MoveToEx(dc,face.left+9,face.top+1,NULL);LineTo(dc,face.right-9,face.top+1);
    SelectObject(dc,oldPen);DeleteObject(hi);
    wchar_t caption[128]={0};GetWindowTextW(item->hwndItem,caption,(int)_countof(caption));
    /* A consistent icon rail preserves alignment across every button tier. */
    if(id==ID_TORRENT_DOWN_DEC||id==ID_TORRENT_UP_DEC||id==ID_TORRENT_CACHE_DEC||
       id==ID_TORRENT_DOWN_INC||id==ID_TORRENT_UP_INC||id==ID_TORRENT_CACHE_INC){
        RECT glyphRect={r.left+1,r.top+1,r.right-1,r.bottom-1};
        TorrentDrawButtonIcon(dc,id,glyphRect,RGB(142,226,211));
    }else{
        RECT iconRect={r.left+3,r.top+2,r.left+31,r.bottom-2};
        TorrentDrawButtonIcon(dc,id,iconRect,iconColor);
        RECT textRect={r.left+31,r.top+2,r.right-5,r.bottom-2};
        TorrentButtonText(dc,caption,textRect,textColor);
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
    COLORREF edge=focus?RGB(91,205,188):(hot?RGB(57,133,139):RGB(35,70,88));
    HPEN pen=CreatePen(PS_SOLID,focus?2:1,edge);
    HGDIOBJ op=SelectObject(dc,pen),ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,r.left,r.top,r.right-1,r.bottom-1,14,14);
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
                HBRUSH fill=CreateSolidBrush(RGB(17,48,65));
                HPEN edge=CreatePen(PS_SOLID,1,RGB(43,105,119));
                HGDIOBJ ob=SelectObject(dc,fill),op=SelectObject(dc,edge);
                RoundRect(dc,arrow.left,arrow.top,arrow.right,arrow.bottom,10,10);
                SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(fill);DeleteObject(edge);
                HPEN chevron=CreatePen(PS_SOLID,2,RGB(116,220,204));
                op=SelectObject(dc,chevron);
                int cx=arrow.left+arrowW/2,cy=(arrow.top+arrow.bottom)/2-1;
                MoveToEx(dc,cx-5,cy-2,NULL);LineTo(dc,cx,cy+3);LineTo(dc,cx+5,cy-2);
                SelectObject(dc,op);DeleteObject(chevron);
                HPEN border=CreatePen(PS_SOLID,1,(GetFocus()==hwnd)?RGB(91,205,188):RGB(35,82,101));
                op=SelectObject(dc,border);ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
                RoundRect(dc,r.left,r.top,r.right-1,r.bottom-1,14,14);
                SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(border);
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
    COLORREF bg=(d->itemState&ODS_SELECTED)?RGB(20,75,83):((d->itemID&1)?RGB(12,28,44):RGB(10,24,39));
    HBRUSH b=CreateSolidBrush(bg);FillRect(dc,&r,b);DeleteObject(b);
    if(d->itemState&ODS_SELECTED){
        RECT pill=r;InflateRect(&pill,-3,-2);
        HBRUSH sb=CreateSolidBrush(RGB(20,68,79));HPEN sp=CreatePen(PS_SOLID,1,RGB(49,132,139));
        HGDIOBJ ob=SelectObject(dc,sb),op=SelectObject(dc,sp);
        RoundRect(dc,pill.left,pill.top,pill.right,pill.bottom,10,10);
        SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(sb);DeleteObject(sp);
        RECT marker={r.left+5,r.top+6,r.left+8,r.bottom-6};
        HBRUSH mb=CreateSolidBrush(RGB(93,218,190));FillRect(dc,&marker,mb);DeleteObject(mb);
    }
    wchar_t line[1200]=L"";
    if(d->itemID!=(UINT)-1)SendMessageW(d->hwndItem,LB_GETTEXT,d->itemID,(LPARAM)line);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,(d->itemState&ODS_SELECTED)?RGB(247,253,255):RGB(178,201,218));
    RECT tx=r;tx.left+=12;tx.right-=10;
    HFONT f=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HGDIOBJ old=SelectObject(dc,f);
    DrawTextW(dc,line,-1,&tx,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SelectObject(dc,old);DeleteObject(f);
    if(d->itemState&ODS_FOCUS){HPEN pen=CreatePen(PS_SOLID,1,RGB(65,190,165));old=SelectObject(dc,pen);
        HGDIOBJ ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));Rectangle(dc,r.left+1,r.top+1,r.right-1,r.bottom-1);
        SelectObject(dc,ob);SelectObject(dc,old);DeleteObject(pen);}
}
static void TorrentDrawTvCombo(const DRAWITEMSTRUCT *d){
    if(!d||!d->hwndItem)return;
    HDC dc=d->hDC;RECT r=d->rcItem;
    COLORREF bg=(d->itemState&ODS_COMBOBOXEDIT)?RGB(13,30,47):RGB(10,24,38);
    HBRUSH b=CreateSolidBrush(bg);FillRect(dc,&r,b);DeleteObject(b);
    if(d->itemID!=(UINT)-1){
        if(d->itemState&ODS_SELECTED){
            HBRUSH sb=CreateSolidBrush(RGB(25,70,83));RECT sr=r;InflateRect(&sr,-2,-1);
            FillRect(dc,&sr,sb);DeleteObject(sb);
        }
        wchar_t name[256]=L"";SendMessageW(d->hwndItem,CB_GETLBTEXT,d->itemID,(LPARAM)name);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,(d->itemState&ODS_SELECTED)?RGB(245,255,255):RGB(190,213,227));
        RECT tr=r;tr.left+=13;tr.right-=36;
        HFONT f=CreateFontW(-14,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        HGDIOBJ old=SelectObject(dc,f);DrawTextW(dc,name,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        SelectObject(dc,old);DeleteObject(f);
    }
    /* The combo's native drop button is covered and redrawn once by the
       control subclass after default painting. The edit item draws no arrow. */
    if(d->itemState&ODS_COMBOBOXEDIT){
        HPEN border=CreatePen(PS_SOLID,1,RGB(29,75,91));HGDIOBJ old=SelectObject(dc,border);
        HGDIOBJ ob=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));RoundRect(dc,r.left,r.top,r.right,r.bottom,10,10);
        SelectObject(dc,ob);SelectObject(dc,old);DeleteObject(border);
    }
}
static LRESULT CALLBACK TorrentWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_PAINT: { PAINTSTRUCT ps;BeginPaint(hwnd,&ps);if(g_torrentUiReady)TorrentPaint(hwnd);EndPaint(hwnd,&ps);return 0; }
    case WM_SIZE:
        if(g_torrentUiReady)ABM_D2D_Resize(&g_torrentUi,(UINT)LOWORD(lp),(UINT)HIWORD(lp));
        TorrentLayout(hwnd);
        RedrawWindow(hwnd,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE);
        return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO *mm=(MINMAXINFO*)lp;mm->ptMinTrackSize.x=900;mm->ptMinTrackSize.y=760;return 0; }
    case WM_ERASEBKGND: return 1;
    
    case WM_DRAWITEM: { const DRAWITEMSTRUCT *dis=(const DRAWITEMSTRUCT*)lp; if(dis&&dis->CtlType==ODT_BUTTON){TorrentDrawButton(dis);return TRUE;} if(dis&&dis->CtlType==ODT_COMBOBOX){TorrentDrawTvCombo(dis);return TRUE;} if(dis&&dis->CtlType==ODT_LISTBOX&&dis->CtlID==ID_TORRENT_FILES){TorrentDrawFileListItem(dis);return TRUE;} break; }
    case WM_MEASUREITEM: { MEASUREITEMSTRUCT *mi=(MEASUREITEMSTRUCT*)lp; if(mi&&mi->CtlType==ODT_COMBOBOX&&mi->CtlID==ID_TORRENT_TV_COMBO){mi->itemHeight=38;return TRUE;} break; }
    
    case WM_CTLCOLORDLG:
        if(!g_torrentSurfaceBrush)g_torrentSurfaceBrush=CreateSolidBrush(RGB(18,21,29));
        return (LRESULT)g_torrentSurfaceBrush;
    case WM_CTLCOLORSTATIC: {
        HDC dc=(HDC)wp;SetTextColor(dc,RGB(204,226,238));SetBkMode(dc,OPAQUE);SetBkColor(dc,RGB(12,27,43));
        if(!g_torrentControlBrush)g_torrentControlBrush=CreateSolidBrush(RGB(12,27,43));
        return (LRESULT)g_torrentControlBrush;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc=(HDC)wp;SetTextColor(dc,RGB(239,247,252));SetBkColor(dc,RGB(15,31,47));
        if(!g_torrentControlBrush)g_torrentControlBrush=CreateSolidBrush(RGB(15,31,47));
        return (LRESULT)g_torrentControlBrush;
    }
    case WM_CREATE: {
        g_torrentUiFont=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        HFONT font=g_torrentUiFont?g_torrentUiFont:(HFONT)GetStockObject(DEFAULT_GUI_FONT);
        ZeroMemory(&g_torrentUi,sizeof(g_torrentUi));
        if(!ABM_D2D_Init(&g_torrentUi,hwnd)){if(g_torrentUiFont){DeleteObject(g_torrentUiFont);g_torrentUiFont=NULL;}return -1;}
        g_torrentUiReady=TRUE;
        if(!TorrentRegisterSliderClass(GetModuleHandleW(NULL)))return -1;
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
            {ID_TORRENT_PAUSE,L"Pause / Resume"},{ID_TORRENT_STREAM,L"Play / Stream"},
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
                else MessageBoxW(hwnd,L"Could not open the configured permanent downloads folder.",L"Torrent Downloads",MB_OK|MB_ICONWARNING);
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
                if(MessageBoxW(hwnd,L"Delete cached data for inactive torrents? Data belonging to the currently loaded torrent is protected. This cannot be undone.",L"Clear Torrent Cache",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return 0;
                uint64_t freed=0,remaining=0;
                if(g_torrentClearCache&&g_torrentClearCache(&freed,&remaining)) {
                    wchar_t a[64],b[64],msg[192];TorrentFormatBytes((int64_t)freed,a,_countof(a));TorrentFormatBytes((int64_t)remaining,b,_countof(b));
                    StringCchPrintfW(msg,_countof(msg),L"Inactive cache cleared — freed %s. Remaining cache: %s.",a,b);
                    TorrentSetLabel(g_torrentStatusLabel,msg);TorrentRefreshCacheStatus();
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
        ABM_D2D_Destroy(&g_torrentUi);
        if(g_torrentUiFont){DeleteObject(g_torrentUiFont);g_torrentUiFont=NULL;}
        g_torrentInfoLabel=NULL;g_torrentStatusLabel=NULL;g_torrentDownLimitEdit=NULL;g_torrentUpLimitEdit=NULL;g_torrentBufferBar=NULL;g_torrentOpenDownloadsButton=NULL;
        g_torrentCacheLimitEdit=NULL;g_torrentCacheInfoLabel=NULL;g_torrentTvCombo=NULL;g_torrentDownloadPathLabel=NULL;return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static void ShowTorrentWindow(HWND owner) {
    if(!g_torrentInitialized){StartTorrentEngine(owner);return;}
    if(g_torrentWindow&&IsWindow(g_torrentWindow)){ShowWindow(g_torrentWindow,SW_SHOWNORMAL);SetForegroundWindow(g_torrentWindow);return;}
    static BOOL registered=FALSE;HINSTANCE inst=GetModuleHandleW(NULL);
    if(!registered){WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.lpfnWndProc=TorrentWndProc;wc.hInstance=inst;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=CreateSolidBrush(RGB(10,17,29));wc.hIcon=LoadIconW(inst,MAKEINTRESOURCEW(IDI_APP_ICON));wc.lpszClassName=L"ABMAVPlayerTorrent";if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;registered=TRUE;}
    RECT wr;GetWindowRect(owner,&wr);MONITORINFO mi={sizeof(mi)};HMONITOR mon=MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST);GetMonitorInfoW(mon,&mi);int maxW=mi.rcWork.right-mi.rcWork.left-32,maxH=mi.rcWork.bottom-mi.rcWork.top-32;int w=maxW<1080?maxW:1080,h=maxH<980?maxH:980;if(w<760)w=760;if(h<620)h=620;
    int x=wr.left+(wr.right-wr.left-w)/2,y=wr.top+(wr.bottom-wr.top-h)/2;if(x<0)x=20;if(y<0)y=20;
    g_torrentWindow=CreateWindowExW(0,L"ABMAVPlayerTorrent",L"ABM AV Player — Torrent Stream",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,x,y,w,h,owner,NULL,inst,NULL);
    if(g_torrentWindow){HICON appIcon=LoadIconW(inst,MAKEINTRESOURCEW(IDI_APP_ICON));if(appIcon){SendMessageW(g_torrentWindow,WM_SETICON,ICON_BIG,(LPARAM)appIcon);SendMessageW(g_torrentWindow,WM_SETICON,ICON_SMALL,(LPARAM)appIcon);}BOOL dark=TRUE;DwmSetWindowAttribute(g_torrentWindow,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));ShowWindow(g_torrentWindow,SW_SHOW);UpdateWindow(g_torrentWindow);TorrentRefreshStatus();}
}

static HBITMAP g_aboutPortrait = NULL;

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
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps);
        RECT rc; GetClientRect(hwnd,&rc);
        HBRUSH bg=CreateSolidBrush(ABM_UI_COLOR_BACKGROUND); FillRect(dc,&rc,bg); DeleteObject(bg);
        RECT band={0,0,rc.right,7};
        HBRUSH accent=CreateSolidBrush(ABM_UI_COLOR_ACCENT); FillRect(dc,&band,accent); DeleteObject(accent);
        SetBkMode(dc,TRANSPARENT);
        HFONT title=CreateFontW(-31,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,ABM_UI_FONT_FAMILY_TEXT);
        HFONT heading=CreateFontW(-22,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HFONT body=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(g_aboutPortrait) {
            HDC mem=CreateCompatibleDC(dc); HBITMAP old=(HBITMAP)SelectObject(mem,g_aboutPortrait);
            BITMAP bm; GetObjectW(g_aboutPortrait,sizeof(bm),&bm);
            int side=250, sw=bm.bmWidth<bm.bmHeight?bm.bmWidth:bm.bmHeight;
            int sx=(bm.bmWidth-sw)/2, sy=(bm.bmHeight-sw)/2;
            SetStretchBltMode(dc,HALFTONE);
            StretchBlt(dc,34,55,side,side,mem,sx,sy,sw,sw,SRCCOPY);
            SelectObject(mem,old); DeleteDC(mem);
        } else {
            RECT photo={34,55,284,305}; HBRUSH ph=CreateSolidBrush(ABM_UI_COLOR_SURFACE_RAISED); FillRect(dc,&photo,ph); DeleteObject(ph);
            SetTextColor(dc,ABM_UI_COLOR_TEXT_MUTED); HFONT hint=body; SelectObject(dc,hint);
            DrawTextW(dc,L"Add your portrait to\nassets\\owner_portrait.bmp",-1,&photo,DT_CENTER|DT_VCENTER|DT_WORDBREAK);
        }
        RECT r={320,58,rc.right-34,108}; SelectObject(dc,title); SetTextColor(dc,ABM_UI_COLOR_TEXT); DrawTextW(dc,L"ABM AV Player",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        r.top=116;r.bottom=145;SelectObject(dc,body);SetTextColor(dc,ABM_UI_COLOR_ACCENT_BRIGHT);DrawTextW(dc,L"THE CREATOR",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        r.top=151;r.bottom=192;SelectObject(dc,heading);SetTextColor(dc,RGB(255,255,255));DrawTextW(dc,L"Abhisek Basu Mullick",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        r.top=198;r.bottom=232;SelectObject(dc,body);SetTextColor(dc,ABM_UI_COLOR_TEXT_SECONDARY);DrawTextW(dc,L"Founder & Developer",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        r.top=245;r.bottom=274;SetTextColor(dc,ABM_UI_COLOR_ACCENT_BRIGHT);DrawTextW(dc,L"CONTACT",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        r.top=278;r.bottom=310;SetTextColor(dc,RGB(250,252,255));DrawTextW(dc,L"abmullick@gmail.com",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        r.top=322;r.bottom=372;SetTextColor(dc,ABM_UI_COLOR_TEXT_SECONDARY);DrawTextW(dc,L"A modern desktop media player built around\\nLibVLC, Direct2D and DirectWrite.",-1,&r,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
        RECT divider={28,405,rc.right-28,407};HBRUSH line=CreateSolidBrush(ABM_UI_COLOR_BORDER);FillRect(dc,&divider,line);DeleteObject(line);
        r.left=34;r.right=rc.right-34;r.top=424;r.bottom=454;SelectObject(dc,body);SetTextColor(dc,ABM_UI_COLOR_ACCENT_BRIGHT);DrawTextW(dc,L"IN TRIBUTE TO DENNIS RITCHIE",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        r.top=461;r.bottom=535;SetTextColor(dc,ABM_UI_COLOR_TEXT);DrawTextW(dc,L"My homage to Dennis Ritchie, creator of the C programming language and co-creator of Unix. His ideas and work helped shape modern computing and generations of computer science.",-1,&r,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
        r.top=550;r.bottom=580;SetTextColor(dc,ABM_UI_COLOR_ACCENT_BRIGHT);DrawTextW(dc,L"BUILT WITH",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        r.top=585;r.bottom=616;SetTextColor(dc,ABM_UI_COLOR_TEXT_SECONDARY);DrawTextW(dc,L"C  •  C++  •  Win32 API  •  LibVLC  •  Direct2D  •  DirectWrite  •  Microsoft WebView2",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        r.top=628;r.bottom=656;SetTextColor(dc,ABM_UI_COLOR_TEXT_MUTED);DrawTextW(dc,L"Version " APP_VERSION L"  |  © 2026 Abhisek Basu Mullick",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
        DeleteObject(title);DeleteObject(heading);DeleteObject(body);
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_KEYDOWN: if(wp==VK_ESCAPE){DestroyWindow(hwnd);return 0;} break;
    case WM_DESTROY: if(g_aboutPortrait){DeleteObject(g_aboutPortrait);g_aboutPortrait=NULL;} return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

static HBRUSH g_helpBrush=NULL;
static HFONT g_helpTitleFont=NULL, g_helpSectionFont=NULL, g_helpBodyFont=NULL, g_helpMetaFont=NULL;
static HWND g_helpEdit=NULL;
static LRESULT CALLBACK HelpWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
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
            L"APPLICATION SETTINGS\r\n"
            L"Open Settings from the Command Centre to configure the player in one dedicated window. General includes volume, playback speed, shuffle and repeat defaults. Audio includes ReplayGain normalisation and the dynamic-range compressor. Torrent & Storage lets you choose the cache parent folder and set a cache quota from 1 to 4096 GiB. The player creates a torrent-cache subfolder inside the selected parent. Network controls set torrent download and upload limits in KiB/s; enter 0 for unlimited. Updates provides a manual check for the latest public GitHub release. Choose Save settings to persist your changes.\r\n"
            L"On a new installation, the cache parent defaults to the current Windows user Downloads known folder, inside an ABM AV Player folder. Redirected Downloads folders are respected. Existing installations with a legacy cache keep using that location unless you choose another. Changing the cache parent while Torrent Stream is running requires restarting ABM AV Player. Changing the location does not automatically move existing cache data; existing files remain in the old folder. Settings are stored in %APPDATA%\\ABMAVPlayer\\settings.ini; audio processing preferences are also stored in audio.ini.\r\n"
            L"\r\n"
            L"AUDIO, VIDEO AND SUBTITLES\r\n"
            L"Right-click the video surface during playback for media actions: choose an available audio track, choose or disable subtitles, load an external subtitle file (SRT, ASS, SSA, SUB or VTT), change playback speed (0.5x–2.0x), select an equalizer preset, toggle ReplayGain normalisation or the dynamic-range compressor, and view Media information. Options depend on the file and VLC plugins.\r\n"
            L"\r\n"
            L"ADVANCED EQUALIZER AND AUDIO PROCESSING\r\n"
            L"Open Advanced equalizer from the Command Centre or press Ctrl+E. The ABM 10-band graphic EQ offers Flat, Bass boost, Vocal clarity and Treble boost profiles. These are fixed-band curves, not a parametric EQ. ReplayGain needs suitable track metadata. The compressor is a dynamic-range effect, not a guaranteed true-peak limiter. Audio preferences are saved in audio.ini.\r\n"
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
            L"SUPPORTED MEDIA AND REQUIREMENTS\r\n"
            L"Common audio formats include MP3, WAV, FLAC, AAC, M4A and OGG. Common video formats include MP4, MKV, AVI, MOV, WMV and WebM. Actual codec support depends on the installed 64-bit VLC runtime and plugins. Streaming Centre requires Microsoft Edge WebView2 Evergreen Runtime.\r\n"
            L"\r\n"
            L"KEYBOARD SHORTCUTS\r\n"
            L"Ctrl+O  Open media     |     Ctrl+D  Find network devices\r\n"
            L"Ctrl+Shift+C  Start local-player receiver discovery     |     Ctrl+Shift+S  Open Streaming Centre\r\n"
            L"Space  Play/Pause     |     Left/Right  Seek 10 seconds     |     Home  Seek to beginning\r\n"
            L"N / P  Next/Previous item     |     + / -  Change playback speed     |     F11  Fullscreen\r\n"
            L"Ctrl+L  Load playlist     |     Ctrl+S  Save playlist     |     Ctrl+J  Recent media\r\n"
            L"Ctrl+E  Advanced equalizer     |     Ctrl+Shift+H  Shuffle     |     Ctrl+R  Repeat current\r\n"
            L"F1  This help     |     Ctrl+I  About     |     Alt+F4  Exit     |     Esc  Close menu/leave fullscreen\r\n"
            L"\r\n"
            L"TROUBLESHOOTING\r\n"
            L"If a file will not play, confirm it exists and that VLC includes its codec/plugin. If a TV is not discovered, check same-network connectivity, mDNS/multicast, VPN, router isolation and Windows Firewall. If a TV appears but casting fails, try a common MP4/H.264 file and confirm the receiver supports the selected protocol. Protected streaming services may block embedded playback or handoff; try Open in browser or the provider's official app.\r\n"
            L"\r\n"
            L"ABM AV Player is a desktop media player built on LibVLC. Streaming Centre web playback is separate from local media playback and does not provide subscriptions or bypass provider protections.";

        if(!g_helpBrush)g_helpBrush=CreateSolidBrush(ABM_UI_COLOR_SURFACE);
        if(!g_helpTitleFont)g_helpTitleFont=CreateFontW(25,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Variable Display");
        if(!g_helpSectionFont)g_helpSectionFont=CreateFontW(15,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Variable Text");
        if(!g_helpBodyFont)g_helpBodyFont=CreateFontW(15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Variable Text");
        if(!g_helpMetaFont)g_helpMetaFont=CreateFontW(11,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Variable Text");
        g_helpEdit=CreateWindowExW(0,L"EDIT",help,
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_LEFT|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,
            24,105,820,540,hwnd,(HMENU)1,GetModuleHandleW(NULL),NULL);
        if(g_helpEdit)SendMessageW(g_helpEdit,WM_SETFONT,(WPARAM)g_helpBodyFont,TRUE);
        return 0;
    }
    case WM_SIZE: {
        int w=LOWORD(lp),h=HIWORD(lp);
        if(g_helpEdit)MoveWindow(g_helpEdit,24,105,w-48,h-165,TRUE);
        InvalidateRect(hwnd,NULL,FALSE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT rc;GetClientRect(hwnd,&rc);
        HBRUSH bg=CreateSolidBrush(ABM_UI_COLOR_BACKGROUND);FillRect(dc,&rc,bg);DeleteObject(bg);
        RECT header={0,0,rc.right,88};HBRUSH head=CreateSolidBrush(ABM_UI_COLOR_SURFACE_BLUE);FillRect(dc,&header,head);DeleteObject(head);
        RECT accent={0,86,rc.right,89};HBRUSH accentBrush=CreateSolidBrush(ABM_UI_COLOR_ACCENT);FillRect(dc,&accent,accentBrush);DeleteObject(accentBrush);
        SetBkMode(dc,TRANSPARENT);
        HFONT old=(HFONT)SelectObject(dc,g_helpTitleFont);SetTextColor(dc,ABM_UI_COLOR_TEXT);
        RECT title={26,13,rc.right-70,47};DrawTextW(dc,L"Help Centre",-1,&title,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(dc,g_helpMetaFont);SetTextColor(dc,ABM_UI_COLOR_ACCENT_BRIGHT);
        RECT kicker={28,48,rc.right-70,69};DrawTextW(dc,L"ABM AV PLAYER   /   USER GUIDE",-1,&kicker,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT close={rc.right-52,18,rc.right-16,54};
        HBRUSH closeBrush=CreateSolidBrush(ABM_UI_COLOR_SURFACE_RAISED);RoundRect(dc,close.left,close.top,close.right,close.bottom,10,10);DeleteObject(closeBrush);
        HPEN pen=CreatePen(PS_SOLID,2,ABM_UI_COLOR_TEXT_SECONDARY);HGDIOBJ oldPen=SelectObject(dc,pen);
        MoveToEx(dc,close.left+13,close.top+12,NULL);LineTo(dc,close.right-13,close.bottom-12);
        MoveToEx(dc,close.right-13,close.top+12,NULL);LineTo(dc,close.left+13,close.bottom-12);
        SelectObject(dc,oldPen);DeleteObject(pen);
        RECT footer={0,rc.bottom-48,rc.right,rc.bottom};HBRUSH foot=CreateSolidBrush(ABM_UI_COLOR_SURFACE);FillRect(dc,&footer,foot);DeleteObject(foot);
        HPEN line=CreatePen(PS_SOLID,1,ABM_UI_COLOR_BORDER);oldPen=SelectObject(dc,line);
        MoveToEx(dc,0,rc.bottom-48,NULL);LineTo(dc,rc.right,rc.bottom-48);
        SelectObject(dc,oldPen);DeleteObject(line);
        SelectObject(dc,g_helpMetaFont);SetTextColor(dc,ABM_UI_COLOR_TEXT_MUTED);
        RECT footText={26,rc.bottom-42,rc.right-24,rc.bottom-12};
        DrawTextW(dc,L"TIP   Press F1 to open this guide   •   Scroll to explore all features",-1,&footText,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(dc,old);EndPaint(hwnd,&ps);return 0;
    }
    case WM_NCHITTEST: {
        LRESULT hit=DefWindowProcW(hwnd,msg,wp,lp);
        if(hit==HTCLIENT) {
            POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);
            RECT rc;GetClientRect(hwnd,&rc);
            if(p.y>=0&&p.y<88&&p.x<rc.right-58)return HTCAPTION;
        }
        return hit;
    }
    case WM_LBUTTONDOWN: {
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);RECT rc;GetClientRect(hwnd,&rc);
        if(x>=rc.right-58&&y>=8&&y<=62){DestroyWindow(hwnd);return 0;}
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc=(HDC)wp;SetTextColor(dc,ABM_UI_COLOR_TEXT_SECONDARY);SetBkColor(dc,ABM_UI_COLOR_SURFACE);
        return (LRESULT)g_helpBrush;
    }
    case WM_KEYDOWN: if(wp==VK_ESCAPE){DestroyWindow(hwnd);return 0;} break;
    case WM_DESTROY:
        g_helpEdit=NULL;
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static void ShowHelpDialog(HWND owner) {
    static BOOL registered=FALSE;HINSTANCE inst=GetModuleHandleW(NULL);
    if(!registered) {
        WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.lpfnWndProc=HelpWndProc;
        wc.hInstance=inst;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);
        wc.hbrBackground=CreateSolidBrush(RGB(7,12,21));wc.lpszClassName=L"ABMAVPlayerHelp";
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
        registered=TRUE;
    }
    int w=920,h=760;
    RECT ownerRect;GetWindowRect(owner,&ownerRect);
    int x=ownerRect.left+(ownerRect.right-ownerRect.left-w)/2;
    int y=ownerRect.top+(ownerRect.bottom-ownerRect.top-h)/2;
    if(x<0)x=20;if(y<0)y=20;
    HWND dlg=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"ABMAVPlayerHelp",
        L"ABM AV Player - Help Centre",WS_POPUP|WS_THICKFRAME,
        x,y,w,h,owner,NULL,inst,NULL);
    if(!dlg)return;
    BOOL dark=TRUE;DwmSetWindowAttribute(dlg,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    {int corners=2;COLORREF border=RGB(46,70,104),caption=RGB(10,15,25);
     DwmSetWindowAttribute(dlg,(DWORD)DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
     DwmSetWindowAttribute(dlg,(DWORD)DWMWA_BORDER_COLOR,&border,sizeof(border));
     DwmSetWindowAttribute(dlg,(DWORD)DWMWA_CAPTION_COLOR,&caption,sizeof(caption));}
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
static BOOL g_settingsShuffle=FALSE,g_settingsRepeat=FALSE;
static BOOL g_settingsReplayGain=FALSE,g_settingsCompressor=FALSE;
static BOOL g_settingsRateOpen=FALSE;
static int g_settingsRateIndex=2;
static wchar_t g_settingsMessage[256]=L"Settings are saved only when you choose Save settings.";

static const float g_settingsRates[7]={0.5f,0.75f,1.0f,1.25f,1.5f,1.75f,2.0f};
static const wchar_t *g_settingsRateLabels[7]={L"0.5x",L"0.75x",L"1.0x",L"1.25x",L"1.5x",L"1.75x",L"2.0x"};

static void SettingsText(const wchar_t *text,int l,int t,int r,int b,COLORREF color,int style,UINT flags) {
 RECT rc={l,t,r,b};ABM_D2D_Text(&g_settingsUi,text,rc,color,style,flags);
}
static void SettingsSection(int top,const wchar_t *title,const wchar_t *description,int bottom) {
 /* Consistent card surface, restrained edge and a short teal section marker. */
 ABM_D2D_Round(&g_settingsUi,20.0f,(float)top,800.0f,(float)bottom,
               (float)ABM_UI_RADIUS_CARD,SETTINGS_PANEL);
 ABM_D2D_StrokeRound(&g_settingsUi,20.5f,(float)top+0.5f,799.5f,(float)bottom-0.5f,
                     (float)ABM_UI_RADIUS_CARD,SETTINGS_BORDER,0.8f);
 ABM_D2D_Round(&g_settingsUi,30.0f,(float)top+13.0f,33.0f,(float)top+34.0f,
               1.5f,ABM_UI_COLOR_ACCENT_BRIGHT);
 SettingsText(title,42,top+12,510,top+36,SETTINGS_ACCENT,1,
              DT_LEFT|DT_VCENTER|DT_SINGLELINE);
 if(description&&description[0])
  SettingsText(description,510,top+12,780,top+36,SETTINGS_MUTED,1,
               DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
static void SettingsEditFrame(int l,int t,int r,int b) {
 ABM_D2D_Round(&g_settingsUi,(float)l,(float)t,(float)r,(float)b,
               (float)ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_INPUT);
 ABM_D2D_StrokeRound(&g_settingsUi,(float)l+0.5f,(float)t+0.5f,
                     (float)r-0.5f,(float)b-0.5f,
                     (float)ABM_UI_RADIUS_CONTROL,ABM_UI_COLOR_BORDER_STRONG,0.9f);
}
static void SettingsButtonDraw(int l,int t,int r,int b,const wchar_t *label,BOOL primary) {
 COLORREF fill=primary?ABM_UI_COLOR_ACCENT:ABM_UI_COLOR_SURFACE_RAISED;
 COLORREF edge=primary?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_BORDER_STRONG;
 ABM_D2D_Round(&g_settingsUi,(float)l,(float)t,(float)r,(float)b,
               (float)ABM_UI_RADIUS_BUTTON,fill);
 ABM_D2D_StrokeRound(&g_settingsUi,(float)l+0.5f,(float)t+0.5f,
                     (float)r-0.5f,(float)b-0.5f,
                     (float)ABM_UI_RADIUS_BUTTON,edge,0.9f);
 SettingsText(label,l+8,t+1,r-8,b-1,ABM_UI_COLOR_TEXT,1,
              DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
static void SettingsCheckDraw(int l,int t,const wchar_t *label,BOOL checked) {
 ABM_D2D_Round(&g_settingsUi,(float)l,(float)t,(float)(l+18),(float)(t+18),
               (float)ABM_UI_RADIUS_CONTROL,
               checked?ABM_UI_COLOR_ACCENT:ABM_UI_COLOR_INPUT);
 ABM_D2D_StrokeRound(&g_settingsUi,(float)l+0.5f,(float)t+0.5f,
                     (float)(l+17.5f),(float)(t+17.5f),
                     (float)ABM_UI_RADIUS_CONTROL,
                     checked?ABM_UI_COLOR_ACCENT_BRIGHT:ABM_UI_COLOR_BORDER_STRONG,0.9f);
 if(checked){
  ABM_D2D_Line(&g_settingsUi,(float)(l+4),(float)(t+9),
               (float)(l+7),(float)(t+12),ABM_UI_COLOR_TEXT,1.8f);
  ABM_D2D_Line(&g_settingsUi,(float)(l+7),(float)(t+12),
               (float)(l+14),(float)(t+5),ABM_UI_COLOR_TEXT,1.8f);
 }
 SettingsText(label,l+26,t-3,l+310,t+21,SETTINGS_FG,0,
              DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
static void SettingsBrowse(HWND w) {
 BROWSEINFOW bi={0};bi.hwndOwner=w;bi.lpszTitle=L"Select torrent cache parent folder";bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
 PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&bi);if(!pid)return;
 wchar_t path[PATH_CAP]={0};if(SHGetPathFromIDListW(pid,path))SetWindowTextW(GetDlgItem(w,6201),path);CoTaskMemFree(pid);
}
static void SettingsBrowseDownloads(HWND w) {
 BROWSEINFOW bi={0};bi.hwndOwner=w;bi.lpszTitle=L"Select permanent torrent downloads folder";bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
 PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&bi);if(!pid)return;
 wchar_t path[PATH_CAP]={0};if(SHGetPathFromIDListW(pid,path))SetWindowTextW(GetDlgItem(w,6202),path);CoTaskMemFree(pid);
}
static void SettingsApply(HWND w) {
 wchar_t path[PATH_CAP]={0},downloads[PATH_CAP]={0},q[32]={0},d[32]={0},u[32]={0},v[32]={0};
 GetWindowTextW(GetDlgItem(w,6201),path,_countof(path));GetWindowTextW(GetDlgItem(w,6202),downloads,_countof(downloads));GetWindowTextW(GetDlgItem(w,6204),q,_countof(q));
 GetWindowTextW(GetDlgItem(w,6205),d,_countof(d));GetWindowTextW(GetDlgItem(w,6206),u,_countof(u));GetWindowTextW(GetDlgItem(w,6207),v,_countof(v));
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
 if((changed||downloadsChanged)&&(g_torrentInitialized||g_torrentInitPending)&&MessageBoxW(w,L"The torrent engine is running. The new location will take effect after restarting ABM AV Player. Continue?",L"Restart required",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
 wcsncpy_s(g_torrentCacheBaseDir,_countof(g_torrentCacheBaseDir),path,_TRUNCATE);
 wcsncpy_s(g_torrentDownloadDir,_countof(g_torrentDownloadDir),downloads,_TRUNCATE);
 g_torrentCacheQuotaGiB=quota;g_torrentDownloadLimitKiB=(int)down;g_torrentUploadLimitKiB=(int)up;g_volume=(int)volume;
 g_shuffle=g_settingsShuffle;g_repeat=g_settingsRepeat;g_replayGain=g_settingsReplayGain;g_compressor=g_settingsCompressor;
 if(g_settingsRateIndex<0||g_settingsRateIndex>6)g_settingsRateIndex=2;
 g_playbackRate=g_settingsRates[g_settingsRateIndex];
 if(g_player)libvlc_audio_set_volume(g_player,g_volume);if(g_hasMedia)libvlc_media_player_set_rate(g_player,g_playbackRate);
 SavePersistentState();SaveAudioPreferences();ApplyEqProfile();
 if(g_torrentInitialized){uint64_t used=0;if(g_torrentSetCacheLimit)g_torrentSetCacheLimit(quota*1024ULL*1024ULL*1024ULL,&used);if(g_torrentSetRateLimits)g_torrentSetRateLimits((int)(down*1024),(int)(up*1024));}
 StringCchCopyW(g_settingsMessage,_countof(g_settingsMessage),(changed||downloadsChanged)&&(g_torrentInitialized||g_torrentInitPending)?L"Settings saved. Restart ABM AV Player to activate the new storage locations.":L"All settings saved successfully.");
 InvalidateRect(w,NULL,FALSE);InvalidateRect(g_main,NULL,FALSE);
}
static BOOL SettingsPoint(int x,int y,int l,int t,int r,int b){return x>=l&&x<=r&&y>=t&&y<=b;}
static LRESULT CALLBACK SettingsWndProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
 switch(msg) {
 case WM_CREATE: {
  if(!ABM_D2D_Init(&g_settingsUi,w))return -1;g_settingsUiReady=TRUE;
  g_settingsEditBrush=CreateSolidBrush(RGB(11,25,40));
  g_settingsShuffle=g_shuffle;g_settingsRepeat=g_repeat;g_settingsReplayGain=g_replayGain;g_settingsCompressor=g_compressor;
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
  return 0;
 }
 case WM_SIZE:if(g_settingsUiReady)ABM_D2D_Resize(&g_settingsUi,LOWORD(lp),HIWORD(lp));InvalidateRect(w,NULL,FALSE);return 0;
 case WM_ERASEBKGND:return 1;
 case WM_CTLCOLOREDIT:{
  HDC dc=(HDC)wp;
  SetTextColor(dc,ABM_UI_COLOR_TEXT);
  SetBkColor(dc,ABM_UI_COLOR_INPUT);
  SetBkMode(dc,OPAQUE);
  return (LRESULT)g_settingsEditBrush;
 }
 case WM_LBUTTONDOWN:{
  int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
  if(SettingsPoint(x,y,565,155,704,190)){g_settingsRateOpen=!g_settingsRateOpen;InvalidateRect(w,NULL,FALSE);return 0;}
  if(g_settingsRateOpen&&SettingsPoint(x,y,565,190,704,372)){int idx=(y-190)/26;if(idx>=0&&idx<7)g_settingsRateIndex=idx;g_settingsRateOpen=FALSE;InvalidateRect(w,NULL,FALSE);return 0;}
  if(SettingsPoint(x,y,38,198,300,224)){g_settingsShuffle=!g_settingsShuffle;InvalidateRect(w,NULL,FALSE);return 0;}
  if(SettingsPoint(x,y,340,198,610,224)){g_settingsRepeat=!g_settingsRepeat;InvalidateRect(w,NULL,FALSE);return 0;}
  if(SettingsPoint(x,y,38,280,340,306)){g_settingsReplayGain=!g_settingsReplayGain;InvalidateRect(w,NULL,FALSE);return 0;}
  if(SettingsPoint(x,y,350,280,680,306)){g_settingsCompressor=!g_settingsCompressor;InvalidateRect(w,NULL,FALSE);return 0;}
  if(SettingsPoint(x,y,655,396,780,428)){SettingsBrowse(w);return 0;}
  if(SettingsPoint(x,y,655,457,780,489)){SettingsBrowseDownloads(w);return 0;}
  if(SettingsPoint(x,y,655,496,780,528)){wchar_t p[PATH_CAP]={0};GetWindowTextW(GetDlgItem(w,6201),p,_countof(p));if(p[0])ShellExecuteW(w,L"open",p,NULL,NULL,SW_SHOWNORMAL);return 0;}
  /* Keep hit targets aligned with the current Direct2D-rendered button rectangles. */
  if(SettingsPoint(x,y,39,681,194,714)){ABM_CheckForUpdates(g_main,APP_VERSION);return 0;}
  if(SettingsPoint(x,y,610,774,735,812)){SettingsApply(w);return 0;}
  if(SettingsPoint(x,y,744,774,800,812)){DestroyWindow(w);return 0;}
  if(g_settingsRateOpen){g_settingsRateOpen=FALSE;InvalidateRect(w,NULL,FALSE);}
  break;
 }
 case WM_KEYDOWN:
  if(wp==VK_ESCAPE){if(g_settingsRateOpen){g_settingsRateOpen=FALSE;InvalidateRect(w,NULL,FALSE);}else DestroyWindow(w);return 0;}
  if((GetKeyState(VK_CONTROL)&0x8000)&&wp=='S'){SettingsApply(w);return 0;}
  break;
 case WM_PAINT:{
  PAINTSTRUCT ps;BeginPaint(w,&ps);
  if(g_settingsUiReady){
   ABM_D2D_Begin(&g_settingsUi,SETTINGS_BG);
   ABM_D2D_GradientRound(&g_settingsUi,20,14,800,76,15,RGB(15,39,57),RGB(13,30,47));
   ABM_D2D_StrokeRound(&g_settingsUi,20,14,800,76,15,RGB(43,88,111),1.0f);
   ABM_D2D_Round(&g_settingsUi,34,27,76,69,12,RGB(28,157,157));
   ABM_D2D_StrokeRound(&g_settingsUi,34,27,76,69,12,RGB(115,210,207),1.0f);
   SettingsText(L"⚙",39,29,72,66,RGB(255,255,255),2,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
   SettingsText(L"SETTINGS",92,20,420,53,RGB(247,250,255),2,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsText(L"PLAYBACK  ·  AUDIO  ·  STORAGE  ·  NETWORK",94,49,550,69,SETTINGS_ACCENT,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsSection(88,L"GENERAL",L"Playback defaults",226);
   SettingsText(L"Volume (0–100)",40,140,155,180,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(160,155,264,188);
   SettingsText(L"Playback speed",430,140,550,180,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(565,155,704,190);
   ABM_D2D_Round(&g_settingsUi,572,160,697,185,5,RGB(16,39,55));
   SettingsText(g_settingsRateLabels[g_settingsRateIndex],579,160,670,185,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   ABM_D2D_Line(&g_settingsUi,681,169,686,175,SETTINGS_ACCENT,1.8f);ABM_D2D_Line(&g_settingsUi,686,175,691,169,SETTINGS_ACCENT,1.8f);
   SettingsCheckDraw(40,202,L"Shuffle by default",g_settingsShuffle);
   SettingsCheckDraw(342,202,L"Repeat current item",g_settingsRepeat);
   SettingsSection(238,L"AUDIO",L"Processing",324);
   SettingsCheckDraw(40,281,L"ReplayGain normalisation",g_settingsReplayGain);
   SettingsCheckDraw(352,281,L"Dynamic range compressor",g_settingsCompressor);
   SettingsSection(336,L"TORRENT & STORAGE",L"Cache, permanent downloads and quota",536);
   SettingsText(L"Cache parent folder  ·  torrent-cache is created inside",40,372,610,394,SETTINGS_MUTED,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(34,396,644,429);SettingsButtonDraw(655,396,780,429,L"Browse…",FALSE);
   SettingsText(L"Permanent downloads folder",40,434,610,456,SETTINGS_MUTED,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(34,457,644,490);SettingsButtonDraw(655,457,780,490,L"Browse…",FALSE);
   SettingsText(L"Cache quota (GiB)",40,495,166,526,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(169,496,279,529);SettingsButtonDraw(655,496,780,529,L"Open cache",FALSE);
   SettingsSection(548,L"NETWORK",L"Torrent bandwidth  ·  0 = unlimited",654);
   SettingsText(L"Download limit (KiB/s)",40,583,202,608,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(199,599,341,634);
   SettingsText(L"Upload limit (KiB/s)",400,583,552,608,SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsEditFrame(551,599,693,634);
   SettingsSection(666,L"UPDATES",L"Maintenance",738);
   SettingsButtonDraw(39,681,194,714,L"Check for updates",TRUE);
   SettingsText(L"Checks the latest published GitHub release.",210,682,770,713,SETTINGS_MUTED,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
   SettingsText(g_settingsMessage,40,744,780,768,SETTINGS_MUTED,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
   SettingsButtonDraw(610,774,735,812,L"Save settings",TRUE);
   SettingsButtonDraw(744,774,800,812,L"Close",FALSE);
   if(g_settingsRateOpen){
    ABM_D2D_Round(&g_settingsUi,565,190,704,374,8,RGB(12,27,43));
    ABM_D2D_StrokeRound(&g_settingsUi,565,190,704,374,8,RGB(67,137,153),1.0f);
    for(int i=0;i<7;i++){int y=191+i*26;if(i==g_settingsRateIndex)ABM_D2D_Round(&g_settingsUi,570,(float)y,699,(float)(y+25),5,RGB(25,91,88));SettingsText(g_settingsRateLabels[i],580,y,694,y+25,i==g_settingsRateIndex?RGB(255,255,255):SETTINGS_FG,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);}
   }
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
 if(g_settingsWindow&&IsWindow(g_settingsWindow)){ShowWindow(g_settingsWindow,SW_SHOWNORMAL);SetForegroundWindow(g_settingsWindow);return;}
 static BOOL registered=FALSE;HINSTANCE inst=GetModuleHandleW(NULL);
 if(!registered){WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.lpfnWndProc=SettingsWndProc;wc.hInstance=inst;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);wc.lpszClassName=L"ABMAVPlayerSettings";if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;registered=TRUE;}
 RECT wr={0,0,820,850};AdjustWindowRectEx(&wr,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);int ww=wr.right-wr.left,hh=wr.bottom-wr.top;RECT o;GetWindowRect(owner,&o);
 int x=o.left+(o.right-o.left-ww)/2,y=o.top+(o.bottom-o.top-hh)/2;
 g_settingsWindow=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"ABMAVPlayerSettings",L"ABM AV Player — Settings",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_POPUP,x,y,ww,hh,owner,NULL,inst,NULL);
 if(!g_settingsWindow)return;
 BOOL dark=TRUE;DwmSetWindowAttribute(g_settingsWindow,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
 {int corners=2;COLORREF border=RGB(46,70,104),caption=RGB(10,15,25);DwmSetWindowAttribute(g_settingsWindow,(DWORD)DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));DwmSetWindowAttribute(g_settingsWindow,(DWORD)DWMWA_BORDER_COLOR,&border,sizeof(border));DwmSetWindowAttribute(g_settingsWindow,(DWORD)DWMWA_CAPTION_COLOR,&caption,sizeof(caption));}
 EnableWindow(owner,FALSE);ShowWindow(g_settingsWindow,SW_SHOW);UpdateWindow(g_settingsWindow);
 MSG m;while(IsWindow(g_settingsWindow)&&GetMessageW(&m,NULL,0,0)>0){if(!IsDialogMessageW(g_settingsWindow,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
 EnableWindow(owner,TRUE);SetActiveWindow(owner);
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
    case WM_TORRENT_PLAYER_EVENT: {
        if(!g_torrentPlaybackActive)return 0;
        switch((libvlc_event_type_t)wp) {
            case libvlc_MediaPlayerBuffering: {
                int percent=(int)lp;
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
                int64_t mediaTime=(int64_t)(LONG_PTR)lp;
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
                g_torrentPlaybackActive=FALSE;
                g_hasMedia=FALSE;
                TorrentDetachPlayerEvents();
                if(g_torrentStopHttpStream&&g_torrentInitialized)g_torrentStopHttpStream();
                TorrentSetPlaybackState(TORRENT_PLAYBACK_FAILED,
                    L"Stream failed — LibVLC could not continue reading the torrent source.");
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
        TorrentStreamRequest *req=(TorrentStreamRequest*)lp;
        if(g_torrentStreamThread){CloseHandle(g_torrentStreamThread);g_torrentStreamThread=NULL;}
        if(g_torrentStreamRequest==req)g_torrentStreamRequest=NULL;
        BOOL cancelled=InterlockedCompareExchange(&g_torrentStreamCancel,0,0)!=0;
        if(req&&req->ok&&!cancelled) {
            int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,req->url,-1,NULL,0,NULL,NULL);
            char *url=bytes>0?(char*)malloc((size_t)bytes):NULL;
            if(url&&WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,req->url,-1,url,bytes,NULL,NULL)>0) {
                libvlc_media_t *media=libvlc_media_new_location(g_vlc,url);
                if(media) {
                    libvlc_media_player_stop(g_player);
                    g_hasMedia=FALSE;g_current=-1;
                    libvlc_media_player_set_media(g_player,media);
                    BeginArtworkParse(media);
                    BOOL eventsAttached=TorrentAttachPlayerEvents();
                    int result=libvlc_media_player_play(g_player);
                    libvlc_media_release(media);
                    if(result==0) {
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
            MessageBoxW(hwnd,L"The native torrent engine could not initialize. Normal playback and Streaming Centre remain available.",
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
    case WM_TIMER:if(wp==TIMER_ID){
        TorrentCheckPlaybackStall();
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
        if(g_hasMedia && g_player && !g_pendingResume &&
           libvlc_media_player_get_state(g_player)==libvlc_Error){
            /* Keep the failed queue entry selected: pressing Play retries it,
               while Next or another queue item remains available as recovery. */
            g_hasMedia=FALSE;g_endHandled=TRUE;g_pendingResume=FALSE;
            g_videoBoundsValid=FALSE;ResetArtwork();
            if(g_visualizerActive)ABM_AudioVisualizer_Stop();
            g_visualizerActive=FALSE;g_visualizerTried=FALSE;
            if(g_video&&!g_fullscreen)ShowWindow(g_video,SW_HIDE);
            if(g_selectedRenderer)
                SetStatus(L"Cast failed - select Play to retry or choose another receiver; no local fallback");
            else
                SetStatus(L"Playback failed - select Play to retry or choose another file; check the media and VLC plugins");
            InvalidateRect(hwnd,NULL,FALSE);
        }
        if(g_pendingResume && g_player){
            libvlc_state_t resumeState=libvlc_media_player_get_state(g_player);
            if(resumeState==libvlc_Playing){
                if(g_pendingResumeMs>0)libvlc_media_player_set_time(g_player,g_pendingResumeMs);
                if(g_pendingResumePause)libvlc_media_player_set_pause(g_player,1);
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
        int visible=(R_LIST.bottom>R_LIST.top)?(R_LIST.bottom-R_LIST.top)/80:0;
        int maxScroll=CountFilteredMedia()-visible;if(maxScroll<0)maxScroll=0;
        int wheelSteps=delta/WHEEL_DELTA;
        g_playlistScroll-=wheelSteps*3;
        if(g_playlistScroll<0)g_playlistScroll=0;
        if(g_playlistScroll>maxScroll)g_playlistScroll=maxScroll;
        InvalidateRect(hwnd,&R_SIDEBAR,FALSE);return 0;
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
            if(Inside(R_STOP,x,y)){
                ULONGLONG stopStart=GetTickCount64();
                FullscreenDiag("fullscreen-stop-begin",0);
                if(g_torrentPlaybackActive||g_torrentStreamThread)TorrentStopPlayback();
                libvlc_media_player_stop(g_player);
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
        if(!g_fullscreen&&!g_drawerOpen&&Inside(R_DRAWER_BUTTON,x,y)){g_drawerOpen=TRUE;g_drawerSettingsOpen=FALSE;g_receiverPopup=FALSE;g_searchFocused=FALSE;g_menuOpen=FALSE;SyncRendererComboVisibility();InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
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
        if(g_drawerOpen&&!g_fullscreen){RECT settingsHit=R_SETTINGS_BUTTON;InflateRect(&settingsHit,8,8);if(Inside(settingsHit,x,y)){/* The header is an expand affordance, not a toggle-to-dismiss control. Once open, keep settings open; the drawer itself closes only on an outside click. */if(!g_drawerSettingsOpen){g_drawerSettingsOpen=TRUE;g_receiverPopup=FALSE;g_searchFocused=FALSE;UpdateLayout(hwnd);InvalidateRect(hwnd,NULL,FALSE);}ReleaseCapture();return 0;}}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_SEARCH,x,y)){g_searchFocused=TRUE;SetFocus(hwnd);InvalidateRect(hwnd,&R_SEARCH,FALSE);ReleaseCapture();return 0;}
        if(!g_fullscreen&&g_searchFocused&&!Inside(R_SEARCH,x,y)){g_searchFocused=FALSE;InvalidateRect(hwnd,&R_SEARCH,FALSE);}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_CLEAR_PLAYLIST,x,y)){if(g_playlistCount&&MessageBoxW(hwnd,L"Remove every item from the playlist? The source files will not be deleted.",APP_NAME,MB_YESNO|MB_ICONQUESTION)==IDYES)ClearPlaylist();ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_TAB_QUEUE,x,y)){g_libraryTab=0;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_TAB_LIBRARY,x,y)){g_libraryTab=1;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_FILTER_ALL,x,y)){g_mediaFilter=0;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_FILTER_AUDIO,x,y)){g_mediaFilter=1;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_FILTER_VIDEO,x,y)){g_mediaFilter=2;g_playlistScroll=0;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
        if(g_drawerOpen&&!g_fullscreen&&Inside(R_GROUP,x,y)){g_groupMode=(g_groupMode+1)%3;InvalidateRect(hwnd,&R_SIDEBAR,FALSE);ReleaseCapture();return 0;}
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
            g_drawerOpen=FALSE;g_drawerSettingsOpen=FALSE;g_receiverPopup=FALSE;g_searchFocused=FALSE;
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
        if(g_menuOpen&&g_menuProgress>=80) {
            for(int mi=0;mi<18;mi++)if(Inside(R_MENU_ITEMS[mi],x,y)) {
                g_menuOpen=FALSE;
                PositionVideoWindow();
                SyncRendererComboVisibility();
                if(mi==0)AddFiles();
                else if(mi==1)StartDiscovery();
                else if(mi==2){StartDiscovery();MessageBoxW(hwnd,L"Choose your TV in the Playback Output selector. If media is already playing, the player will restart it on the selected receiver.",L"Cast to TV",MB_OK|MB_ICONINFORMATION);}
                else if(mi==3){if(g_hasMedia){if(libvlc_media_player_get_state(g_player)==libvlc_Playing)libvlc_media_player_set_pause(g_player,1);else libvlc_media_player_play(g_player);}}
                else if(mi==4)ToggleFullscreen(hwnd);
                else if(mi==5)LoadPlaylist();
                else if(mi==6)SavePlaylist();
                else if(mi==7){POINT rp={x,y};ShowRecentMenu(hwnd,rp.x,rp.y);}
                else if(mi==8){g_shuffle=!g_shuffle;SavePersistentState();}
                else if(mi==9){g_repeat=!g_repeat;SavePersistentState();}
                else if(mi==10)ShowEqualizer();
                else if(mi==11)ABM_Streaming_Show(hwnd);
                else if(mi==12)ShowTorrentWindow(hwnd);
                else if(mi==13)ShowHelpDialog(hwnd);
                else if(mi==14)ABM_CheckForUpdates(hwnd,APP_VERSION);
                else if(mi==15)ShowSettingsDialog(hwnd);
                else if(mi==16)PostMessageW(hwnd,WM_CLOSE,0,0);
                else if(mi==17)ShowAboutDialog(hwnd);
                InvalidateRect(hwnd,NULL,FALSE);return 0;
            }
        }
        SetCapture(hwnd);
        if(Inside(R_OPEN,x,y)){AddFiles();ReleaseCapture();return 0;}
        if(Inside(R_PLAY,x,y)){if(g_hasMedia){if(libvlc_media_player_get_state(g_player)==libvlc_Playing)libvlc_media_player_set_pause(g_player,1);else libvlc_media_player_play(g_player);}else if(g_playlistCount)PlayIndex(g_selected<0?0:g_selected);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(Inside(R_STOP,x,y)){
            ULONGLONG stopStart=GetTickCount64();
            FullscreenDiag("windowed-stop-begin",0);
            if(g_torrentPlaybackActive||g_torrentStreamThread)TorrentStopPlayback();
            libvlc_media_player_stop(g_player);
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
        if(g_receiverPopup){g_receiverPopup=FALSE;InvalidateRect(hwnd,NULL,FALSE);}
        if(y>=R_SEEK.top-12&&y<=R_SEEK.bottom+14){g_dragSeek=1;SeekAt(x);ReleaseCapture();return 0;}
        if(y>=R_VOLUME.top-12&&y<=R_VOLUME.bottom+12){g_dragVolume=1;int v=(x-R_VOLUME.left)*100/(R_VOLUME.right-R_VOLUME.left);if(v<0)v=0;if(v>100)v=100;g_volume=v;libvlc_audio_set_volume(g_player,v);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
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
            if(wp==VK_DOWN||wp==VK_TAB){g_menuSelection=(g_menuSelection+1)%18;g_hoverMenuItem=-1;InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
            if(wp==VK_UP){g_menuSelection=(g_menuSelection+17)%18;g_hoverMenuItem=-1;InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
            if(wp==VK_HOME){g_menuSelection=0;g_hoverMenuItem=-1;InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
            if(wp==VK_END){g_menuSelection=17;g_hoverMenuItem=-1;InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
            if(wp==VK_RETURN){if(g_menuProgress<80)return 0;int chosen=g_menuSelection;POINT p={R_MENU_ITEMS[chosen].left+12,R_MENU_ITEMS[chosen].top+12};SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(p.x,p.y));SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(p.x,p.y));return 0;}
        }
        if(wp==VK_F10&&!shift){g_menuOpen=!g_menuOpen;if(g_menuOpen)g_menuSelection=0;g_hoverMenuItem=-1;PositionVideoWindow();SyncRendererComboVisibility();InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
        if(ctrl && !shift && wp=='O'){AddFiles();return 0;}
        if(ctrl && !shift && wp=='D'){StartDiscovery();return 0;}
        if(ctrl && !shift && wp=='E'){ShowEqualizer();return 0;}
        if(ctrl && shift && wp=='C'){StartDiscovery();MessageBoxW(hwnd,L"Choose your TV in the Playback Output selector. If media is already playing, the player will restart it on the selected receiver.",L"Cast to TV",MB_OK|MB_ICONINFORMATION);return 0;}
        if(ctrl && !shift && wp=='L'){LoadPlaylist();return 0;}
        if(ctrl && !shift && wp=='S'){SavePlaylist();return 0;}
        if(ctrl && !shift && wp=='I'){ShowAboutDialog(hwnd);return 0;}
        if(ctrl && shift && wp=='U'){ABM_CheckForUpdates(hwnd,APP_VERSION);return 0;}
        if(ctrl && !shift && wp==VK_OEM_COMMA){ShowSettingsDialog(hwnd);return 0;}
        if(ctrl && shift && wp=='S'){ABM_Streaming_Show(hwnd);return 0;}
        if(ctrl && shift && wp=='T'){ShowTorrentWindow(hwnd);return 0;}
        if(ctrl && shift && wp=='H'){g_shuffle=!g_shuffle;SavePersistentState();InvalidateRect(hwnd,NULL,FALSE);return 0;}
        if(ctrl && !shift && wp=='R'){g_repeat=!g_repeat;SavePersistentState();InvalidateRect(hwnd,NULL,FALSE);return 0;}
        if(ctrl && !shift && wp=='J'){POINT p;GetCursorPos(&p);ShowRecentMenu(hwnd,p.x,p.y);return 0;}
        if(wp==VK_F1){ShowHelpDialog(hwnd);return 0;}
        if(g_searchFocused&&wp==VK_ESCAPE){g_searchFocused=FALSE;InvalidateRect(hwnd,&R_SEARCH,FALSE);return 0;}
        if(wp==VK_ESCAPE&&g_drawerOpen){g_drawerOpen=FALSE;g_drawerSettingsOpen=FALSE;g_receiverPopup=FALSE;g_searchFocused=FALSE;SyncRendererComboVisibility();InvalidateRect(hwnd,NULL,FALSE);return 0;}
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
        if(wp==VK_ESCAPE&&g_menuOpen){g_menuOpen=FALSE;PositionVideoWindow();SyncRendererComboVisibility();InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
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
        if(g_menuOpen&&g_menuProgress>=35){for(int mi=0;mi<18;mi++)if(Inside(R_MENU_ITEMS[mi],mx,my)){menuHot=mi;break;}}
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
            int visible=(R_LIST.bottom>R_LIST.top)?(R_LIST.bottom-R_LIST.top)/80:0;
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
            if(Inside(R_MENU_BUTTON,pt.x,pt.y)||Inside(R_DRAWER_BUTTON,pt.x,pt.y)||
               Inside(R_OPEN,pt.x,pt.y)||Inside(R_PLAY,pt.x,pt.y)||Inside(R_STOP,pt.x,pt.y)||
               Inside(R_PREV,pt.x,pt.y)||Inside(R_NEXT,pt.x,pt.y)||Inside(R_SCAN,pt.x,pt.y)){
                SetCursor(LoadCursorW(NULL,IDC_HAND));return TRUE;
            }
        }
        break;
    case WM_LBUTTONUP:
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
        TorrentDetachPlayerEvents();
        if(g_torrentStreamThread) {
            InterlockedExchange(&g_torrentStreamCancel,1);
            if(g_torrentStopHttpStream)g_torrentStopHttpStream();
            WaitForSingleObject(g_torrentStreamThread,INFINITE);
            CloseHandle(g_torrentStreamThread);g_torrentStreamThread=NULL;
            if(g_torrentStreamRequest){free(g_torrentStreamRequest);g_torrentStreamRequest=NULL;}
        }
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
        ABM_Streaming_Close();ABM_Cast_StreamDiscoveryStop();SavePersistentState();SaveAudioPreferences();if(g_eqWindow)DestroyWindow(g_eqWindow);DragAcceptFiles(hwnd,FALSE);KillTimer(hwnd,TIMER_ID);ABM_AudioVisualizer_Stop();ResetArtwork();ABM_D2D_Destroy(&g_ui);g_main=NULL;StopDiscovery();if(g_streamCastPlayer){libvlc_media_player_stop(g_streamCastPlayer);libvlc_media_player_release(g_streamCastPlayer);g_streamCastPlayer=NULL;}if(g_streamSelectedRenderer){libvlc_renderer_item_release(g_streamSelectedRenderer);g_streamSelectedRenderer=NULL;}libvlc_media_player_stop(g_player);libvlc_media_player_release(g_player);libvlc_release(g_vlc);PostQuitMessage(0);return 0;
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
    InitializeCriticalSection(&g_rendererLock);
    g_videoBrush=CreateSolidBrush(RGB(0,0,0));g_comboBrush=CreateSolidBrush(C_PANEL2);
    g_font=CreateFontW(18,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Variable Text");
    g_smallFont=CreateFontW(13,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Variable Text");
    g_headingFont=CreateFontW(30,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Variable Display");
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
    const char *args[6]={"--no-video-title-show","--quiet",NULL,NULL,NULL,NULL};
    /* Keep LibVLC plugin caching enabled. Disabling its cache forces a full
       plugin scan on every launch and makes startup unnecessarily slow. */
    int argCount=2;
    if(pluginArg[0])args[argCount++]=pluginArg;
    wchar_t hwMode[32]=L"";
    GetEnvironmentVariableW(L"ABM_VLC_HW",hwMode,(DWORD)_countof(hwMode));
    if(!_wcsicmp(hwMode,L"none"))args[argCount++]="--avcodec-hw=none";
    else if(!_wcsicmp(hwMode,L"d3d11va"))args[argCount++]="--avcodec-hw=d3d11va";
    g_vlc=libvlc_new(argCount,args);
    if(!g_vlc){MessageBoxW(NULL,L"Could not initialize LibVLC. Check the VLC installation and plugins.",APP_NAME,MB_ICONERROR);return 1;}
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

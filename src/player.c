#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <vlc/vlc.h>
#include <vlc/libvlc_renderer_discoverer.h>
#include <vlc/libvlc_events.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>

#define APP_NAME L"ABM AV Player | Product Owner: Abhisek Basu Mullick"
#define ID_OPEN 1001
#define ID_PLAY 1002
#define ID_STOP 1003
#define ID_SEEK 1004
#define ID_VOL 1005
#define ID_VIDEO 1006
#define ID_TITLE 1007
#define ID_TIME 1008
#define ID_RENDERERS 1010
#define ID_SCAN 1011
#define WM_RENDERERS_CHANGED (WM_APP + 10)
#define MAX_RENDERERS 32
#define MAX_DISCOVERERS 16
#define TIMER_ID 1

static libvlc_instance_t *g_vlc;
static libvlc_media_player_t *g_player;
static HWND g_video, g_title, g_time, g_owner;
static HWND g_open, g_play, g_stop, g_seek, g_volume, g_renderers, g_scan;
static HWND g_mainWindow;
static CRITICAL_SECTION g_rendererLock;
static libvlc_renderer_discoverer_t *g_discoverers[MAX_DISCOVERERS];
static size_t g_discovererCount;
static libvlc_renderer_item_t *g_rendererItems[MAX_RENDERERS], *g_comboItems[MAX_RENDERERS];
static size_t g_rendererCount, g_comboCount;
static libvlc_renderer_item_t *g_selectedRenderer;
static void SetStatus(const wchar_t *text);
static HFONT g_font, g_titleFont;
static HBRUSH g_background;
static BOOL g_draggingSeek = FALSE;
static BOOL g_hasMedia = FALSE;
static int g_volumeValue = 70;


static void RendererEvent(const libvlc_event_t *event, void *opaque)
{
    (void)opaque;
    libvlc_renderer_item_t *item = event->type == libvlc_RendererDiscovererItemAdded
        ? event->u.renderer_discoverer_item_added.item
        : event->u.renderer_discoverer_item_deleted.item;
    if (!item) return;
    EnterCriticalSection(&g_rendererLock);
    if (event->type == libvlc_RendererDiscovererItemAdded) {
        BOOL found = FALSE;
        for (size_t i = 0; i < g_rendererCount; ++i)
            if (g_rendererItems[i] == item) found = TRUE;
        if (!found && g_rendererCount < MAX_RENDERERS)
            g_rendererItems[g_rendererCount++] = libvlc_renderer_item_hold(item);
    } else {
        for (size_t i = 0; i < g_rendererCount; ++i) {
            if (g_rendererItems[i] == item) {
                libvlc_renderer_item_release(g_rendererItems[i]);
                for (size_t j = i + 1; j < g_rendererCount; ++j)
                    g_rendererItems[j - 1] = g_rendererItems[j];
                --g_rendererCount;
                break;
            }
        }
    }
    LeaveCriticalSection(&g_rendererLock);
    if (g_mainWindow) PostMessageW(g_mainWindow, WM_RENDERERS_CHANGED, 0, 0);
}

static void RefreshRendererList(void)
{
    for (size_t i = 0; i < g_comboCount; ++i)
        libvlc_renderer_item_release(g_comboItems[i]);
    g_comboCount = 0;
    SendMessageW(g_renderers, CB_RESETCONTENT, 0, 0);
    SendMessageW(g_renderers, CB_ADDSTRING, 0, (LPARAM)L"No receiver selected");
    EnterCriticalSection(&g_rendererLock);
    for (size_t i = 0; i < g_rendererCount && g_comboCount < MAX_RENDERERS; ++i) {
        libvlc_renderer_item_t *item = libvlc_renderer_item_hold(g_rendererItems[i]);
        const char *name = libvlc_renderer_item_name(item);
        wchar_t wide[256] = L"Network media receiver";
        if (name) MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 256);
        int index = (int)SendMessageW(g_renderers, CB_ADDSTRING, 0, (LPARAM)wide);
        if (index != CB_ERR && index != CB_ERRSPACE) g_comboItems[g_comboCount++] = item;
        else libvlc_renderer_item_release(item);
    }
    LeaveCriticalSection(&g_rendererLock);
    SendMessageW(g_renderers, CB_SETCURSEL, 0, 0);
    if (g_selectedRenderer)
        for (size_t i = 0; i < g_comboCount; ++i)
            if (g_comboItems[i] == g_selectedRenderer)
                SendMessageW(g_renderers, CB_SETCURSEL, (WPARAM)(i + 1), 0);
}

static void StartRendererDiscovery(void)
{
    if (g_discovererCount) {
        SetStatus(L"Searching for network receivers...");
        return;
    }
    libvlc_rd_description_t **services = NULL;
    size_t count = libvlc_renderer_discoverer_list_get(g_vlc, &services);
    if (!count || !services) {
        SetStatus(L"No renderer discovery services are available in this VLC build.");
        return;
    }
    for (size_t i = 0; i < count && g_discovererCount < MAX_DISCOVERERS; ++i) {
        libvlc_renderer_discoverer_t *d =
            libvlc_renderer_discoverer_new(g_vlc, services[i]->psz_name);
        if (!d) continue;
        libvlc_event_manager_t *em = libvlc_renderer_discoverer_event_manager(d);
        libvlc_event_attach(em, libvlc_RendererDiscovererItemAdded, RendererEvent, NULL);
        libvlc_event_attach(em, libvlc_RendererDiscovererItemDeleted, RendererEvent, NULL);
        if (libvlc_renderer_discoverer_start(d) == 0)
            g_discoverers[g_discovererCount++] = d;
        else {
            libvlc_event_detach(em, libvlc_RendererDiscovererItemAdded, RendererEvent, NULL);
            libvlc_event_detach(em, libvlc_RendererDiscovererItemDeleted, RendererEvent, NULL);
            libvlc_renderer_discoverer_release(d);
        }
    }
    libvlc_renderer_discoverer_list_release(services, count);
    SetStatus(g_discovererCount
        ? L"Searching for Google Cast and supported network receivers..."
        : L"Receiver discovery failed. Check the VLC plugins installation.");
}

static void SelectRenderer(void)
{
    int selected = (int)SendMessageW(g_renderers, CB_GETCURSEL, 0, 0);
    if (selected <= 0 || (size_t)(selected - 1) >= g_comboCount) {
        if (g_selectedRenderer) libvlc_renderer_item_release(g_selectedRenderer);
        g_selectedRenderer = NULL;
        SetStatus(L"Local playback selected.");
        return;
    }
    if (g_hasMedia) {
        SetStatus(L"Select a receiver before opening media. Restart the player to change an active playback route.");
        return;
    }
    if (g_selectedRenderer) libvlc_renderer_item_release(g_selectedRenderer);
    g_selectedRenderer = libvlc_renderer_item_hold(g_comboItems[selected - 1]);
    const char *type = libvlc_renderer_item_type(g_selectedRenderer);
    SetStatus(type && strcmp(type, "chromecast") == 0
        ? L"Google Cast receiver selected. Open a video to cast."
        : L"Network receiver selected. Open media to stream.");
}

static void StopRendererDiscovery(void)
{
    for (size_t i = 0; i < g_discovererCount; ++i) {
        libvlc_event_manager_t *em = libvlc_renderer_discoverer_event_manager(g_discoverers[i]);
        libvlc_event_detach(em, libvlc_RendererDiscovererItemAdded, RendererEvent, NULL);
        libvlc_event_detach(em, libvlc_RendererDiscovererItemDeleted, RendererEvent, NULL);
        libvlc_renderer_discoverer_stop(g_discoverers[i]);
        libvlc_renderer_discoverer_release(g_discoverers[i]);
    }
    g_discovererCount = 0;
    for (size_t i = 0; i < g_comboCount; ++i)
        libvlc_renderer_item_release(g_comboItems[i]);
    g_comboCount = 0;
    if (g_selectedRenderer) libvlc_renderer_item_release(g_selectedRenderer);
    g_selectedRenderer = NULL;
    EnterCriticalSection(&g_rendererLock);
    for (size_t i = 0; i < g_rendererCount; ++i)
        libvlc_renderer_item_release(g_rendererItems[i]);
    g_rendererCount = 0;
    LeaveCriticalSection(&g_rendererLock);
}

static void SetStatus(const wchar_t *text)
{
    if (g_time) SetWindowTextW(g_time, text);
}

static void FormatClock(int64_t milliseconds, wchar_t *out, size_t count)
{
    if (milliseconds < 0) milliseconds = 0;
    int64_t seconds = milliseconds / 1000;
    swprintf_s(out, count, L"%02lld:%02lld",
               (long long)(seconds / 60), (long long)(seconds % 60));
}

static void UpdatePlaybackUI(void)
{
    if (!g_player) return;

    libvlc_state_t state = libvlc_media_player_get_state(g_player);
    int64_t length = libvlc_media_player_get_length(g_player);
    int64_t current = libvlc_media_player_get_time(g_player);

    if (length > 0 && !g_draggingSeek) {
        int pos = (int)((current * 1000) / length);
        if (pos < 0) pos = 0;
        if (pos > 1000) pos = 1000;
        SendMessageW(g_seek, TBM_SETPOS, TRUE, pos);
    }

    wchar_t a[32], b[32], line[80];
    FormatClock(current, a, 32);
    FormatClock(length, b, 32);
    swprintf_s(line, 80, L"%s  /  %s", a, b);
    SetStatus(line);

    SetWindowTextW(g_play, state == libvlc_Playing ? L"Pause" : L"Play");
    if (state == libvlc_Ended) SetStatus(L"Playback finished");
}

static void SetVolume(int value)
{
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    g_volumeValue = value;
    if (g_player) libvlc_audio_set_volume(g_player, value);
}

static void OpenMedia(HWND owner)
{
    OPENFILENAMEW ofn;
    wchar_t path[32768] = L"";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = path;
    ofn.nMaxFile = (DWORD)(sizeof(path) / sizeof(path[0]));
    ofn.lpstrFilter =
        L"Media files\0*.mp3;*.wav;*.flac;*.m4a;*.aac;*.ogg;*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.webm\0"
        L"Audio files\0*.mp3;*.wav;*.flac;*.m4a;*.aac;*.ogg\0"
        L"Video files\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.webm\0"
        L"All files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                OFN_EXPLORER | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"Open media";
    if (!GetOpenFileNameW(&ofn)) return;

    char utf8Path[32768];
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8Path,
                                (int)sizeof(utf8Path), NULL, NULL);
    if (n <= 0) {
        MessageBoxW(owner, L"Could not convert the selected path.",
                    APP_NAME, MB_ICONERROR);
        return;
    }

    libvlc_media_t *media = libvlc_media_new_path(g_vlc, utf8Path);
    if (!media) {
        MessageBoxW(owner, L"LibVLC could not create a media object.",
                    APP_NAME, MB_ICONERROR);
        return;
    }

    libvlc_media_player_stop(g_player);
    if (g_selectedRenderer && libvlc_media_player_set_renderer(g_player, g_selectedRenderer) != 0) {
        libvlc_media_release(media);
        MessageBoxW(owner, L"Could not select this receiver. Restart ABM AV Player and select the receiver before opening media.", APP_NAME, MB_ICONERROR);
        return;
    }
    libvlc_media_player_set_media(g_player, media);
    libvlc_media_release(media);
    g_hasMedia = TRUE;

    const wchar_t *leaf = wcsrchr(path, L'\\');
    leaf = leaf ? leaf + 1 : path;
    SetWindowTextW(g_title, leaf);
    libvlc_audio_set_volume(g_player, g_volumeValue);

    if (libvlc_media_player_play(g_player) != 0) {
        MessageBoxW(owner,
            L"Playback could not start. Check the VLC installation and plugins.",
            APP_NAME, MB_ICONERROR);
        SetStatus(L"Unable to start playback");
        return;
    }

    SendMessageW(g_seek, TBM_SETPOS, TRUE, 0);
    SetWindowTextW(g_play, L"Pause");
    SetStatus(L"Loading media...");
}

static void TogglePlayPause(void)
{
    if (!g_player || !g_hasMedia) {
        SetStatus(L"Open an audio or video file first.");
        return;
    }

    libvlc_state_t state = libvlc_media_player_get_state(g_player);
    if (state == libvlc_Playing) {
        libvlc_media_player_set_pause(g_player, 1);
        SetWindowTextW(g_play, L"Play");
    } else {
        if (state == libvlc_Ended) libvlc_media_player_stop(g_player);
        if (libvlc_media_player_play(g_player) == 0)
            SetWindowTextW(g_play, L"Pause");
    }
}

static void StopPlayback(void)
{
    if (!g_player) return;
    libvlc_media_player_stop(g_player);
    SendMessageW(g_seek, TBM_SETPOS, TRUE, 0);
    SetWindowTextW(g_play, L"Play");
    SetStatus(L"Stopped");
}

static void SeekToTrackbar(void)
{
    if (!g_player) return;
    int64_t length = libvlc_media_player_get_length(g_player);
    if (length <= 0) return;
    int pos = (int)SendMessageW(g_seek, TBM_GETPOS, 0, 0);
    if (pos < 0) pos = 0;
    if (pos > 1000) pos = 1000;
    libvlc_media_player_set_time(g_player, (length * pos) / 1000);
    UpdatePlaybackUI();
}

static HWND MakeControl(HWND parent, const wchar_t *className,
                        const wchar_t *text, DWORD style,
                        int x, int y, int w, int h, int id)
{
    HWND hwnd = CreateWindowExW(0, className, text,
        WS_CHILD | WS_VISIBLE | style, x, y, w, h,
        parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (hwnd) SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
    return hwnd;
}

static void Layout(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    int bottom = 220;
    int videoHeight = h - bottom;
    if (videoHeight < 120) videoHeight = 120;

    MoveWindow(g_video, 18, 18, w - 36, videoHeight - 12, TRUE);
    MoveWindow(g_title, 24, videoHeight + 8, w - 48, 28, TRUE);
    MoveWindow(g_seek, 24, videoHeight + 42, w - 48, 28, TRUE);

    int bw = 100, gap = 12, total = bw * 3 + gap * 2;
    int start = (w - total) / 2;
    MoveWindow(g_open, start, videoHeight + 82, bw, 38, TRUE);
    MoveWindow(g_play, start + bw + gap, videoHeight + 82, bw, 38, TRUE);
    MoveWindow(g_stop, start + (bw + gap) * 2, videoHeight + 82, bw, 38, TRUE);
    MoveWindow(g_renderers, 24, videoHeight + 128, w - 160, 30, TRUE);
    MoveWindow(g_scan, w - 124, videoHeight + 128, 100, 30, TRUE);
    MoveWindow(g_owner, 24, videoHeight + 166, w - 48, 20, TRUE);
    MoveWindow(g_time, 24, h - 25, 160, 20, TRUE);
    MoveWindow(g_volume, w - 210, h - 30, 180, 28, TRUE);
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
    {
        g_video = MakeControl(hwnd, L"STATIC", L"", SS_BLACKRECT | WS_CLIPCHILDREN,
                              18, 18, 700, 400, ID_VIDEO);
        g_title = MakeControl(hwnd, L"STATIC", L"No media selected",
                              SS_LEFT | SS_CENTERIMAGE, 24, 0, 700, 28, ID_TITLE);
        g_seek = MakeControl(hwnd, TRACKBAR_CLASSW, L"",
                             TBS_HORZ | TBS_NOTICKS, 24, 0, 700, 28, ID_SEEK);
        SendMessageW(g_seek, TBM_SETRANGE, TRUE, MAKELONG(0, 1000));
        g_open = MakeControl(hwnd, L"BUTTON", L"Open", BS_PUSHBUTTON,
                             0, 0, 100, 38, ID_OPEN);
        g_play = MakeControl(hwnd, L"BUTTON", L"Play", BS_PUSHBUTTON,
                             0, 0, 100, 38, ID_PLAY);
        g_stop = MakeControl(hwnd, L"BUTTON", L"Stop", BS_PUSHBUTTON,
                             0, 0, 100, 38, ID_STOP);
        g_time = MakeControl(hwnd, L"STATIC", L"00:00  /  00:00",
                             SS_LEFT | SS_CENTERIMAGE, 24, 0, 160, 20, ID_TIME);
        g_renderers = MakeControl(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL, 24, 0, 500, 240, ID_RENDERERS);
        g_scan = MakeControl(hwnd, L"BUTTON", L"Find TVs", BS_PUSHBUTTON, 0, 0, 100, 30, ID_SCAN);
        g_owner = MakeControl(hwnd, L"STATIC",
            L"Product Owner: Abhisek Basu Mullick  |  \u00A9 2026 Abhisek Basu Mullick. All rights reserved.",
            SS_LEFT | SS_CENTERIMAGE, 24, 0, 800, 20, 1009);
        g_volume = MakeControl(hwnd, TRACKBAR_CLASSW, L"",
                               TBS_HORZ | TBS_NOTICKS, 0, 0, 180, 28, ID_VOL);
        SendMessageW(g_volume, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(g_volume, TBM_SETPOS, TRUE, g_volumeValue);

        HWND controls[] = {g_video, g_title, g_seek, g_open, g_play,
                           g_stop, g_time, g_volume, g_owner, g_renderers, g_scan};
        for (unsigned i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
            if (!controls[i]) return -1;
            if (g_font) SendMessageW(controls[i], WM_SETFONT, (WPARAM)g_font, TRUE);
        }
        if (g_titleFont)
            SendMessageW(g_title, WM_SETFONT, (WPARAM)g_titleFont, TRUE);

        g_mainWindow = hwnd;
        RefreshRendererList();
        libvlc_media_player_set_hwnd(g_player, g_video);
        SetTimer(hwnd, TIMER_ID, 250, NULL);
        Layout(hwnd);
        return 0;
    }
    case WM_SIZE:
        Layout(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_OPEN: OpenMedia(hwnd); return 0;
        case ID_PLAY: TogglePlayPause(); return 0;
        case ID_STOP: StopPlayback(); return 0;
        case ID_SCAN: StartRendererDiscovery(); return 0;
        case ID_RENDERERS:
            if (HIWORD(wp) == CBN_SELCHANGE) SelectRenderer();
            return 0;
        }
        break;
    case WM_HSCROLL:
        if ((HWND)lp == g_volume) {
            SetVolume((int)SendMessageW(g_volume, TBM_GETPOS, 0, 0));
        } else if ((HWND)lp == g_seek) {
            int code = LOWORD(wp);
            if (code == TB_THUMBTRACK) g_draggingSeek = TRUE;
            else if (code == TB_ENDTRACK || code == TB_THUMBPOSITION) {
                g_draggingSeek = FALSE;
                SeekToTrackbar();
            }
        }
        return 0;
    case WM_RENDERERS_CHANGED:
        RefreshRendererList();
        return 0;
    case WM_TIMER:
        if (wp == TIMER_ID) UpdatePlaybackUI();
        return 0;
    case WM_CTLCOLORSTATIC:
    {
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(225, 230, 240));
        SetBkColor(dc, RGB(22, 25, 32));
        return (LRESULT)g_background;
    }
    case WM_ERASEBKGND:
    {
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect((HDC)wp, &r, g_background);
        return 1;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(125, 145, 170));
        RECT label = {24, r.bottom - 25, 190, r.bottom};
        DrawTextW(dc, L"ABM AV PLAYER", -1, &label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        StopRendererDiscovery();
        g_mainWindow = NULL;
        if (g_player) {
            libvlc_media_player_stop(g_player);
            libvlc_media_player_release(g_player);
            g_player = NULL;
        }
        if (g_vlc) {
            libvlc_release(g_vlc);
            g_vlc = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous,
                    PWSTR commandLine, int show)
{
    (void)previous;
    (void)commandLine;

/* Copyright (c) 2026 Abhisek Basu Mullick. All rights reserved.
   Product Owner: Abhisek Basu Mullick. See COPYRIGHT.md and LICENSE. */

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    InitializeCriticalSection(&g_rendererLock);
    g_background = CreateSolidBrush(RGB(22, 25, 32));
    g_font = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_titleFont = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    const char *vlcArgs[] = {"--no-video-title-show", "--quiet"};
    g_vlc = libvlc_new(2, vlcArgs);
    if (!g_vlc) {
        MessageBoxW(NULL,
            L"Could not initialize LibVLC. Install 64-bit VLC and check its plugins folder.",
            APP_NAME, MB_ICONERROR);
        DeleteCriticalSection(&g_rendererLock);
        return 1;
    }

    g_player = libvlc_media_player_new(g_vlc);
    if (!g_player) {
        libvlc_release(g_vlc);
        DeleteCriticalSection(&g_rendererLock);
        MessageBoxW(NULL, L"Could not create the media player.",
                    APP_NAME, MB_ICONERROR);
        return 1;
    }

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"ABMAVPlayerWindow";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = g_background;
    if (!RegisterClassW(&wc)) {
        libvlc_media_player_release(g_player);
        libvlc_release(g_vlc);
        return 1;
    }

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, APP_NAME,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 720,
        NULL, NULL, instance, NULL);
    if (!hwnd) {
        libvlc_media_player_release(g_player);
        libvlc_release(g_vlc);
        return 1;
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_titleFont) DeleteObject(g_titleFont);
    if (g_font) DeleteObject(g_font);
    if (g_background) DeleteObject(g_background);
    DeleteCriticalSection(&g_rendererLock);
    return (int)msg.wParam;
}

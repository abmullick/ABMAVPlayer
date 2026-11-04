#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <vlc/vlc.h>
#include <vlc/libvlc_renderer_discoverer.h>
#include <vlc/libvlc_events.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>

#define APP_NAME L"ABM AV Player"
#define OWNER_TEXT L"Product Owner: Abhisek Basu Mullick  |  © 2026 Abhisek Basu Mullick. All rights reserved."
#define ID_RENDERERS 1010
#define WM_RENDERERS_CHANGED (WM_APP + 10)
#define TIMER_ID 1
#define MAX_RENDERERS 32
#define MAX_DISCOVERERS 16
#define MAX_PLAYLIST 256
#define PATH_CAP 1024

static libvlc_instance_t *g_vlc;
static libvlc_media_player_t *g_player;
static HWND g_video, g_renderers, g_main;
static HFONT g_font, g_smallFont, g_headingFont;
static HBRUSH g_videoBrush;
static CRITICAL_SECTION g_rendererLock;
static libvlc_renderer_discoverer_t *g_discoverers[MAX_DISCOVERERS];
static size_t g_discovererCount;
static libvlc_renderer_item_t *g_rendererItems[MAX_RENDERERS], *g_comboItems[MAX_RENDERERS];
static size_t g_rendererCount, g_comboCount;
static libvlc_renderer_item_t *g_selectedRenderer;
static wchar_t g_playlist[MAX_PLAYLIST][PATH_CAP];
static int g_playlistCount, g_current = -1, g_selected = -1;
static int g_volume = 70, g_dragSeek = 0, g_dragVolume = 0, g_hover = 0;
static int g_duration = 0;
static RECT R_VIDEO, R_SIDEBAR, R_SEEK, R_PLAY, R_PREV, R_NEXT, R_STOP, R_OPEN, R_SCAN, R_VOLUME;
static RECT R_ROW[MAX_PLAYLIST];
static BOOL g_hasMedia = FALSE;

enum { HIT_NONE, HIT_OPEN, HIT_PLAY, HIT_STOP, HIT_PREV, HIT_NEXT, HIT_SEEK, HIT_VOLUME, HIT_SCAN, HIT_ROW_BASE = 1000 };

static COLORREF C_BG = RGB(15,18,24);
static COLORREF C_PANEL = RGB(24,29,38);
static COLORREF C_PANEL2 = RGB(31,38,49);
static COLORREF C_TEXT = RGB(238,242,248);
static COLORREF C_MUTED = RGB(145,157,174);
static COLORREF C_ACCENT = RGB(67,143,255);
static COLORREF C_LINE = RGB(47,56,70);

static void Fill(HDC dc, RECT r, COLORREF c) { HBRUSH b=CreateSolidBrush(c); FillRect(dc,&r,b); DeleteObject(b); }
static void RoundBox(HDC dc, RECT r, COLORREF c, int radius) { HBRUSH b=CreateSolidBrush(c); HPEN p=CreatePen(PS_SOLID,1,c); HGDIOBJ ob=SelectObject(dc,b), op=SelectObject(dc,p); RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius); SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(b); DeleteObject(p); }
static void Txt(HDC dc, const wchar_t *s, RECT r, COLORREF c, HFONT f, UINT flags) { SetBkMode(dc,TRANSPARENT); SetTextColor(dc,c); HGDIOBJ old=SelectObject(dc,f); DrawTextW(dc,s,-1,&r,flags); SelectObject(dc,old); }
static BOOL Inside(RECT r,int x,int y) { return x>=r.left && x<r.right && y>=r.top && y<r.bottom; }
static void SetStatus(const wchar_t *s) { (void)s; if(g_main) InvalidateRect(g_main,NULL,FALSE); }

static void FormatTime(int ms,wchar_t *out,size_t n) {
    if(ms<0) ms=0; int sec=ms/1000;
    swprintf_s(out,n,L"%02d:%02d",sec/60,sec%60);
}
static void UpdateLayout(HWND hwnd) {
    RECT c; GetClientRect(hwnd,&c); int w=c.right,h=c.bottom;
    int side=292, left=24, top=78, bottom=158, gap=18;
    int videoW=w-side- left*2-gap; if(videoW<280) videoW=280;
    int videoH=h-top-bottom; if(videoH<150) videoH=150;
    R_VIDEO=(RECT){left,top,left+videoW,top+videoH};
    R_SIDEBAR=(RECT){R_VIDEO.right+gap,top,w-left,h-bottom};
    MoveWindow(g_video,R_VIDEO.left,R_VIDEO.top,videoW,videoH,TRUE);
    MoveWindow(g_renderers,R_SIDEBAR.left+14,R_SIDEBAR.top+66,side-28,30,TRUE);
    R_SEEK=(RECT){left,h-126,w-left,h-120};
    R_PREV=(RECT){w/2-118,h-94,w/2-70,h-46};
    R_PLAY=(RECT){w/2-48,h-100,w/2+48,h-40};
    R_NEXT=(RECT){w/2+70,h-94,w/2+118,h-46};
    R_STOP=(RECT){w/2+140,h-94,w/2+188,h-46};
    R_OPEN=(RECT){left,h-96,left+112,h-48};
    R_SCAN=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+18,R_SIDEBAR.left+side-28,R_SIDEBAR.top+54};
    R_VOLUME=(RECT){w-220,h-78,w-48,h-70};
    int y=R_SIDEBAR.top+118;
    for(int i=0;i<MAX_PLAYLIST;i++) R_ROW[i]=(RECT){R_SIDEBAR.left+10,y+i*48,R_SIDEBAR.right-10,y+(i+1)*48};
}
static void DrawIconButton(HDC dc,RECT r,int kind,BOOL hot,BOOL active) {
    COLORREF bg=active?C_ACCENT:(hot?RGB(49,61,79):C_PANEL2);
    RoundBox(dc,r,bg,14);
    HPEN p=CreatePen(PS_SOLID,2,C_TEXT); HGDIOBJ old=SelectObject(dc,p);
    int cx=(r.left+r.right)/2,cy=(r.top+r.bottom)/2;
    if(kind==1){ POINT q[3]={{cx-6,cy-10},{cx+10,cy},{cx-6,cy+10}}; HBRUSH b=CreateSolidBrush(C_TEXT); HGDIOBJ ob=SelectObject(dc,b); Polygon(dc,q,3); SelectObject(dc,ob); DeleteObject(b); }
    else if(kind==2){ MoveToEx(dc,cx-6,cy-10,NULL); LineTo(dc,cx-6,cy+10); MoveToEx(dc,cx+6,cy-10,NULL); LineTo(dc,cx+6,cy+10); }
    else if(kind==3){ MoveToEx(dc,cx-8,cy-8,NULL); LineTo(dc,cx+8,cy+8); MoveToEx(dc,cx+8,cy-8,NULL); LineTo(dc,cx-8,cy+8); }
    else if(kind==4){ MoveToEx(dc,cx-8,cy-8,NULL); LineTo(dc,cx+5,cy); LineTo(dc,cx-8,cy+8); MoveToEx(dc,cx+7,cy-8,NULL); LineTo(dc,cx+7,cy+8); }
    else if(kind==5){ MoveToEx(dc,cx+8,cy-8,NULL); LineTo(dc,cx-5,cy); LineTo(dc,cx+8,cy+8); MoveToEx(dc,cx-7,cy-8,NULL); LineTo(dc,cx-7,cy+8); }
    SelectObject(dc,old); DeleteObject(p);
}
static void DrawUI(HDC dc,RECT client) {
    Fill(dc,client,C_BG);
    RECT header={0,0,client.right,62}; Fill(dc,header,RGB(19,23,31));
    RECT logo={24,17,46,39}; RoundBox(dc,logo,C_ACCENT,7);
    RECT mark={logo.left+6,logo.top+5,logo.right-5,logo.bottom-5}; Txt(dc,L"▶",mark,RGB(255,255,255),g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RECT brand={58,10,420,36}; Txt(dc,L"ABM AV PLAYER",brand,C_TEXT,g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT sub={58,34,440,54}; Txt(dc,L"MEDIA, WITHOUT COMPROMISE",sub,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT open=R_OPEN; RoundBox(dc,open,C_PANEL2,12); Txt(dc,L"+   Add media",open,C_TEXT,g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RoundBox(dc,R_SIDEBAR,C_PANEL,16);
    RECT st={R_SIDEBAR.left+14,R_SIDEBAR.top+14,R_SIDEBAR.right-14,R_SIDEBAR.top+38};
    Txt(dc,L"OUTPUT DEVICE",st,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RoundBox(dc,R_SCAN,C_PANEL2,10); Txt(dc,L"⌁   Find TVs",R_SCAN,C_TEXT,g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    RECT playlistHead={R_SIDEBAR.left+14,R_SIDEBAR.top+108,R_SIDEBAR.right-14,R_SIDEBAR.top+138};
    Txt(dc,L"UP NEXT",playlistHead,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    wchar_t count[48]; swprintf_s(count,48,L"%d TRACKS",g_playlistCount);
    RECT cr={R_SIDEBAR.right-110,playlistHead.top,R_SIDEBAR.right-14,playlistHead.bottom};
    Txt(dc,count,cr,C_MUTED,g_smallFont,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    int visible=(client.bottom-R_SIDEBAR.top-160)/48; if(visible<0)visible=0; if(visible>MAX_PLAYLIST)visible=MAX_PLAYLIST;
    for(int i=0;i<g_playlistCount && i<visible;i++) {
        RECT row=R_ROW[i];
        if(i==g_selected) RoundBox(dc,row,RGB(39,57,82),9);
        RECT num={row.left+9,row.top+4,row.left+34,row.bottom-4};
        wchar_t ix[12]; swprintf_s(ix,12,L"%02d",i+1);
        Txt(dc,ix,num,i==g_current?C_ACCENT:C_MUTED,g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        const wchar_t *leaf=wcsrchr(g_playlist[i],L'\\'); leaf=leaf?leaf+1:g_playlist[i];
        RECT name={row.left+40,row.top+4,row.right-8,row.bottom-4};
        Txt(dc,leaf,name,i==g_selected?C_TEXT:RGB(194,202,214),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    RECT videoHint=R_VIDEO;
    if(!g_hasMedia) {
        RECT center=videoHint; center.top+=videoHint.bottom-videoHint.top>180?(videoHint.bottom-videoHint.top)/2-30:20;
        Txt(dc,L"ABM",center,RGB(45,54,68),g_headingFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        center.top+=32; center.bottom=center.top+24;
        Txt(dc,L"Open a file or add media to your playlist",center,C_MUTED,g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    RECT timeline={R_SEEK.left,R_SEEK.top-12,R_SEEK.right,R_SEEK.bottom+14};
    Fill(dc,timeline,C_BG);
    int pos=0; int64_t len=g_player?libvlc_media_player_get_length(g_player):0;
    int64_t now=g_player?libvlc_media_player_get_time(g_player):0;
    if(len>0) { pos=(int)((now*1000)/len); if(pos<0)pos=0;if(pos>1000)pos=1000; }
    RECT track={R_SEEK.left,R_SEEK.top,R_SEEK.right,R_SEEK.bottom}; RoundBox(dc,track,C_LINE,5);
    RECT prog=track; prog.right=prog.left+(prog.right-prog.left)*pos/1000; if(prog.right<prog.left+2)prog.right=prog.left+2; RoundBox(dc,prog,C_ACCENT,5);
    int thumb=R_SEEK.left+(R_SEEK.right-R_SEEK.left)*pos/1000;
    RECT dot={thumb-6,R_SEEK.top-4,thumb+6,R_SEEK.bottom+4}; RoundBox(dc,dot,C_TEXT,8);
    wchar_t t1[24],t2[24],times[64]; FormatTime((int)now,t1,24); FormatTime((int)len,t2,24); swprintf_s(times,64,L"%s  /  %s",t1,t2);
    RECT tr={24,client.bottom-44,180,client.bottom-20}; Txt(dc,times,tr,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    DrawIconButton(dc,R_PREV,5,FALSE,FALSE);
    DrawIconButton(dc,R_PLAY,libvlc_media_player_get_state(g_player)==libvlc_Playing?2:1,FALSE,TRUE);
    DrawIconButton(dc,R_NEXT,4,FALSE,FALSE);
    DrawIconButton(dc,R_STOP,3,FALSE,FALSE);
    RECT volLabel={client.right-286,client.bottom-96,client.right-224,client.bottom-74};
    Txt(dc,L"VOLUME",volLabel,C_MUTED,g_smallFont,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    RECT vr={R_VOLUME.left,R_VOLUME.top-2,R_VOLUME.right,R_VOLUME.bottom+2}; RoundBox(dc,vr,C_LINE,5);
    vr.right=vr.left+(vr.right-vr.left)*g_volume/100; RoundBox(dc,vr,C_ACCENT,5);
    int vx=R_VOLUME.left+(R_VOLUME.right-R_VOLUME.left)*g_volume/100;
    RECT vd={vx-5,R_VOLUME.top-5,vx+5,R_VOLUME.bottom+5}; RoundBox(dc,vd,C_TEXT,7);
    RECT footer={client.right-420,client.bottom-44,client.right-24,client.bottom-20};
    Txt(dc,L"© 2026 ABHISEK BASU MULLICK",footer,RGB(92,104,122),g_smallFont,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
}
static void RefreshRendererList(void) {
    for(size_t i=0;i<g_comboCount;i++) libvlc_renderer_item_release(g_comboItems[i]);
    g_comboCount=0; SendMessageW(g_renderers,CB_RESETCONTENT,0,0);
    SendMessageW(g_renderers,CB_ADDSTRING,0,(LPARAM)L"Local playback");
    EnterCriticalSection(&g_rendererLock);
    for(size_t i=0;i<g_rendererCount && g_comboCount<MAX_RENDERERS;i++) {
        libvlc_renderer_item_t *it=libvlc_renderer_item_hold(g_rendererItems[i]);
        const char *name=libvlc_renderer_item_name(it); wchar_t wide[256]=L"Network receiver";
        if(name) MultiByteToWideChar(CP_UTF8,0,name,-1,wide,256);
        int ix=(int)SendMessageW(g_renderers,CB_ADDSTRING,0,(LPARAM)wide);
        if(ix!=CB_ERR && ix!=CB_ERRSPACE) g_comboItems[g_comboCount++]=it; else libvlc_renderer_item_release(it);
    }
    LeaveCriticalSection(&g_rendererLock); SendMessageW(g_renderers,CB_SETCURSEL,0,0);
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
    if(g_discovererCount){SetStatus(L"Searching");return;}
    libvlc_rd_description_t **services=NULL; size_t n=libvlc_renderer_discoverer_list_get(g_vlc,&services);
    if(!n||!services){MessageBoxW(g_main,L"No renderer discovery services are available in this VLC build.",APP_NAME,MB_ICONINFORMATION);return;}
    for(size_t i=0;i<n&&g_discovererCount<MAX_DISCOVERERS;i++) {
        libvlc_renderer_discoverer_t *d=libvlc_renderer_discoverer_new(g_vlc,services[i]->psz_name); if(!d)continue;
        libvlc_event_manager_t *em=libvlc_renderer_discoverer_event_manager(d);
        libvlc_event_attach(em,libvlc_RendererDiscovererItemAdded,RendererEvent,NULL);
        libvlc_event_attach(em,libvlc_RendererDiscovererItemDeleted,RendererEvent,NULL);
        if(libvlc_renderer_discoverer_start(d)==0)g_discoverers[g_discovererCount++]=d;
        else {libvlc_event_detach(em,libvlc_RendererDiscovererItemAdded,RendererEvent,NULL);libvlc_event_detach(em,libvlc_RendererDiscovererItemDeleted,RendererEvent,NULL);libvlc_renderer_discoverer_release(d);}
    }
    libvlc_renderer_discoverer_list_release(services,n);
    SetStatus(L"Searching for Google Cast and supported receivers");
}
static void SelectRenderer(void) {
    int s=(int)SendMessageW(g_renderers,CB_GETCURSEL,0,0);
    if(g_selectedRenderer){libvlc_renderer_item_release(g_selectedRenderer);g_selectedRenderer=NULL;}
    if(s>0&&(size_t)(s-1)<g_comboCount) {
        if(g_hasMedia){MessageBoxW(g_main,L"Select a receiver before opening media.",APP_NAME,MB_ICONINFORMATION);SendMessageW(g_renderers,CB_SETCURSEL,0,0);return;}
        g_selectedRenderer=libvlc_renderer_item_hold(g_comboItems[s-1]);
    }
}
static void StopDiscovery(void) {
    for(size_t i=0;i<g_discovererCount;i++){libvlc_event_manager_t *em=libvlc_renderer_discoverer_event_manager(g_discoverers[i]);libvlc_event_detach(em,libvlc_RendererDiscovererItemAdded,RendererEvent,NULL);libvlc_event_detach(em,libvlc_RendererDiscovererItemDeleted,RendererEvent,NULL);libvlc_renderer_discoverer_stop(g_discoverers[i]);libvlc_renderer_discoverer_release(g_discoverers[i]);}
    g_discovererCount=0; for(size_t i=0;i<g_comboCount;i++)libvlc_renderer_item_release(g_comboItems[i]);g_comboCount=0;
    if(g_selectedRenderer)libvlc_renderer_item_release(g_selectedRenderer);g_selectedRenderer=NULL;
    EnterCriticalSection(&g_rendererLock);for(size_t i=0;i<g_rendererCount;i++)libvlc_renderer_item_release(g_rendererItems[i]);g_rendererCount=0;LeaveCriticalSection(&g_rendererLock);
}
static void PlayIndex(int index) {
    if(index<0||index>=g_playlistCount)return;
    char utf8[PATH_CAP*4]; int n=WideCharToMultiByte(CP_UTF8,0,g_playlist[index],-1,utf8,sizeof(utf8),NULL,NULL); if(n<=0)return;
    libvlc_media_t *m=libvlc_media_new_path(g_vlc,utf8); if(!m){MessageBoxW(g_main,L"LibVLC could not open this file.",APP_NAME,MB_ICONERROR);return;}
    libvlc_media_player_stop(g_player);
    if(g_selectedRenderer&&libvlc_media_player_set_renderer(g_player,g_selectedRenderer)!=0){libvlc_media_release(m);MessageBoxW(g_main,L"Could not select the network receiver.",APP_NAME,MB_ICONERROR);return;}
    libvlc_media_player_set_media(g_player,m);libvlc_media_release(m);
    if(libvlc_media_player_play(g_player)!=0){MessageBoxW(g_main,L"Playback could not start. Check the VLC plugins.",APP_NAME,MB_ICONERROR);return;}
    g_current=index;g_selected=index;g_hasMedia=TRUE;InvalidateRect(g_main,NULL,FALSE);
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
static void SeekAt(int x) {
    int64_t len=libvlc_media_player_get_length(g_player);if(len<=0)return;
    int width=R_SEEK.right-R_SEEK.left;int p=x-R_SEEK.left;if(p<0)p=0;if(p>width)p=width;
    libvlc_media_player_set_time(g_player,(len*p)/width);
}
static void UpdateUI(void) { if(g_main)InvalidateRect(g_main,NULL,FALSE); }
static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        g_main=hwnd; g_video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_BLACKRECT,0,0,640,360,hwnd,NULL,GetModuleHandleW(NULL),NULL);
        g_renderers=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,0,0,250,220,hwnd,(HMENU)ID_RENDERERS,GetModuleHandleW(NULL),NULL);
        SendMessageW(g_renderers,CB_ADDSTRING,0,(LPARAM)L"Local playback");SendMessageW(g_renderers,CB_SETCURSEL,0,0);
        SetWindowTheme(g_renderers,L"DarkMode_Explorer",NULL);
        libvlc_media_player_set_hwnd(g_player,g_video);UpdateLayout(hwnd);SetTimer(hwnd,TIMER_ID,33,NULL);return 0;
    }
    case WM_SIZE:UpdateLayout(hwnd);InvalidateRect(hwnd,NULL,FALSE);return 0;
    case WM_MEASUREITEM: if(((LPMEASUREITEMSTRUCT)lp)->CtlID==ID_RENDERERS){((LPMEASUREITEMSTRUCT)lp)->itemHeight=26;return TRUE;}break;
    case WM_DRAWITEM: if(((LPDRAWITEMSTRUCT)lp)->CtlID==ID_RENDERERS){LPDRAWITEMSTRUCT d=(LPDRAWITEMSTRUCT)lp;Fill(d->hDC,d->rcItem,(d->itemState&ODS_SELECTED)?RGB(48,67,91):C_PANEL2);if(d->itemID!=UINT(-1)){wchar_t t[256];SendMessageW(g_renderers,CB_GETLBTEXT,d->itemID,(LPARAM)t);RECT r=d->rcItem;r.left+=10;Txt(d->hDC,t,r,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);}return TRUE;}break;
    case WM_CTLCOLORLISTBOX:SetTextColor((HDC)wp,C_TEXT);SetBkColor((HDC)wp,C_PANEL2);return (LRESULT)CreateSolidBrush(C_PANEL2);
    case WM_COMMAND:if(LOWORD(wp)==ID_RENDERERS&&HIWORD(wp)==CBN_SELCHANGE){SelectRenderer();return 0;}break;
    case WM_RENDERERS_CHANGED:RefreshRendererList();return 0;
    case WM_TIMER:if(wp==TIMER_ID)UpdateUI();return 0;
    case WM_LBUTTONDOWN: {
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);SetCapture(hwnd);
        if(Inside(R_OPEN,x,y)){AddFiles();ReleaseCapture();return 0;}
        if(Inside(R_PLAY,x,y)){if(g_hasMedia){if(libvlc_media_player_get_state(g_player)==libvlc_Playing)libvlc_media_player_set_pause(g_player,1);else libvlc_media_player_play(g_player);}else if(g_playlistCount)PlayIndex(g_selected<0?0:g_selected);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(Inside(R_STOP,x,y)){libvlc_media_player_stop(g_player);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(Inside(R_NEXT,x,y)){if(g_playlistCount)PlayIndex((g_current+1)%g_playlistCount);ReleaseCapture();return 0;}
        if(Inside(R_PREV,x,y)){if(g_playlistCount)PlayIndex(g_current>0?g_current-1:0);ReleaseCapture();return 0;}
        if(Inside(R_SCAN,x,y)){StartDiscovery();ReleaseCapture();return 0;}
        if(y>=R_SEEK.top-12&&y<=R_SEEK.bottom+14){g_dragSeek=1;SeekAt(x);ReleaseCapture();return 0;}
        if(y>=R_VOLUME.top-12&&y<=R_VOLUME.bottom+12){g_dragVolume=1;int v=(x-R_VOLUME.left)*100/(R_VOLUME.right-R_VOLUME.left);if(v<0)v=0;if(v>100)v=100;g_volume=v;libvlc_audio_set_volume(g_player,v);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        for(int i=0;i<g_playlistCount&&i<MAX_PLAYLIST;i++)if(Inside(R_ROW[i],x,y)){g_selected=i;if(GetTickCount()-g_hover<400&&g_hover==(DWORD)(i+1))PlayIndex(i);g_hover=(int)(i+1);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        ReleaseCapture();return 0;
    }
    case WM_MOUSEMOVE: if(g_dragSeek)SeekAt(GET_X_LPARAM(lp));if(g_dragVolume){int v=(GET_X_LPARAM(lp)-R_VOLUME.left)*100/(R_VOLUME.right-R_VOLUME.left);if(v<0)v=0;if(v>100)v=100;g_volume=v;libvlc_audio_set_volume(g_player,v);InvalidateRect(hwnd,NULL,FALSE);}return 0;
    case WM_LBUTTONUP:g_dragSeek=0;g_dragVolume=0;ReleaseCapture();return 0;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);DrawUI(dc,r);EndPaint(hwnd,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_CTLCOLORSTATIC:SetBkColor((HDC)wp,C_BG);SetTextColor((HDC)wp,C_TEXT);return (LRESULT)g_videoBrush;
    case WM_DESTROY:KillTimer(hwnd,TIMER_ID);g_main=NULL;StopDiscovery();libvlc_media_player_stop(g_player);libvlc_media_player_release(g_player);libvlc_release(g_vlc);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE prev,PWSTR cmd,int show) {
    (void)prev;(void)cmd;INITCOMMONCONTROLSEX icc={sizeof(icc),ICC_STANDARD_CLASSES};InitCommonControlsEx(&icc);
    InitializeCriticalSection(&g_rendererLock);
    g_videoBrush=CreateSolidBrush(RGB(0,0,0));
    g_font=CreateFontW(15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    g_smallFont=CreateFontW(11,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    g_headingFont=CreateFontW(20,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    const char *args[]={"--no-video-title-show","--quiet"};g_vlc=libvlc_new(2,args);
    if(!g_vlc){MessageBoxW(NULL,L"Could not initialize LibVLC. Check the VLC installation and plugins.",APP_NAME,MB_ICONERROR);return 1;}
    g_player=libvlc_media_player_new(g_vlc);if(!g_player){libvlc_release(g_vlc);return 1;}
    WNDCLASSW wc={0};wc.lpfnWndProc=WndProc;wc.hInstance=inst;wc.lpszClassName=L"ABMAVPlayerWindow";wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=g_videoBrush;
    if(!RegisterClassW(&wc))return 1;
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"ABM AV Player",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1280,820,NULL,NULL,inst,NULL);
    if(!hwnd)return 1;ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG m;while(GetMessageW(&m,NULL,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
    if(g_headingFont)DeleteObject(g_headingFont);if(g_smallFont)DeleteObject(g_smallFont);if(g_font)DeleteObject(g_font);if(g_videoBrush)DeleteObject(g_videoBrush);DeleteCriticalSection(&g_rendererLock);
    return (int)m.wParam;
}

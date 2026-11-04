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
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>

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
static int g_playlistScroll = 0, g_hoverButton = 0, g_hoverLevel = 0;
static DWORD g_lastClickTick = 0;
static int g_duration = 0;
static int64_t g_lastUiTime = -1;
static RECT R_VIDEO, R_SIDEBAR, R_SEEK, R_PLAY, R_PREV, R_NEXT, R_STOP, R_OPEN, R_SCAN, R_VOLUME, R_RECEIVER, R_RECEIVER_LIST;
static RECT R_ROW[MAX_PLAYLIST];
static BOOL g_hasMedia = FALSE;
static BOOL g_receiverPopup = FALSE;
static BOOL g_menuOpen = FALSE;
static BOOL g_fullscreen = FALSE;
static DWORD_PTR g_savedStyle = 0;
static WINDOWPLACEMENT g_savedPlacement = { sizeof(WINDOWPLACEMENT) };
static int g_menuProgress = 0;
static RECT R_MENU_BUTTON, R_MENU_PANEL, R_MENU_ITEMS[12], R_FULLSCREEN;

enum { HIT_NONE, HIT_OPEN, HIT_PLAY, HIT_STOP, HIT_PREV, HIT_NEXT, HIT_SEEK, HIT_VOLUME, HIT_SCAN, HIT_ROW_BASE = 1000 };

static COLORREF C_BG = RGB(15,18,24);
static COLORREF C_PANEL = RGB(24,29,38);
static COLORREF C_PANEL2 = RGB(31,38,49);
static COLORREF C_TEXT = RGB(238,242,248);
static COLORREF C_MUTED = RGB(177,189,205);
static COLORREF C_ACCENT = RGB(67,143,255);
static COLORREF C_LINE = RGB(47,56,70);

static COLORREF Blend(COLORREF a,COLORREF b,int amount);
static void RoundBox(HDC dc, RECT r, COLORREF c, int radius);
static void Fill(HDC dc, RECT r, COLORREF c) { (void)dc;ABM_D2D_Fill(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,c); }
static void GradientRect(HDC dc, RECT r, COLORREF top, COLORREF bottom) { (void)dc;ABM_D2D_Fill(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,Blend(top,bottom,45)); }
static void RoundBox(HDC dc, RECT r, COLORREF c, int radius) { (void)dc;ABM_D2D_Round(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,(float)radius,c); }
static void Panel(HDC dc,RECT r,COLORREF fill,COLORREF edge,int radius) { (void)dc;ABM_D2D_Round(&g_ui,(float)r.left,(float)r.top,(float)r.right,(float)r.bottom,(float)radius,fill);ABM_D2D_StrokeRound(&g_ui,(float)r.left+0.5f,(float)r.top+0.5f,(float)r.right-0.5f,(float)r.bottom-0.5f,(float)radius,edge,1.0f); }
static void DrawLine(HDC dc,int x1,int y1,int x2,int y2,COLORREF color,int width) { (void)dc;ABM_D2D_Line(&g_ui,(float)x1,(float)y1,(float)x2,(float)y2,color,(float)width); }
static void Txt(HDC dc, const wchar_t *s, RECT r, COLORREF c, HFONT f, UINT flags) { (void)dc;int style=(f==g_smallFont)?1:((f==g_headingFont)?2:0);ABM_D2D_Text(&g_ui,s,r,c,style,flags); }
static BOOL Inside(RECT r,int x,int y) { return x>=r.left && x<r.right && y>=r.top && y<r.bottom; }
static COLORREF Blend(COLORREF a,COLORREF b,int amount) {
    if(amount<0)amount=0;if(amount>100)amount=100;
    return RGB((GetRValue(a)*(100-amount)+GetRValue(b)*amount)/100,
               (GetGValue(a)*(100-amount)+GetGValue(b)*amount)/100,
               (GetBValue(a)*(100-amount)+GetBValue(b)*amount)/100);
}
static void SetStatus(const wchar_t *s) { (void)s; if(g_main) InvalidateRect(g_main,NULL,FALSE); }

static void FormatTime(int ms,wchar_t *out,size_t n) {
    if(ms<0) ms=0; int sec=ms/1000;
    swprintf_s(out,n,L"%02d:%02d",sec/60,sec%60);
}
static void UpdateLayout(HWND hwnd) {
    RECT c; GetClientRect(hwnd,&c); int w=c.right,h=c.bottom;
    int left=24,top=78,bottom=158,gap=18;
    int side=w>=1050?292:(w>=760?250:220);
    int videoW=w-side-left*2-gap; if(videoW<240)videoW=240;
    int videoH=h-top-bottom; if(videoH<120)videoH=120;
    R_VIDEO=(RECT){left,top,left+videoW,top+videoH};
    R_SIDEBAR=(RECT){R_VIDEO.right+gap,top,w-left,h-bottom};
    R_RECEIVER=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+66,R_SIDEBAR.right-14,R_SIDEBAR.top+96};
    R_MENU_BUTTON=(RECT){w-252,15,w-210,53};
    R_MENU_PANEL=(RECT){w-258,62,w-18,62+448};
    for(int i=0;i<12;i++)R_MENU_ITEMS[i]=(RECT){R_MENU_PANEL.left+8,R_MENU_PANEL.top+8+i*36,R_MENU_PANEL.right-8,R_MENU_PANEL.top+42+i*36};
    R_FULLSCREEN=(RECT){R_VIDEO.right-48,R_VIDEO.top+14,R_VIDEO.right-14,R_VIDEO.top+48};
    if(g_fullscreen) {
        R_VIDEO=(RECT){0,0,w,h};
        MoveWindow(g_video,0,0,w,h,TRUE);
        ShowWindow(g_renderers,SW_HIDE);
        return;
    }
    MoveWindow(g_video,R_VIDEO.left,R_VIDEO.top,videoW,videoH,TRUE);
    MoveWindow(g_renderers,R_RECEIVER.left,R_RECEIVER.top,R_RECEIVER.right-R_RECEIVER.left,30,FALSE);
    R_SEEK=(RECT){left,h-126,w-left,h-120};
    R_PREV=(RECT){w/2-118,h-94,w/2-70,h-46};
    R_PLAY=(RECT){w/2-48,h-100,w/2+48,h-40};
    R_NEXT=(RECT){w/2+70,h-94,w/2+118,h-46};
    R_STOP=(RECT){w/2+140,h-94,w/2+188,h-46};
    R_OPEN=(RECT){left,h-96,left+112,h-48};
    R_SCAN=(RECT){R_SIDEBAR.left+14,R_SIDEBAR.top+18,R_SIDEBAR.right-14,R_SIDEBAR.top+54};
    R_VOLUME=(RECT){w-220,h-78,w-48,h-70};
    int y=R_SIDEBAR.top+154;
    for(int i=0;i<MAX_PLAYLIST;i++) R_ROW[i]=(RECT){R_SIDEBAR.left+10,y+(i-g_playlistScroll)*48,R_SIDEBAR.right-10,y+(i-g_playlistScroll+1)*48};
}
static void DrawIconButton(HDC dc,RECT r,int kind,BOOL hot,BOOL active) {
    COLORREF bg=active?C_ACCENT:Blend(C_PANEL2,RGB(57,75,99),hot?g_hoverLevel:0);
    RoundBox(dc,r,bg,14);
    int cx=(r.left+r.right)/2,cy=(r.top+r.bottom)/2;
    if(kind==1) {
        DrawLine(dc,cx-6,cy-10,cx+10,cy,C_TEXT,3);
        DrawLine(dc,cx+10,cy,cx-6,cy+10,C_TEXT,3);
        DrawLine(dc,cx-6,cy+10,cx-6,cy-10,C_TEXT,3);
    } else if(kind==2) {
        DrawLine(dc,cx-6,cy-10,cx-6,cy+10,C_TEXT,3);
        DrawLine(dc,cx+6,cy-10,cx+6,cy+10,C_TEXT,3);
    } else if(kind==3) {
        DrawLine(dc,cx-8,cy-8,cx+8,cy+8,C_TEXT,2);
        DrawLine(dc,cx+8,cy-8,cx-8,cy+8,C_TEXT,2);
    } else if(kind==4) {
        DrawLine(dc,cx-8,cy-8,cx+5,cy,C_TEXT,2);
        DrawLine(dc,cx+5,cy,cx-8,cy+8,C_TEXT,2);
        DrawLine(dc,cx+7,cy-8,cx+7,cy+8,C_TEXT,2);
    } else if(kind==5) {
        DrawLine(dc,cx+8,cy-8,cx-5,cy,C_TEXT,2);
        DrawLine(dc,cx-5,cy,cx+8,cy+8,C_TEXT,2);
        DrawLine(dc,cx-7,cy-8,cx-7,cy+8,C_TEXT,2);
    }
}
static void DrawUI(HDC dc,RECT client) {
    if(g_fullscreen) { Fill(dc,client,RGB(0,0,0)); return; }
    Fill(dc,client,C_BG);
    RECT header={0,0,client.right,66};GradientRect(dc,header,RGB(24,31,44),RGB(16,20,29));
    DrawLine(dc,0,65,client.right,65,RGB(37,48,65),1);

    RECT logo={24,18,54,48};RoundBox(dc,logo,C_ACCENT,9);
    DrawLine(dc,31,26,31,40,RGB(255,255,255),2);
    DrawLine(dc,31,26,43,26,RGB(255,255,255),2);
    DrawLine(dc,43,26,43,40,RGB(255,255,255),2);
    RECT brand={68,10,390,38};Txt(dc,L"ABM AV PLAYER",brand,C_TEXT,g_headingFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT sub={69,37,420,56};Txt(dc,L"YOUR MEDIA. YOUR EXPERIENCE.",sub,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT menuBtn=R_MENU_BUTTON;
    RoundBox(dc,menuBtn,g_menuOpen?RGB(42,67,101):RGB(29,39,54),12);
    for(int i=0;i<3;i++)DrawLine(dc,menuBtn.left+12,menuBtn.top+13+i*7,menuBtn.right-12,menuBtn.top+13+i*7,RGB(226,236,249),2);
    RECT status={client.right-190,21,client.right-24,48};
    RoundBox(dc,status,RGB(26,36,51),16);
    RECT led={status.left+13,status.top+14,status.left+21,status.top+22};RoundBox(dc,led,g_hasMedia?RGB(63,205,145):RGB(100,117,139),5);
    RECT statusText={status.left+29,status.top+2,status.right-8,status.bottom-2};
    Txt(dc,g_hasMedia?L"READY TO PLAY":L"LOCAL PLAYER",statusText,C_TEXT,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    /* Video surface is a separate LibVLC child window. Keep the surrounding frame elegant. */
    RECT frame=R_VIDEO;InflateRect(&frame,2,2);Panel(dc,frame,RGB(8,11,16),RGB(43,54,70),10);
    if(!g_hasMedia) {
        RECT badge={R_VIDEO.left+(R_VIDEO.right-R_VIDEO.left)/2-31,R_VIDEO.top+(R_VIDEO.bottom-R_VIDEO.top)/2-52,
                    R_VIDEO.left+(R_VIDEO.right-R_VIDEO.left)/2+31,R_VIDEO.top+(R_VIDEO.bottom-R_VIDEO.top)/2+10};
        RoundBox(dc,badge,RGB(24,38,58),18);
        DrawLine(dc,(badge.left+badge.right)/2-7,badge.top+16,(badge.left+badge.right)/2-7,badge.bottom-16,C_ACCENT,3);
        DrawLine(dc,(badge.left+badge.right)/2-7,badge.top+16,(badge.left+badge.right)/2+13,(badge.top+badge.bottom)/2, C_ACCENT,3);
        DrawLine(dc,(badge.left+badge.right)/2+13,(badge.top+badge.bottom)/2,(badge.left+badge.right)/2-7,badge.bottom-16,C_ACCENT,3);
        RECT empty={R_VIDEO.left+20,badge.bottom+12,R_VIDEO.right-20,badge.bottom+42};
        Txt(dc,L"Nothing playing right now",empty,RGB(190,202,220),g_font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT hint={R_VIDEO.left+20,empty.bottom,R_VIDEO.right-20,empty.bottom+26};
        Txt(dc,L"Add a file or choose something from your playlist",hint,C_MUTED,g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
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

    /* Sidebar cards */
    Panel(dc,R_SIDEBAR,C_PANEL,RGB(40,49,64),16);
    RECT sideTitle={R_SIDEBAR.left+17,R_SIDEBAR.top+12,R_SIDEBAR.right-16,R_SIDEBAR.top+37};
    Txt(dc,L"PLAYBACK OUTPUT",sideTitle,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT scan=R_SCAN;
    Panel(dc,scan,Blend(RGB(31,57,91),RGB(51,91,145),(g_hoverButton==HIT_SCAN)?g_hoverLevel:0),RGB(61,111,174),10);
    RECT scanIcon={scan.left+14,scan.top+8,scan.left+34,scan.bottom-8};
    DrawLine(dc,scanIcon.left+2,scanIcon.top+8,scanIcon.left+2,scanIcon.bottom-7,RGB(220,236,255),2);
    DrawLine(dc,scanIcon.left+2,scanIcon.top+8,scanIcon.right-2,scanIcon.top+8,RGB(220,236,255),2);
    DrawLine(dc,scanIcon.right-2,scanIcon.top+8,scanIcon.right-2,scanIcon.bottom-7,RGB(220,236,255),2);
    RECT scanText={scan.left+42,scan.top+2,scan.right-10,scan.bottom-2};
    Txt(dc,L"Discover devices",scanText,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    RECT receiver={R_RECEIVER.left,R_RECEIVER.top,R_RECEIVER.right,R_RECEIVER.bottom};
    Panel(dc,receiver,RGB(17,23,33),RGB(57,70,89),9);
    RECT deviceIcon={receiver.left+11,receiver.top+8,receiver.left+29,receiver.bottom-8};
    RoundBox(dc,deviceIcon,RGB(44,69,98),4);
    DrawLine(dc,deviceIcon.left+4,deviceIcon.top+5,deviceIcon.right-4,deviceIcon.top+5,C_ACCENT,2);
    wchar_t deviceName[256]=L"Local playback";
    int sel=(int)SendMessageW(g_renderers,CB_GETCURSEL,0,0);
    if(sel>0)SendMessageW(g_renderers,CB_GETLBTEXT,sel,(LPARAM)deviceName);
    RECT deviceText={receiver.left+38,receiver.top+2,receiver.right-28,receiver.bottom-2};
    Txt(dc,deviceName,deviceText,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    DrawLine(dc,receiver.right-19,receiver.top+12,receiver.right-15,receiver.top+17,C_MUTED,2);
    DrawLine(dc,receiver.right-15,receiver.top+17,receiver.right-11,receiver.top+12,C_MUTED,2);

    RECT listHead={R_SIDEBAR.left+17,R_SIDEBAR.top+111,R_SIDEBAR.right-17,R_SIDEBAR.top+137};
    Txt(dc,L"UP NEXT",listHead,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    wchar_t count[48];swprintf_s(count,48,L"%02d TRACKS",g_playlistCount);
    RECT countR={R_SIDEBAR.right-112,listHead.top,R_SIDEBAR.right-17,listHead.bottom};
    Txt(dc,count,countR,C_MUTED,g_smallFont,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    DrawLine(dc,R_SIDEBAR.left+16,listHead.bottom+4,R_SIDEBAR.right-16,listHead.bottom+4,RGB(43,53,69),1);
    int visible=(R_SIDEBAR.bottom-(R_SIDEBAR.top+154)-18)/48;if(visible<0)visible=0;if(visible>MAX_PLAYLIST)visible=MAX_PLAYLIST;
    for(int i=g_playlistScroll;i<g_playlistCount&&i<g_playlistScroll+visible;i++) {
        RECT row=R_ROW[i];
        if(i==g_selected)Panel(dc,row,RGB(34,54,80),RGB(48,83,125),9);
        RECT indexR={row.left+8,row.top+5,row.left+34,row.bottom-5};
        wchar_t ix[12];swprintf_s(ix,12,L"%02d",i+1);
        Txt(dc,ix,indexR,i==g_current?C_ACCENT:C_MUTED,g_smallFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        const wchar_t *leaf=wcsrchr(g_playlist[i],L'\\');leaf=leaf?leaf+1:g_playlist[i];
        RECT name={row.left+42,row.top+3,row.right-9,row.bottom-3};
        Txt(dc,leaf,name,i==g_selected?C_TEXT:RGB(190,201,216),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    if(g_receiverPopup) {
        int n=(int)SendMessageW(g_renderers,CB_GETCOUNT,0,0);if(n>MAX_RENDERERS+1)n=MAX_RENDERERS+1;
        R_RECEIVER_LIST=(RECT){receiver.left,receiver.bottom+5,receiver.right,receiver.bottom+5+n*32};
        Panel(dc,R_RECEIVER_LIST,RGB(22,29,41),RGB(65,83,108),9);
        for(int i=0;i<n;i++) {
            RECT item={R_RECEIVER_LIST.left+5,R_RECEIVER_LIST.top+3+i*32,R_RECEIVER_LIST.right-5,R_RECEIVER_LIST.top+3+(i+1)*32};
            if(i==sel)RoundBox(dc,item,RGB(39,66,101),6);
            wchar_t label[256]=L"";
            SendMessageW(g_renderers,CB_GETLBTEXT,i,(LPARAM)label);
            RECT tx={item.left+10,item.top,item.right-8,item.bottom};
            Txt(dc,label,tx,i==sel?C_TEXT:RGB(190,201,216),g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
    }

    /* Timeline: thin track, bright progress and a precise thumb. */
    RECT timeline={R_SEEK.left,R_SEEK.top-15,R_SEEK.right,R_SEEK.bottom+16};Fill(dc,timeline,C_BG);
    int pos=0;int64_t len=g_player?libvlc_media_player_get_length(g_player):0;
    int64_t now=g_player?libvlc_media_player_get_time(g_player):0;
    if(len>0){pos=(int)((now*1000)/len);if(pos<0)pos=0;if(pos>1000)pos=1000;}
    RECT track={R_SEEK.left,R_SEEK.top-1,R_SEEK.right,R_SEEK.bottom+1};RoundBox(dc,track,RGB(39,49,65),5);
    RECT prog=track;prog.right=prog.left+(prog.right-prog.left)*pos/1000;if(prog.right>prog.left)RoundBox(dc,prog,C_ACCENT,5);
    int thumb=R_SEEK.left+(R_SEEK.right-R_SEEK.left)*pos/1000;
    RECT halo={thumb-10,R_SEEK.top-10,thumb+10,R_SEEK.bottom+10};RoundBox(dc,halo,RGB(36,66,104),12);
    RECT dot={thumb-5,R_SEEK.top-5,thumb+5,R_SEEK.bottom+5};RoundBox(dc,dot,RGB(235,244,255),10);
    wchar_t t1[24],t2[24],times[64];FormatTime((int)now,t1,24);FormatTime((int)len,t2,24);swprintf_s(times,64,L"%s  /  %s",t1,t2);
    RECT timeR={24,client.bottom-44,180,client.bottom-20};Txt(dc,times,timeR,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    /* Control dock */
    RECT dock={client.right/2-190,client.bottom-108,client.right/2+190,client.bottom-34};
    RoundBox(dc,dock,RGB(20,26,36),19);
    DrawIconButton(dc,R_PREV,5,g_hoverButton==HIT_PREV,FALSE);
    DrawIconButton(dc,R_PLAY,libvlc_media_player_get_state(g_player)==libvlc_Playing?2:1,g_hoverButton==HIT_PLAY,TRUE);
    DrawIconButton(dc,R_NEXT,4,g_hoverButton==HIT_NEXT,FALSE);
    DrawIconButton(dc,R_STOP,3,g_hoverButton==HIT_STOP,FALSE);
    RECT volPanel={client.right-282,client.bottom-101,client.right-22,client.bottom-35};
    RoundBox(dc,volPanel,RGB(20,26,36),15);
    RECT volLabel={volPanel.left+13,volPanel.top+8,volPanel.left+75,volPanel.top+27};
    Txt(dc,L"VOLUME",volLabel,C_MUTED,g_smallFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    RECT vr={R_VOLUME.left,R_VOLUME.top-2,R_VOLUME.right,R_VOLUME.bottom+2};RoundBox(dc,vr,RGB(47,59,77),5);
    vr.right=vr.left+(vr.right-vr.left)*g_volume/100;RoundBox(dc,vr,C_ACCENT,5);
    int vx=R_VOLUME.left+(R_VOLUME.right-R_VOLUME.left)*g_volume/100;
    RECT vd={vx-6,R_VOLUME.top-6,vx+6,R_VOLUME.bottom+6};RoundBox(dc,vd,RGB(235,244,255),8);

    RECT open=R_OPEN;
    Panel(dc,open,Blend(RGB(31,60,97),RGB(53,95,151),(g_hoverButton==HIT_OPEN)?g_hoverLevel:0),RGB(61,111,174),12);
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
        Panel(dc,menu,RGB(25,33,47),RGB(68,86,112),13);
        const wchar_t *labels[12]={L"Open media",L"Find network devices",L"Cast to TV",L"Play / Pause",L"Fullscreen",L"Load playlist",L"Save playlist",L"Help",L"About ABM AV Player",L"Exit player",L"Shuffle: Off",L"Repeat: Off"};
        const wchar_t *icons[12]={L"+",L"⌕",L"TV",L"▶",L"⛶",L"↓",L"↑",L"?",L"i",L"×",L"⇄",L"↻"};
        for(int i=0;i<12;i++) {
            RECT item=R_MENU_ITEMS[i];
            if(i==10)labels[10]=g_shuffle?L"Shuffle: On":L"Shuffle: Off";
            if(i==11)labels[11]=g_repeat?L"Repeat: On":L"Repeat: Off";
            if(item.bottom>menu.bottom)item.bottom=menu.bottom;
            if(item.top>=menu.bottom)continue;
            if(g_menuOpen)RoundBox(dc,item,RGB(31,43,61),7);
            RECT glyph={item.left+10,item.top+2,item.left+34,item.bottom-2};
            Txt(dc,icons[i],glyph,C_ACCENT,g_headingFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            RECT label={item.left+40,item.top+2,item.right-8,item.bottom-2};
            Txt(dc,labels[i],label,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        }
    }
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
    libvlc_renderer_item_t *next=NULL;
    if(s>0&&(size_t)(s-1)<g_comboCount)next=libvlc_renderer_item_hold(g_comboItems[s-1]);
    if(g_selectedRenderer)libvlc_renderer_item_release(g_selectedRenderer);
    g_selectedRenderer=next;

    /* set_renderer only takes effect before play(). Recreate the media player
       when changing the output so an already-playing item can move to the TV. */
    if(g_hasMedia&&g_current>=0&&g_current<g_playlistCount){
        int64_t resumeAt=libvlc_media_player_get_time(g_player);
        BOOL wasPlaying=libvlc_media_player_get_state(g_player)==libvlc_Playing;
        libvlc_media_player_stop(g_player);
        libvlc_media_player_release(g_player);
        g_player=libvlc_media_player_new(g_vlc);
        if(!g_player){g_hasMedia=FALSE;MessageBoxW(g_main,L"Could not recreate the media player.",APP_NAME,MB_ICONERROR);return;}
        libvlc_media_player_set_hwnd(g_player,g_video);
        g_rendererApplied=FALSE;
        if(g_selectedRenderer){
            if(libvlc_media_player_set_renderer(g_player,g_selectedRenderer)!=0){
                MessageBoxW(g_main,L"Could not select the network receiver.",APP_NAME,MB_ICONERROR);
                return;
            }
            g_rendererApplied=TRUE;
        }
        char utf8[PATH_CAP*4];
        if(WideCharToMultiByte(CP_UTF8,0,g_playlist[g_current],-1,utf8,sizeof(utf8),NULL,NULL)>0){
            libvlc_media_t *m=libvlc_media_new_path(g_vlc,utf8);
            if(m){libvlc_media_player_set_media(g_player,m);libvlc_media_release(m);
                if(libvlc_media_player_play(g_player)==0){
                    if(resumeAt>0)libvlc_media_player_set_time(g_player,resumeAt);
                    if(!wasPlaying)libvlc_media_player_set_pause(g_player,1);
                }else MessageBoxW(g_main,L"Playback could not restart on the selected output.",APP_NAME,MB_ICONERROR);
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
static void PlayIndex(int index) {
    if(index<0||index>=g_playlistCount)return;
    char utf8[PATH_CAP*4]; int n=WideCharToMultiByte(CP_UTF8,0,g_playlist[index],-1,utf8,sizeof(utf8),NULL,NULL); if(n<=0)return;
    libvlc_media_t *m=libvlc_media_new_path(g_vlc,utf8); if(!m){MessageBoxW(g_main,L"LibVLC could not open this file.",APP_NAME,MB_ICONERROR);return;}
    libvlc_media_player_stop(g_player);
    if(g_selectedRenderer&&!g_rendererApplied){
        if(libvlc_media_player_set_renderer(g_player,g_selectedRenderer)!=0){libvlc_media_release(m);MessageBoxW(g_main,L"Could not select the network receiver.",APP_NAME,MB_ICONERROR);return;}
        g_rendererApplied=TRUE;
    }
    libvlc_media_player_set_media(g_player,m);libvlc_media_release(m);
    if(libvlc_media_player_play(g_player)!=0){MessageBoxW(g_main,L"Playback could not start. Check the VLC plugins.",APP_NAME,MB_ICONERROR);return;}
    g_current=index;g_selected=index;g_hasMedia=TRUE;g_lastUiTime=-1;
    libvlc_media_player_set_rate(g_player,g_playbackRate);
    InvalidateRect(g_main,NULL,FALSE);
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
    InvalidateRect(g_main,NULL,FALSE);
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
    g_playlistCount--;
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
    g_selected=to;InvalidateRect(g_main,NULL,FALSE);
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
static void ToggleFullscreen(HWND hwnd) {
    if(!g_fullscreen) {
        g_savedStyle=(DWORD_PTR)GetWindowLongPtrW(hwnd,GWL_STYLE);
        g_savedPlacement.length=sizeof(g_savedPlacement);
        GetWindowPlacement(hwnd,&g_savedPlacement);
        MONITORINFO mi={sizeof(mi)};
        HMONITOR monitor=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST);
        GetMonitorInfoW(monitor,&mi);
        g_fullscreen=TRUE;
        g_menuOpen=FALSE;g_receiverPopup=FALSE;
        ShowWindow(g_renderers,SW_HIDE);
        SetWindowLongPtrW(hwnd,GWL_STYLE,(LONG_PTR)(WS_POPUP|WS_VISIBLE));
        SetWindowPos(hwnd,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,
            mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,
            SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    } else {
        g_fullscreen=FALSE;
        SetWindowLongPtrW(hwnd,GWL_STYLE,(LONG_PTR)g_savedStyle);
        SetWindowPlacement(hwnd,&g_savedPlacement);
        SetWindowPos(hwnd,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        ShowWindow(g_renderers,SW_SHOW);
    }
    UpdateLayout(hwnd);
    InvalidateRect(hwnd,NULL,TRUE);
}
static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        g_main=hwnd; if(!ABM_D2D_Init(&g_ui,hwnd)){MessageBoxW(hwnd,L"Direct2D / DirectWrite initialization failed.",APP_NAME,MB_ICONERROR);return -1;} g_video=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_BLACKRECT,0,0,640,360,hwnd,NULL,GetModuleHandleW(NULL),NULL);
        DragAcceptFiles(hwnd,TRUE);
        g_renderers=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,0,0,250,220,hwnd,(HMENU)ID_RENDERERS,GetModuleHandleW(NULL),NULL);
        SendMessageW(g_renderers,CB_ADDSTRING,0,(LPARAM)L"Local playback");SendMessageW(g_renderers,CB_SETCURSEL,0,0);
        SetWindowTheme(g_renderers,L"DarkMode_Explorer",NULL);
        libvlc_media_player_set_hwnd(g_player,g_video);UpdateLayout(hwnd);SetTimer(hwnd,TIMER_ID,33,NULL);return 0;
    }
    case WM_DROPFILES: AddDroppedFiles((HDROP)wp);return 0;
    case WM_SIZE: {
        UpdateLayout(hwnd);
        if(wp!=SIZE_MINIMIZED)ABM_D2D_Resize(&g_ui,(UINT)LOWORD(lp),(UINT)HIWORD(lp));
        InvalidateRect(hwnd,NULL,FALSE);return 0;
    }
    case WM_GETMINMAXINFO: { MINMAXINFO *mi=(MINMAXINFO*)lp; mi->ptMinTrackSize.x=820; mi->ptMinTrackSize.y=560; return 0; }
    case WM_MEASUREITEM: if(((LPMEASUREITEMSTRUCT)lp)->CtlID==ID_RENDERERS){((LPMEASUREITEMSTRUCT)lp)->itemHeight=26;return TRUE;}break;
    case WM_DRAWITEM: if(((LPDRAWITEMSTRUCT)lp)->CtlID==ID_RENDERERS){LPDRAWITEMSTRUCT d=(LPDRAWITEMSTRUCT)lp;Fill(d->hDC,d->rcItem,(d->itemState&ODS_SELECTED)?RGB(48,67,91):C_PANEL2);if(d->itemID!=(UINT)-1){wchar_t t[256];SendMessageW(g_renderers,CB_GETLBTEXT,d->itemID,(LPARAM)t);RECT r=d->rcItem;r.left+=10;Txt(d->hDC,t,r,C_TEXT,g_font,DT_LEFT|DT_VCENTER|DT_SINGLELINE);}return TRUE;}break;
    case WM_CTLCOLORLISTBOX:SetTextColor((HDC)wp,C_TEXT);SetBkColor((HDC)wp,C_PANEL2);return (LRESULT)g_comboBrush;
    case WM_COMMAND:if(LOWORD(wp)==ID_RENDERERS&&HIWORD(wp)==CBN_SELCHANGE){SelectRenderer();return 0;}break;
    case WM_RENDERERS_CHANGED:RefreshRendererList();return 0;
    case WM_TIMER:if(wp==TIMER_ID){
        int oldMenuProgress=g_menuProgress;
        if(g_menuOpen&&g_menuProgress<100)g_menuProgress+=20;
        else if(!g_menuOpen&&g_menuProgress>0)g_menuProgress-=20;
        if(g_menuProgress<0)g_menuProgress=0;if(g_menuProgress>100)g_menuProgress=100;
        int oldLevel=g_hoverLevel;
        if(g_hoverButton&&g_hoverLevel<100)g_hoverLevel+=20;
        else if(!g_hoverButton&&g_hoverLevel>0)g_hoverLevel-=20;
        if(g_hoverLevel<0)g_hoverLevel=0;
        if(g_hoverLevel>100)g_hoverLevel=100;
        if(oldLevel!=g_hoverLevel) {
            InvalidateButton(g_hoverButton);
            if(g_hoverButton==HIT_NONE) {
                InvalidateButton(HIT_OPEN);InvalidateButton(HIT_PLAY);
                InvalidateButton(HIT_STOP);InvalidateButton(HIT_PREV);
                InvalidateButton(HIT_NEXT);InvalidateButton(HIT_SCAN);
            }
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
        int delta=GET_WHEEL_DELTA_WPARAM(wp);
        int visible=(R_SIDEBAR.bottom-(R_SIDEBAR.top+154)-18)/48;
        int maxScroll=g_playlistCount-visible;if(maxScroll<0)maxScroll=0;
        g_playlistScroll-=delta/WHEEL_DELTA;
        if(g_playlistScroll<0)g_playlistScroll=0;
        if(g_playlistScroll>maxScroll)g_playlistScroll=maxScroll;
        InvalidateRect(hwnd,NULL,FALSE);return 0;
    }
    case WM_LBUTTONDOWN: {
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
        if(!g_fullscreen&&Inside(R_MENU_BUTTON,x,y)){
            g_menuOpen=!g_menuOpen;g_receiverPopup=FALSE;
            RECT dirty=R_MENU_PANEL;UnionRect(&dirty,&dirty,&R_MENU_BUTTON);
            InvalidateRect(hwnd,&dirty,FALSE);ReleaseCapture();return 0;
        }
        /* Clicking anywhere outside the open menu dismisses it. Continue
           processing the click so the underlying control still responds. */
        if(g_menuOpen && !Inside(R_MENU_PANEL,x,y)) {
            g_menuOpen=FALSE;
            RECT dirty=R_MENU_PANEL;
            InvalidateRect(hwnd,&dirty,FALSE);
        }
        if(!g_fullscreen&&Inside(R_FULLSCREEN,x,y)){ToggleFullscreen(hwnd);ReleaseCapture();return 0;}
        if(g_menuOpen&&g_menuProgress>=80) {
            for(int mi=0;mi<12;mi++)if(Inside(R_MENU_ITEMS[mi],x,y)) {
                g_menuOpen=FALSE;
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
                    L"Cast to TV: Discover receivers, then choose your TV in Playback Output.\n\n"
                    L"Keyboard shortcuts:\nF11 - Toggle fullscreen\nEsc - Exit fullscreen or close the menu\n"
                    L"Double-click the video - Toggle fullscreen",
                    L"ABM AV Player Help",MB_OK|MB_ICONINFORMATION);
                else if(mi==8)MessageBoxW(hwnd,
                    L"ABM AV Player\nVersion 0.3\n\n"
                    L"A Windows desktop media player powered by LibVLC, Direct2D and DirectWrite.\n\n"
                    L"Product Owner: Abhisek Basu Mullick\n"
                    L"Copyright (c) 2026 Abhisek Basu Mullick. All rights reserved.",
                    L"About ABM AV Player",MB_OK|MB_ICONINFORMATION);
                else if(mi==9)PostMessageW(hwnd,WM_CLOSE,0,0);
                else if(mi==10){g_shuffle=!g_shuffle;}
                else if(mi==11){g_repeat=!g_repeat;}
                InvalidateRect(hwnd,NULL,FALSE);return 0;
            }
        }
        SetCapture(hwnd);
        if(Inside(R_OPEN,x,y)){AddFiles();ReleaseCapture();return 0;}
        if(Inside(R_PLAY,x,y)){if(g_hasMedia){if(libvlc_media_player_get_state(g_player)==libvlc_Playing)libvlc_media_player_set_pause(g_player,1);else libvlc_media_player_play(g_player);}else if(g_playlistCount)PlayIndex(g_selected<0?0:g_selected);InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(Inside(R_STOP,x,y)){libvlc_media_player_stop(g_player);g_hasMedia=FALSE;InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(Inside(R_NEXT,x,y)){if(g_playlistCount)PlayIndex((g_current+1)%g_playlistCount);ReleaseCapture();return 0;}
        if(Inside(R_PREV,x,y)){if(g_playlistCount)PlayIndex(g_current>0?g_current-1:0);ReleaseCapture();return 0;}
        if(Inside(R_SCAN,x,y)){StartDiscovery();ReleaseCapture();return 0;}
        if(Inside(R_RECEIVER,x,y)){g_receiverPopup=!g_receiverPopup;InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        if(g_receiverPopup && Inside(R_RECEIVER_LIST,x,y)) {
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
        for(int i=g_playlistScroll;i<g_playlistCount&&i<MAX_PLAYLIST;i++)if(Inside(R_ROW[i],x,y)){g_selected=i;InvalidateRect(hwnd,NULL,FALSE);ReleaseCapture();return 0;}
        ReleaseCapture();return 0;
    }
    case WM_KEYDOWN:
        if(wp==VK_F11){ToggleFullscreen(hwnd);return 0;}
        if(wp==VK_SPACE&&g_hasMedia){if(libvlc_media_player_get_state(g_player)==libvlc_Playing)libvlc_media_player_set_pause(g_player,1);else libvlc_media_player_play(g_player);return 0;}
        if(wp==VK_RIGHT&&g_hasMedia){int64_t t=libvlc_media_player_get_time(g_player);libvlc_media_player_set_time(g_player,t+10000);return 0;}
        if(wp==VK_LEFT&&g_hasMedia){int64_t t=libvlc_media_player_get_time(g_player);libvlc_media_player_set_time(g_player,t>10000?t-10000:0);return 0;}
        if((wp==VK_OEM_PLUS||wp==VK_ADD)&&g_hasMedia){if(g_playbackRate<2.0f)g_playbackRate+=0.25f;libvlc_media_player_set_rate(g_player,g_playbackRate);InvalidateRect(hwnd,NULL,FALSE);return 0;}
        if((wp==VK_OEM_MINUS||wp==VK_SUBTRACT)&&g_hasMedia){if(g_playbackRate>0.5f)g_playbackRate-=0.25f;libvlc_media_player_set_rate(g_player,g_playbackRate);InvalidateRect(hwnd,NULL,FALSE);return 0;}
        if(wp==VK_HOME&&g_hasMedia){libvlc_media_player_set_time(g_player,0);return 0;}
        if(wp==VK_ESCAPE&&g_fullscreen){ToggleFullscreen(hwnd);return 0;}
        if(wp==VK_ESCAPE&&g_menuOpen){g_menuOpen=FALSE;InvalidateRect(hwnd,&R_MENU_PANEL,FALSE);return 0;}
        break;
    case WM_RBUTTONUP: {
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp),index=-1;
        for(int i=g_playlistScroll;i<g_playlistCount&&i<MAX_PLAYLIST;i++)if(Inside(R_ROW[i],x,y)){index=i;break;}
        if(Inside(R_SIDEBAR,x,y)&&y>=R_SIDEBAR.top+140){ShowPlaylistContextMenu(hwnd,x,y,index);return 0;}
        break;
    }
    case WM_LBUTTONDBLCLK: { int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp); if(!g_fullscreen&&Inside(R_VIDEO,x,y)){ToggleFullscreen(hwnd);return 0;} for(int i=g_playlistScroll;i<g_playlistCount&&i<MAX_PLAYLIST;i++) if(Inside(R_ROW[i],x,y)){PlayIndex(i);return 0;} return 0; }
    case WM_MOUSEMOVE: {
        int mx=GET_X_LPARAM(lp),my=GET_Y_LPARAM(lp),hit=HIT_NONE;
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
        if(g_dragVolume){int v=(mx-R_VOLUME.left)*100/(R_VOLUME.right-R_VOLUME.left);if(v<0)v=0;if(v>100)v=100;g_volume=v;libvlc_audio_set_volume(g_player,v);InvalidateRect(hwnd,NULL,FALSE);}
        return 0;
    }
    case WM_LBUTTONUP:g_dragSeek=0;g_dragVolume=0;ReleaseCapture();return 0;
    case WM_PAINT:{
        PAINTSTRUCT ps;BeginPaint(hwnd,&ps);
        RECT r;GetClientRect(hwnd,&r);
        /* Paint only the invalidated area. A full-target Direct2D Clear on every
           menu animation frame was repainting the whole surface and causing flicker. */
        ABM_D2D_BeginClip(&g_ui,C_BG,&ps.rcPaint);
        DrawUI(NULL,r);
        ABM_D2D_End(&g_ui);
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_ERASEBKGND:return 1;
    case WM_CTLCOLORSTATIC:SetBkColor((HDC)wp,C_BG);SetTextColor((HDC)wp,C_TEXT);return (LRESULT)g_videoBrush;
    case WM_DESTROY:DragAcceptFiles(hwnd,FALSE);KillTimer(hwnd,TIMER_ID);ABM_D2D_Destroy(&g_ui);g_main=NULL;StopDiscovery();libvlc_media_player_stop(g_player);libvlc_media_player_release(g_player);libvlc_release(g_vlc);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE prev,PWSTR cmd,int show) {
    (void)prev;(void)cmd;INITCOMMONCONTROLSEX icc={sizeof(icc),ICC_STANDARD_CLASSES};InitCommonControlsEx(&icc);
    InitializeCriticalSection(&g_rendererLock);
    g_videoBrush=CreateSolidBrush(RGB(0,0,0));g_comboBrush=CreateSolidBrush(C_PANEL2);
    g_font=CreateFontW(17,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    g_smallFont=CreateFontW(14,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    g_headingFont=CreateFontW(22,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    const char *args[]={"--no-video-title-show","--quiet"};g_vlc=libvlc_new(2,args);
    if(!g_vlc){MessageBoxW(NULL,L"Could not initialize LibVLC. Check the VLC installation and plugins.",APP_NAME,MB_ICONERROR);return 1;}
    g_player=libvlc_media_player_new(g_vlc);if(!g_player){libvlc_release(g_vlc);return 1;}
    WNDCLASSW wc={0};wc.lpfnWndProc=WndProc;wc.style=CS_DBLCLKS;wc.hInstance=inst;wc.lpszClassName=L"ABMAVPlayerWindow";wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=g_videoBrush;
    if(!RegisterClassW(&wc))return 1;
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"ABM AV Player",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1440,900,NULL,NULL,inst,NULL);
    if(!hwnd)return 1;ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG m;while(GetMessageW(&m,NULL,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
    if(g_headingFont)DeleteObject(g_headingFont);if(g_smallFont)DeleteObject(g_smallFont);if(g_font)DeleteObject(g_font);if(g_videoBrush)DeleteObject(g_videoBrush);if(g_comboBrush)DeleteObject(g_comboBrush);DeleteCriticalSection(&g_rendererLock);
    return (int)m.wParam;
}

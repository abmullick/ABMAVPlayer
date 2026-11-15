#ifndef ABM_UI_DPI_H
#define ABM_UI_DPI_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Shared DPI helpers. Layout constants are authored in 96-DPI units (DIPs);
   convert only at Win32 HWND boundaries. Functions are inline so C and C++
   translation units can use them without another linker dependency. */

static __inline UINT ABM_DpiGetForWindow(HWND hwnd) {
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    HMODULE user32=GetModuleHandleW(L"user32.dll");
    GetDpiForWindowFn fn=user32?(GetDpiForWindowFn)GetProcAddress(user32,"GetDpiForWindow"):NULL;
    if(fn && hwnd) {
        UINT dpi=fn(hwnd);
        if(dpi) return dpi;
    }
    HDC dc=GetDC(hwnd);
    UINT dpi=dc?(UINT)GetDeviceCaps(dc,LOGPIXELSX):96u;
    if(dc) ReleaseDC(hwnd,dc);
    return dpi?dpi:96u;
}

static __inline UINT ABM_DpiGetSystem(void) {
    typedef UINT (WINAPI *GetDpiForSystemFn)(void);
    HMODULE user32=GetModuleHandleW(L"user32.dll");
    GetDpiForSystemFn fn=user32?(GetDpiForSystemFn)GetProcAddress(user32,"GetDpiForSystem"):NULL;
    if(fn) {
        UINT dpi=fn();
        if(dpi) return dpi;
    }
    HDC dc=GetDC(NULL);
    UINT dpi=dc?(UINT)GetDeviceCaps(dc,LOGPIXELSX):96u;
    if(dc) ReleaseDC(NULL,dc);
    return dpi?dpi:96u;
}

static __inline int ABM_DpiScale(int value, UINT dpi) {
    return (int)(((INT64)value*(INT64)(dpi?dpi:96u)+48)/96);
}

static __inline int ABM_DpiUnscale(int value, UINT dpi) {
    UINT actual=dpi?dpi:96u;
    return (int)(((INT64)value*96+(INT64)actual/2)/(INT64)actual);
}

static __inline BOOL ABM_DpiApplySuggestedRect(HWND hwnd, LPARAM lParam) {
    const RECT *r=(const RECT*)lParam;
    if(!hwnd || !r) return FALSE;
    return SetWindowPos(hwnd,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,
        SWP_NOZORDER|SWP_NOACTIVATE);
}

/* Monitor-aware top-level window geometry. All requested sizes are 96-DPI
   logical units; returned RECT coordinates are physical screen pixels. */
static __inline BOOL ABM_DpiGetWorkArea(HWND anchor, RECT *work) {
    if(!work) return FALSE;
    HMONITOR monitor=MonitorFromWindow(anchor,MONITOR_DEFAULTTONEAREST);
    MONITORINFO info={0}; info.cbSize=sizeof(info);
    if(!monitor || !GetMonitorInfoW(monitor,&info)) {
        if(!SystemParametersInfoW(SPI_GETWORKAREA,0,work,0)) return FALSE;
        return TRUE;
    }
    *work=info.rcWork;
    return TRUE;
}

static __inline BOOL ABM_DpiMakeCenteredRect(HWND owner,int logicalWidth,
                                              int logicalHeight,RECT *rect) {
    if(!rect) return FALSE;
    UINT dpi=ABM_DpiGetForWindow(owner);
    RECT work={0};
    if(!ABM_DpiGetWorkArea(owner,&work)) return FALSE;
    int workW=work.right-work.left, workH=work.bottom-work.top;
    int width=ABM_DpiScale(logicalWidth,dpi);
    int height=ABM_DpiScale(logicalHeight,dpi);
    int maxW=workW-16, maxH=workH-16;
    if(width>maxW) width=maxW;
    if(height>maxH) height=maxH;
    if(width<320) width=(workW>320?workW:320);
    if(height<240) height=(workH>240?workH:240);
    int x=work.left+(workW-width)/2, y=work.top+(workH-height)/2;
    if(owner && IsWindow(owner)) {
        RECT parent={0};
        if(GetWindowRect(owner,&parent)) {
            x=parent.left+((parent.right-parent.left)-width)/2;
            y=parent.top+((parent.bottom-parent.top)-height)/2;
            if(x<work.left+8)x=work.left+8;
            if(y<work.top+8)y=work.top+8;
            if(x+width>work.right-8)x=work.right-width-8;
            if(y+height>work.bottom-8)y=work.bottom-height-8;
        }
    }
    SetRect(rect,x,y,x+width,y+height);
    return TRUE;
}

static __inline void ABM_DpiClampMinTrack(HWND hwnd,MINMAXINFO *info,
                                          int logicalMinWidth,int logicalMinHeight) {
    if(!hwnd || !info) return;
    UINT dpi=ABM_DpiGetForWindow(hwnd);
    RECT work={0};
    if(!ABM_DpiGetWorkArea(hwnd,&work)) return;
    int capW=(work.right-work.left)-32, capH=(work.bottom-work.top)-32;
    int minW=ABM_DpiScale(logicalMinWidth,dpi);
    int minH=ABM_DpiScale(logicalMinHeight,dpi);
    if(minW>capW) minW=capW;
    if(minH>capH) minH=capH;
    if(minW<240) minW=240;
    if(minH<180) minH=180;
    info->ptMinTrackSize.x=minW;
    info->ptMinTrackSize.y=minH;
}

#endif

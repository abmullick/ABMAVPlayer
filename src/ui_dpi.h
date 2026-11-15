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

#endif

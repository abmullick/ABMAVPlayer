#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include <projectM-4/projectM.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <stdint.h>
#include "projectm_ipc.h"

#pragma comment(lib,"opengl32.lib")
namespace {
using CreateContextAttribsFn=HGLRC (WINAPI *)(HDC,HGLRC,const int*);
constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB=0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB=0x2092;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB=0x9126;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB=0x00000001;
constexpr UINT_PTR RENDER_TIMER=1;
HWND g_window=nullptr;
HDC g_dc=nullptr;
HGLRC g_gl=nullptr;
projectm_handle g_projectm=nullptr;
ABMProjectMSharedState *g_shared=nullptr;
HANDLE g_mapping=nullptr;
std::vector<std::string> g_presets;
size_t g_presetIndex=0;
DWORD64 g_lastPresetTick=0;
float g_pcm[ABM_PROJECTM_PCM_COUNT]{};

std::string Utf8(const wchar_t *value) {
    if(!value||!*value)return {};
    int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    if(n<=1)return {};
    std::string out((size_t)n,'\0');
    WideCharToMultiByte(CP_UTF8,0,value,-1,&out[0],n,nullptr,nullptr);
    out.resize((size_t)n-1);return out;
}
void CollectPresets(const std::wstring &directory) {
    WIN32_FIND_DATAW fd{};std::wstring pattern=directory+L"\\*";
    HANDLE find=FindFirstFileW(pattern.c_str(),&fd);
    if(find==INVALID_HANDLE_VALUE)return;
    do {
        if(!wcscmp(fd.cFileName,L".")||!wcscmp(fd.cFileName,L".."))continue;
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)continue;
        std::wstring full=directory+L"\\"+fd.cFileName;
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)CollectPresets(full);
        else {
            const wchar_t *ext=wcsrchr(fd.cFileName,L'.');
            if(ext&&!_wcsicmp(ext,L".milk")){std::string p=Utf8(full.c_str());if(!p.empty())g_presets.push_back(p);}
        }
    } while(FindNextFileW(find,&fd));
    FindClose(find);
}
bool CreateContext(HWND hwnd) {
    g_dc=GetDC(hwnd);if(!g_dc)return false;
    PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;
    pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
    pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;pfd.cAlphaBits=8;
    pfd.cDepthBits=24;pfd.cStencilBits=8;pfd.iLayerType=PFD_MAIN_PLANE;
    int pf=ChoosePixelFormat(g_dc,&pfd);
    if(!pf||!SetPixelFormat(g_dc,pf,&pfd))return false;
    HGLRC bootstrap=wglCreateContext(g_dc);if(!bootstrap)return false;
    if(!wglMakeCurrent(g_dc,bootstrap)){wglDeleteContext(bootstrap);return false;}
    PROC proc=wglGetProcAddress("wglCreateContextAttribsARB");
    if(!proc||proc==(PROC)1||proc==(PROC)2||proc==(PROC)3||proc==(PROC)-1) {
        wglMakeCurrent(nullptr,nullptr);wglDeleteContext(bootstrap);return false;
    }
    const int attrs[]={WGL_CONTEXT_MAJOR_VERSION_ARB,3,WGL_CONTEXT_MINOR_VERSION_ARB,3,
        WGL_CONTEXT_PROFILE_MASK_ARB,WGL_CONTEXT_CORE_PROFILE_BIT_ARB,0};
    HGLRC modern=((CreateContextAttribsFn)proc)(g_dc,nullptr,attrs);
    wglMakeCurrent(nullptr,nullptr);wglDeleteContext(bootstrap);
    if(!modern)return false;
    if(!wglMakeCurrent(g_dc,modern)){wglDeleteContext(modern);return false;}
    g_gl=modern;wglMakeCurrent(nullptr,nullptr);return true;
}
void LoadPresetDelta(int delta) {
    if(!g_projectm)return;
    if(g_presets.empty()){projectm_load_preset_file(g_projectm,"idle://",true);return;}
    long long count=(long long)g_presets.size();
    long long next=((long long)g_presetIndex+(long long)delta)%count;
    if(next<0)next+=count;
    g_presetIndex=(size_t)next;
    projectm_load_preset_file(g_projectm,g_presets[g_presetIndex].c_str(),true);
    g_lastPresetTick=GetTickCount64();
}
void ReadPcm() {
    if(!g_shared)return;
    for(int attempt=0;attempt<4;attempt++) {
        LONG before=InterlockedCompareExchange(&g_shared->sequence,0,0);
        if(before&1)continue;
        CopyMemory(g_pcm,g_shared->pcm,sizeof(g_pcm));
        MemoryBarrier();
        LONG after=InterlockedCompareExchange(&g_shared->sequence,0,0);
        if(before==after&&!(after&1))return;
    }
}
void RenderFrame() {
    if(!g_projectm||!g_gl||!g_dc||!g_shared)return;
    if(!wglMakeCurrent(g_dc,g_gl))return;
    RECT rc{};GetClientRect(g_window,&rc);
    int w=rc.right-rc.left,h=rc.bottom-rc.top;
    if(w>0&&h>0) {
        projectm_set_window_size(g_projectm,(size_t)w,(size_t)h);
        int delta=InterlockedExchange(&g_shared->presetDelta,0);
        if(delta)LoadPresetDelta(delta);
        bool automatic=InterlockedCompareExchange(&g_shared->autoSwitch,0,0)!=0;
        if(automatic&&!g_presets.empty()&&GetTickCount64()-g_lastPresetTick>45000)
            LoadPresetDelta(1);
        ReadPcm();
        projectm_pcm_add_float(g_projectm,g_pcm,ABM_PROJECTM_PCM_COUNT,PROJECTM_MONO);
        projectm_opengl_render_frame(g_projectm);
        SwapBuffers(g_dc);
    }
    wglMakeCurrent(nullptr,nullptr);
}
void Cleanup() {
    if(g_window)KillTimer(g_window,RENDER_TIMER);
    if(g_dc&&g_gl)wglMakeCurrent(g_dc,g_gl);
    if(g_projectm){projectm_destroy(g_projectm);g_projectm=nullptr;}
    if(g_gl){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(g_gl);g_gl=nullptr;}
    if(g_dc&&g_window){ReleaseDC(g_window,g_dc);g_dc=nullptr;}
    if(g_shared) {
        InterlockedExchange(&g_shared->ready,0);
        InterlockedExchangePointer((PVOID volatile*)&g_shared->childWindow,nullptr);
        UnmapViewOfFile(g_shared);g_shared=nullptr;
    }
    if(g_mapping){CloseHandle(g_mapping);g_mapping=nullptr;}
}
LRESULT CALLBACK RendererWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_ERASEBKGND:return 1;
    case WM_TIMER:if(wp==RENDER_TIMER){RenderFrame();return 0;}break;
    case WM_SIZE:return 0; /* dimensions are queried on the render thread */
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:KillTimer(hwnd,RENDER_TIMER);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int wmain(int argc,wchar_t **argv) {
    if(argc<5)return 2;
    HWND parent=(HWND)(ULONG_PTR)_wcstoui64(argv[1],nullptr,10);
    if(!parent||!IsWindow(parent))return 3;
    g_mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,argv[2]);
    if(!g_mapping)return 4;
    g_shared=(ABMProjectMSharedState*)MapViewOfFile(g_mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ABMProjectMSharedState));
    if(!g_shared||g_shared->magic!=ABM_PROJECTM_IPC_MAGIC||g_shared->version!=ABM_PROJECTM_IPC_VERSION){Cleanup();return 5;}

    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.style=CS_OWNDC;wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpfnWndProc=RendererWndProc;wc.lpszClassName=L"ABMProjectMIsolatedSurface";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){Cleanup();return 6;}
    g_window=CreateWindowExW(0,wc.lpszClassName,L"",WS_CHILD|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
        0,0,1,1,parent,nullptr,wc.hInstance,nullptr);
    if(!g_window){Cleanup();return 7;}
    if(!CreateContext(g_window)){Cleanup();DestroyWindow(g_window);return 8;}
    g_projectm=projectm_create();
    if(!g_projectm){Cleanup();DestroyWindow(g_window);return 9;}
    projectm_set_preset_duration(g_projectm,45.0);
    projectm_set_soft_cut_duration(g_projectm,4.0);
    projectm_set_beat_sensitivity(g_projectm,1.15f);
    projectm_set_aspect_correction(g_projectm,true);
    if(argv[4][0]) {
        std::string texture=Utf8(argv[4]);const char *paths[]={texture.c_str()};
        projectm_set_texture_search_paths(g_projectm,paths,1);
    }
    if(argv[3][0])CollectPresets(argv[3]);
    std::sort(g_presets.begin(),g_presets.end());
    g_presets.erase(std::unique(g_presets.begin(),g_presets.end()),g_presets.end());
    if(!g_presets.empty()){g_presetIndex=g_presets.size()-1;LoadPresetDelta(1);}
    else projectm_load_preset_file(g_projectm,"idle://",false);
    g_lastPresetTick=GetTickCount64();
    ShowWindow(g_window,SW_SHOWNA);
    SetTimer(g_window,RENDER_TIMER,16,nullptr);
    InterlockedExchangePointer((PVOID volatile*)&g_shared->childWindow,(PVOID)g_window);
    InterlockedExchange(&g_shared->ready,1);
    MSG msg;
    while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    Cleanup();
    if(g_window&&IsWindow(g_window))DestroyWindow(g_window);
    return 0;
}

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
#include "projectm_visualizer.h"

#pragma comment(lib, "opengl32.lib")

namespace {
using CreateContextAttribsFn = HGLRC (WINAPI *)(HDC,HGLRC,const int *);
constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;

HWND g_parent=nullptr, g_window=nullptr;
HDC g_dc=nullptr;
HGLRC g_gl=nullptr;
projectm_handle g_projectm=nullptr;
std::vector<std::string> g_presets;
size_t g_presetIndex=0;
DWORD64 g_lastPresetTick=0;
bool g_ready=false;
bool g_destroying=false;
bool g_autoSwitch=true;
float g_pcm[512]{};

std::string Utf8(const wchar_t *value) {
    if(!value||!*value)return {};
    int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    if(n<=1)return {};
    std::string out((size_t)n,'\0');
    WideCharToMultiByte(CP_UTF8,0,value,-1,&out[0],n,nullptr,nullptr);
    out.resize((size_t)n-1);
    return out;
}
void CollectPresets(const std::wstring &directory) {
    WIN32_FIND_DATAW fd{};
    std::wstring pattern=directory+L"\\*.milk";
    HANDLE find=FindFirstFileW(pattern.c_str(),&fd);
    if(find!=INVALID_HANDLE_VALUE) {
        do {
            if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring full=directory+L"\\"+fd.cFileName;
                std::string path=Utf8(full.c_str());
                if(!path.empty())g_presets.push_back(path);
            }
        } while(FindNextFileW(find,&fd));
        FindClose(find);
    }
    pattern=directory+L"\\*";
    find=FindFirstFileW(pattern.c_str(),&fd);
    if(find!=INVALID_HANDLE_VALUE) {
        do {
            if((fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) &&
               wcscmp(fd.cFileName,L".") && wcscmp(fd.cFileName,L".."))
                CollectPresets(directory+L"\\"+fd.cFileName);
        } while(FindNextFileW(find,&fd));
        FindClose(find);
    }
}
bool CreateModernContext(HWND hwnd) {
    g_dc=GetDC(hwnd);
    if(!g_dc)return false;
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize=sizeof(pfd);pfd.nVersion=1;
    pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
    pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;pfd.cAlphaBits=8;
    pfd.cDepthBits=24;pfd.cStencilBits=8;pfd.iLayerType=PFD_MAIN_PLANE;
    int pf=ChoosePixelFormat(g_dc,&pfd);
    if(!pf||!SetPixelFormat(g_dc,pf,&pfd))return false;
    HGLRC bootstrap=wglCreateContext(g_dc);
    if(!bootstrap||!wglMakeCurrent(g_dc,bootstrap))return false;
    auto create=(CreateContextAttribsFn)wglGetProcAddress("wglCreateContextAttribsARB");
    if(!create) { wglMakeCurrent(nullptr,nullptr);wglDeleteContext(bootstrap);return false; }
    const int attrs[]={WGL_CONTEXT_MAJOR_VERSION_ARB,3,WGL_CONTEXT_MINOR_VERSION_ARB,3,
        WGL_CONTEXT_PROFILE_MASK_ARB,WGL_CONTEXT_CORE_PROFILE_BIT_ARB,0};
    HGLRC modern=create(g_dc,nullptr,attrs);
    wglMakeCurrent(nullptr,nullptr);wglDeleteContext(bootstrap);
    if(!modern||!wglMakeCurrent(g_dc,modern))return false;
    g_gl=modern;
    return true;
}
void LoadNextPreset(bool smooth) {
    if(!g_projectm)return;
    if(g_presets.empty()) {
        projectm_load_preset_file(g_projectm,"idle://",smooth);
        return;
    }
    g_presetIndex=(g_presetIndex+1)%g_presets.size();
    projectm_load_preset_file(g_projectm,g_presets[g_presetIndex].c_str(),smooth);
    g_lastPresetTick=GetTickCount64();
}
void Render() {
    if(!g_ready||!g_window||!g_projectm||!g_dc||!g_gl)return;
    if(!wglMakeCurrent(g_dc,g_gl))return;
    RECT rc{};GetClientRect(g_window,&rc);
    int w=rc.right-rc.left,h=rc.bottom-rc.top;
    if(w<=0||h<=0)return;
    if(g_autoSwitch && !g_presets.empty() && GetTickCount64()-g_lastPresetTick>45000)
        LoadNextPreset(true);
    /* The PCM producer is the existing WASAPI loopback capture worker. */
    projectm_pcm_add_float(g_projectm,g_pcm,512,PROJECTM_MONO);
    projectm_opengl_render_frame(g_projectm);
    SwapBuffers(g_dc);
}
LRESULT CALLBACK VisualizerWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);Render();EndPaint(hwnd,&ps);return 0;
    }
    case WM_SIZE:
        if(g_projectm && g_gl && wglMakeCurrent(g_dc,g_gl)) {
            RECT rc{};GetClientRect(hwnd,&rc);
            if(rc.right>0&&rc.bottom>0)projectm_set_window_size(g_projectm,(size_t)rc.right,(size_t)rc.bottom);
            wglMakeCurrent(nullptr,nullptr);
        }
        return 0;
    case WM_DESTROY:return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}

extern "C" BOOL ABM_ProjectM_Create(HWND parent,const wchar_t *presetDirectory,const wchar_t *textureDirectory) {
    if(g_ready)return TRUE;
    if(!parent)return FALSE;
    g_parent=parent;
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpfnWndProc=VisualizerWndProc;wc.lpszClassName=L"ABMProjectMEmbeddedSurface";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    RegisterClassExW(&wc);
    g_window=CreateWindowExW(0,wc.lpszClassName,L"",WS_CHILD|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
        0,0,1,1,parent,nullptr,wc.hInstance,nullptr);
    if(!g_window)return FALSE;
    if(!CreateModernContext(g_window)) { if(g_dc)ReleaseDC(g_window,g_dc);g_dc=nullptr;DestroyWindow(g_window);g_window=nullptr;return FALSE; }
    g_projectm=projectm_create();
    if(!g_projectm) { wglMakeCurrent(nullptr,nullptr);wglDeleteContext(g_gl);g_gl=nullptr;ReleaseDC(g_window,g_dc);g_dc=nullptr;DestroyWindow(g_window);g_window=nullptr;return FALSE; }
    projectm_set_window_size(g_projectm,1,1);
    projectm_set_preset_duration(g_projectm,45.0);
    projectm_set_soft_cut_duration(g_projectm,4.0);
    projectm_set_beat_sensitivity(g_projectm,1.15f);
    projectm_set_aspect_correction(g_projectm,true);
    if(textureDirectory&&*textureDirectory) {
        std::string texture=Utf8(textureDirectory);
        const char *paths[]={texture.c_str()};
        projectm_set_texture_search_paths(g_projectm,paths,1);
    }
    g_presets.clear();
    if(presetDirectory&&*presetDirectory)CollectPresets(presetDirectory);
    std::sort(g_presets.begin(),g_presets.end());
    g_presets.erase(std::unique(g_presets.begin(),g_presets.end()),g_presets.end());
    g_presetIndex=g_presets.empty()?0:g_presets.size()-1;
    if(!g_presets.empty())LoadNextPreset(false);
    else projectm_load_preset_file(g_projectm,"idle://",false);
    g_lastPresetTick=GetTickCount64();
    g_ready=true;
    wglMakeCurrent(nullptr,nullptr);
    return TRUE;
}
extern "C" void ABM_ProjectM_SetBounds(const RECT *bounds,BOOL visible) {
    if(!g_window)return;
    if(!visible||!bounds||bounds->right<=bounds->left||bounds->bottom<=bounds->top) {
        ShowWindow(g_window,SW_HIDE);return;
    }
    SetWindowPos(g_window,HWND_TOP,bounds->left,bounds->top,
        bounds->right-bounds->left,bounds->bottom-bounds->top,
        SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
extern "C" void ABM_ProjectM_SetAudioSamples(const float *samples,int count) {
    if(!samples||count<=0)return;
    if(count>512)count=512;
    memcpy(g_pcm,samples,(size_t)count*sizeof(float));
    if(count<512)memset(g_pcm+count,0,(size_t)(512-count)*sizeof(float));
}
extern "C" void ABM_ProjectM_Invalidate(void) {
    if(g_window&&IsWindowVisible(g_window))InvalidateRect(g_window,nullptr,FALSE);
}
extern "C" void ABM_ProjectM_NextPreset(void) {
    if(!g_ready||!g_projectm||!g_dc||!g_gl)return;
    if(wglMakeCurrent(g_dc,g_gl)){LoadNextPreset(true);wglMakeCurrent(nullptr,nullptr);ABM_ProjectM_Invalidate();}
}
extern "C" void ABM_ProjectM_PreviousPreset(void) {
    if(!g_ready||!g_projectm||!g_dc||!g_gl||g_presets.empty())return;
    if(wglMakeCurrent(g_dc,g_gl)) {
        g_presetIndex=(g_presetIndex+g_presets.size()-1)%g_presets.size();
        projectm_load_preset_file(g_projectm,g_presets[g_presetIndex].c_str(),true);
        g_lastPresetTick=GetTickCount64();
        wglMakeCurrent(nullptr,nullptr);ABM_ProjectM_Invalidate();
    }
}
extern "C" void ABM_ProjectM_SetAutoSwitch(BOOL enabled) {
    g_autoSwitch=enabled?true:false;
    if(g_projectm)projectm_set_preset_locked(g_projectm,enabled?false:true);
}
extern "C" BOOL ABM_ProjectM_IsReady(void) { return g_ready?TRUE:FALSE; }
extern "C" void ABM_ProjectM_Destroy(void) {
    if(g_destroying)return;g_destroying=true;
    HWND oldWindow=g_window;
    if(g_dc&&g_gl)wglMakeCurrent(g_dc,g_gl);
    if(g_projectm){projectm_destroy(g_projectm);g_projectm=nullptr;}
    if(g_gl){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(g_gl);g_gl=nullptr;}
    if(g_dc&&oldWindow)ReleaseDC(oldWindow,g_dc);
    if(g_window){DestroyWindow(g_window);g_window=nullptr;}
    g_dc=nullptr;g_parent=nullptr;g_presets.clear();g_ready=false;g_destroying=false;
}

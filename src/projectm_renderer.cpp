#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/glew.h>
#include <projectM-4/projectM.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <stdint.h>
#include <cwchar>
#include <stdlib.h>
#include <strsafe.h>
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
volatile LONG g_stage=0;
WCHAR g_logPath[MAX_PATH]={0};

void LogText(const wchar_t *message) {
    if(!g_logPath[0]) {
        wchar_t temp[MAX_PATH]={0};
        DWORD n=GetTempPathW(_countof(temp),temp);
        if(!n||n>=_countof(temp))return;
        if(FAILED(StringCchPrintfW(g_logPath,_countof(g_logPath),L"%sABM-ProjectM-renderer.log",temp)))return;
    }
    HANDLE file=CreateFileW(g_logPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    DWORD bytes=0;
    WriteFile(file,message,(DWORD)(wcslen(message)*sizeof(wchar_t)),&bytes,nullptr);
    WriteFile(file,L"\r\n",4,&bytes,nullptr);
    CloseHandle(file);
}
void SetStage(LONG stage,const wchar_t *description) {
    InterlockedExchange(&g_stage,stage);
    wchar_t line[256];
    StringCchPrintfW(line,_countof(line),L"stage %ld: %s",stage,description?description:L"");
    LogText(line);
}
LONG WINAPI RendererUnhandledException(EXCEPTION_POINTERS *info) {
    wchar_t line[256];
    DWORD code=info&&info->ExceptionRecord?info->ExceptionRecord->ExceptionCode:0;
    ULONG_PTR address=info&&info->ExceptionRecord?(ULONG_PTR)info->ExceptionRecord->ExceptionAddress:0;
    StringCchPrintfW(line,_countof(line),L"FATAL exception 0x%08lX at 0x%p; stage %ld",(unsigned long)code,(void*)address,(long)InterlockedCompareExchange(&g_stage,0,0));
    LogText(line);
    return EXCEPTION_EXECUTE_HANDLER;
}
float g_pcm[ABM_PROJECTM_PCM_COUNT]{};
int g_lastWidth=0,g_lastHeight=0;
volatile LONG g_firstRenderLogged=0;
volatile LONG g_renderFrames=0;

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
    /* projectM 4 uses GLEW on Windows. It must be initialized only after
       the renderer context is current and before projectm_create(). */
    glewExperimental=GL_TRUE;
    GLenum glewStatus=glewInit();
    if(glewStatus!=GLEW_OK) {
        wchar_t line[256];
        StringCchPrintfW(line,_countof(line),L"GLEW initialization failed: %S",glewGetErrorString(glewStatus));
        LogText(line);
        wglMakeCurrent(nullptr,nullptr);wglDeleteContext(modern);return false;
    }
    /* GLEW may leave GL_INVALID_ENUM behind on core contexts. */
    while(glGetError()!=GL_NO_ERROR) {}
    g_gl=modern;return true;
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
    if(!IsWindowVisible(g_window)) {
        if(InterlockedCompareExchange(&g_renderFrames,0,0)==0)LogText(L"render timer active but child HWND is not visible");
        return;
    }
    if(!wglMakeCurrent(g_dc,g_gl)) { if(InterlockedCompareExchange(&g_firstRenderLogged,2,0)==0)LogText(L"render failed: wglMakeCurrent"); return; }
    RECT rc{};GetClientRect(g_window,&rc);
    int w=rc.right-rc.left,h=rc.bottom-rc.top;
    if(w>0&&h>0) {
        if(w!=g_lastWidth||h!=g_lastHeight) {
            projectm_set_window_size(g_projectm,(size_t)w,(size_t)h);
            g_lastWidth=w;g_lastHeight=h;
        }
        int delta=InterlockedExchange(&g_shared->presetDelta,0);
        if(delta)LoadPresetDelta(delta);
        bool automatic=InterlockedCompareExchange(&g_shared->autoSwitch,0,0)!=0;
        if(automatic&&!g_presets.empty()&&GetTickCount64()-g_lastPresetTick>45000)
            LoadPresetDelta(1);
        ReadPcm();
        projectm_pcm_add_float(g_projectm,g_pcm,ABM_PROJECTM_PCM_COUNT,PROJECTM_MONO);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glDrawBuffer(GL_BACK);
        glViewport(0,0,w,h);
        InterlockedExchange(&g_stage,90);
        projectm_opengl_render_frame(g_projectm);
        GLenum glError=glGetError();
        if(InterlockedCompareExchange(&g_firstRenderLogged,0,0)==0) {
            /* Read the rendered back buffer once. This distinguishes a blank
               ProjectM frame from a child-HWND/DWM presentation problem. */
            GLint drawFbo=-1,readFbo=-1;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);
            /* ProjectM may leave an internal FBO bound. Read the actual
               window back buffer explicitly, and report both bindings. */
            while(glGetError()!=GL_NO_ERROR) {}
            glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
            glReadBuffer(GL_BACK);
            GLenum selectError=glGetError();
            std::vector<unsigned char> pixels((size_t)w*(size_t)h*4u,0);
            glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            GLenum pixelsError=glGetError();
            unsigned long long lit=0;
            unsigned int maxChannel=0;
            for(size_t i=0;i<pixels.size();i+=4) {
                unsigned int r=pixels[i],g=pixels[i+1],b=pixels[i+2];
                unsigned int peak=(r>g?(r>b?r:b):(g>b?g:b));
                if(peak>8)++lit;
                if(peak>maxChannel)maxChannel=peak;
            }
            wchar_t pixelsLine[256];
            StringCchPrintfW(pixelsLine,_countof(pixelsLine),
                L"first-frame pixels: nonblack=%llu/%llu maxRGB=%u readbackError=0x%04X",
                lit,(unsigned long long)((size_t)w*(size_t)h),maxChannel,(unsigned)pixelsError);
            LogText(pixelsLine);
            wchar_t fboLine[192];
            StringCchPrintfW(fboLine,_countof(fboLine),
                L"framebuffer state: drawFBO=%d readFBO=%d readBufferError=0x%04X",
                (int)drawFbo,(int)readFbo,(unsigned)selectError);
            LogText(fboLine);
        }
        BOOL swapped=SwapBuffers(g_dc);
        if(InterlockedCompareExchange(&g_firstRenderLogged,1,0)==0) {
            const GLubyte *version=glGetString(GL_VERSION);
            const GLubyte *vendor=glGetString(GL_VENDOR);
            wchar_t line[512];
            StringCchPrintfW(line,_countof(line),L"first frame: %dx%d, GL=%S, vendor=%S, glError=0x%04X, SwapBuffers=%s",w,h,version?(const char*)version:"unknown",vendor?(const char*)vendor:"unknown",(unsigned)glError,swapped?L"true":L"false");
            LogText(line);
        }
        LONG frame=InterlockedIncrement(&g_renderFrames);
        if((frame%120)==0) {
            wchar_t heartbeat[192];
            StringCchPrintfW(heartbeat,_countof(heartbeat),L"render heartbeat: %ld frames, visible=%d, size=%dx%d, glError=0x%04X",(long)frame,(int)IsWindowVisible(g_window),w,h,(unsigned)glGetError());
            LogText(heartbeat);
        }
        InterlockedExchange(&g_stage,91);
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
    case WM_DESTROY:Cleanup();PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int wmain(int argc,wchar_t **argv) {
    SetUnhandledExceptionFilter(RendererUnhandledException);
    SetStage(1,L"renderer entry");
    /* Match the player's system-DPI coordinate space before parenting HWNDs. */
    SetProcessDPIAware();
    if(argc<5){LogText(L"startup failed: insufficient command-line arguments");return 2;}
    HWND parent=(HWND)(ULONG_PTR)_wcstoui64(argv[1],nullptr,10);
    if(!parent||!IsWindow(parent)){LogText(L"startup failed: invalid parent window");return 3;}
    SetStage(2,L"opening shared memory");
    g_mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,argv[2]);
    if(!g_mapping){LogText(L"startup failed: OpenFileMappingW");return 4;}
    g_shared=(ABMProjectMSharedState*)MapViewOfFile(g_mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ABMProjectMSharedState));
    if(!g_shared||g_shared->magic!=ABM_PROJECTM_IPC_MAGIC||g_shared->version!=ABM_PROJECTM_IPC_VERSION){LogText(L"startup failed: shared memory validation");Cleanup();return 5;}
    SetStage(3,L"creating renderer window");

    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.style=CS_OWNDC;wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpfnWndProc=RendererWndProc;wc.lpszClassName=L"ABMProjectMIsolatedSurface";
    wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
    if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){LogText(L"startup failed: RegisterClassExW");Cleanup();return 6;}
    g_window=CreateWindowExW(0,wc.lpszClassName,L"",WS_CHILD|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
        0,0,1,1,parent,nullptr,wc.hInstance,nullptr);
    if(!g_window){LogText(L"startup failed: CreateWindowExW");Cleanup();return 7;}
    SetStage(4,L"creating OpenGL 3.3 context");
    if(!CreateContext(g_window)){LogText(L"startup failed: OpenGL context creation");Cleanup();DestroyWindow(g_window);return 8;}
    SetStage(5,L"projectm_create");
    g_projectm=projectm_create();
    if(!g_projectm){LogText(L"startup failed: projectm_create returned null");Cleanup();DestroyWindow(g_window);return 9;}
    SetStage(6,L"configuring ProjectM");
    projectm_set_preset_duration(g_projectm,45.0);
    projectm_set_soft_cut_duration(g_projectm,4.0);
    projectm_set_beat_sensitivity(g_projectm,1.15f);
    projectm_set_aspect_correction(g_projectm,true);
    /* Preset cycling is owned by this renderer timer, not ProjectM internals. */
    projectm_set_preset_locked(g_projectm,true);
    SetStage(7,L"loading texture paths and presets");
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
    wchar_t countLine[128];StringCchPrintfW(countLine,_countof(countLine),L"loaded %zu presets",g_presets.size());LogText(countLine);
    SetStage(8,L"publishing renderer window");
    /* ProjectM initialisation and preset loading require the renderer context. */
    wglMakeCurrent(nullptr,nullptr);
    ShowWindow(g_window,SW_SHOWNA);
    if(!SetTimer(g_window,RENDER_TIMER,16,nullptr))LogText(L"warning: render timer creation failed");
    LogText(L"renderer initialized; waiting for render timer");
    InterlockedExchangePointer((PVOID volatile*)&g_shared->childWindow,(PVOID)g_window);
    InterlockedExchange(&g_shared->ready,1);
    MSG msg;
    while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    LogText(L"renderer message loop ended normally");
    Cleanup();
    if(g_window&&IsWindow(g_window))DestroyWindow(g_window);
    return 0;
}

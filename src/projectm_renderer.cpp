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
#include <cmath>
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
volatile LONG g_firstTransferErrorLogged=0;
volatile LONG g_frameLockMissLogged=0;
/* A MilkDrop preset can evaluate to a fully black frame. Never publish that
   frame over the last good image; skip the preset if it stays black. */
volatile LONG g_blankFrameStreak=0;
GLuint g_visualProgram=0,g_visualVao=0;
GLint g_visualTime=-1,g_visualResolution=-1,g_visualStyle=-1,g_visualBands=-1;
float g_audioBands[24]{};
ULONGLONG g_visualStart=0;

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
bool CreateVisualShader() {
 const char *vs="#version 330 core\nout vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0.0,1.0);}";
 const char *fs=
 "#version 330 core\nin vec2 uv;out vec4 frag;uniform vec2 resolution;uniform float time;uniform int style;uniform float bands[24];"
 "float b(float x){return clamp(bands[clamp(int(x*23.0),0,23)],0.0,1.0);}float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}"
 "void main(){vec2 p=(uv-0.5)*vec2(resolution.x/resolution.y,1.0);float t=time;float energy=0.0;for(int i=0;i<24;i++)energy+=bands[i];energy/=24.0;vec3 c=vec3(0.008,0.014,0.03);"
 "if(style==1){float q=uv.x*48.0;float id=floor(q);float h=0.025+b((id+0.5)/48.0)*0.72;float y=0.13+h*(0.45+0.55*uv.y);float bar=step(fract(q),0.78)*step(uv.y,y)*step(0.12,uv.y);float glow=exp(-abs(uv.y-y)*45.0);vec3 k=mix(vec3(0.02,0.72,0.98),vec3(0.98,0.18,0.62),id/48.0);c+=k*(bar*0.8+glow*0.18);}"
 "else if(style==2){for(int i=0;i<52;i++){float fi=float(i);vec2 s=vec2(hash(vec2(fi,2.1)),hash(vec2(fi,7.3)));float speed=0.025+hash(vec2(fi,4.2))*0.11;vec2 q=fract(s+vec2(t*speed,-t*speed*0.7));vec2 d=(q-uv)*vec2(resolution.x/resolution.y,1.0);float r=0.0025+b(fract(fi/52.0))*0.005;float dotp=exp(-dot(d,d)/(r*r));vec3 hue=0.5+0.5*cos(vec3(0.0,2.1,4.2)+fi*0.19+t*0.18);c+=hue*dotp*(0.45+energy);}float ring=abs(length(p)-(0.18+energy*0.25));c+=vec3(0.08,0.48,0.95)*exp(-ring*75.0)*0.5;}"
 "else if(style==3){float a=atan(p.y,p.x);float r=length(p);float wave=0.0;for(int i=0;i<24;i++){float f=float(i)/23.0;wave+=bands[i]*sin(a*3.0+f*19.0+t*1.8)*0.006;}float trace=abs(r-(0.28+wave+0.055*sin(a*5.0-t*0.65)));c+=vec3(0.04,0.95,0.78)*exp(-trace*190.0);c+=vec3(0.10,0.35,0.95)*exp(-abs(r-0.46)*55.0)*0.42;}"
 "else if(style==4){vec2 q=p;float v=0.0;for(int i=0;i<5;i++){float f=float(i);q=vec2(sin(q.y*3.2+t*0.24+f)+cos(q.x*2.5-t*0.18),sin(q.x*2.8+t*0.16)-cos(q.y*3.0+t*0.2));v+=sin(q.x+q.y+energy*3.5)*0.2;}float n=0.5+0.5*sin(v*3.4+t*0.35);c=mix(vec3(0.015,0.35,0.68),vec3(0.78,0.08,0.48),n);c*=0.35+0.65*abs(sin(v*5.2));}"
 "else if(style==5){float a=atan(p.y,p.x);float r=max(length(p),0.001);float z=0.8/r-t*1.55;float spokes=abs(sin(a*14.0+floor(z)*0.3));float rings=abs(fract(z)-0.5);float glow=exp(-rings*(30.0+b(fract(a/6.28318))*28.0))*exp(-spokes*8.0);vec3 hue=0.5+0.5*cos(vec3(0.0,2.0,4.0)+z*0.22+a);c+=hue*glow*(0.45+energy);c+=vec3(0.02,0.12,0.2)*exp(-rings*8.0);}"
 "frag=vec4(c,1.0);}";
 auto compile=[](GLenum type,const char *src)->GLuint{GLuint sh=glCreateShader(type);glShaderSource(sh,1,&src,nullptr);glCompileShader(sh);GLint ok=0;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);if(!ok){char log[1024]={0};glGetShaderInfoLog(sh,sizeof(log),nullptr,log);wchar_t msg[1200];StringCchPrintfW(msg,_countof(msg),L"OpenGL shader compile failed: %S",log);LogText(msg);glDeleteShader(sh);return 0;}return sh;};
 GLuint v=compile(GL_VERTEX_SHADER,vs);if(!v)return false;GLuint f=compile(GL_FRAGMENT_SHADER,fs);if(!f){glDeleteShader(v);return false;}
 g_visualProgram=glCreateProgram();glAttachShader(g_visualProgram,v);glAttachShader(g_visualProgram,f);glLinkProgram(g_visualProgram);glDeleteShader(v);glDeleteShader(f);
 GLint linked=0;glGetProgramiv(g_visualProgram,GL_LINK_STATUS,&linked);if(!linked){char log[1024]={0};glGetProgramInfoLog(g_visualProgram,sizeof(log),nullptr,log);wchar_t msg[1200];StringCchPrintfW(msg,_countof(msg),L"OpenGL shader link failed: %S",log);LogText(msg);glDeleteProgram(g_visualProgram);g_visualProgram=0;return false;}
 glGenVertexArrays(1,&g_visualVao);g_visualTime=glGetUniformLocation(g_visualProgram,"time");g_visualResolution=glGetUniformLocation(g_visualProgram,"resolution");g_visualStyle=glGetUniformLocation(g_visualProgram,"style");g_visualBands=glGetUniformLocation(g_visualProgram,"bands[0]");g_visualStart=GetTickCount64();return true;
}
void ReadBands(){for(int a=0;a<4;a++){LONG before=InterlockedCompareExchange(&g_shared->sequence,0,0);if(before&1)continue;CopyMemory(g_audioBands,g_shared->bands,sizeof(g_audioBands));MemoryBarrier();LONG after=InterlockedCompareExchange(&g_shared->sequence,0,0);if(before==after&&!(after&1))return;}}
void RenderOpenGLStyle(int style,int w,int h){
 if(!g_visualProgram&&!CreateVisualShader()){glBindFramebuffer(GL_FRAMEBUFFER,0);glDrawBuffer(GL_BACK);glViewport(0,0,w,h);glClearColor(0.008f,0.014f,0.03f,1);glClear(GL_COLOR_BUFFER_BIT);return;}
 glBindFramebuffer(GL_FRAMEBUFFER,0);glDrawBuffer(GL_BACK);glViewport(0,0,w,h);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glClearColor(0.008f,0.014f,0.03f,1);glClear(GL_COLOR_BUFFER_BIT);
 glUseProgram(g_visualProgram);glBindVertexArray(g_visualVao);glUniform2f(g_visualResolution,(float)w,(float)h);glUniform1f(g_visualTime,(float)(GetTickCount64()-g_visualStart)/1000.0f);glUniform1i(g_visualStyle,style);glUniform1fv(g_visualBands,24,g_audioBands);glDrawArrays(GL_TRIANGLES,0,3);glBindVertexArray(0);glUseProgram(0);
}

void RenderFrame() {
    if(!g_gl||!g_dc||!g_shared)return;
    if(!wglMakeCurrent(g_dc,g_gl)) { if(InterlockedCompareExchange(&g_firstRenderLogged,2,0)==0)LogText(L"render failed: wglMakeCurrent"); return; }
    RECT rc{};GetClientRect(g_window,&rc);
    int w=rc.right-rc.left,h=rc.bottom-rc.top;
    if(w>0&&h>0) {
        if(w!=g_lastWidth||h!=g_lastHeight) {if(g_projectm)projectm_set_window_size(g_projectm,(size_t)w,(size_t)h);g_lastWidth=w;g_lastHeight=h;}
        int style=InterlockedCompareExchange(&g_shared->visualStyle,0,0);ReadPcm();ReadBands();InterlockedExchange(&g_stage,90);
        if(style==0){int delta=InterlockedExchange(&g_shared->presetDelta,0);if(delta)LoadPresetDelta(delta);bool automatic=InterlockedCompareExchange(&g_shared->autoSwitch,0,0)!=0;if(automatic&&!g_presets.empty()&&GetTickCount64()-g_lastPresetTick>45000)LoadPresetDelta(1);projectm_pcm_add_float(g_projectm,g_pcm,ABM_PROJECTM_PCM_COUNT,PROJECTM_MONO);glBindFramebuffer(GL_FRAMEBUFFER,0);glDrawBuffer(GL_BACK);glViewport(0,0,w,h);projectm_opengl_render_frame(g_projectm);}
        else RenderOpenGLStyle(style,w,h);
        GLenum glError=glGetError();
        /* Publish BGRA pixels through shared memory. The main player paints
           them into its own GDI/Direct2D composition; no foreign child HWND. */
        /* Acquire the same cross-process frame lock used by the player reader.
           This prevents the renderer from replacing an 8 MB buffer mid-copy. */
        LONG frameLock=0;
        for(int lockAttempt=0;lockAttempt<200;lockAttempt++) {
            LONG seq=InterlockedCompareExchange(&g_shared->frameSequence,0,0);
            if(!(seq&1)&&InterlockedCompareExchange(&g_shared->frameSequence,seq+1,seq)==seq) {
                frameLock=seq+1;
                break;
            }
            SwitchToThread();
        }
        if(frameLock) {
            LONG nextFrame=(InterlockedCompareExchange(&g_shared->frameIndex,0,0)^1)&1;
            /* projectM may leave its internal FBO bound after rendering.
               GL_BACK is valid only for the default window framebuffer.
               Select framebuffer 0 explicitly for the read path before reading. */
            glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
            glReadBuffer(GL_BACK);
            glReadPixels(0,0,w,h,GL_BGRA,GL_UNSIGNED_BYTE,g_shared->frames[nextFrame]);
            GLenum transferError=glGetError();
            if(transferError==GL_NO_ERROR) {
                const unsigned char *sample=g_shared->frames[nextFrame];
                /* Sample a 64x36 grid instead of scanning 8 MB on every
                   frame. A single illuminated sample is enough to preserve
                   intentionally dark presets; only a truly black image is
                   considered a failed preset. */
                unsigned int visibleSamples=0;
                for(int sy=0;sy<36;sy++) {
                    int y=(sy*h)/36;
                    for(int sx=0;sx<64;sx++) {
                        int x=(sx*w)/64;
                        const unsigned char *px=sample+((size_t)y*(size_t)w+(size_t)x)*4u;
                        if(px[0]>2||px[1]>2||px[2]>2)++visibleSamples;
                    }
                }
                if(visibleSamples>0) {
                    InterlockedExchange(&g_blankFrameStreak,0);
                    /* Only commit a frame to the consumer after it contains
                       actual image data. A transient black frame therefore
                       cannot erase the last successfully displayed frame. */
                    InterlockedExchange(&g_shared->frameWidth,w);
                    InterlockedExchange(&g_shared->frameHeight,h);
                    InterlockedExchange(&g_shared->frameIndex,nextFrame);
                } else {
                    LONG blank=InterlockedIncrement(&g_blankFrameStreak);
                    if(blank==90&&style==0) {
                        wchar_t line[256];
                        StringCchPrintfW(line,_countof(line),
                            L"ProjectM preset %zu produced 90 consecutive black frames; advancing automatically",g_presetIndex);
                        LogText(line);
                        LoadPresetDelta(1);
                        InterlockedExchange(&g_blankFrameStreak,0);
                    }
                }
                /* Periodically measure the actual OpenGL readback. */
                LONG upcomingFrame=InterlockedCompareExchange(&g_renderFrames,0,0)+1;
                if((upcomingFrame%120)==0) {
                    size_t pixelCount=(size_t)w*(size_t)h;
                    unsigned long long lit=0,luminanceSum=0;
                    unsigned int maxChannel=0;
                    for(size_t p=0;p<pixelCount;p++) {
                        unsigned int b=sample[p*4u],g=sample[p*4u+1u],r=sample[p*4u+2u];
                        unsigned int peak=(r>g?(r>b?r:b):(g>b?g:b));
                        unsigned int luminance=(77u*r+150u*g+29u*b)>>8;
                        if(peak>8)++lit;
                        luminanceSum+=luminance;
                        if(peak>maxChannel)maxChannel=peak;
                    }
                    wchar_t metrics[256];
                    StringCchPrintfW(metrics,_countof(metrics),
                        L"OpenGL output metrics: frame=%ld preset=%zu nonblack=%llu/%llu meanLuma=%llu maxRGB=%u sampledVisible=%u blankStreak=%ld",
                        (long)upcomingFrame,g_presetIndex,lit,(unsigned long long)pixelCount,
                        pixelCount?luminanceSum/pixelCount:0,maxChannel,visibleSamples,
                        (long)InterlockedCompareExchange(&g_blankFrameStreak,0,0));
                    LogText(metrics);
                }
            } else if(InterlockedCompareExchange(&g_firstTransferErrorLogged,1,0)==0) {
                wchar_t line[160];StringCchPrintfW(line,_countof(line),
                    L"shared frame transfer failed: 0x%04X",(unsigned)transferError);LogText(line);
            }
            MemoryBarrier();
            InterlockedExchange(&g_shared->frameSequence,frameLock+1);
        } else if(InterlockedCompareExchange(&g_frameLockMissLogged,1,0)==0) {
            LogText(L"shared frame publish skipped: player held frame lock");
        }
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
    if(g_projectm){projectm_destroy(g_projectm);g_projectm=nullptr;}if(g_visualProgram){glDeleteProgram(g_visualProgram);g_visualProgram=0;}if(g_visualVao){glDeleteVertexArrays(1,&g_visualVao);g_visualVao=0;}
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
    /* The hidden HWND is only a WGL context owner. Keep a real render
       surface size so GetClientRect/glViewport never collapse to 1x1 when
       the window is hidden and the host no longer positions it. */
    g_window=CreateWindowExW(0,wc.lpszClassName,L"",WS_CHILD|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
        0,0,ABM_PROJECTM_FRAME_WIDTH,ABM_PROJECTM_FRAME_HEIGHT,parent,nullptr,wc.hInstance,nullptr);
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
    /* Keep the helper window hidden: it exists only to own the WGL context.
       The player composites shared BGRA frames into its own interface. */
    ShowWindow(g_window,SW_HIDE);
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

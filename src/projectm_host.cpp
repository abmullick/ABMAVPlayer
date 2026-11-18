#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <cwchar>
#include "projectm_visualizer.h"
#include "projectm_ipc.h"

namespace {
HANDLE g_mapping=nullptr;
ABMProjectMSharedState *g_shared=nullptr;
HANDLE g_process=nullptr;
HWND g_window=nullptr;
RECT g_lastBounds{};
bool g_surfaceVisible=false;
bool g_starting=false;
DWORD g_lastError=ERROR_SUCCESS;
void HostLog(const wchar_t *message) {
    wchar_t path[MAX_PATH]={0};DWORD n=GetTempPathW(_countof(path),path);
    if(!n||n>=_countof(path))return;
    StringCchCatW(path,_countof(path),L"ABM-ProjectM-renderer.log");
    HANDLE f=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return;DWORD written=0;
    WriteFile(f,message,(DWORD)(wcslen(message)*sizeof(wchar_t)),&written,nullptr);
    WriteFile(f,L"\r\n",4,&written,nullptr);CloseHandle(f);
}

void ResetHost() {
    g_window=nullptr;
    g_surfaceVisible=false;
    ZeroMemory(&g_lastBounds,sizeof(g_lastBounds));
    if(g_shared){UnmapViewOfFile(g_shared);g_shared=nullptr;}
    if(g_mapping){CloseHandle(g_mapping);g_mapping=nullptr;}
    if(g_process){CloseHandle(g_process);g_process=nullptr;}
}
void StopRenderer(bool force) {
    if(g_process) {
        if(g_window&&IsWindow(g_window))PostMessageW(g_window,WM_CLOSE,0,0);
        DWORD wait=WaitForSingleObject(g_process,force?250:1500);
        if(wait==WAIT_TIMEOUT) {
            TerminateProcess(g_process,0);
            WaitForSingleObject(g_process,1000);
        }
    }
    ResetHost();
}
bool QuoteArg(wchar_t *out,size_t count,const wchar_t *value) {
    if(!out||!count||!value)return false;
    HRESULT hr=StringCchCopyW(out,count,L"\"");
    if(FAILED(hr))return false;
    for(const wchar_t *p=value;*p;p++) {
        if(*p==L'\"') {
            if(FAILED(StringCchCatW(out,count,L"\\\"")))return false;
        } else {
            wchar_t one[2]={*p,0};
            if(FAILED(StringCchCatW(out,count,one)))return false;
        }
    }
    return SUCCEEDED(StringCchCatW(out,count,L"\""));
}
}

extern "C" BOOL ABM_ProjectM_Create(HWND parent,const wchar_t *presetDirectory,const wchar_t *textureDirectory) {
    if(g_process&&WaitForSingleObject(g_process,0)==WAIT_TIMEOUT)return TRUE;
    if(g_starting||!parent||!IsWindow(parent)){g_lastError=ERROR_INVALID_WINDOW_HANDLE;return FALSE;}
    g_lastError=ERROR_SUCCESS;
    if(g_process)StopRenderer(true);
    g_starting=true;
    wchar_t exe[MAX_PATH]={0},dir[MAX_PATH]={0},helper[MAX_PATH]={0};
    DWORD n=GetModuleFileNameW(nullptr,exe,_countof(exe));
    if(!n||n>=_countof(exe)){g_starting=false;return FALSE;}
    StringCchCopyW(dir,_countof(dir),exe);
    wchar_t *slash=wcsrchr(dir,L'\\');
    if(!slash){g_starting=false;return FALSE;}
    *slash=0;
    if(FAILED(StringCchCopyW(helper,_countof(helper),dir))||
       FAILED(StringCchCatW(helper,_countof(helper),L"\\abm-projectm-renderer.exe"))) {
        g_starting=false;return FALSE;
    }
    if(GetFileAttributesW(helper)==INVALID_FILE_ATTRIBUTES){g_lastError=ERROR_FILE_NOT_FOUND;g_starting=false;return FALSE;}

    wchar_t mapName[96];
    StringCchPrintfW(mapName,_countof(mapName),L"Local\\ABMProjectM_%lu_%llu",
        GetCurrentProcessId(),(unsigned long long)GetTickCount64());
    g_mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,
        (DWORD)sizeof(ABMProjectMSharedState),mapName);
    if(!g_mapping){g_lastError=GetLastError();g_starting=false;ResetHost();return FALSE;}
    g_shared=(ABMProjectMSharedState*)MapViewOfFile(g_mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ABMProjectMSharedState));
    if(!g_shared){g_lastError=GetLastError();g_starting=false;ResetHost();return FALSE;}
    ZeroMemory(g_shared,sizeof(*g_shared));
    g_shared->magic=ABM_PROJECTM_IPC_MAGIC;
    g_shared->version=ABM_PROJECTM_IPC_VERSION;
    g_shared->autoSwitch=1;

    wchar_t parentArg[40],mapArg[160],presetArg[MAX_PATH*2],textureArg[MAX_PATH*2],cmd[4096];
    StringCchPrintfW(parentArg,_countof(parentArg),L"%llu",(unsigned long long)(ULONG_PTR)parent);
    if(!QuoteArg(mapArg,_countof(mapArg),mapName)||
       !QuoteArg(presetArg,_countof(presetArg),presetDirectory?presetDirectory:L"")||
       !QuoteArg(textureArg,_countof(textureArg),textureDirectory?textureDirectory:L"")) {
        g_starting=false;ResetHost();return FALSE;
    }
    HRESULT hr=StringCchPrintfW(cmd,_countof(cmd),L"\"%s\" %s %s %s %s",
        helper,parentArg,mapArg,presetArg,textureArg);
    if(FAILED(hr)){g_starting=false;ResetHost();return FALSE;}
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    if(!CreateProcessW(helper,cmd,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,dir,&si,&pi)) {
        g_lastError=GetLastError();g_starting=false;ResetHost();return FALSE;
    }
    CloseHandle(pi.hThread);
    g_process=pi.hProcess;
    /* Do not wait for WGL/ProjectM startup on the playback UI thread.
       The Direct2D visualizer remains visible until IsReady observes the
       helper's completed child-window handshake. */
    g_starting=false;
    return TRUE;
}

extern "C" void ABM_ProjectM_SetBounds(const RECT *bounds,BOOL visible) {
    if(!g_shared||!g_process||WaitForSingleObject(g_process,0)!=WAIT_TIMEOUT)return;
    HWND child=(HWND)(ULONG_PTR)InterlockedCompareExchangePointer((PVOID volatile*)&g_shared->childWindow,nullptr,nullptr);
    if(!child||!IsWindow(child)){g_window=nullptr;return;}
    g_window=child;
    if(!visible||!bounds||bounds->right<=bounds->left||bounds->bottom<=bounds->top) {
        if(g_surfaceVisible){HostLog(L"host: hiding ProjectM child surface");ShowWindow(child,SW_HIDE);}
        g_surfaceVisible=false;return;
    }
    if(g_surfaceVisible&&EqualRect(&g_lastBounds,bounds))return;
    SetWindowPos(child,HWND_TOP,bounds->left,bounds->top,bounds->right-bounds->left,
        bounds->bottom-bounds->top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    if(!g_surfaceVisible)HostLog(L"host: showing ProjectM child surface");
    g_lastBounds=*bounds;g_surfaceVisible=true;
}
extern "C" BOOL ABM_ProjectM_CopyFrame(void *destination,int destinationBytes,int *width,int *height) {
    if(!g_shared||!destination||destinationBytes<ABM_PROJECTM_FRAME_BYTES)return FALSE;
    for(int attempt=0;attempt<3;attempt++) {
        LONG seq=InterlockedCompareExchange(&g_shared->frameSequence,0,0);
        if(seq&1)continue;
        LONG index=InterlockedCompareExchange(&g_shared->frameIndex,0,0)&1;
        LONG w=InterlockedCompareExchange(&g_shared->frameWidth,0,0);
        LONG h=InterlockedCompareExchange(&g_shared->frameHeight,0,0);
        if(w<=0||h<=0||w>ABM_PROJECTM_FRAME_WIDTH||h>ABM_PROJECTM_FRAME_HEIGHT)return FALSE;
        CopyMemory(destination,g_shared->frames[index],(size_t)w*(size_t)h*4u);
        MemoryBarrier();
        if(seq==InterlockedCompareExchange(&g_shared->frameSequence,0,0)) {if(width)*width=(int)w;if(height)*height=(int)h;return TRUE;}
    }
    return FALSE;
}
extern "C" void ABM_ProjectM_SetAudioSamples(const float *samples,int count) {
    if(!g_shared||!samples||count<=0)return;
    if(count>ABM_PROJECTM_PCM_COUNT)count=ABM_PROJECTM_PCM_COUNT;
    InterlockedIncrement(&g_shared->sequence); /* odd: write in progress */
    CopyMemory(g_shared->pcm,samples,(size_t)count*sizeof(float));
    if(count<ABM_PROJECTM_PCM_COUNT)
        ZeroMemory(g_shared->pcm+count,(ABM_PROJECTM_PCM_COUNT-count)*sizeof(float));
    InterlockedIncrement(&g_shared->sequence); /* even: stable snapshot */
}
extern "C" void ABM_ProjectM_Invalidate(void) {
    /* The isolated renderer owns a fixed-rate render timer; no paint message is required. */
}
extern "C" void ABM_ProjectM_NextPreset(void) {
    if(g_shared)InterlockedIncrement(&g_shared->presetDelta);
}
extern "C" void ABM_ProjectM_PreviousPreset(void) {
    if(g_shared)InterlockedDecrement(&g_shared->presetDelta);
}
extern "C" void ABM_ProjectM_SetAutoSwitch(BOOL enabled) {
    if(g_shared)InterlockedExchange(&g_shared->autoSwitch,enabled?1:0);
}
extern "C" BOOL ABM_ProjectM_IsReady(void) {
    if(!g_process||!g_shared)return FALSE;
    if(WaitForSingleObject(g_process,0)!=WAIT_TIMEOUT){
        DWORD exitCode=ERROR_GEN_FAILURE;
        if(GetExitCodeProcess(g_process,&exitCode))g_lastError=exitCode;
        else g_lastError=GetLastError();
        if(g_lastError==ERROR_SUCCESS)g_lastError=ERROR_PROCESS_ABORTED;
        ResetHost();return FALSE;
    }
    if(InterlockedCompareExchange(&g_shared->ready,0,0)!=1)return FALSE;
    HWND child=(HWND)(ULONG_PTR)InterlockedCompareExchangePointer((PVOID volatile*)&g_shared->childWindow,nullptr,nullptr);
    if(!child||!IsWindow(child))return FALSE;
    g_window=child;
    return TRUE;
}
extern "C" DWORD ABM_ProjectM_GetLastError(void) { return g_lastError; }
extern "C" void ABM_ProjectM_Destroy(void) {
    StopRenderer(false);
}

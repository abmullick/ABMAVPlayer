#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <stdint.h>
#include <math.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <algorithm>
#include <cstring>

namespace {
constexpr int FFT_SIZE = 512;
constexpr int BAND_COUNT = 24;
struct Complex { float r, i; };
std::atomic<bool> running{false};
std::thread worker;
HANDLE stopEvent = nullptr;
HANDLE startupEvent = nullptr;
std::atomic<bool> captureActive{false};
std::mutex bandsMutex;
float bands[BAND_COUNT]{};

static void PublishSpectrum(const float *samples, int count) {
    Complex a[FFT_SIZE];
    for (int i=0;i<FFT_SIZE;i++) {
        float x = i<count ? samples[i] : 0.0f;
        float window = 0.5f - 0.5f*cosf(6.28318530718f*(float)i/(float)(FFT_SIZE-1));
        a[i] = {x*window,0.0f};
    }
    for (unsigned i=1,j=0;i<FFT_SIZE;i++) {
        unsigned bit=FFT_SIZE>>1;
        for (;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if (i<j) std::swap(a[i],a[j]);
    }
    for (int len=2;len<=FFT_SIZE;len<<=1) {
        float angle=-6.28318530718f/(float)len;
        Complex root={cosf(angle),sinf(angle)};
        for (int base=0;base<FFT_SIZE;base+=len) {
            Complex w={1.0f,0.0f};
            for (int j=0;j<len/2;j++) {
                Complex u=a[base+j],v=a[base+j+len/2];
                Complex t={v.r*w.r-v.i*w.i,v.r*w.i+v.i*w.r};
                a[base+j]={u.r+t.r,u.i+t.i};
                a[base+j+len/2]={u.r-t.r,u.i-t.i};
                w={w.r*root.r-w.i*root.i,w.r*root.i+w.i*root.r};
            }
        }
    }
    float next[BAND_COUNT]{};
    for (int b=0;b<BAND_COUNT;b++) {
        int lo=1+(int)(powf(120.0f,(float)b/(float)BAND_COUNT));
        int hi=1+(int)(powf(120.0f,(float)(b+1)/(float)BAND_COUNT));
        if (hi<=lo) hi=lo+1;
        float energy=0.0f;
        int n=0;
        for (int k=lo;k<hi && k<FFT_SIZE/2;k++) {
            float mag=sqrtf(a[k].r*a[k].r+a[k].i*a[k].i)/(float)FFT_SIZE;
            energy+=mag; n++;
        }
        float level=n?sqrtf(energy/(float)n)*8.0f:0.0f;
        if (level>1.0f) level=1.0f;
        next[b]=level;
    }
    std::lock_guard<std::mutex> lock(bandsMutex);
    for (int i=0;i<BAND_COUNT;i++) bands[i]=bands[i]*0.58f+next[i]*0.42f;
}

static float ReadSample(const BYTE *p, const WAVEFORMATEX *fmt, bool isFloat) {
    if (isFloat && fmt->wBitsPerSample==32) {
        float v; std::memcpy(&v,p,sizeof(v)); return (std::max)(-1.0f,(std::min)(1.0f,v));
    }
    if (!isFloat && fmt->wBitsPerSample==16) {
        int16_t v; std::memcpy(&v,p,sizeof(v)); return (float)v/32768.0f;
    }
    if (!isFloat && fmt->wBitsPerSample==32) {
        int32_t v; std::memcpy(&v,p,sizeof(v)); return (float)((double)v/2147483648.0);
    }
    return 0.0f;
}

static void CaptureThread() {
    HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    IMMDeviceEnumerator *enumerator=nullptr;
    IMMDevice *device=nullptr;
    IAudioClient *client=nullptr;
    IAudioCaptureClient *capture=nullptr;
    WAVEFORMATEX *format=nullptr;
    HANDLE audioEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    bool started=false;
    bool isFloat=false;
    HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),(void**)&enumerator);
    if (SUCCEEDED(hr)) hr=enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device);
    if (SUCCEEDED(hr)) hr=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,(void**)&client);
    if (SUCCEEDED(hr)) hr=client->GetMixFormat(&format);
    if (SUCCEEDED(hr) && format) {
        isFloat=(format->wFormatTag==WAVE_FORMAT_IEEE_FLOAT) ||
            (format->wFormatTag==WAVE_FORMAT_EXTENSIBLE &&
             format->wBitsPerSample==32 &&
             IsEqualGUID(((WAVEFORMATEXTENSIBLE*)format)->SubFormat,KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
        bool supported=(isFloat && format->wBitsPerSample==32) ||
            (format->wFormatTag==WAVE_FORMAT_PCM && (format->wBitsPerSample==16 || format->wBitsPerSample==32)) ||
            (format->wFormatTag==WAVE_FORMAT_EXTENSIBLE && !isFloat && (format->wBitsPerSample==16 || format->wBitsPerSample==32));
        if (supported && format->nChannels>0 && format->nBlockAlign>0) {
            hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_LOOPBACK|AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                0,0,format,nullptr);
            if (SUCCEEDED(hr)) hr=client->SetEventHandle(audioEvent);
            if (SUCCEEDED(hr)) hr=client->GetService(__uuidof(IAudioCaptureClient),(void**)&capture);
            if (SUCCEEDED(hr)) { hr=client->Start(); started=SUCCEEDED(hr); }
        }
    }
    float window[FFT_SIZE]{};
    int used=0;
    while (running.load() && started) {
        HANDLE waits[2]={stopEvent,audioEvent};
        DWORD wr=WaitForMultipleObjects(2,waits,FALSE,500);
        if (wr==WAIT_OBJECT_0) break;
        if (wr!=WAIT_OBJECT_0+1) continue;
        UINT32 packet=0;
        if (FAILED(capture->GetNextPacketSize(&packet))) break;
        while (packet>0 && running.load()) {
            BYTE *data=nullptr; UINT32 frames=0; DWORD flags=0;
            if (FAILED(capture->GetBuffer(&data,&frames,&flags,nullptr,nullptr))) break;
            const unsigned bytesPerSample=format->wBitsPerSample/8;
            for (UINT32 f=0;f<frames;f++) {
                float mixed=0.0f;
                if (!(flags&AUDCLNT_BUFFERFLAGS_SILENT) && bytesPerSample>0) {
                    const BYTE *frame=data+(size_t)f*format->nBlockAlign;
                    for (WORD ch=0;ch<format->nChannels;ch++)
                        mixed+=ReadSample(frame+(size_t)ch*bytesPerSample,format,isFloat);
                    mixed/=(float)format->nChannels;
                }
                window[used++]=mixed;
                if (used==FFT_SIZE) { PublishSpectrum(window,FFT_SIZE); used=0; }
            }
            capture->ReleaseBuffer(frames);
            if (FAILED(capture->GetNextPacketSize(&packet))) { packet=0; break; }
        }
    }
    if (started) client->Stop();
    if (capture) capture->Release();
    if (format) CoTaskMemFree(format);
    if (client) client->Release();
    if (device) device->Release();
    if (enumerator) enumerator->Release();
    if (audioEvent) CloseHandle(audioEvent);
    if (SUCCEEDED(co)) CoUninitialize();
    { std::lock_guard<std::mutex> lock(bandsMutex); for(float &v:bands)v=0.0f; }
    captureActive.store(false);
    running.store(false);
}
}

extern "C" BOOL ABM_AudioVisualizer_Start(void) {
    if (running.load()) return TRUE;
    if (worker.joinable()) worker.join();
    stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    startupEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!stopEvent || !startupEvent) {
        if(stopEvent) CloseHandle(stopEvent);
        if(startupEvent) CloseHandle(startupEvent);
        stopEvent=nullptr; startupEvent=nullptr; return FALSE;
    }
    { std::lock_guard<std::mutex> lock(bandsMutex); for(float &v:bands)v=0.0f; }
    running.store(true);
    try { worker=std::thread(CaptureThread); }
    catch (...) { running.store(false); CloseHandle(stopEvent); CloseHandle(startupEvent); stopEvent=nullptr; startupEvent=nullptr; return FALSE; }
    DWORD ready=WaitForSingleObject(startupEvent,1500);
    if(ready!=WAIT_OBJECT_0 || !captureActive.load()) {
        running.store(false);
        SetEvent(stopEvent);
        if(worker.joinable()) worker.join();
        CloseHandle(stopEvent); CloseHandle(startupEvent);
        stopEvent=nullptr; startupEvent=nullptr; return FALSE;
    }
    return TRUE;
}
extern "C" void ABM_AudioVisualizer_Stop(void) {
    if (!running.exchange(false)) {
        if (worker.joinable()) worker.join();
        if (stopEvent) { CloseHandle(stopEvent); stopEvent=nullptr; }
        if (startupEvent) { CloseHandle(startupEvent); startupEvent=nullptr; }
        return;
    }
    if (stopEvent) SetEvent(stopEvent);
    if (worker.joinable()) worker.join();
    if (stopEvent) { CloseHandle(stopEvent); stopEvent=nullptr; }
    if (startupEvent) { CloseHandle(startupEvent); startupEvent=nullptr; }
}
extern "C" void ABM_AudioVisualizer_GetBands(float *outBands,int count) {
    if (!outBands || count<=0) return;
    std::lock_guard<std::mutex> lock(bandsMutex);
    int n=(std::min)(count,BAND_COUNT);
    std::memcpy(outBands,bands,(size_t)n*sizeof(float));
    for(int i=n;i<count;i++) outBands[i]=0.0f;
}

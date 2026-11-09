#define _WIN32_DCOM
#include "dlna_cast.h"
#include <upnp.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <algorithm>
#include <utility>

namespace {
struct DlnaReceiver {
    std::wstring name;
    IStream *serviceStream = nullptr; /* marshalled from the discovery worker to the UI apartment */
    IUPnPService *uiService = nullptr; /* apartment proxy, created on the UI thread */
    DlnaReceiver() = default;
    DlnaReceiver(const DlnaReceiver&) = delete;
    DlnaReceiver& operator=(const DlnaReceiver&) = delete;
    DlnaReceiver(DlnaReceiver&& other) noexcept : name(std::move(other.name)), serviceStream(other.serviceStream), uiService(other.uiService) { other.serviceStream=nullptr;other.uiService=nullptr; }
    DlnaReceiver& operator=(DlnaReceiver&& other) noexcept {
        if(this!=&other){if(serviceStream)serviceStream->Release();if(uiService)uiService->Release();name=std::move(other.name);serviceStream=other.serviceStream;uiService=other.uiService;other.serviceStream=nullptr;other.uiService=nullptr;}return *this;
    }
    ~DlnaReceiver(){if(serviceStream)serviceStream->Release();if(uiService)uiService->Release();}
};
std::mutex g_lock;
std::vector<DlnaReceiver> g_devices;
std::thread g_worker;
std::atomic<bool> g_stop{false};
IUPnPService *g_selected=nullptr;

std::wstring BstrValue(BSTR value) {
    if(!value)return {};
    std::wstring result(value,SysStringLen(value));
    SysFreeString(value);
    return result;
}
bool Invoke(IUPnPService *service,const wchar_t *action,VARIANT *values,ULONG count) {
    if(!service)return false;
    SAFEARRAY *array=SafeArrayCreateVector(VT_VARIANT,0,count);
    if(!array)return false;
    for(LONG i=0;i<(LONG)count;i++){
        VARIANT v;VariantInit(&v);VariantCopy(&v,&values[i]);
        HRESULT hr=SafeArrayPutElement(array,&i,&v);VariantClear(&v);
        if(FAILED(hr)){SafeArrayDestroy(array);return false;}
    }
    VARIANT input,output,result;VariantInit(&input);VariantInit(&output);VariantInit(&result);
    input.vt=VT_ARRAY|VT_VARIANT;input.parray=array;
    output.vt=VT_ARRAY|VT_VARIANT;output.parray=SafeArrayCreateVector(VT_VARIANT,0,0);
    if(!output.parray){VariantClear(&input);return false;}
    BSTR actionName=SysAllocString(action);
    HRESULT hr=actionName?service->InvokeAction(actionName,input,&output,&result):E_OUTOFMEMORY;
    if(actionName)SysFreeString(actionName);
    VariantClear(&input);VariantClear(&output);VariantClear(&result);
    return SUCCEEDED(hr);
}
bool InvokeSetUri(IUPnPService *service,const wchar_t *url) {
    VARIANT args[3];for(auto &v:args)VariantInit(&v);
    args[0].vt=VT_UI4;args[0].ulVal=0;
    args[1].vt=VT_BSTR;args[1].bstrVal=SysAllocString(url);
    args[2].vt=VT_BSTR;args[2].bstrVal=SysAllocString(L"");
    bool ok=Invoke(service,L"SetAVTransportURI",args,3);
    for(auto &v:args)VariantClear(&v);
    return ok;
}
bool InvokePlay(IUPnPService *service) {
    VARIANT args[2];for(auto &v:args)VariantInit(&v);
    args[0].vt=VT_UI4;args[0].ulVal=0;
    args[1].vt=VT_BSTR;args[1].bstrVal=SysAllocString(L"1");
    bool ok=Invoke(service,L"Play",args,2);
    for(auto &v:args)VariantClear(&v);
    return ok;
}
void DiscoverWorker() {
    HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    IUPnPDeviceFinder *finder=nullptr;
    IUPnPDevices *devices=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_UPnPDeviceFinder,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&finder));
    if(SUCCEEDED(hr)&&finder){
        BSTR type=SysAllocString(L"urn:schemas-upnp-org:device:MediaRenderer:1");
        hr=type?finder->FindByType(type,0,&devices):E_OUTOFMEMORY;
        if(type)SysFreeString(type);
    }
    if(SUCCEEDED(hr)&&devices&&!g_stop.load()){
        IUnknown *enumerator=nullptr;
        if(SUCCEEDED(devices->get__NewEnum(&enumerator))&&enumerator){
            IEnumVARIANT *items=nullptr;
            if(SUCCEEDED(enumerator->QueryInterface(IID_PPV_ARGS(&items)))&&items){
                VARIANT item;VariantInit(&item);ULONG fetched=0;
                while(!g_stop.load()&&items->Next(1,&item,&fetched)==S_OK){
                    IUPnPDevice *device=nullptr;
                    IUnknown *deviceUnknown=nullptr;
                    if(item.vt==VT_DISPATCH&&item.pdispVal)deviceUnknown=item.pdispVal;
                    else if(item.vt==VT_UNKNOWN)deviceUnknown=item.punkVal;
                    if(deviceUnknown)deviceUnknown->QueryInterface(IID_PPV_ARGS(&device));
                    if(device){
                        BSTR friendly=nullptr;device->get_FriendlyName(&friendly);
                        DlnaReceiver receiver;receiver.name=BstrValue(friendly);
                        IUPnPServices *services=nullptr;
                        if(SUCCEEDED(device->get_Services(&services))&&services){
                            IUnknown *serviceEnum=nullptr;
                            if(SUCCEEDED(services->get__NewEnum(&serviceEnum))&&serviceEnum){
                                IEnumVARIANT *serviceItems=nullptr;
                                if(SUCCEEDED(serviceEnum->QueryInterface(IID_PPV_ARGS(&serviceItems)))&&serviceItems){
                                    VARIANT sv;VariantInit(&sv);ULONG got=0;
                                    while(serviceItems->Next(1,&sv,&got)==S_OK){
                                        IUPnPService *service=nullptr;
                                        IUnknown *serviceUnknown=nullptr;
                                        if(sv.vt==VT_DISPATCH&&sv.pdispVal)serviceUnknown=sv.pdispVal;
                                        else if(sv.vt==VT_UNKNOWN)serviceUnknown=sv.punkVal;
                                        if(serviceUnknown)serviceUnknown->QueryInterface(IID_PPV_ARGS(&service));
                                        if(service){
                                            BSTR type=nullptr;service->get_ServiceTypeIdentifier(&type);
                                            std::wstring serviceType=BstrValue(type);
                                            if(serviceType.find(L":service:AVTransport:")!=std::wstring::npos&&!receiver.avTransport){
                                                receiver.avTransport=service;service=nullptr;
                                            }
                                            if(service)service->Release();
                                        }
                                        VariantClear(&sv);
                                    }
                                    serviceItems->Release();
                                }
                                serviceEnum->Release();
                            }
                            services->Release();
                        }
                        if(receiver.avTransport&&!receiver.name.empty()){
                            IStream *stream=nullptr;
                            if(SUCCEEDED(CoMarshalInterThreadInterfaceInStream(IID_IUPnPService,receiver.avTransport,&stream))){
                                receiver.avTransport->Release();receiver.avTransport=nullptr;
                                receiver.serviceStream=stream;
                                std::lock_guard<std::mutex> guard(g_lock);
                                auto it=std::find_if(g_devices.begin(),g_devices.end(),[&](const DlnaReceiver &d){return _wcsicmp(d.name.c_str(),receiver.name.c_str())==0;});
                                if(it==g_devices.end())g_devices.push_back(std::move(receiver));
                            }
                        }
                        device->Release();
                    }
                    VariantClear(&item);
                }
                items->Release();
            }
            enumerator->Release();
        }
    }
    if(devices)devices->Release();
    if(finder)finder->Release();
    if(SUCCEEDED(init))CoUninitialize();
}
}

extern "C" void ABM_Dlna_StartDiscovery(void) {
    ABM_Dlna_StopDiscovery();
    {std::lock_guard<std::mutex> guard(g_lock);g_devices.clear();}
    g_stop=false;
    g_worker=std::thread(DiscoverWorker);
}
extern "C" void ABM_Dlna_StopDiscovery(void) {
    g_stop=true;
    if(g_worker.joinable())g_worker.join();
    std::lock_guard<std::mutex> guard(g_lock);
    if(g_selected){g_selected->Release();g_selected=nullptr;}
}
extern "C" unsigned int ABM_Dlna_DeviceCount(void) {
    std::lock_guard<std::mutex> guard(g_lock);return (unsigned int)g_devices.size();
}
extern "C" BOOL ABM_Dlna_DeviceName(unsigned int index,wchar_t *buffer,unsigned int capacity) {
    if(!buffer||!capacity)return FALSE;buffer[0]=0;
    std::lock_guard<std::mutex> guard(g_lock);
    if(index>=g_devices.size())return FALSE;
    return wcsncpy_s(buffer,capacity,g_devices[index].name.c_str(),_TRUNCATE)==0;
}
extern "C" BOOL ABM_Dlna_SelectDevice(unsigned int index) {
    std::lock_guard<std::mutex> guard(g_lock);
    if(index>=g_devices.size())return FALSE;
    DlnaReceiver &receiver=g_devices[index];
    if(!receiver.uiService&&receiver.serviceStream){
        HRESULT hr=CoGetInterfaceAndReleaseStream(receiver.serviceStream,IID_IUPnPService,(void**)&receiver.uiService);
        receiver.serviceStream=nullptr;
        if(FAILED(hr))return FALSE;
    }
    if(!receiver.uiService)return FALSE;
    if(g_selected)g_selected->Release();
    g_selected=receiver.uiService;g_selected->AddRef();
    return TRUE;
}
extern "C" BOOL ABM_Dlna_PlayUrl(const wchar_t *url) {
    if(!url||(!_wcsnicmp(url,L"http://",7)&&wcslen(url)<8)||(!_wcsnicmp(url,L"https://",8)&&wcslen(url)<9)||
       (_wcsnicmp(url,L"http://",7)&&_wcsnicmp(url,L"https://",8)))return FALSE;
    IUPnPService *service=nullptr;
    {std::lock_guard<std::mutex> guard(g_lock);if(g_selected){service=g_selected;service->AddRef();}}
    if(!service)return FALSE;
    bool ok=InvokeSetUri(service,url)&&InvokePlay(service);
    service->Release();return ok?TRUE:FALSE;
}

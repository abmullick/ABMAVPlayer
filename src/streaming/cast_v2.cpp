#define WIN32_LEAN_AND_MEAN
#define SECURITY_WIN32
#include "cast_v2.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windns.h>
#include <security.h>
#include <schannel.h>
#include <sspi.h>
#include <strsafe.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

#pragma comment(lib,"secur32.lib")
#pragma comment(lib,"dnsapi.lib")

extern "C" BOOL ABM_Dial_PlayLoungeScreenId(const char *screenId,const wchar_t *videoId);

namespace {
struct Receiver { std::wstring name; std::string host; unsigned short port=8009; };
std::mutex g_mutex;
std::vector<Receiver> g_receivers;
std::thread g_discovery;
std::atomic<bool> g_stop{false};
int g_selected=-1;
std::wstring g_error;

std::wstring Wide(const char *s) {
    if(!s||!*s)return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s,-1,nullptr,0);
    if(n<=1)return {};
    std::wstring w((size_t)n,L'\0');MultiByteToWideChar(CP_UTF8,0,s,-1,w.data(),n);w.resize((size_t)n-1);return w;
}
std::string Utf8(const std::wstring &s) {
    if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    if(n<=0)return {};
    std::string out((size_t)n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),out.data(),n,nullptr,nullptr);return out;
}
void Discover() {
    while(!g_stop.load()) {
        PDNS_RECORDA ptr=nullptr;
        DNS_STATUS st=DnsQuery_A("_googlecast._tcp.local",DNS_TYPE_PTR,DNS_QUERY_BYPASS_CACHE|DNS_QUERY_NO_HOSTS_FILE,nullptr,reinterpret_cast<PDNS_RECORD*>(&ptr),nullptr);
        if(st==ERROR_SUCCESS&&ptr) {
            for(PDNS_RECORDA p=ptr;p&&!g_stop.load();p=p->pNext) {
                if(p->wType!=DNS_TYPE_PTR||!p->Data.PTR.pNameHost)continue;
                std::string instance=p->Data.PTR.pNameHost;
                PDNS_RECORDA srv=nullptr;
                if(DnsQuery_A(instance.c_str(),DNS_TYPE_SRV,DNS_QUERY_BYPASS_CACHE|DNS_QUERY_NO_HOSTS_FILE,nullptr,reinterpret_cast<PDNS_RECORD*>(&srv),nullptr)!=ERROR_SUCCESS||!srv)continue;
                for(PDNS_RECORDA q=srv;q;q=q->pNext)if(q->wType==DNS_TYPE_SRV&&q->Data.SRV.pNameTarget) {
                    Receiver d;d.host=q->Data.SRV.pNameTarget;d.port=q->Data.SRV.wPort;
                    PDNS_RECORDA txt=nullptr;
                    if(DnsQuery_A(instance.c_str(),DNS_TYPE_TEXT,DNS_QUERY_BYPASS_CACHE|DNS_QUERY_NO_HOSTS_FILE,nullptr,reinterpret_cast<PDNS_RECORD*>(&txt),nullptr)==ERROR_SUCCESS&&txt) {
                        for(PDNS_RECORDA t=txt;t;t=t->pNext)if(t->wType==DNS_TYPE_TEXT&&t->Data.TXT.dwStringCount) {
                            for(DWORD k=0;k<t->Data.TXT.dwStringCount;k++) {
                                std::string v=t->Data.TXT.pStringArray[k];
                                if(v.rfind("fn=",0)==0)d.name=Wide(v.c_str()+3);
                            }
                        }
                        DnsRecordListFree(txt,DnsFreeRecordList);
                    }
                    if(d.name.empty())d.name=Wide(instance.c_str());
                    std::lock_guard<std::mutex> lock(g_mutex);
                    auto it=std::find_if(g_receivers.begin(),g_receivers.end(),[&](const Receiver &x){return _stricmp(x.host.c_str(),d.host.c_str())==0;});
                    if(it==g_receivers.end())g_receivers.push_back(std::move(d));else {it->name=d.name;it->port=d.port;}
                }
                DnsRecordListFree(srv,DnsFreeRecordList);
            }
            DnsRecordListFree(ptr,DnsFreeRecordList);
        }
        for(int i=0;i<20&&!g_stop.load();++i)Sleep(500);
    }
}
void PutVarint(std::string &o,unsigned v){while(v>=128){o.push_back((char)((v&127)|128));v>>=7;}o.push_back((char)v);}
void PutField(std::string &o,unsigned field,const std::string &v){PutVarint(o,(field<<3)|2);PutVarint(o,(unsigned)v.size());o+=v;}
std::string CastPacket(const std::string &src,const std::string &dst,const std::string &ns,const std::string &json) {
    std::string p;PutVarint(p,8);PutVarint(p,0);PutField(p,2,src);PutField(p,3,dst);PutField(p,4,ns);PutVarint(p,40);PutVarint(p,0);PutField(p,6,json);
    std::string frame;unsigned n=(unsigned)p.size();frame.push_back((char)(n>>24));frame.push_back((char)(n>>16));frame.push_back((char)(n>>8));frame.push_back((char)n);frame+=p;return frame;
}
class CastTls {
    SOCKET s=INVALID_SOCKET; CredHandle cred{}; CtxtHandle ctx{}; bool haveCred=false,haveCtx=false,connected=false,winsock=false;
    SecPkgContext_StreamSizes sizes{};
    bool SendRaw(const void *p,int n){const char *b=(const char*)p;while(n>0){int k=send(s,b,n,0);if(k<=0)return false;b+=k;n-=k;}return true;}
    bool RecvRaw(void *p,int n){char *b=(char*)p;while(n>0){int k=recv(s,b,n,0);if(k<=0)return false;b+=k;n-=k;}return true;}
public:
    ~CastTls(){Close();}
    void Close(){if(connected){DWORD t=SCHANNEL_SHUTDOWN;SecBuffer b{sizeof(t),SECBUFFER_TOKEN,&t};SecBufferDesc d{SECBUFFER_VERSION,1,&b};ApplyControlToken(&ctx,&d);}if(haveCtx)DeleteSecurityContext(&ctx);if(haveCred)FreeCredentialsHandle(&cred);if(s!=INVALID_SOCKET)closesocket(s);s=INVALID_SOCKET;connected=false;if(winsock){WSACleanup();winsock=false;}}
    bool Connect(const std::string &host,unsigned short port) {
        WSADATA wd{};if(WSAStartup(MAKEWORD(2,2),&wd)!=0)return false;winsock=true;
        addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;hints.ai_protocol=IPPROTO_TCP;
        addrinfo *list=nullptr;char service[16];_snprintf_s(service,sizeof(service),_TRUNCATE,"%u",port);
        if(getaddrinfo(host.c_str(),service,&hints,&list)!=0)return false;
        for(addrinfo *a=list;a;a=a->ai_next){s=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(s!=INVALID_SOCKET&&connect(s,a->ai_addr,(int)a->ai_addrlen)==0)break;if(s!=INVALID_SOCKET)closesocket(s);s=INVALID_SOCKET;}
        freeaddrinfo(list);if(s==INVALID_SOCKET)return false;
        DWORD ioTimeout=5000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&ioTimeout,sizeof(ioTimeout));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(const char*)&ioTimeout,sizeof(ioTimeout));
        SCHANNEL_CRED sc{};sc.dwVersion=SCHANNEL_CRED_VERSION;sc.dwFlags=SCH_CRED_MANUAL_CRED_VALIDATION|SCH_CRED_NO_DEFAULT_CREDS;
        TimeStamp expiry{};SECURITY_STATUS ss=AcquireCredentialsHandleW(nullptr,const_cast<wchar_t*>(UNISP_NAME_W),SECPKG_CRED_OUTBOUND,nullptr,&sc,nullptr,nullptr,&cred,&expiry);
        if(ss!=SEC_E_OK)return false;haveCred=true;
        DWORD req=ISC_REQ_SEQUENCE_DETECT|ISC_REQ_REPLAY_DETECT|ISC_REQ_CONFIDENTIALITY|ISC_REQ_EXTENDED_ERROR|ISC_REQ_ALLOCATE_MEMORY|ISC_REQ_STREAM;
        std::string incoming;bool first=true;
        for(int attempt=0;attempt<8;attempt++){
            SecBuffer inBufs[2]{};SecBufferDesc inDesc{SECBUFFER_VERSION,2,inBufs};
            if(!incoming.empty()){inBufs[0]={ (ULONG)incoming.size(),SECBUFFER_TOKEN,incoming.data()};inBufs[1]={0,SECBUFFER_EMPTY,nullptr};}
            SecBuffer outBuf{};SecBufferDesc outDesc{SECBUFFER_VERSION,1,&outBuf};
            ss=InitializeSecurityContextW(&cred,haveCtx?&ctx:nullptr,const_cast<wchar_t*>(Wide(host.c_str()).c_str()),req,0,SECURITY_NATIVE_DREP,first?nullptr:&inDesc,0,&ctx,&outDesc,&req,&expiry);
            first=false;if(ss==SEC_E_OK||ss==SEC_I_CONTINUE_NEEDED||ss==SEC_I_COMPLETE_NEEDED||ss==SEC_I_COMPLETE_AND_CONTINUE)haveCtx=true;
            if(outBuf.cbBuffer&&outBuf.pvBuffer){bool ok=SendRaw(outBuf.pvBuffer,(int)outBuf.cbBuffer);FreeContextBuffer(outBuf.pvBuffer);if(!ok)return false;}
            if(ss==SEC_E_OK||ss==SEC_I_COMPLETE_NEEDED||ss==SEC_I_COMPLETE_AND_CONTINUE){if(ss==SEC_I_COMPLETE_NEEDED||ss==SEC_I_COMPLETE_AND_CONTINUE)CompleteAuthToken(&ctx,&outDesc);break;}
            if(ss!=SEC_I_CONTINUE_NEEDED)return false;
            char buf[16384];int got=recv(s,buf,sizeof(buf),0);if(got<=0)return false;incoming.assign(buf,got);
            if(attempt==7)return false;
        }
        if(!haveCtx||QueryContextAttributesW(&ctx,SECPKG_ATTR_STREAM_SIZES,&sizes)!=SEC_E_OK)return false;
        connected=true;return true;
    }
    bool Send(const std::string &data) {
        if(!connected)return false;
        std::vector<char> buf(sizes.cbHeader+data.size()+sizes.cbTrailer);
        memcpy(buf.data()+sizes.cbHeader,data.data(),data.size());
        SecBuffer b[4]={{sizes.cbHeader,SECBUFFER_STREAM_HEADER,buf.data()},{(ULONG)data.size(),SECBUFFER_DATA,buf.data()+sizes.cbHeader},{sizes.cbTrailer,SECBUFFER_STREAM_TRAILER,buf.data()+sizes.cbHeader+data.size()},{0,SECBUFFER_EMPTY,nullptr}};
        SecBufferDesc d{SECBUFFER_VERSION,4,b};if(EncryptMessage(&ctx,0,&d,0)!=SEC_E_OK)return false;
        return SendRaw(buf.data(),(int)(b[0].cbBuffer+b[1].cbBuffer+b[2].cbBuffer));
    }
    bool Receive(std::string &data) {
        // Cast is a byte stream layered over TLS. TLS record boundaries do not
        // correspond to Cast frame boundaries, so retain plaintext until an
        // entire 4-byte-length-prefixed Cast frame is available.
        data.clear();
        std::vector<char> encrypted(65536);
        std::string pending;
        int used=0;
        for(int loops=0;loops<32;loops++) {
            if(pending.size()>=4) {
                unsigned n=((unsigned char)pending[0]<<24)|
                           ((unsigned char)pending[1]<<16)|
                           ((unsigned char)pending[2]<<8)|
                           (unsigned char)pending[3];
                if(n==0||n>1024*1024)return false;
                if(pending.size()>=(size_t)n+4) {
                    data.assign(pending.data(),(size_t)n+4);
                    return true;
                }
            }
            if(used==(int)encrypted.size())return false;
            int got=recv(s,encrypted.data()+used,(int)encrypted.size()-used,0);
            if(got<=0)return false;
            used+=got;
            SecBuffer b[4]={{(ULONG)used,SECBUFFER_DATA,encrypted.data()},
                            {0,SECBUFFER_EMPTY,nullptr},{0,SECBUFFER_EMPTY,nullptr},
                            {0,SECBUFFER_EMPTY,nullptr}};
            SecBufferDesc d{SECBUFFER_VERSION,4,b};
            SECURITY_STATUS st=DecryptMessage(&ctx,&d,0,nullptr);
            if(st==SEC_E_INCOMPLETE_MESSAGE)continue;
            if(st!=SEC_E_OK&&st!=SEC_I_RENEGOTIATE)return false;
            for(auto &x:b) {
                if(x.BufferType==SECBUFFER_DATA&&x.cbBuffer)
                    pending.append((const char*)x.pvBuffer,x.cbBuffer);
            }
            // Preserve encrypted bytes following the TLS record. SChannel may
            // return them as SECBUFFER_EXTRA when TCP coalesces records.
            int extra=0;
            for(auto &x:b)if(x.BufferType==SECBUFFER_EXTRA)extra=(int)x.cbBuffer;
            if(extra>0) {
                memmove(encrypted.data(),encrypted.data()+used-extra,(size_t)extra);
                used=extra;
            } else used=0;
        }
        return false;
    }
};
std::string JsonValue(const std::string &j,const char *key) {
    std::string k="\"";k+=key;k+="\"";size_t p=j.find(k);if(p==std::string::npos)return {};
    p=j.find(':',p+k.size());if(p==std::string::npos)return {};p=j.find_first_not_of(" \t\r\n",p+1);if(p==std::string::npos||j[p]!='"')return {};
    size_t e=j.find('"',p+1);return e==std::string::npos?std::string():j.substr(p+1,e-p-1);
}
bool SendCast(CastTls &tls,const std::string &src,const std::string &dst,const std::string &ns,const std::string &json){return tls.Send(CastPacket(src,dst,ns,json));}
bool ReceiveCast(CastTls &tls,std::string &json) {
    std::string frame;if(!tls.Receive(frame)||frame.size()<4)return false;
    unsigned n=((unsigned char)frame[0]<<24)|((unsigned char)frame[1]<<16)|((unsigned char)frame[2]<<8)|(unsigned char)frame[3];
    if(n>frame.size()-4)return false;
    const char *p=frame.data()+4,*end=p+n;
    while(p<end){unsigned key=0,shift=0;while(p<end){unsigned char c=*p++;key|=(unsigned)(c&127)<<shift;if(!(c&128))break;shift+=7;}unsigned field=key>>3,wire=key&7;
        if(wire==2){unsigned len=0;shift=0;while(p<end){unsigned char c=*p++;len|=(unsigned)(c&127)<<shift;if(!(c&128))break;shift+=7;}if(len>(unsigned)(end-p))return false;if(field==6)json.assign(p,len);p+=len;}
        else if(wire==0){while(p<end&&((unsigned char)*p&128))++p;if(p<end)++p;}else return false;
    }return !json.empty();
}
bool WaitFor(const std::string &host,unsigned short port,const char *needle,std::string &payload) {
    CastTls tls;if(!tls.Connect(host,port))return false;
    SendCast(tls,"sender-0","receiver-0","urn:x-cast:com.google.cast.tp.connection","{\"type\":\"CONNECT\"}");
    SendCast(tls,"sender-0","receiver-0","urn:x-cast:com.google.cast.receiver","{\"type\":\"GET_STATUS\",\"requestId\":1}");
    for(int i=0;i<6;i++){std::string j;if(!ReceiveCast(tls,j))continue;if(j.find(needle)!=std::string::npos){payload=j;return true;}}
    return false;
}
bool Launch(const Receiver &r,const wchar_t *video) {
    CastTls tls;if(!tls.Connect(r.host,r.port)){g_error=L"Could not establish a secure Google Cast connection to this TV.";return false;}
    const std::string src="sender-0",receiver="receiver-0",conn="urn:x-cast:com.google.cast.tp.connection",recvns="urn:x-cast:com.google.cast.receiver";
    if(!SendCast(tls,src,receiver,conn,"{\"type\":\"CONNECT\"}")||
       !SendCast(tls,src,receiver,recvns,"{\"type\":\"LAUNCH\",\"appId\":\"233637DE\",\"requestId\":1}")){g_error=L"Could not send the YouTube app launch request.";return false;}
    std::string transport;
    for(int i=0;i<12;i++){std::string j;if(!ReceiveCast(tls,j))continue;transport=JsonValue(j,"transportId");if(!transport.empty())break;}
    if(transport.empty()){g_error=L"The Cast receiver did not start the YouTube application (no transport session was returned).";return false;}
    if(!SendCast(tls,src,transport,conn,"{\"type\":\"CONNECT\"}")){g_error=L"Could not connect to the YouTube receiver application.";return false;}
    const std::string mdx="urn:x-cast:com.google.youtube.mdx";
    if(!SendCast(tls,src,transport,mdx,"{\"type\":\"getMdxSessionStatus\"}")){g_error=L"Could not query the YouTube TV session.";return false;}
    std::string screen;
    for(int i=0;i<12;i++){std::string j;if(!ReceiveCast(tls,j))continue;if(j.find("mdxSessionStatus")!=std::string::npos){screen=JsonValue(j,"screenId");if(!screen.empty())break;}}
    if(screen.empty()){g_error=L"YouTube opened, but did not provide an MDX screen ID. This TV may not expose YouTube remote control.";return false;}
    if(!ABM_Dial_PlayLoungeScreenId(screen.c_str(),video)){g_error=L"YouTube returned a screen ID, but the video handoff through its remote-control service failed.";return false;}
    g_error.clear();return true;
}
}
extern "C" void ABM_CastV2_StartDiscovery(void) {
    if(g_discovery.joinable()&&!g_stop.load())return;
    ABM_CastV2_StopDiscovery();{std::lock_guard<std::mutex> l(g_mutex);g_receivers.clear();g_selected=-1;}
    g_stop=false;g_discovery=std::thread(Discover);
}
extern "C" void ABM_CastV2_StopDiscovery(void) {
    g_stop=true;if(g_discovery.joinable())g_discovery.join();
}
extern "C" unsigned int ABM_CastV2_DeviceCount(void) {
    std::lock_guard<std::mutex> l(g_mutex);return (unsigned int)g_receivers.size();
}
extern "C" BOOL ABM_CastV2_DeviceName(unsigned int i,wchar_t *b,unsigned int c) {
    if(!b||!c)return FALSE;b[0]=0;std::lock_guard<std::mutex> l(g_mutex);if(i>=g_receivers.size())return FALSE;
    return SUCCEEDED(StringCchCopyW(b,c,g_receivers[i].name.c_str()));
}
extern "C" BOOL ABM_CastV2_SelectDevice(unsigned int i) {
    std::lock_guard<std::mutex> l(g_mutex);if(i>=g_receivers.size())return FALSE;g_selected=(int)i;return TRUE;
}
extern "C" BOOL ABM_CastV2_LaunchYouTube(const wchar_t *id) {
    if(!id||wcslen(id)!=11)return FALSE;
    for(const wchar_t *p=id;*p;++p)if(!((*p>=L'a'&&*p<=L'z')||(*p>=L'A'&&*p<=L'Z')||(*p>=L'0'&&*p<=L'9')||*p==L'_'||*p==L'-'))return FALSE;
    Receiver r;{std::lock_guard<std::mutex> l(g_mutex);if(g_selected<0||(size_t)g_selected>=g_receivers.size()){g_error=L"No Google Cast V2 receiver is selected.";return FALSE;}r=g_receivers[(size_t)g_selected];}
    return Launch(r,id)?TRUE:FALSE;
}
extern "C" BOOL ABM_CastV2_LastError(wchar_t *b,unsigned int c) {
    if(!b||!c)return FALSE;return SUCCEEDED(StringCchCopyW(b,c,g_error.c_str()));
}

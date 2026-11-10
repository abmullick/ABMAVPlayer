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
#include <cstdio>
#include <cstdarg>
#include <map>
#include <cctype>

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
std::mutex g_logMutex;
void Diag(const wchar_t *fmt, ...) {
    wchar_t dir[MAX_PATH]{};
    DWORD n=GetTempPathW(MAX_PATH,dir);
    if(!n || n>=MAX_PATH) return;
    std::wstring path=dir; path+=L"ABMAVPlayer-GoCast-diagnostics.log";
    SYSTEMTIME st{}; GetLocalTime(&st);
    wchar_t line[2048]{};
    int pos=_snwprintf_s(line,_countof(line),_TRUNCATE,L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,st.wMilliseconds);
    va_list ap; va_start(ap,fmt); _vsnwprintf_s(line+pos,_countof(line)-(size_t)pos,_TRUNCATE,fmt,ap); va_end(ap);
    size_t len=wcslen(line); line[len++]=L'\r'; line[len++]=L'\n';
    std::lock_guard<std::mutex> lock(g_logMutex);
    HANDLE h=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h!=INVALID_HANDLE_VALUE) {
        int bytes=WideCharToMultiByte(CP_UTF8,0,line,(int)len,nullptr,0,nullptr,nullptr);
        if(bytes>0) { std::string out((size_t)bytes,'\0'); WideCharToMultiByte(CP_UTF8,0,line,(int)len,out.data(),bytes,nullptr,nullptr); DWORD written=0; WriteFile(h,out.data(),(DWORD)out.size(),&written,nullptr); }
        CloseHandle(h);
    }
}

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
std::atomic<SOCKET> g_mdnsSocket{INVALID_SOCKET};

static unsigned ReadU16(const unsigned char *p) {
    return ((unsigned)p[0]<<8)|(unsigned)p[1];
}
static bool ReadDnsName(const unsigned char *packet,size_t length,size_t &offset,std::string &name) {
    size_t pos=offset, resume=0;
    bool jumped=false;
    unsigned hops=0;
    name.clear();
    while(pos<length && hops++<128) {
        unsigned char n=packet[pos++];
        if(n==0) {
            if(!jumped) offset=pos; else offset=resume;
            return true;
        }
        if((n&0xC0)==0xC0) {
            if(pos>=length) return false;
            size_t pointer=((size_t)(n&0x3F)<<8)|packet[pos++];
            if(pointer>=length) return false;
            if(!jumped){resume=pos;jumped=true;}
            pos=pointer;
            continue;
        }
        if((n&0xC0)!=0 || n>63 || pos+n>length) return false;
        if(!name.empty()) name.push_back('.');
        name.append((const char*)packet+pos,n);
        pos+=n;
    }
    return false;
}
static std::string LowerDnsName(std::string value) {
    while(!value.empty()&&value.back()=='.') value.pop_back();
    for(char &c:value)c=(char)tolower((unsigned char)c);
    return value;
}
static bool MakeMdnsQuery(std::vector<unsigned char> &q) {
    q.assign(12,0);
    q[5]=1; // QDCOUNT=1
    const char *labels[]={"_googlecast","_tcp","local"};
    for(const char *label:labels) {
        size_t n=strlen(label);
        q.push_back((unsigned char)n);
        q.insert(q.end(),label,label+n);
    }
    q.push_back(0);
    q.push_back(0);q.push_back(12); // PTR
    q.push_back(0);q.push_back(1); // IN class
    return true;
}
struct MdnsRecord {
    std::string owner;
    unsigned type=0;
    std::string target;
    std::string text;
    unsigned short port=0;
    std::string address;
};
static void ParseMdnsPacket(const unsigned char *packet,size_t length,std::vector<MdnsRecord> &records) {
    if(length<12)return;
    unsigned questions=ReadU16(packet+4);
    unsigned total=(unsigned)ReadU16(packet+6)+(unsigned)ReadU16(packet+8)+(unsigned)ReadU16(packet+10);
    size_t off=12;
    for(unsigned i=0;i<questions;i++) {
        std::string qname;
        if(!ReadDnsName(packet,length,off,qname)||off+4>length)return;
        off+=4;
    }
    for(unsigned i=0;i<total;i++) {
        MdnsRecord r;
        if(!ReadDnsName(packet,length,off,r.owner)||off+10>length)return;
        r.type=ReadU16(packet+off);
        unsigned klass=ReadU16(packet+off+2)&0x7FFF;
        unsigned rdlen=ReadU16(packet+off+8);
        off+=10;
        if(off+rdlen>length)return;
        size_t rdata=off;
        if(klass!=1){off+=rdlen;continue;}
        if(r.type==12) {
            size_t p=rdata;
            ReadDnsName(packet,length,p,r.target);
        } else if(r.type==33&&rdlen>=7) {
            r.port=(unsigned short)ReadU16(packet+rdata+4);
            size_t p=rdata+6;
            ReadDnsName(packet,length,p,r.target);
        } else if(r.type==16) {
            size_t p=rdata,limit=rdata+rdlen;
            while(p<limit) {
                unsigned n=packet[p++];
                if(p+n>limit)break;
                std::string item((const char*)packet+p,n);
                if(item.rfind("fn=",0)==0)r.text=item.substr(3);
                p+=n;
            }
        } else if(r.type==1&&rdlen==4) {
            char ip[INET_ADDRSTRLEN]{};
            if(inet_ntop(AF_INET,packet+rdata,ip,sizeof(ip)))r.address=ip;
        }
        if(r.type==12||r.type==16||r.type==33||r.type==1)records.push_back(std::move(r));
        off+=rdlen;
    }
}
void Discover() {
    WSADATA wd{};
    int wsa=WSAStartup(MAKEWORD(2,2),&wd);
    if(wsa!=0){Diag(L"mDNS: WSAStartup failed=%d",wsa);g_stop=true;return;}
    SOCKET sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(sock==INVALID_SOCKET){Diag(L"mDNS: socket creation failed WSA=%d",WSAGetLastError());WSACleanup();g_stop=true;return;}
    BOOL reuse=TRUE;
    setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(const char*)&reuse,sizeof(reuse));
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(5353);local.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(sock,(sockaddr*)&local,sizeof(local))==SOCKET_ERROR) {
        Diag(L"mDNS: bind UDP 5353 failed WSA=%d",WSAGetLastError());closesocket(sock);WSACleanup();g_stop=true;return;
    }
    ip_mreq membership{};membership.imr_multiaddr.s_addr=inet_addr("224.0.0.251");membership.imr_interface.s_addr=htonl(INADDR_ANY);
    if(setsockopt(sock,IPPROTO_IP,IP_ADD_MEMBERSHIP,(const char*)&membership,sizeof(membership))==SOCKET_ERROR) {
        Diag(L"mDNS: multicast join failed WSA=%d",WSAGetLastError());closesocket(sock);WSACleanup();g_stop=true;return;
    }
    DWORD timeout=1000;
    setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout));
    int multicastTtl=255;
    setsockopt(sock,IPPROTO_IP,IP_MULTICAST_TTL,(const char*)&multicastTtl,sizeof(multicastTtl));
    g_mdnsSocket.store(sock);
    sockaddr_in multicast{};multicast.sin_family=AF_INET;multicast.sin_port=htons(5353);multicast.sin_addr.s_addr=inet_addr("224.0.0.251");
    std::vector<unsigned char> query;MakeMdnsQuery(query);
    DWORD lastQuery=0;
    std::map<std::string,Receiver> knownServices;
    std::map<std::string,std::string> knownAddresses;
    Diag(L"mDNS: listening on UDP 5353 and joined 224.0.0.251");
    while(!g_stop.load()) {
        DWORD now=GetTickCount();
        if(lastQuery==0||now-lastQuery>=5000) {
            int sent=sendto(sock,(const char*)query.data(),(int)query.size(),0,(sockaddr*)&multicast,sizeof(multicast));
            if(sent==SOCKET_ERROR)Diag(L"mDNS: multicast query send failed WSA=%d",WSAGetLastError());
            else Diag(L"mDNS: sent PTR query for _googlecast._tcp.local");
            lastQuery=now;
        }
        unsigned char packet[9000];
        sockaddr_in from{};int fromLen=sizeof(from);
        int got=recvfrom(sock,(char*)packet,sizeof(packet),0,(sockaddr*)&from,&fromLen);
        if(got==SOCKET_ERROR) {
            int e=WSAGetLastError();
            if(e==WSAETIMEDOUT||e==WSAEWOULDBLOCK)continue;
            if(g_stop.load())break;
            Diag(L"mDNS: recvfrom failed WSA=%d",e);continue;
        }
        Diag(L"mDNS: received %d-byte packet from %S",got,inet_ntoa(from.sin_addr));
        std::vector<MdnsRecord> records;
        ParseMdnsPacket(packet,(size_t)got,records);
        if(records.empty())Diag(L"mDNS: packet contained no supported PTR/SRV/TXT/A records");
        for(const auto &r:records) {
            if(r.type==1&&!r.address.empty())knownAddresses[LowerDnsName(r.owner)]=r.address;
            if(r.type==12&&LowerDnsName(r.owner)=="_googlecast._tcp.local"&&!r.target.empty())
                knownServices[LowerDnsName(r.target)].name=Wide(r.target.c_str());
            if(r.type==33) {
                auto &d=knownServices[LowerDnsName(r.owner)];
                d.host=r.target;d.port=r.port;
            }
            if(r.type==16&&!r.text.empty())knownServices[LowerDnsName(r.owner)].name=Wide(r.text.c_str());
        }
        for(auto &entry:knownServices) {
            Receiver d=entry.second;
            auto ip=knownAddresses.find(LowerDnsName(d.host));
            if(ip!=knownAddresses.end())d.host=ip->second;
            if(d.host.empty()||d.port==0)continue;
            if(d.name.empty())d.name=Wide(entry.first.c_str());
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it=std::find_if(g_receivers.begin(),g_receivers.end(),[&](const Receiver &x){return _stricmp(x.host.c_str(),d.host.c_str())==0;});
            if(it==g_receivers.end()) {
                Diag(L"mDNS: receiver discovered name=%s host=%S port=%u",d.name.c_str(),d.host.c_str(),d.port);
                g_receivers.push_back(std::move(d));
            } else {it->name=d.name;it->port=d.port;}
        }
    }
    SOCKET expected=sock;
    if(g_mdnsSocket.compare_exchange_strong(expected,INVALID_SOCKET))closesocket(sock);
    Diag(L"mDNS: discovery worker stopped");
    WSACleanup();
    g_stop=true;
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
    std::string castPending;
    bool SendRaw(const void *p,int n){const char *b=(const char*)p;while(n>0){int k=send(s,b,n,0);if(k<=0)return false;b+=k;n-=k;}return true;}
    bool RecvRaw(void *p,int n){char *b=(char*)p;while(n>0){int k=recv(s,b,n,0);if(k<=0)return false;b+=k;n-=k;}return true;}
public:
    ~CastTls(){Close();}
    void Close(){if(connected){DWORD t=SCHANNEL_SHUTDOWN;SecBuffer b{sizeof(t),SECBUFFER_TOKEN,&t};SecBufferDesc d{SECBUFFER_VERSION,1,&b};ApplyControlToken(&ctx,&d);}if(haveCtx)DeleteSecurityContext(&ctx);if(haveCred)FreeCredentialsHandle(&cred);if(s!=INVALID_SOCKET)closesocket(s);s=INVALID_SOCKET;connected=false;if(winsock){WSACleanup();winsock=false;}}
    bool Connect(const std::string &host,unsigned short port) {
        WSADATA wd{};int wsa=WSAStartup(MAKEWORD(2,2),&wd);if(wsa!=0){Diag(L"WSAStartup failed=%d",wsa);return false;}winsock=true;
        addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;hints.ai_protocol=IPPROTO_TCP;
        addrinfo *list=nullptr;char service[16];_snprintf_s(service,sizeof(service),_TRUNCATE,"%u",port);
        int gai=getaddrinfo(host.c_str(),service,&hints,&list);if(gai!=0){Diag(L"getaddrinfo failed host=%S port=%u error=%d",host.c_str(),port,gai);return false;}
        for(addrinfo *a=list;a;a=a->ai_next){s=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(s!=INVALID_SOCKET&&connect(s,a->ai_addr,(int)a->ai_addrlen)==0)break;if(s!=INVALID_SOCKET)closesocket(s);s=INVALID_SOCKET;}
        freeaddrinfo(list);if(s==INVALID_SOCKET){Diag(L"TCP connect failed host=%S port=%u WSA=%d",host.c_str(),port,WSAGetLastError());return false;} Diag(L"TCP connected host=%S port=%u",host.c_str(),port);
        DWORD ioTimeout=5000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&ioTimeout,sizeof(ioTimeout));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(const char*)&ioTimeout,sizeof(ioTimeout));
        SCHANNEL_CRED sc{};sc.dwVersion=SCHANNEL_CRED_VERSION;sc.dwFlags=SCH_CRED_MANUAL_CRED_VALIDATION|SCH_CRED_NO_DEFAULT_CREDS;
        TimeStamp expiry{};SECURITY_STATUS ss=AcquireCredentialsHandleW(nullptr,const_cast<wchar_t*>(UNISP_NAME_W),SECPKG_CRED_OUTBOUND,nullptr,&sc,nullptr,nullptr,&cred,&expiry);
        if(ss!=SEC_E_OK){Diag(L"AcquireCredentialsHandle failed status=0x%08X",(unsigned)ss);return false;}haveCred=true;
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
            if(ss!=SEC_I_CONTINUE_NEEDED){Diag(L"TLS handshake failed status=0x%08X",(unsigned)ss);return false;}
            char buf[16384];int got=recv(s,buf,sizeof(buf),0);if(got<=0)return false;incoming.assign(buf,got);
            if(attempt==7)return false;
        }
        if(!haveCtx||QueryContextAttributesW(&ctx,SECPKG_ATTR_STREAM_SIZES,&sizes)!=SEC_E_OK)return false;
        connected=true;Diag(L"TLS channel established");return true;
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
        int used=0;
        for(int loops=0;loops<32;loops++) {
            if(castPending.size()>=4) {
                unsigned n=((unsigned char)castPending[0]<<24)|
                           ((unsigned char)castPending[1]<<16)|
                           ((unsigned char)castPending[2]<<8)|
                           (unsigned char)castPending[3];
                if(n==0||n>1024*1024)return false;
                if(castPending.size()>=(size_t)n+4) {
                    data.assign(castPending.data(),(size_t)n+4);
                    castPending.erase(0,(size_t)n+4);
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
                    castPending.append((const char*)x.pvBuffer,x.cbBuffer);
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

int CheckYouTubeAvailability(const Receiver &receiver) {
    Diag(L"Checking YouTube Cast app availability receiver=%s host=%S",receiver.name.c_str(),receiver.host.c_str());
    wchar_t module[MAX_PATH]{};
    if(!GetModuleFileNameW(nullptr,module,MAX_PATH))return -1;
    std::wstring directory(module);
    size_t slash=directory.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return -1;
    directory.resize(slash+1);
    std::wstring helper=directory+L"abm-gocast.exe";
    if(GetFileAttributesW(helper.c_str())==INVALID_FILE_ATTRIBUTES){
        Diag(L"Cannot check Cast app availability: missing GoCast helper path=%s",helper.c_str());
        return -1;
    }
    std::wstring command=L"\""+helper+L"\" --check-youtube \""+Wide(receiver.host.c_str())+L"\" "+std::to_wstring(receiver.port);
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE readPipe=nullptr,writePipe=nullptr;
    if(!CreatePipe(&readPipe,&writePipe,&sa,0))return -1;
    SetHandleInformation(readPipe,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    /* Keep verbose GoCast diagnostics off the captured stdout pipe.
       The parent waits for process exit before draining stdout; piping stderr
       as well can fill the anonymous pipe and deadlock both processes. The
       dedicated YouTube launcher correctly inherits stderr separately. */
    si.wShowWindow=SW_HIDE;
    si.hStdOutput=writePipe;
    si.hStdError=GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    BOOL created=CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,directory.c_str(),&si,&pi);
    CloseHandle(writePipe);
    if(!created){Diag(L"Could not start GoCast availability helper Win32=%lu",GetLastError());CloseHandle(readPipe);return -1;}
    DWORD wait=WaitForSingleObject(pi.hProcess,15000),exitCode=1;
    if(wait==WAIT_OBJECT_0)GetExitCodeProcess(pi.hProcess,&exitCode);
    else TerminateProcess(pi.hProcess,1);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    std::string output;char buffer[512];DWORD got=0;
    if(wait==WAIT_OBJECT_0)while(ReadFile(readPipe,buffer,sizeof(buffer),&got,nullptr)&&got){if(output.size()<8192)output.append(buffer,got);}
    CloseHandle(readPipe);
    if(wait!=WAIT_OBJECT_0){Diag(L"GoCast availability helper timed out");return -1;}
    Diag(L"GoCast availability helper exited=%lu output=%S",exitCode,output.c_str());
    if(output.find("\"availability\":\"APP_AVAILABLE\"")!=std::string::npos)return 1;
    if(output.find("\"availability\":\"APP_UNAVAILABLE\"")!=std::string::npos)return 0;
    return -1;
}
int RunCastAppHelper(const Receiver &receiver,const wchar_t *mode,const wchar_t *appId,bool launch) {
    if(!appId||wcslen(appId)!=8)return -1;
    for(const wchar_t *p=appId;*p;++p)
        if(!((*p>=L'0'&&*p<=L'9')||(*p>=L'A'&&*p<=L'F')))return -1;
    wchar_t module[MAX_PATH]{};
    if(!GetModuleFileNameW(nullptr,module,MAX_PATH))return -1;
    std::wstring directory(module);
    size_t slash=directory.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return -1;
    directory.resize(slash+1);
    std::wstring helper=directory+L"abm-gocast.exe";
    if(GetFileAttributesW(helper.c_str())==INVALID_FILE_ATTRIBUTES){
        Diag(L"Cast app helper missing path=%s",helper.c_str());return -1;
    }
    std::wstring command=L"\""+helper+L"\" "+mode+L" \""+Wide(receiver.host.c_str())+L"\" "+
        std::to_wstring(receiver.port)+L" \""+appId+L"\"";
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE readPipe=nullptr,writePipe=nullptr;
    if(!CreatePipe(&readPipe,&writePipe,&sa,0))return -1;
    SetHandleInformation(readPipe,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    /* Capture only the small JSON result on stdout. GoCast writes verbose
       diagnostics to stderr. If stderr shares this pipe, the child can fill
       it while the parent waits for process exit, deadlocking both sides. */
    si.wShowWindow=SW_HIDE;
    si.hStdOutput=writePipe;
    si.hStdError=GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    BOOL created=CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,directory.c_str(),&si,&pi);
    CloseHandle(writePipe);
    if(!created){Diag(L"Could not start Cast app helper Win32=%lu",GetLastError());CloseHandle(readPipe);return -1;}
    DWORD wait=WaitForSingleObject(pi.hProcess,launch?40000:15000),exitCode=1;
    if(wait==WAIT_OBJECT_0)GetExitCodeProcess(pi.hProcess,&exitCode);else TerminateProcess(pi.hProcess,1);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    std::string output;char buffer[512];DWORD got=0;
    if(wait==WAIT_OBJECT_0)while(ReadFile(readPipe,buffer,sizeof(buffer),&got,nullptr)&&got){
        if(output.size()<8192)output.append(buffer,got);
    }
    CloseHandle(readPipe);
    Diag(L"Cast app helper app=%s mode=%s wait=%lu exit=%lu output=%S",appId,mode,wait,exitCode,output.c_str());
    if(wait!=WAIT_OBJECT_0||exitCode!=0)return -1;
    if(wcscmp(mode,L"--stop-app")==0){
        if(output.find("\"availability\":\"APP_STOPPED\"")!=std::string::npos)return 1;
        if(output.find("\"availability\":\"APP_NOT_RUNNING\"")!=std::string::npos)return 0;
        return -1;
    }
    if(wcscmp(mode,L"--app-status")==0){
        if(output.find("\"availability\":\"APP_RUNNING\"")!=std::string::npos)return 1;
        if(output.find("\"availability\":\"APP_NOT_RUNNING\"")!=std::string::npos)return 0;
        return -1;
    }
    if(launch)return output.find("\"launched\":true")!=std::string::npos?1:0;
    if(output.find("\"availability\":\"APP_AVAILABLE\"")!=std::string::npos)return 1;
    if(output.find("\"availability\":\"APP_UNAVAILABLE\"")!=std::string::npos)return 0;
    return -1;
}
int CheckCastAppAvailability(const Receiver &receiver,const wchar_t *appId) {
    return RunCastAppHelper(receiver,L"--check-app",appId,false);
}
bool LaunchCastApp(const Receiver &receiver,const wchar_t *appId) {
    return RunCastAppHelper(receiver,L"--launch-app",appId,true)==1;
}
bool Launch(const Receiver &r,const wchar_t *video) {
    Diag(L"Launch requested receiver=%s host=%S port=%u",r.name.c_str(),r.host.c_str(),r.port);
    wchar_t module[MAX_PATH]{};
    if(!GetModuleFileNameW(nullptr,module,MAX_PATH)) { Diag(L"GetModuleFileName failed Win32=%lu",GetLastError());g_error=L"Cannot locate ABM AV Player installation."; return false; }
    std::wstring directory(module);
    size_t slash=directory.find_last_of(L"\\\\/");
    if(slash==std::wstring::npos) { g_error=L"Cannot locate ABM AV Player installation folder."; return false; }
    directory.resize(slash+1);
    std::wstring helper=directory+L"abm-gocast.exe";
    DWORD attrs=GetFileAttributesW(helper.c_str());
    if(attrs==INVALID_FILE_ATTRIBUTES) { Diag(L"Missing helper path=%s Win32=%lu",helper.c_str(),GetLastError());g_error=L"GoCast helper is missing. Rebuild and reinstall ABM AV Player."; return false; }
    std::wstring videoId(video);
    std::wstring command=L"\""+helper+L"\" \""+Wide(r.host.c_str())+L"\" "+std::to_wstring(r.port)+L" \""+videoId+L"\"";
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE readPipe=nullptr,writePipe=nullptr;
    if(!CreatePipe(&readPipe,&writePipe,&sa,0)) { g_error=L"Could not create GoCast process pipe."; return false; }
    SetHandleInformation(readPipe,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    si.hStdOutput=writePipe;si.hStdError=GetStdHandle(STD_ERROR_HANDLE);si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    BOOL created=CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,directory.c_str(),&si,&pi);
    CloseHandle(writePipe);
    if(!created) { DWORD e=GetLastError();Diag(L"CreateProcess failed Win32=%lu path=%s",e,helper.c_str());CloseHandle(readPipe);g_error=L"Could not start the GoCast sender helper.";return false; }
    DWORD wait=WaitForSingleObject(pi.hProcess,45000),exitCode=1;
    if(wait==WAIT_OBJECT_0)GetExitCodeProcess(pi.hProcess,&exitCode);else TerminateProcess(pi.hProcess,1);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    std::string output;char buffer[1024];DWORD got=0;
    if(wait==WAIT_OBJECT_0) while(ReadFile(readPipe,buffer,sizeof(buffer),&got,nullptr)&&got) { if(output.size()<16384)output.append(buffer,got); }
    CloseHandle(readPipe);
    if(wait!=WAIT_OBJECT_0) { Diag(L"Helper wait failed/timed out wait=%lu Win32=%lu",wait,GetLastError());g_error=L"GoCast timed out while starting YouTube and handing off the video.";return false; }
    Diag(L"Helper exited code=%lu stdout=%S",exitCode,output.c_str());
    if(exitCode!=0) {
        size_t p=output.find("\"error\":\"");
        if(p!=std::string::npos) { p+=9;size_t e=output.find('"',p);if(e!=std::string::npos){std::string msg=output.substr(p,e-p);g_error=Wide(msg.c_str());} }
        if(g_error.empty())g_error=L"GoCast could not complete YouTube casting. See helper diagnostics.";
        return false;
    }
    g_error.clear();Diag(L"Helper completed successfully");return true;
}
}
extern "C" void ABM_CastV2_StartDiscovery(void) {
    Diag(L"========== GoCast discovery started ==========");
    if(g_discovery.joinable()&&!g_stop.load())return;
    ABM_CastV2_StopDiscovery();{std::lock_guard<std::mutex> l(g_mutex);g_receivers.clear();g_selected=-1;}
    g_stop=false;g_discovery=std::thread(Discover);
}
extern "C" void ABM_CastV2_StopDiscovery(void) {
    g_stop=true;
    SOCKET active=g_mdnsSocket.exchange(INVALID_SOCKET);
    if(active!=INVALID_SOCKET)closesocket(active); // interrupt recvfrom immediately
    if(g_discovery.joinable())g_discovery.join();
}
extern "C" unsigned int ABM_CastV2_DeviceCount(void) {
    std::lock_guard<std::mutex> l(g_mutex);return (unsigned int)g_receivers.size();
}
extern "C" BOOL ABM_CastV2_DeviceName(unsigned int i,wchar_t *b,unsigned int c) {
    if(!b||!c)return FALSE;b[0]=0;std::lock_guard<std::mutex> l(g_mutex);if(i>=g_receivers.size())return FALSE;
    return SUCCEEDED(StringCchCopyW(b,c,g_receivers[i].name.c_str()));
}
extern "C" BOOL ABM_CastV2_SelectDevice(unsigned int i) {
    std::lock_guard<std::mutex> l(g_mutex);if(i>=g_receivers.size()){Diag(L"SelectDevice rejected index=%u count=%u",i,(unsigned)g_receivers.size());return FALSE;}g_selected=(int)i;Diag(L"Selected receiver index=%u name=%s host=%S port=%u",i,g_receivers[i].name.c_str(),g_receivers[i].host.c_str(),g_receivers[i].port);return TRUE;
}

extern "C" int ABM_CastV2_CheckYouTubeAvailability(unsigned int i) {
    Receiver receiver;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if(i>=g_receivers.size())return -1;
        receiver=g_receivers[i];
    }
    return CheckYouTubeAvailability(receiver);
}

extern "C" int ABM_CastV2_CheckApplicationAvailability(unsigned int i,const wchar_t *appId) {
    Receiver receiver;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if(i>=g_receivers.size())return -1;
        receiver=g_receivers[i];
    }
    return CheckCastAppAvailability(receiver,appId);
}
extern "C" int ABM_CastV2_QueryApplicationStatus(unsigned int i,const wchar_t *appId) {
    if(!appId||!*appId)return -1;
    Receiver receiver;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if(i>=g_receivers.size())return -1;
        receiver=g_receivers[i];
    }
    return RunCastAppHelper(receiver,L"--app-status",appId,false);
}
extern "C" int ABM_CastV2_StopApplication(unsigned int i,const wchar_t *appId) {
    Receiver receiver;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if(i>=g_receivers.size())return -1;
        receiver=g_receivers[i];
    }
    int result=RunCastAppHelper(receiver,L"--stop-app",appId,true);
    return result;
}
extern "C" BOOL ABM_CastV2_LaunchApplication(unsigned int i,const wchar_t *appId) {
    Receiver receiver;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if(i>=g_receivers.size())return FALSE;
        receiver=g_receivers[i];
    }
    return LaunchCastApp(receiver,appId)?TRUE:FALSE;
}

bool LaunchYouTubeApp(const Receiver &receiver) {
    Diag(L"Launching YouTube Cast app receiver=%s host=%S",receiver.name.c_str(),receiver.host.c_str());
    wchar_t module[MAX_PATH]{};
    if(!GetModuleFileNameW(nullptr,module,MAX_PATH))return false;
    std::wstring directory(module);
    size_t slash=directory.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return false;
    directory.resize(slash+1);
    std::wstring helper=directory+L"abm-gocast.exe";
    if(GetFileAttributesW(helper.c_str())==INVALID_FILE_ATTRIBUTES){g_error=L"GoCast helper is missing. Rebuild and reinstall ABM AV Player.";return false;}
    std::wstring command=L"\""+helper+L"\" --launch-youtube \""+Wide(receiver.host.c_str())+L"\" "+std::to_wstring(receiver.port);
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE readPipe=nullptr,writePipe=nullptr;
    if(!CreatePipe(&readPipe,&writePipe,&sa,0))return false;
    SetHandleInformation(readPipe,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    si.wShowWindow=SW_HIDE;si.hStdOutput=writePipe;si.hStdError=writePipe;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    BOOL created=CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,directory.c_str(),&si,&pi);
    CloseHandle(writePipe);
    if(!created){Diag(L"Could not start GoCast YouTube launch helper Win32=%lu",GetLastError());CloseHandle(readPipe);return false;}
    DWORD wait=WaitForSingleObject(pi.hProcess,40000),exitCode=1;
    if(wait==WAIT_OBJECT_0)GetExitCodeProcess(pi.hProcess,&exitCode);else TerminateProcess(pi.hProcess,1);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    std::string output;char buffer[512];DWORD got=0;
    if(wait==WAIT_OBJECT_0)while(ReadFile(readPipe,buffer,sizeof(buffer),&got,nullptr)&&got){if(output.size()<8192)output.append(buffer,got);}
    CloseHandle(readPipe);
    if(wait!=WAIT_OBJECT_0||exitCode!=0||output.find("\"launched\":true")==std::string::npos){
        Diag(L"GoCast YouTube app launch failed wait=%lu exit=%lu output=%S",wait,exitCode,output.c_str());
        g_error=L"Google Cast could not confirm that the YouTube TV app launched. See GoCast diagnostics.";
        return false;
    }
    g_error.clear();Diag(L"YouTube TV app launch confirmed");return true;
}

extern "C" BOOL ABM_CastV2_LaunchYouTubeApp(unsigned int i) {
    Receiver receiver;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if(i>=g_receivers.size())return FALSE;
        receiver=g_receivers[i];
    }
    return LaunchYouTubeApp(receiver)?TRUE:FALSE;
}
extern "C" BOOL ABM_CastV2_LaunchYouTube(const wchar_t *id) {
    if(!id||wcslen(id)!=11){Diag(L"Launch rejected: invalid YouTube video ID");return FALSE;}
    for(const wchar_t *p=id;*p;++p)if(!((*p>=L'a'&&*p<=L'z')||(*p>=L'A'&&*p<=L'Z')||(*p>=L'0'&&*p<=L'9')||*p==L'_'||*p==L'-'))return FALSE;
    Receiver r;{std::lock_guard<std::mutex> l(g_mutex);if(g_selected<0||(size_t)g_selected>=g_receivers.size()){Diag(L"Launch failed: no selected Google Cast V2 receiver");g_error=L"No Google Cast V2 receiver is selected.";return FALSE;}r=g_receivers[(size_t)g_selected];}
    return Launch(r,id)?TRUE:FALSE;
}
extern "C" BOOL ABM_CastV2_LastError(wchar_t *b,unsigned int c) {
    if(!b||!c)return FALSE;return SUCCEEDED(StringCchCopyW(b,c,g_error.c_str()));
}

#define WIN32_LEAN_AND_MEAN
#include "metadata_engine.h"
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <strsafe.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <cstdlib>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

namespace {
static const DWORD kConnectTimeout=5000,kSendTimeout=8000,kReceiveTimeout=12000;

// Respect the public provider limits even when the user repeats searches quickly.
// TVmaze documents at least 20 requests / 10 seconds per IP; Wikidata Query
// Service is deliberately kept to one request per second to avoid burst traffic.
bool WaitForProviderRateLimit(const wchar_t *host) {
    static SRWLOCK lock=SRWLOCK_INIT;
    static ULONGLONG lastWikidata=0,lastTvmaze=0;
    AcquireSRWLockExclusive(&lock);
    ULONGLONG *last=nullptr;
    DWORD interval=0;
    if(host&&!_wcsicmp(host,L"api.tvmaze.com")) {
        last=&lastTvmaze;
        interval=550;
    } else if(host&&!_wcsicmp(host,L"query.wikidata.org")) {
        last=&lastWikidata;
        interval=1100;
    }
    if(last) {
        ULONGLONG now=GetTickCount64();
        if(*last&&now-*last<interval)Sleep((DWORD)(interval-(now-*last)));
        *last=GetTickCount64();
    }
    ReleaseSRWLockExclusive(&lock);
    return last!=nullptr;
}

std::wstring UrlEncode(const wchar_t *value) {
    if(!value)return {};
    int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,nullptr,0,nullptr,nullptr);
    if(bytes<=1)return {};
    std::string utf8((size_t)bytes,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,&utf8[0],bytes,nullptr,nullptr);
    utf8.pop_back();
    static const char hex[]="0123456789ABCDEF";
    std::string encoded;
    for(unsigned char c:utf8) {
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')encoded.push_back((char)c);
        else {encoded.push_back('%');encoded.push_back(hex[c>>4]);encoded.push_back(hex[c&15]);}
    }
    return std::wstring(encoded.begin(),encoded.end());
}
bool HttpGet(const wchar_t *host,const std::wstring &path,std::vector<wchar_t> &out,DWORD timeoutMs=kReceiveTimeout) {
    if(!WaitForProviderRateLimit(host))return false;
    HINTERNET session=WinHttpOpen(L"ABM-AV-Player/1.2 (keyless metadata engine)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session)return false;
    WinHttpSetTimeouts(session,(int)kConnectTimeout,(int)kConnectTimeout,(int)kSendTimeout,(int)timeoutMs);
    HINTERNET connect=WinHttpConnect(session,host,INTERNET_DEFAULT_HTTPS_PORT,0);
    HINTERNET request=connect?WinHttpOpenRequest(connect,L"GET",path.c_str(),nullptr,
        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
    const wchar_t *headers=L"Accept: application/json\r\nUser-Agent: ABM-AV-Player/1.2 (https://github.com/abmullick/ABMAVPlayer)\r\n";
    BOOL ok=request&&WinHttpSendRequest(request,headers,(DWORD)-1L,WINHTTP_NO_REQUEST_DATA,0,0,0)
        &&WinHttpReceiveResponse(request,nullptr);
    DWORD status=0,statusSize=sizeof(status);
    if(ok)ok=WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&status,&statusSize,WINHTTP_NO_HEADER_INDEX)&&status>=200&&status<300;
    std::vector<BYTE> bytes;
    while(ok) {
        DWORD available=0;
        if(!WinHttpQueryDataAvailable(request,&available)){ok=FALSE;break;}
        if(!available)break;
        if(bytes.size()+available>8*1024*1024){ok=FALSE;break;}
        size_t old=bytes.size();bytes.resize(old+available);DWORD got=0;
        if(!WinHttpReadData(request,bytes.data()+old,available,&got)){ok=FALSE;break;}
        bytes.resize(old+got);if(!got)break;
    }
    if(request)WinHttpCloseHandle(request);if(connect)WinHttpCloseHandle(connect);WinHttpCloseHandle(session);
    if(!ok||bytes.empty())return false;
    int chars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(LPCCH)bytes.data(),(int)bytes.size(),nullptr,0);
    if(chars<=0)return false;
    out.assign((size_t)chars+1,L'\0');
    int written=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(LPCCH)bytes.data(),(int)bytes.size(),out.data(),chars);
    if(written<=0)return false;out[(size_t)written]=0;return true;
}
const wchar_t *FindObjectEnd(const wchar_t *start) {
    if(!start||*start!=L'{')return start;
    int depth=0;bool quoted=false,escaped=false;
    for(const wchar_t *p=start;*p;p++) {
        if(quoted){if(escaped)escaped=false;else if(*p==L'\\')escaped=true;else if(*p==L'"')quoted=false;continue;}
        if(*p==L'"'){quoted=true;continue;}
        if(*p==L'{')depth++;else if(*p==L'}'&&--depth==0)return p+1;
    }
    return start;
}
bool JsonString(const wchar_t *object,const wchar_t *end,const wchar_t *key,wchar_t *out,size_t cap) {
    if(!out||!cap)return false;out[0]=0;
    wchar_t needle[96];if(FAILED(StringCchPrintfW(needle,_countof(needle),L"\"%s\"",key)))return false;
    const wchar_t *p=object;
    while(p&&p<end) {
        p=wcsstr(p,needle);if(!p||p>=end)return false;p+=wcslen(needle);
        while(p<end&&iswspace(*p))p++;if(p>=end||*p++!=L':')continue;
        while(p<end&&iswspace(*p))p++;if(p>=end||*p++!=L'"')return false;
        size_t n=0;
        while(p<end&&*p&&*p!=L'"'&&n+1<cap) {
            wchar_t c=*p++;
            if(c==L'\\'&&p<end) {
                wchar_t e=*p++;
                if(e==L'n'||e==L'r'||e==L't')c=L' ';
                else if(e==L'u'&&end-p>=4){wchar_t hex[5]={p[0],p[1],p[2],p[3],0};c=(wchar_t)wcstoul(hex,nullptr,16);p+=4;}
                else c=e;
            }
            out[n++]=c;
        }
        out[n]=0;return true;
    }
    return false;
}
bool BindingValue(const wchar_t *object,const wchar_t *end,const wchar_t *key,wchar_t *out,size_t cap) {
    wchar_t needle[96];if(FAILED(StringCchPrintfW(needle,_countof(needle),L"\"%s\"",key)))return false;
    const wchar_t *p=wcsstr(object,needle);if(!p||p>=end)return false;
    const wchar_t *finish=FindObjectEnd(wcschr(p,L'{'));if(!finish||finish<=p||finish>end)return false;
    return JsonString(p,finish,L"value",out,cap);
}
int JsonInt(const wchar_t *object,const wchar_t *end,const wchar_t *key) {
    wchar_t needle[96];if(FAILED(StringCchPrintfW(needle,_countof(needle),L"\"%s\"",key)))return 0;
    const wchar_t *p=wcsstr(object,needle);if(!p||p>=end)return 0;p+=wcslen(needle);
    while(p<end&&iswspace(*p))p++;if(p>=end||*p++!=L':')return 0;
    while(p<end&&iswspace(*p))p++;return p<end?_wtoi(p):0;
}
std::wstring JsonEscape(const wchar_t *s) {
    std::wstring out=L"\"";
    for(;s&&*s;s++) {
        wchar_t c=*s;
        if(c==L'"'||c==L'\\'){out.push_back(L'\\');out.push_back(c);}
        else if(c==L'\r'||c==L'\n'||c==L'\t')out.push_back(L' ');
        else if(c>=0x20)out.push_back(c);
    }
    out.push_back(L'"');return out;
}
void AddField(std::wstring &out,const wchar_t *name,const wchar_t *value,bool &first) {
    if(!first)out+=L",";first=false;out+=JsonEscape(name);out+=L":";out+=JsonEscape(value?value:L"");
}
std::wstring StripHtml(const wchar_t *s) {
    std::wstring out;bool tag=false;
    for(;s&&*s;s++){if(*s==L'<'){tag=true;continue;}if(*s==L'>'){tag=false;out.push_back(L' ');continue;}if(!tag)out.push_back(*s);}
    while(!out.empty()&&iswspace(out.front()))out.erase(out.begin());
    while(!out.empty()&&iswspace(out.back()))out.pop_back();return out;
}
std::wstring UrlDecode(const wchar_t *value) {
    if(!value)return {};
    int bytes=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    if(bytes<=1)return {};
    std::string encoded((size_t)bytes,'\\0');
    WideCharToMultiByte(CP_UTF8,0,value,-1,&encoded[0],bytes,nullptr,nullptr);
    encoded.pop_back();
    std::string decoded;
    for(size_t i=0;i<encoded.size();i++) {
        if(encoded[i]== '%' && i+2<encoded.size()) {
            auto hexValue=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
            int hi=hexValue(encoded[i+1]),lo=hexValue(encoded[i+2]);
            if(hi>=0&&lo>=0){decoded.push_back((char)((hi<<4)|lo));i+=2;continue;}
        }
        decoded.push_back(encoded[i]);
    }
    int chars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,decoded.data(),(int)decoded.size(),nullptr,0);
    if(chars<=0)return {};
    std::wstring result((size_t)chars,L'\\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,decoded.data(),(int)decoded.size(),&result[0],chars);
    return result;
}
std::wstring CommonsUrl(const wchar_t *file) {
    if(!file||!*file)return {};
    std::wstring clean=file;
    const wchar_t *specialHttp=L"http://commons.wikimedia.org/wiki/Special:FilePath/";
    const wchar_t *specialHttps=L"https://commons.wikimedia.org/wiki/Special:FilePath/";
    const wchar_t *uploadHttp=L"http://upload.wikimedia.org/wikipedia/commons/";
    const wchar_t *uploadHttps=L"https://upload.wikimedia.org/wikipedia/commons/";
    if(!_wcsnicmp(clean.c_str(),uploadHttps,wcslen(uploadHttps)))return clean;
    if(!_wcsnicmp(clean.c_str(),uploadHttp,wcslen(uploadHttp)))return L"https"+clean.substr(4);
    if(!_wcsnicmp(clean.c_str(),specialHttps,wcslen(specialHttps)))clean.erase(0,wcslen(specialHttps));
    else if(!_wcsnicmp(clean.c_str(),specialHttp,wcslen(specialHttp)))clean.erase(0,wcslen(specialHttp));
    size_t query=clean.find_first_of(L"?#");if(query!=std::wstring::npos)clean.erase(query);
    clean=UrlDecode(clean.c_str());
    if(clean.rfind(L"File:",0)==0)clean.erase(0,5);
    if(clean.empty())return {};
    int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,clean.c_str(),-1,nullptr,0,nullptr,nullptr);
    if(bytes<=1)return {};
    std::string utf8((size_t)bytes,'\\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,clean.c_str(),-1,&utf8[0],bytes,nullptr,nullptr);
    utf8.pop_back();
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;BYTE digest[16]={};DWORD digestBytes=sizeof(digest);
    BOOL ok=CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT)
        &&CryptCreateHash(provider,CALG_MD5,0,0,&hash)
        &&CryptHashData(hash,(const BYTE*)utf8.data(),(DWORD)utf8.size(),0)
        &&CryptGetHashParam(hash,HP_HASHVAL,digest,&digestBytes,0);
    if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);
    if(!ok||digestBytes!=16)return {};
    static const wchar_t hex[]=L"0123456789abcdef";
    std::wstring first,second;
    first.push_back(hex[digest[0]>>4]);first.push_back(hex[digest[0]&15]);
    second.push_back(hex[digest[1]>>4]);second.push_back(hex[digest[1]&15]);
    return L"https://upload.wikimedia.org/wikipedia/commons/"+first+L"/"+second+L"/"+UrlEncode(clean.c_str());
}
std::wstring SparqlEscape(const wchar_t *s) {
    std::wstring out;
    for(;s&&*s;s++){if(*s==L'\\'||*s==L'"')out.push_back(L'\\');if(*s==L'\r'||*s==L'\n')out.push_back(L' ');else out.push_back(*s);}
    return out;
}
std::wstring MovieQuery(const wchar_t *filter,const wchar_t *query) {
    std::wstring q=L"SELECT ?item ?itemLabel ?itemDescription (MIN(?dateValue) AS ?date) (SAMPLE(?imageValue) AS ?image) (GROUP_CONCAT(DISTINCT ?genreLabel; separator=\" | \") AS ?genres) (GROUP_CONCAT(DISTINCT ?directorLabel; separator=\" | \") AS ?directors) (GROUP_CONCAT(DISTINCT ?castLabel; separator=\" | \") AS ?cast) WHERE { ";
    q+=filter;
    if(query&&*query){q+=L" ?item rdfs:label ?searchLabel . FILTER(LANG(?searchLabel)=\"en\" && CONTAINS(LCASE(STR(?searchLabel)),LCASE(\"";q+=SparqlEscape(query);q+=L"\"))) ";}
    q+=L" OPTIONAL { ?item wdt:P577 ?dateValue . } OPTIONAL { ?item wdt:P18 ?imageValue . } OPTIONAL { ?item wdt:P136 ?genre . ?genre rdfs:label ?genreLabel . FILTER(LANG(?genreLabel)=\"en\") } OPTIONAL { ?item wdt:P57 ?director . ?director rdfs:label ?directorLabel . FILTER(LANG(?directorLabel)=\"en\") } OPTIONAL { ?item wdt:P161 ?actor . ?actor rdfs:label ?castLabel . FILTER(LANG(?castLabel)=\"en\") } SERVICE wikibase:label { bd:serviceParam wikibase:language \"en\". } } GROUP BY ?item ?itemLabel ?itemDescription LIMIT 30";
    return q;
}
bool QueryWikidata(const std::wstring &sparql,std::vector<wchar_t> &json) {
    std::wstring path=L"/sparql?format=json&query="+UrlEncode(sparql.c_str());
    return HttpGet(L"query.wikidata.org",path,json,20000);
}
bool MusicArtworkRateLimit() {
    static SRWLOCK lock=SRWLOCK_INIT;
    static ULONGLONG lastRequest=0;
    AcquireSRWLockExclusive(&lock);
    ULONGLONG now=GetTickCount64();
    if(lastRequest&&now-lastRequest<2000)Sleep((DWORD)(2000-(now-lastRequest)));
    lastRequest=GetTickCount64();
    ReleaseSRWLockExclusive(&lock);
    return true;
}
std::wstring MusicAlbumQuery(const wchar_t *artist,const wchar_t *album) {
    std::wstring q=L"SELECT ?item ?itemLabel ?itemDescription (MIN(?dateValue) AS ?date) (SAMPLE(?imageValue) AS ?image) WHERE { ";
    q+=L"?item wdt:P31/wdt:P279* wd:Q482994 . ";
    q+=L"?item rdfs:label ?searchLabel . FILTER(LANG(?searchLabel)=\"en\" && CONTAINS(LCASE(STR(?searchLabel)),LCASE(\"";
    q+=SparqlEscape(album);q+=L"\"))) ";
    q+=L"?item wdt:P175 ?performer . ?performer rdfs:label ?artistLabel . FILTER(LANG(?artistLabel)=\"en\" && CONTAINS(LCASE(STR(?artistLabel)),LCASE(\"";
    q+=SparqlEscape(artist);q+=L"\"))) ";
    q+=L"OPTIONAL { ?item wdt:P577 ?dateValue . } OPTIONAL { ?item wdt:P18 ?imageValue . } ";
    q+=L"SERVICE wikibase:label { bd:serviceParam wikibase:language \"en\". } } GROUP BY ?item ?itemLabel ?itemDescription ORDER BY DESC(BOUND(?image)) LIMIT 12";
    return q;
}
bool BuildMovieResults(const wchar_t *raw,std::wstring &out,bool details) {
    const wchar_t *bindings=wcsstr(raw,L"\"bindings\"");if(!bindings)return false;
    const wchar_t *p=wcschr(bindings,L'[');if(!p)return false;p++;
    out=details?L"{":L"{\"results\":[";
    int count=0;
    while(*p&&count<(details?1:30)) {
        while(*p&&(iswspace(*p)||*p==L','))p++;if(*p==L']'||!*p)break;
        if(*p!=L'{'){p++;continue;}
        const wchar_t *start=p,*end=FindObjectEnd(p);if(end<=start)break;p=end;
        wchar_t uri[512]={},title[256]={},description[1800]={},date[64]={},image[512]={},genres[512]={},directors[512]={},cast[1000]={};
        BindingValue(start,end,L"item",uri,_countof(uri));
        BindingValue(start,end,L"itemLabel",title,_countof(title));
        BindingValue(start,end,L"itemDescription",description,_countof(description));
        BindingValue(start,end,L"date",date,_countof(date));if(wcslen(date)>10)date[10]=0;
        BindingValue(start,end,L"image",image,_countof(image));
        BindingValue(start,end,L"genres",genres,_countof(genres));
        BindingValue(start,end,L"directors",directors,_countof(directors));
        BindingValue(start,end,L"cast",cast,_countof(cast));
        const wchar_t *q=wcsrchr(uri,L'Q');int id=q?_wtoi(q+1):0;if(id<=0||!title[0])continue;
        std::wstring poster=CommonsUrl(image);
        if(details) {
            bool first=true;AddField(out,L"overview",description,first);AddField(out,L"release_date",date,first);
            AddField(out,L"poster_path",poster.c_str(),first);
            out+=L",\"genres\":[";
            wchar_t genreCopy[512];StringCchCopyW(genreCopy,_countof(genreCopy),genres);
            wchar_t *ctx=nullptr;wchar_t *part=wcstok_s(genreCopy,L"|",&ctx);bool f=true;
            while(part){while(*part&&iswspace(*part))part++;if(!f)out+=L",";f=false;out+=L"{\"name\":"+JsonEscape(part)+L"}";part=wcstok_s(nullptr,L"|",&ctx);}
            out+=L"],\"credits\":{\"cast\":[";
            wchar_t castCopy[1000];StringCchCopyW(castCopy,_countof(castCopy),cast);ctx=nullptr;part=wcstok_s(castCopy,L"|",&ctx);f=true;int n=0;
            while(part&&n++<8){while(*part&&iswspace(*part))part++;if(!f)out+=L",";f=false;out+=L"{\"name\":"+JsonEscape(part)+L"}";part=wcstok_s(nullptr,L"|",&ctx);}
            out+=L"],\"crew\":[";
            if(directors[0])out+=L"{\"job\":\"Director\",\"name\":"+JsonEscape(directors)+L"}";
            out+=L"]}}";count++;break;
        } else {
            if(count)out+=L",";out+=L"{\"id\":"+std::to_wstring(id)+L",\"title\":"+JsonEscape(title)+L",\"release_date\":"+JsonEscape(date)+L",\"overview\":"+JsonEscape(description)+L",\"poster_path\":"+JsonEscape(poster.c_str())+L"}";count++;
        }
    }
    if(!count)return false;
    if(!details)out+=L"]}";
    return true;
}
bool TvMazeSearch(const wchar_t *query,std::wstring &out) {
    std::vector<wchar_t> raw;
    if(!HttpGet(L"api.tvmaze.com",L"/search/shows?q="+UrlEncode(query),raw))return false;
    const wchar_t *p=raw.data();out=L"{\"results\":[";int count=0;
    while(*p&&count<30) {
        if(*p!=L'{'){p++;continue;}
        const wchar_t *end=FindObjectEnd(p);if(end<=p)break;
        const wchar_t *show=wcsstr(p,L"\"show\"");if(show&&show<end){const wchar_t *obj=wcschr(show,L'{');const wchar_t *se=obj?FindObjectEnd(obj):obj;
            if(obj&&se&&se<=end) {
                int id=JsonInt(obj,se,L"id");wchar_t name[256]={},date[64]={},summary[1800]={},image[512]={};
                JsonString(obj,se,L"name",name,_countof(name));JsonString(obj,se,L"premiered",date,_countof(date));JsonString(obj,se,L"summary",summary,_countof(summary));
                const wchar_t *im=wcsstr(obj,L"\"image\"");if(im&&im<se)JsonString(im,se,L"original",image,_countof(image));
                if(id>0&&name[0]){if(count)out+=L",";out+=L"{\"id\":"+std::to_wstring(id)+L",\"name\":"+JsonEscape(name)+L",\"first_air_date\":"+JsonEscape(date)+L",\"overview\":"+JsonEscape(StripHtml(summary).c_str())+L",\"poster_path\":"+JsonEscape(image)+L"}";count++;}
            }
        }
        p=end;
    }
    out+=L"]}";return count>0;
}
bool TvMazeDetails(int id,std::wstring &out) {
    std::vector<wchar_t> raw;
    wchar_t path[128];StringCchPrintfW(path,_countof(path),L"/shows/%d?embed[]=cast&embed[]=seasons",id);
    if(!HttpGet(L"api.tvmaze.com",path,raw))return false;
    const wchar_t *root=raw.data(),*end=FindObjectEnd(root);
    wchar_t name[256]={},date[64]={},summary[1800]={},image[512]={};
    JsonString(root,end,L"name",name,_countof(name));JsonString(root,end,L"premiered",date,_countof(date));JsonString(root,end,L"summary",summary,_countof(summary));
    const wchar_t *im=wcsstr(root,L"\"image\"");if(im&&im<end)JsonString(im,end,L"original",image,_countof(image));
    int minutes=JsonInt(root,end,L"runtime");
    out=L"{";bool first=true;AddField(out,L"overview",StripHtml(summary).c_str(),first);AddField(out,L"first_air_date",date,first);AddField(out,L"poster_path",image,first);if(minutes>0){out+=L",\"runtime\":"+std::to_wstring(minutes);}
    out+=L",\"genres\":[";const wchar_t *g=wcsstr(root,L"\"genres\"");const wchar_t *ga=g?wcschr(g,L'['):nullptr;const wchar_t *gb=ga?wcschr(ga,L']'):nullptr;bool f=true;
    if(ga&&gb)for(const wchar_t *p=ga;p<gb;){if(*p==L'"'){const wchar_t *q=wcschr(p+1,L'"');if(!q)break;size_t n=(size_t)(q-p-1);if(n>0&&n<120){wchar_t value[128]={};wcsncpy_s(value,p+1,n);if(!f)out+=L",";f=false;out+=L"{\"name\":"+JsonEscape(value)+L"}";}p=q+1;}else p++;}
    out+=L"],\"credits\":{\"cast\":[";f=true;
    const wchar_t *embedded=wcsstr(root,L"\"_embedded\"");const wchar_t *cast=embedded?wcsstr(embedded,L"\"cast\""):nullptr;const wchar_t *ca=cast?wcschr(cast,L'['):nullptr;const wchar_t *ce=ca?wcschr(ca,L']'):nullptr;
    if(ca&&ce){const wchar_t *p=ca;int n=0;while((p=wcsstr(p,L"\"person\""))&&p<ce&&n<8){const wchar_t *o=wcschr(p,L'{');const wchar_t *oe=o?FindObjectEnd(o):o;if(!o||oe>ce)break;wchar_t person[128]={};JsonString(o,oe,L"name",person,_countof(person));if(person[0]){if(!f)out+=L",";f=false;out+=L"{\"name\":"+JsonEscape(person)+L"}";n++;}p=oe;}}
    out+=L"],\"crew\":[]},\"seasons\":[";f=true;
    const wchar_t *seasons=embedded?wcsstr(embedded,L"\"seasons\""):nullptr;const wchar_t *sa=seasons?wcschr(seasons,L'['):nullptr;const wchar_t *sb=sa?wcschr(sa,L']'):nullptr;
    if(sa&&sb){const wchar_t *p=sa;while((p=wcschr(p,L'{'))&&p<sb){const wchar_t *oe=FindObjectEnd(p);if(oe>sb)break;int number=JsonInt(p,oe,L"number");wchar_t label[128]={},premiere[64]={};JsonString(p,oe,L"name",label,_countof(label));JsonString(p,oe,L"premiereDate",premiere,_countof(premiere));if(number>=0){if(!f)out+=L",";f=false;out+=L"{\"season_number\":"+std::to_wstring(number)+L",\"name\":"+JsonEscape(label)+L",\"air_date\":"+JsonEscape(premiere)+L"}";}p=oe;}}
    out+=L"]}";return name[0]!=0;
}
bool TvMazeEpisodes(int id,int season,std::wstring &out) {
    std::vector<wchar_t> raw;wchar_t path[128];StringCchPrintfW(path,_countof(path),L"/shows/%d/episodes?specials=1",id);
    if(!HttpGet(L"api.tvmaze.com",path,raw))return false;
    const wchar_t *p=raw.data();out=L"{\"episodes\":[";int count=0;
    while(*p){if(*p!=L'{'){p++;continue;}const wchar_t *end=FindObjectEnd(p);if(end<=p)break;
        if(JsonInt(p,end,L"season")==season){int number=JsonInt(p,end,L"number");wchar_t title[256]={},date[64]={},summary[1200]={},image[512]={};JsonString(p,end,L"name",title,_countof(title));JsonString(p,end,L"airdate",date,_countof(date));JsonString(p,end,L"summary",summary,_countof(summary));const wchar_t *im=wcsstr(p,L"\"image\"");if(im&&im<end)JsonString(im,end,L"original",image,_countof(image));
            if(number>0&&title[0]){if(count)out+=L",";out+=L"{\"episode_number\":"+std::to_wstring(number)+L",\"name\":"+JsonEscape(title)+L",\"air_date\":"+JsonEscape(date)+L",\"overview\":"+JsonEscape(StripHtml(summary).c_str())+L",\"still_path\":"+JsonEscape(image)+L"}";count++;}}
        p=end;
    }
    out+=L"]}";return count>0;
}
bool QueryRelatedMovies(int providerId,std::wstring &out) {
    if(providerId<=0)return false;
    wchar_t entity[64];StringCchPrintfW(entity,_countof(entity),L"wd:Q%d",providerId);
    std::wstring q=L"SELECT ?item ?itemLabel ?itemDescription (MIN(?dateValue) AS ?date) (SAMPLE(?imageValue) AS ?image) (COUNT(DISTINCT ?genre) AS ?sharedGenres) WHERE { ";
    q+=entity;
    q+=L" wdt:P136 ?genre . ?item wdt:P31/wdt:P279* wd:Q11424 ; wdt:P136 ?genre . FILTER(?item != ";
    q+=entity;
    q+=L") OPTIONAL { ?item wdt:P577 ?dateValue . } OPTIONAL { ?item wdt:P18 ?imageValue . } SERVICE wikibase:label { bd:serviceParam wikibase:language \"en\". } } GROUP BY ?item ?itemLabel ?itemDescription ORDER BY DESC(?sharedGenres) LIMIT 16";
    std::vector<wchar_t> raw;
    return QueryWikidata(q,raw)&&BuildMovieResults(raw.data(),out,false);
}
bool CopyOutput(const std::wstring &value,wchar_t *out,size_t cap) {
    if(!out||!cap||value.size()>=cap)return false;
    return SUCCEEDED(StringCchCopyW(out,cap,value.c_str()));
}
}

extern "C" BOOL ABM_Metadata_Search(const wchar_t *query,ABM_METADATA_MEDIA_TYPE type,wchar_t *json,size_t capacity) {
    if(!query||!*query||!json||!capacity)return FALSE;
    std::wstring result;bool ok=false;
    if(type==ABM_METADATA_TV)ok=TvMazeSearch(query,result);
    else if(type==ABM_METADATA_MOVIE) {
        std::wstring sparql=MovieQuery(L"?item wdt:P31/wdt:P279* wd:Q11424 .",query);
        std::vector<wchar_t> raw;if(QueryWikidata(sparql,raw))ok=BuildMovieResults(raw.data(),result,false);
    }
    return ok&&CopyOutput(result,json,capacity);
}
extern "C" BOOL ABM_Metadata_GetDetails(ABM_METADATA_MEDIA_TYPE type,int providerId,wchar_t *json,size_t capacity) {
    if(providerId<=0||!json||!capacity)return FALSE;
    std::wstring result;bool ok=false;
    if(type==ABM_METADATA_TV)ok=TvMazeDetails(providerId,result);
    else if(type==ABM_METADATA_MOVIE) {
        wchar_t filter[128];StringCchPrintfW(filter,_countof(filter),L"?item wdt:P31/wdt:P279* wd:Q11424 . FILTER(?item=wd:Q%d)",providerId);
        std::wstring sparql=MovieQuery(filter,nullptr);std::vector<wchar_t> raw;
        if(QueryWikidata(sparql,raw))ok=BuildMovieResults(raw.data(),result,true);
    }
    return ok&&CopyOutput(result,json,capacity);
}
extern "C" BOOL ABM_Metadata_GetRecommendations(ABM_METADATA_MEDIA_TYPE type,int providerId,wchar_t *json,size_t capacity) {
    if(type!=ABM_METADATA_MOVIE||providerId<=0||!json||!capacity)return FALSE;
    std::wstring result;
    return QueryRelatedMovies(providerId,result)&&CopyOutput(result,json,capacity);
}
extern "C" BOOL ABM_Metadata_GetSeasonDetails(int tvmazeId,int seasonNumber,wchar_t *json,size_t capacity) {
    if(tvmazeId<=0||seasonNumber<0||!json||!capacity)return FALSE;
    std::wstring result;return TvMazeEpisodes(tvmazeId,seasonNumber,result)&&CopyOutput(result,json,capacity);
}
extern "C" BOOL ABM_Metadata_FindAlbumArtwork(const wchar_t *artist,const wchar_t *album,wchar_t *imageUrl,size_t capacity) {
    if(!artist||!*artist||!album||!*album||!imageUrl||capacity==0)return FALSE;
    imageUrl[0]=0;
    MusicArtworkRateLimit();
    std::vector<wchar_t> raw;
    if(!QueryWikidata(MusicAlbumQuery(artist,album),raw))return FALSE;
    std::wstring normalized;
    if(!BuildMovieResults(raw.data(),normalized,FALSE))return FALSE;
    const wchar_t *results=wcsstr(normalized.c_str(),L"\"results\"");
    const wchar_t *p=results?wcschr(results,L'['):nullptr;if(!p)return FALSE;p++;
    const wchar_t *end=normalized.c_str()+normalized.size();
    while(p<end&&*p) {
        while(p<end&&(*p==L','||iswspace(*p)))p++;
        if(p>=end||*p==L']')break;
        if(*p!=L'{'){p++;continue;}
        const wchar_t *objectEnd=FindObjectEnd(p);if(objectEnd<=p||objectEnd>end)break;
        wchar_t url[1024]={};JsonString(p,objectEnd,L"poster_path",url,_countof(url));
        if(url[0])return SUCCEEDED(StringCchCopyW(imageUrl,capacity,url));
        p=objectEnd;
    }
    return FALSE;
}
extern "C" const wchar_t *ABM_Metadata_ProviderName(ABM_METADATA_MEDIA_TYPE type) {
    return type==ABM_METADATA_TV?L"TVmaze":L"Wikidata";
}
extern "C" BOOL ABM_Metadata_DownloadImage(const wchar_t *url,const wchar_t *destination,DWORD timeoutMs) {
    if(!url||!destination||!destination[0])return FALSE;
    URL_COMPONENTS parts={};parts.dwStructSize=sizeof(parts);
    wchar_t host[256]={},path[4096]={},extra[2048]={},tempPath[MAX_PATH]={};
    parts.lpszHostName=host;parts.dwHostNameLength=_countof(host);
    parts.lpszUrlPath=path;parts.dwUrlPathLength=_countof(path);
    parts.lpszExtraInfo=extra;parts.dwExtraInfoLength=_countof(extra);
    if(!WinHttpCrackUrl(url,0,0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS)return FALSE;
    if(_wcsicmp(host,L"static.tvmaze.com")&&_wcsicmp(host,L"upload.wikimedia.org"))return FALSE;
    if(FAILED(StringCchPrintfW(tempPath,_countof(tempPath),L"%s.part",destination)))return FALSE;
    std::wstring requestPath(path);if(extra[0])requestPath+=extra;
    HINTERNET session=WinHttpOpen(L"ABM-AV-Player/1.2",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session)return FALSE;
    WinHttpSetTimeouts(session,(int)kConnectTimeout,(int)kConnectTimeout,(int)kSendTimeout,(int)(timeoutMs?timeoutMs:kReceiveTimeout));
    HINTERNET connect=WinHttpConnect(session,host,parts.nPort,0);
    HINTERNET request=connect?WinHttpOpenRequest(connect,L"GET",requestPath.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
    DWORD noRedirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;if(request)WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&noRedirect,sizeof(noRedirect));
    BOOL ok=request&&WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,nullptr);
    DWORD status=0,statusSize=sizeof(status);
    if(ok)ok=WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&statusSize,WINHTTP_NO_HEADER_INDEX)&&status==200;
    wchar_t contentType[128]={};DWORD contentTypeSize=sizeof(contentType);
    if(ok)ok=WinHttpQueryHeaders(request,WINHTTP_QUERY_CONTENT_TYPE,WINHTTP_HEADER_NAME_BY_INDEX,contentType,&contentTypeSize,WINHTTP_NO_HEADER_INDEX)&&_wcsnicmp(contentType,L"image/",6)==0;
    HANDLE file=INVALID_HANDLE_VALUE;if(ok)file=CreateFileW(tempPath,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,nullptr);
    if(ok&&file==INVALID_HANDLE_VALUE)ok=FALSE;
    ULONGLONG total=0;
    while(ok){
        DWORD available=0;if(!WinHttpQueryDataAvailable(request,&available)){ok=FALSE;break;}if(!available)break;
        BYTE buffer[16384];DWORD got=0,take=available<(DWORD)sizeof(buffer)?available:(DWORD)sizeof(buffer);
        if(!WinHttpReadData(request,buffer,take,&got)){ok=FALSE;break;}if(!got)break;
        total+=got;if(total>15ull*1024ull*1024ull){ok=FALSE;break;}
        DWORD written=0;if(!WriteFile(file,buffer,got,&written,nullptr)||written!=got){ok=FALSE;break;}
    }
    if(file!=INVALID_HANDLE_VALUE){if(!FlushFileBuffers(file))ok=FALSE;CloseHandle(file);}
    if(ok&&total>=16)ok=MoveFileExW(tempPath,destination,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    else ok=FALSE;
    if(!ok)DeleteFileW(tempPath);
    if(request)WinHttpCloseHandle(request);if(connect)WinHttpCloseHandle(connect);WinHttpCloseHandle(session);return ok;
}

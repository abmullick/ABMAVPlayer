#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <strsafe.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include "licensing.h"

#define ABM_TRIAL_DAYS 10ULL
#define ABM_TICKS_PER_DAY 864000000000ULL
#define ABM_LICENSE_MAX 1024
#define ABM_ID_LEN 65
#define ID_LICENSE_KEY 7101
#define ID_LICENSE_ACTIVATE 7102
#define ID_LICENSE_TRIAL 7103
#define ID_LICENSE_COPY 7104

/* Public-only P-256 key. The matching private key is distributed separately to the publisher. */
static const BYTE g_publicX[32] = {
  0x61,0xdc,0x9f,0xaa,0xc3,0x7c,0xa4,0x6c,0x25,0x49,0x2f,0x2f,0xd9,0xab,0xa7,0xf2,
  0x0d,0x9f,0xb9,0x8f,0x34,0xc7,0xce,0x57,0xfb,0xa6,0xdc,0xec,0x0a,0x5d,0xf1,0x65
};
static const BYTE g_publicY[32] = {
  0x17,0x3f,0x3a,0x31,0xb1,0xae,0x79,0x5e,0x35,0x6d,0x6d,0x52,0xc7,0xdd,0xeb,0x90,
  0xb7,0x75,0xed,0xbb,0xb1,0xac,0x10,0x45,0xec,0xc2,0x22,0xc6,0x5c,0x5b,0xf6,0xf1
};
typedef struct ABMTrialState { DWORD magic; DWORD reserved; ULONGLONG firstUtc; ULONGLONG lastUtc; } ABMTrialState;
#define ABM_TRIAL_MAGIC 0x314D4241u
typedef struct ABMLicenseDialog { HWND window, edit, status; HINSTANCE instance; BOOL done, allowed, trialAllowed; int daysLeft; wchar_t deviceId[ABM_ID_LEN]; } ABMLicenseDialog;

static BOOL GetLicensePaths(wchar_t *dir,size_t dc,wchar_t *licensePath,size_t lc,wchar_t *statePath,size_t sc) {
    wchar_t base[MAX_PATH];
    if(FAILED(SHGetFolderPathW(NULL,CSIDL_APPDATA|CSIDL_FLAG_CREATE,NULL,SHGFP_TYPE_CURRENT,base)))return FALSE;
    if(FAILED(StringCchPrintfW(dir,dc,L"%s\\ABMAVPlayer",base)))return FALSE;
    if(!CreateDirectoryW(dir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return FALSE;
    return SUCCEEDED(StringCchPrintfW(licensePath,lc,L"%s\\license.key",dir)) &&
           SUCCEEDED(StringCchPrintfW(statePath,sc,L"%s\\licensing.state",dir));
}
static BOOL Sha256(const BYTE *data,ULONG length,BYTE digest[32]) {
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;DWORD objectSize=0,got=0;BYTE *object=NULL;BOOL ok=FALSE;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto done;
    if(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&objectSize,sizeof(objectSize),&got,0)<0)goto done;
    object=(BYTE*)HeapAlloc(GetProcessHeap(),0,objectSize);if(!object)goto done;
    if(BCryptCreateHash(alg,&hash,object,objectSize,NULL,0,0)<0)goto done;
    if(BCryptHashData(hash,(PUCHAR)data,length,0)<0)goto done;
    if(BCryptFinishHash(hash,digest,32,0)<0)goto done;
    ok=TRUE;
done:
    if(hash)BCryptDestroyHash(hash);if(object)HeapFree(GetProcessHeap(),0,object);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok;
}
static void HexEncode(const BYTE *src,size_t n,wchar_t *dst,size_t cap) {
    static const wchar_t h[]=L"0123456789abcdef";size_t i;
    if(cap<n*2+1){if(cap)dst[0]=0;return;}
    for(i=0;i<n;i++){dst[i*2]=h[src[i]>>4];dst[i*2+1]=h[src[i]&15];}dst[n*2]=0;
}
static BOOL GetDeviceId(wchar_t out[ABM_ID_LEN]) {
    HKEY key=NULL;wchar_t machine[256]=L"",input[512];DWORD type=0,size=sizeof(machine),serial=0;BYTE digest[32];
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Cryptography",0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key)==ERROR_SUCCESS){
        RegQueryValueExW(key,L"MachineGuid",NULL,&type,(BYTE*)machine,&size);RegCloseKey(key);
    }
    if(!machine[0])return FALSE;
    GetVolumeInformationW(L"C:\\",NULL,0,&serial,NULL,NULL,NULL,0);
    if(FAILED(StringCchPrintfW(input,_countof(input),L"ABMAV1|%s|%08lX",machine,(unsigned long)serial)))return FALSE;
    if(!Sha256((const BYTE*)input,(ULONG)(wcslen(input)*sizeof(wchar_t)),digest))return FALSE;
    HexEncode(digest,sizeof(digest),out,ABM_ID_LEN);SecureZeroMemory(digest,sizeof(digest));return TRUE;
}
static int B64Value(char c) {
    if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;
    if(c=='-'||c=='+')return 62;if(c=='_'||c=='/')return 63;return -1;
}
static BOOL Base64UrlDecode(const char *src,BYTE *dst,size_t cap,size_t *written) {
    size_t i,o=0,n=strlen(src);unsigned int acc=0,bits=0;
    for(i=0;i<n;i++){int v=B64Value(src[i]);if(v<0)return FALSE;acc=(acc<<6)|(unsigned int)v;bits+=6;
        if(bits>=8){bits-=8;if(o>=cap)return FALSE;dst[o++]=(BYTE)((acc>>bits)&255);}}
    if(bits==6)return FALSE;if(written)*written=o;return TRUE;
}
static BOOL VerifySignedPayload(const BYTE *payload,size_t payloadLen,const BYTE signature[64]) {
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_KEY_HANDLE pub=NULL;BYTE blob[sizeof(BCRYPT_ECCKEY_BLOB)+64],digest[32];
    BCRYPT_ECCKEY_BLOB *header=(BCRYPT_ECCKEY_BLOB*)blob;BOOL ok=FALSE;
    header->dwMagic=BCRYPT_ECDSA_PUBLIC_P256_MAGIC;header->cbKey=32;
    memcpy(blob+sizeof(*header),g_publicX,32);memcpy(blob+sizeof(*header)+32,g_publicY,32);
    if(!Sha256(payload,(ULONG)payloadLen,digest))return FALSE;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_ECDSA_P256_ALGORITHM,NULL,0)<0)goto done;
    if(BCryptImportKeyPair(alg,NULL,BCRYPT_ECCPUBLIC_BLOB,&pub,blob,sizeof(blob),0)<0)goto done;
    ok=BCryptVerifySignature(pub,NULL,digest,sizeof(digest),(PUCHAR)signature,64,0)>=0;
done:
    SecureZeroMemory(digest,sizeof(digest));if(pub)BCryptDestroyKey(pub);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok;
}
static BOOL ValidateLicenseToken(const wchar_t *token,const wchar_t *deviceId) {
    char ascii[ABM_LICENSE_MAX],*p1,*p2;BYTE payload[512],sig[64];size_t payloadLen=0,sigLen=0;wchar_t payloadWide[512];int n;
    if(!token||!deviceId)return FALSE;
    n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,token,-1,ascii,sizeof(ascii),NULL,NULL);if(n<=1||n>=(int)sizeof(ascii))return FALSE;
    if(strncmp(ascii,"ABM1.",5)!=0)return FALSE;p1=ascii+5;p2=strchr(p1,'.');if(!p2)return FALSE;*p2++=0;if(strchr(p2,'.'))return FALSE;
    if(!Base64UrlDecode(p1,payload,sizeof(payload),&payloadLen)||!Base64UrlDecode(p2,sig,sizeof(sig),&sigLen)||sigLen!=64)return FALSE;
    n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char*)payload,(int)payloadLen,payloadWide,(int)_countof(payloadWide)-1);if(n<=0)return FALSE;payloadWide[n]=0;
    if(wcsncmp(payloadWide,L"ABM1|",5)!=0)return FALSE;
    wchar_t expected[256];if(FAILED(StringCchPrintfW(expected,_countof(expected),L"ABM1|%s|",deviceId)))return FALSE;
    if(wcsncmp(payloadWide,expected,wcslen(expected))!=0||wcslen(payloadWide)<=wcslen(expected))return FALSE;
    return VerifySignedPayload(payload,payloadLen,sig);
}
static BOOL ReadLicenseFile(const wchar_t *path,wchar_t *token,size_t cap) {
    FILE *fp=NULL;if(_wfopen_s(&fp,path,L"rt, ccs=UTF-8")!=0||!fp)return FALSE;
    BOOL ok=fgetws(token,(int)cap,fp)!=NULL;fclose(fp);if(ok)token[wcscspn(token,L"\\r\\n")]=0;return ok;
}
static BOOL SaveLicenseFile(const wchar_t *path,const wchar_t *token) {
    FILE *fp=NULL;if(_wfopen_s(&fp,path,L"wt, ccs=UTF-8")!=0||!fp)return FALSE;
    BOOL ok=(fputws(token,fp)>=0&&fputwc(L'\\n',fp)!=WEOF);if(fclose(fp)!=0)ok=FALSE;return ok;
}
static BOOL UnprotectTrial(const BYTE *encrypted,DWORD encryptedLen,ABMTrialState *state) {
    DATA_BLOB in={(DWORD)encryptedLen,(BYTE*)encrypted},out={0};BOOL ok=FALSE;
    if(CryptUnprotectData(&in,NULL,NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&out)&&out.cbData==sizeof(*state)){
        memcpy(state,out.pbData,sizeof(*state));ok=state->magic==ABM_TRIAL_MAGIC;}
    if(out.pbData){SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);}return ok;
}
static BOOL ReadTrialFile(const wchar_t *path,ABMTrialState *state) {
    BYTE buf[512];DWORD got=0;HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_HIDDEN,NULL);
    if(f==INVALID_HANDLE_VALUE)return FALSE;BOOL ok=ReadFile(f,buf,sizeof(buf),&got,NULL)&&got>0;CloseHandle(f);
    if(ok)ok=UnprotectTrial(buf,got,state);SecureZeroMemory(buf,sizeof(buf));return ok;
}
static BOOL ReadTrialRegistry(ABMTrialState *state) {
    HKEY key=NULL;BYTE buf[512];DWORD type=0,size=sizeof(buf);LONG e=RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\ABMAVPlayer\\Licensing",0,KEY_QUERY_VALUE,&key);
    if(e!=ERROR_SUCCESS)return FALSE;e=RegQueryValueExW(key,L"TrialState",NULL,&type,buf,&size);RegCloseKey(key);
    BOOL ok=e==ERROR_SUCCESS&&type==REG_BINARY&&UnprotectTrial(buf,size,state);SecureZeroMemory(buf,sizeof(buf));return ok;
}
static BOOL SaveTrialState(const wchar_t *file,const ABMTrialState *state) {
    DATA_BLOB in={(DWORD)sizeof(*state),(BYTE*)state},out={0};BOOL saved=FALSE;
    if(!CryptProtectData(&in,L"ABM AV Player trial",NULL,NULL,NULL,CRYPTPROTECT_UI_FORBIDDEN,&out))return FALSE;
    HANDLE f=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_HIDDEN,NULL);
    if(f!=INVALID_HANDLE_VALUE){DWORD wrote=0;if(WriteFile(f,out.pbData,out.cbData,&wrote,NULL)&&wrote==out.cbData){FlushFileBuffers(f);saved=TRUE;}CloseHandle(f);}
    HKEY key=NULL;if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\ABMAVPlayer\\Licensing",0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL)==ERROR_SUCCESS){
        if(RegSetValueExW(key,L"TrialState",0,REG_BINARY,out.pbData,out.cbData)==ERROR_SUCCESS)saved=TRUE;RegCloseKey(key);}
    SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return saved;
}
static BOOL LoadTrialState(const wchar_t *file,ABMTrialState *state) {
    ABMTrialState a={0},b={0};BOOL fa=ReadTrialFile(file,&a),fb=ReadTrialRegistry(&b);
    if(!fa&&!fb)return FALSE;
    if(fa&&fb){state->magic=ABM_TRIAL_MAGIC;state->firstUtc=a.firstUtc<b.firstUtc?a.firstUtc:b.firstUtc;state->lastUtc=a.lastUtc>b.lastUtc?a.lastUtc:b.lastUtc;}
    else *state=fa?a:b;return TRUE;
}
static ULONGLONG UtcNow(void) {
    FILETIME ft;ULARGE_INTEGER u;GetSystemTimeAsFileTime(&ft);u.LowPart=ft.dwLowDateTime;u.HighPart=ft.dwHighDateTime;return u.QuadPart;
}
static int TrialDaysLeft(const ABMTrialState *state,ULONGLONG now) {
    ULONGLONG expiry=state->firstUtc+ABM_TRIAL_DAYS*ABM_TICKS_PER_DAY;if(now>=expiry)return 0;
    ULONGLONG remaining=expiry-now;return (int)((remaining+ABM_TICKS_PER_DAY-1)/ABM_TICKS_PER_DAY);
}
static BOOL CopyDeviceId(const wchar_t *id) {
    size_t bytes=(wcslen(id)+1)*sizeof(wchar_t);HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!h)return FALSE;
    void *p=GlobalLock(h);if(!p){GlobalFree(h);return FALSE;}memcpy(p,id,bytes);GlobalUnlock(h);
    if(!OpenClipboard(NULL)){GlobalFree(h);return FALSE;}EmptyClipboard();
    if(!SetClipboardData(CF_UNICODETEXT,h)){CloseClipboard();GlobalFree(h);return FALSE;}CloseClipboard();return TRUE;
}
static LRESULT CALLBACK LicenseWndProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    ABMLicenseDialog *d=(ABMLicenseDialog*)GetWindowLongPtrW(w,GWLP_USERDATA);
    switch(msg) {
    case WM_NCCREATE:
        d=(ABMLicenseDialog*)((CREATESTRUCTW*)lp)->lpCreateParams;d->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)d);return TRUE;
    case WM_CREATE: {
        HFONT font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
        CreateWindowW(L"STATIC",L"Activate ABM AV Player",WS_CHILD|WS_VISIBLE,28,22,520,32,w,NULL,d->instance,NULL);
        CreateWindowW(L"STATIC",L"Enter your perpetual product key, or continue with the 10-day full-feature trial.",WS_CHILD|WS_VISIBLE,28,62,530,36,w,NULL,d->instance,NULL);
        d->edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,28,112,520,34,w,(HMENU)ID_LICENSE_KEY,d->instance,NULL);
        d->status=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,28,154,520,42,w,NULL,d->instance,NULL);
        CreateWindowW(L"BUTTON",L"Copy device ID",WS_CHILD|WS_VISIBLE|WS_TABSTOP,28,204,130,32,w,(HMENU)ID_LICENSE_COPY,d->instance,NULL);
        CreateWindowW(L"BUTTON",L"Activate",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,326,258,104,38,w,(HMENU)ID_LICENSE_ACTIVATE,d->instance,NULL);
        CreateWindowW(L"BUTTON",d->trialAllowed?L"Continue trial":L"Exit",WS_CHILD|WS_VISIBLE|WS_TABSTOP,444,258,104,38,w,(HMENU)ID_LICENSE_TRIAL,d->instance,NULL);
        HWND child=GetWindow(w,GW_CHILD);while(child){SendMessageW(child,WM_SETFONT,(WPARAM)font,TRUE);child=GetWindow(child,GW_HWNDNEXT);}
        wchar_t status[256];
        if(d->trialAllowed)StringCchPrintfW(status,_countof(status),L"Trial: %d day(s) remaining. Device ID: %.64s",d->daysLeft,d->deviceId);
        else StringCchPrintfW(status,_countof(status),L"Trial expired or system clock changed. Activate to continue. Device ID: %.64s",d->deviceId);
        SetWindowTextW(d->status,status);return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==ID_LICENSE_COPY){CopyDeviceId(d->deviceId);SetWindowTextW(d->status,L"Device ID copied. Send it to the publisher to receive a licence key.");return 0;}
        if(LOWORD(wp)==ID_LICENSE_ACTIVATE){
            wchar_t token[ABM_LICENSE_MAX];GetWindowTextW(d->edit,token,_countof(token));
            wchar_t dir[MAX_PATH],lic[MAX_PATH],state[MAX_PATH];
            if(!GetLicensePaths(dir,_countof(dir),lic,_countof(lic),state,_countof(state))||!ValidateLicenseToken(token,d->deviceId)){
                SetWindowTextW(d->status,L"That product key is invalid or belongs to a different device. Check it and try again.");return 0;}
            if(!SaveLicenseFile(lic,token)){SetWindowTextW(d->status,L"Could not save the licence. Check your Windows user profile permissions.");return 0;}
            d->allowed=TRUE;d->done=TRUE;DestroyWindow(w);return 0;
        }
        if(LOWORD(wp)==ID_LICENSE_TRIAL){d->allowed=d->trialAllowed;d->done=TRUE;DestroyWindow(w);return 0;}
        break;
    case WM_CLOSE:d->done=TRUE;d->allowed=FALSE;DestroyWindow(w);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{
        PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT rc;GetClientRect(w,&rc);HBRUSH bg=CreateSolidBrush(RGB(9,15,25));
        FillRect(dc,&rc,bg);DeleteObject(bg);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(240,247,255));EndPaint(w,&ps);return 0;}
    case WM_NCDESTROY:if(d)d->done=TRUE;break;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
static BOOL ShowLicenseDialog(HINSTANCE instance,HWND owner,const wchar_t *deviceId,int daysLeft,BOOL trialAllowed) {
    static BOOL registered=FALSE;WNDCLASSEXW wc={0};ABMLicenseDialog d={0};RECT work={0};int width=590,height=350;HWND w;
    BOOL ownerEnabled=owner&&IsWindowEnabled(owner);
    if(!registered){wc.cbSize=sizeof(wc);wc.lpfnWndProc=LicenseWndProc;wc.hInstance=instance;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);wc.lpszClassName=L"ABMAVPlayer.Licensing";
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return FALSE;registered=TRUE;}
    d.instance=instance;d.daysLeft=daysLeft;d.trialAllowed=trialAllowed;StringCchCopyW(d.deviceId,_countof(d.deviceId),deviceId);
    SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);int x=work.left+(work.right-work.left-width)/2,y=work.top+(work.bottom-work.top-height)/2;
    w=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT|WS_EX_TOPMOST,L"ABMAVPlayer.Licensing",L"ABM AV Player — Activation",
        WS_POPUP|WS_CAPTION|WS_SYSMENU,x,y,width,height,owner,NULL,instance,&d);if(!w)return FALSE;
    if(ownerEnabled)EnableWindow(owner,FALSE);ShowWindow(w,SW_SHOW);UpdateWindow(w);SetForegroundWindow(w);SetFocus(d.edit);
    MSG m;while(!d.done&&IsWindow(w)){BOOL got=GetMessageW(&m,NULL,0,0);if(got<=0)break;if(!IsDialogMessageW(w,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    if(IsWindow(w))DestroyWindow(w);if(ownerEnabled){EnableWindow(owner,TRUE);SetActiveWindow(owner);}return d.allowed;
}
BOOL ABM_LicenseCheckAndPrompt(HINSTANCE instance,HWND owner) {
    wchar_t dir[MAX_PATH],licensePath[MAX_PATH],statePath[MAX_PATH],deviceId[ABM_ID_LEN],token[ABM_LICENSE_MAX];
    ABMTrialState state={0};ULONGLONG now;BOOL trialExists,tampered=FALSE;int daysLeft=0;
    if(!GetDeviceId(deviceId))return FALSE;
    if(!GetLicensePaths(dir,_countof(dir),licensePath,_countof(licensePath),statePath,_countof(statePath)))return FALSE;
    if(ReadLicenseFile(licensePath,token,_countof(token))&&ValidateLicenseToken(token,deviceId))return TRUE;
    now=UtcNow();trialExists=LoadTrialState(statePath,&state);
    if(!trialExists){state.magic=ABM_TRIAL_MAGIC;state.firstUtc=now;state.lastUtc=now;}
    else if(now+2ULL*60ULL*60ULL*10000000ULL<state.lastUtc)tampered=TRUE;
    if(now>state.lastUtc)state.lastUtc=now;
    if(!SaveTrialState(statePath,&state))return FALSE;
    if(!tampered)daysLeft=TrialDaysLeft(&state,now);
    return ShowLicenseDialog(instance,owner,deviceId,daysLeft,daysLeft>0&&!tampered);
}

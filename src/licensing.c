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
#define ID_LICENSE_KEY 7101
#define ID_LICENSE_ACTIVATE 7102
#define ID_LICENSE_TRIAL 7103

/* SHA-256 digest of the shared 16-character universal activation key. */
static const BYTE g_masterKeyHash[32] = {
  0x03,0xaa,0xc1,0x01,0x27,0x20,0x54,0xc8,
  0xb4,0x73,0x28,0xb2,0x5e,0x83,0x9d,0xab,
  0x96,0xd8,0x2b,0xf0,0x00,0x58,0x0f,0xf1,
  0xd4,0x31,0x33,0x84,0x67,0x5b,0x71,0x05
};
typedef struct ABMTrialState { DWORD magic; DWORD reserved; ULONGLONG firstUtc; ULONGLONG lastUtc; } ABMTrialState;
#define ABM_TRIAL_MAGIC 0x314D4241u
typedef struct ABMLicenseDialog { HWND window, edit, status; HINSTANCE instance; BOOL done, allowed, trialAllowed, manual, alreadyActivated; int daysLeft; } ABMLicenseDialog;

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
static BOOL ValidateMasterKey(const wchar_t *input) {
    wchar_t normalized[64]; size_t n=0,i; BYTE digest[32];
    char ascii[16];
    if(!input)return FALSE;
    for(i=0;input[i]&&n<_countof(normalized)-1;i++) {
        wchar_t ch=input[i];
        if(ch==L' '||ch==L'-')continue;
        if(ch>=L'a'&&ch<=L'z')ch-=32;
        if(!((ch>=L'A'&&ch<=L'Z')||(ch>=L'0'&&ch<=L'9')))return FALSE;
        normalized[n++]=ch;
    }
    if(n!=16){SecureZeroMemory(normalized,sizeof(normalized));return FALSE;}
    for(i=0;i<16;i++)ascii[i]=(char)normalized[i];
    BOOL hashed=Sha256((const BYTE*)ascii,16,digest);
    BOOL valid=hashed&&memcmp(digest,g_masterKeyHash,sizeof(digest))==0;
    SecureZeroMemory(digest,sizeof(digest));
    SecureZeroMemory(ascii,sizeof(ascii));
    SecureZeroMemory(normalized,sizeof(normalized));
    return valid;
}
static BOOL ReadLicenseFile(const wchar_t *path,wchar_t *token,size_t cap) {
    FILE *fp=NULL;if(_wfopen_s(&fp,path,L"rt, ccs=UTF-8")!=0||!fp)return FALSE;
    BOOL ok=fgetws(token,(int)cap,fp)!=NULL;fclose(fp);if(ok)token[wcscspn(token,L"\r\n")]=0;return ok;
}
static BOOL SaveLicenseFile(const wchar_t *path,const wchar_t *token) {
    FILE *fp=NULL;if(_wfopen_s(&fp,path,L"wt, ccs=UTF-8")!=0||!fp)return FALSE;
    BOOL ok=(fputws(token,fp)>=0&&fputwc(L'\n',fp)!=WEOF);if(fclose(fp)!=0)ok=FALSE;return ok;
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
static LRESULT CALLBACK LicenseWndProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    ABMLicenseDialog *d=(ABMLicenseDialog*)GetWindowLongPtrW(w,GWLP_USERDATA);
    switch(msg) {
    case WM_NCCREATE:
        d=(ABMLicenseDialog*)((CREATESTRUCTW*)lp)->lpCreateParams;d->window=w;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)d);return TRUE;
    case WM_CREATE: {
        HFONT font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
        CreateWindowW(L"STATIC",d->manual?L"License & Activation":L"Activate ABM AV Player",WS_CHILD|WS_VISIBLE,28,22,520,32,w,NULL,d->instance,NULL);
        CreateWindowW(L"STATIC",d->manual?L"Enter your universal 16-character activation key to unlock the full version.":L"Enter the 16-character activation key, or continue with the 10-day full-feature trial.",WS_CHILD|WS_VISIBLE,28,62,530,36,w,NULL,d->instance,NULL);
        d->edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,28,112,520,34,w,(HMENU)ID_LICENSE_KEY,d->instance,NULL);
        SendMessageW(d->edit,EM_LIMITTEXT,16,0);
        d->status=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,28,154,520,42,w,NULL,d->instance,NULL);
        CreateWindowW(L"BUTTON",d->alreadyActivated?L"Close":L"Activate",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,326,258,104,38,w,(HMENU)ID_LICENSE_ACTIVATE,d->instance,NULL);
        CreateWindowW(L"BUTTON",(d->manual||!d->trialAllowed)?L"Close":L"Continue trial",WS_CHILD|WS_VISIBLE|WS_TABSTOP,444,258,104,38,w,(HMENU)ID_LICENSE_TRIAL,d->instance,NULL);
        if(d->alreadyActivated)EnableWindow(d->edit,FALSE);
        HWND child=GetWindow(w,GW_CHILD);while(child){SendMessageW(child,WM_SETFONT,(WPARAM)font,TRUE);child=GetWindow(child,GW_HWNDNEXT);}
        wchar_t status[256];
        if(d->alreadyActivated)StringCchCopyW(status,_countof(status),L"This installation is already activated.");
        else if(d->manual)StringCchCopyW(status,_countof(status),L"Activation is immediate. Close this window to keep using your trial.");
        else if(d->trialAllowed)StringCchPrintfW(status,_countof(status),L"Trial: %d day(s) remaining.",d->daysLeft);
        else StringCchCopyW(status,_countof(status),L"Trial expired or system clock changed. Enter the activation key to continue.");
        SetWindowTextW(d->status,status);return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==ID_LICENSE_ACTIVATE){
            if(d->alreadyActivated){d->done=TRUE;DestroyWindow(w);return 0;}
            wchar_t token[ABM_LICENSE_MAX];GetWindowTextW(d->edit,token,_countof(token));
            wchar_t dir[MAX_PATH],lic[MAX_PATH],state[MAX_PATH];
            if(!GetLicensePaths(dir,_countof(dir),lic,_countof(lic),state,_countof(state))||!ValidateMasterKey(token)){
                SetWindowTextW(d->status,L"That activation key is invalid. Enter the 16-character key and try again.");return 0;}
            if(!SaveLicenseFile(lic,token)){SetWindowTextW(d->status,L"Could not save the licence. Check your Windows user profile permissions.");return 0;}
            d->allowed=TRUE;d->done=TRUE;DestroyWindow(w);return 0;
        }
        if(LOWORD(wp)==ID_LICENSE_TRIAL){d->allowed=d->manual?FALSE:d->trialAllowed;d->done=TRUE;DestroyWindow(w);return 0;}
        break;
    case WM_CLOSE:d->done=TRUE;d->allowed=FALSE;DestroyWindow(w);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_CTLCOLORSTATIC:{HDC dc=(HDC)wp;SetTextColor(dc,RGB(226,238,250));SetBkMode(dc,TRANSPARENT);return (LRESULT)GetStockObject(NULL_BRUSH);}
    case WM_CTLCOLOREDIT:{static HBRUSH editBrush=NULL;HDC dc=(HDC)wp;if(!editBrush)editBrush=CreateSolidBrush(RGB(11,25,40));SetTextColor(dc,RGB(247,250,255));SetBkColor(dc,RGB(11,25,40));return (LRESULT)editBrush;}
    case WM_PAINT:{
        PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT rc;GetClientRect(w,&rc);HBRUSH bg=CreateSolidBrush(RGB(9,15,25));
        FillRect(dc,&rc,bg);DeleteObject(bg);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(240,247,255));EndPaint(w,&ps);return 0;}
    case WM_NCDESTROY:if(d)d->done=TRUE;break;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
static BOOL ShowLicenseDialog(HINSTANCE instance,HWND owner,int daysLeft,BOOL trialAllowed,BOOL manual,BOOL alreadyActivated) {
    static BOOL registered=FALSE;WNDCLASSEXW wc={0};ABMLicenseDialog d={0};RECT work={0};int width=590,height=330;HWND w;
    BOOL ownerEnabled=owner&&IsWindowEnabled(owner);
    if(!registered){wc.cbSize=sizeof(wc);wc.lpfnWndProc=LicenseWndProc;wc.hInstance=instance;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);wc.lpszClassName=L"ABMAVPlayer.Licensing";
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return FALSE;registered=TRUE;}
    d.instance=instance;d.daysLeft=daysLeft;d.trialAllowed=trialAllowed;d.manual=manual;d.alreadyActivated=alreadyActivated;
    SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);int x=work.left+(work.right-work.left-width)/2,y=work.top+(work.bottom-work.top-height)/2;
    w=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT|WS_EX_TOPMOST,L"ABMAVPlayer.Licensing",L"ABM AV Player — Activation",
        WS_POPUP|WS_CAPTION|WS_SYSMENU,x,y,width,height,owner,NULL,instance,&d);if(!w)return FALSE;
    if(ownerEnabled)EnableWindow(owner,FALSE);ShowWindow(w,SW_SHOW);UpdateWindow(w);SetForegroundWindow(w);SetFocus(d.edit);
    MSG m;while(!d.done&&IsWindow(w)){BOOL got=GetMessageW(&m,NULL,0,0);if(got<=0)break;if(!IsDialogMessageW(w,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    if(IsWindow(w))DestroyWindow(w);if(ownerEnabled){EnableWindow(owner,TRUE);SetActiveWindow(owner);}return d.allowed;
}
BOOL ABM_LicenseCheckAndPrompt(HINSTANCE instance,HWND owner) {
    wchar_t dir[MAX_PATH],licensePath[MAX_PATH],statePath[MAX_PATH],token[ABM_LICENSE_MAX];
    ABMTrialState state={0};ULONGLONG now;BOOL trialExists,tampered=FALSE;int daysLeft=0;
    if(!GetLicensePaths(dir,_countof(dir),licensePath,_countof(licensePath),statePath,_countof(statePath)))return FALSE;
    if(ReadLicenseFile(licensePath,token,_countof(token))&&ValidateMasterKey(token))return TRUE;
    now=UtcNow();trialExists=LoadTrialState(statePath,&state);
    if(!trialExists){state.magic=ABM_TRIAL_MAGIC;state.firstUtc=now;state.lastUtc=now;}
    else if(now+2ULL*60ULL*60ULL*10000000ULL<state.lastUtc)tampered=TRUE;
    if(now>state.lastUtc)state.lastUtc=now;
    if(!SaveTrialState(statePath,&state))return FALSE;
    if(!tampered)daysLeft=TrialDaysLeft(&state,now);
    return ShowLicenseDialog(instance,owner,daysLeft,daysLeft>0&&!tampered,FALSE,FALSE);
}


/* Open the same branded activation surface from the running application. */
BOOL ABM_LicenseShowActivation(HINSTANCE instance,HWND owner) {
    wchar_t dir[MAX_PATH],licensePath[MAX_PATH],statePath[MAX_PATH],token[ABM_LICENSE_MAX]={0};
    BOOL alreadyActivated=GetLicensePaths(dir,_countof(dir),licensePath,_countof(licensePath),statePath,_countof(statePath)) &&
        ReadLicenseFile(licensePath,token,_countof(token)) && ValidateMasterKey(token);
    SecureZeroMemory(token,sizeof(token));
    return ShowLicenseDialog(instance,owner,0,FALSE,TRUE,alreadyActivated);
}

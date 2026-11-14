#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
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
typedef struct ABMLicenseDialog {
    HWND window, edit, activateButton, trialButton;
    HINSTANCE instance;
    BOOL done, allowed, trialAllowed, manual, alreadyActivated;
    int daysLeft;
    wchar_t statusMessage[160]; COLORREF statusColor;
} ABMLicenseDialog;
static HBRUSH g_licenseEditBrush=NULL;
static HFONT g_licenseBodyFont=NULL, g_licenseTitleFont=NULL, g_licenseCaptionFont=NULL;
static void LicenseDrawText(HDC dc,const wchar_t *text,RECT r,COLORREF color,HFONT font,UINT flags) {
    HGDIOBJ old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    DrawTextW(dc,text,-1,&r,flags|DT_NOPREFIX);SelectObject(dc,old);
}
static void LicenseDrawButton(const DRAWITEMSTRUCT *d) {
    BOOL primary=d->CtlID==ID_LICENSE_ACTIVATE,hot=(d->itemState&ODS_SELECTED)!=0,focus=(d->itemState&ODS_FOCUS)!=0;
    /* Owner-drawn BUTTON controls retain a native class background in the
       unpainted corner pixels. Paint those pixels with the dialog surface
       first, then draw the rounded ABM button over it. */
    HBRUSH surface=CreateSolidBrush(RGB(6,10,18));
    FillRect(d->hDC,&d->rcItem,surface);
    DeleteObject(surface);
    HBRUSH brush=CreateSolidBrush(primary?(hot?RGB(42,157,148):RGB(29,125,119)):(hot?RGB(31,54,72):RGB(22,36,52)));
    HPEN pen=CreatePen(PS_SOLID,focus?2:1,focus?RGB(99,225,205):(primary?RGB(67,178,166):RGB(52,77,98)));
    HGDIOBJ ob=SelectObject(d->hDC,brush),op=SelectObject(d->hDC,pen);
    RoundRect(d->hDC,d->rcItem.left,d->rcItem.top,d->rcItem.right,d->rcItem.bottom,14,14);
    /* Focus is shown by the rounded accent pen above; no native rectangular
       focus rectangle or default-button border is used. */
    SelectObject(d->hDC,ob);SelectObject(d->hDC,op);DeleteObject(brush);DeleteObject(pen);
    wchar_t label[64]=L"";
    GetWindowTextW(d->hwndItem,label,_countof(label));
    RECT text=d->rcItem;LicenseDrawText(d->hDC,label,text,RGB(247,250,255),g_licenseBodyFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

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
        d->edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|ES_UPPERCASE|ES_NOHIDESEL,
            38,222,544,38,w,(HMENU)ID_LICENSE_KEY,d->instance,NULL);
        SendMessageW(d->edit,EM_LIMITTEXT,16,0);
        SendMessageW(d->edit,EM_SETCUEBANNER,TRUE,(LPARAM)L"Enter your 16-character activation key");
        d->activateButton=CreateWindowW(L"BUTTON",d->alreadyActivated?L"Close":L"Activate",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,310,365,132,44,w,(HMENU)ID_LICENSE_ACTIVATE,d->instance,NULL);
        d->trialButton=CreateWindowW(L"BUTTON",(d->manual||!d->trialAllowed)?L"Close":L"Continue trial",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,454,365,132,44,w,(HMENU)ID_LICENSE_TRIAL,d->instance,NULL);
        g_licenseEditBrush=CreateSolidBrush(RGB(11,25,40));
        g_licenseBodyFont=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Variable Text");
        g_licenseTitleFont=CreateFontW(-25,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Variable Display");
        g_licenseCaptionFont=CreateFontW(-12,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI Variable Text");
        if(g_licenseBodyFont)SendMessageW(d->edit,WM_SETFONT,(WPARAM)g_licenseBodyFont,TRUE);
        if(g_licenseBodyFont){SendMessageW(d->activateButton,WM_SETFONT,(WPARAM)g_licenseBodyFont,TRUE);SendMessageW(d->trialButton,WM_SETFONT,(WPARAM)g_licenseBodyFont,TRUE);}
        if(d->alreadyActivated)EnableWindow(d->edit,FALSE);
        if(d->alreadyActivated){StringCchCopyW(d->statusMessage,_countof(d->statusMessage),L"ACTIVATED  ·  This installation is ready to use.");d->statusColor=RGB(77,196,153);}
        else if(d->manual){StringCchCopyW(d->statusMessage,_countof(d->statusMessage),L"READY TO ACTIVATE  ·  Your trial remains available until activation.");d->statusColor=RGB(82,205,190);}
        else if(d->trialAllowed){StringCchPrintfW(d->statusMessage,_countof(d->statusMessage),L"TRIAL ACTIVE  ·  %d day(s) remaining",d->daysLeft);d->statusColor=RGB(82,205,190);}
        else {StringCchCopyW(d->statusMessage,_countof(d->statusMessage),L"TRIAL ENDED  ·  Enter your key to continue using the player.");d->statusColor=RGB(242,177,79);}
        return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==ID_LICENSE_ACTIVATE && HIWORD(wp)==BN_CLICKED) {
            if(d->alreadyActivated){d->done=TRUE;DestroyWindow(w);return 0;}
            wchar_t token[ABM_LICENSE_MAX]={0},dir[MAX_PATH],lic[MAX_PATH],state[MAX_PATH];
            GetWindowTextW(d->edit,token,_countof(token));
            if(!GetLicensePaths(dir,_countof(dir),lic,_countof(lic),state,_countof(state))||!ValidateMasterKey(token)){
                StringCchCopyW(d->statusMessage,_countof(d->statusMessage),L"That key wasn't recognized. Check all 16 characters and try again.");
                d->statusColor=RGB(220,75,85);InvalidateRect(w,NULL,FALSE);
                SetFocus(d->edit);SendMessageW(d->edit,EM_SETSEL,0,-1);
                MessageBeep(MB_ICONWARNING);SecureZeroMemory(token,sizeof(token));return 0;
            }
            if(!SaveLicenseFile(lic,token)){
                StringCchCopyW(d->statusMessage,_countof(d->statusMessage),L"Unable to save activation. Check your Windows profile permissions.");
                d->statusColor=RGB(220,75,85);InvalidateRect(w,NULL,FALSE);
                MessageBeep(MB_ICONERROR);SecureZeroMemory(token,sizeof(token));return 0;
            }
            SecureZeroMemory(token,sizeof(token));d->allowed=TRUE;d->done=TRUE;DestroyWindow(w);return 0;
        }
        if(LOWORD(wp)==ID_LICENSE_TRIAL && HIWORD(wp)==BN_CLICKED){d->allowed=d->manual?FALSE:d->trialAllowed;d->done=TRUE;DestroyWindow(w);return 0;}
        break;
    case WM_DRAWITEM:
        if(((DRAWITEMSTRUCT*)lp)->CtlID==ID_LICENSE_ACTIVATE||((DRAWITEMSTRUCT*)lp)->CtlID==ID_LICENSE_TRIAL){LicenseDrawButton((DRAWITEMSTRUCT*)lp);return TRUE;}
        break;
    case WM_CTLCOLORSTATIC:{
        HDC dc=(HDC)wp;SetTextColor(dc,RGB(226,238,250));SetBkMode(dc,TRANSPARENT);return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    case WM_CTLCOLOREDIT:{
        HDC dc=(HDC)wp;SetTextColor(dc,RGB(247,250,255));SetBkColor(dc,RGB(11,25,40));SetBkMode(dc,OPAQUE);
        return (LRESULT)g_licenseEditBrush;
    }
    case WM_PAINT:{
        PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT rc;GetClientRect(w,&rc);
        HBRUSH bg=CreateSolidBrush(RGB(6,10,18));FillRect(dc,&rc,bg);DeleteObject(bg);
        RECT hero={18,16,rc.right-18,160};HBRUSH panel=CreateSolidBrush(RGB(14,34,49));FillRect(dc,&hero,panel);DeleteObject(panel);
        HPEN accent=CreatePen(PS_SOLID,2,RGB(54,177,168));HGDIOBJ oldPen=SelectObject(dc,accent);
        MoveToEx(dc,hero.left+14,hero.top+1,NULL);LineTo(dc,hero.right-14,hero.top+1);SelectObject(dc,oldPen);DeleteObject(accent);
        HBRUSH logo=CreateSolidBrush(RGB(28,125,119));RECT logoRc={34,36,88,90};
        HGDIOBJ oldLogoBrush=SelectObject(dc,logo);HGDIOBJ oldLogoPen=SelectObject(dc,GetStockObject(NULL_PEN));
        RoundRect(dc,logoRc.left,logoRc.top,logoRc.right,logoRc.bottom,13,13);
        SelectObject(dc,oldLogoPen);SelectObject(dc,oldLogoBrush);DeleteObject(logo);
        LicenseDrawText(dc,L"A",(RECT){34,36,88,90},RGB(255,255,255),g_licenseTitleFont,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        LicenseDrawText(dc,L"ABM AV PLAYER  /  LICENSE",(RECT){106,34,rc.right-34,56},RGB(104,205,190),g_licenseCaptionFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        LicenseDrawText(dc,d->alreadyActivated?L"You're all set.":L"Unlock the full experience.",(RECT){106,58,rc.right-34,101},RGB(247,250,255),g_licenseTitleFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        const wchar_t *intro=d->manual?L"Enter your universal 16-character key to activate ABM AV Player.":
            L"Enter your activation key now, or continue with your 10-day full-feature trial.";
        LicenseDrawText(dc,intro,(RECT){106,105,rc.right-34,145},RGB(177,196,213),g_licenseBodyFont,DT_LEFT|DT_VCENTER|DT_WORDBREAK);
        LicenseDrawText(dc,L"ACTIVATION KEY",(RECT){34,184,rc.right-34,210},RGB(132,171,187),g_licenseCaptionFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        RECT inputFrame={32,216,rc.right-32,266};
        HBRUSH inputFill=CreateSolidBrush(RGB(11,25,40));FillRect(dc,&inputFrame,inputFill);DeleteObject(inputFill);
        HPEN inputPen=CreatePen(PS_SOLID,1,RGB(53,111,127));HGDIOBJ old=SelectObject(dc,inputPen);
        HGDIOBJ oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));RoundRect(dc,inputFrame.left,inputFrame.top,inputFrame.right,inputFrame.bottom,10,10);
        SelectObject(dc,oldBrush);SelectObject(dc,old);DeleteObject(inputPen);
        RECT status={32,282,rc.right-32,332};HBRUSH statusBrush=CreateSolidBrush(RGB(12,27,40));FillRect(dc,&status,statusBrush);DeleteObject(statusBrush);
        HBRUSH dot=CreateSolidBrush(d->alreadyActivated?RGB(77,196,153):(d->trialAllowed?RGB(82,205,190):RGB(242,177,79)));
        RECT dotRc={48,302,56,310};RoundRect(dc,dotRc.left,dotRc.top,dotRc.right,dotRc.bottom,5,5);DeleteObject(dot);
        LicenseDrawText(dc,d->statusMessage,(RECT){68,290,rc.right-44,324},d->statusColor,g_licenseBodyFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        LicenseDrawText(dc,L"Your key is checked offline. No account or internet connection is required.",(RECT){34,338,rc.right-34,357},RGB(116,140,160),g_licenseCaptionFont,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        EndPaint(w,&ps);return 0;
    }
    case WM_CLOSE:d->done=TRUE;d->allowed=FALSE;DestroyWindow(w);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE){d->done=TRUE;d->allowed=FALSE;DestroyWindow(w);return 0;}
        break;
    case WM_NCDESTROY:if(d)d->done=TRUE;break;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
static BOOL ShowLicenseDialog(HINSTANCE instance,HWND owner,int daysLeft,BOOL trialAllowed,BOOL manual,BOOL alreadyActivated) {
    static BOOL registered=FALSE;WNDCLASSEXW wc={0};ABMLicenseDialog d={0};RECT work={0},wr={0},ownerRect={0};
    int width=620,height=470;HWND w;BOOL ownerEnabled=owner&&IsWindowEnabled(owner);
    if(!registered){wc.cbSize=sizeof(wc);wc.lpfnWndProc=LicenseWndProc;wc.hInstance=instance;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);
        wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);wc.lpszClassName=L"ABMAVPlayer.Licensing";
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return FALSE;registered=TRUE;}
    d.instance=instance;d.daysLeft=daysLeft;d.trialAllowed=trialAllowed;d.manual=manual;d.alreadyActivated=alreadyActivated;
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_POPUP;
    AdjustWindowRectEx(&wr,style,FALSE,WS_EX_DLGMODALFRAME);width=wr.right-wr.left+620;height=wr.bottom-wr.top+470;
    if(owner&&IsWindow(owner))GetWindowRect(owner,&ownerRect);else SystemParametersInfoW(SPI_GETWORKAREA,0,&ownerRect,0);
    SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int x=ownerRect.left+(ownerRect.right-ownerRect.left-width)/2,y=ownerRect.top+(ownerRect.bottom-ownerRect.top-height)/2;
    if(x<work.left+8)x=work.left+8;if(y<work.top+8)y=work.top+8;
    if(x+width>work.right-8)x=work.right-width-8;if(y+height>work.bottom-8)y=work.bottom-height-8;
    w=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"ABMAVPlayer.Licensing",L"ABM AV Player — License & Activation",
        style,x,y,width,height,owner,NULL,instance,&d);if(!w)return FALSE;
    {BOOL dark=TRUE;int corners=2;COLORREF border=RGB(46,70,104),caption=RGB(6,10,18);
     DwmSetWindowAttribute(w,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
     DwmSetWindowAttribute(w,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
     DwmSetWindowAttribute(w,DWMWA_BORDER_COLOR,&border,sizeof(border));
     DwmSetWindowAttribute(w,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));}
    if(ownerEnabled)EnableWindow(owner,FALSE);ShowWindow(w,SW_SHOW);UpdateWindow(w);SetForegroundWindow(w);SetFocus(d.edit);
    MSG m;while(!d.done&&IsWindow(w)){BOOL got=GetMessageW(&m,NULL,0,0);if(got<=0)break;if(!IsDialogMessageW(w,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    if(IsWindow(w))DestroyWindow(w);if(ownerEnabled){EnableWindow(owner,TRUE);SetActiveWindow(owner);}
    if(g_licenseEditBrush){DeleteObject(g_licenseEditBrush);g_licenseEditBrush=NULL;}
    if(g_licenseBodyFont){DeleteObject(g_licenseBodyFont);g_licenseBodyFont=NULL;}
    if(g_licenseTitleFont){DeleteObject(g_licenseTitleFont);g_licenseTitleFont=NULL;}
    if(g_licenseCaptionFont){DeleteObject(g_licenseCaptionFont);g_licenseCaptionFont=NULL;}
    return d.allowed;
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

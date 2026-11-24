#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <strsafe.h>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <cstdio>
#include <new>
#include <utility>
#include "media_library.h"
#include "ui_d2d.h"
#include "ui_theme.h"
#include "ui_skin.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {
enum { ML_ADD_FOLDER=7101, ML_ADD_DRIVES=7102, ML_SCAN_ALL=7103, ML_SCAN=7104,
       ML_STOP=7105, ML_REMOVE=7106, ML_PLAY=7107, ML_CLOSE=7108,
       ML_ROOTS=7110, ML_FILES=7111, ML_PROGRESS=7112 };
enum { ML_PROGRESS_MSG=WM_APP+0x6B1, ML_DONE_MSG=WM_APP+0x6B2 };
static const size_t ML_PATH_CAP=32768;
struct PathLess {
    bool operator()(const std::wstring &a,const std::wstring &b) const {
        return _wcsicmp(a.c_str(),b.c_str())<0;
    }
};
struct LibraryState {
    HWND window=nullptr,owner=nullptr,rootsList=nullptr,filesList=nullptr,status=nullptr,progress=nullptr;
    HWND buttons[8]={};
    HFONT font=nullptr,smallFont=nullptr;
    ABM_D2D_CANVAS canvas={};
    BOOL canvasReady=FALSE,closePending=FALSE;
    HANDLE thread=nullptr;
    volatile LONG cancel=0,scanning=0;
    LONG scannedDirectories=0,foundFiles=0,scanErrors=0;
    std::vector<std::wstring> roots,files;
    ABM_MediaLibraryPlayFn play=nullptr;
} g;

static bool IsMediaExtension(const wchar_t *path) {
    const wchar_t *dot=path?wcsrchr(path,L'.'):nullptr;if(!dot)return false;
    static const wchar_t *extensions[]={
        L".mp4",L".mkv",L".avi",L".mov",L".wmv",L".m4v",L".webm",L".mpg",L".mpeg",
        L".ts",L".m2ts",L".mts",L".flv",L".ogv",L".3gp",L".vob",L".mp3",L".wav",
        L".flac",L".m4a",L".aac",L".ogg",L".opus",L".wma",L".aiff",L".ape",L".alac",
        L".ac3",L".dts",L".amr",L".mid",L".midi"
    };
    for(const wchar_t *ext:extensions)if(!_wcsicmp(dot,ext))return true;
    return false;
}
static std::wstring TrimRoot(const wchar_t *path) {
    std::wstring value=path?path:L"";
    while(value.size()>3&&(value.back()==L'\\'||value.back()==L'/'))value.pop_back();
    return value;
}
static bool ContainsPath(const std::vector<std::wstring> &items,const std::wstring &path) {
    for(const auto &item:items)if(!_wcsicmp(item.c_str(),path.c_str()))return true;
    return false;
}
static bool IsSkippedSystemFolder(const wchar_t *name) {
    static const wchar_t *skip[]={L"Windows",L"Program Files",L"Program Files (x86)",
        L"ProgramData",L"$Recycle.Bin",L"System Volume Information",L"PerfLogs",
        L"Recovery",L"$WinREAgent",L"AppData",L"WindowsApps"};
    for(const wchar_t *s:skip)if(!_wcsicmp(name,s))return true;
    return false;
}
static std::wstring JoinPath(const std::wstring &a,const wchar_t *b) {
    std::wstring out=a;
    if(!out.empty()&&out.back()!=L'\\')out.push_back(L'\\');
    out+=b?b:L"";
    return out;
}
static std::wstring EnumeratePath(const std::wstring &path) {
    if(path.size()<248||path.rfind(L"\\\\?\\",0)==0)return path;
    if(path.rfind(L"\\\\",0)==0)return L"\\\\?\\UNC\\"+path.substr(2);
    return L"\\\\?\\"+path;
}
static void SetStatusText(const wchar_t *text) {
    if(g.status&&IsWindow(g.status))SetWindowTextW(g.status,text?text:L"");
}
static std::wstring DataFolder() {
    wchar_t local[ML_PATH_CAP]={};
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,(DWORD)_countof(local));
    if(!n||n>=_countof(local))return L"";
    std::wstring root=JoinPath(local,L"ABMAVPlayer");
    CreateDirectoryW(root.c_str(),nullptr);
    std::wstring folder=JoinPath(root,L"MediaLibrary");
    CreateDirectoryW(folder.c_str(),nullptr);
    return folder;
}
static void SavePaths(const wchar_t *name,const std::vector<std::wstring> &items) {
    std::wstring folder=DataFolder();if(folder.empty())return;
    std::wstring target=JoinPath(folder,name),temp=target+L".tmp";
    FILE *fp=nullptr;if(_wfopen_s(&fp,temp.c_str(),L"wt, ccs=UTF-8")!=0||!fp)return;
    for(const auto &item:items) {
        if(item.find_first_of(L"\r\n\t")!=std::wstring::npos)continue;
        fwprintf(fp,L"%ls\n",item.c_str());
    }
    bool ok=ferror(fp)==0;fclose(fp);
    if(!ok||!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        DeleteFileW(temp.c_str());
}
static void LoadPaths(const wchar_t *name,std::vector<std::wstring> &items) {
    std::wstring folder=DataFolder();if(folder.empty())return;
    std::wstring file=JoinPath(folder,name);FILE *fp=nullptr;
    if(_wfopen_s(&fp,file.c_str(),L"rt, ccs=UTF-8")!=0||!fp)return;
    wchar_t *line=new(std::nothrow) wchar_t[ML_PATH_CAP];
    if(line) {
        while(fgetws(line,(int)ML_PATH_CAP,fp)) {
            line[wcscspn(line,L"\r\n")]=0;
            std::wstring value=TrimRoot(line);
            if(!value.empty()&&!ContainsPath(items,value))items.push_back(value);
        }
        delete[] line;
    }
    fclose(fp);
}
static void RefreshRoots() {
    if(!g.rootsList)return;
    SendMessageW(g.rootsList,LB_RESETCONTENT,0,0);
    for(const auto &root:g.roots)SendMessageW(g.rootsList,LB_ADDSTRING,0,(LPARAM)root.c_str());
}
static void RefreshFiles() {
    if(!g.filesList)return;
    SendMessageW(g.filesList,LB_RESETCONTENT,0,0);
    if(!g.files.empty()) {
        size_t reserveBytes=g.files.size()*96;
        if(reserveBytes>0x7fffffff)reserveBytes=0x7fffffff;
        SendMessageW(g.filesList,LB_INITSTORAGE,(WPARAM)g.files.size(),(LPARAM)reserveBytes);
    }
    for(const auto &path:g.files) {
        const wchar_t *leaf=wcsrchr(path.c_str(),L'\\');
        std::wstring label=(leaf&&leaf[1])?leaf+1:path;
        SendMessageW(g.filesList,LB_ADDSTRING,0,(LPARAM)label.c_str());
    }
}
static void AddRoot(const wchar_t *path) {
    std::wstring value=TrimRoot(path);
    if(value.empty()||ContainsPath(g.roots,value))return;
    DWORD attr=GetFileAttributesW(value.c_str());
    if(attr==INVALID_FILE_ATTRIBUTES||!(attr&FILE_ATTRIBUTE_DIRECTORY)||(attr&FILE_ATTRIBUTE_REPARSE_POINT))return;
    g.roots.push_back(value);RefreshRoots();SavePaths(L"folders.tsv",g.roots);
}
static bool ChooseFolder(std::wstring &path) {
    IFileOpenDialog *dialog=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
    if(FAILED(hr)||!dialog)return false;
    FILEOPENDIALOGOPTIONS options=0;
    if(SUCCEEDED(dialog->GetOptions(&options)))dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Add a media folder to ABM AV Player");
    hr=dialog->Show(g.window);
    if(SUCCEEDED(hr)) {
        IShellItem *item=nullptr;
        if(SUCCEEDED(dialog->GetResult(&item))&&item) {
            PWSTR value=nullptr;
            if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&value))&&value){path=value;CoTaskMemFree(value);}
            item->Release();
        }
    }
    dialog->Release();return !path.empty();
}
static void AddAvailableDrives(bool fixedOnly) {
    DWORD mask=GetLogicalDrives();if(!mask)return;
    for(int i=0;i<26;i++)if(mask&(1u<<i)) {
        wchar_t root[]={ (wchar_t)(L'A'+i),L':',L'\\',0 };
        UINT type=GetDriveTypeW(root);
        if(type==DRIVE_FIXED||( !fixedOnly&&type==DRIVE_REMOVABLE))AddRoot(root);
    }
}
static void ScanDirectory(const std::wstring &root,std::set<std::wstring,PathLess> &seen) {
    std::vector<std::wstring> pending;pending.push_back(root);
    while(!pending.empty()&&InterlockedCompareExchange(&g.cancel,0,0)==0) {
        std::wstring directory=std::move(pending.back());pending.pop_back();
        std::wstring query=EnumeratePath(JoinPath(directory,L"*"));
        WIN32_FIND_DATAW data={};HANDLE find=FindFirstFileW(query.c_str(),&data);
        if(find==INVALID_HANDLE_VALUE){InterlockedIncrement(&g.scanErrors);continue;}
        InterlockedIncrement(&g.scannedDirectories);
        do {
            if(InterlockedCompareExchange(&g.cancel,0,0)!=0)break;
            if(!wcscmp(data.cFileName,L".")||!wcscmp(data.cFileName,L".."))continue;
            std::wstring full=JoinPath(directory,data.cFileName);
            if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) {
                if((data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||IsSkippedSystemFolder(data.cFileName))continue;
                pending.push_back(std::move(full));
            } else if(IsMediaExtension(data.cFileName)) {
                if(seen.insert(full).second)InterlockedIncrement(&g.foundFiles);
            }
        } while(FindNextFileW(find,&data));
        FindClose(find);
        if((InterlockedCompareExchange(&g.scannedDirectories,0,0)&63)==0)PostMessageW(g.window,ML_PROGRESS_MSG,0,0);
    }
}
static DWORD WINAPI ScanWorker(LPVOID) {
    std::vector<std::wstring> roots=g.roots,files=g.files;
    std::set<std::wstring,PathLess> seen;
    for(const auto &path:files)seen.insert(path);
    InterlockedExchange(&g.scannedDirectories,0);InterlockedExchange(&g.foundFiles,0);InterlockedExchange(&g.scanErrors,0);
    for(const auto &root:roots) {
        if(InterlockedCompareExchange(&g.cancel,0,0)!=0)break;
        ScanDirectory(root,seen);
    }
    if(InterlockedCompareExchange(&g.cancel,0,0)==0) {
        files.assign(seen.begin(),seen.end());
        g.files.swap(files);
        SavePaths(L"media.tsv",g.files);
    }
    InterlockedExchange(&g.scanning,0);
    PostMessageW(g.window,ML_DONE_MSG,0,0);
    return 0;
}
static void StartScan() {
    if(InterlockedCompareExchange(&g.scanning,1,0)!=0)return;
    if(g.roots.empty()){InterlockedExchange(&g.scanning,0);SetStatusText(L"Add a folder or drive before scanning.");return;}
    InterlockedExchange(&g.cancel,0);
    g.thread=CreateThread(nullptr,0,ScanWorker,nullptr,0,nullptr);
    if(!g.thread){InterlockedExchange(&g.scanning,0);SetStatusText(L"Could not start the background scanner.");return;}
    SetThreadPriority(g.thread,THREAD_PRIORITY_BELOW_NORMAL);
    EnableWindow(g.buttons[0],FALSE);EnableWindow(g.buttons[1],FALSE);
    EnableWindow(g.buttons[2],FALSE);EnableWindow(g.buttons[3],FALSE);
    EnableWindow(g.buttons[4],TRUE);EnableWindow(g.buttons[5],FALSE);
    SendMessageW(g.progress,PBM_SETMARQUEE,TRUE,0);
    EnableWindow(g.filesList,FALSE);EnableWindow(g.buttons[6],FALSE);
    SetStatusText(L"Scanning selected locations in the background…");
}
static void FinishScan() {
    if(g.thread){WaitForSingleObject(g.thread,INFINITE);CloseHandle(g.thread);g.thread=nullptr;}
    RefreshFiles();
    wchar_t status[256];
    StringCchPrintfW(status,_countof(status),L"Scan complete · %ld new media files found · %ld folders visited · %ld inaccessible folders",
        InterlockedCompareExchange(&g.foundFiles,0,0),(long)InterlockedCompareExchange(&g.scannedDirectories,0,0),
        (long)InterlockedCompareExchange(&g.scanErrors,0,0));
    if(InterlockedCompareExchange(&g.cancel,0,0))StringCchCatW(status,_countof(status),L" · cancelled");
    SetStatusText(status);
    for(int i=0;i<4;i++)EnableWindow(g.buttons[i],TRUE);
    EnableWindow(g.buttons[4],FALSE);EnableWindow(g.buttons[5],TRUE);
    EnableWindow(g.filesList,TRUE);EnableWindow(g.buttons[6],TRUE);
    if(g.closePending&&g.window)DestroyWindow(g.window);
}
static void Layout(HWND w) {
    RECT c;GetClientRect(w,&c);int width=c.right,height=c.bottom;
    int margin=24,gap=14,top=190,footer=88;
    int leftWidth=width<900?280:330;
    if(g.rootsList)SetWindowPos(g.rootsList,nullptr,margin,top,leftWidth,height-top-footer,SWP_NOZORDER);
    if(g.filesList)SetWindowPos(g.filesList,nullptr,margin+leftWidth+gap,top,width-margin-(margin+leftWidth+gap),height-top-footer,SWP_NOZORDER);
    if(g.status)SetWindowPos(g.status,nullptr,margin,height-70,width-2*margin,24,SWP_NOZORDER);
    if(g.progress)SetWindowPos(g.progress,nullptr,margin,height-40,width-2*margin,10,SWP_NOZORDER);
    int x=margin,y=112;
    const int widths[8]={112,112,150,104,90,122,120,90};
    for(int i=0;i<8;i++){if(g.buttons[i]){SetWindowPos(g.buttons[i],nullptr,x,y,widths[i],34,SWP_NOZORDER);x+=widths[i]+8;}}
}
static void DrawButton(LPDRAWITEMSTRUCT d) {
    if(!d||!g.canvasReady)return;
    wchar_t label[96]={};GetWindowTextW(d->hwndItem,label,_countof(label));
    if(!ABM_D2D_CanvasBegin(&g.canvas,d->hDC,&d->rcItem))return;
    bool primary=d->CtlID==ML_SCAN||d->CtlID==ML_SCAN_ALL||d->CtlID==ML_PLAY;
    COLORREF fill=primary?ABM_THEME_COLOR_187E74:ABM_THEME_COLOR_192737;
    ABM_D2D_CanvasFillRound(&g.canvas,1,1,(float)(d->rcItem.right-d->rcItem.left-1),(float)(d->rcItem.bottom-d->rcItem.top-1),11,fill);
    ABM_D2D_CanvasStrokeRound(&g.canvas,1,1,(float)(d->rcItem.right-d->rcItem.left-1),(float)(d->rcItem.bottom-d->rcItem.top-1),11,
        (d->itemState&ODS_FOCUS)?ABM_THEME_COLOR_52CDBE:ABM_THEME_COLOR_394E63,(d->itemState&ODS_FOCUS)?2.0f:1.0f);
    RECT text={0,0,d->rcItem.right-d->rcItem.left,d->rcItem.bottom-d->rcItem.top};
    ABM_D2D_CanvasText(&g.canvas,label,text,ABM_THEME_COLOR_F2F8FF,ABM_D2D_TEXT_SMALL,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    ABM_D2D_CanvasEnd(&g.canvas);
}
static LRESULT CALLBACK MediaLibraryProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        g.window=w;ABM_SkinRegisterWindow(w);
        g.canvasReady=ABM_D2D_CanvasInit(&g.canvas);
        g.font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,ABM_UI_FONT_FAMILY_TEXT);
        g.smallFont=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,ABM_UI_FONT_FAMILY_TEXT);
        g.rootsList=CreateWindowExW(0,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT,0,0,10,10,w,(HMENU)ML_ROOTS,GetModuleHandleW(nullptr),nullptr);
        g.filesList=CreateWindowExW(0,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT,0,0,10,10,w,(HMENU)ML_FILES,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(g.rootsList,LB_SETITEMHEIGHT,0,44);SendMessageW(g.filesList,LB_SETITEMHEIGHT,0,52);
        SendMessageW(g.rootsList,WM_SETFONT,(WPARAM)g.font,TRUE);SendMessageW(g.filesList,WM_SETFONT,(WPARAM)g.font,TRUE);
        SetWindowTheme(g.rootsList,L"DarkMode_Explorer",nullptr);SetWindowTheme(g.filesList,L"DarkMode_Explorer",nullptr);
        const int ids[8]={ML_ADD_FOLDER,ML_ADD_DRIVES,ML_SCAN_ALL,ML_SCAN,ML_STOP,ML_REMOVE,ML_PLAY,ML_CLOSE};
        const wchar_t *labels[8]={L"Add folder",L"Add drives",L"Scan computer",L"Scan now",L"Stop",L"Remove",L"Play selected",L"Close"};
        for(int i=0;i<8;i++){g.buttons[i]=CreateWindowExW(0,L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,w,(HMENU)(INT_PTR)ids[i],GetModuleHandleW(nullptr),nullptr);SendMessageW(g.buttons[i],WM_SETFONT,(WPARAM)g.font,TRUE);}
        g.status=CreateWindowExW(0,L"STATIC",L"Add folders or drives, then scan. Scanning never interrupts playback.",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,10,10,w,nullptr,GetModuleHandleW(nullptr),nullptr);
        g.progress=CreateWindowExW(0,PROGRESS_CLASSW,L"",WS_CHILD|WS_VISIBLE|PBS_SMOOTH|PBS_MARQUEE,0,0,10,10,w,(HMENU)ML_PROGRESS,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(g.progress,PBM_SETRANGE32,0,100);SendMessageW(g.progress,PBM_SETBKCOLOR,0,ABM_THEME_COLOR_141D2A);SendMessageW(g.progress,PBM_SETBARCOLOR,0,ABM_THEME_COLOR_187E74);SendMessageW(g.progress,PBM_SETMARQUEE,FALSE,0);
        LoadPaths(L"folders.tsv",g.roots);LoadPaths(L"media.tsv",g.files);RefreshRoots();RefreshFiles();
        EnableWindow(g.buttons[4],FALSE);Layout(w);return 0;
    }
    case WM_SIZE:Layout(w);return 0;
    case WM_CTLCOLORLISTBOX: {
        HDC dc=(HDC)wp;SetTextColor(dc,ABM_THEME_COLOR_E2EEF9);SetBkColor(dc,ABM_THEME_COLOR_141D2A);
        static HBRUSH brush=nullptr;if(!brush)brush=CreateSolidBrush(ABM_THEME_COLOR_141D2A);return (LRESULT)brush;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc=(HDC)wp;SetTextColor(dc,ABM_THEME_COLOR_D7E4F1);SetBkColor(dc,ABM_THEME_COLOR_090F19);
        static HBRUSH brush=nullptr;if(!brush)brush=CreateSolidBrush(ABM_THEME_COLOR_090F19);return (LRESULT)brush;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi=(MEASUREITEMSTRUCT*)lp;
        if(mi&&(mi->CtlID==ML_ROOTS||mi->CtlID==ML_FILES)){mi->itemHeight=mi->CtlID==ML_ROOTS?44:52;return TRUE;}
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d=(LPDRAWITEMSTRUCT)lp;
        if(!d)return TRUE;
        if(d->CtlType==ODT_BUTTON){DrawButton(d);return TRUE;}
        if(d->CtlType==ODT_LISTBOX&&(d->CtlID==ML_ROOTS||d->CtlID==ML_FILES)){
            if(d->itemID==(UINT)-1)return TRUE;
            wchar_t label[ML_PATH_CAP]={};
            SendMessageW(d->hwndItem,LB_GETTEXT,d->itemID,(LPARAM)label);
            RECT r=d->rcItem;bool selected=(d->itemState&ODS_SELECTED)!=0;
            if(ABM_D2D_CanvasBegin(&g.canvas,d->hDC,&r)){
                COLORREF bg=selected?ABM_THEME_COLOR_163B48:ABM_THEME_COLOR_141D2A;
                ABM_D2D_CanvasFillRound(&g.canvas,3,2,(float)(r.right-r.left-3),(float)(r.bottom-r.top-2),8,bg);
                if(selected)ABM_D2D_CanvasFillRound(&g.canvas,3,8,6,(float)(r.bottom-r.top-8),3,ABM_THEME_COLOR_36B1A8);
                RECT title={selected?18:14,3,r.right-r.left-12,26};
                RECT sub={selected?18:14,25,r.right-r.left-12,r.bottom-r.top-3};
                ABM_D2D_CanvasText(&g.canvas,label,title,ABM_THEME_COLOR_F2F8FF,ABM_D2D_TEXT_BODY,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                const wchar_t *secondary=d->CtlID==ML_ROOTS?L"SCAN LOCATION":L"LOCAL MEDIA";
                ABM_D2D_CanvasText(&g.canvas,secondary,sub,ABM_THEME_COLOR_84CDC2,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                ABM_D2D_CanvasEnd(&g.canvas);
            }
            return TRUE;
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch(LOWORD(wp)) {
        case ML_ADD_FOLDER: {std::wstring path;if(ChooseFolder(path)){AddRoot(path.c_str());SetStatusText(L"Folder added. Select Scan now to index its media.");}return 0;}
        case ML_ADD_DRIVES:AddAvailableDrives(false);SetStatusText(L"Available fixed and removable drives added.");return 0;
        case ML_SCAN_ALL:AddAvailableDrives(true);StartScan();return 0;
        case ML_SCAN:StartScan();return 0;
        case ML_STOP:InterlockedExchange(&g.cancel,1);SetStatusText(L"Stopping after the current directory…");return 0;
        case ML_REMOVE: {
            int selected=(int)SendMessageW(g.rootsList,LB_GETCURSEL,0,0);
            if(selected>=0&&selected<(int)g.roots.size()&&!InterlockedCompareExchange(&g.scanning,0,0)) {
                g.roots.erase(g.roots.begin()+selected);RefreshRoots();SavePaths(L"folders.tsv",g.roots);
            }return 0;
        }
        case ML_PLAY: {
            int selected=(int)SendMessageW(g.filesList,LB_GETCURSEL,0,0);
            if(selected>=0&&selected<(int)g.files.size()&&g.play)g.play(g.files[(size_t)selected].c_str());
            return 0;
        }
        case ML_CLOSE:SendMessageW(w,WM_CLOSE,0,0);return 0;
        }
        if(LOWORD(wp)==ML_FILES&&HIWORD(wp)==LBN_SELCHANGE) {
            int selected=(int)SendMessageW(g.filesList,LB_GETCURSEL,0,0);
            if(selected>=0&&selected<(int)g.files.size())SetStatusText(g.files[(size_t)selected].c_str());
            return 0;
        }
        if(LOWORD(wp)==ML_FILES&&HIWORD(wp)==LBN_DBLCLK) {
            int selected=(int)SendMessageW(g.filesList,LB_GETCURSEL,0,0);
            if(selected>=0&&selected<(int)g.files.size()&&g.play)g.play(g.files[(size_t)selected].c_str());
            return 0;
        }
        return 0;
    case ML_PROGRESS_MSG: {
        wchar_t status[256];StringCchPrintfW(status,_countof(status),L"Scanning… %ld folders visited · %ld media files discovered",
            (long)InterlockedCompareExchange(&g.scannedDirectories,0,0),(long)InterlockedCompareExchange(&g.foundFiles,0,0));
        SetStatusText(status);return 0;
    }
    case ML_DONE_MSG:FinishScan();return 0;
    case WM_CLOSE:
        /* Keep the window alive until the worker has posted its completion
           message and its handle can be joined/closed on the UI thread. */
        if(g.thread){
            g.closePending=TRUE;
            if(InterlockedCompareExchange(&g.scanning,0,0))InterlockedExchange(&g.cancel,1);
            SetStatusText(L"Stopping scan before closing…");
            return 0;
        }
        DestroyWindow(w);return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT rc;GetClientRect(w,&rc);
        if(g.canvasReady&&ABM_D2D_CanvasBegin(&g.canvas,dc,&rc)){
            ABM_D2D_CanvasFill(&g.canvas,0,0,(float)rc.right,(float)rc.bottom,ABM_THEME_COLOR_090F19);
            ABM_SkinRenderCanvas(&g.canvas,&rc);
            /* Shared ABM brand header, matching Home Theatre wordmark geometry. */
            ABM_D2D_CanvasFill(&g.canvas,0,0,(float)rc.right,96,ABM_THEME_COLOR_0D1B2D);
            ABM_D2D_CanvasFill(&g.canvas,0,92,(float)rc.right,96,ABM_THEME_COLOR_273D53);
            ABM_D2D_CanvasFillRound(&g.canvas,20,7,102,87,22,ABM_THEME_COLOR_0D1C2D);
            ABM_D2D_CanvasStrokeRound(&g.canvas,20,7,102,87,22,ABM_THEME_COLOR_3F97B4,1.0f);
            ABM_D2D_CanvasFillRound(&g.canvas,28,14,94,80,17,ABM_THEME_COLOR_1C9D9D);
            ABM_D2D_CanvasStrokeRound(&g.canvas,33,19,89,75,14,ABM_THEME_COLOR_BEE5FF,0.8f);
            ABM_D2D_CanvasLine(&g.canvas,42,63,59,30,ABM_THEME_COLOR_FFFFFF,3.0f);
            ABM_D2D_CanvasLine(&g.canvas,59,30,76,63,ABM_THEME_COLOR_FFFFFF,3.0f);
            ABM_D2D_CanvasLine(&g.canvas,49,49,68,49,ABM_THEME_COLOR_E2F5FF,2.0f);
            ABM_D2D_CanvasLine(&g.canvas,62,40,75,48,ABM_THEME_COLOR_FFFFFF,2.0f);
            ABM_D2D_CanvasLine(&g.canvas,75,48,62,56,ABM_THEME_COLOR_FFFFFF,2.0f);
            ABM_D2D_CanvasLine(&g.canvas,62,56,62,40,ABM_THEME_COLOR_FFFFFF,2.0f);
            ABM_D2D_CanvasText(&g.canvas,L"ABM",RECT{116,8,420,49},ABM_THEME_COLOR_F7FAFF,ABM_D2D_TEXT_HEADING,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            ABM_D2D_CanvasText(&g.canvas,L"AV  PLAYER",RECT{188,20,390,48},ABM_THEME_COLOR_84CDC2,ABM_D2D_TEXT_BODY,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            ABM_D2D_CanvasFillRound(&g.canvas,116,52,354,54,1,ABM_THEME_COLOR_36B1A8);
            ABM_D2D_CanvasText(&g.canvas,L"MEDIA LIBRARY  ·  LOCAL AUDIO & VIDEO",RECT{116,60,560,83},ABM_THEME_COLOR_9DB1C9,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            ABM_D2D_CanvasText(&g.canvas,L"SCAN LOCATIONS",RECT{24,164,340,184},ABM_THEME_COLOR_59B9AE,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            ABM_D2D_CanvasText(&g.canvas,L"DISCOVERED MEDIA",RECT{368,164,rc.right-24,184},ABM_THEME_COLOR_59B9AE,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            int panelTop=190,panelBottom=rc.bottom-88,leftWidth=rc.right<900?280:330;
            ABM_D2D_CanvasFillRound(&g.canvas,24,panelTop,24+leftWidth,panelBottom,12,ABM_THEME_COLOR_141D2A);
            ABM_D2D_CanvasStrokeRound(&g.canvas,24.5f,(float)panelTop+0.5f,(float)(24+leftWidth)-0.5f,(float)panelBottom-0.5f,12,ABM_THEME_COLOR_394E63,1.0f);
            int rightLeft=24+leftWidth+12;
            ABM_D2D_CanvasFillRound(&g.canvas,(float)rightLeft,panelTop,(float)(rc.right-24),panelBottom,12,ABM_THEME_COLOR_141D2A);
            ABM_D2D_CanvasStrokeRound(&g.canvas,(float)rightLeft+0.5f,(float)panelTop+0.5f,(float)(rc.right-24)-0.5f,(float)panelBottom-0.5f,12,ABM_THEME_COLOR_394E63,1.0f);
            ABM_D2D_CanvasEnd(&g.canvas);
        }
        EndPaint(w,&ps);return 0;
    }
    case WM_DESTROY:
        ABM_SkinUnregisterWindow(w);
        if(g.canvasReady){ABM_D2D_CanvasDestroy(&g.canvas);g.canvasReady=FALSE;}
        if(g.font){DeleteObject(g.font);g.font=nullptr;}if(g.smallFont){DeleteObject(g.smallFont);g.smallFont=nullptr;}
        g.window=nullptr;return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
}

extern "C" void ABM_MediaLibrary_Show(HWND owner,ABM_MediaLibraryPlayFn playCallback) {
    if(g.window&&IsWindow(g.window)){ShowWindow(g.window,SW_SHOWNORMAL);SetForegroundWindow(g.window);return;}
    g.owner=owner;g.play=playCallback;g.closePending=FALSE;
    static bool registered=false;
    if(!registered) {
        WNDCLASSEXW wc={};wc.cbSize=sizeof(wc);wc.lpfnWndProc=MediaLibraryProc;wc.hInstance=GetModuleHandleW(nullptr);
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName=L"ABMAVPlayer.MediaLibrary";
        if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
        registered=true;
    }
    RECT ownerRect={};if(owner&&IsWindow(owner))GetWindowRect(owner,&ownerRect);else SystemParametersInfoW(SPI_GETWORKAREA,0,&ownerRect,0);
    int width=1120,height=720,x=ownerRect.left+(ownerRect.right-ownerRect.left-width)/2,y=ownerRect.top+(ownerRect.bottom-ownerRect.top-height)/2;
    HWND window=CreateWindowExW(WS_EX_APPWINDOW,L"ABMAVPlayer.MediaLibrary",L"ABM AV Player — Media Library",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,x,y,width,height,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return;
    HICON appIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101));
    if(appIcon){SendMessageW(window,WM_SETICON,ICON_BIG,(LPARAM)appIcon);SendMessageW(window,WM_SETICON,ICON_SMALL,(LPARAM)appIcon);}
    /* Use the same Windows 11 dark caption and rounded frame policy as Home Theatre. */
    { BOOL dark=TRUE; DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark)); DWM_WINDOW_CORNER_PREFERENCE corners=DWMWCP_ROUND; DwmSetWindowAttribute(window,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners)); }
    ShowWindow(window,SW_SHOWNORMAL);UpdateWindow(window);SetForegroundWindow(window);
}

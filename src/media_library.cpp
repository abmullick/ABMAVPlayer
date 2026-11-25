#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <strsafe.h>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <cstring>
#include <cmath>
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
       ML_STOP=7105, ML_PLAY=7107, ML_CLOSE=7108,
       ML_ROOTS=7110, ML_FILES=7111, ML_PROGRESS=7112,
       ML_SORT_NAME=7113, ML_SORT_TYPE=7114, ML_SORT_LOCATION=7115, ML_SEARCH=7116, ML_STATUS=7117,
       ML_CTX_OPEN=7121, ML_CTX_PLAY=7122, ML_CTX_REMOVE_ROOT=7123, ML_CTX_REMOVE_FILE=7124, ML_CTX_COPY_PATH=7125,
       ML_SORT_SIZE=7118, ML_SCAN_ANIM_TIMER=7130 };
enum { ML_PROGRESS_MSG=WM_APP+0x6B1, ML_DONE_MSG=WM_APP+0x6B2 };
static const size_t ML_PATH_CAP=32768;
struct PathLess {
    bool operator()(const std::wstring &a,const std::wstring &b) const {
        return _wcsicmp(a.c_str(),b.c_str())<0;
    }
};
struct LibraryState {
    HWND window=nullptr,owner=nullptr,rootsList=nullptr,filesList=nullptr,status=nullptr,progress=nullptr,searchEdit=nullptr;
    HWND sortButtons[4]={};
    HWND buttons[7]={};
    HFONT font=nullptr,smallFont=nullptr;
    ABM_D2D_CANVAS canvas={};
    BOOL canvasReady=FALSE,closePending=FALSE;
    HANDLE thread=nullptr;
    volatile LONG cancel=0,scanning=0;
    LONG scannedDirectories=0,foundFiles=0,scanErrors=0;
    std::vector<std::wstring> roots,files;
    std::vector<size_t> visibleFiles;
    int sortColumn=0;
    bool sortDescending=false;
    int activeScanButton=ML_SCAN;
    DWORD scanGlowUntil=0;
    float spinnerAngle=0.0f;
    ABM_MediaLibraryPlayFn play=nullptr;
} g;

static bool IsMediaExtension(const wchar_t *path) {
    if(!path)return false;
    /* .d.ts is a TypeScript declaration file, not an MPEG transport stream. */
    const wchar_t *leaf=path;
    if(const wchar_t *slash=wcsrchr(path,L'\\'))leaf=slash+1;
    if(const wchar_t *slash=wcsrchr(leaf,L'/'))leaf=slash+1;
    size_t leafLength=wcslen(leaf);
    if(leafLength>=5&&!_wcsicmp(leaf+leafLength-5,L".d.ts"))return false;
    const wchar_t *dot=wcsrchr(path,L'.');if(!dot)return false;
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
    if(g.status&&IsWindow(g.status)){SetWindowTextW(g.status,text?text:L"");InvalidateRect(g.status,nullptr,FALSE);}
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
static std::wstring FileLeaf(const std::wstring &path) {
    size_t slash=path.find_last_of(L"\\/");
    return slash==std::wstring::npos?path:path.substr(slash+1);
}
static std::wstring FileType(const std::wstring &path) {
    std::wstring leaf=FileLeaf(path);size_t dot=leaf.find_last_of(L'.');
    if(dot==std::wstring::npos||dot==0)return L"File";
    std::wstring ext=leaf.substr(dot+1);
    for(auto &ch:ext)ch=(wchar_t)towupper(ch);
    return ext+L" file";
}
static ULONGLONG FileSizeBytes(const std::wstring &path) {
    WIN32_FILE_ATTRIBUTE_DATA data={};std::wstring query=EnumeratePath(path);
    if(!GetFileAttributesExW(query.c_str(),GetFileExInfoStandard,&data))return 0;
    ULARGE_INTEGER size;size.HighPart=data.nFileSizeHigh;size.LowPart=data.nFileSizeLow;return size.QuadPart;
}
static std::wstring FileSizeText(const std::wstring &path) {
    WIN32_FILE_ATTRIBUTE_DATA data={};std::wstring query=EnumeratePath(path);
    if(!GetFileAttributesExW(query.c_str(),GetFileExInfoStandard,&data))return L"—";
    ULARGE_INTEGER fileSize;fileSize.HighPart=data.nFileSizeHigh;fileSize.LowPart=data.nFileSizeLow;
    ULONGLONG bytes=fileSize.QuadPart;
    const wchar_t *units[]={L"B",L"KB",L"MB",L"GB",L"TB"};
    double displayValue=(double)bytes;int unit=0;
    while(displayValue>=1024.0&&unit<4){displayValue/=1024.0;++unit;}
    wchar_t text[48];
    if(unit==0)StringCchPrintfW(text,_countof(text),L"%llu B",(unsigned long long)bytes);
    else StringCchPrintfW(text,_countof(text),L"%.1f %ls",displayValue,units[unit]);
    return text;
}
static std::wstring FileLocation(const std::wstring &path) {
    size_t slash=path.find_last_of(L"\\/");
    return slash==std::wstring::npos?L"":path.substr(0,slash);
}
static std::wstring LowerText(std::wstring value) {
    for(auto &ch:value)ch=(wchar_t)towlower(ch);
    return value;
}
static bool ContainsInsensitive(const std::wstring &value,const std::wstring &needle) {
    return needle.empty()||LowerText(value).find(LowerText(needle))!=std::wstring::npos;
}
static void UpdateSortLabels() {
    const wchar_t *labels[4]={L"NAME",L"TYPE",L"SIZE",L"LOCATION"};
    for(int i=0;i<4;i++)if(g.sortButtons[i]) {
        wchar_t text[64];StringCchPrintfW(text,_countof(text),L"%ls%ls",labels[i],
            g.sortColumn==i?(g.sortDescending?L"  ▼":L"  ▲"):L"");
        SetWindowTextW(g.sortButtons[i],text);InvalidateRect(g.sortButtons[i],nullptr,FALSE);
    }
}
static void RefreshFiles() {
    if(!g.filesList)return;
    std::wstring filter;
    if(g.searchEdit&&IsWindow(g.searchEdit)) {
        int length=GetWindowTextLengthW(g.searchEdit);
        if(length>0&&length<4096){std::vector<wchar_t> text((size_t)length+1);GetWindowTextW(g.searchEdit,text.data(),length+1);filter=text.data();}
    }
    g.visibleFiles.clear();
    for(size_t i=0;i<g.files.size();i++) {
        const std::wstring &path=g.files[i];
        if(ContainsInsensitive(FileLeaf(path),filter)||ContainsInsensitive(FileType(path),filter)||
           ContainsInsensitive(FileLocation(path),filter))g.visibleFiles.push_back(i);
    }
    auto key=[&](size_t i)->std::wstring {
        if(g.sortColumn==1)return FileType(g.files[i]);
        if(g.sortColumn==3)return FileLocation(g.files[i]);
        return FileLeaf(g.files[i]);
    };
    std::vector<ULONGLONG> sizes;
    if(g.sortColumn==2){sizes.resize(g.files.size());for(size_t i=0;i<g.files.size();++i)sizes[i]=FileSizeBytes(g.files[i]);}
    std::sort(g.visibleFiles.begin(),g.visibleFiles.end(),[&](size_t x,size_t y){
        int cmp=0;
        if(g.sortColumn==2){ULONGLONG sx=sizes[x],sy=sizes[y];cmp=sx<sy?-1:(sx>sy?1:0);}
        else cmp=_wcsicmp(key(x).c_str(),key(y).c_str());
        if(cmp==0)cmp=_wcsicmp(FileLeaf(g.files[x]).c_str(),FileLeaf(g.files[y]).c_str());
        return g.sortDescending?cmp>0:cmp<0;
    });
    SendMessageW(g.filesList,LB_RESETCONTENT,0,0);
    if(!g.visibleFiles.empty()) {
        size_t reserveBytes=g.visibleFiles.size()*128;
        if(reserveBytes>0x7fffffff)reserveBytes=0x7fffffff;
        SendMessageW(g.filesList,LB_INITSTORAGE,(WPARAM)g.visibleFiles.size(),(LPARAM)reserveBytes);
    }
    for(size_t index:g.visibleFiles)SendMessageW(g.filesList,LB_ADDSTRING,0,(LPARAM)g.files[index].c_str());
    UpdateSortLabels();
    if(g.status) {
        wchar_t status[160];StringCchPrintfW(status,_countof(status),L"%zu media files  ·  %zu shown",g.files.size(),g.visibleFiles.size());
        SetStatusText(status);
    }
}
static void SortFiles(int column) {
    if(g.sortColumn==column)g.sortDescending=!g.sortDescending;
    else {g.sortColumn=column;g.sortDescending=false;}
    RefreshFiles();
}
static void OpenContainingFolder(const std::wstring &path) {
    if(path.empty())return;
    std::wstring args=L"/select,\""+path+L"\"";
    ShellExecuteW(nullptr,L"open",L"explorer.exe",args.c_str(),nullptr,SW_SHOWNORMAL);
}
struct LibraryMenuItem { UINT command; const wchar_t *label; bool separator; };
static const LibraryMenuItem kMenuOpenFolder={ML_CTX_OPEN,L"Open folder",false};
static const LibraryMenuItem kMenuRemoveLocation={ML_CTX_REMOVE_ROOT,L"Remove location",false};
static const LibraryMenuItem kMenuPlay={ML_CTX_PLAY,L"Play",false};
static const LibraryMenuItem kMenuOpenContaining={ML_CTX_OPEN,L"Open containing folder",false};
static const LibraryMenuItem kMenuCopyPath={ML_CTX_COPY_PATH,L"Copy full path",false};
static const LibraryMenuItem kMenuRemoveFile={ML_CTX_REMOVE_FILE,L"Remove from library",false};
static const LibraryMenuItem kMenuSeparator={0,L"",true};
static void AppendLibraryMenuItem(HMENU menu,const LibraryMenuItem *item) {
    MENUITEMINFOW info={};info.cbSize=sizeof(info);info.fMask=MIIM_FTYPE|MIIM_ID|MIIM_DATA;
    info.fType=MFT_OWNERDRAW;info.wID=item->command;info.dwItemData=(ULONG_PTR)item;
    InsertMenuItemW(menu,(UINT)-1,TRUE,&info);
}
static void DrawLibraryMenuItem(const DRAWITEMSTRUCT *d) {
    if(!d||!g.canvasReady||!d->itemData)return;
    const LibraryMenuItem *item=(const LibraryMenuItem*)d->itemData;
    RECT r=d->rcItem;
    if(!ABM_D2D_CanvasBegin(&g.canvas,d->hDC,&r))return;
    float w=(float)(r.right-r.left),h=(float)(r.bottom-r.top);
    ABM_D2D_CanvasFill(&g.canvas,0,0,w,h,ABM_UI_COLOR_INPUT);
    if(item->separator) {
        ABM_D2D_CanvasFill(&g.canvas,12.0f,h*0.5f,w-12.0f,h*0.5f+1.0f,ABM_THEME_COLOR_334F65);
    } else {
        bool hot=(d->itemState&ODS_SELECTED)!=0;
        if(hot)ABM_D2D_CanvasFillRound(&g.canvas,4.0f,2.0f,w-4.0f,h-2.0f,6.0f,ABM_THEME_COLOR_163B48);
        if(hot)ABM_D2D_CanvasFillRound(&g.canvas,4.0f,8.0f,7.0f,h-8.0f,2.0f,ABM_THEME_COLOR_36B1A8);
        COLORREF color=(item->command==ML_CTX_REMOVE_ROOT||item->command==ML_CTX_REMOVE_FILE)
            ?(hot?ABM_THEME_COLOR_F2F8FF:ABM_UI_COLOR_DANGER)
            :(hot?ABM_THEME_COLOR_F2F8FF:ABM_THEME_COLOR_D7E4F1);
        RECT text={18,0,(LONG)w-14,(LONG)h};
        ABM_D2D_CanvasText(&g.canvas,item->label,text,color,ABM_D2D_TEXT_BODY,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
    ABM_D2D_CanvasEnd(&g.canvas);
}
static void ShowLibraryContextMenu(HWND child,int screenX,int screenY) {
    if(!child||!IsWindow(child))return;
    bool roots=(child==g.rootsList),files=(child==g.filesList);
    if(!roots&&!files)return;
    if(InterlockedCompareExchange(&g.scanning,0,0)!=0)return;
    POINT pt={screenX,screenY};
    int selected=-1;
    if(screenX==-1&&screenY==-1) {
        selected=(int)SendMessageW(child,LB_GETCURSEL,0,0);
        RECT item={};
        if(selected>=0&&SendMessageW(child,LB_GETITEMRECT,(WPARAM)selected,(LPARAM)&item)!=LB_ERR) {
            pt.x=item.left+16;pt.y=item.top+(item.bottom-item.top)/2;
            ClientToScreen(child,&pt);
        } else {
            RECT wr={};GetWindowRect(child,&wr);pt.x=wr.left+24;pt.y=wr.top+24;
        }
    } else {
        POINT client=pt;ScreenToClient(child,&client);
        DWORD hit=(DWORD)SendMessageW(child,LB_ITEMFROMPOINT,0,MAKELPARAM(client.x,client.y));
        if(HIWORD(hit)!=0)return;
        selected=(int)LOWORD(hit);
    }
    int count=(int)SendMessageW(child,LB_GETCOUNT,0,0);
    if(selected<0||selected>=count)return;
    SendMessageW(child,LB_SETCURSEL,(WPARAM)selected,0);
    HMENU menu=CreatePopupMenu();if(!menu)return;
    if(roots) {
        AppendLibraryMenuItem(menu,&kMenuOpenFolder);
        AppendLibraryMenuItem(menu,&kMenuSeparator);
        AppendLibraryMenuItem(menu,&kMenuRemoveLocation);
    } else {
        AppendLibraryMenuItem(menu,&kMenuPlay);
        AppendLibraryMenuItem(menu,&kMenuOpenContaining);
        AppendLibraryMenuItem(menu,&kMenuSeparator);
        AppendLibraryMenuItem(menu,&kMenuCopyPath);
        AppendLibraryMenuItem(menu,&kMenuRemoveFile);
    }
    /* Owner-drawn menu items use ABM colours and typography; the menu surface
       itself also receives the shared dark palette instead of Explorer defaults. */
    static HBRUSH menuBrush=nullptr;
    if(!menuBrush)menuBrush=CreateSolidBrush(ABM_UI_COLOR_INPUT);
    MENUINFO menuInfo={};menuInfo.cbSize=sizeof(menuInfo);menuInfo.fMask=MIM_BACKGROUND|MIM_STYLE;
    menuInfo.hbrBack=menuBrush;menuInfo.dwStyle=MNS_NOCHECK;
    SetMenuInfo(menu,&menuInfo);
    int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|TPM_NONOTIFY,pt.x,pt.y,0,g.window,nullptr);
    DestroyMenu(menu);
    if(!command)return;
    if(roots&&selected<(int)g.roots.size()) {
        const std::wstring path=g.roots[(size_t)selected];
        if(command==ML_CTX_OPEN)ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        else if(command==ML_CTX_REMOVE_ROOT) {
            g.roots.erase(g.roots.begin()+selected);
            RefreshRoots();SavePaths(L"folders.tsv",g.roots);
            SetStatusText(L"Scan location removed. Files already indexed remain in the library.");
        }
    } else if(files&&selected<(int)g.visibleFiles.size()) {
        size_t fileIndex=g.visibleFiles[(size_t)selected];
        if(fileIndex>=g.files.size())return;
        const std::wstring path=g.files[fileIndex];
        if(command==ML_CTX_PLAY) {
            if(g.play)g.play(path.c_str());
        } else if(command==ML_CTX_OPEN)OpenContainingFolder(path);
        else if(command==ML_CTX_REMOVE_FILE) {
            g.files.erase(g.files.begin()+(ptrdiff_t)fileIndex);
            SavePaths(L"media.tsv",g.files);RefreshFiles();
            SetStatusText(L"Media entry removed from the library. The file on disk was not deleted.");
        } else if(command==ML_CTX_COPY_PATH) {
            if(OpenClipboard(g.window)) {
                EmptyClipboard();
                size_t bytes=(path.size()+1)*sizeof(wchar_t);
                HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);
                if(memory) {
                    void *dest=GlobalLock(memory);
                    if(dest){memcpy(dest,path.c_str(),bytes);GlobalUnlock(memory);SetClipboardData(CF_UNICODETEXT,memory);}
                    else GlobalFree(memory);
                }
                CloseClipboard();
            }
        }
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
static void StartScan(int initiatingButton) {
    if(InterlockedCompareExchange(&g.scanning,1,0)!=0)return;
    g.activeScanButton=initiatingButton;g.scanGlowUntil=0;g.spinnerAngle=0.0f;
    if(g.roots.empty()){InterlockedExchange(&g.scanning,0);SetStatusText(L"Add a folder or drive before scanning.");return;}
    InterlockedExchange(&g.cancel,0);
    g.thread=CreateThread(nullptr,0,ScanWorker,nullptr,0,nullptr);
    if(!g.thread){InterlockedExchange(&g.scanning,0);SetStatusText(L"Could not start the background scanner.");return;}
    SetThreadPriority(g.thread,THREAD_PRIORITY_BELOW_NORMAL);
    EnableWindow(g.buttons[0],FALSE);EnableWindow(g.buttons[1],FALSE);
    EnableWindow(g.buttons[2],FALSE);EnableWindow(g.buttons[3],FALSE);
    EnableWindow(g.buttons[4],TRUE);EnableWindow(g.buttons[5],FALSE);
    if(g.window)SetTimer(g.window,ML_SCAN_ANIM_TIMER,55,nullptr);
    InvalidateRect(g.window,nullptr,FALSE);
    EnableWindow(g.filesList,FALSE);EnableWindow(g.searchEdit,FALSE);
    for(int i=0;i<4;i++)EnableWindow(g.sortButtons[i],FALSE);
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
    g.scanGlowUntil=GetTickCount()+3200;
    if(g.window)SetTimer(g.window,ML_SCAN_ANIM_TIMER,55,nullptr);
    EnableWindow(g.filesList,TRUE);EnableWindow(g.searchEdit,TRUE);
    if(g.window)InvalidateRect(g.window,nullptr,FALSE);
    for(int i=0;i<4;i++)EnableWindow(g.sortButtons[i],TRUE);
    if(g.closePending&&g.window)DestroyWindow(g.window);
}
static void Layout(HWND w) {
    RECT c;GetClientRect(w,&c);int width=c.right,height=c.bottom;
    int margin=24,gap=14,top=190,footer=88;
    int leftWidth=width<900?280:330;
    int rightLeft=margin+leftWidth+gap,rightWidth=width-margin-rightLeft;
    if(g.rootsList)SetWindowPos(g.rootsList,nullptr,margin,top,leftWidth,height-top-footer,SWP_NOZORDER);
    if(g.searchEdit)SetWindowPos(g.searchEdit,nullptr,rightLeft+17,top+17,rightWidth-34,24,SWP_NOZORDER);
    int colY=top+54,available=rightWidth-24,nameW=available*40/100,typeW=available*16/100,sizeW=available*14/100;
    if(g.sortButtons[0])SetWindowPos(g.sortButtons[0],nullptr,rightLeft+12,colY,nameW,28,SWP_NOZORDER);
    if(g.sortButtons[1])SetWindowPos(g.sortButtons[1],nullptr,rightLeft+12+nameW,colY,typeW,28,SWP_NOZORDER);
    if(g.sortButtons[2])SetWindowPos(g.sortButtons[2],nullptr,rightLeft+12+nameW+typeW,colY,sizeW,28,SWP_NOZORDER);
    if(g.sortButtons[3])SetWindowPos(g.sortButtons[3],nullptr,rightLeft+12+nameW+typeW+sizeW,colY,available-nameW-typeW-sizeW,28,SWP_NOZORDER);
    if(g.filesList)SetWindowPos(g.filesList,nullptr,rightLeft+12,top+84,rightWidth-24,height-(top+84)-footer,SWP_NOZORDER);
    if(g.status)SetWindowPos(g.status,nullptr,margin,height-70,width-2*margin,24,SWP_NOZORDER);
    int x=margin,y=112;
    const int widths[7]={112,112,150,104,90,120,90};
    for(int i=0;i<7;i++){if(g.buttons[i]){SetWindowPos(g.buttons[i],nullptr,x,y,widths[i],34,SWP_NOZORDER);x+=widths[i]+8;}}
}
static void DrawButton(LPDRAWITEMSTRUCT d) {
    if(!d||!g.canvasReady)return;
    wchar_t label[96]={};GetWindowTextW(d->hwndItem,label,_countof(label));
    if(!ABM_D2D_CanvasBegin(&g.canvas,d->hDC,&d->rcItem))return;
    const float buttonWidth=(float)(d->rcItem.right-d->rcItem.left);
    const float buttonHeight=(float)(d->rcItem.bottom-d->rcItem.top);
    /* Owner-drawn buttons must paint the entire item rectangle. Without this,
       the native BUTTON class leaves its default white background visible in
       the four square corners outside the rounded ABM surface. */
    ABM_D2D_CanvasFill(&g.canvas,0.0f,0.0f,buttonWidth,buttonHeight,ABM_THEME_COLOR_090F19);
    if(d->CtlID==ML_SORT_NAME||d->CtlID==ML_SORT_TYPE||d->CtlID==ML_SORT_SIZE||d->CtlID==ML_SORT_LOCATION) {
        bool active=(d->CtlID==ML_SORT_NAME&&g.sortColumn==0)||(d->CtlID==ML_SORT_TYPE&&g.sortColumn==1)||(d->CtlID==ML_SORT_SIZE&&g.sortColumn==2)||(d->CtlID==ML_SORT_LOCATION&&g.sortColumn==3);
        ABM_D2D_CanvasFillRound(&g.canvas,1,1,buttonWidth-1,buttonHeight-1,6,active?ABM_THEME_COLOR_163B48:ABM_THEME_COLOR_141D2A);
        ABM_D2D_CanvasText(&g.canvas,label,RECT{10,0,(LONG)buttonWidth-8,(LONG)buttonHeight},active?ABM_THEME_COLOR_84CDC2:ABM_THEME_COLOR_9DB1C9,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        ABM_D2D_CanvasEnd(&g.canvas);return;
    }
    bool scanning=InterlockedCompareExchange(&g.scanning,0,0)!=0;
    bool scanButton=d->CtlID==ML_SCAN||d->CtlID==ML_SCAN_ALL;
    bool activeSpinner=scanning&&(int)d->CtlID==g.activeScanButton;
    bool glowing=!scanning&&scanButton&&d->CtlID==g.activeScanButton&&g.scanGlowUntil&&(LONG)(g.scanGlowUntil-GetTickCount())>0;
    bool primary=scanButton||d->CtlID==ML_PLAY;
    COLORREF fill=glowing?ABM_THEME_COLOR_311F2B:((scanning&&scanButton&&!activeSpinner)?ABM_THEME_COLOR_192737:(primary?ABM_THEME_COLOR_187E74:ABM_THEME_COLOR_192737));
    ABM_D2D_CanvasFillRound(&g.canvas,1.0f,1.0f,buttonWidth-1.0f,buttonHeight-1.0f,11.0f,fill);
    COLORREF edge=glowing?ABM_THEME_COLOR_DC414C:((d->itemState&ODS_FOCUS)?ABM_THEME_COLOR_52CDBE:ABM_THEME_COLOR_394E63);
    if(glowing)ABM_D2D_CanvasStrokeRound(&g.canvas,0.0f,0.0f,buttonWidth,buttonHeight,12.0f,ABM_THEME_COLOR_844C51,3.0f);
    ABM_D2D_CanvasStrokeRound(&g.canvas,1.0f,1.0f,buttonWidth-1.0f,buttonHeight-1.0f,11.0f,edge,(glowing||(d->itemState&ODS_FOCUS))?2.0f:1.0f);
    if(activeSpinner) {
        float cx=17.0f,cy=buttonHeight*0.5f,radius=7.0f;
        ABM_D2D_CanvasStrokeRound(&g.canvas,cx-radius,cy-radius,cx+radius,cy+radius,radius,ABM_THEME_COLOR_84CDC2,1.3f);
        float angle=g.spinnerAngle;
        ABM_D2D_CanvasLine(&g.canvas,cx,cy,cx+cosf(angle)*radius*0.72f,cy+sinf(angle)*radius*0.72f,ABM_THEME_COLOR_F2F8FF,1.8f);
        ABM_D2D_CanvasLine(&g.canvas,cx,cy,cx+cosf(angle+1.57f)*radius*0.48f,cy+sinf(angle+1.57f)*radius*0.48f,ABM_THEME_COLOR_84CDC2,1.2f);
    }
    RECT text=activeSpinner?RECT{30,0,d->rcItem.right-d->rcItem.left,d->rcItem.bottom-d->rcItem.top}:RECT{0,0,d->rcItem.right-d->rcItem.left,d->rcItem.bottom-d->rcItem.top};
    const wchar_t *display=activeSpinner?L"Scanning…":label;
    COLORREF textColor=(scanning&&scanButton&&!activeSpinner)?ABM_THEME_COLOR_9DB1C9:ABM_THEME_COLOR_F2F8FF;
    ABM_D2D_CanvasText(&g.canvas,display,text,textColor,ABM_D2D_TEXT_SMALL,activeSpinner?(DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS):(DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS));
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
        g.searchEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,10,10,w,(HMENU)ML_SEARCH,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(g.searchEdit,WM_SETFONT,(WPARAM)g.font,TRUE);
        SendMessageW(g.searchEdit,EM_SETCUEBANNER,TRUE,(LPARAM)L"Search by file name, type or folder...");
        const int sortIds[4]={ML_SORT_NAME,ML_SORT_TYPE,ML_SORT_SIZE,ML_SORT_LOCATION};
        const wchar_t *sortText[4]={L"NAME",L"TYPE",L"SIZE",L"LOCATION"};
        for(int i=0;i<4;i++){g.sortButtons[i]=CreateWindowExW(0,L"BUTTON",sortText[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,w,(HMENU)(INT_PTR)sortIds[i],GetModuleHandleW(nullptr),nullptr);SendMessageW(g.sortButtons[i],WM_SETFONT,(WPARAM)g.smallFont,TRUE);}
        SendMessageW(g.rootsList,LB_SETITEMHEIGHT,0,44);SendMessageW(g.filesList,LB_SETITEMHEIGHT,0,44);
        SendMessageW(g.rootsList,WM_SETFONT,(WPARAM)g.font,TRUE);SendMessageW(g.filesList,WM_SETFONT,(WPARAM)g.font,TRUE);
        SetWindowTheme(g.rootsList,L"DarkMode_Explorer",nullptr);SetWindowTheme(g.filesList,L"DarkMode_Explorer",nullptr);SetWindowTheme(g.searchEdit,L"DarkMode_Explorer",nullptr);
        const int ids[7]={ML_ADD_FOLDER,ML_ADD_DRIVES,ML_SCAN_ALL,ML_SCAN,ML_STOP,ML_PLAY,ML_CLOSE};
        const wchar_t *labels[7]={L"Add folder",L"Add drives",L"Scan computer",L"Scan now",L"Stop",L"Play selected",L"Close"};
        for(int i=0;i<7;i++){g.buttons[i]=CreateWindowExW(0,L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,w,(HMENU)(INT_PTR)ids[i],GetModuleHandleW(nullptr),nullptr);SendMessageW(g.buttons[i],WM_SETFONT,(WPARAM)g.font,TRUE);}
        g.status=CreateWindowExW(0,L"STATIC",L"Add folders or drives, then scan. Scanning never interrupts playback.",WS_CHILD|WS_VISIBLE|SS_OWNERDRAW,0,0,10,10,w,(HMENU)ML_STATUS,GetModuleHandleW(nullptr),nullptr);
        LoadPaths(L"folders.tsv",g.roots);LoadPaths(L"media.tsv",g.files);
        /* Purge stale non-media entries from indexes created by earlier scans. */
        g.files.erase(std::remove_if(g.files.begin(),g.files.end(),[](const std::wstring &p){return !IsMediaExtension(p.c_str());}),g.files.end());
        RefreshRoots();RefreshFiles();
        EnableWindow(g.buttons[4],FALSE);Layout(w);return 0;
    }
    case WM_SIZE:Layout(w);return 0;
    case WM_CONTEXTMENU:ShowLibraryContextMenu((HWND)wp,GET_X_LPARAM(lp),GET_Y_LPARAM(lp));return 0;
    case WM_CTLCOLOREDIT: {
        HDC dc=(HDC)wp;SetTextColor(dc,ABM_THEME_COLOR_F2F8FF);SetBkColor(dc,ABM_THEME_COLOR_141D2A);
        static HBRUSH editBrush=nullptr;if(!editBrush)editBrush=CreateSolidBrush(ABM_THEME_COLOR_141D2A);return (LRESULT)editBrush;
    }
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
        if(mi&&mi->CtlID==ML_ROOTS||mi&&mi->CtlID==ML_FILES){mi->itemHeight=mi->CtlID==ML_ROOTS?44:52;return TRUE;}
        if(mi&&mi->CtlType==ODT_MENU&&mi->itemData){
            const LibraryMenuItem *item=(const LibraryMenuItem*)mi->itemData;
            mi->itemWidth=232;mi->itemHeight=item->separator?9:36;return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d=(LPDRAWITEMSTRUCT)lp;
        if(!d)return TRUE;
        if(d->CtlType==ODT_BUTTON){DrawButton(d);return TRUE;}
        if(d->CtlType==ODT_MENU){DrawLibraryMenuItem(d);return TRUE;}
        if(d->CtlType==ODT_STATIC&&d->CtlID==ML_STATUS){
            RECT r=d->rcItem;
            if(ABM_D2D_CanvasBegin(&g.canvas,d->hDC,&r)){
                float width=(float)(r.right-r.left),height=(float)(r.bottom-r.top);
                if(d->CtlID==ML_STATUS){
                    wchar_t text[512]={};GetWindowTextW(d->hwndItem,text,_countof(text));
                    ABM_D2D_CanvasFill(&g.canvas,0,0,width,height,ABM_THEME_COLOR_090F19);
                    ABM_D2D_CanvasText(&g.canvas,text,RECT{0,0,(LONG)width,(LONG)height},ABM_UI_COLOR_TEXT_MUTED,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                }
                ABM_D2D_CanvasEnd(&g.canvas);
            }
            return TRUE;
        }
        if(d->CtlType==ODT_LISTBOX&&(d->CtlID==ML_ROOTS||d->CtlID==ML_FILES)){
            if(d->itemID==(UINT)-1)return TRUE;
            wchar_t label[ML_PATH_CAP]={};
            SendMessageW(d->hwndItem,LB_GETTEXT,d->itemID,(LPARAM)label);
            RECT r=d->rcItem;bool selected=(d->itemState&ODS_SELECTED)!=0;
            if(ABM_D2D_CanvasBegin(&g.canvas,d->hDC,&r)){
                COLORREF bg=selected?ABM_THEME_COLOR_163B48:ABM_THEME_COLOR_141D2A;
                ABM_D2D_CanvasFillRound(&g.canvas,3,2,(float)(r.right-r.left-3),(float)(r.bottom-r.top-2),8,bg);
                if(selected)ABM_D2D_CanvasFillRound(&g.canvas,3,8,6,(float)(r.bottom-r.top-8),3,ABM_THEME_COLOR_36B1A8);
                if(d->CtlID==ML_ROOTS) {
                    RECT title={14,3,r.right-r.left-12,26},sub={14,25,r.right-r.left-12,r.bottom-r.top-3};
                    ABM_D2D_CanvasText(&g.canvas,label,title,ABM_THEME_COLOR_F2F8FF,ABM_D2D_TEXT_BODY,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                    ABM_D2D_CanvasText(&g.canvas,L"SCAN LOCATION",sub,ABM_THEME_COLOR_84CDC2,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                } else {
                    std::wstring path=label,leaf=FileLeaf(path),type=FileType(path),location=FileLocation(path);
                    int total=r.right-r.left,available=total-24,nameW=available*40/100,typeW=available*16/100,sizeW=available*14/100;
                    std::wstring size=FileSizeText(path);
                    RECT nr={14,0,nameW-8,r.bottom-r.top},tr={nameW+4,0,nameW+typeW-8,r.bottom-r.top},sr={nameW+typeW+4,0,nameW+typeW+sizeW-8,r.bottom-r.top},lr={nameW+typeW+sizeW+4,0,total-12,r.bottom-r.top};
                    ABM_D2D_CanvasText(&g.canvas,leaf.c_str(),nr,ABM_THEME_COLOR_F2F8FF,ABM_D2D_TEXT_BODY,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                    ABM_D2D_CanvasText(&g.canvas,type.c_str(),tr,ABM_THEME_COLOR_84CDC2,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                    ABM_D2D_CanvasText(&g.canvas,size.c_str(),sr,ABM_THEME_COLOR_9DB1C9,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                    ABM_D2D_CanvasText(&g.canvas,location.c_str(),lr,ABM_THEME_COLOR_9DB1C9,ABM_D2D_TEXT_SMALL,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                }
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
        case ML_SCAN_ALL:AddAvailableDrives(true);StartScan(ML_SCAN_ALL);return 0;
        case ML_SCAN:StartScan(ML_SCAN);return 0;
        case ML_STOP:InterlockedExchange(&g.cancel,1);SetStatusText(L"Stopping after the current directory…");return 0;
        case ML_PLAY: {
            int selected=(int)SendMessageW(g.filesList,LB_GETCURSEL,0,0);
            if(selected>=0&&selected<(int)g.visibleFiles.size()&&g.play)g.play(g.files[g.visibleFiles[(size_t)selected]].c_str());
            return 0;
        }
        case ML_CLOSE:SendMessageW(w,WM_CLOSE,0,0);return 0;
        case ML_SORT_NAME:SortFiles(0);return 0;
        case ML_SORT_TYPE:SortFiles(1);return 0;
        case ML_SORT_SIZE:SortFiles(2);return 0;
        case ML_SORT_LOCATION:SortFiles(3);return 0;
        }
        if(LOWORD(wp)==ML_SEARCH&&HIWORD(wp)==EN_CHANGE){RefreshFiles();return 0;}
        if(LOWORD(wp)==ML_FILES&&HIWORD(wp)==LBN_SELCHANGE) {
            int selected=(int)SendMessageW(g.filesList,LB_GETCURSEL,0,0);
            if(selected>=0&&selected<(int)g.visibleFiles.size())SetStatusText(g.files[g.visibleFiles[(size_t)selected]].c_str());
            return 0;
        }
        if(LOWORD(wp)==ML_FILES&&HIWORD(wp)==LBN_DBLCLK) {
            int selected=(int)SendMessageW(g.filesList,LB_GETCURSEL,0,0);
            if(selected>=0&&selected<(int)g.visibleFiles.size()&&g.play)g.play(g.files[g.visibleFiles[(size_t)selected]].c_str());
            return 0;
        }
        return 0;
    case ML_PROGRESS_MSG: {
        wchar_t status[256];StringCchPrintfW(status,_countof(status),L"Scanning… %ld folders visited · %ld media files discovered",
            (long)InterlockedCompareExchange(&g.scannedDirectories,0,0),(long)InterlockedCompareExchange(&g.foundFiles,0,0));
        SetStatusText(status);return 0;
    }
    case ML_DONE_MSG:FinishScan();return 0;
    case WM_TIMER:
        if(wp==ML_SCAN_ANIM_TIMER) {
            if(InterlockedCompareExchange(&g.scanning,0,0)!=0) {
                g.spinnerAngle+=0.32f;if(g.spinnerAngle>6.2831853f)g.spinnerAngle-=6.2831853f;
                InvalidateRect(g.buttons[2],nullptr,FALSE);InvalidateRect(g.buttons[3],nullptr,FALSE);
            } else if(g.scanGlowUntil&&(LONG)(g.scanGlowUntil-GetTickCount())>0) {
                InvalidateRect(g.buttons[g.activeScanButton==ML_SCAN_ALL?2:3],nullptr,FALSE);
            } else {
                g.scanGlowUntil=0;KillTimer(w,ML_SCAN_ANIM_TIMER);
                InvalidateRect(g.buttons[2],nullptr,FALSE);InvalidateRect(g.buttons[3],nullptr,FALSE);
            }
            return 0;
        }
        break;
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
            ABM_D2D_CanvasFillRound(&g.canvas,24.0f,(float)panelTop,(float)(24+leftWidth),(float)panelBottom,12.0f,ABM_THEME_COLOR_141D2A);
            ABM_D2D_CanvasStrokeRound(&g.canvas,24.5f,(float)panelTop+0.5f,(float)(24+leftWidth)-0.5f,(float)panelBottom-0.5f,12,ABM_THEME_COLOR_394E63,1.0f);
            int rightLeft=24+leftWidth+12;
            ABM_D2D_CanvasFillRound(&g.canvas,(float)rightLeft,(float)panelTop,(float)(rc.right-24),(float)panelBottom,12.0f,ABM_THEME_COLOR_141D2A);
            ABM_D2D_CanvasStrokeRound(&g.canvas,(float)rightLeft+0.5f,(float)panelTop+0.5f,(float)(rc.right-24)-0.5f,(float)panelBottom-0.5f,12,ABM_THEME_COLOR_394E63,1.0f);
            /* Shared Direct2D search surface; native EDIT is borderless and inset. */
            ABM_D2D_CanvasFillRound(&g.canvas,(float)(rightLeft+12),(float)(panelTop+12),(float)(rc.right-36),(float)(panelTop+48),7.0f,ABM_UI_COLOR_INPUT);
            ABM_D2D_CanvasStrokeRound(&g.canvas,(float)(rightLeft+12.5f),(float)(panelTop+12.5f),(float)(rc.right-36.5f),(float)(panelTop+47.5f),7.0f,ABM_UI_COLOR_BORDER_STRONG,1.0f);
            /* Footer progress track is painted directly on the parent canvas;
               no native STATIC/PROGRESS control or default white frame remains. */
            float trackLeft=24.0f,trackRight=(float)rc.right-24.0f,trackY=(float)rc.bottom-40.0f;
            ABM_D2D_CanvasFillRound(&g.canvas,trackLeft,trackY,trackRight,trackY+5.0f,2.5f,ABM_UI_COLOR_BORDER);
            if(InterlockedCompareExchange(&g.scanning,0,0))
                ABM_D2D_CanvasFillRound(&g.canvas,trackLeft,trackY,trackLeft+(trackRight-trackLeft)*0.24f,trackY+5.0f,2.5f,ABM_UI_COLOR_ACCENT);
            ABM_D2D_CanvasEnd(&g.canvas);
        }
        EndPaint(w,&ps);return 0;
    }
    case WM_DESTROY:
        KillTimer(w,ML_SCAN_ANIM_TIMER);
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

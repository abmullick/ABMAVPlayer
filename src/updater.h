#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

int ABM_ShowMessageBox(HWND owner,const wchar_t *message,const wchar_t *caption,UINT flags);
bool ABM_CheckForUpdates(HWND owner, const wchar_t *currentVersion);

#ifdef __cplusplus
}
#endif

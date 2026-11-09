#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ABM_CheckForUpdates(HWND owner, const wchar_t *currentVersion);

#ifdef __cplusplus
}
#endif

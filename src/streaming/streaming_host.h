#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
void ABM_Streaming_Show(HWND mainWindow);
void ABM_Streaming_Close(void);
void ABM_Cast_ShowPicker(HWND owner);
BOOL ABM_Cast_StreamUrl(const wchar_t *url);
#ifdef __cplusplus
}
#endif

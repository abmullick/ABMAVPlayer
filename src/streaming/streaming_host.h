#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
void ABM_Streaming_Show(HWND mainWindow);
void ABM_Streaming_Close(void);
void ABM_Cast_ShowPicker(HWND owner);
BOOL ABM_Cast_StreamUrl(const wchar_t *url);
void ABM_Cast_StreamDiscoveryStart(void);
unsigned int ABM_Cast_StreamDeviceCount(void);
BOOL ABM_Cast_StreamDeviceName(unsigned int index, wchar_t *buffer, unsigned int capacity);
BOOL ABM_Cast_StreamSelectDevice(unsigned int index);
void ABM_Cast_StreamClearSelection(void);
#ifdef __cplusplus
}
#endif

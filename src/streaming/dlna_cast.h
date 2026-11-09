#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
void ABM_Dlna_StartDiscovery(void);
void ABM_Dlna_StopDiscovery(void);
unsigned int ABM_Dlna_DeviceCount(void);
BOOL ABM_Dlna_DeviceName(unsigned int index, wchar_t *buffer, unsigned int capacity);
BOOL ABM_Dlna_SelectDevice(unsigned int index);
BOOL ABM_Dlna_PlayUrl(const wchar_t *url);
#ifdef __cplusplus
}
#endif

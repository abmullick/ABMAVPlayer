#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
void ABM_Dial_StartDiscovery(void);
void ABM_Dial_StopDiscovery(void);
unsigned int ABM_Dial_DeviceCount(void);
BOOL ABM_Dial_DeviceName(unsigned int index,wchar_t *buffer,unsigned int capacity);
BOOL ABM_Dial_LaunchYouTube(unsigned int index,const wchar_t *videoId);
unsigned int ABM_Dial_LastLaunchHttpStatus(void);
#ifdef __cplusplus
}
#endif

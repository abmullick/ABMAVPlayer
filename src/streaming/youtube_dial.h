#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
void ABM_Dial_StartDiscovery(void);
void ABM_Dial_StopDiscovery(void);
unsigned int ABM_Dial_DeviceCount(void);
BOOL ABM_Dial_DeviceName(unsigned int index,wchar_t *buffer,unsigned int capacity);
/* 1=recognized/running, 2=recognized/stopped, 3=recognized/unknown state, 0=not exposed, -1=transport error, -2=unexpected HTTP status. */
int ABM_Dial_QueryApplication(unsigned int index,const wchar_t *appName);
BOOL ABM_Dial_LaunchYouTube(unsigned int index,const wchar_t *videoId);
BOOL ABM_Dial_PlayLoungeScreenId(const char *screenId,const wchar_t *videoId);
BOOL ABM_Dial_LastLaunchError(wchar_t *buffer,unsigned int capacity);
#ifdef __cplusplus
}
#endif

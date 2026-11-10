#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
void ABM_CastV2_StartDiscovery(void);
void ABM_CastV2_StopDiscovery(void);
unsigned int ABM_CastV2_DeviceCount(void);
BOOL ABM_CastV2_DeviceName(unsigned int index,wchar_t *buffer,unsigned int capacity);
BOOL ABM_CastV2_SelectDevice(unsigned int index);
/* 1=APP_AVAILABLE, 0=APP_UNAVAILABLE, -1=transport/protocol failure. */
int ABM_CastV2_CheckYouTubeAvailability(unsigned int index);
BOOL ABM_CastV2_LaunchYouTubeApp(unsigned int index);
/* 1=APP_AVAILABLE, 0=APP_UNAVAILABLE, -1=transport/protocol failure. */
int ABM_CastV2_CheckApplicationAvailability(unsigned int index,const wchar_t *applicationId);
BOOL ABM_CastV2_LaunchApplication(unsigned int index,const wchar_t *applicationId);
BOOL ABM_CastV2_LaunchYouTube(const wchar_t *videoId);
BOOL ABM_CastV2_LastError(wchar_t *buffer,unsigned int capacity);
#ifdef __cplusplus
}
#endif

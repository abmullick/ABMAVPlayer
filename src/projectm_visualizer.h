#pragma once
#include <windows.h>
#ifdef ABM_PROJECTM_BUILD
#define ABM_PROJECTM_API __declspec(dllexport)
#else
#define ABM_PROJECTM_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Process-isolated ProjectM renderer controller. OpenGL and ProjectM are never
   loaded into the ABM AV Player playback process. */
ABM_PROJECTM_API BOOL ABM_ProjectM_Create(HWND parent,const wchar_t *presetDirectory,const wchar_t *textureDirectory);
ABM_PROJECTM_API void ABM_ProjectM_SetBounds(const RECT *bounds,BOOL visible);
ABM_PROJECTM_API void ABM_ProjectM_Invalidate(void);
ABM_PROJECTM_API void ABM_ProjectM_SetAudioSamples(const float *samples,int count);
ABM_PROJECTM_API BOOL ABM_ProjectM_CopyFrame(void *destination,int destinationBytes,int *width,int *height);
ABM_PROJECTM_API void ABM_ProjectM_NextPreset(void);
ABM_PROJECTM_API void ABM_ProjectM_PreviousPreset(void);
ABM_PROJECTM_API void ABM_ProjectM_SetAutoSwitch(BOOL enabled);
ABM_PROJECTM_API void ABM_ProjectM_Destroy(void);
ABM_PROJECTM_API BOOL ABM_ProjectM_IsReady(void);
ABM_PROJECTM_API DWORD ABM_ProjectM_GetLastError(void);
#ifdef __cplusplus
}
#endif

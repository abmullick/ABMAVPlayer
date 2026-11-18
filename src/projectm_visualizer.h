#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Process-isolated ProjectM renderer controller. OpenGL and ProjectM are never
   loaded into the ABM AV Player playback process. */
__declspec(dllimport) BOOL ABM_ProjectM_Create(HWND parent,const wchar_t *presetDirectory,const wchar_t *textureDirectory);
__declspec(dllimport) void ABM_ProjectM_SetBounds(const RECT *bounds,BOOL visible);
__declspec(dllimport) void ABM_ProjectM_Invalidate(void);
__declspec(dllimport) void ABM_ProjectM_SetAudioSamples(const float *samples,int count);
__declspec(dllimport) void ABM_ProjectM_NextPreset(void);
__declspec(dllimport) void ABM_ProjectM_PreviousPreset(void);
__declspec(dllimport) void ABM_ProjectM_SetAutoSwitch(BOOL enabled);
__declspec(dllimport) void ABM_ProjectM_Destroy(void);
__declspec(dllimport) BOOL ABM_ProjectM_IsReady(void);
#ifdef __cplusplus
}
#endif

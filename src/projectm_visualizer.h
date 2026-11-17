#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
BOOL ABM_ProjectM_Create(HWND parent, const wchar_t *presetDirectory, const wchar_t *textureDirectory);
void ABM_ProjectM_SetBounds(const RECT *bounds, BOOL visible);
void ABM_ProjectM_Invalidate(void);
void ABM_ProjectM_SetAudioSamples(const float *samples, int count);
void ABM_ProjectM_NextPreset(void);
void ABM_ProjectM_PreviousPreset(void);
void ABM_ProjectM_SetAutoSwitch(BOOL enabled);
void ABM_ProjectM_Destroy(void);
BOOL ABM_ProjectM_IsReady(void);
#ifdef __cplusplus
}
#endif

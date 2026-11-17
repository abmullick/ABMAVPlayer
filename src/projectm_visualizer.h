#pragma once
#include <windows.h>
/*
 * projectM is temporarily disabled in the in-process player until its renderer
 * is hosted out-of-process. A native visualizer/driver fault must never be able
 * to terminate LibVLC playback. Define ABM_PROJECTM_DISABLED in player.c to
 * compile safe no-op entry points and keep the existing Direct2D visualizer.
 */
#ifdef ABM_PROJECTM_DISABLED
static inline BOOL ABM_ProjectM_Create(HWND parent, const wchar_t *presetDirectory, const wchar_t *textureDirectory) {
    (void)parent; (void)presetDirectory; (void)textureDirectory; return FALSE;
}
static inline void ABM_ProjectM_SetBounds(const RECT *bounds, BOOL visible) { (void)bounds; (void)visible; }
static inline void ABM_ProjectM_Invalidate(void) {}
static inline void ABM_ProjectM_SetAudioSamples(const float *samples, int count) { (void)samples; (void)count; }
static inline void ABM_ProjectM_NextPreset(void) {}
static inline void ABM_ProjectM_PreviousPreset(void) {}
static inline void ABM_ProjectM_SetAutoSwitch(BOOL enabled) { (void)enabled; }
static inline void ABM_ProjectM_Destroy(void) {}
static inline BOOL ABM_ProjectM_IsReady(void) { return FALSE; }
#else
#ifdef ABM_PROJECTM_BUILD
#define ABM_PROJECTM_API __declspec(dllexport)
#else
#define ABM_PROJECTM_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif
ABM_PROJECTM_API BOOL ABM_ProjectM_Create(HWND parent, const wchar_t *presetDirectory, const wchar_t *textureDirectory);
ABM_PROJECTM_API void ABM_ProjectM_SetBounds(const RECT *bounds, BOOL visible);
ABM_PROJECTM_API void ABM_ProjectM_Invalidate(void);
ABM_PROJECTM_API void ABM_ProjectM_SetAudioSamples(const float *samples, int count);
ABM_PROJECTM_API void ABM_ProjectM_NextPreset(void);
ABM_PROJECTM_API void ABM_ProjectM_PreviousPreset(void);
ABM_PROJECTM_API void ABM_ProjectM_SetAutoSwitch(BOOL enabled);
ABM_PROJECTM_API void ABM_ProjectM_Destroy(void);
ABM_PROJECTM_API BOOL ABM_ProjectM_IsReady(void);
#ifdef __cplusplus
}
#endif
#endif

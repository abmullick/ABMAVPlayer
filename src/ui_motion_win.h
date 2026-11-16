#ifndef ABM_UI_MOTION_WIN_H
#define ABM_UI_MOTION_WIN_H
/* Generic Win32 timer host for ABM motion values.
   The caller owns animation state, layout, painting and input semantics. */
#include <windows.h>
#include "ui_motion.h"
#ifdef __cplusplus
extern "C" {
#endif
#define ABM_MOTION_WIN_FRAME_MS 16u
BOOL ABM_MotionWinSchedule(HWND hwnd, UINT timer_id);
void ABM_MotionWinStop(HWND hwnd, UINT timer_id);
BOOL ABM_MotionWinSample(HWND hwnd, UINT timer_id,
                         ABMMotionValue *const motions[],
                         double values[], UINT count);
#ifdef __cplusplus
}
#endif
#endif

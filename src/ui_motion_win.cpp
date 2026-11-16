#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ui_motion_win.h"

BOOL ABM_MotionWinSchedule(HWND hwnd, UINT timer_id)
{
    if (!hwnd || !IsWindow(hwnd) || timer_id == 0) return FALSE;
    return SetTimer(hwnd, timer_id, ABM_MOTION_WIN_FRAME_MS, NULL) != 0;
}

void ABM_MotionWinStop(HWND hwnd, UINT timer_id)
{
    if (hwnd && IsWindow(hwnd) && timer_id != 0) KillTimer(hwnd, timer_id);
}

BOOL ABM_MotionWinSample(HWND hwnd, UINT timer_id,
                         ABMMotionValue *const motions[],
                         double values[], UINT count)
{
    BOOL any_active = FALSE;
    UINT i;
    if (!motions || !values) {
        ABM_MotionWinStop(hwnd, timer_id);
        return FALSE;
    }
    for (i = 0; i < count; ++i) {
        if (!motions[i]) {
            values[i] = 0.0;
            continue;
        }
        if (ABM_MotionSample(motions[i], &values[i])) any_active = TRUE;
    }
    if (!any_active) ABM_MotionWinStop(hwnd, timer_id);
    return any_active;
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include "ui_motion.h"

static double MotionEase(double t, ABMMotionEasing easing)
{
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    switch (easing) {
    case ABM_MOTION_EASE_IN: return t * t * t;
    case ABM_MOTION_EASE_IN_OUT:
        return t < 0.5 ? 4.0 * t * t * t
                       : 1.0 - pow(-2.0 * t + 2.0, 3.0) / 2.0;
    case ABM_MOTION_EASE_OUT: return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
    case ABM_MOTION_LINEAR:
    default: return t;
    }
}

ULONGLONG ABM_MotionNow(void)
{
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    if (frequency.QuadPart <= 0) return GetTickCount64();
    return (ULONGLONG)((counter.QuadPart * 1000ULL) /
                       (ULONGLONG)frequency.QuadPart);
}

void ABM_MotionInit(ABMMotionValue *motion, double value)
{
    if (!motion) return;
    motion->from_value = value;
    motion->target_value = value;
    motion->start_ms = ABM_MotionNow();
    motion->duration_ms = 0;
    motion->easing = ABM_MOTION_EASE_OUT;
    motion->active = FALSE;
}

BOOL ABM_MotionSample(ABMMotionValue *motion, double *value)
{
    double t, result;
    ULONGLONG now;
    if (!motion) return FALSE;
    if (!motion->active || motion->duration_ms == 0) {
        if (value) *value = motion->target_value;
        return FALSE;
    }
    now = ABM_MotionNow();
    t = now <= motion->start_ms ? 0.0 :
        (double)(now - motion->start_ms) / (double)motion->duration_ms;
    if (t >= 1.0) {
        motion->from_value = motion->target_value;
        motion->active = FALSE;
        if (value) *value = motion->target_value;
        return FALSE;
    }
    result = motion->from_value + (motion->target_value - motion->from_value) *
             MotionEase(t, motion->easing);
    if (value) *value = result;
    return TRUE;
}

void ABM_MotionStart(ABMMotionValue *motion, double target, UINT duration_ms,
                     ABMMotionEasing easing)
{
    double current;
    BOOL animations_enabled = TRUE;
    if (!motion) return;
    ABM_MotionSample(motion, &current);
    if (!motion->active && motion->duration_ms == 0)
        current = motion->target_value;
    motion->from_value = current;
    motion->target_value = target;
    motion->start_ms = ABM_MotionNow();
    motion->duration_ms = duration_ms;
    motion->easing = easing;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0,
                               &animations_enabled, 0) || !animations_enabled ||
        duration_ms == 0 || fabs(target - current) < 0.0001) {
        motion->from_value = target;
        motion->target_value = target;
        motion->duration_ms = 0;
        motion->active = FALSE;
    } else {
        motion->active = TRUE;
    }
}

void ABM_MotionCancel(ABMMotionValue *motion, double *value)
{
    double current;
    if (!motion) return;
    ABM_MotionSample(motion, &current);
    motion->from_value = current;
    motion->target_value = current;
    motion->duration_ms = 0;
    motion->active = FALSE;
    if (value) *value = current;
}

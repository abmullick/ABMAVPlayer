#ifndef ABM_UI_MOTION_H
#define ABM_UI_MOTION_H
/* C-compatible, time-based UI motion values. No HWND, layout or paint ownership. */
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum ABMMotionEasing {
    ABM_MOTION_LINEAR = 0,
    ABM_MOTION_EASE_OUT = 1,
    ABM_MOTION_EASE_IN_OUT = 2,
    ABM_MOTION_EASE_IN = 3
} ABMMotionEasing;
typedef struct ABMMotionValue {
    double from_value;
    double target_value;
    ULONGLONG start_ms;
    UINT duration_ms;
    ABMMotionEasing easing;
    BOOL active;
} ABMMotionValue;
void ABM_MotionInit(ABMMotionValue *motion, double value);
void ABM_MotionStart(ABMMotionValue *motion, double target, UINT duration_ms,
                     ABMMotionEasing easing);
BOOL ABM_MotionSample(ABMMotionValue *motion, double *value);
void ABM_MotionCancel(ABMMotionValue *motion, double *value);
ULONGLONG ABM_MotionNow(void);
#ifdef __cplusplus
}
#endif
#endif

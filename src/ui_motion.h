#ifndef ABM_UI_MOTION_H
#define ABM_UI_MOTION_H
/* C-compatible, time-based UI motion values. No HWND, layout or paint ownership. */
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ABM_MOTION_DURATION_QUICK       120u
#define ABM_MOTION_DURATION_STANDARD    180u
#define ABM_MOTION_DURATION_CONTENT     240u
#define ABM_MOTION_DURATION_WINDOW      260u
#define ABM_MOTION_DURATION_PANEL      280u
#define ABM_MOTION_DURATION_EMPHASIS   350u

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

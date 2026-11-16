/* Compile-time C11 ABI smoke test for the shared UI motion API. */
#include "../src/ui_motion.h"
#include "../src/ui_motion_win.h"

void ABM_UIMotion_CAbiCompileCheck(void)
{
    ABMMotionValue motion;
    double sample = 0.0;
    ABM_MotionInit(&motion, 0.0);
    ABM_MotionStart(&motion, 1.0, 180, ABM_MOTION_EASE_OUT);
    (void)ABM_MotionSample(&motion, &sample);
    ABM_MotionCancel(&motion, &sample);
    (void)ABM_MotionWinSchedule((HWND)0, 1);
    ABM_MotionWinStop((HWND)0, 1);
}

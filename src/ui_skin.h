#ifndef ABM_UI_SKIN_H
#define ABM_UI_SKIN_H

#include <windows.h>
#include "ui_d2d.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ABMBackgroundSkin {
    ABM_SKIN_CLASSIC_MUSIC_PLAYER = 0,
    ABM_SKIN_OCEAN_FLOW = 1,
    ABM_SKIN_AURORA = 2,
    ABM_SKIN_GOLDEN_WAVES = 3,
    ABM_SKIN_LIQUID_CHROME = 4,
    ABM_SKIN_NEON_PULSE = 5,
    ABM_SKIN_DEEP_OCEAN = 6,
    ABM_SKIN_HOLOGRAPHIC = 7,
    ABM_SKIN_COUNT = 8
} ABMBackgroundSkin;

/* One process-wide appearance selection shared by every ABM-owned surface.
   Colour-theme presets remain independent and are still managed by ui_theme. */
void ABM_SkinSet(int skin);
int ABM_SkinGet(void);
const wchar_t *ABM_SkinName(int skin);
BOOL ABM_SkinIsAnimated(void);

/* Register visible top-level surfaces that paint an ABM skin background.
   The main UI timer calls InvalidateRegisteredWindows only while a skin is
   animated; no permanent per-window animation timers are created. */
void ABM_SkinRegisterWindow(HWND hwnd);
void ABM_SkinUnregisterWindow(HWND hwnd);
void ABM_SkinInvalidateRegisteredWindows(void);

/* Draw the same low-contrast ambient layer into either renderer type.
   Call after the surface's base fill and before cards, controls or text. */
void ABM_SkinRenderD2D(ABM_D2D *renderer, const RECT *bounds);
void ABM_SkinRenderCanvas(ABM_D2D_CANVAS *canvas, const RECT *bounds);

#ifdef __cplusplus
}
#endif
#endif

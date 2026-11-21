#ifndef ABM_UI_SKIN_H
#define ABM_UI_SKIN_H
#include <windows.h>
#include "ui_d2d.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum ABMBackgroundSkin {
 ABM_SKIN_CLASSIC_MUSIC_PLAYER=0, ABM_SKIN_AURORA_FLOW=1, ABM_SKIN_LIQUID_GLASS=2,
 ABM_SKIN_COSMIC_DRIFT=3, ABM_SKIN_PRISM_WAVE=4, ABM_SKIN_GLASS_FACETS=5,
 ABM_SKIN_CARBON_WEAVE=6, ABM_SKIN_GOLD_GEOMETRY=7, ABM_SKIN_MINIMAL_HORIZON=8,
 ABM_SKIN_COUNT=9
} ABMBackgroundSkin;
void ABM_SkinSet(int skin);
int ABM_SkinGet(void);
const wchar_t *ABM_SkinName(int skin);
BOOL ABM_SkinIsAnimated(void);
void ABM_SkinRegisterWindow(HWND hwnd);
void ABM_SkinUnregisterWindow(HWND hwnd);
void ABM_SkinInvalidateRegisteredWindows(void);
void ABM_SkinRenderD2D(ABM_D2D *renderer,const RECT *bounds);
void ABM_SkinRenderCanvas(ABM_D2D_CANVAS *canvas,const RECT *bounds);
#ifdef __cplusplus
}
#endif
#endif

#ifndef ABM_UI_THEME_H
#define ABM_UI_THEME_H

/* ABM AV Player shared visual tokens.
 *
 * These constants define the target design language for the native Windows UI.
 * They are intentionally additive: existing windows adopt them incrementally
 * in their own UI/UX implementation steps. Do not change media, casting,
 * torrent, or WebView2 behaviour when consuming these tokens.
 */

#include <windows.h>

/* Surfaces */
#define ABM_UI_COLOR_BACKGROUND       RGB(6, 9, 16)
#define ABM_UI_COLOR_SURFACE          RGB(12, 18, 29)
#define ABM_UI_COLOR_SURFACE_RAISED   RGB(20, 29, 43)
#define ABM_UI_COLOR_SURFACE_BLUE     RGB(15, 31, 48)
#define ABM_UI_COLOR_INPUT            RGB(11, 25, 40)

/* Text */
#define ABM_UI_COLOR_TEXT             RGB(247, 250, 255)
#define ABM_UI_COLOR_TEXT_SECONDARY   RGB(226, 238, 250)
#define ABM_UI_COLOR_TEXT_MUTED       RGB(145, 160, 181)
#define ABM_UI_COLOR_TEXT_DISABLED    RGB(91, 108, 128)

/* Brand and semantic accents */
#define ABM_UI_COLOR_ACCENT           RGB(54, 177, 168)
#define ABM_UI_COLOR_ACCENT_BRIGHT    RGB(82, 205, 190)
#define ABM_UI_COLOR_ACCENT_SOFT      RGB(28, 91, 94)
#define ABM_UI_COLOR_ACCENT_ORANGE    RGB(255, 145, 28)
#define ABM_UI_COLOR_SUCCESS          RGB(77, 196, 153)
#define ABM_UI_COLOR_WARNING          RGB(242, 177, 79)
#define ABM_UI_COLOR_DANGER           RGB(220, 75, 85)

/* Borders and separators */
#define ABM_UI_COLOR_BORDER           RGB(39, 51, 70)
#define ABM_UI_COLOR_BORDER_STRONG    RGB(53, 111, 127)
#define ABM_UI_COLOR_FOCUS            RGB(99, 225, 205)

/* Typography: DirectWrite point-independent pixel/DIP target sizes.
 * Main application rendering currently uses a 96-DPI layout coordinate space.
 */
#define ABM_UI_FONT_FAMILY_TEXT       L"Segoe UI Variable Text"
#define ABM_UI_FONT_FAMILY_DISPLAY    L"Segoe UI Variable Display"
#define ABM_UI_FONT_SIZE_CAPTION      12.0f
#define ABM_UI_FONT_SIZE_BODY         14.0f
#define ABM_UI_FONT_SIZE_BODY_LARGE   16.0f
#define ABM_UI_FONT_SIZE_SECTION     18.0f
#define ABM_UI_FONT_SIZE_TITLE       24.0f
#define ABM_UI_FONT_SIZE_DISPLAY     30.0f

/* Spacing scale (layout units; 1 unit = 4 px at the current 96-DPI layout). */
#define ABM_UI_SPACE_1                4
#define ABM_UI_SPACE_2                8
#define ABM_UI_SPACE_3                12
#define ABM_UI_SPACE_4                16
#define ABM_UI_SPACE_5                20
#define ABM_UI_SPACE_6                24
#define ABM_UI_SPACE_8                32

/* Corner radii */
#define ABM_UI_RADIUS_CONTROL         6
#define ABM_UI_RADIUS_BUTTON          8
#define ABM_UI_RADIUS_CARD            12
#define ABM_UI_RADIUS_PANEL           16

/* Standard control heights */
#define ABM_UI_HEIGHT_COMPACT         30
#define ABM_UI_HEIGHT_STANDARD        36
#define ABM_UI_HEIGHT_PRIMARY         40

#endif /* ABM_UI_THEME_H */

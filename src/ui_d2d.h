#ifndef ABM_UI_D2D_H
#define ABM_UI_D2D_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque Direct2D objects keep this interface safe to include from C. */
typedef struct ABM_D2D_CANVAS {
    void *factory;
    void *target;
    void *writeFactory;
    void *body;
    void *smallFormat;
    void *iconFormat;
    void *brush;
    BOOL drawing;
} ABM_D2D_CANVAS;

typedef struct ABM_D2D {
    void *factory;
    void *target;
    void *writeFactory;
    void *body;
    void *smallFormat;
    void *heading;
    void *brandMain;
    void *brandSuffix;
    void *brush;
    HWND hwnd;
    BOOL clipActive;
    void *wicFactory;
    void *image;
} ABM_D2D;

BOOL ABM_D2D_Init(ABM_D2D *ui, HWND hwnd);
/* Shared Direct2D/DirectWrite renderer for owner-draw controls. The DC target
   binds to DRAWITEMSTRUCT::hDC, so it paints the actual control surface. */
BOOL ABM_D2D_CanvasInit(ABM_D2D_CANVAS *canvas);
BOOL ABM_D2D_CanvasBegin(ABM_D2D_CANVAS *canvas, HDC dc, const RECT *bounds);
BOOL ABM_D2D_CanvasEnd(ABM_D2D_CANVAS *canvas);
void ABM_D2D_CanvasFill(ABM_D2D_CANVAS *canvas, float l, float t, float r, float b, COLORREF color);
void ABM_D2D_CanvasFillRound(ABM_D2D_CANVAS *canvas, float l, float t, float r, float b, float radius, COLORREF color);
void ABM_D2D_CanvasStrokeRound(ABM_D2D_CANVAS *canvas, float l, float t, float r, float b, float radius, COLORREF color, float width);
void ABM_D2D_CanvasText(ABM_D2D_CANVAS *canvas, const wchar_t *text, RECT rect, COLORREF color, int style, UINT flags);
void ABM_D2D_CanvasDestroy(ABM_D2D_CANVAS *canvas);
void ABM_D2D_Resize(ABM_D2D *ui, UINT width, UINT height);
void ABM_D2D_Begin(ABM_D2D *ui, COLORREF clear);
BOOL ABM_D2D_BeginClip(ABM_D2D *ui, COLORREF clear, const RECT *clip);
BOOL ABM_D2D_End(ABM_D2D *ui);
void ABM_D2D_Fill(ABM_D2D *ui, float l, float t, float r, float b, COLORREF color);
void ABM_D2D_Round(ABM_D2D *ui, float l, float t, float r, float b, float radius, COLORREF color);
void ABM_D2D_GradientRound(ABM_D2D *ui, float l, float t, float r, float b, float radius, COLORREF start, COLORREF end);
void ABM_D2D_StrokeRound(ABM_D2D *ui, float l, float t, float r, float b, float radius, COLORREF color, float width);
void ABM_D2D_Line(ABM_D2D *ui, float x1, float y1, float x2, float y2, COLORREF color, float width);
void ABM_D2D_Text(ABM_D2D *ui, const wchar_t *text, RECT rect, COLORREF color, int style, UINT flags);
void ABM_D2D_Destroy(ABM_D2D *ui);
BOOL ABM_D2D_LoadImage(ABM_D2D *ui, const wchar_t *path);
void ABM_D2D_ClearImage(ABM_D2D *ui);
void ABM_D2D_DrawImage(ABM_D2D *ui, float l, float t, float r, float b);
void ABM_D2D_DrawImageCover(ABM_D2D *ui, float l, float t, float r, float b, float opacity);

/* Captures the Windows render mix for audio-reactive visualisation. */
BOOL ABM_AudioVisualizer_Start(void);
void ABM_AudioVisualizer_Stop(void);
void ABM_AudioVisualizer_GetBands(float *outBands, int count);

#ifdef __cplusplus
}
#endif
#endif

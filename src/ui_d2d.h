#ifndef ABM_UI_D2D_H
#define ABM_UI_D2D_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque Direct2D objects keep this interface safe to include from C. */
typedef struct ABM_D2D {
    void *factory;
    void *target;
    void *writeFactory;
    void *body;
    void *smallFormat;
    void *heading;
    void *brush;
    HWND hwnd;
    BOOL clipActive;
} ABM_D2D;

BOOL ABM_D2D_Init(ABM_D2D *ui, HWND hwnd);
void ABM_D2D_Resize(ABM_D2D *ui, UINT width, UINT height);
void ABM_D2D_Begin(ABM_D2D *ui, COLORREF clear);
void ABM_D2D_BeginClip(ABM_D2D *ui, COLORREF clear, const RECT *clip);
BOOL ABM_D2D_End(ABM_D2D *ui);
void ABM_D2D_Fill(ABM_D2D *ui, float l, float t, float r, float b, COLORREF color);
void ABM_D2D_Round(ABM_D2D *ui, float l, float t, float r, float b, float radius, COLORREF color);
void ABM_D2D_StrokeRound(ABM_D2D *ui, float l, float t, float r, float b, float radius, COLORREF color, float width);
void ABM_D2D_Line(ABM_D2D *ui, float x1, float y1, float x2, float y2, COLORREF color, float width);
void ABM_D2D_Text(ABM_D2D *ui, const wchar_t *text, RECT rect, COLORREF color, int style, UINT flags);
void ABM_D2D_Destroy(ABM_D2D *ui);

#ifdef __cplusplus
}
#endif
#endif

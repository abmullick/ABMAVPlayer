#ifndef ABM_UI_D2D_H
#define ABM_UI_D2D_H
#define COBJMACROS
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

typedef struct ABM_D2D {
    ID2D1Factory *factory;
    ID2D1HwndRenderTarget *target;
    IDWriteFactory *writeFactory;
    IDWriteTextFormat *body;
    IDWriteTextFormat *small;
    IDWriteTextFormat *heading;
    ID2D1SolidColorBrush *brush;
    HWND hwnd;
} ABM_D2D;

BOOL ABM_D2D_Init(ABM_D2D *ui, HWND hwnd);
void ABM_D2D_Resize(ABM_D2D *ui, UINT width, UINT height);
void ABM_D2D_Begin(ABM_D2D *ui, COLORREF clear);
BOOL ABM_D2D_End(ABM_D2D *ui);
void ABM_D2D_Fill(ABM_D2D *ui, float l,float t,float r,float b,COLORREF color);
void ABM_D2D_Round(ABM_D2D *ui,float l,float t,float r,float b,float radius,COLORREF color);
void ABM_D2D_StrokeRound(ABM_D2D *ui,float l,float t,float r,float b,float radius,COLORREF color,float width);
void ABM_D2D_Line(ABM_D2D *ui,float x1,float y1,float x2,float y2,COLORREF color,float width);
void ABM_D2D_Text(ABM_D2D *ui,const wchar_t *text,RECT rect,COLORREF color,int style,UINT flags);
void ABM_D2D_Destroy(ABM_D2D *ui);
#endif

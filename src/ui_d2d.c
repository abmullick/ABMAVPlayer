#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string.h>
#include "ui_d2d.h"

static D2D1_COLOR_F ToD2D(COLORREF c) {
    D2D1_COLOR_F v={(float)GetRValue(c)/255.0f,(float)GetGValue(c)/255.0f,(float)GetBValue(c)/255.0f,1.0f};
    return v;
}
static BOOL EnsureTarget(ABM_D2D *ui) {
    if(ui->target)return TRUE;
    RECT rc;GetClientRect(ui->hwnd,&rc);
    D2D1_RENDER_TARGET_PROPERTIES rp={D2D1_RENDER_TARGET_TYPE_DEFAULT,{DXGI_FORMAT_UNKNOWN,D2D1_ALPHA_MODE_UNKNOWN},0,0,D2D1_RENDER_TARGET_USAGE_NONE,D2D1_FEATURE_LEVEL_DEFAULT};
    D2D1_HWND_RENDER_TARGET_PROPERTIES hp={ui->hwnd,{(UINT)(rc.right-rc.left),(UINT)(rc.bottom-rc.top)},D2D1_PRESENT_OPTIONS_NONE};
    HRESULT hr=ID2D1Factory_CreateHwndRenderTarget(ui->factory,&rp,&hp,&ui->target);
    if(FAILED(hr))return FALSE;
    hr=ID2D1HwndRenderTarget_CreateSolidColorBrush(ui->target,&(D2D1_COLOR_F){1,1,1,1},NULL,&ui->brush);
    if(FAILED(hr)){ID2D1HwndRenderTarget_Release(ui->target);ui->target=NULL;return FALSE;}
    return TRUE;
}
static BOOL MakeFormat(IDWriteFactory *f,const wchar_t *family,float size,DWRITE_FONT_WEIGHT weight,IDWriteTextFormat **out) {
    return SUCCEEDED(IDWriteFactory_CreateTextFormat(f,family,NULL,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-us",out));
}
BOOL ABM_D2D_Init(ABM_D2D *ui,HWND hwnd) {
    ZeroMemory(ui,sizeof(*ui));ui->hwnd=hwnd;
    HRESULT hr=D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&IID_ID2D1Factory,NULL,(void**)&ui->factory);
    if(FAILED(hr))return FALSE;
    hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,&IID_IDWriteFactory,(IUnknown**)&ui->writeFactory);
    if(FAILED(hr)){ABM_D2D_Destroy(ui);return FALSE;}
    if(!MakeFormat(ui->writeFactory,L"Segoe UI",15.0f,DWRITE_FONT_WEIGHT_NORMAL,&ui->body)||
       !MakeFormat(ui->writeFactory,L"Segoe UI",12.0f,DWRITE_FONT_WEIGHT_SEMI_BOLD,&ui->small)||
       !MakeFormat(ui->writeFactory,L"Segoe UI",21.0f,DWRITE_FONT_WEIGHT_BOLD,&ui->heading)) {
        ABM_D2D_Destroy(ui);return FALSE;
    }
    return EnsureTarget(ui);
}
void ABM_D2D_Resize(ABM_D2D *ui,UINT width,UINT height) {
    if(ui->target){D2D1_SIZE_U s={width,height};HRESULT hr=ID2D1HwndRenderTarget_Resize(ui->target,&s);if(hr==D2DERR_RECREATE_TARGET){ID2D1SolidColorBrush_Release(ui->brush);ui->brush=NULL;ID2D1HwndRenderTarget_Release(ui->target);ui->target=NULL;}}
}
void ABM_D2D_Begin(ABM_D2D *ui,COLORREF clear) {
    if(!EnsureTarget(ui))return;
    D2D1_COLOR_F c=ToD2D(clear);ID2D1HwndRenderTarget_BeginDraw(ui->target);ID2D1HwndRenderTarget_Clear(ui->target,&c);
}
BOOL ABM_D2D_End(ABM_D2D *ui) {
    if(!ui->target)return FALSE;
    HRESULT hr=ID2D1HwndRenderTarget_EndDraw(ui->target,NULL,NULL);
    if(hr==D2DERR_RECREATE_TARGET){if(ui->brush){ID2D1SolidColorBrush_Release(ui->brush);ui->brush=NULL;}ID2D1HwndRenderTarget_Release(ui->target);ui->target=NULL;return FALSE;}
    return SUCCEEDED(hr);
}
static void SetColor(ABM_D2D *ui,COLORREF color){D2D1_COLOR_F c=ToD2D(color);ID2D1SolidColorBrush_SetColor(ui->brush,&c);}
void ABM_D2D_Fill(ABM_D2D *ui,float l,float t,float r,float b,COLORREF color) {
    if(!EnsureTarget(ui))return;SetColor(ui,color);D2D1_RECT_F x={l,t,r,b};ID2D1RenderTarget_FillRectangle((ID2D1RenderTarget*)ui->target,&x,(ID2D1Brush*)ui->brush);
}
void ABM_D2D_Round(ABM_D2D *ui,float l,float t,float r,float b,float radius,COLORREF color) {
    if(!EnsureTarget(ui))return;SetColor(ui,color);D2D1_ROUNDED_RECT x={{l,t,r,b},radius,radius};ID2D1RenderTarget_FillRoundedRectangle((ID2D1RenderTarget*)ui->target,&x,(ID2D1Brush*)ui->brush);
}
void ABM_D2D_StrokeRound(ABM_D2D *ui,float l,float t,float r,float b,float radius,COLORREF color,float width) {
    if(!EnsureTarget(ui))return;SetColor(ui,color);D2D1_ROUNDED_RECT x={{l,t,r,b},radius,radius};ID2D1RenderTarget_DrawRoundedRectangle((ID2D1RenderTarget*)ui->target,&x,(ID2D1Brush*)ui->brush,width,NULL);
}
void ABM_D2D_Line(ABM_D2D *ui,float x1,float y1,float x2,float y2,COLORREF color,float width) {
    if(!EnsureTarget(ui))return;SetColor(ui,color);D2D1_POINT_2F a={x1,y1},b={x2,y2};ID2D1RenderTarget_DrawLine((ID2D1RenderTarget*)ui->target,a,b,(ID2D1Brush*)ui->brush,width,NULL);
}
void ABM_D2D_Text(ABM_D2D *ui,const wchar_t *text,RECT rect,COLORREF color,int style,UINT flags) {
    if(!EnsureTarget(ui)||!text||!text[0])return;
    IDWriteTextFormat *fmt=style==1?ui->small:(style==2?ui->heading:ui->body);
    DWRITE_TEXT_ALIGNMENT align=(flags&DT_CENTER)?DWRITE_TEXT_ALIGNMENT_CENTER:((flags&DT_RIGHT)?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING);
    DWRITE_PARAGRAPH_ALIGNMENT valign=(flags&DT_VCENTER)?DWRITE_PARAGRAPH_ALIGNMENT_CENTER:DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    IDWriteTextFormat_SetTextAlignment(fmt,align);IDWriteTextFormat_SetParagraphAlignment(fmt,valign);
    IDWriteTextFormat_SetWordWrapping(fmt,(flags&DT_SINGLELINE)?DWRITE_WORD_WRAPPING_NO_WRAP:DWRITE_WORD_WRAPPING_WRAP);
    SetColor(ui,color);D2D1_RECT_F layout={(FLOAT)rect.left,(FLOAT)rect.top,(FLOAT)rect.right,(FLOAT)rect.bottom};
    ID2D1RenderTarget_DrawText((ID2D1RenderTarget*)ui->target,text,(UINT32)wcslen(text),fmt,&layout,(ID2D1Brush*)ui->brush,D2D1_DRAW_TEXT_OPTIONS_CLIP,DWRITE_MEASURING_MODE_NATURAL);
}
void ABM_D2D_Destroy(ABM_D2D *ui) {
    if(ui->brush)ID2D1SolidColorBrush_Release(ui->brush);
    if(ui->target)ID2D1HwndRenderTarget_Release(ui->target);
    if(ui->body)IDWriteTextFormat_Release(ui->body);
    if(ui->small)IDWriteTextFormat_Release(ui->small);
    if(ui->heading)IDWriteTextFormat_Release(ui->heading);
    if(ui->writeFactory)IDWriteFactory_Release(ui->writeFactory);
    if(ui->factory)ID2D1Factory_Release(ui->factory);
    ZeroMemory(ui,sizeof(*ui));
}

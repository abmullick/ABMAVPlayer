#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dxgiformat.h>
#include <dwrite.h>
#include <wincodec.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include <strsafe.h>
#include "ui_d2d.h"
#include "ui_theme.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

template <typename T> static void SafeRelease(T **value) {
    if (value && *value) { (*value)->Release(); *value = nullptr; }
}

static D2D1_COLOR_F ToD2D(COLORREF c) {
    return D2D1_COLOR_F{
        static_cast<float>(GetRValue(c)) / 255.0f,
        static_cast<float>(GetGValue(c)) / 255.0f,
        static_cast<float>(GetBValue(c)) / 255.0f,
        1.0f
    };
}

static ID2D1Factory *Factory(ABM_D2D *ui) { return static_cast<ID2D1Factory *>(ui->factory); }
static ID2D1HwndRenderTarget *Target(ABM_D2D *ui) { return static_cast<ID2D1HwndRenderTarget *>(ui->target); }
static ID2D1SolidColorBrush *Brush(ABM_D2D *ui) { return static_cast<ID2D1SolidColorBrush *>(ui->brush); }
static std::vector<BYTE> g_bgraFrameUploadScratch;
static volatile LONG g_projectMFrameDiagLogged = 0;
static void LogProjectMFrameDiag(const wchar_t *message) {
    if (InterlockedCompareExchange(&g_projectMFrameDiagLogged, 1, 0) != 0) return;
    wchar_t path[MAX_PATH] = {};
    DWORD n = GetTempPathW(_countof(path), path);
    if (!n || n >= _countof(path)) return;
    StringCchCatW(path, _countof(path), L"ABM-ProjectM-renderer.log");
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, message, (DWORD)(wcslen(message) * sizeof(wchar_t)), &written, nullptr);
    WriteFile(file, L"\\r\\n", 4, &written, nullptr);
    CloseHandle(file);
}

static void SetColor(ABM_D2D *ui, COLORREF color);

static BOOL EnsureTarget(ABM_D2D *ui) {
    if (ui->target) return TRUE;
    RECT rc{};
    GetClientRect(ui->hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hp =
        D2D1::HwndRenderTargetProperties(ui->hwnd,
            D2D1::SizeU(static_cast<UINT>((rc.right > rc.left) ? (rc.right - rc.left) : 1),
                        static_cast<UINT>((rc.bottom > rc.top) ? (rc.bottom - rc.top) : 1)));
    ID2D1HwndRenderTarget *target = nullptr;
    HRESULT hr = Factory(ui)->CreateHwndRenderTarget(rp, hp, &target);
    if (FAILED(hr)) return FALSE;

    /* The Win32 layout engine and hit-testing use client-area pixel
       coordinates. An HWND render target otherwise inherits the system DPI
       and interprets those same values as DIPs, stretching the drawing beyond
       the client area on scaled displays (for example 125% / 150%). Keep the
       render target in the same 96-DPI coordinate space as the layout. */
    target->SetDpi(96.0f, 96.0f);

    ID2D1SolidColorBrush *brush = nullptr;
    hr = target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);
    if (FAILED(hr)) { target->Release(); return FALSE; }
    /* Explicit ClearType text rasterization keeps small labels crisp on
       high-density Windows displays instead of inheriting grayscale AA. */
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    ui->target = target;
    ui->brush = brush;
    return TRUE;
}

static BOOL MakeFormat(IDWriteFactory *factory, const wchar_t *family,
                       float size, DWRITE_FONT_WEIGHT weight,
                       IDWriteTextFormat **out) {
    return SUCCEEDED(factory->CreateTextFormat(
        family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", out));
}

/* One authoritative format set for every ABM-owned Direct2D surface.
   Values intentionally match the Main Player's original typography. */
static BOOL CreateCanonicalTextFormats(
    IDWriteFactory *factory,
    IDWriteTextFormat **body,
    IDWriteTextFormat **smallFormat,
    IDWriteTextFormat **heading,
    IDWriteTextFormat **brandMain,
    IDWriteTextFormat **brandSuffix,
    IDWriteTextFormat **icon) {
    if (!factory || !body || !smallFormat || !heading || !brandMain || !brandSuffix || !icon)
        return FALSE;
    return
        MakeFormat(factory, ABM_UI_FONT_FAMILY_TEXT, ABM_UI_FONT_SIZE_D2D_BODY,
                   DWRITE_FONT_WEIGHT_NORMAL, body) &&
        MakeFormat(factory, ABM_UI_FONT_FAMILY_TEXT, ABM_UI_FONT_SIZE_D2D_SMALL,
                   DWRITE_FONT_WEIGHT_SEMI_BOLD, smallFormat) &&
        MakeFormat(factory, ABM_UI_FONT_FAMILY_DISPLAY, ABM_UI_FONT_SIZE_D2D_HEADING,
                   DWRITE_FONT_WEIGHT_SEMI_BOLD, heading) &&
        MakeFormat(factory, ABM_UI_FONT_FAMILY_DISPLAY, ABM_UI_FONT_SIZE_DISPLAY,
                   DWRITE_FONT_WEIGHT_BOLD, brandMain) &&
        MakeFormat(factory, ABM_UI_FONT_FAMILY_TEXT, ABM_UI_FONT_SIZE_SECTION,
                   DWRITE_FONT_WEIGHT_MEDIUM, brandSuffix) &&
        MakeFormat(factory, ABM_UI_FONT_FAMILY_ICON, ABM_UI_FONT_SIZE_ICON,
                   DWRITE_FONT_WEIGHT_NORMAL, icon);
}


static ID2D1DCRenderTarget *CanvasTarget(ABM_D2D_CANVAS *c) { return static_cast<ID2D1DCRenderTarget *>(c->target); }
static ID2D1SolidColorBrush *CanvasBrush(ABM_D2D_CANVAS *c) { return static_cast<ID2D1SolidColorBrush *>(c->brush); }

static BOOL CanvasCreateTarget(ABM_D2D_CANVAS *c) {
    if(!c||!c->factory)return FALSE;
    if(c->target&&c->brush)return TRUE;
    SafeRelease(reinterpret_cast<ID2D1SolidColorBrush **>(&c->brush));
    SafeRelease(reinterpret_cast<ID2D1DCRenderTarget **>(&c->target));
    D2D1_RENDER_TARGET_PROPERTIES rp=D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,96.0f,D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE,D2D1_FEATURE_LEVEL_DEFAULT);
    ID2D1DCRenderTarget *target=nullptr;
    if(FAILED(static_cast<ID2D1Factory *>(c->factory)->CreateDCRenderTarget(&rp,&target)))return FALSE;
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    ID2D1SolidColorBrush *brush=nullptr;
    if(FAILED(target->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&brush))){
        target->Release();return FALSE;
    }
    c->target=target;c->brush=brush;
    return TRUE;
}

extern "C" BOOL ABM_D2D_CanvasInit(ABM_D2D_CANVAS *c) {
    if (!c) return FALSE;
    ZeroMemory(c, sizeof(*c));
    ID2D1Factory *factory = nullptr;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory))) return FALSE;
    c->factory = factory;
    IDWriteFactory *wf = nullptr;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown **>(&wf)))) { ABM_D2D_CanvasDestroy(c); return FALSE; }
    c->writeFactory = wf;
    IDWriteTextFormat *body = nullptr, *smallText = nullptr, *heading = nullptr;
    IDWriteTextFormat *brandMain = nullptr, *brandSuffix = nullptr, *icon = nullptr;
    if (!CreateCanonicalTextFormats(wf, &body, &smallText, &heading,
                                    &brandMain, &brandSuffix, &icon)) {
        SafeRelease(&body); SafeRelease(&smallText); SafeRelease(&heading);
        SafeRelease(&brandMain); SafeRelease(&brandSuffix); SafeRelease(&icon);
        ABM_D2D_CanvasDestroy(c); return FALSE;
    }
    c->body = body; c->smallFormat = smallText; c->heading = heading;
    c->brandMain = brandMain; c->brandSuffix = brandSuffix; c->iconFormat = icon;
    IDWriteInlineObject *ellipsis = nullptr;
    if (SUCCEEDED(wf->CreateEllipsisTrimmingSign(static_cast<IDWriteTextFormat *>(c->smallFormat), &ellipsis))) c->ellipsis = ellipsis;
    if(!CanvasCreateTarget(c)){ABM_D2D_CanvasDestroy(c);return FALSE;}
    return TRUE;
}
extern "C" BOOL ABM_D2D_CanvasBegin(ABM_D2D_CANVAS *c, HDC dc, const RECT *bounds) {
    if (!c || !dc || !bounds || c->drawing || !CanvasCreateTarget(c)) return FALSE;
    if (FAILED(CanvasTarget(c)->BindDC(dc, bounds))) return FALSE;
    c->hwnd = WindowFromDC(dc);
    CanvasTarget(c)->BeginDraw(); c->drawing = TRUE; return TRUE;
}
extern "C" BOOL ABM_D2D_CanvasEnd(ABM_D2D_CANVAS *c) {
    if (!c || !c->target || !c->drawing) return FALSE;
    c->drawing = FALSE;
    HRESULT hr = CanvasTarget(c)->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        SafeRelease(reinterpret_cast<ID2D1SolidColorBrush **>(&c->brush));
        SafeRelease(reinterpret_cast<ID2D1DCRenderTarget **>(&c->target));
        /* DRAWITEM DCs are transient: schedule a fresh paint after target loss. */
        if (c->hwnd && IsWindow(c->hwnd)) InvalidateRect(c->hwnd, NULL, FALSE);
        return FALSE;
    }
    if (FAILED(hr) && c->hwnd && IsWindow(c->hwnd))
        InvalidateRect(c->hwnd, NULL, FALSE);
    return SUCCEEDED(hr);
}
extern "C" void ABM_D2D_CanvasLine(ABM_D2D_CANVAS *c,float x1,float y1,float x2,float y2,COLORREF color,float width) {
    if (!c || !c->drawing) return;
    CanvasBrush(c)->SetColor(ToD2D(color));
    CanvasTarget(c)->DrawLine(D2D1::Point2F(x1,y1),D2D1::Point2F(x2,y2),CanvasBrush(c),width);
}
extern "C" void ABM_D2D_CanvasFill(ABM_D2D_CANVAS *c,float l,float t,float r,float b,COLORREF color) {
    if (!c || !c->drawing || r<=l || b<=t) return;
    CanvasBrush(c)->SetColor(ToD2D(color));
    CanvasTarget(c)->FillRectangle(D2D1::RectF(l,t,r,b),CanvasBrush(c));
}
extern "C" void ABM_D2D_CanvasFillRound(ABM_D2D_CANVAS *c,float l,float t,float r,float b,float radius,COLORREF color) {
    if (!c || !c->drawing || r<=l || b<=t) return;
    D2D1_COLOR_F col=ToD2D(color); CanvasBrush(c)->SetColor(col);
    CanvasTarget(c)->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(l,t,r,b),radius,radius),CanvasBrush(c));
}
extern "C" void ABM_D2D_CanvasStrokeRound(ABM_D2D_CANVAS *c,float l,float t,float r,float b,float radius,COLORREF color,float width) {
    if (!c || !c->drawing || r<=l || b<=t) return;
    CanvasBrush(c)->SetColor(ToD2D(color));
    CanvasTarget(c)->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(l,t,r,b),radius,radius),CanvasBrush(c),width);
}
extern "C" void ABM_D2D_CanvasText(ABM_D2D_CANVAS *c,const wchar_t *value,RECT rect,COLORREF color,int style,UINT flags) {
    if (!c || !c->drawing || !value || !value[0]) return;
    IDWriteTextFormat *fmt=static_cast<IDWriteTextFormat *>(
        style==ABM_D2D_TEXT_ICON?c->iconFormat:
        style==ABM_D2D_TEXT_SMALL?c->smallFormat:
        style==ABM_D2D_TEXT_HEADING?c->heading:
        style==ABM_D2D_TEXT_BRAND_MAIN?c->brandMain:
        style==ABM_D2D_TEXT_BRAND_SUFFIX?c->brandSuffix:c->body);
    fmt->SetTextAlignment((flags&DT_CENTER)?DWRITE_TEXT_ALIGNMENT_CENTER:((flags&DT_RIGHT)?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING));
    fmt->SetParagraphAlignment((flags&DT_VCENTER)?DWRITE_PARAGRAPH_ALIGNMENT_CENTER:DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    fmt->SetWordWrapping((flags&DT_SINGLELINE)?DWRITE_WORD_WRAPPING_NO_WRAP:DWRITE_WORD_WRAPPING_WRAP);
    if ((flags&DT_END_ELLIPSIS) && c->ellipsis) {
        DWRITE_TRIMMING trimming={DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};
        fmt->SetTrimming(&trimming,static_cast<IDWriteInlineObject *>(c->ellipsis));
    } else {
        DWRITE_TRIMMING trimming={DWRITE_TRIMMING_GRANULARITY_NONE,0,0};
        fmt->SetTrimming(&trimming,nullptr);
    }
    CanvasBrush(c)->SetColor(ToD2D(color));
    D2D1_RECT_F box=D2D1::RectF((float)rect.left,(float)rect.top,(float)rect.right,(float)rect.bottom);
    CanvasTarget(c)->DrawText(value,(UINT32)wcslen(value),fmt,box,CanvasBrush(c),D2D1_DRAW_TEXT_OPTIONS_CLIP,DWRITE_MEASURING_MODE_NATURAL);
}
extern "C" void ABM_D2D_CanvasDestroy(ABM_D2D_CANVAS *c) {
    if (!c) return;
    SafeRelease(reinterpret_cast<ID2D1SolidColorBrush **>(&c->brush));
    SafeRelease(reinterpret_cast<ID2D1DCRenderTarget **>(&c->target));
    SafeRelease(reinterpret_cast<IDWriteInlineObject **>(&c->ellipsis));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&c->body));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&c->smallFormat));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&c->heading));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&c->brandMain));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&c->brandSuffix));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&c->iconFormat));
    SafeRelease(reinterpret_cast<IDWriteFactory **>(&c->writeFactory));
    SafeRelease(reinterpret_cast<ID2D1Factory **>(&c->factory));
    ZeroMemory(c,sizeof(*c));
}

extern "C" BOOL ABM_D2D_Init(ABM_D2D *ui, HWND hwnd) {
    if (!ui || !hwnd) return FALSE;
    ZeroMemory(ui, sizeof(*ui));
    ui->hwnd = hwnd;

    ID2D1Factory *factory = nullptr;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
    if (FAILED(hr)) return FALSE;
    ui->factory = factory;

    IDWriteFactory *writeFactory = nullptr;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(&writeFactory));
    if (FAILED(hr)) { ABM_D2D_Destroy(ui); return FALSE; }
    ui->writeFactory = writeFactory;

    IDWriteTextFormat *body = nullptr, *smallFormat = nullptr, *heading = nullptr;
    IDWriteTextFormat *brandMain = nullptr, *brandSuffix = nullptr, *icon = nullptr;
    /* Main Player and all secondary surfaces consume this exact format set. */
    if (!CreateCanonicalTextFormats(writeFactory, &body, &smallFormat, &heading,
                                    &brandMain, &brandSuffix, &icon)) {
        SafeRelease(&body); SafeRelease(&smallFormat); SafeRelease(&heading);
        SafeRelease(&brandMain); SafeRelease(&brandSuffix); SafeRelease(&icon);
        ABM_D2D_Destroy(ui);
        return FALSE;
    }
    ui->body = body; ui->smallFormat = smallFormat; ui->heading = heading;
    ui->brandMain = brandMain; ui->brandSuffix = brandSuffix; ui->iconFormat = icon;
    if (!EnsureTarget(ui)) { ABM_D2D_Destroy(ui); return FALSE; }
    return TRUE;
}

extern "C" void ABM_D2D_Resize(ABM_D2D *ui, UINT width, UINT height) {
    if (!ui || !ui->target) return;
    D2D1_SIZE_U size = D2D1::SizeU(width, height);
    HRESULT hr = Target(ui)->Resize(&size);
    if (FAILED(hr)) {
        SafeRelease(reinterpret_cast<ID2D1Bitmap **>(&ui->frameImage));
        ui->frameImageWidth = ui->frameImageHeight = 0;
        SafeRelease(reinterpret_cast<ID2D1SolidColorBrush **>(&ui->brush));
        SafeRelease(reinterpret_cast<ID2D1HwndRenderTarget **>(&ui->target));
        if (ui->hwnd && IsWindow(ui->hwnd)) InvalidateRect(ui->hwnd, NULL, FALSE);
    }
}

extern "C" void ABM_D2D_Begin(ABM_D2D *ui, COLORREF clear) {
    if (!ui || !EnsureTarget(ui)) return;
    Target(ui)->BeginDraw();
    D2D1_COLOR_F color = ToD2D(clear);
    Target(ui)->Clear(&color);
    ui->clipActive = FALSE;
}

extern "C" BOOL ABM_D2D_BeginClip(ABM_D2D *ui, COLORREF clear, const RECT *clip) {
    if (!ui || !clip || !EnsureTarget(ui)) return FALSE;
    Target(ui)->BeginDraw();
    D2D1_RECT_F bounds = D2D1::RectF(
        static_cast<float>(clip->left), static_cast<float>(clip->top),
        static_cast<float>(clip->right), static_cast<float>(clip->bottom));
    Target(ui)->PushAxisAlignedClip(bounds, D2D1_ANTIALIAS_MODE_ALIASED);
    ui->clipActive = TRUE;
    SetColor(ui, clear);
    Target(ui)->FillRectangle(bounds, Brush(ui));
    return TRUE;
}

extern "C" void ABM_D2D_SetOffset(ABM_D2D *ui, float x, float y) {
    if (!ui || !ui->target) return;
    Target(ui)->SetTransform(D2D1::Matrix3x2F::Translation(x, y));
}

extern "C" BOOL ABM_D2D_End(ABM_D2D *ui) {
    if (!ui || !ui->target) return FALSE;
    if (ui->clipActive) {
        Target(ui)->SetTransform(D2D1::Matrix3x2F::Identity());
        Target(ui)->PopAxisAlignedClip();
        ui->clipActive = FALSE;
    }
    HRESULT hr = Target(ui)->EndDraw();
    if (FAILED(hr)) {
        wchar_t line[192];
        StringCchPrintfW(line, _countof(line),
            L"player: Direct2D EndDraw failed: 0x%08lX (recreate=%d)",
            (unsigned long)hr, hr == D2DERR_RECREATE_TARGET ? 1 : 0);
        LogProjectMFrameDiag(line);
    }
    if (hr == D2DERR_RECREATE_TARGET) {
        SafeRelease(reinterpret_cast<ID2D1Bitmap **>(&ui->frameImage));
        ui->frameImageWidth = ui->frameImageHeight = 0;
        SafeRelease(reinterpret_cast<ID2D1SolidColorBrush **>(&ui->brush));
        SafeRelease(reinterpret_cast<ID2D1HwndRenderTarget **>(&ui->target));
        if (ui->hwnd && IsWindow(ui->hwnd)) InvalidateRect(ui->hwnd, NULL, FALSE);
        return FALSE;
    }
    if (FAILED(hr) && ui->hwnd && IsWindow(ui->hwnd))
        InvalidateRect(ui->hwnd, NULL, FALSE);
    return SUCCEEDED(hr);
}

static void SetColor(ABM_D2D *ui, COLORREF color) {
    D2D1_COLOR_F c = ToD2D(color);
    Brush(ui)->SetColor(c);
}

extern "C" void ABM_D2D_Fill(ABM_D2D *ui, float l, float t, float r, float b, COLORREF color) {
    if (!ui || !EnsureTarget(ui)) return;
    SetColor(ui, color);
    D2D1_RECT_F rect{l, t, r, b};
    Target(ui)->FillRectangle(rect, Brush(ui));
}

extern "C" void ABM_D2D_Round(ABM_D2D *ui, float l, float t, float r, float b,
                               float radius, COLORREF color) {
    if (!ui || !EnsureTarget(ui)) return;
    SetColor(ui, color);
    D2D1_ROUNDED_RECT rect{D2D1::RectF(l, t, r, b), radius, radius};
    Target(ui)->FillRoundedRectangle(rect, Brush(ui));
}

extern "C" void ABM_D2D_GradientRound(ABM_D2D *ui, float l, float t, float r, float b,
                                         float radius, COLORREF start, COLORREF end) {
    if (!ui || !EnsureTarget(ui) || r <= l || b <= t) return;
    D2D1_GRADIENT_STOP stops[2] = {
        {0.0f, ToD2D(start)}, {1.0f, ToD2D(end)}
    };
    ID2D1GradientStopCollection *collection = nullptr;
    ID2D1LinearGradientBrush *gradient = nullptr;
    HRESULT hr = Target(ui)->CreateGradientStopCollection(stops, 2,
        D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &collection);
    if (SUCCEEDED(hr)) {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = {
            D2D1::Point2F(l, t), D2D1::Point2F(r, t)
        };
        hr = Target(ui)->CreateLinearGradientBrush(props,
            D2D1::BrushProperties(1.0f, D2D1::Matrix3x2F::Identity()),
            collection, &gradient);
    }
    if (SUCCEEDED(hr)) {
        D2D1_ROUNDED_RECT rect{D2D1::RectF(l, t, r, b), radius, radius};
        Target(ui)->FillRoundedRectangle(rect, gradient);
    }
    SafeRelease(&gradient);
    SafeRelease(&collection);
}

extern "C" void ABM_D2D_StrokeRound(ABM_D2D *ui, float l, float t, float r, float b,
                                     float radius, COLORREF color, float width) {
    if (!ui || !EnsureTarget(ui)) return;
    SetColor(ui, color);
    D2D1_ROUNDED_RECT rect{D2D1::RectF(l, t, r, b), radius, radius};
    Target(ui)->DrawRoundedRectangle(rect, Brush(ui), width);
}

extern "C" void ABM_D2D_Line(ABM_D2D *ui, float x1, float y1, float x2, float y2,
                              COLORREF color, float width) {
    if (!ui || !EnsureTarget(ui)) return;
    SetColor(ui, color);
    Target(ui)->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), Brush(ui), width);
}

extern "C" void ABM_D2D_Polyline(ABM_D2D *ui,const float *xy,int pointCount,
                                      COLORREF color,float width) {
    if(!ui||!xy||pointCount<2||!EnsureTarget(ui))return;
    if(pointCount>512)pointCount=512;
    D2D1_POINT_2F points[512];
    for(int i=0;i<pointCount;i++) {
        points[i].x=xy[i*2];
        points[i].y=xy[i*2+1];
    }
    ID2D1PathGeometry *geometry=nullptr;
    ID2D1GeometrySink *sink=nullptr;
    HRESULT hr=Factory(ui)->CreatePathGeometry(&geometry);
    if(SUCCEEDED(hr))hr=geometry->Open(&sink);
    if(SUCCEEDED(hr)) {
        sink->BeginFigure(points[0],D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLines(points+1,(UINT32)(pointCount-1));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        hr=sink->Close();
    }
    if(SUCCEEDED(hr)) {
        SetColor(ui,color);
        Target(ui)->DrawGeometry(geometry,Brush(ui),width);
    }
    SafeRelease(&sink);
    SafeRelease(&geometry);
}

extern "C" void ABM_D2D_Text(ABM_D2D *ui, const wchar_t *text, RECT rect,
                              COLORREF color, int style, UINT flags) {
    if (!ui || !EnsureTarget(ui) || !text || !text[0]) return;
    IDWriteTextFormat *format = static_cast<IDWriteTextFormat *>(
        style == ABM_D2D_TEXT_SMALL ? ui->smallFormat :
        style == ABM_D2D_TEXT_HEADING ? ui->heading :
        style == ABM_D2D_TEXT_BRAND_MAIN ? ui->brandMain :
        style == ABM_D2D_TEXT_BRAND_SUFFIX ? ui->brandSuffix :
        style == ABM_D2D_TEXT_ICON ? ui->iconFormat : ui->body);
    DWRITE_TEXT_ALIGNMENT align = (flags & DT_CENTER) ? DWRITE_TEXT_ALIGNMENT_CENTER :
        ((flags & DT_RIGHT) ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    DWRITE_PARAGRAPH_ALIGNMENT valign = (flags & DT_VCENTER) ?
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    format->SetTextAlignment(align);
    format->SetParagraphAlignment(valign);
    format->SetWordWrapping((flags & DT_SINGLELINE) ?
        DWRITE_WORD_WRAPPING_NO_WRAP : DWRITE_WORD_WRAPPING_WRAP);
    SetColor(ui, color);
    D2D1_RECT_F layout{static_cast<float>(rect.left), static_cast<float>(rect.top),
                       static_cast<float>(rect.right), static_cast<float>(rect.bottom)};
    Target(ui)->DrawText(text, static_cast<UINT32>(wcslen(text)), format, layout,
                         Brush(ui), D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
}

extern "C" BOOL ABM_D2D_LoadImage(ABM_D2D *ui, const wchar_t *path) {
    if (!ui || !path || !path[0] || !EnsureTarget(ui)) return FALSE;
    IWICImagingFactory *wic = static_cast<IWICImagingFactory *>(ui->wicFactory);
    if (!wic) {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&wic));
        if (FAILED(hr)) return FALSE;
        ui->wicFactory = wic;
    }
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *converter = nullptr;
    ID2D1Bitmap *bitmap = nullptr;
    HRESULT hr = wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) hr = Target(ui)->CreateBitmapFromWicBitmap(converter, nullptr, &bitmap);
    SafeRelease(&converter); SafeRelease(&frame); SafeRelease(&decoder);
    if (FAILED(hr)) { SafeRelease(&bitmap); return FALSE; }
    SafeRelease(reinterpret_cast<ID2D1Bitmap **>(&ui->image));
    ui->image = bitmap;
    return TRUE;
}

extern "C" void ABM_D2D_ClearImage(ABM_D2D *ui) {
    if (ui) SafeRelease(reinterpret_cast<ID2D1Bitmap **>(&ui->image));
}

extern "C" void ABM_D2D_DrawImage(ABM_D2D *ui, float l, float t, float r, float b) {
    if (!ui || !ui->image || !EnsureTarget(ui)) return;
    ID2D1Bitmap *bitmap = static_cast<ID2D1Bitmap *>(ui->image);
    D2D1_SIZE_F size = bitmap->GetSize();
    float boxW = r-l, boxH = b-t;
    if (size.width <= 0 || size.height <= 0 || boxW <= 0 || boxH <= 0) return;
    float scale = (std::min)(boxW/size.width, boxH/size.height);
    float w = size.width*scale, h = size.height*scale;
    float x = l+(boxW-w)*0.5f, y = t+(boxH-h)*0.5f;
    Target(ui)->DrawBitmap(bitmap, D2D1::RectF(x,y,x+w,y+h), 1.0f,
                           D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

extern "C" void ABM_D2D_DrawImageCover(ABM_D2D *ui, float l, float t, float r, float b, float opacity) {
    if (!ui || !ui->image || !EnsureTarget(ui)) return;
    ID2D1Bitmap *bitmap = static_cast<ID2D1Bitmap *>(ui->image);
    D2D1_SIZE_F size = bitmap->GetSize();
    float boxW = r-l, boxH = b-t;
    if (size.width <= 0 || size.height <= 0 || boxW <= 0 || boxH <= 0) return;

    /* Crop the source to the destination aspect ratio, preserving the image's
       proportions. This is used for the soft, low-opacity artwork backdrop. */
    float sourceW = size.width, sourceH = size.height;
    float imageAspect = size.width / size.height;
    float boxAspect = boxW / boxH;
    if (imageAspect > boxAspect) {
        sourceW = size.height * boxAspect;
    } else {
        sourceH = size.width / boxAspect;
    }
    float sx = (size.width - sourceW) * 0.5f;
    float sy = (size.height - sourceH) * 0.5f;
    D2D1_RECT_F source = D2D1::RectF(sx, sy, sx + sourceW, sy + sourceH);
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    Target(ui)->DrawBitmap(bitmap, D2D1::RectF(l,t,r,b), opacity,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
}

struct FileImageCacheEntry {
    wchar_t path[MAX_PATH];
    ID2D1Bitmap *bitmap;
    ULONGLONG lastUse;
};
static FileImageCacheEntry g_fileImageCache[64]={};
static ID2D1HwndRenderTarget *g_fileImageCacheTarget=nullptr;
static void ClearFileImageCache(void) {
    for(auto &entry:g_fileImageCache){SafeRelease(&entry.bitmap);entry.path[0]=0;entry.lastUse=0;}
    g_fileImageCacheTarget=nullptr;
}
extern "C" BOOL ABM_D2D_DrawImageFile(ABM_D2D *ui,const wchar_t *path,float l,float t,float r,float b) {
    if(!ui||!path||!path[0]||!EnsureTarget(ui))return FALSE;
    ID2D1HwndRenderTarget *target=Target(ui);
    if(g_fileImageCacheTarget!=target){ClearFileImageCache();g_fileImageCacheTarget=target;}
    FileImageCacheEntry *entry=nullptr,*victim=&g_fileImageCache[0];
    ULONGLONG now=GetTickCount64();
    for(auto &candidate:g_fileImageCache) {
        if(candidate.bitmap&&_wcsicmp(candidate.path,path)==0){entry=&candidate;break;}
        if(!candidate.bitmap){victim=&candidate;break;}
        if(candidate.lastUse<victim->lastUse)victim=&candidate;
    }
    if(!entry) {
        IWICImagingFactory *wic=nullptr;IWICBitmapDecoder *decoder=nullptr;
        IWICBitmapFrameDecode *frame=nullptr;IWICFormatConverter *converter=nullptr;
        ID2D1Bitmap *bitmap=nullptr;
        HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));
        if(SUCCEEDED(hr))hr=wic->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder);
        if(SUCCEEDED(hr))hr=decoder->GetFrame(0,&frame);
        if(SUCCEEDED(hr))hr=wic->CreateFormatConverter(&converter);
        if(SUCCEEDED(hr))hr=converter->Initialize(frame,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom);
        if(SUCCEEDED(hr))hr=target->CreateBitmapFromWicBitmap(converter,nullptr,&bitmap);
        SafeRelease(&converter);SafeRelease(&frame);SafeRelease(&decoder);SafeRelease(&wic);
        if(FAILED(hr)||!bitmap){SafeRelease(&bitmap);return FALSE;}
        entry=victim;SafeRelease(&entry->bitmap);entry->bitmap=bitmap;
        wcsncpy_s(entry->path,_countof(entry->path),path,_TRUNCATE);
    }
    entry->lastUse=now;
    D2D1_SIZE_F size=entry->bitmap->GetSize();float boxW=r-l,boxH=b-t;
    if(size.width<=0||size.height<=0||boxW<=0||boxH<=0)return FALSE;
    float sourceW=size.width,sourceH=size.height;
    float imageAspect=size.width/size.height,boxAspect=boxW/boxH;
    if(imageAspect>boxAspect)sourceW=size.height*boxAspect;else sourceH=size.width/boxAspect;
    float sx=(size.width-sourceW)*0.5f,sy=(size.height-sourceH)*0.5f;
    D2D1_RECT_F source=D2D1::RectF(sx,sy,sx+sourceW,sy+sourceH);
    target->DrawBitmap(entry->bitmap,D2D1::RectF(l,t,r,b),1.0f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,&source);
    return TRUE;
}

extern "C" BOOL ABM_D2D_DrawBgraFrame(ABM_D2D *ui, const void *pixels,
    UINT width, UINT height, float l, float t, float r, float b) {
    if (!ui || !pixels || !width || !height || !EnsureTarget(ui)) return FALSE;
    ID2D1Bitmap *bitmap = static_cast<ID2D1Bitmap *>(ui->frameImage);
    if (!bitmap || ui->frameImageWidth != width || ui->frameImageHeight != height) {
        SafeRelease(&bitmap);
        ui->frameImage = nullptr;
        ui->frameImageWidth = ui->frameImageHeight = 0;
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        HRESULT hr = Target(ui)->CreateBitmap(D2D1::SizeU(width, height),
            nullptr, 0, &props, &bitmap);
        if (FAILED(hr)) {
            wchar_t line[160]; StringCchPrintfW(line, _countof(line),
                L"player: Direct2D CreateBitmap failed: 0x%08lX", (unsigned long)hr);
            LogProjectMFrameDiag(line);
            return FALSE;
        }
        ui->frameImage = bitmap;
        ui->frameImageWidth = width;
        ui->frameImageHeight = height;
    }
    /* OpenGL readback is bottom-up; Direct2D bitmap memory is top-down. Flip
       rows into a reusable scratch buffer so the MilkDrop image is upright. */
    const size_t stride = (size_t)width * 4u;
    const size_t bytes = stride * (size_t)height;
    if (g_bgraFrameUploadScratch.size() < bytes) g_bgraFrameUploadScratch.resize(bytes);
    const BYTE *source = static_cast<const BYTE *>(pixels);
    for (UINT y = 0; y < height; ++y) {
        BYTE *destinationRow = g_bgraFrameUploadScratch.data() + (size_t)y * stride;
        memcpy(destinationRow, source + (size_t)(height - 1u - y) * stride, stride);
        /* MilkDrop output is opaque. Force alpha to 255 for the premultiplied
           Direct2D target so driver-specific GL alpha values cannot hide it. */
        for (UINT x = 0; x < width; ++x) destinationRow[(size_t)x * 4u + 3u] = 255;
    }
    HRESULT hr = bitmap->CopyFromMemory(nullptr, g_bgraFrameUploadScratch.data(), (UINT32)stride);
    if (FAILED(hr)) {
        wchar_t line[160]; StringCchPrintfW(line, _countof(line),
            L"player: Direct2D CopyFromMemory failed: 0x%08lX", (unsigned long)hr);
        LogProjectMFrameDiag(line);
        return FALSE;
    }
    LogProjectMFrameDiag(L"player: Direct2D accepted first ProjectM frame upload");
    Target(ui)->DrawBitmap(bitmap, D2D1::RectF(l, t, r, b), 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    return TRUE;
}

extern "C" void ABM_D2D_Destroy(ABM_D2D *ui) {
    if (!ui) return;
    SafeRelease(reinterpret_cast<ID2D1Bitmap **>(&ui->image));
    SafeRelease(reinterpret_cast<ID2D1Bitmap **>(&ui->frameImage));
    ui->frameImageWidth = ui->frameImageHeight = 0;
    SafeRelease(reinterpret_cast<IWICImagingFactory **>(&ui->wicFactory));
    SafeRelease(reinterpret_cast<ID2D1SolidColorBrush **>(&ui->brush));
    SafeRelease(reinterpret_cast<ID2D1HwndRenderTarget **>(&ui->target));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&ui->body));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&ui->smallFormat));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&ui->heading));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&ui->brandMain));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&ui->brandSuffix));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&ui->iconFormat));
    SafeRelease(reinterpret_cast<IDWriteFactory **>(&ui->writeFactory));
    SafeRelease(reinterpret_cast<ID2D1Factory **>(&ui->factory));
    ZeroMemory(ui, sizeof(*ui));
}

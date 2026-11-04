#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <string.h>
#include <algorithm>
#include "ui_d2d.h"

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
static IDWriteFactory *WriteFactory(ABM_D2D *ui) { return static_cast<IDWriteFactory *>(ui->writeFactory); }
static ID2D1SolidColorBrush *Brush(ABM_D2D *ui) { return static_cast<ID2D1SolidColorBrush *>(ui->brush); }

static void SetColor(ABM_D2D *ui, COLORREF color);

static BOOL EnsureTarget(ABM_D2D *ui) {
    if (ui->target) return TRUE;
    RECT rc{};
    GetClientRect(ui->hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hp =
        D2D1::HwndRenderTargetProperties(ui->hwnd,
            D2D1::SizeU(static_cast<UINT>(rc.right - rc.left),
                        static_cast<UINT>(rc.bottom - rc.top)));
    ID2D1HwndRenderTarget *target = nullptr;
    HRESULT hr = Factory(ui)->CreateHwndRenderTarget(rp, hp, &target);
    if (FAILED(hr)) return FALSE;
    ID2D1SolidColorBrush *brush = nullptr;
    hr = target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);
    if (FAILED(hr)) { target->Release(); return FALSE; }
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
    if (!MakeFormat(writeFactory, L"Segoe UI", 16.0f, DWRITE_FONT_WEIGHT_NORMAL, &body) ||
        !MakeFormat(writeFactory, L"Segoe UI", 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &smallFormat) ||
        !MakeFormat(writeFactory, L"Segoe UI", 23.0f, DWRITE_FONT_WEIGHT_BOLD, &heading)) {
        SafeRelease(&body); SafeRelease(&smallFormat); SafeRelease(&heading);
        ABM_D2D_Destroy(ui);
        return FALSE;
    }
    ui->body = body; ui->smallFormat = smallFormat; ui->heading = heading;
    if (!EnsureTarget(ui)) { ABM_D2D_Destroy(ui); return FALSE; }
    return TRUE;
}

extern "C" void ABM_D2D_Resize(ABM_D2D *ui, UINT width, UINT height) {
    if (!ui || !ui->target) return;
    D2D1_SIZE_U size = D2D1::SizeU(width, height);
    HRESULT hr = Target(ui)->Resize(&size);
    if (hr == D2DERR_RECREATE_TARGET) {
        SafeRelease(reinterpret_cast<ID2D1SolidColorBrush **>(&ui->brush));
        SafeRelease(reinterpret_cast<ID2D1HwndRenderTarget **>(&ui->target));
    }
}

extern "C" void ABM_D2D_Begin(ABM_D2D *ui, COLORREF clear) {
    if (!ui || !EnsureTarget(ui)) return;
    Target(ui)->BeginDraw();
    D2D1_COLOR_F color = ToD2D(clear);
    Target(ui)->Clear(&color);
    ui->clipActive = FALSE;
}

extern "C" void ABM_D2D_BeginClip(ABM_D2D *ui, COLORREF clear, const RECT *clip) {
    if (!ui || !clip || !EnsureTarget(ui)) return;
    Target(ui)->BeginDraw();
    D2D1_RECT_F bounds = D2D1::RectF(
        static_cast<float>(clip->left), static_cast<float>(clip->top),
        static_cast<float>(clip->right), static_cast<float>(clip->bottom));
    Target(ui)->PushAxisAlignedClip(bounds, D2D1_ANTIALIAS_MODE_ALIASED);
    ui->clipActive = TRUE;
    SetColor(ui, clear);
    Target(ui)->FillRectangle(bounds, Brush(ui));
}

extern "C" BOOL ABM_D2D_End(ABM_D2D *ui) {
    if (!ui || !ui->target) return FALSE;
    if (ui->clipActive) {
        Target(ui)->PopAxisAlignedClip();
        ui->clipActive = FALSE;
    }
    HRESULT hr = Target(ui)->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        SafeRelease(reinterpret_cast<ID2D1SolidColorBrush **>(&ui->brush));
        SafeRelease(reinterpret_cast<ID2D1HwndRenderTarget **>(&ui->target));
        return FALSE;
    }
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

extern "C" void ABM_D2D_Text(ABM_D2D *ui, const wchar_t *text, RECT rect,
                              COLORREF color, int style, UINT flags) {
    if (!ui || !EnsureTarget(ui) || !text || !text[0]) return;
    IDWriteTextFormat *format = static_cast<IDWriteTextFormat *>(
        style == 1 ? ui->smallFormat : (style == 2 ? ui->heading : ui->body));
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

extern "C" void ABM_D2D_Destroy(ABM_D2D *ui) {
    if (!ui) return;
    SafeRelease(reinterpret_cast<ID2D1Bitmap **>(&ui->image));
    SafeRelease(reinterpret_cast<IWICImagingFactory **>(&ui->wicFactory));
    SafeRelease(reinterpret_cast<ID2D1SolidColorBrush **>(&ui->brush));
    SafeRelease(reinterpret_cast<ID2D1HwndRenderTarget **>(&ui->target));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&ui->body));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&ui->smallFormat));
    SafeRelease(reinterpret_cast<IDWriteTextFormat **>(&ui->heading));
    SafeRelease(reinterpret_cast<IDWriteFactory **>(&ui->writeFactory));
    SafeRelease(reinterpret_cast<ID2D1Factory **>(&ui->factory));
    ZeroMemory(ui, sizeof(*ui));
}

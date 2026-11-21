#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include "ui_skin.h"

namespace {
static int g_skin = ABM_SKIN_CLASSIC_MUSIC_PLAYER;
static HWND g_registered[96] = {};

struct SkinPalette { COLORREF a,b,c,lineA,lineB; };
static SkinPalette Palette(int skin) {
    switch(skin) {
    case ABM_SKIN_OCEAN_FLOW: return {RGB(8,38,55),RGB(7,26,46),RGB(8,48,58),RGB(19,83,103),RGB(22,112,119)};
    case ABM_SKIN_AURORA: return {RGB(28,34,76),RGB(12,48,64),RGB(20,66,59),RGB(66,78,143),RGB(41,116,111)};
    case ABM_SKIN_GOLDEN_WAVES: return {RGB(66,43,18),RGB(48,27,16),RGB(77,54,20),RGB(119,83,33),RGB(130,97,43)};
    case ABM_SKIN_LIQUID_CHROME: return {RGB(38,51,66),RGB(25,34,48),RGB(51,62,75),RGB(91,111,130),RGB(105,125,143)};
    case ABM_SKIN_NEON_PULSE: return {RGB(48,18,63),RGB(12,39,69),RGB(13,56,55),RGB(92,44,120),RGB(31,102,111)};
    case ABM_SKIN_DEEP_OCEAN: return {RGB(7,33,48),RGB(4,17,36),RGB(7,43,51),RGB(14,63,84),RGB(17,85,93)};
    case ABM_SKIN_HOLOGRAPHIC: return {RGB(42,25,62),RGB(12,39,56),RGB(55,25,52),RGB(87,51,113),RGB(33,103,112)};
    default: return {RGB(8,38,55),RGB(7,26,46),RGB(8,48,58),RGB(19,83,103),RGB(22,112,119)};
    }
}
static float Phase(void) {
    return (float)(GetTickCount64()%60000ULL)*0.00135f;
}
static void BuildWave(float *xy,int count,float width,float height,float phase,float base,float amplitude,float speed) {
    if(!xy||count<2)return;
    for(int i=0;i<count;i++) {
        float t=(float)i/(float)(count-1);
        xy[i*2]=width*t;
        xy[i*2+1]=height*(base+amplitude*sinf(t*6.2831853f*1.65f+phase*speed)
            +amplitude*0.28f*sinf(t*6.2831853f*3.1f-phase*0.72f));
    }
}
}

extern "C" void ABM_SkinSet(int skin) {
    if(skin<0||skin>=ABM_SKIN_COUNT)skin=ABM_SKIN_CLASSIC_MUSIC_PLAYER;
    g_skin=skin;
}
extern "C" int ABM_SkinGet(void) { return g_skin; }
extern "C" const wchar_t *ABM_SkinName(int skin) {
    static const wchar_t *names[ABM_SKIN_COUNT]={
        L"Classic Music Player",L"Ocean Flow",L"Aurora",L"Golden Waves",
        L"Liquid Chrome",L"Neon Pulse",L"Deep Ocean",L"Holographic"
    };
    return skin>=0&&skin<ABM_SKIN_COUNT?names[skin]:names[0];
}
extern "C" BOOL ABM_SkinIsAnimated(void) { return g_skin!=ABM_SKIN_CLASSIC_MUSIC_PLAYER; }
extern "C" void ABM_SkinRegisterWindow(HWND hwnd) {
    if(!hwnd||!IsWindow(hwnd))return;
    for(HWND item:g_registered)if(item==hwnd)return;
    for(HWND &item:g_registered)if(!item||!IsWindow(item)){item=hwnd;return;}
}
extern "C" void ABM_SkinUnregisterWindow(HWND hwnd) {
    if(!hwnd)return;
    for(HWND &item:g_registered)if(item==hwnd)item=nullptr;
}
extern "C" void ABM_SkinInvalidateRegisteredWindows(void) {
    for(HWND &hwnd:g_registered) {
        if(hwnd&&!IsWindow(hwnd)){hwnd=nullptr;continue;}
        if(hwnd&&IsWindowVisible(hwnd))InvalidateRect(hwnd,nullptr,FALSE);
    }
}
extern "C" void ABM_SkinRenderD2D(ABM_D2D *renderer,const RECT *bounds) {
    if(!renderer||!bounds||!ABM_SkinIsAnimated())return;
    float w=(float)(bounds->right-bounds->left),h=(float)(bounds->bottom-bounds->top);
    if(w<=1.0f||h<=1.0f)return;
    SkinPalette p=Palette(g_skin);float phase=Phase(),drift=sinf(phase)*w*0.018f;
    float ox=(float)bounds->left,oy=(float)bounds->top;
    float r1=h*0.22f<110.0f?h*0.22f:110.0f,r2=h*0.24f<120.0f?h*0.24f:120.0f,r3=h*0.20f<100.0f?h*0.20f:100.0f;
    ABM_D2D_Round(renderer,ox+w*0.04f+drift,oy+h*0.20f,ox+w*0.43f+drift,oy+h*0.70f,r1,p.a);
    ABM_D2D_Round(renderer,ox+w*0.57f-drift,oy+h*0.14f,ox+w*0.96f-drift,oy+h*0.61f,r2,p.b);
    ABM_D2D_Round(renderer,ox+w*0.29f,oy+h*0.64f,ox+w*0.73f,oy+h*0.98f,r3,p.c);
    float points[64];BuildWave(points,32,w,h,phase,0.49f,0.055f,1.0f);
    for(int i=0;i<32;i++){points[i*2]+=ox;points[i*2+1]+=oy;}
    ABM_D2D_Polyline(renderer,points,32,p.lineA,2.0f);
    BuildWave(points,32,w,h,phase,0.56f,0.041f,0.72f);
    for(int i=0;i<32;i++){points[i*2]+=ox;points[i*2+1]+=oy;}
    ABM_D2D_Polyline(renderer,points,32,p.lineB,1.2f);
}
extern "C" void ABM_SkinRenderCanvas(ABM_D2D_CANVAS *canvas,const RECT *bounds) {
    if(!canvas||!bounds||!ABM_SkinIsAnimated())return;
    float w=(float)(bounds->right-bounds->left),h=(float)(bounds->bottom-bounds->top);
    if(w<=1.0f||h<=1.0f)return;
    SkinPalette p=Palette(g_skin);float phase=Phase(),drift=sinf(phase)*w*0.018f;
    float ox=(float)bounds->left,oy=(float)bounds->top;
    float r1=h*0.22f<110.0f?h*0.22f:110.0f,r2=h*0.24f<120.0f?h*0.24f:120.0f,r3=h*0.20f<100.0f?h*0.20f:100.0f;
    ABM_D2D_CanvasFillRound(canvas,ox+w*0.04f+drift,oy+h*0.20f,ox+w*0.43f+drift,oy+h*0.70f,r1,p.a);
    ABM_D2D_CanvasFillRound(canvas,ox+w*0.57f-drift,oy+h*0.14f,ox+w*0.96f-drift,oy+h*0.61f,r2,p.b);
    ABM_D2D_CanvasFillRound(canvas,ox+w*0.29f,oy+h*0.64f,ox+w*0.73f,oy+h*0.98f,r3,p.c);
    float points[64];BuildWave(points,32,w,h,phase,0.49f,0.055f,1.0f);
    for(int i=0;i<32;i++){points[i*2]+=ox;points[i*2+1]+=oy;}
    for(int i=1;i<32;i++)ABM_D2D_CanvasLine(canvas,points[(i-1)*2],points[(i-1)*2+1],points[i*2],points[i*2+1],p.lineA,2.0f);
    BuildWave(points,32,w,h,phase,0.56f,0.041f,0.72f);
    for(int i=0;i<32;i++){points[i*2]+=ox;points[i*2+1]+=oy;}
    for(int i=1;i<32;i++)ABM_D2D_CanvasLine(canvas,points[(i-1)*2],points[(i-1)*2+1],points[i*2],points[i*2+1],p.lineB,1.2f);
}

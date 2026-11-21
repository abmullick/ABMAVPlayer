#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include "ui_skin.h"

namespace {
static int g_skin = ABM_SKIN_CLASSIC_MUSIC_PLAYER;
static HWND g_registered[96] = {};

static float Phase(void) {
    return (float)(GetTickCount64()%60000ULL)*0.00115f;
}
static COLORREF C(BYTE r,BYTE g,BYTE b) { return RGB(r,g,b); }

/* Keep one composition engine for both HWND and owner-draw surfaces. */
static void Fill(ABM_D2D *r,float l,float t,float rr,float b,COLORREF c){ABM_D2D_Fill(r,l,t,rr,b,c);}
static void Fill(ABM_D2D_CANVAS *r,float l,float t,float rr,float b,COLORREF c){ABM_D2D_CanvasFill(r,l,t,rr,b,c);}
static void Round(ABM_D2D *r,float l,float t,float rr,float b,float rad,COLORREF c){ABM_D2D_Round(r,l,t,rr,b,rad,c);}
static void Round(ABM_D2D_CANVAS *r,float l,float t,float rr,float b,float rad,COLORREF c){ABM_D2D_CanvasFillRound(r,l,t,rr,b,rad,c);}
static void Line(ABM_D2D *r,float x,float y,float xx,float yy,COLORREF c,float w){ABM_D2D_Line(r,x,y,xx,yy,c,w);}
static void Line(ABM_D2D_CANVAS *r,float x,float y,float xx,float yy,COLORREF c,float w){ABM_D2D_CanvasLine(r,x,y,xx,yy,c,w);}

template<class R> static void Curve(R *r,float ox,float oy,float w,float h,float phase,
    float base,float amp,float cycles,float speed,COLORREF color,float thickness) {
    float px=ox,py=oy+h*(base+amp*sinf(phase*speed));
    for(int i=1;i<=72;i++){
        float t=(float)i/72.0f;
        float x=ox+w*t;
        float y=oy+h*(base+amp*sinf(t*6.2831853f*cycles+phase*speed)
            +amp*.22f*sinf(t*6.2831853f*(cycles*1.83f)-phase*.7f));
        Line(r,px,py,x,y,color,thickness);px=x;py=y;
    }
}
template<class R> static void DrawComposition(R *r,const RECT *bounds,int skin) {
    const float w=(float)(bounds->right-bounds->left),h=(float)(bounds->bottom-bounds->top);
    const float ox=(float)bounds->left,oy=(float)bounds->top,ph=Phase();
    if(w<2||h<2)return;
    switch(skin) {
    case ABM_SKIN_OCEAN_FLOW: {
        /* Long, overlapping tidal currents rather than floating blobs. */
        for(int i=0;i<7;i++){
            float base=.22f+i*.105f;
            Curve(r,ox,oy,w,h,ph+i*.62f,base,.035f+i*.002f,1.25f+i*.08f,.55f+i*.07f,
                i%2?C(13,91,112):C(18,132,143),i==0?2.4f:1.25f);
        }
        for(int i=0;i<5;i++){
            float x=ox+w*(.12f+i*.19f)+sinf(ph*.4f+i)*w*.018f;
            Round(r,x,oy+h*(.18f+(i%3)*.2f),x+5,oy+h*(.18f+(i%3)*.2f)+5,4,C(57,166,174));
        }
        break;
    }
    case ABM_SKIN_AURORA: {
        /* Tall, independently moving curtains with fine luminous folds. */
        for(int curtain=0;curtain<6;curtain++){
            float cx=ox+w*(.08f+curtain*.17f)+sinf(ph*.32f+curtain)*w*.025f;
            float top=oy+h*(.04f+(curtain%2)*.12f);
            float bottom=oy+h*(.88f-(curtain%3)*.08f);
            for(int strand=0;strand<5;strand++){
                float lastX=cx+strand*7,lastY=top;
                for(int j=1;j<=30;j++){
                    float t=(float)j/30.0f;
                    float x=cx+strand*7+sinf(t*4.8f+ph*.7f+curtain*.8f+strand*.24f)*w*.035f*t;
                    float y=top+(bottom-top)*t;
                    Line(r,lastX,lastY,x,y,strand%2?C(49,177,151):C(105,119,220),strand==0?2.0f:1.0f);
                    lastX=x;lastY=y;
                }
            }
        }
        break;
    }
    case ABM_SKIN_GOLDEN_WAVES: {
        /* Fine metallic orbital arcs and drifting gold glints. */
        float cx=ox+w*.52f,cy=oy+h*.55f;
        for(int ring=0;ring<9;ring++){
            float rx=w*(.12f+ring*.055f),ry=h*(.10f+ring*.047f);
            float start=-2.55f+ring*.16f,end=1.35f+ring*.13f;
            float px=cx+cosf(start)*rx,py=cy+sinf(start)*ry;
            for(int j=1;j<=64;j++){
                float a=start+(end-start)*(float)j/64.0f;
                float x=cx+cosf(a)*rx,y=cy+sinf(a)*ry;
                Line(r,px,py,x,y,ring%3?C(145,101,42):C(220,174,82),ring%3?1.0f:2.0f);
                px=x;py=y;
            }
        }
        for(int i=0;i<24;i++){
            float x=ox+w*(.06f+fmodf(i*.618f,1.0f)*.88f);
            float y=oy+h*(.12f+fmodf(i*.371f+ph*.025f,1.0f)*.76f);
            Round(r,x,y,x+2.5f,y+2.5f,2,C(238,194,112));
        }
        break;
    }
    case ABM_SKIN_LIQUID_CHROME: {
        /* Broad, slanted reflective ribbons with narrow specular highlights. */
        for(int band=0;band<8;band++){
            float y=oy+h*(.12f+band*.115f)+sinf(ph*.35f+band*.7f)*h*.025f;
            float slope=(band%2?1.0f:-1.0f)*h*.13f;
            for(int k=0;k<4;k++){
                float yy=y+k*5.0f;
                Line(r,ox-w*.05f,yy,ox+w*.48f,yy+slope*.55f,C(69+k*18,86+k*17,104+k*17),k==0?3.0f:1.0f);
                Line(r,ox+w*.48f,yy+slope*.55f,ox+w*1.05f,yy+slope,C(116+k*16,137+k*15,157+k*14),k==0?2.0f:1.0f);
            }
        }
        break;
    }
    case ABM_SKIN_NEON_PULSE: {
        /* Perspective wire grid and a bright, energetic signal trace. */
        float horizon=oy+h*.42f;
        for(int i=0;i<=16;i++){
            float x=ox+w*i/16.0f;
            Line(r,x,horizon,ox+w*.5f+(x-ox-w*.5f)*2.2f,oy+h*1.03f,C(59,37,116),.8f);
        }
        for(int j=0;j<12;j++){
            float t=(float)j/11.0f;
            float y=horizon+(h*.59f)*t*t;
            Line(r,ox, y, ox+w,y,C(49,54,119),.8f);
        }
        Curve(r,ox,oy,w,h,ph*1.7f,.40f,.12f,2.4f,1.0f,C(191,63,245),2.5f);
        Curve(r,ox,oy,w,h,ph*1.25f,.43f,.07f,3.1f,-.8f,C(37,218,220),1.5f);
        for(int i=0;i<9;i++){
            float x=ox+w*(.08f+i*.105f),y=oy+h*(.39f+.1f*sinf(i*.9f+ph*1.7f));
            Round(r,x-3,y-3,x+3,y+3,3,C(222,94,255));
        }
        break;
    }
    case ABM_SKIN_DEEP_OCEAN: {
        /* Submerged light shafts, distant particulate and rising bubbles. */
        float source=ox+w*.58f;
        for(int ray=0;ray<13;ray++){
            float spread=(ray-6)*w*.085f;
            float sway=sinf(ph*.28f+ray*.6f)*w*.018f;
            Line(r,source+sway,oy-h*.05f,source+spread+sway,oy+h*(.78f+(ray%4)*.045f),
                ray%3?C(13,69,91):C(26,111,132),ray%4==0?3.0f:1.0f);
        }
        for(int i=0;i<34;i++){
            float x=ox+w*fmodf(i*.618f+.13f,1.0f);
            float rise=fmodf(i*.293f+ph*.035f,1.0f);
            float y=oy+h*(1.0f-rise);
            float radius=2.0f+(i%4)*1.4f;
            Round(r,x-radius,y-radius,x+radius,y+radius,radius,C(25,105,128));
        }
        break;
    }
    case ABM_SKIN_HOLOGRAPHIC: {
        /* Interlaced spectral mesh and shifting elliptical interference bands. */
        for(int i=0;i<17;i++){
            float t=(float)i/16.0f;
            Curve(r,ox,oy,w,h,ph*.55f+i*.19f,.12f+t*.76f,.045f,1.0f+t*1.8f,
                .6f,C(91+(i%3)*25,65+(i%4)*20,166+(i%3)*24),.85f);
        }
        for(int i=0;i<13;i++){
            float x=ox+w*(.08f+i*.07f);
            float lastX=x,lastY=oy;
            for(int j=1;j<=32;j++){
                float t=(float)j/32.0f;
                float y=oy+h*t;
                float xx=x+sinf(t*6.2831853f+ph*.65f+i*.32f)*w*.07f;
                Line(r,lastX,lastY,xx,y,i%2?C(35,166,177):C(174,81,180),.8f);
                lastX=xx;lastY=y;
            }
        }
        break;
    }
    default: break;
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
    if(!renderer||!bounds||g_skin==ABM_SKIN_CLASSIC_MUSIC_PLAYER)return;
    DrawComposition(renderer,bounds,g_skin);
}
extern "C" void ABM_SkinRenderCanvas(ABM_D2D_CANVAS *canvas,const RECT *bounds) {
    if(!canvas||!bounds||g_skin==ABM_SKIN_CLASSIC_MUSIC_PLAYER)return;
    DrawComposition(canvas,bounds,g_skin);
}

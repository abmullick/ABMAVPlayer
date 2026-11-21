#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include "ui_skin.h"
namespace {
static int g_skin=ABM_SKIN_CLASSIC_MUSIC_PLAYER;
static HWND g_registered[96]={};
static float Phase(){return (float)(GetTickCount64()%90000ULL)*.0007f;}
static COLORREF C(BYTE r,BYTE g,BYTE b){return RGB(r,g,b);}
static COLORREF Mix(COLORREF a,COLORREF b,float t){
 if(t<0)t=0;if(t>1)t=1;
 return RGB((BYTE)(GetRValue(a)+(GetRValue(b)-GetRValue(a))*t),(BYTE)(GetGValue(a)+(GetGValue(b)-GetGValue(a))*t),(BYTE)(GetBValue(a)+(GetBValue(b)-GetBValue(a))*t));
}
static void F(ABM_D2D*r,float l,float t,float rr,float b,COLORREF c){ABM_D2D_Fill(r,l,t,rr,b,c);}
static void F(ABM_D2D_CANVAS*r,float l,float t,float rr,float b,COLORREF c){ABM_D2D_CanvasFill(r,l,t,rr,b,c);}
static void R(ABM_D2D*r,float l,float t,float rr,float b,float q,COLORREF c){ABM_D2D_Round(r,l,t,rr,b,q,c);}
static void R(ABM_D2D_CANVAS*r,float l,float t,float rr,float b,float q,COLORREF c){ABM_D2D_CanvasFillRound(r,l,t,rr,b,q,c);}
static void L(ABM_D2D*r,float x,float y,float xx,float yy,COLORREF c,float w){ABM_D2D_Line(r,x,y,xx,yy,c,w);}
static void L(ABM_D2D_CANVAS*r,float x,float y,float xx,float yy,COLORREF c,float w){ABM_D2D_CanvasLine(r,x,y,xx,yy,c,w);}
template<class T>static void Grad(T*r,float l,float t,float rr,float b,COLORREF a,COLORREF z,BOOL vert){
 for(int i=0;i<36;i++){float p=(float)i/35.0f;if(vert)F(r,l,t+(b-t)*i/36.0f,rr,t+(b-t)*(i+1)/36.0f+1,Mix(a,z,p));else F(r,l+(rr-l)*i/36.0f,t,l+(rr-l)*(i+1)/36.0f+1,b,Mix(a,z,p));}
}
template<class T>static void Curve(T*r,float x,float y,float w,float h,float phase,float base,float amp,float cycles,COLORREF c,float thick){
 float px=x,py=y+h*(base+amp*sinf(phase));
 for(int i=1;i<=64;i++){float q=(float)i/64,nx=x+w*q,ny=y+h*(base+amp*sinf(q*6.2831853f*cycles+phase));L(r,px,py,nx,ny,c,thick);px=nx;py=ny;}
}
template<class T>static void Arc(T*r,float cx,float cy,float rx,float ry,float a0,float a1,COLORREF c,float thick){
 float px=cx+cosf(a0)*rx,py=cy+sinf(a0)*ry;
 for(int i=1;i<=56;i++){float a=a0+(a1-a0)*i/56.0f,nx=cx+cosf(a)*rx,ny=cy+sinf(a)*ry;L(r,px,py,nx,ny,c,thick);px=nx;py=ny;}
}
template<class T>static void Draw(T*r,const RECT*b,int skin){
 float w=(float)(b->right-b->left),h=(float)(b->bottom-b->top),x=(float)b->left,y=(float)b->top;
 if(w<2||h<2||skin==ABM_SKIN_CLASSIC_MUSIC_PLAYER)return;
 float p=ABM_SkinIsAnimated()?Phase():0.0f;
 switch(skin){
 case ABM_SKIN_AURORA_FLOW:
  Grad(r,x+w*.02f,y+h*.03f,x+w*.98f,y+h*.97f,C(7,24,39),C(13,40,49),TRUE);
  for(int k=0;k<6;k++){float cx=x+w*(.08f+k*.17f)+sinf(p*.48f+k)*w*.025f;
   for(int j=0;j<5;j++){float xx=cx+j*5.0f,top=y+h*.06f,bottom=y+h*.91f;
    for(int n=1;n<=26;n++){float t=n/26.0f,xx2=xx+sinf(t*4.2f+p*.65f+k*.7f+j*.2f)*w*.035f*t,yy=top+(bottom-top)*t; if(n>1)L(r,xx,top+(bottom-top)*(n-1)/26.0f,xx2,yy,Mix(C(20,103,119),C(87,179,162),.2f+(j%3)*.2f),j==0?2.0f:1.0f);xx=xx2;}
   }}
  for(int i=0;i<3;i++)Curve(r,x,y,w,h,p*.4f+i,.78f+i*.04f,.012f,1.2f,C(26,103,116),.9f);
  break;
 case ABM_SKIN_LIQUID_GLASS:
  Grad(r,x,y,x+w,y+h,C(11,24,42),C(17,42,58),TRUE);
  for(int i=0;i<4;i++){float ww=w*(.34f+i*.09f),hh=h*(.27f+i*.12f),cx=x+w*.53f+sinf(p*.34f+i*1.5f)*w*.08f,cy=y+h*.51f+cosf(p*.29f+i)*h*.06f,l=cx-ww*.5f,t=cy-hh*.5f;
   R(r,l,t,l+ww,t+hh,28.0f+(float)i*5.0f,Mix(C(17,48,67),C(31,76,94),(float)i*0.13f));
   L(r,l+24,t+5,l+ww-25,t+5,C(91,161,183),1.1f);L(r,l+20,t+hh-7,l+ww-35,t+hh-7,C(36,103,127),.8f);
   Arc(r,cx,cy,ww*.46f,hh*.43f,-2.5f,-.25f,C(79,150,174),1.0f);
  }break;
 case ABM_SKIN_COSMIC_DRIFT:
  Grad(r,x,y,x+w,y+h,C(6,11,29),C(19,24,57),TRUE);
  for(int i=0;i<6;i++){float cx=x+w*(.12f+i*.15f)+sinf(p*.18f+i)*w*.025f,cy=y+h*(.22f+(i%3)*.23f)+cosf(p*.16f+i)*h*.03f,rx=w*(.13f+(i%2)*.04f),ry=h*.17f;Grad(r,cx-rx,cy-ry,cx+rx,cy+ry,i%2?C(29,48,102):C(22,66,91),C(12,20,49),TRUE);}
  for(int i=0;i<58;i++){float q=(float)i,sx=x+w*fmodf(q*.618f,1),sy=y+h*fmodf(q*.382f+p*.018f,1),sz=1+(i%3)*.45f;R(r,sx,sy,sx+sz,sy+sz,sz,C(112+i%4*17,146+i%3*20,199+i%2*25));}
  Arc(r,x+w*.69f,y+h*.48f,w*.23f,h*.31f,-2.1f,1.4f,C(75,105,177),1.1f);break;
 case ABM_SKIN_PRISM_WAVE:
  Grad(r,x,y,x+w,y+h,C(12,17,41),C(27,25,58),TRUE);
  for(int i=0;i<9;i++){COLORREF colors[]={C(46,99,155),C(92,69,157),C(38,132,146),C(125,73,145)};Curve(r,x,y,w,h,p*.8f+i*.46f,.22f+i*.07f,.085f,1.2f+(i%3)*.35f,colors[i%4],i%3?1.0f:2.0f);}
  for(int i=0;i<5;i++){float yy=y+h*(.18f+i*.14f)+sinf(p*.5f+i)*h*.02f;Grad(r,x+w*.08f,yy,x+w*.92f,yy+2,C(36,106,155),C(122,79,169),FALSE);}break;
 case ABM_SKIN_GLASS_FACETS:
  Grad(r,x,y,x+w,y+h,C(12,28,43),C(18,43,57),TRUE);
  L(r,x+w*.08f,y+h*.82f,x+w*.48f,y+h*.12f,C(47,111,131),1.2f);L(r,x+w*.48f,y+h*.12f,x+w*.91f,y+h*.72f,C(64,127,145),1.2f);L(r,x+w*.08f,y+h*.82f,x+w*.91f,y+h*.72f,C(34,83,103),1);
  L(r,x+w*.24f,y+h*.78f,x+w*.58f,y+h*.26f,C(43,96,115),.8f);L(r,x+w*.58f,y+h*.26f,x+w*.84f,y+h*.86f,C(49,108,127),.8f);break;
 case ABM_SKIN_CARBON_WEAVE:
  Grad(r,x,y,x+w,y+h,C(12,16,23),C(24,32,41),TRUE);
  for(int i=-30;i<(int)(w+h);i+=18){L(r,x+i,y,x+i-h,y+h,C(38,50,61),.7f);L(r,x+i+7,y,x+i-h+7,y+h,C(25,36,46),.7f);}
  L(r,x+w*.04f,y+h*.16f,x+w*.96f,y+h*.16f,C(65,85,100),1);L(r,x+w*.04f,y+h*.84f,x+w*.96f,y+h*.84f,C(48,67,82),1);break;
 case ABM_SKIN_GOLD_GEOMETRY:
  Grad(r,x,y,x+w,y+h,C(23,20,21),C(40,32,26),TRUE);
  Arc(r,x+w*.55f,y+h*.53f,w*.34f,h*.38f,-2.5f,1.2f,C(155,115,60),1.4f);Arc(r,x+w*.55f,y+h*.53f,w*.26f,h*.30f,-2.5f,1.2f,C(111,84,49),.9f);Arc(r,x+w*.55f,y+h*.53f,w*.19f,h*.22f,-2.5f,1.2f,C(188,145,78),.8f);L(r,x+w*.12f,y+h*.78f,x+w*.82f,y+h*.18f,C(125,94,55),.8f);break;
 case ABM_SKIN_MINIMAL_HORIZON:
  Grad(r,x,y,x+w,y+h,C(14,22,33),C(23,36,50),TRUE);float hy=y+h*.67f;L(r,x+w*.06f,hy,x+w*.94f,hy,C(68,104,123),1);
  for(int i=1;i<=8;i++){float yy=hy+i*h*.035f;L(r,x+w*.18f-i*w*.012f,yy,x+w*.82f+i*w*.012f,yy,C(39,66,84),.8f);}Arc(r,x+w*.5f,hy,w*.15f,h*.21f,3.14159f,6.28318f,C(88,132,151),1.1f);break;
 }
}
}
extern "C" void ABM_SkinSet(int s){g_skin=(s<0||s>=ABM_SKIN_COUNT)?ABM_SKIN_CLASSIC_MUSIC_PLAYER:s;}
extern "C" int ABM_SkinGet(void){return g_skin;}
extern "C" const wchar_t*ABM_SkinName(int s){static const wchar_t*n[]={L"Classic Music Player",L"Aurora Flow",L"Liquid Glass",L"Cosmic Drift",L"Prism Wave",L"Glass Facets",L"Carbon Weave",L"Gold Geometry",L"Minimal Horizon"};return s>=0&&s<ABM_SKIN_COUNT?n[s]:n[0];}
extern "C" BOOL ABM_SkinIsAnimated(void){return g_skin>=ABM_SKIN_AURORA_FLOW&&g_skin<=ABM_SKIN_PRISM_WAVE;}
extern "C" void ABM_SkinRegisterWindow(HWND h){if(!h||!IsWindow(h))return;for(HWND q:g_registered)if(q==h)return;for(HWND&q:g_registered)if(!q||!IsWindow(q)){q=h;return;}}
extern "C" void ABM_SkinUnregisterWindow(HWND h){for(HWND&q:g_registered)if(q==h)q=nullptr;}
extern "C" void ABM_SkinInvalidateRegisteredWindows(void){for(HWND&q:g_registered){if(q&&!IsWindow(q)){q=nullptr;continue;}if(q&&IsWindowVisible(q))InvalidateRect(q,nullptr,FALSE);}}
extern "C" void ABM_SkinRenderD2D(ABM_D2D*r,const RECT*b){if(r&&b&&g_skin!=ABM_SKIN_CLASSIC_MUSIC_PLAYER)Draw(r,b,g_skin);}
extern "C" void ABM_SkinRenderCanvas(ABM_D2D_CANVAS*r,const RECT*b){if(r&&b&&g_skin!=ABM_SKIN_CLASSIC_MUSIC_PLAYER)Draw(r,b,g_skin);}

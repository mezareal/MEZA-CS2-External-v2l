#pragma once
// Native Windows GDI+ visual components. No web view and no third-party UI package.
#include <Windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <cmath>
#include <string>
#include <objidl.h>
#include <cstring>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace glassui {
using namespace Gdiplus;
struct runtime {
    static inline ULONG_PTR s_token = 0;
    static inline int s_ref_count = 0;
    static inline std::mutex s_mutex;

    runtime() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_ref_count++ == 0) {
            GdiplusStartupInput input;
            GdiplusStartup(&s_token, &input, nullptr);
        }
    }
    ~runtime() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (--s_ref_count == 0 && s_token) {
            GdiplusShutdown(s_token);
            s_token = 0;
        }
    }
    runtime(const runtime&) = delete; runtime& operator=(const runtime&) = delete;
};
inline Color c(int r,int g,int b,int a=255) {
    // MEZA: midnight glass, neon pink and a restrained cyan confirmation state.
    if (r == 255 && g == 255 && b == 255 && a >= 250)
        return Color((BYTE)a, 250, 248, 252); // chrome white
    if ((b > g + 28 && b > r + 22 && r >= 95) || (r > 160 && g < 100 && b > 200))
        return Color((BYTE)a, 255, 42, 167);
    if (r >= 190 && g >= 184 && b >= 185) // pale window/card backgrounds -> dark obsidian
        return Color((BYTE)a, (BYTE)(12+(255-r)/10), (BYTE)(10+(255-g)/10), (BYTE)(12+(255-b)/10));
    if (r > g + 55 && r > b + 48 && g < 135) // keep errors clearly distinct
        return Color((BYTE)a, 255, 101, 173);
    if (g > r + 60 && g > b + 21) // successful state
        return Color((BYTE)a, 54, 224, 191);
    if (r < 170 && g < 175 && b < 195) { // dark label ink -> warm light silver label ink
        const int high = (r < 115) ? 230 : 180;
        return Color((BYTE)a, (BYTE)(high), (BYTE)(high-5), (BYTE)(high-5));
    }
    return Color((BYTE)a,(BYTE)r,(BYTE)g,(BYTE)b);
}
inline std::unique_ptr<GraphicsPath> rounded(float x,float y,float w,float h,float r) {
    auto p=std::make_unique<GraphicsPath>();
    r=(std::min)(r,(std::min)(w,h)*0.5f);
    float d=2.f*r;
    p->AddArc(x,y,d,d,180,90);p->AddArc(x+w-d,y,d,d,270,90);
    p->AddArc(x+w-d,y+h-d,d,d,0,90);p->AddArc(x,y+h-d,d,d,90,90);
    p->CloseFigure();return p;
}
inline void fill(Graphics& g,float x,float y,float w,float h,float r,Color color) {
    auto p=rounded(x,y,w,h,r);SolidBrush b(color);g.FillPath(&b,p.get());
}
inline void stroke(Graphics& g,float x,float y,float w,float h,float r,Color color,float line=1.f) {
    auto p=rounded(x+.5f,y+.5f,w-1.f,h-1.f,r);Pen pen(color,line);g.DrawPath(&pen,p.get());
}
inline void grad(Graphics& g,float x,float y,float w,float h,float r,Color a,Color b) {
    auto p=rounded(x,y,w,h,r);
    LinearGradientBrush brush(PointF(x,y),PointF(x+w,y+h),a,b);
    g.FillPath(&brush,p.get());
}
inline void label(Graphics& g,const wchar_t* s,float x,float y,float w,float h,float pts,Color color,
                  INT weight=FontStyleRegular,StringAlignment align=StringAlignmentNear) {
    FontFamily ff(L"Segoe UI");Font f(&ff,(std::max)(11.5f,pts),weight,UnitPixel);SolidBrush brush(color);
    StringFormat fmt;fmt.SetAlignment(align);fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    g.DrawString(s,-1,&f,RectF(x,y,w,h),&fmt,&brush);
}
inline void setup(Graphics& g) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    // ClearType improves small native Windows text on our opaque double-buffered backplate.
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetCompositingQuality(CompositingQualityHighQuality);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
}
// Native MEZA midnight-glass palette, no heavyweight shader or runtime blur.
inline void surface(Graphics& g,int w,int h,bool sidebar=true) {
    auto raw=[](int r,int gg,int b,int a=255){return Color((BYTE)a,(BYTE)r,(BYTE)gg,(BYTE)b);};
    grad(g,0,0,(float)w,(float)h,18,raw(9,10,20,253),raw(20,13,29,252));
    { SolidBrush glow(raw(255,42,167,30));g.FillEllipse(&glow,RectF((float)w*.56f,-90.f,350.f,160.f)); }
    { SolidBrush glow(raw(95,62,255,22));g.FillEllipse(&glow,RectF(-130.f,(float)h*.55f,330.f,280.f)); }
    if(sidebar) {
      grad(g,1,1,255,(float)h-2,17,raw(12,11,25,248),raw(26,15,37,244));
      fill(g,254,18,1,(float)h-36,0,raw(255,42,167,145));
    }
    stroke(g,1,1,(float)w-2,(float)h-2,17,raw(255,42,167,155),1.f);
}
// Cached embedded logo. Lifetime of the IStream exceeds the Bitmap lifetime.
// This is actual Windows GDI+ rendering of the same PNG embedded for DX9.
struct brand_bitmap_cache {
    IStream* stream = nullptr;
    std::unique_ptr<Bitmap> bitmap;
    explicit brand_bitmap_cache(int resource_id = 103) {
        load(resource_id);
    }
    void reset() {
        bitmap.reset();
        if (stream) { stream->Release(); stream = nullptr; }
    }
    void load(int resource_id) {
        reset();
        const HRSRC res = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resource_id), MAKEINTRESOURCEW(10));
        if (!res) return;
        const DWORD bytes = SizeofResource(GetModuleHandleW(nullptr), res);
        const HGLOBAL loaded = LoadResource(GetModuleHandleW(nullptr), res);
        const void* data = loaded ? LockResource(loaded) : nullptr;
        if (!data || !bytes) return;
        const HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!copy) return;
        void* dest = GlobalLock(copy);
        if (!dest) { GlobalFree(copy); return; }
        std::memcpy(dest, data, bytes);
        GlobalUnlock(copy);
        if (FAILED(CreateStreamOnHGlobal(copy, TRUE, &stream))) {
            GlobalFree(copy); return;
        }
        bitmap.reset(Bitmap::FromStream(stream));
        if (!bitmap || bitmap->GetLastStatus() != Ok) {
            reset();
        }
    }
    ~brand_bitmap_cache() {
        reset();
    }
};
inline bool cs2_banner(Graphics& g, float x, float y, float w, float h) {
    static brand_bitmap_cache* picture = new brand_bitmap_cache(120);
    if (!picture->bitmap || picture->bitmap->GetLastStatus() != Ok) {
        picture->load(120);
    }
    if (!picture->bitmap) return false;
    g.DrawImage(picture->bitmap.get(), RectF(x,y,w,h));
    return true;
}
inline void logo(Graphics& g,float x,float y,float sz=52) {
    static brand_bitmap_cache* branding = new brand_bitmap_cache(121);
    if (!branding->bitmap || branding->bitmap->GetLastStatus() != Ok) {
        branding->load(121);
    }
    if (branding->bitmap) {
        g.DrawImage(branding->bitmap.get(), RectF(x,y,sz,sz));
        return;
    }

    grad(g,x,y,sz,sz,sz*.23f,c(193,169,255),c(134,103,231));
    stroke(g,x,y,sz,sz,sz*.23f,c(255,255,255,138),1.f);
    Pen pen(c(255,255,255),3.f);pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);
    const float u=sz/52.f;
    g.DrawLine(&pen,PointF(x+16*u,y+35*u),PointF(x+16*u,y+17*u));
    g.DrawLine(&pen,PointF(x+16*u,y+17*u),PointF(x+36*u,y+35*u));
    g.DrawLine(&pen,PointF(x+36*u,y+35*u),PointF(x+36*u,y+17*u));
}
inline void meza_logo(Graphics& g,float x,float y,float sz=52) {
    static brand_bitmap_cache* branding = new brand_bitmap_cache(122);
    if (!branding->bitmap || branding->bitmap->GetLastStatus() != Ok) {
        branding->load(122);
    }
    if (branding->bitmap) {
        g.DrawImage(branding->bitmap.get(), RectF(x,y,sz,sz));
        return;
    }
    logo(g, x, y, sz);
}
inline void nav(Graphics& g,int stage,int h) {
    logo(g,27,27,48);
    label(g,L"TRAY",91,27,145,27,18,c(225,248,252),FontStyleBold);
    label(g,L"COMPACT  SERIES",92,55,145,20,11,c(132,124,168),FontStyleBold);
    fill(g,23,99,205,1,0,c(212,206,235,170));
    label(g,L"ACCESS FLOW",27,118,205,25,11,c(117,111,151),FontStyleBold);
    const wchar_t* names[3]={L"01   LICENSE",L"02   VERIFY",L"03   READY"};
    for(int i=0;i<3;i++){
        float y=160.f+i*58.f;
        if(stage==i){
            fill(g,18,y,219,45,11,c(224,213,255));
            stroke(g,18,y,219,45,11,c(174,151,246),1.f);
            fill(g,19,y+11,3,23,2,c(150,114,244));
            label(g,names[i],42,y,176,45,13,c(76,55,144),FontStyleBold);
            fill(g,219,y+19,6,6,3,c(153,109,246));
        } else {
            fill(g,19,y,217,45,11,c(250,249,255,170));
            label(g,names[i],42,y,176,45,13,c(132,124,160),FontStyleBold);
        }
    }
    fill(g,25,(float)h-115,205,1,0,c(214,207,235,150));
    label(g,L"SECURE SESSION",27,(float)h-99,180,22,11,c(115,110,146),FontStyleBold);
    fill(g,23,(float)h-67,212,42,10,c(250,249,255,228));
    stroke(g,23,(float)h-67,212,42,10,c(207,199,232),1.f);
    fill(g,40,(float)h-50,8,8,4,stage==2?c(61,183,132):c(153,112,244));
    label(g,stage==2?L"Connection ready":L"Encrypted connection",59,(float)h-67,170,42,12,c(68,58,105),FontStyleBold);
}
// Uniform dark graphite and restrained TRAY-red accents across all Win32 views.
inline void panel(Graphics& g,float x,float y,float w,float h) {
    auto raw=[](int r,int gg,int b,int a=255){return Color((BYTE)a,(BYTE)r,(BYTE)gg,(BYTE)b);};
    grad(g,x,y,w,h,12,raw(18,16,20,250),raw(28,20,24,250));
    stroke(g,x,y,w,h,12,raw(92,54,64,190),1.0f);
}
inline void button(Graphics& g,float x,float y,float w,float h,const wchar_t* title,bool accent,bool hot=false) {
    auto raw=[](int r,int gg,int b,int a=255){return Color((BYTE)a,(BYTE)r,(BYTE)gg,(BYTE)b);};
    if(accent) {
        grad(g,x,y,w,h,9,hot?raw(255,92,111):raw(229,49,72),hot?raw(234,48,73):raw(184,31,51));
        stroke(g,x,y,w,h,9,raw(255,132,145,220),1.f);
    } else {
        fill(g,x,y,w,h,9,hot?raw(48,35,41):raw(31,26,30));
        stroke(g,x,y,w,h,9,raw(82,58,67),1.f);
    }
    label(g,title,x,y,w,h,14,accent?raw(255,247,248):raw(232,225,228),FontStyleBold,StringAlignmentCenter);
}
inline void progress(Graphics& g,float x,float y,float w,int tick,bool done=false) {
    auto raw=[](int r,int gg,int b,int a=255){return Color((BYTE)a,(BYTE)r,(BYTE)gg,(BYTE)b);};
    fill(g,x,y,w,6,3,raw(52,39,45));
    if(done){grad(g,x,y,w,6,3,raw(188,35,55),raw(255,95,113));return;}
    const float track=(float)((tick*4)%(int)(w+125));
    float xx=x+track-125;
    float sx=(std::max)(x,xx), ex=(std::min)(x+w,xx+125);
    if(ex>sx)grad(g,sx,y,ex-sx,6,3,raw(172,29,49),raw(255,84,104));
}
inline void paintbuffer(HWND hwnd, const std::function<void(Graphics&,int,int)>& painter){
    PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);
    if(!dc)return;
    RECT r{};GetClientRect(hwnd,&r);int w=r.right-r.left,h=r.bottom-r.top;
    if(w <= 0 || h <= 0) { EndPaint(hwnd,&ps); return; }
    HDC bdc=CreateCompatibleDC(dc);
    if(!bdc) { EndPaint(hwnd,&ps); return; }
    HBITMAP bmp=CreateCompatibleBitmap(dc,w,h);
    if(!bmp) { DeleteDC(bdc); EndPaint(hwnd,&ps); return; }
    HGDIOBJ old=SelectObject(bdc,bmp);
    {Graphics g(bdc);setup(g);painter(g,w,h);}
    BitBlt(dc,0,0,w,h,bdc,0,0,SRCCOPY);
    SelectObject(bdc,old);DeleteObject(bmp);DeleteDC(bdc);EndPaint(hwnd,&ps);
}
inline void rounded_window(HWND hwnd,int w,int h){
   HRGN region=CreateRoundRectRgn(0,0,w+1,h+1,24,24);
   if(region)SetWindowRgn(hwnd,region,TRUE); // Window owns region after success
}
} // namespace glassui

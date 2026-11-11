#pragma once

// Shared, authentication-independent GDI+ chrome used by the public MEZA UI.
#include "glass_style.h"

inline Gdiplus::Color rgb(int r, int g, int b, int a = 255) {
    return Gdiplus::Color((BYTE)a, (BYTE)r, (BYTE)g, (BYTE)b);
}

inline void rect(Gdiplus::Graphics& g, float x, float y, float w, float h, float radius,
                 Gdiplus::Color background, Gdiplus::Color border) {
    auto path = glassui::rounded(x, y, w, h, radius);
    Gdiplus::SolidBrush fill(background);
    Gdiplus::Pen stroke(border, 1.0f);
    g.FillPath(&fill, path.get());
    g.DrawPath(&stroke, path.get());
}

inline void txt(Gdiplus::Graphics& g, const wchar_t* value, float x, float y, float w, float h,
                float size, Gdiplus::Color color, int style = Gdiplus::FontStyleRegular,
                Gdiplus::StringAlignment align = Gdiplus::StringAlignmentNear) {
    glassui::label(g, value, x, y, w, h, size, color, style, align);
}

inline void topbar(Gdiplus::Graphics& g) {
    Gdiplus::LinearGradientBrush gradient(Gdiplus::PointF(0.f, 0.f), Gdiplus::PointF(650.f, 368.f),
        rgb(9, 10, 20), rgb(22, 14, 31));
    g.FillRectangle(&gradient, 0.f, 0.f, 650.f, 368.f);
    rect(g, 1.f, 1.f, 648.f, 366.f, 9.f, rgb(0, 0, 0, 0), rgb(255, 42, 167, 140));
    txt(g, L"MEZA", 52.f, 20.f, 75.f, 22.f, 16.f, rgb(255, 132, 214), Gdiplus::FontStyleBold);
    txt(g, L"NATIVE CONTROL", 128.f, 22.f, 130.f, 18.f, 9.5f, rgb(159, 145, 188), Gdiplus::FontStyleBold);
    rect(g, 52.f, 51.f, 340.f, 1.f, 0.f, rgb(70, 44, 93), rgb(70, 44, 93));
}

inline void shield(Gdiplus::Graphics& g, float frame) {
    const float bob = std::sinf(frame * 0.06f) * 2.0f;
    glassui::grad(g, 430.f, 66.f, 178.f, 250.f, 18.f, rgb(20, 13, 36, 244), rgb(50, 13, 50, 235));
    glassui::stroke(g, 430.f, 66.f, 178.f, 250.f, 18.f, rgb(255, 42, 167, 132), 1.f);
    Gdiplus::SolidBrush aura_a(rgb(255, 42, 167, 25)), aura_b(rgb(113, 83, 255, 23));
    g.FillEllipse(&aura_a, Gdiplus::RectF(450.f, 83.f + bob, 138.f, 138.f));
    g.FillEllipse(&aura_b, Gdiplus::RectF(465.f, 100.f - bob, 108.f, 108.f));
    glassui::meza_logo(g, 464.f, 95.f + bob, 110.f);
    txt(g, L"MEZA", 445.f, 211.f, 148.f, 25.f, 17.f, rgb(255, 132, 214), Gdiplus::FontStyleBold, Gdiplus::StringAlignmentCenter);
    txt(g, L"NATIVE CONTROL", 445.f, 238.f, 148.f, 16.f, 9.3f, rgb(190, 175, 219), Gdiplus::FontStyleBold, Gdiplus::StringAlignmentCenter);
}

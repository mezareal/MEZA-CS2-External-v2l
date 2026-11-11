#include "dx_renderer.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace forceinline {
    // Customize your font here
    dx_renderer::dx_renderer( IDirect3DDevice9* device ) {
        if ( !device )
            throw std::invalid_argument( "dx_renderer::dx_renderer: device is nullptr" );

        m_device = device;

        if ( FAILED( D3DXCreateLine( m_device, &m_line ) ) )
            throw std::exception( "dx_renderer::dx_renderer: failed to create line" );

        // Compact UI typography: clear at overlay scale without looking oversized.
        if ( FAILED( D3DXCreateFontA( m_device, 15, 0, FW_SEMIBOLD, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI", &m_font ) ) ) {
            if ( FAILED( D3DXCreateFontA( m_device, 14, 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Tahoma", &m_font ) ) )
                throw std::exception( "dx_renderer::dx_renderer: failed to create font" );
        }

        // Page titles are deliberately restrained; hierarchy comes from spacing.
        if ( FAILED( D3DXCreateFontA( m_device, 22, 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI", &m_font_title ) ) ) {
            D3DXCreateFontA( m_device, 18, 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Tahoma", &m_font_title );
        }

        // Small labels and metadata stay readable while keeping the deck dense.
        if ( FAILED( D3DXCreateFontA( m_device, 12, 0, FW_SEMIBOLD, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI", &m_font_small ) ) ) {
            D3DXCreateFontA( m_device, 11, 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Tahoma", &m_font_small );
        }

        const HRSRC image_resource = FindResourceA( nullptr, MAKEINTRESOURCEA( 101 ), RT_RCDATA );
        if ( image_resource ) {
            const DWORD image_size = SizeofResource( nullptr, image_resource );
            const HGLOBAL image_handle = LoadResource( nullptr, image_resource );
            const void* image_data = image_handle ? LockResource( image_handle ) : nullptr;
            if ( image_data && image_size ) {
                D3DXCreateTextureFromFileInMemoryEx( m_device, image_data, image_size,
                    D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_UNKNOWN,
                    D3DPOOL_MANAGED, D3DX_FILTER_LINEAR, D3DX_FILTER_LINEAR,
                    0, nullptr, nullptr, &m_operator_texture );
            }
        }

        // Persistent managed-pool textures: loaded once, never per frame.
        auto load_ui_resource = [&]( int id, IDirect3DTexture9** out ) {
            const HRSRC r = FindResourceA(nullptr, MAKEINTRESOURCEA(id), RT_RCDATA);
            if (!r) return;
            const DWORD count = SizeofResource(nullptr, r);
            const HGLOBAL glob = LoadResource(nullptr, r);
            const void* bytes = glob ? LockResource(glob) : nullptr;
            if (bytes && count > 0) {
                D3DXCreateTextureFromFileInMemoryEx(m_device, bytes, count,
                    D3DX_DEFAULT, D3DX_DEFAULT, 1, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED,
                    D3DX_FILTER_LINEAR, D3DX_FILTER_LINEAR, 0, nullptr, nullptr, out);
            }
        };
        load_ui_resource(102, &m_airglass_banner);
        load_ui_resource(103, &m_airglass_logo);
        load_ui_resource(121, &m_tray_logo);
        load_ui_resource(122, &m_meza_logo);
        for (int i=0; i<4; ++i) load_ui_resource(110+i, &m_airglass_pages[static_cast<size_t>(i)]);

        if ( !m_operator_texture ) {
            char exe_path[ MAX_PATH ]{};
            if ( GetModuleFileNameA( nullptr, exe_path, MAX_PATH ) > 0 ) {
                std::string path( exe_path );
                const size_t pos = path.find_last_of( "\\/" );
                if ( pos != std::string::npos ) {
                    const std::string dir = path.substr( 0, pos + 1 );
                    if ( FAILED( D3DXCreateTextureFromFileA( m_device, ( dir + "assets\\operator_preview.png" ).c_str( ), &m_operator_texture ) ) ) {
                        D3DXCreateTextureFromFileA( m_device, ( dir + "..\\assets\\operator_preview.png" ).c_str( ), &m_operator_texture );
                    }
                }
            }
            if ( !m_operator_texture ) {
                D3DXCreateTextureFromFileA( m_device, "assets\\operator_preview.png", &m_operator_texture );
            }
        }
    }

    dx_renderer::~dx_renderer( ) {
        if ( m_font )
            m_font->Release( );
        if ( m_font_title )
            m_font_title->Release( );
        if ( m_font_small )
            m_font_small->Release( );

        if ( m_line )
            m_line->Release( );
        if ( m_operator_texture )
            m_operator_texture->Release( );
        if ( m_airglass_banner ) m_airglass_banner->Release();
        if ( m_airglass_logo ) m_airglass_logo->Release();
        if ( m_tray_logo ) m_tray_logo->Release();
        if ( m_meza_logo ) m_meza_logo->Release();
        for (auto*& skin : m_airglass_pages) { if (skin) { skin->Release(); skin=nullptr; } }
    }

    dx_renderer::dx_renderer( dx_renderer&& other ) noexcept
        : m_fps( other.m_fps ),
          m_device( other.m_device ),
          m_font( other.m_font ),
          m_font_title( other.m_font_title ),
          m_font_small( other.m_font_small ),
          m_line( other.m_line ),
          m_operator_texture( other.m_operator_texture ),
          m_airglass_banner( other.m_airglass_banner ),
          m_airglass_logo( other.m_airglass_logo ),
          m_tray_logo( other.m_tray_logo ),
          m_meza_logo( other.m_meza_logo ),
          m_airglass_pages( other.m_airglass_pages ) {
        other.m_device = nullptr;
        other.m_font = nullptr;
        other.m_font_title = nullptr;
        other.m_font_small = nullptr;
        other.m_line = nullptr;
        other.m_operator_texture = nullptr;
        other.m_airglass_banner = nullptr;
        other.m_airglass_logo = nullptr;
        other.m_tray_logo = nullptr;
        other.m_meza_logo = nullptr;
        other.m_airglass_pages.fill(nullptr);
    }

    dx_renderer& dx_renderer::operator=( dx_renderer&& other ) noexcept {
        if ( this == &other )
            return *this;

        if ( m_font )
            m_font->Release( );
        if ( m_font_title )
            m_font_title->Release( );
        if ( m_font_small )
            m_font_small->Release( );
        if ( m_line )
            m_line->Release( );
        if ( m_operator_texture )
            m_operator_texture->Release( );
        if ( m_airglass_banner ) m_airglass_banner->Release();
        if ( m_airglass_logo ) m_airglass_logo->Release();
        if ( m_tray_logo ) m_tray_logo->Release();
        if ( m_meza_logo ) m_meza_logo->Release();
        for (auto*& skin : m_airglass_pages) { if (skin) { skin->Release(); skin=nullptr; } }

        m_fps = other.m_fps;
        m_device = other.m_device;
        m_font = other.m_font;
        m_font_title = other.m_font_title;
        m_font_small = other.m_font_small;
        m_line = other.m_line;
        m_operator_texture = other.m_operator_texture;
        m_airglass_banner = other.m_airglass_banner;
        m_airglass_logo = other.m_airglass_logo;
        m_tray_logo = other.m_tray_logo;
        m_meza_logo = other.m_meza_logo;
        m_airglass_pages = other.m_airglass_pages;

        other.m_device = nullptr;
        other.m_font = nullptr;
        other.m_font_title = nullptr;
        other.m_font_small = nullptr;
        other.m_line = nullptr;
        other.m_operator_texture = nullptr;
        other.m_airglass_banner = nullptr;
        other.m_airglass_logo = nullptr;
        other.m_tray_logo = nullptr;
        other.m_meza_logo = nullptr;
        other.m_airglass_pages.fill(nullptr);
        return *this;
    }

    bool dx_renderer::begin_rendering( ) {
        // Do not attempt font or geometry calls on a lost/uninitialized D3D9 device.
        if (!m_device || FAILED(m_device->TestCooperativeLevel())) return false;
        // Must exactly match dx_overlay's chroma key. A violet backing avoids
        // black antialias fringes on text while the keyed pixels remain fully
        // transparent to the user.
        if (FAILED(m_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 6, 0, 10), 1.f, 0))) return false;
        if (FAILED(m_device->BeginScene())) return false;
        m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        m_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        m_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        return true;
    }

    bool dx_renderer::end_rendering( ) {
        static int   fps            = 0;
        static const auto first_tick = std::chrono::steady_clock::now( );
        static auto last_tick = first_tick;

        // FPS-Zaehler erhoehen
        fps++;

        // Aktuelle Zeit holen
        const auto cur_tick = std::chrono::steady_clock::now( );

        // Normalize by the actual sample duration. A delayed frame must not
        // make the displayed rate look lower or higher for the next second.
        const float sample_seconds = std::chrono::duration<float>( cur_tick - last_tick ).count( );
        if ( sample_seconds >= 1.0f ) {
            m_fps = static_cast<int>( static_cast<float>( fps ) / sample_seconds + 0.5f );
            last_tick = cur_tick;
            fps   = 0;
        }

        if ( FAILED( m_device->EndScene( ) ) )
            return false;
        return SUCCEEDED( m_device->Present( NULL, NULL, NULL, NULL ) );
    }

    void dx_renderer::on_lost_device( ) {
        if ( m_font )
            m_font->OnLostDevice( );
        if ( m_font_title )
            m_font_title->OnLostDevice( );
        if ( m_font_small )
            m_font_small->OnLostDevice( );
        if ( m_line )
            m_line->OnLostDevice( );
    }

    void dx_renderer::on_reset_device( ) {
        if ( m_font )
            m_font->OnResetDevice( );
        if ( m_font_title )
            m_font_title->OnResetDevice( );
        if ( m_font_small )
            m_font_small->OnResetDevice( );
        if ( m_line )
            m_line->OnResetDevice( );
    }

    int dx_renderer::get_fps( ) {
        return m_fps; // Aktuellen Overlay-FPS zurueckgeben
    }

    void dx_renderer::draw_line( int x0, int y0, int x1, int y1, unsigned long color ) {
        D3DTLVERTEX lines[ 2 ] = {
            { static_cast< float >( x0 ), static_cast< float >( y0 ), 0.f, 1.f, color },
            { static_cast< float >( x1 ), static_cast< float >( y1 ), 0.f, 1.f, color }
        };

        m_device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
        m_device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        m_device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
        m_device->SetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
        m_device->SetTexture( 0, nullptr );
        m_device->DrawPrimitiveUP( D3DPT_LINELIST, 1, lines, sizeof( D3DTLVERTEX ) );
    }

    void dx_renderer::draw_rect( int x, int y, int w, int h, unsigned long color ) {
        D3DTLVERTEX lines[ 5 ] = {
            { static_cast< float >( x ),         static_cast< float >( y ),         0.f, 1.f, color },
            { static_cast< float >( x + w ),     static_cast< float >( y ),         0.f, 1.f, color },
            { static_cast< float >( x + w ),     static_cast< float >( y + h ),     0.f, 1.f, color },
            { static_cast< float >( x ),         static_cast< float >( y + h ),     0.f, 1.f, color },
            { static_cast< float >( x ),         static_cast< float >( y ),         0.f, 1.f, color }
        };

        m_device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
        m_device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        m_device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
        m_device->SetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
        m_device->SetTexture( 0, nullptr );
        m_device->DrawPrimitiveUP( D3DPT_LINESTRIP, 4, lines, sizeof( D3DTLVERTEX ) );
    }


    void dx_renderer::draw_round_filled(int x, int y, int w, int h, int radius, unsigned long color) {
        if (!m_device || w <= 0 || h <= 0) return;
        const float r = static_cast<float>((std::clamp)(radius, 0, (std::min)(w, h) / 2));
        if (r < 2.f) { draw_filled_rect(x, y, w, h, color); return; }
        constexpr int STEPS = 7;
        constexpr int N = 4 * (STEPS + 1);
        D3DTLVERTEX fan[N + 2]{};
        D3DTLVERTEX fringe[(N + 1) * 2]{};
        const float centers_x[4] = { x + r, x + w - r, x + w - r, x + r };
        const float centers_y[4] = { y + r, y + r, y + h - r, y + h - r };
        constexpr float pi = 3.14159265358979323846f;
        const unsigned long fade = color & 0x00ffffffUL;
        fan[0] = { x + w * .5f, y + h * .5f, 0.f, 1.f, color };
        for (int i = 0; i < N; ++i) {
            const int corner = i / (STEPS + 1);
            const int step = i % (STEPS + 1);
            const float theta = (180.f + corner * 90.f + step * (90.f / STEPS)) * pi / 180.f;
            const float cs = cosf(theta), sn = sinf(theta);
            const float xx = centers_x[corner] + cs * r;
            const float yy = centers_y[corner] + sn * r;
            fan[i + 1] = { xx, yy, 0.f, 1.f, color };
            fringe[2 * i] = { xx, yy, 0.f, 1.f, color };
            fringe[2 * i + 1] = { xx + cs * 1.15f, yy + sn * 1.15f, 0.f, 1.f, fade };
        }
        fan[N + 1] = fan[1];
        fringe[2 * N] = fringe[0];
        fringe[2 * N + 1] = fringe[1];
        m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        m_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        m_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        m_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        m_device->SetTexture(0, nullptr);
        m_device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, N, fan, sizeof(D3DTLVERTEX));
        m_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2 * N, fringe, sizeof(D3DTLVERTEX));
    }

    void dx_renderer::draw_filled_rect( int x, int y, int w, int h, unsigned long color ) {
        D3DTLVERTEX qV[ 4 ] = {
            { float( x ),     float( y + h ), 0.f, 1.f, color },
            { float( x ),     float( y ),     0.f, 1.f, color },
            { float( x + w ), float( y + h ), 0.f, 1.f, color },
            { float( x + w ), float( y ),     0.f, 1.f, color }
        };

        m_device->SetRenderState( D3DRS_ALPHABLENDENABLE, true );
        m_device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        m_device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
        m_device->SetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
        m_device->SetTexture( 0, nullptr );
        m_device->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, 2, qV, sizeof( D3DTLVERTEX ) );
    }

    void dx_renderer::draw_outlined_rect( int x, int y, int w, int h, unsigned long color ) {
        if ( w <= 2 || h <= 2 ) {
            draw_rect( x, y, w, h, color );
            return;
        }

        D3DTLVERTEX vertices[ 16 ]{};
        int cursor = 0;
        auto append_rect = [&]( int rx, int ry, int rw, int rh, unsigned long col ) {
            const int points[ 4 ][ 4 ] = {
                { rx, ry, rx + rw, ry }, { rx + rw, ry, rx + rw, ry + rh },
                { rx + rw, ry + rh, rx, ry + rh }, { rx, ry + rh, rx, ry }
            };
            for ( const auto& p : points ) {
                vertices[ cursor++ ] = { static_cast<float>( p[ 0 ] ), static_cast<float>( p[ 1 ] ), 0.f, 1.f, col };
                vertices[ cursor++ ] = { static_cast<float>( p[ 2 ] ), static_cast<float>( p[ 3 ] ), 0.f, 1.f, col };
            }
        };
        // One clean colour pass. Low-alpha halo layers are intentionally not
        // used: with a chroma-key overlay they become dark visible borders.
        append_rect( x, y, w, h, color );

        m_device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
        m_device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        m_device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
        m_device->SetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
        m_device->SetTexture( 0, nullptr );
        m_device->DrawPrimitiveUP( D3DPT_LINELIST, cursor / 2, vertices, sizeof( D3DTLVERTEX ) );
    }

    void dx_renderer::draw_corner_box( int x, int y, int w, int h, int corner, unsigned long color ) {
        if ( w <= 0 || h <= 0 )
            return;
        corner = (std::clamp)( corner, 2, (std::max)( 2, (std::min)( w, h ) / 2 ) );

        D3DTLVERTEX vertices[ 16 ]{};
        int cursor = 0;
        auto append_layer = [&]( int ox, int oy, unsigned long col ) {
            const int l=x+ox,t=y+oy,r=x+w+ox,b=y+h+oy,q=corner;
            const int segments[ 8 ][ 4 ] = {
                { l,t,l+q,t }, { l,t,l,t+q }, { r-q,t,r,t }, { r,t,r,t+q },
                { l,b-q,l,b }, { l,b,l+q,b }, { r-q,b,r,b }, { r,b-q,r,b }
            };
            for ( const auto& s : segments ) {
                vertices[ cursor++ ] = { static_cast<float>( s[ 0 ] ), static_cast<float>( s[ 1 ] ), 0.f, 1.f, col };
                vertices[ cursor++ ] = { static_cast<float>( s[ 2 ] ), static_cast<float>( s[ 3 ] ), 0.f, 1.f, col };
            }
        };
        append_layer( 0,0,color );

        m_device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
        m_device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        m_device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
        m_device->SetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
        m_device->SetTexture( 0, nullptr );
        m_device->DrawPrimitiveUP( D3DPT_LINELIST, 8, vertices, sizeof( D3DTLVERTEX ) );
    }

    void dx_renderer::draw_text( std::string_view text, int x, int y, unsigned long color, bool center, bool outline ) {
        if (!m_font) return;
        if ( center ) {
            RECT dimensions = get_text_dimensions( text );
            x -= ( dimensions.right - dimensions.left ) / 2;
        }

        auto _text = [ & ]( std::string_view _text, int _x, int _y, unsigned long _color ) {
            RECT r{ _x, _y, _x, _y };
            m_font->DrawTextA( NULL, _text.data( ), static_cast<int>( _text.size() ), &r, DT_NOCLIP, _color );
        };

        if ( outline ) {
            // One compact shadow keeps text readable and avoids four extra
            // font draw calls for every player label.
            _text( text, x + 1, y + 1, D3DCOLOR_RGBA( 1, 1, 1, 230 ) );
        }

        _text( text, x, y, color );
    }

    // Zeichnet einen Kreis aus Liniensegmenten (kein gefuellter Kreis)
    void dx_renderer::draw_circle( int cx, int cy, int radius, int segments, unsigned long color ) {
        if ( segments < 8 )
            segments = 16;
        if ( segments > 64 )
            segments = 64;

        constexpr int max_segments = 65;
        D3DTLVERTEX vertices[ max_segments ];
        const float step = static_cast< float >( 2.0 * M_PI ) / static_cast< float >( segments );

        for ( int i = 0; i <= segments; ++i ) {
            const float a = step * ( i % segments );
            vertices[ i ] = {
                static_cast< float >( cx ) + cosf( a ) * static_cast< float >( radius ),
                static_cast< float >( cy ) + sinf( a ) * static_cast< float >( radius ),
                0.f, 1.f, color
            };
        }

        m_device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
        m_device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        m_device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
        m_device->SetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
        m_device->SetTexture( 0, nullptr );
        m_device->DrawPrimitiveUP( D3DPT_LINESTRIP, segments, vertices, sizeof( D3DTLVERTEX ) );
    }

    // Zeichnet ein gefuelltes Rechteck mit vertikalem Farbverlauf (top -> bottom)
    void dx_renderer::draw_filled_rect_gradient( int x, int y, int w, int h, unsigned long color_top, unsigned long color_bottom ) {
        // Jede Vertexfarbe individuell setzen fuer Gradient-Effekt
        D3DTLVERTEX qV[ 4 ] = {
            { float( x ),     float( y + h ), 0.f, 1.f, color_bottom },
            { float( x ),     float( y ),     0.f, 1.f, color_top    },
            { float( x + w ), float( y + h ), 0.f, 1.f, color_bottom },
            { float( x + w ), float( y ),     0.f, 1.f, color_top    }
        };

        m_device->SetRenderState( D3DRS_ALPHABLENDENABLE, true );
        m_device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        m_device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
        m_device->SetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
        m_device->SetTexture( 0, nullptr );
        m_device->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, 2, qV, sizeof( D3DTLVERTEX ) );
    }

    // Convenience-Wrapper: Text mit versetztem Schattenpixel (dunkler, semi-transparent)
    void dx_renderer::draw_text_shadow( std::string_view text, int x, int y, unsigned long color ) {
        // Schatten: 1px nach rechts-unten versetzt, schwarz mit 50% Alpha
        draw_text( text, x + 1, y + 1, D3DCOLOR_RGBA( 0, 0, 0, 180 ), false, false );
        draw_text( text, x,     y,     color,                          false, false );
    }

    RECT dx_renderer::get_text_dimensions( std::string_view text ) {
        RECT r{};
        m_font->DrawTextA( NULL, text.data( ), static_cast<int>( text.size() ), &r, DT_CALCRECT, 0xFFFFFFFF );
        return r;
    }

    void dx_renderer::draw_filled_circle( int cx, int cy, float radius, int segments, unsigned long color ) {
        if ( segments < 8 )
            segments = 16;
        if ( segments > 48 )
            segments = 48;

        constexpr int max_vertices = 52;
        D3DTLVERTEX vertices[ max_vertices ];
        vertices[ 0 ] = { static_cast< float >( cx ), static_cast< float >( cy ), 0.f, 1.f, color };

        const float step = static_cast< float >( 2.0 * M_PI ) / static_cast< float >( segments );
        for ( int i = 0; i <= segments; ++i ) {
            const float a = step * i;
            vertices[ i + 1 ] = {
                static_cast< float >( cx ) + cosf( a ) * radius,
                static_cast< float >( cy ) + sinf( a ) * radius,
                0.f, 1.f, color
            };
        }

        m_device->SetRenderState( D3DRS_ALPHABLENDENABLE, true );
        m_device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        m_device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
        m_device->SetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
        m_device->SetTexture( 0, nullptr );
        m_device->DrawPrimitiveUP( D3DPT_TRIANGLEFAN, segments, vertices, sizeof( D3DTLVERTEX ) );
    }

    void dx_renderer::draw_filled_triangle( int x1, int y1, int x2, int y2, int x3, int y3, unsigned long color ) {
        if ( !m_device ) return;
        const D3DTLVERTEX vertices[3] = {
            { static_cast<float>( x1 ), static_cast<float>( y1 ), 0.f, 1.f, color },
            { static_cast<float>( x2 ), static_cast<float>( y2 ), 0.f, 1.f, color },
            { static_cast<float>( x3 ), static_cast<float>( y3 ), 0.f, 1.f, color }
        };
        m_device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
        m_device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        m_device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
        m_device->SetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
        m_device->SetTexture( 0, nullptr );
        m_device->DrawPrimitiveUP( D3DPT_TRIANGLELIST, 1, vertices, sizeof( D3DTLVERTEX ) );
    }

    void dx_renderer::draw_toggle_switch( int x, int y, bool enabled, bool hovered ) {
        const int switch_w = 42;
        const int switch_h = 22;

        // Soft drop shadow
        draw_filled_rect( x + 1, y + 2, switch_w, switch_h, D3DCOLOR_RGBA( 0, 0, 0, 110 ) );

        // Track background (Deep Crimson Red & Pitch Black palette)
        const unsigned long track_color = enabled
            ? ( hovered ? D3DCOLOR_RGBA( 255, 71, 87, 255 ) : D3DCOLOR_RGBA( 227, 52, 69, 255 ) )
            : ( hovered ? D3DCOLOR_RGBA( 36, 36, 44, 255 ) : D3DCOLOR_RGBA( 25, 25, 32, 255 ) );
        draw_round_filled( x, y, switch_w, switch_h, 11, track_color );

        // Track border
        const unsigned long border_color = enabled
            ? D3DCOLOR_RGBA( 255, 120, 135, 255 )
            : ( hovered ? D3DCOLOR_RGBA( 227, 52, 69, 140 ) : D3DCOLOR_RGBA( 56, 56, 68, 255 ) );
        draw_outlined_rect( x, y, switch_w, switch_h, border_color );

        // Thumb slider circle with soft crimson glow
        const int thumb_x = enabled ? ( x + switch_w - 11 ) : ( x + 11 );
        const int thumb_y = y + switch_h / 2;
        const unsigned long thumb_color = enabled
            ? D3DCOLOR_RGBA( 255, 255, 255, 255 )
            : ( hovered ? D3DCOLOR_RGBA( 230, 230, 238, 255 ) : D3DCOLOR_RGBA( 165, 165, 175, 255 ) );
        if ( enabled ) {
            draw_filled_circle( thumb_x, thumb_y, 9.0f, 16, D3DCOLOR_RGBA( 227, 52, 69, 85 ) );
        }
        draw_filled_circle( thumb_x, thumb_y, 7.0f, 16, thumb_color );
        if ( enabled ) {
            draw_filled_circle( thumb_x, thumb_y, 2.5f, 10, D3DCOLOR_RGBA( 227, 52, 69, 255 ) );
        }
    }

    void dx_renderer::draw_text_title( std::string_view text, int x, int y, unsigned long color, bool center, bool shadow ) {
        ID3DXFont* font = m_font_title ? m_font_title : m_font;
        if ( !font ) return;

        if ( center ) {
            RECT r{};
            font->DrawTextA( NULL, text.data( ), static_cast<int>( text.size() ), &r, DT_CALCRECT, 0xFFFFFFFF );
            x -= ( r.right - r.left ) / 2;
        }

        if ( shadow ) {
            RECT r_shadow{ x + 1, y + 1, x + 1, y + 1 };
            font->DrawTextA( NULL, text.data( ), static_cast<int>( text.size() ), &r_shadow, DT_NOCLIP, D3DCOLOR_RGBA( 0, 0, 0, 190 ) );
        }

        RECT r{ x, y, x, y };
        font->DrawTextA( NULL, text.data( ), static_cast<int>( text.size() ), &r, DT_NOCLIP, color );
    }

    void dx_renderer::draw_text_small( std::string_view text, int x, int y, unsigned long color, bool center, bool shadow ) {
        ID3DXFont* font = m_font_small ? m_font_small : m_font;
        if ( !font ) return;

        if ( center ) {
            RECT r{};
            font->DrawTextA( NULL, text.data( ), static_cast<int>( text.size() ), &r, DT_CALCRECT, 0xFFFFFFFF );
            x -= ( r.right - r.left ) / 2;
        }

        if ( shadow ) {
            RECT r_shadow{ x + 1, y + 1, x + 1, y + 1 };
            font->DrawTextA( NULL, text.data( ), static_cast<int>( text.size() ), &r_shadow, DT_NOCLIP, D3DCOLOR_RGBA( 0, 0, 0, 170 ) );
        }

        RECT r{ x, y, x, y };
        font->DrawTextA( NULL, text.data( ), static_cast<int>( text.size() ), &r, DT_NOCLIP, color );
    }

    RECT dx_renderer::get_text_dimensions_title( std::string_view text ) {
        RECT r{};
        ID3DXFont* font = m_font_title ? m_font_title : m_font;
        if ( font )
            font->DrawTextA( NULL, text.data( ), static_cast<int>( text.size() ), &r, DT_CALCRECT, 0xFFFFFFFF );
        return r;
    }

    RECT dx_renderer::get_text_dimensions_small( std::string_view text ) {
        RECT r{};
        ID3DXFont* font = m_font_small ? m_font_small : m_font;
        if ( font )
            font->DrawTextA( NULL, text.data( ), static_cast<int>( text.size() ), &r, DT_CALCRECT, 0xFFFFFFFF );
        return r;
    }

    void dx_renderer::draw_badge( std::string_view text, int x, int y, unsigned long bg_color, unsigned long text_color, unsigned long border_color ) {
        RECT dim = get_text_dimensions_small( text );
        const int text_w = dim.right - dim.left;
        const int text_h = dim.bottom - dim.top;
        const int badge_w = text_w + 14;
        const int badge_h = text_h + 6;

        draw_filled_rect( x, y, badge_w, badge_h, bg_color );
        if ( border_color != 0 )
            draw_outlined_rect( x, y, badge_w, badge_h, border_color );

        draw_text_small( text, x + badge_w / 2, y + 2, text_color, true, false );
    }

    void dx_renderer::draw_filled_rect_gradient_h( int x, int y, int w, int h, unsigned long color_left, unsigned long color_right ) {
        D3DTLVERTEX qV[ 4 ] = {
            { float( x ),     float( y + h ), 0.f, 1.f, color_left  },
            { float( x ),     float( y ),     0.f, 1.f, color_left  },
            { float( x + w ), float( y + h ), 0.f, 1.f, color_right },
            { float( x + w ), float( y ),     0.f, 1.f, color_right }
        };

        m_device->SetRenderState( D3DRS_ALPHABLENDENABLE, true );
        m_device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        m_device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
        m_device->SetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
        m_device->SetTexture( 0, nullptr );
        m_device->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, 2, qV, sizeof( D3DTLVERTEX ) );
    }

    void dx_renderer::draw_drop_shadow( int x, int y, int w, int h, int depth, unsigned long shadow_color ) {
        if ( depth <= 0 ) return;
        const unsigned int alpha_base = ( shadow_color >> 24 ) & 0xFF;
        const unsigned long rgb_base = ( ( shadow_color & 0x00FFFFFF ) == 0 ) ? 0x0006080C : ( shadow_color & 0x00FFFFFF );

        for ( int i = depth; i >= 1; --i ) {
            const unsigned int layer_alpha = static_cast< unsigned int >(
                ( alpha_base * ( depth - i + 1 ) ) / ( depth * 2.8f )
            );
            if ( layer_alpha == 0 ) continue;
            const unsigned long col = ( layer_alpha << 24 ) | rgb_base;
            draw_filled_rect( x - i, y - i + 2, w + i * 2, h + i * 2, col );
        }
    }

    void dx_renderer::draw_slider( int x, int y, int w, int h, std::string_view label, float val, float min_val, float max_val, std::string_view unit, bool hovered, bool active, unsigned long accent ) {
        (void)h;
        // Label text (left)
        draw_text_shadow( label, x, y, hovered ? D3DCOLOR_RGBA( 250, 252, 255, 255 ) : D3DCOLOR_RGBA( 210, 218, 230, 255 ) );

        // Value text (right)
        char val_buf[ 32 ];
        if ( val == std::floor( val ) ) {
            snprintf( val_buf, sizeof( val_buf ), "%d %s", static_cast< int >( val ), unit.data( ) );
        } else {
            snprintf( val_buf, sizeof( val_buf ), "%.1f %s", val, unit.data( ) );
        }
        RECT val_dim = get_text_dimensions_small( val_buf );
        const int val_w = val_dim.right - val_dim.left;
        draw_text_small( val_buf, x + w - val_w, y + 1, accent );

        // Track bar geometry
        const int track_y = y + 20;
        const int track_h = 6;
        const float pct = std::clamp( ( val - min_val ) / ( ( max_val > min_val ) ? ( max_val - min_val ) : 1.0f ), 0.0f, 1.0f );
        const int fill_w = static_cast< int >( pct * w );

        // Background track with inner shadow (Deep Obsidian Black #101014)
        draw_round_filled( x, track_y, w, track_h, 3, D3DCOLOR_RGBA( 16, 16, 20, 255 ) );
        draw_outlined_rect( x, track_y, w, track_h, hovered ? D3DCOLOR_RGBA( 227, 52, 69, 180 ) : D3DCOLOR_RGBA( 45, 45, 56, 255 ) );

        // Filled active progress portion - Dark Wine to Bright Crimson Red
        if ( fill_w > 0 ) {
            draw_filled_rect_gradient_h( x + 1, track_y + 1, fill_w - 1, track_h - 2,
                D3DCOLOR_RGBA( 186, 24, 40, 255 ), accent ? accent : D3DCOLOR_RGBA( 227, 52, 69, 255 ) );
        }

        // Thumb handle (circular slider knob with glow)
        const int thumb_cx = x + fill_w;
        const int thumb_cy = track_y + track_h / 2;
        if ( hovered || active ) {
            draw_filled_circle( thumb_cx, thumb_cy, 9.0f, 16, ( accent & 0x00FFFFFF ) | 0x66000000 );
        }
        draw_filled_circle( thumb_cx, thumb_cy, 5.0f, 14, D3DCOLOR_RGBA( 255, 255, 255, 255 ) );
        draw_circle( thumb_cx, thumb_cy, 5, 14, accent ? accent : D3DCOLOR_RGBA( 227, 52, 69, 255 ) );
        draw_filled_circle( thumb_cx, thumb_cy, 2.0f, 10, accent ? accent : D3DCOLOR_RGBA( 227, 52, 69, 255 ) );
    }

    void dx_renderer::draw_color_swatch( int x, int y, int size, unsigned long color, bool selected, bool hovered ) {
        if ( selected ) {
            draw_round_filled( x - 3, y - 3, size + 6, size + 6, 8, D3DCOLOR_RGBA( 227, 52, 69, 120 ) );
            draw_round_filled( x - 2, y - 2, size + 4, size + 4, 8, D3DCOLOR_RGBA( 255, 71, 87, 230 ) );
            draw_round_filled( x - 1, y - 1, size + 2, size + 2, 7, D3DCOLOR_RGBA( 255, 255, 255, 245 ) );
        } else if ( hovered ) {
            draw_round_filled( x - 2, y - 2, size + 4, size + 4, 8, D3DCOLOR_RGBA( 227, 52, 69, 140 ) );
        }

        draw_round_filled( x, y, size, size, 6, color );

        if ( selected ) {
            const unsigned long check_col = ( ( ( color >> 16 ) & 0xFF ) > 180 && ( ( color >> 8 ) & 0xFF ) > 180 )
                ? D3DCOLOR_RGBA( 10, 10, 10, 255 ) : D3DCOLOR_RGBA( 255, 255, 255, 255 );
            const int cx = x + size / 2;
            const int cy = y + size / 2;
            draw_line( cx - 4, cy, cx - 1, cy + 3, check_col );
            draw_line( cx - 1, cy + 3, cx + 4, cy - 3, check_col );
        }
    }

    bool dx_renderer::draw_transparent_texture(IDirect3DTexture9* texture, int x, int y, int w, int h) {
        if (!m_device || !texture || w <= 0 || h <= 0) return false;
        struct textured_vertex { float x, y, z, rhw; unsigned long color; float u, v; };
        const float l = float(x) - .5f, t = float(y) - .5f;
        const float r = float(x+w) - .5f, b = float(y+h) - .5f;
        const textured_vertex q[4] = {
            {l,b,0.f,1.f,0xffffffff,0.f,1.f}, {l,t,0.f,1.f,0xffffffff,0.f,0.f},
            {r,b,0.f,1.f,0xffffffff,1.f,1.f}, {r,t,0.f,1.f,0xffffffff,1.f,0.f}
        };
        m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        m_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        m_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        m_device->SetTexture(0, texture);
        m_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        m_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        m_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        m_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        m_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        m_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        m_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        m_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(textured_vertex));
        m_device->SetTexture(0, nullptr);
        m_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        m_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        return true;
    }
    bool dx_renderer::draw_airglass_banner(int x, int y, int w, int h) {
        return draw_transparent_texture(m_airglass_banner, x, y, w, h);
    }
    bool dx_renderer::draw_airglass_logo(int x, int y, int w, int h) {
        return draw_transparent_texture(m_airglass_logo, x, y, w, h);
    }

    bool dx_renderer::draw_tray_logo(int x, int y, int w, int h) {
        return draw_transparent_texture(m_tray_logo, x, y, w, h);
    }

    bool dx_renderer::draw_meza_logo(int x, int y, int w, int h) {
        return draw_transparent_texture(m_meza_logo, x, y, w, h);
    }

    bool dx_renderer::draw_airglass_page(int page, int x, int y, int w, int h) {
    if (page < 0 || page >= static_cast<int>(m_airglass_pages.size())) return false;
    return draw_transparent_texture(m_airglass_pages[static_cast<size_t>(page)], x, y, w, h);
}

bool dx_renderer::draw_operator_preview( int x, int y, int w, int h ) {
        if ( !m_operator_texture || w <= 0 || h <= 0 )
            return false;

        struct textured_vertex {
            float x, y, z, rhw;
            unsigned long color;
            float u, v;
        };
        const float l = static_cast<float>( x ) - 0.5f;
        const float t = static_cast<float>( y ) - 0.5f;
        const float r = static_cast<float>( x + w ) - 0.5f;
        const float b = static_cast<float>( y + h ) - 0.5f;
        const textured_vertex vertices[ 4 ] = {
            { l, b, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
            { l, t, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f },
            { r, b, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f },
            { r, t, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f }
        };

        m_device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
        m_device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        m_device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
        m_device->SetTexture( 0, m_operator_texture );
        m_device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
        m_device->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
        m_device->SetTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
        m_device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
        m_device->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
        m_device->SetTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
        m_device->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
        m_device->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
        m_device->SetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 );
        m_device->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, 2, vertices, sizeof( textured_vertex ) );
        m_device->SetTexture( 0, nullptr );
        m_device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
        m_device->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE );
        m_device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
        m_device->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE );
        return true;
    }

    void dx_renderer::draw_segmented_button( int x, int y, int w, int h, std::string_view text, bool active, bool hovered, unsigned long accent ) {
        const unsigned long acc = (accent != 0) ? accent : D3DCOLOR_RGBA( 227, 52, 69, 255 );
        if ( active ) {
            draw_round_filled( x, y, w, h, 6, D3DCOLOR_RGBA( 48, 18, 25, 250 ) );
            draw_outlined_rect( x, y, w, h, acc );
            draw_filled_rect_gradient_h( x + 4, y + h - 2, w - 8, 2, acc, D3DCOLOR_RGBA( 255, 71, 87, 255 ) );
            draw_text_shadow( text, x + w / 2, y + ( h - 16 ) / 2, D3DCOLOR_RGBA( 243, 243, 245, 255 ) );
        } else if ( hovered ) {
            draw_round_filled( x, y, w, h, 6, D3DCOLOR_RGBA( 32, 28, 34, 240 ) );
            draw_outlined_rect( x, y, w, h, D3DCOLOR_RGBA( 227, 52, 69, 140 ) );
            draw_text_small( text, x + w / 2, y + ( h - 14 ) / 2, D3DCOLOR_RGBA( 243, 243, 245, 255 ), true, false );
        } else {
            draw_round_filled( x, y, w, h, 6, D3DCOLOR_RGBA( 23, 23, 28, 230 ) );
            draw_outlined_rect( x, y, w, h, D3DCOLOR_RGBA( 45, 45, 56, 180 ) );
            draw_text_small( text, x + w / 2, y + ( h - 14 ) / 2, D3DCOLOR_RGBA( 166, 166, 178, 255 ), true, false );
        }
    }

    void dx_renderer::draw_card_container( int x, int y, int w, int h, std::string_view header_title, unsigned long accent ) {
        const unsigned long acc = (accent != 0) ? accent : D3DCOLOR_RGBA( 227, 52, 69, 255 );
        draw_round_filled( x, y, w, h, 8, D3DCOLOR_RGBA( 19, 19, 23, 245 ) );
        draw_outlined_rect( x, y, w, h, D3DCOLOR_RGBA( 36, 36, 46, 200 ) );
        draw_outlined_rect( x + 1, y + 1, w - 2, h - 2, D3DCOLOR_RGBA( 9, 9, 12, 160 ) );

        if ( !header_title.empty( ) ) {
            draw_filled_rect_gradient_h( x + 1, y + 1, w - 2, 30, D3DCOLOR_RGBA( 28, 16, 20, 240 ), D3DCOLOR_RGBA( 19, 19, 23, 220 ) );
            draw_line( x + 1, y + 31, x + w - 2, y + 31, D3DCOLOR_RGBA( 227, 52, 69, 65 ) );

            draw_filled_rect( x + 10, y + 9, 3, 14, acc );
            draw_text_title( header_title, x + 20, y + 7, D3DCOLOR_RGBA( 243, 243, 245, 255 ), false, true );
        }
    }

    void dx_renderer::draw_toast_notification( int x, int y, int w, int h, std::string_view title, std::string_view message, float alpha_pct, unsigned long accent ) {
        const unsigned int a = static_cast< unsigned int >( std::clamp( alpha_pct, 0.0f, 1.0f ) * 255.0f );
        if ( a == 0 ) return;

        const unsigned long bg = D3DCOLOR_RGBA( 9, 9, 12, a * 245 / 255 );
        const unsigned long acc = (accent != 0) ? accent : D3DCOLOR_RGBA( 227, 52, 69, 255 );
        const unsigned long border = ( acc & 0x00FFFFFF ) | ( a << 24 );
        const unsigned long text_m = D3DCOLOR_RGBA( 170, 170, 182, a );

        draw_drop_shadow( x, y, w, h, 6, D3DCOLOR_RGBA( 0, 0, 0, a * 165 / 255 ) );
        draw_round_filled( x, y, w, h, 8, border );
        draw_round_filled( x + 1, y + 1, w - 2, h - 2, 7, bg );
        draw_filled_rect( x + 2, y + 6, 3, h - 12, acc );

        draw_text( title, x + 16, y + 10, D3DCOLOR_RGBA( 243, 243, 245, a ), false, false );
        draw_text_small( message, x + 16, y + 30, text_m, false, false );
    }

    void dx_renderer::draw_hud_watermark( int x, int y, std::string_view title, int fps, unsigned long accent ) {
        const std::string text = std::string( title ) + "  |  " + std::to_string( fps ) + " FPS";
        RECT dim = get_text_dimensions_small( text );
        const int tw = dim.right - dim.left;
        const int w = tw + 28;
        const int h = 26;

        draw_drop_shadow( x, y, w, h, 4, D3DCOLOR_RGBA( 0, 0, 0, 130 ) );
        draw_round_filled( x, y, w, h, 8, D3DCOLOR_RGBA( 227, 52, 69, 235 ) );
        draw_round_filled( x + 1, y + 1, w - 2, h - 2, 7, D3DCOLOR_RGBA( 9, 9, 12, 245 ) );
        draw_filled_rect_gradient_h( x + 9, y + 1, w - 18, 2, accent ? accent : D3DCOLOR_RGBA( 227, 52, 69, 255 ), D3DCOLOR_RGBA( 227, 52, 69, 0 ) );

        draw_filled_circle( x + 10, y + h / 2, 3.0f, 10, D3DCOLOR_RGBA( 34, 197, 94, 255 ) );
        draw_text_small( text, x + 18, y + 6, D3DCOLOR_RGBA( 243, 243, 245, 255 ), false, true );
    }

    void dx_renderer::draw_custom_cursor( int x, int y, unsigned long accent ) {
        if ( x < 0 || y < 0 ) return;
        const unsigned long border = D3DCOLOR_RGBA( 0, 0, 0, 255 );
        const unsigned long fill = D3DCOLOR_RGBA( 243, 243, 245, 255 );
        const unsigned long acc = (accent != 0) ? accent : D3DCOLOR_RGBA( 227, 52, 69, 255 );

        draw_line( x, y, x, y + 16, border );
        draw_line( x, y, x + 12, y + 12, border );
        draw_line( x, y + 16, x + 4, y + 12, border );
        draw_line( x + 4, y + 12, x + 8, y + 18, border );
        draw_line( x + 8, y + 18, x + 11, y + 17, border );
        draw_line( x + 11, y + 17, x + 7, y + 11, border );
        draw_line( x + 7, y + 11, x + 12, y + 12, border );

        draw_line( x + 1, y + 2, x + 1, y + 14, acc );
        draw_line( x + 2, y + 3, x + 2, y + 12, fill );
        draw_line( x + 3, y + 4, x + 3, y + 11, fill );
        draw_line( x + 4, y + 5, x + 4, y + 10, fill );
    }
}


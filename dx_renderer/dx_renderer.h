#pragma once
#include <string>
#include <string_view>
#include <array>
#include <stdexcept>
#include <thread>
#include <Windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#pragma comment (lib, "d3d9.lib")
#pragma comment (lib, "d3dx9.lib")

namespace forceinline {
    class dx_renderer {
    private:
        struct D3DTLVERTEX {
            float x, y, z, rhw;
            unsigned long color;
        };

    public:
        dx_renderer( ) { }
        dx_renderer( IDirect3DDevice9* device );
        ~dx_renderer( );

        dx_renderer( const dx_renderer& ) = delete;
        dx_renderer& operator=( const dx_renderer& ) = delete;
        dx_renderer( dx_renderer&& other ) noexcept;
        dx_renderer& operator=( dx_renderer&& other ) noexcept;

        bool begin_rendering( );
        bool end_rendering( );

        void on_lost_device( );
        void on_reset_device( );

        int get_fps( );

        void draw_line( int x0, int y0, int x1, int y1, unsigned long color );
        void draw_rect( int x, int y, int w, int h, unsigned long color );
        void draw_filled_rect( int x, int y, int w, int h, unsigned long color );
        // Rounded glass panel with a 1px anti-aliased fringe (native Direct3D9).
        void draw_round_filled( int x, int y, int w, int h, int radius, unsigned long color );
        void draw_outlined_rect( int x, int y, int w, int h, unsigned long color );
        // Eight corner segments plus a four-direction shadow in one draw call.
        void draw_corner_box( int x, int y, int w, int h, int corner, unsigned long color );
        void draw_text( std::string_view text, int x, int y, unsigned long color, bool center = true, bool outline = true );

        // Zeichnet einen Kreis (approximiert durch Liniensegmente)
        void draw_circle( int x, int y, int radius, int segments, unsigned long color );

        // Zeichnet ein gefuelltes Rechteck mit vertikalem Farbverlauf (oben -> unten)
        void draw_filled_rect_gradient( int x, int y, int w, int h, unsigned long color_top, unsigned long color_bottom );

        // Convenience-Wrapper: Zeichnet Text mit schwarzem Schatten
        void draw_text_shadow( std::string_view text, int x, int y, unsigned long color );

        RECT get_text_dimensions( std::string_view text );

        // Zeichnet einen gefuellten Kreis (mittels Dreiecksfaecher)
        void draw_filled_circle( int cx, int cy, float radius, int segments, unsigned long color );
        void draw_filled_triangle( int x1, int y1, int x2, int y2, int x3, int y3, unsigned long color );

        // Zeichnet einen modernen Toggle-Switch im Fluent/iOS Design
        void draw_toggle_switch( int x, int y, bool enabled, bool hovered = false );

        // Text mit Titel-Schriftart (groesser & fett)
        void draw_text_title( std::string_view text, int x, int y, unsigned long color, bool center = false, bool shadow = true );

        // Text mit kleiner Schriftart (fuer Badges, Hotkeys & Subtitles)
        void draw_text_small( std::string_view text, int x, int y, unsigned long color, bool center = false, bool shadow = true );

        // Zeichnet ein stilvolles Pill-Badge mit Hintergrund und Rahmen
        void draw_badge( std::string_view text, int x, int y, unsigned long bg_color, unsigned long text_color, unsigned long border_color = 0 );

        // Zeichnet ein gefuelltes Rechteck mit horizontalem Farbverlauf (links -> rechts)
        void draw_filled_rect_gradient_h( int x, int y, int w, int h, unsigned long color_left, unsigned long color_right );

        // Zeichnet weichen mehrstufigen Schlagschatten fuer Tiefe
        void draw_drop_shadow( int x, int y, int w, int h, int depth, unsigned long shadow_color = D3DCOLOR_RGBA( 0, 0, 0, 160 ) );

        // Zeichnet einen modernen interaktiven Schieberegler (Slider)
        void draw_slider( int x, int y, int w, int h, std::string_view label, float val, float min_val, float max_val, std::string_view unit, bool hovered, bool active, unsigned long accent );

        // Zeichnet eine runde Farbpalette / Swatch mit Glow & Checkmark bei Selektion
        void draw_color_swatch( int x, int y, int size, unsigned long color, bool selected, bool hovered );

        // Transparent character art used only by the menu preview.
        bool draw_operator_preview( int x, int y, int w, int h );
        // Real DX9 textures embedded in resources.rc for the Airglass theme.
        bool draw_airglass_banner( int x, int y, int w, int h );
        bool draw_airglass_logo( int x, int y, int w, int h );
        bool draw_tray_logo( int x, int y, int w, int h );
        bool draw_meza_logo( int x, int y, int w, int h );
        // Four static high-resolution interface skins. Interactive controls remain native DX9.
        bool draw_airglass_page( int page, int x, int y, int w, int h );

        // Zeichnet einen modernen Segmented / Tab-Button
        void draw_segmented_button( int x, int y, int w, int h, std::string_view text, bool active, bool hovered, unsigned long accent );

        // Zeichnet ein elegantes Kartenpanel mit dezentem Rahmen
        void draw_card_container( int x, int y, int w, int h, std::string_view header_title = "", unsigned long accent = 0 );

        // Zeichnet eine animierte schwebende Toast-Benachrichtigung
        void draw_toast_notification( int x, int y, int w, int h, std::string_view title, std::string_view message, float alpha_pct, unsigned long accent );

        // Zeichnet ein High-Tech In-Game Watermark / HUD Banner
        void draw_hud_watermark( int x, int y, std::string_view title, int fps, unsigned long accent );

        // Zeichnet einen edlen Cyber-Mauszeiger
        void draw_custom_cursor( int x, int y, unsigned long accent );

        RECT get_text_dimensions_title( std::string_view text );
        RECT get_text_dimensions_small( std::string_view text );

    private:
        int m_fps = 0;

        IDirect3DDevice9* m_device = nullptr;

        ID3DXFont* m_font = nullptr;
        ID3DXFont* m_font_title = nullptr;
        ID3DXFont* m_font_small = nullptr;
        ID3DXLine* m_line = nullptr;
        IDirect3DTexture9* m_operator_texture = nullptr;
        IDirect3DTexture9* m_airglass_banner = nullptr;
        IDirect3DTexture9* m_airglass_logo = nullptr;
        IDirect3DTexture9* m_tray_logo = nullptr;
        IDirect3DTexture9* m_meza_logo = nullptr;
        std::array<IDirect3DTexture9*,4> m_airglass_pages{};
        bool draw_transparent_texture( IDirect3DTexture9* texture, int x, int y, int w, int h );
    };
}

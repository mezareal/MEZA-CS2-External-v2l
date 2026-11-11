#pragma once
#include "../dx_renderer/dx_renderer.h"

namespace forceinline {
	class dx_overlay {
	private:
		struct wnd_rect_t : public RECT {
			int width( ) { return right - left; }
			int height( ) { return bottom - top; }
		};

	public:
		dx_overlay( ) { }
		dx_overlay( HWND target_window, bool not_topmost = false );
		dx_overlay( std::wstring_view target_class, std::wstring_view target_window, bool not_topmost = false );
		~dx_overlay( );

		dx_renderer create_renderer( );
		HWND get_overlay_wnd( );
		bool sync_to_target( dx_renderer* renderer = nullptr );
		void frame_presented( );
		void set_input_region( int x, int y, int width, int height );
		void set_interactive( bool interactive );
		bool consume_primary_click( bool& is_down );

		bool is_initialized( );

	private:
		void create_overlay( HWND target_window );
		void init_dx9( );
		bool reset_device( dx_renderer& renderer, int width, int height );

		int m_fps = 0;
		bool m_initialized = false;
		bool m_interactive = false;
		bool m_reset_pending = false;
		bool m_resources_lost = false;
		bool m_waiting_for_present = false;
		int m_backbuffer_width = 0;
		int m_backbuffer_height = 0;
		int m_cursor_show_adjustments = 0;
		HWND m_input_wnd = nullptr;
		RECT m_input_region{};
		bool m_input_region_valid = false;
		bool m_primary_down = false;
		bool m_primary_pressed = false;
		
		static bool m_not_topmost;

		static HWND m_overlay_wnd, m_target_wnd;
		wnd_rect_t m_overlay_wnd_size, m_target_wnd_size;

		IDirect3D9* m_d3d = nullptr;
		IDirect3DDevice9* m_device = nullptr;

		static LRESULT CALLBACK m_wnd_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam );
		static LRESULT CALLBACK m_input_wnd_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam );
	};
}

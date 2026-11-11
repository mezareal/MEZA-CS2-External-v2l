#include "dx_overlay.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace forceinline {
	dx_overlay::dx_overlay( HWND target_window, bool not_topmost ) {
		m_not_topmost = not_topmost;
		if ( !target_window || !IsWindow( target_window ) )
			throw std::invalid_argument( "dx_overlay::dx_overlay: invalid target window" );

		create_overlay( target_window );
	}

	/*
		Regarding the not_topmost argument:
		Some games/anti-cheats specifically check if a window has the WS_EX_TOPMOST and WS_VISIBLE flags, or has the game window as its parent.
		By setting not_topmost to true, we will use a trick to make our window act similar to having the WS_EX_TOPMOST style without it actually having it, 
		therefore making that window harder to detect (or easier, it depends on the anti-cheat. Before doing anything you should become familiar with the
		anti-cheat the game has to avoid being banned.
	*/
	dx_overlay::dx_overlay( std::wstring_view target_class, std::wstring_view target_window, bool not_topmost ) {
		m_not_topmost = not_topmost;

		if ( target_window.empty( ) && target_class.empty( ) )
			throw std::invalid_argument( "dx_overlay::dx_overlay: target_class and target_window empty" );

		const HWND found_window = FindWindowW( target_class.empty( ) ? NULL : target_class.data( ),
			target_window.empty( ) ? NULL : target_window.data( ) );
		if ( !found_window ) {
			throw std::invalid_argument( "dx_overlay::dx_overlay: target window could not be found" );
		}

		create_overlay( found_window );
	}

	dx_overlay::~dx_overlay( ) {
		if ( m_interactive )
			set_interactive( false );

        // Release Direct3D objects before destroying their target window.
        if (m_device) { m_device->Release(); m_device = nullptr; }
        if (m_d3d) { m_d3d->Release(); m_d3d = nullptr; }
		if ( m_input_wnd && IsWindow( m_input_wnd ) )
			DestroyWindow( m_input_wnd );
		m_input_wnd = nullptr;
        if (m_overlay_wnd && IsWindow(m_overlay_wnd))
            DestroyWindow(m_overlay_wnd);
        m_overlay_wnd = nullptr;
        m_target_wnd = nullptr;
	}

	dx_renderer dx_overlay::create_renderer( ) {
		return dx_renderer( m_device );	//Return a renderer object
	}

	HWND dx_overlay::get_overlay_wnd( ) {
		return m_overlay_wnd;	//Return our window handle
	}

	void dx_overlay::set_input_region( int x, int y, int width, int height ) {
		m_input_region.left = x;
		m_input_region.top = y;
		m_input_region.right = x + width;
		m_input_region.bottom = y + height;
		m_input_region_valid = width > 0 && height > 0;
	}

	bool dx_overlay::consume_primary_click( bool& is_down ) {
		is_down = m_primary_down;
		const bool pressed = m_primary_pressed;
		m_primary_pressed = false;
		return pressed;
	}

	void dx_overlay::set_interactive( bool interactive ) {
		if ( !m_input_wnd || !IsWindow( m_input_wnd ) )
			return;

		const bool state_changed = m_interactive != interactive;
		m_interactive = interactive;

		if ( interactive ) {
			ClipCursor( nullptr );
			if ( state_changed || !IsWindowVisible( m_input_wnd ) ) {
				RECT client_rect{};
				if ( m_overlay_wnd && GetClientRect( m_overlay_wnd, &client_rect ) ) {
					POINT screen_pos{ client_rect.left, client_rect.top };
					ClientToScreen( m_overlay_wnd, &screen_pos );
					const int w = client_rect.right - client_rect.left;
					const int h = client_rect.bottom - client_rect.top;
					SetWindowPos( m_input_wnd, HWND_TOPMOST, screen_pos.x, screen_pos.y,
						w > 0 ? w : 1, h > 0 ? h : 1,
						SWP_SHOWWINDOW | SWP_NOOWNERZORDER );
				}

				const HWND foreground = GetForegroundWindow( );
				const DWORD current_thread = GetCurrentThreadId( );
				const DWORD foreground_thread = foreground
					? GetWindowThreadProcessId( foreground, nullptr ) : 0;
				const bool attached = foreground_thread != 0 && foreground_thread != current_thread &&
					AttachThreadInput( current_thread, foreground_thread, TRUE ) != FALSE;
				SetForegroundWindow( m_input_wnd );
				SetActiveWindow( m_input_wnd );
				SetFocus( m_input_wnd );
				if ( attached )
					AttachThreadInput( current_thread, foreground_thread, FALSE );

				m_cursor_show_adjustments = 0;
				int display_count = 0;
				do {
					display_count = ShowCursor( TRUE );
					++m_cursor_show_adjustments;
				} while ( display_count < 0 && m_cursor_show_adjustments < 16 );
			}
			return;
		}

		ShowWindow( m_input_wnd, SW_HIDE );
		if ( GetCapture( ) == m_input_wnd )
			ReleaseCapture( );
		if ( state_changed ) {
			while ( m_cursor_show_adjustments-- > 0 )
				ShowCursor( FALSE );
			m_cursor_show_adjustments = 0;
			if ( m_target_wnd && IsWindow( m_target_wnd ) )
				SetForegroundWindow( m_target_wnd );
		}
	}

	bool dx_overlay::sync_to_target( dx_renderer* renderer ) {
		if ( !m_target_wnd || !IsWindow( m_target_wnd ) || !m_overlay_wnd || !IsWindow( m_overlay_wnd ) )
			return false;

		if ( IsIconic( m_target_wnd ) ) {
			ShowWindow( m_overlay_wnd, SW_HIDE );
			if ( m_input_wnd && IsWindow( m_input_wnd ) )
				ShowWindow( m_input_wnd, SW_HIDE );
			return true;
		}

		RECT client_rect{};
		if ( !GetClientRect( m_target_wnd, &client_rect ) )
			return false;

		const int new_width = client_rect.right - client_rect.left;
		const int new_height = client_rect.bottom - client_rect.top;

		if ( new_width <= 0 || new_height <= 0 ) {
			ShowWindow( m_overlay_wnd, SW_HIDE );
			if ( m_input_wnd && IsWindow( m_input_wnd ) )
				ShowWindow( m_input_wnd, SW_HIDE );
			return true;
		}

		POINT top_left{ client_rect.left, client_rect.top };
		if ( !ClientToScreen( m_target_wnd, &top_left ) )
			return false;

		const int old_width = m_overlay_wnd_size.width( );
		const int old_height = m_overlay_wnd_size.height( );
		const int old_left = m_overlay_wnd_size.left;
		const int old_top = m_overlay_wnd_size.top;

		m_overlay_wnd_size.left = top_left.x;
		m_overlay_wnd_size.top = top_left.y;
		m_overlay_wnd_size.right = top_left.x + new_width;
		m_overlay_wnd_size.bottom = top_left.y + new_height;

		if ( old_left != m_overlay_wnd_size.left || old_top != m_overlay_wnd_size.top ||
		     old_width != new_width || old_height != new_height ) {
			SetWindowPos( m_overlay_wnd,
				m_not_topmost ? HWND_TOP : HWND_TOPMOST,
				m_overlay_wnd_size.left,
				m_overlay_wnd_size.top,
				m_overlay_wnd_size.width( ),
				m_overlay_wnd_size.height( ),
				SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING );

			if ( m_interactive && m_input_wnd && IsWindow( m_input_wnd ) ) {
				SetWindowPos( m_input_wnd, HWND_TOPMOST,
					m_overlay_wnd_size.left, m_overlay_wnd_size.top,
					new_width, new_height,
					SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER );
			}
		}

		if ( renderer ) {
			if ( !m_device )
				return false;

			bool needs_reset = m_reset_pending ||
				m_backbuffer_width != new_width || m_backbuffer_height != new_height;
			const HRESULT coop = m_device->TestCooperativeLevel( );
			if ( coop == D3DERR_DEVICELOST ) {
				// Alt-tab and display-mode switches can keep D3D9 lost for several
				// frames. This is recoverable and must not terminate the overlay.
				m_reset_pending = true;
				m_waiting_for_present = true;
				ShowWindow( m_overlay_wnd, SW_HIDE );
				if ( m_input_wnd && IsWindow( m_input_wnd ) )
					ShowWindow( m_input_wnd, SW_HIDE );
				return true;
			}
			if ( coop == D3DERR_DEVICENOTRESET )
				needs_reset = true;

			if ( needs_reset ) {
				if ( !reset_device( *renderer, new_width, new_height ) ) {
					m_reset_pending = true;
					m_waiting_for_present = true;
					ShowWindow( m_overlay_wnd, SW_HIDE );
					if ( m_input_wnd && IsWindow( m_input_wnd ) )
						ShowWindow( m_input_wnd, SW_HIDE );
					return true;
				}
				m_reset_pending = false;
				m_backbuffer_width = new_width;
				m_backbuffer_height = new_height;
				m_waiting_for_present = true;
			}
		}

		// Overlay visibility is handled cleanly after presentation.
		// Avoid calling SetLayeredWindowAttributes on every frame, which deadlocks DWM/dxgkrnl.
		if ( !m_waiting_for_present && !IsWindowVisible( m_overlay_wnd ) )
			ShowWindow( m_overlay_wnd, SW_SHOWNOACTIVATE );
		if ( !m_waiting_for_present && m_interactive && m_input_wnd && IsWindow( m_input_wnd ) && !IsWindowVisible( m_input_wnd ) ) {
			SetWindowPos( m_input_wnd, HWND_TOPMOST,
				m_overlay_wnd_size.left, m_overlay_wnd_size.top,
				new_width, new_height,
				SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER );
		}

		return true;
	}

	void dx_overlay::frame_presented( ) {
		if ( !m_waiting_for_present || !m_overlay_wnd || !IsWindow( m_overlay_wnd ) )
			return;
		ShowWindow( m_overlay_wnd, SW_SHOWNOACTIVATE );
		m_waiting_for_present = false;
		if ( m_interactive && m_input_wnd && IsWindow( m_input_wnd ) ) {
			SetWindowPos( m_input_wnd, HWND_TOPMOST,
				m_overlay_wnd_size.left, m_overlay_wnd_size.top,
				m_overlay_wnd_size.width( ), m_overlay_wnd_size.height( ),
				SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER );
		}
	}

	bool dx_overlay::is_initialized( ) {
		return m_initialized;	//Is our overlay initialized properly?
	}

	void dx_overlay::create_overlay( HWND target_window ) {
		WNDCLASSEXW wc{};
		wc.cbSize = sizeof( wc );

		//Create our window class
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = m_wnd_proc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = GetModuleHandleW( nullptr );
		wc.hIcon = NULL;
		wc.hCursor = LoadCursor( NULL, IDC_ARROW );
		// A numeric RGB value is not a brush handle. Leaving this null and
		// consuming WM_ERASEBKGND prevents Windows from painting a black frame
		// between D3D presents during resize/alt-tab.
		wc.hbrBackground = nullptr;
		wc.lpszMenuName = nullptr;
		wc.lpszClassName = L"forceinline::dx_overlay";
		wc.hIconSm = NULL;

		//Register our window class
		if ( !RegisterClassExW( &wc ) && GetLastError( ) != ERROR_CLASS_ALREADY_EXISTS )
			throw std::exception( "dx_overlay::create_overlay: failed to register wndclassex" );

		// Keep the exact window selected by the caller. Looking it up again by
		// class can select an unrelated hidden SDL window.
		m_target_wnd = target_window;

		//Use the client area so the overlay follows the actual render surface.
		RECT client_rect{};
		GetClientRect( m_target_wnd, &client_rect );
		POINT top_left{ client_rect.left, client_rect.top };
		ClientToScreen( m_target_wnd, &top_left );
		m_target_wnd_size.left = top_left.x;
		m_target_wnd_size.top = top_left.y;
		m_target_wnd_size.right = top_left.x + ( client_rect.right - client_rect.left );
		m_target_wnd_size.bottom = top_left.y + ( client_rect.bottom - client_rect.top );
		if ( m_target_wnd_size.width( ) <= 0 )
			m_target_wnd_size.right = m_target_wnd_size.left + 1;
		if ( m_target_wnd_size.height( ) <= 0 )
			m_target_wnd_size.bottom = m_target_wnd_size.top + 1;

		//Make the window transparent
		DWORD ex_styles = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

		//Add WS_EX_TOPMOST if we choose
		if ( !m_not_topmost )
			ex_styles |= WS_EX_TOPMOST;

		//Create our window
		m_overlay_wnd = CreateWindowExW( ex_styles, L"forceinline::dx_overlay", L"", WS_POPUP | WS_VISIBLE,
											 m_target_wnd_size.left, m_target_wnd_size.top, m_target_wnd_size.width( ), m_target_wnd_size.height( ), nullptr, nullptr, wc.hInstance, this );

		if ( !m_overlay_wnd )
			throw std::exception( "dx_overlay::create_overlay: failed to create overlay window" );

		m_overlay_wnd_size = m_target_wnd_size;

		// A dedicated almost-invisible input window sits only over the menu. The
		// full-screen render overlay can remain permanently click-through, so game
		// focus/fullscreen transitions can no longer disable menu interaction.
		WNDCLASSEXW input_wc{};
		input_wc.cbSize = sizeof( input_wc );
		input_wc.style = CS_HREDRAW | CS_VREDRAW;
		input_wc.lpfnWndProc = m_input_wnd_proc;
		input_wc.hInstance = wc.hInstance;
		input_wc.hCursor = LoadCursor( nullptr, IDC_ARROW );
		input_wc.hbrBackground = nullptr;
		input_wc.lpszClassName = L"forceinline::dx_overlay_input";
		if ( !RegisterClassExW( &input_wc ) && GetLastError( ) != ERROR_CLASS_ALREADY_EXISTS )
			throw std::exception( "dx_overlay::create_overlay: failed to register input window" );

		m_input_wnd = CreateWindowExW( WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
			input_wc.lpszClassName, L"", WS_POPUP,
			m_target_wnd_size.left, m_target_wnd_size.top, 1, 1,
			m_overlay_wnd, nullptr, input_wc.hInstance, this );
		if ( !m_input_wnd )
			throw std::exception( "dx_overlay::create_overlay: failed to create input window" );
		SetLayeredWindowAttributes( m_input_wnd, 0, 1, LWA_ALPHA );
		ShowWindow( m_input_wnd, SW_HIDE );

		// Use a very dark violet chroma key instead of pure black. D3DX font
		// antialiasing blends glyph edges against the cleared back buffer; a
		// black key therefore produced visible black fringes around live ESP
		// labels. The violet key keeps those edge pixels hue-matched.
		SetLayeredWindowAttributes( m_overlay_wnd, RGB( 6, 0, 10 ), 0, LWA_COLORKEY );

		const MARGINS margins = { -1, -1, -1, -1 };
		DwmExtendFrameIntoClientArea( m_overlay_wnd, &margins );

		//Show our window
		ShowWindow( m_overlay_wnd, SW_SHOW );

        // If initialization throws, the constructor never runs its destructor.
        // Clean up the partially constructed HWND and D3D state explicitly.
        try {
            init_dx9();
        } catch (...) {
            if (m_device) { m_device->Release(); m_device = nullptr; }
            if (m_d3d) { m_d3d->Release(); m_d3d = nullptr; }
            if (m_overlay_wnd && IsWindow(m_overlay_wnd)) DestroyWindow(m_overlay_wnd);
            m_overlay_wnd = nullptr;
            m_target_wnd = nullptr;
            throw;
        }
	}

	void dx_overlay::init_dx9( ) {
		//Create DirectX object
		m_d3d = Direct3DCreate9( D3D_SDK_VERSION );

		if ( !m_d3d )
			throw std::exception( "dx_overlay::init_dx9: failed to create dx3d9 object" );

		// In windowed mode D3DFMT_UNKNOWN lets D3D9 choose the desktop format.
		// Supplying a fixed A8R8G8B8 buffer is rejected by some drivers for
		// layered windows, so use the measured client size and adapter format.
		RECT overlay_client{};
		GetClientRect( m_overlay_wnd, &overlay_client );
		const UINT back_buffer_width = overlay_client.right > overlay_client.left
			? static_cast< UINT >( overlay_client.right - overlay_client.left ) : 1u;
		const UINT back_buffer_height = overlay_client.bottom > overlay_client.top
			? static_cast< UINT >( overlay_client.bottom - overlay_client.top ) : 1u;

		D3DPRESENT_PARAMETERS d3d_pp{};
		d3d_pp.Windowed = TRUE;
		d3d_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
		d3d_pp.BackBufferCount = 1;
		d3d_pp.BackBufferFormat = D3DFMT_UNKNOWN;
		d3d_pp.BackBufferWidth = back_buffer_width;
		d3d_pp.BackBufferHeight = back_buffer_height;
		d3d_pp.hDeviceWindow = m_overlay_wnd;
		d3d_pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

		// Hardware vertex processing is not available on every adapter. Try
		// the normal path first and then fall back to mixed/software processing.
		const DWORD behavior_flags[] = {
			D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
			D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
			D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE
		};

		HRESULT last_result = D3DERR_INVALIDCALL;
		for ( const DWORD behavior : behavior_flags ) {
			last_result = m_d3d->CreateDevice( D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
				m_overlay_wnd, behavior, &d3d_pp, &m_device );
			if ( SUCCEEDED( last_result ) )
				break;
		}

		// Retain a compatibility fallback for drivers that reject COPY.
		if ( FAILED( last_result ) || !m_device ) {
			d3d_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
			d3d_pp.BackBufferCount = 1;
			d3d_pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
			last_result = m_d3d->CreateDevice( D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
				m_overlay_wnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
				&d3d_pp, &m_device );
		}

		if ( FAILED( last_result ) || !m_device ) {
			std::ostringstream error;
			error << "dx_overlay::init_dx9: failed to create device (HRESULT 0x"
				<< std::uppercase << std::hex << static_cast< unsigned long >( last_result ) << ")";

			m_d3d->Release( );
			m_d3d = nullptr;
			throw std::runtime_error( error.str( ) );
		}

		//Overlay successfully initialized
		m_backbuffer_width = static_cast< int >( back_buffer_width );
		m_backbuffer_height = static_cast< int >( back_buffer_height );
		m_reset_pending = false;
		m_initialized = true;
	}

	bool dx_overlay::reset_device( dx_renderer& renderer, int width, int height ) {
		if ( !m_device || width <= 0 || height <= 0 )
			return false;

		D3DPRESENT_PARAMETERS present_parameters{};
		present_parameters.Windowed = TRUE;
		present_parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
		present_parameters.BackBufferCount = 1;
		present_parameters.BackBufferFormat = D3DFMT_UNKNOWN;
		present_parameters.BackBufferWidth = static_cast< UINT >( width );
		present_parameters.BackBufferHeight = static_cast< UINT >( height );
		present_parameters.hDeviceWindow = m_overlay_wnd;
		present_parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

		if ( !m_resources_lost ) {
			renderer.on_lost_device( );
			m_resources_lost = true;
		}
		HRESULT result = m_device->Reset( &present_parameters );
		if ( FAILED( result ) ) {
			present_parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
			present_parameters.BackBufferCount = 1;
			present_parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
			result = m_device->Reset( &present_parameters );
		}
		if ( FAILED( result ) ) {
            // A failed Reset leaves D3D9 in a lost state; do not call on_reset_device until reset succeeds.
            return false;
		}

		renderer.on_reset_device( );
		m_resources_lost = false;
		return true;
	}

	bool dx_overlay::m_not_topmost = false;
	HWND dx_overlay::m_target_wnd, dx_overlay::m_overlay_wnd;

	LRESULT CALLBACK dx_overlay::m_wnd_proc( HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam ) {
		auto* overlay = reinterpret_cast< dx_overlay* >( GetWindowLongPtrW( wnd, GWLP_USERDATA ) );
		if ( msg == WM_NCCREATE ) {
			auto* create = reinterpret_cast< CREATESTRUCTW* >( lparam );
			overlay = static_cast< dx_overlay* >( create->lpCreateParams );
			SetWindowLongPtrW( wnd, GWLP_USERDATA, reinterpret_cast< LONG_PTR >( overlay ) );
		}

		//Imitate WS_EX_TOPMOST if specified (only on activate / pos changes to prevent message recursion)
		if ( m_not_topmost && ( msg == WM_ACTIVATE || msg == WM_WINDOWPOSCHANGED ) ) {
			wnd_rect_t r;
			if ( m_target_wnd && IsWindow( m_target_wnd ) && GetWindowRect( m_target_wnd, &r ) ) {
				SetWindowPos( m_target_wnd, m_overlay_wnd, r.left, r.top, r.width( ), r.height( ), SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
			}
		}

		switch ( msg ) {
			case WM_NCHITTEST:
				// Make the entire overlay a normal client surface while the menu is
				// open. Outside that state, pass every hit straight to the game.
				return overlay && overlay->m_interactive ? HTCLIENT : HTTRANSPARENT;
			case WM_MOUSEACTIVATE:
				return overlay && overlay->m_interactive ? MA_ACTIVATE : MA_NOACTIVATE;
			case WM_ERASEBKGND:
				return 1;
			case WM_SETCURSOR:
				SetCursor( nullptr );
				return TRUE;
            case WM_DESTROY:
                // We have a manually managed message loop. Posting WM_QUIT
                // here poisons the message queue for the Retry path.
                return 0;
			default:
				return DefWindowProc( wnd, msg, wparam, lparam );
		}
	}

	LRESULT CALLBACK dx_overlay::m_input_wnd_proc( HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam ) {
		auto* overlay = reinterpret_cast< dx_overlay* >( GetWindowLongPtrW( wnd, GWLP_USERDATA ) );
		if ( msg == WM_NCCREATE ) {
			auto* create = reinterpret_cast< CREATESTRUCTW* >( lparam );
			overlay = static_cast< dx_overlay* >( create->lpCreateParams );
			SetWindowLongPtrW( wnd, GWLP_USERDATA, reinterpret_cast< LONG_PTR >( overlay ) );
		}
		switch ( msg ) {
			case WM_NCHITTEST:
				return HTCLIENT;
			case WM_MOUSEACTIVATE:
				return MA_ACTIVATE;
			case WM_LBUTTONDOWN:
				if ( overlay ) {
					overlay->m_primary_down = true;
					overlay->m_primary_pressed = true;
				}
				SetCapture( wnd );
				return 0;
			case WM_LBUTTONUP:
				if ( overlay )
					overlay->m_primary_down = false;
				if ( GetCapture( ) == wnd )
					ReleaseCapture( );
				return 0;
			case WM_CAPTURECHANGED:
				if ( overlay )
					overlay->m_primary_down = false;
				return 0;
			case WM_SETCURSOR:
				SetCursor( nullptr );
				return TRUE;
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT: {
				PAINTSTRUCT paint{};
				BeginPaint( wnd, &paint );
				EndPaint( wnd, &paint );
				return 0;
			}
			default:
				return DefWindowProcW( wnd, msg, wparam, lparam );
		}
	}
}

#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

#include "cs2_sdk/cs2_sdk.h"
#include "dx_overlay/dx_overlay.h"
#include "esp/esp.h"
#include "aim/aim.h"
#include "memory_manager/memory_manager.h"
#include "open_auth/open_auth_gate.h"
#include "ui/glass_style.h"

namespace {
    class scoped_handle {
    public:
        explicit scoped_handle( HANDLE value ) : m_value( value ) {}
        ~scoped_handle( ) {
            if ( m_value )
                CloseHandle( m_value );
        }

        scoped_handle( const scoped_handle& ) = delete;
        scoped_handle& operator=( const scoped_handle& ) = delete;

    private:
        HANDLE m_value = nullptr;
    };

    class scoped_timer_resolution {
    public:
        scoped_timer_resolution( ) : m_active( timeBeginPeriod( 1 ) == TIMERR_NOERROR ) {}
        ~scoped_timer_resolution( ) {
            if ( m_active )
                timeEndPeriod( 1 );
        }

        scoped_timer_resolution( const scoped_timer_resolution& ) = delete;
        scoped_timer_resolution& operator=( const scoped_timer_resolution& ) = delete;

    private:
        bool m_active = false;
    };

    struct find_window_params {
        HWND found = nullptr;
        DWORD my_pid = 0;
    };

    HWND find_game_window( ) {
        find_window_params params{};
        params.my_pid = GetCurrentProcessId( );

        EnumWindows( []( HWND hwnd, LPARAM lparam ) -> BOOL {
            if ( !IsWindowVisible( hwnd ) )
                return TRUE;

            auto* p = reinterpret_cast<find_window_params*>( lparam );

            DWORD pid = 0;
            GetWindowThreadProcessId( hwnd, &pid );
            if ( pid == 0 || pid == p->my_pid )
                return TRUE; // Ignore our own windows

            wchar_t cls[64]{};
            GetClassNameW( hwnd, cls, 64 );

            wchar_t title[128]{};
            GetWindowTextW( hwnd, title, 128 );

            if ( wcscmp( cls, L"SDL_app" ) == 0 || wcscmp( title, L"Counter-Strike 2" ) == 0 ) {
                HANDLE proc = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid );
                if ( proc ) {
                    wchar_t path[MAX_PATH]{};
                    DWORD size = MAX_PATH;
                    if ( QueryFullProcessImageNameW( proc, 0, path, &size ) ) {
                        std::wstring_view sv( path );
                        if ( sv.size() >= 7 && _wcsnicmp( sv.data() + sv.size() - 7, L"cs2.exe", 7 ) == 0 ) {
                            p->found = hwnd;
                            CloseHandle( proc );
                            return FALSE; // Found game window
                        }
                    }
                    CloseHandle( proc );
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>( &params ) );

        return params.found;
    }

    // Glass loading screen. No demo path or preview shortcut exists.
    struct wait_window_context {
        HWND hwnd=nullptr;
        HWND game_hwnd=nullptr;
        bool cancelled=false;
        bool found=false;
        bool close_hover=false;
        int animation=0;
        int found_ticks=0;
    };
    LRESULT CALLBACK wait_window_proc(HWND wnd,UINT msg,WPARAM wp,LPARAM lp){
        auto* ctx=reinterpret_cast<wait_window_context*>(GetWindowLongPtrW(wnd,GWLP_USERDATA));
        switch(msg){
        case WM_NCCREATE:{auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp);
            ctx=reinterpret_cast<wait_window_context*>(cs->lpCreateParams);ctx->hwnd=wnd;
            SetWindowLongPtrW(wnd,GWLP_USERDATA,(LONG_PTR)ctx);return TRUE;}
        case WM_CREATE:{SetTimer(wnd,1,33,nullptr);return 0;}
        case WM_TIMER:{
            if(!ctx)return 0;
            ++ctx->animation;
            if(ctx->found){
                if(++ctx->found_ticks>=32){DestroyWindow(wnd);return 0;}
            } else if(ctx->animation%17==0){
                if(HWND game=find_game_window()){
                    ctx->found=true;ctx->game_hwnd=game;ctx->found_ticks=0;
                }
            }
            InvalidateRect(wnd,nullptr,FALSE);return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{
            if(!ctx)return 0;
            glassui::paintbuffer(wnd,[&](Gdiplus::Graphics& g,int,int){
                using namespace meza_public_auth;
                topbar(g);
                // Compact 650x368 native loading view; same palette as the supplied GUI.
                shield(g, static_cast<float>(ctx->animation));
                txt(g,L"VEX",55,37,360,35,23,rgb(250,248,252),Gdiplus::FontStyleBold);
                txt(g,L"SYSTEM BEREIT & AKTIV",55,78,340,20,11,rgb(235,45,62),Gdiplus::FontStyleBold);
                const float cx=173.f,cy=187.f;
                const float radius[3] = {51.f,35.f,20.f};
                for(float r : radius){
                    Gdiplus::Pen ring(rgb(160,32,45,110),1.f);
                    g.DrawEllipse(&ring,Gdiplus::RectF(cx-r,cy-r,2.f*r,2.f*r));
                }
                const float angle=ctx->animation*.065f;
                Gdiplus::Pen beam(rgb(255,59,75),2.2f);
                g.DrawLine(&beam,Gdiplus::PointF(cx,cy),Gdiplus::PointF(cx+49.f*cosf(angle),cy+49.f*sinf(angle)));
                rect(g,cx-5.f,cy-5.f,10.f,10.f,5.f,rgb(220,38,54),rgb(255,75,92));
                txt(g,ctx->found?L"GAME DETECTED":L"WAITING FOR COUNTER-STRIKE 2",264,136,251,38,15,rgb(245,242,248),Gdiplus::FontStyleBold);
                txt(g,ctx->found?L"CS2 found. Starting MEZA...":L"Launch CS2 to establish a local session.",265,176,330,27,11.0f,rgb(165,152,160));
                rect(g,71,255,467,7,3,rgb(22,18,22),rgb(56,36,42));
                if(ctx->found){
                    rect(g,71,255,467,7,3,rgb(220,38,54),rgb(255,60,78));
                }else{
                    float fx=static_cast<float>((ctx->animation*3)%410);
                    rect(g,71+fx,255,68,7,3,rgb(220,38,54),rgb(255,60,78));
                }
                txt(g,L"LOCAL SESSION  /  NATIVE C++ x64",71,278,405,20,10.5f,rgb(145,135,142));
                txt(g,L"MEZA Support",318,322,284,24,11,rgb(255,103,205),Gdiplus::FontStyleBold);
                rect(g,71,319,119,29,4,rgb(24,18,22),rgb(95,45,54));
                txt(g,L"EXIT",71,319,119,29,12,rgb(245,242,248),Gdiplus::FontStyleBold,Gdiplus::StringAlignmentCenter);
            });return 0;}
        case WM_MOUSEMOVE:{if(!ctx)return 0;int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            bool hovered=(x>=71&&x<=190&&y>=319&&y<=348);
            if(hovered!=ctx->close_hover){ctx->close_hover=hovered;InvalidateRect(wnd,nullptr,FALSE);}
            TRACKMOUSEEVENT me{sizeof(me),TME_LEAVE,wnd,0};TrackMouseEvent(&me);return 0;}
        case WM_MOUSELEAVE: if(ctx){ctx->close_hover=false;InvalidateRect(wnd,nullptr,FALSE);}return 0;
        case WM_LBUTTONDOWN:{int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);
            if(x>=318 && x<604 && y>=317 && y<355) { meza_public_auth::open_support(); return 0; }
            if((x>=71&&x<=190&&y>=319&&y<=348)||(x>=615&&y<35)){
                if(ctx)ctx->cancelled=true;DestroyWindow(wnd);return 0;
            }
            if(y<113){ReleaseCapture();SendMessageW(wnd,WM_NCLBUTTONDOWN,HTCAPTION,0);return 0;}
            return 0;}
        case WM_KEYDOWN:if(wp==VK_ESCAPE){if(ctx)ctx->cancelled=true;DestroyWindow(wnd);}return 0;
        case WM_CLOSE:if(ctx)ctx->cancelled=true;DestroyWindow(wnd);return 0;
        case WM_DESTROY:KillTimer(wnd,1);PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(wnd,msg,wp,lp);
    }
    HWND wait_for_game_window(){
        if (HWND direct_game = find_game_window()) {
            return direct_game;
        }
        glassui::runtime gdiplus;
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.style=CS_HREDRAW|CS_VREDRAW;
        wc.lpfnWndProc=wait_window_proc;wc.hInstance=GetModuleHandleW(nullptr);
        wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));wc.lpszClassName=L"TRAY.Control.GameLoading";
        RegisterClassExW(&wc);
        wait_window_context state{};
        state.game_hwnd=find_game_window();
        state.found=(state.game_hwnd!=nullptr);
        constexpr int w=650,h=368;
        HWND wnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_LAYERED,wc.lpszClassName,L"MEZA / Loading",
            WS_POPUP,(GetSystemMetrics(SM_CXSCREEN)-w)/2,(GetSystemMetrics(SM_CYSCREEN)-h)/2,
            w,h,nullptr,nullptr,wc.hInstance,&state);
        if(!wnd)return nullptr;
        SetLayeredWindowAttributes(wnd,0,248,LWA_ALPHA);
        glassui::rounded_window(wnd,w,h);
        ShowWindow(wnd,SW_SHOW);UpdateWindow(wnd);
        MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
        return state.cancelled?nullptr:state.game_hwnd;
    }

    void show_error( const wchar_t* text ) {
        MessageBoxW( nullptr, text, L"MEZA", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND );
    }

    struct toast_notification {
        std::string title;
        std::string message;
        int remaining_ticks = 0;
        int max_ticks = 0;
        unsigned long accent = 0;
    };

    struct menu_geometry {
        int left = 0;
        int top = 0;
        int width = 900;
        int height = 590;
        int nav_width = 140;
        int brand_width = 0;
        int header_height = 68;
        int content_left = 0;
        int content_width = 0;
        int col1_x = 0;
        int col1_w = 0;
        int col2_x = 0;
        int col2_w = 0;
        int cards_y = 0;
        int cards_h = 0;
    };

    // Set once in wWinMain. The config writer and the menu touch the aimbot
    // through this pointer so neither signature has to grow a parameter.
    aim::aimbot* g_aim = nullptr;

    struct menu_state {
        int page = 0;           // 0 = Rage, 1 = Legit, 2 = Visuals, 3 = Misc, 4 = Config
        int sub_tab = 0;        // 0 = Aimbot, 1 = Visuals, 2 = Misc
        int side_icon = 0;      // 0 = Rage, 1 = Legit, 2 = Visuals, 3 = Misc, 4 = Config
        int active_slider = -1; // -1 = none
        bool mouse_was_down = false;
        bool show_status = false; // In-Game Watermark HUD (disabled)
        int panel_opacity = 230; // 135..245; rendered as dark glass
        int enemy_color_index = 0;
        int teammate_color_index = 0;
        int mouse_x = -1;
        int mouse_y = -1;
        int pos_x = -1;
        int pos_y = -1;
        bool is_dragging = false;
        int drag_start_mx = 0;
        int drag_start_my = 0;
        int drag_start_wx = 0;
        int drag_start_wy = 0;
        int preview_target = 0; // 0 = Enemies, 1 = Teammates
        toast_notification toast{};
        float ui_toggle_motion[32]{};
        float ui_nav_motion[5]{};
        float ui_rail_y = -1.f;
        bool preview_chams = true;
        bool preview_glow = false;
        bool preview_skeleton = true;
        bool preview_flags[7]{ true, true, true, true, true, true, true };
        int preview_material = 0; // 0 = Light Glow, 1 = Metallic, 2 = Glass Crystal
        bool search_focused = false;
        std::string search_query;
        bool search_keys_prev[256]{};
        // -1 = keine Aufnahme, 0 = Aim-Trigger-Taste wird gebunden.
        int binding_slot = -1;
        std::chrono::steady_clock::time_point ui_last_frame{};

        // TRAY V25 Screenshot exact parameters:
        bool silent_aim = false;
        bool auto_fire = false;
        bool auto_scope = false;
        int hitbox = 0;         // 0 = Head, 1 = Neck, 2 = Chest, 3 = Pelvis
        int multipoint = 0;     // 0 = Off, 1 = Low, 2 = Medium, 3 = High
        int hit_chance = 75;    // 0..100% (75% in screenshot)
        int min_damage = 25;    // 1..100 (25 in screenshot)
        bool antiaim_enable = false;
        int aa_pitch = 0;       // 0 = Off, 1 = Down, 2 = Up, 3 = Zero
        int aa_yaw = 0;         // 0 = Off, 1 = Backward, 2 = Spin, 3 = Jitter
        int aa_yaw_jitter = 0;  // 0 = Off, 1 = Offset, 2 = Center, 3 = Random
        int aa_desync = 0;      // 0..100% (0% in screenshot)
        bool resolver = true;   // (ON in screenshot)
        bool knife_bot = false; // (OFF in screenshot)
        bool backtrack = true;  // (ON in screenshot)
        bool lag_compensation = true; // (ON in screenshot)

        // Additional tabs state:
        bool legit_silent = false;
        bool legit_friendly = false;
        bool legit_scope_check = true;
        int legit_fov = 140;
        int legit_smooth = 4;
        bool trigger_enabled = false;
        int trigger_delay = 15;
        int trigger_hitchance = 85;
        bool trigger_magnet = false;
        int rcs_strength_pct = 100;
        bool edge_jump = false;
        bool auto_strafe = false;
        bool stream_proof = false;
        bool fast_stop = false;
    };

    void push_toast( menu_state& state, std::string_view title, std::string_view message, unsigned long accent ) {
        state.toast.title = title;
        state.toast.message = message;
        state.toast.max_ticks = 90;
        state.toast.remaining_ticks = 90;
        state.toast.accent = accent;
    }

    std::string get_config_file_path( ) {
        char override_dir[ MAX_PATH ]{};
        DWORD override_length = GetEnvironmentVariableA( "TRAY_CONFIG_DIR", override_dir, MAX_PATH );
        if ( override_length == 0 ) {
            override_length = GetEnvironmentVariableA( "NEMESIS_CONFIG_DIR", override_dir, MAX_PATH );
        }
        if ( override_length == 0 ) {
            override_length = GetEnvironmentVariableA( "NEBULA_CONFIG_DIR", override_dir, MAX_PATH );
        }
        if ( override_length > 0 && override_length < MAX_PATH ) {
            std::string path( override_dir, override_length );
            if ( path.back( ) != '\\' && path.back( ) != '/' )
                path += '\\';
            return path + "config.ini";
        }
        char module_path[ MAX_PATH ]{};
        if ( GetModuleFileNameA( nullptr, module_path, MAX_PATH ) > 0 ) {
            std::string path( module_path );
            const size_t pos = path.find_last_of( "\\/" );
            if ( pos != std::string::npos ) {
                const std::string local_cfg = path.substr( 0, pos + 1 ) + "config.ini";
                if ( GetFileAttributesA( local_cfg.c_str( ) ) != INVALID_FILE_ATTRIBUTES )
                    return local_cfg;
            }
        }
        char local_appdata[ MAX_PATH ]{};
        if ( GetEnvironmentVariableA( "LOCALAPPDATA", local_appdata, MAX_PATH ) > 0 ) {
            // Use the same writable config directory as the Python dashboard, including
            // on the first launch when config.ini does not exist yet.
            const std::string tray_dir = std::string( local_appdata ) + "\\TRAY";
            if ( CreateDirectoryA( tray_dir.c_str( ), nullptr ) || GetLastError( ) == ERROR_ALREADY_EXISTS ) {
                const std::string new_cfg = tray_dir + "\\config.ini";
                const std::string old_cfg = std::string( local_appdata ) + "\\NEMESIS\\config.ini";
                if ( GetFileAttributesA( new_cfg.c_str( ) ) == INVALID_FILE_ATTRIBUTES &&
                     GetFileAttributesA( old_cfg.c_str( ) ) != INVALID_FILE_ATTRIBUTES ) {
                    CopyFileA( old_cfg.c_str( ), new_cfg.c_str( ), TRUE );  // Import only if target is missing.
                }
                return new_cfg;
            }
            // If the directory cannot be created, retain the legacy fallback below.
            std::string nebula_cfg = std::string( local_appdata ) + "\\NebulaCS2\\config.ini";
            if ( GetFileAttributesA( nebula_cfg.c_str( ) ) != INVALID_FILE_ATTRIBUTES )
                return nebula_cfg;
        }
        char buffer[ MAX_PATH ]{};
        if ( GetModuleFileNameA( nullptr, buffer, MAX_PATH ) > 0 ) {
            std::string path( buffer );
            const size_t pos = path.find_last_of( "\\/" );
            if ( pos != std::string::npos )
                return path.substr( 0, pos + 1 ) + "config.ini";
        }
        return "config.ini";
    }

    bool copy_text_to_clipboard( HWND owner, std::string_view text ) {
        if ( !OpenClipboard( owner ) )
            return false;

        EmptyClipboard( );
        const SIZE_T bytes = text.size( ) + 1;
        HGLOBAL memory = GlobalAlloc( GMEM_MOVEABLE, bytes );
        if ( !memory ) {
            CloseClipboard( );
            return false;
        }

        void* destination = GlobalLock( memory );
        if ( !destination ) {
            GlobalFree( memory );
            CloseClipboard( );
            return false;
        }

        memcpy( destination, text.data( ), text.size( ) );
        static_cast<char*>( destination )[ text.size( ) ] = '\0';
        GlobalUnlock( memory );

        if ( !SetClipboardData( CF_TEXT, memory ) ) {
            GlobalFree( memory );
            CloseClipboard( );
            return false;
        }

        CloseClipboard( );
        return true;
    }

    bool open_config_directory( ) {
        char directory[ MAX_PATH ]{};
        const std::string config_path = get_config_file_path( );
        if ( config_path.empty( ) || config_path.size( ) >= MAX_PATH )
            return false;

        memcpy( directory, config_path.c_str( ), config_path.size( ) + 1 );
        if ( !PathRemoveFileSpecA( directory ) )
            return false;

        const auto result = reinterpret_cast<INT_PTR>(
            ShellExecuteA( nullptr, "open", directory, nullptr, nullptr, SW_SHOWNORMAL ) );
        return result > 32;
    }

    std::string get_diagnostics_file_path( ) {
        std::string path = get_config_file_path( );
        const size_t pos = path.find_last_of( "\\/" );
        if ( pos != std::string::npos )
            return path.substr( 0, pos + 1 ) + "meza_diagnostics.txt";
        return "meza_diagnostics.txt";
    }

    void write_live_diagnostics( DWORD target_pid, std::uintptr_t client_base,
                                 bool menu_open, bool show_esp,
                                 const esp::renderer& esp_renderer,
                                 const aim::aimbot& aim_controller ) {
        const auto sd = esp_renderer.stats( );
        const auto as = aim_controller.state( );
        std::ofstream out( get_diagnostics_file_path( ), std::ios::trunc );
        if ( !out )
            return;

        out << "pid=" << target_pid << '\n'
            << "client=0x" << std::hex << client_base << '\n'
            << "entity_list=0x" << sd.entity_list << '\n'
            << "local_pawn=0x" << sd.local_pawn << '\n'
            << "local_controller=0x" << sd.local_controller << '\n'
            << "stride=0x" << sd.stride << std::dec << '\n'
            << "stride_detected=" << sd.stride_detected << '\n'
            << "view_matrix_ok=" << sd.view_matrix_ok << '\n'
            << "controllers=" << sd.controllers << '\n'
            << "pawns=" << sd.pawns << '\n'
            << "drawn=" << sd.drawn << '\n'
            << "skipped_dead=" << sd.skipped_dead << '\n'
            << "skipped_team=" << sd.skipped_team << '\n'
            << "skipped_distance=" << sd.skipped_distance << '\n'
            << "skipped_projection=" << sd.skipped_projection << '\n'
            << "menu_open=" << menu_open << '\n'
            << "esp_enabled=" << show_esp << '\n'
            << "aim_enabled=" << aim_controller.config.enabled << '\n'
            << "aim_active=" << as.active << '\n'
            << "aim_foreground=" << as.game_foreground << '\n'
            << "aim_candidates=" << as.candidates << '\n'
            << "aim_in_fov=" << as.in_fov << '\n'
            << "aim_has_target=" << as.has_target << '\n'
            << "aim_move_x=" << as.move_x << '\n'
            << "aim_move_y=" << as.move_y << '\n'
            << "aim_fov=" << aim_controller.config.fov_pixels << '\n'
            << "aim_input_ok=" << as.input_ok << '\n'
            << "aim_target_changed=" << as.target_changed << '\n'
            << "team_verified=" << as.team_verified << '\n'
            << "offset_entity_list=0x" << std::hex << sdk::off().entity_list << '\n'
            << "offset_local_pawn=0x" << sdk::off().local_player_pawn << '\n'
            << "offset_local_controller=0x" << sdk::off().local_player_controller << '\n'
            << "offset_view_matrix=0x" << sdk::off().view_matrix << std::dec << '\n';
    }

    bool point_in_rect( int x, int y, int left, int top, int width, int height ) {
        return x >= left && x < left + width && y >= top && y < top + height;
    }

    unsigned long enemy_palette_color( int index ) {
        switch ( ( index % 8 + 8 ) % 8 ) {
        case 1: return D3DCOLOR_RGBA( 255, 60, 78, 245 );   // Bright Ruby Red (#FF3C4E)
        case 2: return D3DCOLOR_RGBA( 255, 170, 0, 245 );   // Amber Gold (#FFAA00)
        case 3: return D3DCOLOR_RGBA( 0, 245, 212, 245 );   // Neon Cyan (#00F5D4)
        case 4: return D3DCOLOR_RGBA( 133, 224, 38, 245 );  // Neon Lime (#85E026)
        case 5: return D3DCOLOR_RGBA( 255, 45, 85, 245 );   // Crimson Coral (#FF2D55)
        case 6: return D3DCOLOR_RGBA( 216, 115, 255, 245 ); // Neon Purple (#D873FF)
        case 7: return D3DCOLOR_RGBA( 248, 250, 255, 245 ); // Pure Ice White
        default: return D3DCOLOR_RGBA( 225, 36, 52, 245 );  // Deep Crimson Red (#E12434)
        }
    }

    unsigned long teammate_palette_color( int index ) {
        switch ( ( index % 8 + 8 ) % 8 ) {
        case 1: return D3DCOLOR_RGBA( 0, 245, 180, 245 );   // Emerald Mint (#00F5B4)
        case 2: return D3DCOLOR_RGBA( 168, 85, 247, 245 );  // Vapor Violet (#A855F7)
        case 3: return D3DCOLOR_RGBA( 133, 224, 38, 245 );  // Neon Lime (#85E026)
        case 4: return D3DCOLOR_RGBA( 255, 170, 0, 245 );   // Cyber Amber (#FFAA00)
        case 5: return D3DCOLOR_RGBA( 0, 245, 212, 245 );   // Neon Cyan (#00F5D4)
        case 6: return D3DCOLOR_RGBA( 255, 59, 86, 245 );   // Crimson Red (#FF3B56)
        case 7: return D3DCOLOR_RGBA( 245, 248, 255, 245 ); // Pure White
        default: return D3DCOLOR_RGBA( 92, 205, 255, 245 ); // Glass Cyan (#5CCDFF)
        }
    }

    const char* box_style_name( int style ) {
        switch ( ( style % 3 + 3 ) % 3 ) {
        case 0: return "Outlined 2D";
        case 1: return "Filled Glass";
        default: return "Corner Brackets";
        }
    }

    // Human readable name for any virtual key so the aim trigger can show
    // whatever the user actually bound (mouse 4/5, F-keys, letters, ...).
    std::string key_label( int vk ) {
        if ( vk <= 0 )
            return "NONE";
        switch ( vk ) {
        case VK_LBUTTON:  return "MOUSE 1";
        case VK_RBUTTON:  return "MOUSE 2";
        case VK_MBUTTON:  return "MOUSE 3";
        case VK_XBUTTON1: return "MOUSE 4";
        case VK_XBUTTON2: return "MOUSE 5";
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "SHIFT";
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "CTRL";
        case VK_MENU: case VK_LMENU: case VK_RMENU: return "ALT";
        case VK_SPACE:    return "SPACE";
        case VK_TAB:      return "TAB";
        case VK_CAPITAL:  return "CAPS";
        case VK_ESCAPE:   return "ESC";
        case VK_RETURN:   return "ENTER";
        case VK_BACK:     return "BACKSPACE";
        case VK_INSERT:   return "INSERT";
        case VK_DELETE:   return "DELETE";
        case VK_HOME:     return "HOME";
        case VK_END:      return "END";
        case VK_PRIOR:    return "PAGE UP";
        case VK_NEXT:     return "PAGE DOWN";
        case VK_LWIN: case VK_RWIN: return "WIN";
        default: break;
        }
        if ( vk >= 'A' && vk <= 'Z' )
            return std::string( 1, static_cast< char >( vk ) );
        if ( vk >= '0' && vk <= '9' )
            return std::string( 1, static_cast< char >( vk ) );
        if ( vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9 )
            return "NUM " + std::to_string( vk - VK_NUMPAD0 );
        if ( vk >= VK_F1 && vk <= VK_F24 )
            return "F" + std::to_string( vk - VK_F1 + 1 );
        char buffer[16]{};
        snprintf( buffer, sizeof( buffer ), "0x%02X", vk );
        return buffer;
    }

    const char* priority_name( int priority ) {
        switch ( ( priority % 4 + 4 ) % 4 ) {
        case 1: return "LOW HP";
        case 2: return "NEAREST";
        case 3: return "VISIBLE";
        default: return "CROSSHAIR";
        }
    }

    FILETIME g_last_config_filetime{};

    void load_config( menu_state& state, bool& show_esp, esp::renderer& esp_renderer ) {
        const std::string path = get_config_file_path( );
        if ( GetFileAttributesA( path.c_str( ) ) == INVALID_FILE_ATTRIBUTES )
            return;

        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if ( GetFileAttributesExA( path.c_str( ), GetFileExInfoStandard, &fad ) ) {
            g_last_config_filetime = fad.ftLastWriteTime;
        }

        // [Offsets] overrides are applied before anything reads a pointer, so a
        // corrected client.dll layout only needs a config edit, not a rebuild.
        sdk::load_offset_overrides( path );
        // TRAY V25 Parameters
        state.silent_aim = GetPrivateProfileIntA("Rage", "SilentAim", state.silent_aim ? 1 : 0, path.c_str()) != 0;
        state.auto_fire = GetPrivateProfileIntA("Rage", "AutoFire", state.auto_fire ? 1 : 0, path.c_str()) != 0;
        state.auto_scope = GetPrivateProfileIntA("Rage", "AutoScope", state.auto_scope ? 1 : 0, path.c_str()) != 0;
        state.hitbox = (std::clamp)(static_cast<int>(GetPrivateProfileIntA("Rage", "Hitbox", state.hitbox, path.c_str())), 0, 3);
        state.multipoint = (std::clamp)(static_cast<int>(GetPrivateProfileIntA("Rage", "Multipoint", state.multipoint, path.c_str())), 0, 3);
        state.hit_chance = (std::clamp)(static_cast<int>(GetPrivateProfileIntA("Rage", "HitChance", state.hit_chance, path.c_str())), 0, 100);
        state.min_damage = (std::clamp)(static_cast<int>(GetPrivateProfileIntA("Rage", "MinDamage", state.min_damage, path.c_str())), 1, 100);
        state.antiaim_enable = GetPrivateProfileIntA("Antiaim", "Enabled", state.antiaim_enable ? 1 : 0, path.c_str()) != 0;
        state.aa_pitch = (std::clamp)(static_cast<int>(GetPrivateProfileIntA("Antiaim", "Pitch", state.aa_pitch, path.c_str())), 0, 3);
        state.aa_yaw = (std::clamp)(static_cast<int>(GetPrivateProfileIntA("Antiaim", "Yaw", state.aa_yaw, path.c_str())), 0, 3);
        state.aa_yaw_jitter = (std::clamp)(static_cast<int>(GetPrivateProfileIntA("Antiaim", "YawJitter", state.aa_yaw_jitter, path.c_str())), 0, 3);
        state.aa_desync = (std::clamp)(static_cast<int>(GetPrivateProfileIntA("Antiaim", "Desync", state.aa_desync, path.c_str())), 0, 100);
        state.resolver = GetPrivateProfileIntA("Rage", "Resolver", state.resolver ? 1 : 0, path.c_str()) != 0;
        state.knife_bot = GetPrivateProfileIntA("Rage", "KnifeBot", state.knife_bot ? 1 : 0, path.c_str()) != 0;
        state.backtrack = GetPrivateProfileIntA("Rage", "Backtrack", state.backtrack ? 1 : 0, path.c_str()) != 0;
        state.lag_compensation = GetPrivateProfileIntA("Rage", "LagCompensation", state.lag_compensation ? 1 : 0, path.c_str()) != 0;

        state.legit_silent = GetPrivateProfileIntA( "InterfaceState", "LegitSilent", state.legit_silent ? 1 : 0, path.c_str( ) ) != 0;
        state.legit_friendly = GetPrivateProfileIntA( "InterfaceState", "LegitFriendly", state.legit_friendly ? 1 : 0, path.c_str( ) ) != 0;
        state.legit_scope_check = GetPrivateProfileIntA( "InterfaceState", "ScopeCheck", state.legit_scope_check ? 1 : 0, path.c_str( ) ) != 0;
        state.trigger_enabled = GetPrivateProfileIntA( "InterfaceState", "TriggerEnabled", state.trigger_enabled ? 1 : 0, path.c_str( ) ) != 0;
        state.trigger_delay = (std::clamp)( static_cast<int>( GetPrivateProfileIntA( "InterfaceState", "TriggerDelay", state.trigger_delay, path.c_str( ) ) ), 0, 100 );
        state.trigger_hitchance = (std::clamp)( static_cast<int>( GetPrivateProfileIntA( "InterfaceState", "TriggerHitchance", state.trigger_hitchance, path.c_str( ) ) ), 0, 100 );
        state.trigger_magnet = GetPrivateProfileIntA( "InterfaceState", "TriggerMagnet", state.trigger_magnet ? 1 : 0, path.c_str( ) ) != 0;
        state.edge_jump = GetPrivateProfileIntA( "InterfaceState", "EdgeJump", state.edge_jump ? 1 : 0, path.c_str( ) ) != 0;
        state.auto_strafe = GetPrivateProfileIntA( "InterfaceState", "AutoStrafe", state.auto_strafe ? 1 : 0, path.c_str( ) ) != 0;
        state.fast_stop = GetPrivateProfileIntA( "InterfaceState", "FastStop", state.fast_stop ? 1 : 0, path.c_str( ) ) != 0;


        state.panel_opacity = (std::clamp)(static_cast<int>(GetPrivateProfileIntA("Interface", "PanelOpacity", 190, path.c_str())), 135, 245);
        show_esp = GetPrivateProfileIntA( "Visuals", "MasterEsp", 1, path.c_str( ) ) != 0;
        esp_renderer.config.enabled = show_esp;
        esp_renderer.config.draw_box = GetPrivateProfileIntA( "Visuals", "Box", esp_renderer.config.draw_box ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.draw_teammates = GetPrivateProfileIntA( "Visuals", "Teammates", esp_renderer.config.draw_teammates ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.deathmatch_mode = GetPrivateProfileIntA( "Visuals", "DeathmatchMode", 2, path.c_str( ) );
        esp_renderer.config.draw_health = GetPrivateProfileIntA( "Visuals", "Health", esp_renderer.config.draw_health ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.draw_armor = GetPrivateProfileIntA( "Visuals", "Armor", esp_renderer.config.draw_armor ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.draw_weapon = GetPrivateProfileIntA( "Visuals", "Weapon", esp_renderer.config.draw_weapon ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.draw_tracers = GetPrivateProfileIntA( "Visuals", "Tracers", esp_renderer.config.draw_tracers ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.draw_names = GetPrivateProfileIntA( "Visuals", "Names", esp_renderer.config.draw_names ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.draw_distance = GetPrivateProfileIntA( "Visuals", "Distance", esp_renderer.config.draw_distance ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.draw_head_dot = GetPrivateProfileIntA( "Visuals", "HeadDot", esp_renderer.config.draw_head_dot ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.draw_fov_circle = GetPrivateProfileIntA( "Visuals", "FovCircle", esp_renderer.config.draw_fov_circle ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.draw_skeleton = GetPrivateProfileIntA( "Visuals", "Skeleton", esp_renderer.config.draw_skeleton ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.draw_offscreen = GetPrivateProfileIntA( "Visuals", "OffscreenArrows", esp_renderer.config.draw_offscreen ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.draw_bomb_timer = GetPrivateProfileIntA( "Visuals", "BombTimer", esp_renderer.config.draw_bomb_timer ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.draw_dropped_weapons = GetPrivateProfileIntA( "Visuals", "DroppedWeapons", esp_renderer.config.draw_dropped_weapons ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.draw_grenades = GetPrivateProfileIntA( "Visuals", "Grenades", esp_renderer.config.draw_grenades ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.flag_bomb = GetPrivateProfileIntA( "PlayerFlags", "Bomb", esp_renderer.config.flag_bomb ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.flag_flashed = GetPrivateProfileIntA( "PlayerFlags", "Flashed", esp_renderer.config.flag_flashed ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.flag_scoped = GetPrivateProfileIntA( "PlayerFlags", "Scoped", esp_renderer.config.flag_scoped ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.flag_defusing = GetPrivateProfileIntA( "PlayerFlags", "Defusing", esp_renderer.config.flag_defusing ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.flag_hostage = GetPrivateProfileIntA( "PlayerFlags", "Hostage", esp_renderer.config.flag_hostage ? 1 : 0, path.c_str( ) ) != 0;
        esp_renderer.config.fov_radius = GetPrivateProfileIntA( "Visuals", "FovRadius", esp_renderer.config.fov_radius, path.c_str( ) );
        esp_renderer.config.max_distance = GetPrivateProfileIntA( "Visuals", "MaxDistance", esp_renderer.config.max_distance, path.c_str( ) );
        esp_renderer.config.tracer_origin = GetPrivateProfileIntA( "Visuals", "TracerOrigin", esp_renderer.config.tracer_origin, path.c_str( ) );
        state.show_status = GetPrivateProfileIntA( "Visuals", "Watermark", 0, path.c_str( ) ) != 0;
        state.stream_proof = GetPrivateProfileIntA( "Interface", "StreamProof", 0, path.c_str( ) ) != 0;

        esp_renderer.config.box_style = GetPrivateProfileIntA( "Style", "BoxStyle", esp_renderer.config.box_style, path.c_str( ) );
        state.enemy_color_index = GetPrivateProfileIntA( "Style", "EnemyColorIndex", state.enemy_color_index, path.c_str( ) );
        state.teammate_color_index = GetPrivateProfileIntA( "Style", "TeammateColorIndex", state.teammate_color_index, path.c_str( ) );
        esp_renderer.config.enemy_color = enemy_palette_color( state.enemy_color_index );
        esp_renderer.config.teammate_color = teammate_palette_color( state.teammate_color_index );

        // Guard rendering and sliders against malformed/stale config.ini values.
        esp_renderer.config.fov_radius = std::clamp( esp_renderer.config.fov_radius, 30, 250 );
        esp_renderer.config.max_distance = std::clamp( esp_renderer.config.max_distance, 0, 300 );
        esp_renderer.config.box_style = std::clamp( esp_renderer.config.box_style, 0, 2 );
        esp_renderer.config.tracer_origin = std::clamp( esp_renderer.config.tracer_origin, 0, 1 );
        state.enemy_color_index = std::clamp( state.enemy_color_index, 0, 7 );
        state.teammate_color_index = std::clamp( state.teammate_color_index, 0, 7 );
        esp_renderer.config.enemy_color = enemy_palette_color( state.enemy_color_index );
        esp_renderer.config.teammate_color = teammate_palette_color( state.teammate_color_index );

        if ( g_aim ) {
            auto& a = g_aim->config;
            a.enabled = GetPrivateProfileIntA( "Aim", "Enabled", a.enabled ? 1 : 0, path.c_str( ) ) != 0;
            a.activation_mode = (std::clamp)(static_cast<int>(GetPrivateProfileIntA( "Aim", "ActivationMode", a.activation_mode, path.c_str( ) )), 0, 2);
            a.hotkey = (std::clamp)(static_cast<int>(GetPrivateProfileIntA( "Aim", "Hotkey", a.hotkey, path.c_str( ) )), 1, 255);
            a.fov_pixels = (std::clamp)(static_cast<int>(GetPrivateProfileIntA( "Aim", "Fov", a.fov_pixels, path.c_str( ) )), 10, 600);
            a.smooth = (std::clamp)(static_cast<int>(GetPrivateProfileIntA( "Aim", "Smooth", a.smooth, path.c_str( ) )), 1, 20);
            a.bone = (std::clamp)(static_cast<int>(GetPrivateProfileIntA( "Aim", "Bone", a.bone, path.c_str( ) )), 0, 2);
            a.max_distance_m = (std::clamp)(static_cast<int>(GetPrivateProfileIntA( "Aim", "MaxDistance", a.max_distance_m, path.c_str( ) )), 0, 300);
            a.teammates = GetPrivateProfileIntA( "Aim", "Teammates", a.teammates ? 1 : 0, path.c_str( ) ) != 0;
            a.draw_fov = GetPrivateProfileIntA( "Aim", "DrawFov", a.draw_fov ? 1 : 0, path.c_str( ) ) != 0;
            a.draw_target = GetPrivateProfileIntA( "Aim", "DrawTarget", a.draw_target ? 1 : 0, path.c_str( ) ) != 0;
            a.humanize = GetPrivateProfileIntA( "Aim", "Humanize", a.humanize ? 1 : 0, path.c_str( ) ) != 0;
            a.priority = (std::clamp)(static_cast<int>(GetPrivateProfileIntA( "Aim", "Priority", a.priority, path.c_str( ) )), 0, 3);
            a.visible_only = GetPrivateProfileIntA( "Aim", "VisibleOnly", a.visible_only ? 1 : 0, path.c_str( ) ) != 0;
            a.prediction_ms = (std::clamp)(static_cast<int>(GetPrivateProfileIntA( "Aim", "Prediction", a.prediction_ms, path.c_str( ) )), 0, 200);
            a.sticky_lock = GetPrivateProfileIntA( "Aim", "StickyLock", a.sticky_lock ? 1 : 0, path.c_str( ) ) != 0;
            a.lock_ms = (std::clamp)(static_cast<int>(GetPrivateProfileIntA( "Aim", "LockMs", a.lock_ms, path.c_str( ) )), 0, 1000);
            a.ring_style = (std::clamp)(static_cast<int>(GetPrivateProfileIntA( "Aim", "RingStyle", a.ring_style, path.c_str( ) )), 0, 2);
            a.draw_path = GetPrivateProfileIntA( "Aim", "DrawPath", a.draw_path ? 1 : 0, path.c_str( ) ) != 0;
            a.distance_smoothing = GetPrivateProfileIntA( "Aim", "DistanceSmoothing", a.distance_smoothing ? 1 : 0, path.c_str( ) ) != 0;
            a.rcs_enabled = GetPrivateProfileIntA( "Aim", "RcsEnabled", a.rcs_enabled ? 1 : 0, path.c_str( ) ) != 0;
            a.rcs_strength = (std::clamp)( GetPrivateProfileIntA( "Aim", "RcsStrength", static_cast<int>( a.rcs_strength * 100.0f ), path.c_str( ) ) / 100.0f, 0.0f, 2.0f );
            a.bhop = GetPrivateProfileIntA( "Aim", "Bhop", a.bhop ? 1 : 0, path.c_str( ) ) != 0;
            a.sensitivity = (std::clamp)( GetPrivateProfileIntA( "Aim", "Sensitivity", static_cast<int>( a.sensitivity * 100.0f ), path.c_str( ) ) / 100.0f, 0.1f, 10.0f );
            state.rcs_strength_pct = (std::clamp)( static_cast<int>( a.rcs_strength * 100.0f + 0.5f ), 0, 200 );
        }

        const int saved_x = GetPrivateProfileIntA( "Window", "PosX", -1, path.c_str( ) );        const int saved_y = GetPrivateProfileIntA( "Window", "PosY", -1, path.c_str( ) );
        if ( saved_x >= 0 && saved_y >= 0 ) {
            state.pos_x = saved_x;
            state.pos_y = saved_y;
        }

        // Synchronize preview state with loaded configuration
        state.preview_flags[0] = esp_renderer.config.draw_armor;
        state.preview_flags[1] = esp_renderer.config.flag_bomb;
        state.preview_flags[2] = esp_renderer.config.draw_distance;
        state.preview_flags[3] = esp_renderer.config.flag_flashed;
        state.preview_flags[4] = esp_renderer.config.flag_scoped;
        state.preview_flags[5] = esp_renderer.config.flag_defusing;
        state.preview_flags[6] = esp_renderer.config.flag_hostage;
        state.preview_skeleton = esp_renderer.config.draw_skeleton;
        state.preview_target = esp_renderer.config.draw_teammates ? 1 : 0;
    }

    void save_config( const menu_state& state, bool show_esp, const esp::renderer& esp_renderer ) {
        const std::string path = get_config_file_path( );

        std::string cfg;
        cfg.reserve(2048);
                cfg += "[Rage]\n";
        cfg += "SilentAim = " + std::to_string(state.silent_aim ? 1 : 0) + "\n";
        cfg += "AutoFire = " + std::to_string(state.auto_fire ? 1 : 0) + "\n";
        cfg += "AutoScope = " + std::to_string(state.auto_scope ? 1 : 0) + "\n";
        cfg += "Hitbox = " + std::to_string(state.hitbox) + "\n";
        cfg += "Multipoint = " + std::to_string(state.multipoint) + "\n";
        cfg += "HitChance = " + std::to_string(state.hit_chance) + "\n";
        cfg += "MinDamage = " + std::to_string(state.min_damage) + "\n";
        cfg += "Resolver = " + std::to_string(state.resolver ? 1 : 0) + "\n";
        cfg += "KnifeBot = " + std::to_string(state.knife_bot ? 1 : 0) + "\n";
        cfg += "Backtrack = " + std::to_string(state.backtrack ? 1 : 0) + "\n";
        cfg += "LagCompensation = " + std::to_string(state.lag_compensation ? 1 : 0) + "\n\n";

        cfg += "[Antiaim]\n";
        cfg += "Enabled = " + std::to_string(state.antiaim_enable ? 1 : 0) + "\n";
        cfg += "Pitch = " + std::to_string(state.aa_pitch) + "\n";
        cfg += "Yaw = " + std::to_string(state.aa_yaw) + "\n";
        cfg += "YawJitter = " + std::to_string(state.aa_yaw_jitter) + "\n";
        cfg += "Desync = " + std::to_string(state.aa_desync) + "\n\n";
cfg += "[Visuals]\n";
        cfg += "MasterEsp = " + std::to_string(show_esp ? 1 : 0) + "\n";
        cfg += "Box = " + std::to_string(esp_renderer.config.draw_box ? 1 : 0) + "\n";
        cfg += "Teammates = " + std::to_string(esp_renderer.config.draw_teammates ? 1 : 0) + "\n";
        cfg += "DeathmatchMode = " + std::to_string(esp_renderer.config.deathmatch_mode) + "\n";
        cfg += "Health = " + std::to_string(esp_renderer.config.draw_health ? 1 : 0) + "\n";
        cfg += "Armor = " + std::to_string(esp_renderer.config.draw_armor ? 1 : 0) + "\n";
        cfg += "Weapon = " + std::to_string(esp_renderer.config.draw_weapon ? 1 : 0) + "\n";
        cfg += "Tracers = " + std::to_string(esp_renderer.config.draw_tracers ? 1 : 0) + "\n";
        cfg += "Names = " + std::to_string(esp_renderer.config.draw_names ? 1 : 0) + "\n";
        cfg += "Distance = " + std::to_string(esp_renderer.config.draw_distance ? 1 : 0) + "\n";
        cfg += "HeadDot = " + std::to_string(esp_renderer.config.draw_head_dot ? 1 : 0) + "\n";
        cfg += "FovCircle = " + std::to_string(esp_renderer.config.draw_fov_circle ? 1 : 0) + "\n";
        cfg += "Skeleton = " + std::to_string(esp_renderer.config.draw_skeleton ? 1 : 0) + "\n";
        cfg += "OffscreenArrows = " + std::to_string(esp_renderer.config.draw_offscreen ? 1 : 0) + "\n";
        cfg += "BombTimer = " + std::to_string(esp_renderer.config.draw_bomb_timer ? 1 : 0) + "\n";
        cfg += "DroppedWeapons = " + std::to_string(esp_renderer.config.draw_dropped_weapons ? 1 : 0) + "\n";
        cfg += "Grenades = " + std::to_string(esp_renderer.config.draw_grenades ? 1 : 0) + "\n";
        cfg += "FovRadius = " + std::to_string(esp_renderer.config.fov_radius) + "\n";
        cfg += "MaxDistance = " + std::to_string(esp_renderer.config.max_distance) + "\n";
        cfg += "TracerOrigin = " + std::to_string(esp_renderer.config.tracer_origin) + "\n";
        cfg += "Watermark = " + std::to_string(state.show_status ? 1 : 0) + "\n\n";

        cfg += "[PlayerFlags]\n";
        cfg += "Bomb = " + std::to_string(esp_renderer.config.flag_bomb ? 1 : 0) + "\n";
        cfg += "Flashed = " + std::to_string(esp_renderer.config.flag_flashed ? 1 : 0) + "\n";
        cfg += "Scoped = " + std::to_string(esp_renderer.config.flag_scoped ? 1 : 0) + "\n";
        cfg += "Defusing = " + std::to_string(esp_renderer.config.flag_defusing ? 1 : 0) + "\n";
        cfg += "Hostage = " + std::to_string(esp_renderer.config.flag_hostage ? 1 : 0) + "\n\n";

        cfg += "[Style]\n";
        cfg += "BoxStyle = " + std::to_string(esp_renderer.config.box_style) + "\n";
        cfg += "EnemyColorIndex = " + std::to_string(state.enemy_color_index) + "\n";
        cfg += "TeammateColorIndex = " + std::to_string(state.teammate_color_index) + "\n\n";

        if ( g_aim ) {
            const auto& a = g_aim->config;
            cfg += "[Aim]\n";
            cfg += "Enabled = " + std::to_string( a.enabled ? 1 : 0 ) + "\n";
            cfg += "ActivationMode = " + std::to_string( a.activation_mode ) + "\n";
            cfg += "Hotkey = " + std::to_string( a.hotkey ) + "\n";
            cfg += "Fov = " + std::to_string( a.fov_pixels ) + "\n";
            cfg += "Smooth = " + std::to_string( a.smooth ) + "\n";
            cfg += "Bone = " + std::to_string( a.bone ) + "\n";
            cfg += "MaxDistance = " + std::to_string( a.max_distance_m ) + "\n";
            cfg += "Teammates = " + std::to_string( a.teammates ? 1 : 0 ) + "\n";
            cfg += "DrawFov = " + std::to_string( a.draw_fov ? 1 : 0 ) + "\n";
            cfg += "DrawTarget = " + std::to_string( a.draw_target ? 1 : 0 ) + "\n";
            cfg += "Humanize = " + std::to_string( a.humanize ? 1 : 0 ) + "\n";
            cfg += "Priority = " + std::to_string( a.priority ) + "\n";
            cfg += "VisibleOnly = " + std::to_string( a.visible_only ? 1 : 0 ) + "\n";
            cfg += "Prediction = " + std::to_string( a.prediction_ms ) + "\n";
            cfg += "StickyLock = " + std::to_string( a.sticky_lock ? 1 : 0 ) + "\n";
            cfg += "LockMs = " + std::to_string( a.lock_ms ) + "\n";
            cfg += "RingStyle = " + std::to_string( a.ring_style ) + "\n";
            cfg += "DrawPath = " + std::to_string( a.draw_path ? 1 : 0 ) + "\n";
            cfg += "DistanceSmoothing = " + std::to_string( a.distance_smoothing ? 1 : 0 ) + "\n\n";
            cfg += "RcsEnabled = " + std::to_string( a.rcs_enabled ? 1 : 0 ) + "\n";
            cfg += "RcsStrength = " + std::to_string( static_cast<int>( a.rcs_strength * 100.0f + 0.5f ) ) + "\n";
            cfg += "Bhop = " + std::to_string( a.bhop ? 1 : 0 ) + "\n";
            cfg += "Sensitivity = " + std::to_string( static_cast<int>( a.sensitivity * 100.0f + 0.5f ) ) + "\n\n";
        }

        // Preserved verbatim so a profile save never wipes a hand-corrected
        // offset table.
        {
            const std::string preserved_offsets = sdk::serialize_offset_overrides( path );
            if ( !preserved_offsets.empty( ) ) {
                cfg += "[Offsets]\n";
                cfg += preserved_offsets;
                cfg += "\n";
            }
        }

        cfg += "[Interface]\n";
        cfg += "PanelOpacity = " + std::to_string(state.panel_opacity) + "\n\n";
        cfg += "StreamProof = " + std::to_string( state.stream_proof ? 1 : 0 ) + "\n\n";

        cfg += "[InterfaceState]\n";
        cfg += "LegitSilent = " + std::to_string( state.legit_silent ? 1 : 0 ) + "\n";
        cfg += "LegitFriendly = " + std::to_string( state.legit_friendly ? 1 : 0 ) + "\n";
        cfg += "ScopeCheck = " + std::to_string( state.legit_scope_check ? 1 : 0 ) + "\n";
        cfg += "TriggerEnabled = " + std::to_string( state.trigger_enabled ? 1 : 0 ) + "\n";
        cfg += "TriggerDelay = " + std::to_string( state.trigger_delay ) + "\n";
        cfg += "TriggerHitchance = " + std::to_string( state.trigger_hitchance ) + "\n";
        cfg += "TriggerMagnet = " + std::to_string( state.trigger_magnet ? 1 : 0 ) + "\n";
        cfg += "EdgeJump = " + std::to_string( state.edge_jump ? 1 : 0 ) + "\n";
        cfg += "AutoStrafe = " + std::to_string( state.auto_strafe ? 1 : 0 ) + "\n";
        cfg += "FastStop = " + std::to_string( state.fast_stop ? 1 : 0 ) + "\n\n";

        if (state.pos_x >= 0 && state.pos_y >= 0) {
            cfg += "[Window]\n";
            cfg += "PosX = " + std::to_string(state.pos_x) + "\n";
            cfg += "PosY = " + std::to_string(state.pos_y) + "\n\n";
        }

        HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hFile, cfg.data(), static_cast<DWORD>(cfg.size()), &written, nullptr);
            CloseHandle(hFile);
        }

        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if ( GetFileAttributesExA( path.c_str( ), GetFileExInfoStandard, &fad ) ) {
            g_last_config_filetime = fad.ftLastWriteTime;
        }
    }

    menu_geometry get_menu_geometry(const menu_state& state, int sw, int sh) {
        menu_geometry g{};
        // TRAY V25 canvas: 940 x 610
        g.width = (std::min)(940, (std::max)(1, sw - 16));
        g.height = (std::min)(610, (std::max)(1, sh - 16));
        g.nav_width = 180;
        g.brand_width = 0;
        g.header_height = 56;

        g.left = state.pos_x >= 0
            ? (std::clamp)(state.pos_x, 8, (std::max)(8, sw - g.width - 8))
            : (sw - g.width) / 2;
        g.top = state.pos_y >= 0
            ? (std::clamp)(state.pos_y, 8, (std::max)(8, sh - g.height - 8))
            : (sh - g.height) / 2;

        g.cards_y = g.top + 64;
        g.col1_x = g.left + g.nav_width + 16;
        g.col1_w = (g.width - g.nav_width - 48) / 2; // ~348px
        g.col2_x = g.col1_x + g.col1_w + 16;
        g.col2_w = g.col1_w;
        g.cards_h = 250;
        return g;
    }

    void handle_menu_input(menu_state& st, HWND hwnd, bool& show_esp,
                           esp::renderer& er, int sw, int sh,
                           bool* p_menu_open = nullptr, forceinline::dx_overlay* p_overlay = nullptr) {
        POINT p{};
        if (GetCursorPos(&p) && ScreenToClient(hwnd, &p)) {
            st.mouse_x = p.x;
            st.mouse_y = p.y;
        } else {
            st.mouse_x = -1;
            st.mouse_y = -1;
        }

        const bool event_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool down = event_down;
        bool click = false;
        if (p_overlay && p_overlay->consume_primary_click(down)) {
            click = true;
        } else {
            click = event_down && !st.mouse_was_down;
            down = event_down;
        }
        st.mouse_was_down = down;

        const auto g = get_menu_geometry(st, sw, sh);
        const int L = g.left, T = g.top, W = g.width; [[maybe_unused]] const int H = g.height;
        const int CW = g.col1_w;
        const int X = g.col1_x, R = g.col2_x;
        const int Y1 = g.cards_y;
        const int Y2 = g.cards_y + g.cards_h + 14;

        auto hit = [&](int x, int y, int w, int h) {
            return point_in_rect(st.mouse_x, st.mouse_y, x, y, w, h);
        };

        auto save = [&](const char* reason) {
            save_config(st, show_esp, er);
            push_toast(st, reason, "Einstellung gespeichert", D3DCOLOR_RGBA(235, 42, 58, 255));
        };

        // Window dragging (Toolbar top)
        if (st.is_dragging) {
            if (down) {
                st.pos_x = (std::clamp)(st.drag_start_wx + st.mouse_x - st.drag_start_mx, 8, (std::max)(8, sw - g.width - 8));
                st.pos_y = (std::clamp)(st.drag_start_wy + st.mouse_y - st.drag_start_my, 8, (std::max)(8, sh - g.height - 8));
                return;
            }
            st.is_dragging = false;
            save_config(st, show_esp, er);
        }

        // Active slider dragging
        if (st.active_slider != -1) {
            if (down) {
                // Slider IDs
                if (st.active_slider == 10) { // Hit Chance
                    const int track_x = R + 16, track_w = CW - 32;
                    float u = (std::clamp)((float)(st.mouse_x - track_x) / (float)track_w, 0.f, 1.f);
                    st.hit_chance = (int)(u * 100.f);
                } else if (st.active_slider == 11) { // Min Damage
                    const int track_x = R + 16, track_w = CW - 32;
                    float u = (std::clamp)((float)(st.mouse_x - track_x) / (float)track_w, 0.f, 1.f);
                    st.min_damage = (std::max)(1, (int)(u * 100.f));
                } else if (st.active_slider == 12) { // Desync
                    const int track_x = X + 16, track_w = CW - 32;
                    float u = (std::clamp)((float)(st.mouse_x - track_x) / (float)track_w, 0.f, 1.f);
                    st.aa_desync = (int)(u * 100.f);
                } else if (st.active_slider == 13 && g_aim) { // Legit FOV
                    const int track_x = R + 16, track_w = CW - 32;
                    float u = (std::clamp)((float)(st.mouse_x - track_x) / (float)track_w, 0.f, 1.f);
                    g_aim->config.fov_pixels = (int)(10.f + u * 590.f);
                } else if (st.active_slider == 14 && g_aim) { // Legit Smooth
                    const int track_x = R + 16, track_w = CW - 32;
                    float u = (std::clamp)((float)(st.mouse_x - track_x) / (float)track_w, 0.f, 1.f);
                    g_aim->config.smooth = (int)(1.f + u * 19.f);
                } else if (st.active_slider == 15 && g_aim) { // Legit Distance
                    const int track_x = R + 16, track_w = CW - 32;
                    float u = (std::clamp)((float)(st.mouse_x - track_x) / (float)track_w, 0.f, 1.f);
                    g_aim->config.max_distance_m = (int)(u * 200.f);
                } else if (st.active_slider == 16) { // Menu Opacity
                    const int track_x = R + 16, track_w = CW - 32;
                    float u = (std::clamp)((float)(st.mouse_x - track_x) / (float)track_w, 0.f, 1.f);
                    st.panel_opacity = (int)(135.f + u * 110.f);
                } else if (st.active_slider == 17) { // Trigger Delay
                    const int track_x = X + 16, track_w = CW - 32;
                    float u = (std::clamp)((float)(st.mouse_x - track_x) / (float)track_w, 0.f, 1.f);
                    st.trigger_delay = (int)(u * 100.f);
                } else if (st.active_slider == 18) { // Trigger Hitchance
                    const int track_x = X + 16, track_w = CW - 32;
                    float u = (std::clamp)((float)(st.mouse_x - track_x) / (float)track_w, 0.f, 1.f);
                    st.trigger_hitchance = (int)(u * 100.f);
                } else if (st.active_slider == 19) { // RCS Strength
                    const int track_x = R + 16, track_w = CW - 32;
                    float u = (std::clamp)((float)(st.mouse_x - track_x) / (float)track_w, 0.f, 1.f);
                    st.rcs_strength_pct = (int)(u * 200.f);
                    if (g_aim) g_aim->config.rcs_strength = (float)st.rcs_strength_pct / 100.f;
                } else if (st.active_slider == 20) { // ESP Distance
                    const int track_x = R + 16, track_w = CW - 32;
                    float u = (std::clamp)((float)(st.mouse_x - track_x) / (float)track_w, 0.f, 1.f);
                    er.config.max_distance = (int)(25.f + u * 275.f);
                }
                return;
            }
            st.active_slider = -1;
            save_config(st, show_esp, er);
        }

        // Keybinding capture
        if (st.binding_slot != -1) {
            if (down) {
                st.mouse_was_down = true;
                return;
            }
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                st.binding_slot = -1;
                push_toast(st, "AUFNAHME ABGEBROCHEN", "Taste unveraendert", D3DCOLOR_RGBA(148, 163, 184, 255));
                return;
            }
            int captured = 0;
            for (int vk = 2; vk < 256; ++vk) {
                if (vk == VK_LBUTTON) continue;
                if (GetAsyncKeyState(vk) & 0x8000) { captured = vk; break; }
            }
            if (captured == 0) return;
            if (st.binding_slot == 0 && g_aim) {
                g_aim->config.hotkey = captured;
                save_config(st, show_esp, er);
                push_toast(st, "TRIGGER: " + key_label(captured), "Taste gebunden", D3DCOLOR_RGBA(235, 42, 58, 255));
            }
            st.binding_slot = -1;
            return;
        }

        if (!click) return;

        // Top-right window controls
        // Close button 'X'
        if (hit(L + W - 32, T + 18, 22, 22)) {
            if (p_menu_open) *p_menu_open = false;
            if (p_overlay) p_overlay->set_interactive(false);
            return;
        }
        // Minimize button '-'
        if (hit(L + W - 62, T + 18, 22, 22)) {
            if (p_menu_open) *p_menu_open = false;
            if (p_overlay) p_overlay->set_interactive(false);
            return;
        }
        // Settings gear button
        if (hit(L + W - 92, T + 18, 22, 22)) {
            st.page = 4; // Config
            return;
        }

        // Top Header Bar Horizontal Sub-Pills (3 Clean Pills matching reference)
        // Pill 0: Aimbot
        if (hit(L + 210, T + 18, 110, 32)) {
            st.page = 0;
            st.sub_tab = 0;
            return;
        }
        // Pill 1: Visuals
        if (hit(L + 332, T + 18, 110, 32)) {
            st.page = 2;
            st.sub_tab = 1;
            return;
        }
        // Pill 2: Misc
        if (hit(L + 454, T + 18, 100, 32)) {
            st.page = 3;
            st.sub_tab = 2;
            return;
        }

        // Left Sidebar Navigation Items (Vertical)
        // 0: Rage
        if (hit(L + 10, T + 76 + 0 * 46, 160, 38)) {
            st.page = 0;
            st.sub_tab = 0;
            return;
        }
        // 1: Legit
        if (hit(L + 10, T + 76 + 1 * 46, 160, 38)) {
            st.page = 1;
            return;
        }
        // 2: Visuals
        if (hit(L + 10, T + 76 + 2 * 46, 160, 38)) {
            st.page = 2;
            st.sub_tab = 1;
            return;
        }
        // 3: Misc
        if (hit(L + 10, T + 76 + 3 * 46, 160, 38)) {
            st.page = 3;
            st.sub_tab = 2;
            return;
        }
        // 4: Config
        if (hit(L + 10, T + 76 + 4 * 46, 160, 38)) {
            st.page = 4;
            return;
        }

        // Toolbar Dragging (Header bar)
        if (hit(L, T, W - 100, 56)) {
            st.is_dragging = true;
            st.drag_start_mx = st.mouse_x;
            st.drag_start_my = st.mouse_y;
            st.drag_start_wx = L;
            st.drag_start_wy = T;
            return;
        }

        // =====================================================================
        // PAGE 0: RAGE (1:1 with Screenshot)
        // =====================================================================
        if (st.page == 0) {
            // Card 1: Aimbot (Top-Left)
            // Enable
            if (hit(X + 12, Y1 + 42, CW - 24, 32)) {
                if (g_aim) g_aim->config.enabled = !g_aim->config.enabled;
                save(g_aim && g_aim->config.enabled ? "Aimbot aktiviert" : "Aimbot deaktiviert");
                return;
            }
            // Silent Aim
            if (hit(X + 12, Y1 + 82, CW - 24, 32)) {
                st.silent_aim = !st.silent_aim;
                save(st.silent_aim ? "Silent Aim an" : "Silent Aim aus");
                return;
            }
            // Auto Fire
            if (hit(X + 12, Y1 + 122, CW - 24, 32)) {
                st.auto_fire = !st.auto_fire;
                save(st.auto_fire ? "Auto Fire an" : "Auto Fire aus");
                return;
            }
            // Auto Scope
            if (hit(X + 12, Y1 + 162, CW - 24, 32)) {
                st.auto_scope = !st.auto_scope;
                save(st.auto_scope ? "Auto Scope an" : "Auto Scope aus");
                return;
            }

            // Card 2: Targeting (Top-Right)
            // Hitbox combo
            if (hit(R + CW - 130, Y1 + 46, 114, 26)) {
                st.hitbox = (st.hitbox + 1) % 4;
                if (g_aim) g_aim->config.bone = (std::min)(2, st.hitbox);
                save("Ziel-Hitbox geaendert");
                return;
            }
            // Multipoint combo
            if (hit(R + CW - 130, Y1 + 84, 114, 26)) {
                st.multipoint = (st.multipoint + 1) % 4;
                save("Multipoint geaendert");
                return;
            }
            // Hit Chance slider
            if (hit(R + 16, Y1 + 140, CW - 32, 22)) {
                st.active_slider = 10;
                return;
            }
            // Min. Damage slider
            if (hit(R + 16, Y1 + 188, CW - 32, 22)) {
                st.active_slider = 11;
                return;
            }

            // Card 3: Antiaim (Bottom-Left)
            // Enable
            if (hit(X + 12, Y2 + 38, CW - 24, 32)) {
                st.antiaim_enable = !st.antiaim_enable;
                save(st.antiaim_enable ? "Antiaim aktiv" : "Antiaim aus");
                return;
            }
            // Pitch combo
            if (hit(X + CW - 130, Y2 + 78, 114, 26)) {
                st.aa_pitch = (st.aa_pitch + 1) % 4;
                save("Pitch geaendert");
                return;
            }
            // Yaw combo
            if (hit(X + CW - 130, Y2 + 112, 114, 26)) {
                st.aa_yaw = (st.aa_yaw + 1) % 4;
                save("Yaw geaendert");
                return;
            }
            // Yaw Jitter combo
            if (hit(X + CW - 130, Y2 + 146, 114, 26)) {
                st.aa_yaw_jitter = (st.aa_yaw_jitter + 1) % 4;
                save("Yaw Jitter geaendert");
                return;
            }
            // Desync slider
            if (hit(X + 16, Y2 + 196, CW - 32, 22)) {
                st.active_slider = 12;
                return;
            }

            // Card 4: Other (Bottom-Right)
            // Resolver
            if (hit(R + 12, Y2 + 42, CW - 24, 32)) {
                st.resolver = !st.resolver;
                save(st.resolver ? "Resolver aktiv" : "Resolver aus");
                return;
            }
            // Knife Bot
            if (hit(R + 12, Y2 + 82, CW - 24, 32)) {
                st.knife_bot = !st.knife_bot;
                save(st.knife_bot ? "Knife Bot an" : "Knife Bot aus");
                return;
            }
            // Backtrack
            if (hit(R + 12, Y2 + 122, CW - 24, 32)) {
                st.backtrack = !st.backtrack;
                save(st.backtrack ? "Backtrack an" : "Backtrack aus");
                return;
            }
            // Lag Compensation
            if (hit(R + 12, Y2 + 162, CW - 24, 32)) {
                st.lag_compensation = !st.lag_compensation;
                save(st.lag_compensation ? "Lag Compensation an" : "Lag Compensation aus");
                return;
            }
            return;
        }

        // =====================================================================
        // PAGE 1: LEGIT
        // =====================================================================
        if (st.page == 1) {
            // Legit Aimbot
            if (hit(X + 12, Y1 + 42, CW - 24, 32)) {
                if (g_aim) g_aim->config.enabled = !g_aim->config.enabled;
                save(g_aim && g_aim->config.enabled ? "Legit Aim an" : "Legit Aim aus");
                return;
            }
            if (hit(X + 12, Y1 + 82, CW - 24, 32)) {
                st.legit_silent = !st.legit_silent;
                save("Silent Aim umgeschaltet");
                return;
            }
            if (hit(X + 12, Y1 + 122, CW - 24, 32)) {
                st.legit_friendly = !st.legit_friendly;
                if (g_aim) g_aim->config.teammates = st.legit_friendly;
                save("Friendly Fire umgeschaltet");
                return;
            }
            if (hit(X + 12, Y1 + 162, CW - 24, 32)) {
                st.legit_scope_check = !st.legit_scope_check;
                save("Scope Check umgeschaltet");
                return;
            }
            // Targeting
            if (hit(R + CW - 130, Y1 + 46, 114, 26)) {
                st.hitbox = (st.hitbox + 1) % 3;
                if (g_aim) g_aim->config.bone = st.hitbox;
                save("Bone gewaehlt");
                return;
            }
            if (hit(R + 16, Y1 + 94, CW - 32, 22)) { st.active_slider = 13; return; }
            if (hit(R + 16, Y1 + 144, CW - 32, 22)) { st.active_slider = 14; return; }
            if (hit(R + 16, Y1 + 194, CW - 32, 22)) { st.active_slider = 15; return; }

            // Triggerbot
            if (hit(X + 12, Y2 + 42, CW - 24, 32)) {
                st.trigger_enabled = !st.trigger_enabled;
                save(st.trigger_enabled ? "Triggerbot an" : "Triggerbot aus");
                return;
            }
            if (hit(X + 16, Y2 + 94, CW - 32, 22)) { st.active_slider = 17; return; }
            if (hit(X + 16, Y2 + 144, CW - 32, 22)) { st.active_slider = 18; return; }
            if (hit(X + 12, Y2 + 182, CW - 24, 32)) {
                st.trigger_magnet = !st.trigger_magnet;
                save("Magnet Aim umgeschaltet");
                return;
            }

            // Recoil
            if (hit(R + 12, Y2 + 42, CW - 24, 32)) {
                if (g_aim) g_aim->config.rcs_enabled = !g_aim->config.rcs_enabled;
                save("RCS umgeschaltet");
                return;
            }
            if (hit(R + 16, Y2 + 94, CW - 32, 22)) { st.active_slider = 19; return; }
            if (hit(R + 12, Y2 + 142, CW - 24, 32)) {
                if (g_aim) g_aim->config.bhop = !g_aim->config.bhop;
                save("Bunnyhop umgeschaltet");
                return;
            }
            return;
        }

        // =====================================================================
        // PAGE 2: VISUALS
        // =====================================================================
        if (st.page == 2) {
            // ESP Main
            if (hit(X + 12, Y1 + 42, CW - 24, 32)) {
                show_esp = !show_esp;
                er.config.enabled = show_esp;
                save(show_esp ? "ESP an" : "ESP aus");
                return;
            }
            if (hit(X + 12, Y1 + 82, CW - 24, 32)) {
                er.config.draw_box = !er.config.draw_box;
                save("Boxen umgeschaltet");
                return;
            }
            if (hit(X + 12, Y1 + 122, CW - 24, 32)) {
                er.config.draw_skeleton = !er.config.draw_skeleton;
                save("Skelett umgeschaltet");
                return;
            }
            if (hit(X + 12, Y1 + 162, CW - 24, 32)) {
                er.config.draw_health = !er.config.draw_health;
                save("Health Bar umgeschaltet");
                return;
            }

            // Player Info
            if (hit(R + 12, Y1 + 42, CW - 24, 32)) {
                er.config.draw_names = !er.config.draw_names;
                save("Namen umgeschaltet");
                return;
            }
            if (hit(R + 12, Y1 + 82, CW - 24, 32)) {
                er.config.draw_tracers = !er.config.draw_tracers;
                save("Snaplines umgeschaltet");
                return;
            }
            if (hit(R + 12, Y1 + 122, CW - 24, 32)) {
                er.config.draw_distance = !er.config.draw_distance;
                save("Distanz umgeschaltet");
                return;
            }
            if (hit(R + 16, Y1 + 188, CW - 32, 22)) {
                st.active_slider = 20;
                return;
            }

            // Chams & Models
            if (hit(X + 12, Y2 + 42, CW - 24, 32)) {
                st.preview_chams = !st.preview_chams;
                save("Chams umgeschaltet");
                return;
            }
            if (hit(X + 12, Y2 + 82, CW - 24, 32)) {
                er.config.draw_head_dot = !er.config.draw_head_dot;
                save("Kopfpunkt umgeschaltet");
                return;
            }
            if (hit(X + 12, Y2 + 122, CW - 24, 32)) {
                st.preview_glow = !st.preview_glow;
                save("Glow umgeschaltet");
                return;
            }
            if (hit(X + 12, Y2 + 162, CW - 24, 32)) {
                er.config.draw_offscreen = !er.config.draw_offscreen;
                save("Offscreen Arrows umgeschaltet");
                return;
            }

            // World & Filter
            if (hit(R + 12, Y2 + 42, CW - 24, 32)) {
                er.config.draw_teammates = !er.config.draw_teammates;
                save("Teammates umgeschaltet");
                return;
            }
            if (hit(R + 12, Y2 + 82, CW - 24, 32)) {
                er.config.draw_bomb_timer = !er.config.draw_bomb_timer;
                save("Bomb Timer umgeschaltet");
                return;
            }
            if (hit(R + 12, Y2 + 122, CW - 24, 32)) {
                er.config.draw_dropped_weapons = !er.config.draw_dropped_weapons;
                save("Dropped Weapons umgeschaltet");
                return;
            }
            if (hit(R + 12, Y2 + 162, CW - 24, 32)) {
                er.config.draw_grenades = !er.config.draw_grenades;
                save("Grenades umgeschaltet");
                return;
            }
            return;
        }

        // =====================================================================
        // PAGE 3: MISC
        // =====================================================================
        if (st.page == 3) {
            // Movement
            if (hit(X + 12, Y1 + 42, CW - 24, 32)) {
                if (g_aim) g_aim->config.bhop = !g_aim->config.bhop;
                save("Bunnyhop umgeschaltet");
                return;
            }
            if (hit(X + 12, Y1 + 82, CW - 24, 32)) {
                st.fast_stop = !st.fast_stop;
                save("Fast Stop umgeschaltet");
                return;
            }
            if (hit(X + 12, Y1 + 122, CW - 24, 32)) {
                st.edge_jump = !st.edge_jump;
                save("Edge Jump umgeschaltet");
                return;
            }
            if (hit(X + 12, Y1 + 162, CW - 24, 32)) {
                st.auto_strafe = !st.auto_strafe;
                save("Auto Strafe umgeschaltet");
                return;
            }

            // Overlay & Display
            if (hit(R + 16, Y1 + 68, CW - 32, 22)) {
                st.active_slider = 16;
                return;
            }
            if (hit(R + 12, Y1 + 102, CW - 24, 32)) {
                st.show_status = !st.show_status;
                save("Watermark umgeschaltet");
                return;
            }
            if (hit(R + 12, Y1 + 142, CW - 24, 32)) {
                st.stream_proof = !st.stream_proof;
                constexpr DWORD exclude_from_capture = 0x00000011;
                if ( !SetWindowDisplayAffinity( hwnd, st.stream_proof ? exclude_from_capture : WDA_NONE ) ) {
                    st.stream_proof = false;
                    push_toast( st, "STREAM PROOF", "Von dieser Windows-Version nicht unterstuetzt", D3DCOLOR_RGBA(245, 158, 11, 255) );
                    return;
                }
                save("Stream Proof umgeschaltet");
                return;
            }
            if (hit(R + 12, Y1 + 182, CW - 24, 32)) {
                if (g_aim) g_aim->config.draw_fov = !g_aim->config.draw_fov;
                save("FOV Ring umgeschaltet");
                return;
            }

            // Keybinds Trigger
            if (hit(R + CW - 130, Y2 + 84, 114, 26)) {
                st.binding_slot = 0;
                push_toast(st, "TASTE DRUECKEN", "Beliebige Taste fuer Aim druecken", D3DCOLOR_RGBA(245, 158, 11, 255));
                return;
            }
            return;
        }

        // =====================================================================
        // PAGE 4: CONFIG
        // =====================================================================
        if (st.page == 4) {
            // Save Button
            if (hit(R + 16, Y1 + 48, CW - 32, 36)) {
                save("Konfiguration manuell gespeichert");
                return;
            }
            // Reload Button
            if (hit(R + 16, Y1 + 96, CW - 32, 36)) {
                load_config(st, show_esp, er);
                push_toast(st, "CONFIG GELADEN", "Einstellungen wurden aktualisiert", D3DCOLOR_RGBA(59, 158, 255, 255));
                return;
            }
            // Reset Button
            if (hit(R + 16, Y1 + 144, CW - 32, 36)) {
                st.hit_chance = 75;
                st.min_damage = 25;
                st.aa_desync = 0;
                st.resolver = true;
                st.backtrack = true;
                st.lag_compensation = true;
                st.panel_opacity = 230;
                st.show_status = false;
                st.stream_proof = false;
                SetWindowDisplayAffinity( hwnd, WDA_NONE );
                save("Standardwerte wiederhergestellt");
                return;
            }
            // Local configuration tools. These replace the old non-functional
            // cloud/share placeholders with actions that are available offline.
            if (hit(X + 16, Y2 + 46, CW - 32, 36)) {
                const bool opened = open_config_directory( );
                push_toast(st, opened ? "CONFIG-ORDNER" : "FEHLER",
                    opened ? "Ordner wurde geoeffnet" : "Ordner konnte nicht geoeffnet werden",
                    opened ? D3DCOLOR_RGBA(34, 197, 94, 255) : D3DCOLOR_RGBA(235, 42, 58, 255));
                return;
            }
            if (hit(X + 16, Y2 + 94, CW - 32, 36)) {
                const bool copied = copy_text_to_clipboard( hwnd, get_config_file_path( ) );
                push_toast(st, copied ? "PFAD KOPIERT" : "FEHLER",
                    copied ? "Config-Pfad liegt in der Zwischenablage" : "Zwischenablage nicht verfuegbar",
                    copied ? D3DCOLOR_RGBA(34, 197, 94, 255) : D3DCOLOR_RGBA(235, 42, 58, 255));
                return;
            }
            return;
        }
    }

    // =========================================================================
    // TRAY V25 ENGINE - 1:1 RECREATION OF TRAY V25 INTERFACE
    // =========================================================================
    void draw_menu(forceinline::dx_renderer& renderer, const esp::renderer& er,
                   [[maybe_unused]] bool show_esp, menu_state& st, int sw, int sh) {
        st.page = (std::clamp)(st.page, 0, 4);
        const auto g = get_menu_geometry(st, sw, sh);
        const int L = g.left, T = g.top, W = g.width, H = g.height;
        const int CW = g.col1_w;
        const int X = g.col1_x, R = g.col2_x;
        const int Y1 = g.cards_y;
        const int Y2 = g.cards_y + g.cards_h + 14;

        auto C = [](int r, int gn, int b, int a = 255) -> unsigned long {
            return D3DCOLOR_RGBA(r, gn, b, a);
        };

        // Frame timing & persistent animation clock
        const auto frame_now = std::chrono::steady_clock::now();
        float frame_dt = 1.0f / 60.0f;
        if (st.ui_last_frame.time_since_epoch().count() != 0) {
            frame_dt = std::chrono::duration<float>(frame_now - st.ui_last_frame).count();
            frame_dt = (std::clamp)(frame_dt, 0.001f, 0.050f);
        }
        st.ui_last_frame = frame_now;
        static float s_anim_time = 0.0f;
        s_anim_time += frame_dt;

        const auto animate_to = [frame_dt](float& value, float target, float speed = 16.0f) {
            value += (target - value) * (std::min)(1.0f, frame_dt * speed);
        };

        const int alpha              = (std::clamp)(st.panel_opacity, 160, 255);
        const int card_alpha         = (std::min)(255, alpha + 15);

        // MEZA: deep midnight glass with a deliberate magenta/cyan contrast.
        const auto bg_win            = C(9, 10, 20, alpha);
        const auto bg_card           = C(17, 16, 31, card_alpha);
        const auto bg_control        = C(25, 22, 42, card_alpha);
        const auto border_win        = C(63, 38, 91, 190);
        const auto border_card       = C(52, 34, 76, 225);
        const auto border_control    = C(69, 47, 96, 205);

        const auto accent_red        = C(255, 42, 167, 255);            // MEZA pink
        const auto accent_bright     = C(255, 103, 205, 255);
        const auto accent_dark_red   = C(61, 13, 48, 240);
        const auto accent_green      = C(54, 224, 191, 255);            // MEZA cyan-mint

        const auto text_white        = C(245, 246, 250, 255);
        const auto text_gray         = C(155, 160, 175, 255);
        const auto text_dark         = C(95, 102, 118, 255);

        auto hit = [&](int x, int y, int w, int h) {
            return point_in_rect(st.mouse_x, st.mouse_y, x, y, w, h);
        };
        auto round = [&](int x, int y, int w, int h, int r, unsigned long col) {
            if (w > 2 && h > 2) renderer.draw_round_filled(x, y, w, h, r, col);
        };
        auto border = [&](int x, int y, int w, int h, int r, unsigned long fill, unsigned long edge) {
            round(x, y, w, h, r, edge);
            round(x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 1, fill);
        };
        auto line = [&](int x, int y, int xx, int yy, unsigned long col) {
            renderer.draw_line(x, y, xx, yy, col);
        };
        auto txt = [&](const char* t, int x, int y, unsigned long col, bool centered = false) {
            renderer.draw_text_small(t, x, y, col, centered, false);
        };
        auto label = [&](const char* t, int x, int y, unsigned long col, bool centered = false) {
            renderer.draw_text(t, x, y, col, centered, false);
        };

        // --- 1. Main Outer Window Shell (940 x 610, Radius 12) ---
        border(L, T, W, H, 12, bg_win, border_win);

        // Animated Top Border Light Sweep Beam
        {
            const float sweep_pct = fmodf(s_anim_time * 0.32f, 1.0f);
            const int sweep_x = L + 16 + (int)((W - 140) * sweep_pct);
            renderer.draw_filled_rect_gradient_h(sweep_x, T + 1, 50, 2, C(235, 42, 58, 20), C(255, 90, 105, 180));
            renderer.draw_filled_rect_gradient_h(sweep_x + 50, T + 1, 50, 2, C(255, 90, 105, 180), C(235, 42, 58, 20));
        }

        // Drifting Living Ambient Embers & Particle Field
        {
            for (int p = 0; p < 8; ++p) {
                const float p_spd = 0.35f + (p % 5) * 0.16f;
                const float p_phase = s_anim_time * p_spd + p * 3.47f;
                const float px = (float)L + 190.f + fmodf(p * 43.f + sinf(p_phase * 0.85f) * 36.f, (float)(W - 220));
                const float py = (float)(T + H - 20) - fmodf(p_phase * 32.f + p * 27.f, (float)(H - 50));
                const float p_pulse = (sinf(p_phase * 2.3f) * 0.5f + 0.5f);
                const int p_a = (int)(18 + 44 * p_pulse);
                const float p_r = 1.0f + (p % 3) * 0.75f;
                renderer.draw_filled_circle((int)px, (int)py, p_r, 8, C(235, 42, 58, p_a));
            }
        }

        // --- 2. Dynamic Undulating Fluid Red Ribbons in Bottom Left ---
        {
            const int base_x = L;
            const int base_y = T + H;
            for (int step = 0; step < 24; ++step) {
                float t1 = (float)step / 24.0f;
                float t2 = (float)(step + 1) / 24.0f;
                auto pt = [&](float t, float cx, float cy, float ex, float ey, float freq, float amp) {
                    const float bx = (float)base_x, by = (float)base_y;
                    const float wave = sinf(t * 3.14159f * freq + s_anim_time * 2.2f) * amp;
                    const float x = (1.f - t) * (1.f - t) * bx + 2.f * (1.f - t) * t * cx + t * t * ex;
                    const float y = (1.f - t) * (1.f - t) * (by - 20.f) + 2.f * (1.f - t) * t * (cy + wave) + t * t * ey;
                    return POINT{(int)x, (int)y};
                };
                // Ribbon 1
                POINT p1 = pt(t1, (float)(base_x + 60), (float)(base_y - 130), (float)(base_x + 195), (float)(base_y - 12), 1.8f, 9.f);
                POINT p2 = pt(t2, (float)(base_x + 60), (float)(base_y - 130), (float)(base_x + 195), (float)(base_y - 12), 1.8f, 9.f);
                const int a1 = (int)(110 + 25 * sinf(s_anim_time * 2.2f));
                line(p1.x, p1.y, p2.x, p2.y, C(220, 30, 48, a1));
                line(p1.x, p1.y + 1, p2.x, p2.y + 1, C(220, 30, 48, a1 / 2));

                // Ribbon 2
                p1 = pt(t1, (float)(base_x + 85), (float)(base_y - 95), (float)(base_x + 225), (float)(base_y - 45), 2.2f, 11.f);
                p2 = pt(t2, (float)(base_x + 85), (float)(base_y - 95), (float)(base_x + 225), (float)(base_y - 45), 2.2f, 11.f);
                const int a2 = (int)(135 + 35 * cosf(s_anim_time * 2.5f));
                line(p1.x, p1.y, p2.x, p2.y, C(245, 45, 65, a2));
                line(p1.x, p1.y + 1, p2.x, p2.y + 1, C(245, 45, 65, a2 / 2));

                // Ribbon 3
                p1 = pt(t1, (float)(base_x + 110), (float)(base_y - 65), (float)(base_x + 245), (float)(base_y - 75), 1.5f, 7.f);
                p2 = pt(t2, (float)(base_x + 110), (float)(base_y - 65), (float)(base_x + 245), (float)(base_y - 75), 1.5f, 7.f);
                const int a3 = (int)(90 + 25 * sinf(s_anim_time * 1.8f));
                line(p1.x, p1.y, p2.x, p2.y, C(210, 25, 40, a3));

                // Ribbon 4 - high sweep
                p1 = pt(t1, (float)(base_x + 35), (float)(base_y - 210), (float)(base_x + 175), (float)(base_y - 30), 2.0f, 12.f);
                p2 = pt(t2, (float)(base_x + 35), (float)(base_y - 210), (float)(base_x + 175), (float)(base_y - 30), 2.0f, 12.f);
                const int a4 = (int)(55 + 20 * sinf(s_anim_time * 3.f));
                line(p1.x, p1.y, p2.x, p2.y, C(180, 20, 35, a4));
            }
        }

        // --- 3. MEZA Brand Header ---
        {
            const int logox = L + 34, logoy = T + 28;
            const float pulse = sinf(s_anim_time * 3.2f);
            const int aura_alpha = (int)(42 + 24 * pulse);
            renderer.draw_filled_circle(logox, logoy, 20.f + 2.f * pulse, 18, C(255, 42, 167, aura_alpha));
            renderer.draw_meza_logo(L + 10, T + 5, 48, 48);
            const float t_shimmer = sinf(s_anim_time * 3.4f);
            const int white_shimmer = (int)(235 + 20 * t_shimmer);
            renderer.draw_text_title("MEZA", L + 59, T + 17, C(255, 42, 167, (int)(110 + 60 * t_shimmer)), false, false);
            renderer.draw_text_title("MEZA", L + 58, T + 16, C(255, white_shimmer, white_shimmer, 255), false, true);
        }

        // --- 4. Left Sidebar Navigation Items (Smooth Animated Rail & Hover) ---
        // Smooth sliding active container
        const float target_rail_y = (float)(T + 76 + st.page * 46);
        if (st.ui_rail_y < 0.f) st.ui_rail_y = target_rail_y;
        else animate_to(st.ui_rail_y, target_rail_y, 18.0f);

        const int ry_pos = (int)st.ui_rail_y;
        round(L + 9, ry_pos - 1, 162, 40, 9, C(229, 42, 58, (int)(45 + 15 * sinf(s_anim_time * 3.f))));
        border(L + 10, ry_pos, 160, 38, 8, accent_dark_red, accent_red);

        auto draw_sidebar_nav_item = [&](int id, const char* name, int page_id, int icon_type) {
            const int px = L + 10, py = T + 76 + id * 46, pw = 160, ph = 38;
            const bool is_selected = (st.page == page_id);
            const bool is_hov = hit(px, py, pw, ph);

            animate_to(st.ui_nav_motion[id], (is_hov && !is_selected) ? 1.0f : 0.0f, 16.0f);
            const float hov_m = st.ui_nav_motion[id];
            if (hov_m > 0.01f) {
                round(px, py, pw, ph, 8, C(26, 29, 39, (int)(210 * hov_m)));
            }

            const unsigned long icon_col = is_selected ? accent_bright : (is_hov ? text_white : text_gray);
            const unsigned long label_col = is_selected ? text_white : (is_hov ? text_white : text_gray);

            const int icx = px + 18, icy = py + 19;
            if (icon_type == 0) { // Crosshair (Rage / Legit)
                renderer.draw_circle(icx, icy, 6, 14, icon_col);
                line(icx - 9, icy, icx - 4, icy, icon_col);
                line(icx + 4, icy, icx + 9, icy, icon_col);
                line(icx, icy - 9, icx, icy - 4, icon_col);
                line(icx, icy + 4, icx, icy + 9, icon_col);
            } else if (icon_type == 1) { // Eye (Visuals)
                line(icx - 8, icy, icx, icy - 5, icon_col);
                line(icx, icy - 5, icx + 8, icy, icon_col);
                line(icx - 8, icy, icx, icy + 5, icon_col);
                line(icx, icy + 5, icx + 8, icy, icon_col);
                renderer.draw_filled_circle(icx, icy, 2.5f, 10, icon_col);
            } else if (icon_type == 2) { // 4-Squares Grid (Misc)
                round(icx - 7, icy - 7, 5, 5, 1, icon_col);
                round(icx + 2, icy - 7, 5, 5, 1, icon_col);
                round(icx - 7, icy + 2, 5, 5, 1, icon_col);
                round(icx + 2, icy + 2, 5, 5, 1, icon_col);
            } else { // Config / Floppy
                border(icx - 7, icy - 7, 14, 14, 2, bg_control, icon_col);
                round(icx - 4, icy - 5, 8, 4, 1, icon_col);
                line(icx - 4, icy + 2, icx + 4, icy + 2, icon_col);
            }

            if (is_selected) {
                // Neon vertical left edge indicator with fluid glow
                line(px + 2, py + 8, px + 2, py + 30, accent_bright);
                line(px + 3, py + 10, px + 3, py + 28, C(255, 120, 140, 220));
            }

            label(name, px + 36, py + 10, label_col);
        };

        draw_sidebar_nav_item(0, "Control", 0, 0);
        draw_sidebar_nav_item(1, "Assist", 1, 0);
        draw_sidebar_nav_item(2, "Visuals", 2, 1);
        draw_sidebar_nav_item(3, "System", 3, 2);
        draw_sidebar_nav_item(4, "Profiles", 4, 3);

        // --- 5. Animated MEZA brand mark & profile ---
        {
            const int mascot_w = 120;
            const int mascot_h = 120;
            const int mascot_cx = L + g.nav_width / 2;
            const float bob = sinf(s_anim_time * 2.8f) * 4.0f;
            const int mascot_cy = T + 395 + (int)bob;
            const int mascot_x = mascot_cx - mascot_w / 2;
            const int mascot_y = mascot_cy - mascot_h / 2;

            // Breathing glowing circular aura ring (subtle, seamless)
            const float pulse = sinf(s_anim_time * 3.2f);
            const int aura_r = 46 + (int)(3.0f * pulse);
            renderer.draw_filled_circle(mascot_cx, mascot_cy, (float)aura_r, 24, C(255, 42, 167, (int)(32 + 18 * pulse)));
            renderer.draw_circle(mascot_cx, mascot_cy, aura_r + 2, 24, C(255, 103, 205, (int)(65 + 25 * pulse)));

            // 3 Spinning orbital neon sparkles
            const float orbit_angle = s_anim_time * 2.0f;
            for (int k = 0; k < 3; ++k) {
                float ang = orbit_angle + k * (2.0f * 3.14159f / 3.0f);
                int ox = mascot_cx + (int)((aura_r + 4) * cosf(ang));
                int oy = mascot_cy + (int)((aura_r + 4) * sinf(ang));
                renderer.draw_filled_circle(ox, oy, 2.0f, 8, C(105, 228, 255, 230));
            }

            // Soft drop shadow underneath
            renderer.draw_filled_circle(mascot_cx, mascot_cy + 42, 32.0f, 16, C(0, 0, 0, 110));

            renderer.draw_meza_logo(mascot_x, mascot_y, mascot_w, mascot_h);

            // Sleek, minimal Profile Pill under mascot: [ ● tugra  VIP ]
            const int pill_w = 110, pill_h = 22;
            const int pill_x = mascot_cx - pill_w / 2, pill_y = mascot_cy + 64;
            border(pill_x, pill_y, pill_w, pill_h, 6, C(18, 21, 30, 220), C(45, 50, 68, 200));
            // Pulsing green online dot
            const int dot_a = (int)(180 + 75 * sinf(s_anim_time * 4.2f));
            renderer.draw_filled_circle(pill_x + 12, pill_y + 11, 2.5f, 8, C(34, 197, 94, dot_a));
            std::string auth_user = meza_public_auth::get_user_name_utf8();
            if (auth_user.empty()) auth_user = "tugra";
            renderer.draw_text_small(auth_user.c_str(), pill_x + 20, pill_y + 4, text_white, false, false);
            border(pill_x + pill_w - 32, pill_y + 3, 26, 16, 4, C(58, 18, 53, 220), accent_red);
            renderer.draw_text_small("ME", pill_x + pill_w - 19, pill_y + 4, accent_bright, true, false);
        }

        // --- 6. Bottom Status Bar ---
        renderer.draw_filled_circle(L + 24, T + H - 24, 3.5f, 12, accent_green);
        txt("CS2", L + 34, T + H - 30, text_white, false);
        txt("MEZA", L + 96, T + H - 30, text_dark, false);

        // --- 7. Top Header Sub-Pills (Horizontal) ---
        auto draw_top_pill = [&](int px, int py, int pw, int ph, const char* name, bool active, int icon_type) {
            const bool is_hov = hit(px, py, pw, ph);
            if (active) {
                round(px - 1, py - 1, pw + 2, ph + 2, 11, C(229, 42, 58, (int)(55 + 20 * sinf(s_anim_time * 3.2f))));
                border(px, py, pw, ph, 10, accent_dark_red, accent_red);
            } else {
                round(px, py, pw, ph, 10, is_hov ? C(26, 29, 40, 200) : C(18, 20, 28, 140));
            }
            const unsigned long icol = active ? accent_bright : (is_hov ? text_white : text_gray);
            const unsigned long tcol = active ? text_white : (is_hov ? text_white : text_gray);

            const int icx = px + 18, icy = py + 16;
            if (icon_type == 0) { // Crosshair
                renderer.draw_circle(icx, icy, 5, 12, icol);
                line(icx - 7, icy, icx - 3, icy, icol);
                line(icx + 3, icy, icx + 7, icy, icol);
                line(icx, icy - 7, icx, icy - 3, icol);
                line(icx, icy + 3, icx, icy + 7, icol);
            } else if (icon_type == 1) { // Eye
                line(icx - 6, icy, icx, icy - 4, icol);
                line(icx, icy - 4, icx + 6, icy, icol);
                line(icx - 6, icy, icx, icy + 4, icol);
                line(icx, icy + 4, icx + 6, icy, icol);
                renderer.draw_filled_circle(icx, icy, 2.0f, 8, icol);
            } else { // Grid
                round(icx - 5, icy - 5, 4, 4, 1, icol);
                round(icx + 2, icy - 5, 4, 4, 1, icol);
                round(icx - 5, icy + 2, 4, 4, 1, icol);
                round(icx + 2, icy + 2, 4, 4, 1, icol);
            }
            label(name, px + 32, py + 7, tcol);
        };

        draw_top_pill(L + 210, T + 18, 110, 32, "Aimbot", st.page == 0 || st.page == 1, 0);
        draw_top_pill(L + 332, T + 18, 110, 32, "Visuals", st.page == 2, 1);
        draw_top_pill(L + 454, T + 18, 100, 32, "Misc", st.page >= 3, 2);

        const char* page_titles[] = { "RAGE CONTROL", "LEGIT CONTROL", "VISUALS", "SYSTEM", "CONFIGURATION" };
        const char* page_subtitles[] = { "Experimental UI", "Input & recoil", "Overlay renderer", "Status & shortcuts", "Local profile" };
        txt(page_titles[st.page], L + 580, T + 15, text_white);
        txt(page_subtitles[st.page], L + 580, T + 32, text_dark);

        // --- 8. Top Right Window Controls ---
        // Gear
        const int gx = L + W - 92, gy = T + 18;
        const bool ghov = hit(gx, gy, 24, 24);
        if (ghov) round(gx, gy, 24, 24, 4, C(28, 31, 42, 200));
        renderer.draw_circle(gx + 12, gy + 12, 5, 12, ghov ? accent_bright : text_gray);
        line(gx + 12, gy + 4, gx + 12, gy + 6, ghov ? accent_bright : text_gray);
        line(gx + 12, gy + 18, gx + 12, gy + 20, ghov ? accent_bright : text_gray);
        line(gx + 4, gy + 12, gx + 6, gy + 12, ghov ? accent_bright : text_gray);
        line(gx + 18, gy + 12, gx + 20, gy + 12, ghov ? accent_bright : text_gray);

        // Minimize '-'
        const int mx = L + W - 62, my = T + 18;
        const bool mhov = hit(mx, my, 24, 24);
        if (mhov) round(mx, my, 24, 24, 4, C(28, 31, 42, 200));
        line(mx + 6, my + 13, mx + 18, my + 13, mhov ? text_white : text_gray);

        // Close 'X'
        const int cx = L + W - 32, cy = T + 18;
        const bool chov = hit(cx, cy, 24, 24);
        if (chov) round(cx, cy, 24, 24, 4, C(227, 52, 69, 90));
        line(cx + 6, cy + 6, cx + 18, cy + 18, chov ? accent_bright : text_gray);
        line(cx + 18, cy + 6, cx + 6, cy + 18, chov ? accent_bright : text_gray);

        // --- 9. Card & Widget Drawing Primitives (Smooth Animated) ---
        auto draw_tray_card = [&](int card_x, int card_y, int card_w, int card_h, int header_icon, const char* header_title) {
            const bool chov = hit(card_x, card_y, card_w, card_h);
            const unsigned long c_border = chov ? C(235, 42, 58, (int)(140 + 40 * sinf(s_anim_time * 3.5f))) : border_card;
            border(card_x, card_y, card_w, card_h, 10, bg_card, c_border);
            // Subtle header glass tint
            renderer.draw_filled_rect_gradient_h(card_x + 1, card_y + 1, card_w - 2, 32, C(32, 16, 22, 120), C(17, 19, 27, 40));
            // Card top laser beam on hover
            if (chov) {
                const float sw_t = fmodf(s_anim_time * 1.6f, 1.0f);
                const int sw_x = card_x + 12 + (int)((card_w - 74) * sw_t);
                renderer.draw_filled_rect_gradient_h(sw_x, card_y + 1, 50, 2, C(235, 42, 58, 20), C(255, 90, 105, 200));
            }
            line(card_x + 12, card_y + 34, card_x + card_w - 12, card_y + 34, chov ? C(235, 42, 58, 140) : C(30, 34, 46, 180));
            const int icx = card_x + 18, icy = card_y + 18;
            if (header_icon == 0) { // Crosshair (Aimbot)
                renderer.draw_circle(icx, icy, 5, 12, accent_red);
                line(icx - 8, icy, icx - 3, icy, accent_red);
                line(icx + 3, icy, icx + 8, icy, accent_red);
                line(icx, icy - 8, icx, icy - 3, accent_red);
                line(icx, icy + 3, icx, icy + 8, accent_red);
            } else if (header_icon == 1) { // User silhouette (Targeting)
                renderer.draw_circle(icx, icy - 3, 3, 10, accent_red);
                line(icx - 6, icy + 6, icx + 6, icy + 6, accent_red);
                line(icx - 6, icy + 6, icx - 3, icy + 1, accent_red);
                line(icx + 6, icy + 6, icx + 3, icy + 1, accent_red);
            } else if (header_icon == 2) { // Crossed swords / X (Antiaim)
                line(icx - 6, icy - 6, icx + 6, icy + 6, accent_red);
                line(icx + 6, icy - 6, icx - 6, icy + 6, accent_red);
                renderer.draw_circle(icx, icy, 2, 8, accent_bright);
            } else if (header_icon == 3) { // Gear (Other)
                renderer.draw_circle(icx, icy, 4, 10, accent_red);
                line(icx, icy - 6, icx, icy + 6, accent_red);
                line(icx - 6, icy, icx + 6, icy, accent_red);
            } else if (header_icon == 4) { // Eye
                line(icx - 6, icy, icx, icy - 4, accent_red);
                line(icx, icy - 4, icx + 6, icy, accent_red);
                line(icx - 6, icy, icx, icy + 4, accent_red);
                line(icx, icy + 4, icx + 6, icy, accent_red);
                renderer.draw_filled_circle(icx, icy, 2.0f, 8, accent_red);
            } else { // Grid
                round(icx - 5, icy - 5, 4, 4, 1, accent_red);
                round(icx + 2, icy - 5, 4, 4, 1, accent_red);
                round(icx - 5, icy + 2, 4, 4, 1, accent_red);
                round(icx + 2, icy + 2, 4, 4, 1, accent_red);
            }
            label(header_title, card_x + 34, card_y + 10, text_white);
        };

        // Smooth Sliding Toggle Switch with Fluid Knob Easing
        auto draw_tray_toggle = [&](int id, int tx, int ty, bool enabled, bool hovered) {
            const int tw = 36, th = 20;
            animate_to(st.ui_toggle_motion[id % 32], enabled ? 1.0f : 0.0f, 18.0f);
            const float m = st.ui_toggle_motion[id % 32];

            // Smooth track color transition
            const int tr = (int)(38 + (235 - 38) * m);
            const int tg = (int)(42 + (42 - 42) * m);
            const int tb = (int)(54 + (58 - 54) * m);
            round(tx, ty, tw, th, 10, C(tr, tg, tb, 255));

            if (hovered || m > 0.05f) {
                const int ga = (int)((hovered ? 75 : 45) * m);
                if (ga > 0) renderer.draw_circle(tx + 11 + (int)(14.0f * m), ty + 10, 8, 14, C(235, 42, 58, ga));
            }

            // Smooth gliding knob
            const int kx = tx + 11 + (int)(14.0f * m);
            const int kr = (int)(130 + (255 - 130) * m);
            const int kg = (int)(136 + (255 - 136) * m);
            const int kb = (int)(150 + (255 - 150) * m);
            renderer.draw_filled_circle(kx, ty + 10, 6.5f, 14, C(kr, kg, kb, 255));
        };

        auto draw_tray_combo = [&](int bx, int by, const char* text_val, bool hovered) {
            const int bw = 114, bh = 26;
            border(bx, by, bw, bh, 5, bg_control, hovered ? C(55, 60, 80, 255) : border_control);
            label(text_val, bx + 12, by + 4, text_white);
            line(bx + bw - 18, by + 11, bx + bw - 14, by + 15, text_gray);
            line(bx + bw - 14, by + 15, bx + bw - 10, by + 11, text_gray);
        };

        auto draw_tray_slider = [&](int sx, int sy, int sw, const char* label_text, const char* val_text, float pct, bool is_active) {
            label(label_text, sx, sy, text_white);
            const RECT dim = renderer.get_text_dimensions(val_text);
            const int val_w = dim.right - dim.left;
            label(val_text, sx + sw - val_w, sy, text_white);

            const int track_y = sy + 22;
            round(sx, track_y, sw, 3, 1, C(38, 42, 54, 255));
            const int fill_w = (int)(sw * (std::clamp)(pct, 0.0f, 1.0f));
            if (fill_w > 0) {
                round(sx, track_y, fill_w, 3, 1, accent_red);
            }
            const int thumb_x = sx + fill_w;
            const float thumb_r = is_active ? 6.5f : 5.5f;
            renderer.draw_filled_circle(thumb_x, track_y + 1, thumb_r, 14, C(255, 255, 255, 255));

            const int glow_a = is_active ? (int)(160 + 40 * sinf(s_anim_time * 8.f)) : 100;
            renderer.draw_circle(thumb_x, track_y + 1, is_active ? 8 : 7, 14, C(235, 42, 58, glow_a));
        };

        // =====================================================================
        // PAGE 0: RAGE (1:1 with Screenshot)
        // =====================================================================
        if (st.page == 0) {
            // --- Card 1: Top-Left -> "Aimbot" ---
            draw_tray_card(X, Y1, CW, g.cards_h, 0, "Aimbot");
            label("Enable", X + 16, Y1 + 48, text_white);
            draw_tray_toggle(0, X + CW - 52, Y1 + 48, g_aim ? g_aim->config.enabled : true, hit(X + CW - 52, Y1 + 48, 36, 20));

            label("Silent Aim", X + 16, Y1 + 88, text_white);
            draw_tray_toggle(1, X + CW - 52, Y1 + 88, st.silent_aim, hit(X + CW - 52, Y1 + 88, 36, 20));

            label("Auto Fire", X + 16, Y1 + 128, text_white);
            draw_tray_toggle(2, X + CW - 52, Y1 + 128, st.auto_fire, hit(X + CW - 52, Y1 + 128, 36, 20));

            label("Auto Scope", X + 16, Y1 + 168, text_white);
            draw_tray_toggle(3, X + CW - 52, Y1 + 168, st.auto_scope, hit(X + CW - 52, Y1 + 168, 36, 20));

            // --- Card 2: Top-Right -> "Targeting" ---
            draw_tray_card(R, Y1, CW, g.cards_h, 1, "Targeting");
            label("Hitbox", R + 16, Y1 + 48, text_white);
            const char* hitbox_names[] = { "Head", "Neck", "Chest", "Pelvis" };
            draw_tray_combo(R + CW - 130, Y1 + 46, hitbox_names[st.hitbox % 4], hit(R + CW - 130, Y1 + 46, 114, 26));

            label("Multipoint", R + 16, Y1 + 86, text_white);
            const char* multi_names[] = { "Off", "Low", "Medium", "High" };
            draw_tray_combo(R + CW - 130, Y1 + 84, multi_names[st.multipoint % 4], hit(R + CW - 130, Y1 + 84, 114, 26));

            char hc_buf[16];
            std::snprintf(hc_buf, sizeof(hc_buf), "%d%%", st.hit_chance);
            draw_tray_slider(R + 16, Y1 + 124, CW - 32, "Hit Chance", hc_buf, (float)st.hit_chance / 100.f, st.active_slider == 10);

            char md_buf[16];
            std::snprintf(md_buf, sizeof(md_buf), "%d", st.min_damage);
            draw_tray_slider(R + 16, Y1 + 172, CW - 32, "Min. Damage", md_buf, (float)st.min_damage / 100.f, st.active_slider == 11);

            // --- Card 3: Bottom-Left -> "Antiaim" ---
            draw_tray_card(X, Y2, CW, g.cards_h, 2, "Antiaim");
            label("Enable", X + 16, Y2 + 44, text_white);
            draw_tray_toggle(4, X + CW - 52, Y2 + 44, st.antiaim_enable, hit(X + CW - 52, Y2 + 44, 36, 20));

            label("Pitch", X + 16, Y2 + 80, text_white);
            const char* pitch_names[] = { "Off", "Down", "Up", "Zero" };
            draw_tray_combo(X + CW - 130, Y2 + 78, pitch_names[st.aa_pitch % 4], hit(X + CW - 130, Y2 + 78, 114, 26));

            label("Yaw", X + 16, Y2 + 114, text_white);
            const char* yaw_names[] = { "Off", "Backward", "Spin", "Jitter" };
            draw_tray_combo(X + CW - 130, Y2 + 112, yaw_names[st.aa_yaw % 4], hit(X + CW - 130, Y2 + 112, 114, 26));

            label("Yaw Jitter", X + 16, Y2 + 148, text_white);
            const char* jitter_names[] = { "Off", "Offset", "Center", "Random" };
            draw_tray_combo(X + CW - 130, Y2 + 146, jitter_names[st.aa_yaw_jitter % 4], hit(X + CW - 130, Y2 + 146, 114, 26));

            char ds_buf[16];
            std::snprintf(ds_buf, sizeof(ds_buf), "%d%%", st.aa_desync);
            draw_tray_slider(X + 16, Y2 + 180, CW - 32, "Desync", ds_buf, (float)st.aa_desync / 100.f, st.active_slider == 12);

            // --- Card 4: Bottom-Right -> "Other" ---
            draw_tray_card(R, Y2, CW, g.cards_h, 3, "Other");
            label("Resolver", R + 16, Y2 + 48, text_white);
            draw_tray_toggle(5, R + CW - 52, Y2 + 48, st.resolver, hit(R + CW - 52, Y2 + 48, 36, 20));

            label("Knife Bot", R + 16, Y2 + 88, text_white);
            draw_tray_toggle(6, R + CW - 52, Y2 + 88, st.knife_bot, hit(R + CW - 52, Y2 + 88, 36, 20));

            label("Backtrack", R + 16, Y2 + 128, text_white);
            draw_tray_toggle(7, R + CW - 52, Y2 + 128, st.backtrack, hit(R + CW - 52, Y2 + 128, 36, 20));

            label("Lag Compensation", R + 16, Y2 + 168, text_white);
            draw_tray_toggle(8, R + CW - 52, Y2 + 168, st.lag_compensation, hit(R + CW - 52, Y2 + 168, 36, 20));
        }

        // =====================================================================
        // PAGE 1: LEGIT
        // =====================================================================
        else if (st.page == 1) {
            // Card 1: Legit Aimbot
            draw_tray_card(X, Y1, CW, g.cards_h, 0, "Legit Aimbot");
            label("Enable", X + 16, Y1 + 48, text_white);
            draw_tray_toggle(9, X + CW - 52, Y1 + 48, g_aim ? g_aim->config.enabled : false, hit(X + CW - 52, Y1 + 48, 36, 20));
            label("Silent Aim", X + 16, Y1 + 88, text_white);
            draw_tray_toggle(10, X + CW - 52, Y1 + 88, st.legit_silent, hit(X + CW - 52, Y1 + 88, 36, 20));
            label("Friendly Fire", X + 16, Y1 + 128, text_white);
            draw_tray_toggle(11, X + CW - 52, Y1 + 128, st.legit_friendly, hit(X + CW - 52, Y1 + 128, 36, 20));
            label("Scope Check", X + 16, Y1 + 168, text_white);
            draw_tray_toggle(12, X + CW - 52, Y1 + 168, st.legit_scope_check, hit(X + CW - 52, Y1 + 168, 36, 20));

            // Card 2: Targeting & FOV
            draw_tray_card(R, Y1, CW, g.cards_h, 1, "Targeting & FOV");
            label("Hitbox", R + 16, Y1 + 46, text_white);
            const char* l_bones[] = { "Head", "Neck", "Chest" };
            draw_tray_combo(R + CW - 130, Y1 + 44, l_bones[st.hitbox % 3], hit(R + CW - 130, Y1 + 44, 114, 26));

            char fov_str[16];
            std::snprintf(fov_str, sizeof(fov_str), "%dpx", g_aim ? g_aim->config.fov_pixels : 140);
            const float fov_p = g_aim ? (float)(g_aim->config.fov_pixels - 10) / 590.f : 0.25f;
            draw_tray_slider(R + 16, Y1 + 82, CW - 32, "Field of View", fov_str, fov_p, st.active_slider == 13);

            char sm_str[16];
            std::snprintf(sm_str, sizeof(sm_str), "%d", g_aim ? g_aim->config.smooth : 4);
            const float sm_p = g_aim ? (float)(g_aim->config.smooth - 1) / 19.f : 0.2f;
            draw_tray_slider(R + 16, Y1 + 130, CW - 32, "Smoothing", sm_str, sm_p, st.active_slider == 14);

            char dst_str[16];
            std::snprintf(dst_str, sizeof(dst_str), "%dm", g_aim ? g_aim->config.max_distance_m : 80);
            const float dst_p = g_aim ? (float)g_aim->config.max_distance_m / 200.f : 0.4f;
            draw_tray_slider(R + 16, Y1 + 178, CW - 32, "Max Distance", dst_str, dst_p, st.active_slider == 15);

            // Card 3: Triggerbot
            draw_tray_card(X, Y2, CW, g.cards_h, 2, "Triggerbot (Preview)");
            label("Enable", X + 16, Y2 + 48, text_white);
            draw_tray_toggle(13, X + CW - 52, Y2 + 48, st.trigger_enabled, hit(X + CW - 52, Y2 + 48, 36, 20));

            char del_str[16];
            std::snprintf(del_str, sizeof(del_str), "%dms", st.trigger_delay);
            draw_tray_slider(X + 16, Y2 + 82, CW - 32, "Delay", del_str, (float)st.trigger_delay / 100.f, st.active_slider == 17);

            char th_str[16];
            std::snprintf(th_str, sizeof(th_str), "%d%%", st.trigger_hitchance);
            draw_tray_slider(X + 16, Y2 + 130, CW - 32, "Hit Chance", th_str, (float)st.trigger_hitchance / 100.f, st.active_slider == 18);

            label("Magnet Aim", X + 16, Y2 + 188, text_white);
            draw_tray_toggle(14, X + CW - 52, Y2 + 188, st.trigger_magnet, hit(X + CW - 52, Y2 + 188, 36, 20));

            // Card 4: Recoil & Movement
            draw_tray_card(R, Y2, CW, g.cards_h, 3, "Recoil & Movement");
            label("Recoil Control (RCS)", R + 16, Y2 + 48, text_white);
            draw_tray_toggle(15, R + CW - 52, Y2 + 48, g_aim ? g_aim->config.rcs_enabled : true, hit(R + CW - 52, Y2 + 48, 36, 20));

            char rcs_str[16];
            std::snprintf(rcs_str, sizeof(rcs_str), "%d%%", st.rcs_strength_pct);
            draw_tray_slider(R + 16, Y2 + 82, CW - 32, "RCS Strength", rcs_str, (float)st.rcs_strength_pct / 200.f, st.active_slider == 19);

            label("Auto Bunnyhop", R + 16, Y2 + 148, text_white);
            draw_tray_toggle(16, R + CW - 52, Y2 + 148, g_aim ? g_aim->config.bhop : true, hit(R + CW - 52, Y2 + 148, 36, 20));
        }

        // =====================================================================
        // PAGE 2: VISUALS
        // =====================================================================
        else if (st.page == 2) {
            // Card 1: ESP Main
            draw_tray_card(X, Y1, CW, g.cards_h, 4, "ESP Main");
            label("Enable ESP", X + 16, Y1 + 48, text_white);
            draw_tray_toggle(17, X + CW - 52, Y1 + 48, show_esp, hit(X + CW - 52, Y1 + 48, 36, 20));
            label("Bounding Box", X + 16, Y1 + 88, text_white);
            draw_tray_toggle(18, X + CW - 52, Y1 + 88, er.config.draw_box, hit(X + CW - 52, Y1 + 88, 36, 20));
            label("Skeleton Bones", X + 16, Y1 + 128, text_white);
            draw_tray_toggle(19, X + CW - 52, Y1 + 128, er.config.draw_skeleton, hit(X + CW - 52, Y1 + 128, 36, 20));
            label("Health Bar", X + 16, Y1 + 168, text_white);
            draw_tray_toggle(20, X + CW - 52, Y1 + 168, er.config.draw_health, hit(X + CW - 52, Y1 + 168, 36, 20));

            // Card 2: Player Info
            draw_tray_card(R, Y1, CW, g.cards_h, 1, "Player Info");
            label("Player Names", R + 16, Y1 + 48, text_white);
            draw_tray_toggle(21, R + CW - 52, Y1 + 48, er.config.draw_names, hit(R + CW - 52, Y1 + 48, 36, 20));
            label("Snaplines", R + 16, Y1 + 88, text_white);
            draw_tray_toggle(22, R + CW - 52, Y1 + 88, er.config.draw_tracers, hit(R + CW - 52, Y1 + 88, 36, 20));
            label("Distance In Meters", R + 16, Y1 + 128, text_white);
            draw_tray_toggle(23, R + CW - 52, Y1 + 128, er.config.draw_distance, hit(R + CW - 52, Y1 + 128, 36, 20));

            char dist_buf[16];
            std::snprintf(dist_buf, sizeof(dist_buf), "%dm", er.config.max_distance);
            draw_tray_slider(R + 16, Y1 + 172, CW - 32, "Render Distance", dist_buf, (float)(er.config.max_distance - 25) / 275.f, st.active_slider == 20);

            // Card 3: Chams & Models
            draw_tray_card(X, Y2, CW, g.cards_h, 0, "Chams & Models");
            label("Enemy Chams", X + 16, Y2 + 48, text_white);
            draw_tray_toggle(24, X + CW - 52, Y2 + 48, st.preview_chams, hit(X + CW - 52, Y2 + 48, 36, 20));
            label("Head Dot", X + 16, Y2 + 88, text_white);
            draw_tray_toggle(25, X + CW - 52, Y2 + 88, er.config.draw_head_dot, hit(X + CW - 52, Y2 + 88, 36, 20));
            label("Glow Outline", X + 16, Y2 + 128, text_white);
            draw_tray_toggle(26, X + CW - 52, Y2 + 128, st.preview_glow, hit(X + CW - 52, Y2 + 128, 36, 20));
            label("Offscreen Arrows", X + 16, Y2 + 168, text_white);
            draw_tray_toggle(27, X + CW - 52, Y2 + 168, er.config.draw_offscreen, hit(X + CW - 52, Y2 + 168, 36, 20));

            // Card 4: World & Filter
            draw_tray_card(R, Y2, CW, g.cards_h, 5, "World & Filter");
            label("Teammates Filter", R + 16, Y2 + 48, text_white);
            draw_tray_toggle(28, R + CW - 52, Y2 + 48, er.config.draw_teammates, hit(R + CW - 52, Y2 + 48, 36, 20));
            label("Bomb / C4 Timer", R + 16, Y2 + 88, text_white);
            draw_tray_toggle(29, R + CW - 52, Y2 + 88, er.config.draw_bomb_timer, hit(R + CW - 52, Y2 + 88, 36, 20));
            label("Dropped Weapons", R + 16, Y2 + 128, text_white);
            draw_tray_toggle(30, R + CW - 52, Y2 + 128, er.config.draw_dropped_weapons, hit(R + CW - 52, Y2 + 128, 36, 20));
            label("Grenades ESP", R + 16, Y2 + 168, text_white);
            draw_tray_toggle(31, R + CW - 52, Y2 + 168, er.config.draw_grenades, hit(R + CW - 52, Y2 + 168, 36, 20));
        }

        // =====================================================================
        // PAGE 3: MISC
        // =====================================================================
        else if (st.page == 3) {
            // Card 1: Movement
            draw_tray_card(X, Y1, CW, g.cards_h, 5, "Movement (Preview)");
            label("Auto Bunnyhop", X + 16, Y1 + 48, text_white);
            draw_tray_toggle(0, X + CW - 52, Y1 + 48, g_aim ? g_aim->config.bhop : true, hit(X + CW - 52, Y1 + 48, 36, 20));
            label("Fast Stop", X + 16, Y1 + 88, text_white);
            draw_tray_toggle(1, X + CW - 52, Y1 + 88, st.fast_stop, hit(X + 12, Y1 + 82, CW - 24, 32));
            label("Edge Jump", X + 16, Y1 + 128, text_white);
            draw_tray_toggle(2, X + CW - 52, Y1 + 128, st.edge_jump, hit(X + CW - 52, Y1 + 128, 36, 20));
            label("Auto Strafe", X + 16, Y1 + 168, text_white);
            draw_tray_toggle(3, X + CW - 52, Y1 + 168, st.auto_strafe, hit(X + CW - 52, Y1 + 168, 36, 20));

            // Card 2: Overlay & Display
            draw_tray_card(R, Y1, CW, g.cards_h, 3, "Overlay & Display");
            char op_buf[16];
            std::snprintf(op_buf, sizeof(op_buf), "%d%%", (int)((st.panel_opacity / 255.f) * 100.f));
            draw_tray_slider(R + 16, Y1 + 48, CW - 32, "Menu Opacity", op_buf, (float)(st.panel_opacity - 135) / 110.f, st.active_slider == 16);

            label("Watermark HUD", R + 16, Y1 + 108, text_white);
            draw_tray_toggle(4, R + CW - 52, Y1 + 108, st.show_status, hit(R + CW - 52, Y1 + 108, 36, 20));

            label("Stream Proof", R + 16, Y1 + 148, text_white);
            draw_tray_toggle(5, R + CW - 52, Y1 + 148, st.stream_proof, hit(R + CW - 52, Y1 + 148, 36, 20));

            label("Draw FOV Ring", R + 16, Y1 + 188, text_white);
            draw_tray_toggle(6, R + CW - 52, Y1 + 188, g_aim ? g_aim->config.draw_fov : true, hit(R + CW - 52, Y1 + 188, 36, 20));

            // Card 3: Diagnostics
            draw_tray_card(X, Y2, CW, g.cards_h, 0, "Diagnostics");
            const auto& runtime_stats = er.stats( );
            const bool engine_ready = runtime_stats.entity_list != 0;
            const bool player_ready = runtime_stats.local_pawn != 0;
            label("CS2 Engine Status:", X + 16, Y2 + 48, text_gray);
            txt(engine_ready ? "Connected" : "Waiting", X + CW - 96, Y2 + 50, engine_ready ? accent_green : accent_bright);

            label("Player Data:", X + 16, Y2 + 88, text_gray);
            txt(player_ready ? "Ready" : "Waiting", X + CW - 80, Y2 + 90, player_ready ? accent_green : accent_bright);

            label("Compositor:", X + 16, Y2 + 128, text_gray);
            txt("DirectX 9 / Active", X + CW - 128, Y2 + 130, text_white);

            char entity_status[48]{};
            std::snprintf(entity_status, sizeof(entity_status), "%d players / %d drawn", runtime_stats.pawns, runtime_stats.drawn);
            label("Renderer:", X + 16, Y2 + 168, text_gray);
            txt(entity_status, X + CW - 132, Y2 + 170, runtime_stats.drawn > 0 ? accent_green : text_white);

            // Card 4: Keybinds
            draw_tray_card(R, Y2, CW, g.cards_h, 1, "Keybinds");
            label("Menu Toggle Key", R + 16, Y2 + 48, text_white);
            draw_tray_combo(R + CW - 130, Y2 + 46, "INSERT", false);

            label("Aim Keybind", R + 16, Y2 + 88, text_white);
            const std::string akey = g_aim ? key_label(g_aim->config.hotkey) : "MOUSE5";
            draw_tray_combo(R + CW - 130, Y2 + 86, akey.c_str(), hit(R + CW - 130, Y2 + 86, 114, 26));

            label("Panic Key", R + 16, Y2 + 128, text_white);
            draw_tray_combo(R + CW - 130, Y2 + 126, "END", false);

            label("Quick Toggle", R + 16, Y2 + 168, text_white);
            draw_tray_combo(R + CW - 130, Y2 + 166, "F11", false);
        }

        // =====================================================================
        // PAGE 4: CONFIG (ELITE DASHBOARD)
        // =====================================================================
        else if (st.page == 4) {
            // Card 1: Preset Profiles
            draw_tray_card(X, Y1, CW, g.cards_h, 5, "Preset Configurations");
            const char* presets[] = { "Current Local Profile", "Visual Settings", "Input Settings", "Window & Interface" };
            for (int i = 0; i < 4; ++i) {
                const int py = Y1 + 44 + i * 46;
                const unsigned long r_edge = (i == 0) ? accent_green : border_control;
                const unsigned long r_bg = bg_control;
                border(X + 16, py, CW - 32, 38, 6, r_bg, r_edge);

                // Small icon on left (disk/settings)
                renderer.draw_circle(X + 32, py + 19, 4, 10, i == 0 ? accent_green : text_gray);
                label(presets[i], X + 44, py + 9, text_white);

                // Honest local-state badge; the former LOAD buttons had no action.
                if (i == 0) {
                    border(X + CW - 92, py + 8, 66, 22, 4, C(20, 48, 30, 240), accent_green);
                    const int dot_a = (int)(180 + 75 * sinf(s_anim_time * 4.2f));
                    renderer.draw_filled_circle(X + CW - 82, py + 19, 2.5f, 8, C(34, 197, 94, dot_a));
                    txt("ACTIVE", X + CW - 74, py + 12, accent_green, false);
                } else {
                    border(X + CW - 76, py + 8, 50, 22, 4, C(20, 23, 32, 220), border_control);
                    txt("SAVED", X + CW - 51, py + 12, text_gray, true);
                }
            }

            // Card 2: Configuration Storage
            draw_tray_card(R, Y1, CW, g.cards_h, 3, "Configuration Storage");
            // Save button: Full filled Crimson Gradient CTA
            const bool save_hov = hit(R + 16, Y1 + 46, CW - 32, 38);
            renderer.draw_filled_rect_gradient_h(R + 16, Y1 + 46, CW - 32, 38,
                save_hov ? C(255, 55, 75, 255) : C(235, 42, 58, 255),
                save_hov ? C(200, 25, 42, 255) : C(170, 18, 32, 255));
            border(R + 16, Y1 + 46, CW - 32, 38, 6, 0, save_hov ? C(255, 120, 140, 255) : accent_bright);
            // Save icon
            renderer.draw_circle(R + 36, Y1 + 65, 4, 10, text_white);
            label("Save Active Configuration", R + CW / 2 + 6, Y1 + 55, text_white, true);

            // Reload button: Sleek dark glass button
            const bool rel_hov = hit(R + 16, Y1 + 96, CW - 32, 36);
            border(R + 16, Y1 + 96, CW - 32, 36, 6, rel_hov ? C(30, 34, 48, 240) : bg_control, rel_hov ? accent_bright : border_control);
            label("Reload Configuration", R + CW / 2, Y1 + 104, text_white, true);

            // Reset button: Warning outline button
            const bool res_hov = hit(R + 16, Y1 + 144, CW - 32, 36);
            border(R + 16, Y1 + 144, CW - 32, 36, 6, res_hov ? C(45, 18, 22, 220) : bg_control, res_hov ? C(235, 42, 58, 220) : border_control);
            label("Reset Factory Defaults", R + CW / 2, Y1 + 152, res_hov ? accent_bright : text_gray, true);

            // Card 3: Cloud & Share
            draw_tray_card(X, Y2, CW, g.cards_h, 1, "Local Configuration Tools");
            const bool exp_hov = hit(X + 16, Y2 + 46, CW - 32, 36);
            border(X + 16, Y2 + 46, CW - 32, 36, 6, exp_hov ? C(28, 32, 46, 240) : bg_control, exp_hov ? accent_bright : border_control);
            label("Open Configuration Folder", X + CW / 2, Y2 + 54, text_white, true);

            const bool imp_hov = hit(X + 16, Y2 + 94, CW - 32, 36);
            border(X + 16, Y2 + 94, CW - 32, 36, 6, imp_hov ? C(28, 32, 46, 240) : bg_control, imp_hov ? accent_bright : border_control);
            label("Copy Configuration Path", X + CW / 2, Y2 + 102, text_white, true);

            label("Automatic Local Save", X + 16, Y2 + 154, text_white);
            txt("Every change is written to config.ini", X + 16, Y2 + 174, accent_green);
            draw_tray_toggle(0, X + CW - 52, Y2 + 154, true, false);

            // Card 4: TRAY Systems Engine & Telemetry
            draw_tray_card(R, Y2, CW, g.cards_h, 0, "TRAY Systems Engine & Telemetry");
            auto draw_telemetry_chip = [&](int row, const char* name, const char* val, unsigned long vcol) {
                const int cy = Y2 + 44 + row * 40;
                round(R + 16, cy, CW - 32, 32, 6, C(18, 21, 30, 220));
                line(R + 24, cy + 32, R + CW - 24, cy + 32, C(28, 32, 45, 160));
                label(name, R + 26, cy + 6, text_gray);
                const RECT vr = renderer.get_text_dimensions(val);
                const int vw = vr.right - vr.left;
                // Value badge
                border(R + CW - 34 - vw, cy + 5, vw + 16, 22, 4, C(24, 28, 40, 230), vcol);
                txt(val, R + CW - 26 - vw, cy + 8, vcol);
            };

            const auto& config_stats = er.stats( );
            draw_telemetry_chip(0, "Core Version:", "TRAY v2.5", text_white);
            draw_telemetry_chip(1, "Architecture:", "x64 Native", accent_bright);
            draw_telemetry_chip(2, "Runtime:", config_stats.entity_list ? "Connected" : "Waiting for game", config_stats.entity_list ? accent_green : accent_bright);
            std::string lic_name = meza_public_auth::get_user_name_utf8();
            if (lic_name.empty()) lic_name = "Tugra";
            std::string lic_badge = lic_name + " (Lifetime VIP)";
            draw_telemetry_chip(3, "Licensee:", lic_badge.c_str(), C(255, 215, 0, 255));
        }

        // Custom Cursor
        renderer.draw_custom_cursor(st.mouse_x, st.mouse_y, accent_red);
    }

    void draw_toast_notification( forceinline::dx_renderer& renderer, menu_state& state, int screen_width ) {
        if ( state.toast.remaining_ticks <= 0 || state.toast.max_ticks <= 0 )
            return;

        const int elapsed = state.toast.max_ticks - state.toast.remaining_ticks;
        const int fade_ticks = 12;
        float opacity = 1.0f;
        if ( elapsed < fade_ticks )
            opacity = static_cast<float>( elapsed + 1 ) / static_cast<float>( fade_ticks );
        else if ( state.toast.remaining_ticks < fade_ticks )
            opacity = static_cast<float>( state.toast.remaining_ticks ) / static_cast<float>( fade_ticks );

        const int alpha = (std::clamp)( static_cast<int>( opacity * 245.0f ), 0, 245 );
        const int width = 330;
        const int height = 70;
        const int x = (std::max)( 18, screen_width - width - 24 );
        const int y = 24;
        const auto color = []( int r, int g, int b, int a ) { return D3DCOLOR_RGBA( r, g, b, a ); };

        renderer.draw_round_filled( x, y, width, height, 9, color( 8, 10, 16, alpha ) );
        renderer.draw_round_filled( x + 1, y + 1, 4, height - 2, 2, state.toast.accent );
        renderer.draw_line( x + 16, y + 39, x + width - 16, y + 39, color( 45, 49, 64, alpha ) );
        renderer.draw_text( state.toast.title.c_str( ), x + 18, y + 12, color( 247, 248, 252, alpha ), false, true );
        renderer.draw_text_small( state.toast.message.c_str( ), x + 18, y + 46, color( 168, 173, 188, alpha ), false, false );

        --state.toast.remaining_ticks;
    }

    void draw_status_watermark( forceinline::dx_renderer& renderer, const esp::renderer& esp_renderer ) {
        const auto& stats = esp_renderer.stats( );
        const bool connected = stats.entity_list != 0;
        const auto color = []( int r, int g, int b, int a = 255 ) { return D3DCOLOR_RGBA( r, g, b, a ); };
        const int x = 22;
        const int y = 22;
        const int width = 222;
        const int height = 38;

        renderer.draw_round_filled( x, y, width, height, 8, color( 9, 11, 17, 225 ) );
        renderer.draw_round_filled( x + 1, y + 1, 3, height - 2, 2, connected ? color( 34, 197, 94 ) : color( 245, 158, 11 ) );
        renderer.draw_filled_circle( x + 18, y + 19, 3.0f, 10, connected ? color( 34, 197, 94 ) : color( 245, 158, 11 ) );
        renderer.draw_text_small( "MEZA", x + 30, y + 8, color( 248, 249, 252 ), false, true );

        char status[64]{};
        if ( connected )
            std::snprintf( status, sizeof( status ), "%d players  /  %d rendered", stats.pawns, stats.drawn );
        else
            std::snprintf( status, sizeof( status ), "Waiting for game data" );
        renderer.draw_text_small( status, x + 30, y + 22, color( 150, 156, 172 ), false, false );
    }

    bool is_process_elevated() {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;
        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof(TOKEN_ELEVATION);
        bool elevated = false;
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
            elevated = (elevation.TokenIsElevated != 0);
        }
        CloseHandle(token);
        return elevated;
    }

    bool relaunch_as_administrator() {
        wchar_t exe_path[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0)
            return false;

        PCWSTR args = PathGetArgsW(GetCommandLineW());

        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.lpVerb = L"runas";
        sei.lpFile = exe_path;
        sei.lpParameters = (args && *args) ? args : nullptr;
        sei.nShow = SW_SHOWNORMAL;

        if (ShellExecuteExW(&sei)) {
            return true;
        }
        return false;
    }

    bool enable_debug_privilege() {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
            return false;

        LUID luid{};
        if (!LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
            CloseHandle(token);
            return false;
        }

        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
        const bool success = (GetLastError() == ERROR_SUCCESS);
        CloseHandle(token);
        return success;
    }

} // anonymous namespace

int WINAPI wWinMain( HINSTANCE, HINSTANCE, PWSTR cmd_line, int ) {
    try {
        // Ensure running as Administrator to allow opening elevated CS2 process handles
        if (!is_process_elevated()) {
            if (relaunch_as_administrator()) {
                return 0; // elevated instance spawned; terminate un-elevated instance
            }
        }
        enable_debug_privilege();
        // Accurate 60/30 Hz pacing. Without this Windows can round a 16 ms
        // sleep up to the next coarse timer tick, producing visible stutter.
        const scoped_timer_resolution timer_resolution{};
        // Maintain GDI+ runtime active throughout the entire application process
        glassui::runtime gdiplus_session{};
        // Enable DPI awareness for sharp fonts and accurate overlay coordinates
        typedef BOOL( WINAPI* PFN_SetProcessDpiAwarenessContext )( DPI_AWARENESS_CONTEXT );
        if ( HMODULE user32_dll = GetModuleHandleW( L"user32.dll" ) ) {
            if ( auto pfn_set_dpi = reinterpret_cast< PFN_SetProcessDpiAwarenessContext >(
                    GetProcAddress( user32_dll, "SetProcessDpiAwarenessContext" ) ) ) {
                pfn_set_dpi( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
            } else {
                SetProcessDPIAware();
            }
        }

        const HANDLE mutex_handle = CreateMutexW( nullptr, FALSE,
            L"Local\\CS2ExternalEspOverlay.SingleInstance" );
        const DWORD mutex_error = GetLastError( );
        if ( !mutex_handle ) {
            show_error( L"Die Einzelinstanz-Sperre konnte nicht erstellt werden." );
            return 1;
        }
        const scoped_handle instance_mutex( mutex_handle );

        const HANDLE show_event_handle = CreateEventW( nullptr, FALSE, FALSE,
            L"Local\\CS2ExternalEspOverlay.ShowMenu" );
        if ( !show_event_handle ) {
            show_error( L"Das Menue-Signal konnte nicht erstellt werden." );
            return 1;
        }
        const scoped_handle show_menu_event( show_event_handle );

        if ( mutex_error == ERROR_ALREADY_EXISTS ) {
            SetEvent( show_event_handle );
            if ( const HWND existing = FindWindowW( L"forceinline::dx_overlay", nullptr ) ) {
                DWORD_PTR res = 0;
                SendMessageTimeoutW( existing, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 300, &res );
                ShowWindow( existing, SW_SHOW );
                SetForegroundWindow( existing );
            }
            return 0;
        }

        const bool no_menu = ( cmd_line && wcsstr( cmd_line, L"--no-menu" ) != nullptr );
        // Authenticate only once. Recoverable errors can retry starting the UI
        // without forcing the user to reopen the entire application.
        if (!meza_public_auth::authorize(GetModuleHandleW(nullptr))) return 1;
        for (;;) {
            HWND game_window = wait_for_game_window();
            if (!game_window) return 0;
            if (!IsWindow(game_window)) {
                if (MessageBoxW(nullptr,
                    L"The game window closed before MEZA could start.\n"
                    L"Search again?",
                    L"MEZA / Startup", MB_RETRYCANCEL | MB_ICONWARNING | MB_TOPMOST) == IDRETRY)
                    continue;
                return 1;
            }
            try {
        HWND target_window = game_window;
        DWORD target_process_id = 0;
        GetWindowThreadProcessId( target_window, &target_process_id );

        // Use the PID of the already verified CS2 window. Re-enumerating the
        // global process list here races with Steam/CS2 startup and can select
        // the wrong process or report "not found" even with a valid window.
        if (!target_process_id)
            throw std::runtime_error("CS2 window has no process ID. Please retry.");
        forceinline::memory_manager memory(target_process_id);
        std::uintptr_t client_base = 0;
        // The game window can appear before client.dll is loaded; wait for it
        // rather than failing instantly. Do not keep reading if CS2 exits.
        for (int attempt = 0; attempt < 32 && !client_base; ++attempt) {
            if (!memory.is_attached() || !IsWindow(target_window))
                throw std::runtime_error("CS2 closed while client.dll was initializing. Retry after game restart.");
            try {
                client_base = memory.try_get_module_base("client.dll");
                if (!client_base) {
                    if (attempt == 31)
                        throw std::runtime_error("client.dll not available in CS2. Wait for the main menu and retry.");
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
            } catch (const std::exception&) {
                if (attempt == 31)
                    throw std::runtime_error("client.dll not available in CS2. Wait for the main menu and retry.");
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        }
        if (!client_base)
            throw std::runtime_error("client.dll base address is zero. Overlay was not started.");

        forceinline::dx_overlay overlay( target_window, false );
        auto dx = overlay.create_renderer( );
        esp::renderer esp_renderer( memory, client_base );
        aim::aimbot aim_controller( memory, client_base );
        // The menu and the config writer reach the aimbot through this pointer.
        g_aim = &aim_controller;

        bool running = true;
        bool show_esp = true;
        // A successful launch must produce visible feedback. Headless startup is
        // still available explicitly for dashboard/automation callers.
        bool menu_open = !no_menu;
        auto last_config_check = std::chrono::steady_clock::now();
        auto last_process_check = std::chrono::steady_clock::now();
        auto last_diagnostics_write = std::chrono::steady_clock::time_point{};
        bool connection_lost = false;
        bool empty_overlay_cleared = false;
        menu_state menu{};
        MSG message{};
        load_config( menu, show_esp, esp_renderer );
        if ( menu.stream_proof ) {
            constexpr DWORD exclude_from_capture = 0x00000011;
            if ( !SetWindowDisplayAffinity( overlay.get_overlay_wnd( ), exclude_from_capture ) )
                menu.stream_proof = false;
        }
        auto last_dashboard_config_refresh = std::chrono::steady_clock::now( );

        while ( running ) {
            const auto frame_start = std::chrono::steady_clock::now( );
            if ( no_menu && frame_start - last_dashboard_config_refresh >= std::chrono::milliseconds( 500 ) ) {
                load_config( menu, show_esp, esp_renderer );
                last_dashboard_config_refresh = frame_start;
            }
            while ( PeekMessageW( &message, nullptr, 0, 0, PM_REMOVE ) ) {
                if ( message.message == WM_QUIT ) {
                    running = false;
                    break;
                }
                TranslateMessage( &message );
                DispatchMessageW( &message );
            }

            // Edge-triggered hotkeys (prevents missed inputs from & 1 being cleared by game)
            static bool s_end_down = false;
            const bool k_end = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
            if (k_end && !s_end_down) { running = false; break; }
            s_end_down = k_end;

            if ( !no_menu && WaitForSingleObject( show_event_handle, 0 ) == WAIT_OBJECT_0 ) {
                menu_open = true;
                menu.mouse_was_down = false;
                overlay.set_interactive( true );
            }

            // Menu toggle keys need independent edge states. A single combined
            // state gets stuck whenever one alternate key (notably Right Shift)
            // remains held and then blocks INSERT completely.
            static bool s_insert_down = false;
            static bool s_delete_down = false;
            static bool s_home_down = false;
            static bool s_f11_down = false;
            auto key_pressed = []( int vk, bool& was_down ) {
                const bool is_down = ( GetAsyncKeyState( vk ) & 0x8000 ) != 0;
                const bool pressed = is_down && !was_down;
                was_down = is_down;
                return pressed;
            };
            bool menu_toggle_pressed = key_pressed( VK_INSERT, s_insert_down );
            menu_toggle_pressed = key_pressed( VK_DELETE, s_delete_down ) || menu_toggle_pressed;
            menu_toggle_pressed = key_pressed( VK_HOME, s_home_down ) || menu_toggle_pressed;
            menu_toggle_pressed = key_pressed( VK_F11, s_f11_down ) || menu_toggle_pressed;
            if (!no_menu && menu_toggle_pressed && menu.binding_slot == -1) {
                menu_open = !menu_open;
                menu.mouse_was_down = ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) != 0;
                overlay.set_interactive( menu_open );
                MessageBeep( MB_OK );
            }

            // ESC closes menu if open â€” but while a key is being captured it
            // cancels the capture instead, otherwise ESC would bind nothing and
            // close the whole menu in one keystroke.
            static bool s_esc_down = false;
            const bool k_esc = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
            if (menu_open && k_esc && !s_esc_down && menu.binding_slot == -1) {
                menu_open = false;
                menu.mouse_was_down = false;
                overlay.set_interactive( false );
                MessageBeep( MB_OK );
            }
            s_esc_down = k_esc;

            static bool s_f1_down = false;
            const bool k_f1 = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
            if (k_f1 && !s_f1_down) {
                show_esp = !show_esp;
                MessageBeep( MB_OK );
                push_toast( menu, "MASTER ESP", show_esp ? "ESP Visuals aktiviert" : "ESP Visuals pausiert",
                    show_esp ? D3DCOLOR_RGBA( 133, 224, 38, 255 ) : D3DCOLOR_RGBA( 255, 90, 90, 255 ) );
                save_config( menu, show_esp, esp_renderer );
            }
            s_f1_down = k_f1;

            static bool s_f2_down = false;
            const bool k_f2 = ((GetAsyncKeyState(VK_F2) & 0x8000) != 0) || ((GetAsyncKeyState(VK_F5) & 0x8000) != 0);
            if (k_f2 && !s_f2_down) {
                esp_renderer.config.draw_teammates = !esp_renderer.config.draw_teammates;
                MessageBeep( MB_OK );
                push_toast( menu, "TEAMMATES", esp_renderer.config.draw_teammates ? "Sichtbar" : "Ausgeblendet",
                    D3DCOLOR_RGBA( 59, 158, 255, 255 ) );
                save_config( menu, show_esp, esp_renderer );
            }
            s_f2_down = k_f2;

            static bool s_f3_down = false;
            const bool k_f3 = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
            if (k_f3 && !s_f3_down) {
                esp_renderer.config.draw_health = !esp_renderer.config.draw_health;
                MessageBeep( MB_OK );
                push_toast( menu, "HEALTH BAR", esp_renderer.config.draw_health ? "Aktiviert" : "Ausgeblendet",
                    D3DCOLOR_RGBA( 45, 230, 115, 255 ) );
                save_config( menu, show_esp, esp_renderer );
            }
            s_f3_down = k_f3;

            static bool s_f4_down = false;
            const bool k_f4 = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
            if (k_f4 && !s_f4_down) {
                esp_renderer.config.draw_tracers = !esp_renderer.config.draw_tracers;
                MessageBeep( MB_OK );
                push_toast( menu, "SNAPLINES", esp_renderer.config.draw_tracers ? "Aktiviert" : "Ausgeblendet",
                    D3DCOLOR_RGBA( 0, 245, 212, 255 ) );
                save_config( menu, show_esp, esp_renderer );
            }
            s_f4_down = k_f4;

            static bool s_f6_down = false;
            const bool k_f6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
            if (k_f6 && !s_f6_down) {
                esp_renderer.config.deathmatch_mode = ( esp_renderer.config.deathmatch_mode + 1 ) % 3;
                MessageBeep( MB_OK );
                const char* dm_toast = "AUTO (Automatisch)";
                if ( esp_renderer.config.deathmatch_mode == 0 ) dm_toast = "AUS (Normales Team-Spiel)";
                else if ( esp_renderer.config.deathmatch_mode == 1 ) dm_toast = "AN (Alle sind Gegner)";
                push_toast( menu, "FFA DEATHMATCH", dm_toast, D3DCOLOR_RGBA( 255, 170, 0, 255 ) );
                save_config( menu, show_esp, esp_renderer );
            }
            s_f6_down = k_f6;

            // Stop drawing before touching stale game data after process exit.
            if (frame_start - last_process_check >= std::chrono::seconds(1)) {
                last_process_check = frame_start;
                DWORD current_pid = 0;
                if (!IsWindow(target_window) ||
                    !GetWindowThreadProcessId(target_window, &current_pid) ||
                    current_pid != target_process_id || !memory.is_attached()) {
                    connection_lost = true;
                    break;
                }
            }
            esp_renderer.config.enabled = show_esp;

            // Fokus- und Fenstervalidierung (behebt das Problem beim Raustabben/Startmenue)
            const HWND fg = GetForegroundWindow( );
            DWORD foreground_process_id = 0;
            if ( fg )
                GetWindowThreadProcessId( fg, &foreground_process_id );
            const bool is_focused = ( fg == target_window ) ||
                                    ( fg == overlay.get_overlay_wnd( ) ) ||
                                    ( target_process_id != 0 && foreground_process_id == target_process_id ) ||
                                    ( foreground_process_id == GetCurrentProcessId( ) );

            // Wenn CS2 minimiert ist ODER weder Spiel noch MenÃ¼ aktiv sind:
            const bool is_active = is_focused || (menu_open && !IsIconic(target_window));
            if ( IsIconic( target_window ) || !is_active ) {
                if ( !empty_overlay_cleared ) {
                    if ( dx.begin_rendering( ) ) {
                        if ( dx.end_rendering( ) )
                            overlay.frame_presented( );
                    }
                    empty_overlay_cleared = true;
                }
                std::this_thread::sleep_for( std::chrono::milliseconds( 40 ) );
                continue;
            }
            empty_overlay_cleared = false;

            if ( !overlay.sync_to_target( &dx ) ) {
                connection_lost = true;
                break;
            }

            RECT client_rect{};
            GetClientRect( overlay.get_overlay_wnd( ), &client_rect );
            const int screen_width = client_rect.right - client_rect.left;
            const int screen_height = client_rect.bottom - client_rect.top;

			const auto input_geometry = get_menu_geometry( menu, screen_width, screen_height );
			overlay.set_input_region( input_geometry.left, input_geometry.top,
				input_geometry.width, input_geometry.height );
			overlay.set_interactive( menu_open );

            if ( menu_open ) {
                handle_menu_input( menu, overlay.get_overlay_wnd( ), show_esp, esp_renderer,
                    screen_width, screen_height, &menu_open, &overlay );
            }

            if ( frame_start - last_config_check >= std::chrono::seconds(2) ) {
                last_config_check = frame_start;
                WIN32_FILE_ATTRIBUTE_DATA fad{};
                const std::string path = get_config_file_path( );
                if ( GetFileAttributesExA( path.c_str( ), GetFileExInfoStandard, &fad ) ) {
                    if ( CompareFileTime( &fad.ftLastWriteTime, &g_last_config_filetime ) > 0 ) {
                        load_config( menu, show_esp, esp_renderer );
                    }
                }
            }
            // No enabled overlays and no menu: clear once, then stop submitting GPU frames.
            // This reduces GPU/CPU use while the overlay has nothing to show.
            // The aimbot needs a frame every tick even with ESP and menu off,
            // otherwise its servo never runs and the crosshair never moves.
            const bool aim_needs_frames = aim_controller.config.enabled;
            const bool nothing_visible = !menu_open && !show_esp && !aim_needs_frames && menu.toast.remaining_ticks <= 0;
            if (nothing_visible && empty_overlay_cleared) {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                continue;
            }
            empty_overlay_cleared = nothing_visible;
            if (!dx.begin_rendering()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }

            if ( show_esp ) {
                esp_renderer.draw( dx, screen_width, screen_height );
            }

            // Auto Bunnyhop (Spacebar halten fuer maximales Bewegungstempo)
            if ( !menu_open && is_focused && aim_controller.config.bhop && ( GetAsyncKeyState( VK_SPACE ) & 0x8000 ) != 0 ) {
                const auto lp = memory.read<std::uintptr_t>( client_base + sdk::off().local_player_pawn );
                if ( sdk::plausible_pointer( lp ) ) {
                    const std::uint32_t flags = memory.read<std::uint32_t>( lp + sdk::nv().m_fFlags );
                    if ( flags & 1 ) {
                        mouse_event( MOUSEEVENTF_WHEEL, 0, 0, (DWORD)-120, 0 );
                        keybd_event( VK_SPACE, 0, 0, 0 );
                        keybd_event( VK_SPACE, 0, KEYEVENTF_KEYUP, 0 );
                    }
                }
            }

            // Aim runs after ESP so the overlay already holds the frame. It
            // injects relative mouse motion and draws its own FOV ring.
            aim_controller.update( dx, menu_open, screen_width, screen_height );

            if ( frame_start - last_diagnostics_write >= std::chrono::seconds( 1 ) ) {
                write_live_diagnostics( target_process_id, client_base, menu_open, show_esp,
                    esp_renderer, aim_controller );
                last_diagnostics_write = frame_start;
            }

            if ( menu.show_status && !menu_open ) {
                draw_status_watermark( dx, esp_renderer );
            }

            // Draw Full Menu
            if ( menu_open ) {
                draw_menu( dx, esp_renderer, show_esp, menu, screen_width, screen_height );
            }
            draw_toast_notification( dx, menu, screen_width );

            if ( dx.end_rendering( ) )
                overlay.frame_presented( );

            // FlÃ¼ssige Bildrate: 60 FPS bei geÃ¶ffnetem MenÃ¼, 45 FPS im Spielbetrieb
            const auto frame_budget = menu_open
                ? std::chrono::microseconds(16667)
                : std::chrono::microseconds(22222);
            const auto deadline = frame_start + frame_budget;
            if ( std::chrono::steady_clock::now( ) < deadline )
                std::this_thread::sleep_until( deadline );
        }

        save_config( menu, show_esp, esp_renderer );
        if ( connection_lost ) {
            g_aim = nullptr;
            std::this_thread::sleep_for( std::chrono::milliseconds( 750 ) );
            continue;
        }
        return 0;
            } catch (const std::exception& error) {
                // Only recover ordinary C++ initialization/render exceptions.
                // Windows access violations (0xC0000005) require a debugger/dump;
                // they are NOT caught or disguised here.
                const std::string detail = std::string("MEZA could not start: ") + error.what() +
                    "\n\nRetry starts the local session again without repeating sign-in." +
                    "\nMEZA Support";
                if (MessageBoxA(nullptr, detail.c_str(), "MEZA / Fehler", MB_RETRYCANCEL | MB_ICONERROR | MB_TOPMOST) == IDRETRY) {
                    if (!is_process_elevated()) {
                        if (relaunch_as_administrator())
                            return 0;
                    }
                    continue;
                }
                return 1;
            }
        }
    } catch ( const std::exception& error ) {
        const std::string message = error.what( );
        MessageBoxA( nullptr, message.c_str(), "MEZA", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND );
        return 1;
    }
}

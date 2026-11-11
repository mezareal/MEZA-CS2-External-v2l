#include "aim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace aim {

    namespace {
        constexpr float k_units_per_meter = 39.3700787f; // 1 m = 39.37 Source units
        // Per-frame cap: Unlocked higher throughput so flicks snap instantaneously
        // without artificial speed damping.
        constexpr int k_max_step = 260;
        // Lead correction cap extended for high-velocity sprint and jump tracking.
        constexpr float k_max_lead_units = 180.0f;
        // Real CS2 bone indices: 7 is HEAD (between eyes), 6 is NECK, 4 is CHEST
        constexpr int k_bone_head = 7;
        constexpr int k_bone_neck = 6;
        constexpr int k_bone_chest = 4;

        unsigned int next_rand( std::uint32_t& state ) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        float rand_signed( std::uint32_t& state ) {
            // [-1, 1)
            return static_cast< float >( next_rand( state ) & 0xFFFFu ) / 32768.0f - 1.0f;
        }
    }

    aimbot::aimbot( forceinline::memory_manager& memory, std::uintptr_t client_base )
        : m_memory( memory ), m_client_base( client_base ) {
        m_entity_stride = sdk::off().entity_stride ? sdk::off().entity_stride : 0x70;
        m_last_shot = std::chrono::steady_clock::now( );
        m_candidates.reserve( 16 );
    }

    float aimbot::bone_height( int bone, bool ducking ) const {
        switch ( bone ) {
        case 1:  return ducking ? 46.0f : 58.0f;  // Hals
        case 2:  return ducking ? 38.0f : 48.0f;  // Brust
        default: return ducking ? 54.0f : 68.0f;  // Kopf (echte Kopfhoehe in CS2)
        }
    }

    bool aimbot::aim_point_for( const sdk::player_entry& player, sdk::vec3_t& point ) {
        // Bone path. The fixed height offset drifts the moment a player crouches
        // on a slope, uses a ladder or has a prop-adjusted origin; the model bone
        // array follows the actual pose. Every read is sanity-checked against the
        // known body proportions and falls back to the fixed height when it fails.
        const int wanted = ( config.bone % 3 + 3 ) % 3;
        const int index = wanted == 0 ? k_bone_head : ( wanted == 1 ? k_bone_neck : k_bone_chest );
        const auto scene_node = m_memory.read<std::uintptr_t>( player.pawn + sdk::nv().m_pGameSceneNode );
        if ( sdk::plausible_pointer( scene_node ) ) {
            sdk::vec3_t bone {};
            if ( sdk::read_bone_position( m_memory, scene_node, index, bone ) ) {
                const float rise = bone.z - player.origin.z;
                const float low = player.ducking ? 26.0f : 34.0f;
                const float high = player.ducking ? 74.0f : 86.0f;
                const float flat = std::sqrt( ( bone.x - player.origin.x ) * ( bone.x - player.origin.x ) +
                                              ( bone.y - player.origin.y ) * ( bone.y - player.origin.y ) );
                if ( rise > low && rise < high && flat < 40.0f ) {
                    point = bone;
                    return true;
                }
            }
        }
        point = player.origin + sdk::vec3_t { 0.0f, 0.0f, bone_height( config.bone, player.ducking ) };
        return true;
    }

    bool aimbot::game_foreground( ) const {
        const HWND foreground = GetForegroundWindow( );
        if ( !foreground )
            return false;
        DWORD pid = 0;
        GetWindowThreadProcessId( foreground, &pid );
        return pid != 0 && pid == m_memory.process_id( );
    }

    bool aimbot::inject_step( int dx, int dy ) {
        if ( dx == 0 && dy == 0 )
            return true;

        INPUT input {};
        input.type = INPUT_MOUSE;
        input.mi.dx = dx;
        input.mi.dy = dy;
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE;
        input.mi.time = 0;
        input.mi.dwExtraInfo = 0;
        const bool accepted = SendInput( 1, &input, sizeof( INPUT ) ) == 1;

        m_stats.move_x = dx;
        m_stats.move_y = dy;
        m_stats.input_ok = accepted;
        return accepted;
    }

    float aimbot::score( const aimbot::candidate& c ) const {
        // Lower is better. Every mode keeps the crosshair distance as a
        // tiebreaker so two equal candidates still resolve to the closer one.
        switch ( ( config.priority % 4 + 4 ) % 4 ) {
        case 1:  // wenig HP zuerst
            return c.health * 6.0f + c.fov_px * 0.35f + ( c.spotted ? 0.0f : 45.0f );
        case 2:  // raeumlich naechster Spieler
            return c.meters * 8.0f + c.fov_px * 0.5f;
        case 3:  // sichtbare Ziele zuerst
            return ( c.spotted ? 0.0f : 900.0f ) + c.fov_px;
        default: // Fadenkreuz naechster Punkt
            return c.fov_px + ( c.spotted ? 0.0f : 18.0f );
        }
    }

    void aimbot::draw_ring( forceinline::dx_renderer& overlay, int cx, int cy, bool locked ) const {
        const int radius = (std::clamp)( config.fov_pixels, 10, 600 );
        const unsigned long line_color = locked
            ? D3DCOLOR_RGBA( 232, 148, 255, 235 )
            : D3DCOLOR_RGBA( 172, 118, 224, 140 );
        const int style = ( config.ring_style % 3 + 3 ) % 3;

        if ( style == 2 ) {
            // Soft two-pass glow; on a chroma-keyed overlay only low alpha
            // circles are safe, so no filled disc is used here.
            overlay.draw_circle( cx, cy, radius + 2, 96, D3DCOLOR_RGBA( 226, 138, 255, 26 ) );
            overlay.draw_circle( cx, cy, radius + 1, 96, D3DCOLOR_RGBA( 226, 138, 255, 44 ) );
        }
        overlay.draw_circle( cx, cy, radius, 96, line_color );

        if ( style >= 1 ) {
            constexpr int tick = 6;
            const unsigned long tick_color = locked
                ? D3DCOLOR_RGBA( 244, 176, 255, 240 )
                : D3DCOLOR_RGBA( 182, 132, 226, 150 );
            overlay.draw_line( cx - tick, cy - radius, cx + tick, cy - radius, tick_color );
            overlay.draw_line( cx - tick, cy + radius, cx + tick, cy + radius, tick_color );
            overlay.draw_line( cx - radius, cy - tick, cx - radius, cy + tick, tick_color );
            overlay.draw_line( cx + radius, cy - tick, cx + radius, cy + tick, tick_color );
        }
    }

    void aimbot::draw_lock( forceinline::dx_renderer& overlay, const candidate& c, int cx, int cy ) const {
        const int tx = static_cast< int >( c.point.x );
        const int ty = static_cast< int >( c.point.y );
        constexpr int arm = 7;
        const unsigned long lock_color = D3DCOLOR_RGBA( 246, 158, 255, 245 );

        if ( config.draw_path ) {
            // Faint servo line: shows what the mouse is currently being pulled
            // towards, which makes a mis-set FOV immediately obvious.
            overlay.draw_line( cx, cy, tx, ty, D3DCOLOR_RGBA( 226, 138, 255, 60 ) );
        }

        overlay.draw_line( tx - arm, ty, tx - 2, ty, lock_color );
        overlay.draw_line( tx + 2, ty, tx + arm, ty, lock_color );
        overlay.draw_line( tx, ty - arm, tx, ty - 2, lock_color );
        overlay.draw_line( tx, ty + 2, tx, ty + arm, lock_color );
        overlay.draw_filled_circle( tx, ty, 1.8f, 10, lock_color );

        // HP rail under the marker: enough to judge a commit without reading
        // the ESP box across the map.
        const int bar_w = 28, bar_h = 3;
        const int bx = tx - bar_w / 2;
        const int by = ty + arm + 4;
        const int filled = bar_w * (std::clamp)( c.health, 0, 100 ) / 100;
        overlay.draw_filled_rect( bx, by, bar_w, bar_h, D3DCOLOR_RGBA( 20, 16, 28, 190 ) );
        if ( filled > 0 ) {
            const unsigned long hp_color = c.health > 60
                ? D3DCOLOR_RGBA( 120, 232, 156, 235 )
                : ( c.health > 30 ? D3DCOLOR_RGBA( 250, 204, 21, 235 ) : D3DCOLOR_RGBA( 244, 88, 110, 235 ) );
            overlay.draw_filled_rect( bx, by, filled, bar_h, hp_color );
        }

        char label[32]{};
        snprintf( label, sizeof( label ), "%dm", static_cast< int >( c.meters ) );
        overlay.draw_text_small( label, tx, by + 5, D3DCOLOR_RGBA( 206, 190, 232, 220 ), true, true );
    }

    void aimbot::update( forceinline::dx_renderer& overlay, bool menu_open, int screen_width, int screen_height ) {
        m_stats = stats {};

        if ( screen_width <= 0 || screen_height <= 0 )
            return;

        const int center_x = screen_width / 2;
        const int center_y = screen_height / 2;
        const auto now = std::chrono::steady_clock::now( );

        // Hotkey edge detection runs even while disabled so a toggle does not
        // "stick" when the feature is switched from the menu.
        const bool hotkey_down = config.hotkey > 0 && ( GetAsyncKeyState( config.hotkey ) & 0x8000 ) != 0;
        m_stats.hotkey_down = hotkey_down;
        if ( config.activation_mode == 1 ) {
            if ( hotkey_down && !m_prev_hotkey )
                m_toggled_on = !m_toggled_on;
        } else {
            m_toggled_on = false;
        }
        m_prev_hotkey = hotkey_down;

        bool trigger = false;
        switch ( config.activation_mode ) {
        case 1:  trigger = m_toggled_on; break;
        case 2:  trigger = true; break;          // immer aktiv, kein Keybind noetig
        default: trigger = hotkey_down; break;
        }

        const bool aiming = config.enabled && !menu_open && trigger;
        m_stats.active = aiming;
        m_stats.game_foreground = game_foreground( );

        if ( config.draw_fov )
            draw_ring( overlay, center_x, center_y, aiming );

        if ( !aiming ) {
            m_sticky_pawn = 0;   // a new engagement starts without inherited lock
            m_last_target = 0;
            m_last_error = -1.0f;
            m_gain = 1.0f;
            m_residual_x = 0.0f;
            m_residual_y = 0.0f;
            m_settle_frames = 0;
            return;
        }

        // GetForegroundWindow gate: without it the injected mouse motion lands
        // in whatever window owns the focus (browser, Discord, the menu itself).
        if ( !m_stats.game_foreground || !m_client_base )
            return;

        const auto entity_list = m_memory.read<std::uintptr_t>( m_client_base + sdk::off().entity_list );
        if ( !sdk::plausible_pointer( entity_list ) )
            return;

        const auto local_pawn = m_memory.read<std::uintptr_t>( m_client_base + sdk::off().local_player_pawn );
        const auto local_controller = m_memory.read<std::uintptr_t>(
            m_client_base + sdk::off().local_player_controller );
        if ( !sdk::plausible_pointer( local_pawn ) )
            return;

        const int local_health = m_memory.read<int>( local_pawn + sdk::nv().m_iHealth );
        if ( local_health <= 0 )
            return;

        const auto local_scene = m_memory.read<std::uintptr_t>( local_pawn + sdk::nv().m_pGameSceneNode );
        if ( !sdk::plausible_pointer( local_scene ) )
            return;
        const sdk::vec3_t local_origin = m_memory.read<sdk::vec3_t>( local_scene + sdk::nv().m_vecAbsOrigin );
        if ( !std::isfinite( local_origin.x ) || !std::isfinite( local_origin.y ) || !std::isfinite( local_origin.z ) )
            return;

        if ( now >= m_next_stride_probe ) {
            sdk::walk_report probe {};
            const std::uintptr_t probed = sdk::detect_entity_stride( m_memory, entity_list, &probe );
            if ( probed )
                m_entity_stride = probed;
            m_next_stride_probe = now + std::chrono::seconds( 1 );
        }

        // The team offset has to be validated before it is trusted. A stale byte
        // reads every player as an enemy, which is exactly the "aims at
        // teammates" symptom; a garbage byte reads everyone as the same team,
        // which is the "does nothing" symptom.
        if ( !m_team_verified || now >= m_next_team_probe ) {
            m_team_verified = sdk::calibrate_team_offset( m_memory, entity_list, local_pawn, m_entity_stride );
            m_next_team_probe = now + std::chrono::seconds( 5 );
        }
        m_stats.team_verified = m_team_verified;
        m_stats.gain = m_gain;

        const std::uint8_t local_team = m_memory.read<std::uint8_t>( local_pawn + sdk::nv().m_iTeamNum );

        sdk::view_matrix_t view_matrix {};
        m_memory.read_ex( &view_matrix, m_client_base + sdk::off().view_matrix, 1 );
        if ( !std::isfinite( view_matrix[0][0] ) || !std::isfinite( view_matrix[3][3] ) )
            return;

        const auto players = sdk::collect_players( m_memory, entity_list, local_pawn, local_controller,
            m_entity_stride, nullptr );

        const sdk::vec3_t screen_size { static_cast< float >( screen_width ),
                                        static_cast< float >( screen_height ), 0.0f };
        const float fov_limit = static_cast< float >( (std::clamp)( config.fov_pixels, 10, 600 ) );
        const float lead_seconds = static_cast< float >( (std::clamp)( config.prediction_ms, 0, 200 ) ) / 1000.0f;

        m_candidates.clear();

        for ( const auto& player : players ) {
            // Only filter when the offset is confirmed and the local team is a
            // real side. Guessing here is what produces teammate locks.
            const bool teammate = m_team_verified && local_team >= 2u && player.team == local_team;
            if ( teammate && !config.teammates )
                continue;

            if ( config.visible_only && !player.spotted )
                continue;

            const float meters = local_origin.dist( player.origin ) / k_units_per_meter;
            if ( config.max_distance_m > 0 && meters > static_cast< float >( config.max_distance_m ) )
                continue;

            ++m_stats.candidates;

            sdk::vec3_t aim_world {};
            if ( !aim_point_for( player, aim_world ) )
                continue;

            if ( lead_seconds > 0.0f ) {
                sdk::vec3_t lead = player.velocity * lead_seconds;
                const float lead_length = lead.length( );
                if ( lead_length > k_max_lead_units )
                    lead *= k_max_lead_units / lead_length;
                aim_world += lead;
            }

            sdk::vec3_t aim_screen {};
            if ( !sdk::world_to_screen( screen_size, aim_world, aim_screen, view_matrix ) )
                continue;
            if ( !std::isfinite( aim_screen.x ) || !std::isfinite( aim_screen.y ) )
                continue;

            const float dx = aim_screen.x - static_cast< float >( center_x );
            const float dy = aim_screen.y - static_cast< float >( center_y );
            const float distance = std::sqrt( dx * dx + dy * dy );
            if ( distance > fov_limit )
                continue;

            ++m_stats.in_fov;

            candidate entry {};
            entry.pawn = player.pawn;
            entry.point = aim_screen;
            entry.fov_px = distance;
            entry.meters = meters;
            entry.health = player.health;
            entry.spotted = player.spotted;
            entry.score = score( entry );
            m_candidates.push_back( entry );
        }

        m_stats.has_target = !m_candidates.empty( );
        if ( !m_candidates.empty( ) ) {
            auto best = std::min_element( m_candidates.begin( ), m_candidates.end( ),
                []( const candidate& a, const candidate& b ) { return a.score < b.score; } );

            // Sticky lock: if the previous target is still alive, still inside a
            // slightly widened FOV and not clearly worse, keep it. Without this
            // two players at the same pixel distance trade places every frame.
            if ( config.sticky_lock && m_sticky_pawn != 0 &&
                 now - m_sticky_seen <= std::chrono::milliseconds( (std::clamp)( config.lock_ms, 0, 1000 ) ) ) {
                const auto held = std::find_if( m_candidates.begin( ), m_candidates.end( ),
                    [&]( const candidate& c ) { return c.pawn == m_sticky_pawn; } );
                if ( held != m_candidates.end( ) && best->pawn != held->pawn &&
                     best->score >= held->score * 0.78f ) {
                    best = held;
                    m_stats.sticky_hold = true;
                }
            }

            m_stats.target_fov = best->fov_px;
            m_stats.target_health = best->health;
            m_stats.target_distance_m = best->meters;
            m_sticky_pawn = best->pawn;
            m_sticky_seen = now;

            if ( m_last_target != best->pawn ) {
                m_stats.target_changed = m_last_target != 0;
                m_last_target = best->pawn;
                m_last_error = -1.0f;
                m_gain = 1.0f;
                m_residual_x = 0.0f;
                m_residual_y = 0.0f;
                m_settle_frames = 0;
            }

            if ( config.draw_target )
                draw_lock( overlay, *best, center_x, center_y );

            // Screen delta from crosshair to target bone (Bone 7 = Head)
            const float raw_delta_x = best->point.x - static_cast< float >( center_x );
            const float raw_delta_y = best->point.y - static_cast< float >( center_y );
            const float dist_px = std::sqrt( raw_delta_x * raw_delta_x + raw_delta_y * raw_delta_y );

            if ( dist_px < 1.0f ) {
                m_last_shot = now;
                return;
            }

            // Standard pixel-to-mickey conversion calibrated for Source 2
            const float sens_scale = 1.45f;
            int move_x = 0;
            int move_y = 0;

            if ( config.smooth <= 1 ) {
                // Pure Instant Snap (Rage): direct 1-tick acquisition without lag
                move_x = static_cast< int >( std::lround( raw_delta_x * sens_scale ) );
                move_y = static_cast< int >( std::lround( raw_delta_y * sens_scale ) );
            } else {
                // Proportional smooth tracking: rock-solid, linear and predictable
                const float s = static_cast< float >( (std::clamp)( config.smooth, 2, 20 ) );
                move_x = static_cast< int >( std::lround( ( raw_delta_x * sens_scale ) / s ) );
                move_y = static_cast< int >( std::lround( ( raw_delta_y * sens_scale ) / s ) );
            }

            // Recoil Compensation System (RCS): counters spray during firing
            const bool is_firing = ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) != 0;
            if ( config.rcs_enabled && is_firing ) {
                ++m_spray_ticks;
                const int rcs_pull = (std::clamp)( m_spray_ticks / 3, 1, 9 );
                move_y += rcs_pull;
            } else {
                m_spray_ticks = 0;
            }

            // Micro-snap so the crosshair never hovers 1px off
            if ( move_x == 0 && std::abs( raw_delta_x ) >= 2.0f )
                move_x = ( raw_delta_x > 0.0f ) ? 1 : -1;
            if ( move_y == 0 && std::abs( raw_delta_y ) >= 2.0f )
                move_y = ( raw_delta_y > 0.0f ) ? 1 : -1;

            move_x = (std::clamp)( move_x, -k_max_step, k_max_step );
            move_y = (std::clamp)( move_y, -k_max_step, k_max_step );

            inject_step( move_x, move_y );
            m_last_shot = now;
        } else {
            m_sticky_pawn = 0;
            m_last_target = 0;
            m_last_error = -1.0f;
            m_gain = 1.0f;
            m_residual_x = 0.0f;
            m_residual_y = 0.0f;
            m_settle_frames = 0;
            m_spray_ticks = 0;
            // Screen spinbot removed so camera stays 100% still and playable
        }
    }

} // namespace aim

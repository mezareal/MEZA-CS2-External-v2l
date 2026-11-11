#include "esp.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

namespace esp {

    namespace {
        constexpr int k_player_slot_count = 64;
        constexpr int k_entity_page_count = 64;

        bool finite_vector( const sdk::vec3_t& value ) {
            return std::isfinite( value.x ) && std::isfinite( value.y ) && std::isfinite( value.z );
        }

        unsigned long with_alpha( unsigned long color, unsigned int alpha ) {
            return ( color & 0x00FFFFFFUL ) | ( ( alpha & 0xFFUL ) << 24 );
        }

        struct bone_transform {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            std::uint8_t padding[20]{};
        };
        static_assert( sizeof( bone_transform ) == 0x20 );

        std::string uppercase_label( std::string text ) {
            for ( char& ch : text ) {
                if ( ch == '_' ) ch = ' ';
                else ch = static_cast<char>( std::toupper( static_cast<unsigned char>( ch ) ) );
            }
            return text;
        }
    }

    renderer::renderer( forceinline::memory_manager& memory, std::uintptr_t client_base )
        : m_memory( memory ), m_client_base( client_base ) {
        // Seeded from the runtime offset table so an [Offsets] override in
        // config.ini takes effect without a rebuild. probe == first draw().
        m_entity_stride = sdk::off().entity_stride ? sdk::off().entity_stride : 0x70;
        m_next_stride_probe = std::chrono::steady_clock::time_point{};
    }

    bool renderer::get_world_position( std::uintptr_t pawn, sdk::vec3_t& position ) const {
        const auto scene_node = m_memory.read<std::uintptr_t>( pawn + sdk::nv().m_pGameSceneNode );
        if ( !scene_node )
            return false;

        position = m_memory.read<sdk::vec3_t>( scene_node + sdk::nv().m_vecAbsOrigin );
        return finite_vector( position );
    }

    namespace {
        const char* weapon_id_to_name( int16_t id ) {
            switch ( id ) {
            case 1: return "Deagle";
            case 2: return "Dual Berettas";
            case 3: return "Five-SeveN";
            case 4: return "Glock-18";
            case 7: return "AK-47";
            case 8: return "AUG";
            case 9: return "AWP";
            case 10: return "FAMAS";
            case 11: return "G3SG1";
            case 13: return "Galil AR";
            case 14: return "M249";
            case 16: return "M4A4";
            case 17: return "MAC-10";
            case 19: return "P90";
            case 23: return "MP5-SD";
            case 24: return "UMP-45";
            case 25: return "XM1014";
            case 26: return "PP-Bizon";
            case 27: return "MAG-7";
            case 28: return "Negev";
            case 29: return "Sawed-Off";
            case 30: return "Tec-9";
            case 31: return "Zeus x27";
            case 32: return "P2000";
            case 33: return "MP7";
            case 34: return "MP9";
            case 35: return "Nova";
            case 36: return "P250";
            case 38: return "SCAR-20";
            case 39: return "SG 553";
            case 40: return "SSG 08";
            case 43: return "Flashbang";
            case 44: return "HE Grenade";
            case 45: return "Smoke";
            case 46: return "Molotov";
            case 47: return "Decoy";
            case 48: return "Incendiary";
            case 49: return "C4";
            case 60: return "M4A1-S";
            case 61: return "USP-S";
            case 63: return "CZ75-Auto";
            case 64: return "R8 Revolver";
            default:
                if ( id >= 500 && id <= 525 ) return "Knife";
                return "";
            }
        }
    }

    int renderer::get_armor( std::uintptr_t controller, std::uintptr_t pawn ) const {
        if ( pawn ) {
            const int val = m_memory.read<int>( pawn + sdk::nv().m_ArmorValue );
            if ( val > 0 && val <= 100 )
                return val;
        }
        if ( controller ) {
            const int val = m_memory.read<int>( controller + sdk::nv().m_iPawnArmor );
            if ( val > 0 && val <= 100 )
                return val;
        }
        return 0;
    }

    std::string renderer::get_weapon_name( std::uintptr_t entity_list, std::uintptr_t pawn ) const {
        if ( !pawn || !entity_list )
            return "";

        const auto weapon_services = m_memory.read<std::uintptr_t>( pawn + sdk::nv().m_pWeaponServices );
        if ( !weapon_services )
            return "";

        const auto weapon_handle = m_memory.read<std::uint32_t>( weapon_services + sdk::nv().m_hActiveWeapon );
        if ( weapon_handle == 0 || weapon_handle == 0xFFFFFFFFu )
            return "";

        const auto weapon_page = m_memory.read<std::uintptr_t>(
            entity_list + 0x10 + 0x8 * ( ( weapon_handle & 0x7FFF ) >> 9 ) );
        if ( !weapon_page )
            return "";

        const auto weapon_entity = m_memory.read<std::uintptr_t>(
            weapon_page + m_entity_stride * ( weapon_handle & 0x1FF ) );
        if ( !weapon_entity )
            return "";

        const int16_t def_index = m_memory.read<int16_t>(
            weapon_entity + sdk::nv().m_iItemDefinitionIndexOffset );

        return weapon_id_to_name( def_index );
    }

    void renderer::refresh_world_cache( std::uintptr_t entity_list ) {
        m_world_cache.clear();
        if ( !entity_list ) return;

        const int highest = std::clamp( m_memory.read<int>(
            entity_list + sdk::off().highest_entity_index ), 64, 2048 );
        std::array<std::uintptr_t, k_entity_page_count> pages{};

        for ( int index = 65; index <= highest; ++index ) {
            const std::size_t page_index = static_cast<std::size_t>( index ) >> 9;
            if ( page_index >= pages.size() ) break;
            if ( !pages[page_index] ) {
                pages[page_index] = m_memory.read<std::uintptr_t>(
                    entity_list + 0x10 + 0x8 * page_index );
            }
            if ( !pages[page_index] ) continue;

            const auto entity = m_memory.read<std::uintptr_t>( pages[page_index] +
                m_entity_stride * ( static_cast<unsigned int>( index ) & 0x1FFu ) );
            if ( !entity ) continue;

            const auto identity = m_memory.read<std::uintptr_t>( entity + sdk::nv().m_pEntityIdentity );
            if ( !identity ) continue;
            const auto name_ptr = m_memory.read<std::uintptr_t>( identity + sdk::nv().m_designerName );
            if ( !name_ptr ) continue;

            char raw_name[64]{};
            m_memory.read_ex( raw_name, name_ptr, sizeof( raw_name ) - 1 );
            raw_name[sizeof( raw_name ) - 1] = 0;
            if ( raw_name[0] < 32 || raw_name[0] > 126 ) continue;

            std::string name( raw_name, (std::min)( std::strlen( raw_name ), std::size_t( 63 ) ) );
            for ( char& ch : name )
                ch = static_cast<char>( std::tolower( static_cast<unsigned char>( ch ) ) );

            const bool projectile = name.find( "_projectile" ) != std::string::npos;
            if ( projectile && config.draw_grenades ) {
                std::string label;
                if ( name.find( "smoke" ) != std::string::npos ) label = "SMOKE";
                else if ( name.find( "flash" ) != std::string::npos ) label = "FLASH";
                else if ( name.find( "hegrenade" ) != std::string::npos ) label = "HE";
                else if ( name.find( "molotov" ) != std::string::npos || name.find( "incgrenade" ) != std::string::npos ) label = "FIRE";
                else if ( name.find( "decoy" ) != std::string::npos ) label = "DECOY";
                if ( !label.empty() )
                    m_world_cache.push_back( { entity, world_kind::grenade, std::move( label ) } );
                continue;
            }

            if ( config.draw_dropped_weapons && name.rfind( "weapon_", 0 ) == 0 &&
                 name.find( "worldmodel" ) == std::string::npos ) {
                const auto owner = m_memory.read<std::uint32_t>( entity + sdk::nv().m_hOwnerEntity );
                if ( owner != 0 && owner != 0xFFFFFFFFu ) continue;
                std::string label = uppercase_label( name.substr( 7 ) );
                if ( !label.empty() && label.size() <= 24 )
                    m_world_cache.push_back( { entity, world_kind::weapon, std::move( label ) } );
            }
        }
    }

    bool renderer::is_deathmatch_active() const {
        if ( !m_client_base )
            return false;

        const auto game_rules = m_memory.read<std::uintptr_t>( m_client_base + sdk::off().game_rules );
        if ( !game_rules )
            return false;

        const auto mode_rules = m_memory.read<std::uintptr_t>( game_rules + sdk::nv().m_pGameModeRules );
        if ( !mode_rules )
            return false;

        const float dm_bonus_time = m_memory.read<float>( mode_rules + sdk::nv().m_flDMBonusTimeLength );
        return ( dm_bonus_time >= 5.0f && dm_bonus_time <= 120.0f );
    }

    void renderer::draw( forceinline::dx_renderer& overlay, int screen_width, int screen_height ) {
        m_stats = draw_stats{};
        m_stats.client_base = m_client_base;
        m_stats.stride = m_entity_stride;
        m_stats.stride_detected = m_stride_detected;

        if ( !config.enabled || screen_width <= 0 || screen_height <= 0 || !m_client_base )
            return;

        const auto entity_list = m_memory.read<std::uintptr_t>( m_client_base + sdk::off().entity_list );
        m_stats.entity_list = entity_list;
        if ( !entity_list )
            return;

        const auto local_pawn = m_memory.read<std::uintptr_t>(
            m_client_base + sdk::off().local_player_pawn );
        const auto local_controller = m_memory.read<std::uintptr_t>(
            m_client_base + sdk::off().local_player_controller );
        m_stats.local_pawn = local_pawn;
        m_stats.local_controller = local_controller;

        if ( !local_pawn && !local_controller )
            return;

        // Stride re-probe every second. A CS2 update that grows
        // CEntityIdentity turns every controller read into garbage, which is
        // exactly what a "nothing draws anymore" report looks like.
        const auto probe_now = std::chrono::steady_clock::now();
        if ( probe_now >= m_next_stride_probe ) {
            sdk::walk_report probe{};
            const std::uintptr_t probed = sdk::detect_entity_stride( m_memory, entity_list, &probe );
            if ( probe.stride_detected && probed ) {
                if ( probed == m_entity_stride ) {
                    m_stride_detected = true;
                    m_pending_stride = 0;
                    m_pending_stride_hits = 0;
                } else {
                    if ( probed == m_pending_stride ) {
                        ++m_pending_stride_hits;
                    } else {
                        m_pending_stride = probed;
                        m_pending_stride_hits = 1;
                    }

                    // A single noisy sample must not invalidate every cached
                    // entity. Two consecutive probes still recover quickly
                    // after a real game update without one-frame ESP dropouts.
                    if ( m_pending_stride_hits >= 2 ) {
                        m_entity_stride = probed;
                        m_stride_detected = true;
                        m_pending_stride = 0;
                        m_pending_stride_hits = 0;
                        m_display_cache = {};
                        m_world_cache.clear( );
                    }
                }
            }
            m_next_stride_probe = probe_now + std::chrono::seconds( 1 );
        }
        m_stats.stride = m_entity_stride;
        m_stats.stride_detected = m_stride_detected;

        // These schema fields are uint8_t values. Reading them as int also
        // consumes adjacent bytes and can make every entity look invalid.
        std::uint8_t local_team = 0;
        if ( local_pawn ) {
            local_team = m_memory.read<std::uint8_t>( local_pawn + sdk::nv().m_iTeamNum );
        } else if ( local_controller ) {
            local_team = m_memory.read<std::uint8_t>( local_controller + sdk::nv().m_iTeamNum );
        }

        bool ffa_mode = ( config.deathmatch_mode == 1 );
        if ( config.deathmatch_mode == 2 ) {
            ffa_mode = is_deathmatch_active();
        }

        sdk::view_matrix_t view_matrix{};
        m_memory.read_ex( &view_matrix, m_client_base + sdk::off().view_matrix, 1 );
        // A valid view matrix is never all-zero and never NaN. If this is false,
        // dwViewMatrix is stale and every projection silently fails.
        m_stats.view_matrix_ok = std::isfinite( view_matrix[0][0] ) && std::isfinite( view_matrix[3][3] ) &&
            ( view_matrix[3][3] > 0.0f || view_matrix[0][0] != 0.0f );

        // Handle local player dead / spectating
        std::uintptr_t active_local_pawn = local_pawn;
        if ( local_pawn ) {
            const int local_hp = m_memory.read<int>( local_pawn + sdk::nv().m_iHealth );
            const auto local_life = m_memory.read<std::uint8_t>( local_pawn + sdk::nv().m_lifeState );
            if ( local_hp <= 0 || local_life != 0 ) {
                // Check if spectating another player
                const auto obs_services = m_memory.read<std::uintptr_t>( local_pawn + 0x1308 );
                if ( obs_services ) {
                    const auto target_handle = m_memory.read<std::uint32_t>( obs_services + 0x4C );
                    if ( target_handle != 0 && target_handle != 0xFFFFFFFFu ) {
                        const auto target_page = m_memory.read<std::uintptr_t>(
                            entity_list + 0x10 + 0x8 * ( ( target_handle & 0x7FFF ) >> 9 ) );
                        if ( target_page ) {
                            const auto target_pawn = m_memory.read<std::uintptr_t>(
                                target_page + m_entity_stride * ( target_handle & 0x1FF ) );
                            if ( target_pawn ) {
                                active_local_pawn = target_pawn;
                            }
                        }
                    }
                }
            }
        }

        sdk::vec3_t local_origin{};
        const bool has_local_origin = active_local_pawn ? get_world_position( active_local_pawn, local_origin ) : false;

        if ( config.draw_fov_circle ) {
            // One crisp HUD arc. Semi-transparent halo rings turn into dark
            // bands on chroma-key overlays, so no secondary outline is drawn.
            const int fov_x = screen_width / 2;
            const int fov_y = screen_height / 2;
            overlay.draw_circle( fov_x, fov_y, config.fov_radius, 64,
                D3DCOLOR_RGBA( 205, 128, 255, 230 ) );
            // Four restrained range ticks make the circle read like a real HUD
            // instrument without turning it into a second crosshair.
            constexpr int tick = 5;
            overlay.draw_line( fov_x - tick, fov_y - config.fov_radius, fov_x + tick, fov_y - config.fov_radius,
                D3DCOLOR_RGBA( 222, 152, 255, 235 ) );
            overlay.draw_line( fov_x - tick, fov_y + config.fov_radius, fov_x + tick, fov_y + config.fov_radius,
                D3DCOLOR_RGBA( 222, 152, 255, 235 ) );
            overlay.draw_line( fov_x - config.fov_radius, fov_y - tick, fov_x - config.fov_radius, fov_y + tick,
                D3DCOLOR_RGBA( 222, 152, 255, 235 ) );
            overlay.draw_line( fov_x + config.fov_radius, fov_y - tick, fov_x + config.fov_radius, fov_y + tick,
                D3DCOLOR_RGBA( 222, 152, 255, 235 ) );
        }

        // Player controllers occupy the first 64 slots. Iterating the entity
        // system's highest index walked up to 2048 unrelated objects every
        // frame and caused thousands of cross-process reads.
        const auto controller_page = m_memory.read<std::uintptr_t>( entity_list + 0x10 );
        const std::size_t controller_bytes =
            static_cast<std::size_t>( m_entity_stride ) * ( k_player_slot_count + 1 );
        // Sized for the largest stride the probe will ever accept (0x88), then
        // only controller_bytes are filled. A runtime stride cannot size an array.
        std::array<std::uint8_t, 0x88u * ( k_player_slot_count + 1 )> controller_entries{};
        if ( controller_page ) {
            m_memory.read_ex( controller_entries.data( ), controller_page,
                ( std::min )( controller_bytes, controller_entries.size( ) ) );
        }

        // Pawn handles can point to different 0x200-entry pages. Cache each
        // page pointer once per frame instead of rereading it for every player.
        std::array<std::uintptr_t, k_entity_page_count> entity_pages{};
        std::array<bool, k_entity_page_count> entity_page_loaded{};
        entity_pages[ 0 ] = controller_page;
        entity_page_loaded[ 0 ] = true;

        const auto frame_now = std::chrono::steady_clock::now();
        const sdk::vec3_t screen_size{
            static_cast< float >( screen_width ),
            static_cast< float >( screen_height ),
            0.0f
        };

        auto draw_offscreen = [&]( const sdk::vec3_t& world, unsigned long color ) {
            if ( !config.draw_offscreen ) return;
            float clip_x = view_matrix[0][0] * world.x + view_matrix[0][1] * world.y +
                view_matrix[0][2] * world.z + view_matrix[0][3];
            float clip_y = view_matrix[1][0] * world.x + view_matrix[1][1] * world.y +
                view_matrix[1][2] * world.z + view_matrix[1][3];
            float clip_w = view_matrix[3][0] * world.x + view_matrix[3][1] * world.y +
                view_matrix[3][2] * world.z + view_matrix[3][3];
            if ( !std::isfinite( clip_x ) || !std::isfinite( clip_y ) ||
                 !std::isfinite( clip_w ) || std::fabs( clip_w ) < 0.001f ) return;

            float dir_x = clip_x / clip_w;
            float dir_y = -( clip_y / clip_w );
            if ( clip_w < 0.0f ) { dir_x = -dir_x; dir_y = -dir_y; }
            const float length = std::sqrt( dir_x * dir_x + dir_y * dir_y );
            if ( length < 0.001f || !std::isfinite( length ) ) return;
            dir_x /= length;
            dir_y /= length;

            const float radius_x = (std::max)( 30.0f, screen_width * 0.5f - 42.0f );
            const float radius_y = (std::max)( 30.0f, screen_height * 0.5f - 58.0f );
            const int tip_x = static_cast<int>( screen_width * 0.5f + dir_x * radius_x );
            const int tip_y = static_cast<int>( screen_height * 0.5f + dir_y * radius_y );
            const int base_x = static_cast<int>( tip_x - dir_x * 15.0f );
            const int base_y = static_cast<int>( tip_y - dir_y * 15.0f );
            const int side_x = static_cast<int>( -dir_y * 7.0f );
            const int side_y = static_cast<int>( dir_x * 7.0f );
            overlay.draw_filled_triangle( tip_x, tip_y, base_x + side_x, base_y + side_y,
                base_x - side_x, base_y - side_y, with_alpha( color, 242 ) );
        };

        if ( config.draw_bomb_timer ) {
            const auto bomb = m_memory.read<std::uintptr_t>( m_client_base + sdk::off().planted_c4 );
            const auto globals = m_memory.read<std::uintptr_t>( m_client_base + sdk::off().global_vars );
            if ( bomb && globals && m_memory.read<std::uint8_t>( bomb + sdk::nv().m_bBombTicking ) &&
                 !m_memory.read<std::uint8_t>( bomb + sdk::nv().m_bBombDefused ) ) {
                const float current_time = m_memory.read<float>( globals + 0x34 );
                const float blow_time = m_memory.read<float>( bomb + sdk::nv().m_flC4Blow );
                const float remaining = blow_time - current_time;
                if ( std::isfinite( remaining ) && remaining > 0.0f && remaining <= 90.0f ) {
                    const bool urgent = remaining <= 10.0f;
                    const unsigned long bomb_color = urgent
                        ? D3DCOLOR_RGBA( 255, 76, 92, 248 )
                        : D3DCOLOR_RGBA( 255, 194, 71, 245 );
                    const int site = m_memory.read<int>( bomb + sdk::nv().m_nBombSite );
                    char label[48]{};
                    snprintf( label, sizeof( label ), "BOMB %c  %.1fs", site == 1 ? 'B' : 'A', remaining );
                    overlay.draw_text_small( label, screen_width / 2, 78, bomb_color, true, false );
                    const int rail_w = 180;
                    const int fill_w = std::clamp( static_cast<int>( remaining / 40.0f * rail_w ), 0, rail_w );
                    overlay.draw_line( screen_width / 2 - rail_w / 2, 96,
                        screen_width / 2 - rail_w / 2 + fill_w, 96, bomb_color );

                    if ( m_memory.read<std::uint8_t>( bomb + sdk::nv().m_bBeingDefused ) ) {
                        const float defuse_end = m_memory.read<float>( bomb + sdk::nv().m_flDefuseCountDown );
                        const float defuse_left = defuse_end - current_time;
                        if ( std::isfinite( defuse_left ) && defuse_left > 0.0f && defuse_left < 20.0f ) {
                            char defuse_label[40]{};
                            snprintf( defuse_label, sizeof( defuse_label ), "DEFUSE  %.1fs", defuse_left );
                            overlay.draw_text_small( defuse_label, screen_width / 2, 101,
                                D3DCOLOR_RGBA( 101, 211, 255, 248 ), true, false );
                        }
                    }
                }
            }
        }

        if ( config.draw_dropped_weapons || config.draw_grenades ) {
            if ( frame_now >= m_next_world_refresh ) {
                refresh_world_cache( entity_list );
                m_next_world_refresh = frame_now + std::chrono::milliseconds( 850 );
            }
            for ( const auto& item : m_world_cache ) {
                if ( ( item.kind == world_kind::weapon && !config.draw_dropped_weapons ) ||
                     ( item.kind == world_kind::grenade && !config.draw_grenades ) ) continue;
                sdk::vec3_t world{};
                if ( !get_world_position( item.entity, world ) ) continue;
                sdk::vec3_t point{};
                if ( !sdk::world_to_screen( screen_size, world, point, view_matrix ) || !finite_vector( point ) ) continue;
                if ( point.x < 6.0f || point.x >= screen_width - 6.0f ||
                     point.y < 6.0f || point.y >= screen_height - 6.0f ) continue;
                float distance = has_local_origin ? local_origin.dist( world ) * 0.0254f : 0.0f;
                if ( config.max_distance > 0 && distance > static_cast<float>( config.max_distance ) ) continue;
                const unsigned long item_color = item.kind == world_kind::grenade
                    ? D3DCOLOR_RGBA( 255, 190, 72, 238 )
                    : D3DCOLOR_RGBA( 101, 211, 255, 225 );
                overlay.draw_circle( static_cast<int>( point.x ), static_cast<int>( point.y ),
                    item.kind == world_kind::grenade ? 4 : 3, 14, item_color );
                std::string label = item.label;
                if ( has_local_origin )
                    label += "  " + std::to_string( static_cast<int>( std::round( distance ) ) ) + "m";
                overlay.draw_text_small( label, static_cast<int>( point.x ), static_cast<int>( point.y ) + 7,
                    item_color, true, false );
            }
        } else {
            m_world_cache.clear();
        }

        for ( int index = 1; index <= k_player_slot_count; ++index ) {
            std::uintptr_t controller = 0;
            std::uintptr_t pawn = 0;
            std::memcpy( &controller,
                controller_entries.data( ) + static_cast<std::size_t>( index ) * m_entity_stride,
                sizeof( controller ) );
            if ( !controller )
                continue;
            ++m_stats.controllers;

            const auto pawn_handle = m_memory.read<std::uint32_t>(
                controller + sdk::nv().m_hPlayerPawn );
            if ( pawn_handle == 0 || pawn_handle == 0xFFFFFFFFu )
                continue;

            const std::size_t pawn_page_index = ( pawn_handle & 0x7FFFu ) >> 9;
            if ( pawn_page_index >= entity_pages.size( ) )
                continue;
            if ( !entity_page_loaded[ pawn_page_index ] ) {
                entity_pages[ pawn_page_index ] = m_memory.read<std::uintptr_t>(
                    entity_list + 0x10 + 0x8 * pawn_page_index );
                entity_page_loaded[ pawn_page_index ] = true;
            }
            const auto pawn_page = entity_pages[ pawn_page_index ];
            if ( !pawn_page )
                continue;
            pawn = m_memory.read<std::uintptr_t>(
                pawn_page + m_entity_stride * ( pawn_handle & 0x1FFu ) );
            if ( !pawn )
                continue;
            ++m_stats.pawns;
            if ( ( active_local_pawn && pawn == active_local_pawn ) || ( local_pawn && pawn == local_pawn ) )
                continue;

            auto& display = m_display_cache[static_cast<std::size_t>(index)];
            if (display.controller != controller || display.pawn != pawn) {
                display = display_cache_entry{};
                display.controller = controller;
                display.pawn = pawn;
            }

            // Health, life state, team and flags live in one compact region.
            // One bulk read replaces four separate kernel transitions. The span
            // is runtime (m_fFlags - m_iHealth) now that netvars come from the
            // offset table, so the buffer is sized for the widest expected span.
            const std::uintptr_t status_base = sdk::nv().m_iHealth;
            auto field_offset = [&]( std::uintptr_t field ) -> std::size_t {
                return field >= status_base ? static_cast<std::size_t>( field - status_base ) : 0;
            };
            constexpr std::size_t k_status_capacity = 0x100;
            const std::size_t status_span = field_offset( sdk::nv().m_fFlags ) + sizeof( std::uint32_t );
            const std::size_t pawn_status_size = ( std::min )( status_span, k_status_capacity );
            std::array<std::uint8_t, k_status_capacity> pawn_status{};
            m_memory.read_ex( pawn_status.data( ), pawn + status_base, pawn_status_size );

            int health = 0;
            std::uint8_t life_state = 0;
            std::uint8_t team = 0;
            std::uint32_t flags = 0;
            auto status_copy = [&]( std::uintptr_t field, void* out, std::size_t size ) {
                const std::size_t offset = field_offset( field );
                // A hand-edited [Offsets] table can invert the layout; never read
                // past the buffer because of it.
                if ( offset + size > pawn_status_size )
                    return false;
                std::memcpy( out, pawn_status.data( ) + offset, size );
                return true;
            };
            status_copy( sdk::nv().m_iHealth, &health, sizeof( health ) );
            status_copy( sdk::nv().m_lifeState, &life_state, sizeof( life_state ) );
            status_copy( sdk::nv().m_iTeamNum, &team, sizeof( team ) );
            status_copy( sdk::nv().m_fFlags, &flags, sizeof( flags ) );

            if ( health <= 0 || health > 100 || life_state != 0 ) {
                ++m_stats.skipped_dead;
                continue;
            }

            const bool teammate = !ffa_mode && ( team == local_team );
            if ( teammate && !config.draw_teammates ) {
                ++m_stats.skipped_team;
                continue;
            }

            sdk::vec3_t origin{};
            if ( !get_world_position( pawn, origin ) )
                continue;

            float dist_meters = 0.0f;
            if ( has_local_origin ) {
                dist_meters = local_origin.dist( origin ) * 0.0254f;
                if ( config.max_distance > 0 && dist_meters > static_cast< float >( config.max_distance ) ) {
                    ++m_stats.skipped_distance;
                    continue;
                }
            }

            // Detect crouching / ducking state for accurate box height.
            const bool is_ducking = ( flags & ( 1 << 1 ) ) != 0;
            const float head_height = is_ducking ? 56.0f : 74.0f;

            sdk::vec3_t feet_screen{};
            sdk::vec3_t head_screen{};
            const bool feet_projected = sdk::world_to_screen( screen_size,
                origin - sdk::vec3_t{ 0.0f, 0.0f, 2.0f }, feet_screen, view_matrix );
            const bool head_projected = sdk::world_to_screen( screen_size,
                origin + sdk::vec3_t{ 0.0f, 0.0f, head_height }, head_screen, view_matrix );
            if ( !feet_projected || !head_projected ) {
                ++m_stats.skipped_projection;
                draw_offscreen( origin + sdk::vec3_t{ 0.0f, 0.0f, head_height * 0.5f },
                    teammate ? config.teammate_color : config.enemy_color );
                continue;
            }

            // Corrupt or half-updated matrices can briefly produce NaN or
            // enormous screen coordinates. Never submit those to D3D9.
            if ( !finite_vector( feet_screen ) || !finite_vector( head_screen ) )
                continue;
            const float guard_x = static_cast< float >( screen_width );
            const float guard_y = static_cast< float >( screen_height );
            if ( feet_screen.x < -guard_x || feet_screen.x > guard_x * 2.0f ||
                 head_screen.x < -guard_x || head_screen.x > guard_x * 2.0f ||
                 feet_screen.y < -guard_y || feet_screen.y > guard_y * 2.0f ||
                 head_screen.y < -guard_y || head_screen.y > guard_y * 2.0f ) {
                draw_offscreen( origin + sdk::vec3_t{ 0.0f, 0.0f, head_height * 0.5f },
                    teammate ? config.teammate_color : config.enemy_color );
                continue;
            }

            const float height = feet_screen.y - head_screen.y;
			if ( height < 8.0f || height > static_cast< float >( screen_height ) * 1.5f )
                continue;

            const int box_height = static_cast< int >( height );
            const int box_width = ( std::max )( 4, static_cast< int >( height * 0.44f ) );
            const int center_x = static_cast< int >( ( head_screen.x + feet_screen.x ) * 0.5f );
            const int box_x = center_x - box_width / 2;
            const int box_y = static_cast< int >( head_screen.y );
			if ( box_x + box_width < 0 || box_x >= screen_width ||
				 box_y + box_height < 0 || box_y >= screen_height ) {
				draw_offscreen( origin + sdk::vec3_t{ 0.0f, 0.0f, head_height * 0.5f },
					teammate ? config.teammate_color : config.enemy_color );
				continue;
			}

            const unsigned long box_color = teammate
                ? config.teammate_color
                : config.enemy_color;
            const unsigned int visual_alpha = box_height < 42 ? 225u : 250u;
            const unsigned long primary_color = with_alpha( box_color, visual_alpha );

            // The game's own visibility bit. A target the server considers
            // visible renders hotter; a hidden one keeps a dim halo so the ESP
            // still reads clean through a wall without shouting.
            [[maybe_unused]] const bool spotted = m_memory.read<std::uint8_t>(
                pawn + sdk::nv().m_entitySpottedState + sdk::nv().m_bSpotted ) != 0;

            if ( config.draw_skeleton ) {
                const auto scene_node = m_memory.read<std::uintptr_t>( pawn + sdk::nv().m_pGameSceneNode );
                const auto bone_array = scene_node ? m_memory.read<std::uintptr_t>(
                    scene_node + sdk::nv().m_modelState + sdk::nv().m_boneArray ) : 0;
                if ( bone_array ) {
                    std::array<bone_transform, 32> bones{};
                    m_memory.read_ex( bones.data(), bone_array, bones.size() );
                    static constexpr int links[][2] = {
                        // Spine & Neck & Head
                        { 7, 6 }, { 6, 5 }, { 5, 4 }, { 4, 2 }, { 2, 0 },
                        // Arms
                        { 6, 8 }, { 8, 9 }, { 9, 10 }, { 10, 11 },
                        { 6, 12 }, { 12, 13 }, { 13, 14 }, { 14, 15 },
                        // CS2 Legs (0 -> 22/25)
                        { 0, 22 }, { 22, 23 }, { 23, 24 },
                        { 0, 25 }, { 25, 26 }, { 26, 27 },
                        // Legacy models fallback
                        { 0, 17 }, { 17, 18 }, { 18, 19 },
                        { 0, 20 }, { 20, 21 }, { 21, 22 }
                    };
                    for ( const auto& link : links ) {
                        const sdk::vec3_t a{ bones[link[0]].x, bones[link[0]].y, bones[link[0]].z };
                        const sdk::vec3_t b{ bones[link[1]].x, bones[link[1]].y, bones[link[1]].z };
                        if ( !finite_vector( a ) || !finite_vector( b ) ||
                             a.dist( origin ) > 180.0f || b.dist( origin ) > 180.0f ) continue;
                        if ( a.dist( b ) > 42.0f || a.dist( b ) < 0.5f ) continue;

                        sdk::vec3_t sa{}, sb{};
                        if ( !sdk::world_to_screen( screen_size, a, sa, view_matrix ) ||
                             !sdk::world_to_screen( screen_size, b, sb, view_matrix ) ||
                             !finite_vector( sa ) || !finite_vector( sb ) ) continue;
                        if ( sa.x < -32.0f || sa.x > screen_width + 32.0f ||
                             sb.x < -32.0f || sb.x > screen_width + 32.0f ||
                             sa.y < -32.0f || sa.y > screen_height + 32.0f ||
                             sb.y < -32.0f || sb.y > screen_height + 32.0f ) continue;

                        // Clean lines only: 1px contrast shadow + crisp primary bone line (NO circles / dots)
                        overlay.draw_line( static_cast<int>( sa.x ) + 1, static_cast<int>( sa.y ) + 1,
                            static_cast<int>( sb.x ) + 1, static_cast<int>( sb.y ) + 1, D3DCOLOR_RGBA( 0, 0, 0, 210 ) );

                        overlay.draw_line( static_cast<int>( sa.x ), static_cast<int>( sa.y ),
                            static_cast<int>( sb.x ), static_cast<int>( sb.y ), with_alpha( primary_color, 245 ) );
                    }
                }
            }

            if ( config.draw_box ) {
                if ( config.box_style == 1 ) {
                    // Filled Glass: subtle tinted center with sharp outline
                    overlay.draw_filled_rect( box_x, box_y, box_width, box_height, with_alpha( box_color, 25u ) );
                    overlay.draw_outlined_rect( box_x - 1, box_y - 1, box_width + 2, box_height + 2, D3DCOLOR_RGBA( 0, 0, 0, 210 ) );
                    overlay.draw_outlined_rect( box_x, box_y, box_width, box_height, primary_color );
                    overlay.draw_outlined_rect( box_x + 1, box_y + 1, box_width - 2, box_height - 2, D3DCOLOR_RGBA( 0, 0, 0, 210 ) );
                } else if ( config.box_style == 2 ) {
                    // Corner Brackets: sharp 1px black outline backing
                    const int max_c = (std::max)(3, (box_width - 4) / 2);
                    const int corner = (std::min)(std::clamp( box_width / 4, 4, 18 ), max_c);
                    overlay.draw_corner_box( box_x - 1, box_y - 1, box_width + 2, box_height + 2, corner + 1, D3DCOLOR_RGBA( 0, 0, 0, 220 ) );
                    overlay.draw_corner_box( box_x + 1, box_y + 1, box_width - 2, box_height - 2, corner - 1, D3DCOLOR_RGBA( 0, 0, 0, 220 ) );
                    overlay.draw_corner_box( box_x, box_y, box_width, box_height, corner, primary_color );
                } else {
                    // Clean Outlined Box with 1px black inner and outer drop stroke
                    overlay.draw_outlined_rect( box_x - 1, box_y - 1, box_width + 2, box_height + 2, D3DCOLOR_RGBA( 0, 0, 0, 210 ) );
                    overlay.draw_outlined_rect( box_x, box_y, box_width, box_height, primary_color );
                    overlay.draw_outlined_rect( box_x + 1, box_y + 1, box_width - 2, box_height - 2, D3DCOLOR_RGBA( 0, 0, 0, 210 ) );
                }
            }

            // Bottom Stack: Health Bar -> Armor Bar -> Weapon Name
            int bottom_cursor_y = box_y + box_height + 4;

            if ( config.draw_health ) {
                const int bar_h = 4;
                const int health_width = std::clamp( box_width * health / 100, 0, box_width );

                // Background rail behind health
                overlay.draw_filled_rect( box_x - 1, bottom_cursor_y - 1, box_width + 2, bar_h + 2,
                    D3DCOLOR_RGBA( 10, 8, 16, 215 ) );

                // Dynamic color gradient based on current HP
                unsigned long hp_col_left, hp_col_right;
                if ( health >= 60 ) {
                    hp_col_left  = D3DCOLOR_RGBA( 46, 213, 115, 255 );
                    hp_col_right = D3DCOLOR_RGBA( 38, 222, 129, 255 );
                } else if ( health >= 25 ) {
                    hp_col_left  = D3DCOLOR_RGBA( 255, 177, 66, 255 );
                    hp_col_right = D3DCOLOR_RGBA( 255, 159, 26, 255 );
                } else {
                    hp_col_left  = D3DCOLOR_RGBA( 255, 71, 87, 255 );
                    hp_col_right = D3DCOLOR_RGBA( 255, 56, 56, 255 );
                }

                if ( health_width > 0 ) {
                    overlay.draw_filled_rect_gradient_h( box_x, bottom_cursor_y, health_width, bar_h,
                        hp_col_left, hp_col_right );
                    // Bright leading edge tick
                    overlay.draw_filled_rect( box_x + health_width - 1, bottom_cursor_y, 1, bar_h,
                        D3DCOLOR_RGBA( 255, 255, 255, 220 ) );
                }

                // Clean numeric HP text on right edge if injured
                if ( health < 100 && health > 0 ) {
                    const std::string hp_str = std::to_string( health );
                    overlay.draw_text_small( hp_str, box_x + box_width + 4, bottom_cursor_y - 5, hp_col_left, false, false );
                }

                bottom_cursor_y += bar_h + 3;
            }

            const int armor = config.draw_armor ? get_armor( controller, pawn ) : 0;
            if ( config.draw_armor && armor > 0 ) {
                const int armor_h = 2;
                const int armor_width = std::clamp( box_width * armor / 100, 0, box_width );
                overlay.draw_filled_rect( box_x - 1, bottom_cursor_y - 1, box_width + 2, armor_h + 2,
                    D3DCOLOR_RGBA( 10, 8, 16, 195 ) );
                if ( armor_width > 0 ) {
                    overlay.draw_filled_rect_gradient_h( box_x, bottom_cursor_y, armor_width, armor_h,
                        D3DCOLOR_RGBA( 56, 189, 248, 250 ), D3DCOLOR_RGBA( 184, 69, 255, 242 ) );
                }
                bottom_cursor_y += armor_h + 3;
            }

            if ( config.draw_weapon || config.flag_bomb ) {
                if (frame_now >= display.next_weapon) {
                    display.weapon = get_weapon_name(entity_list, pawn);
                    display.next_weapon = frame_now + std::chrono::milliseconds(250);
                }
                const std::string& weapon = display.weapon;
                if ( config.draw_weapon && !weapon.empty( ) ) {
                    const unsigned long wep_col = ( weapon == "C4" )
                        ? D3DCOLOR_RGBA( 255, 215, 60, 250 )
                        : D3DCOLOR_RGBA( 218, 145, 255, 245 );
                    overlay.draw_text_small( weapon, center_x, bottom_cursor_y, wep_col, true, false );
                }
            }

            int flag_y = box_y + 1;
            const int flag_x = box_x + box_width + 7;
            auto draw_flag = [&]( const char* text, unsigned long color ) {
                if ( flag_x < screen_width - 8 && flag_y < screen_height - 12 ) {
                    overlay.draw_text_small( text, flag_x, flag_y, color, false, false );
                    flag_y += 12;
                }
            };
            if ( config.flag_bomb && display.weapon == "C4" )
                draw_flag( "BOMB", D3DCOLOR_RGBA( 255, 194, 71, 248 ) );
            if ( config.flag_flashed ) {
                const float flash = m_memory.read<float>( pawn + sdk::nv().m_flFlashDuration );
                if ( std::isfinite( flash ) && flash > 0.15f && flash < 20.0f )
                    draw_flag( "FLASHED", D3DCOLOR_RGBA( 255, 230, 135, 245 ) );
            }
            if ( config.flag_scoped && m_memory.read<std::uint8_t>( pawn + sdk::nv().m_bIsScoped ) )
                draw_flag( "SCOPED", D3DCOLOR_RGBA( 140, 214, 255, 242 ) );
            if ( config.flag_defusing && m_memory.read<std::uint8_t>( pawn + sdk::nv().m_bIsDefusing ) )
                draw_flag( "DEFUSING", D3DCOLOR_RGBA( 101, 211, 255, 248 ) );
            if ( config.flag_hostage && m_memory.read<std::uint8_t>( pawn + sdk::nv().m_bIsGrabbingHostage ) )
                draw_flag( "HOSTAGE", D3DCOLOR_RGBA( 255, 164, 105, 245 ) );
            if ( m_memory.read<std::uint8_t>( controller + sdk::nv().m_bPawnHasDefuser ) )
                draw_flag( "KIT", D3DCOLOR_RGBA( 101, 211, 255, 220 ) );
            if ( m_memory.read<std::uint8_t>( controller + sdk::nv().m_bPawnHasHelmet ) )
                draw_flag( "HELMET", D3DCOLOR_RGBA( 190, 205, 222, 210 ) );

            if ( config.draw_tracers ) {
                const int tracer_sx = screen_width / 2;
                const int tracer_sy = ( config.tracer_origin == 1 ) ? ( screen_height / 2 + 25 ) : screen_height;
                if ( config.tracer_origin == 1 ) {
                    // Leicht unterhalb des Fadenkreuzes ansetzen, damit das direkte Sichtfeld frei bleibt
                    overlay.draw_line( tracer_sx, tracer_sy,
                        center_x, box_y + box_height / 2, with_alpha( primary_color, 232 ) );
                    overlay.draw_filled_circle( center_x, box_y + box_height / 2, 2.2f, 10, primary_color );
                } else {
                    // Connect to feet for bottom-of-screen snaplines
                    overlay.draw_line( tracer_sx, tracer_sy,
                        static_cast< int >( feet_screen.x ), static_cast< int >( feet_screen.y ),
                        with_alpha( primary_color, 232 ) );
                    overlay.draw_filled_circle( static_cast< int >( feet_screen.x ),
                        static_cast< int >( feet_screen.y ), 2.2f, 10, primary_color );
                }
            }

            if ( config.draw_head_dot ) {
                sdk::vec3_t head_dot_screen{};
                const float head_dot_z = is_ducking ? 48.0f : 66.0f;
                if ( sdk::world_to_screen( screen_size, origin + sdk::vec3_t{ 0.0f, 0.0f, head_dot_z }, head_dot_screen, view_matrix ) &&
					 finite_vector( head_dot_screen ) ) {
                    const float r = std::clamp( box_height * 0.038f, 1.8f, 3.6f );
                    const int hx = static_cast< int >( head_dot_screen.x );
                    const int hy = static_cast< int >( head_dot_screen.y );
                    overlay.draw_filled_circle( hx, hy, r, 12, primary_color );
                    if ( r >= 2.5f ) {
                        overlay.draw_filled_circle( hx, hy, r * 0.45f, 8, D3DCOLOR_RGBA( 255, 255, 255, 250 ) );
                    }
                }
            }

            std::string tag_name;
            if (config.draw_names) {
                if (frame_now >= display.next_name) {
                    char raw_name[128]{};
                    m_memory.read_ex(raw_name, controller + sdk::nv().m_iszPlayerName,
                        sizeof(raw_name) - 1);
                    raw_name[sizeof(raw_name) - 1] = 0;
                    if (raw_name[0] >= 32 && raw_name[0] <= 126) {
                        display.name.assign(raw_name, (std::min)(std::strlen(raw_name), std::size_t(28)));
                    } else {
                        display.name.clear();
                    }
                    display.next_name = frame_now + std::chrono::milliseconds(750);
                }
                tag_name = display.name.empty() ? (teammate ? "TEAM" : "ENEMY") : display.name;
            }

            std::string tag_dist;
            if (config.draw_distance && has_local_origin) {
                tag_dist = "[" + std::to_string(static_cast<int>(std::round(dist_meters))) + "m]";
            }

            // Two-line tag: name above the frame, distance dimmer underneath it.
            // One combined string made the two values read at the same weight and
            // got long enough to clip on narrow boxes.
            if (!tag_name.empty() || !tag_dist.empty()) {
                const int tag_y = (box_y >= 28) ? (box_y - 16) : (box_y + box_height + 15);

                if (!tag_dist.empty()) {
                    const unsigned long dist_col = teammate
                        ? D3DCOLOR_RGBA( 126, 180, 206, 230 )
                        : D3DCOLOR_RGBA( 190, 176, 220, 230 );
                    overlay.draw_text_small( tag_dist, center_x, tag_y, dist_col, true, false );
                }
                if (!tag_name.empty()) {
                    const unsigned long name_col = teammate
                        ? D3DCOLOR_RGBA( 172, 234, 255, 255 )
                        : D3DCOLOR_RGBA( 250, 246, 255, 255 );
                    overlay.draw_text_small( tag_name, center_x, tag_y + 11, name_col, true, false );
                }
            }



            ++m_stats.drawn;
        }
    }



} // namespace esp

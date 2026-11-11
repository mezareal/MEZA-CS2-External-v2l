#pragma once

#include <cstdint>
#include <array>
#include <chrono>
#include <vector>

#include "../cs2_sdk/cs2_sdk.h"
#include "../dx_renderer/dx_renderer.h"
#include "../memory_manager/memory_manager.h"

namespace esp {

    struct settings {
        bool enabled = true;
        bool draw_box = true;         // Bounding Box an/aus schaltbar
        bool draw_teammates = false;
        int deathmatch_mode = 2; // 0 = Aus (Team), 1 = An (FFA), 2 = Auto (Automatische Erkennung)
        bool draw_health = true;
        bool draw_armor = true;       // Ruestungs-Balken
        bool draw_weapon = true;      // Aktive Waffe unter der Box
        bool draw_tracers = false;
        bool draw_names = true;
        bool draw_distance = true;    // Distanz in Metern [9m]
        bool draw_head_dot = true;    // Kopfpunkt
        bool draw_fov_circle = false; // Zentrierter FOV-Kreis
        bool draw_skeleton = true;
        bool draw_offscreen = true;
        bool draw_bomb_timer = true;
        bool draw_dropped_weapons = true;
        bool draw_grenades = true;
        bool flag_bomb = true;
        bool flag_flashed = true;
        bool flag_scoped = true;
        bool flag_defusing = true;
        bool flag_hostage = true;
        int fov_radius = 85;          // FOV-Kreis-Radius
        int max_distance = 150;       // Max ESP-Distanz in Metern (0 = unbegrenzt)
        int tracer_origin = 0;        // 0 = Bildschirm unten, 1 = Bildschirmmitte
        int box_style = 2; // 0 = outline, 1 = filled, 2 = corners
        // Obsidian Glass defaults: violet for enemies, cyan for teammates.
        // These match the menu without changing which ESP elements are drawn.
        unsigned long enemy_color = D3DCOLOR_RGBA( 184, 69, 255, 245 );
        unsigned long teammate_color = D3DCOLOR_RGBA( 92, 205, 255, 235 );
    };

    // Filled every frame. The menu renders this so a silent "nothing is drawn"
    // failure can be diagnosed in-game instead of guessed at.
    struct draw_stats {
        std::uintptr_t client_base = 0;
        std::uintptr_t entity_list = 0;
        std::uintptr_t local_pawn = 0;
        std::uintptr_t local_controller = 0;
        std::uintptr_t stride = 0;
        bool stride_detected = false;
        bool view_matrix_ok = false;
        int controllers = 0;
        int pawns = 0;
        int drawn = 0;
        int skipped_dead = 0;
        int skipped_team = 0;
        int skipped_distance = 0;
        int skipped_projection = 0;
    };

    class renderer {
    public:
        renderer( forceinline::memory_manager& memory, std::uintptr_t client_base );

        void draw( forceinline::dx_renderer& overlay, int screen_width, int screen_height );

        settings config{};

        const draw_stats& stats( ) const { return m_stats; }

    private:
        bool get_world_position( std::uintptr_t pawn, sdk::vec3_t& position ) const;
        int get_armor( std::uintptr_t controller, std::uintptr_t pawn ) const;
        std::string get_weapon_name( std::uintptr_t entity_list, std::uintptr_t pawn ) const;
        bool is_deathmatch_active( ) const;
        void refresh_world_cache( std::uintptr_t entity_list );

        forceinline::memory_manager& m_memory;
        std::uintptr_t m_client_base = 0;
        draw_stats m_stats{};
        // Resolved once per second. A CS2 update can change the stride, and a
        // wrong stride turns every entity read into garbage.
        std::uintptr_t m_entity_stride = 0;
        bool m_stride_detected = false;
        std::uintptr_t m_pending_stride = 0;
        int m_pending_stride_hits = 0;
        std::chrono::steady_clock::time_point m_next_stride_probe{};
        // Short-lived display cache cuts down expensive cross-process reads.
        // Cache invalidates automatically if a player/controller pointer changes.
        struct display_cache_entry {
            std::uintptr_t controller = 0;
            std::uintptr_t pawn = 0;
            std::string name;
            std::string weapon;
            std::chrono::steady_clock::time_point next_name{};
            std::chrono::steady_clock::time_point next_weapon{};
        };
        std::array<display_cache_entry, 65> m_display_cache{};
        enum class world_kind { weapon, grenade };
        struct world_cache_entry {
            std::uintptr_t entity = 0;
            world_kind kind = world_kind::weapon;
            std::string label;
        };
        std::vector<world_cache_entry> m_world_cache;
        std::chrono::steady_clock::time_point m_next_world_refresh{};
    };

} // namespace esp

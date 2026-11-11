#pragma once

#include <cstdint>
#include <chrono>
#include <vector>

#include "../cs2_sdk/cs2_sdk.h"
#include "../dx_renderer/dx_renderer.h"
#include "../memory_manager/memory_manager.h"

namespace aim {

    // External, mouse-driven aim assist. The cheat only holds PROCESS_VM_READ,
    // so view angles are never written; the crosshair is steered by injecting
    // relative mouse motion and re-measuring every frame (proportional servo).
    struct settings {
        bool enabled = false;
        int activation_mode = 0;   // 0 = halten, 1 = umschalten, 2 = immer aktiv
        int hotkey = 0x05;         // beliebige VK, im Menue per Tastendruck gebunden
        int fov_pixels = 140;      // Radius in Bildschirmpixeln
        int smooth = 4;            // 1 = direkt, 20 = weich
        int bone = 0;              // 0 = Kopf, 1 = Hals, 2 = Brust
        int max_distance_m = 80;
        bool teammates = false;
        bool draw_fov = true;
        bool draw_target = true;
        bool humanize = true;

        // ---- V25.5 additions ------------------------------------------------
        int priority = 0;          // 0 = Fadenkreuz, 1 = wenig HP, 2 = naechster, 3 = sichtbar zuerst
        bool visible_only = false; // nur Ziele mit gesetztem m_bSpotted
        int prediction_ms = 0;     // 0..200 ms Vorhalt auf die Zielgeschwindigkeit
        bool sticky_lock = true;   // Ziel kurz halten statt bei jedem Frame zu springen
        int lock_ms = 220;         // Haltefenster in ms
        int ring_style = 1;        // 0 = duenn, 1 = Marker, 2 = Glow
        bool draw_path = true;     // Linie Fadenkreuz -> Ziel
        bool distance_smoothing = true; // weiter entfernte Ziele werden weicher gezogen

        // ---- V25.7 additions: RCS, Auto-Bhop & Precision Scaling -------
        bool rcs_enabled = true;        // Recoil Compensation System
        float rcs_strength = 1.0f;      // 0.0 .. 2.0
        bool bhop = true;               // Auto Bunnyhop for rapid movement
        float sensitivity = 1.5f;       // In-game sensitivity scale
    };

    struct stats {
        bool hotkey_down = false;
        bool active = false;       // Aim wird diesen Frame ausgefuehrt
        int candidates = 0;        // Spieler nach Filter
        int in_fov = 0;            // davon im FOV
        bool has_target = false;
        bool sticky_hold = false;  // Ziel wurde ueber das Haltefenster gehalten
        float target_fov = 0.0f;   // Pixelabstand zum Fadenkreuz
        int target_health = 0;
        float target_distance_m = 0.0f;
        int move_x = 0;            // letzter injizierter Schritt
        int move_y = 0;
        bool game_foreground = false;
        bool team_verified = false; // Team-Offsetkalibrierung erfolgreich
        float gain = 1.0f;          // aktueller Servo-Gain (adaptiv)
        bool input_ok = true;       // SendInput accepted the last movement
        bool target_changed = false;
    };

    class aimbot {
    public:
        aimbot( forceinline::memory_manager& memory, std::uintptr_t client_base );

        settings config {};
        const stats& state( ) const { return m_stats; }

        // Reads the world, picks a target, injects the mouse step and draws the
        // FOV ring. Safe to call every frame; does nothing when disabled or when
        // the game window is not in the foreground.
        void update( forceinline::dx_renderer& overlay, bool menu_open, int screen_width, int screen_height );

    private:
        struct candidate {
            std::uintptr_t pawn = 0;
            sdk::vec3_t point {};
            float fov_px = 0.0f;
            float meters = 0.0f;
            int health = 0;
            bool spotted = false;
            float score = 0.0f;
        };

        float bone_height( int bone, bool ducking ) const;
        bool  aim_point_for( const sdk::player_entry& player, sdk::vec3_t& point );
        bool  game_foreground( ) const;
        bool  inject_step( int dx, int dy );
        float score( const candidate& c ) const;
        void  draw_ring( forceinline::dx_renderer& overlay, int cx, int cy, bool locked ) const;
        void  draw_lock( forceinline::dx_renderer& overlay, const candidate& c, int cx, int cy ) const;

        forceinline::memory_manager& m_memory;
        std::uintptr_t m_client_base = 0;
        stats m_stats {};
        // Stride is probed once per second, then reused for every walk. Probing
        // per frame costs ~120 extra kernel transitions for no benefit.
        std::uintptr_t m_entity_stride = 0;
        std::chrono::steady_clock::time_point m_next_stride_probe {};
        // Team offset self-calibration. A wrong byte is the difference between
        // "aims at teammates" and a working filter, so it is re-probed.
        std::chrono::steady_clock::time_point m_next_team_probe {};
        bool m_team_verified = false;
        bool m_toggled_on = false;
        bool m_prev_hotkey = false;
        std::chrono::steady_clock::time_point m_last_shot {};
        // Adaptive gain: game-side sensitivity scales injected counts, so the
        // servo measures whether the error actually shrinks and corrects itself.
        float m_gain = 1.0f;
        float m_last_error = -1.0f;
        float m_residual_x = 0.0f;
        float m_residual_y = 0.0f;
        int m_settle_frames = 0;
        int m_spray_ticks = 0;
        std::uintptr_t m_last_target = 0;
        std::uint32_t m_rng_state = 0x9E3779B9u;
        // Target stickiness: the last locked pawn is reused for lock_ms so the
        // crosshair does not hop between two equally close players.
        std::uintptr_t m_sticky_pawn = 0;
        std::chrono::steady_clock::time_point m_sticky_seen {};
        std::vector<candidate> m_candidates;
    };

} // namespace aim

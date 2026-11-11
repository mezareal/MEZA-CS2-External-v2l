#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include "../memory_manager/memory_manager.h"

namespace sdk {

    // =========================================================
    // Runtime offset table (client.dll)
    //
    // These values are only the compiled fallback. CS2 re-links client.dll on
    // nearly every update, so every value below can be overridden at runtime
    // from the [Offsets] section of config.ini without a rebuild:
    //
    //   [Offsets]
    //   EntityList = 0x271EB08
    //   LocalPlayerPawn = 0x25698C8
    //   ...
    //
    // Values are validated by sdk::probe() before the ESP trusts them.
    // =========================================================
    struct offsets_t {
        std::uintptr_t entity_list             = 0x271FBB8;  // dwEntityList
        std::uintptr_t local_player_controller = 0x2542548;  // dwLocalPlayerController
        std::uintptr_t local_player_pawn       = 0x256A948;  // dwLocalPlayerPawn
        std::uintptr_t view_matrix             = 0x2570250;  // dwViewMatrix
        std::uintptr_t global_vars             = 0x2235108;  // dwGlobalVars
        std::uintptr_t planted_c4              = 0x24D0B70;  // dwPlantedC4
        std::uintptr_t game_rules              = 0x256A3F8;  // dwGameRules

        // Layout of the flat entity list. Stride is 0x70 (112 bytes).
        std::uintptr_t entity_stride           = 0x70;
        std::uintptr_t highest_entity_index    = 0x2120;     // CGameEntitySystem::m_highestEntityIndex
    };

    // =========================================================
    // Schema netvars. Same override mechanism ([Offsets] section).
    // =========================================================
    struct netvars_t {
        // C_BaseEntity
        std::uintptr_t m_pGameSceneNode     = 0x330;
        std::uintptr_t m_iHealth            = 0x34C;
        std::uintptr_t m_lifeState          = 0x354;
        std::uintptr_t m_iTeamNum           = 0x3E7;
        std::uintptr_t m_fFlags             = 0x3F4;
        std::uintptr_t m_vecAbsVelocity     = 0x3F8;
        std::uintptr_t m_hOwnerEntity       = 0x520;

        // CGameSceneNode / CSkeletonInstance
        std::uintptr_t m_vecAbsOrigin       = 0xC8;
        std::uintptr_t m_vecOrigin          = 0x80;
        std::uintptr_t m_modelState         = 0x140;
        std::uintptr_t m_boneArray          = 0x80;

        // CBasePlayerController / CCSPlayerController
        std::uintptr_t m_hPawn              = 0x6BC;
        std::uintptr_t m_iszPlayerName      = 0x6FC;
        std::uintptr_t m_hPlayerPawn        = 0x92C;
        std::uintptr_t m_bPawnIsAlive       = 0x934;
        std::uintptr_t m_iPawnHealth        = 0x938;
        std::uintptr_t m_iPawnArmor         = 0x93C;
        std::uintptr_t m_bPawnHasDefuser    = 0x940;
        std::uintptr_t m_bPawnHasHelmet     = 0x941;
        std::uintptr_t m_sSanitizedPlayerName = 0x878;

        // C_CSPlayerPawn
        std::uintptr_t m_bSpotted           = 0x8;     // inside m_entitySpottedState
        std::uintptr_t m_pWeaponServices    = 0x12F0;
        std::uintptr_t m_pItemServices      = 0x12F8;
        std::uintptr_t m_ArmorValue         = 0x1ED0;
        std::uintptr_t m_hActiveWeapon      = 0x60;
        std::uintptr_t m_iItemDefinitionIndexOffset = 0x14FA;
        std::uintptr_t m_bIsScoped          = 0x1EA0;
        std::uintptr_t m_bIsDefusing        = 0x1EA2;
        std::uintptr_t m_bIsGrabbingHostage = 0x1EA3;
        std::uintptr_t m_flFlashDuration    = 0x1510;
        std::uintptr_t m_vecViewOffset      = 0xF60;
        std::uintptr_t m_entitySpottedState = 0x1E88;

        // Entity identity
        std::uintptr_t m_pEntityIdentity    = 0x10;
        std::uintptr_t m_designerName       = 0x20;

        // C_PlantedC4
        std::uintptr_t m_bBombTicking       = 0x1288;
        std::uintptr_t m_nBombSite          = 0x128C;
        std::uintptr_t m_flC4Blow           = 0x12B8;
        std::uintptr_t m_bBeingDefused      = 0x12C4;
        std::uintptr_t m_flDefuseCountDown  = 0x12D8;
        std::uintptr_t m_bBombDefused       = 0x12DC;

        // C_CSGameRules
        std::uintptr_t m_pGameModeRules     = 0xDA0;
        std::uintptr_t m_flDMBonusTimeLength = 0x34;
    };

    offsets_t& off();
    netvars_t& nv();

    // Reads the [Offsets] section of the given ini file and applies any key that
    // is present. Missing keys keep their compiled fallback.
    void load_offset_overrides(const std::string& ini_path);

    // Collects every key=value pair currently present in [Offsets] so the config
    // writer can round-trip user supplied values instead of dropping them.
    std::string serialize_offset_overrides(const std::string& ini_path);

    bool plausible_pointer(std::uintptr_t value);

    // =========================================================
    // vec3_t
    // =========================================================
    struct vec3_t {
        float x = 0, y = 0, z = 0;

        vec3_t();
        vec3_t(float _x, float _y, float _z);

        vec3_t operator+(const vec3_t& other) const;
        vec3_t operator+=(const vec3_t& other);
        vec3_t operator-(const vec3_t& other) const;
        vec3_t operator-=(const vec3_t& other);
        vec3_t operator*(float factor) const;
        vec3_t operator*(const vec3_t& other) const;
        vec3_t operator*=(float factor);
        vec3_t operator*=(const vec3_t& other);
        vec3_t operator/(float factor) const;
        vec3_t operator/(const vec3_t& other) const;
        vec3_t operator/=(float factor);
        vec3_t operator/=(const vec3_t& other);

        bool  is_zero() const;
        float dot(const vec3_t& other) const;
        float dist(const vec3_t& other) const;
        float dist_2d(const vec3_t& other) const;
        float length() const;
        float length_sqr() const;
    };

    // =========================================================
    // view_matrix_t
    // =========================================================
    struct view_matrix_t {
        float* operator[](int index) {
            return matrix[index];
        }
        const float* operator[](int index) const {
            return matrix[index];
        }
        float matrix[4][4];
    };

    bool world_to_screen(const vec3_t& screen_size, const vec3_t& pos, vec3_t& out, const view_matrix_t& matrix);

    // =========================================================
    // Entity walking shared by ESP and the aimbot
    // =========================================================
    struct player_entry {
        std::uintptr_t controller = 0;
        std::uintptr_t pawn = 0;
        vec3_t origin{};
        vec3_t velocity{};        // units/s, used for aim lead
        int health = 0;
        std::uint8_t team = 0;
        bool ducking = false;
        bool spotted = false;     // m_entitySpottedState.m_bSpotted
    };

    struct walk_report {
        std::uintptr_t entity_list = 0;      // dereferenced list pointer
        std::uintptr_t controller_page = 0;  // first chunk
        std::uintptr_t stride = 0;           // stride actually used
        int controllers = 0;                 // non-null controller slots
        int pawns = 0;                       // pawns resolved from a handle
        int alive = 0;                       // health/lifestate/team sane
        bool stride_detected = false;        // false => compiled value used blind
    };

    // Detects the flat-entity-list stride at runtime by validating controller
    // slots. Returns the compiled value if no candidate validates.
    std::uintptr_t detect_entity_stride(forceinline::memory_manager& memory,
                                        std::uintptr_t entity_list,
                                        walk_report* report = nullptr);

    // Resolves every controller's pawn pointer. No health/team filtering, so it
    // stays usable while the schema is still being calibrated.
    std::vector<std::uintptr_t> collect_pawns(forceinline::memory_manager& memory,
                                              std::uintptr_t entity_list,
                                              std::uintptr_t stride,
                                              int max_slots = 64);

    // Reads one bone position out of the player model. CS2 stores 0x20-byte
    // transforms; the position is the first three floats.
    bool read_bone_position(forceinline::memory_manager& memory,
                            std::uintptr_t scene_node,
                            int bone_index,
                            vec3_t& out);

    // Team offsets move with every schema update, and a wrong byte silently
    // makes the aimbot treat teammates as enemies. This probes a list of
    // candidate offsets and writes the one that produces sane values
    // (all 0..3, local player 2 or 3, at least one differing value) into
    // nv().m_iTeamNum. Returns false when nothing validates.
    bool calibrate_team_offset(forceinline::memory_manager& memory,
                               std::uintptr_t entity_list,
                               std::uintptr_t local_pawn,
                               std::uintptr_t stride);

    // Resolves every alive player pawn. skip_pawn / skip_controller may be set to
    // exclude the local player. forced_stride skips the probe (0 = auto) so the
    // caller can cache the detected stride instead of probing every frame.
    std::vector<player_entry> collect_players(forceinline::memory_manager& memory,
                                              std::uintptr_t entity_list,
                                              std::uintptr_t skip_pawn,
                                              std::uintptr_t skip_controller,
                                              std::uintptr_t forced_stride = 0,
                                              walk_report* report = nullptr);

} // namespace sdk

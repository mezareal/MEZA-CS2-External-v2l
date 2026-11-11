#include "cs2_sdk.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace sdk {

    // =========================================================
    // vec3_t Implementierung
    // =========================================================
    vec3_t::vec3_t() { x = y = z = 0; }
    vec3_t::vec3_t(float _x, float _y, float _z) { x = _x; y = _y; z = _z; }

    vec3_t vec3_t::operator+(const vec3_t& other) const { return { x + other.x, y + other.y, z + other.z }; }
    vec3_t vec3_t::operator+=(const vec3_t& other) { x += other.x; y += other.y; z += other.z; return *this; }
    vec3_t vec3_t::operator-(const vec3_t& other) const { return { x - other.x, y - other.y, z - other.z }; }
    vec3_t vec3_t::operator-=(const vec3_t& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
    vec3_t vec3_t::operator*(float factor) const { return { x * factor, y * factor, z * factor }; }
    vec3_t vec3_t::operator*(const vec3_t& other) const { return { x * other.x, y * other.y, z * other.z }; }
    vec3_t vec3_t::operator*=(float factor) { x *= factor; y *= factor; z *= factor; return *this; }
    vec3_t vec3_t::operator*=(const vec3_t& other) { x *= other.x; y *= other.y; z *= other.z; return *this; }
    vec3_t vec3_t::operator/(float factor) const { return { x / factor, y / factor, z / factor }; }
    vec3_t vec3_t::operator/(const vec3_t& other) const { return { x / other.x, y / other.y, z / other.z }; }
    vec3_t vec3_t::operator/=(float factor) { x /= factor; y /= factor; z /= factor; return *this; }
    vec3_t vec3_t::operator/=(const vec3_t& other) { x /= other.x; y /= other.y; z /= other.z; return *this; }

    bool  vec3_t::is_zero() const { return x == 0 && y == 0 && z == 0; }
    float vec3_t::dot(const vec3_t& other) const { return x * other.x + y * other.y + z * other.z; }
    float vec3_t::length_sqr() const { return x * x + y * y + z * z; }
    float vec3_t::length() const { return sqrtf(length_sqr()); }

    float vec3_t::dist(const vec3_t& other) const {
        return (*this - other).length();
    }

    // 2D-Distanz: ignoriert Z-Achse
    float vec3_t::dist_2d(const vec3_t& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        return sqrtf(dx * dx + dy * dy);
    }

    // =========================================================
    // world_to_screen
    // =========================================================
    bool world_to_screen(const vec3_t& screen_size, const vec3_t& pos, vec3_t& out, const view_matrix_t& matrix) {
        out.x = matrix[0][0] * pos.x + matrix[0][1] * pos.y + matrix[0][2] * pos.z + matrix[0][3];
        out.y = matrix[1][0] * pos.x + matrix[1][1] * pos.y + matrix[1][2] * pos.z + matrix[1][3];

        float w = matrix[3][0] * pos.x + matrix[3][1] * pos.y + matrix[3][2] * pos.z + matrix[3][3];

        // Hinter der Kamera - nicht zeichnen
        if (w < 0.01f)
            return false;

        float inv_w = 1.f / w;
        out.x *= inv_w;
        out.y *= inv_w;

        float x = screen_size.x * 0.5f;
        float y = screen_size.y * 0.5f;

        x += 0.5f * out.x * screen_size.x + 0.5f;
        y -= 0.5f * out.y * screen_size.y + 0.5f;

        out.x = x;
        out.y = y;

        return true;
    }

    // =========================================================
    // Offset table access
    // =========================================================
    offsets_t& off() {
        static offsets_t table{};
        return table;
    }

    netvars_t& nv() {
        static netvars_t table{};
        return table;
    }

    bool plausible_pointer(std::uintptr_t value) {
        // User mode canonical range. Anything outside is a failed read or junk.
        return value > 0x10000ULL && value < 0x00007FFFFFFE0000ULL;
    }

    namespace {
        bool parse_offset(std::string_view text, std::uintptr_t& out) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\r' || text.back() == '\n' || text.back() == '\t'))
                text.remove_suffix(1);
            if (text.empty())
                return false;

            // Tolerate an "client.dll+0x..." prefix so values can be pasted whole.
            const std::size_t plus = text.find('+');
            if (plus != std::string_view::npos)
                text = text.substr(plus + 1);
            if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
                text = text.substr(2);
            if (text.empty())
                return false;

            std::uintptr_t value = 0;
            for (const char ch : text) {
                int digit = -1;
                if (ch >= '0' && ch <= '9') digit = ch - '0';
                else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
                else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
                else return false;
                value = (value << 4) | static_cast<std::uintptr_t>(digit);
            }
            out = value;
            return value != 0;
        }

        struct offset_key {
            const char* name;
            std::uintptr_t offsets_t::* client_field;
            std::uintptr_t netvars_t::* netvar_field;
        };

        // Keys accepted inside [Offsets]. client_field and netvar_field are
        // mutually exclusive; exactly one is non-null per row.
        const offset_key k_offset_keys[] = {
            { "EntityList",             &offsets_t::entity_list,             nullptr },
            { "LocalPlayerController",  &offsets_t::local_player_controller, nullptr },
            { "LocalPlayerPawn",        &offsets_t::local_player_pawn,       nullptr },
            { "ViewMatrix",             &offsets_t::view_matrix,             nullptr },
            { "GlobalVars",             &offsets_t::global_vars,             nullptr },
            { "PlantedC4",              &offsets_t::planted_c4,              nullptr },
            { "GameRules",              &offsets_t::game_rules,              nullptr },
            { "EntityStride",           &offsets_t::entity_stride,           nullptr },
            { "HighestEntityIndex",     &offsets_t::highest_entity_index,    nullptr },

            { "m_pGameSceneNode",       nullptr, &netvars_t::m_pGameSceneNode },
            { "m_iHealth",              nullptr, &netvars_t::m_iHealth },
            { "m_lifeState",            nullptr, &netvars_t::m_lifeState },
            { "m_iTeamNum",             nullptr, &netvars_t::m_iTeamNum },
            { "m_fFlags",               nullptr, &netvars_t::m_fFlags },
            { "m_vecAbsOrigin",         nullptr, &netvars_t::m_vecAbsOrigin },
            { "m_modelState",           nullptr, &netvars_t::m_modelState },
            { "m_boneArray",            nullptr, &netvars_t::m_boneArray },
            { "m_hPlayerPawn",          nullptr, &netvars_t::m_hPlayerPawn },
            { "m_iszPlayerName",        nullptr, &netvars_t::m_iszPlayerName },
            { "m_ArmorValue",           nullptr, &netvars_t::m_ArmorValue },
            { "m_pWeaponServices",      nullptr, &netvars_t::m_pWeaponServices },
            { "m_hActiveWeapon",        nullptr, &netvars_t::m_hActiveWeapon },
            { "m_vecViewOffset",        nullptr, &netvars_t::m_vecViewOffset },
            { "m_entitySpottedState",   nullptr, &netvars_t::m_entitySpottedState },
            { "m_bSpotted",             nullptr, &netvars_t::m_bSpotted },
            { "m_vecAbsVelocity",       nullptr, &netvars_t::m_vecAbsVelocity },
        };
    }

    void load_offset_overrides(const std::string& ini_path) {
        char buffer[128]{};
        for (const auto& key : k_offset_keys) {
            GetPrivateProfileStringA("Offsets", key.name, "", buffer, sizeof(buffer), ini_path.c_str());
            std::uintptr_t value = 0;
            if (!parse_offset(buffer, value))
                continue;
            if (key.client_field)
                off().*key.client_field = value;
            else if (key.netvar_field)
                nv().*key.netvar_field = value;
        }
    }

    std::string serialize_offset_overrides(const std::string& ini_path) {
        std::string out;
        char buffer[128]{};
        for (const auto& key : k_offset_keys) {
            buffer[0] = 0;
            GetPrivateProfileStringA("Offsets", key.name, "", buffer, sizeof(buffer), ini_path.c_str());
            if (buffer[0] == 0)
                continue;

            char line[192]{};
            const auto value = key.client_field ? (off().*key.client_field) : (nv().*key.netvar_field);
            snprintf(line, sizeof(line), "%s = 0x%llX\n", key.name,
                     static_cast<unsigned long long>(value));
            out += line;
        }
        return out;
    }

    // =========================================================
    // Entity list walking
    // =========================================================
    std::uintptr_t detect_entity_stride(forceinline::memory_manager& memory,
                                        std::uintptr_t entity_list,
                                        walk_report* report) {
        if (report)
            report->entity_list = entity_list;

        if (!plausible_pointer(entity_list))
            return off().entity_stride;

        const auto controller_page = memory.read<std::uintptr_t>(entity_list + 0x10);
        if (report)
            report->controller_page = controller_page;
        if (!plausible_pointer(controller_page))
            return off().entity_stride;

        // The compiled stride is tried first so a healthy build keeps working
        // even if a probe slot happens to be empty.
        std::array<std::uintptr_t, 6> candidates{};
        std::size_t candidate_count = 0;
        candidates[candidate_count++] = off().entity_stride ? off().entity_stride : 0x70;
        for (const std::uintptr_t fallback : { std::uintptr_t(0x70), std::uintptr_t(0x78),
                                               std::uintptr_t(0x80), std::uintptr_t(0x68),
                                               std::uintptr_t(0x88) }) {
            if (fallback == candidates[0])
                continue;
            if (candidate_count < candidates.size())
                candidates[candidate_count++] = fallback;
        }

        for (std::size_t c = 0; c < candidate_count; ++c) {
            const std::uintptr_t stride = candidates[c];
            if (stride == 0)
                continue;

            int controllers = 0;
            int verified = 0;
            for (int slot = 1; slot <= 32; ++slot) {
                const auto controller = memory.read<std::uintptr_t>(
                    controller_page + stride * static_cast<std::uintptr_t>(slot));
                if (!plausible_pointer(controller))
                    continue;
                ++controllers;

                const auto handle = memory.read<std::uint32_t>(controller + nv().m_hPlayerPawn);
                const unsigned int index = handle & 0x7FFFu;
                if (index == 0 || index == 0x7FFFu)
                    continue;

                const std::size_t page_index = static_cast<std::size_t>(index) >> 9;
                const auto page = memory.read<std::uintptr_t>(entity_list + 0x10 + 0x8 * page_index);
                if (!plausible_pointer(page))
                    continue;
                const auto pawn = memory.read<std::uintptr_t>(
                    page + stride * static_cast<std::uintptr_t>(index & 0x1FFu));
                if (!plausible_pointer(pawn))
                    continue;

                const int health = memory.read<int>(pawn + nv().m_iHealth);
                if (health >= 0 && health <= 100)
                    ++verified;
            }

            if (report && controllers > 0)
                report->controllers = controllers;

            if (verified >= 1) {
                if (report) {
                    report->stride = stride;
                    report->stride_detected = true;
                }
                return stride;
            }
        }

        if (report)
            report->stride = off().entity_stride;
        return off().entity_stride;
    }

    std::vector<std::uintptr_t> collect_pawns(forceinline::memory_manager& memory,
                                              std::uintptr_t entity_list,
                                              std::uintptr_t stride,
                                              int max_slots) {
        std::vector<std::uintptr_t> pawns;
        if (!plausible_pointer(entity_list) || !stride)
            return pawns;

        const auto controller_page = memory.read<std::uintptr_t>(entity_list + 0x10);
        if (!plausible_pointer(controller_page))
            return pawns;

        for (int slot = 1; slot <= max_slots; ++slot) {
            const auto controller = memory.read<std::uintptr_t>(
                controller_page + stride * static_cast<std::uintptr_t>(slot));
            if (!plausible_pointer(controller))
                continue;

            const auto handle = memory.read<std::uint32_t>(controller + nv().m_hPlayerPawn);
            const unsigned int index = handle & 0x7FFFu;
            if (index == 0 || index == 0x7FFFu)
                continue;

            const auto page = memory.read<std::uintptr_t>(entity_list + 0x10 + 0x8 * (index >> 9));
            if (!plausible_pointer(page))
                continue;

            const auto pawn = memory.read<std::uintptr_t>(
                page + stride * static_cast<std::uintptr_t>(index & 0x1FFu));
            if (plausible_pointer(pawn))
                pawns.push_back(pawn);
        }
        return pawns;
    }

    bool read_bone_position(forceinline::memory_manager& memory,
                            std::uintptr_t scene_node,
                            int bone_index,
                            vec3_t& out) {
        if (!plausible_pointer(scene_node) || bone_index < 0 || bone_index > 127)
            return false;

        const auto bone_array = memory.read<std::uintptr_t>(
            scene_node + nv().m_modelState + nv().m_boneArray);
        if (!plausible_pointer(bone_array))
            return false;

        out = memory.read<vec3_t>(bone_array + static_cast<std::uintptr_t>(bone_index) * 0x20u);
        return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z) && !out.is_zero();
    }

    bool calibrate_team_offset(forceinline::memory_manager& memory,
                               std::uintptr_t entity_list,
                               std::uintptr_t local_pawn,
                               std::uintptr_t stride) {
        if (!plausible_pointer(local_pawn) || !stride)
            return false;

        const auto pawns = collect_pawns(memory, entity_list, stride, 64);
        if (pawns.empty())
            return false;

        // Current offset first so a healthy schema costs one candidate. The rest
        // are the offsets m_iTeamNum has occupied since the CS2 beta.
        const std::uintptr_t candidates[] = {
            nv().m_iTeamNum, 0x3E3, 0x3CB, 0x3D3, 0x3DB, 0x3E0, 0x3F0, 0x400
        };

        for (std::uintptr_t candidate : candidates) {
            const auto local_value = memory.read<std::uint8_t>(local_pawn + candidate);
            if (local_value != 2 && local_value != 3)
                continue;

            int same = 0, differing = 0;
            bool rejected = false;
            for (std::uintptr_t pawn : pawns) {
                const auto value = memory.read<std::uint8_t>(pawn + candidate);
                if (value > 3) { rejected = true; break; }
                if (value == local_value)
                    ++same;
                else if (value != 0)
                    ++differing;
            }
            if (rejected)
                continue;
            if (same + differing == 0)
                continue;
            // A lobby with no enemy present cannot prove the offset. Keep the
            // compiled value in that case instead of guessing from one side.
            if (differing == 0 && pawns.size() > 1 && candidate != nv().m_iTeamNum)
                continue;

            nv().m_iTeamNum = candidate;
            return true;
        }
        return false;
    }

    std::vector<player_entry> collect_players(forceinline::memory_manager& memory,
                                              std::uintptr_t entity_list,
                                              std::uintptr_t skip_pawn,
                                              std::uintptr_t skip_controller,
                                              std::uintptr_t forced_stride,
                                              walk_report* report) {
        std::vector<player_entry> players;
        if (!plausible_pointer(entity_list))
            return players;

        const std::uintptr_t stride = forced_stride ? forced_stride
            : detect_entity_stride(memory, entity_list, report);
        if (!stride)
            return players;
        if (report && !report->stride)
            report->stride = stride;

        const auto controller_page = memory.read<std::uintptr_t>(entity_list + 0x10);
        if (!plausible_pointer(controller_page))
            return players;

        for (int slot = 1; slot <= 64; ++slot) {
            const auto controller = memory.read<std::uintptr_t>(
                controller_page + stride * static_cast<std::uintptr_t>(slot));
            if (!plausible_pointer(controller))
                continue;
            if (skip_controller && controller == skip_controller)
                continue;
            if (report)
                ++report->controllers;

            const auto handle = memory.read<std::uint32_t>(controller + nv().m_hPlayerPawn);
            const unsigned int index = handle & 0x7FFFu;
            if (index == 0 || index == 0x7FFFu)
                continue;

            const std::size_t page_index = static_cast<std::size_t>(index) >> 9;
            const auto page = memory.read<std::uintptr_t>(entity_list + 0x10 + 0x8 * page_index);
            if (!plausible_pointer(page))
                continue;
            const auto pawn = memory.read<std::uintptr_t>(
                page + stride * static_cast<std::uintptr_t>(index & 0x1FFu));
            if (!plausible_pointer(pawn))
                continue;
            if (skip_pawn && pawn == skip_pawn)
                continue;
            if (report)
                ++report->pawns;

            const int health = memory.read<int>(pawn + nv().m_iHealth);
            const auto life_state = memory.read<std::uint8_t>(pawn + nv().m_lifeState);
            const auto team = memory.read<std::uint8_t>(pawn + nv().m_iTeamNum);
            if (health <= 0 || health > 100 || life_state != 0)
                continue;
            if (team > 3)
                continue;

            const auto scene_node = memory.read<std::uintptr_t>(pawn + nv().m_pGameSceneNode);
            if (!plausible_pointer(scene_node))
                continue;
            const auto origin = memory.read<vec3_t>(scene_node + nv().m_vecAbsOrigin);
            if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z))
                continue;
            if (origin.is_zero())
                continue;

            const std::uint32_t flags = memory.read<std::uint32_t>(pawn + nv().m_fFlags);

            // Velocity is only used for aim lead, so a single magnitude sanity
            // check is enough: CS2 player speed never exceeds ~4000 u/s.
            sdk::vec3_t velocity = memory.read<sdk::vec3_t>(pawn + nv().m_vecAbsVelocity);
            if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z) ||
                velocity.length_sqr() > 4000.0f * 4000.0f) {
                velocity = sdk::vec3_t{};
            }

            const auto spotted = memory.read<std::uint8_t>(pawn + nv().m_entitySpottedState + nv().m_bSpotted);

            player_entry entry{};
            entry.controller = controller;
            entry.pawn = pawn;
            entry.origin = origin;
            entry.velocity = velocity;
            entry.health = health;
            entry.team = team;
            entry.ducking = (flags & (1u << 1)) != 0;
            entry.spotted = spotted != 0;
            players.push_back(entry);
            if (report)
                ++report->alive;
        }

        return players;
    }

} // namespace sdk

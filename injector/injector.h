#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

// Pre-flight before the overlay opens. The gate used to hand control straight to
// the cheat; now it waits for the game, proves the client module is mapped and
// reports whether a match is actually running, so the ESP does not start against
// a lobby that is about to be replaced by a map load.
namespace injector {

    enum class stage {
        idle,
        wait_process,   // cs2.exe not running yet
        wait_modules,   // process found, client.dll not mapped yet
        wait_match,     // both mapped, no local pawn yet (menu / loading)
        payload,        // optional: dropping a payload DLL into the process
        ready,          // game reachable, match live
        failed
    };

    struct status {
        stage now = stage::idle;
        DWORD pid = 0;
        bool client_loaded = false;
        bool in_match = false;
        bool payload_present = false;
        bool payload_injected = false;
        std::wstring line;      // ready to draw in the gate window
    };

    // Copy under lock: the worker mutates the status block while the gate paints.
    status snapshot();
    bool running();             // worker still working
    bool finished();            // ready or failed
    bool ok();

    // Starts the pre-flight worker. Safe to call repeatedly; a second call while
    // a worker is live is ignored.
    void start_async();

    // Restarts from idle after a failure.
    void reset();

} // namespace injector

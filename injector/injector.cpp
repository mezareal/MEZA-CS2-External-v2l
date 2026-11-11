#include "injector.h"

#include <TlHelp32.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "../cs2_sdk/cs2_sdk.h"

namespace injector {

    namespace {
        std::mutex g_mutex;
        status g_status{};
        std::thread g_worker;
        std::atomic<bool> g_running{ false };

        using clock_type = std::chrono::steady_clock;

        void publish( stage next, const std::wstring& line ) {
            std::lock_guard<std::mutex> lock( g_mutex );
            g_status.now = next;
            g_status.line = line;
        }

        void publish_line( const std::wstring& line ) {
            std::lock_guard<std::mutex> lock( g_mutex );
            g_status.line = line;
        }

        void fail( const std::wstring& line ) {
            std::lock_guard<std::mutex> lock( g_mutex );
            g_status.now = stage::failed;
            g_status.line = line;
        }

        DWORD find_process( const wchar_t* name ) {
            const HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
            if ( snapshot == INVALID_HANDLE_VALUE )
                return 0;
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof( entry );
            DWORD pid = 0;
            if ( Process32FirstW( snapshot, &entry ) ) {
                do {
                    if ( !_wcsicmp( entry.szExeFile, name ) ) {
                        pid = entry.th32ProcessID;
                        break;
                    }
                } while ( Process32NextW( snapshot, &entry ) );
            }
            CloseHandle( snapshot );
            return pid;
        }

        std::uintptr_t module_base( DWORD pid, const wchar_t* module ) {
            const HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid );
            if ( snapshot == INVALID_HANDLE_VALUE )
                return 0;
            MODULEENTRY32W entry{};
            entry.dwSize = sizeof( entry );
            std::uintptr_t base = 0;
            if ( Module32FirstW( snapshot, &entry ) ) {
                do {
                    if ( !_wcsicmp( entry.szModule, module ) ) {
                        base = reinterpret_cast<std::uintptr_t>( entry.modBaseAddr );
                        break;
                    }
                } while ( Module32NextW( snapshot, &entry ) );
            }
            CloseHandle( snapshot );
            return base;
        }

        std::wstring own_directory() {
            wchar_t path[ MAX_PATH ]{};
            const DWORD length = GetModuleFileNameW( nullptr, path, MAX_PATH );
            if ( length == 0 )
                return {};
            std::wstring dir( path, length );
            const auto slash = dir.find_last_of( L"\\/" );
            if ( slash != std::wstring::npos )
                dir.erase( slash + 1 );
            return dir;
        }

        // Optional in-process payload. Everything the ESP needs is read from the
        // game process externally, so a payload is not required; if one sits next
        // to VEX.exe it is loaded so the shared build keeps working.
        std::wstring find_payload() {
            const std::wstring dir = own_directory();
            if ( dir.empty() )
                return {};
            static const wchar_t* const names[] = { L"VEX_PAYLOAD.dll", L"payload.dll", L"vex_payload.dll" };
            for ( const wchar_t* name : names ) {
                const std::wstring full = dir + name;
                if ( GetFileAttributesW( full.c_str() ) != INVALID_FILE_ATTRIBUTES )
                    return full;
            }
            return {};
        }

        bool inject_dll( DWORD pid, const std::wstring& dll ) {
            const HANDLE process = OpenProcess(
                PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid );
            if ( !process )
                return false;

            const SIZE_T size = ( dll.size() + 1 ) * sizeof( wchar_t );
            void* remote = VirtualAllocEx( process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
            bool injected = false;
            if ( remote && WriteProcessMemory( process, remote, dll.c_str(), size, nullptr ) ) {
                const HMODULE kernel = GetModuleHandleW( L"kernel32.dll" );
                const auto load_library = kernel
                    ? reinterpret_cast<LPTHREAD_START_ROUTINE>( GetProcAddress( kernel, "LoadLibraryW" ) )
                    : nullptr;
                if ( load_library ) {
                    const HANDLE thread = CreateRemoteThread( process, nullptr, 0, load_library, remote, 0, nullptr );
                    if ( thread ) {
                        WaitForSingleObject( thread, 5000 );
                        DWORD exit_code = 0;
                        if ( GetExitCodeThread( thread, &exit_code ) )
                            injected = exit_code != 0;
                        CloseHandle( thread );
                    }
                }
            }
            if ( remote )
                VirtualFreeEx( process, remote, 0, MEM_RELEASE );
            CloseHandle( process );
            return injected;
        }

        template <typename T>
        bool read_remote( HANDLE process, std::uintptr_t address, T& out ) {
            SIZE_T read = 0;
            return ReadProcessMemory( process, reinterpret_cast<LPCVOID>( address ), &out, sizeof( T ), &read ) &&
                   read == sizeof( T );
        }

        // A live local pawn with health > 0 is the only reliable "in a match"
        // signal from outside the process. Offsets here are the compiled
        // defaults: the gate runs before config.ini overrides are applied.
        bool match_running( DWORD pid, std::uintptr_t client_base ) {
            const HANDLE process = OpenProcess( PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid );
            if ( !process )
                return false;

            std::uintptr_t pawn = 0;
            bool live = false;
            if ( read_remote( process, client_base + sdk::off().local_player_pawn, pawn ) &&
                 pawn > 0x10000 && pawn < 0x7FFFFFFFFFFFULL ) {
                int health = 0;
                if ( read_remote( process, pawn + sdk::nv().m_iHealth, health ) )
                    live = health > 0 && health <= 100;
            }
            CloseHandle( process );
            return live;
        }

        void run() {
            // 1. cs2.exe has to exist.
            const auto process_deadline = clock_type::now( ) + std::chrono::seconds( 180 );
            DWORD pid = 0;
            while ( clock_type::now( ) < process_deadline ) {
                pid = find_process( L"cs2.exe" );
                if ( pid )
                    break;
                publish( stage::wait_process, L"Warte auf cs2.exe - Spiel starten." );
                std::this_thread::sleep_for( std::chrono::milliseconds( 400 ) );
            }
            if ( !pid ) {
                fail( L"cs2.exe nicht gefunden. Spiel starten und erneut klicken." );
                g_running = false;
                return;
            }
            {
                std::lock_guard<std::mutex> lock( g_mutex );
                g_status.pid = pid;
            }

            // 2. client.dll has to be mapped, otherwise every read lands nowhere.
            const auto module_deadline = clock_type::now( ) + std::chrono::seconds( 150 );
            std::uintptr_t client_base = 0;
            while ( clock_type::now( ) < module_deadline ) {
                client_base = module_base( pid, L"client.dll" );
                if ( client_base )
                    break;
                publish( stage::wait_modules, L"cs2.exe laeuft - warte auf client.dll ..." );
                std::this_thread::sleep_for( std::chrono::milliseconds( 350 ) );
            }
            if ( !client_base ) {
                fail( L"client.dll nicht gefunden. Spiel komplett laden lassen." );
                g_running = false;
                return;
            }
            {
                std::lock_guard<std::mutex> lock( g_mutex );
                g_status.client_loaded = true;
            }

            // 3. Optional payload.
            const std::wstring payload = find_payload( );
            if ( !payload.empty() ) {
                publish( stage::payload, L"Payload wird geladen ..." );
                const bool injected = inject_dll( pid, payload );
                std::lock_guard<std::mutex> lock( g_mutex );
                g_status.payload_present = true;
                g_status.payload_injected = injected;
            }

            // 4. Match probe. Not a gate: injecting in the menu is normal, the
            //    flag only tells the user whether the ESP has targets right now.
            bool in_match = false;
            const auto match_deadline = clock_type::now( ) + std::chrono::seconds( 25 );
            while ( clock_type::now( ) < match_deadline ) {
                in_match = match_running( pid, client_base );
                if ( in_match )
                    break;
                publish( stage::wait_match, L"client.dll bereit - warte auf Match ..." );
                std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
            }
            {
                std::lock_guard<std::mutex> lock( g_mutex );
                g_status.in_match = in_match;
                g_status.now = stage::ready;
                g_status.line = in_match
                    ? L"Verbunden - Match laeuft. try wird gestartet."
                    : L"Verbunden - Menue. ESP startet mit dem Match.";
            }
            g_running = false;
        }
    }

    status snapshot() {
        std::lock_guard<std::mutex> lock( g_mutex );
        return g_status;
    }

    bool running() {
        return g_running.load();
    }

    bool finished() {
        std::lock_guard<std::mutex> lock( g_mutex );
        return g_status.now == stage::ready || g_status.now == stage::failed;
    }

    bool ok() {
        std::lock_guard<std::mutex> lock( g_mutex );
        return g_status.now == stage::ready;
    }

    void start_async() {
        if ( g_running.exchange( true ) )
            return;
        if ( g_worker.joinable() )
            g_worker.join( );

        {
            std::lock_guard<std::mutex> lock( g_mutex );
            g_status = status{};
            g_status.now = stage::wait_process;
            g_status.line = L"Suche cs2.exe ...";
        }
        g_worker = std::thread( run );
    }

    void reset() {
        if ( g_running.load( ) )
            return;
        if ( g_worker.joinable() )
            g_worker.join( );
        std::lock_guard<std::mutex> lock( g_mutex );
        g_status = status{};
    }

} // namespace injector

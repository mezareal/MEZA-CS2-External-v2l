#include "memory_manager.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace {
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
}

namespace forceinline {
	memory_manager::memory_manager( std::string_view process ) {
		if ( process.empty( ) )
			throw std::invalid_argument( "memory_manager::memory_manager: process argument is empty" );

		attach_to_process( process );
	}

	memory_manager::memory_manager( DWORD process_id ) {
        attach_to_pid( process_id );
    }

    memory_manager::~memory_manager( ) {
		if ( m_proc_handle )
			CloseHandle( m_proc_handle );
	}

	MODULEENTRY32 memory_manager::get_module_entry( std::string_view module ) {
		MODULEENTRY32 me{};
		me.dwSize = sizeof me;

		//Create snapshot that includes 32 and 64 bit modules
		HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_proc_id );

		//If snapshot failed, fallback to EnumProcessModulesEx
		if ( snapshot == INVALID_HANDLE_VALUE ) {
			HMODULE hMods[ 1024 ];
			DWORD cbNeeded = 0;
			if ( EnumProcessModulesEx( m_proc_handle, hMods, sizeof( hMods ), &cbNeeded, LIST_MODULES_ALL ) ) {
				for ( unsigned int i = 0; i < ( cbNeeded / sizeof( HMODULE ) ); i++ ) {
					char szModName[ MAX_PATH ]{};
					if ( GetModuleBaseNameA( m_proc_handle, hMods[ i ], szModName, sizeof( szModName ) ) ) {
						if ( _stricmp( std::string( module ).c_str( ), szModName ) == 0 ) {
							MODULEINFO mi{};
							GetModuleInformation( m_proc_handle, hMods[ i ], &mi, sizeof( mi ) );
							me.modBaseAddr = static_cast< BYTE* >( mi.lpBaseOfDll );
							me.modBaseSize = mi.SizeOfImage;
							strncpy_s( me.szModule, szModName, sizeof( me.szModule ) );
							return me;
						}
					}
				}
			}
			throw std::exception( "memory_manager::get_module_entry: failed to find module" );
		}

		//Iterate through the processes modules
		bool found = false;
		if ( Module32First( snapshot, &me ) ) {
			do {	
				//We found our module, stop iterating
				if ( _stricmp( std::string( module ).c_str( ), me.szModule ) == 0 ) {
					found = true;
					break;
				}
			} while ( Module32Next( snapshot, &me ) );
		}

		//Close our snapshot handle and return the MODULEENTRY32 struct
		CloseHandle( snapshot );
		if ( !found )
			throw std::invalid_argument( "memory_manager::get_module_entry: module not found" );

		return me;
	}

	std::uintptr_t memory_manager::get_module_base( std::string_view module ) {
		return std::uintptr_t( get_module_entry( module ).modBaseAddr ); //Convenience
	}

	std::uintptr_t memory_manager::try_get_module_base( std::string_view module ) noexcept {
		if ( !m_proc_handle ) return 0;
		MODULEENTRY32 me{};
		me.dwSize = sizeof me;
		HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_proc_id );
		if ( snapshot != INVALID_HANDLE_VALUE ) {
			if ( Module32First( snapshot, &me ) ) {
				do {
					if ( _stricmp( std::string( module ).c_str( ), me.szModule ) == 0 ) {
						CloseHandle( snapshot );
						return reinterpret_cast<std::uintptr_t>( me.modBaseAddr );
					}
				} while ( Module32Next( snapshot, &me ) );
			}
			CloseHandle( snapshot );
		}
		HMODULE hMods[ 1024 ];
		DWORD cbNeeded = 0;
		if ( EnumProcessModulesEx( m_proc_handle, hMods, sizeof( hMods ), &cbNeeded, LIST_MODULES_ALL ) ) {
			for ( unsigned int i = 0; i < ( cbNeeded / sizeof( HMODULE ) ); i++ ) {
				char szModName[ MAX_PATH ]{};
				if ( GetModuleBaseNameA( m_proc_handle, hMods[ i ], szModName, sizeof( szModName ) ) ) {
					if ( _stricmp( std::string( module ).c_str( ), szModName ) == 0 ) {
						MODULEINFO mi{};
						GetModuleInformation( m_proc_handle, hMods[ i ], &mi, sizeof( mi ) );
						return reinterpret_cast<std::uintptr_t>( mi.lpBaseOfDll );
					}
				}
			}
		}
		return 0;
	}

	//Wrapper for find_pattern below
	std::uintptr_t memory_manager::find_pattern( std::string_view module, std::string_view pattern ) {
		//Get the target modules info
		auto module_entry = get_module_entry( module );

		//Get module begin and size
		std::size_t module_size = std::size_t( module_entry.modBaseSize );
		std::uintptr_t module_begin = std::uintptr_t( module_entry.modBaseAddr );
		
		//Scan for our pattern
		return find_pattern( module_begin, module_size, pattern );
	}

	/*
		Find pattern in given module
		Example pattern: "E8 ?? ?? ?? ?? CC"
		Pattern can NOT be without spaces. Wildcars can be single or double question marks, but they have to be seperated with spaces. 
	*/
	std::uintptr_t memory_manager::find_pattern( std::uintptr_t module_begin, std::size_t module_size, std::string_view pattern ) {
		if ( !module_begin || module_size == 0 || pattern.empty() )
			return 0;

		//Allocate sizeof module bytes
		std::vector< std::uint8_t > module_bytes( module_size );

		std::size_t page_size = 4096;
		std::size_t num_pages = module_size / page_size;
		std::size_t page_remainder = module_size % page_size;

		//Function to read a page. The helper is deliberately read-only; changing
		//protection in a foreign process is unnecessary for a signature scan.
		std::uintptr_t total_bytes_read = 0x0;
		auto read_page = [ & ]( std::uintptr_t start, std::size_t size ) -> bool {
			//Read a whole page (or the remainder of it)
			SIZE_T bytes_read;
			if ( !ReadProcessMemory( m_proc_handle, reinterpret_cast< LPCVOID >( start ), module_bytes.data( ) + total_bytes_read, size, &bytes_read ) )
				return 0x0;

			//Add the read bytes
			total_bytes_read += bytes_read;

			//Did we read as much as we requested to?
			return bytes_read == size;
		};

		//Read each page seperately
		for ( std::size_t i = 0; i < num_pages; i++ ) {
			//Return 0 if we failed to read
			if ( !read_page( module_begin + i * page_size, page_size ) )
				return 0x0;
		}

		//Read remainder of page. The old implementation decremented num_pages
		//and reread the last full page, which corrupted scans for most modules.
		if ( page_remainder > 0 && !read_page( module_begin + num_pages * page_size, page_remainder ) )
			return 0x0;

		//We didn't read the whole module
		if ( total_bytes_read != module_size )
			return 0x0;

		auto get_byte_vector_and_mask = [ ]( const std::string& pattern, std::vector< std::uint8_t >& byte_vec, std::string& mask ) {
			for ( std::size_t i = 0; i < pattern.length( ); i++ ) {
				//Skip spaces
				if ( pattern[ i ] == ' ' )
					continue;

				//Add wildcard
				if ( pattern[ i ] == '?' ) {
					while ( i + 1 < pattern.length( ) && pattern[ i + 1 ] == '?' )
						i++;

					mask.append( "?" );
					byte_vec.push_back( 0x0 );
					continue;
				}

				//Add converted byte. Ignore malformed tokens instead of throwing
				//halfway through a scan.
				if ( i + 1 >= pattern.length( ) || !std::isxdigit( static_cast< unsigned char >( pattern[ i ] ) ) || !std::isxdigit( static_cast< unsigned char >( pattern[ i + 1 ] ) ) )
					return;

				byte_vec.push_back( static_cast< std::uint8_t >( std::stoul( pattern.substr( i, 2 ), nullptr, 16 ) ) );
				i++;
				mask.append( "x" );
			}
		};
		
		//Generate pattern bytes and mask from signature
		std::string pattern_mask;
		std::vector< std::uint8_t > pattern_bytes;
		get_byte_vector_and_mask( std::string( pattern ), pattern_bytes, pattern_mask );
		if ( pattern_bytes.empty( ) || pattern_bytes.size( ) != pattern_mask.size( ) || pattern_bytes.size( ) > module_size )
			return 0;

		//Scan for our pattern
		std::uintptr_t pattern_offset = 0x0;
		for ( std::size_t i = 0; i + pattern_bytes.size( ) <= module_size; i++ ) {
			//Found a matching byte
			if ( module_bytes[ i ] == pattern_bytes[ 0 ] ) {
				//Save offset
				pattern_offset = i;

				//Check the pattern further
				for ( std::size_t j = 1; j < pattern_bytes.size( ); j++ ) {
					auto pattern_byte = pattern_bytes[ j ];
					auto mask_byte = pattern_mask[ j ];

					//Is current mask byte a wildcard?
					if ( mask_byte != '?' ) {
						//Bytes don't match and pattern at current position, start over
						if ( pattern_byte != module_bytes[ i + j ] )
							break;
					}

					//Pattern matches, return offset
					if ( j + 1 == pattern_bytes.size( ) )
						return module_begin + pattern_offset;
				}

				//Didn't find pattern, reset our pattern offset
				pattern_offset = 0;
			}
		}

		//Return 0 as we didn't find anything
		return 0x0;
	}

	bool memory_manager::is_attached( ) {
		// A non-null handle is not enough: the game may have exited since it was opened.
        if (!m_proc_handle) return false;
        DWORD code = 0;
        return GetExitCodeProcess(m_proc_handle, &code) && code == STILL_ACTIVE;
	}

	std::uintptr_t memory_manager::operator[ ]( std::string_view module ) {
		return get_module_base( module ); //Convenience
	}

    void memory_manager::attach_to_pid( DWORD process_id ) {
        if (!process_id)
            throw std::invalid_argument("CS2 process ID is zero. Game window no longer has a process.");

        // Ensure SeDebugPrivilege is active so DACL check allows opening target process
        enable_debug_privilege();

        HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                    FALSE, process_id);
        DWORD error = handle ? ERROR_SUCCESS : GetLastError();
        if (!handle) {
            handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                                 FALSE, process_id);
            if (!handle) error = GetLastError();
        }
        if (!handle) {
            handle = OpenProcess(PROCESS_VM_READ, FALSE, process_id);
            if (!handle) error = GetLastError();
        }
        if (!handle) {
            std::ostringstream out;
            out << "Unable to open CS2 process (PID " << process_id << ", Win32 " << error << "). ";
            if (error == ERROR_ACCESS_DENIED) {
                if (!is_process_elevated()) {
                    out << "Zugriff verweigert: CS2 laeuft mit Administratorrechten. Bitte starten Sie TRAY ebenfalls als Administrator.";
                } else {
                    out << "Access denied; verify your permissions and game security policies.";
                }
            } else if (error == ERROR_INVALID_PARAMETER) {
                out << "Process exited before try could connect.";
            } else {
                out << "Please ensure the game is still running.";
            }
            throw std::runtime_error(out.str());
        }
        DWORD exit_code = 0;
        if (!GetExitCodeProcess(handle, &exit_code) || exit_code != STILL_ACTIVE) {
            CloseHandle(handle);
            throw std::runtime_error("CS2 exited while try was connecting. Start or restart the game and retry.");
        }
        if (m_proc_handle) CloseHandle(m_proc_handle);
        m_proc_handle = handle;
        m_proc_id = process_id;
    }

	void memory_manager::attach_to_process( std::string_view process ) {
		//Create snapshot
		HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, NULL );

		if ( snapshot == INVALID_HANDLE_VALUE )
			throw std::exception( "memory_manager::attach_to_process: failed to create snapshot" );

		//Create PROCESSENTRY structure
		PROCESSENTRY32 pe{};
		pe.dwSize = sizeof pe;

		//Enumerate running processes, including the first snapshot entry.
		if ( Process32First( snapshot, &pe ) ) do {
			//Have we found our target process?
			if ( _stricmp( std::string( process ).c_str( ), pe.szExeFile ) == 0 ) {
				//The ESP only needs read access. Keeping the handle read-only avoids
				//accidental writes to the game process.
                const DWORD pid = pe.th32ProcessID;
                CloseHandle(snapshot);
                attach_to_pid(pid);
                return;
			}
		} while ( Process32Next( snapshot, &pe ) );

		//Close snapshot handle
		CloseHandle( snapshot );
		
        throw std::runtime_error("CS2 process not found in process list. Start CS2 and retry.");
	}
}

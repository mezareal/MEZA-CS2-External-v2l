#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <stdexcept>

#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>

namespace forceinline {
	class memory_manager {
	public:
		memory_manager( ) { }
		memory_manager( std::string_view process );
		explicit memory_manager( DWORD process_id );
		~memory_manager( );

		memory_manager( memory_manager&& other ) noexcept
			: m_proc_handle( other.m_proc_handle ), m_proc_id( other.m_proc_id ) {
			other.m_proc_handle = nullptr;
			other.m_proc_id = 0;
		}

		memory_manager& operator=( memory_manager&& other ) noexcept {
			if ( this != &other ) {
				if ( m_proc_handle )
					CloseHandle( m_proc_handle );
				m_proc_handle = other.m_proc_handle;
				m_proc_id = other.m_proc_id;
				other.m_proc_handle = nullptr;
				other.m_proc_id = 0;
			}
			return *this;
		}

		memory_manager( const memory_manager& ) = delete;
		memory_manager& operator=( const memory_manager& ) = delete;

		MODULEENTRY32 get_module_entry( std::string_view module );
		std::uintptr_t get_module_base( std::string_view module );
		std::uintptr_t try_get_module_base( std::string_view module ) noexcept;

		std::uintptr_t find_pattern( std::string_view module, std::string_view pattern );
		std::uintptr_t find_pattern( std::uintptr_t module_begin, std::size_t module_size, std::string_view pattern );

		bool is_attached( );
		DWORD process_id() const noexcept { return m_proc_id; }

		template < typename T >
		T read( std::uintptr_t address ) {
			T temp_val{};
			ReadProcessMemory( m_proc_handle, reinterpret_cast< LPCVOID >( address ), &temp_val, sizeof( T ), nullptr );

			return temp_val;
		}

		/*
			Reads custom length.
			Example: you want to read a float[ 64 ].
			you'd use this as follows:
			read_ex< float >( float_arr_ptr, 0xDEADBEEF, 64 )
		*/
		template < typename T >
		void read_ex( T* out_object_ptr, std::uintptr_t address, std::size_t object_count ) {
			if ( !out_object_ptr || object_count == 0 )
				return;

			ReadProcessMemory( m_proc_handle, reinterpret_cast< LPCVOID >( address ), out_object_ptr, sizeof( T ) * object_count, nullptr );
		}

		template < typename T >
		bool write( std::uintptr_t address, T value ) {
			SIZE_T bytes_written = 0;
			WriteProcessMemory( m_proc_handle, reinterpret_cast< LPVOID >( address ), &value, sizeof( T ), &bytes_written );

			return bytes_written == sizeof( T );
		}

		//See read_ex
		template < typename T >
		bool write_ex( T* object_ptr, std::uintptr_t address, std::size_t object_count ) {
			SIZE_T bytes_written = 0;
			WriteProcessMemory( m_proc_handle, reinterpret_cast< LPVOID >( address ), object_ptr, sizeof( T ) * object_count, &bytes_written );

			return bytes_written == sizeof( T ) * object_count;
		}

		std::uintptr_t operator[ ]( std::string_view module );

	private:
		void attach_to_process( std::string_view process );
		void attach_to_pid( DWORD process_id );

		HANDLE m_proc_handle = nullptr;
		DWORD m_proc_id = 0;
	};
}

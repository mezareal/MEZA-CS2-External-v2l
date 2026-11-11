cmake_minimum_required(VERSION 3.24)
if(NOT WIN32)
    message(FATAL_ERROR "MEZA Open Source requires Windows and Visual Studio C++ tools.")
endif()
project(MEZA_OpenSource LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
set(D3DX_ROOT "${CMAKE_SOURCE_DIR}/packages/Microsoft.DXSDK.D3DX.9.29.952.8/build/native")
set(D3DX_HEADERS "${D3DX_ROOT}/include/d3dx9.h")
set(D3DX_LIB "${D3DX_ROOT}/release/lib/x64/d3dx9.lib")
set(D3DX_DLL_DIR "${D3DX_ROOT}/release/bin/x64")

foreach(REQUIRED_FILE "${D3DX_HEADERS}" "${D3DX_LIB}" "${D3DX_DLL_DIR}/D3DX9_43.dll")
    if(NOT EXISTS "${REQUIRED_FILE}")
        message(FATAL_ERROR "DirectX dependency missing: ${REQUIRED_FILE}")
    endif()
endforeach()

add_executable(MEZA_OpenSource WIN32
    main.cpp resources.rc
    injector/injector.cpp
    cs2_sdk/cs2_sdk.cpp
    dx_overlay/dx_overlay.cpp
    dx_renderer/dx_renderer.cpp
    esp/esp.cpp
    memory_manager/memory_manager.cpp
)
target_include_directories(MEZA_OpenSource PRIVATE "${CMAKE_SOURCE_DIR}" "${D3DX_ROOT}/include")
target_link_libraries(MEZA_OpenSource PRIVATE
    d3d9 "${D3DX_LIB}" dwmapi user32 gdi32 gdiplus msimg32 psapi Shlwapi
    winhttp crypt32 bcrypt advapi32
)

if(MSVC)
    target_compile_options(MEZA_OpenSource PRIVATE /W4 /EHsc /O2 /Oi /Gy /GS /guard:cf)
    target_link_options(MEZA_OpenSource PRIVATE /OPT:REF /OPT:ICF /GUARD:CF /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
else()
    target_compile_options(MEZA_OpenSource PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()

add_custom_command(TARGET MEZA_OpenSource POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${D3DX_DLL_DIR}/D3DX9_43.dll" "$<TARGET_FILE_DIR:MEZA_OpenSource>/D3DX9_43.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${D3DX_DLL_DIR}/D3DCompiler_43.dll" "$<TARGET_FILE_DIR:MEZA_OpenSource>/D3DCompiler_43.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/config.example.ini" "$<TARGET_FILE_DIR:MEZA_OpenSource>/config.ini"
    VERBATIM
)

#pragma once

// Public-source authentication boundary.
//
// The private KeyAuth implementation and every application identifier are
// deliberately absent from this repository. Integrate an authentication
// provider owned by your project before enabling a distributable build.
#include <Windows.h>
#include <string>

namespace meza_public_auth {

inline std::string get_user_name_utf8() {
    return "Open-source build";
}

inline std::string get_subscription_utf8() {
    return "Not configured";
}

inline void open_support() {
    MessageBoxW(nullptr,
        L"This public source package has no bundled support endpoint.",
        L"MEZA Open Source", MB_OK | MB_ICONINFORMATION);
}

inline bool authorize(HINSTANCE) {
    MessageBoxW(nullptr,
        L"Authentication is intentionally not included in the public source package.\n\n"
        L"Connect an authentication provider you own before distributing a build.",
        L"MEZA Open Source", MB_OK | MB_ICONINFORMATION);
    return false;
}

} // namespace meza_public_auth

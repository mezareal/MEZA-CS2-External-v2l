# MEZA Open Source

This folder is a GitHub-ready source package for the MEZA native Windows UI,
rendering, configuration, and build setup.

## What is included

- C++ source code, Visual Studio solution, CMake project, UI assets, fonts, and
  required build package references.
- `config.example.ini` as a safe configuration template.

## What is intentionally excluded

- KeyAuth code, application identifiers, owner IDs, license keys, saved
  accounts, runtime configuration, release binaries, diagnostics, logs, and
  archived builds.

The `open_auth` adapter is a compile-safe boundary. It does not authenticate a
user and it does not bypass authentication. Replace it only with an
authentication provider that you own and are authorized to use.

## Build

1. Install Visual Studio with **Desktop development with C++** and the Windows SDK.
2. Open `TRAY.sln` and select `Release | x64`.
3. Build the solution. The output remains in this folder's `dist` directory.

Do not commit `config.ini`, generated binaries, logs, or user data. The included
`.gitignore` already excludes them.

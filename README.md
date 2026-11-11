<p align="center">
  <img src="assets/github-banner.png" alt="MEZA" width="960" />
</p>

<p align="center">
  Native Windows UI, rendering, configuration, and build-source package.
  <br />
  <a href="https://discord.gg/meza"><strong>Join the MEZA Discord →</strong></a>
</p>

## Overview

MEZA Open Source contains the native C++ user-interface, rendering, menu
configuration, assets, fonts, and Visual Studio/CMake build setup used by the
project. It is prepared for developers who want to inspect, extend, or learn
from the Windows UI and rendering architecture.

<p align="center">
  <img src="assets/github-showcase.png" alt="MEZA interface showcase" width="900" />
</p>

## Included

- Native DirectX 9 UI and rendering source
- C++ project sources, assets, fonts, and resource definitions
- `meza.sln` for Visual Studio and `CMakeLists.txt` for CMake
- A safe `config.example.ini` template
- A custom MEZA Windows application icon

## Intentionally excluded

This public package does **not** contain private authentication code,
application identifiers, owner IDs, license keys, saved accounts, local
configuration, release binaries, diagnostic logs, or archived builds.

`open_auth/open_auth_gate.h` is an explicit public boundary. It does not
authenticate users and does not bypass authentication. Connect only an
authentication provider you own and are authorized to use before distributing a
build.

## Build

1. Install Visual Studio with **Desktop development with C++** and the Windows SDK.
2. Open `meza.sln`.
3. Choose `Release | x64`.
4. Select **Build Solution**.

After a successful build, Visual Studio creates:

```text
dist\x64\Release\MEZA.exe
```

The repository intentionally ignores generated binaries. A fresh GitHub clone
must be built locally before it can be run.

## Community

Questions, previews, and community discussion: **[discord.gg/meza](https://discord.gg/meza)**

## Repository hygiene

Do not commit `config.ini`, generated binaries, logs, dumps, or user data. The
included `.gitignore` is configured to keep those files out of Git.

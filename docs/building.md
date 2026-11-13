# Building OpenD2K

OpenD2K is one CMake project (C++20, SDL3) for macOS, Windows, Linux and iPadOS. The game logic
(`src/game`, `src/formats`) has no platform code at all; everything platform-specific goes through SDL3.
**No original game data is part of the build or of any package** — players point the game at their own copy
(the Gruntmods Edition installer `.exe` works directly, on every platform).

| Platform | Compiler | SDL3 | 3D mode (Graphics: 3D) | Status |
|---|---|---|---|---|
| macOS 11+ (arm64; x86_64 untested) | Apple clang (Xcode 15+) | Homebrew `sdl3`, or downloaded | Metal (MSL) | daily driver |
| iPadOS 17+ | Xcode | built from source in the Xcode project | Metal (MSL) | see `docs/ios.md` |
| Windows 10 1903+ / 11 (x64) | MSVC 2022 (`cl` or `clang-cl`) | downloaded + built (FetchContent) | Direct3D 12 (DXIL, needs `dxc`) or Vulkan (SPIR-V) | builds in CI; tested 2026-11-09 on an RTX 3080 laptop (Win 11): all tests + `--simtest` pass, 2D and 3D (D3D12 and Vulkan) render |
| Linux x64 (Ubuntu 24.04+, any distro with SDL3's deps) | GCC 13+ / clang 17+ | downloaded + built, or the distro's SDL3 >= 3.4 | Vulkan (SPIR-V) | builds in CI; untested on hardware |

Requirements everywhere: CMake >= 3.24, Ninja (recommended) or Visual Studio / Xcode generators, git.
SDL3 is pinned to **3.4.20** (the Homebrew version; `cmake/FetchSDL.cmake`).

## macOS

```sh
brew install cmake ninja sdl3
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
open "build/Dune 2000.app"        # or ./build/d2k (terminal, logs to stdout)
```
Without Homebrew's SDL3 (or with `-DD2K_FETCH_SDL=ON`) the pinned release is downloaded and built; the app
bundle then carries that `libSDL3.0.dylib` instead. For an older-macOS build pass
`-DD2K_FETCH_SDL=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0` (Homebrew's SDL is built for the running macOS only).
iPad: `docs/ios.md`.

## Windows (MSVC)

### One-time setup
1. **Visual Studio 2022** (Community is fine) or the *Build Tools for Visual Studio 2022*, workload
   **"Desktop development with C++"**. Keep the default components: MSVC v143, **Windows 11 SDK** (it contains
   `dxc.exe`, used for the Direct3D 12 shaders), *C++ CMake tools for Windows* (CMake + Ninja).
2. **Git for Windows** (https://git-scm.com).
3. Your GPU driver (for the RTX 3080: a current NVIDIA driver; it provides both D3D12 and Vulkan).

### Build
Open **"x64 Native Tools Command Prompt for VS 2022"** (Start menu), then:
```bat
git clone <repo url> opend2k
cd opend2k
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\OpenD2K.exe
```
- The first configure downloads SDL3 (~10 MB) and builds it with the game (a few minutes).
- The configure log says `3D shaders: DXIL via ...\dxc.exe` when the Windows SDK's dxc was found. Without it
  you get `dxc not found - no DXIL`, and the 3D mode runs on Vulkan instead (fine on any recent NVIDIA/AMD/Intel
  driver). `-DD2K_DXIL=ON` makes a missing dxc a configure error; `-DD2K_DXC=C:\path\dxc.exe` picks one.
- Visual Studio generator instead of Ninja (opens in the IDE as `build\d2k.sln`):
  ```bat
  cmake -S . -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release
  build\Release\OpenD2K.exe
  ```
- **Without the Visual Studio Installer** (it can fail silently with exit code 1, or C: has no room): a
  portable MSVC + Windows SDK from Microsoft's own packages, e.g. mmozeiko's `portable-msvc.py --vs 2022
  --accept-license` (unpacks into `msvc\`, including the SDK's `dxc.exe`), CMake + Ninja from pip
  (`pip install cmake ninja`), then `call msvc\setup_x64.bat` in a `cmd` before configuring. The game builds
  and passes all tests this way (MSVC 14.44, SDK 10.0.26100); pass the SDK's dxc with
  `-DD2K_DXC="...\msvc\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe"` since it is not on PATH.
- clang-cl works too: add `-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl` (Ninja) or `-T ClangCL` (VS).
- Optional: `-DD2K_FETCH_ZLIB=ON` builds zlib for *deflate*-compressed NSIS installers. The Gruntmods
  installers are LZMA and work without it.

### What gets built
- `build\OpenD2K.exe` — the game, GUI subsystem (no console window). This is the one to start.
- `build\d2k.exe` — the same program as a console app: logs in the terminal, and the command-line modes
  (`--simtest`, `--screenshot`, `--menu-screenshot`, `--map A1V1.MAP`, ...; docs/STATUS.md "Run").
- `build\SDL3.dll` — copied next to both after each build.
- `build\game_test.exe`, `build\formats_test.exe` — the tests (`ctest --test-dir build`).
Both executables embed `cmake/windows/opend2k.manifest`: the **UTF-8 code page** (all paths are UTF-8 inside
the game, so folders with Cyrillic or accented names work) and long-path awareness.

### First start: game data
The data folder page appears when no game is found. Either
- **Choose installer (.exe)…** and pick `Dune 2000 Gruntmods Edition 1.6.7.exe` — the game unpacks it itself
  (no installation, nothing runs) into `%APPDATA%\d2k-mac\Dune 2000\GameData\`; or
- **Choose folder…** and pick an installed `Dune 2000` folder (the one with `data\` inside).

The game also finds an installation by itself next to `OpenD2K.exe` (copy the build into the game folder, or
the game folder next to it), in `%ProgramFiles(x86)%\Dune 2000`, `%ProgramFiles%\Dune 2000`,
`C:\Games\Dune 2000`, `C:\Westwood\Dune2000` and `C:\Dune 2000`. From a console: `d2k.exe --data "D:\Games\Dune 2000\data"`.
Cutscenes: the Gruntmods cutscene installers are chosen the same way (**Choose installer**).

Settings, saves, unpacked data and movies live in `%APPDATA%\d2k-mac\Dune 2000\` (`SDL_GetPrefPath`).

### 3D mode on Windows
Graphics → 3D (or `--graphics 3d`). The renderer log line says which GPU backend runs:
`Renderer: SDL GPU renderer on direct3d12` (DXIL shaders) or `... on vulkan` (SPIR-V).
To compare or work around a driver problem:
```bat
set SDL_GPU_DRIVER=vulkan
build\d2k.exe --graphics 3d
set SDL_GPU_DRIVER=direct3d12
build\d2k.exe --graphics 3d
```
If neither works, the game logs `3D is not available here` and shows the Enhanced battlefield; `D2K_RENDERER=sdl`
forces SDL's classic renderer (Original / Enhanced only).

## Linux

```sh
# Ubuntu / Debian: compiler, Ninja, zlib, and SDL3's build dependencies (X11, Wayland, audio)
sudo apt install build-essential cmake ninja-build pkg-config git zlib1g-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev \
  libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev libwayland-dev libdecor-0-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
./build/d2k
```
Distributions with an SDL3 >= 3.4 package (`libsdl3-dev`, `sdl3`) use it automatically. Fedora: the same
packages with `-devel` names (SDL's `docs/README-linux.md` lists them). The file dialogs (folder / installer
picker) use the XDG desktop portal or `zenity`; without either, pass `--data DIR` or `--install-from FILE.exe`.
Data is also found under `~/Games/Dune 2000` and in the Wine prefix (`$WINEPREFIX` or `~/.wine`,
`drive_c/Program Files (x86)/Dune 2000`, `drive_c/Games/Dune 2000`). Settings: `~/.local/share/d2k-mac/Dune 2000/`.
The in-game art pack download (Options → Art pack) loads `libcurl.so.4` at run time (`libcurl4`, present on most
desktops; no build dependency); without it the page shows the manual download link.

## Build options

| Option | Default | |
|---|---|---|
| `D2K_FETCH_SDL` | `OFF` | Always download/build SDL3 3.4.20 (otherwise only when `find_package(SDL3 3.4)` fails). `-DFETCHCONTENT_SOURCE_DIR_SDL3=<dir>` uses a local source tree. |
| `D2K_FETCH_ZLIB` | `OFF` | Build zlib 1.3.1 when none is installed (deflate NSIS installers only). |
| `D2K_DXIL` | `AUTO` | DXIL shaders via dxc: `AUTO` = on Windows when dxc is found, `ON` = required, `OFF`. |
| `D2K_DXC` | (search) | Path of `dxc` (else PATH, the Windows SDK, `%VULKAN_SDK%`). |
| `D2K_TEST_DATA_DIR` | `game/Dune 2000/data` | Original data for `formats_test` / `game_test`; the data tests are skipped when it is missing (CI). |
| `D2K_APP_BUNDLE` | `ON` on macOS | Build `Dune 2000.app`. |
| `D2K_APP_NAME` | `Dune 2000` | App name: the macOS bundle (`<name>.app`) and the window title. Release packages use `OpenD2K`. |
| `D2K_MACOS_BUNDLE_ID` | `com.d2kmac.dune2000` | macOS bundle identifier (release packages: `com.opend2k.app`). |

Warnings are never errors (`cmake/CompilerOptions.cmake`).

## The 3D shaders

`src/render3d/shaders.hlsl` (HLSL) is the source for Vulkan and Direct3D 12; Metal uses the hand-written MSL in
`shaders_msl.hpp` / `models_msl.hpp`. Change both together (docs/3d-mode.md "Shaders").
- SPIR-V is compiled offline and **checked in** (`src/render3d/shaders_spirv.inc`), so no build needs a shader
  compiler: after editing `shaders.hlsl` run `tools/compile_shaders.sh` (needs `glslangValidator`; macOS
  `brew install glslang spirv-tools`, Ubuntu `glslang-tools spirv-tools`, or the Vulkan SDK). CI runs
  `tools/compile_shaders.sh --check`.
- DXIL is compiled at build time on Windows (`cmake/CompileShadersDXIL.cmake` → `cmake/EmbedDXIL.cmake`,
  shader model 6.0, `<build>/generated/shaders_dxil.inc`).
- At run time `render3d/shader_blobs.cpp` picks MSL, DXIL or SPIR-V from `SDL_GetGPUShaderFormats()`, and
  `render3d/gpu.cpp` only accepts a GPU device that takes one of the formats the build carries.
- Testing the SPIR-V on a Mac: `SDL_GPU_DRIVER=vulkan SDL_VULKAN_LIBRARY=/opt/homebrew/lib/libMoltenVK.dylib
  ./build/d2k --graphics 3d` (`brew install molten-vk`).

## Packaging

Release packages are made by `tools/release/package_macos.sh` (universal `OpenD2K.app`, macOS 11+, SDL3 built
from source and bundled), `tools/release/package_linux.sh` (Docker `ubuntu:24.04`, linux/amd64) and
`tools/release/package_windows.sh` (MinGW-w64 cross-build from macOS / Linux, DXIL via dxc in Docker or Vulkan
only); each writes a zip / tarball and its `.sha256` to `build-release/`. Details in the scripts' headers.

`cmake --install build --prefix <dir>` (Windows, Linux) produces a self-contained folder:
```
OpenD2K.exe  d2k.exe  SDL3.dll          (Linux: d2k  libSDL3.so.0, RPATH $ORIGIN)
assets/models/...                       our own 3D models (no game data); assets/terrain: CC0 rocks / textures
licenses/                               SDL3 (zlib), Play font (OFL), cgltf (MIT), LZMA SDK, THIRD-PARTY.md
README.md  CHANGELOG.md  LICENSE.txt
```
- **Windows**: zip that folder (CI artifact `OpenD2K-windows-x64`). MSVC builds link the C/C++ runtime
  **statically** by default (`CMAKE_MSVC_RUNTIME_LIBRARY` = `MultiThreaded`, `MultiThreadedDebug` for Debug,
  set in CMakeLists.txt for every target incl. the fetched SDL3), so `d2k.exe`, `OpenD2K.exe` and `SDL3.dll`
  import only Windows system DLLs and the zip needs **no VC++ redistributable** (checked with
  `dumpbin /dependents`; Vulkan's `vulkan-1.dll` comes with the GPU driver and is loaded at run time).
  `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL` keeps the DLL runtime (then ship `vc_redist` or the
  `msvcp140*` / `vcruntime140*` DLLs app-local). MinGW builds (tools/release/package_windows.sh) link their
  runtime statically too. An installer (Inno Setup / NSIS / WiX) and an `.ico` resource are release-time work.
- **Linux**: tarball (CI artifact). AppImage plan: `cmake --install build --prefix AppDir/usr/bin`, add a
  `.desktop` file + icon, run `linuxdeploy --appdir AppDir --output appimage` (it bundles `libSDL3.so.0`;
  SDL loads X11/Wayland/PulseAudio/PipeWire dynamically, so those stay on the host). Flatpak (with the
  freedesktop runtime) is the alternative for Flathub; the game data then has to come in through the file
  portal (the installer picker already does that).
- **macOS**: `Dune 2000.app` (ad-hoc signed; CI artifact zip). A public release needs a Developer ID signature +
  notarisation, and SDL3 built with `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0` (+ `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`
  for a universal app).
- Development builds still say "Dune 2000" (window title, `Dune 2000.app`); release builds pass `-DD2K_APP_NAME=OpenD2K`.

## Portability notes

- **Platform code**: only in `src/platform` and `src/render*`, through SDL3 (window, input, audio, file dialogs,
  `SDL_GetPrefPath`, `SDL_GetBasePath`). `#if`s: `_WIN32` in `formats/nsis.cpp` (64-bit file offsets,
  `_wfopen`) and `platform/saves.cpp` (`localtime_s`), `__APPLE__` in `platform/platform_info.*` (device model,
  touch platform), the install-folder candidates in `platform/data_dir.cpp`. HTTPS (the art pack download,
  `platform/http_client.hpp`) has one backend per platform: `http_client_apple.mm` (NSURLSession, ARC, macOS + iOS),
  `http_client_win.cpp` (WinHTTP), `http_client_curl.cpp` (libcurl via `dlopen`, Linux; also built on macOS and
  selected with `D2K_HTTP_BACKEND=curl` for testing). The notch / safe-area helpers
  (`platform/pointer.hpp`) are plain SDL and harmless elsewhere.
- **Paths**: UTF-8 `std::string` everywhere. Windows: the manifest's UTF-8 code page makes the char-based
  `std::filesystem::path` / `fstream` / `fopen` calls UTF-8 aware (Windows 10 1903+). Game files are looked
  up case-insensitively (`formats::findFileNoCase`) because the data mixes `BLOXBGBS`/`Bloxbgbs` — needed on
  Linux, where the file system is case-sensitive.
- **Line endings**: the data's text files are CRLF; the text parsers trim whitespace including `\r` (ini,
  mission .ini, UIL, settings, catalogs). Our own files are written in text mode (CRLF on Windows) and read back
  the same way. `.gitattributes` keeps the repository LF and scripts LF on Windows checkouts.
- **Endianness**: little-endian hosts only (all targets are). `Templates.bin` is loaded by `memcpy`
  (static_assert in `formats/templates.cpp`); every other format and the save games read/write bytes explicitly.
- **Integer sizes**: the simulation uses fixed-width types; Windows is LLP64 (`long` = 32 bit) and the code does
  not rely on `long`. Save games write `int` as 4 bytes and `size_t` as 8.
- **Packed structs**: `#pragma pack(push, 1)` (`formats/templates.hpp`), understood by MSVC, GCC and clang;
  `static_assert(sizeof...)` pins the layouts. The save game's `sizeof` checks run only on libc++ / 64-bit;
  its member-count checks on clang and GCC (not MSVC's cl).
- **Determinism**: no FMA contraction, no x87 (`d2k_deterministic_fp` in `cmake/CompilerOptions.cmake`: GCC/clang
  `-ffp-contract=off`, MSVC `/fp:precise`, clang-cl additionally `/clang:-ffp-contract=off`, 32-bit x86
  `-msse2 -mfpmath=sse`); never `-ffast-math`. The only libm transcendental in the simulation is the sin/cos table
  built at start-up; `game_test` checks its hash, so a C library that rounds differently fails the test on that
  platform. `sqrt` is exact (IEEE).
- **MinGW**: MSVC is the supported Windows compiler, but a MinGW-w64 (GCC 16) cross-build from macOS compiles and
  links everything (`tools/release/mingw-w64-x86_64.cmake`; the 0.0.1 Windows package is built this way); it
  embeds the manifest as a resource and links its runtimes statically (only `SDL3.dll` and system DLLs are needed).

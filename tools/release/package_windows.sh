#!/usr/bin/env bash
# Windows x64 release package, cross-compiled with MinGW-w64 (macOS: brew install mingw-w64; Linux:
# g++-mingw-w64-x86-64-posix). The supported Windows compiler is MSVC (docs/building.md); this script
# exists so a release can be made from the Mac. Alternative: run the CI workflow's Windows job by hand
# (MSVC + the Windows SDK's dxc), see docs/release.md.
#
#   tools/release/package_windows.sh [--dxil docker|native|off] [--dxc PATH] [--allow-stale-previews]
#
# Output: build-release/OpenD2K-<version>-windows-x64.zip (+ .sha256) containing
#   OpenD2K-<version>-windows-x64/{OpenD2K.exe, d2k.exe, SDL3.dll, assets/, licenses/, README.md, CHANGELOG.md, LICENSE.txt}
# Build dir: build-release/windows-x64.
#
# 3D shaders: SPIR-V (Vulkan) is always in the build. DXIL (Direct3D 12) needs dxc:
#   --dxil docker (default when Docker is there): Microsoft's Linux dxc release, downloaded once into
#                 build-release/tools/dxc, run in an ubuntu:24.04 linux/amd64 container (tools/release/dxc-docker.sh)
#   --dxil native --dxc PATH: a dxc on this machine (>= 1.8.2502 signs the DXIL itself)
#   --dxil off:   no DXIL; the 3D mode then runs on Vulkan only (any current NVIDIA / AMD / Intel driver)
# MinGW links libstdc++ / libgcc / winpthread statically (cmake/Desktop.cmake), so the zip needs no
# runtime DLLs besides SDL3.dll. (MSVC builds - CI, a Windows PC - link the static MSVC runtime the same way:
# CMAKE_MSVC_RUNTIME_LIBRARY in CMakeLists.txt; no VC++ redistributable needed, docs/building.md.) The tests are cross-compiled but cannot run here (no Windows / Wine).
set -euo pipefail
source "$(dirname "$0")/common.sh"

DXIL=auto
DXC=""
DXC_TAG="${DXC_TAG:-v1.8.2505.1}"
while [ $# -gt 0 ]; do
    case "$1" in
        --dxil) DXIL="$2"; shift 2 ;;
        --dxc) DXC="$2"; DXIL=native; shift 2 ;;
        --allow-stale-previews) ALLOW_STALE_PREVIEWS=1; shift ;;
        *) die "unknown option $1" ;;
    esac
done
command -v x86_64-w64-mingw32-g++ >/dev/null || command -v x86_64-w64-mingw32-g++-posix >/dev/null \
    || die "MinGW-w64 not found (brew install mingw-w64)"
check_terrain_assets
check_previews
if [ "$DXIL" = auto ]; then
    if command -v docker >/dev/null && docker info >/dev/null 2>&1; then DXIL=docker; else DXIL=off; fi
fi

BUILD="$OUT/windows-x64"
NAME="OpenD2K-$VERSION-windows-x64"
DXC_ARGS=(-DD2K_DXIL=OFF)
case "$DXIL" in
    docker)
        DXC_DIR="$OUT/tools/dxc-$DXC_TAG"
        if [ ! -x "$DXC_DIR/bin/dxc" ]; then
            log "Download dxc $DXC_TAG (Linux x86-64 release, github.com/microsoft/DirectXShaderCompiler)"
            url="$(curl -fsSL "https://api.github.com/repos/microsoft/DirectXShaderCompiler/releases/tags/$DXC_TAG" \
                | grep -o '"browser_download_url": *"[^"]*linux_dxc[^"]*x86_64\.tar\.gz"' | head -1 | sed 's/.*"\(http[^"]*\)"/\1/')"
            [ -n "$url" ] || die "no linux_dxc asset in release $DXC_TAG"
            mkdir -p "$DXC_DIR"
            curl -fsSL "$url" -o "$OUT/tools/dxc-$DXC_TAG.tar.gz"
            echo "dxc archive sha256 $(sha256 "$OUT/tools/dxc-$DXC_TAG.tar.gz") ($url)"
            tar -xzf "$OUT/tools/dxc-$DXC_TAG.tar.gz" -C "$DXC_DIR"
            rm -f "$OUT/tools/dxc-$DXC_TAG.tar.gz"
        fi
        export D2K_DXC_DIR="$(cd "$DXC_DIR" && pwd -P)"
        export D2K_DXC_MOUNTS="$(mkdir -p "$BUILD" && cd "$BUILD" && pwd -P)"
        DXC_ARGS=(-DD2K_DXIL=ON "-DD2K_DXC=$REPO/tools/release/dxc-docker.sh")
        ;;
    native) [ -x "$DXC" ] || die "--dxc PATH (an executable dxc)"; DXC_ARGS=(-DD2K_DXIL=ON "-DD2K_DXC=$DXC") ;;
    off) ;;
    *) die "--dxil docker|native|off" ;;
esac

log "Configure (MinGW-w64, DXIL: $DXIL) in $BUILD"
cmake -S "$REPO" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$REPO/tools/release/mingw-w64-x86_64.cmake" \
    -DD2K_FETCH_SDL=ON -DD2K_FETCH_ZLIB=ON -DD2K_APP_NAME=OpenD2K "${DXC_ARGS[@]}" >/dev/null
log "Build (-j $JOBS)"
cmake --build "$BUILD" -j "$JOBS"

if [ "$DXIL" != off ]; then
    log "DXIL blobs signed? (DXBC container digest must be non-zero)"
    for f in "$BUILD"/generated/*.dxil; do
        d="$(xxd -s 4 -l 16 -p "$f")"
        [ "$d" != 00000000000000000000000000000000 ] || die "$(basename "$f") is unsigned: D3D12 would reject it"
    done
    echo "ok: $(ls "$BUILD"/generated/*.dxil | wc -l | tr -d ' ') signed DXIL shaders"
fi

log "Install + check"
STAGE="$OUT/stage/$NAME"
rm -rf "$STAGE"
cmake --install "$BUILD" --prefix "$STAGE" >/dev/null
cp "$REPO/assets/terrain/LICENSES.md" "$STAGE/licenses/terrain-assets-CC0.md"
for exe in OpenD2K.exe d2k.exe SDL3.dll; do
    [ -f "$STAGE/$exe" ] || die "$exe missing"
    deps="$(x86_64-w64-mingw32-objdump -p "$STAGE/$exe" | sed -n 's/.*DLL Name: //p' | tr '\n' ' ')"
    echo "$exe imports: $deps"
    for d in $deps; do
        case "$(echo "$d" | tr 'A-Z' 'a-z')" in
            sdl3.dll|kernel32.dll|user32.dll|gdi32.dll|shell32.dll|ole32.dll|oleaut32.dll|advapi32.dll|imm32.dll|\
            version.dll|winmm.dll|setupapi.dll|cfgmgr32.dll|msvcrt.dll|ucrtbase.dll|api-ms-win-*|ws2_32.dll|\
            comdlg32.dll|shlwapi.dll|dwmapi.dll|uuid.dll) ;;
            *) die "$exe imports $d, which a stock Windows does not have" ;;
        esac
    done
done
x86_64-w64-mingw32-strip "$STAGE"/*.exe "$STAGE"/SDL3.dll 2>/dev/null || true
du -sh "$STAGE"

ZIP="$OUT/$NAME.zip"
rm -f "$ZIP"
(cd "$OUT/stage" && zip -qrX "$ZIP" "$NAME")
finish_artifact "$ZIP"

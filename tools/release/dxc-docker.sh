#!/usr/bin/env bash
# dxc (DirectX Shader Compiler) for hosts without a native one (macOS): runs Microsoft's Linux x86-64
# release of dxc in a throw-away ubuntu:24.04 linux/amd64 container. Passed to CMake as
# -DD2K_DXC=<this script> by tools/release/package_windows.sh; cmake/EmbedDXIL.cmake calls it with
# absolute paths, so the repository and the build dir are mounted at the same paths inside.
# dxc >= 1.8.2502 validates and signs the DXIL itself (no dxil.dll needed), which Direct3D 12 requires.
# Env: D2K_DXC_DIR = the unpacked dxc release (bin/dxc, lib/libdxcompiler.so), D2K_DXC_MOUNTS = extra
# colon-separated absolute paths to mount (the build dir).
set -euo pipefail
: "${D2K_DXC_DIR:?set D2K_DXC_DIR to the unpacked dxc release}"
REPO="$(cd "$(dirname "$0")/../.." && pwd -P)"
args=(-v "$D2K_DXC_DIR:/opt/dxc:ro" -v "$REPO:$REPO")
IFS=: read -r -a extra <<< "${D2K_DXC_MOUNTS:-}"
for m in "${extra[@]}"; do [ -n "$m" ] && args+=(-v "$m:$m"); done
exec docker run --rm --platform linux/amd64 "${args[@]}" -w "$PWD" -e LD_LIBRARY_PATH=/opt/dxc/lib \
    ubuntu:24.04 /opt/dxc/bin/dxc "$@"

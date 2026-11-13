#!/usr/bin/env bash
# Linux x86-64 release package, built inside Docker (ubuntu:24.04, linux/amd64) from any host with Docker
# (on an Apple Silicon Mac the container runs emulated: slow but fine).
#
#   tools/release/package_linux.sh [--no-tests] [--allow-stale-previews]
#
# Output: build-release/OpenD2K-<version>-linux-x86_64.tar.gz (+ .sha256) containing
#   OpenD2K-<version>-linux-x86_64/{d2k, libSDL3.so.0, assets/, licenses/, README.md, CHANGELOG.md, LICENSE.txt}
# (`cmake --install` layout, RPATH $ORIGIN: the folder runs from anywhere; start ./d2k).
# Build dir: build-release/linux-x86_64 (kept for incremental re-runs).
# Runtime requirement: glibc >= 2.38 and libstdc++ of GCC 13+ (Ubuntu 24.04+, Debian 13+, Fedora 39+; measured
# for 0.0.1: GLIBC_2.38, GLIBCXX_3.4.31); the script prints the
# highest GLIBC symbol version the binaries need. SDL loads X11 / Wayland / ALSA / PulseAudio / PipeWire /
# Vulkan from the system at run time.
# The original game data is mounted read-only only for the tests (when game/ exists); it never enters
# the package. The container is removed afterwards (--rm) and no Docker build cache is created. The
# linux/amd64 variant of ubuntu:24.04 (~120 MB) stays in Docker's image store for the next run (this Docker
# cannot remove one platform of a multi-platform tag without the others; `docker image rm ubuntu:24.04`
# removes all of them).
set -euo pipefail
source "$(dirname "$0")/common.sh"

TESTS=1
while [ $# -gt 0 ]; do
    case "$1" in
        --no-tests) TESTS=0; shift ;;
        --allow-stale-previews) ALLOW_STALE_PREVIEWS=1; shift ;;
        *) die "unknown option $1" ;;
    esac
done
command -v docker >/dev/null || die "docker not found"
check_terrain_assets
check_previews

IMAGE=ubuntu:24.04

NAME="OpenD2K-$VERSION-linux-x86_64"
BUILD="$OUT/linux-x86_64"
mkdir -p "$BUILD"
DATA_ARGS=()
if [ "$TESTS" = 1 ] && [ -d "$REPO/game/Dune 2000/data" ]; then
    DATA_ARGS=(-v "$(cd "$REPO/game/Dune 2000" && pwd -P):/gamedata:ro")
fi

log "Docker build ($IMAGE linux/amd64) -> $OUT/$NAME.tar.gz"
docker run --rm --platform linux/amd64 \
    -v "$REPO:/src:ro" -v "$BUILD:/build" -v "$OUT:/out" "${DATA_ARGS[@]+"${DATA_ARGS[@]}"}" \
    -e NAME="$NAME" -e JOBS="$JOBS" -e TESTS="$TESTS" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    "$IMAGE" bash /src/tools/release/linux_container_build.sh

finish_artifact "$OUT/$NAME.tar.gz"

#!/usr/bin/env bash
# Runs INSIDE the ubuntu:24.04 container started by tools/release/package_linux.sh:
#   /src (repository, read-only), /build (build dir), /out (artifacts), /gamedata (optional, read-only).
# Env: NAME (package folder name), JOBS, TESTS (1/0), HOST_UID / HOST_GID (owner of the outputs).
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

echo "== apt: compiler, CMake, Ninja, SDL3 build dependencies"
apt-get update -qq
# Same list as docs/building.md "Linux" / .github/workflows/build.yml.
apt-get install -y -qq --no-install-recommends build-essential cmake ninja-build pkg-config git ca-certificates \
    zlib1g-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libjack-dev libsndio-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
    libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
    libdbus-1-dev libibus-1.0-dev libudev-dev libwayland-dev libdecor-0-dev liburing-dev >/dev/null
gcc --version | head -1

DATA_DEF=()
[ -d /gamedata/data ] && DATA_DEF=(-DD2K_TEST_DATA_DIR=/gamedata/data)
echo "== configure"
cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DD2K_FETCH_SDL=ON -DD2K_APP_NAME=OpenD2K \
    "${DATA_DEF[@]+"${DATA_DEF[@]}"}" >/dev/null
echo "== build (-j $JOBS)"
cmake --build /build -j "$JOBS"

if [ "$TESTS" = 1 ]; then
    echo "== tests"
    (cd /build && ./formats_test >/dev/null && ./game_test >/dev/null) && echo "formats_test, game_test: ok"
    if [ -d /gamedata/data ]; then
        # Headless: SDL's offscreen video and dummy audio drivers.
        SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy D2K_RENDERER=sdl \
            /build/d2k --data /gamedata/data --simtest --map H5V1.MAP >/build/simtest.log 2>&1 \
            && echo "--simtest --map H5V1.MAP: ok" \
            || { echo "--simtest failed (tail of /build/simtest.log):"; tail -20 /build/simtest.log; exit 1; }
    fi
fi

echo "== install + check"
STAGE=/tmp/stage/$NAME
rm -rf /tmp/stage
cmake --install /build --prefix "$STAGE" >/dev/null
cp /src/assets/terrain/LICENSES.md "$STAGE/licenses/terrain-assets-CC0.md"
ldd "$STAGE/d2k" | sed 's/^/  /'
if ldd "$STAGE/d2k" | grep -q "not found"; then echo "missing libraries"; exit 1; fi
echo "highest GLIBC version needed: $( (objdump -T "$STAGE/d2k"; objdump -T "$STAGE"/libSDL3.so.0) \
    | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
strip --strip-unneeded "$STAGE/d2k" "$STAGE"/libSDL3.so.0* 2>/dev/null || true
du -sh "$STAGE"
tar -C /tmp/stage -czf "/out/$NAME.tar.gz" "$NAME"
chown "${HOST_UID:-0}:${HOST_GID:-0}" "/out/$NAME.tar.gz" || true
echo "wrote /out/$NAME.tar.gz"

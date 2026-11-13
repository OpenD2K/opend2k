#!/usr/bin/env bash
# macOS release package: OpenD2K.app (Release, SDL3 built from source and bundled, ad-hoc signed) in a zip.
#
#   tools/release/package_macos.sh [--arch universal|arm64] [--min-macos 11.0] [--no-tests] [--allow-stale-previews]
#
# Output: build-release/OpenD2K-<version>-macos-<arch>.zip (+ .sha256), containing
#   OpenD2K-<version>-macos-<arch>/OpenD2K.app, README.md, CHANGELOG.md, LICENSE.txt, licenses/
# Build dir: build-release/macos-<arch> (kept for incremental re-runs; delete it for a clean build).
#
# - SDL3 is always downloaded and built (D2K_FETCH_SDL=ON) for the chosen architectures and minimum
#   macOS: Homebrew's SDL3 is arm64-only and built for the running macOS, which would make the app refuse
#   to start on older systems.
# - universal (default) = arm64 + x86_64 (Intel Macs; Rosetta is not needed). The tests run natively and,
#   when Rosetta is installed, once more as x86_64.
# - Ad-hoc signature only: not notarised. Players have to allow it once (README "macOS"). A Developer ID
#   signature + notarisation needs a paid Apple developer account (docs/release.md).
# No game data is used for the package itself; the tests use game/ when it exists.
set -euo pipefail
source "$(dirname "$0")/common.sh"

ARCH=universal
MIN_MACOS=11.0
TESTS=1
while [ $# -gt 0 ]; do
    case "$1" in
        --arch) ARCH="$2"; shift 2 ;;
        --min-macos) MIN_MACOS="$2"; shift 2 ;;
        --no-tests) TESTS=0; shift ;;
        --allow-stale-previews) ALLOW_STALE_PREVIEWS=1; shift ;;
        *) die "unknown option $1" ;;
    esac
done
case "$ARCH" in
    universal) ARCHS="arm64;x86_64" ;;
    arm64) ARCHS="arm64" ;;
    x86_64) ARCHS="x86_64" ;;
    *) die "--arch universal|arm64|x86_64" ;;
esac
[ "$(uname)" = Darwin ] || die "run on macOS"
check_terrain_assets
check_previews

BUILD="$OUT/macos-$ARCH"
NAME="OpenD2K-$VERSION-macos-$ARCH"
log "Configure ($ARCHS, macOS >= $MIN_MACOS) in $BUILD"
cmake -S "$REPO" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DD2K_FETCH_SDL=ON -DCMAKE_OSX_ARCHITECTURES="$ARCHS" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN_MACOS" \
    -DD2K_APP_NAME=OpenD2K -DD2K_MACOS_BUNDLE_ID=com.opend2k.app >/dev/null
log "Build (-j $JOBS)"
cmake --build "$BUILD" -j "$JOBS"

APP="$BUILD/OpenD2K.app"
[ -d "$APP" ] || die "$APP not built"
EXE="$APP/Contents/MacOS/OpenD2K"

if [ "$TESTS" = 1 ]; then
    log "Tests (native)"
    (cd "$REPO" && "$BUILD/formats_test" >/dev/null && "$BUILD/game_test" >/dev/null) || die "tests failed"
    if [ -d "$REPO/game/Dune 2000/data" ]; then
        (cd "$REPO" && "$EXE" --simtest --map H5V1.MAP >/dev/null 2>&1) || die "--simtest failed"
    fi
    if [[ "$ARCHS" == *x86_64* ]] && arch -x86_64 /usr/bin/true 2>/dev/null; then
        log "Tests (x86_64 under Rosetta)"
        (cd "$REPO" && arch -x86_64 "$BUILD/game_test" >/dev/null) || die "x86_64 game_test failed"
        if [ -d "$REPO/game/Dune 2000/data" ]; then
            (cd "$REPO" && arch -x86_64 "$EXE" --simtest --map H5V1.MAP >/dev/null 2>&1) || die "x86_64 --simtest failed"
        fi
    fi
fi

log "Check the bundle"
lipo -archs "$EXE"
lipo -archs "$APP/Contents/Frameworks/libSDL3.0.dylib"
otool -L "$EXE" | grep -v ':$' | sort -u
if otool -L "$EXE" | grep -v ':$' | grep -E '/opt/homebrew|/usr/local|/Users/' ; then die "the executable links a non-system library"; fi
otool -l "$EXE" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print "minos " $2; exit}'
/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' -c 'Print :CFBundleIdentifier' "$APP/Contents/Info.plist"
du -sh "$APP"

log "Stage $NAME"
STAGE="$OUT/stage/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE"
ditto "$APP" "$STAGE/OpenD2K.app"
stage_docs "$STAGE"
SDL_LICENSE="$(ls "$BUILD"/_deps/sdl3-src/LICENSE.txt 2>/dev/null || true)"
[ -n "$SDL_LICENSE" ] && cp "$SDL_LICENSE" "$STAGE/licenses/SDL3-LICENSE.txt"
cp "$REPO/assets/terrain/LICENSES.md" "$STAGE/licenses/terrain-assets-CC0.md"
# Re-sign the whole bundle (ad-hoc) and verify.
codesign --force --deep --sign - "$STAGE/OpenD2K.app"
codesign --verify --deep --strict "$STAGE/OpenD2K.app"
echo "codesign: ok (ad-hoc)"

ZIP="$OUT/$NAME.zip"
rm -f "$ZIP"
(cd "$OUT/stage" && ditto -c -k --keepParent "$NAME" "$ZIP")
finish_artifact "$ZIP"

#!/usr/bin/env bash
# Android release package: build-release/OpenD2K-<version>-android.apk (+ .sha256), arm64-v8a + x86_64.
#
#   tools/release/package_android.sh [--debug] [--allow-stale-previews]
#
# Needs: JAVA_HOME (JDK 17), ANDROID_HOME (SDK with platform 35, build-tools 35, NDK 27.2.12479018,
# CMake 3.31.6; docs/android.md). Gradle itself comes with the wrapper (android/gradlew).
# Signing: a release APK is signed with the project's release key when D2K_KEYSTORE (path) and
# D2K_KEYSTORE_PASS are set (key alias: D2K_KEY_ALIAS, default "opend2k"; key password = store password
# unless D2K_KEY_PASS). The key is never in the repository (owner-held; docs/android.md "Signing").
# Without them, or with --debug, the APK is debug-signed (sideloading tests only - not for a release).
# The SDL source tree for SDL's Java activity: build-release/sdl3-src (downloaded once, the version and
# hash of cmake/FetchSDL.cmake), or SDL_SRC=<dir>.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$REPO/build-release"
die() { echo "error: $*" >&2; exit 1; }
log() { echo; echo "== $*"; }
DEBUG=0
ALLOW_STALE_PREVIEWS=0
for a in "$@"; do
    case "$a" in
        --debug) DEBUG=1 ;;
        --allow-stale-previews) ALLOW_STALE_PREVIEWS=1 ;;
        *) die "unknown option $a" ;;
    esac
done
# The shipped Graphics previews (assets/previews, packed into the APK) must be current (common.sh check_previews).
PY="$REPO/.venv/bin/python"; [ -x "$PY" ] || PY=python3
if ! "$PY" -I "$REPO/tools/release/check_previews.py"; then
    [ "$ALLOW_STALE_PREVIEWS" = 1 ] || die "assets/previews is stale: run tools/release/render_previews.sh and commit it (or pass --allow-stale-previews)"
    echo "warning: packaging with STALE previews (--allow-stale-previews)" >&2
fi
[ -n "${JAVA_HOME:-}" ] || die "set JAVA_HOME to a JDK 17"
[ -n "${ANDROID_HOME:-}" ] || die "set ANDROID_HOME to the Android SDK"
VERSION="$(sed -n 's/^project(d2k VERSION \([0-9.]*\).*/\1/p' "$REPO/CMakeLists.txt")"
[ -n "$VERSION" ] || die "version not found in CMakeLists.txt"
mkdir -p "$OUT"

log "SDL source (Java activity + native)"
SDL_VER="$(sed -n 's/^set(D2K_SDL_VERSION "\(.*\)")/\1/p' "$REPO/cmake/FetchSDL.cmake")"
SDL_HASH="$(sed -n 's/.*URL_HASH SHA256=\([0-9a-f]*\).*/\1/p' "$REPO/cmake/FetchSDL.cmake")"
SDL_SRC="${SDL_SRC:-$OUT/sdl3-src}"
if [ ! -f "$SDL_SRC/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java" ]; then
    TGZ="$OUT/SDL3-$SDL_VER.tar.gz"
    curl -fsSL -o "$TGZ" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL3-$SDL_VER.tar.gz"
    echo "$SDL_HASH  $TGZ" | sha256sum -c - >/dev/null || die "SDL tarball hash mismatch"
    rm -rf "$SDL_SRC" && mkdir -p "$SDL_SRC"
    tar xzf "$TGZ" -C "$SDL_SRC" --strip-components=1
fi
echo "SDL $SDL_VER: $SDL_SRC"

TASK=assembleRelease
if [ "$DEBUG" = 1 ] || [ -z "${D2K_KEYSTORE:-}" ] || [ -z "${D2K_KEYSTORE_PASS:-}" ]; then
    TASK=assembleDebug
    echo "note: debug-signed APK (no D2K_KEYSTORE / D2K_KEYSTORE_PASS or --debug) - sideloading tests only"
else
    [ -f "$D2K_KEYSTORE" ] || die "keystore not found: $D2K_KEYSTORE"
    export ORG_GRADLE_PROJECT_d2kKeystore="$D2K_KEYSTORE"
    export ORG_GRADLE_PROJECT_d2kStorePass="$D2K_KEYSTORE_PASS"
    export ORG_GRADLE_PROJECT_d2kKeyAlias="${D2K_KEY_ALIAS:-opend2k}"
    export ORG_GRADLE_PROJECT_d2kKeyPass="${D2K_KEY_PASS:-$D2K_KEYSTORE_PASS}"
fi

log "Gradle $TASK"
cd "$REPO/android"
GRADLEW=./gradlew
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) GRADLEW=./gradlew.bat ;; esac
"$GRADLEW" --no-daemon "$TASK" "-Psdl.dir=$SDL_SRC"

KIND=release; [ "$TASK" = assembleDebug ] && KIND=debug
SRC_APK="$(ls app/build/outputs/apk/$KIND/*.apk | head -1)"
NAME="OpenD2K-$VERSION-android"; [ "$KIND" = debug ] && NAME="$NAME-DEBUG"
cp "$SRC_APK" "$OUT/$NAME.apk"
( cd "$OUT" && sha256sum "$NAME.apk" > "$NAME.apk.sha256" )

log "Verify"
APKSIGNER="$(ls -d "$ANDROID_HOME"/build-tools/*/ | sort -V | tail -1)apksigner"
[ -f "$APKSIGNER.bat" ] && APKSIGNER="$APKSIGNER.bat"
"$APKSIGNER" verify --print-certs "$OUT/$NAME.apk" | grep -E 'Signer #1 certificate (DN|SHA-256)' || true
unzip -l "$OUT/$NAME.apk" | grep -E 'lib/.*\.so$' | awk '{print "  " $4 "  " $1 " bytes"}'
echo "artifact: $OUT/$NAME.apk ($(du -h "$OUT/$NAME.apk" | cut -f1), sha256 $(cut -d' ' -f1 < "$OUT/$NAME.apk.sha256"))"

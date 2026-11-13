#!/usr/bin/env bash
# iOS / iPadOS release package: an UNSIGNED .ipa for sideloading (players sign it themselves with
# AltStore / SideStore / Sideloadly, or install it with TrollStore; docs/ios.md "Sideload the IPA").
#
#   tools/release/package_ios.sh [--min-ios 17.0] [--config Release|RelWithDebInfo] [--allow-stale-previews]
#
# Output: build-release/OpenD2K-<version>-ios-unsigned.ipa (+ .sha256) = a zip with Payload/OpenD2K.app
# (arm64, iphoneos, no code signature, bundle id com.opend2k.app, CFBundleShortVersionString = <version>).
# Build dir: build-release/ios (kept for incremental re-runs; D2K_IOS_BUILD=<dir> to use another one).
#
# - Needs Xcode with the iOS SDK and the SDL3 release sources (docs/ios.md §1). SDL3_SRC=<dir> or
#   -DD2K_SDL_SOURCE's default ~/Tools/sdl-ios/SDL3-3.4.20; when neither exists the pinned release
#   (same version + sha256 as cmake/FetchSDL.cmake) is downloaded once to build-release/sdl3-ios-src.
# - Built with CODE_SIGNING_ALLOWED=NO: the IPA carries no signature and no provisioning profile, which
#   is what AltStore / SideStore / Sideloadly expect (they sign it with the user's Apple ID). The script
#   checks that an ad-hoc re-signature of a copy verifies (`codesign -fs -`), as those tools would do.
# No game data goes into the package: players copy the Gruntmods installer in through the Files app.
set -euo pipefail
source "$(dirname "$0")/common.sh"

MIN_IOS=17.0
CONFIG=Release
while [ $# -gt 0 ]; do
    case "$1" in
        --min-ios) MIN_IOS="$2"; shift 2 ;;
        --config) CONFIG="$2"; shift 2 ;;
        --allow-stale-previews) ALLOW_STALE_PREVIEWS=1; shift ;;
        *) die "unknown option $1" ;;
    esac
done
case "$CONFIG" in Release|RelWithDebInfo) ;; *) die "--config Release|RelWithDebInfo" ;; esac
[ "$(uname)" = Darwin ] || die "run on macOS (Xcode)"
command -v xcodebuild >/dev/null || die "xcodebuild not found (install Xcode)"
xcrun --sdk iphoneos --show-sdk-path >/dev/null 2>&1 || die "no iOS SDK (Xcode > Settings > Components)"
check_terrain_assets
check_previews

# SDL3 sources (built as a static library inside the Xcode project).
SDL_VERSION=3.4.20
SDL_SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
SDL_DIR="${SDL3_SRC:-$HOME/Tools/sdl-ios/SDL3-$SDL_VERSION}"
if [ ! -f "$SDL_DIR/CMakeLists.txt" ]; then
    SDL_DIR="$OUT/sdl3-ios-src/SDL3-$SDL_VERSION"
    if [ ! -f "$SDL_DIR/CMakeLists.txt" ]; then
        log "Download SDL3 $SDL_VERSION"
        mkdir -p "$OUT/sdl3-ios-src"
        TGZ="$OUT/sdl3-ios-src/SDL3-$SDL_VERSION.tar.gz"
        curl -fL -o "$TGZ" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz"
        [ "$(sha256 "$TGZ")" = "$SDL_SHA256" ] || die "SDL3 tarball sha256 mismatch"
        tar xzf "$TGZ" -C "$OUT/sdl3-ios-src"
    fi
fi

BUILD="${D2K_IOS_BUILD:-$OUT/ios}"
NAME="OpenD2K-$VERSION-ios-unsigned"
log "Configure (iphoneos arm64, iOS >= $MIN_IOS, SDL3 $SDL_DIR) in $BUILD"
cmake -S "$REPO" -B "$BUILD" -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN_IOS" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DD2K_SDL_SOURCE="$SDL_DIR" -DD2K_IOS_BUNDLE_ID=com.opend2k.app \
    -DD2K_IOS_TEAM= >/dev/null

log "Build ($CONFIG, unsigned, -jobs $JOBS)"
# DEPLOYMENT_POSTPROCESSING + STRIP_INSTALLED_PRODUCT: strip the local symbols from the shipped binary.
xcodebuild -project "$BUILD/d2k.xcodeproj" -target d2k_ios -sdk iphoneos -configuration "$CONFIG" \
    -jobs "$JOBS" ARCHS=arm64 ONLY_ACTIVE_ARCH=NO \
    CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" DEVELOPMENT_TEAM="" \
    DEPLOYMENT_POSTPROCESSING=YES STRIP_INSTALLED_PRODUCT=YES STRIP_STYLE=non-global \
    build -quiet

APP="$BUILD/$CONFIG-iphoneos/OpenD2K.app"
[ -d "$APP" ] || die "$APP not built"
EXE="$APP/OpenD2K"
PLIST="$APP/Info.plist"

log "Check the bundle"
pb() { /usr/libexec/PlistBuddy -c "Print :$1" "$PLIST" 2>/dev/null; }
ARCHS_OUT="$(lipo -archs "$EXE")"
echo "archs: $ARCHS_OUT"
[ "$ARCHS_OUT" = arm64 ] || die "the executable is not arm64-only"
otool -l "$EXE" | awk '/LC_BUILD_VERSION/{f=1} f&&/platform/{p=$2} f&&/minos/{print "platform " p " minos " $2; exit}'
otool -l "$EXE" | awk '/LC_BUILD_VERSION/{f=1} f&&/platform/{print $2; exit}' | grep -qx 2 \
    || die "the executable is not built for iphoneos (LC_BUILD_VERSION platform 2)"
otool -L "$EXE" | grep -v ':$' | grep -vE '^\s*(/System/|/usr/lib/)' && die "the executable links a non-system library"
[ "$(pb CFBundleIdentifier)" = com.opend2k.app ] || die "CFBundleIdentifier is '$(pb CFBundleIdentifier)'"
[ "$(pb CFBundleShortVersionString)" = "$VERSION" ] || die "CFBundleShortVersionString != $VERSION"
[ "$(pb CFBundleExecutable)" = OpenD2K ] || die "CFBundleExecutable != OpenD2K"
[ "$(pb MinimumOSVersion)" = "$MIN_IOS" ] || die "MinimumOSVersion is '$(pb MinimumOSVersion)'"
[ "$(pb UIFileSharingEnabled)" = true ] || die "UIFileSharingEnabled missing"
[ "$(pb LSSupportsOpeningDocumentsInPlace)" = true ] || die "LSSupportsOpeningDocumentsInPlace missing"
pb CFBundleIcons:CFBundlePrimaryIcon:CFBundleIconName | grep -qx AppIcon || die "no AppIcon in Info.plist"
[ -f "$APP/Assets.car" ] || die "Assets.car missing (icon asset catalog)"
ls "$APP"/AppIcon*.png >/dev/null 2>&1 || die "no AppIcon*.png in the bundle"
for d in assets/models assets/previews/index.json; do [ -e "$APP/$d" ] || die "$d missing in the bundle"; done
[ -d "$APP/assets/terrain/materials/sand" ] || echo "warning: no terrain textures in the bundle" >&2
[ -e "$APP/_CodeSignature" ] && die "the app is signed (expected unsigned)"
[ -e "$APP/embedded.mobileprovision" ] && die "the app carries a provisioning profile"
codesign -dv "$APP" >/dev/null 2>&1 && die "codesign finds a signature (expected none)"
echo "unsigned: ok; bundle id $(pb CFBundleIdentifier), version $(pb CFBundleShortVersionString) ($(pb CFBundleVersion))"
du -sh "$APP"

log "Stage Payload/OpenD2K.app"
STAGE="$OUT/stage/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE/Payload"
ditto "$APP" "$STAGE/Payload/OpenD2K.app"
# Licences inside the app (an IPA has no room for loose files next to it).
LIC="$STAGE/Payload/OpenD2K.app/licenses"
mkdir -p "$LIC"
cp "$REPO/LICENSE" "$LIC/LICENSE.txt"
cp "$REPO/third_party/LICENSES.md" "$LIC/THIRD-PARTY.md"
cp "$REPO/third_party/fonts/play/OFL.txt" "$LIC/Play-Font-OFL.txt"
cp "$REPO/third_party/cgltf/LICENSE" "$LIC/cgltf-LICENSE.txt"
cp "$REPO/third_party/lzma/README.md" "$LIC/LZMA-SDK-README.md"
[ -f "$SDL_DIR/LICENSE.txt" ] && cp "$SDL_DIR/LICENSE.txt" "$LIC/SDL3-LICENSE.txt"
cp "$REPO/assets/terrain/LICENSES.md" "$LIC/terrain-assets-CC0.md"

log "Ad-hoc re-sign test (a copy, as AltStore / Sideloadly would sign it)"
T="$OUT/stage/$NAME-resign-test"
rm -rf "$T"
ditto "$STAGE/Payload/OpenD2K.app" "$T/OpenD2K.app"
codesign -fs - "$T/OpenD2K.app" 2>&1
codesign --verify --strict "$T/OpenD2K.app"
codesign -dv "$T/OpenD2K.app" 2>&1 | grep -E '^(Identifier|Format|Signature)'
rm -rf "$T"
echo "re-sign: ok"

IPA="$OUT/$NAME.ipa"
rm -f "$IPA"
# An IPA is a plain zip of Payload/ (no __MACOSX / resource forks).
(cd "$STAGE" && ditto -c -k --norsrc --noextattr --keepParent Payload "$IPA")
unzip -l "$IPA" | awk 'NR==4{print "first entry: " $4}'
finish_artifact "$IPA"

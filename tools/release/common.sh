# Shared helpers for tools/release/*.sh (sourced, not run).
# REPO = repository root, VERSION = project(VERSION) from CMakeLists.txt, OUT = build-release/.

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="$(sed -n 's/^project(d2k VERSION \([0-9][0-9.]*\).*/\1/p' "$REPO/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "error: no project(d2k VERSION x.y.z) in CMakeLists.txt" >&2; exit 1; }
OUT="${D2K_RELEASE_OUT:-$REPO/build-release}"
JOBS="${JOBS:-3}"
mkdir -p "$OUT"

log() { printf '\n== %s\n' "$*"; }
die() { echo "error: $*" >&2; exit 1; }

# Size of a file in MB (one decimal).
size_mb() { awk -v b="$(wc -c < "$1" | tr -d ' ')" 'BEGIN { printf "%.1f MB", b / 1048576 }'; }

# sha256 of a file (macOS shasum or GNU sha256sum).
sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

# Writes <file>.sha256 next to an artifact and prints a summary line.
finish_artifact() {
    local f="$1"
    (cd "$(dirname "$f")" && echo "$(sha256 "$f")  $(basename "$f")" > "$(basename "$f").sha256")
    echo "artifact: $f ($(size_mb "$f"), sha256 $(cut -d' ' -f1 < "$f.sha256"))"
}

# The CC0 terrain textures are not in git (assets/terrain/.gitignore): packages should carry them so
# the designed 3D terrain looks as intended. Warn (or fail with REQUIRE_TERRAIN=1) when missing.
check_terrain_assets() {
    if [ ! -d "$REPO/assets/terrain/materials/sand" ]; then
        local msg="assets/terrain/materials missing: run 'python3 tools/fetch_terrain_assets.py' first (CC0 textures, ~30 MB); without them the designed terrain falls back to the original tiles"
        [ "${REQUIRE_TERRAIN:-0}" = 1 ] && die "$msg"
        echo "warning: $msg" >&2
    fi
}

# The shipped Graphics previews (assets/previews) must match this engine version and the art pack they
# show (tools/release/check_previews.py; extra arguments go to it, e.g. --pack DIR). Stale: a big warning with
# the regenerate command, and the script stops unless --allow-stale-previews (ALLOW_STALE_PREVIEWS=1).
check_previews() {
    local py="$REPO/.venv/bin/python"
    [ -x "$py" ] || py=python3
    if ! "$py" -I "$REPO/tools/release/check_previews.py" "$@"; then
        if [ "${ALLOW_STALE_PREVIEWS:-0}" = 1 ]; then
            echo "warning: packaging with STALE previews (--allow-stale-previews)" >&2
            return 0
        fi
        die "assets/previews is stale: run tools/release/render_previews.sh and commit it (or pass --allow-stale-previews)"
    fi
}

# Files every package carries next to the program (the macOS zip; Windows / Linux get them from
# `cmake --install`).
stage_docs() {
    local dst="$1"
    mkdir -p "$dst/licenses"
    cp "$REPO/README.md" "$dst/README.md"
    cp "$REPO/CHANGELOG.md" "$dst/CHANGELOG.md"
    cp "$REPO/LICENSE" "$dst/LICENSE.txt"
    cp "$REPO/third_party/LICENSES.md" "$dst/licenses/THIRD-PARTY.md"
    cp "$REPO/third_party/fonts/play/OFL.txt" "$dst/licenses/Play-Font-OFL.txt"
    cp "$REPO/third_party/cgltf/LICENSE" "$dst/licenses/cgltf-LICENSE.txt"
    cp "$REPO/third_party/lzma/README.md" "$dst/licenses/LZMA-SDK-README.md"
}

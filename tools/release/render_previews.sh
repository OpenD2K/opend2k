#!/usr/bin/env bash
# Regenerates the shipped Graphics / welcome page previews (assets/previews) - before every release and
# after changing any model or rendered frame of the art pack:
#
#   tools/release/render_previews.sh [--build DIR] [--data DIR] [--pack DIR] [--ps1 IMAGE.cue]
#
# 1. `d2k --render-previews assets/previews` renders every picture the menus show for the default setups
#    (platform/preset_previews.hpp: the presets / welcome cards and every value of every picture row of the
#    Custom tab) from the game data and the art pack, with the watermark on pictures of the original art;
# 2. tools/release/check_previews.py --stamp shrinks the PNGs and records the pack's content sha256, so
#    tools/release/package_*.sh can tell when the set is stale.
# Then commit assets/previews. Defaults: build/, game/Dune 2000/data, artpacks/opend2k-redesign, the first
# PS1 disc image under game/ps1*/ (the "3D models: PS1" picture; without one that picture shows "no disc").
set -euo pipefail
source "$(dirname "$0")/common.sh"

BUILD="$REPO/build"
DATA="$REPO/game/Dune 2000/data"
PACK="$REPO/artpacks/opend2k-redesign"
PS1=""
while [ $# -gt 0 ]; do
    case "$1" in
        --build) BUILD="$2"; shift 2 ;;
        --data) DATA="$2"; shift 2 ;;
        --pack) PACK="$2"; shift 2 ;;
        --ps1) PS1="$2"; shift 2 ;;
        *) die "unknown option $1" ;;
    esac
done
[ -x "$BUILD/d2k" ] || die "$BUILD/d2k not found (cmake --build build)"
[ -d "$DATA" ] || die "game data not found: $DATA (--data DIR)"
[ -f "$PACK/pack.ini" ] || die "art pack not found: $PACK (--pack DIR)"
if [ -z "$PS1" ]; then
    PS1="$( (ls "$REPO"/game/ps1*/*.cue 2>/dev/null || true) | head -1)"
fi
PY="${PY:-$REPO/.venv/bin/python}"
[ -x "$PY" ] || PY=python3
"$PY" -c "import PIL" 2>/dev/null || die "$PY needs Pillow (the project venv)"

log "Render (d2k --render-previews)"
ARGS=(--render-previews "$REPO/assets/previews" --data "$DATA" --preview-pack "$PACK")
[ -n "$PS1" ] && ARGS+=(--ps1 "$PS1")
set +e
"$BUILD/d2k" "${ARGS[@]}" 2>&1 | grep -E -- "--render-previews|rror"
rc=${PIPESTATUS[0]}
set -e
[ "$rc" = 0 ] || die "d2k --render-previews failed (exit $rc)"
[ -f "$REPO/assets/previews/index.json" ] || die "the render wrote no index.json"

log "Stamp (shrink the PNGs, record the pack's content hash)"
"$PY" -I "$REPO/tools/release/check_previews.py" --stamp --pack "$PACK"
"$PY" -I "$REPO/tools/release/check_previews.py" --pack "$PACK"
echo "now commit assets/previews"

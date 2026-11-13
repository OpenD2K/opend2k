"""Ordos research centre - the same design as the Atreides / Harkonnen research centre (the Ordos art 50 is its own
set of frames, but the same picture: grey mushroom caps on a dark root), so this is tools/models/
atreides_research_centre.py built under the Ordos key, with the Atreides kit's grey palette (ordos_kit is NOT
imported: it would re-colour the shared materials brass).

    blender -b --factory-startup --python tools/models/ordos_research_centre.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ORDOS_RESEARCH_CENTRE .venv/bin/python -I tools/models/ordos_check/pack.py

Art 50: main 3868, damaged 3869, animation 55 = 20 overlay frames (the rims under three caps pulse); build-up 53 kept.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import atreides_kit as K  # noqa: E402
import atreides_research_centre as A  # noqa: E402

KEY = "ORDOS_RESEARCH_CENTRE"

if __name__ == "__main__":
    if "--" not in sys.argv:
        sys.argv.append("--")
    if "--out" not in sys.argv:
        sys.argv += ["--out", os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-ordos-research_centre")]
    P = A.P
    K.run(KEY, A.build, spec=lambda: dict(anim="glow", glow=A.glows(),
                                          flags=dict(shadow_azimuth=P["sh_az"], sun_elevation=P["sun_el"], fill=(0.3, 90.0, 35.0), sky_strength=0.09)), P=P)

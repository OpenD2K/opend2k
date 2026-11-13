"""Harkonnen spice silo - the original's design: the Atreides silo (tools/models/atreides_silo.py: a low dome on a
drum with a dark window belt where the spice level shows, three house patches on the dome) in the Harkonnen dress.

    blender -b --factory-startup --python tools/models/harkonnen_silo.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=HARKONNEN_SILO .venv/bin/python -I tools/models/harkonnen_check/pack.py

How the original was read (art 26: frames 3386..3389 = the four fill levels, 3390 = damaged, no base, no animation;
build-up 38 kept): the outline is the Atreides silo's exactly (overlap 1.0). Differences: the dome is dark
gun-metal with a pale sheen, the drum blackened, the foot is not a white ring but dark plating half buried in
orange sandstone rubble; the house patches are narrow red armour strips.
Poses: the node "fill" (the spice behind the window) scaled up in three steps -> frames 3387..3389.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harkonnen_kit as K  # noqa: E402
import atreides_silo as A  # noqa: E402

KEY = "HARKONNEN_SILO"
P = A.P


def build(damaged=False):
    built = A.build(damaged)
    main = built["nodes"]["main"]
    K.remat(main, {"ring": "hk_black", "belt": "hk_black", "eave": "hk_plate", "pier": "stone_dk", "dome": "hk_black", "crown": "hk_brass",
                   "mullion": "stone", "win_head": "wall", "foot": "hk_black"})
    c, r = P["c"], P["r"]
    for i, (a, s) in enumerate(((205.0, 5.6), (250.0, 4.6), (300.0, 5.2), (340.0, 3.6), (165.0, 3.8))):
        main.extend(K.rocks(f"rub{i}", K.polar(c, r + 2.2, a), s, 40 + i, 4, 0.6))
    K.dress(built, 0.25)
    return built


if __name__ == "__main__":
    K.run(KEY, build, poses=[A.level(0.36), A.level(0.68), A.level(1.0)], rest=A.level(0.001),
          spec=dict(levels=[-1, 0, 1, 2], flags=dict(shadow_azimuth=25.0, fill=(0.35, 90.0, 35.0), sky_strength=0.1)), P=P)

"""Engineer - the light infantry reference (infantry_v1.py, infantry_derive.py) without weapon or pack: house-colour
jacket and hood, pale overalls, a brown tool case in each hand.

    blender -b --factory-startup --python tools/models/infantry_engineer.py -- --out artpacks/models-infantry-engineer
    render_pack.py --models artpacks/models-infantry-engineer --out artpacks/rendered-infantry-engineer --types ENGINEER <RENDER_FLAGS>

How the original was read (DATA.R16 frames 1294-1565, 236 of 272 present; measurements only, infantry_check with
D2K_INF_UNIT=ENGINEER):
  * A slighter figure (72 opaque px standing against 74; walking 61), 53-57 % house colour: a hood / helmet over the
    whole head, the jacket and the boots. Trousers and forearms are white with grey shade (30 % neutral, 6 % white),
    the face a small orange patch (grey goggles over it from the south), an orange patch on the back of the hood.
  * No weapon, no pack: a dark-brown tool case hangs from each hand (2 x 2 px at hip height on both sides), the arms
    swing with them when walking.
  * Rows 7-11 and 17-21 (fire, prone fire) are left-over frames of another soldier with a flame; the engineer has
    no weapon and the game never plays them. Ours: the standing / prone pose, no flash.
Poses: infantry_engineer_poses.json (the reference's legs and body; hands refitted per direction).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import infantry_derive as DV  # noqa: E402
from infantry_derive import K, L, Bx, V

import numpy as np


def parts(P):
    P = DV.drop(P, bones=("rifle", "flash"), mats=("pack", "pack_dark", "roll", "pouch", "HOUSE_plate"))
    out = []
    for p in P:
        fx = p.get("fx")
        b, m = p["bone"], p["mat"]
        if not fx and (b[:-2] in ("thigh", "shin") or b == "pelvis") and m == "HOUSE":
            p = dict(p, mat="white")                      # overalls: white from the waist down
        elif not fx and b[:-2] == "forearm" and m in ("HOUSE", "skin", "belt"):
            p = dict(p, mat="white")                      # pale sleeves and gloves
        elif not fx and m in ("boot", "sole") and b[:-2] in ("foot", "shin"):
            p = dict(p, mat="HOUSE", shade=0.42)          # house-colour boots (dark)
        elif not fx and b == "chest" and m == "belt":
            continue                                      # no harness
        elif not fx and b == "head" and m in ("belt", "HOUSE"):
            continue                                      # the reference's helmet rim, crest and chin strap
        out.append(p)
    P = out
    h = V.D["head"]
    # the hood: over the whole head, open at the face; goggles on the brow; an orange patch at the back
    P += V.stretch([L("head", "HOUSE", "z", [(0.2, 0, -0.8, 1.5, 1.3), (0.9, 0, -0.8, 1.72, 1.5), (1.75, 0, -0.7, 1.78, 1.6),
                                             (2.2, 0, -0.15, 1.66, 1.8), (2.7, 0, -0.1, 1.15, 1.25), (2.95, 0, -0.1, 0.3, 0.32)],
                      segs=18, shade=0.7),
                    Bx("head", "goggle", (0.0, 1.62, 1.72), (1.9, 0.5, 0.5), n=3),
                    Bx("head", "skin", (0.0, -2.25, 1.15), (1.7, 0.6, 1.3), n=3)], h, h, h)
    # a dark waist band, pale bib straps
    P += V.stretch([L("chest", "HOUSE", "z", [(-0.1, 0, 0, 2.32, 1.5, 3), (0.75, 0, 0, 2.36, 1.52, 3)], shade=0.45),
                    Bx("chest", "white", (-1.2, 1.5, 3.3), (0.5, 0.3, 2.6)),
                    Bx("chest", "white", (1.2, 1.5, 3.3), (0.5, 0.3, 2.6))], V.D["torso_w"], V.D["torso_d"], V.D["neck_z"] / 5.55)
    # the tool cases: one in each hand, hanging below it (the forearm's +y runs to the hand)
    for side in ("L", "R"):
        P += [Bx("forearm." + side, "case", (0.0, 4.75, 0.0), (1.35, 2.3, 2.5), n=4),
              Bx("forearm." + side, "case_dark", (0.0, 3.75, 0.0), (0.4, 0.5, 1.3))]        # handle
    return P


def carry(row, col):
    name = V.ROW_SET[row]
    if name == "walk":
        return DV.arm_swing(row, reach=1.8, rest=(2.9, 0.4, -1.3))
    if name in ("stand", "fire"):
        return DV.hands([-3.0, 0.5, -1.4], [3.0, 0.5, -1.4])
    if name == "kneel":
        return DV.hands([-3.0, 1.2, -1.0], [3.0, 1.2, -1.0])
    return {}       # lying: the reference's arms (forward, on the elbows), no flash


def hook(row, col, spec, fit):
    if spec is None:
        return spec
    if 7 <= row <= 11:                      # no weapon: the "fire" rows stand
        st = V.frame_spec(0, col, fit)
        spec = dict(spec, pose=st["pose"], yaw=st["yaw"], shift=st["shift"])
    elif 17 <= row <= 21:                   # and the "prone fire" rows lie still
        st = V.frame_spec(13, col, fit)
        spec = dict(spec, pose=st["pose"], yaw=st["yaw"], shift=st["shift"])
    spec["pose"]["flash"] = 0
    return DV.fidget_hands(row, col, spec, fit)


DV.setup(
    "infantry_engineer", "ENGINEER", "ENGINEER", parts=parts, carry=carry, frame_hook=hook,
    classes=dict(white=4, case=4, case_dark=4, goggle=4),
    col=dict(white=(0.80, 0.87, 0.86), case=(0.40, 0.17, 0.035), case_dark=(0.20, 0.08, 0.02), goggle=(0.42, 0.44, 0.44)),
    spec=dict(white=(0.0, 0.8, "paint"), case=(0.0, 0.6, "paint"), case_dark=(0.0, 0.6, "dark"), goggle=(0.3, 0.3, "steel")),
    film=dict(white=(0.62, 0.66, 0.64), case=(0.36, 0.17, 0.05), case_dark=(0.2, 0.09, 0.03), goggle=(0.36, 0.38, 0.38)),
    glow=dict(white=0.07), extra=dict(PLAIN=("skin", "eye", "white"), GROW=1.1),
    flash_rows=(), fit_dir=DV.FIT_DIR_HANDS,
)
DV.finish(__name__)

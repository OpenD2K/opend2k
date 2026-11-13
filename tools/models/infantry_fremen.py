"""Fremen - the light infantry reference (infantry_v1.py, infantry_derive.py) in a desert robe: tan hood and robe to
the ground, a house-colour cloak over shoulders and back, a blue face mask, a long thin black rifle.

    blender -b --factory-startup --python tools/models/infantry_fremen.py -- --out artpacks/models-infantry-fremen
    render_pack.py --models artpacks/models-infantry-fremen --out artpacks/rendered-infantry-fremen --types FREMEN <RENDER_FLAGS>

How the original was read (DATA.R16 frames 750-1021, 236 of 272 present; shared by FREMEN and FR NON STEALTH;
measurements only, infantry_check with D2K_INF_UNIT=FREMEN):
  * House colour is used differently from the other infantry: only 24 % of the figure (the light infantry 50 %) -
    the cloak over the shoulders and the upper back (a round blue shape of 5 x 5 px from behind). 61 % is sand
    tan: the hood (the top of the head is tan from every side) and a robe that reaches the ground and hides the legs
    (91 opaque px standing; only the black boot tips show).
  * The face is a fixed light blue (mask / stillsuit, 2 px), the hands orange, the rifle a thin black line of 9 px
    held level across the body, no navy fore-end; its flash is a tan-orange flame in rows 7 / 9 / 11 and 17 / 19 / 21.
Poses: infantry_fremen_poses.json (started from the light infantry's and refitted).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import infantry_derive as DV  # noqa: E402
from infantry_derive import K, L, Bx, V  # noqa: E402


def parts(P):
    P = DV.drop(P, mats=("pack", "pack_dark", "roll", "pouch", "HOUSE_plate"))
    out = []
    for p in P:
        fx = p.get("fx")
        b, m = p["bone"], p["mat"]
        if fx:
            out.append(p)
            continue
        if b == "head":
            continue                                       # hood and mask instead
        if b[:-2] == "thigh" and "box" in p:
            continue                                       # no thigh pockets
        if b == "rifle":                                   # a thin black rifle
            p = V.stretch([dict(p, mat="gun")], 0.72, 1.12, 0.78)[0]
        elif m == "HOUSE" and (b == "pelvis" or b[:-2] in ("thigh", "shin", "forearm")):
            p = dict(p, mat="robe")
            if b[:-2] in ("thigh", "shin"):                # the robe hides the legs: wide cloth round them
                p = V.stretch([p], 1.45, 1.0, 1.45)[0]
        elif m == "belt":
            if b == "chest":
                continue                                   # no harness, no bedroll straps
            p = dict(p, mat="robe_dark")
        out.append(p)
    P = out
    tw, td, tz = V.D["torso_w"], V.D["torso_d"], V.D["neck_z"] / 5.55
    h = V.D["head"]
    P += V.stretch([
        # the robe's skirt (with the pelvis)
        L("pelvis", "robe", "z", [(-6.6, 0, 0.1, 3.5, 2.8, 3), (-3.2, 0, 0.05, 2.9, 2.15, 3), (0.9, 0, 0, 2.4, 1.6, 3)], segs=14),
        L("pelvis", "robe_dark", "z", [(0.7, 0, 0, 2.46, 1.64, 3), (1.3, 0, 0, 2.46, 1.64, 3)], segs=14)], tw, td, 1.0)       # sash
    P += V.stretch([
        # the cloak: house colour, over shoulders and back, open in front where the robe shows
        L("chest", "HOUSE_plate", "z", [(0.9, 0, -0.55, 2.7, 1.9, 3), (2.6, 0, -0.65, 3.15, 2.25, 3), (4.7, 0, -0.5, 3.1, 2.05, 3),
                                        (5.7, 0, -0.3, 2.0, 1.45, 3)], segs=16, shade=0.66),
        Bx("chest", "robe", (0.0, 1.62, 2.6), (1.5, 0.5, 5.0), n=3),
        Bx("chest", "robe_dark", (0.0, 1.9, 2.6), (0.35, 0.2, 4.6))], tw, td, tz)
    P += V.stretch([
        L("head", "robe", "z", [(-0.5, 0, -0.35, 1.45, 1.4), (0.5, 0, -0.3, 1.8, 1.8), (1.6, 0, -0.25, 1.86, 1.92), (2.4, 0, -0.25, 1.5, 1.58),
                                (2.9, 0, -0.3, 0.85, 0.9), (3.12, 0, -0.35, 0.2, 0.22)], segs=18),                       # the hood
        V.Ball("head", "mask", (0.0, 1.05, 1.25), (1.2, 1.05, 1.05), segs=14, rings=8),                                  # the masked face
        Bx("head", "eye", (0.0, 2.0, 1.55), (1.3, 0.2, 0.25), n=3)], h, h, h)
    return P


DV.setup(
    "infantry_fremen", "FREMEN", "FREMEN", parts=parts,
    classes=dict(robe=4, robe_dark=4, mask=4),
    col=dict(robe=(0.86, 0.78, 0.63), robe_dark=(0.50, 0.42, 0.30), mask=(0.24, 0.56, 0.72)),
    spec=dict(robe=(0.0, 0.9, "paint"), robe_dark=(0.0, 0.85, "paint"), mask=(0.0, 0.5, "dark")),
    film=dict(robe=(0.78, 0.70, 0.56), robe_dark=(0.46, 0.39, 0.29)),
    glow=dict(mask=0.12), extra=dict(PLAIN=("skin", "eye", "mask")),
)
DV.finish(__name__)

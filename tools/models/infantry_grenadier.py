"""Grenadier - the light infantry reference (infantry_v1.py, infantry_derive.py) with free hands, a large dark pack
of grenades and a brown cap; throws instead of shooting.

    blender -b --factory-startup --python tools/models/infantry_grenadier.py -- --out artpacks/models-infantry-grenadier
    render_pack.py --models artpacks/models-infantry-grenadier --out artpacks/rendered-infantry-grenadier --types GRENADIER <RENDER_FLAGS>

How the original was read (DATA.R16 frames 2785-3056, 248 of 272 present: deaths 1-3 have 12 frames, death 4 has 8;
measurements only, infantry_check with D2K_INF_UNIT=GRENADIER):
  * 78 opaque px standing, half house colour (the uniform), 21 % black (large boots, dark straps), a pack as wide as
    the body and from the hips to above the shoulders in grey-brown (24 %), darker than the light infantry's tan;
    the head is dark brown from above and behind (a cap), the face a small orange patch.
  * No rifle: dark gloved fists with bright orange cuffs swing free; a dark band (goggles) across the orange face.
  * Rows 7-11 are the throw (arm back and up, forward, follow-through), without a flash; rows 17-21 the same lying.
Poses: infantry_grenadier_poses.json (the reference's legs and body; hands refitted per direction, the throw authored).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import infantry_derive as DV  # noqa: E402
from infantry_derive import K, L, Bx, V

# the throwing hand (right; chest frame) and the chest's twist per fire row 7..11
THROW = {7: ([3.0, -1.6, 5.6], -22.0), 8: ([3.3, -0.2, 7.2], -10.0), 9: ([2.2, 3.6, 5.4], 16.0), 10: ([1.6, 3.9, 1.8], 22.0),
         11: ([2.6, 1.8, -0.4], 8.0)}


def parts(P):
    P = DV.drop(P, bones=("rifle", "flash"))
    out = []
    for p in P:
        if not p.get("fx") and p["bone"] == "head" and p["mat"] in ("HOUSE_plate", "HOUSE", "pack", "belt"):
            continue                # the helmet, its rim, crest, strap and neck flap: a cap instead
        out.append(p)
    P = DV.remat(out, {"skin": "glove", "belt": "cuff"}, bones=("forearm.L", "forearm.R"))    # dark gloves, orange cuffs
    h = V.D["head"]
    P += V.stretch([L("head", "cap", "z", [(1.45, 0, -0.25, 1.5, 1.6), (1.9, 0, -0.25, 1.52, 1.62), (2.35, 0, -0.25, 1.42, 1.5),
                                           (2.72, 0, -0.25, 1.0, 1.06), (2.84, 0, -0.25, 0.3, 0.32)], segs=16),
                    Bx("head", "cap", (0.0, 1.55, 1.78), (2.0, 1.1, 0.22), n=3),                      # peak
                    Bx("head", "glove", (0.0, 1.62, 1.42), (2.0, 0.5, 0.42), n=3),                    # dark goggles
                    L("head", "cap", "z", [(0.2, 0, -0.75, 1.25, 0.95), (1.5, 0, -0.55, 1.5, 1.25)], segs=14)],   # hair / cloth at the back
                   h, h, h)
    # grenades (dark): three on the belt, one in the right hand
    tw, td = V.D["torso_w"], V.D["torso_d"]
    for x in (-1.5, 0.0, 1.5):
        P += [V.Ball("pelvis", "gren", (x * tw, 1.62 * td, 0.9), (0.52, 0.45, 0.6), segs=8, rings=6)]
    P += [V.Ball("pelvis", "gren", (-2.45 * tw, 0.2, 0.8), (0.45, 0.52, 0.6), segs=8, rings=6),
          V.Ball("forearm.R", "gren", (0.0, 3.75, 0.55), (0.55, 0.55, 0.6), segs=8, rings=6)]
    return P


def carry(row, col):
    name = V.ROW_SET[row]
    if name == "walk":
        return DV.arm_swing(row, reach=2.0, rest=(2.9, 0.4, -1.0))
    if name == "stand":
        return DV.hands([-3.0, 0.6, -1.2], [3.1, 0.9, -0.6])
    if name == "fire":
        return DV.hands([-3.0, 1.6, 0.6], THROW[row][0], elbow=25.0)
    if name == "kneel":
        return DV.hands([-3.0, 1.2, -1.0], [3.0, 1.2, -1.0])
    return {}       # lying: the reference's arms (forward, on the elbows)


def hook(row, col, spec, fit):
    if spec is None:
        return spec
    if 7 <= row <= 11:        # the throw is authored per row: the fit places the strip, not the throwing arm
        spec["pose"]["hand.R"] = {"space": "chest", "pos": list(THROW[row][0]), "elbow": 25.0}
        tw = spec["pose"]["chest"]["rot"]
        spec["pose"]["chest"]["rot"] = [THROW[row][1], tw[1], tw[2]]
    spec["pose"]["flash"] = 0
    return DV.fidget_hands(row, col, spec, fit)


DV.setup(
    "infantry_grenadier", "GRENADIER", "GRENADIER", parts=parts, carry=carry, frame_hook=hook,
    classes=dict(cap=4, cuff=2, glove=1, gren=1),
    col=dict(cap=(0.30, 0.17, 0.07), cuff=(1.0, 0.60, 0.10), glove=(0.035, 0.045, 0.07), gren=(0.06, 0.07, 0.05),
             pack=(0.60, 0.52, 0.46), pack_dark=(0.20, 0.16, 0.13), roll=(0.24, 0.19, 0.16), pouch=(0.30, 0.25, 0.2)),
    spec=dict(cap=(0.0, 0.85, "paint"), cuff=(0.0, 0.6, "dark"), glove=(0.0, 0.5, "rubber"), gren=(0.4, 0.4, "steel")),
    film=dict(cap=(0.28, 0.17, 0.09), pack=(0.52, 0.46, 0.41), pack_dark=(0.2, 0.16, 0.13), roll=(0.24, 0.2, 0.17), pouch=(0.28, 0.24, 0.2)),
    glow=dict(cuff=0.3), extra=dict(PLAIN=("skin", "eye", "cuff")),
    flash_rows=(), fit_dir=DV.FIT_DIR_HANDS,
)
DV.finish(__name__)

"""Saboteur - the light infantry reference (infantry_v1.py, infantry_derive.py) as a slim figure in a dark suit:
no weapon, no pack, arms held away from the body, a pale chest harness with a red buckle, a green light on the wrist.

    blender -b --factory-startup --python tools/models/infantry_saboteur.py -- --out artpacks/models-infantry-saboteur
    render_pack.py --models artpacks/models-infantry-saboteur --out artpacks/rendered-infantry-saboteur --types SABOTEUR <RENDER_FLAGS>

How the original was read (DATA.R16 frames 2384-2655, 240 of 272 present: death 3 has 12 frames; measurements only,
infantry_check with D2K_INF_UNIT=SABOTEUR):
  * 70 opaque px standing (55 walking), 78 % house colour in the dark shades (mean (24, 54, 86) as Atreides): suit,
    gloves, boots and a hood over the top and back of the head. The face is an orange block of 2 x 2 px; on the chest
    pale grey-white straps (14 % neutral) with a red buckle at the belt (2 px); a bright green light on the left wrist.
  * Standing, the arms are held out from the body; walking they swing.
  * Rows 7-11 / 17-21 are left-over frames of another soldier with a flame (as the engineer's): the saboteur has no
    weapon. Ours: the standing / prone pose.
  * Death 3 (column 3, 12 frames) is the charge going off: a white-orange burst in frames 1-4, pieces flung out.
    Ours: the reference's burst (death 2) with a fireball.
Poses: infantry_saboteur_poses.json (the reference's legs and body; hands refitted per direction).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import infantry_derive as DV  # noqa: E402
from infantry_derive import K, L, Bx, V  # noqa: E402

DARK = 0.68
BLAST = [0.9, 2.4, 3.4, 2.8, 1.6, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]      # fireball size per frame of death 3


def parts(P):
    P = DV.drop(P, bones=("rifle",), mats=("pack", "pack_dark", "roll", "pouch", "HOUSE_plate"))
    out = []
    for p in P:
        fx = p.get("fx")
        b, m = p["bone"], p["mat"]
        if fx:
            out.append(p)
            continue
        if b == "head" and m in ("HOUSE", "belt"):
            continue                                       # the helmet's rim, crest, strap: a hood instead
        if m == "belt":
            if b == "chest":
                continue                                   # no harness of the reference, no bedroll straps
            p = dict(p, mat="glove")
        elif m in ("boot", "sole") or (m == "skin" and b[:-2] == "forearm"):
            p = dict(p, mat="HOUSE", shade=0.3)            # boots and gloves: the suit's darkest
        elif m == "HOUSE":
            p = dict(p, shade=p.get("shade", 0.8) * DARK)
        out.append(p)
    P = out
    h = V.D["head"]
    # the hood: over the top, the back and the sides of the head, open at the face
    P += V.stretch([L("head", "HOUSE", "z", [(0.2, 0, -0.8, 1.5, 1.3), (0.9, 0, -0.8, 1.72, 1.5), (1.75, 0, -0.7, 1.78, 1.6),
                                             (2.2, 0, -0.15, 1.66, 1.8), (2.7, 0, -0.1, 1.15, 1.25), (2.95, 0, -0.1, 0.3, 0.32)],
                      segs=18, shade=DARK * 0.95)], h, h, h)
    tw, td, tz = V.D["torso_w"], V.D["torso_d"], V.D["neck_z"] / 5.55
    P += V.stretch([
        L("chest", "strap", "y", [(1.2, 3.1, 0, 1.0, 2.2, 4), (1.76, 3.1, 0, 0.95, 2.1, 4)], segs=12),         # pale chest piece
        Bx("chest", "strap", (-1.5, 0.0, 3.4), (0.5, 3.2, 3.0)),                                              # harness over the shoulders
        Bx("chest", "strap", (1.5, 0.0, 3.4), (0.5, 3.2, 3.0)),
        Bx("chest", "strap", (0.0, -1.55, 2.9), (3.0, 0.4, 2.4), n=3),                                        # and on the back
        Bx("chest", "red", (0.0, -1.6, 0.9), (1.5, 0.4, 0.95), n=3),                                          # the charge, seen from behind too
        Bx("chest", "red", (0.0, 1.62, 0.9), (1.5, 0.4, 0.95), n=3)], tw, td, tz)                             # the buckle / charge
    shz = V.B["shoulder"][2] + 0.45
    for sx in (-1.0, 1.0):          # light shoulder caps (the original's lit shoulders are the ramp's light shades)
        P += [Bx("chest", "HOUSE_plate", (sx * (V.D["sh_w"] + 0.3), 0.0, shz), (1.9, 2.2, 0.6), n=3.2, shade=1.0)]
    P += [V.Ball("forearm.L", "lamp", (0.0, 2.5, 0.55), (0.8, 0.8, 0.7), segs=8, rings=6)]                    # wrist light
    return P


def carry(row, col):
    name = V.ROW_SET[row]
    if name == "walk":
        return DV.arm_swing(row, reach=2.4, rest=(3.3, 0.4, -0.4))
    if name in ("stand", "fire"):
        return DV.hands([-4.3, 0.5, 0.4], [4.3, 0.5, 0.4], elbow=30.0)
    if name == "kneel":
        return DV.hands([-3.6, 1.2, -0.6], [3.6, 1.2, -0.6], elbow=20.0)
    return {}


def hook(row, col, spec, fit):
    if spec is None:
        return spec
    flash = 0
    if 7 <= row <= 11:
        st = V.frame_spec(0, col, fit)
        spec = dict(spec, pose=st["pose"], yaw=st["yaw"], shift=st["shift"])
    elif 17 <= row <= 21:
        st = V.frame_spec(13, col, fit)
        spec = dict(spec, pose=st["pose"], yaw=st["yaw"], shift=st["shift"])
    elif row >= 22 and col == 3:            # the charge goes off: the reference's burst and a fireball
        st = V.frame_spec(row, 2, fit)
        spec = dict(spec, pose=K.merge({}, st["pose"]), yaw=st["yaw"], shift=st["shift"])
        flash = BLAST[row - 22]
        if flash:
            spec["pose"]["rifle"] = {"space": "root", "pos": [0.0, 0.0, 7.5], "rot": [0.0, -90.0, 0.0], "hold": None}     # flame upwards
        for b in ("blood0", "blood1", "blood2"):      # no pool: the original leaves only scattered pieces
            (spec["pose"].get("fx") or {}).pop(b, None)
    spec["pose"]["flash"] = flash
    return DV.fidget_hands(row, col, spec, fit, raise_to=(2.0, 2.6, 3.0))


DV.setup(
    "infantry_saboteur", "SABOTEUR", "SABOTEUR", parts=parts, carry=carry, frame_hook=hook,
    body=dict(muzzle=(0.0, 0.0, 0.0)),
    classes=dict(glove=1, strap=4, red=5, lamp=4),
    col=dict(glove=(0.028, 0.028, 0.03), strap=(0.84, 0.92, 0.91), red=(0.72, 0.02, 0.10), lamp=(0.05, 1.0, 0.12)),
    spec=dict(glove=(0.0, 0.5, "rubber"), strap=(0.0, 0.7, "paint"), red=(0.0, 0.5, "dark"), lamp=(0.0, 0.3, "dark")),
    film=dict(strap=(0.56, 0.62, 0.6), glove=(0.06, 0.06, 0.06)),
    glow=dict(lamp=1.2, red=0.15, strap=0.1), extra=dict(PLAIN=("skin", "eye", "lamp", "red", "strap"), GROW=1.09),
    flash_rows=(), fit_dir=DV.FIT_DIR_HANDS,
)
DV.finish(__name__)

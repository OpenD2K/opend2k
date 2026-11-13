"""Thumper infantry - the light infantry reference (infantry_v1.py, infantry_derive.py) in a brown vest and cap,
carrying the thumper (a black post with a hammer head and a green light) over the shoulder; rows 7-11 are the
thumping: the man crouches at the planted device.

    blender -b --factory-startup --python tools/models/infantry_thumper.py -- --out artpacks/models-infantry-thumper
    render_pack.py --models artpacks/models-infantry-thumper --out artpacks/rendered-infantry-thumper --types THUMPER <RENDER_FLAGS>

How the original was read (DATA.R16 frames 1566-1837, 201 of 272 present; measurements only, infantry_check with
D2K_INF_UNIT=THUMPER):
  * 82 opaque px standing: 40 % house colour (sleeves, trousers), 34 % brown (a vest on the back and over the
    shoulders, a pack, the cap; mean (88, 62, 27)), 23 % black - the device: a bulky black shape from behind / above the right shoulder
    down across the body, with a bright green light in its middle. Orange face, brown boots.
  * Rows 7-11 exist in column 2 only (AnimType 8, the thumping): the man crouches facing east (12 px tall against 14.5
    standing), hands on the device planted upright 5 px in front of him - a black post 10 px tall on screen (14 px
    seen at 45 deg; ours is 11.4 px, between that and the 9 px it has when carried) with a wider head and a foot; its light is dark green in
    rows 7 / 9 / 11 and bright in 8 / 10. Nothing else moves.
  * Rows 17-21 are left-over light infantry frames (prone fire); the thumper has no weapon. Ours: the prone pose.
Poses: infantry_thumper_poses.json (the reference's legs and body; the carried device refitted per direction).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import infantry_derive as DV  # noqa: E402
from infantry_derive import K, L, Bx, V  # noqa: E402

# the device in the weapon bone's frame: the grip at 0, the foot at +y, the hammer head at -y
FOOT, HEAD = 5.2, -6.2
LAMP = (0.0, -1.3, 1.3)
# the thumping (rows 7-11): the post's place in front of the man (px), the man's crouch (px) and his shift east / north (px)
THUMP_Y, THUMP_DROP, THUMP_SHIFT, THUMP_N = 7.8, 2.2, -1.5, -1.0
LIT_ROWS = (0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 13, 14, 15, 16)      # the light is on when carried, blinks when planted


def parts(P):
    P = DV.drop(P, bones=("rifle", "flash"))
    out = []
    for p in P:
        fx = p.get("fx")
        b, m = p["bone"], p["mat"]
        if not fx and b == "head" and m in ("HOUSE_plate", "HOUSE", "pack", "belt"):
            continue                                       # a cap instead of the helmet
        if not fx and m in ("boot", "sole"):
            p = dict(p, mat="vest_dark")                   # brown boots
        out.append(p)
    P = out
    h = V.D["head"]
    tw, td, tz = V.D["torso_w"], V.D["torso_d"], V.D["neck_z"] / 5.55
    P += V.stretch([L("head", "vest_dark", "z", [(1.45, 0, -0.25, 1.5, 1.6), (1.9, 0, -0.25, 1.52, 1.62), (2.35, 0, -0.25, 1.42, 1.5),
                                                 (2.72, 0, -0.25, 1.0, 1.06), (2.84, 0, -0.25, 0.3, 0.32)], segs=16),
                    L("head", "vest_dark", "z", [(0.2, 0, -0.75, 1.25, 0.95), (1.5, 0, -0.55, 1.5, 1.25)], segs=14)], h, h, h)
    # the vest: brown, on the back and over the shoulders (the chest stays house colour, as in the original's front views)
    P += V.stretch([Bx("chest", "vest", (0.0, -1.25, 2.7), (4.6, 1.0, 4.9), n=3),
                    Bx("chest", "vest", (-1.55, 0.1, 4.75), (0.9, 3.0, 0.5), n=3),
                    Bx("chest", "vest", (1.55, 0.1, 4.75), (0.9, 3.0, 0.5), n=3)], tw, td, tz)
    # the thumper
    z = 0.95
    P += [L("rifle", "tblack", "y", [(HEAD + 2.2, 0, z, 1.15, 1.15, 4), (FOOT - 0.9, 0, z, 1.1, 1.1, 4)], segs=10),          # the post
          Bx("rifle", "tblack", (0.0, HEAD + 1.2, z), (3.4, 2.4, 3.0), n=4),                                                 # hammer head
          Bx("rifle", "steel", (0.0, HEAD + 2.55, z), (2.9, 0.35, 2.6), n=4),                                             # its collar
          L("rifle", "tblack", "y", [(FOOT - 0.9, 0, z, 1.1, 1.1, 4), (FOOT - 0.4, 0, z, 1.9, 1.7, 4), (FOOT, 0, z, 2.0, 1.8, 4)], segs=10),   # foot
          Bx("rifle", "tblack", (0.0, 0.0, z - 0.9), (0.5, 0.6, 1.0)),                                                       # carrying handles
          Bx("rifle", "tblack", (0.0, 2.9, z - 0.85), (0.5, 0.6, 0.9)),
          Bx("rifle", "steel", (0.0, 1.2, z + 0.75), (0.5, 1.6, 0.3)),
          V.Ball("rifle", "lamp_off", LAMP, (0.6, 0.6, 0.5), segs=8, rings=6),                                            # the light, off
          V.Ball("flash", "lamp", (0.0, 0.0, 0.08), (0.6, 0.6, 0.5), segs=8, rings=6, fx=True)]                                 # and lit (the flash bone)
    return P


def carry(row, col):
    """The device per direction (authored starting values; the fit refines them): carried over the right shoulder,
    the head behind and above it, the foot down in front."""
    name = V.ROW_SET[row]
    lit = {"flash": 1.0 if row in LIT_ROWS else 0.0}
    if name in ("stand", "walk", "kneel"):
        return dict(lit, rifle={"space": "chest", "pos": [2.2, 0.9, 2.6], "rot": [0.0, 42.0, 0.0], "hold": "R"})
    if name == "fire":      # planted upright in front of the man, who crouches with his hands on it
        out = DV.hands([-1.0, 4.4, 0.6], [1.0, 4.6, 1.6], elbow=20.0)
        out["rifle"] = {"space": "root", "pos": [0.4, THUMP_Y, FOOT + 0.15 - (0.35 if row in (8, 10) else 0.0)], "rot": [0.0, 90.0, 90.0], "hold": None}
        return dict(out, **lit)
    return lit


def hook(row, col, spec, fit):
    if spec is None:
        return spec
    if 7 <= row <= 11:            # the thumping: the standing man (the east frame's place), the device as authored
        st = V.frame_spec(0, col, fit)
        pose = K.merge({}, st["pose"])
        pose.update(K.merge({}, carry(row, col)))
        pz = pose["pelvis"]["pos"]
        pose["pelvis"] = {"pos": [pz[0], pz[1] - 0.6, pz[2] - THUMP_DROP], "rot": [0.0, 14.0, 0.0]}      # knees bent, leaning to it
        pose["chest"] = {"rot": [0.0, 12.0, 0.0]}
        pose["head"] = {"rot": [0.0, -10.0, 0.0]}
        spec = dict(spec, pose=pose, yaw=st["yaw"], shift=[st["shift"][0] + THUMP_SHIFT, st["shift"][1] + THUMP_N])
    elif 17 <= row <= 21:
        st = V.frame_spec(13, col, fit)
        spec = dict(spec, pose=st["pose"], yaw=st["yaw"], shift=st["shift"])
    elif row >= 22:
        spec["pose"]["flash"] = 1.0 if col in (5, 6) else 0.0
    return spec


FIT_DIR = [
    ("shift_e", "shift", 0, 0.8, -8.0, 8.0), ("shift_n", "shift", 1, 0.8, -8.0, 8.0), ("yaw", "yaw", None, 4.0, -18.0, 18.0),
    ("rifle_x", "pose", "rifle/pos/0", 0.6, -4.0, 4.0), ("rifle_y", "pose", "rifle/pos/1", 0.6, -2.0, 6.0),
    ("rifle_z", "pose", "rifle/pos/2", 0.6, -1.0, 5.2), ("rifle_yaw", "pose", "rifle/rot/0", 10.0, -200.0, 200.0),
    ("rifle_pitch", "pose", "rifle/rot/1", 8.0, -40.0, 100.0),
    ("chest_twist", "pose", "chest/rot/0", 6.0, -40.0, 40.0), ("head_yaw", "pose", "head/rot/0", 8.0, -45.0, 45.0),
]

DV.setup(
    "infantry_thumper", "THUMPER", "THUMPER", parts=parts, carry=carry, frame_hook=hook,
    body=dict(muzzle=LAMP),
    classes=dict(vest=4, vest_dark=4, tblack=1, steel=1, lamp=4, lamp_off=1),
    col=dict(tblack=(0.075, 0.075, 0.08), vest=(0.46, 0.30, 0.12), vest_dark=(0.26, 0.15, 0.06), steel=(0.34, 0.35, 0.36), lamp=(0.05, 1.0, 0.1), lamp_off=(0.02, 0.30, 0.10),
             pack=(0.44, 0.31, 0.15), pack_dark=(0.26, 0.17, 0.08), roll=(0.40, 0.29, 0.15), pouch=(0.32, 0.22, 0.10)),
    spec=dict(tblack=(0.5, 0.45, "steel"), vest=(0.0, 0.8, "paint"), vest_dark=(0.0, 0.7, "paint"), steel=(0.7, 0.35, "steel"), lamp=(0.0, 0.3, "dark"),
              lamp_off=(0.0, 0.3, "dark")),
    film=dict(tblack=(0.13, 0.125, 0.12), vest=(0.42, 0.29, 0.14), vest_dark=(0.25, 0.16, 0.08), pack=(0.4, 0.29, 0.16), pack_dark=(0.25, 0.17, 0.09),
              roll=(0.37, 0.28, 0.16), pouch=(0.3, 0.21, 0.11)),
    glow=dict(lamp=1.3), flash_rows=(), fit_dir=FIT_DIR,
)
DV.finish(__name__)

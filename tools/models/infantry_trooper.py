"""Trooper - the light infantry reference (infantry_v1.py, infantry_derive.py) with a rocket launcher.

    blender -b --factory-startup --python tools/models/infantry_trooper.py -- --out artpacks/models-infantry-trooper
    render_pack.py --models artpacks/models-infantry-trooper --out artpacks/rendered-infantry-trooper --types TROOPER <RENDER_FLAGS>

How the original was read (DATA.R16 frames 478-749, 236 of 272 present: deaths 1-4 have 8 frames; measurements only,
tools/models/infantry_check with D2K_INF_UNIT=TROOPER):
  * The same soldier, a little bigger on screen (89 opaque px standing against 74), half of it house colour; the pack is
    a darker olive-tan with a yellow-olive band.
  * The weapon is a bright blue tube (fixed colour, not house: 18-24 % of the figure; cyan highlight on its lit side,
    dark blue under it), about 10 px long and 2 px thick. Standing it is carried at the side, pointing where the
    soldier faces (upright for N / S); walking it rides on the shoulder; firing (rows 7-11) it is shouldered and aimed
    along the facing.
  * The flash is the BACKBLAST at the tube's rear end: nothing in row 7 (aiming), small in 8, full in 9 (with a
    shorter flame at the muzzle as well), fading in 10 and 11; the same in the prone fire rows 18-21 (17 = aiming).
  * The tube is the brightest thing in the sprite from every side: its material glows a little (as the reference's
    skin does), and the figure is enlarged 7 % (GROW; the reference 4 %).
Poses: infantry_trooper_poses.json (started from the light infantry's; launcher placement refitted per direction).
"""
import numpy as np

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import infantry_derive as DV  # noqa: E402
from infantry_derive import K, L, Bx, V

TUBE_Z = 1.05                 # the tube's axis above the grip
TUBE = (-4.3, 5.9)            # rear end, front end (y in the weapon's frame; the grip at 0)
# blast size per art row (1 = out of both ends of the tube)
BLAST = {8: 0.55, 9: 1.0, 10: 0.75, 11: 0.5, 18: 0.55, 19: 1.0, 20: 0.75, 21: 0.55}


def parts(P):
    P = DV.drop(P, bones=("rifle", "flash"))
    z = TUBE_Z
    r = 1.08
    P += [L("rifle", "tube", "y", [(TUBE[0], 0, z, r, r), (TUBE[1], 0, z, r, r)], segs=12),
          L("rifle", "tube_dark", "y", [(TUBE[0] - 0.25, 0, z, r + 0.2, r + 0.2), (TUBE[0] + 0.7, 0, z, r + 0.2, r + 0.2)], segs=12),   # rear venturi ring
          L("rifle", "tube_dark", "y", [(TUBE[1] - 0.8, 0, z, r + 0.16, r + 0.16), (TUBE[1] + 0.2, 0, z, r + 0.16, r + 0.16)], segs=12),  # muzzle ring
          L("rifle", "tube_dark", "y", [(0.6, 0, z, r + 0.1, r + 0.1), (1.5, 0, z, r + 0.1, r + 0.1)], segs=12),                       # clamp band
          Bx("rifle", "gun", (0.0, TUBE[1] + 0.18, z), (1.25, 0.14, 1.25), n=2),  # the bore
          Bx("rifle", "gun", (0.0, -0.05, z - 1.25), (0.5, 0.6, 1.3)),          # pistol grip
          Bx("rifle", "gun", (0.0, 2.9, z - 1.15), (0.5, 0.55, 1.1)),           # fore grip
          Bx("rifle", "gun", (0.0, 1.4, z - 0.95), (0.4, 2.6, 0.3)),            # trigger frame between the grips
          Bx("rifle", "gun", (-0.75, 1.2, z + 0.7), (0.4, 1.3, 0.55)),          # sight, on the firer's side
          Bx("rifle", "tube_dark", (0.0, -2.2, z - 0.85), (0.9, 1.4, 0.5), n=4)]  # shoulder rest
    # the blast: the flash bone sits at the tube's middle and scales about it, so a small flash stays inside the
    # tube and the full one (row 9) reaches out of both ends - the backblast behind, a shorter flame at the muzzle
    half = (TUBE[1] - TUBE[0]) / 2
    back = (K.translate([0.0, -half, 0.0]) @ K.rot_z(180.0)).tolist()
    front = K.translate([0.0, half, 0.0]).tolist()
    P += [L("flash", "flash", "y", [(0.0, 0, 0, 0.5, 0.5), (0.8, 0, 0, 1.3, 1.3), (2.2, 0, 0, 1.85, 1.85), (4.0, 0, 0, 1.35, 1.35),
                                    (5.6, 0, 0, 0.5, 0.5), (6.3, 0, 0, 0.05, 0.05)], segs=12, fx=True, tm=back),
          V.Ball("flash", "flash", (0.0, -half - 1.6, 0.0), (2.3, 0.9, 0.5), fx=True, segs=10, rings=6),
          V.Ball("flash", "flash", (0.0, -half - 1.6, 0.0), (0.5, 0.9, 2.3), fx=True, segs=10, rings=6),
          L("flash", "flash", "y", [(0.0, 0, 0, 0.5, 0.5), (0.7, 0, 0, 1.15, 1.15), (1.8, 0, 0, 1.0, 1.0), (3.2, 0, 0, 0.3, 0.3),
                                    (3.6, 0, 0, 0.04, 0.04)], segs=12, fx=True, tm=front)]
    return P


def carry(row, col):
    """The launcher per direction (authored starting values; the fit refines place and aim per direction)."""
    name = V.ROW_SET[row]
    if name == "stand":       # at the side, pointing where the soldier faces; upright for N / S
        pitch = [80.0, 35.0, 0.0, -10.0, 80.0, -10.0, 0.0, 35.0][col]
        hold = "L" if col in (0, 4, 5, 6, 7) else "R"
        sx = -1.0 if hold == "L" else 1.0
        return {"rifle": {"space": "chest", "pos": [sx * 2.6, 0.8, 0.6], "rot": [0.0, pitch, 0.0], "hold": hold}}
    if name == "walk":        # on the shoulder
        rel = [0.0, 30.0, 0.0, -45.0, 90.0, 45.0, 0.0, -30.0][col]
        hold = "L" if rel > 0 else "R"
        sx = -1.0 if hold == "L" else 1.0
        return {"rifle": {"space": "chest", "pos": [sx * 2.3, 0.6, 3.6], "rot": [rel, 4.0, 0.0], "hold": hold}}
    if name == "fire":        # shouldered, aimed along the facing
        return {"rifle": {"space": "chest", "pos": [2.3, 0.6, 3.6], "rot": [0.0, 4.0, 0.0], "hold": "R"},
                "flash": BLAST.get(row, 0.0)}
    if name == "kneel":       # going down: the tube across the shoulders
        return {"rifle": {"space": "chest", "pos": [1.0, 0.8, 3.8], "rot": [[90.0, 45.0, 0.0, -30.0, 90.0, 45.0, 0.0, -30.0][col], 0.0, 0.0],
                          "hold": "R"}}
    return {"flash": BLAST.get(row, 0.0)} if row in V.FLASH_ROWS else {}


# the fitter's per-direction parameters: the reference's with room for a weapon on the shoulder or upright
FIT_DIR = [
    ("shift_e", "shift", 0, 0.8, -8.0, 8.0), ("shift_n", "shift", 1, 0.8, -8.0, 8.0), ("yaw", "yaw", None, 4.0, -18.0, 18.0),
    ("rifle_x", "pose", "rifle/pos/0", 0.6, -4.0, 4.0), ("rifle_y", "pose", "rifle/pos/1", 0.6, -2.0, 9.0),
    ("rifle_z", "pose", "rifle/pos/2", 0.6, -2.5, 7.5), ("rifle_yaw", "pose", "rifle/rot/0", 10.0, -200.0, 200.0),
    ("rifle_pitch", "pose", "rifle/rot/1", 8.0, -35.0, 95.0),
    ("chest_twist", "pose", "chest/rot/0", 6.0, -40.0, 40.0), ("head_yaw", "pose", "head/rot/0", 8.0, -45.0, 45.0),
]

DV.setup(
    "infantry_trooper", "TROOPER", "TROOPER", parts=parts, carry=carry,
    body=dict(muzzle=(0.0, (TUBE[0] + TUBE[1]) / 2, TUBE_Z), fore=(0.0, 2.9, 0.05)),
    classes=dict(tube=6, tube_dark=6),
    col=dict(tube=(0.10, 0.62, 0.92), tube_dark=(0.03, 0.20, 0.50),
             pack=(0.50, 0.44, 0.31), pack_dark=(0.30, 0.25, 0.16), roll=(0.66, 0.58, 0.08), pouch=(0.40, 0.34, 0.22)),
    spec=dict(tube=(0.35, 0.3, "steel"), tube_dark=(0.4, 0.4, "steel")),
    film=dict(tube=(0.12, 0.42, 0.60), tube_dark=(0.05, 0.16, 0.36),
              pack=(0.44, 0.39, 0.28), pack_dark=(0.28, 0.23, 0.15), roll=(0.58, 0.51, 0.10), pouch=(0.36, 0.31, 0.21)),
    glow=dict(tube=0.3, tube_dark=0.15), extra=dict(PLAIN=("skin", "eye", "tube", "tube_dark"), GROW=1.07),
    flash_rows=(8, 9, 10, 11, 18, 19, 20, 21), fit_dir=FIT_DIR,
)
DV.finish(__name__)

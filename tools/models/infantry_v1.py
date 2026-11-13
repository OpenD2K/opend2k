"""Light infantry, v1 - the ORIGINAL soldier rebuilt as a rigged, animated model (the reference for every
infantry type: trooper, engineer, thumper, Fremen, Sardaukar, saboteur, grenadier reuse the rig, the animation
set and the pipeline; see tools/models/infantry_check/README.md).

    blender -b --factory-startup --python tools/models/infantry_v1.py -- \
        [--out DIR]            write DIR/models/unit/LIGHT_INF.glb        skeleton, skinned meshes, one animation per
                                                                          strip of the original art (63)
                                     DIR/models/unit/LIGHT_INF.anim.json  original frame -> animation, time, yaw, shift
                                     DIR/models/unit/LIGHT_INF.house.png  the uniform's shade map (with the bake)
                                     DIR/models.ini                       [LIGHT_INF] scale
                               (default artpacks/models-infantry; --model-pack / render_pack --models take DIR)
        [--no-bake]            plain colours instead of the baked atlas and shade map (fast iteration)
        [--bake-size 1024]
        [--turntable DIR]      review renders of the model in its rest pose (house colour previewed as Atreides blue)
        [--samples 64] [--device cpu|gpu]

    then the 2D pack (docs/art-pipeline.md "Animated models"; RENDER_FLAGS below):
    python tools/render_sprites/render_pack.py --models artpacks/models-infantry --out artpacks/rendered-infantry \
        --types LIGHT_INF <RENDER_FLAGS>

The upper half of this file is plain data and numpy (importable without Blender: the pose fitter and the
measuring scripts in tools/models/infantry_check/ use it); the Blender part starts at "Blender".

Everything is in ORIGINAL SPRITE PIXELS (1 px = 1/32 tile): x = the soldier's right, y = forward, z = up.

How the original was read (DATA.R16 frames 206-477, 34 rows x 8 columns; nothing is copied: the numbers are
measurements of the model - tools/models/infantry_check/):

  * The figure is 19 px tall in my first reading and 17.8 after the fit of its proportions (D), 75 opaque pixels
    per frame. Half of it is house colour - helmet crown, uniform - drawn with five shades of the 16-shade ramp
    (3, 4 | 7 | 9, 11): navy with light-blue lit sides. Not house colour: a tan pack as tall as the torso with the
    back of the head tan as well (a neck flap), a bright orange face (two rows under ONE row of helmet: the face
    is open up to the crown, the helmet sits on the top and back of the head) and hands, black boots, a black
    rifle a full pixel thick and 8 px long with a navy fore-end.
  * Animation table gInfantryAnimRanges 0x4e9530: rows 0 stand, 1-6 walk (two steps; 9 px per step at 1 px/tick,
    3 ticks per frame), 7-11 fire (hip fire, flash in 7 / 9 / 11), 12 kneel, 13 prone, 14-16 crawl, 17-21 prone
    fire, 22-33 the direction-less columns: five deaths and two idle fidgets.
  * The original is not one rigid figure turned eight ways: standing, the rifle is drawn across the screen in
    every direction; walking it is at port arms for NE / E / W / NW and level for the rest; each strip sits where
    its artist put it (the NE / E / SE walking frames 3 px east of the standing frame). So every direction gets its
    own animation ("walk.E"), with the legs and body shared and the rifle, the strip's place and yaw its own.
  * The lying frames are shorter than the standing figure laid down (14.5 px boots to helmet, about 0.8 of it):
    ours keeps its proportions and natural limbs (knees out to the side instead of folded under the body) and
    its lying strips are drawn at LYING_SCALE = 0.87 of the standing size (frame map "scale").

Poses are descriptions (infantry_kit.py), fitted to the original frames by infantry_check/fit.py and kept in
infantry_v1_poses.json; the lying strips, the deaths and the fidgets are authored below and only placed by the fit.

Look: the uniform is the plain house mask, so its dirt, seams and cloth grain are a grey shade map
(LIGHT_INF.house.png: darker and lighter house colour through the game's ramp); the gear is baked into one atlas
with the tank kit's weathering; face and hands carry a little emission (the original's are bright whichever way
they face). The pack is rendered with a dim high sun for the short ground shadow, a strong shadowless side light
from the west-north-west and a tone curve for the uniform (RENDER_FLAGS).
"""
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import infantry_kit as K  # noqa: E402

KEY = "LIGHT_INF"
UNIT = "LIGHT INF"
POSES_FILE = os.path.join(HERE, "infantry_v1_poses.json")
ANCHOR_PX = (-1, -2)     # the hotspot pixel's corner relative to the model origin's projection (px): the frames'
                         # hotspot sits on the body axis 2.8 px above the ground, the axis 1 px right of it

# ------------------------------------------------------------------------------------------------
# Body (px). Read off the original frames with the 45 deg camera: first by hand from the eight standing frames,
# then the proportions in D fitted with the poses (tools/models/infantry_check/fit.py --dims): the original is a
# stocky figure - a large head, wide shoulders, short thick legs, big boots - under a pack as tall as its torso.

D = dict(
    head=1.0,                    # size of the head (skull, helmet, neck flap)
    torso_w=1.0, torso_d=1.0,    # width / depth of chest and hips
    neck_z=5.55,                 # the neck's base above the waist (torso length)
    sh_w=2.4,                    # shoulder joints: half their distance
    pack_w=1.0, pack_d=1.0, pack_h=1.0,
    arm=1.0, leg=1.0,            # limb thickness
    boot=1.0,                    # boot size
    hip_z=9.6, hip_w=1.2,        # hip joints: height in the neutral stand, half their distance
)


# extra bones under the root for the deaths: blood pools, body pieces (the burst), blood drops
FX_BONES = ("blood0", "blood1", "blood2", "gib0", "gib1", "gib2", "gib3", "gib4", "gib5",
            "drop0", "drop1", "drop2", "drop3", "drop4", "drop5", "drop6", "drop7")


def body_dims(D):
    k = (D["hip_z"] - 1.25 * D["boot"]) / (9.6 - 1.25)          # leg length follows the hip height
    return dict(
        hip_z=D["hip_z"], hip_w=D["hip_w"], thigh=4.5 * k, shin=4.1 * k, ankle_z=1.25 * D["boot"], stance=D["hip_w"] + 0.3,
        waist=1.2, neck=(0.0, 0.15, D["neck_z"]), shoulder=(D["sh_w"], 0.0, 4.5 * D["neck_z"] / 5.55), upper_arm=3.3, forearm=3.0,
        grip=(0.0, 0.0, 0.0), fore=(0.0, 2.9, 0.05), muzzle=(0.0, 5.95, 0.2),
        fx=FX_BONES,
    )


B = body_dims(D)
SKEL = K.Skeleton(B)
_cloud = None
GROW = 1.04      # the finished model's scale about its ground point (written to the model pack's models.ini): box-filtered
                 # to the original's pixels a render loses the half-covered rim that pixel art draws as full pixels

# material -> pixel class of the measuring scripts (infantry_check/classes.py): 0 house, 1 black, 2 skin,
# 3 flash, 4 pack, 5 blood, 6 navy
CLASSES = dict(HOUSE=0, HOUSE_plate=0, boot=1, sole=1, gun=1, skin=2, eye=2, flash=3,
               pack=4, pack_dark=4, roll=4, belt=4, pouch=4, blood=5, blood_dark=5, gun_blue=6)


def L(bone, mat, axis, rings, segs=10, **kw):
    return dict(bone=bone, mat=mat, axis=axis, loft=rings, segs=segs, **kw)


def Bx(bone, mat, c, s, **kw):
    return dict(bone=bone, mat=mat, box=(c, s), **kw)


def Ball(bone, mat, c, r, **kw):
    return dict(bone=bone, mat=mat, ball=(c, r), **kw)


def limb_pair(fn):
    """Parts for both sides: fn(side, sx) -> list."""
    return fn("L", -1.0) + fn("R", 1.0)


def stretch(parts_, sx=1.0, sy=1.0, sz=1.0, about=(0.0, 0.0, 0.0), move=(0.0, 0.0, 0.0)):
    """The parts scaled by (sx, sy, sz) about a point of their bone's frame, then moved (a proportion of D)."""
    m = K.translate(np.add(about, move)) @ np.diag([sx, sy, sz, 1.0]) @ K.translate(np.negative(about))
    out = []
    for p in parts_:
        p = dict(p)
        p["tm"] = (m @ np.asarray(p["tm"], float)).tolist() if "tm" in p else m.tolist()
        out.append(p)
    return out


def parts():
    """The soldier, part by part, each in its bone's frame (kit docstring); the numbers are the first reading,
    D stretches them to the fitted proportions."""
    P = []
    tw, td, tz = D["torso_w"], D["torso_d"], D["neck_z"] / 5.55
    # ---- pelvis (origin between the hip joints)
    P += stretch([L("pelvis", "HOUSE", "z", [(-1.15, 0, 0, 1.95, 1.25, 3), (0.0, 0, 0, 2.3, 1.45, 3), (1.25, 0, 0, 2.2, 1.4, 3)], shade=0.8),
                  L("pelvis", "belt", "z", [(0.85, 0, 0, 2.38, 1.52, 3), (1.5, 0, 0, 2.38, 1.52, 3)]),
                  Bx("pelvis", "pouch", (-1.45, 1.5, 0.55), (0.95, 0.55, 1.0), n=4),
                  Bx("pelvis", "pouch", (1.45, 1.5, 0.55), (0.95, 0.55, 1.0), n=4),
                  Bx("pelvis", "pouch", (2.3, 0.1, 0.4), (0.55, 1.1, 1.1), n=4)], tw, td, 1.0)
    # ---- chest (origin at the waist; shoulders at z 4.5)
    P += stretch([L("chest", "HOUSE", "z", [(0.0, 0, 0, 2.15, 1.35, 3), (1.5, 0, 0.05, 2.3, 1.45, 3), (3.2, 0, 0.1, 2.6, 1.55, 3),
                                            (4.55, 0, 0.05, 2.75, 1.45, 3), (5.25, 0, 0, 1.9, 1.15, 2.5), (5.75, 0, 0, 0.95, 0.85)], shade=0.8),
                  # the armour vest: a breast plate and a belly plate (lighter than the cloth), a dark gap between them
                  L("chest", "HOUSE_plate", "y", [(1.15, 3.55, 0, 1.0, 1.85, 4), (1.78, 3.55, 0, 0.95, 1.78, 4)], segs=12),
                  L("chest", "HOUSE_plate", "y", [(1.1, 1.55, 0, 0.75, 1.6, 4), (1.66, 1.55, 0, 0.7, 1.52, 4)], segs=12, shade=0.92),
                  L("chest", "HOUSE", "z", [(5.1, 0, 0.05, 1.32, 1.22), (5.8, 0, 0.05, 1.2, 1.1)], shade=0.65),      # collar
                  # harness straps over the shoulders, a pouch on the left of the chest
                  Bx("chest", "belt", (-1.5, 1.72, 3.1), (0.4, 0.3, 3.6)),
                  Bx("chest", "belt", (1.5, 1.72, 3.1), (0.4, 0.3, 3.6)),
                  Bx("chest", "pouch", (-1.05, 1.95, 2.3), (0.9, 0.5, 0.95), n=4)], tw, td, tz)
    shz = B["shoulder"][2] + 0.3
    for sx in (-1.0, 1.0):       # shoulder plates: rounded slabs sloping outwards over the arms
        tm = K.translate([sx * (D["sh_w"] + 0.25), 0.0, shz]) @ K.rot_y(sx * 28.0) @ np.diag([D["arm"], D["arm"], 1.0, 1.0])
        P += [Bx("chest", "HOUSE_plate", (0.0, 0.0, 0.0), (2.1, 2.5, 0.75), n=3.2, tm=tm.tolist()),
              Bx("chest", "HOUSE", (0.0, 0.0, -0.42), (2.3, 2.7, 0.3), n=3.2, tm=tm.tolist(), shade=0.6)]
    # the pack: tan, from the hips to the shoulders, a bedroll across its top; it hangs on the back (y -1.42 td)
    pack = [L("chest", "pack", "z", [(-1.35, 0, -2.95, 2.15, 1.45, 5), (-0.6, 0, -3.0, 2.5, 1.58, 5), (4.6, 0, -3.0, 2.5, 1.58, 5),
                                     (5.35, 0, -2.9, 2.25, 1.4, 5)], segs=16),
            L("chest", "roll", "x", [(-2.75, -2.85, 6.05, 0.95, 0.95), (2.75, -2.85, 6.05, 0.95, 0.95)], segs=12),
            Bx("chest", "pack_dark", (0.0, -4.62, 3.7), (4.3, 0.22, 2.5)),          # flap
            Bx("chest", "pack_dark", (-1.3, -4.66, 1.2), (0.4, 0.2, 2.6)),          # straps
            Bx("chest", "pack_dark", (1.3, -4.66, 1.2), (0.4, 0.2, 2.6)),
            Bx("chest", "pack_dark", (-2.7, -3.0, 1.0), (0.6, 1.8, 2.4), n=4),      # side pockets
            Bx("chest", "pack_dark", (2.7, -3.0, 1.0), (0.6, 1.8, 2.4), n=4),
            L("chest", "belt", "x", [(-2.15, -2.85, 6.05, 1.02, 1.02), (-1.8, -2.85, 6.05, 1.02, 1.02)], segs=12),   # bedroll straps
            L("chest", "belt", "x", [(1.8, -2.85, 6.05, 1.02, 1.02), (2.15, -2.85, 6.05, 1.02, 1.02)], segs=12)]
    P += stretch(pack, D["pack_w"], D["pack_d"], D["pack_h"], about=(0.0, -1.42, -1.35), move=(0.0, -1.42 * (td - 1.0), 0.0))
    # ---- head (origin at the neck's base; the skull's centre at z 1.35). From the front and above the original
    # shows two rows of orange under ONE row of house colour: a bare face, open up to the crown; the helmet sits
    # on the top and back of the head (its rim tilted back), a tan cloth flap hangs from its rear rim.
    tilt = (K.translate([0.0, 0.0, 1.35]) @ K.rot_x(40.0) @ K.translate([0.0, 0.0, -1.35])).tolist()
    P += stretch([Ball("head", "skin", (0.0, 0.48, 1.42), (1.27, 1.38, 1.36), segs=18, rings=10),   # a bare face (owner): skull,
                  Ball("head", "skin", (0.0, 0.74, 0.66), (1.02, 1.04, 0.86), segs=14, rings=8),     # jaw,
                  Ball("head", "skin", (0.0, 1.8, 1.1), (0.2, 0.3, 0.34)),                           # nose,
                  Bx("head", "skin", (0.0, 1.62, 1.66), (1.75, 0.42, 0.22), n=3),                    # brow,
                  Ball("head", "skin", (-1.27, 0.45, 1.22), (0.17, 0.3, 0.4)),                       # ears
                  Ball("head", "skin", (1.27, 0.45, 1.22), (0.17, 0.3, 0.4)),
                  Bx("head", "eye", (-0.5, 1.72, 1.43), (0.36, 0.16, 0.16), n=3),                    # eyes in the brow's shadow
                  Bx("head", "eye", (0.5, 1.72, 1.43), (0.36, 0.16, 0.16), n=3),
                  L("head", "skin", "z", [(-0.35, 0, 0.15, 0.72, 0.72), (0.4, 0, 0.2, 0.78, 0.8)]),  # neck
                  L("head", "HOUSE_plate", "z", [(1.5, 0, -0.3, 1.6, 1.68), (1.8, 0, -0.3, 1.6, 1.68), (2.1, 0, -0.3, 1.52, 1.6),
                                                 (2.38, 0, -0.3, 1.32, 1.4), (2.6, 0, -0.3, 1.02, 1.08), (2.76, 0, -0.3, 0.62, 0.66),
                                                 (2.82, 0, -0.3, 0.2, 0.22)], segs=18, tm=tilt),
                  L("head", "HOUSE", "z", [(1.38, 0, -0.3, 1.7, 1.8), (1.56, 0, -0.3, 1.7, 1.8)], segs=18, shade=0.62, tm=tilt),     # rim
                  L("head", "HOUSE", "y", [(-1.15, 2.6, 0, 0.26, 0.22, 3), (0.7, 2.6, 0, 0.26, 0.22, 3)], segs=8, shade=0.7, tm=tilt),   # crest
                  Bx("head", "belt", (-1.24, 0.5, 0.95), (0.16, 0.3, 1.5)),                          # chin strap
                  Bx("head", "belt", (1.24, 0.5, 0.95), (0.16, 0.3, 1.5)),
                  # the neck flap: tan cloth from the helmet's rear rim to the shoulders
                  L("head", "pack", "z", [(-0.6, 0, -1.3, 1.7, 0.5, 3), (0.1, 0, -1.55, 1.65, 0.5, 3), (0.65, 0, -1.8, 1.55, 0.45, 3)], segs=12)],
                 D["head"], D["head"], D["head"])

    def arm(side, sx):
        return stretch([L("upper_arm." + side, "HOUSE", "y", [(-0.35, 0, 0, 0.88, 0.88), (1.2, 0, 0, 0.88, 0.86), (2.5, 0, 0, 0.78, 0.76),
                                                              (3.3, 0, 0, 0.72, 0.72)], shade=0.8),
                        L("forearm." + side, "HOUSE", "y", [(-0.25, 0, 0, 0.74, 0.74), (0.7, 0, 0, 0.78, 0.76), (2.0, 0, 0, 0.68, 0.66),
                                                            (2.75, 0, 0, 0.6, 0.6)], shade=0.8),
                        L("forearm." + side, "HOUSE_plate", "y", [(0.95, 0.12, 0, 0.74, 0.8, 3), (2.25, 0.1, 0, 0.68, 0.74, 3)]),      # arm guard
                        L("forearm." + side, "belt", "y", [(2.55, 0, 0, 0.66, 0.66), (2.8, 0, 0, 0.66, 0.66)]),                        # cuff
                        Ball("forearm." + side, "skin", (0.0, 3.35, 0.0), (0.8, 0.95, 0.8), segs=12, rings=8)], D["arm"], 1.0, D["arm"])

    def leg(side, sx):
        ky = B["thigh"] / 4.5
        boot = stretch([L("foot." + side, "boot", "y", [(-1.2, -0.45, 0, 0.78, 0.8), (0.0, -0.4, 0, 0.82, 0.88), (1.3, -0.62, 0, 0.6, 0.85),
                                                        (2.15, -0.8, 0, 0.38, 0.6)]),
                        Bx("foot." + side, "sole", (0.0, 0.45, -1.13), (1.8, 3.3, 0.24), n=4)], D["boot"], D["boot"], D["boot"])
        return stretch([L("thigh." + side, "HOUSE", "y", [(-0.45, 0, 0, 1.12, 1.15), (1.2, 0.05, 0, 1.16, 1.15), (2.8, 0, 0, 1.04, 1.02),
                                                          (4.5, 0, 0, 0.9, 0.9)], shade=0.8),
                        Bx("thigh." + side, "HOUSE", (sx * 1.0, 2.0, 0.0), (0.5, 1.5, 1.25), shade=0.65),     # thigh pocket, outer side
                        L("shin." + side, "HOUSE", "y", [(-0.3, 0, 0, 0.95, 0.95), (0.9, -0.05, 0, 0.98, 0.95), (2.0, 0, 0, 0.9, 0.88),
                                                         (2.65, 0, 0, 1.0, 1.0), (2.95, 0, 0, 0.82, 0.82)], shade=0.8),   # bloused over the boot
                        L("shin." + side, "HOUSE_plate", "y", [(-0.55, 0.42, 0, 0.62, 0.8, 3), (0.75, 0.4, 0, 0.56, 0.74, 3)]),          # knee pad
                        L("shin." + side, "boot", "y", [(2.7, 0, 0, 0.88, 0.88), (4.2, 0, 0, 0.9, 0.9)])],  # boot shaft
                       D["leg"], ky, D["leg"]) + boot

    P += limb_pair(arm) + limb_pair(leg)
    # ---- rifle (origin at the grip, +y = the muzzle): 8.6 px from butt to muzzle
    rifle = [L("rifle", "gun", "y", [(-2.7, -0.45, 0, 0.6, 0.3, 4), (-0.7, -0.05, 0, 0.42, 0.3, 4), (-0.4, 0.05, 0, 0.4, 0.3, 4)]),   # stock
             Bx("rifle", "gun", (0.0, 0.9, 0.12), (0.62, 2.7, 0.92)),            # receiver
             Bx("rifle", "gun", (0.0, -0.05, -0.6), (0.46, 0.55, 0.9)),          # pistol grip
             Bx("rifle", "gun", (0.0, 1.35, -0.8), (0.46, 0.7, 1.05)),           # magazine
             L("rifle", "gun_blue", "y", [(2.25, 0.12, 0, 0.42, 0.42, 3), (4.9, 0.14, 0, 0.38, 0.38, 3)]),      # shroud
             L("rifle", "gun", "y", [(4.9, 0.2, 0, 0.2, 0.2), (5.95, 0.2, 0, 0.2, 0.2)], segs=8),               # barrel
             Bx("rifle", "gun", (0.0, 0.7, 0.72), (0.3, 1.1, 0.3))]              # sight rail
    P += stretch(rifle, 1.45, 1.0, 1.3)        # a full pixel thick, as the original draws it
    # ---- muzzle flash (fx: hidden unless the pose shows it)
    P += [L("flash", "flash", "y", [(0.0, 0, 0, 0.25, 0.25), (0.7, 0, 0, 1.0, 1.0), (1.9, 0, 0, 1.45, 1.45), (3.4, 0, 0, 1.0, 1.0),
                                    (4.7, 0, 0, 0.3, 0.3), (5.3, 0, 0, 0.04, 0.04)], segs=12, fx=True),            # the tongue
          Ball("flash", "flash", (0.0, 1.2, 0.0), (2.35, 0.75, 0.42), fx=True, segs=10, rings=6),               # side jets (a cross)
          Ball("flash", "flash", (0.0, 1.2, 0.0), (0.42, 0.75, 2.35), fx=True, segs=10, rings=6)]
    # ---- blood pools: a wet red dome on a darker, wider stain; 3 px radius at scale 1 (the poses scale them)
    for i, seed in enumerate((11, 23, 37)):
        P += [dict(bone="blood%d" % i, mat="blood_dark", splat=(3.3, 0.04), seed=seed, wobble=0.42, fx=True),
              dict(bone="blood%d" % i, mat="blood", splat=(2.7, 0.07), seed=seed + 1, wobble=0.36, c=(0.0, 0.0, 0.025), fx=True)]
    # ---- blood drops: stretched along their flight (+y of the bone)
    for i in range(8):
        P += [Ball("drop%d" % i, "blood", (0.0, 0.0, 0.0), (0.3, 0.55, 0.3), fx=True, segs=6, rings=4)]
    # ---- body pieces (death 2: the body bursts): torso, head, arm, two legs, a piece of the pack
    P += [L("gib0", "HOUSE", "z", [(-1.3, 0, 0, 1.9, 1.2, 3), (0.2, 0.2, 0, 2.2, 1.35, 3), (1.3, -0.1, 0.1, 1.6, 1.1, 3)], shade=0.75, fx=True),
          Ball("gib0", "blood", (0.3, 0.2, 1.25), (1.1, 0.8, 0.35), fx=True),
          L("gib1", "HOUSE_plate", "z", [(-0.5, 0, 0, 1.5, 1.58), (0.05, 0, 0, 1.45, 1.52), (0.5, 0, 0, 1.1, 1.2), (0.78, 0, 0, 0.5, 0.55)],
            segs=12, fx=True),                                                             # the helmet
          L("gib1", "HOUSE", "z", [(-0.62, 0, 0, 1.6, 1.68), (-0.45, 0, 0, 1.6, 1.68)], segs=12, shade=0.6, fx=True),
          L("gib2", "HOUSE", "y", [(-1.6, 0, 0, 0.8, 0.8), (0.6, 0, 0, 0.72, 0.72), (1.5, 0, 0, 0.6, 0.6)], shade=0.8, fx=True),
          Ball("gib2", "skin", (0.0, 1.95, 0.0), (0.6, 0.72, 0.6), fx=True),
          Ball("gib2", "blood", (0.0, -1.6, 0.0), (0.7, 0.4, 0.7), fx=True),
          L("gib5", "pack", "z", [(-1.0, 0, 0, 1.5, 1.1, 5), (0.9, 0.1, 0, 1.6, 1.2, 5)], segs=12, fx=True)]
    for b in ("gib3", "gib4"):
        P += [L(b, "HOUSE", "y", [(-2.3, 0, 0, 1.0, 1.0), (0.2, 0, 0, 0.9, 0.9), (1.2, 0, 0, 0.8, 0.8)], shade=0.8, fx=True),
              L(b, "boot", "y", [(1.0, 0, 0, 0.85, 0.85), (2.0, 0.1, 0, 0.9, 0.95), (3.3, 0.45, 0, 0.6, 0.85)], fx=True),
              Ball(b, "blood", (0.0, -2.3, 0.0), (0.85, 0.4, 0.85), fx=True)]
    return P


def set_dims(d=None):
    """Change proportions (entries of D): rebuilds the skeleton and drops the cached preview cloud."""
    global B, SKEL, _cloud
    D.update(d or {})
    B = body_dims(D)
    SKEL = K.Skeleton(B)
    _cloud = None


# ------------------------------------------------------------------------------------------------
# Animation set. The original's art rows (frame = first + row * 8 + column): one strip per AnimType.

DIRS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
SETS = [  # name, rows, per-direction
    ("stand", [0], True), ("walk", [1, 2, 3, 4, 5, 6], True), ("fire", [7, 8, 9, 10, 11], True), ("kneel", [12], True),
    ("prone", [13], True), ("crawl", [14, 15, 16], True), ("pronefire", [17, 18, 19, 20, 21], True),
]
DEATHS = [("death%d" % k, list(range(22, 34)), k) for k in range(5)] + \
         [("fidget0", list(range(22, 30)), 5), ("fidget1", list(range(22, 30)), 6)]
ROW_SET = {r: name for name, rows, _ in SETS for r in rows}
FLASH_ROWS = (7, 9, 11, 17, 19, 21)
LYING = ("prone", "crawl", "pronefire")
LYING_SCALE = 0.87       # the lying strips are drawn at this size (frame map "scale"; 1 = off): the original's lying
                         # figure is 14.5 px from boots to helmet, about 0.8 of the same figure standing - ours keeps
                         # natural limbs and is brought most of the way by scale instead of folding its legs under
FLASH_SIZE = [1.0, 1.0, 1.0, 1.25, 1.7, 1.25, 1.0, 1.0]      # muzzle flash size per direction (N .. NW)


def walk_pose(k):
    """Walk cycle, frame k = 0..5 (rows 1..6): two steps; strides in frames 0 and 3. The planted foot moves back
    3 px per frame under the body (1 px / tick, 3 ticks per frame)."""
    hip = B["hip_z"]
    #        planted foot y, swing foot y, swing lift, swing pitch, pelvis drop
    table = [(4.3, -4.4, 1.9, 55.0, 0.9), (1.4, -1.6, 2.1, 30.0, 0.3), (-1.6, 2.4, 1.0, -10.0, 0.4)]
    py, sy, lift, pitch, drop = table[k % 3]
    plant, swing = ("R", "L") if k < 3 else ("L", "R")
    sx = {"L": -1.0, "R": 1.0}
    return {
        "pelvis": {"pos": [0.0, 0.0, hip - drop], "rot": [0.0, 12.0, 0.0]},
        "chest": {"rot": [0.0, 6.0, 0.0]},
        "head": {"rot": [0.0, -12.0, 0.0]},
        "foot." + plant: {"pos": [sx[plant] * 1.1, py, B["ankle_z"]], "rot": [0.0, 0.0, 0.0]},
        "foot." + swing: {"pos": [sx[swing] * 1.1, sy, B["ankle_z"] + lift], "rot": [0.0, pitch, 0.0]},
    }


def base_pose(row, par=None):
    """The pose every direction shares (legs and body), by art row. par: authoring parameters the fit may set for
    the row (the lying strips: how far each knee is drawn up, how far the body has slid forward)."""
    hip = B["hip_z"]
    if row == 0:
        return {}
    if 1 <= row <= 6:
        return walk_pose(row - 1)
    if 7 <= row <= 11:        # firing from the hip: wide stance, crouched; rows 8, 10 between shots (rifle lower)
        return {
            "pelvis": {"pos": [0.0, -0.2, hip - 1.5], "rot": [0.0, 8.0, 0.0]},
            "chest": {"rot": [0.0, 6.0, 0.0]}, "head": {"rot": [0.0, -8.0, 0.0]},
            "foot.L": {"pos": [-1.9, 2.0, B["ankle_z"]], "rot": [-10.0, 0.0, 0.0]},
            "foot.R": {"pos": [1.9, -2.2, B["ankle_z"]], "rot": [25.0, 0.0, 0.0]},
            "flash": 1 if row in FLASH_ROWS else 0,
        }
    if row == 12:             # kneeling on the right knee
        return {
            "pelvis": {"pos": [0.0, -0.3, 5.6], "rot": [0.0, 12.0, 0.0]},
            "chest": {"rot": [0.0, 10.0, 0.0]}, "head": {"rot": [0.0, -12.0, 0.0]},
            "foot.L": {"pos": [-1.5, 2.6, B["ankle_z"]], "rot": [0.0, 0.0, 0.0]},
            "foot.R": {"pos": [1.5, -3.6, 0.9], "rot": [0.0, 80.0, 0.0], "pole": [0.15, 1.0, -0.3]},
        }
    return lying_pose(row, par or {})


def lying_pose(row, par):
    """Rows 13 (prone), 14-16 (crawl), 17-21 (prone fire). The body is low and level, the legs behind it with the
    knees out to the side (never under the body), both hands on the rifle, which is carried with the shoulders - so
    it moves with them in every frame - and stays level; elbows down beside the chest.
    Crawl (three frames, looping): 14 the right knee drawn up, hips and shoulders turned to it; 15 stretched out,
    pushed forward, the rifle reached out; 16 the left knee drawn up.
    par (fitted per row): drawn.L / drawn.R = how far each knee is drawn up beside the body (0 straight back .. 1),
    slide = the body moved along its axis."""
    crawl = row in (14, 15, 16)
    dl, dr = {13: (0.3, 0.8), 14: (0.6, 1.0), 15: (0.1, 0.2), 16: (1.0, 0.6)}.get(row, (0.4, 0.45))
    dl, dr = float(par.get("drawn.L", dl)), float(par.get("drawn.R", dr))
    # what the animation is, the fit may not undo: the crawl gathers both knees (one further) and stretches out in
    # turn (the original's frames are 12 px long gathered, 19 px stretched); at rest one knee is cocked; firing,
    # the legs are spread behind the body
    lim = {13: ((0.0, 0.35), (0.5, 0.9)), 14: ((0.4, 0.75), (0.8, 1.0)), 15: ((0.0, 0.25), (0.0, 0.25)),
           16: ((0.8, 1.0), (0.4, 0.75))}.get(row, ((0.2, 0.5), (0.2, 0.5)))
    dl, dr = min(max(dl, lim[0][0]), lim[0][1]), min(max(dr, lim[1][0]), lim[1][1])
    slide = float(par.get("slide", 1.0 if row == 15 else 0.0))
    yp, hz = -4.5 + slide, 1.35               # the pelvis: behind the origin, belly on the ground

    def leg(sx, d):
        return {"pos": [sx * (1.5 + 1.6 * d), yp - 8.0 + 4.6 * d, 0.75], "rot": [sx * (22.0 + 45.0 * d), 96.0, 0.0],
                "pole": [sx * 1.0, 0.8 * d, -0.1]}

    def rifle(lean, dx, dy, dz, yaw=0.0, fore=2.0):
        """The grip `dx` right of, `dy` ahead of and `dz` above the right shoulder (root axes), for a chest leaning
        forward by `lean` degrees in all; the barrel level, turned by yaw."""
        a = math.radians(lean)
        return {"space": "shoulder", "hold": "R", "rot": [yaw, 0.0, 0.0], "fore": [0.0, fore, 0.05],
                "pos": [B["shoulder"][0] + dx, dy * math.cos(a) - dz * math.sin(a), B["shoulder"][2] + dy * math.sin(a) + dz * math.cos(a)]}

    bend = -14.0 if crawl else -20.0          # the chest raised on the elbows
    pose = {
        "p": {"drawn.L": dl, "drawn.R": dr, "slide": slide},
        "pelvis": {"pos": [0.0, yp, hz], "rot": [0.0, 88.0, 0.0]},
        "chest": {"rot": [0.0, bend, 0.0]}, "head": {"rot": [0.0, -60.0 if crawl else -56.0, 0.0]},
        "foot.L": leg(-1.0, dl), "foot.R": leg(1.0, dr),
        "hand.L": {"pole": [-1.0, 0.2, -1.0]}, "hand.R": {"pole": [1.0, -0.1, -1.0]},       # elbows down and out
        "rifle": rifle(88.0 + bend, -0.6, 2.1, 0.15, -3.0, 1.9),                              # the butt in the right shoulder
        "flash": 1 if row in FLASH_ROWS else 0,
    }
    if row == 13:             # prone, at rest: the rifle lowered
        pose["rifle"] = rifle(88.0 + bend, -0.8, 2.2, -0.6, -8.0, 2.3)
    elif row == 15:           # crawl, stretched: the rifle reached out
        pose["rifle"] = rifle(88.0 + bend, -1.0, 2.9, -0.9, -6.0, 2.3)
    elif crawl:               # crawl, gathered: the hips up a little and turned towards the knee drawn furthest
        sx = 1.0 if row == 14 else -1.0
        pose["pelvis"]["pos"] = [0.0, yp, hz + 0.55]
        pose["pelvis"]["rot"] = [-sx * 8.0, 82.0, sx * 5.0]
        pose["chest"]["rot"] = [sx * 6.0, bend, 0.0]
        pose["rifle"] = rifle(88.0 + bend, -1.1 + 0.2 * sx, 2.5 + 0.2 * sx, -1.0, -19.0 + 7.0 * sx, 2.4)
    elif row in (18, 20):     # between shots: the rifle rocks back a little, nothing else moves
        pose["rifle"] = rifle(88.0 + bend, -0.6, 1.85, 0.1, -3.0, 1.9)
    return pose


def carry(row, col):
    """The rifle per direction. The original draws it across the screen rather than along the facing: standing it
    is horizontal in every direction (pointing east for NE / E / SE, west for the rest); walking it is at port arms
    for NE / E / W / NW and horizontal for N / SE / S / SW; only the firing rows aim where the soldier faces.
    (Authored starting values; the fit refines place and aim per direction and picks the hand.)"""
    name = ROW_SET[row]
    if name in ("stand", "walk"):
        rel = [-90.0, 45.0, 0.0, -45.0, 90.0, 45.0, 0.0, -45.0][col]      # rifle yaw relative to the body, clockwise
        hold = "L" if rel > 0 else "R"
        sx = -1.0 if hold == "L" else 1.0
        if name == "walk" and col in (1, 2, 6, 7):                          # port arms: muzzle up and forward
            return {"rifle": {"space": "chest", "pos": [sx * 0.6, 1.6, 1.2], "rot": [0.0, 45.0, 0.0], "hold": hold}}
        if abs(rel) > 60:                                                   # across the chest
            return {"rifle": {"space": "chest", "pos": [sx * 0.4, 1.75, 2.1], "rot": [rel, 0.0, 0.0], "hold": hold}}
        if abs(rel) > 20:
            return {"rifle": {"space": "chest", "pos": [sx * 1.0, 1.5, 2.0], "rot": [rel, 0.0, 0.0], "hold": hold}}
        return {"rifle": {"space": "chest", "pos": [sx * 1.2, 1.4, 1.9], "rot": [rel, 0.0, 0.0], "hold": hold}}
    if name == "fire":
        out = {"rifle": {"space": "chest", "pos": [1.3, 1.3, -0.3 if row in FLASH_ROWS else -1.1], "rot": [0.0, -6.0, 0.0], "hold": "R"}}
        if row in FLASH_ROWS:      # the original paints a larger flash when the soldier fires towards the camera
            out["flash"] = FLASH_SIZE[col]
        return out
    if name == "kneel":
        return {"rifle": {"space": "chest", "pos": [1.2, 1.6, 1.6], "rot": [0.0, 10.0, 0.0], "hold": "R"}}
    # lying: the rifle is part of the shared pose (it moves with the shoulders); only the flash differs
    return {"flash": FLASH_SIZE[col]} if row in FLASH_ROWS else {}


# ---- deaths and idle fidgets (the direction-less columns of rows 22..33). Positions are authored as seen on the
# screen (east, north, up in px from the model origin) and turned into the model's frame for the death's yaw, so
# the whole motion - flight, fall, where the rifle and the blood land - is in the animation itself.

DEATH_YAW = dict(death0=100.0, death1=270.0, death2=180.0, death3=180.0, death4=90.0, fidget0=180.0, fidget1=180.0)
FADE = {8: 0.85, 9: 0.62, 10: 0.38, 11: 0.16}           # rows 30..33: the remains dissolve


def w2l(yaw, e, n):
    """World (east, north) px -> the model's (x right, y forward) when it is turned clockwise by yaw."""
    a = math.radians(yaw)
    return [e * math.cos(a) - n * math.sin(a), e * math.sin(a) + n * math.cos(a)]


def scr(yaw, sx, sy, z=0.0):
    """Model-frame position of the point seen at screen (sx, sy) px from the hotspot pixel's corner, z above ground."""
    e = sx + ANCHOR_PX[0]
    n = -(sy + ANCHOR_PX[1]) / K.S45 - z
    x, y = w2l(yaw, e, n)
    return [x, y, z]


def limbs(kind):
    """Limb sets for a body that is not standing: targets relative to chest / pelvis."""
    if kind == "flail":      # in the air: arms flung up and out, legs trailing
        return {"hand.L": {"space": "chest", "pos": [-3.9, 0.9, 6.0], "elbow": 30.0},
                "hand.R": {"space": "chest", "pos": [3.6, 1.6, 5.2], "elbow": 30.0},
                "foot.L": {"space": "pelvis", "pos": [-1.7, 1.3, -7.6], "rot": [0.0, 35.0, 0.0]},
                "foot.R": {"space": "pelvis", "pos": [1.9, -1.6, -7.3], "rot": [0.0, 60.0, 0.0]}}
    if kind == "sprawl":     # lying: arms out, legs apart
        return {"hand.L": {"space": "chest", "pos": [-4.6, 0.4, 3.4], "elbow": 60.0},
                "hand.R": {"space": "chest", "pos": [4.3, 0.6, 1.6], "elbow": 60.0},
                "foot.L": {"space": "pelvis", "pos": [-2.2, 0.5, -8.2], "rot": [-20.0, 60.0, 0.0]},
                "foot.R": {"space": "pelvis", "pos": [2.6, 0.9, -7.6], "rot": [25.0, 60.0, 0.0]}}
    if kind == "heap":       # crumpled on the spot: sitting back on the heels, knees apart, arms limp
        return {"hand.L": {"space": "chest", "pos": [-3.4, 1.9, 0.6], "elbow": 20.0},
                "hand.R": {"space": "chest", "pos": [3.4, 2.1, 0.4], "elbow": 20.0},
                "foot.L": {"pos": [-2.7, -3.4, 0.7], "rot": [-25.0, 90.0, 0.0], "pole": [-0.6, 1.0, 0.2]},
                "foot.R": {"pos": [2.9, -3.0, 0.7], "rot": [25.0, 90.0, 0.0], "pole": [0.6, 1.0, 0.2]}}
    return {}


def free_rifle(yaw, e, n, z, bearing, pitch=0.0, roll=0.0):
    """The rifle on its own: at (east, north, up), pointing along the compass bearing (90 = screen right)."""
    x, y = w2l(yaw, e, n)
    return {"rifle": {"space": "root", "pos": [x, y, z], "rot": [bearing - yaw, pitch, roll], "hold": None}}


def fx_at(yaw, e, n, z=0.0, scale=1.0, bearing=0.0, pitch=0.0, roll=0.0):
    x, y = w2l(yaw, e, n)
    return {"pos": [x, y, z], "rot": [bearing - yaw, pitch, roll], "scale": scale}


def ballistic(p0, v, k, g=1.6, floor=0.2):
    """Position after k frames of a piece thrown from p0 with velocity v (px / frame): -> (e, n, z, landed)."""
    z = p0[2] + v[2] * k - 0.5 * g * k * k
    if z <= floor:      # where it came down
        kl = (v[2] + math.sqrt(max(0.0, v[2] * v[2] + 2 * g * (p0[2] - floor)))) / g
        return p0[0] + v[0] * kl, p0[1] + v[1] * kl, floor, True
    return p0[0] + v[0] * k, p0[1] + v[1] * k, z, False


def death_pose(name, k, fit=None):
    """Authored key poses (the fit adjusts the body's place and attitude per frame)."""
    yaw = DEATH_YAW[name]
    hip = B["hip_z"]
    fx = {}
    if name == "death0":       # thrown up and to the west, tumbling backwards; lands on its back
        e, z, pitch = [(-3.0, 15.0, -15), (-5.0, 16.5, -40), (-6.5, 16.0, -75), (-8.5, 14.5, -95), (-10.5, 10.5, -115),
                       (-12.0, 3.0, -128), (-12.3, 2.0, -104), (-12.5, 1.7, -92)][min(k, 7)]
        n = 0.0 if k < 5 else -1.8
        x, y = w2l(yaw, e, n)
        pose = {"pelvis": {"pos": [x, y, z], "rot": [0.0, pitch, 0.0 if k < 5 else 12.0]}, "chest": {"rot": [0.0, -8.0, 0.0]},
                "head": {"rot": [0.0, -15.0, 0.0]}}
        pose.update(limbs("flail" if k < 6 else "sprawl"))
        r = [(3.5, 0.0, 19.8, 90, 50), (1.5, 0.0, 21.5, 90, 80), (3.0, 0.0, 10.0, 90, -40), (-1.0, -3.0, 3.5, 90, -50)]
        pose.update(free_rifle(yaw, *r[k][:3], r[k][3], r[k][4]) if k < 4 else free_rifle(yaw, -1.5, -6.4, 0.35, 90, 0, 90))
        if k >= 5:
            fx["blood0"] = fx_at(yaw, -11.5, -3.8, 0.02, [0.3, 0.5, 0.75, 0.95, 1.1, 1.2, 1.25][k - 5])
        if k in (5, 6):
            for i, (ve, vz) in enumerate(((-1.6, 1.5), (1.4, 1.8), (0.4, 2.3), (-0.6, 1.2))):
                pe, pn, pz, _ = ballistic((-12.0, -1.8, 1.5), (ve, -0.4, vz), k - 4)
                fx["drop%d" % i] = fx_at(yaw, pe, pn, pz, 1.0, 90 if ve > 0 else 270, 40)
    elif name == "death1":     # thrown backwards to the east, lands flat on its back in a splash
        e, z, pitch = [(2.5, 12.5, -35), (4.0, 19.0, -80), (6.0, 12.0, -95), (8.5, 1.7, -90)][min(k, 3)]
        n = 0.0 if k < 3 else -1.0
        x, y = w2l(yaw, e, n)
        pose = {"pelvis": {"pos": [x, y, z], "rot": [0.0, pitch, 0.0]}, "chest": {"rot": [0.0, -6.0, 0.0]}, "head": {"rot": [0.0, -10.0, 0.0]}}
        pose.update(limbs("flail" if k < 3 else "sprawl"))
        r = [(6.0, 0.0, 20.0, 90, 70), (13.0, 0.0, 14.0, 90, -30), (15.0, 0.0, 4.0, 90, -60)]
        pose.update(free_rifle(yaw, *r[k][:3], r[k][3], r[k][4]) if k < 3 else free_rifle(yaw, 17.5, -5.0, 0.35, 90, 0, 90))
        if k >= 3:
            g = [0.45, 0.7, 0.9, 1.05, 1.15, 1.2, 1.22, 1.24, 1.25][k - 3]
            fx["blood0"] = fx_at(yaw, 6.5, -1.6, 0.02, [1.5 * g, 0.75 * g, 1.0])
            fx["blood1"] = fx_at(yaw, 14.5, -1.2, 0.02, [0.8 * g, 0.6 * g, 1.0])
        if k in (3, 4, 5):     # the splash
            for i, (ve, vz) in enumerate(((-2.2, 2.4), (-0.8, 3.0), (0.6, 2.6), (1.8, 2.9), (3.0, 2.2), (-3.2, 1.6))):
                pe, pn, pz, landed = ballistic((8.5 + ve, -1.2, 1.2), (ve * 0.9, -0.2, vz), k - 2)
                if not landed:
                    fx["drop%d" % i] = fx_at(yaw, pe, pn, pz, 1.0, 90 if ve > 0 else 270, 60)
    elif name == "death2":     # bursts: a fountain of blood, pieces fall around
        pose = {}
        if k < 2:
            pose = {"pelvis": {"pos": [0.0, 0.0, hip - (0.0, 0.6)[k]], "rot": [0.0, (-6.0, -14.0)[k], 0.0]},
                    "hand.L": {"space": "chest", "pos": [-3.4, 0.8, 6.6], "elbow": 20.0},
                    "hand.R": {"space": "chest", "pos": [3.2, 1.0, 6.2], "elbow": 20.0}}
            pose.update(free_rifle(yaw, (-1.0, -3.5)[k], 0.0, (21.0, 20.0)[k], 0, (75, 40)[k]))
            n_drop = (2, 6)[k]
            for i in range(n_drop):
                a = 2 * math.pi * i / 6 + 0.4
                pe, pn, pz, _ = ballistic((0.0, 1.0, 13.0), (1.5 * math.cos(a), 0.3, 1.4 + 0.9 * math.sin(a)), k + 0.5)
                fx["drop%d" % i] = fx_at(yaw, pe, pn, pz, 1.6 + 0.4 * k, 90, 60)
        else:
            pose = {"body": 0}
            pose.update(free_rifle(yaw, -5.5, -5.0, 0.35, 90, 0, 90))
            pieces = [("gib0", (-0.5, 0.5, 12.0), (-1.3, -0.5, 1.2), 200, 80), ("gib1", (0.0, 0.5, 17.0), (-2.6, 0.4, 2.6), 90, 30),
                      ("gib2", (2.0, 0.5, 13.0), (1.0, -1.0, 2.2), 60, 10), ("gib3", (1.0, 0.0, 6.0), (2.2, 0.2, 1.6), 100, 5),
                      ("gib4", (-1.0, 0.0, 6.0), (3.4, -0.2, 2.4), 80, 5), ("gib5", (0.0, 2.0, 13.0), (-0.3, 1.0, 1.8), 30, 10)]
            for nme, p0, v, bearing, tumble in pieces:
                pe, pn, pz, landed = ballistic(p0, v, k - 1, floor=0.9)
                fx[nme] = fx_at(yaw, pe, pn, pz, 1.0, bearing + (0 if landed else tumble * (k - 1)), 0 if landed else 25 * (k - 1), 90 if nme in ("gib2", "gib3", "gib4") else 0)
            for i in range(8):     # the fountain
                a = 2 * math.pi * i / 8 + 0.2
                pe, pn, pz, landed = ballistic((0.0, 0.5, 12.0), (2.0 * math.cos(a), 0.4 * math.sin(a), 2.6 + 1.2 * math.sin(a * 1.7)), k - 1)
                if not landed:
                    fx["drop%d" % i] = fx_at(yaw, pe, pn, pz, 2.2, 90 if math.cos(a) > 0 else 270, 55)
            g = [0.35, 0.6, 0.85, 1.0, 1.1, 1.16, 1.2, 1.22, 1.24, 1.25][k - 2]
            fx["blood0"] = fx_at(yaw, 0.0, -0.8, 0.02, [1.5 * g, 1.1 * g, 1.0])
            fx["blood1"] = fx_at(yaw, -6.5, 0.6, 0.02, 0.7 * g)
            fx["blood2"] = fx_at(yaw, 8.0, -0.4, 0.02, 0.8 * g)
    elif name == "death3":     # hit, the knees give way, crumples on the spot
        if k == 0:
            pose = {"pelvis": {"pos": [0.0, -0.3, hip - 0.3], "rot": [0.0, -10.0, 0.0]},
                    "hand.L": {"space": "chest", "pos": [-3.6, 1.0, 6.2], "elbow": 20.0}, "hand.R": {"space": "chest", "pos": [3.4, 1.4, 5.6], "elbow": 20.0}}
            pose.update(free_rifle(yaw, -6.0, 0.0, 19.5, 315, 60))
        elif k == 1:
            pose = {"pelvis": {"pos": [0.0, -0.8, 7.6], "rot": [0.0, -14.0, 6.0]}, "chest": {"rot": [0.0, 10.0, 0.0]},
                    "hand.L": {"space": "chest", "pos": [-3.9, 1.2, 3.2], "elbow": 30.0}, "hand.R": {"space": "chest", "pos": [3.6, 1.6, 2.4], "elbow": 30.0}}
            pose.update(free_rifle(yaw, -8.0, 0.0, 11.0, 300, -40))
        elif k == 2:
            pose = {"pelvis": {"pos": [0.0, -1.0, 4.9], "rot": [0.0, 12.0, 8.0]}, "chest": {"rot": [0.0, 28.0, 0.0]}, "head": {"rot": [0.0, 20.0, 0.0]},
                    "foot.L": {"pos": [-1.9, 1.6, B["ankle_z"]], "rot": [-25.0, 0.0, 0.0], "knee": 25.0},
                    "foot.R": {"pos": [1.9, 1.2, B["ankle_z"]], "rot": [25.0, 0.0, 0.0], "knee": 25.0},
                    "hand.L": {"space": "chest", "pos": [-3.2, 1.8, 0.6], "elbow": 20.0}, "hand.R": {"space": "chest", "pos": [3.0, 2.0, 0.2], "elbow": 20.0}}
            pose.update(free_rifle(yaw, -8.5, -1.0, 0.35, 90, 0, 90))
        else:
            pose = {"pelvis": {"pos": [0.0, -1.2, 2.5], "rot": [0.0, 30.0, 10.0]}, "chest": {"rot": [0.0, 62.0, 0.0]}, "head": {"rot": [0.0, 30.0, 0.0]}}
            pose.update(limbs("heap"))
            pose.update(free_rifle(yaw, -8.5, -1.0, 0.35, 90, 0, 90))
            if k >= 4:
                g = [0.3, 0.55, 0.8, 1.0, 1.1, 1.18, 1.22, 1.25][k - 4]
                fx["blood0"] = fx_at(yaw, 0.5, -2.6, 0.02, [1.4 * g, 0.9 * g, 1.0])
        if k < 2:
            fx["drop0"] = fx_at(yaw, 0.6 + k, 1.4, 13.0 + k, 1.0, 90, 30)
    elif name == "death4":     # crushed: flat in a wide pool
        x, y = w2l(yaw, -1.0, -1.0)
        pose = {"pelvis": {"pos": [x, y, 1.25], "rot": [0.0, 90.0, 0.0]}, "chest": {"rot": [0.0, 0.0, 0.0]}, "head": {"rot": [20.0, -20.0, 0.0]}}
        pose.update(limbs("sprawl"))
        pose.update(free_rifle(yaw, 6.0, -4.5, 0.3, 75, 0, 90))
        g = [0.82, 0.93, 1.0, 1.03, 1.05, 1.06, 1.07, 1.07, 1.07, 1.07, 1.07, 1.07][k]
        fx["blood0"] = fx_at(yaw, 0.5, -1.2, 0.02, [2.9 * g, 1.3 * g, 1.0])
        fx["blood1"] = fx_at(yaw, -7.0, -0.6, 0.03, [1.7 * g, 0.9 * g, 1.0], 25.0)
        fx["blood2"] = fx_at(yaw, 8.0, -1.6, 0.03, [1.6 * g, 0.8 * g, 1.0], -20.0)
    else:                      # fidgets: from the standing pose facing south (the fit shapes the rifle per frame)
        base = frame_spec(0, 4, fit)["pose"]
        pose = {key: base[key] for key in ("pelvis", "chest", "head", "foot.L", "foot.R", "rifle")}
        lift = [0.0, 0.35, 0.8, 1.0, 1.0, 0.8, 0.35, 0.0][k]
        r = K.merge({}, pose["rifle"])
        if name == "fidget0":      # raises the rifle to the shoulder and sights along it
            r["pos"] = [r["pos"][0], r["pos"][1], r["pos"][2] + 1.9 * lift]
            r["rot"] = [r["rot"][0], r["rot"][1] + 18.0 * lift, 0.0]
            pose["head"] = {"rot": [20.0 * lift, base["head"]["rot"][1], 0.0]}
        else:                      # looks down at the rifle, turning it over in front of the chest
            r["pos"] = [r["pos"][0] * (1 - 0.5 * lift), r["pos"][1], r["pos"][2] - 0.8 * lift]
            r["rot"] = [r["rot"][0] * (1 - 0.35 * lift), r["rot"][1] - 25.0 * lift, 0.0]
            pose["head"] = {"rot": [0.0, base["head"]["rot"][1] + 14.0 * lift, 0.0]}
        pose["rifle"] = r
    if fx:
        pose["fx"] = fx
    return pose


# ---- fitted corrections (tools/models/infantry_check/fit.py writes them)

def load_fit(path=POSES_FILE):
    if os.path.exists(path):
        return json.load(open(path))
    return {}


FIT = load_fit()
set_dims(FIT.get("dims"))


def apply_flat(pose, flat):
    for path, v in (flat or {}).items():
        K.set_path(pose, path, v)
    return pose


def frame_spec(row, col, fit=None):
    """-> dict(pose, yaw, shift) for the original frame (row, col); None if the art has no such frame."""
    fit = FIT if fit is None else fit
    if row >= 22:
        d = next((d for d in DEATHS if d[2] == col and row in d[1]), None)
        if d is None:
            return None
        k = row - 22
        pose = K.merge(SKEL.neutral, death_pose(d[0], k, fit))
        key = "%d,%d" % (row, col)
        pose = apply_flat(pose, fit.get("frame", {}).get(key))
        fid = d[0].startswith("fidget")
        yaw = DEATH_YAW[d[0]] + (fit.get("yaw", {}).get("stand,4", 0.0) if fid else 0.0)
        shift = fit.get("shift", {}).get("stand,4", [0.0, 0.0]) if fid else [0.0, 0.0]      # fidgets stand where the stand S frame does
        extra = fit.get("shift", {}).get(key, [0.0, 0.0])
        return dict(pose=pose, yaw=yaw, shift=[shift[0] + extra[0], shift[1] + extra[1]], anim=d[0], index=k,
                    alpha=FADE.get(k, 1.0) if not fid else 1.0)
    name = ROW_SET[row]
    shared = fit.get("shared", {}).get(str(row)) or {}
    pose = K.merge(SKEL.neutral, base_pose(row, {k[2:]: v for k, v in shared.items() if k.startswith("p/")}))
    pose = apply_flat(pose, shared)
    pose = K.merge(pose, carry(row, col))
    pose = apply_flat(pose, fit.get("dir", {}).get("%s,%d" % (name, col)))
    pose = apply_flat(pose, fit.get("frame", {}).get("%d,%d" % (row, col)))
    shift = fit.get("shift", {}).get("%s,%d" % (name, col), [0.0, 0.0])
    extra = fit.get("shift", {}).get("%d,%d" % (row, col), [0.0, 0.0])
    yaw = col * 45.0 + fit.get("yaw", {}).get("%s,%d" % (name, col), 0.0)
    rows = next(rows for n, rows, _ in SETS if n == name)
    return dict(pose=pose, yaw=yaw, shift=[shift[0] + extra[0], shift[1] + extra[1]],
                anim="%s.%s" % (name, DIRS[col]), index=rows.index(row), scale=LYING_SCALE if name in LYING else 1.0)


def all_frames():
    """[(row, col, spec)] for every frame the original art has."""
    out = []
    for row in range(34):
        for col in range(8):
            s = frame_spec(row, col)
            if s is not None:
                out.append((row, col, s))
    return out


def cloud(density=60.0):
    global _cloud
    if _cloud is None:
        _cloud = K.build_cloud(SKEL, parts(), CLASSES, density)
    return _cloud


def preview(row, col, box=(-16, -20, 16, 8), ss=4, spec=None):
    """Class coverage [8, h, w] of the frame as the kit's preview renderer draws it."""
    s = spec or frame_spec(row, col)
    return K.render_classes(cloud(), SKEL.matrices(s["pose"]), s["yaw"], s["shift"], ANCHOR_PX, box, ss,
                            grow=GROW * s.get("scale", 1.0))


# ================================================================================================
# Blender (everything below needs bpy; run by `blender --python infantry_v1.py`)
# ================================================================================================

# The pack's light: a dim high sun in the west-north-west only for the ground shadow (short, to the east-south-east,
# as the original's) and a strong shadowless side light from the west (render_pack --fill) for the shading - the
# original is lit from the camera's left: light blue on the west side of every part, navy on the rest. The uniform's
# grey then goes through a tone curve (--house-curve) that gathers the shaded side into the ramp's dark shades and
# the lit side into its light ones: the original uses five shades of the sixteen (3, 4 | 7 | 9, 11).
RENDER_FLAGS = ('--device cpu --view-transform Standard --look "High Contrast" --exposure 0 --house-grey 0.56 '
                '--sky-strength 0.12 --shadow-azimuth -25 --sun-elevation 72 --sun-strength 0.6 --fill 4.5,-8,32 '
                '--contour 0.8 --contour-width 0.75 --contour-azimuth -60 --sharpen 0.8 '
                '--house-curve 0:0.13,0.22:0.155,0.42:0.19,0.52:0.31,0.62:0.49,0.76:0.65,0.9:0.75,1:0.88')
PX = 1.0 / 32.0

# sRGB base colours (the pack is rendered with the Standard view transform: these are nearly the colours on
# screen for a sunlit face) - the original's own few colours: tan / brown, orange, black, navy
COL = dict(
    pack=(0.80, 0.72, 0.60),        # pack and neck flap: sun-bleached canvas (original lit (165,148,123))
    pack_dark=(0.47, 0.38, 0.29),   # flap, straps, pockets: the original's browns
    roll=(0.76, 0.70, 0.60),        # bedroll
    belt=(0.20, 0.155, 0.115),      # leather: belt, harness
    pouch=(0.50, 0.41, 0.30),
    skin=(0.97, 0.60, 0.22),        # bare face and hands: the original's orange skin (239,173,82) lit, (198,99,0) in shade
    eye=(0.42, 0.21, 0.08),
    boot=(0.030, 0.027, 0.024),
    sole=(0.022, 0.020, 0.018),
    gun=(0.048, 0.046, 0.05),
    gun_blue=(0.105, 0.17, 0.33),   # the original's navy fore-end (24,41,82)
    HOUSE=(0.80, 0.80, 0.80),
)
# (metallic, roughness, surface kind of the tank kit's weathering: paint = dusty matt, steel, rubber, dark)
SPEC = dict(
    pack=(0.0, 0.85, "paint"), pack_dark=(0.0, 0.8, "paint"), roll=(0.0, 0.9, "paint"), belt=(0.0, 0.55, "rubber"),
    pouch=(0.0, 0.8, "paint"), skin=(0.0, 0.6, "dark"), eye=(0.0, 0.5, "dark"),
    boot=(0.0, 0.5, "rubber"), sole=(0.0, 0.8, "dark"), gun=(0.5, 0.4, "steel"), gun_blue=(0.6, 0.35, "steel"),
    HOUSE=(0.0, 0.7, "house"),
)
FILM = dict(pack=(0.68, 0.61, 0.50), pack_dark=(0.42, 0.35, 0.27), roll=(0.64, 0.58, 0.49), pouch=(0.44, 0.37, 0.28),
            boot=(0.06, 0.052, 0.044), belt=(0.26, 0.21, 0.16), gun=(0.10, 0.095, 0.09), gun_blue=(0.13, 0.18, 0.30))
HOUSE_SHADE = dict(HOUSE=0.8, HOUSE_plate=1.0)      # COLOR_0 of cloth / armour plates (the engine tints by luminance)
PLAIN = ("skin", "eye")                             # materials kept out of the atlas
GLOW = dict(skin=0.16)                               # self-illumination (share of the base colour)
PREVIEW_HOUSE = (0.36, 0.5, 0.72)      # review renders only: about the Atreides ramp over the shade map


def blender_main():
    import time

    import bmesh  # noqa: F401
    import bpy
    from mathutils import Matrix, Vector, noise
    from mathutils.bvhtree import BVHTree

    import combat_tank_v2 as V2
    import combat_tank_v3 as V3
    log = V2.log

    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opt = dict(out=None, bake=True, bake_size=1024, turntable=None, samples=64, proxy=None)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--out":
            opt["out"] = argv[i + 1]; i += 1
        elif a == "--no-bake":
            opt["bake"] = False
        elif a == "--bake-size":
            opt["bake_size"] = int(argv[i + 1]); i += 1
        elif a == "--turntable":
            opt["turntable"] = argv[i + 1]; i += 1
        elif a == "--samples":
            opt["samples"] = int(argv[i + 1]); i += 1
        elif a == "--device":
            V3.DEVICE = argv[i + 1].lower(); i += 1
        else:
            raise SystemExit("unknown option " + a)
        i += 1
    if opt["out"] is None and opt["turntable"] is None:
        opt["out"] = os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-infantry")

    V3.COL.update(COL)
    V3.SPEC.update(SPEC)
    V3.FILM.update(FILM)
    V3.SOOT.clear()
    #                       film  blotch cavity soft  grime streak chip  low dust
    V3.WEATHER["paint"] = (0.75, 0.55, 0.85, 0.55, 0.65, 0.35, 0.0, 0.35)       # canvas / cloth: dusty, no chips
    V3.WEATHER["steel"] = (0.55, 0.0, 0.6, 0.3, 0.2, 0.0, 0.35, 0.1)
    V3.WEATHER["rubber"] = (0.6, 0.0, 0.5, 0.25, 0.0, 0.0, 0.0, 0.16)           # boots and leather: a little sand on them
    V3.WEATHER["dark"] = (0.0, 0.0, 0.45, 0.15, 0.0, 0.0, 0.0, 0.0)
    V3.UV_SCALE.update(boot=0.6, sole=0.3, gun=0.6, gun_blue=0.6, belt=0.6, skin=0.8, HOUSE=1.0)
    V2.setup_gpu = V3.setup_device
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    t0 = time.time()

    # ---- parts -> meshes in the rest pose (the neutral stand), px -> tiles
    rest = SKEL.rest()
    body_objs, fx_objs, flash_objs, skin_objs = [], [], [], []
    for k, part in enumerate(parts()):
        v, f = K.part_mesh(part)
        m = rest[part["bone"]]
        v = (v @ m[:3, :3].T + m[:3, 3]) * PX
        me = bpy.data.meshes.new("p%d" % k)
        me.from_pydata([tuple(p) for p in v], [], [tuple(int(q) for q in face) for face in f])
        me.update()
        o = bpy.data.objects.new("p%d_%s" % (k, part["mat"]), me)
        scene.collection.objects.link(o)
        mat = part["mat"]
        fx = part.get("fx") or part["bone"] in B["fx"] or part["bone"] == "flash"
        if mat in HOUSE_SHADE:
            if fx:                         # the pieces' own house material: the body's gets the shade map
                hm = bpy.data.materials.get("HOUSE_fx")
                if hm is None:
                    hm = V3.mat("HOUSE").copy()
                    hm.name = "HOUSE_fx"
                me.materials.append(hm)
            else:
                me.materials.append(V3.mat("HOUSE"))
            o["d2k_shade"] = part.get("shade", HOUSE_SHADE[mat])
        elif fx or mat in PLAIN:       # fx parts and the skin keep plain materials (the atlas holds the gear only)
            me.materials.append(fx_material(bpy, mat))
            o["d2k_shade"] = 1.0
        else:
            me.materials.append(V3.mat(mat))
            o["d2k_shade"] = 1.0
        vg = o.vertex_groups.new(name=part["bone"])
        vg.add(list(range(len(me.vertices))), 1.0, "REPLACE")
        V2.smooth_by_angle(o, 75 if mat in PLAIN else 40)
        (flash_objs if part["bone"] == "flash" else fx_objs if fx else skin_objs if mat in PLAIN else body_objs).append(o)
    for o in body_objs + fx_objs + flash_objs + skin_objs:
        part_colours(o, bpy)
    body = V2.join(body_objs, "body")
    extra = ([V2.join(skin_objs, "skin")] if skin_objs else []) + ([V2.join(fx_objs, "fx")] if fx_objs else []) + \
        ([V2.join(flash_objs, "flash")] if flash_objs else [])
    house_shading(body, bpy, BVHTree, Vector, noise)
    log(f"built: {V2.tri_count([body])} triangles (+ {V2.tri_count(extra)} fx), {len(body.data.vertices)} vertices in {time.time() - t0:.1f} s")
    house_png = None
    if opt["bake"]:
        t1 = time.time()
        V3.bake_and_merge([body], opt["bake_size"])
        house_png = os.path.join(opt["out"], "models", "unit", KEY + ".house.png") if opt["out"] else \
            os.path.join(bpy.app.tempdir, "d2k_infantry_house.png")
        os.makedirs(os.path.dirname(house_png), exist_ok=True)
        bake_house_shade(bpy, body, opt["bake_size"], house_png)
        log(f"textures baked in {time.time() - t1:.1f} s ({opt['bake_size']} px atlas, uniform shade map)")

    # ---- armature: one bone per kit bone, its frame = the kit's rest frame
    arm_data = bpy.data.armatures.new("rig")
    arm = bpy.data.objects.new("rig", arm_data)
    scene.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")

    def to_tiles(m):
        mm = Matrix([list(r) for r in m])
        mm.translation = mm.translation * PX
        return mm

    rest_m = {n: to_tiles(rest[n]) for n in SKEL.bones}
    for n in SKEL.bones:
        eb = arm_data.edit_bones.new(n)
        eb.head = (0, 0, 0)
        eb.tail = (0, 2.0 * PX, 0)
        r = rest_m[n].copy()
        sc = r.to_scale()
        r = r @ Matrix.Diagonal((1 / sc.x, 1 / sc.y, 1 / sc.z, 1.0))      # edit bones carry no scale
        eb.matrix = r
        rest_m[n] = r
        if SKEL.parent[n]:
            eb.parent = arm_data.edit_bones[SKEL.parent[n]]
    bpy.ops.object.mode_set(mode="OBJECT")
    for o in [body] + extra:
        o.parent = arm
        md = o.modifiers.new("rig", "ARMATURE")
        md.object = arm

    # ---- animations: one action per strip of the original art ("walk.E", "death2", ...), one key per frame
    anims = {}
    for row, col, spec in all_frames():
        anims.setdefault(spec["anim"], {})[spec["index"]] = spec
    arm.animation_data_create()
    for pb in arm.pose.bones:
        pb.rotation_mode = "QUATERNION"
    t2 = time.time()
    for name in sorted(anims):
        act = bpy.data.actions.new(name)
        arm.animation_data.action = act
        prev_q = {}
        for idx in sorted(anims[name]):
            pose = SKEL.solve(anims[name][idx]["pose"])
            pm = {n: to_tiles(pose[n]) for n in SKEL.bones}
            for n in SKEL.bones:
                par = SKEL.parent[n]
                rl = (rest_m[par].inverted() @ rest_m[n]) if par else rest_m[n]
                pl = (pm[par].inverted() @ pm[n]) if par else pm[n]
                pb = arm.pose.bones[n]
                pb.matrix_basis = rl.inverted() @ pl
                q = pb.rotation_quaternion.copy()
                if n in prev_q and q.dot(prev_q[n]) < 0:      # keep the quaternions on one side (clean interpolation)
                    q.negate()
                    pb.rotation_quaternion = q
                prev_q[n] = q
                pb.keyframe_insert("location", frame=idx)
                pb.keyframe_insert("rotation_quaternion", frame=idx)
                pb.keyframe_insert("scale", frame=idx)
        tr = arm.animation_data.nla_tracks.new()
        tr.name = name
        tr.strips.new(name, 0, act)
        arm.animation_data.action = None
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    log(f"{len(anims)} animations keyed in {time.time() - t2:.1f} s")

    if opt["out"]:
        path = os.path.join(opt["out"], "models", "unit", KEY + ".glb")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        bpy.ops.object.select_all(action="DESELECT")
        for o in [arm, body] + extra:
            o.select_set(True)
        bpy.ops.export_scene.gltf(
            filepath=path, export_format="GLB", use_selection=True, export_yup=True, export_apply=False,
            export_normals=True, export_materials="EXPORT", export_image_format="AUTO", export_extras=False,
            export_tangents=False, export_texcoords=True, export_attributes=False,
            export_vertex_color="MATERIAL", export_all_vertex_colors=False,
            export_animations=True, export_animation_mode="NLA_TRACKS", export_skins=True, export_frame_range=False,
            export_force_sampling=True, export_optimize_animation_size=False, export_anim_single_armature=True,
            export_all_influences=False, export_def_bones=False)
        log(f"exported {path} ({os.path.getsize(path) // 1024} KB)")
        write_frame_map(os.path.join(opt["out"], "models", "unit", KEY + ".anim.json"), house_png)
        with open(os.path.join(opt["out"], "models.ini"), "w") as f:
            f.write("; scale: the fitted figure is enlarged a little - box-filtered to the original's pixels a render loses\n"
                    "; the half-covered rim that the pixel art draws as full pixels\n[%s]\nscale=%g\n" % (KEY, GROW))
        log("2D pack: tools/render_sprites/render_pack.py --models " + opt["out"] + " --out artpacks/rendered-infantry "
            "--types " + KEY + " " + RENDER_FLAGS)
    if opt["turntable"]:
        review_renders(bpy, Vector, arm, opt["turntable"], opt["samples"])


def fx_material(bpy, name):
    """Plain materials of the fx parts: FLASH (emissive; the renderer gives it its glow), blood (wet, dark red), and
    the body pieces' own colours."""
    mname = "FLASH" if name == "flash" else name if name.startswith("blood") else "fx_" + name
    m = bpy.data.materials.get(mname)
    if m is not None:
        return m
    m = bpy.data.materials.new(mname)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    if name == "flash":
        b.inputs["Base Color"].default_value = (1.0, 0.72, 0.2, 1)
        b.inputs["Emission Color"].default_value = (1.0, 0.78, 0.35, 1)
        b.inputs["Emission Strength"].default_value = 6.0
    elif name == "blood":           # the original's blood is a dark red-brown (mean 150, 38, 8): wet, not jelly
        b.inputs["Base Color"].default_value = (0.26, 0.016, 0.006, 1)
        b.inputs["Roughness"].default_value = 0.28
    elif name == "blood_dark":      # the soaked sand round it
        b.inputs["Base Color"].default_value = (0.085, 0.014, 0.007, 1)
        b.inputs["Roughness"].default_value = 0.7
    else:
        import combat_tank_v2 as V2
        lin = V2.srgb_to_linear(COL[name])
        b.inputs["Base Color"].default_value = (*lin, 1)
        b.inputs["Metallic"].default_value = SPEC[name][0]
        b.inputs["Roughness"].default_value = SPEC[name][1]
        if name in GLOW:               # the original's face and hands are bright orange whichever way they face
            b.inputs["Emission Color"].default_value = (*lin, 1)
            b.inputs["Emission Strength"].default_value = GLOW[name]
    return m


def bake_house_shade(bpy, body, size, path):
    """The uniform's surface as a grey map in the atlas's UV layout (LIGHT_INF.house.png; the frame map names it and
    render_pack.py shades the HOUSE areas with it): the part's shade (COLOR_0) x dirt in creases and seams x a cloth
    grain x stains of about a sprite pixel x a few darker patches x grime towards the bottom of each part, with
    lighter worn edges. The house mask itself stays plain - the game's ramp gives the colour, this gives the shade."""
    scene = bpy.context.scene
    house = next(m for m in body.data.materials if m is not None and m.name.startswith("HOUSE"))
    nt = house.node_tree
    nodes, links = nt.nodes, nt.links
    out = next(n for n in nodes if n.bl_idname == "ShaderNodeOutputMaterial")
    keep = out.inputs["Surface"].links[0].from_socket
    geo = nodes.new("ShaderNodeNewGeometry")

    def noise(scale, detail, rough, off):
        n = nodes.new("ShaderNodeTexNoise")
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = rough
        mp = nodes.new("ShaderNodeMapping")
        mp.inputs["Location"].default_value = (off, off * 0.7, off * 1.3)
        links.new(geo.outputs["Position"], mp.inputs["Vector"])
        links.new(mp.outputs[0], n.inputs["Vector"])
        return n.outputs["Fac"]

    def remap(sock, a, b, c, d):
        m = nodes.new("ShaderNodeMapRange")
        for k, v in (("From Min", a), ("From Max", b), ("To Min", c), ("To Max", d)):
            m.inputs[k].default_value = v
        links.new(sock, m.inputs["Value"])
        return m.outputs[0]

    def mul(a, b):
        m = nodes.new("ShaderNodeMath")
        m.operation = "MULTIPLY"
        links.new(a, m.inputs[0])
        links.new(b, m.inputs[1])
        return m.outputs[0]

    vc = nodes.new("ShaderNodeVertexColor")
    vc.layer_name = "Col"
    bw = nodes.new("ShaderNodeRGBToBW")
    links.new(vc.outputs["Color"], bw.inputs[0])
    ao = nodes.new("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = 0.045
    ao.samples = 12
    ao.only_local = True
    val = mul(bw.outputs[0], remap(ao.outputs["AO"], 0.92, 0.45, 1.0, 0.3))                  # creases and seams
    val = mul(val, remap(noise(95.0, 2.0, 0.6, 0.0), 0.3, 0.7, 0.86, 1.08))                 # cloth grain
    val = mul(val, remap(noise(24.0, 3.0, 0.6, 3.1), 0.3, 0.7, 0.8, 1.1))                   # stains of about a pixel
    val = mul(val, remap(noise(7.5, 2.0, 0.5, 11.0), 0.55, 0.75, 1.0, 0.7))                 # a few darker patches
    ph = nodes.new("ShaderNodeAttribute")
    ph.attribute_name = "grime_h"
    val = mul(val, remap(ph.outputs["Fac"], 0.35, 0.0, 1.0, 0.72))                          # grime at the bottom of each part
    bev = nodes.new("ShaderNodeBevel")
    bev.samples = 8
    bev.inputs["Radius"].default_value = 0.012
    dot = nodes.new("ShaderNodeVectorMath")
    dot.operation = "DOT_PRODUCT"
    links.new(bev.outputs["Normal"], dot.inputs[0])
    links.new(geo.outputs["Normal"], dot.inputs[1])
    val = mul(val, remap(dot.outputs["Value"], 0.995, 0.9, 1.0, 1.25))                      # worn edges: a shade lighter
    comb = nodes.new("ShaderNodeCombineColor")
    for i in range(3):
        links.new(val, comb.inputs[i])
    em = nodes.new("ShaderNodeEmission")
    links.new(comb.outputs[0], em.inputs["Color"])
    links.new(em.outputs[0], out.inputs["Surface"])
    img = bpy.data.images.new("d2k_house_shade", size, size, alpha=False, float_buffer=False)
    dummy = bpy.data.images.new("d2k_bake_dummy", 8, 8, alpha=False)
    for m in body.data.materials:           # every material of the object needs a bake target
        t = m.node_tree.nodes.new("ShaderNodeTexImage")
        t.name = "HOUSE_BAKE_TARGET"
        t.image = img if m is house else dummy
        m.node_tree.nodes.active = t
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 16
    scene.cycles.use_denoising = False
    scene.render.bake.margin = 6
    scene.render.bake.use_clear = True
    bpy.ops.object.select_all(action="DESELECT")
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.bake(type="EMIT", margin=6, use_clear=True)
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    links.new(keep, out.inputs["Surface"])
    for m in body.data.materials:
        m.node_tree.nodes.remove(m.node_tree.nodes["HOUSE_BAKE_TARGET"])


def part_colours(o, bpy):
    """COLOR_0 "Col" (the part's shade on house parts, white elsewhere) and the kit's "grime_h" attribute (height
    within the part, 0 bottom .. 1 top) that the weathering shaders read."""
    me = o.data
    k = o.get("d2k_shade", 1.0)
    ca = me.color_attributes.new("Col", "FLOAT_COLOR", "CORNER")
    for d in ca.data:
        d.color = (k, k, k, 1.0)
    zs = [v.co.z for v in me.vertices]
    z0, z1 = min(zs), max(zs)
    gh = me.attributes.new("grime_h", "FLOAT", "POINT")
    for v, d in zip(me.vertices, gh.data):
        d.value = (v.co.z - z0) / max(1e-6, z1 - z0)


def house_shading(body, bpy, BVHTree, Vector, noise):
    """The uniform is the plain house mask, so its only surface detail is COLOR_0: ambient occlusion (creases,
    under the arms, between the legs, under the pack), a mottle of about one sprite pixel, dust towards the
    boots - all as darker shades of the house ramp."""
    me = body.data
    me.calc_loop_triangles()
    bvh = BVHTree.FromPolygons([tuple(v.co) for v in me.vertices], [tuple(p.vertices) for p in me.polygons])
    house = {i for i, m in enumerate(me.materials) if m is not None and m.name.startswith("HOUSE")}
    ca = me.color_attributes["Col"]
    rng = np.random.default_rng(3)
    dirs = rng.normal(size=(20, 3))
    dirs /= np.linalg.norm(dirs, axis=1, keepdims=True)
    ao = {}
    for p in me.polygons:
        if p.material_index not in house:
            continue
        for vi in p.vertices:
            if vi in ao:
                continue
            v = me.vertices[vi]
            n = v.normal
            hit = 0
            cnt = 0
            for d in dirs:
                dv = Vector(d)
                if dv.dot(n) < 0.15:
                    continue
                cnt += 1
                if bvh.ray_cast(v.co + n * 0.004, dv, 2.2 * PX)[0] is not None:
                    hit += 1
            ao[vi] = 1.0 - hit / max(1, cnt)
    for p in me.polygons:
        if p.material_index not in house:
            continue
        for li in p.loop_indices:
            vi = me.loops[li].vertex_index
            v = me.vertices[vi]
            k = ca.data[li].color[0]
            a = 0.42 + 0.58 * ao[vi] ** 0.8
            mott = 1.0 + 0.2 * noise.noise(v.co * 46.0) + 0.12 * noise.noise(v.co * 120.0)
            blot = 1.0 - 0.22 * max(0.0, noise.noise(v.co * 17.0 + Vector((3.1, 1.7, 5.3))) - 0.15) / 0.5     # a few dirt patches
            dust = 1.0 - 0.2 * max(0.0, 1.0 - v.co.z / (4.0 * PX))          # dusty trouser bottoms: a shade darker
            f = max(0.0, min(1.0, k * a * mott * blot * dust))
            ca.data[li].color = (f, f, f, 1.0)


def write_frame_map(path, house_png=None):
    """LIGHT_INF.anim.json: for every original frame (offset from the art's first frame = row * 8 + column) the
    animation, the frame in it, the model's yaw and its ground shift - what render_pack.py renders."""
    frames = {}
    for row, col, spec in all_frames():
        e = {"anim": spec["anim"], "frame": spec["index"], "yaw": round(spec["yaw"], 3),
             "shift": [round(spec["shift"][0], 3), round(spec["shift"][1], 3)]}
        if spec.get("alpha", 1.0) < 1.0:
            e["alpha"] = spec["alpha"]
        if spec.get("scale", 1.0) != 1.0:
            e["scale"] = spec["scale"]
        frames[str(row * 8 + col)] = e
    out = {"format": "d2k-anim-1", "unit": UNIT, "model": KEY + ".glb", "dirs": 8, "anchor_px": list(ANCHOR_PX),
           "frames": frames}
    if house_png:
        out["house_texture"] = os.path.basename(house_png)
    with open(path, "w") as f:
        json.dump(out, f, indent=1)
        f.write("\n")


def review_renders(bpy, Vector, arm, out_dir, samples, size=720):
    """The model itself: eight views around it and close-ups (house colour previewed as Atreides blue)."""
    import combat_tank_v2 as V2
    import combat_tank_v3 as V3
    os.makedirs(out_dir, exist_ok=True)
    scene = bpy.context.scene
    if arm.animation_data is not None:        # the rest pose (the neutral stand), not whatever the NLA tracks blend to
        arm.animation_data.action = None
        for tr in arm.animation_data.nla_tracks:
            tr.mute = True
    scene.frame_set(0)
    from mathutils import Matrix
    for pb in arm.pose.bones:                 # (the exporter leaves the last sampled pose on the bones)
        pb.matrix_basis = Matrix.Identity(4)
    for m in bpy.data.materials:              # the skin's glow is for the sprites: here the face is lit like the rest
        if m.use_nodes and m.name.startswith("fx_skin"):
            m.node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = 0.0
    scene.render.engine = "CYCLES"
    V3.setup_device(scene)
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.render.film_transparent = False
    scene.render.resolution_x = scene.render.resolution_y = size
    scene.render.resolution_percentage = 100
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    shade = bpy.data.images.get("d2k_house_shade")
    for m in bpy.data.materials:
        if m.name.startswith("HOUSE") and m.use_nodes:
            tint = m.node_tree.nodes.get("HOUSE_TINT")
            if tint:
                tint.inputs[6].default_value = (*V2.srgb_to_linear(PREVIEW_HOUSE), 1)
                if shade is not None and m.name == "HOUSE":      # the uniform's shade map instead of the plain COLOR_0
                    tex = m.node_tree.nodes.new("ShaderNodeTexImage")
                    tex.image = shade
                    m.node_tree.links.new(tex.outputs["Color"], tint.inputs[7])
    bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 0, -0.0005))
    gp = bpy.context.active_object
    gm = bpy.data.materials.new("sand")
    gm.use_nodes = True
    gb = gm.node_tree.nodes["Principled BSDF"]
    gb.inputs["Base Color"].default_value = (*V2.srgb_to_linear((0.77, 0.63, 0.43)), 1)
    gb.inputs["Roughness"].default_value = 1
    gp.data.materials.append(gm)
    w = bpy.data.worlds.new("w")
    scene.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs["Color"].default_value = (0.55, 0.62, 0.75, 1)
    w.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 4.0
    sun.angle = math.radians(3)
    so = bpy.data.objects.new("sun", sun)
    scene.collection.objects.link(so)
    cam = bpy.data.cameras.new("cam")
    co = bpy.data.objects.new("cam", cam)
    scene.collection.objects.link(co)
    scene.camera = co

    def shot(name, az_deg, el_deg, dist, lens, target, sun_az, sun_el=52.0):
        a, e = math.radians(az_deg), math.radians(el_deg)
        pos = target + Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))) * dist
        co.location = pos
        co.rotation_euler = (target - pos).to_track_quat("-Z", "Y").to_euler()
        cam.lens = lens
        sa, se = math.radians(sun_az), math.radians(sun_el)
        to_sun = Vector((math.cos(sa) * math.cos(se), math.sin(sa) * math.cos(se), math.sin(se)))
        so.rotation_euler = to_sun.to_track_quat("Z", "Y").to_euler()
        scene.render.filepath = os.path.join(out_dir, name + ".png")
        bpy.ops.render.render(write_still=True)

    tgt = Vector((0, 0, 0.3))
    for i in range(8):
        az = -90 - 45 * i                       # 0: from behind (the game's direction 0), then clockwise seen from above
        shot(f"turn_{i}", az, 25, 2.6, 85, tgt, az - 60)
    scene.render.resolution_x, scene.render.resolution_y = 1100, 1100
    shot("closeup_front", 70, 12, 1.9, 85, Vector((0, 0, 0.36)), 130)
    shot("closeup_back", -110, 15, 1.9, 85, Vector((0, 0, 0.36)), -170)
    shot("closeup_head", 60, 10, 0.95, 85, Vector((0, 0.01, 0.53)), 120)
    shot("closeup_game_angle", 90, 45, 2.2, 85, Vector((0, 0, 0.3)), 150)
    import bpy as _b
    _b.data.objects.remove(gp)
    _b.data.objects.remove(so)
    _b.data.objects.remove(co)


if __name__ == "__main__":
    try:
        import bpy  # noqa: F401
    except ImportError:
        raise SystemExit("run inside Blender: blender -b --factory-startup --python tools/models/infantry_v1.py -- [options]")
    blender_main()

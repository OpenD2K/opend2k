"""Infantry rig kit: skeleton, poses, the game camera and a fast preview renderer - plain numpy, no Blender.

Shared by the infantry generators (tools/models/infantry_v1.py and the later types), the pose fitter and the
measuring scripts (tools/models/infantry_check/). Nothing here knows a particular soldier: a character is

  B       body dimensions (dict, px)                                  -> Skeleton(B)
  PARTS   list of primitives per bone (lofts / boxes / spheres)       -> meshes for Blender, a point cloud here
  poses   one pose description per original frame                     -> Skeleton.solve(pose) -> bone matrices

Units and axes: ORIGINAL SPRITE PIXELS (1 px = 1/32 tile). Model space: x = the soldier's right, y = forward,
z = up, origin on the ground under the standing figure. The soldier faces +y; a game direction turns the whole
model clockwise seen from above (direction 0 = north, 2 = east).

Bones (name: parent). A bone's frame is where its parts are modelled; limb bones have +Y along the limb and +Z
towards the joint's apex side (knee: forward), so the same frames serve as Blender bones.

  root: -        pelvis: root      chest: pelvis     head: chest
  upper_arm.L/R: chest             forearm.L/R: upper_arm (carries the hand)
  thigh.L/R: pelvis                shin.L/R: thigh                    foot.L/R: shin
  rifle: root    flash: rifle      fx bones (blood pools, debris): root

A pose is a dict; every entry is optional (defaults = the neutral stand):

  pelvis   {pos: [x, y, z], rot: [yaw, pitch, roll]}          root space; yaw clockwise, pitch = lean forward,
                                                              roll = lean to the right (degrees)
  chest    {rot: [...]}                                       relative to the pelvis
  head     {rot: [...]}                                       relative to the chest
  foot.L   {pos: [x, y, z], rot: [...], knee: deg,            ankle target and foot attitude in root space (or, with
            space: "root" | "pelvis"}                         space "pelvis", relative to the pelvis), knee swivel
  rifle    {space: "chest" | "root" | "shoulder", pos, rot, hold: "R" | "L" | None}
                                                              grip point + aim; "shoulder" = placed with the chest
                                                              but aimed in the root frame; hold = which hand is on
                                                              the grip (the other takes the fore-end, or the point
                                                              `fore` of the rifle's frame)
  hand.L   {space: "chest" | "root", pos: [...], elbow: deg,  wrist target of a hand that is not on the rifle; elbow
            pole: [x, y, z]}                                  swivel, or the elbow's side as a root-frame vector
Feet and hands also take `pole` (the knee's / elbow's side, root frame). Knees, elbows, wrists and ankles never
go under the ground (GROUND): a limb whose joint would is swivelled about its own axis until it is clear.
  flash    0 | size                                           muzzle flash: hidden, or its size (1 = as modelled)
  body     1 | 0                                              0: the body is hidden (only fx pieces show)
  fx       {bone: {pos, rot, scale}}                          blood / debris bones (root space)

The camera is the original's (docs/art-pipeline.md): orthographic, looking north, 45 deg down; a point shows at
sx = x, sy = -(y sin 45 + z cos 45).
"""
import copy
import math

import numpy as np

S45 = math.sqrt(0.5)
LIMB_BONES = ("upper_arm", "forearm", "thigh", "shin", "foot")
BONES = ["root", "pelvis", "chest", "head", "upper_arm.L", "forearm.L", "upper_arm.R", "forearm.R",
         "thigh.L", "shin.L", "foot.L", "thigh.R", "shin.R", "foot.R", "rifle", "flash"]
PARENT = {"root": None, "pelvis": "root", "chest": "pelvis", "head": "chest",
          "upper_arm.L": "chest", "forearm.L": "upper_arm.L", "upper_arm.R": "chest", "forearm.R": "upper_arm.R",
          "thigh.L": "pelvis", "shin.L": "thigh.L", "foot.L": "shin.L",
          "thigh.R": "pelvis", "shin.R": "thigh.R", "foot.R": "shin.R", "rifle": "root", "flash": "rifle"}
TINY = 1e-3      # fx parts are modelled at this scale and shown by scaling their bone by 1 / TINY (see fx_matrix)
GROUND = dict(knee=0.85, elbow=0.7, wrist=0.55, ankle=0.6)      # lowest height of a joint's centre (px): limbs stay above ground


# ----------------------------------------------------------------------------------------------
# 4x4 helpers


def translate(v):
    m = np.eye(4)
    m[:3, 3] = v
    return m


def rot_x(deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    m = np.eye(4)
    m[1, 1], m[1, 2], m[2, 1], m[2, 2] = c, -s, s, c
    return m


def rot_y(deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    m = np.eye(4)
    m[0, 0], m[0, 2], m[2, 0], m[2, 2] = c, s, -s, c
    return m


def rot_z(deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    m = np.eye(4)
    m[0, 0], m[0, 1], m[1, 0], m[1, 1] = c, -s, s, c
    return m


def scale(k):
    m = np.eye(4)
    m[0, 0] = m[1, 1] = m[2, 2] = k
    return m


def euler(rot):
    """[yaw, pitch, roll] in degrees -> 4x4: yaw clockwise seen from above, pitch = lean forward (towards +y),
    roll = lean to the right (+x). Applied roll first, then pitch, then yaw."""
    yaw, pitch, roll = rot
    return rot_z(-yaw) @ rot_x(-pitch) @ rot_y(roll)


def unit(v):
    v = np.asarray(v, float)
    n = np.linalg.norm(v)
    return v / n if n > 1e-9 else v


def frame(origin, y_axis, z_hint):
    """4x4 with +Y along y_axis and +Z as close to z_hint as an orthonormal frame allows."""
    y = unit(y_axis)
    z = np.asarray(z_hint, float) - np.dot(z_hint, y) * y
    if np.linalg.norm(z) < 1e-6:
        z = np.cross(y, [1.0, 0.0, 0.0])
        if np.linalg.norm(z) < 1e-6:
            z = np.cross(y, [0.0, 1.0, 0.0])
    z = unit(z)
    x = np.cross(y, z)
    m = np.eye(4)
    m[:3, 0], m[:3, 1], m[:3, 2], m[:3, 3] = x, y, z, origin
    return m


def rotate_about(v, axis, deg):
    """Rodrigues: v turned about the unit axis."""
    a = math.radians(deg)
    axis = unit(axis)
    return v * math.cos(a) + np.cross(axis, v) * math.sin(a) + axis * np.dot(axis, v) * (1 - math.cos(a))


def ik2(start, target, l1, l2, pole):
    """Two-bone limb: -> (joint position, reached end position, pole direction used).
    pole: the side the joint (knee / elbow) bends towards; only its part across the limb counts."""
    d = np.asarray(target, float) - start
    dist = np.linalg.norm(d)
    dn = d / dist if dist > 1e-9 else np.array([0.0, 0.0, -1.0])
    dist = min(max(dist, abs(l1 - l2) + 1e-4), l1 + l2 - 1e-4)
    a = (l1 * l1 - l2 * l2 + dist * dist) / (2 * dist)
    h = math.sqrt(max(l1 * l1 - a * a, 0.0))
    p = np.asarray(pole, float) - np.dot(pole, dn) * dn
    if np.linalg.norm(p) < 1e-6:
        p = np.cross(dn, [1.0, 0.0, 0.0])
        if np.linalg.norm(p) < 1e-6:
            p = np.cross(dn, [0.0, 1.0, 0.0])
    p = unit(p)
    return start + a * dn + h * p, start + dist * dn, p


def ik2_ground(start, target, l1, l2, pole, z_min):
    """ik2, but the joint never goes under z_min (the ground): if the pole would put it there, the limb is
    swivelled about its own axis - by as little as needed - until the joint is clear (a knee drawn up beside a
    lying body, an elbow planted next to the chest)."""
    j, end, p = ik2(start, target, l1, l2, pole)
    if j[2] >= z_min:
        return j, end, p
    d = unit(end - start)
    best = (j[2], j, end, p)
    for step in range(1, 13):
        for sgn in (1.0, -1.0):
            p2 = rotate_about(p, d, sgn * 15.0 * step)
            j2, end2, p2 = ik2(start, target, l1, l2, p2)
            if j2[2] >= z_min:
                return j2, end2, p2
            if j2[2] > best[0]:
                best = (j2[2], j2, end2, p2)
    return best[1], best[2], best[3]


# ----------------------------------------------------------------------------------------------
# Skeleton


DEFAULT_B = dict(
    hip_z=9.6,            # hip joints above the ground in the neutral stand
    hip_w=1.25,           # half the distance between the hip joints
    thigh=4.5, shin=4.1,  # hip -> knee, knee -> ankle
    ankle_z=1.25,         # ankle above the sole
    stance=1.5,           # half the distance between the feet in the neutral stand
    waist=1.2,            # pelvis centre -> the chest's pivot (the waist)
    neck=(0.0, 0.15, 5.6),        # chest pivot -> the neck's base
    shoulder=(2.45, 0.0, 4.55),   # chest pivot -> the right shoulder joint (the left is mirrored)
    upper_arm=3.3, forearm=3.1,   # shoulder -> elbow, elbow -> wrist
    grip=(0.0, 0.0, 0.0),         # the rifle frame's origin is the grip (the trigger hand's wrist)
    fore=(0.0, 3.0, 0.1),         # the fore-end hold in the rifle's frame
    muzzle=(0.0, 5.9, 0.25),      # the flash bone's origin in the rifle's frame
    fx=(),                        # names of extra bones under the root (blood pools, debris, ...)
)


def neutral_pose(B):
    """The neutral stand: upright, feet under the hips, rifle held level in front of the body, pointing forward."""
    return {
        "pelvis": {"pos": [0.0, 0.0, B["hip_z"]], "rot": [0.0, 0.0, 0.0]},
        "chest": {"rot": [0.0, 0.0, 0.0]},
        "head": {"rot": [0.0, 0.0, 0.0]},
        "foot.L": {"pos": [-B["stance"], 0.0, B["ankle_z"]], "rot": [0.0, 0.0, 0.0], "knee": 0.0},
        "foot.R": {"pos": [B["stance"], 0.0, B["ankle_z"]], "rot": [0.0, 0.0, 0.0], "knee": 0.0},
        "rifle": {"space": "chest", "pos": [1.3, 1.6, 1.4], "rot": [0.0, 0.0, 0.0], "hold": "R"},
        "hand.L": {"space": "chest", "pos": [-2.6, 0.6, -0.9], "elbow": 0.0},
        "hand.R": {"space": "chest", "pos": [2.6, 0.6, -0.9], "elbow": 0.0},
        "flash": 0,
        "fx": {},
    }


def merge(base, over):
    """Deep copy of `base` with the entries of `over` laid on top (dicts merge, everything else replaces)."""
    out = copy.deepcopy(base)
    for k, v in (over or {}).items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = merge(out[k], v)
        else:
            out[k] = copy.deepcopy(v)
    return out


def get_path(pose, path):
    """pose value at "foot.L/pos/2" (parts separated by '/')."""
    cur = pose
    for p in path.split("/"):
        cur = cur[int(p)] if isinstance(cur, list) else cur[p]
    return cur


def set_path(pose, path, value):
    parts = path.split("/")
    cur = pose
    for p, nxt in zip(parts[:-1], parts[1:]):
        if isinstance(cur, list):
            cur = cur[int(p)]
        else:
            if p not in cur:
                cur[p] = [0.0, 0.0, 0.0] if nxt.isdigit() else {}
            cur = cur[p]
    if isinstance(cur, list):
        cur[int(parts[-1])] = value
    else:
        cur[parts[-1]] = value


class Skeleton:
    def __init__(self, B=None):
        self.B = dict(DEFAULT_B)
        self.B.update(B or {})
        self.bones = list(BONES) + list(self.B["fx"])
        self.parent = dict(PARENT)
        for n in self.B["fx"]:
            self.parent[n] = "root"
        self.index = {n: i for i, n in enumerate(self.bones)}
        self.neutral = neutral_pose(self.B)

    # -- forward kinematics ------------------------------------------------------------------
    def solve(self, pose=None):
        """-> {bone: 4x4} in model space (px), for a pose given as overrides of the neutral stand."""
        B = self.B
        P = merge(self.neutral, pose)
        M = {"root": np.eye(4)}
        pel = translate(P["pelvis"]["pos"]) @ euler(P["pelvis"]["rot"])
        M["pelvis"] = pel
        chest = pel @ translate([0.0, 0.0, B["waist"]]) @ euler(P["chest"]["rot"])
        M["chest"] = chest
        M["head"] = chest @ translate(B["neck"]) @ euler(P["head"]["rot"])
        # rifle
        r = P["rifle"]
        if r.get("space", "chest") == "shoulder":      # carried with the chest, aimed in the root frame (a level rifle
            at = chest @ np.array([r["pos"][0], r["pos"][1], r["pos"][2], 1.0])      # whatever the torso does)
            rifle = translate(at[:3]) @ euler(r["rot"])
        else:
            space = chest if r.get("space", "chest") == "chest" else M["root"]
            rifle = space @ translate(r["pos"]) @ euler(r["rot"])
        M["rifle"] = rifle
        vis = float(P.get("flash") or 0.0) or TINY          # the flash's size (1 = as modelled); 0 hides it
        M["flash"] = rifle @ translate(B["muzzle"]) @ scale(vis / TINY)
        # arms
        hold = r.get("hold")
        for side, sx in (("L", -1.0), ("R", 1.0)):
            sh = (chest @ np.array([sx * B["shoulder"][0], B["shoulder"][1], B["shoulder"][2], 1.0]))[:3]
            h = P["hand." + side]
            if hold in ("L", "R"):
                pt = B["grip"] if side == hold else r.get("fore", B["fore"])
                tgt = (rifle @ np.array([pt[0], pt[1], pt[2], 1.0]))[:3]
            else:
                sp = chest if h.get("space", "chest") == "chest" else M["root"]
                tgt = (sp @ np.array([h["pos"][0], h["pos"][1], h["pos"][2], 1.0]))[:3]
            # elbow: down and back relative to the chest, swivelled outwards by `elbow` degrees - or towards an
            # explicit `pole` (root frame); never under the ground
            tgt = np.array([tgt[0], tgt[1], max(tgt[2], GROUND["wrist"])])
            if "pole" in h:
                pole = np.asarray(h["pole"], float)
            else:
                pole = chest[:3, :3] @ np.array([sx * 0.35, -0.6, -1.0])
                pole = rotate_about(pole, unit(tgt - sh), -sx * h.get("elbow", 0.0))
            j, end, p = ik2_ground(sh, tgt, B["upper_arm"], B["forearm"], pole, GROUND["elbow"])
            M["upper_arm." + side] = frame(sh, j - sh, p)
            M["forearm." + side] = frame(j, end - j, p)
        # legs
        for side, sx in (("L", -1.0), ("R", 1.0)):
            hip = (pel @ np.array([sx * B["hip_w"], 0.0, 0.0, 1.0]))[:3]
            f = P["foot." + side]
            in_pelvis = f.get("space", "root") == "pelvis"        # a body off the ground: feet follow the pelvis
            tgt = np.asarray(f["pos"], float)
            if in_pelvis:
                tgt = (pel @ np.array([tgt[0], tgt[1], tgt[2], 1.0]))[:3]
            tgt = np.array([tgt[0], tgt[1], max(tgt[2], GROUND["ankle"])])
            if "pole" in f:                                            # explicit knee direction (root frame)
                pole = np.asarray(f["pole"], float)
            else:
                fwd = pel[:3, :3] @ np.array([sx * 0.12, 1.0, 0.25])   # knees forward, slightly outwards
                pole = rotate_about(fwd, unit(tgt - hip), -sx * f.get("knee", 0.0))
            j, end, p = ik2_ground(hip, tgt, B["thigh"], B["shin"], pole, GROUND["knee"])
            M["thigh." + side] = frame(hip, j - hip, p)
            M["shin." + side] = frame(j, end - j, p)
            att = euler(f.get("rot", [0.0, 0.0, 0.0]))
            if in_pelvis:
                att = np.block([[pel[:3, :3], np.zeros((3, 1))], [np.zeros((1, 3)), np.ones((1, 1))]]) @ att
            M["foot." + side] = translate(end) @ att
        if not P.get("body", 1):          # the body gone (burst into pieces): collapsed to a point under the ground
            for n in BONES:
                if n not in ("root", "flash"):
                    M[n] = translate([0.0, 0.0, -0.5]) @ scale(TINY)
        # fx bones: hidden (TINY) unless the pose shows them
        fx = P.get("fx") or {}
        for n in B["fx"]:
            e = fx.get(n)
            if e is None:
                M[n] = scale(1.0)
            else:
                M[n] = translate(e.get("pos", [0.0, 0.0, 0.0])) @ euler(e.get("rot", [0.0, 0.0, 0.0])) @ \
                    np.diag([*(np.atleast_1d(e.get("scale", 1.0)) * np.ones(3) / TINY), 1.0])
        return M

    def matrices(self, pose=None):
        """[bones, 4, 4] in the order of self.bones."""
        M = self.solve(pose)
        return np.stack([M[n] for n in self.bones])

    def rest(self):
        return self.solve(None)


# ----------------------------------------------------------------------------------------------
# What a pose must not do (the fitter pays for it, tools/models/infantry_check/limbcheck.py lists it)


def seg_dist(p1, q1, p2, q2):
    """Shortest distance between the segments p1-q1 and p2-q2."""
    d1, d2, r = q1 - p1, q2 - p2, p1 - p2
    a, e, f = d1 @ d1, d2 @ d2, d2 @ r
    if a < 1e-9 and e < 1e-9:
        return float(np.linalg.norm(r))
    if a < 1e-9:
        sa, t = 0.0, min(max(f / e, 0.0), 1.0)
    else:
        c = d1 @ r
        if e < 1e-9:
            sa, t = min(max(-c / a, 0.0), 1.0), 0.0
        else:
            b = d1 @ d2
            den = a * e - b * b
            sa = min(max((b * f - c * e) / den, 0.0), 1.0) if den > 1e-9 else 0.0
            t = (b * sa + f) / e
            if t < 0:
                sa, t = min(max(-c / a, 0.0), 1.0), 0.0
            elif t > 1:
                sa, t = min(max((b - c) / a, 0.0), 1.0), 1.0
    return float(np.linalg.norm(p1 + d1 * sa - (p2 + d2 * t)))


def _angle(a, b):
    return math.degrees(math.acos(min(1.0, max(-1.0, float(a @ b) / max(1e-9, float(np.linalg.norm(a) * np.linalg.norm(b)))))))


def pose_faults(skel, pose, M=None):
    """-> (penalty, [(amount, text)]) for a pose: limbs that are wrong on their own terms.
      reach   a hand off the rifle it should hold, a hand or foot short of its target
      cross   a forearm, the rifle or a shin running through the torso; the legs through each other
      knee    a knee bending backwards (its apex more than 110 deg from the pelvis' belly side)
      fold    a knee or elbow folded shut
    The penalty is 0 for a clean pose and is the sum of the squared amounts (an amount of 1 = about half a pixel
    of violation)."""
    B = skel.B
    P = merge(skel.neutral, pose)
    if not P.get("body", 1):
        return 0.0, []
    M = M or skel.solve(pose)
    pos = {n: M[n][:3, 3] for n in BONES}
    pel, chest, rifle = M["pelvis"], M["chest"], M["rifle"]
    spine0 = pos["pelvis"] + (pos["chest"] - pos["pelvis"]) * 0.5
    spine1 = (chest @ np.array([0.0, 0.0, B["neck"][2] * 0.92, 1.0]))[:3]
    pen, out = 0.0, []

    def fault(kind, text, amount):
        nonlocal pen
        pen += amount * amount
        out.append((amount, kind + ": " + text))

    wrist, ankle = {}, {}
    hold = P["rifle"].get("hold")
    for side in ("L", "R"):
        hip, knee, ank = pos["thigh." + side], pos["shin." + side], pos["foot." + side]
        sh, elb = pos["upper_arm." + side], pos["forearm." + side]
        wr = (M["forearm." + side] @ np.array([0.0, B["forearm"], 0.0, 1.0]))[:3]
        wrist[side], ankle[side] = wr, ank
        # reach
        f = P["foot." + side]
        tgt = np.asarray(f["pos"], float)
        if f.get("space", "root") == "pelvis":
            tgt = (pel @ np.array([tgt[0], tgt[1], tgt[2], 1.0]))[:3]
        tgt = np.array([tgt[0], tgt[1], max(tgt[2], GROUND["ankle"])])
        d = float(np.linalg.norm(tgt - ank))
        if d > 0.15:
            fault("reach", f"foot.{side} is {d:.1f} px short of its target", (d - 0.15) / 0.4)
        if hold in ("L", "R"):
            pt = B["grip"] if side == hold else P["rifle"].get("fore", B["fore"])
            tg = (rifle @ np.array([pt[0], pt[1], pt[2], 1.0]))[:3]
            tg = np.array([tg[0], tg[1], max(tg[2], GROUND["wrist"])])
            d = float(np.linalg.norm(tg - wr))
            if d > 0.15:
                fault("reach", f"hand.{side} is {d:.1f} px off the rifle", (d - 0.15) / 0.3)
        # through the torso
        d = seg_dist(elb + (wr - elb) * 0.3, wr, spine0, spine1)
        if d < 1.45:
            fault("cross", f"forearm.{side} passes {d:.1f} px from the spine", (1.45 - d) / 0.4)
        d = seg_dist(knee + (ank - knee) * 0.2, ank, spine0, spine1)
        if d < 1.3:
            fault("cross", f"shin.{side} passes {d:.1f} px from the spine", (1.3 - d) / 0.4)
        # knee: which way it bends, how far it folds
        bend = _angle(knee - hip, ank - knee)
        if bend > 12.0:
            a = _angle(knee - (hip + ank) / 2, pel[:3, 1])
            if a > 110.0:
                fault("knee", f"knee.{side} bends {a:.0f} deg away from the front", (a - 110.0) / 20.0)
        if 180.0 - bend < 20.0:
            fault("fold", f"knee.{side} folded to {180.0 - bend:.0f} deg", (20.0 - (180.0 - bend)) / 8.0)
        eb = _angle(elb - sh, wr - elb)
        if 180.0 - eb < 14.0:
            fault("fold", f"elbow.{side} folded to {180.0 - eb:.0f} deg", (14.0 - (180.0 - eb)) / 6.0)
    if hold in ("L", "R"):      # a held rifle lies outside the torso
        butt = (rifle @ np.array([0.0, -2.4, -0.2, 1.0]))[:3]
        muzzle = (rifle @ np.array([0.0, 5.9, 0.2, 1.0]))[:3]
        d = seg_dist(butt, muzzle, spine0, spine1)
        if d < 1.55:
            fault("cross", f"the rifle passes {d:.1f} px from the spine", (1.55 - d) / 0.4)
    legs = ((pos["thigh.L"] + (pos["shin.L"] - pos["thigh.L"]) * 0.45, pos["shin.L"],
             pos["thigh.R"] + (pos["shin.R"] - pos["thigh.R"]) * 0.45, pos["shin.R"], "the thighs"),
            (pos["shin.L"], ankle["L"], pos["shin.R"], ankle["R"], "the shins"),
            (pos["thigh.L"] + (pos["shin.L"] - pos["thigh.L"]) * 0.45, pos["shin.L"], pos["shin.R"], ankle["R"], "left thigh / right shin"),
            (pos["thigh.R"] + (pos["shin.R"] - pos["thigh.R"]) * 0.45, pos["shin.R"], pos["shin.L"], ankle["L"], "right thigh / left shin"))
    for a_, b_, c_, d_, nm in legs:
        d = seg_dist(a_, b_, c_, d_)
        if d < 1.0:
            fault("cross", f"{nm} {d:.1f} px apart", (1.0 - d) / 0.4)
    return pen, out


# ----------------------------------------------------------------------------------------------
# Primitives -> meshes (the same vertices and faces go to Blender and to the preview point cloud)
#
# A part is a dict: bone, mat (material name), and one of
#   loft:  axis "x" | "y" | "z", rings [(t, c1, c2, r1, r2, n), ...], segs
#          a closed tube through super-ellipse rings: ring at `t` along the axis, centred (c1, c2) on the two
#          other axes (in the cyclic order x->yz, y->zx, z->xy), radii r1, r2, exponent n (2 = ellipse, 4+ = a
#          rounded box; default 2). Ends are capped.
#   box:   c (centre), s (size)  [r = corner rounding as a super-ellipse exponent]
#   ball:  c, r (radius or 3 radii)
#   splat: (radius, height) [seed, wobble, c]: a pool - a low dome over an irregular lobed outline
# optional: shade (COLOR_0 grey of the part, default 1), tm (4x4 list: extra transform inside the bone),
#           fx (True: modelled at TINY scale, see Skeleton), name


def superellipse(n_seg, r1, r2, n=2.0):
    a = np.arange(n_seg) * (2 * math.pi / n_seg)
    c, s = np.cos(a), np.sin(a)
    e = 2.0 / n
    return np.sign(c) * np.abs(c) ** e * r1, np.sign(s) * np.abs(s) ** e * r2


def loft_mesh(axis, rings, segs=10):
    verts, faces = [], []
    ax = "xyz".index(axis)
    i1, i2 = (ax + 1) % 3, (ax + 2) % 3
    for ring in rings:
        t, c1, c2, r1, r2 = ring[:5]
        n = ring[5] if len(ring) > 5 else 2.0
        u, v = superellipse(segs, r1, r2, n)
        p = np.zeros((segs, 3))
        p[:, ax] = t
        p[:, i1] = c1 + u
        p[:, i2] = c2 + v
        verts.append(p)
    nr = len(rings)
    for k in range(nr - 1):
        for i in range(segs):
            j = (i + 1) % segs
            faces.append((k * segs + i, k * segs + j, (k + 1) * segs + j, (k + 1) * segs + i))
    faces.append(tuple(range(segs - 1, -1, -1)))
    faces.append(tuple(range((nr - 1) * segs, nr * segs)))
    return np.concatenate(verts), faces


def box_mesh(c, s, n=None, segs=12):
    """A box; with n (super-ellipse exponent, e.g. 5) its horizontal section is rounded."""
    c, s = np.asarray(c, float), np.asarray(s, float) / 2
    if n:
        return loft_mesh("z", [(c[2] - s[2], c[0], c[1], s[0], s[1], n), (c[2] + s[2], c[0], c[1], s[0], s[1], n)], segs)
    v = np.array([[x, y, z] for z in (-1, 1) for y in (-1, 1) for x in (-1, 1)], float) * s + c
    f = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    return v, f


def ball_mesh(c, r, segs=10, rings=6):
    r = np.ones(3) * np.asarray(r, float)
    rs = []
    for k in range(1, rings):
        a = math.pi * k / rings
        rs.append((c[2] - math.cos(a) * r[2], c[0], c[1], math.sin(a) * r[0], math.sin(a) * r[1]))
    v, f = loft_mesh("z", rs, segs)
    # close the poles with fans
    n = len(v)
    v = np.concatenate([v, [[c[0], c[1], c[2] - r[2]], [c[0], c[1], c[2] + r[2]]]])
    f = f[:-2]
    for i in range(segs):
        j = (i + 1) % segs
        f.append((n, j, i))
        f.append((n + 1, (rings - 2) * segs + i, (rings - 2) * segs + j))
    return v, f


def splat_mesh(radius, height=0.14, seed=0, segs=28, wobble=0.3, c=(0.0, 0.0, 0.0)):
    """A pool: a low dome over an irregular, lobed outline (r varies with the angle), lying on z = c[2]."""
    rng = np.random.default_rng(seed)
    a = np.arange(segs) * (2 * math.pi / segs)
    r = np.ones(segs)
    for j in range(2, 7):
        r += wobble * rng.uniform(0.3, 1.0) / (j - 1) * np.cos(j * a + rng.uniform(0, 2 * math.pi))
    r *= radius / max(1e-6, r.mean())
    rings = [(0.0, 1.0), (height * 0.75, 0.9), (height, 0.62)]
    v = []
    for z, k in rings:
        v.append(np.stack([c[0] + np.cos(a) * r * k, c[1] + np.sin(a) * r * k, np.full(segs, c[2] + z)], 1))
    v = np.concatenate(v + [np.array([[c[0], c[1], c[2] + height * 1.05]])])
    f = []
    for k in range(len(rings) - 1):
        for i in range(segs):
            j = (i + 1) % segs
            f.append((k * segs + i, k * segs + j, (k + 1) * segs + j, (k + 1) * segs + i))
    top = len(rings) * segs
    for i in range(segs):
        f.append(((len(rings) - 1) * segs + i, (len(rings) - 1) * segs + (i + 1) % segs, top))
    f.append(tuple(range(segs - 1, -1, -1)))
    return v, f


def part_mesh(part):
    """-> (verts [N, 3] in the bone's frame, faces [tuples])."""
    if "splat" in part:
        v, f = splat_mesh(*part["splat"][:2], seed=part.get("seed", 0), wobble=part.get("wobble", 0.3), c=part.get("c", (0.0, 0.0, 0.0)))
    elif "loft" in part:
        v, f = loft_mesh(part.get("axis", "z"), part["loft"], part.get("segs", 10))
    elif "box" in part:
        v, f = box_mesh(part["box"][0], part["box"][1], part.get("n"), part.get("segs", 12))
    elif "ball" in part:
        v, f = ball_mesh(part["ball"][0], part["ball"][1], part.get("segs", 10), part.get("rings", 6))
    else:
        raise ValueError("unknown primitive: %r" % (part,))
    if "tm" in part:
        tm = np.asarray(part["tm"], float)
        v = v @ tm[:3, :3].T + tm[:3, 3]
    if part.get("fx"):
        v = v * TINY
    return v, f


def triangles(faces):
    out = []
    for f in faces:
        for k in range(1, len(f) - 1):
            out.append((f[0], f[k], f[k + 1]))
    return np.array(out, int)


def build_cloud(skel, parts, classes, density=14.0, seed=1):
    """Sample the parts' surfaces: -> dict(pos [N, 3] bone-local, nrm, bone [N], cls [N]).
    classes: material name -> class index (materials missing from it are skipped); density = points per px^2."""
    rng = np.random.default_rng(seed)
    pos, nrm, bone, cls = [], [], [], []
    for part in parts:
        k = classes.get(part["mat"])
        if k is None:
            continue
        v, f = part_mesh(part)
        if part.get("fx"):
            v = v / TINY            # the cloud keeps fx parts at their shown size: the fx matrix is divided back
        tri = triangles(f)
        a, b, c = v[tri[:, 0]], v[tri[:, 1]], v[tri[:, 2]]
        n = np.cross(b - a, c - a)
        area = np.linalg.norm(n, axis=1) / 2
        cnt = max(1, int(round(area.sum() * density)))
        pick = rng.choice(len(tri), cnt, p=area / area.sum())
        r1, r2 = rng.random(cnt), rng.random(cnt)
        s = np.sqrt(r1)
        w = np.stack([1 - s, s * (1 - r2), s * r2], 1)
        pos.append(a[pick] * w[:, :1] + b[pick] * w[:, 1:2] + c[pick] * w[:, 2:3])
        nn = n[pick] / np.maximum(np.linalg.norm(n[pick], axis=1, keepdims=True), 1e-12)
        nrm.append(nn)
        bone.append(np.full(cnt, skel.index[part["bone"]]))
        cls.append(np.full(cnt, k))
    return make_cloud(skel, np.concatenate(pos), np.concatenate(nrm), np.concatenate(bone), np.concatenate(cls))


def make_cloud(skel, pos, nrm, bone, cls):
    """Cloud dict sorted by bone (the renderer transforms one block per bone)."""
    order = np.argsort(bone, kind="stable")
    pos, nrm, bone, cls = pos[order].astype(np.float32), nrm[order].astype(np.float32), bone[order], cls[order].astype(np.int16)
    slices = {}
    for b in np.unique(bone):
        idx = np.nonzero(bone == b)[0]
        slices[int(b)] = (int(idx[0]), int(idx[-1]) + 1)
    fx_bone = [n in skel.B["fx"] or n == "flash" for n in skel.bones]
    return dict(pos=pos, nrm=nrm, bone=bone, cls=cls, slices=slices, fx_bone=fx_bone)


# ----------------------------------------------------------------------------------------------
# The game camera and the preview renderer


def project(p):
    """Model-space points [N, 3] (already turned to their direction) -> screen px (x right, y down) and depth
    (smaller = nearer the camera)."""
    return np.stack([p[:, 0], -(p[:, 1] + p[:, 2]) * S45], 1), (p[:, 1] - p[:, 2]) * S45


def turn(yaw_deg):
    """3x3: the model turned clockwise (seen from above) by yaw."""
    return rot_z(-yaw_deg)[:3, :3]


def render_classes(cloud, mats, yaw, shift=(0.0, 0.0), anchor=(0.0, 0.0), box=(-16, -20, 16, 8), ss=4, n_cls=8, grow=1.0):
    """Preview of one frame: -> coverage [n_cls, h, w] in 0..1 per ORIGINAL pixel (box = x0, y0, x1, y1 in px
    relative to the hotspot pixel's corner; anchor = the hotspot's offset from the model origin's projection).
    Hidden-surface removal by depth sorting ss x ss texels per pixel; back faces culled. grow: the model's scale
    about its origin (the model pack's models.ini scale)."""
    r = turn(yaw).astype(np.float32)
    n_pts = len(cloud["cls"])
    p = np.empty((n_pts, 3), np.float32)
    front = np.empty(n_pts, np.float32)
    cam = np.array([0.0, -S45, S45], np.float32)        # towards the camera (south and above)
    for b, (i0, i1) in cloud["slices"].items():
        m = mats[b]
        a = r @ m[:3, :3].astype(np.float32)
        if cloud["fx_bone"][b]:
            a = a * TINY                     # fx bones carry 1 / TINY (their parts are modelled TINY)
            if np.abs(a).max() < 0.02:       # hidden
                p[i0:i1] = 1e6
                front[i0:i1] = -1.0
                continue
        p[i0:i1] = (cloud["pos"][i0:i1] @ a.T + (r @ m[:3, 3].astype(np.float32))) * grow
        front[i0:i1] = cloud["nrm"][i0:i1] @ (a.T @ cam)
    keep = front > 0.0
    x = p[keep, 0] + shift[0]
    y = p[keep, 1] + shift[1]
    z = p[keep, 2]
    sx, sy, depth = x, -(y + z) * S45, (y - z) * S45
    x0, y0, x1, y1 = box
    w, h = (x1 - x0) * ss, (y1 - y0) * ss
    u = np.floor((sx - anchor[0] - x0) * ss).astype(np.int32)
    v = np.floor((sy - anchor[1] - y0) * ss).astype(np.int32)
    ok = (u >= 0) & (u < w) & (v >= 0) & (v < h)
    u, v, depth, c = u[ok], v[ok], depth[ok], cloud["cls"][keep][ok]
    order = np.argsort(-depth)                 # far first: the nearest point is written last
    img = np.full(h * w, -1, np.int16)
    img[v[order] * w + u[order]] = c[order]
    img = img.reshape(y1 - y0, ss, x1 - x0, ss)
    out = np.zeros((n_cls, y1 - y0, x1 - x0), np.float32)
    for k in np.unique(c):
        out[k] = (img == k).mean((1, 3))
    return out

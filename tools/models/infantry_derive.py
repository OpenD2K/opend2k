"""Shared base of the other infantry types (trooper, engineer, grenadier, Fremen, Sardaukar, saboteur, thumper).

Every type is the approved light infantry (infantry_v1.py: skeleton, animation set, frame plan, pipeline) with other
bodies / equipment / colours. A type's module (infantry_<type>.py) calls setup(), which re-points infantry_v1's
module globals (KEY, UNIT, POSES_FILE, parts, carry, colours, ...) at the type, and then stands in for it:

    import infantry_trooper as m        -> m is the configured infantry_v1 module (m.KEY == "TROOPER"), so the fitter
                                           and the measuring scripts (tools/models/infantry_check/, --char
                                           infantry_trooper, GEN=infantry_trooper) work unchanged
    blender -b --factory-startup --python tools/models/infantry_trooper.py -- --out DIR      builds the model

One process configures one type (the scripts are run per type). Nothing here contains game data.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import infantry_v1 as V  # noqa: E402

K = V.K
REF_LEGS = V.B["thigh"] + V.B["shin"]        # the reference's leg length (its fitted proportions)
L, Bx, Ball, stretch = V.L, V.Bx, V.Ball, V.stretch


def drop(parts, bones=(), mats=(), keep_fx=True):
    """The parts without those on the given bones / of the given materials (fx parts - blood, pieces - stay)."""
    out = []
    for p in parts:
        fx = p.get("fx") and p["bone"] != "flash"
        if (p["bone"] in bones or p["mat"] in mats) and not (keep_fx and fx):
            continue
        out.append(p)
    return out


def remat(parts, table, bones=None):
    """Parts with their materials renamed by table (only on `bones` if given)."""
    out = []
    for p in parts:
        if p["mat"] in table and (bones is None or p["bone"] in bones):
            p = dict(p)
            p["mat"] = table[p["mat"]]
        out.append(p)
    return out


def setup(name, key, unit, parts=None, carry=None, body=None, classes=None, col=None, spec=None, film=None,
          glow=None, uv=None, flash_rows=None, fit_dir=None, frame_hook=None, extra=None):
    """Configure infantry_v1 as the type `key` (Templates.bin unit `unit`); `name` = the type's module name.
    parts(base_parts) -> parts; carry(row, col) -> weapon pose; body = entries overriding body_dims (grip, fore,
    muzzle ...); classes / col / spec / film / glow = new materials; frame_hook(row, col, spec) -> spec may change
    a frame; extra = other module attributes."""
    V.KEY, V.UNIT = key, unit
    V.POSES_FILE = os.path.join(HERE, name + "_poses.json")
    V.load_fit.__defaults__ = (V.POSES_FILE,)
    if parts is not None:
        base_parts = V.parts
        V.parts = lambda: parts(base_parts())
    if carry is not None:
        V.carry = carry
    if body:
        base_dims = V.body_dims
        V.body_dims = lambda D: dict(base_dims(D), **body)
    V.CLASSES.update(classes or {})
    V.COL.update(col or {})
    V.SPEC.update(spec or {})
    V.FILM.update(film or {})
    V.GLOW.update(glow or {})
    V.UV_EXTRA = dict(uv or {})
    if flash_rows is not None:
        V.FLASH_ROWS = tuple(flash_rows)
    if fit_dir is not None:
        V.FIT_DIR = fit_dir
    if frame_hook is not None:
        base_spec = V.frame_spec

        def frame_spec(row, col, fit=None):
            s = base_spec(row, col, fit)
            return frame_hook(row, col, s, V.FIT if fit is None else fit)
        V.frame_spec = frame_spec
    for k, v in (extra or {}).items():
        setattr(V, k, v)
    if not getattr(V, "_limbs_scaled", False):
        # the deaths' limb sets are authored for the reference's legs: a type with shorter legs reaches less far
        base_limbs = V.limbs

        def limbs(kind):
            out = base_limbs(kind)
            f = min(1.0, (V.B["thigh"] + V.B["shin"]) / REF_LEGS)
            for side in ("foot.L", "foot.R"):
                e = out.get(side)
                if e and e.get("space") == "pelvis" and f < 1.0:
                    e["pos"] = [v * f for v in e["pos"]]
            return out
        V.limbs = limbs
        V._limbs_scaled = True
    V.FIT = V.load_fit()
    V._cloud = None
    V.set_dims(V.FIT.get("dims"))
    return V


# ---- types without a weapon in their hands (engineer, grenadier, saboteur): the arms are posed directly

NO_WEAPON = {"space": "chest", "pos": [0.0, 1.5, 0.0], "rot": [0.0, 0.0, 0.0], "hold": None}
# the fitter's per-direction parameters for free hands (instead of the rifle's place and aim)
FIT_DIR_HANDS = [
    ("shift_e", "shift", 0, 0.8, -8.0, 8.0), ("shift_n", "shift", 1, 0.8, -8.0, 8.0), ("yaw", "yaw", None, 4.0, -18.0, 18.0),
    ("handL_x", "pose", "hand.L/pos/0", 0.5, -5.5, -1.6), ("handL_y", "pose", "hand.L/pos/1", 0.6, -3.0, 5.5),
    ("handL_z", "pose", "hand.L/pos/2", 0.6, -2.0, 7.5),
    ("handR_x", "pose", "hand.R/pos/0", 0.5, 1.6, 5.5), ("handR_y", "pose", "hand.R/pos/1", 0.6, -3.0, 5.5),
    ("handR_z", "pose", "hand.R/pos/2", 0.6, -2.0, 7.5),
    ("chest_twist", "pose", "chest/rot/0", 6.0, -40.0, 40.0), ("head_yaw", "pose", "head/rot/0", 8.0, -45.0, 45.0),
]


def hands(left, right, elbow=8.0):
    """Pose entries for two free hands (wrist targets in the chest's frame) and no weapon."""
    return {"rifle": dict(NO_WEAPON),
            "hand.L": {"space": "chest", "pos": list(left), "elbow": elbow},
            "hand.R": {"space": "chest", "pos": list(right), "elbow": elbow}}


def arm_swing(row, reach=2.2, rest=(2.7, 0.5, -1.0), out=0.0):
    """Free hands for the walk rows 1..6: each arm swings against its leg (reference walk: the right foot is
    planted forward in row 1, the left in row 4)."""
    k = row - 1
    s = (1.0, 0.3, -0.55)[k % 3] * (1.0 if k < 3 else -1.0)
    x, y, z = rest
    return hands([-x - out, y + reach * s, z + 0.5 * abs(s)], [x + out, y - reach * s, z + 0.5 * abs(s)])


def fidget_hands(row, col, spec, fit, raise_to=(1.3, 2.3, 5.3)):
    """Idle fidgets of a type with free hands (art rows 22-29, columns 5 and 6): the hands of the standing S frame;
    fidget 0 raises the right hand to the brow, fidget 1 looks left and right."""
    if spec is None or row < 22 or col not in (5, 6):
        return spec
    base = V.frame_spec(0, 4, fit)["pose"]
    lift = [0.0, 0.35, 0.8, 1.0, 1.0, 0.8, 0.35, 0.0][min(row - 22, 7)]
    pose = spec["pose"]
    for h in ("hand.L", "hand.R"):
        pose[h] = K.merge({}, base[h])
    pose["rifle"] = dict(NO_WEAPON)
    if col == 5:
        a = pose["hand.R"]["pos"]
        pose["hand.R"]["pos"] = [a[i] + (raise_to[i] - a[i]) * lift for i in range(3)]
        pose["head"] = {"rot": [0.0, base["head"]["rot"][1] + 8.0 * lift, 0.0]}
    else:
        turn = [0.0, 0.5, 1.0, 0.6, -0.4, -1.0, -0.5, 0.0][min(row - 22, 7)]
        pose["head"] = {"rot": [38.0 * turn, base["head"]["rot"][1], 0.0]}
        pose["chest"] = {"rot": [10.0 * turn, base["chest"]["rot"][1], 0.0]}
    return spec


def finish(module_name):
    """Last line of a type's module: build in Blender when run as a script, else stand in for infantry_v1."""
    if module_name == "__main__":
        try:
            import bpy  # noqa: F401
        except ImportError:
            raise SystemExit("run inside Blender: blender -b --factory-startup --python tools/models/infantry_<type>.py -- [options]")
        V.blender_main()
    else:
        sys.modules[module_name] = V

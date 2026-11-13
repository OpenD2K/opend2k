"""Shared kit of the Harkonnen buildings batch (tools/models/harkonnen_<name>.py).

The original Harkonnen set is, building by building, the Atreides set's layout re-dressed (the two houses' idle
pictures overlap 0.8-1.0 in outline for everything but the barracks): the same drums, scoops, vaults and domes, but
in dark gun-metal and blackened plating instead of cream, with heavier, more angular additions (double chimneys,
armoured collars, red domes) and red house panels where the Atreides carry blue bands. So this kit IS
tools/models/atreides_kit.py (all its mesh helpers, battle damage, bake and export; `from atreides_kit import *`)
with

  * the Harkonnen palette: the Atreides material names re-mapped to the tones read from the Harkonnen sprites
    (cream shells -> dark warm steel with a metallic sheen, walls -> grey plating, pale stone -> soot-dark stone;
    rust, copper and the near-blacks stay) plus a few own materials (hk_*);
  * dirtier weathering (more grime and streaks, sand film only low down);
  * helpers the Harkonnen pictures need: remat() (re-dress parts of an Atreides build by name), plate() (a
    house-colour armour panel on any surface point), stack() (the heavy double-collared chimney), dock_socket()
    (the refinery's harvester inlet), spikes;
  * run(): the same generator command line, output in artpacks/models-harkonnen-<name>.

A building script either re-dresses the Atreides generator of the same building
    import atreides_wind_trap as A;  built = A.build(damaged);  K.remat(...);  extra parts ...
or builds from scratch with the kit. Everything is in ORIGINAL SPRITE PIXELS (x east, y north of the footprint
centre, z up); the 45 degree camera puts a point at (OX + x, OY - 0.7071 (y + z)) on the canvas of
harkonnen_check/cfg.py.

    blender -b --factory-startup --python tools/models/harkonnen_<name>.py -- [--out DIR] [--no-bake] [--no-variants]
        [--no-damaged] [--no-poses] [--bake-size 2048]
    D2K_BUILDING=HARKONNEN_<NAME> .venv/bin/python -I tools/models/harkonnen_check/pack.py
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import atreides_kit as AK  # noqa: E402
from atreides_kit import *  # noqa: E402,F401,F403  (box, prism, arc, sweep, vault, tank, dome, cyl, pipe, flue, hole, ...)
from atreides_kit import V3, RF, COL as _ACOL  # noqa: E402,F401

# after the kit imports (atreides_kit puts tools/models first again): the frozen Atreides generators win
sys.path.insert(0, os.path.join(HERE, "harkonnen_base"))    # frozen Atreides generators (harkonnen_base/README.md)

# ------------------------------------------------------------------------------------------------
# Palette (sRGB, sunlit top face). Atreides names re-mapped + own names.

HCOL = dict(
    roof=(0.470, 0.405, 0.365),       # shells, vaults, drums' roofs: dark warm gun-metal
    roof2=(0.370, 0.325, 0.300),      # every other panel darker
    rib=(0.520, 0.400, 0.270),        # ribs: dull brass
    rim=(0.430, 0.280, 0.150),
    wall=(0.540, 0.510, 0.470),       # frames, cornices, plating: grey steel
    white=(0.800, 0.780, 0.720),      # pale concrete: berms, foot rings, steps
    cream_pipe=(0.660, 0.620, 0.540),
    skirt=(0.430, 0.320, 0.240),      # rusted skirts
    fin=(0.560, 0.450, 0.340),
    block=(0.450, 0.430, 0.405),      # machine blocks: grey
    block_dk=(0.300, 0.280, 0.265),
    stone=(0.420, 0.385, 0.360),      # soot-dark plating
    stone_dk=(0.310, 0.285, 0.270),
    concrete=(0.560, 0.545, 0.500),
    concrete_dk=(0.380, 0.360, 0.330),
    tan=(0.480, 0.350, 0.250),
    steel=(0.470, 0.465, 0.455),
    steel_dk=(0.270, 0.270, 0.275),
    floor=(0.300, 0.270, 0.240),
    # own
    hk_shell=(0.370, 0.335, 0.315),   # the darkest armour shells
    hk_black=(0.215, 0.200, 0.195),   # blackened plating (lit: dark grey)
    hk_plate=(0.600, 0.570, 0.520),   # pale worn plating / highlights
    hk_brass=(0.720, 0.520, 0.250),   # bright brass trim, hoops
    hk_gold=(0.950, 0.720, 0.250),    # golden bars / lamps housings
    hk_redglass=(0.800, 0.180, 0.150),  # fixed red domes (not house colour)
    hk_sand=(0.900, 0.740, 0.460),    # pale sand-stone (the Emperor's palace)
    hk_sand_dk=(0.640, 0.470, 0.270),
)
HSPEC = dict(
    roof=(0.55, 0.36, "steel"), roof2=(0.55, 0.42, "steel"), rib=(0.5, 0.4, "steel"), wall=(0.35, 0.45, "steel"),
    white=(0.0, 0.55, "paint"), block=(0.2, 0.5, "paint"), block_dk=(0.2, 0.55, "paint"), stone=(0.3, 0.5, "steel"),
    stone_dk=(0.3, 0.55, "steel"), tan=(0.3, 0.55, "steel"), cream_pipe=(0.3, 0.45, "steel"),
    hk_shell=(0.6, 0.34, "steel"), hk_black=(0.5, 0.4, "dark"), hk_plate=(0.4, 0.4, "steel"), hk_brass=(0.7, 0.32, "steel"),
    hk_gold=(0.6, 0.3, "dark"), hk_redglass=(0.2, 0.25, "dark"), hk_sand=(0.0, 0.7, "paint"), hk_sand_dk=(0.0, 0.75, "paint"),
)
_SAND = (0.60, 0.50, 0.38)


def _film(c, k=0.09):
    return tuple(round(a * (1 - k) + b * k, 3) for a, b in zip(c, _SAND))


def apply_palette():
    V3.COL.update(HCOL)
    for n, s in HSPEC.items():
        V3.SPEC[n] = s
    for n in list(V3.FILM):                       # every dust film follows its (re-mapped) base colour
        if n in V3.COL:
            V3.FILM[n] = _film(V3.COL[n])
    for n in ("hk_shell", "hk_plate", "hk_brass", "hk_sand", "hk_sand_dk", "stone", "stone_dk", "tan"):
        V3.FILM[n] = _film(V3.COL[n])
    # The refinery pilot's building weathering (sand drifted up the feet, pale run-off stains) is keyed by material
    # name and tuned for cream walls: on the Harkonnen darks it turned everything sand-brown. Dark plating gets
    # V3's own weathering only ("rim" is in none of the pilot's lists); pale concrete and sandstone keep the sand.
    for n in ("roof", "roof2", "wall", "block", "block_dk", "stone", "stone_dk", "steel", "steel_dk", "tan", "gun", "floor", "slab", "slab_dk",
              "hk_shell", "hk_plate", "hk_brass", "fin", "foot", "tank"):
        AK.ALIAS[n] = "rim"
    AK.ALIAS.update(hk_sand="wall", hk_sand_dk="wall", concrete="block", concrete_dk="block", rust="skirt")
    V3.UV_SCALE.update(hk_black=0.5, hk_gold=0.3, hk_redglass=0.3)
    # emission (combat_tank_v2.EMIT): the fixed red glass domes glow dimly at night
    V3.V2.EMIT.update(hk_redglass=(0.60, 0.07, 0.04))
    # dirtier and darker than the Atreides: more grime and streaks, chips on paint, grime on bare steel
    V3.WEATHER["paint"] = (0.55, 0.75, 0.95, 0.70, 1.0, 1.0, 0.70, 0.35)
    V3.WEATHER["steel"] = (0.35, 0.50, 0.92, 0.50, 0.80, 0.70, 0.60, 0.28)     # chips: pale bare-metal glints on the dark plates' edges
    V3.DIRT.update(sand=(0.470, 0.385, 0.280), soft=(0.300, 0.260, 0.220), blotch=(0.460, 0.390, 0.310), grime=(0.250, 0.200, 0.160), streak=(0.330, 0.230, 0.150))


apply_palette()

# ------------------------------------------------------------------------------------------------
# Helpers


def objs(built):
    """Every object of a build() result."""
    return [o for parts in built["nodes"].values() for o in parts]


def remat(parts, mapping, default=None):
    """Re-dress parts by name: mapping = {name prefix: material | ("HOUSE", shade)}; the longest matching prefix
    wins. HOUSE parts are left alone unless named."""
    keys = sorted(mapping, key=len, reverse=True)
    for o in parts:
        m = next((mapping[k] for k in keys if o.name.startswith(k)), default)
        if m is None:
            continue
        if isinstance(m, tuple):
            AK.house(o, m[1])
            continue
        if o.data.materials and o.data.materials[0] is not None and o.data.materials[0].name.startswith("HOUSE") and not any(o.name.startswith(k) for k in keys):
            continue
        o.data.materials.clear()
        o.data.materials.append(V3.mat(m))
        if "d2k_shade" in o:
            del o["d2k_shade"]


def drop(built, *prefixes):
    """Remove parts by name prefix from a build() result (their Blender objects too)."""
    import bpy
    for node, parts in built["nodes"].items():
        keep = []
        for o in parts:
            if any(o.name.startswith(p) for p in prefixes):
                bpy.data.objects.remove(o)
            else:
                keep.append(o)
        built["nodes"][node] = keep


def move(parts, prefixes, d=(0.0, 0.0, 0.0), scale=None, about=(0.0, 0.0, 0.0)):
    """Shift (and optionally scale about a point) parts by name prefix: meshes are in px before the node is joined."""
    from mathutils import Matrix, Vector
    if isinstance(prefixes, str):
        prefixes = (prefixes,)
    m = Matrix.Translation(Vector(d))
    if scale is not None:
        sc = (scale, scale, scale) if isinstance(scale, (int, float)) else scale
        m = m @ Matrix.Translation(Vector(about)) @ Matrix.Diagonal((*sc, 1.0)) @ Matrix.Translation(-Vector(about))
    for o in parts:
        if any((o.name == p[:-1]) if p.endswith("$") else o.name.startswith(p) for p in prefixes):      # "name$" = exactly that part
            o.data.transform(m)


def dress(built, house_k=0.5):
    """Finish a re-dressed Atreides build: the Harkonnen red reads far darker than the Atreides blue at the same
    grey, so every house part's shade is scaled by house_k."""
    for o in objs(built):
        if "d2k_shade" in o:
            o["d2k_shade"] = o["d2k_shade"] * house_k
    return built


def plate(name, c, ang, hl, hw, z, k=0.4, tilt=0.0, th=0.5, slope=0.0):
    """A house-colour armour panel lying on a surface: centre c (x, y), long side along ang (deg), half length hl,
    half width hw, top at z; tilt = how much higher its far (left of ang) edge is (px); slope the same along ang."""
    a = math.radians(ang)
    dx, dy = math.cos(a), math.sin(a)
    nx, ny = -dy, dx
    import bmesh
    lo, hi = [], []
    for (s, t) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        x, y = c[0] + dx * hl * s + nx * hw * t, c[1] + dy * hl * s + ny * hw * t
        zz = z + tilt * t * 0.5 + slope * s * 0.5
        lo.append((x, y, zz - th))
        hi.append((x, y, zz))
    bm = bmesh.new()
    AK.rings_solid(bm, [lo, hi])
    return AK.house(AK.new_obj(name, bm, "HOUSE"), k)


def stack(name, c, r, z0, z1, material="copper", collars=2, cap=True):
    """The Harkonnen chimney: a heavy pipe with thick dark collars, a flared sooty mouth. -> (objects, top point)."""
    out = [AK.tube(f"{name}_p", (c[0], c[1], z0), (c[0], c[1], z1), r, material, 12),
           AK.cyl(f"{name}_foot", c, r * 1.5, z0, z0 + 1.6, "copper_dk", 12, r * 1.2)]
    for i in range(collars):
        z = z0 + (z1 - z0) * (i + 1) / (collars + 1)
        out.append(AK.cyl(f"{name}_col{i}", c, r * 1.32, z - 0.8, z + 0.8, "copper_dk", 12))
    if cap:
        out.append(AK.cyl(f"{name}_lip", c, r * 1.2, z1 - 1.6, z1 + 0.1, "soot", 12, r * 1.4))
        out.append(AK.cyl(f"{name}_bore", c, r * 0.95, z1 - 0.5, z1 + 0.2, "burnt", 12))
    return out, (c[0], c[1], z1)


def dock_socket(name, mouth, z, d=(0.7071, -0.7071), length=4.0, r_out=2.7, r_in=2.15, r_lip=3.1, to=None, material="inlet"):
    """The refinery's harvester inlet (as tools/models/refinery_pilot_fable.py inlet()): a steel socket with a
    flared mouth and a dark bore whose mouth centre is at `mouth` (x, y), height z, opening towards d (the dock,
    where the docked harvester's pipe comes from); `to` (x, y, z) = where a feed pipe runs from its back."""
    import bmesh
    from mathutils import Vector
    d3 = Vector((d[0], d[1], 0.0)).normalized()
    side = Vector((d3.y, -d3.x, 0.0))
    o = Vector((mouth[0], mouth[1], z)) - d3 * length

    def ring(t, r, segs=18):
        return [tuple(o + d3 * t + (side * math.cos(2 * math.pi * i / segs) + Vector((0, 0, 1)) * math.sin(2 * math.pi * i / segs)) * r) for i in range(segs)]
    prof = [(0.0, r_in), (0.0, r_out), (length - 0.9, r_out), (length - 0.5, r_lip), (length, r_lip), (length, r_in)]
    bm = bmesh.new()
    rs = [[bm.verts.new(p) for p in ring(t, r)] for (t, r) in prof]
    for a, b in zip(rs, rs[1:] + rs[:1]):
        for i in range(len(a)):
            j = (i + 1) % len(a)
            bm.faces.new([a[i], a[j], b[j], b[i]])
    out = [AK.new_obj(f"{name}_socket", bm, material)]
    bm = bmesh.new()
    AK.rings_solid(bm, [ring(0.2, r_in + 0.02), ring(length - 1.6, r_in + 0.02)])
    out.append(AK.new_obj(f"{name}_bore", bm, "burnt"))
    bm = bmesh.new()
    AK.rings_solid(bm, [ring(length - 2.0, r_out + 0.18), ring(length - 1.45, r_out + 0.18)])
    out.append(AK.new_obj(f"{name}_band", bm, "copper_dk"))
    if to is not None:
        out.extend(AK.pipe(f"{name}_feed", [tuple(o + d3 * 0.3), tuple(o - d3 * 1.2), to], r_out * 0.8, "copper"))
    return out


def spike(name, c, r, z0, z1, material="hk_black"):
    """A pointed finial."""
    return AK.cyl(name, c, r, z0, z1, material, 8, 0.05)


def run(key, build, poses=(), spec=None, P=None, rest=None, short=None):
    short = short or key.replace("HARKONNEN_", "").lower()
    if "--" not in sys.argv:
        sys.argv.append("--")
    if "--out" not in sys.argv:
        sys.argv += ["--out", os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-harkonnen-" + short)]
    AK.run(key, build, poses=poses, spec=spec, P=P, rest=rest)

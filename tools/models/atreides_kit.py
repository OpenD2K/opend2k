"""Shared kit of the Atreides buildings batch (tools/models/atreides_<name>.py): the mesh helpers, palette, weathering
and atlas bake of the approved refinery pilot (refinery_pilot_fable.py, itself on combat_tank_v3.py) plus what the
other buildings need: boxes, prisms, barrel vaults, lying tanks, domes, pipe runs, vents, lights, house bands,
generic battle damage, effect anchors, a turret node, animation poses, and the generator's command line.

A building script defines  build(damaged) -> dict(nodes={"main": [objs], "base": [...], "turret": [...], NAME: [...]},
                                                  origins={NAME: (x, y, z)}, anchors=[(name, (x, y, z), size)])
and calls  run(KEY, build, poses=[...], spec={...}).  Everything is authored in ORIGINAL SPRITE PIXELS
(1 px = 1/32 tile; x east, y north of the footprint centre, z up); the 45 degree game camera puts a point at
(OX + x, OY - 0.7071 (y + z)) on the canvas of atreides_check/cfg.py (the "v = y + z" ruler of its brief sheets).

    blender -b --factory-startup --python tools/models/atreides_<name>.py -- [--out DIR] [--no-bake] [--no-variants]
        [--no-damaged] [--no-poses] [--bake-size 2048]

writes DIR/models/building/KEY.glb (default DIR = artpacks/models-atreides-<name>) and DIR/variants/:
  KEY_split.glb          only with a "base" node: the main node as HOUSE = the mask that cuts the one render into the
                         game's base (under units) and main frames (refinery pilot method)
  KEY_damaged[_split].glb  the damaged state
  KEY_poseNN.glb         animation poses: the baked model with its named nodes moved by poses[k](nodes) - the pack
                         driver renders each and keeps what differs from the idle render as the overlay frame
  spec.json              what the pack driver needs to know (pose -> animation frame, glow regions, turret)

Design language (read from the original Atreides set): warm cream / white rounded shells (barrel vaults, lying tanks,
domes) on rust-brown machinery and grey-beige blocks, thin house-colour bands and panels (about 6-10 % of the
picture), copper pipes and flues, near-black recesses; everything dusty, stained, sand drifted against the feet.
"""
import json
import math
import os
import random
import sys
import time

import bmesh
import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import combat_tank_v2 as V2  # noqa: E402
import combat_tank_v3 as V3  # noqa: E402
import refinery_pilot_fable as RF  # noqa: E402  (its import sets the building palette / weathering on V3)

PX = V3.PX
log = V2.log
lathe, lathe_loop, tube, beam, obox, polar, circle, star = RF.lathe, RF.lathe_loop, RF.tube, RF.beam, RF.obox, RF.polar, RF.circle, RF.star
rings_solid, new_obj, shade = V3.rings_solid, V3.new_obj, V3.shade

# additions to the refinery palette (sRGB, sunlit top face)
COL = dict(
    concrete=(0.560, 0.520, 0.450),   # foundations, aprons
    concrete_dk=(0.360, 0.330, 0.290),
    steel=(0.520, 0.520, 0.510),      # bare machinery
    steel_dk=(0.250, 0.250, 0.255),
    rust=(0.440, 0.270, 0.150),       # rusted machinery, darker than skirt
    glow=(0.300, 0.900, 0.930),       # cyan louvres / lights (kept clean)
    glow_dk=(0.060, 0.330, 0.400),
    lamp=(1.000, 0.960, 0.800),
    red=(0.750, 0.160, 0.100),
    gun=(0.300, 0.300, 0.310),        # barrels
    floor=(0.300, 0.260, 0.220),      # bay floors, pits
    stone=(0.640, 0.570, 0.450),      # weathered cream-brown masonry / plating (darker than wall)
    stone_dk=(0.430, 0.370, 0.290),
    green=(0.420, 0.900, 0.380),      # the turrets' green lamps
    tan=(0.760, 0.600, 0.440),        # the rocket turret's launcher boxes
    slab=(0.500, 0.480, 0.500),       # landing / repair pad floor plates: grey with a violet cast
    slab_dk=(0.340, 0.330, 0.350),
    cap=(0.230, 0.230, 0.240),        # the research centre's caps: neutral dark grey, hardly weathered
    cap_hi=(0.800, 0.720, 0.580),     # their warm highlight
    rock=(0.760, 0.470, 0.230),       # the orange sandstone boulders some originals stand among
    rock_dk=(0.520, 0.300, 0.150),
)
SPEC = dict(
    concrete=(0.0, 0.7, "paint"), concrete_dk=(0.0, 0.75, "paint"), steel=(0.7, 0.4, "steel"), steel_dk=(0.6, 0.45, "steel"),
    rust=(0.3, 0.6, "steel"), glow=(0.0, 0.3, "dark"), glow_dk=(0.0, 0.4, "dark"), lamp=(0.0, 0.3, "dark"), red=(0.0, 0.5, "dark"),
    gun=(0.7, 0.35, "steel"), floor=(0.0, 0.8, "paint"),
    stone=(0.0, 0.6, "paint"), stone_dk=(0.0, 0.65, "paint"), rock=(0.0, 0.85, "paint"), rock_dk=(0.0, 0.9, "paint"),
    green=(0.0, 0.3, "dark"), tan=(0.0, 0.5, "paint"), cap=(0.2, 0.45, "dark"), cap_hi=(0.0, 0.4, "dark"), slab=(0.1, 0.6, "paint"), slab_dk=(0.1, 0.65, "paint"),
)
FILM = dict(slab=(0.470, 0.440, 0.440), slab_dk=(0.330, 0.310, 0.320), concrete=(0.520, 0.470, 0.390), concrete_dk=(0.350, 0.310, 0.260), steel=(0.450, 0.430, 0.400), steel_dk=(0.250, 0.240, 0.230),
            stone=(0.560, 0.490, 0.380), stone_dk=(0.400, 0.340, 0.260), rust=(0.400, 0.270, 0.170), gun=(0.300, 0.290, 0.280), floor=(0.330, 0.280, 0.220))
V3.COL.update(COL)
V3.SPEC.update(SPEC)
V3.FILM.update(FILM)
V3.UV_SCALE.update(glow=0.4, glow_dk=0.4, lamp=0.2, red=0.2, floor=0.6)
# Emission (combat_tank_v2.EMIT, docs/3d-models.md "Emission"; linear RGB, brightest channel = strength): the
# building lights glow at night - the cyan louvres / light strips (wind trap energy cores, research centre), the
# lamp beads, the red and green warning lamps.
V3.V2.EMIT.update(glow=(0.20, 0.85, 1.00), glow_dk=(0.03, 0.22, 0.30), lamp=(1.00, 0.80, 0.50), red=(0.90, 0.10, 0.05),
                  green=(0.20, 0.90, 0.25))



# The refinery's building weathering (sand drifted up the feet, run-off streaks, stains, specks) is keyed by its
# material names: the kit's own materials borrow it through these aliases (the base colour stays their own).
ALIAS = dict(stone="wall", stone_dk="block", slab="block", slab_dk="block", concrete="block", concrete_dk="block", floor="block",
             tan="wall", rust="skirt", steel="skirt", steel_dk="skirt", rock="wall", rock_dk="wall", gun="skirt")
_RF_SURFACE, _V3_SURFACE = RF.surface, RF._V3_SURFACE


def _surface(nt, name):
    RF._V3_SURFACE = lambda nt_, _n: _V3_SURFACE(nt_, name)
    try:
        return _RF_SURFACE(nt, ALIAS.get(name, name))
    finally:
        RF._V3_SURFACE = _V3_SURFACE


RF.surface = _surface
# dirtier than the vehicles: more grime, streaks and chipping on painted surfaces, grime on bare steel
V3.WEATHER["paint"] = (0.80, 0.80, 0.95, 0.75, 1.0, 0.95, 0.60, 0.75)
V3.WEATHER["steel"] = (0.75, 0.45, 0.90, 0.55, 0.65, 0.55, 0.0, 0.50)

# ------------------------------------------------------------------------------------------------
# Mesh helpers (px)


def box(name, x0, x1, y0, y1, z0, z1, material, bev=0.0, top=1.0):
    """Axis-aligned box; `top` scales the top face about its centre (a batter)."""
    return obox(name, ((x0 + x1) / 2, (y0 + y1) / 2), 0.0, (x1 - x0) / 2, (y1 - y0) / 2, z0, z1, material, bev, top)


def prism(name, pts, z0, z1, material, bev=0.0, inset_top=0.0):
    """Vertical prism over the polygon pts (x, y), counter-clockwise; inset_top pulls the top ring towards the centroid."""
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)

    def ring(z, k):
        return [(cx + (x - cx) * k, cy + (y - cy) * k, z) for (x, y) in pts]
    bm = bmesh.new()
    r = max(math.hypot(x - cx, y - cy) for (x, y) in pts)
    rings_solid(bm, [ring(z0, 1.0), ring(z1, 1.0 - inset_top / max(r, 1e-6))])
    return new_obj(name, bm, material, bev)


def arc(name, c, r0, r1, a0, a1, z0, z1, material, n=12, bev=0.0, z1b=None):
    """A curved wall / band: the ring sector between radii r0 < r1 from angle a0 to a1 (deg), z0..z1 (z1b: the top
    height at the a1 end, for a ramp)."""
    z1b = z1 if z1b is None else z1b
    outer = [polar(c, r1, a0 + (a1 - a0) * i / n) for i in range(n + 1)]
    inner = [polar(c, r0, a1 + (a0 - a1) * i / n) for i in range(n + 1)]
    zt = [z1 + (z1b - z1) * i / n for i in range(n + 1)]
    bm = bmesh.new()
    rings_solid(bm, [[(x, y, z0) for (x, y) in outer + inner],
                     [(x, y, z) for (x, y), z in zip(outer, zt)] + [(x, y, z) for (x, y), z in zip(inner, zt[::-1])]])
    return new_obj(name, bm, material, bev)


def loop_ring(name, rings, material, segs=48):
    """A closed ring lofted through circles [(centre (x, y), r, z)] - like lathe_loop, but every circle has its own
    centre (an eccentric berm: thick on one side, thin on the other)."""
    bm = bmesh.new()
    vs = [[bm.verts.new(p) for p in circle(c[0], c[1], r, z, segs)] for (c, r, z) in rings]
    for a, b in zip(vs, vs[1:] + vs[:1]):
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new([a[i], a[j], b[j], b[i]])
    return new_obj(name, bm, material)


def ramp(name, pts_low, pts_high, z0, z_low, z_high, material, bev=0.0):
    """A wedge: quad pts (4 x (x, y)); the edge pts[0]-pts[1] is at z_low, pts[2]-pts[3] at z_high; bottom at z0."""
    q = list(pts_low) + list(pts_high)
    bm = bmesh.new()
    rings_solid(bm, [[(x, y, z0) for (x, y) in q],
                     [(q[0][0], q[0][1], z_low), (q[1][0], q[1][1], z_low), (q[2][0], q[2][1], z_high), (q[3][0], q[3][1], z_high)]])
    return new_obj(name, bm, material, bev)


def _frame(a, b):
    a, b = Vector(a), Vector(b)
    d = (b - a).normalized()
    side = d.cross(Vector((0, 0, 1)))
    if side.length < 1e-6:
        side = Vector((1, 0, 0))
    side.normalize()
    up = side.cross(d)
    return a, b, d, side, up


def sweep(name, a, b, profile, material, segs=20, bev=0.0, squash=1.0, arc=(0.0, 360.0), point=0.0):
    """Solid of revolution about the axis a -> b (3D points): profile = [(t, r)] with t 0..1 along the axis.
    squash scales the section along the axis' "up" direction; arc (deg, 0 = side, 90 = up) limits the section
    (a closed D-shape for a vault: arc=(0, 180)). point > 0 makes the section a pointed arch: its height becomes
    1 - |cos|^point (1 = a triangle, 1.5 = a pointed arch) instead of sin."""
    a, b, d, side, up = _frame(a, b)
    full = arc[1] - arc[0] >= 359.9
    n = segs if full else segs + 1
    rings = []
    for (t, r) in profile:
        p = a + (b - a) * t
        ring = []
        for i in range(n):
            ang = math.radians(arc[0] + (arc[1] - arc[0]) * i / (segs if not full else segs))
            hgt = math.sin(ang) if point <= 0 else math.copysign(1.0 - abs(math.cos(ang)) ** point, math.sin(ang))
            ring.append(tuple(p + side * (math.cos(ang) * max(r, 0.02)) + up * (hgt * max(r, 0.02) * squash)))
        rings.append(ring)
    bm = bmesh.new()
    rings_solid(bm, rings)
    return new_obj(name, bm, material, bev)


def vault(name, a, b, r, z0, material, segs=14, h=None, bev=0.0, ends=0.0):
    """Barrel vault: half cylinder lying on z0 along a -> b (x, y points), radius r across, height h (default r).
    ends > 0 rounds the two ends in over that length."""
    h = r if h is None else h
    L = math.hypot(b[0] - a[0], b[1] - a[1])
    prof = [(0.0, r), (1.0, r)]
    if ends > 0:
        e = ends / L
        prof = [(0.0, r * 0.55), (e * 0.35, r * 0.85), (e, r), (1 - e, r), (1 - e * 0.35, r * 0.85), (1.0, r * 0.55)]
    return sweep(name, (a[0], a[1], z0), (b[0], b[1], z0), prof, material, segs, bev, squash=h / r, arc=(0.0, 180.0))


def tank(name, a, b, r, material, segs=20, cap=0.3, bev=0.0):
    """Lying (or standing) tank: cylinder a -> b (3D) with dished ends (cap = dish length / radius)."""
    L = (Vector(b) - Vector(a)).length
    e = min(0.45, cap * r / L)
    prof = [(0.0, r * 0.35), (e * 0.3, r * 0.74), (e * 0.65, r * 0.93), (e, r), (1 - e, r), (1 - e * 0.65, r * 0.93), (1 - e * 0.3, r * 0.74), (1.0, r * 0.35)]
    return sweep(name, a, b, prof, material, segs, bev)


def hoop(name, a, b, t0, t1, r, material, segs=20, arc=(0.0, 360.0), squash=1.0):
    """A band round a sweep / tank / vault: from t0 to t1 along the axis a -> b at radius r (use r + 0.2..0.4)."""
    return sweep(name, a, b, [(t0, r), (t1, r)], material, segs, squash=squash, arc=arc)


def dome(name, c, r, z0, h, material, segs=24, steps=6):
    """Dome on z0: radius r, height h."""
    prof = [(r * math.cos(math.pi / 2 * i / steps), z0 + h * math.sin(math.pi / 2 * i / steps)) for i in range(steps)] + [(0.02, z0 + h)]
    return lathe(name, c, prof, material, segs)


def ball(name, c, r, material, segs=12):
    prof = [(max(0.02, r * math.sin(math.pi * i / 6)), c[2] - r * math.cos(math.pi * i / 6)) for i in range(7)]
    return lathe(name, (c[0], c[1]), prof, material, segs)


def cyl(name, c, r, z0, z1, material, segs=24, r1=None, bev=0.0):
    """Standing cylinder / cone."""
    bm = bmesh.new()
    rings_solid(bm, [circle(c[0], c[1], r, z0, segs), circle(c[0], c[1], r if r1 is None else r1, z1, segs)])
    return new_obj(name, bm, material, bev)


def pipe(name, pts, r, material, segs=10):
    """A pipe run through 3D points, with ball elbows. Returns a list of objects."""
    out = []
    for i, (p, q) in enumerate(zip(pts[:-1], pts[1:])):
        out.append(tube(f"{name}_{i}", p, q, r, material, segs))
        if i:
            out.append(ball(f"{name}_e{i}", p, r * 1.12, material, segs))
    return out


def flue(name, c, r, z0, z1, material="copper", collar="copper_dk"):
    """An open chimney: pipe, a collar, a dark bore and a sooty lip. Returns (objects, top point)."""
    out = [tube(f"{name}_p", (c[0], c[1], z0), (c[0], c[1], z1), r, material, 12),
           cyl(f"{name}_lip", c, r * 1.25, z1 - 1.2, z1 + 0.1, "soot", 12),
           cyl(f"{name}_bore", c, r * 0.8, z1 - 0.5, z1 + 0.2, "burnt", 12),
           cyl(f"{name}_col", c, r * 1.3, z0 + (z1 - z0) * 0.45, z0 + (z1 - z0) * 0.45 + 1.0, collar, 12)]
    return out, (c[0], c[1], z1)


def vent(name, c, ang, hl, z0, z1, depth=0.5, slats=3, frame="wall"):
    """A louvred vent on a wall: centre c (x, y) on the wall face, the wall runs along ang (deg), half length hl."""
    out = [obox(f"{name}_f", c, ang, hl + 0.5, depth, z0 - 0.4, z1 + 0.4, frame),
           obox(f"{name}_d", c, ang, hl, depth + 0.15, z0, z1, "recess")]
    for i in range(slats):
        z = z0 + (z1 - z0) * (i + 0.5) / slats
        out.append(obox(f"{name}_s{i}", c, ang, hl, depth + 0.3, z - 0.18, z + 0.18, "steel"))
    return out


def rocks(name, c, r, seed, n=5, h=0.7):
    """A pile of faceted boulders round c (x, y), spread r."""
    rnd = random.Random(seed)
    out = []
    for i in range(n):
        q = polar(c, r * rnd.uniform(0.0, 0.8), rnd.uniform(0, 360))
        rr = r * rnd.uniform(0.3, 0.55)
        pts = star(q, rr, seed * 17 + i, 7)
        top = star((q[0] + rnd.uniform(-0.2, 0.2) * rr, q[1] + rnd.uniform(-0.2, 0.2) * rr), rr * rnd.uniform(0.35, 0.6), seed * 19 + i, 7)
        bm = bmesh.new()
        hh = rr * h * rnd.uniform(0.8, 1.6)
        rings_solid(bm, [[(x, y, 0.0) for (x, y) in pts], [(q[0] + (x - q[0]) * 0.93, q[1] + (y - q[1]) * 0.93, hh * 0.55) for (x, y) in pts],
                         [(x, y, hh) for (x, y) in top]])
        out.append(new_obj(f"{name}_{i}", bm, "rock" if i % 3 else "rock_dk", rr * 0.12))
    return out


def lamp(name, p, r=0.7, material="lamp"):
    """A small light: a bright bead on a dark socket."""
    return [cyl(f"{name}_s", (p[0], p[1]), r * 1.3, p[2] - 0.5, p[2], "recess", 8), ball(f"{name}_b", (p[0], p[1], p[2] + r * 0.4), r, material, 8)]


def lit(name, base, rgb, k3d=1.0):
    """Register `name` as an emissive twin of material `base` (docs/3d-models.md "Emission"): the same colour, gloss,
    dust film, weathering and atlas share (every per-material table of the kits that knows `base` gets `name`), plus
    the emission `rgb` (linear; the brightest channel = strength). Swapping a part from `base` to the twin leaves the
    mesh, the UVs and the colour bake as they were: only the emission atlas changes, which graft_emission.py copies
    into the pack's approved glb.
    k3d: the 3D shader shows only the emission's strength, tinted with the ALBEDO's hue (the 2D glow layer shows
    `rgb` itself), so a warm light on a grey / black surface turns into a hard white patch there. With D2K_GLOW_3D=1
    in the environment (the run whose glb is grafted into the pack; the plain run feeds render_glow.py) the twin's
    emission is scaled by k3d: 1 for surfaces whose own colour is the light's (gold, copper, lamps), ~0.1 for lit
    grey windows, less for black holes."""
    if os.environ.get("D2K_GLOW_3D"):
        rgb = tuple(round(v * k3d, 4) for v in rgb)
    for mod in (V3, V3.V2, RF, sys.modules[__name__]):
        for key, table in list(vars(mod).items()):
            if isinstance(table, dict) and key not in ("EMIT", "_mats") and not key.startswith("__") and base in table and name not in table:
                table[name] = table[base]
    ALIAS.setdefault(name, ALIAS.get(base, base))
    V3.V2.EMIT[name] = rgb
    return name


def relight(parts, mapping):
    """Swap parts to emissive twins by name: mapping = {name prefix: twin} (the longest prefix wins; a prefix
    ending in "$" must match the whole name). The part's first non-HOUSE material slot is replaced; call it after
    every re-dress (remat) of the build. Returns the number of parts changed."""
    keys = sorted(mapping, key=len, reverse=True)
    n = 0
    for o in parts:
        k = next((k for k in keys if (o.name == k[:-1] if k.endswith("$") else o.name.startswith(k))), None)
        if k is None:
            continue
        me = o.data
        for i, m in enumerate(me.materials):
            if m is not None and not m.name.startswith("HOUSE"):
                me.materials[i] = V3.mat(mapping[k])
                n += 1
                break
    return n


def seam(name, p0, p1, w=0.35, d=0.3):
    """A near-black panel seam along p0 -> p1 lying on a surface (3D points on the surface)."""
    return beam(name, p0, p1, w, d, "recess")


def house(o, k=0.5):
    """Make an object a house-colour part of shade k (linear grey: 0.5 light band, 0.25 dark panel)."""
    o.data.materials.clear()
    o.data.materials.append(V3.mat("HOUSE"))
    return shade(o, k)


# ------------------------------------------------------------------------------------------------
# The gun turrets' shared pedestal (art 10 / 12: a round rust drum with a pale house-colour deck, steps in front,
# sandstone rubble at its foot; the 16 frames differ by the wall stubs that link it to neighbouring walls)

PEDESTAL = dict(c=(0.0, -4.0), r0=13.0, r1=9.2, h=21.0, deck=8.4)
LINK_NODES = ("link_e", "link_w", "link_s")
# which stubs each of the 16 wall-link frames shows (north stubs are hidden behind the drum), read from the frames
LINK_FRAMES = ["", "e", "w", "", "s", "s", "ew", "ew", "es", "sw", "esw", "e", "sw", "sw", "esw", "es"]
LINK_POSES = ["e", "w", "s", "ew", "es", "sw", "esw"]


def turret_pedestal(damaged=False):
    """-> (main parts, {link node: parts}, the turret pivot (x, y, z))."""
    c, r0, r1, h = PEDESTAL["c"], PEDESTAL["r0"], PEDESTAL["r1"], PEDESTAL["h"]
    main = [cyl("ped_foot", c, r0 + 0.8, 0.0, 2.2, "foot", 32),
            lathe("ped_drum", c, [(r0, 1.6), (r0 - 0.6, 5.0), (r1 + 0.9, h - 3.0), (r1, h)], "stone_dk", 32),
            lathe_loop("ped_band", c, [(r0 - 0.4, 5.0), (r0 - 0.1, 5.4), (r0 - 0.5, 7.0), (r0 - 0.9, 6.8)], "copper_dk", 32),
            cyl("ped_rim", c, r1 + 0.5, h - 0.6, h + 0.8, "copper_dk", 32)]
    main.append(house(cyl("ped_deck", c, PEDESTAL["deck"], h + 0.6, h + 1.6, "HOUSE", 32), 0.45))
    for k in range(12):                                  # buttress fins round the drum
        a = 15.0 + 30.0 * k
        main.append(beam(f"ped_fin{k}", polar(c, r0 + 0.5, a, 1.8), polar(c, r1 + 0.6, a, h - 1.0), 1.3, 1.0, "stone" if k % 2 else "block_dk", 0.1))
    main.append(beam("ped_steps", (c[0] - 0.5, c[1] - r1 - 0.5, h - 2.0), (c[0] - 0.5, c[1] - r0 - 2.0, 0.4), 3.6, 1.2, "wall", 0.2))
    for i in range(4):
        t = (i + 0.5) / 4
        main.append(box(f"ped_step{i}", c[0] - 2.4, c[0] + 1.4, c[1] - r1 - 1.2 - 3.0 * t * 1.1, c[1] - r1 - 0.4 - 3.0 * t * 1.1, (h - 2.0) * (1 - t) + 0.4, (h - 2.0) * (1 - t) + 1.2, "roof"))
    main.extend(vent("ped_vent", polar(c, r0 - 0.9, 300.0), 30.0, 2.0, 6.5, 9.5, 0.5, 2, "steel_dk"))
    main.extend(rocks("ped_rock_w", (c[0] - 11.0, c[1] - 7.0), 4.2, 3, 4))
    main.extend(rocks("ped_rock_e", (c[0] + 12.0, c[1] - 6.0), 3.4, 5, 3))
    links = {}
    for name, (x0, x1, y0, y1) in (("link_e", (c[0] + r0 - 3.0, 16.0, c[1] - 6.0, c[1] + 2.0)), ("link_w", (-16.0, c[0] - r0 + 3.0, c[1] - 6.0, c[1] + 2.0)),
                                   ("link_s", (c[0] - 4.5, c[0] + 3.5, -16.0, c[1] - r0 + 3.0))):
        links[name] = [box(f"{name}_wall", x0, x1, y0, y1, 0.0, 9.0, "stone_dk", 0.5, 0.92),
                       box(f"{name}_cap", x0 + 0.4, x1 - 0.4, y0 + 1.2, y1 - 1.2, 8.8, 10.0, "stone", 0.3)]
    if damaged:
        wall_gash(main, polar(c, r0 - 0.3, 250.0), 160.0, 4.0, 3.0, 11.0, 61, 0.9)
        scorch(main, (c[0] + 4.0, c[1] - 2.0), 4.0, h + 1.7, 62, 4)
    return main, links, (c[0], c[1], h + 1.6)


def link_pose(combo):
    def pose(nodes):
        for n in LINK_NODES:
            s = 1.0 if n[-1] in combo else 0.001
            nodes[n].scale = (s, s, s)
    return pose


# ------------------------------------------------------------------------------------------------
# Battle damage (the refinery pilot's rules: ragged holes, the black inside, torn panels still ATTACHED, scorch,
# nothing on the ground)

_CUTTERS = []


def _cutter(c, r, seed, z0, z1):
    pts = star(c, r, seed)
    bm = bmesh.new()
    rings_solid(bm, [[(x, y, z0) for (x, y) in pts], [(x, y, z1) for (x, y) in pts]])
    o = new_obj("cutter", bm, "burnt")
    o.hide_render = True
    _CUTTERS.append(o)
    return o


def hole(out, targets, c, r, zf, seed, depth=5.0, panel=("roof", "roof2"), rib="rib", scorch=True, cut_up=30.0):
    """Punch a ragged hole of radius r at c (x, y) through `targets` (objects) whose surface height is zf(x, y)
    (a number or a function), expose a burnt floor `depth` below, add folded / peeled panels, bent stubs and scorch
    to `out`."""
    z = zf if callable(zf) else (lambda x, y: zf)
    rnd = random.Random(seed * 7 + 1)
    zc = z(*c)
    cu = _cutter(c, r, seed, zc - depth, zc + cut_up)
    for o in targets:
        m = o.modifiers.new("blast", "BOOLEAN")
        m.operation = "DIFFERENCE"
        m.object = cu
        m.solver = "EXACT"
    pts = star(c, r * 1.15, seed + 3, 11)
    bm = bmesh.new()
    if targets:
        rings_solid(bm, [[(x, y, zc - depth - 0.6) for (x, y) in pts], [(x, y, zc - depth) for (x, y) in pts]])
    else:            # nothing to cut (a flat roof / floor that is part of a bigger block): the black hole as a patch on it
        pts = star(c, r * 0.95, seed, 13)
        rings_solid(bm, [[(x, y, zc - 0.3) for (x, y) in pts], [(x, y, zc + 0.14) for (x, y) in pts]])
    out.append(new_obj(f"hole{seed}_floor", bm, "burnt"))
    for i in range(5):                                   # folded down into the hole
        a = rnd.uniform(0, 360)
        e = polar(c, r * rnd.uniform(0.8, 0.95), a)
        q = polar(c, r * rnd.uniform(0.15, 0.45), a + rnd.uniform(-25, 25))
        out.append(beam(f"hole{seed}_fold{i}", (*e, z(*e) - 0.1), (*q, zc - rnd.uniform(1.5, min(depth, 3.6))),
                        rnd.uniform(0.2, 0.36) * r, 0.4, rnd.choice(panel)))
    for i in range(4):                                   # peeled up at the edge
        a = rnd.uniform(0, 360)
        e = polar(c, r * rnd.uniform(0.85, 1.0), a)
        q = polar(c, r * rnd.uniform(1.15, 1.4), a + rnd.uniform(-20, 20))
        out.append(beam(f"hole{seed}_peel{i}", (*e, z(*e)), (*q, z(*q) + rnd.uniform(1.0, 2.4)), rnd.uniform(0.18, 0.3) * r, 0.4,
                        rnd.choice(panel + (rib,))))
    for i in range(3):                                   # bent stubs
        a = rnd.uniform(0, 360)
        e = polar(c, r * 0.65, a)
        q = polar(c, r * rnd.uniform(0.0, 0.25), a + rnd.uniform(-40, 40))
        out.append(beam(f"hole{seed}_stub{i}", (*e, z(*e) - 0.3), (*q, zc + rnd.uniform(-1.6, 1.8)), 0.5, 0.5, rib))
    if scorch:
        for i in range(7):
            q = polar(c, r * rnd.uniform(1.0, 1.6), rnd.uniform(0, 360))
            sp = star(q, rnd.uniform(0.18, 0.36) * r + 0.6, seed * 13 + i, 9)
            bm = bmesh.new()
            zq = z(*q)
            rings_solid(bm, [[(x, y, zq - 0.3) for (x, y) in sp], [(x, y, zq + 0.18) for (x, y) in sp]])
            out.append(new_obj(f"hole{seed}_scorch{i}", bm, "soot" if i % 3 else "burnt"))


def scorch(out, c, r, zq, seed, n=5):
    """Soot patches on a surface at height zq (number or function) round c."""
    z = zq if callable(zq) else (lambda x, y: zq)
    rnd = random.Random(seed)
    for i in range(n):
        q = polar(c, r * rnd.uniform(0.0, 1.0), rnd.uniform(0, 360))
        sp = star(q, rnd.uniform(0.25, 0.5) * r + 0.5, seed * 11 + i, 9)
        bm = bmesh.new()
        rings_solid(bm, [[(x, y, z(*q) - 0.3) for (x, y) in sp], [(x, y, z(*q) + 0.2) for (x, y) in sp]])
        out.append(new_obj(f"scorch{seed}_{i}", bm, "soot" if i % 2 else "burnt"))


def wall_gash(out, c, ang, hl, z0, z1, seed, depth=0.7):
    """A torn gash on a wall face (centre c on the face, wall along ang): a ragged burnt patch proud of the wall with
    bent strips hanging from its top edge."""
    rnd = random.Random(seed)
    n = 9
    for i in range(n):
        t = (i + 0.5) / n * 2 - 1
        k = 1.0 - 0.5 * abs(t) - 0.3 * rnd.random()
        p = (c[0] + math.cos(math.radians(ang)) * t * hl, c[1] + math.sin(math.radians(ang)) * t * hl)
        zm = (z0 + z1) / 2
        out.append(obox(f"gash{seed}_{i}", p, ang, hl / n * 1.15, depth, zm - (zm - z0) * k, zm + (z1 - zm) * k, "burnt" if i % 3 else "soot"))
    for i in range(3):
        t = rnd.uniform(-0.8, 0.8)
        p = (c[0] + math.cos(math.radians(ang)) * t * hl, c[1] + math.sin(math.radians(ang)) * t * hl)
        nx, ny = math.sin(math.radians(ang)), -math.cos(math.radians(ang))
        out.append(beam(f"gash{seed}_strip{i}", (p[0] + nx * depth, p[1] + ny * depth, z1 - 0.3),
                        (p[0] + nx * (depth + rnd.uniform(0.8, 2.0)), p[1] + ny * (depth + rnd.uniform(0.8, 2.0)), z1 - rnd.uniform(1.5, 3.5)),
                        rnd.uniform(0.8, 1.6), 0.3, rnd.choice(("wall", "roof2", "rib"))))


# ------------------------------------------------------------------------------------------------
# Build, bake, export


def assemble(key, built):
    root = bpy.data.objects.new(key, None)
    bpy.context.scene.collection.objects.link(root)
    nodes = {}
    origins = built.get("origins", {})
    for name, parts in built["nodes"].items():
        if not parts:
            continue
        node = V3.finish_node(parts, name, origins.get(name, (0, 0, 0)))
        node.parent = root
        nodes[name] = node
    for name, parent in built.get("parents", {}).items():      # e.g. {"barrel": "turret"}
        if name in nodes and parent in nodes:
            mw = nodes[name].matrix_world.copy()
            nodes[name].parent = nodes[parent]
            nodes[name].matrix_world = mw
    for (name, p, size) in built.get("anchors", []):
        e = bpy.data.objects.new(name, None)
        bpy.context.scene.collection.objects.link(e)
        par = nodes.get(built.get("anchor_parent", {}).get(name, ""), root)
        e.parent = par
        e.matrix_world = __import__("mathutils").Matrix.Translation(Vector(p) * PX) @ __import__("mathutils").Matrix.Scale(size, 4)
    for o in _CUTTERS:
        bpy.data.objects.remove(o)
    _CUTTERS.clear()
    bpy.context.view_layer.update()
    return root, nodes


def run(key, build, poses=(), spec=None, P=None, rest=None):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    short = key.replace("ATREIDES_", "").lower()
    opt = dict(out=os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-atreides-" + short),
               bake=True, bake_size=2048, variants=True, damaged=True, poses=True)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--out":
            opt["out"] = argv[i + 1]; i += 1
        elif a == "--no-bake":
            opt["bake"] = False
        elif a == "--no-variants":
            opt["variants"] = False
        elif a == "--no-damaged":
            opt["damaged"] = False
        elif a == "--no-poses":
            opt["poses"] = False
        elif a == "--bake-size":
            opt["bake_size"] = int(argv[i + 1]); i += 1
        elif a == "--device":
            V3.DEVICE = argv[i + 1].lower(); i += 1
        elif a == "--set" and P is not None:
            k, v = argv[i + 1].split("=", 1)
            P[k] = float(v) if isinstance(P[k], float) else type(P[k])(eval(v))   # noqa: S307 (own CLI)
            i += 1
        else:
            raise SystemExit("unknown option " + a)
        i += 1
    V2.setup_gpu = V3.setup_device
    vdir = os.path.join(opt["out"], "variants")
    os.makedirs(vdir, exist_ok=True)
    for f in os.listdir(vdir):                   # stale variants would be rendered by the pack driver
        if f.endswith(".glb"):
            os.remove(os.path.join(vdir, f))
    if spec and not callable(spec) and spec.get("damaged") is False:    # an art without a damaged frame (turrets)
        opt["damaged"] = False
    states = [False] + ([True] if opt["damaged"] and opt["variants"] else [])
    info = dict(key=key, poses=0, split=False, damaged=bool(opt["damaged"] and opt["variants"]), turret=False)
    info.update(spec() if callable(spec) else (spec or {}))
    for damaged in states:
        suffix = "_damaged" if damaged else ""
        bpy.ops.wm.read_factory_settings(use_empty=True)
        V3._mats.clear()
        t0 = time.time()
        root, nodes = assemble(key, build(damaged))
        for name in list(V3._mats):
            RF.mat(name)
        log(f"built{suffix}: {V2.tri_count(V2.meshes_under(root))} triangles in {time.time() - t0:.1f} s; nodes {sorted(nodes)}")
        info["turret"] = "turret" in nodes
        if opt["bake"]:
            t1 = time.time()
            RF.bake_and_merge(list(nodes.values()), opt["bake_size"])
            log(f"textures baked in {time.time() - t1:.1f} s")
        if rest is not None:                     # the idle pose (e.g. an empty silo, hidden wall-link stubs)
            rest(nodes)
            bpy.context.view_layer.update()
        path = os.path.join(opt["out"], "models", "building", key + ".glb") if not suffix else os.path.join(vdir, key + suffix + ".glb")
        V3.export_glb(root, path)
        log(f"exported {path} ({os.path.getsize(path) // 1024} KB)")
        if not damaged and opt["variants"] and opt["poses"]:
            base = {n: (o.location.copy(), o.rotation_euler.copy(), o.scale.copy()) for n, o in nodes.items()}
            for k, pose in enumerate(poses):
                for n, o in nodes.items():
                    o.location, o.rotation_euler, o.scale = base[n][0].copy(), base[n][1].copy(), base[n][2].copy()
                pose(nodes)
                bpy.context.view_layer.update()
                V3.export_glb(root, os.path.join(vdir, f"{key}_pose{k:02d}.glb"))
            for n, o in nodes.items():
                o.location, o.rotation_euler, o.scale = base[n]
            info["poses"] = len(poses)
            if poses:
                log(f"exported {len(poses)} poses")
        if opt["variants"] and "base" in nodes and "main" in nodes:
            RF.make_split([nodes["base"], nodes["main"]] + [o for n, o in nodes.items() if n not in ("base", "main")])
            # nodes other than base count as main (their name does not start with "main": fix the material)
            hm = V3.mat("HOUSE")
            for n, o in nodes.items():
                if n not in ("base", "main"):
                    o.data.materials.clear()
                    o.data.materials.append(hm)
            V3.export_glb(root, os.path.join(vdir, key + suffix + "_split.glb"))
            info["split"] = True
    json.dump(info, open(os.path.join(vdir, "spec.json"), "w"), indent=1)
    log("2D pack: D2K_BUILDING=" + key + " .venv/bin/python -I tools/models/atreides_check/pack.py")

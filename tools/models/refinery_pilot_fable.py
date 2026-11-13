"""Atreides spice refinery (pilot "fable") - the ORIGINAL design rebuilt as a modern hard-surface model,
with the kit, weathering and atlas bake of combat_tank_v3.py / devastator_v1.py.

    blender -b --factory-startup --python tools/models/refinery_pilot_fable.py -- \
        [--out DIR]            write DIR/models/building/ATREIDES_REFINERY.glb (default artpacks/models-refinery-fable)
                               and DIR/variants/*.glb (what the 2D pack needs besides: the damaged state and the
                               "split" twins whose main-node faces are HOUSE = the mask of the main frame)
        [--no-bake]            plain colours instead of the baked atlas (fast iteration)
        [--no-variants]        only the model itself
        [--no-damaged]         skip the damaged state (iteration)
        [--tops]               print the open flue tops (smoke / fire anchors) and exit
        [--inlet]              print the models.ini lines of the anchors (flue tops, harvester inlet) and exit
                               (--set inlet_2d=1 before it: the 2D layout the pack's frames are rendered from)
        [--bake-size 2048]     atlas size
        [--set key=value ...]  override a proportion in P (px)
        [--turntable DIR]      review renders: 8 views + close-ups (house colour = Atreides blue)
        [--samples 48] [--device cpu|gpu]

    then the 2D pack (base / main / damaged frames; the pilot's own driver around tools/render_sprites):
    .venv/bin/python -I tools/models/refinery_fable_check/pack.py --models artpacks/models-refinery-fable \
        --out artpacks/rendered-refinery-fable
    (the 2D frames are rendered from the 2D layout of the harvester inlet: build with --set inlet_2d=1 for them; the
    default build is the 3D layout that goes into the pack as the glb - see P["inlet_3d"])

Conventions (docs/3d-models.md): Blender units = tiles, +X east, +Y north, +Z up, origin on the ground at
the centre of the 3 x 3 footprint. Two nodes, as the original art is two layers (re/notes/rendering.md
section 3): "base" = what the game draws UNDER units (pass A, DATA.R16 frame 3105: drum walls, the dark
tank, the yellow unloading pad - a harvester on the dock is drawn over them) and "main" = what it draws
in the sorted pass (frame 3106: drum roofs, stacks, the bridge with its house-colour roof and dome).
Materials: everything but the house areas is baked into ONE atlas material "body"; "HOUSE" is the
house-colour mask (COLOR_0 = the shade of each part: light bands, dark bridge roof).

Everything is authored in ORIGINAL SPRITE PIXELS (1 px = 1/32 tile; x east, y north of the footprint
centre, z up) so each number can be checked against the sprite. The camera of the original looks north
and down at 45 deg: a point (x, y, z) lands at canvas (72 + x, 88 - 0.707 (y + z)) when the building
anchor (tileX*32, tileY*32 + 96) is at (24, 136)  (refinery_fable_check/cfg.py).

How the original was read (nothing is copied; proportions from pixel positions of the COMPOSITE the game
draws for a finished refinery = base frame 3105 + main frame 3106; the template has no HAS_ANIMATION flag
(0x40), so its buildingAnimation index 0 - the construction yard's crane - never plays, and its build-up
art 20 is a generic rock mound shared by the three houses; both stay original):

  * Two identical drums on a diagonal: ground centres (-21.5, +0.5) and (+22.5, +35). Each: a flared
    ribbed rust-brown skirt (r 24 at the ground), a thin house-colour band at z 8-10.5, a tier of
    pointed arches in cream frames (24 bays, leaning inwards, z 10.5-19.5), a dark recessed neck, a
    brown roof rim (r 19) and a cream radial-ribbed roof disc (r 17.4, z 27.5, low cone).
  * Stacks: twin copper flues on the left roof (to z 56.5), a thick one on the right roof with a white
    collar with two lugs (to z 60.5), and the tallest (z 69) - a cream and a copper pipe - rising from a
    near-black tank (r 5, h 12, house ring) that stands between the drums and the pad. All five are OPEN
    at the top (dark bore, soot-blackened rim, soot running down the last pixels): a working refinery
    vents smoke and fire there - stack_tops() / --tops give the anchors.
  * Between the drums a full-height bridge block with a dark house-colour roof (the big navy patch)
    and, on its north-west side, a cream dome.
  * On the dock cell (2, 1) the yellow unloading pad: a 35 x 19 px tray with a cream rim, turned 45
    deg (long side south-west to north-east), centre (32.4, -2).
  * NOT in the original: the unloading inlet - a steel socket branching off the tall copper flue towards the
    dock (z 17). The original's docked harvester (its own frames, drawn over the base frame) pushes a pipe out
    that just ends on the tank's top; here the pipe of tools/models/harvester_v1.py ends inside this socket,
    which is part of the main frame and so drawn over the harvester. inlet_axis() is the shared geometry.
  * White steps at the south-west of the left drum; an orange pipe on the right roof running
    south-south-east from the hub; a copper standpipe between tank and pad.
  * Light: the sprite's shadows fall to the east-north-east (pole of 63 px -> shadow tip 42 px east,
    32 px north): sun from the west-south-west, about 50 deg high.
Free choices where the sprite says nothing: bolts and hub vents on the roofs, the ladder, plate seams
and guide rails on the pad, vents and pipe runs on the bridge, the exact fin count of the skirts.
"""
import math
import os
import sys
import time

import bmesh
import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import combat_tank_v2 as V2  # noqa: E402  generic helpers (join, bake, camera)
import combat_tank_v3 as V3  # noqa: E402  the kit: mesh helpers, materials, weathering, atlas
import refinery_dock as RD   # noqa: E402  the dock's numbers (the 3D layout)

KEY = "ATREIDES_REFINERY"
PX = V3.PX
log = V2.log
srgb_to_linear = V2.srgb_to_linear

# ------------------------------------------------------------------------------------------------
# Proportions (px)
P = dict(
    d1=(-20.9, -1.0), d2=(22.7, 35.0),         # drum ground centres
    r_foot=24.3, r_skirt=23.8, r_band=23.3, r_arch0=22.7, r_arch1=19.7, r_neck=17.9, r_rim=19.7, r_roof=18.3,
    z_foot=1.4, z_band0=8.8, z_band1=11.2, z_arch1=20.4, z_corn=21.2, z_rim0=23.0, z_roof=27.5, z_apex=29.4,
    bays=24, ribs=20,
    tank=(11.5, 5.0), tank_r=5.0, tank_h=12.0,
    pole_l=56.5, pole_r=60.5, pole_t=69.0,
    bridge_c=(0.5, 17.75), bridge_half=11.0, bridge_se=6.5, bridge_nw=11.0, bridge_h=27.2,
    dome_r=6.4, dome_z=26.0,
    # the unloading inlet: a socket on the tall copper flue, pointing at the dock (harvester_v1.py's pipe ends in it):
    # axis height, where its mouth is / where it starts (px from the flue's axis, towards the south-east), radii
    inlet=dict(z=16.3, mouth=3.4, back=1.2, r_out=2.7, r_in=2.15, r_lip=3.1),
    # ... and where the socket really is (the 3D layout, the pack's glb): its mouth (x, y, z). In Graphics: 3D the
    # harvester stands on the pad (the engine backs its pipe onto the anchor fx_inlet_0 = this mouth + 1.6 px into
    # the bore) and its pipe (26.35 px behind its centre, 10.6 px above the pad's floor) ends here - on the right
    # drum's wall. The numbers above are the 2D layout (inlet_2d=1): the same socket seen by the 45-degree camera
    # where the harvester SPRITE's pipe ends (the sprite is drawn on the dock cell, 8 px away on the screen); the
    # pack's 2D frames are rendered from that layout. See docs/3d-models.md "Docking".
    inlet_3d=(10.6, 15.2, 12.4), inlet_2d=0,
    # the 3D layout (layout_3d(), applied unless inlet_2d=1) also moves / resizes the drums, bridge, tank and pad:
    drum_k=1.0,                                # drum radii of the 3D layout / the picture's
    dome_c=(0.0, 0.0), layout=0,               # the dome's centre (3D layout; 2D: from the bridge), 1 = 3D layout applied
    pad_c=(28.1, -2.3), pad_len=17.0, pad_wid=15.0, pad_h=2.3, pad_floor=1.3, pad_rim=1.7, pad_split=4.0,
    stairs_ang=232.0,
    house_low=0.75,
)

# sRGB base colours: with the Standard view transform at exposure 0 these are very nearly the colours on
# screen for a sunlit top face
COL = dict(
    roof=(0.975, 0.930, 0.800),      # roof panels: the original's warm cream
    roof2=(0.900, 0.840, 0.700),     # every other panel a little dustier
    rib=(0.600, 0.480, 0.340),       # roof ribs, hub
    rim=(0.470, 0.290, 0.140),       # roof rim: brown
    neck=(0.270, 0.200, 0.150),      # the recessed neck under the rim: near-black brown
    wall=(0.800, 0.750, 0.630),      # arch frames, cornice
    arch_in=(0.170, 0.150, 0.130),   # inside the arches
    skirt=(0.520, 0.380, 0.260),     # the flared skirt: rust
    fin=(0.740, 0.600, 0.430),       # its fins, lighter
    foot=(0.230, 0.180, 0.140),
    copper=(0.560, 0.350, 0.170),    # stacks, pipes
    copper_dk=(0.300, 0.190, 0.105),
    orange=(0.960, 0.600, 0.200),    # the orange pipe on the right roof
    cream_pipe=(0.930, 0.880, 0.760),
    white=(0.975, 0.960, 0.900),     # steps, collar
    tank=(0.070, 0.075, 0.090),      # the near-black tank
    block=(0.640, 0.610, 0.540),     # bridge walls: grey-beige
    block_dk=(0.330, 0.300, 0.260),
    pad=(1.000, 0.880, 0.360),       # the pad's yellow floor
    pad_dk=(0.520, 0.380, 0.180),    # its ochre-brown apron
    pad_rim=(0.970, 0.940, 0.830),
    recess=(0.085, 0.078, 0.068),    # seams, bores, vents: near-black
    inlet=(0.640, 0.630, 0.600),     # the unloading inlet's socket: steel, as the harvester's pipe
    burnt=(0.045, 0.040, 0.036),     # flue bores; damaged state: holes and soot
    soot=(0.150, 0.120, 0.100),      # the soot-blackened rims of the flues
    rubble=(0.330, 0.290, 0.240),
)
# (metallic, roughness, surface kind)
SPEC = dict(
    roof=(0.0, 0.42, "paint"), roof2=(0.0, 0.5, "paint"), rib=(0.3, 0.5, "steel"), rim=(0.3, 0.5, "steel"),
    neck=(0.0, 0.85, "dark"), wall=(0.0, 0.5, "paint"), arch_in=(0.0, 0.9, "dark"), skirt=(0.25, 0.6, "steel"),
    fin=(0.25, 0.55, "steel"), foot=(0.0, 0.8, "dark"), copper=(0.5, 0.4, "steel"), copper_dk=(0.4, 0.5, "steel"),
    orange=(0.4, 0.4, "steel"), cream_pipe=(0.0, 0.4, "paint"), white=(0.0, 0.4, "paint"), tank=(0.3, 0.35, "dark"),
    block=(0.0, 0.55, "paint"), block_dk=(0.0, 0.6, "paint"), pad=(0.0, 0.45, "paint"), pad_dk=(0.0, 0.6, "paint"), pad_rim=(0.0, 0.45, "paint"),
    inlet=(0.6, 0.4, "steel"), recess=(0.0, 0.9, "dark"), burnt=(0.0, 0.95, "dark"), soot=(0.0, 0.9, "dark"), rubble=(0.0, 0.8, "paint"),
)
# the dust film that dulls most of a surface: its colour per material
FILM = dict(
    roof=(0.930, 0.880, 0.750), roof2=(0.860, 0.790, 0.650), wall=(0.740, 0.680, 0.550), white=(0.970, 0.950, 0.880),
    block=(0.560, 0.520, 0.440), block_dk=(0.340, 0.305, 0.255), pad=(1.000, 0.840, 0.330), pad_rim=(0.940, 0.900, 0.780), pad_dk=(0.450, 0.340, 0.180),
    skirt=(0.420, 0.300, 0.200), fin=(0.540, 0.400, 0.260), rim=(0.380, 0.280, 0.190), rib=(0.470, 0.370, 0.260),
    inlet=(0.500, 0.480, 0.440), copper=(0.520, 0.360, 0.210), cream_pipe=(0.780, 0.720, 0.600), rubble=(0.380, 0.330, 0.270),
)
PREVIEW_HOUSE = (0.33, 0.45, 0.65)   # Atreides blue, previews only

V3.COL.update(COL)
V3.SPEC.update(SPEC)
# emission (combat_tank_v2.EMIT, docs/3d-models.md "Emission"): the dark insides of the arches = the lit windows
# of the plant at night (warm amber; linear RGB, the brightest channel = strength)
V3.V2.EMIT.update(arch_in=(0.90, 0.45, 0.12))
V3.FILM.update(FILM)
V3.WEATHER["paint"] = (0.75, 0.65, 0.92, 0.65, 0.85, 0.80, 0.45, 0.60)   # film, blotch, cavity, soft, grime, streak, chip, low dust
V3.WEATHER["steel"] = (0.70, 0.30, 0.85, 0.45, 0.45, 0.35, 0.0, 0.35)
V3.WEATHER["dark"] = (0.0, 0.0, 0.50, 0.10, 0.0, 0.0, 0.0, 0.18)
V3.SOOT.clear()
V3.DIRT.update(soft=(0.600, 0.500, 0.380), blotch=(0.660, 0.560, 0.420), grime=(0.430, 0.330, 0.230), streak=(0.450, 0.340, 0.230))
V3.GRIME_SCALE.clear()
V3.GRIME_SCALE.update(pad=0.4, pad_rim=0.5, white=0.4)
V3.UV_SCALE.clear()
V3.UV_SCALE.update(soot=0.5, neck=0.4, arch_in=0.5, foot=0.5, recess=0.3, tank=0.6, copper_dk=0.6, burnt=0.3, HOUSE=0.06)
V3.P["house_low"] = P["house_low"]

rings_solid, new_obj, shade = V3.rings_solid, V3.new_obj, V3.shade


def layout_3d():
    """The 3D layout (the pack's glb; docs/3d-models.md "Docking", refinery_dock.py): the model laid out on its
    cells like the heavy factories instead of on the picture. The pad lies on the dock cell's centre (cut to the
    footprint: an octagonal tray, nothing on it higher than the plate the harvester stands on), nothing else
    stands in the cylinder a harvester sweeps when it turns and unloads there, and the inlet socket's mouth is
    where the pipe of the harvester on the cell's centre ends - on the north drum's wall. For that the two drums
    are 0.82 of the picture's radius (same heights, 20 arches instead of 24) and stand steeper: the west one
    against the west edge, the north one against the north edge; the bridge between them is shorter; the tank with
    the tall flue moved from in front of the drums (where the harvester's rear now is) to the north of the pad."""
    if P["inlet_2d"] or P["layout"]:
        return
    k = 0.82
    old = RD.dist(P["d1"], P["d2"])
    for key in ("r_foot", "r_skirt", "r_band", "r_arch0", "r_arch1", "r_neck", "r_rim", "r_roof"):
        P[key] *= k
    rf = P["r_foot"] + 0.4
    d1, d2 = RD.drums(rf, rf, gap=2.4, edge=0.4, air=0.4)
    tr = P["tank_r"] + 0.3
    P.update(layout=1, drum_k=k, bays=20, ribs=16, d1=d1, d2=d2,
             bridge_c=((d1[0] + d2[0]) / 2, (d1[1] + d2[1]) / 2),
             bridge_half=RD.dist(d1, d2) / 2 - (old / 2 - P["bridge_half"]) * k,
             dome_c=RD.crook(d1, d2, rf + P["dome_r"] - 2.9),
             tank=RD.nook(d2, rf + tr + 0.6, RD.K_LOW + tr + 1.2),
             pad_c=RD.DOCK, pad_h=2.0,
             inlet_3d=(*RD.mouth(), 2.0 + RD.NOZZLE_UP))


# ------------------------------------------------------------------------------------------------
# Mesh helpers (px: x east, y north, z up)


def circle(cx, cy, r, z, segs, a0=0.0):
    return [(cx + r * math.cos(a0 + 2 * math.pi * i / segs), cy + r * math.sin(a0 + 2 * math.pi * i / segs), z)
            for i in range(segs)]


def lathe(name, c, profile, material, segs=48, bev=0.0, cap=True):
    """Solid of revolution about the vertical through c = (x, y): profile = (r, z), bottom to top."""
    bm = bmesh.new()
    rings_solid(bm, [circle(c[0], c[1], max(r, 0.02), z, segs) for (r, z) in profile], cap)
    return new_obj(name, bm, material, bev, 2, angle=40)


def lathe_loop(name, c, profile, material, segs=48):
    """Closed ring of revolution: the profile (r, z) is a closed loop (a watertight torus-like solid, so it can
    take a boolean cut)."""
    bm = bmesh.new()
    rings = [[bm.verts.new(p) for p in circle(c[0], c[1], r, z, segs)] for (r, z) in profile]
    for a, b in zip(rings, rings[1:] + rings[:1]):
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new([a[i], a[j], b[j], b[i]])
    return new_obj(name, bm, material)


def tube(name, p0, p1, r, material, segs=12, r1=None):
    """Cylinder / cone between two points."""
    p0, p1 = Vector(p0), Vector(p1)
    d = (p1 - p0).normalized()
    side = d.cross(Vector((0, 0, 1)))
    if side.length < 1e-6:
        side = Vector((1, 0, 0))
    side.normalize()
    up = side.cross(d)
    rb = r if r1 is None else r1
    bm = bmesh.new()
    rings_solid(bm, [[tuple(p + (side * math.cos(2 * math.pi * i / segs) + up * math.sin(2 * math.pi * i / segs)) * rr)
                      for i in range(segs)] for p, rr in ((p0, r), (p1, rb))])
    return new_obj(name, bm, material)


def beam(name, p0, p1, w, h, material, bev=0.0):
    """Rectangular bar from p0 to p1: w across (horizontal), h in the third direction, centred."""
    p0, p1 = Vector(p0), Vector(p1)
    d = (p1 - p0).normalized()
    side = d.cross(Vector((0, 0, 1)))
    if side.length < 1e-6:
        side = Vector((1, 0, 0))
    side.normalize()
    up = side.cross(d)
    bm = bmesh.new()
    rings_solid(bm, [[tuple(p + side * (sx * w / 2) + up * (sy * h / 2)) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
                     for p in (p0, p1)])
    return new_obj(name, bm, material, bev)


def obox(name, c, ang_deg, hl, hw, z0, z1, material, bev=0.0, top=1.0):
    """Box turned about the vertical: centre c = (x, y), half length hl along the direction ang_deg (0 = east,
    counter-clockwise), half width hw across; `top` scales the top face (a batter)."""
    a = math.radians(ang_deg)
    ax, ay = math.cos(a), math.sin(a)

    def ring(z, k):
        return [(c[0] + ax * sl * hl * k - ay * sw * hw * k, c[1] + ay * sl * hl * k + ax * sw * hw * k, z)
                for sl, sw in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    bm = bmesh.new()
    rings_solid(bm, [ring(z0, 1.0), ring(z1, top)])
    return new_obj(name, bm, material, bev)


def polar(c, r, ang_deg, z=None):
    a = math.radians(ang_deg)
    p = (c[0] + r * math.cos(a), c[1] + r * math.sin(a))
    return p if z is None else (p[0], p[1], z)


def arch_tier(name, c, n, z0, z1, r0, r1, thick, material):
    """A ring of n pointed arches: the cream frame (piers, sill, spandrels) as a shell `thick` proud of a
    dark back wall (built separately), leaning from radius r0 at z0 to r1 at z1."""
    pier, tb, ts, ta, m = 0.14, 0.20, 0.55, 0.94, 5

    def arch_t(s):          # the opening's upper edge at s (pier <= s <= 1 - pier)
        u = 1.0 - abs(s - 0.5) / (0.5 - pier)
        return ts + (ta - ts) * (1.0 - (1.0 - u) ** 1.7)
    ss = [0.0, pier] + [pier + (0.5 - pier) * i / m for i in range(1, m)] + [0.5] + \
         [0.5 + (0.5 - pier) * i / m for i in range(1, m)] + [1.0 - pier, 1.0]
    bm = bmesh.new()
    cache = {}

    def vert(k, s, t, inner):
        a = 2 * math.pi * (k + s) / n
        key = (round(a % (2 * math.pi), 5) % round(2 * math.pi, 5), round(t, 5), inner)
        if key not in cache:
            r = r0 + (r1 - r0) * t - (thick if inner else 0.0)
            cache[key] = bm.verts.new((c[0] + r * math.cos(a), c[1] + r * math.sin(a), z0 + (z1 - z0) * t))
        return cache[key]

    def face(pts, k):
        vs = [vert(k, s, t, inner) for (s, t, inner) in pts]
        if len(set(vs)) >= 3:
            bm.faces.new(vs)
    for k in range(n):
        for sa, sb in zip(ss[:-1], ss[1:]):
            if sb <= pier + 1e-9 or sa >= 1 - pier - 1e-9:
                face([(sa, 0, 0), (sb, 0, 0), (sb, 1, 0), (sa, 1, 0)], k)
            else:
                face([(sa, 0, 0), (sb, 0, 0), (sb, tb, 0), (sa, tb, 0)], k)
                face([(sa, arch_t(sa), 0), (sb, arch_t(sb), 0), (sb, 1, 0), (sa, 1, 0)], k)
                # reveals: sill top and the arch's underside
                face([(sa, tb, 0), (sb, tb, 0), (sb, tb, 1), (sa, tb, 1)], k)
                face([(sb, arch_t(sb), 0), (sa, arch_t(sa), 0), (sa, arch_t(sa), 1), (sb, arch_t(sb), 1)], k)
        for s, flip in ((pier, False), (1 - pier, True)):       # jambs
            q = [(s, tb, 0), (s, ts, 0), (s, ts, 1), (s, tb, 1)]
            face(q[::-1] if flip else q, k)
    return new_obj(name, bm, material)


def cone_z(c, p, r_hub=2.6):
    """Height of the roof cone above the point p (x, y)."""
    r = math.hypot(p[0] - c[0], p[1] - c[1])
    t = min(1.0, max(0.0, (P["r_roof"] - r) / (P["r_roof"] - r_hub)))
    return P["z_roof"] + (P["z_apex"] - P["z_roof"]) * t


# ------------------------------------------------------------------------------------------------
# Parts


CUTTERS = []


def star(c, r, seed, n=13):
    """A ragged outline (x, y) round c: the plan of a blast hole."""
    import random
    rnd = random.Random(seed)
    a0 = rnd.uniform(0, 2 * math.pi)
    return [(c[0] + r * ((0.60 + 0.30 * rnd.random()) if i % 2 else (0.92 + 0.22 * rnd.random())) * math.cos(a0 + 2 * math.pi * i / n),
             c[1] + r * ((0.60 + 0.30 * rnd.random()) if i % 2 else (0.92 + 0.22 * rnd.random())) * math.sin(a0 + 2 * math.pi * i / n))
            for i in range(n)]


def cutter(c, r, seed, z0, z1):
    """Ragged prism used to punch a hole (boolean difference); removed after the nodes are joined."""
    pts = star(c, r, seed)
    bm = bmesh.new()
    rings_solid(bm, [[(x, y, z0) for (x, y) in pts], [(x, y, z1) for (x, y) in pts]])
    o = new_obj("cutter", bm, "burnt")
    o.hide_render = True
    CUTTERS.append(o)
    return o


def cut(objs, cut_obj):
    for o in objs:
        m = o.modifiers.new("blast", "BOOLEAN")
        m.operation = "DIFFERENCE"
        m.object = cut_obj
        m.solver = "EXACT"


def blast(main, tag, c, holes, panels, ribs, rim, ring):
    """Battle damage of one drum roof, as the sprite's damaged frame shows it: ragged holes punched through the
    panels (through the rim too where the hole reaches it), the black interior exposed, the roof's ribs left
    standing bent over the hole, torn panels still ATTACHED - folded down into the hole, peeled up at its edge,
    hanging over the rim - and scorch marks round it. Nothing lies on the ground."""
    import random
    for hi, (ang, dist, r, seed, rim_too) in enumerate(holes):
        rnd = random.Random(seed * 7 + 1)
        hc = polar(c, dist, ang)
        cut(panels + [ring], cutter(hc, r, seed, P["z_rim0"] + 1.0, P["z_apex"] + 3.0))
        cut(ribs, cutter(hc, r * 0.62, seed + 50, P["z_rim0"] + 1.0, P["z_apex"] + 3.0))
        if rim_too:
            cut([rim], cutter(polar(c, dist + r * 0.45, ang), r * 0.62, seed + 90, P["z_rim0"] - 1.5, P["z_apex"] + 3.0))
        zr = cone_z(c, hc)
        for i in range(5):                               # torn panels folded down into the hole (attached at its edge)
            a = rnd.uniform(0, 360)
            e = polar(hc, r * rnd.uniform(0.78, 0.95), a)
            q = polar(hc, r * rnd.uniform(0.15, 0.45), a + rnd.uniform(-25, 25))
            main.append(beam(f"{tag}_fold{hi}_{i}", (*e, cone_z(c, e) - 0.1), (*q, zr - rnd.uniform(1.8, 3.6)),
                             rnd.uniform(2.0, 3.6), 0.4, rnd.choice(("roof", "roof2", "roof2"))))
        for i in range(4):                               # panels peeled up and outwards at the hole's edge
            a = rnd.uniform(0, 360)
            e = polar(hc, r * rnd.uniform(0.85, 1.0), a)
            q = polar(hc, r * rnd.uniform(1.15, 1.4), a + rnd.uniform(-20, 20))
            if math.hypot(q[0] - c[0], q[1] - c[1]) > P["r_rim"] + 3.5:
                continue
            main.append(beam(f"{tag}_peel{hi}_{i}", (*e, cone_z(c, e)), (*q, cone_z(c, q) + rnd.uniform(1.2, 2.6)),
                             rnd.uniform(1.6, 3.0), 0.4, rnd.choice(("roof", "roof2", "rib"))))
        for i in range(3):                               # bent rib stubs standing over the hole
            a = rnd.uniform(0, 360)
            e = polar(hc, r * 0.6, a)
            q = polar(hc, r * rnd.uniform(0.0, 0.25), a + rnd.uniform(-40, 40))
            main.append(beam(f"{tag}_stub{hi}_{i}", (*e, cone_z(c, e) + 0.2), (*q, zr + rnd.uniform(-1.6, 2.4)), 0.5, 0.5, "rib"))
        if rim_too:                                      # plates hanging over the rim, jagged pieces sticking out of it
            for i in range(5):
                a = ang + rnd.uniform(-34, 34)
                e = polar(c, P["r_rim"] - 0.6, a)
                down = i % 2 == 0
                q = polar(c, P["r_rim"] + rnd.uniform(2.2, 4.6), a + rnd.uniform(-10, 10))
                zq = P["z_roof"] + (rnd.uniform(-6.5, -3.0) if down else rnd.uniform(0.6, 2.4))
                main.append(beam(f"{tag}_hang{hi}_{i}", (*e, P["z_roof"] - 0.2), (*q, zq), rnd.uniform(1.8, 3.4), 0.45,
                                 rnd.choice(("roof2", "rim", "roof"))))
        for i in range(7):                               # scorch round the hole (thin ragged plates on the roof)
            a = rnd.uniform(0, 360)
            q = polar(hc, r * rnd.uniform(1.0, 1.55), a)
            if math.hypot(q[0] - c[0], q[1] - c[1]) > P["r_roof"] - 1.2:
                continue
            pts = star(q, rnd.uniform(1.3, 2.9), seed * 13 + i, 9)
            bm = bmesh.new()
            z = cone_z(c, q)
            rings_solid(bm, [[(x, y, z - 0.2) for (x, y) in pts], [(x, y, z + 0.16) for (x, y) in pts]])
            main.append(new_obj(f"{tag}_scorch{hi}_{i}", bm, "soot" if i % 3 else "burnt"))
        for i in range(5):                               # wreckage inside, on the burnt floor
            q = polar(hc, r * rnd.uniform(0.0, 0.6), rnd.uniform(0, 360))
            main.append(obox(f"{tag}_wreck{hi}_{i}", q, rnd.uniform(0, 180), rnd.uniform(0.8, 2.0), rnd.uniform(0.5, 1.1),
                             P["z_rim0"] + 0.5, P["z_rim0"] + rnd.uniform(1.0, 2.6), rnd.choice(("rubble", "burnt", "rubble")), 0.1))
    # the exposed interior: a burnt floor and the dark roof trusses
    main.append(lathe(f"{tag}_hole", c, [(P["r_roof"] - 0.4, P["z_rim0"] + 0.5), (0.02, P["z_rim0"] + 0.5)], "burnt", 48))
    for k in range(4):
        a = 45.0 * k + 12.0
        main.append(beam(f"{tag}_truss{k}", (*polar(c, P["r_roof"] - 0.8, a), P["z_roof"] - 1.6), (*polar(c, P["r_roof"] - 0.8, a + 180.0), P["z_roof"] - 1.6),
                         0.7, 0.9, "copper_dk"))


def drum(base, main, tag, c, holes=()):
    n = P["bays"]
    # --- base node: what stands on the ground up to the cornice
    base.append(lathe(f"{tag}_foot", c, [(P["r_foot"] + 0.4, 0.0), (P["r_foot"] + 0.4, P["z_foot"] * 0.6), (P["r_foot"], P["z_foot"])],
                      "foot", 72))
    base.append(lathe(f"{tag}_skirt", c, [(P["r_skirt"] + 0.5, P["z_foot"] * 0.5), (P["r_band"] - 0.2, P["z_band0"])], "skirt", 72))
    for k in range(2 * n):                               # fins: under every pier and every arch
        a = 360.0 * (k + 0.0) / (2 * n)
        p0 = polar(c, P["r_skirt"] + 0.75, a, P["z_foot"] * 0.5)
        p1 = polar(c, P["r_band"] + 0.05, a, P["z_band0"] - 0.1)
        base.append(beam(f"{tag}_fin{k}", p0, p1, 1.25 if k % 2 == 0 else 0.8, 1.1, "fin", 0.12))
    band = lathe(f"{tag}_band", c, [(P["r_band"], P["z_band0"]), (P["r_band"] + 0.25, P["z_band0"] + 0.4),
                                    (P["r_band"] + 0.25, P["z_band1"] - 0.4), (P["r_band"] - 0.3, P["z_band1"])], "HOUSE", 72)
    base.append(shade(band, 0.5))
    base.append(lathe(f"{tag}_back", c, [(P["r_arch0"] - 0.9, P["z_band1"] - 0.2), (P["r_arch1"] - 0.9, P["z_arch1"])], "arch_in", 72))
    base.append(arch_tier(f"{tag}_arches", c, n, P["z_band1"], P["z_arch1"], P["r_arch0"], P["r_arch1"], 0.9, "wall"))
    base.append(lathe(f"{tag}_cornice", c, [(P["r_arch1"], P["z_arch1"]), (P["r_arch1"] + 0.55, P["z_arch1"] + 0.3),
                                            (P["r_arch1"] + 0.55, P["z_corn"] - 0.2), (P["r_neck"], P["z_corn"])], "wall", 72))
    base.append(lathe(f"{tag}_neck", c, [(P["r_neck"], P["z_corn"] - 0.1), (P["r_neck"], P["z_rim0"] + 0.2)], "neck", 48))
    # --- main node: rim, roof, ribs, hub
    panels, ribs = [], []
    rim = lathe_loop(f"{tag}_rim", c, [(P["r_neck"] + 0.2, P["z_rim0"] - 0.3), (P["r_rim"] - 0.3, P["z_rim0"]), (P["r_rim"], P["z_rim0"] + 1.0),
                                        (P["r_rim"] - 0.5, P["z_roof"] - 0.6), (P["r_roof"] + 0.3, P["z_roof"] - 0.05),
                                        (P["r_roof"] - 0.6, P["z_roof"] - 0.25), (P["r_roof"] - 0.6, P["z_rim0"] + 0.6), (P["r_neck"] + 0.2, P["z_rim0"] + 0.3)], "rim", 72)
    main.append(rim)
    nr = P["ribs"]
    r_hub = 2.6
    for k in range(nr):                                  # roof panels: alternate cream / dustier cream
        a0, a1 = 2 * math.pi * k / nr, 2 * math.pi * (k + 1) / nr
        steps = 3
        pts_out = [(c[0] + P["r_roof"] * math.cos(a0 + (a1 - a0) * i / steps), c[1] + P["r_roof"] * math.sin(a0 + (a1 - a0) * i / steps))
                   for i in range(steps + 1)]
        pts_in = [(c[0] + r_hub * math.cos(a0 + (a1 - a0) * i / steps), c[1] + r_hub * math.sin(a0 + (a1 - a0) * i / steps))
                  for i in range(steps + 1)]
        bm = bmesh.new()
        top = [bm.verts.new((x, y, P["z_roof"])) for (x, y) in pts_out] + [bm.verts.new((x, y, P["z_apex"])) for (x, y) in pts_in[::-1]]
        bot = [bm.verts.new((x, y, P["z_roof"] - 0.5)) for (x, y) in pts_out] + [bm.verts.new((x, y, P["z_apex"] - 0.5)) for (x, y) in pts_in[::-1]]
        bm.faces.new(top)
        bm.faces.new(bot[::-1])
        m = len(top)
        for i in range(m):
            j = (i + 1) % m
            bm.faces.new([top[j], top[i], bot[i], bot[j]])
        panels.append(new_obj(f"{tag}_panel{k}", bm, "roof" if k % 2 == 0 else "roof2"))
    for k in range(nr):                                  # ribs
        a = 360.0 * k / nr
        p0, p1 = polar(c, r_hub + 0.3, a), polar(c, P["r_roof"] + 0.35, a)
        ribs.append(beam(f"{tag}_rib{k}", (*p0, P["z_apex"] + 0.05), (*p1, P["z_roof"] + 0.05), 0.5, 0.5, "rib"))
    ra, rb = 9.4 * P["drum_k"], 8.7 * P["drum_k"]
    ring = lathe(f"{tag}_ring", c, [(ra, cone_z(c, (c[0] + ra, c[1])) - 0.1), (ra, cone_z(c, (c[0] + ra, c[1])) + 0.42),
                                         (rb, cone_z(c, (c[0] + rb, c[1])) + 0.42), (rb, cone_z(c, (c[0] + rb, c[1])) - 0.1)], "rib", 48)
    main.extend(panels + ribs + [ring])
    main.append(lathe(f"{tag}_hub", c, [(3.4, P["z_apex"] - 0.6), (3.4, P["z_apex"] + 0.9), (2.9, P["z_apex"] + 1.4)], "rim", 24))
    for k in range(nr):                                  # bolts on the rim: modern detail
        p = polar(c, P["r_roof"] - 1.3, 360.0 * (k + 0.5) / nr)
        main.append(lathe(f"{tag}_bolt{k}", p, [(0.34, P["z_roof"] - 0.05), (0.34, cone_z(c, p) + 0.35)], "copper_dk", 6))
    if holes:
        blast(main, tag, c, holes, panels, ribs, rim, ring)


def stack_tops():
    """The open tops of the five flues: name -> (x, y, z, bore radius), px. The smoke / fire of a working
    refinery is anchored here (README "Stack tops"; `--tops` prints them with their frame pixels)."""
    d1, d2, tk = P["d1"], P["d2"], P["tank"]
    return dict(left_a=(d1[0] - 0.8, d1[1], P["pole_l"] - 0.6, 0.8), left_b=(d1[0] + 1.45, d1[1], P["pole_l"], 1.05),
                right=(d2[0], d2[1], P["pole_r"], 1.9),
                tall_cream=(tk[0] - 2.1, tk[1] - 0.3, P["pole_t"] - 2.4, 0.75), tall_copper=(tk[0] + 0.6, tk[1], P["pole_t"], 1.4))


# The flue tops exported as anchors fx_flue_0..4 (in this order) with their vent strength.
FLUE_ANCHORS = (("left_a", 0.45), ("left_b", 0.8), ("right", 0.9), ("tall_cream", 0.4), ("tall_copper", 0.65))


def open_pipe(parts, name, top, r, z0, material, lip=0.35, segs=14, steps=()):
    """An upright flue that is OPEN at the top: wall with a small flared lip, a soot-blackened rim (the last
    2.4 px outside, the lip's top face and the inside) and a dark bore - the refinery vents smoke and fire
    here. top = (x, y, z, bore radius); steps = extra (r, z) profile points between z0 and the lip."""
    x, y, z1, r_in = top
    prof = [(r, z0)] + list(steps) + [(steps[-1][0] if steps else r, z1 - 1.3), ((steps[-1][0] if steps else r) + lip, z1 - 1.0)]
    r_top = prof[-1][0]
    parts.append(lathe(name, (x, y), prof, material, segs, cap=False))
    # sooty rim: outside of the lip, its top face, down the inside
    parts.append(lathe(name + "_soot", (x, y), [(r_top - lip + 0.03, z1 - 2.6), (r_top + 0.03, z1 - 1.05), (r_top + 0.03, z1), (r_in, z1), (r_in, z1 - 3.2)],
                       "soot", segs, cap=False))
    parts.append(lathe(name + "_bore", (x, y), [(r_in + 0.02, z1 - 3.4), (r_in + 0.02, z1 - 1.6)], "burnt", segs))


def stacks(base, main, damaged=False):
    d1, d2, tk = P["d1"], P["d2"], P["tank"]
    tops = stack_tops()
    if damaged:
        # battle damage: the right flue's top section is blown off and hangs bent over (the sprite's stack is 4 px
        # shorter), the thin left flue and the cream pipe are snapped, their ends bent aside - all still attached
        broken = dict(right=(5.5, 2.25, (4.2, -1.2, 2.6), "copper"), left_a=(8.5, 1.15, (-3.4, -0.6, 1.2), "copper_dk"),
                      tall_cream=(13.0, 1.1, (-3.0, -1.0, -2.2), "cream_pipe"))
        for k, (dz, r, (ex, ey, ez), m_) in broken.items():
            x, y, z, ri = tops[k]
            tops[k] = (x, y, z - dz, ri)
            main.append(tube(k + "_bent", (x + r * 0.5 * (1 if ex > 0 else -1), y, z - dz - 0.6), (x + ex, y + ey, z - dz + ez), r, m_, 12, r1=r * 0.9))
            main.append(tube(k + "_bent_soot", (x + ex, y + ey, z - dz + ez), (x + ex * 1.12, y + ey * 1.12, z - dz + ez * 1.12 + 0.05), r * 0.92, "burnt", 12))
    z1 = P["z_apex"] + 1.2
    # left roof: twin copper flues, open
    open_pipe(main, "pl_a", tops["left_a"], 1.15, z1, "copper_dk", lip=0.25, segs=12)
    open_pipe(main, "pl_b", tops["left_b"], 1.5, z1, "copper", lip=0.35, segs=12)
    main.append(lathe("pl_clamp", (d1[0] + 0.4, d1[1]), [(2.9, 40.0), (2.9, 41.2)], "copper_dk", 12))
    # right roof: the thick flue with a white collar and two lugs; open top with a wider sooty lip (the
    # sprite's dark, slightly wider top)
    zr = P["pole_r"]
    open_pipe(main, "pr", tops["right"], 2.5, z1, "copper", lip=0.55, segs=16, steps=((2.5, 38.0), (2.25, 39.0)))
    main.append(lathe("pr_collar", d2, [(3.3, 50.3), (3.7, 50.8), (3.7, 52.6), (3.3, 53.1)], "white", 16))
    for s in ((1,) if damaged else (-1, 1)):
        main.append(obox(f"pr_lug{s}", (d2[0] + s * 4.7, d2[1]), 0.0, 1.5, 0.9, 50.9, 52.5, "white", 0.15))
    main.append(lathe("pr_base", d2, [(3.6, z1 - 0.3), (3.6, z1 + 1.2), (2.6, z1 + 2.0)], "rim", 16))
    # the tank (base) and its tall twin flue (main)
    base.append(lathe("tank", tk, [(P["tank_r"] + 0.3, 0.0), (P["tank_r"] + 0.3, 0.8), (P["tank_r"], 1.0), (P["tank_r"], P["tank_h"] - 2.5)], "tank", 24))
    ring = lathe("tank_ring", tk, [(P["tank_r"] + 0.15, P["tank_h"] - 2.6), (P["tank_r"] + 0.15, P["tank_h"] - 0.9)], "HOUSE", 24)
    base.append(shade(ring, 0.22))
    base.append(lathe("tank_top", tk, [(P["tank_r"] + 0.25, P["tank_h"] - 1.0), (P["tank_r"] + 0.25, P["tank_h"]), (3.0, P["tank_h"] + 0.9)], "tank", 24))
    for k in range(3):                                   # hoops on the tank: modern detail
        z = 2.2 + 2.0 * k
        base.append(lathe(f"tank_hoop{k}", tk, [(P["tank_r"] + 0.12, z), (P["tank_r"] + 0.12, z + 0.3)], "block_dk", 24))
    open_pipe(main, "pt_cream", tops["tall_cream"], 1.1, P["tank_h"] + 0.3, "cream_pipe", lip=0.2, segs=12)
    open_pipe(main, "pt_copper", tops["tall_copper"], 1.95, P["tank_h"] + 0.6, "copper", lip=0.4, segs=14)
    main.append(lathe("pt_collar", tk, [(3.2, P["tank_h"] + 0.8), (3.2, P["tank_h"] + 1.8), (2.6, P["tank_h"] + 2.5)], "block", 16))
    for z in (30.0, 48.0):
        main.append(lathe(f"pt_clamp{int(z)}", (tk[0] - 0.5, tk[1]), [(3.1, z), (3.1, z + 1.0)], "copper_dk", 12))
    inlet(main)


def inlet_axis():
    """The unloading inlet's axis: (point on the copper flue's axis at the socket's height, unit direction) - it
    points horizontally south-east, at the dock cell, where a docked harvester's pipe comes from (the harvester
    stands on the dock at facing 12 with its rear to the refinery; tools/models/harvester_v1.py)."""
    d = Vector((math.sqrt(0.5), -math.sqrt(0.5), 0.0))
    if not P["inlet_2d"]:                                # the 3D layout: the mouth is given, the axis starts behind it
        return Vector(P["inlet_3d"]) - d * P["inlet"]["mouth"], d
    x, y, _z, _r = stack_tops()["tall_copper"]
    return Vector((x, y, P["inlet"]["z"])), d


def inlet_anchor():
    """fx_inlet_0 (docs/3d-models.md "Docking"): 1.6 px inside the socket's mouth, model px (x east, y north, z up)."""
    o, d = inlet_axis()
    return o + d * (P["inlet"]["mouth"] - 1.6)


def inlet(main):
    """The unloading inlet (main node: drawn OVER a docked harvester, so its pipe's nozzle disappears into the
    socket): a steel socket with a flared mouth and a dark bore branching off the tall copper flue, a clamp
    round the flue, a strut down to the tank's collar. In the original sprite the harvester's pipe simply ends
    on the tank's top at the foot of this flue."""
    il = P["inlet"]
    o, d = inlet_axis()
    side = Vector((d.y, -d.x, 0.0))

    def ring(t, r, segs=18):
        return [tuple(o + d * t + (side * math.cos(2 * math.pi * i / segs) + Vector((0, 0, 1)) * math.sin(2 * math.pi * i / segs)) * r) for i in range(segs)]

    # the socket: a tube with a bore (outer wall, flared lip, the inside)
    prof = [(il["back"], il["r_in"]), (il["back"], il["r_out"]), (il["mouth"] - 0.9, il["r_out"]), (il["mouth"] - 0.5, il["r_lip"]),
            (il["mouth"], il["r_lip"]), (il["mouth"], il["r_in"])]
    bm = bmesh.new()
    rs = [[bm.verts.new(p) for p in ring(t, r)] for (t, r) in prof]
    for a, b in zip(rs, rs[1:] + rs[:1]):
        for i in range(len(a)):
            j = (i + 1) % len(a)
            bm.faces.new([a[i], a[j], b[j], b[i]])
    main.append(new_obj("inlet_socket", bm, "inlet"))
    bm = bmesh.new()
    rings_solid(bm, [ring(il["back"] + 0.2, il["r_in"] + 0.02), ring(il["mouth"] - 1.6, il["r_in"] + 0.02)])
    main.append(new_obj("inlet_bore", bm, "burnt"))
    bm = bmesh.new()
    rings_solid(bm, [ring(il["mouth"] - 2.0, il["r_out"] + 0.18), ring(il["mouth"] - 1.45, il["r_out"] + 0.18)])
    main.append(new_obj("inlet_band", bm, "copper_dk"))
    if not P["inlet_2d"]:
        # 3D layout: the socket comes out of the north drum's wall, facing the dock - a collar where it leaves
        # the wall, a foot down to the drum's skirt
        d2 = Vector((*P["d2"], 0.0))
        back = o + d * il["back"]
        main.append(tube("inlet_collar", tuple(back - d * 1.6), tuple(back + d * 0.5), il["r_out"] + 0.7, "block_dk", 14))
        mid = o + d * (il["mouth"] - 1.7)
        inw = (d2 - Vector((mid.x, mid.y, 0.0))).normalized()
        main.append(tube("inlet_foot", (mid.x - inw.x * 0.6, mid.y - inw.y * 0.6, mid.z - il["r_out"] + 0.3), (mid.x + inw.x * 1.4, mid.y + inw.y * 1.4, P["z_foot"]), 0.7, "copper_dk", 8))
        return
    # a clamp round the flue and a strut down to the tank's collar
    main.append(lathe("inlet_clamp", (o.x, o.y), [(2.55, il["z"] - 2.4), (2.75, il["z"] - 2.0), (2.75, il["z"] + 2.0), (2.55, il["z"] + 2.4)], "copper_dk", 14))
    p = o + d * (il["mouth"] - 1.4)
    main.append(tube("inlet_strut", (p.x, p.y, il["z"] - il["r_out"] + 0.2), (o.x + d.x * 2.4, o.y + d.y * 2.4, P["tank_h"] + 1.0), 0.6, "copper_dk", 8))


def bridge(base, main):
    c = P["bridge_c"]
    d1, d2 = P["d1"], P["d2"]
    ang = math.degrees(math.atan2(d2[1] - d1[1], d2[0] - d1[0]))
    a = math.radians(ang)
    ax, ay = math.cos(a), math.sin(a)
    nx, ny = -ay, ax                                     # towards the north-west
    hl, se, nw, h = P["bridge_half"], P["bridge_se"], P["bridge_nw"], P["bridge_h"]
    mid = (nw - se) / 2
    cc = (c[0] + nx * mid, c[1] + ny * mid)
    main.append(obox("bridge", cc, ang, hl, (nw + se) / 2, 0.0, h, "block", 0.3))
    # the house-colour roof plate (dark shade) on the south-east part
    rc = (c[0] - ax * 1.5 - nx * 1.5, c[1] - ay * 1.5 - ny * 1.5)
    roof = obox("bridge_roof", rc, ang, hl - 1.5, 7.5, h - 0.2, h + 1.2, "HOUSE", 0.25)
    main.append(shade(roof, 0.025))
    main.append(obox("bridge_lip", rc, ang, hl - 0.9, 8.1, h - 0.7, h + 0.45, "block_dk", 0.15))
    # small light fittings on the house plate (the sprite's light dots)
    for k, (t, s) in enumerate(((0.0, 0.5), (-3.2, -2.6))):
        p = (rc[0] + ax * t + nx * s, rc[1] + ay * t + ny * s)
        main.append(obox(f"bridge_lamp{k}", p, ang, 0.9, 0.7, h + 1.1, h + 1.85, "white", 0.12))
    # vents on the south-east wall (modern detail) and a pipe run
    for k in (-1, 0, 1):
        p = (c[0] + ax * 2.6 * k - nx * (se + 0.05), c[1] + ay * 2.6 * k - ny * (se + 0.05))
        main.append(obox(f"bridge_vent{k}", p, ang, 0.9, 0.25, 12.0, 17.5, "recess"))
    p0 = (c[0] - ax * hl / 2 - nx * (se + 0.7), c[1] - ay * hl / 2 - ny * (se + 0.7), 6.0)
    p1 = (c[0] + ax * hl / 2 - nx * (se + 0.7), c[1] + ay * hl / 2 - ny * (se + 0.7), 6.0)
    main.append(tube("bridge_pipe", p0, p1, 0.6, "copper", 8))
    # the dome on the north-west side: a drum pedestal and a cream cap
    dc = P["dome_c"] if P["layout"] else (c[0] + nx * 10.5 - ax * 1.5, c[1] + ny * 10.5 - ay * 1.5)
    r, z = P["dome_r"], P["dome_z"]
    main.append(lathe("dome_ped", dc, [(r + 0.3, 0.0), (r + 0.3, z - 1.0), (r + 0.1, z)], "block", 24))
    prof = [(r * math.cos(t), z + r * 0.92 * math.sin(t)) for t in [math.pi / 2 * i / 7 for i in range(8)]]
    dome = lathe("dome", dc, prof, "roof", 24)
    main.append(dome)
    main.append(lathe("dome_band", dc, [(r + 0.3, z - 0.5), (r + 0.3, z + 0.35)], "rib", 24))
    main.append(lathe("dome_knob", dc, [(0.9, z + r * 0.9), (0.9, z + r * 0.92 + 0.9), (0.4, z + r * 0.92 + 1.3)], "copper_dk", 8))
    return dict(c=dc, obj=dome)


def pad(base, main):
    c, hl, hw = P["pad_c"], P["pad_len"], P["pad_wid"]
    h, fl, rim = P["pad_h"], P["pad_floor"], P["pad_rim"]
    ax, ay = math.cos(math.radians(45)), math.sin(math.radians(45))
    nx, ny = -ay, ax
    tray = []
    # the floor: yellow tray on the south-east 3/5, the ochre-brown apron (spilled spice, oil) towards tank and drum
    split = P["pad_split"]
    for nm, n0, n1, m_ in (("pad_floor", -(hw - rim * 0.5), split, "pad"), ("pad_apron", split, hw - rim * 0.5, "pad_dk")):
        cc = (c[0] + nx * (n0 + n1) / 2, c[1] + ny * (n0 + n1) / 2)
        tray.append(obox(nm, cc, 45.0, hl - rim * 0.5, (n1 - n0) / 2, 0.0, fl if m_ == "pad" else fl - 0.15, m_))
    cc = (c[0] + nx * split, c[1] + ny * split)
    tray.append(obox("pad_kerb", cc, 45.0, hl - rim, 0.45, 0.0, fl + 0.5, "pad_rim", 0.1))
    for s in (-1, 1):                                    # long rims
        cc = (c[0] + nx * s * (hw - rim / 2), c[1] + ny * s * (hw - rim / 2))
        tray.append(obox(f"pad_rimL{s}", cc, 45.0, hl, rim / 2, 0.0, h, "pad_rim", 0.25))
        cc = (c[0] + ax * s * (hl - rim / 2), c[1] + ay * s * (hl - rim / 2))
        tray.append(obox(f"pad_rimS{s}", cc, 45.0, rim / 2, hw, 0.0, h, "pad_rim", 0.25))
    # plate seams: modern detail under the harvester
    for k in (-0.5, 0.5):
        cc = (c[0] + ax * k * (hl * 0.66), c[1] + ay * k * (hl * 0.66))
        cc = (cc[0] + nx * (split - hw) / 2, cc[1] + ny * (split - hw) / 2)
        tray.append(obox(f"pad_seam{k}", cc, 45.0, 0.09, (split + hw - rim) / 2, fl - 0.05, fl + 0.05, "recess"))
    if P["layout"]:
        # 3D layout: the tray's corners are cut where it would leave the footprint (an octagon; the cut edges get
        # their rim), and the harvester stands on a round plate in the middle that is as high as the rims - so
        # nothing of the pad is higher than the surface it stands on (no bollards: its tracks are wider than the tray)
        reach = RD.BOX - c[0]
        tray = RD.clip_plan(tray, c, reach)
        for i, (cc, ang, half) in enumerate(RD.pad_chamfers(c, hl, hw, reach)):
            cc = (cc[0] - (rim / 2) * (1 if cc[0] > c[0] + reach - 0.1 else -1 if cc[0] < c[0] - reach + 0.1 else 0),
                  cc[1] - (rim / 2) * (1 if cc[1] > c[1] + reach - 0.1 else -1 if cc[1] < c[1] - reach + 0.1 else 0))
            tray.append(obox(f"pad_rimC{i}", cc, ang, half + rim * 0.2, rim / 2, 0.0, h, "pad_rim", 0.25))
        tray.append(lathe("pad_plate", c, [(4.6, fl - 0.1), (4.6, h - 0.12), (4.3, h)], "pad", 24))
        tray.append(lathe_loop("pad_plate_ring", c, [(2.2, h - 0.05), (2.2, h + 0.03), (1.9, h + 0.03), (1.9, h - 0.05)], "recess", 16))
        base.extend(tray)
        return
    base.extend(tray)
    for sl in (-1, 1):                                   # corner bollards
        for sw in (-1, 1):
            p = (c[0] + ax * sl * (hl - 0.9) + nx * sw * (hw - 0.9), c[1] + ay * sl * (hl - 0.9) + ny * sw * (hw - 0.9))
            base.append(lathe(f"pad_post{sl}{sw}", p, [(0.75, h - 0.1), (0.75, h + 0.9), (0.4, h + 1.2)], "copper_dk", 8))
    # the standpipe between tank and pad, elbowed into the right drum's skirt
    # (2D layout only: in 3D it stands where the docked harvester's rear is)
    if P["inlet_2d"]:
        sp = (18.0, 6.0)
        base.append(lathe("standpipe", sp, [(1.7, 0.0), (1.7, 15.0), (1.3, 16.2)], "copper", 12))
        base.append(lathe("standpipe_foot", sp, [(2.5, 0.0), (2.5, 1.0)], "copper_dk", 12))
        base.append(tube("standpipe_arm", (sp[0], sp[1], 13.6), (sp[0] + 1.0, sp[1] + 8.5, 9.0), 1.1, "copper", 10))


def extras(base, main):
    d1, d2 = P["d1"], P["d2"]
    # the white steps at the south-west of the left drum
    a = P["stairs_ang"]
    rf = P["r_foot"] - 24.3
    for k, (r_in, r_out, z) in enumerate(((rf + 22.0, rf + 24.6, 10.4), (rf + 24.4, rf + 26.0, 7.4), (rf + 25.8, rf + 27.2, 4.4))):
        cc = polar(d1, (r_in + r_out) / 2, a)
        base.append(obox(f"step{k}", cc, a, (r_out - r_in) / 2, 5.6, 0.0, z, "white", 0.25))
    # the right roof's raised wedge (the sprite's lighter sector with orange edges) and its orange pipe
    ang = -60.0
    w0, w1, lift = math.radians(-104.0), math.radians(-60.0), 0.7
    bm = bmesh.new()
    ring_t, ring_b = [], []
    for z_off, ring in ((lift, ring_t), (0.1, ring_b)):
        for i in range(5):
            p = polar(d2, P["r_roof"] + 0.5, math.degrees(w0 + (w1 - w0) * i / 4))
            ring.append(bm.verts.new((p[0], p[1], P["z_roof"] + z_off)))
        for i in range(4, -1, -1):
            p = polar(d2, 3.8, math.degrees(w0 + (w1 - w0) * i / 4))
            ring.append(bm.verts.new((p[0], p[1], cone_z(d2, p) + z_off)))
    bm.faces.new(ring_t)
    bm.faces.new(ring_b[::-1])
    for i in range(10):
        j = (i + 1) % 10
        bm.faces.new([ring_t[j], ring_t[i], ring_b[i], ring_b[j]])
    wedge = new_obj("wedge", bm, "roof")
    main.append(wedge)
    for cu in CUTTERS:                                   # damaged state: the blast holes go through it too
        cut([wedge], cu)
    pw = polar(d2, 3.8, -104.0), polar(d2, P["r_roof"] + 0.5, -104.0)
    main.append(tube("wedge_edge", (*pw[0], cone_z(d2, pw[0]) + lift + 0.2), (*pw[1], P["z_roof"] + lift + 0.2), 0.4, "orange", 6))
    p0, p1 = polar(d2, 3.6, ang), polar(d2, P["r_roof"] + 0.6, ang)
    main.append(tube("orange_pipe", (*p0, cone_z(d2, p0) + 1.2), (*p1, P["z_roof"] + 1.1), 0.6, "orange", 8))
    p2 = polar(d2, P["r_rim"] + 0.5, ang)
    main.append(tube("orange_drop", (*p1, P["z_roof"] + 1.1), (*p2, P["z_rim0"] - 0.5), 0.6, "orange", 8))
    # hatch on the left roof and a ladder up the left drum's south-east wall: modern detail
    hp = polar(d1, 12.6 * P["drum_k"], 120.0)
    main.append(lathe("hatch", hp, [(1.9, cone_z(d1, hp) - 0.2), (1.9, cone_z(d1, hp) + 0.55), (1.5, cone_z(d1, hp) + 0.7)], "rim", 12))
    for s in (-1, 1):
        la = -48.0 + s * 2.4
        base.append(tube(f"ladder{s}", polar(d1, P["r_band"] + 0.7, la, P["z_band1"]), polar(d1, P["r_arch1"] + 0.9, la, P["z_corn"] + 0.2), 0.16, "copper_dk", 6))
    for k in range(5):
        t = (k + 0.5) / 5
        r = P["r_band"] + 0.7 + (P["r_arch1"] + 0.9 - P["r_band"] - 0.7) * t
        z = P["z_band1"] + (P["z_corn"] + 0.2 - P["z_band1"]) * t
        base.append(tube(f"rung{k}", polar(d1, r, -50.4, z), polar(d1, r, -45.6, z), 0.13, "copper_dk", 6))


def damage(base, main, dome):
    """The rest of the damaged state (frame first + 2): the dome holed, scorch on the bridge and round the dome."""
    import random
    rnd = random.Random(11)
    dc = dome["c"]
    cut([dome["obj"]], cutter((dc[0] + 2.2, dc[1] - 1.8), 3.6, 31, P["dome_z"] + 1.0, P["dome_z"] + 12.0))
    main.append(lathe("dome_inside", dc, [(P["dome_r"] - 0.7, P["dome_z"] + 0.2), (P["dome_r"] * 0.75, P["dome_z"] + 3.4), (0.02, P["dome_z"] + 4.6)], "burnt", 16))
    for i in range(3):                                   # shell fragments bent outwards, still attached
        a = rnd.uniform(-90, 40)
        e = polar((dc[0] + 2.2, dc[1] - 1.8), 3.0, a)
        q = polar((dc[0] + 2.2, dc[1] - 1.8), 5.0, a + rnd.uniform(-15, 15))
        main.append(beam(f"dome_tear{i}", (*e, P["dome_z"] + 3.6), (*q, P["dome_z"] + rnd.uniform(3.0, 5.5)), 1.8, 0.35, "roof2"))
    bc = P["bridge_c"]
    for i, (dx, dy, r) in enumerate(((2.5, -1.0, 3.0), (-4.0, -3.5, 2.2), (5.5, 2.5, 1.8))):
        pts = star((bc[0] + dx, bc[1] + dy), r, 40 + i, 9)
        bm = bmesh.new()
        rings_solid(bm, [[(x, y, P["bridge_h"] + 1.0) for (x, y) in pts], [(x, y, P["bridge_h"] + 1.3) for (x, y) in pts]])
        main.append(new_obj(f"bridge_scorch{i}", bm, "burnt"))


def build(damaged=False):
    layout_3d()
    root = bpy.data.objects.new(KEY, None)
    bpy.context.scene.collection.objects.link(root)
    base, main = [], []
    # blast holes where the sprite's damaged frame has them: (direction deg, distance from the roof centre, radius,
    # seed, through the rim). Left roof: its west / south-west third, a small one north-east. Right roof: front
    # centre (the big dark hole under the wedge), back left.
    drum(base, main, "dl", P["d1"], ((205.0, 10.5, 9.8, 3, True), (60.0, 11.5, 3.6, 5, False)) if damaged else ())
    drum(base, main, "dr", P["d2"], ((258.0, 10.5, 6.6, 8, True), (160.0, 12.0, 5.2, 9, True), (35.0, 12.5, 3.2, 12, False)) if damaged else ())
    stacks(base, main, damaged)
    dome = bridge(base, main)
    pad(base, main)
    extras(base, main)
    if damaged:
        damage(base, main, dome)
    nodes = []
    for name, parts in (("base", base), ("main", main)):
        node = V3.finish_node(parts, name, (0, 0, 0))
        node.parent = root
        nodes.append(node)
    # Effect anchors (docs/3d-models.md "Anchors"): empties fx_flue_0..4 on the open flue tops - Graphics: 3D
    # vents the refinery's fire / smoke there; the scale is the strength of that flue's vent.
    tops = stack_tops()
    for i, (key, power) in enumerate(FLUE_ANCHORS):
        x, y, z, _r = tops[key]
        e = bpy.data.objects.new(f"fx_flue_{i}", None)
        bpy.context.scene.collection.objects.link(e)
        e.location = (x * PX, y * PX, z * PX)
        e.scale = (power, power, power)
        e.parent = root
    if not P["inlet_2d"]:                                # the harvester inlet (docs/3d-models.md "Docking")
        q = inlet_anchor()
        e = bpy.data.objects.new("fx_inlet_0", None)
        bpy.context.scene.collection.objects.link(e)
        e.location = (q.x * PX, q.y * PX, q.z * PX)
        e.parent = root
    for o in CUTTERS:
        bpy.data.objects.remove(o)
    CUTTERS.clear()
    bpy.context.view_layer.update()
    return root, nodes


# ------------------------------------------------------------------------------------------------
# Materials


def mat(name):
    m = V3.mat(name)
    c = PREVIEW_HOUSE if name == "HOUSE" else V3.COL[name]
    m.diffuse_color = (*srgb_to_linear(c), 1)
    return m


_V3_SURFACE = V3.surface_tree


def surface(nt, name):
    """V3's weathering tree + what a building adds: sand blown up the walls, rain / rust streaks down the
    cream panels from the rim, a darker, rustier foot on the skirts, tyre dirt on the pad."""
    c, r, m = _V3_SURFACE(nt, name)
    nodes, links = nt.nodes, nt.links
    geo = nodes.new("ShaderNodeNewGeometry")
    pos = nodes.new("ShaderNodeSeparateXYZ")
    links.new(geo.outputs["Position"], pos.inputs[0])

    def remap(sock, a_, b_, c_, d_):
        mr = nodes.new("ShaderNodeMapRange")
        mr.inputs["From Min"].default_value = a_
        mr.inputs["From Max"].default_value = b_
        mr.inputs["To Min"].default_value = c_
        mr.inputs["To Max"].default_value = d_
        links.new(sock, mr.inputs["Value"])
        return mr.outputs[0]

    def mix(fac, a_, col):
        mx = nodes.new("ShaderNodeMix")
        mx.data_type = "RGBA"
        links.new(fac, mx.inputs["Factor"])
        links.new(a_, mx.inputs[6])
        mx.inputs[7].default_value = (*srgb_to_linear(col), 1)
        return mx.outputs[2]

    def mul(a_, b_):
        mm = nodes.new("ShaderNodeMath")
        mm.operation = "MULTIPLY"
        mm.use_clamp = True
        links.new(a_, mm.inputs[0])
        if isinstance(b_, float):
            mm.inputs[1].default_value = b_
        else:
            links.new(b_, mm.inputs[1])
        return mm.outputs[0]

    def noise(scale, stretch=(1.0, 1.0, 1.0)):
        n = nodes.new("ShaderNodeTexNoise")
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = 3.0
        mp = nodes.new("ShaderNodeMapping")
        mp.inputs["Scale"].default_value = stretch
        links.new(geo.outputs["Position"], mp.inputs["Vector"])
        links.new(mp.outputs[0], n.inputs["Vector"])
        return n.outputs["Fac"]

    if name in ("skirt", "fin", "foot", "block", "white", "tank", "pad_rim"):
        # sand drifted against everything that stands on the ground (up to 5 px)
        low = remap(pos.outputs["Z"], 5.5 * PX, 0.5 * PX, 0.0, 1.0)
        c = mix(mul(mul(low, remap(noise(9.0), 0.3, 0.7, 0.35, 1.0)), 0.7), c, (0.66, 0.55, 0.40))
    if name in ("wall", "block", "roof", "roof2", "white", "cream_pipe"):
        # brown run-off streaks (stretched along z on walls; broad stains on the roofs)
        st = remap(noise(26.0, (1.0, 1.0, 0.04)), 0.55, 0.72, 0.0, 1.0)
        nz = nodes.new("ShaderNodeSeparateXYZ")
        links.new(geo.outputs["Normal"], nz.inputs[0])
        wall = remap(nz.outputs["Z"], 0.8, 0.4, 0.0, 1.0)
        c = mix(mul(mul(st, wall), 0.55), c, (0.46, 0.36, 0.26))
        stain = remap(noise(3.2), 0.5, 0.66, 0.0, 1.0)
        c = mix(mul(stain, 0.55), c, (0.60, 0.48, 0.33))
        stain2 = remap(noise(9.5), 0.5, 0.66, 0.0, 1.0)
        c = mix(mul(stain2, 0.42), c, (0.52, 0.39, 0.26))
        speck = remap(noise(46.0), 0.56, 0.7, 0.0, 1.0)
        c = mix(mul(speck, 0.5), c, (0.40, 0.31, 0.22))
    if name in ("copper", "copper_dk", "cream_pipe"):
        # soot runs down from the open tops: the upper 9 px of every flue darken towards the rim
        ph = nodes.new("ShaderNodeAttribute")
        ph.attribute_name = "grime_h"
        top = remap(ph.outputs["Fac"], 0.72, 1.0, 0.0, 1.0)
        c = mix(mul(mul(top, remap(noise(20.0, (1.0, 1.0, 0.15)), 0.3, 0.7, 0.45, 1.0)), 0.85), c, (0.16, 0.13, 0.11))
    if name in ("pad", "pad_rim"):
        # worn in patches
        worn = remap(noise(6.0, (1.0, 1.0, 1.0)), 0.45, 0.7, 0.0, 1.0)
        c = mix(mul(worn, 0.22), c, (0.80, 0.58, 0.24) if name == "pad" else (0.66, 0.60, 0.50))
    return c, r, m


def procedural_materials():
    out = {}
    for name, m in list(V3._mats.items()):
        if name.startswith("HOUSE"):
            continue
        nt = m.node_tree
        b = nt.nodes["Principled BSDF"]
        c, r, mt = surface(nt, name)
        nt.links.new(c, b.inputs["Base Color"])
        nt.links.new(r, b.inputs["Roughness"])
        nt.links.new(mt, b.inputs["Metallic"])
        em = nt.nodes.new("ShaderNodeEmission")
        em.name = "BAKE_EMIT"
        out[name] = dict(mat=m, sockets=(c, r, mt), emit=em)
    return out


def bake_and_merge(objs, size, samples=24):
    """Bake all materials but HOUSE into one atlas material "body" and remap the slots (V3's bake)."""
    procs = procedural_materials()
    V3.uv_unwrap(objs, margin=0.002)
    images = V2.bake_atlas(objs, procs, size, samples)
    mr = V3.pack_mr(images, size)
    body_mat = V3.export_material(images, mr, os.path.join(bpy.app.tempdir, "d2k_refinery_fable_bake"))
    for o in objs:
        me = o.data
        for s in o.material_slots:
            if s.material and not s.material.name.startswith("HOUSE"):
                s.material = body_mat
        idx_map, keep = {}, []
        for s in me.materials:
            key = s.name if s else ""
            if key not in idx_map:
                idx_map[key] = len(keep)
                keep.append(s)
        new_index = {si: idx_map[(s.name if s else "")] for si, s in enumerate(me.materials)}
        old_idx = [p.material_index for p in me.polygons]
        me.materials.clear()
        for s in keep:
            me.materials.append(s)
        for p, oi in zip(me.polygons, old_idx):
            p.material_index = new_index[oi]
    return body_mat


def make_split(nodes):
    """The "split" twin: every face of the main node becomes HOUSE, every face of the base node a plain
    material - rendered by the pack driver only for its mask pass (R = the main frame's coverage, with
    the occlusion between the two layers resolved)."""
    plain = bpy.data.materials.new("plain")
    house = V3.mat("HOUSE")
    for o in nodes:
        me = o.data
        me.materials.clear()
        me.materials.append(house if o.name.startswith("main") else plain)
        for p in me.polygons:
            p.material_index = 0


# ------------------------------------------------------------------------------------------------
# Review renders


def review_renders(root, out_dir, samples, size=800):
    os.makedirs(out_dir, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    V3.setup_device(scene)
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.render.film_transparent = False
    scene.render.resolution_x = scene.render.resolution_y = size
    scene.render.resolution_percentage = 100
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    for m in bpy.data.materials:
        if m.name.startswith("HOUSE") and m.use_nodes:
            tint = m.node_tree.nodes.get("HOUSE_TINT")
            if tint:
                tint.inputs[6].default_value = (*srgb_to_linear(PREVIEW_HOUSE), 1)
    bpy.ops.mesh.primitive_plane_add(size=60, location=(0, 0, -0.0005))
    gp = bpy.context.active_object
    gm = bpy.data.materials.new("sand")
    gm.use_nodes = True
    gb = gm.node_tree.nodes["Principled BSDF"]
    gb.inputs["Base Color"].default_value = (*srgb_to_linear((0.77, 0.63, 0.43)), 1)
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
    tgt = Vector((0.0, 0.5, 0.55))

    def shot(name, az_deg, el_deg, dist, lens, target=tgt, sun_az=150.0, sun_el=50.0):
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

    for i in range(8):
        az = -90 - 45 * i                                  # 0: the game's view (from the south), then clockwise
        shot(f"turn_{i}", az, 32 if i % 2 == 0 else 24, 11.5, 70, sun_az=az - 65)
    scene.render.resolution_x, scene.render.resolution_y = 1400, 900
    shot("closeup_game_view", -90, 45, 9.5, 70, sun_az=-145)
    shot("closeup_pad", -40, 20, 5.0, 70, target=Vector((0.75, 0.1, 0.3)), sun_az=-120)
    shot("closeup_left_drum", -120, 16, 4.6, 70, target=Vector((-0.7, 0.0, 0.5)), sun_az=-170)
    shot("closeup_roofs", -75, 58, 7.5, 80, target=Vector((0.0, 0.6, 0.8)), sun_az=-150)
    bpy.data.objects.remove(gp)
    bpy.data.objects.remove(so)
    bpy.data.objects.remove(co)


# ------------------------------------------------------------------------------------------------


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opt = dict(out=None, bake=True, bake_size=2048, turntable=None, samples=48, variants=True, damaged=True)
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
        elif a == "--inlet":
            # models.ini line of the harvester inlet (glb axes: x east, y up, z south; tiles)
            layout_3d()
            for n_, (key, power) in enumerate(FLUE_ANCHORS):
                x, y, z, _r = stack_tops()[key]
                print(f"anchor.fx_flue_{n_} = {x * PX:.4f} {z * PX:.4f} {-y * PX:.4f} {power:g}")
            if not P["inlet_2d"]:
                q = inlet_anchor()
                print(f"anchor.fx_inlet_0 = {q.x * PX:.4f} {q.z * PX:.4f} {-q.y * PX:.4f}")
            return
        elif a == "--tops":
            # px relative to the building's top-left map pixel (tileX*32, tileY*32), as the game camera projects them
            for k, (x, y, z, r) in stack_tops().items():
                print(f"{k:12s} model px ({x:+.2f}, {y:+.2f}, {z:.2f})  tiles ({x * PX:+.4f}, {y * PX:+.4f}, {z * PX:.4f})  "
                      f"map px from the building's top-left ({48 + x:.1f}, {48 - 0.70711 * (y + z):.1f})  bore r {r}")
            return
        elif a == "--bake-size":
            opt["bake_size"] = int(argv[i + 1]); i += 1
        elif a == "--turntable":
            opt["turntable"] = argv[i + 1]; i += 1
        elif a == "--samples":
            opt["samples"] = int(argv[i + 1]); i += 1
        elif a == "--device":
            V3.DEVICE = argv[i + 1].lower(); i += 1
        elif a == "--set":
            k, v = argv[i + 1].split("=", 1)
            if k not in P:
                raise SystemExit("unknown proportion " + k)
            P[k] = float(v) if isinstance(P[k], float) else type(P[k])(eval(v))   # noqa: S307 (own CLI)
            i += 1
        else:
            raise SystemExit("unknown option " + a)
        i += 1
    if opt["out"] is None:
        opt["out"] = os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-refinery-fable")
    V2.setup_gpu = V3.setup_device        # the bake helpers of v2 pick the device through this
    V3.P["house_low"] = P["house_low"]

    jobs = [("", False, False)]
    if opt["variants"]:
        jobs += [("_split", False, True)] + ([("_damaged", True, False), ("_damaged_split", True, True)] if opt["damaged"] else [])
    for suffix, damaged, split in jobs:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        V3._mats.clear()
        t0 = time.time()
        root, nodes = build(damaged)
        for name in list(V3._mats):
            mat(name)
        log(f"built{suffix}: {V2.tri_count(V2.meshes_under(root))} triangles in {time.time() - t0:.1f} s")
        if split:
            make_split(nodes)
        elif opt["bake"]:
            t1 = time.time()
            bake_and_merge(nodes, opt["bake_size"])
            log(f"textures baked in {time.time() - t1:.1f} s ({opt['bake_size']} px atlas)")
        if suffix:
            path = os.path.join(opt["out"], "variants", KEY + suffix + ".glb")
        else:
            path = os.path.join(opt["out"], "models", "building", KEY + ".glb")
        V3.export_glb(root, path)
        log(f"exported {path} ({os.path.getsize(path) // 1024} KB)")
        if not suffix and opt["turntable"]:
            t2 = time.time()
            review_renders(root, opt["turntable"], opt["samples"])
            log(f"review renders in {time.time() - t2:.1f} s -> {opt['turntable']}")
    log("2D pack" + ("" if P["inlet_2d"] else " (build with --set inlet_2d=1 first: this is the 3D layout)") +
        ": .venv/bin/python -I tools/models/refinery_fable_check/pack.py --models " + opt["out"] + " --out artpacks/rendered-refinery-fable")


if __name__ == "__main__":
    main()

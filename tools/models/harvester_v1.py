"""Spice harvester, v1 - the ORIGINAL design rebuilt as a modern hard-surface model, with the docking /
unloading poses (same method and kit as combat_tank_v3.py / devastator_v1.py, whose helpers, weathering
and bake it reuses).

    blender -b --factory-startup --python tools/models/harvester_v1.py -- \
        [--out DIR]            write DIR/models/unit/HARVESTER.glb and DIR/variants/HARVESTER_unloadNN.glb
                               (default artpacks/models-harvester; --model-pack takes DIR)
        [--no-bake]            plain colours instead of the baked atlas (fast iteration)
        [--no-variants]        only the driving model
        [--bake-size 2048]     atlas size
        [--set key=value ...]  override a proportion in P (px)
        [--iou ORIG.npz]       silhouette IoU against the original frames (32 facings, game camera;
                               harvester_check/ref.py writes the npz)
        [--flat DIR] [--posmap FILE.npz]   with --iou: flat-colour facings / model points per pixel
        [--turntable DIR]      review renders: 8 views + close-ups (house colour = Atreides blue)
        [--pose K]             build the model in unloading pose K (0..10) - with --turntable: the docked look
        [--nozzle]             print where the unloading pipe's nozzle is in every pose and exit
        [--samples 64] [--device cpu|gpu]

    then the 2D pack (the harvester's own driver around tools/render_sprites):
    .venv/bin/python -I tools/models/harvester_check/pack.py

ONE template and one art for all three houses (Templates.bin HARVESTER, owner mask 127, unit art 9 =
DATA.R16 frames 1934-1965, no barrel art); model key HARVESTER.

Conventions (docs/3d-models.md "New models"): Blender units = tiles, +X east, +Y north = the vehicle's
front, +Z up, origin on the ground on the turning axis. ONE node "body". Materials: everything but the
house band is baked into ONE atlas material "body"; "HOUSE" is the house-colour mask.
Everything is authored in ORIGINAL SPRITE PIXELS (1 px = 1/32 tile; u forward, v right, z up).

How the original was read (nothing is copied: a visual hull carved out of the 32 silhouettes with the
game camera, least-squares position of the house band over all facings, pixel positions in the four
cardinal views; tools/models/harvester_check/; the frames turn about the centre of the hotspot pixel,
about 5.5 px above the ground):

  * A fat lemon-shaped hull of longitudinal shell plates ("gores"), 30 px long (u -17..+13, its middle
    2 px behind the turning axis), widest (about +-10.5 px) at 8 px above the ground, top at 14 px.
    Warm peach-cream, lit from the upper left, dark brown below.
  * Along its top a slightly raised dorsal cover 8 px wide with the house band across it (u -2.5,
    z 14.1) - the only house-colour area (1.9 % of the sprite). With the uppermost shell plate on either
    side it is the part that lifts while unloading (vertex group "lid").
  * Round 2 (owner): along the cover's middle a thin raised CREST (2.5 px wide, 2 px high) - the top is not a
    flat roof. The sides: the big light bulge of the side views is the UPPER flank (the two shell plates
    between the cover and the hull's widest line, 20..90 degrees round from the top - it faces the camera):
    those are the bright plates, the cover above them tan and ribbed, the plates below the widest line
    darker; the wheels sit close under the flank (small dark frame).
  * Six balloon wheels, plain light balls: front and rear ones 13.6 px off the centre line, the middle
    pair bigger and further out (15.8 px) under a shoulder housing - the plan is a diamond.
  * At the front a neck going down to a dark maroon spice head on the ground (u 15..23), steel hoses
    on both sides; above it a stalk with a boxy cream sensor head (u 12, z 18).
  * At the rear a dark maroon exhaust stack leaning back (from u -13, z 12 to u -18.5, z 20).

Docking / unloading (re/notes/economy.md, rendering.md): on the refinery's dock cell the harvester turns
to facing 12 (south-east, its rear to the refinery) and is then drawn with the direction-less frames of
animation art 0 (EX_HARV, DATA.R16 4273-4283) instead of its facing frame: frame k = 1..10 while it
enters (one step every 5 ticks), frame 10 while it unloads, back down when it leaves. In the original
those frames show: 1-7 the dorsal cover with the exhaust stack tilting up about a hinge at its front
end (its rear rises about 6 px; here the cover, the uppermost shell plate on either side and the sensor
stalk lift together, to 21 degrees), 8-10 a steel pipe with a wider nozzle telescoping out of the hull's
rear tip towards the refinery (the nozzle ends 21 px left of and 15 px above the hotspot). Here the nozzle
(5 px long) reaches the inlet's mouth in frame 9 and is 1.6 px inside it in frame 10. POSES below
reproduce that: the cover's angle and the pipe's extension per frame. The pipe ends in a socket on the
redesigned refinery's tall stack (tools/models/refinery_pilot_fable.py "inlet"), which is drawn over the
harvester: the nozzle slides into it.

Look: as the tank / devastator (dust film, blotches, cavity dirt, grime, streaks, chips), browner -
the original's shades are warm browns, a third of the sprite is darker than 60.
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
import devastator_v1 as DV   # noqa: E402  more helpers, flat-colour IoU, position maps, review renders

# the pack driver (harvester_check/pack.py) reads these
RENDER = dict(view_transform="Standard", look="High Contrast", exposure=0.0, house_grey=0.6, sky_strength=0.05,
              shadow_azimuth=-10.0, sun_elevation=54.0, sun_strength=1.95, fill=(0.7, 60.0, 28.0),
              contour=0.7, contour_width=1.0, contour_azimuth=-70.0, sharpen=0.9, shadow_strength=1.0,
              # a tone curve over the finished frames (luminance in -> out, piecewise linear): the original has few mid tones
              tone=((0.0, 0.0), (0.25, 0.22), (0.5, 0.42), (0.75, 0.78), (1.0, 1.0)))
KEY = "HARVESTER"
PX = V3.PX
log = V2.log
srgb_to_linear = V2.srgb_to_linear

P = dict(
    grow=1.0,
    # --- hull: stations along u, cross-section a superellipse (exponent n) of half-width w, half-height c about zc
    hull_u=(-17.8, 13.2), hull_b=10.2, hull_zc=7.95, hull_c=5.75, hull_c_tip=2.7, hull_n=2.1,
    plan=[(0.0, 1.0), (0.15, 0.975), (0.3, 0.86), (0.5, 0.63), (0.75, 0.40), (0.92, 0.23), (1.0, 0.10)],   # |t| -> w / hull_b, front half
    plan_rear=[(0.0, 1.0), (0.15, 0.975), (0.3, 0.89), (0.5, 0.75), (0.75, 0.58), (0.92, 0.42), (1.0, 0.2)],   # the rear is blunter
    gores=[(19.0, 46.0, "hull"), (46.0, 90.0, "hull"), (90.0, 124.0, "hull2"), (124.0, 180.0, "under")],   # deg from the top, right side
    gore_gap=0.6, gore_sink=0.22,     # each plate's edges are drawn in (deg) and sunk (px): the seams
    lid_phi=19.0, lid_u=(-15.8, 7.4), lid_up=0.55, lid_t=1.3,
    hinge=(7.4, 13.3),                # the cover's hinge (u, z)
    band_u=(-4.1, -0.9), band_up=0.28,
    ribs_u=(-13.2, -10.4, -7.4, 1.6, 4.4), hoops_u=(-9.4, 3.6),
    # --- wheels (u, |v|, radius)
    wheels=[(5.6, 12.9, 4.1), (-2.8, 15.0, 4.5), (-11.3, 12.8, 4.1)], wheel_squash=1.0,
    shoulder=dict(u=-2.8, v=9.2, z=8.0, r=(5.2, 2.7, 3.0)),
    chassis=dict(u=(-12.0, 8.0), v=5.2, z=(1.6, 4.8)),
    # the crest: a narrow raised spine along the dorsal cover (u range, half width at its foot / top, height)
    crest=dict(u=(-10.6, 6.6), w=(1.3, 0.75), h=2.1, notch=3.0),
    # --- front: neck, spice head, hoses, sensor
    neck=[(9.0, 5.4), (16.5, 5.2), (16.5, 8.0), (9.0, 10.0)], neck_v=2.4,
    head=[(15.6, 2.9), (23.3, 2.9), (23.5, 4.6), (22.0, 7.4), (17.6, 8.6), (15.6, 8.0)], head_v=3.2,
    lug=dict(u=(16.2, 19.4), v=3.7, z=(3.4, 7.0)),
    hose=((9.2, 4.6, 6.6), (17.4, 3.6, 6.2), 0.55),
    stalk=((4.2, 13.9), (11.6, 20.3), 0.75), knob=dict(u=(10.2, 14.8), v=2.1, z=(19.5, 23.0)),
    # --- rear: exhaust stack on the cover
    stack=dict(base=(-12.8, 12.2), dir=(-0.6, 0.8), len=10.8, hv=(2.6, 2.1), ht=(1.75, 1.5)),
    # --- the unloading pipe (retracted inside the hull's rear; POSES push it out along -u)
    pipe=dict(z=10.6, tip=-15.4, len=15.0, r=1.1, head=5.0, head_r=1.7, lip_r=2.0),
    house_band=0.68, house_low=0.8,
)
# frame k of the unloading art: (cover angle deg, pipe extension px)
POSES = [(0.0, 0.0), (3.0, 0.0), (6.0, 0.0), (9.0, 0.0), (12.0, 0.0), (15.0, 0.0), (18.0, 0.0), (21.0, 0.0),
         (21.0, 4.8), (21.0, 9.3), (21.0, 10.95)]

COL = dict(
    hull=(1.000, 0.950, 0.890),      # shell plates: the original's peach cream
    hull2=(0.880, 0.785, 0.700),     # every other plate, the neck, the shoulders
    lid=(0.800, 0.700, 0.610),       # the dorsal cover
    rib=(0.500, 0.390, 0.310),       # its cross ribs, the hoops
    under=(0.300, 0.215, 0.165),     # the belly plates
    tyre=(0.985, 0.870, 0.785),      # balloon wheels
    tyre_dk=(0.620, 0.490, 0.390),   # tread grooves
    chassis=(0.170, 0.115, 0.090),   # frame, axles, hubs
    maroon=(0.300, 0.160, 0.150),    # spice head, exhaust stack
    maroon_lt=(0.470, 0.340, 0.280), # their lighter collars
    steel=(0.720, 0.710, 0.670),     # hoses, the unloading pipe
    steel_dk=(0.400, 0.390, 0.365),
    recess=(0.085, 0.070, 0.060),
    bay=(0.400, 0.315, 0.265),       # the hull under the shell plates: shows in the seams and under the open cover
)
SPEC = dict(
    hull=(0.0, 0.3, "paint"), hull2=(0.0, 0.38, "paint"), lid=(0.0, 0.45, "paint"), rib=(0.0, 0.55, "paint"),
    under=(0.0, 0.7, "paint"), tyre=(0.0, 0.3, "steel"), tyre_dk=(0.0, 0.8, "dark"), chassis=(0.2, 0.7, "dark"),
    maroon=(0.1, 0.5, "paint"), maroon_lt=(0.1, 0.5, "paint"), steel=(0.7, 0.35, "steel"), steel_dk=(0.6, 0.45, "steel"),
    recess=(0.0, 0.9, "dark"), bay=(0.2, 0.7, "paint"),
)
FILM = dict(
    hull=(0.930, 0.855, 0.780), hull2=(0.770, 0.680, 0.600), lid=(0.690, 0.600, 0.520), rib=(0.420, 0.330, 0.260),
    under=(0.300, 0.220, 0.170), tyre=(0.870, 0.760, 0.680), maroon=(0.300, 0.190, 0.170), maroon_lt=(0.420, 0.310, 0.260),
    steel=(0.560, 0.530, 0.480), steel_dk=(0.360, 0.340, 0.310), bay=(0.340, 0.270, 0.225),
)
PREVIEW_HOUSE = (0.42, 0.58, 0.74)   # Atreides blue (COLOURS.BIN house 0, shade 9), previews only
KEEP = ("HOUSE",)

V3.COL.update(COL)
V3.SPEC.update(SPEC)
V3.FILM.clear()
V3.FILM.update(FILM)
V3.WEATHER["paint"] = (0.6, 0.5, 0.95, 0.75, 0.8, 0.6, 0.5, 0.4)      # film, blotch, cavity, soft, grime, streak, chip, low dust
V3.WEATHER["steel"] = (0.7, 0.3, 0.80, 0.40, 0.4, 0.3, 0.0, 0.1)
V3.DIRT.update(cavity=(0.085, 0.058, 0.045), soft=(0.400, 0.310, 0.255), blotch=(0.580, 0.480, 0.400), grime=(0.340, 0.255, 0.205),
               streak=(0.360, 0.265, 0.205), chip=(0.250, 0.180, 0.140), sand=(0.640, 0.520, 0.400))
V3.SOOT.clear()
V3.GRIME_SCALE.clear()
V3.GRIME_SCALE.update(steel=0.5, tyre=0.8)
V3.UV_SCALE.update(recess=0.4, chassis=0.5, under=0.6, tyre_dk=0.4, steel_dk=0.6, HOUSE=0.06)
DV.PREVIEW_HOUSE = PREVIEW_HOUSE
DV.P["house_low"] = P["house_low"]

box, prism_uz, cyl, loft, new_obj, rings_solid = V3.box, V3.prism_uz, V3.cyl, V3.loft, V3.new_obj, V3.rings_solid
shade, sphere = DV.shade, DV.sphere


def group(o, name):
    """Mark a part as belonging to a moving group ("lid", "pipe"): a vertex group that survives the join."""
    o["d2k_group"] = name
    return o


def tube(name, p0, p1, r, material, segs=10, r1=None):
    """Cylinder between two points (v, u, z)."""
    a, b = Vector(p0), Vector(p1)
    d = (b - a).normalized()
    s = d.orthogonal().normalized()
    t = d.cross(s)
    rb = r if r1 is None else r1
    ring0 = [tuple(a + (s * math.cos(2 * math.pi * i / segs) + t * math.sin(2 * math.pi * i / segs)) * r) for i in range(segs)]
    ring1 = [tuple(b + (s * math.cos(2 * math.pi * i / segs) + t * math.sin(2 * math.pi * i / segs)) * rb) for i in range(segs)]
    bm = bmesh.new()
    rings_solid(bm, [ring0, ring1])
    return new_obj(name, bm, material)


def ellipsoid(name, c, r, material, segs=20, rings=12):
    """Ellipsoid centred on c = (v, u, z) with radii r = (rv, ru, rz)."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segs, v_segments=rings, radius=1.0)
    bmesh.ops.scale(bm, verts=bm.verts, vec=Vector(r))
    bmesh.ops.translate(bm, verts=bm.verts, vec=Vector(c))
    return new_obj(name, bm, material)


# ------------------------------------------------------------------------------------------------
# The hull: a lemon of shell plates


def interp(table, x):
    for (x0, y0), (x1, y1) in zip(table, table[1:]):
        if x <= x1:
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0)
    return table[-1][1]


def section(u):
    """(w, c) of the hull's cross-section at u."""
    u0, u1 = P["hull_u"]
    t = (u - (u0 + u1) / 2) / ((u1 - u0) / 2)
    t = max(-1.0, min(1.0, t))
    w = P["hull_b"] * interp(P["plan"] if t >= 0 else P["plan_rear"], abs(t))
    c = P["hull_c_tip"] + (P["hull_c"] - P["hull_c_tip"]) * (1 - abs(t) ** 3.0) ** 0.5
    return w, c


def hull_pt(u, phi_deg, sink=0.0, up=0.0):
    """Point (v, u, z) of the hull's surface at u, phi degrees round from the top (positive: right);
    sink: pulled towards the axis by that many px; up: raised."""
    w, c = section(u)
    a = math.radians(phi_deg)
    e = 2.0 / P["hull_n"]
    s, co = math.sin(a), math.cos(a)
    v = math.copysign(abs(s) ** e, s) * max(0.05, w - sink)
    z = math.copysign(abs(co) ** e, co) * max(0.05, c - sink)
    return (v, u, P["hull_zc"] + z + up)


def stations(u0, u1, step=1.0):
    n = max(2, int(round((u1 - u0) / step)))
    return [u0 + (u1 - u0) * i / n for i in range(n + 1)]


def shell(name, u0, u1, phi0, phi1, material, up=0.0, thick=1.2, sink_edge=0.0, n_phi=6, step=1.0, ends=0.0, proud=0.0):
    """A plate of the hull's shell between two angles: outer surface (raised by `up`, standing `proud` of the
    hull, its two long edges sunk by sink_edge) and an inner one `thick` below. ends: the plate's two ends
    are sunk by this too."""
    us = stations(u0, u1, step)
    rings = []
    for k, u in enumerate(us):
        end = ends if k in (0, len(us) - 1) else 0.0
        outer, inner = [], []
        for i in range(n_phi + 1):
            f = i / n_phi
            phi = phi0 + (phi1 - phi0) * f
            edge = sink_edge * (1.0 if i in (0, n_phi) else 0.0)
            outer.append(hull_pt(u, phi, sink=edge + end - proud, up=up))
            inner.append(hull_pt(u, phi, sink=thick, up=up))
        rings.append(outer + inner[::-1])
    bm = bmesh.new()
    rings_solid(bm, rings)
    return new_obj(name, bm, material, 0.12, 2)


def hull_parts(parts):
    u0, u1 = P["hull_u"]
    g, sink = P["gore_gap"], P["gore_sink"]
    # a dark core just inside the shell: what shows in the seams and under the open cover
    us = stations(u0, u1, 1.0)
    rings = [[hull_pt(u, 360.0 * i / 28, sink=0.75) for i in range(28)] for u in us]
    bm = bmesh.new()
    rings_solid(bm, rings)
    parts.append(new_obj("core", bm, "bay"))
    lp_, (l0_, l1_) = P["lid_phi"], P["lid_u"]
    for k in range(7):                       # frames across the hull under the cover: seen when it is open
        fu = l0_ + 1.5 + (l1_ - l0_ - 3.0) * k / 6
        parts.append(shell("bay_frame", fu - 0.3, fu + 0.3, -P["gores"][0][1] + 2.0, P["gores"][0][1] - 2.0, "under", thick=0.5, proud=-0.45, n_phi=6, step=0.3))
    for sgn in (1, -1):
        for k, (a0, a1, material) in enumerate(P["gores"]):
            p0, p1 = (a0 + g, a1 - g) if sgn > 0 else (-a1 + g, -a0 - g)
            if k == 0:
                # the uppermost plates lift with the dorsal cover (group "lid") behind its hinge
                parts.append(group(shell("gore_up", u0 + 0.15, l1_ - 0.1, p0, p1, material, sink_edge=sink, n_phi=6), "lid"))
                parts.append(shell("gore", l1_ + 0.15, u1 - 0.15, p0, p1, material, sink_edge=sink, n_phi=6))
            else:
                parts.append(shell("gore", u0 + 0.15, u1 - 0.15, p0, p1, material, sink_edge=sink, n_phi=6))
    # the fixed top plate ahead of the cover
    lp = P["lid_phi"]
    l0, l1 = P["lid_u"]
    parts.append(shell("top_front", l1 + 0.35, u1 - 0.15, -lp + 0.4, lp - 0.4, "lid", up=0.3, sink_edge=0.2, n_phi=6))
    # hoops round the shell
    for hu in P["hoops_u"]:
        up_ = P["gores"][0][1]
        for sgn in (-1, 1):
            a0, a1, a2 = sgn * (lp + 1.0), sgn * up_, sgn * 122.0
            parts.append(group(shell("hoop_up", hu - 0.32, hu + 0.32, min(a0, a1), max(a0, a1), "rib", proud=0.22, thick=0.6, n_phi=6, step=0.5), "lid"))
            parts.append(shell("hoop", hu - 0.32, hu + 0.32, min(a1, a2), max(a1, a2), "rib", proud=0.22, thick=0.6, n_phi=10, step=0.5))
    # the port the unloading pipe comes out of, in the rear tip
    z = P["pipe"]["z"]
    parts.append(cyl("port", 2.25, u0 - 0.25, u0 + 1.6, 0.0, z - 0.3, "rib", segs=16, bev=0.15))
    parts.append(cyl("port_bore", 1.8, u0 - 0.3, u0 - 0.2, 0.0, z - 0.3, "recess", segs=16))


def lid_parts(parts):
    """The dorsal cover (group "lid": tilts up about P["hinge"] while unloading) with the house band,
    its ribs and the exhaust stack."""
    lp = P["lid_phi"]
    l0, l1 = P["lid_u"]
    up, t = P["lid_up"], P["lid_t"]
    out = []
    out.append(shell("lid", l0, l1, -lp, lp, "lid", up=up, thick=t, sink_edge=0.35, n_phi=8, ends=0.3))
    b0, b1 = P["band_u"]
    out.append(shade(shell("band", b0, b1, -lp + 0.2, lp - 0.2, "HOUSE", up=up + P["band_up"], thick=0.6, sink_edge=0.3, n_phi=8, step=0.8),
                     P["house_band"]))
    for ru in P["ribs_u"]:
        out.append(shell("lid_rib", ru - 0.25, ru + 0.25, -lp + 0.3, lp - 0.3, "rib", up=up + 0.22, thick=0.5, sink_edge=0.25, n_phi=8, step=0.35))
    # two rails along the cover's edges
    for sgn in (-1, 1):
        a = sgn * (lp - 2.6)
        out.append(shell("lid_rail", l0 + 0.8, l1 - 0.6, min(a, a + sgn * 2.0), max(a, a + sgn * 2.0), "hull2", up=up + 0.3, thick=0.6, n_phi=2))
    # the crest: a narrow raised spine along the cover, in segments, the house band carried over it
    cr = P["crest"]
    c0, c1 = cr["u"]
    us = stations(c0, c1, 0.5)
    rings = []
    for u in us:
        k = min(1.0, (u - c0) / 2.2, (c1 - u) / 3.0)          # it rises out of the cover at both ends
        zb = hull_pt(u, 0)[2] + up - 0.15
        zt = zb + 0.15 + cr["h"] * max(0.02, k) ** 0.7
        rings.append([(-cr["w"][0], u, zb), (cr["w"][0], u, zb), (cr["w"][1], u, zt), (-cr["w"][1], u, zt)])
    bm = bmesh.new()
    rings_solid(bm, rings)
    out.append(new_obj("crest", bm, "hull", 0.18, 2))
    nu = c0 + 2.6
    while nu < c1 - 3.0:
        if not (b0 - 0.6 < nu < b1 + 0.6):
            zb = hull_pt(nu, 0)[2] + up
            out.append(box("crest_notch", -cr["w"][0] - 0.08, cr["w"][0] + 0.08, nu - 0.16, nu + 0.16, zb, zb + cr["h"] + 0.2, "rib"))
        nu += cr["notch"]
    zb = hull_pt((b0 + b1) / 2, 0)[2] + up
    out.append(shade(box("band_crest", -cr["w"][0] - 0.1, cr["w"][0] + 0.1, b0, b1, zb, zb + cr["h"] + 0.27, "HOUSE", 0.12), P["house_band"]))
    # the exhaust stack: a tapering box leaning back, a lighter collar, an open top
    st = P["stack"]
    bu, bz = st["base"]
    du, dz = st["dir"]
    nu, nz = -dz, du                        # the box's "thickness" direction in the (u, z) plane
    rings = []
    for f, grow in ((0.0, 1.0), (0.14, 1.0), (0.18, 1.12), (0.34, 1.12), (0.38, 1.0), (1.0, 1.0)):
        hv = (st["hv"][0] + (st["hv"][1] - st["hv"][0]) * f) * grow
        ht = (st["ht"][0] + (st["ht"][1] - st["ht"][0]) * f) * grow
        cu, cz = bu + du * st["len"] * f, bz + dz * st["len"] * f
        rings.append([(sv * hv, cu + su * ht * nu, cz + su * ht * nz) for (sv, su) in ((-1, -1), (1, -1), (1, 1), (-1, 1))])
    bm = bmesh.new()
    rings_solid(bm, rings)
    out.append(new_obj("stack", bm, "maroon", 0.35, 2))
    cu, cz = bu + du * st["len"] * 0.26, bz + dz * st["len"] * 0.26
    hv, ht = st["hv"][0] * 1.16, st["ht"][0] * 1.16
    bm = bmesh.new()
    rings_solid(bm, [[(sv * hv, cu + du * e + su * ht * nu, cz + dz * e + su * ht * nz) for (sv, su) in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
                     for e in (-0.75, 0.75)])
    out.append(new_obj("stack_collar", bm, "maroon_lt", 0.2, 2))
    cu, cz = bu + du * (st["len"] + 0.03), bz + dz * (st["len"] + 0.03)
    hv, ht = st["hv"][1] - 0.55, st["ht"][1] - 0.5
    bm = bmesh.new()
    rings_solid(bm, [[(sv * hv, cu + du * e + su * ht * nu, cz + dz * e + su * ht * nz) for (sv, su) in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
                     for e in (-0.3, 0.0)])
    out.append(new_obj("stack_bore", bm, "recess"))
    # foot of the stack on the cover
    out.append(box("stack_foot", -3.0, 3.0, bu - 2.6, bu + 2.2, hull_pt(bu, 0)[2] - 0.2, hull_pt(bu, 0)[2] + up + 0.75, "rib", 0.3))
    for o in out:
        group(o, "lid")
    parts.extend(out)


def wheel_parts(parts):
    for (u, v, r) in P["wheels"]:
        for sgn in (-1, 1):
            c = (sgn * v, u, r - 0.12)
            parts.append(ellipsoid("wheel", c, (r * P["wheel_squash"], r, r), "tyre", segs=24, rings=14))
            xo = sgn * (v + r * P["wheel_squash"] - 0.25)
            parts.append(cyl("hub", r * 0.3, min(xo, xo + sgn * 0.3), max(xo, xo + sgn * 0.3), u, r - 0.12, "tyre_dk", segs=14, axis="v", bev=0.1))
            parts.append(cyl("axle", 1.25, min(sgn * 5.5, sgn * v), max(sgn * 5.5, sgn * v), u, r * 0.95, "chassis", segs=10, axis="v"))
    sh = P["shoulder"]
    for sgn in (-1, 1):
        parts.append(ellipsoid("shoulder", (sgn * sh["v"], sh["u"], sh["z"]), (sh["r"][1], sh["r"][0], sh["r"][2]), "hull2", segs=18, rings=10))
        parts.append(box("shoulder_strap", sgn * sh["v"] - 2.2, sgn * sh["v"] + 2.2, sh["u"] - 0.45, sh["u"] + 0.45, sh["z"] - 2.0, sh["z"] + sh["r"][2] + 0.12, "rib", 0.15))
    ch = P["chassis"]
    parts.append(box("chassis", -ch["v"], ch["v"], ch["u"][0], ch["u"][1], ch["z"][0], ch["z"][1], "chassis", 0.6))


def front_parts(parts):
    parts.append(prism_uz("neck", P["neck"], -P["neck_v"], P["neck_v"], "hull2", 0.45))
    parts.append(prism_uz("neck_rib", [(11.6, 5.1), (12.6, 5.0), (12.6, 9.5), (11.6, 9.8)], -P["neck_v"] - 0.3, P["neck_v"] + 0.3, "rib", 0.15))
    parts.append(prism_uz("head", P["head"], -P["head_v"], P["head_v"], "maroon", 0.55, 3))
    parts.append(prism_uz("head_plate", [(18.0, 8.45), (21.6, 7.5), (21.7, 7.8), (18.1, 8.8)], -P["head_v"] + 0.7, P["head_v"] - 0.7, "maroon_lt", 0.1))
    parts.append(box("head_slot", -P["head_v"] + 0.5, P["head_v"] - 0.5, 23.35, 23.6, 2.6, 3.8, "recess"))
    lg = P["lug"]
    for sgn in (-1, 1):
        parts.append(box("lug", min(sgn * 2.4, sgn * lg["v"]), max(sgn * 2.4, sgn * lg["v"]), lg["u"][0], lg["u"][1], lg["z"][0], lg["z"][1], "maroon", 0.4))
        (u0, v0, z0), (u1, v1, z1), r = P["hose"]
        mid = ((u0 + u1) / 2 + 0.6, (v0 + v1) / 2 + 0.5, min(z0, z1) - 1.0)      # the hose sags
        pts = [(sgn * v0, u0, z0), (sgn * mid[1], mid[0], mid[2]), (sgn * v1, u1, z1)]
        for a, b in zip(pts, pts[1:]):
            parts.append(tube("hose", a, b, r, "steel", segs=8))
        parts.append(sphere("hose_joint", pts[1], r * 1.05, "steel", 8, 6))
        parts.append(tube("hose_end", (sgn * v1, u1, z1), (sgn * (v1 - 0.3), u1 + 0.9, z1 - 0.2), r * 1.5, "steel_dk", segs=8))
    # the sensor stalk stands on the dorsal cover just behind its hinge and moves with it (group "lid")
    (su0, sz0), (su1, sz1), sr = P["stalk"]
    kn = P["knob"]
    for o in (tube("stalk", (0, su0, sz0), (0, su1, sz1), sr, "hull", segs=10),
              cyl("stalk_foot", 1.5, su0 - 1.2, su0 + 1.0, 0.0, sz0 - 0.4, "rib", segs=12, bev=0.2),
              box("knob", -kn["v"], kn["v"], kn["u"][0], kn["u"][1], kn["z"][0], kn["z"][1], "hull", 0.5, 3),
              box("knob_lens", -kn["v"] + 0.6, kn["v"] - 0.6, kn["u"][1] - 0.05, kn["u"][1] + 0.12, kn["z"][0] + 0.8, kn["z"][1] - 0.8, "recess"),
              box("knob_band", -kn["v"] - 0.1, kn["v"] + 0.1, kn["u"][0] + 1.4, kn["u"][0] + 2.0, kn["z"][0] - 0.1, kn["z"][1] + 0.1, "rib", 0.05)):
        parts.append(group(o, "lid"))


def pipe_parts(parts):
    """The unloading pipe (group "pipe"), retracted: a steel tube along the centre line with a wider nozzle."""
    pp = P["pipe"]
    z, tip = pp["z"], pp["tip"]
    out = [cyl("pipe", pp["r"], tip + pp["head"] - 0.2, tip + pp["len"], 0.0, z, "steel", segs=14),
           cyl("pipe_sleeve", pp["r"] + 0.3, tip + pp["len"] - 4.5, tip + pp["len"] - 0.2, 0.0, z, "steel_dk", segs=14),
           DV.ring_u("nozzle", pp["r"] * 0.9, ((tip, pp["lip_r"]), (tip + 0.55, pp["lip_r"]), (tip + 0.6, pp["head_r"]),
                                                (tip + pp["head"] - 0.5, pp["head_r"]), (tip + pp["head"], pp["r"] + 0.2)), 0.0, z, "steel", segs=16),
           cyl("nozzle_band", pp["head_r"] + 0.12, tip + 1.5, tip + 2.0, 0.0, z, "steel_dk", segs=16),
           cyl("nozzle_bore", pp["r"] * 0.92, tip + 0.5, tip + 0.6, 0.0, z, "recess", segs=12)]
    for o in out:
        group(o, "pipe")
    parts.extend(out)


def build():
    root = bpy.data.objects.new(KEY, None)
    bpy.context.scene.collection.objects.link(root)
    parts = []
    hull_parts(parts)
    lid_parts(parts)
    wheel_parts(parts)
    front_parts(parts)
    pipe_parts(parts)
    for o in parts:
        g = o.get("d2k_group")
        if g:
            o.vertex_groups.new(name=g).add(list(range(len(o.data.vertices))), 1.0, "REPLACE")
    body = V3.finish_node(parts, "body", (0, 0, 0), (0, 0, 0))
    body.data.transform(Matrix.Scale(P["grow"], 4))
    body.parent = root
    bpy.context.view_layer.update()
    return root


# ------------------------------------------------------------------------------------------------
# Poses


def pose_matrices(k):
    """Frame k of the unloading art -> {group: matrix} in the model's units (tiles)."""
    ang, ext = POSES[k]
    s = PX * P["grow"]
    hu, hz = P["hinge"]
    piv = Matrix.Translation(Vector((0, hu * s, hz * s)))
    return dict(lid=piv @ Matrix.Rotation(-math.radians(ang), 4, "X") @ piv.inverted(),
                pipe=Matrix.Translation(Vector((0, -ext * s, 0))))


def apply_pose(body, k, rest=None):
    """Move the vertices of the moving groups to pose k; returns the rest coordinates (pass them back to restore
    or to go to another pose)."""
    me = body.data
    if rest is None:
        rest = [v.co.copy() for v in me.vertices]
    mats = pose_matrices(k)
    gi = {g.index: g.name for g in body.vertex_groups}
    for v, co in zip(me.vertices, rest):
        m = None
        for ge in v.groups:
            if ge.weight > 0.5 and gi.get(ge.group) in mats:
                m = mats[gi[ge.group]]
        v.co = (m @ co) if m is not None else co
    me.update()
    return rest


def nozzle(k):
    """The centre of the nozzle's mouth in pose k: (u, v, z) px."""
    pp = P["pipe"]
    return (pp["tip"] - POSES[k][1], 0.0, pp["z"])


# ------------------------------------------------------------------------------------------------
# Materials


def mat(name):
    m = V3.mat(name)
    c = PREVIEW_HOUSE if name == "HOUSE" else V3.COL[name]
    m.diffuse_color = (*srgb_to_linear(c), 1)
    return m


def finish_materials():
    for name in list(V3._mats):
        mat(name)


def surface(nt, name):
    """V3's weathering tree + what this vehicle adds: the shell plates and tyres darken towards their
    undersides (the original's warm brown shading), dust on the tyres' upper halves."""
    c, r, m = V3.surface_tree(nt, name)
    nodes, links = nt.nodes, nt.links
    if name in ("hull", "hull2", "tyre", "lid"):
        geo = nodes.new("ShaderNodeNewGeometry")
        nrm = nodes.new("ShaderNodeSeparateXYZ")
        links.new(geo.outputs["Normal"], nrm.inputs[0])
        mr = nodes.new("ShaderNodeMapRange")
        mr.inputs["From Min"].default_value = 0.45 if name == "tyre" else 0.25
        mr.inputs["From Max"].default_value = -0.35
        mr.inputs["To Min"].default_value = 0.0
        mr.inputs["To Max"].default_value = 0.85
        links.new(nrm.outputs["Z"], mr.inputs["Value"])
        mx = nodes.new("ShaderNodeMix")
        mx.data_type = "RGBA"
        links.new(mr.outputs[0], mx.inputs["Factor"])
        links.new(c, mx.inputs[6])
        mx.inputs[7].default_value = (*srgb_to_linear((0.33, 0.235, 0.19)), 1)
        c = mx.outputs[2]
    return c, r, m


def procedural_materials():
    out = {}
    for name, m in list(V3._mats.items()):
        if name in KEEP:
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
    V3.uv_unwrap(objs, margin=0.003)
    images = V2.bake_atlas(objs, procs, size, samples)
    mr = V3.pack_mr(images, size)
    body_mat = V3.export_material(images, mr, os.path.join(bpy.app.tempdir, "d2k_harvester_bake"))
    for o in objs:
        me = o.data
        for s in o.material_slots:
            if s.material and not s.material.name.startswith(KEEP):
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


# ------------------------------------------------------------------------------------------------


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opt = dict(out=None, bake=True, bake_size=2048, iou=None, flat=None, posmap=None, turntable=None, samples=64,
               variants=True, pose=0, nozzle=False)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--out":
            opt["out"] = argv[i + 1]; i += 1
        elif a == "--no-bake":
            opt["bake"] = False
        elif a == "--no-variants":
            opt["variants"] = False
        elif a == "--nozzle":
            opt["nozzle"] = True
        elif a == "--pose":
            opt["pose"] = int(argv[i + 1]); i += 1
        elif a == "--bake-size":
            opt["bake_size"] = int(argv[i + 1]); i += 1
        elif a == "--iou":
            opt["iou"] = argv[i + 1]; i += 1
        elif a == "--flat":
            opt["flat"] = argv[i + 1]; i += 1
        elif a == "--posmap":
            opt["posmap"] = argv[i + 1]; i += 1
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
    if opt["nozzle"]:
        for k in range(len(POSES)):
            u, v, z = nozzle(k)
            log(f"pose {k:2d}: cover {POSES[k][0]:4.1f} deg, nozzle mouth u {u:6.2f} v {v:4.1f} z {z:4.1f} px")
        return
    if opt["out"] is None and opt["iou"] is None and opt["turntable"] is None:
        opt["out"] = os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-harvester")

    V2.setup_gpu = V3.setup_device
    bpy.ops.wm.read_factory_settings(use_empty=True)
    t0 = time.time()
    root = build()
    finish_materials()
    body = next(o for o in root.children if o.name == "body")
    log(f"built: {V2.tri_count(V2.meshes_under(root))} triangles in {time.time() - t0:.1f} s")
    if opt["iou"]:
        DV.iou_report(root, opt["iou"], opt["flat"])
        if opt["posmap"]:
            DV.position_maps(root, opt["iou"], opt["posmap"])
    if opt["bake"] and (opt["out"] or opt["turntable"]):
        t1 = time.time()
        bake_and_merge([body], opt["bake_size"])
        log(f"textures baked in {time.time() - t1:.1f} s ({opt['bake_size']} px atlas)")
    if opt["out"]:
        path = os.path.join(opt["out"], "models", "unit", KEY + ".glb")
        V3.export_glb(root, path)
        log(f"exported {path} ({os.path.getsize(path) // 1024} KB)")
        if opt["variants"]:
            rest = None
            for k in range(len(POSES)):
                rest = apply_pose(body, k, rest)
                vp = os.path.join(opt["out"], "variants", f"{KEY}_unload{k:02d}.glb")
                V3.export_glb(root, vp)
            apply_pose(body, 0, rest)
            log(f"exported {len(POSES)} unloading poses -> {os.path.join(opt['out'], 'variants')}")
        log("2D pack: .venv/bin/python -I tools/models/harvester_check/pack.py --models " + opt["out"])
    if opt["turntable"]:
        t2 = time.time()
        if opt["pose"]:
            apply_pose(body, opt["pose"])
        DV.review_renders(root, opt["turntable"], opt["samples"])
        log(f"review renders in {time.time() - t2:.1f} s -> {opt['turntable']}")


if __name__ == "__main__":
    main()

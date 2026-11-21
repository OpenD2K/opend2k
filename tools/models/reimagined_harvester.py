"""Reimagined harvester, v2 - the first "big-shape redesign" of the Reimagined pack (docs/3d-models.md "Reimagined
models": "Big-shape redesigns"). The Dune 2000 harvester's design (a cream lemon hull on six balloon wheels in a
diamond plan, the maroon spice head on a neck at the front, the maroon stack leaning back at the rear, a sensor head
on a stalk, one house band across the top) with its BIG FORMS rebuilt for the game's near top-down 45 degree camera:

  * three masses that differ in tone: a light armour carapace, a dark chassis / belly that the carapace overhangs,
    and mid-tone running gear - instead of one cream blob;
  * the carapace in few large plates standing proud of each other (bright shoulder plates over cream flank plates
    over an inset dark belly), cut by two heavy dark hoops: every joint is a step or a gap that makes a shadow;
  * a raised spice hopper along the top (the part that lifts while unloading) in three ribbed lids with a crest,
    spice showing in the open bay;
  * chunky running gear: bigger balloon tyres with deep cross lugs and dished dark hubs on visible axle arms,
    fender housings over the middle pair - all six wheels and the cutter drum are nodes that turn with Smooth driving;
  * a few strong accents: a wide maroon spice head with a steel cutter drum, rams and ribbed hoses, a thick maroon
    stack, a steel unloading port, and the house colour as a saddle from fender to fender across the hopper.

    blender -b --factory-startup --python tools/models/reimagined_harvester.py -- \
        [--out DIR]            DIR/models/unit/HARVESTER.glb, HARVESTER_lod1.glb, DIR/variants/HARVESTER_unloadNN.glb
                               (full glbs; tools/models/export_poses.py turns them into node poses in a pack),
                               DIR/models.ini.HARVESTER (the anchors for the pack's models.ini), DIR/stats/HARVESTER.json
        [--no-bake]            geometry, nodes and variants only
        [--bake-size 2048] [--lod 6500]
        [--preview DIR]        quick plain-colour views (no bake)
        [--iou ORIG.npz [--flat DIR]]   silhouettes / flat colours with the game camera, 32 facings
                               (harvester_check/ref.py writes the npz; harvester_check/reimagined_metrics.py reads DIR)
        [--lit DIR]            (after the bake) the 32 facings lit, game camera, 4x: tone distribution vs the original
        [--turntable DIR] [--samples 40] [--work DIR] [--pose K]
        [--set key=value ...]  override a proportion in P (px)
        [--tris]               log the triangles per part and per node

Everything is in ORIGINAL SPRITE PIXELS (1 px = 1/32 tile; u forward, v right, z up), on the kit of the Atreides
vehicles (atreides_vehicle_kit.py) and the Reimagined bake (reimagined_kit.py). Nodes: "body", "lid" (the hopper cover
with the stack; turns about its hinge at the front while unloading), "pipe" (the unloading pipe; slides out of the
rear), "wheel_R0..2" / "wheel_L0..2" (0 = front) and "wheel_drum" (the cutter drum), anchors fx_nozzle (the pipe's
mouth fully out: docking) and fx_light_*. Numbers the game relies on are harvester_v1.py's: the pipe's axis
(z 10.6), its mouth (u -15.4, 10.95 px of travel), the hinge (u 7.4, z 13.3), the poses per unloading frame.
Nothing is copied from the original art: it is measured (tools/models/harvester_check) and rebuilt.
"""
import json
import math
import os
import sys
import time

import bmesh
import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import atreides_vehicle_kit as K  # noqa: E402

V2, V3, DV = K.V2, K.V3, K.DV
KEY = "HARVESTER"
PX = K.PX
log = K.log

P = dict(
    # --- hull: a lemon; cross-section a superellipse (exponent n) of half-width w(u), half-height c(u) about zc
    hull_u=(-17.6, 13.0), hull_b=10.3, hull_zc=8.25, hull_c=5.85, hull_c_tip=3.0, hull_n=2.35,
    plan=[(0.0, 1.0), (0.15, 0.975), (0.3, 0.86), (0.5, 0.63), (0.75, 0.40), (0.92, 0.23), (1.0, 0.10)],   # front half: |t| -> w / hull_b
    plan_rear=[(0.0, 1.0), (0.15, 0.975), (0.3, 0.89), (0.5, 0.75), (0.75, 0.58), (0.92, 0.42), (1.0, 0.2)],
    # tiers of the carapace, degrees round from the top: (phi0, phi1, material, proud px)
    tiers=[(25.0, 68.0, "white", 0.75), (68.0, 102.0, "cream", 0.15), (102.0, 180.0, "under", -0.85)],
    tier_gap=2.0,                      # deg drawn in at every plate edge (the dark joint)
    hoops_u=(-9.65, 3.45), hoop_w=1.5, hoop_proud=1.05, plate_gap=0.5,
    # --- the hopper (node "lid"): a raised box along the top
    lid_u=(-15.6, 7.2), lid_hw=4.7, lid_frac=0.62, lid_h=1.75, lid_cham=1.1, lid_gap=0.75,
    lid_cuts=(-8.25, -0.15),           # the gaps between the three lids (u)
    hinge=(7.4, 13.3),
    crest=dict(u=(-12.6, 5.6), w=(1.35, 0.85), h=1.45),
    band_u=(-4.4, -0.8), band_phi=47.0,
    # --- wheels (u, |v| of the centre plane, radius, width)
    wheels=[(5.6, 12.9, 4.25, 7.0), (-2.8, 15.0, 4.65, 7.6), (-11.3, 12.8, 4.25, 7.0)],
    lugs=11, lug_h=0.5, balloon=0.32,
    fender=dict(u=(-6.4, 0.8), v=(8.4, 12.9), z=(7.3, 10.2)),
    chassis=dict(u=(-13.2, 9.2), v=5.9, z=(1.5, 5.4)),
    # --- front: neck, spice head, drum, sensor
    neck=[(9.0, 4.8), (17.2, 4.4), (17.2, 8.3), (9.0, 10.8)], neck_v=2.7,
    head=[(15.0, 2.5), (21.2, 2.5), (21.5, 4.4), (20.6, 7.7), (17.0, 9.1), (15.0, 8.4)], head_v=4.3,
    drum=dict(u=21.85, z=1.65, r=1.5, v=3.7),
    stalk=((8.3, 13.0), (11.9, 20.3), 0.95), knob=dict(u=(10.3, 15.0), v=2.4, z=(19.4, 23.1)),
    # --- rear: the exhaust stack on the hopper, the unloading pipe
    stack=dict(base=(-12.6, 14.6), dir=(-0.6, 0.8), len=8.3, hv=(2.75, 2.25), ht=(1.95, 1.6)),
    pipe=dict(z=10.6, tip=-15.4, len=15.0, r=1.2, head=5.0, head_r=1.8, lip_r=2.1),
    house_band=0.9, house_low=0.72,
    # --- size in the game: the model is authored on the original sprite's envelope (so the silhouette / tone checks
    # compare 1:1) and shown `size` times bigger (models.ini `scale`, written by write_ini; owner 2026-11-17: "should
    # be bigger" next to the tanks). The pipe keeps what the refineries were laid out for at any size: its axis
    # pipe.z px above the ground and its mouth, fully out, tip - 10.95 = 26.35 px behind the centre IN THE GAME, so
    # pipe_z() / pipe_travel() undo the scale on those two numbers (size 1 = exactly harvester_v1's).
    size=1.25,
)
# frame k of the unloading art: (hopper cover angle deg, pipe extension px) - harvester_v1.POSES
POSES = [(0.0, 0.0), (3.0, 0.0), (6.0, 0.0), (9.0, 0.0), (12.0, 0.0), (15.0, 0.0), (18.0, 0.0), (21.0, 0.0),
         (21.0, 4.8), (21.0, 9.3), (21.0, 10.95)]

COL = dict(
    white=(1.000, 0.930, 0.850),     # shoulder plates: the brightest cream
    cream=(0.930, 0.810, 0.700),     # flank plates, sensor head
    lid=(0.740, 0.630, 0.525),       # the hopper lids: tan
    tan=(0.700, 0.590, 0.490),       # neck, fender housings
    rib=(0.230, 0.165, 0.125),       # hoops, ribs, frames: dark brown
    under=(0.160, 0.112, 0.085),     # belly plates
    chassis=(0.120, 0.090, 0.072),   # frame, axle arms
    bay=(0.170, 0.125, 0.100),       # the hull under the plates: joints, the open bay
    spice=(0.800, 0.410, 0.150),     # the load, seen while the hopper is open
    tyre=(0.800, 0.680, 0.570),      # balloon tyres: a mid tone under the carapace
    lug=(0.940, 0.840, 0.740),       # their lugs: dusty light
    hub=(0.110, 0.085, 0.075),       # dished wheel centres
    maroon=(0.340, 0.150, 0.140),    # spice head, stack
    maroon_lt=(0.520, 0.330, 0.270),
    amber=(0.960, 0.640, 0.130),     # lamp housings
)
SPEC = dict(
    white=(0.0, 0.32, "paint"), cream=(0.0, 0.36, "paint"), lid=(0.0, 0.45, "paint"), tan=(0.0, 0.5, "paint"),
    rib=(0.0, 0.55, "paint"), under=(0.0, 0.7, "paint"), chassis=(0.3, 0.65, "dark"), bay=(0.2, 0.75, "dark"),
    spice=(0.0, 0.9, "rubber"), tyre=(0.0, 0.75, "rubber"), lug=(0.0, 0.7, "rubber"), hub=(0.4, 0.5, "dark"),
    maroon=(0.1, 0.5, "paint"), maroon_lt=(0.1, 0.5, "paint"), amber=(0.0, 0.3, "dark"),
)
FILM = dict(
    white=(0.940, 0.850, 0.760), cream=(0.830, 0.720, 0.620), lid=(0.660, 0.565, 0.475), tan=(0.620, 0.525, 0.440),
    rib=(0.260, 0.195, 0.155), under=(0.190, 0.140, 0.108), tyre=(0.720, 0.610, 0.510), lug=(0.850, 0.750, 0.660),
    maroon=(0.340, 0.190, 0.170), maroon_lt=(0.470, 0.320, 0.265), steel=(0.580, 0.555, 0.510), steel_dk=(0.350, 0.335, 0.310),
)
K.P["house_low"] = P["house_low"]
K.palette(COL, SPEC, FILM,
          weather=dict(paint=(0.6, 0.5, 0.95, 0.7, 0.8, 0.6, 0.5, 0.4), steel=(0.7, 0.3, 0.8, 0.4, 0.4, 0.3, 0.0, 0.1),
                       rubber=(0.7, 0.0, 0.7, 0.35, 0.0, 0.0, 0.0, 0.25)),
          dirt=dict(cavity=(0.075, 0.052, 0.040), soft=(0.400, 0.310, 0.255), blotch=(0.580, 0.480, 0.400), grime=(0.330, 0.250, 0.200),
                    streak=(0.360, 0.265, 0.205), chip=(0.250, 0.180, 0.140), sand=(0.640, 0.520, 0.400)),
          uv_scale=dict(chassis=0.45, under=0.5, bay=0.4, hub=0.5, spice=0.5, HOUSE=0.75), grime_scale=dict(steel=0.5, lug=0.4, tyre=0.7))

box, prism_uz, cyl, new_obj, rings_solid = K.box, K.prism_uz, K.cyl, K.new_obj, K.rings_solid


def pipe_z():
    """The pipe's axis in model px: pipe.z px above the ground once the game scales the model by P["size"]."""
    return P["pipe"]["z"] / P["size"]


def pipe_travel(ext):
    """POSES' pipe extension (px of harvester_v1) -> model px: the mouth fully out ends where it did at size 1
    (26.35 px behind the centre in the game), the intermediate frames in proportion."""
    pp = P["pipe"]
    full = POSES[-1][1]
    return ext * (pp["tip"] - (pp["tip"] - full) / P["size"]) / full


# ------------------------------------------------------------------------------------------------
# The hull


def interp(table, x):
    for (x0, y0), (x1, y1) in zip(table, table[1:]):
        if x <= x1:
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0)
    return table[-1][1]


def section(u):
    """(w, c): half-width and half-height of the hull's cross-section at u."""
    u0, u1 = P["hull_u"]
    t = max(-1.0, min(1.0, (u - (u0 + u1) / 2) / ((u1 - u0) / 2)))
    w = P["hull_b"] * interp(P["plan"] if t >= 0 else P["plan_rear"], abs(t))
    c = P["hull_c_tip"] + (P["hull_c"] - P["hull_c_tip"]) * (1 - abs(t) ** 3.0) ** 0.5
    return w, c


def hull_pt(u, phi_deg, sink=0.0, up=0.0):
    """Point (v, u, z) on the hull at u, phi degrees round from the top (positive: right), `sink` px towards the
    axis (negative: standing proud), raised by `up`."""
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


def shell(name, u0, u1, phi0, phi1, material, proud=0.0, thick=1.3, n_phi=5, step=1.6, edge=0.3, bev=0.0):
    """A plate of the hull's shell between two angles and two stations, standing `proud` of the hull surface; its
    rim is drawn in by `edge` px so that it reads as a separate, slightly pillowed plate."""
    us = stations(u0, u1, step)
    rings = []
    for k, u in enumerate(us):
        end = edge if k in (0, len(us) - 1) else 0.0
        outer, inner = [], []
        for i in range(n_phi + 1):
            phi = phi0 + (phi1 - phi0) * i / n_phi
            e2 = edge if i in (0, n_phi) else 0.0
            outer.append(hull_pt(u, phi, sink=-proud + max(end, e2)))
            inner.append(hull_pt(u, phi, sink=-proud + thick))
        rings.append(outer + inner[::-1])
    bm = bmesh.new()
    rings_solid(bm, rings)
    return new_obj(name, bm, material, bev, 2)


def hull_parts(parts):
    u0, u1 = P["hull_u"]
    # the dark core: what shows in every joint and in the open bay
    us = stations(u0, u1, 2.2)
    bm = bmesh.new()
    rings_solid(bm, [[hull_pt(u, 360.0 * i / 16, sink=0.55) for i in range(16)] for u in us])
    parts.append(new_obj("core", bm, "bay"))
    h0, h1 = P["hoops_u"]
    hw, g = P["hoop_w"] / 2, P["plate_gap"]
    spans = [(u0 + 0.15, h0 - hw - g), (h0 + hw + g, h1 - hw - g), (h1 + hw + g, u1 - 0.15)]
    tg = P["tier_gap"]
    for sgn in (1, -1):
        for (a0, a1, material, proud) in P["tiers"]:
            p0, p1 = (a0 + tg, a1 - tg) if sgn > 0 else (-a1 + tg, -a0 - tg)
            if a1 >= 180.0:
                p0, p1 = (a0 + tg, 180.0) if sgn > 0 else (-180.0, -a0 - tg)
            for (s0, s1) in spans:
                thick = 1.2 if proud > 0 else 0.5
                parts.append(shell("plate", s0, s1, p0, p1, material, proud=proud, thick=thick + max(0.0, proud),
                                   n_phi=5 if material == "white" else 4, edge=0.3 if proud > 0 else 0.12))
    # the two hoops: heavy dark straps round the hull, standing proud of the plates
    for hu in (h0, h1):
        for sgn in (1, -1):
            a0, a1 = (P["tiers"][0][0] + 0.5, 126.0) if sgn > 0 else (-126.0, -P["tiers"][0][0] - 0.5)
            parts.append(shell("hoop", hu - hw, hu + hw, a0, a1, "rib", proud=P["hoop_proud"], thick=1.9, n_phi=8, step=1.5, edge=0.0))
    # exhaust grilles: a dark recessed panel with three slats on both rear flank plates
    t1 = P["tiers"][1]
    for sgn in (1, -1):
        g0, g1 = u0 + 2.3, h0 - hw - g - 1.3
        a0, a1 = (t1[0] + 6.5, t1[1] - 6.5) if sgn > 0 else (-t1[1] + 6.5, -t1[0] - 6.5)
        parts.append(shell("grille", g0, g1, a0, a1, "recess", proud=t1[3] + 0.1, thick=0.6, n_phi=3, edge=0.0))
        for k in range(3):
            c = a0 + (a1 - a0) * (k + 0.5) / 3
            parts.append(shell("grille_slat", g0 - 0.25, g1 + 0.25, c - 2.3, c + 2.3, "rib", proud=t1[3] + 0.6, thick=0.5, n_phi=1, step=3.0, edge=0.0))
    # the fixed top plate ahead of the hopper, and the bay's rim under the hopper
    lp = P["tiers"][0][0]
    l0, l1 = P["lid_u"]
    parts.append(shell("top_front", l1 + 0.5, u1 - 0.2, -lp + 0.6, lp - 0.6, "lid", proud=0.55, thick=1.6, n_phi=6, edge=0.3))
    # the load: a low mound of spice in the bay (hidden by the hopper cover until it lifts)
    bm = bmesh.new()
    rings = []
    for u in stations(l0 + 1.2, l1 - 1.6, 1.6):
        w = min(P["lid_hw"] - 0.9, section(u)[0] * P["lid_frac"] - 0.9)
        zt = hull_pt(u, 0)[2]
        k = min(1.0, (u - l0 - 1.2) / 3.0, (l1 - 1.6 - u) / 3.0)
        rings.append([(-w, u, zt - 1.2), (-w * 0.6, u, zt - 0.5 + 0.5 * k), (0.0, u, zt - 0.3 + 0.8 * k), (w * 0.6, u, zt - 0.5 + 0.5 * k), (w, u, zt - 1.2),
                      (0.0, u, zt - 2.0)])
    rings_solid(bm, rings)
    parts.append(new_obj("load", bm, "spice"))
    # the port the unloading pipe comes out of: a steel collar on the rear tip
    z = pipe_z()
    parts.append(DV.ring_u("port", 1.9, ((u0 - 0.55, 2.35), (u0 - 0.35, 2.75), (u0 + 0.5, 2.75), (u0 + 0.7, 2.4), (u0 + 2.0, 2.4)), 0.0, z - 0.2, "steel", segs=16))
    parts.append(cyl("port_bore", 1.95, u0 - 0.2, u0 - 0.1, 0.0, z - 0.2, "recess", segs=14))
    parts.append(box("port_brace", -0.8, 0.8, u0 + 0.2, u0 + 2.8, z + 2.0, z + 3.2, "rib", 0.2))
    # the house saddle on the shoulder plates (the part on the hopper is in lid_parts)
    b0, b1 = P["band_u"]
    for sgn in (1, -1):
        a0, a1 = (lp + 1.0, P["band_phi"]) if sgn > 0 else (-P["band_phi"], -lp - 1.0)
        parts.append(K.house(shell("band_side", b0, b1, a0, a1, "HOUSE", proud=P["tiers"][0][3] + 0.4, thick=0.9, n_phi=3, step=1.9, edge=0.25), P["house_band"]))


def lid_ring(u, grow=0.0, up=0.0):
    """Cross-section of the hopper cover at u: a box with chamfered shoulders -> [(v, u, z)] (6 points)."""
    hw = min(P["lid_hw"], section(u)[0] * P["lid_frac"]) + grow
    zt = hull_pt(u, 0)[2] + P["lid_h"] + up
    zb = hull_pt(u, P["tiers"][0][0])[2] - 0.5
    ch = min(P["lid_cham"], hw * 0.4)
    return [(-hw, u, zb), (-hw, u, zt - ch), (-hw + ch, u, zt), (hw - ch, u, zt), (hw, u, zt - ch), (hw, u, zb)]


def lid_parts():
    """The hopper cover (node "lid"; turns about P["hinge"] while unloading): three ribbed lids on a dark frame,
    the crest, the house saddle's top, the exhaust stack."""
    out = []
    l0, l1 = P["lid_u"]
    c0, c1 = P["lid_cuts"]
    g = P["lid_gap"] / 2
    # the frame: a dark box a little lower than the lids - what shows in the gaps between them
    bm = bmesh.new()
    rings_solid(bm, [lid_ring(u, grow=-0.25, up=-0.8) for u in stations(l0 + 0.2, l1 - 0.2, 1.5)])
    out.append(new_obj("lid_frame", bm, "rib"))
    b0, b1 = P["band_u"]
    for (s0, s1) in ((l0, c0 - g), (c0 + g, c1 - g), (c1 + g, l1)):
        bm = bmesh.new()
        rings_solid(bm, [lid_ring(u) for u in stations(s0, s1, 2.4)])
        out.append(new_obj("lid", bm, "lid", 0.22, 2))
        # two heavy cross ribs per lid
        for f in ():
            ru = s0 + (s1 - s0) * f
            if b0 - 0.9 < ru < b1 + 0.9:
                continue
            bm = bmesh.new()
            rings_solid(bm, [lid_ring(u, grow=0.22, up=0.5) for u in (ru - 0.45, ru + 0.45)])
            out.append(new_obj("lid_rib", bm, "rib"))
    # the house saddle over the hopper
    bm = bmesh.new()
    rings_solid(bm, [lid_ring(u, grow=0.3, up=0.42) for u in stations(b0, b1, 1.9)])
    out.append(K.house(new_obj("band_top", bm, "HOUSE", 0.15, 1), P["house_band"]))
    # the crest: a raised spine along the hopper, in segments
    cr = P["crest"]
    k0, k1 = cr["u"]
    for (s0, s1, material) in ((k0, c0 - g - 0.3, "white"), (c0 + g + 0.3, b0 - 0.25, "white"), (b0 - 0.1, b1 + 0.1, "HOUSE"),
                               (b1 + 0.25, c1 - g - 0.3, "white"), (c1 + g + 0.3, k1, "white")):
        if s1 - s0 < 0.6:
            continue
        rings = []
        for u in stations(s0, s1, 1.2):
            k = min(1.0, (u - k0 + 0.3) / 2.0, (k1 - u + 0.3) / 2.6)
            zb = hull_pt(u, 0)[2] + P["lid_h"] - 0.1
            zt = zb + 0.1 + cr["h"] * max(0.15, k) ** 0.7 + (0.45 if material == "HOUSE" else 0.0)
            w0, w1 = cr["w"][0] + (0.12 if material == "HOUSE" else 0.0), cr["w"][1] + (0.12 if material == "HOUSE" else 0.0)
            rings.append([(-w0, u, zb), (w0, u, zb), (w1, u, zt), (-w1, u, zt)])
        bm = bmesh.new()
        rings_solid(bm, rings)
        o = new_obj("crest", bm, material)
        out.append(K.house(o, P["house_band"]) if material == "HOUSE" else o)
    # the exhaust stack: a thick tapering box leaning back, a lighter collar, an open top, a foot block
    st = P["stack"]
    bu, bz = st["base"]
    du, dz = st["dir"]
    nu, nz = -dz, du

    def stack_ring(f, grow=1.0, shrink=0.0):
        hv = (st["hv"][0] + (st["hv"][1] - st["hv"][0]) * f) * grow - shrink
        ht = (st["ht"][0] + (st["ht"][1] - st["ht"][0]) * f) * grow - shrink
        cu, cz = bu + du * st["len"] * f, bz + dz * st["len"] * f
        return [(sv * hv, cu + su * ht * nu, cz + su * ht * nz) for (sv, su) in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    bm = bmesh.new()
    rings_solid(bm, [stack_ring(f) for f in (-0.2, 0.0, 0.5, 1.0)])
    out.append(new_obj("stack", bm, "maroon", 0.4, 2))
    for (f0, f1, material, grow) in ((0.3, 0.52, "maroon_lt", 1.16), (0.9, 1.02, "maroon_lt", 1.1)):
        bm = bmesh.new()
        rings_solid(bm, [stack_ring(f, grow) for f in (f0, f1)])
        out.append(new_obj("stack_collar", bm, material, 0.18, 2))
    bm = bmesh.new()
    rings_solid(bm, [stack_ring(f, 1.0, 0.55) for f in (0.97, 1.035)])
    out.append(new_obj("stack_bore", bm, "recess"))
    zf = hull_pt(bu, 0)[2] + P["lid_h"]
    out.append(box("stack_foot", -3.6, 3.6, bu - 2.7, bu + 2.5, zf - 0.6, zf + 0.9, "rib", 0.35))
    return out


# ------------------------------------------------------------------------------------------------
# Running gear


def wheel_parts(u, v, r, w):
    """A balloon tyre (centre plane at v, axle along v at height r) with cross lugs and dished hubs."""
    out = []
    sg = 1.0 if v >= 0 else -1.0
    segs = 24
    prof = []
    for i in range(7):
        t = -1.0 + 2.0 * i / 6
        prof.append((v + t * w / 2, r * (1.0 - P["balloon"] * abs(t) ** 2.6) - (0.0 if abs(t) < 0.99 else 0.9)))
    bm = bmesh.new()
    rings_solid(bm, [[(vv, u + rr * math.cos(2 * math.pi * i / segs), r + rr * math.sin(2 * math.pi * i / segs)) for i in range(segs)] for (vv, rr) in prof])
    out.append(new_obj("tyre", bm, "tyre"))
    n = P["lugs"]
    bm = bmesh.new()
    ts = (-0.8, 0.0, 0.8)
    for i in range(n):
        a = 2 * math.pi * (i + 0.5) / n
        rings = []
        for t in ts:
            rr = r * (1.0 - P["balloon"] * abs(t) ** 2.6)
            aa = a + 0.16 * abs(t)           # a shallow chevron
            ring = []
            for (da, dr) in ((-0.215, -0.25), (0.215, -0.25), (0.185, P["lug_h"]), (-0.185, P["lug_h"])):
                ring.append((v + t * w / 2, u + (rr + dr) * math.cos(aa + da), r + (rr + dr) * math.sin(aa + da)))
            rings.append(ring)
        rings_solid(bm, rings)
    out.append(new_obj("lugs", bm, "lug"))
    for side in (1, -1):
        f = v + side * (w / 2 - 0.45)
        o = v + side * (w / 2 + 0.05)
        out.append(cyl("hub", r * 0.52, min(f, o), max(f, o), u, r, "hub", segs=12, axis="v"))
        if side * sg > 0:         # the outer face: a steel cap
            c0, c1 = o, v + side * (w / 2 + 0.45)
            out.append(cyl("hub_cap", r * 0.24, min(c0, c1), max(c0, c1), u, r, "steel", segs=8, axis="v"))
    return out


def gear_parts(parts):
    """Chassis, axle arms, fender housings (right side; mirrored by the caller's mirror_all)."""
    ch = P["chassis"]
    for (u, v, r, w) in P["wheels"]:
        parts.append(box("arm", ch["v"] - 0.6, v - w / 2 + 0.3, u - 1.35, u + 1.35, r - 1.5, r + 1.3, "chassis"))
        parts.append(cyl("axle", 1.0, ch["v"], v, u, r, "steel_dk", segs=10, axis="v"))
    fd = P["fender"]
    (u, v, r, w) = P["wheels"][1]
    c = 1.2
    plan = [(fd["v"][0] + c, fd["u"][0]), (fd["v"][1] - c, fd["u"][0]), (fd["v"][1], fd["u"][0] + c), (fd["v"][1], fd["u"][1] - c),
            (fd["v"][1] - c, fd["u"][1]), (fd["v"][0] + c, fd["u"][1]), (fd["v"][0], fd["u"][1] - c), (fd["v"][0], fd["u"][0] + c)]
    parts.append(DV.pillow("fender", plan, fd["z"][0], fd["z"][1], "tan", edge=0.6, drop=0.6, bev=0.25, segs=1))
    parts.append(box("fender_strut", fd["v"][0] - 1.6, fd["v"][0] + 0.6, u - 1.6, u + 1.6, fd["z"][0] - 2.4, fd["z"][0] + 0.6, "rib"))
    parts.append(box("fender_plate", fd["v"][0] + 1.0, fd["v"][1] - 0.9, fd["u"][0] + 1.3, fd["u"][1] - 1.3, fd["z"][1] - 0.2, fd["z"][1] + 0.35, "white", 0.12, 1))
    # mud scrapers: dark blocks behind the front and rear wheels (they close the gap to the hull with a shadow)
    for (u, v, r, w) in (P["wheels"][0], P["wheels"][2]):
        parts.append(box("guard", ch["v"] + 0.3, v - w / 2 - 0.1, u - 3.4, u + 3.4, r + 1.6, r + 2.6, "rib"))


def front_parts(parts, side):
    """Neck, spice head, rams, hoses (side = the parts to mirror), sensor."""
    nv, hv = P["neck_v"], P["head_v"]
    parts.append(prism_uz("neck", P["neck"], -nv, nv, "tan", 0.5))
    parts.append(prism_uz("neck_rib", [(11.4, 4.4), (12.8, 4.3), (12.8, 9.9), (11.4, 10.3)], -nv - 0.45, nv + 0.45, "rib", 0.2))
    parts.append(prism_uz("head", P["head"], -hv, hv, "maroon", 0.6, 3))
    parts.append(prism_uz("head_plate", [(17.4, 9.0), (20.4, 7.8), (20.6, 8.25), (17.5, 9.5)], -hv + 1.1, hv - 1.1, "maroon_lt", 0.12))
    dr = P["drum"]
    parts.append(box("mouth", -dr["v"] + 0.2, dr["v"] - 0.2, 21.2, 21.6, 0.9, 3.4, "recess"))
    # skids on both cheeks of the head, rams from the hull to the head, ribbed hoses
    side.append(box("skid", hv - 0.2, hv + 0.55, 15.4, 22.6, 0.25, 3.3, "rib", 0.25))
    side.append(K.tube("ram_rod", (4.3, 9.4, 9.4), (4.0, 17.6, 7.2), 0.55, "steel", segs=8))
    side.append(K.tube("ram", (4.3, 9.0, 9.5), (4.15, 13.6, 8.3), 0.95, "steel_dk", segs=10))
    side.append(box("ram_eye", 3.2, 4.8, 17.0, 18.6, 6.2, 8.0, "rib"))
    p0, p1 = Vector((6.4, 8.6, 6.0)), Vector((3.6, 16.6, 5.4))
    mid = (p0 + p1) / 2 + Vector((0.9, 0.3, -1.4))
    pts = [p0, (p0 + mid) / 2 + Vector((0.3, 0, -0.5)), mid, (mid + p1) / 2 + Vector((0.1, 0, -0.4)), p1]
    for a, b in zip(pts, pts[1:]):
        side.append(K.tube("hose", tuple(a), tuple(b), 0.8, "steel", segs=8))
        m = (a + b) / 2
        d = (b - a).normalized()
        side.append(K.tube("hose_ring", tuple(m - d * 0.3), tuple(m + d * 0.3), 1.05, "steel_dk", segs=8))
    # the sensor: a stalk on the fixed top plate with a boxy head, a dark lens and an amber beacon
    (su0, sz0), (su1, sz1), sr = P["stalk"]
    kn = P["knob"]
    parts.append(K.tube("stalk", (0, su0, sz0), (0, su1, sz1), sr, "cream", segs=10))
    parts.append(cyl("stalk_foot", 1.9, su0 - 1.6, su0 + 1.3, 0.0, sz0 - 0.5, "rib", segs=12, bev=0.25))
    parts.append(box("knob", -kn["v"], kn["v"], kn["u"][0], kn["u"][1], kn["z"][0], kn["z"][1], "cream", 0.55, 3))
    parts.append(box("knob_lens", -kn["v"] + 0.6, kn["v"] - 0.6, kn["u"][1] - 0.05, kn["u"][1] + 0.15, kn["z"][0] + 0.8, kn["z"][1] - 0.8, "recess"))
    parts.append(box("knob_band", -kn["v"] - 0.15, kn["v"] + 0.15, kn["u"][0] + 1.3, kn["u"][0] + 2.2, kn["z"][0] - 0.15, kn["z"][1] + 0.15, "rib", 0.08))
    parts.append(box("beacon", -0.7, 0.7, kn["u"][0] + 2.7, kn["u"][0] + 4.0, kn["z"][1] - 0.1, kn["z"][1] + 0.55, "amber", 0.15))
    # work lamps on the hull's front face either side of the neck, tail lamps
    side.append(box("lamp", 4.6, 6.0, 9.6, 10.3, 9.9, 11.0, "amber"))
    side.append(box("lamp_rear", 3.3, 4.4, -16.7, -16.2, 8.6, 9.5, "maroon_lt"))


def drum_parts():
    dr = P["drum"]
    out = [cyl("drum", dr["r"] * 0.78, -dr["v"], dr["v"], dr["u"], dr["z"], "steel_dk", segs=14, axis="v")]
    bm = bmesh.new()
    for i in range(7):
        a = 2 * math.pi * i / 7
        rings = []
        for k in range(5):
            vv = -dr["v"] + 0.15 + (2 * dr["v"] - 0.3) * k / 4
            aa = a + 0.5 * (k / 4 - 0.5)
            ring = []
            for (da, rr) in ((-0.2, dr["r"] * 0.7), (0.2, dr["r"] * 0.7), (0.12, dr["r"]), (-0.12, dr["r"])):
                ring.append((vv, dr["u"] + rr * math.cos(aa + da), dr["z"] + rr * math.sin(aa + da)))
            rings.append(ring)
        rings_solid(bm, rings)
    out.append(new_obj("drum_blades", bm, "steel"))
    return out


def pipe_parts():
    """The unloading pipe (node "pipe"), retracted: a steel tube along the centre line with a wider nozzle."""
    pp = P["pipe"]
    z, tip = pipe_z(), pp["tip"]
    return [cyl("pipe", pp["r"], tip + pp["head"] - 0.2, tip + pp["len"], 0.0, z, "steel", segs=14),
            cyl("pipe_sleeve", pp["r"] + 0.3, tip + pp["len"] - 4.5, tip + pp["len"] - 0.2, 0.0, z, "steel_dk", segs=14),
            DV.ring_u("nozzle", pp["r"] * 0.9, ((tip, pp["lip_r"]), (tip + 0.55, pp["lip_r"]), (tip + 0.6, pp["head_r"]),
                                                 (tip + pp["head"] - 0.5, pp["head_r"]), (tip + pp["head"], pp["r"] + 0.2)), 0.0, z, "steel", segs=16),
            cyl("nozzle_band", pp["head_r"] + 0.14, tip + 1.5, tip + 2.1, 0.0, z, "steel_dk", segs=16),
            cyl("nozzle_bore", pp["r"] * 0.92, tip + 0.5, tip + 0.6, 0.0, z, "recess", segs=12)]


# ------------------------------------------------------------------------------------------------


ANCHORS = {}       # name -> ((v, u, z) px, size): also written for the pack's models.ini


TRIS = {}


def count_parts(objs):
    for ob in objs:
        deps = bpy.context.evaluated_depsgraph_get()
        me = ob.evaluated_get(deps).data
        TRIS[ob.name.split(".")[0]] = TRIS.get(ob.name.split(".")[0], 0) + sum(len(p.vertices) - 2 for p in me.polygons)
    return objs


def build():
    root = K.root(KEY)
    side, parts = [], []
    gear_parts(side)
    front_parts(parts, side)
    K.mirror_all(side)
    ch = P["chassis"]
    parts.append(box("chassis", -ch["v"], ch["v"], ch["u"][0], ch["u"][1], ch["z"][0], ch["z"][1], "chassis", 0.5))
    hull_parts(parts)
    K.node(count_parts(side + parts), "body", (0, 0, 0), root)
    hu, hz = P["hinge"]
    K.node(count_parts(lid_parts()), "lid", (0, hu, hz), root)
    pp = P["pipe"]
    K.node(pipe_parts(), "pipe", (0, pp["tip"], pipe_z()), root)
    for i, (u, v, r, w) in enumerate(P["wheels"]):
        K.node(count_parts(wheel_parts(u, v, r, w)), f"wheel_R{i}", (v, u, r), root)
        K.node(wheel_parts(u, -v, r, w), f"wheel_L{i}", (-v, u, r), root)
    dr = P["drum"]
    K.node(drum_parts(), "wheel_drum", (0, dr["u"], dr["z"]), root)
    kn = P["knob"]
    ANCHORS.clear()
    ANCHORS.update({
        "fx_nozzle": ((0.0, pp["tip"] - pipe_travel(POSES[-1][1]), pipe_z()), 1.0),
        "fx_light_beacon_0": ((0.0, kn["u"][0] + 3.35, kn["z"][1] + 0.6), 0.9),
        "fx_light_work_0": ((5.3, 10.4, 10.45), 1.0), "fx_light_work_1": ((-5.3, 10.4, 10.45), 1.0),
        "fx_light_head_0": ((4.0, 21.3, 6.6), 0.8), "fx_light_head_1": ((-4.0, 21.3, 6.6), 0.8),
        "fx_light_tail_0": ((3.85, -16.8, 9.05), 0.8), "fx_light_tail_1": ((-3.85, -16.8, 9.05), 0.8),
    })
    for name, (p, size) in ANCHORS.items():
        e = K.anchor(name, p, root)
        e.scale = (size, size, size)
    bpy.context.view_layer.update()
    return root


def set_pose(root, k):
    """Unloading frame k: the hopper cover's angle and the pipe's travel as node transforms."""
    ang, ext = POSES[k]
    nodes = {o.name: o for o in root.children}
    nodes["lid"].rotation_euler = (-math.radians(ang), 0.0, 0.0)
    pp = P["pipe"]
    nodes["pipe"].location = Vector((0.0, (pp["tip"] - pipe_travel(ext)) * PX, pipe_z() * PX))
    bpy.context.view_layer.update()


def surface(nt, name):
    """The kit's weathering + the light shells and tyres darkening towards their undersides (the original's warm
    brown shading: light from above, a third of the sprite darker than 60)."""
    c, r, m = V3.surface_tree(nt, name)
    if name in ("white", "cream", "tyre", "lug", "lid", "tan"):
        nodes, links = nt.nodes, nt.links
        geo = nodes.new("ShaderNodeNewGeometry")
        nrm = nodes.new("ShaderNodeSeparateXYZ")
        links.new(geo.outputs["Normal"], nrm.inputs[0])
        mr = nodes.new("ShaderNodeMapRange")
        mr.inputs["From Min"].default_value = 0.05 if name in ("tyre", "lug") else 0.2
        mr.inputs["From Max"].default_value = -0.4
        mr.inputs["To Min"].default_value = 0.0
        mr.inputs["To Max"].default_value = 0.6 if name in ("tyre", "lug") else 0.85
        links.new(nrm.outputs["Z"], mr.inputs["Value"])
        mx = nodes.new("ShaderNodeMix")
        mx.data_type = "RGBA"
        links.new(mr.outputs[0], mx.inputs["Factor"])
        links.new(c, mx.inputs[6])
        mx.inputs[7].default_value = (*V2.srgb_to_linear((0.30, 0.21, 0.165)), 1)
        c = mx.outputs[2]
    return c, r, m


# ------------------------------------------------------------------------------------------------
# Renders


def preview(root, out_dir):
    """Plain material colours, a few quick Cycles views (no bake)."""
    import reimagined_build as RB
    RB.preview(root, out_dir, KEY, 2.5)


def lit_facings(root, png_dir, out_dir, scale=4, n=64, samples=24):
    """The baked model lit, with the game camera (45 degrees, orthographic), 32 facings at `scale` texels per original
    pixel -> out_dir/lit.npz (rgba [32, n*scale, n*scale, 4], house [32, ...]: the house mask): the tone distribution
    check. The light is the original's: from the upper left of the picture."""
    import numpy as np
    import reimagined_kit as RK
    os.makedirs(out_dir, exist_ok=True)
    scene = bpy.context.scene
    pm = RK.preview_material(png_dir)
    saved = {}
    for o in V2.meshes_under(root):
        saved[o.name] = list(o.data.materials)
        o.data.materials.clear()
        o.data.materials.append(pm)
    scene.render.engine = "CYCLES"
    V3.setup_device(scene)
    scene.cycles.samples = samples
    scene.cycles.use_denoising = False
    scene.render.film_transparent = True
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    w = scene.world or bpy.data.worlds.new("w")
    scene.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs["Color"].default_value = (0.75, 0.72, 0.68, 1)
    w.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.35
    sun = bpy.data.lights.new("sun", "SUN")
    sun.energy = 3.4
    sun.angle = math.radians(3)
    so = bpy.data.objects.new("sun", sun)
    scene.collection.objects.link(so)
    sa, se = math.radians(160.0), math.radians(52.0)      # from the north-west, as the original art is lit
    so.rotation_euler = Vector((math.cos(sa) * math.cos(se), math.sin(sa) * math.cos(se), math.sin(se))).to_track_quat("Z", "Y").to_euler()
    size = n * scale
    cam = V2.game_camera(scene, 45.0, 32 * scale, size)
    yaw = bpy.data.objects.new("YAW", None)
    scene.collection.objects.link(yaw)
    root.parent = yaw
    tmp = os.path.join(bpy.app.tempdir, "d2k_harv_lit.png")
    frames = []
    for f in range(32):
        yaw.rotation_euler = (0, 0, -math.radians(f * 11.25))
        scene.render.filepath = tmp
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(tmp)
        frames.append((np.clip(np.array(img.pixels[:], np.float32).reshape(size, size, 4)[::-1], 0, 1) * 255 + 0.5).astype(np.uint8))
        bpy.data.images.remove(img)
    np.savez_compressed(os.path.join(out_dir, "lit.npz"), rgba=np.array(frames), scale=scale)
    root.parent = None
    yaw.rotation_euler = (0, 0, 0)
    for o in (cam, yaw, so):
        bpy.data.objects.remove(o)
    for o in V2.meshes_under(root):
        o.data.materials.clear()
        for m in saved[o.name]:
            o.data.materials.append(m)
    log(f"lit facings -> {os.path.join(out_dir, 'lit.npz')}")


def write_ini(path):
    """The [HARVESTER] section for the pack's models.ini: the size in the game (scale) and the anchors in the glb's
    axes (tiles; x east, y up, z south; the game scales them with the model)."""
    with open(path, "w") as f:
        f.write("[HARVESTER]\n; reimagined_harvester.py: the size in the game (P size), the unloading pipe's mouth fully out "
                "(docking) and the lamps\n")
        if P["size"] != 1.0:
            f.write(f"scale = {P['size']:g}\n")
        for name, ((v, u, z), size) in ANCHORS.items():
            f.write(f"anchor.{name} = {v * PX:.4f} {z * PX:.4f} {-u * PX:.4f} {size:g}\n")


def run(o):
    """Build -> (IoU) -> bake -> glb + simplified version + unloading variants -> review renders."""
    import reimagined_kit as RK
    V2.setup_gpu = V3.setup_device
    bpy.ops.wm.read_factory_settings(use_empty=True)
    V3._mats.clear()
    t0 = time.time()
    root = build()
    K.finish_materials()
    objs = V2.meshes_under(root)
    tris = V2.tri_count(objs)
    log(f"built {KEY}: {len(objs)} nodes, {tris} triangles in {time.time() - t0:.1f} s")
    if o.get("tris"):
        count = {}
        for ob in bpy.data.objects:
            if ob.type == "MESH":
                count[ob.name] = V2.tri_count([ob])
        log("triangles per part: " + ", ".join(f"{k} {v}" for k, v in sorted(TRIS.items(), key=lambda kv: -kv[1])))
        log("triangles per node: " + ", ".join(f"{k} {v}" for k, v in sorted(count.items(), key=lambda kv: -kv[1])))
    if o.get("iou"):
        K.iou_report(root, o["iou"], o.get("flat"))
    if o.get("preview"):
        if o.get("pose"):
            set_pose(root, o["pose"])
        preview(root, o["preview"])
        return
    if not o.get("out"):
        return
    out = o["out"]
    unit = os.path.join(out, "models", "unit")
    path = os.path.join(unit, KEY + ".glb")
    png_dir = o.get("work") or os.path.join(bpy.app.tempdir, "d2k_reimagined_" + KEY)
    if o.get("bake", True):
        det = dict(panel_keep=0.0, rivet_keep=0.0, bevel_r=0.011, house_uv=0.75, ao_distance=0.14, dent=0.22, wear=0.55)
        RK.bake_reimagined(objs, o.get("bake_size", 2048), surface, png_dir, det)
    V3.export_glb(root, path)
    if o.get("bake", True):
        RK.glb_patch(path, *(open(os.path.join(png_dir, f"rm_{k}.png"), "rb").read() for k in ("color", "orm", "normal")))
    log(f"exported {path} ({os.path.getsize(path) // 1024} KB, {tris} triangles)")
    for k in range(1, len(POSES)):
        set_pose(root, k)
        V3.export_glb(root, os.path.join(out, "variants", f"{KEY}_unload{k:02d}.glb"))
    set_pose(root, 0)
    log(f"exported {len(POSES) - 1} unloading poses -> {os.path.join(out, 'variants')}")
    lod = os.path.join(unit, KEY + "_lod1.glb")
    lt = RK.export_lod(root, lod, min(1.0, o.get("lod", 6500) / max(tris, 1)))
    RK.strip_images(lod)
    write_ini(os.path.join(out, "models.ini." + KEY))
    os.makedirs(os.path.join(out, "stats"), exist_ok=True)
    json.dump(dict(key=KEY, triangles=tris, lod1=lt, glb_kb=os.path.getsize(path) // 1024, bake=o.get("bake_size", 2048)),
              open(os.path.join(out, "stats", KEY + ".json"), "w"))
    if o.get("bake", True) and o.get("lit"):
        lit_facings(root, png_dir, o["lit"])
    if o.get("bake", True) and o.get("turntable"):
        if o.get("pose"):
            set_pose(root, o["pose"])
        RK.review(root, o["turntable"], png_dir, KEY + (f"_pose{o['pose']}" if o.get("pose") else ""), samples=o.get("samples", 40), dist=2.5,
                  target=(0, 0, 0.22), closeups=[("closeup_a", 55, 24, 1.7, 70, (0, 0.1, 0.2)), ("closeup_b", -125, 28, 1.7, 70, (0, -0.1, 0.2)),
                                                 ("closeup_top", -90, 62, 2.0, 70, (0, 0, 0.2))])
    log(f"total {KEY} {time.time() - t0:.1f} s")


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    o = dict(bake=True, bake_size=2048, samples=40, lod=6500, pose=0)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--out", "--preview", "--iou", "--flat", "--lit", "--turntable", "--work"):
            o[a[2:]] = argv[i + 1]; i += 1
        elif a in ("--bake-size", "--samples", "--lod", "--pose"):
            o[a[2:].replace("-", "_")] = int(argv[i + 1]); i += 1
        elif a == "--no-bake":
            o["bake"] = False
        elif a == "--tris":
            o["tris"] = True
        elif a == "--set":
            k, v = argv[i + 1].split("=", 1)
            if k not in P:
                raise SystemExit("unknown proportion " + k)
            P[k] = float(v) if isinstance(P[k], float) else eval(v)   # noqa: S307 (own CLI)
            i += 1
        else:
            raise SystemExit("unknown option " + a)
        i += 1
    if not (o.get("out") or o.get("preview") or o.get("iou")):
        o["out"] = os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-reimagined-harvester")
    run(o)


if __name__ == "__main__":
    main()

"""Harkonnen devastator, v1 - the ORIGINAL design rebuilt as a modern hard-surface model
(same method and kit as combat_tank_v3.py, whose helpers, weathering and bake it reuses).

    blender -b --factory-startup --python tools/models/devastator_v1.py -- \
        [--out DIR]            write DIR/models/unit/DEVASTATOR_TANK.glb
                               (default artpacks/models-devastator; --model-pack / render_pack --models take DIR)
        [--no-bake]            plain colours instead of the baked atlas (fast iteration)
        [--bake-size 2048]     atlas size (base colour + metallic/roughness PNG inside the .glb)
        [--set key=value ...]  override a proportion in P (px, see below), e.g. --set pod_out=14.0
        [--iou ORIG.npz]       silhouette IoU against the original frames (32 facings, game camera); the npz holds
                               body_opaque [32, N, N] masks and origin = the hotspot pixel (devastator_check/ref.py)
        [--flat DIR]           with --iou: also write the 32 facings in flat material colours (layout checks)
        [--posmap FILE.npz]    with --iou: the model point seen at every pixel of every facing (error maps)
        [--turntable DIR]      review renders: 8 views around the model + close-ups (house colour = Harkonnen red)
        [--samples 64]
        [--device cpu|gpu]     Cycles device for the bake and the review renders (default cpu)

    then the 2D pack (docs/art-pipeline.md "Rendered from 3D"; RENDER_FLAGS below):
    python tools/render_sprites/render_pack.py --models artpacks/models-devastator --out artpacks/rendered-devastator \
        --types DEVASTATOR_TANK <RENDER_FLAGS>

Conventions (docs/3d-models.md "New models"): Blender units = tiles, +X east, +Y north = the vehicle's
front, +Z up, origin on the ground on the turning axis. ONE node "body": the original has no barrel
art (Templates.bin barrel -1) - the twin cannons are fixed and the vehicle aims by turning. Materials:
everything but the house areas and the visor lens is baked into ONE atlas material "body" (base
colour, metallic / roughness); "HOUSE" is the house-colour mask (COLOR_0 carries the shade of each
house part: light caps, mid bands, dark centre plate); "GLOW" is the emissive cyan visor lens.

Everything is authored in ORIGINAL SPRITE PIXELS (1 px = 1/32 tile; u forward, v right of the body's
centre line, z up from the ground) and scaled at the end (P["grow"]: +1.2 %), so every number below can be checked
against the sprite.

How the original was read (DATA.R16 frames 2318-2349; nothing is copied: the proportions come from
pixel counts, from a visual hull carved out of the 32 silhouettes with the game camera - compared
level by level with the hull of this model's own silhouettes - from a photo-consistent height
field, from least-squares fits of the brass parts over all facings and from maps of the difference
between the pack and the original averaged back onto the model: tools/models/devastator_check/; the
frames turn about the CENTRE of the hotspot pixel, 6 px above the ground; the body's centre line
sits 0.2 px right of the turning axis, its centre 2 px behind it):

  * Two track pods, 28.5 px over all. Each runs on a chunky track that fills the pod: a main belt
    (5.6 px wide, u -19..+15.3) and a shorter outer belt (to +14.1 / -15.8), because the original's
    pod ends are stepped and pointed in plan and its corners cut. Both ends are faceted (octagon-
    like, a few chamfered segments), no wheel shows; in the open front bay sits an angular bolted
    hub plate in rust brown where the sprite has its brown pixels. The tracks are the dark blocks at
    the vehicle's corners and the dark band with link teeth along the bottom of the flanks.
  * Side armour: NOT one skirt but separate bulging blocks with dark gaps where the track shows,
    read off the two side views pixel by pixel - and not the same left and right (the original's
    pods are copies, not mirror images). Right flank, rear to front: narrow block, gap, long tall
    block, gap, short block, gap, strut, gap, house band (3.2 px), block, open bay. Left flank:
    narrow block, gap, tall block, a long open bay under a rail with one strut, house band, short
    block, open bay; a grey strip runs along the left plates' outer edge. Lower edges are stepped.
  * On top of each pod, rear to front: a red mudguard over the track's tail (the original's own
    palette reds, not house-indexed), a light lip, a house-colour cap (light shades), a white rear
    plate, a grey section, an open bay with a grating over the track, a long white front plate
    (cut back at its outer front corner on the right pod). The white plates are crowned (pillow).
  * Between the pods a low rear deck and the superstructure on a near-black foot: two white rear
    blocks (the left one 0.45 px further out and 0.3 px higher), two white front blocks over the
    cannon breeches whose roofs fall outwards from an inner ridge, a house-colour plate (dark
    shades) in the valley between them, a dark-grey trunk between the rear blocks, a trough
    between the cannons, a sloped glacis.
  * Two brass domes on the rear of the superstructure, NOT a mirrored pair: the left one (d 4.3)
    carries the whip antenna, the right one (d 4.9, 1 px further forward, 0.8 px higher) a cyan
    visor slit (emissive).
  * Two cannons 7.7 px apart, axis 7.95 px up: dark breech collar, dark ring, brass barrel 3.1 px
    thick from 9.8 to 17.35 px, dark chamfered muzzle with a bore.

Look: the original's slightly pinkish warm white, dark-grey armour, warm near-black tracks and
recesses, brass that is bright yellow on top, orange on the flanks and brown below (painted into
the albedo by the surface's tilt: the vehicle only yaws). Weathering as on the tank (dust film,
blotches, cavity dirt, grime, streaks, chips) plus what this sprite shows: each pod is clean towards
the vehicle's left and dirtier towards its right, the front plates are dirtiest at their rear ends,
soot behind the exhaust slots. The pack is rendered with Blender's Standard view transform and its
High Contrast look; the sun stands in the north-west as the original's ground shadow says
(--shadow-azimuth -45; with the tank's west-south-west sun the light/dark layout correlates worse
here) and a shadowless fill from the camera's side lights the flanks (render_pack --fill: the
original's flanks are brightest when they face the camera and equally dim to east and west); a dark
contour runs along the bottom and up both sides of every frame.
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

RENDER_FLAGS = ('--subpixel --device cpu --view-transform Standard --look "High Contrast" --exposure 0 --house-grey 0.6 '
                '--sky-strength 0.05 --shadow-azimuth -45 --sun-elevation 62 --sun-strength 3.3 --fill 1.0,90,30 '
                '--contour 0.8 --contour-width 1.0 --contour-azimuth -90 --sharpen 0.9')
KEY = "DEVASTATOR_TANK"
PX = V3.PX
log = V2.log
srgb_to_linear = V2.srgb_to_linear

# ------------------------------------------------------------------------------------------------
# Proportions, in original sprite pixels (u forward from the turning axis, v right of the body's
# centre line, z above the ground).

P = dict(
    v0=0.2,                          # the body's centre line sits 0.2 px right of the turning axis
    grow=1.012,                       # the finished model is scaled by this about the ground point on the turning axis
    # --- track pods (the right one; the left is its mirror image but for the side armour and the plates' outer edge)
    pod_out=14.1, pod_in=6.5,        # outer face of the side armour / inner edge of the top plates
    # the track: two belts of chunky links side by side, each round a faceted loop (side profile u, z; counter-
    # clockwise from the rear bottom) - a wide main belt and a shorter, lower outer one: the original's pod ends
    # are stepped and pointed in plan (visual hull of its 32 silhouettes) and no wheel shows. The main belt also
    # narrows towards its tips (u where that starts, outer / inner slope). Above them the dark pod body.
    track_profile=[(-16.2, 0.28), (13.1, 0.28), (14.5, 1.4), (15.25, 2.5), (15.25, 3.9), (14.7, 4.7), (13.7, 5.25),
                   (-16.6, 5.25), (-17.8, 5.0), (-18.6, 4.3), (-18.95, 3.5), (-18.9, 2.9), (-18.4, 2.2), (-17.3, 1.1)],
    outer_profile=[(-14.9, 0.28), (12.6, 0.28), (13.55, 1.3), (14.1, 2.5), (14.1, 3.7), (13.5, 4.4), (12.7, 4.8),
                   (-14.8, 4.8), (-15.45, 4.4), (-15.8, 3.6), (-15.8, 2.3), (-15.4, 1.2)],
    track_round=0.18,
    belt_v=(6.3, 11.9), outer_v=(12.05, 13.4), belt_t=0.55, link_pitch=1.7, link_len=1.18, link_h=0.3,   # the links stand 0.3 px proud: they touch the ground
    nose_taper=(13.6, 1.0, 0.8), tail_taper=(-16.2, 0.7, 0.4),
    side_plate=[(-16.1, 0.5), (12.7, 0.5), (13.6, 1.4), (14.1, 2.5), (14.1, 3.9), (13.4, 5.0), (12.6, 5.5),
                (-16.0, 5.5), (-16.9, 5.0), (-17.2, 4.2), (-17.2, 2.4), (-16.8, 1.3)],          # the plate on the main belt's inner side
    side_plate_v=5.5,
    pod_body=dict(u=(-16.6, 12.7), z=(5.62, 6.3), z_out=5.15, v_out=13.3),   # the slab over the belts: its outer edge falls to z_out at |v| v_out (under the armour's shoulders)
    # side armour: separate chunky modules with gaps between them - the track shows through. Bottom edge 2.2 px up,
    # 0.95 thick; std = face to 5.0 px + 45 deg shoulder; tall = a block to 5.9 px with a chamfered top edge
    side_z=2.3, module_t=1.15, shoulder_z=4.4, shoulder=(11.7, 6.8), tall_z=5.2, tall=(12.25, 7.05),
    # modules, rear to front (kind, u0, u1, material[, lower edge higher by, proud by]; band: house shade of its face, of its
    # shoulder): read off the side views - the flanks differ, the lower edge is stepped and the long block bulges
    modules_r=[("std", -15.5, -14.5, "skirt", 1.3, -0.4), ("tall", -13.5, -5.55, "panel", 0.0, 0.3), ("beam", -5.5, 1.35, "hull"),
               ("face", -4.5, -1.5, "panel", 0.9, -0.25), ("face", -0.4, 0.4, "skirt", 1.4, -0.25), ("band", 1.4, 4.6, 0.2, 0.3),
               ("std", 4.72, 7.6, "hull", 0.9, -0.3)],
    modules_l=[("std", -15.5, -14.5, "panel", 1.3, -0.4), ("tall", -13.5, -7.55, "light", 0.0, 0.3), ("rail", -7.45, 1.28, "hull"),
               ("face", -4.4, -3.6, "hull", 0.9, -0.25), ("band", 1.4, 4.6, 0.2, 0.4), ("std", 4.72, 6.6, "panel", 0.5, -0.2)],
    plate_z=(6.6, 7.35),              # underside / top of the white plates and the cap
    plate_v=(12.15, 11.7),            # outer edge of the front plate: right pod / left pod (a grey strip runs outside it there)
    plate_front=(2.0, 12.9), plate_cut=(7.6, 11.5), inner_strip=(5.55, 1.6, 13.1, 7.15),   # ...; the grey strip inside the front plate: |v|, u0, u1, top   # front plate; on the right pod narrower ahead of plate_cut[0]: the track shows beside it
    plate_rear=(-13.5, -8.5), plate_rear_l=-10.4, plate_mid=-4.3, grating=(-4.15, 1.85, 5),   # white rear plate; a grey section runs on to plate_mid, then the open bay with a grating (u0, u1, bars)
    strip_l=dict(v=(11.7, 12.75), spans=((-13.4, -7.6, 7.1, "white"), (-7.5, -3.6, 7.1, "hull"), (-2.5, 0.5, 7.0, "hull"), (4.7, 13.2, 7.3, "hull"))),   # u0, u1, top, material:   # the left pod's grey edge strip
    cap=(-16.85, -13.7), cap_v=(11.5, 12.2), cap_lip=-17.5, guard=dict(v=(7.3, 11.2), t=0.5, pts=((-17.3, 6.38), (-18.5, 5.3), (-18.62, 5.0))),   # house cap, the lip behind it, the dull red mudguard on the track's rear facets
    # --- hull and superstructure
    hull_w=6.5, belly_w=5.45, hull_belly=0.9, hull_u=(-16.5, 13.3), glacis=(3.9, 1.6),   # glacis: its foot's height, its depth
    deck_z=6.8, deck_u=(-16.5, -11.5),
    super_w=7.5, super_u=(-11.5, 6.4), super_z=10.4,
    rear_block=dict(u=(-6.3, -0.15), v=(2.3, 7.3), v_core=5.6, v_right=7.2, z=12.4, z_left=12.7, z_out=12.25),
    front_block=dict(u=(0.6, 5.0), v=(2.5, 5.5), z=11.9, eave=10.9, ridge=0.9, skirt_v=7.4),   # a roof sloping outwards from an inner ridge
    plate_red=dict(u=(1.9, 5.4), v=2.45, z=(10.85, 10.55), nose=(6.5, 9.45)),   # the valley floor between the front blocks
    channel_z=9.7, spine_z=10.8,
    trough_z=5.45, nose_plate=dict(u=(9.6, 11.7), v=1.75, z=5.95),
    # --- domes (u, v relative to the turning axis, z of the centre; radius)
    dome_l=(-8.95, -5.6, 13.65, 2.15), dome_r=(-7.85, 4.7, 14.45, 2.45),
    antenna=(-10.05, -6.95, 18.7, 0.32),            # u, v, tip z, radius
    visor=dict(dz=-0.15, h=1.0, half_angle=50.0),   # a slit round the front of the right dome
    # --- cannons
    cannon_v=3.85, cannon_dv=-0.1, cannon_z=7.95,   # half spacing, offset of the pair from the body's centre line, axis height
    collar=(7.3, 9.3, 2.25), cover_arc=48.0, ring=(9.3, 9.85, 1.76), barrel=(9.8, 17.35, 1.55), muzzle=(17.3, 17.75, 18.4, 1.63, 1.15),   # muzzle: u0, end of its full radius, tip; radius, tip radius
    house_cap=0.88, house_plate=0.07, house_low=0.55,
)

# sRGB base colours: with the Standard view transform at exposure 0 these are very nearly the
# colours on screen (a sunlit top face a few % brighter, a wall in shade at a third to a half).
COL = dict(
    white=(0.845, 0.815, 0.798),     # top plates and blocks: the original's slightly pinkish warm white
    light=(0.690, 0.645, 0.600),     # ledges, nose plate
    mid=(0.565, 0.520, 0.476),       # collar covers, rear deck centre, block flanks
    skirt=(0.445, 0.412, 0.384),     # armour skirts and shoulders
    panel=(0.520, 0.476, 0.432),     # the grey panel ahead of the band
    hull=(0.335, 0.302, 0.270),      # hull and superstructure walls
    recess=(0.095, 0.088, 0.078),    # slots, seams, bores, the channel: near-black
    dullred=(0.600, 0.255, 0.245),   # the mudguards on the tracks' rear facets: red in the original's own palette (not house-indexed)
    rim=(0.430, 0.245, 0.095),       # wheel rims: rust brown (the sprite's brownish ring)
    brass=(0.950, 0.860, 0.500),     # barrels and domes: the lit top (surface() shades it to orange and brown below)
    brass_dark=(0.450, 0.290, 0.140),  # muzzles, dome collars, breech rings
    antenna=(0.070, 0.066, 0.060),
    teal=(0.050, 0.200, 0.205),      # visor frame
    GLOW=(0.165, 0.930, 0.915),      # visor lens (emissive)
)
# (metallic, roughness, surface kind)
SPEC = dict(
    white=(0.0, 0.36, "paint"), light=(0.0, 0.42, "paint"), mid=(0.0, 0.5, "paint"), skirt=(0.0, 0.55, "paint"),
    panel=(0.0, 0.55, "paint"), hull=(0.0, 0.62, "paint"), recess=(0.0, 0.9, "dark"),
    dullred=(0.0, 0.5, "paint"), rim=(0.5, 0.55, "steel"), brass=(0.55, 0.32, "steel"), brass_dark=(0.4, 0.5, "steel"),
    antenna=(0.3, 0.5, "dark"), teal=(0.2, 0.3, "dark"), GLOW=(0.0, 0.2, "glow"),
)
# the dust film that dulls most of a surface: its colour per material
FILM = dict(
    white=(0.600, 0.565, 0.540), light=(0.590, 0.550, 0.512), mid=(0.520, 0.474, 0.424), skirt=(0.462, 0.428, 0.392),
    panel=(0.510, 0.462, 0.410), hull=(0.385, 0.347, 0.303), rim=(0.400, 0.290, 0.180),
    dullred=(0.500, 0.290, 0.270), brass=(0.700, 0.490, 0.220), brass_dark=(0.330, 0.220, 0.120),
)
SOOT_W = dict(white=0.0, light=0.1, mid=0.3, skirt=0.35, hull=0.4)     # how much soot a material takes at the rear
POD_DIRT = dict(white=(0.22, (0.42, 0.39, 0.36)), light=(0.3, (0.38, 0.35, 0.32)))   # weight, colour (surface())
RIM_W = dict(white=0.22, light=0.35, mid=0.35)        # how dark the rim of a plate gets (surface())
TOP_DUST = dict(tread=0.15, link=0.22)               # a little dust on the upward faces of the belt (surface())
PREVIEW_HOUSE = (0.84, 0.29, 0.26)   # Harkonnen red (COLOURS.BIN house 1, shade 8), previews only
KEEP = ("HOUSE", "GLOW")             # materials that stay their own (not baked into the atlas)

V3.COL.update(COL)
V3.SPEC.update(SPEC)
V3.FILM.update(FILM)
V3.COL.update(tread=(0.085, 0.076, 0.064), link=(0.105, 0.094, 0.080), core=(0.120, 0.103, 0.084))   # the original's track blacks: warm, near (33, 24, 16)
V3.FILM.update(tread=(0.100, 0.092, 0.080), link=(0.118, 0.108, 0.092), wheel=(0.190, 0.172, 0.148))
V3.WEATHER["paint"] = (0.85, 0.9, 0.95, 0.7, 0.85, 0.85, 0.85, 0.5)      # film, blotch, cavity, soft, grime, streak, chip, low dust
V3.WEATHER["rubber"] = (0.6, 0.0, 0.55, 0.25, 0.0, 0.0, 0.0, 0.07)
V3.WEATHER["steel"] = (0.75, 0.25, 0.80, 0.40, 0.35, 0.25, 0.0, 0.08)
V3.DIRT.update(blotch=(0.47, 0.425, 0.375), soft=(0.40, 0.36, 0.31))     # greyer, darker dirt than the tank's sand dust
V3.SOOT.clear()                      # soot is laid on by this script (the superstructure's rear), see surface()
V3.GRIME_SCALE.update(brass=0.4, light=0.7)
V3.UV_SCALE.update(recess=0.4, rim=0.5, antenna=0.2, teal=0.3, brass_dark=0.6, hull=0.7, GLOW=0.05)

box, prism_uz, hull_pts, loft, cyl = V3.box, V3.prism_uz, V3.hull_pts, V3.loft, V3.cyl
rings_solid, new_obj, round_poly, inset, along = V3.rings_solid, V3.new_obj, V3.round_poly, V3.inset, V3.along


def shade(o, k, low=None):
    """COLOR_0 of a house part: its shade k and, optionally, how dark its lower edge gets (default P["house_low"])."""
    o["d2k_shade"] = k
    if low is not None:
        o["d2k_low"] = low
    return o


def colour_attribute(o):
    """V3.colour_attribute with a per-part lower-edge factor ("d2k_low")."""
    V3.P["house_low"] = o.get("d2k_low", P["house_low"])
    _V3_COLOUR_ATTRIBUTE(o)


_V3_COLOUR_ATTRIBUTE = V3.colour_attribute
V3.colour_attribute = colour_attribute


def mirror(o):
    return V3.mirror_copy(o)


def plan_prism(name, pts_vu, z0, z1, material, bev=0.0, segs=2, top=None):
    """Plan polygon (v, u) extruded from z0 to z1 (`top`: the polygon at z1 when it differs)."""
    bm = bmesh.new()
    rings_solid(bm, [[(v, u, z0) for (v, u) in pts_vu], [(v, u, z1) for (v, u) in (top or pts_vu)]])
    return new_obj(name, bm, material, bev, segs)


def pillow(name, pts_vu, z0, z1, material, edge=0.7, drop=0.34, bev=0.22, segs=3):
    """A bulging armour plate: plan polygon (v, u) from z0, its top crowned - the outline is drawn in
    by `edge` over the last `drop` of its height (the original's plates are pillow-shaded)."""
    area = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(pts_vu, pts_vu[1:] + pts_vu[:1]))
    pts = list(pts_vu) if area > 0 else list(pts_vu)[::-1]
    top = inset(pts, edge)
    bm = bmesh.new()
    rings_solid(bm, [[(v, u, z0) for (v, u) in pts], [(v, u, z1 - drop) for (v, u) in pts], [(v, u, z1) for (v, u) in top]])
    return new_obj(name, bm, material, bev, segs)


def prism_vz(name, pts_vz, u0, u1, material, bev=0.0, segs=2):
    """Cross-section (v, z) extruded along the vehicle from u0 to u1."""
    bm = bmesh.new()
    rings_solid(bm, [[(v, u, z) for (v, z) in pts_vz] for u in (u0, u1)])
    return new_obj(name, bm, material, bev, segs)


def sphere(name, c, r, material, segs=20, rings=12):
    """UV sphere centred on c = (v, u, z)."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segs, v_segments=rings, radius=r)
    bmesh.ops.translate(bm, verts=bm.verts, vec=Vector(c))
    return new_obj(name, bm, material)


def tube_v(name, r, v0, v1, u, z, material, segs=16, bev=0.0):
    """Cylinder across the vehicle (axis along v)."""
    return cyl(name, r, v0, v1, u, z, material, segs=segs, bev=bev, axis="v")


def arc_shell(name, r0, r1, u0, u1, v, z, half_deg, material, n=8):
    """A shell round the top of a cylinder along u: radii r0..r1, +-half_deg about the vertical."""
    bm = bmesh.new()
    rings = []
    for u in (u0, u1):
        ring = []
        for rad in (r1, r0):
            pts = []
            for i in range(n + 1):
                a = math.radians(-half_deg + 2 * half_deg * i / n)
                pts.append((v + rad * math.sin(a), u, z + rad * math.cos(a)))
            ring.append(pts if rad == r1 else pts[::-1])
        rings.append(ring[0] + ring[1])
    rings_solid(bm, rings)
    return new_obj(name, bm, material)


def ring_u(name, r_in, stations, v, z, material, segs=24):
    """A tube along u with a bore: stations = (u, outer radius), ...; inner wall and both end faces."""
    bm = bmesh.new()
    rings = []
    for (u, rad) in [(stations[0][0], r_in)] + list(stations) + [(stations[-1][0], r_in)]:
        rings.append([bm.verts.new((v + rad * math.cos(2 * math.pi * i / segs), u, z + rad * math.sin(2 * math.pi * i / segs))) for i in range(segs)])
    for a, b in zip(rings, rings[1:] + rings[:1]):
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new([a[i], a[j], b[j], b[i]])
    return new_obj(name, bm, material)


def tube_z(name, r, v, u, z0, z1, material, segs=12, r1=None):
    """Upright cylinder / cone."""
    bm = bmesh.new()
    ra, rb = r, (r if r1 is None else r1)
    ring0 = [(v + ra * math.cos(2 * math.pi * i / segs), u + ra * math.sin(2 * math.pi * i / segs), z0) for i in range(segs)]
    ring1 = [(v + rb * math.cos(2 * math.pi * i / segs), u + rb * math.sin(2 * math.pi * i / segs), z1) for i in range(segs)]
    rings_solid(bm, [ring0, ring1])
    return new_obj(name, bm, material)


# ------------------------------------------------------------------------------------------------
# The track pods (built on the right, v > 0, and mirrored; the side armour and the plates' outer
# edge differ left and right)


def taper(u, outer):
    """|v| of a belt edge at u: the belt narrows towards both tips (the original's pod ends are
    pointed in plan - the visual hull of its 32 silhouettes - and its corners cut)."""
    tv0, tv1 = P["belt_v"]
    un, ko, ki = P["nose_taper"]
    ur, lo, li = P["tail_taper"]
    f, r = max(0.0, u - un), max(0.0, ur - u)
    return tv1 - ko * f - lo * r if outer else tv0 + ki * f + li * r


def belt_parts(parts, name, profile, edge):
    """One belt: the band, its chunky links, its dark core. edge(u, outer) -> |v| of the belt's edge at u."""
    prof = round_poly(profile, P["track_round"], 2)
    t = P["belt_t"]
    inner = inset(prof, t)
    n = len(prof)
    bm = bmesh.new()
    ro = [[bm.verts.new((edge(u, o), u, z)) for (u, z) in prof] for o in (False, True)]
    ri = [[bm.verts.new((edge(u, o), u, z)) for (u, z) in inner] for o in (False, True)]
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new([ro[0][i], ro[0][j], ro[1][j], ro[1][i]])
        bm.faces.new([ri[0][j], ri[0][i], ri[1][i], ri[1][j]])
        bm.faces.new([ro[0][j], ro[0][i], ri[0][i], ri[0][j]])
        bm.faces.new([ro[1][i], ro[1][j], ri[1][j], ri[1][i]])
    parts.append(new_obj(name, bm, "tread"))
    bm = bmesh.new()
    L, h = P["link_len"], P["link_h"]
    c = L * 0.16
    sect = [(-L / 2, -0.03), (-L / 2 + c, h), (L / 2 - c, h), (L / 2, -0.03)]
    for (p, tg) in along(prof, P["link_pitch"]):
        nrm = Vector((tg.y, -tg.x))
        ring = [p + tg * s_ + nrm * hh for (s_, hh) in sect]
        rings_solid(bm, [[(v, q.x, q.y) for q in ring] for v in (edge(p.x, False) + 0.06, edge(p.x, True) - 0.06)])
    parts.append(new_obj(name + "_links", bm, "link"))
    core = inset(profile, t + 0.1)
    bm = bmesh.new()
    rings_solid(bm, [[(edge(u, o) + (-0.28 if o else 0.28), u, z) for (u, z) in core] for o in (False, True)])
    parts.append(new_obj(name + "_core", bm, "core"))


def track_parts(parts):
    """The track of one pod: the main belt, the shorter outer belt, the plate on the main belt's inner
    side, the dark pod body over them, and a bolted hub cover on the outer belt's side (seen in the
    open bays) - an angular plate, not a wheel: the original's track ends are dark faceted blocks."""
    belt_parts(parts, "belt", P["track_profile"], taper)
    o0, o1 = P["outer_v"]
    belt_parts(parts, "belt_out", P["outer_profile"], lambda u, outer: o1 if outer else o0)
    parts.append(prism_uz("side_plate", P["side_plate"], P["side_plate_v"], P["belt_v"][0] + 0.05, "core", bev=0.1))
    pb = P["pod_body"]
    (b0, b1), (z0, z1) = pb["u"], pb["z"]
    tv0, tv1 = P["belt_v"]
    sec = [(tv0 + 0.05, z0), (tv1 + 0.1, z0), (tv1 + 0.1, pb["z_out"]), (pb["v_out"], pb["z_out"]), (pb["v_out"], pb["z_out"] + 0.1), (tv1 + 0.2, z1), (tv0 + 0.05, z1)]
    parts.append(prism_vz("pod_body", sec, b0, b1, "core", bev=0.08, segs=1))
    vf = o1 - 0.28                                   # the outer belt's side face
    cover = [(9.0, 1.25), (12.3, 1.25), (13.3, 2.3), (13.3, 3.2), (12.5, 3.95), (9.0, 3.95)]
    parts.append(prism_uz("track_cover", cover, vf - 0.05, vf + 0.16, "rim", bev=0.06, segs=1))
    for (u, z) in ((9.6, 1.8), (12.1, 1.8), (12.75, 2.6), (12.1, 3.4), (9.6, 3.4)):
        parts.append(tube_v("track_bolt", 0.2, vf + 0.1, vf + 0.26, u, z, "antenna", segs=6))


def side_modules(parts, modules):
    """The armour on a pod's outer side: separate chunky modules with gaps between them (the track
    frame shows through). kinds: std = face + 45 deg shoulder, tall = a block with a chamfered top
    edge, face, rail = shoulder only, band = the house band (std, slightly proud)."""
    vo = P["pod_out"]
    z_lo, th = P["side_z"], P["module_t"]
    zs, (sv, sz) = P["shoulder_z"], P["shoulder"]
    zt, (tv, tz) = P["tall_z"], P["tall"]

    def section(kind, proud=0.0, z_from=None, lift=0.0):
        d = 0.7071 * proud
        o = vo + proud
        zl = z_lo + lift
        if kind == "face":                           # set back: the original's flank is narrower there
            return [(o - 0.42, zl), (o - 0.12, zl + 0.5), (o - 0.12, zs - 0.1), (o - th, zs - 0.1), (o - th, zl)]
        if kind == "rail":
            za = zs - 0.6 if z_from is None else z_from
            return [(o, za), (o, zs), (sv + d, sz + d), (sv - 0.6, sz - 0.02), (o - th, za)]
        if kind == "tall":                           # a bulging block: chamfered below, a rounded shoulder above
            return [(o - 0.45, zl), (o, zl + 0.8), (o, zt - 0.45), (o - 0.25, zt + 0.15), (tv + d, tz + d), (tv - 0.7, tz - 0.02),
                    (o - th, zt - 0.5), (o - th, zl)]
        if kind == "beam":                           # the dark bar along the top of the pod body, seen in the gaps
            return [(o - 1.2, 5.6), (sv + 0.35, sz - 0.3), (sv - 0.2, sz - 0.3), (sv - 0.2, sz - 0.9), (o - 1.65, 5.45)]
        return [(o - 0.3, zl), (o, zl + 0.55), (o, zs), (sv + d, sz + d), (sv - 0.6, sz - 0.02), (o - th, zs - 0.5), (o - th, zl)]

    for m in modules:
        kind, ua, ub = m[0], m[1], m[2]
        if kind == "band":
            parts.append(shade(prism_vz("band", section("face", 0.24), ua, ub, "HOUSE", bev=0.16, segs=3), m[3], 0.1))
            parts.append(shade(prism_vz("band_sh", section("rail", 0.12, zs - 0.1), ua, ub, "HOUSE", bev=0.16, segs=3), m[4]))
            continue
        lift, proud = (m[4], m[5]) if len(m) > 5 else (0.0, 0.0)
        parts.append(prism_vz("module", section(kind, proud, None, lift), ua, ub, m[3], bev=0.42 if kind in ("std", "tall") else 0.25, segs=4))
        if kind in ("std", "tall") and ub - ua > 2.5:          # bolt heads (read only close up)
            for u in (ua + 0.85, ub - 0.85):
                for z in (z_lo + lift + 1.0, zs - 0.75):
                    parts.append(tube_v("bolt", 0.16, vo + proud - 0.02, vo + proud + 0.08, u, z, m[3], segs=6))


def top_parts(parts, side):
    """A pod's top armour, rear to front: the dull red mudguard on the track's rear facets, the lip,
    the house cap, the white rear plate, a light-grey section, the open bay (the track shows), the
    white front plate. side: +1 = the right pod, -1 = the left one (built on the right, mirrored by
    the caller): its plates end 0.45 px further in and a grey strip runs along their outer edge."""
    vi = P["pod_in"]
    vs = P["plate_v"][0 if side > 0 else 1]
    z0, z1 = P["plate_z"]
    f0, f1 = P["plate_front"]
    if side > 0:
        cu, cv = P["plate_cut"]
        plan = [(vi, f0), (vs, f0), (vs, cu), (cv, cu + (vs - cv)), (cv, f1), (vi, f1)]
    else:
        plan = [(vi, f0), (vs, f0), (vs, f1), (vi, f1)]
    parts.append(pillow("plate_front", round_poly(plan, 1.0, 3), z0 - 0.15, z1 + 0.05, "white"))
    sv_, su0, su1, sz_ = P["inner_strip"]
    parts.append(box("inner_strip", sv_, vi + 0.1, su0, su1, z0 - 0.4, sz_, "mid", bev=0.14, segs=2))
    a0, a1 = P["plate_rear"]
    if side < 0:
        a1 = P["plate_rear_l"]                       # the left pod's white rear plate is shorter
    m1 = P["plate_mid"]
    vr = P["tall"][0] if side > 0 else vs
    parts.append(pillow("plate_rear", round_poly([(vi, a0), (vr, a0), (vr, a1), (vi, a1)], 0.9, 3), z0 - 0.15, z1 + 0.05, "white"))
    if side > 0:
        # the white runs on along the outer edge: the top of the tall side module
        e = P["modules_r"][1][2]
        parts.append(plan_prism("plate_edge", [(vr - 1.25, a1 - 0.3), (vr, a1 - 0.3), (vr, e), (vr - 1.25, e)], z0, z1 - 0.06, "light", bev=0.3, segs=3))
        parts.append(plan_prism("plate_mid", [(vi, a1 + 0.14), (vr - 1.35, a1 + 0.14), (vr - 1.35, -6.3), (vi, -6.3)], z0, z1 - 0.2, "light", bev=0.2, segs=2))
        parts.append(plan_prism("plate_mid", [(vi, -6.2), (vr - 1.35, -6.2), (vr - 1.35, m1), (vi, m1)], z0, z1 - 0.3, "skirt", bev=0.2, segs=2))
    else:
        parts.append(plan_prism("plate_mid", [(vi, a1 + 0.14), (vs, a1 + 0.14), (vs, m1), (vi, m1)], z0, z1 - 0.2, "light", bev=0.2, segs=2))
        st = P["strip_l"]
        for (ua, ub, zt_, material) in st["spans"]:
            parts.append(box("strip", st["v"][0] + 0.06, st["v"][1], ua, ub, z0 - 0.3, zt_, material, bev=0.16, segs=2))
    g0, g1, gn = P["grating"]
    for j in range(gn):                          # the grating over the track in the open bay
        u = g0 + (g1 - g0) * (j + 0.5) / gn
        parts.append(box("grating", vi + 0.15, P["belt_v"][1] - 0.1, u - 0.38, u + 0.38, z0 - 0.15, z0 + 0.22, "mid", bev=0.08, segs=1))
    for (u, v) in ((2.9, vi + 0.9), (2.9, vs - 0.9), (11.3, vi + 0.9), (11.3, P["plate_cut"][1] - 0.8), (-9.3, vs - 0.9), (-12.7, vs - 0.9)):
        parts.append(tube_z("rivet", 0.17, v, u, z1 - 0.02, z1 + 0.05, "light", segs=8))
    # the house cap and the light lip behind it
    c0, c1 = P["cap"]
    lip = P["cap_lip"]
    parts.append(shade(box("cap", vi + 0.1, P["cap_v"][0 if side > 0 else 1], c0, c1, z0, z1 + 0.06, "HOUSE", bev=0.16, segs=3), P["house_cap"], 0.7))
    prof = [(c0 - 0.06, z0 - 0.2), (c0 - 0.06, z1 + 0.02), (lip + 0.3, z1 - 0.08), (lip, z1 - 0.5), (lip, z0 - 0.2)]
    parts.append(prism_uz("cap_lip", prof, vi + 0.3, 11.8, "mid", bev=0.14, segs=2))
    # the mudguard on the track's upper rear facets: dull red (the original's own palette reds, not house-indexed)
    g = P["guard"]
    inner = [Vector(q) for q in g["pts"]]
    outer = []
    for i, q in enumerate(inner):
        ns = []
        for a_, b_ in ((i - 1, i), (i, i + 1)):
            if 0 <= a_ and b_ < len(inner):
                d = (inner[b_] - inner[a_]).normalized()
                ns.append(Vector((d.y, -d.x)))            # to the right of the direction of travel = outwards (up / rear)
        nn = sum(ns, Vector((0.0, 0.0))).normalized()
        outer.append(q + nn * (g["t"] / max(0.5, nn.dot(ns[0]))))
    poly = [(q.x, q.y) for q in inner] + [(q.x, q.y) for q in outer[::-1]]
    parts.append(prism_uz("guard", poly, g["v"][0], g["v"][1], "dullred", bev=0.08, segs=2))


def pod_parts(parts):
    common = []
    track_parts(common)
    for o in common:
        V2.apply_all(o)
        parts.append(o)
        parts.append(mirror(o))
    top_parts(parts, 1)
    side_modules(parts, P["modules_r"])
    # a rusty lug on one belt nose of each pod - the original's pods are copies, not mirror images: the brown
    # shows on the right pod's outer belt and on the left pod's inner plate
    nose = max(u for (u, _z) in P["outer_profile"])
    parts.append(box("nose_lug", P["outer_v"][0] + 0.25, P["outer_v"][1] - 0.3, nose - 0.2, nose + 0.36, 2.55, 3.6, "rim", bev=0.06, segs=1))
    left = []
    left.append(box("nose_lug", P["side_plate_v"] + 0.1, P["belt_v"][0] - 0.05, nose - 0.35, nose + 0.05, 2.55, 3.75, "rim", bev=0.06, segs=1))
    top_parts(left, -1)
    side_modules(left, P["modules_l"])
    for o in left:
        V2.apply_all(o)
        parts.append(mirror(o))
        bpy.data.objects.remove(o, do_unlink=True)


# ------------------------------------------------------------------------------------------------
# Hull, superstructure, domes, cannons


def hull_parts(parts):
    w = P["hull_w"]
    zb = P["hull_belly"]
    u0, u1 = P["hull_u"]
    zd = P["deck_z"]
    d0, d1 = P["deck_u"]
    sw = P["super_w"]
    s0, s1 = P["super_u"]
    sz = P["super_z"]
    tz = P["trough_z"]
    # lower hull between the pods
    bw = P["belly_w"]
    gz, gd = P["glacis"]
    parts.append(prism_uz("hull_lower", [(u0 - 0.4, zb), (u1 - 0.9, zb), (u1, zb + 1.0), (u1, gz), (u1 - gd, tz - 0.25), (u0, zd - 0.3), (u0 - 0.4, zb + 0.5)],
                          -bw, bw, "hull", bev=0.18))
    # rear deck: a light bar with a lighter centre panel
    parts.append(box("deck", -w + 0.05, w - 0.05, d0 + 0.9, d1 + 0.2, zd - 0.5, zd, "skirt", bev=0.12))
    parts.append(box("deck_rear", -bw, bw, d0 - 0.05, d0 + 0.95, zb + 0.5, zd - 0.05, "hull", bev=0.1))
    parts.append(box("deck_mid", -2.6, 2.6, d0 + 1.1, d1 - 0.4, zd - 0.05, zd + 0.16, "light", bev=0.08))
    for j in range(7):                           # louvres of the rear grille
        v = -4.5 + j * 1.5
        parts.append(box("grille", v - 0.42, v + 0.42, d0 - 0.16, d0 + 0.1, zd - 2.7, zd - 0.5, "skirt", bev=0.06, segs=1))
    # superstructure walls (dark) up to the blocks' feet
    cz = P["channel_z"]
    rb, fb = P["rear_block"], P["front_block"]
    parts.append(prism_uz("super_rear", [(s0, zd - 0.1), (rb["u"][1], zd - 0.1), (rb["u"][1], cz - 0.1), (s0 + 0.3, cz - 0.1), (s0, cz - 0.5)], -sw, sw, "hull", bev=0.14))
    parts.append(box("super_rear_face", -sw + 0.3, sw - 0.3, s0 - 0.06, s0 + 0.2, zd + 0.5, cz - 0.6, "skirt", bev=0.05, segs=1))
    parts.append(prism_uz("super_foot", [(s0 - 0.12, zd - 0.1), (rb["u"][1], zd - 0.1), (rb["u"][1], zd + 0.55), (s0 - 0.12, zd + 0.55)], -sw - 0.12, sw + 0.12, "recess", bev=0.05, segs=1))
    parts.append(box("super_front", -fb["v"][1], fb["v"][1], rb["u"][1] - 0.1, s1, zd - 0.1, cz - 0.1, "hull", bev=0.14))
    # exhaust slots in its rear wall (in shade: read only close up)
    for k in (-1, 1):
        for j in range(3):
            z = zd + 0.95 + j * 0.8
            parts.append(box("slot", k * 4.9 - 1.3, k * 4.9 + 1.3, s0 - 0.05, s0 + 0.25, z, z + 0.42, "recess"))
    # dome platform: light, with a dark channel down the middle
    for k in (-1, 1):
        a, b = sorted((k * rb["v"][0], k * rb["v"][1]))
        parts.append(box("platform", a, b, s0 + 1.1, rb["u"][0] + 0.1, cz - 0.2, sz + 0.2, "panel", bev=0.12))
        la, lb = sorted((k * rb["v"][0], k * (rb["v"][1] - 1.3)))
        parts.append(box("ledge", la, lb, s0 - 0.1, s0 + 1.25, cz - 0.2, sz + 0.26, "light", bev=0.12))
    parts.append(box("channel", -rb["v"][0] - 0.05, rb["v"][0] + 0.05, s0 + 0.3, fb["u"][0] - 0.2, cz - 0.6, cz, "recess"))
    # centre section between the rear blocks and the domes: a mid-grey trunk a little lower than the blocks
    parts.append(box("spine", -rb["v"][0] + 0.25, rb["v"][0] - 0.25, s0 + 0.5, rb["u"][1] - 0.3, cz - 0.05, P["spine_z"], "hull", bev=0.16))
    parts.append(box("crosspiece", -rb["v"][0] + 0.2, rb["v"][0] - 0.2, rb["u"][1] - 0.35, P["plate_red"]["u"][0] + 0.05, cz - 0.05, P["spine_z"] + 0.25, "hull", bev=0.14))
    parts.append(box("spine_hatch", -1.2, 1.2, -4.4, -1.6, P["spine_z"] - 0.02, P["spine_z"] + 0.1, "skirt", bev=0.06, segs=1))
    # the four white blocks
    for k in (-1, 1):
        if k > 0:
            a, b = rb["v"][0], rb["v_core"]
            parts.append(box("rear_block_out", b - 0.1, rb["v_right"], rb["u"][0] + 0.2, rb["u"][1] - 0.3, cz - 0.2, rb["z_out"], "white", bev=0.36, segs=4))
        else:
            a, b = -rb["v"][1] - 0.4, -rb["v"][0] - 0.45
        zr = rb["z"] if k > 0 else rb["z_left"]
        parts.append(box("rear_block", a, b, rb["u"][0], rb["u"][1], cz - 0.2, zr, "white", bev=0.42, segs=4))
        # an inspection hatch on top (close-up detail)
        parts.append(box("hatch", a + 0.7, b - 0.7, rb["u"][0] + 1.2, rb["u"][1] - 1.2, zr - 0.02, zr + 0.1, "white", bev=0.06, segs=1))
        vi_, vo_ = fb["v"]
        sec = [(k * vi_, cz - 0.2), (k * vi_, fb["z"]), (k * (vi_ + fb["ridge"]), fb["z"]), (k * vo_, fb["eave"]), (k * vo_, cz - 0.2)]
        parts.append(prism_vz("front_block", sec, fb["u"][0], fb["u"][1], "white", bev=0.38, segs=4))
        # its outer flank: a sloped mid-grey plate down to the pod
        pts = []
        for u in (fb["u"][0] + 0.1, fb["u"][1] - 0.05):
            pts += [(k * (vo_ - 0.05), u, fb["eave"] - 0.3), (k * fb["skirt_v"], u, zd + 0.5), (k * fb["skirt_v"], u, zd), (k * (vo_ - 0.05), u, zd)]
        parts.append(hull_pts("block_flank", pts, "skirt", bev=0.1))
    # the house plate between the front blocks: slightly recessed, thin raised rims, a short nose
    pr = P["plate_red"]
    (ua, ub), hv, (za, zb_), (un, zn) = pr["u"], pr["v"], pr["z"], pr["nose"]
    parts.append(shade(prism_uz("plate_red", [(ua, za - 0.5), (ub, zb_ - 0.5), (ub + 0.25, zb_ - 0.2), (ub, zb_), (ua, za)], -hv, hv, "HOUSE", bev=0.06, segs=1),
                       P["house_plate"], 0.9))
    parts.append(prism_uz("plate_foot", [(ub - 0.2, zb_ - 0.6), (un, zn - 0.5), (un, zn), (ub + 0.2, zb_ - 0.25)], -hv, hv, "hull", bev=0.06, segs=1))
    # the near-black trough between the cannons and the light nose plate
    parts.append(box("trough", -bw + 0.2, bw - 0.2, s1 - 0.1, u1 - gd - 0.1, tz - 0.5, tz, "hull"))
    parts.append(box("front_wall", -fb["v"][1] + 0.1, fb["v"][1] - 0.1, s1 - 0.02, s1 + 0.1, tz, cz - 0.3, "recess"))
    np_ = P["nose_plate"]
    parts.append(prism_uz("nose_plate", [(np_["u"][0], np_["z"] - 0.4), (np_["u"][1] - 0.5, np_["z"] - 0.4), (np_["u"][1], np_["z"] - 0.9), (np_["u"][1], np_["z"] - 0.45),
                                         (np_["u"][1] - 0.5, np_["z"]), (np_["u"][0], np_["z"])], -np_["v"], np_["v"], "skirt", bev=0.1))


def dome_parts(parts):
    v0 = P["v0"]
    sz = P["super_z"] + 0.2
    for name, (u, v, z, r) in (("dome_l", P["dome_l"]), ("dome_r", P["dome_r"])):
        v -= v0                                    # body coordinates
        parts.append(tube_z(name + "_collar", r * 0.82, v, u, sz - 0.05, z - r * 0.55, "brass_dark", segs=20, r1=r * 0.74))
        parts.append(tube_z(name + "_base", r * 0.98, v, u, sz - 0.05, sz + 0.28, "brass_dark", segs=20))
        parts.append(sphere(name, (v, u, z), r, "brass", segs=24, rings=14))
    # whip antenna on the left dome's rear outer shoulder
    au, av, az, ar = P["antenna"]
    av -= v0
    lu, lv, lz, lr = P["dome_l"]
    base = lz + lr * 0.45
    parts.append(tube_z("antenna_mount", ar * 2.1, av, au, base - 0.5, base + 0.5, "antenna", segs=8, r1=ar * 1.5))
    parts.append(tube_z("antenna", ar, av, au, base + 0.3, az, "antenna", segs=6, r1=ar * 0.55))
    # the visor on the right dome: a dark teal frame round the front with the lens in it
    ru, rv, rz, rr = P["dome_r"]
    rv -= v0
    vz, vh, ha = rz + P["visor"]["dz"], P["visor"]["h"], math.radians(P["visor"]["half_angle"])

    def band(name, r_out, z0, z1, a0, a1, material, n=10):
        """A strip round the dome's front between heights z0..z1 and angles a0..a1 (0 = forward)."""
        bm = bmesh.new()
        ring_lo, ring_hi, in_lo, in_hi = [], [], [], []
        for i in range(n + 1):
            a = a0 + (a1 - a0) * i / n
            for zz, lst_o, lst_i in ((z0, ring_lo, in_lo), (z1, ring_hi, in_hi)):
                rad = math.sqrt(max(0.01, rr * rr - (zz - rz) ** 2))
                lst_o.append(bm.verts.new((rv + (rad + r_out) * math.sin(a), ru + (rad + r_out) * math.cos(a), zz)))
                lst_i.append(bm.verts.new((rv + (rad - 0.3) * math.sin(a), ru + (rad - 0.3) * math.cos(a), zz)))
        for i in range(n):
            bm.faces.new([ring_lo[i], ring_lo[i + 1], ring_hi[i + 1], ring_hi[i]])
            bm.faces.new([ring_hi[i], ring_hi[i + 1], in_hi[i + 1], in_hi[i]])
            bm.faces.new([in_lo[i], in_lo[i + 1], ring_lo[i + 1], ring_lo[i]])
        bm.faces.new([ring_lo[0], ring_hi[0], in_hi[0], in_lo[0]])
        bm.faces.new([ring_hi[n], ring_lo[n], in_lo[n], in_hi[n]])
        return new_obj(name, bm, material)

    parts.append(band("visor_frame", 0.1, vz - vh * 0.5 - 0.22, vz + vh * 0.5 + 0.22, -ha - 0.12, ha + 0.12, "teal"))
    lens = band("visor_lens", 0.16, vz - vh * 0.5, vz + vh * 0.5, -ha * 0.62, ha * 0.62, "GLOW")
    return lens


def cannon_parts(parts):
    z = P["cannon_z"]
    for k in (-1, 1):
        v = k * P["cannon_v"] + P["cannon_dv"]
        c0, c1, cr = P["collar"]
        parts.append(cyl("breech", cr * 0.8, P["super_u"][1] - 0.6, c0 + 0.1, v, z, "hull", segs=20))
        parts.append(cyl("collar", cr - 0.22, c0, c1, v, z, "brass_dark", segs=20, bev=0.1))
        parts.append(arc_shell("collar_cover", cr - 0.2, cr + 0.1, c0 - 0.1, c1 - 0.3, v, z, P["cover_arc"], "hull"))
        parts.append(arc_shell("collar_strip", cr + 0.05, cr + 0.17, c0 + 0.5, c1 - 0.45, v, z, 20.0, "skirt"))
        u = c0 + 1.45                            # one cooling slot through the cover
        parts.append(arc_shell("slot", cr + 0.08, cr + 0.115, u, u + 0.22, v, z, P["cover_arc"] - 14, "recess"))
        r0, r1, rr = P["ring"]
        parts.append(cyl("ring", rr, r0, r1, v, z, "recess", segs=18))
        b0, b1, br = P["barrel"]
        parts.append(cyl("barrel", br, b0, b1, v, z, "brass", segs=22))
        parts.append(cyl("barrel_band", br + 0.07, b0 + 0.25, b0 + 0.75, v, z, "brass", segs=22))
        m0, m1, m2, mr, mt = P["muzzle"]
        parts.append(ring_u("muzzle", br * 0.6, ((m0, mr), (m1, mr), (m2, mt)), v, z, "brass_dark", segs=22))
        parts.append(cyl("bore", br * 0.64, m2 - 1.1, m2 - 0.8, v, z, "recess", segs=16))


def build():
    root = bpy.data.objects.new(KEY, None)
    bpy.context.scene.collection.objects.link(root)
    parts = []
    pod_parts(parts)
    hull_parts(parts)
    cannon_parts(parts)
    lens = dome_parts(parts)
    body = V3.finish_node(parts, "body", (0, 0, 0), (P["v0"], 0, 0))
    body.data.transform(Matrix.Scale(P["grow"], 4))
    body.parent = root
    # the lens stays its own object: its emissive material is not baked into the atlas
    colour_attribute(lens)
    lens.data.transform(Matrix.Scale(PX * P["grow"], 4) @ Matrix.Translation(Vector((P["v0"], 0, 0))))
    lens.name = "visor"
    lens.parent = root
    bpy.context.view_layer.update()
    return root


# ------------------------------------------------------------------------------------------------
# Materials


def mat(name):
    """V3's material + the viewport colour (flat previews) and the visor's emission."""
    m = V3.mat(name)
    c = PREVIEW_HOUSE if name == "HOUSE" else COL.get(name, V3.COL[name])
    m.diffuse_color = (*srgb_to_linear(c), 1)
    if name == "GLOW" and not m.get("d2k_glow"):
        b = m.node_tree.nodes["Principled BSDF"]
        b.inputs["Emission Color"].default_value = (*srgb_to_linear(COL["GLOW"]), 1)
        b.inputs["Emission Strength"].default_value = 1.6
        m["d2k_glow"] = True
    return m


def finish_materials():
    for name in list(V3._mats):
        mat(name)


def surface(nt, name):
    """V3's weathering tree + what this vehicle adds: soot on the superstructure's rear (the
    exhaust slots are there), the pods' left-clean / right-dirty tops, worn plate ends, darker
    plate rims, a little dust on the belts' upward faces, the brass's tilt shading and a heat
    tint towards the muzzles."""
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

    if name in ("light", "hull", "white", "skirt", "mid"):
        # soot from the exhaust slots: the rear of the vehicle (behind u = -6 px), thickest behind the domes
        rear = remap(pos.outputs["Y"], -5.5 * PX, -11.6 * PX, 0.0, 1.0)
        up = remap(pos.outputs["Z"], 6.0 * PX, 7.2 * PX, 0.0, 1.0)
        n = nodes.new("ShaderNodeTexNoise")
        n.inputs["Scale"].default_value = 13.0
        n.inputs["Detail"].default_value = 3.0
        links.new(geo.outputs["Position"], n.inputs["Vector"])
        so = mul(mul(mul(rear, up), remap(n.outputs["Fac"], 0.2, 0.7, 0.45, 0.9)), SOOT_W[name])
        c = mix(so, c, V3.DIRT["soot"])
    if name in POD_DIRT:
        # the original's pods are painted the same way on both sides, not mirrored: each pod's top is clean
        # towards the vehicle's left (-v) and dirtier towards its right
        pxg = nodes.new("ShaderNodeSeparateXYZ")
        links.new(geo.outputs["Position"], pxg.inputs[0])
        g_r = mul(remap(pxg.outputs["X"], 9.4 * PX, 12.2 * PX, 0.0, 1.0), remap(pxg.outputs["X"], 5.6 * PX, 6.0 * PX, 0.0, 1.0))
        g_l = mul(remap(pxg.outputs["X"], -9.6 * PX, -6.8 * PX, 0.0, 1.0), remap(pxg.outputs["X"], -5.6 * PX, -6.0 * PX, 0.0, 1.0))
        addg = nodes.new("ShaderNodeMath")
        addg.operation = "ADD"
        links.new(g_r, addg.inputs[0])
        links.new(g_l, addg.inputs[1])
        low = remap(pxg.outputs["Z"], 7.6 * PX, 7.4 * PX, 0.0, 1.0)               # the pod tops only
        ng = nodes.new("ShaderNodeTexNoise")
        ng.inputs["Scale"].default_value = 9.0
        ng.inputs["Detail"].default_value = 3.0
        links.new(geo.outputs["Position"], ng.inputs["Vector"])
        g = mul(mul(mul(addg.outputs[0], low), remap(ng.outputs["Fac"], 0.3, 0.7, 0.55, 1.0)), POD_DIRT[name][0])
        c = mix(g, c, POD_DIRT[name][1])
    if name == "white":
        # worn, dirtier areas the original shows: the rear end of each front plate (more on the left pod), the rear blocks
        px_ = nodes.new("ShaderNodeSeparateXYZ")
        links.new(geo.outputs["Position"], px_.inputs[0])
        pod = remap(px_.outputs["X"], 6.0 * PX, 6.6 * PX, 0.0, 0.9)              # right pod
        podl = remap(px_.outputs["X"], -6.0 * PX, -6.6 * PX, 0.0, 2.2)            # left pod
        add = nodes.new("ShaderNodeMath")
        add.operation = "ADD"
        links.new(pod, add.inputs[0])
        links.new(podl, add.inputs[1])
        rear_end = mul(remap(px_.outputs["Y"], 7.5 * PX, 2.0 * PX, 0.0, 0.5), remap(px_.outputs["Y"], 1.0 * PX, 1.6 * PX, 0.0, 1.0))
        c = mix(mul(add.outputs[0], rear_end), c, (0.46, 0.42, 0.39))
        # the left rear block's top is duller than the right one's
        lb = mul(remap(px_.outputs["Z"], 11.6 * PX, 12.2 * PX, 0.0, 0.22), remap(px_.outputs["X"], -2.0 * PX, -2.6 * PX, 0.0, 1.0))
        c = mix(mul(lb, remap(px_.outputs["Y"], 0.6 * PX, 0.0, 0.0, 1.0)), c, (0.47, 0.43, 0.40))
    if name in ("white", "light", "mid"):
        # the original's plates are lighter in the middle and darker along their rims
        bev = nodes.new("ShaderNodeBevel")
        bev.samples = 6
        bev.inputs["Radius"].default_value = 0.8 * PX
        dot = nodes.new("ShaderNodeVectorMath")
        dot.operation = "DOT_PRODUCT"
        links.new(bev.outputs["Normal"], dot.inputs[0])
        links.new(geo.outputs["Normal"], dot.inputs[1])
        c = mix(remap(dot.outputs["Value"], 0.999, 0.93, 0.0, RIM_W[name]), c, (0.40, 0.36, 0.32))
    if name in TOP_DUST:
        # dust settles on what faces up: the track and its frames read dark grey from above, near-black from the side
        nz = nodes.new("ShaderNodeSeparateXYZ")
        links.new(geo.outputs["Normal"], nz.inputs[0])
        nd = nodes.new("ShaderNodeTexNoise")
        nd.inputs["Scale"].default_value = 11.0
        nd.inputs["Detail"].default_value = 3.0
        links.new(geo.outputs["Position"], nd.inputs["Vector"])
        c = mix(mul(remap(nz.outputs["Z"], 0.3, 0.85, 0.0, TOP_DUST[name]), remap(nd.outputs["Fac"], 0.25, 0.75, 0.6, 1.0)), c, (0.43, 0.39, 0.345))
    if name == "brass":
        # the original's brass reads bright yellow on top, orange on the flanks, dark brown below:
        # that gradient is painted into the albedo by the surface's tilt (the vehicle only yaws)
        nrm = nodes.new("ShaderNodeSeparateXYZ")
        links.new(geo.outputs["Normal"], nrm.inputs[0])
        c = mix(remap(nrm.outputs["Z"], 0.96, 0.74, 0.0, 0.94), c, (0.78, 0.46, 0.15))
        c = mix(remap(nrm.outputs["Z"], 0.5, -0.1, 0.0, 0.9), c, (0.50, 0.31, 0.13))
        # heat tint: the last 2 px before the muzzle darken to a burnt bronze
        hot = remap(pos.outputs["Y"], 16.0 * PX, 17.4 * PX, 0.0, 0.35)
        c = mix(hot, c, (0.36, 0.2, 0.08))
    return c, r, m


_V3_SURFACE = V3.surface_tree


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
    """Bake all materials but KEEP into one atlas material "body" and remap the slots (V3's bake;
    the visor lens object is not in `objs`)."""
    procs = procedural_materials()
    V3.uv_unwrap(objs, margin=0.003)
    images = V2.bake_atlas(objs, procs, size, samples)
    mr = V3.pack_mr(images, size)
    body_mat = V3.export_material(images, mr, os.path.join(bpy.app.tempdir, "d2k_devastator_bake"))
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
# Review: silhouettes / flat colours with the game camera, turntable and close-ups


def iou_report(root, npz_path, flat_dir=None, scale=4, elev=45.0):
    """Silhouette IoU against the original, aligned as render_pack --subpixel does (texel-precise
    centroid shift); with flat_dir the 32 facings in flat material colours on the original's canvas."""
    import numpy as np
    d = np.load(npz_path)
    ox, oy = (int(v) for v in d["origin"])
    n = d["body_opaque"].shape[1]
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "FLAT"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.render_aa = "8"
    scene.render.film_transparent = True
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "Standard"
    size = n * scale
    yaw = bpy.data.objects.new("YAW", None)
    scene.collection.objects.link(yaw)
    root.parent = yaw
    tmp = os.path.join(bpy.app.tempdir, "d2k_iou_devastator.png")
    cam = V2.game_camera(scene, elev, 32 * scale, size)
    frames = []
    for f in range(32):
        yaw.rotation_euler = (0, 0, -math.radians(f * 11.25))
        scene.render.filepath = tmp
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(tmp)
        frames.append(np.array(img.pixels[:], np.float32).reshape(size, size, 4)[::-1].copy())
        bpy.data.images.remove(img)
    yaw.rotation_euler = (0, 0, 0)
    bpy.data.objects.remove(cam)
    root.parent = None
    bpy.data.objects.remove(yaw)
    rgba = np.array(frames)
    alpha = rgba[..., 3]
    # the alignment of render_pack --subpixel: mean centroid difference, in texels
    yy, xx = np.mgrid[0:size, 0:size]
    ds = []
    for f in range(32):
        om = d["body_opaque"][f]
        ys, xs = np.nonzero(om)
        co = np.array([xs.mean() + 0.5 - ox, ys.mean() + 0.5 - oy])
        a = alpha[f]
        w = a.sum()
        cr = np.array([((xx + 0.5) * a).sum() / w - size / 2, ((yy + 0.5) * a).sum() / w - size / 2]) / scale
        ds.append(cr - co)
    dmean = np.mean(ds, 0)
    tx, ty = int(round(-dmean[0] * scale)), int(round(-dmean[1] * scale))
    log(f"IOU: render centroid - original centroid = ({dmean[0]:+.2f}, {dmean[1]:+.2f}) px -> frames moved ({tx / scale:+.2f}, {ty / scale:+.2f}) px")

    def place(a):
        """Canvas centre = model origin -> the original's canvas (anchor at ox, oy), aligned."""
        return np.roll(np.roll(a, ty + (oy - n // 2) * scale, 0), tx + (ox - n // 2) * scale, 1)

    ious = []
    for f in range(32):
        a = place(alpha[f])
        r = a.reshape(n, scale, n, scale).mean((1, 3)) > 0.5
        o = d["body_opaque"][f]
        ious.append((r & o).sum() / max(1, (r | o).sum()))
    log(f"IOU body: mean {np.mean(ious):.3f} min {np.min(ious):.3f} | " + " ".join(f"{v:.2f}" for v in ious))
    if flat_dir:
        os.makedirs(flat_dir, exist_ok=True)
        placed = np.stack([place(rgba[f]) for f in range(32)])
        np.savez_compressed(os.path.join(flat_dir, "flat.npz"), rgba=(np.clip(placed, 0, 1) * 255 + 0.5).astype(np.uint8), scale=scale,
                            shift=np.array([tx, ty]), iou=np.array(ious))
        log("flat colours -> " + os.path.join(flat_dir, "flat.npz"))
    return ious


def position_maps(root, npz_path, out_path, scale=4, elev=45.0):
    """For every facing, the model point (u, v, z in px) seen at each pixel of the original's canvas
    (aligned like iou_report): lets the check scripts map a difference between the pack and the
    original back onto the model (devastator_check/errmap.py). -> out_path (.npz: pos [32, N, N, 3], hit)."""
    import numpy as np
    d = np.load(npz_path)
    ox, oy = (int(v) for v in d["origin"])
    n = d["body_opaque"].shape[1]
    size = n * scale
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    V3.setup_device(scene)
    scene.cycles.samples = 1
    scene.cycles.use_denoising = False
    scene.cycles.filter_width = 0.01
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "OPEN_EXR"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "32"
    pm = bpy.data.materials.new("POSMAP")
    pm.use_nodes = True
    nt = pm.node_tree
    for nd in list(nt.nodes):
        nt.nodes.remove(nd)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    tc = nt.nodes.new("ShaderNodeTexCoord")
    tc.object = root
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (32.0 / 64.0, 32.0 / 64.0, 32.0 / 64.0)      # px / 64 + 0.5: positive, < 1
    mp.inputs["Location"].default_value = (0.5, 0.5, 0.0)
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    nt.links.new(mp.outputs[0], em.inputs["Color"])
    nt.links.new(em.outputs[0], out.inputs["Surface"])
    meshes = V2.meshes_under(root)
    saved = {o.name: [sl.material for sl in o.material_slots] for o in meshes}
    for o in meshes:
        for sl in o.material_slots:
            sl.material = pm
    yaw = bpy.data.objects.new("YAW", None)
    scene.collection.objects.link(yaw)
    root.parent = yaw
    cam = V2.game_camera(scene, elev, 32 * scale, size)
    tmp = os.path.join(bpy.app.tempdir, "d2k_pos_devastator.exr")
    frames = []
    for f in range(32):
        yaw.rotation_euler = (0, 0, -math.radians(f * 11.25))
        scene.render.filepath = tmp
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(tmp)
        frames.append(np.array(img.pixels[:], np.float32).reshape(size, size, 4)[::-1].copy())
        bpy.data.images.remove(img)
    yaw.rotation_euler = (0, 0, 0)
    bpy.data.objects.remove(cam)
    root.parent = None
    bpy.data.objects.remove(yaw)
    for o in meshes:
        for sl, m in zip(o.material_slots, saved[o.name]):
            sl.material = m
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_depth = "8"
    rgba = np.array(frames)
    alpha = rgba[..., 3]
    yy, xx = np.mgrid[0:size, 0:size]
    ds = []
    for f in range(32):
        ys, xs = np.nonzero(d["body_opaque"][f])
        co = np.array([xs.mean() + 0.5 - ox, ys.mean() + 0.5 - oy])
        a = alpha[f]
        w = a.sum()
        ds.append(np.array([((xx + 0.5) * a).sum() / w - size / 2, ((yy + 0.5) * a).sum() / w - size / 2]) / scale - co)
    dmean = np.mean(ds, 0)
    tx, ty = int(round(-dmean[0] * scale)), int(round(-dmean[1] * scale))
    placed = np.stack([np.roll(np.roll(rgba[f], ty + (oy - n // 2) * scale, 0), tx + (ox - n // 2) * scale, 1) for f in range(32)])
    c = placed[:, scale // 2::scale, scale // 2::scale]                 # the texel at each original pixel's centre
    pos = np.stack([(c[..., 1] - 0.5) * 64.0, (c[..., 0] - 0.5) * 64.0, c[..., 2] * 64.0], -1)   # (u, v, z) px: Blender x = v, y = u
    np.savez_compressed(out_path, pos=pos.astype(np.float32), hit=c[..., 3] > 0.5, shift=np.array([tx, ty]))
    log(f"position maps -> {out_path} (shift {tx}, {ty} texels)")


def review_renders(root, out_dir, samples, size=720):
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
    bpy.ops.mesh.primitive_plane_add(size=30, location=(0, 0, -0.0005))
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
    tgt = Vector((0, -0.02, 0.2))

    def shot(name, az_deg, el_deg, dist, lens, target=tgt, sun_az=150.0, sun_el=52.0):
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
        az = -90 - 45 * i                                  # 0: from behind (the game's facing 0), then clockwise seen from above
        shot(f"turn_{i}", az, 30 if i % 2 == 0 else 22, 3.1, 70, sun_az=az - 60)
    scene.render.resolution_x, scene.render.resolution_y = 1400, 900
    shot("closeup_front", 58, 22, 3.0, 70, sun_az=120)
    shot("closeup_rear", -122, 26, 3.2, 70, sun_az=-170)
    shot("closeup_side", 0, 14, 3.1, 80, target=Vector((0, -0.03, 0.2)), sun_az=-55)
    shot("closeup_track", 20, 12, 1.5, 85, target=Vector((0.36, 0.3, 0.12)), sun_az=-40)
    shot("closeup_domes", -60, 34, 1.7, 85, target=Vector((0.0, -0.22, 0.38)), sun_az=-140)
    shot("closeup_top", -90, 62, 3.4, 80, sun_az=-150, sun_el=58)
    bpy.data.objects.remove(gp)
    bpy.data.objects.remove(so)
    bpy.data.objects.remove(co)


# ------------------------------------------------------------------------------------------------


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opt = dict(out=None, bake=True, bake_size=2048, iou=None, flat=None, posmap=None, turntable=None, samples=64)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--out":
            opt["out"] = argv[i + 1]; i += 1
        elif a == "--no-bake":
            opt["bake"] = False
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
    if opt["out"] is None and opt["iou"] is None and opt["turntable"] is None:
        opt["out"] = os.path.join(os.path.dirname(os.path.dirname(HERE)), "artpacks", "models-devastator")

    V2.setup_gpu = V3.setup_device        # the bake helpers of v2 pick the device through this
    bpy.ops.wm.read_factory_settings(use_empty=True)
    t0 = time.time()
    root = build()
    finish_materials()
    body = next(o for o in root.children if o.name == "body")
    objs = [body]
    log(f"built: {V2.tri_count(V2.meshes_under(root))} triangles in {time.time() - t0:.1f} s")
    if opt["iou"]:
        iou_report(root, opt["iou"], opt["flat"])
        if opt["posmap"]:
            position_maps(root, opt["iou"], opt["posmap"])
    if opt["bake"] and (opt["out"] or opt["turntable"]):
        t1 = time.time()
        bake_and_merge(objs, opt["bake_size"])
        log(f"textures baked in {time.time() - t1:.1f} s ({opt['bake_size']} px atlas)")
    if opt["out"]:
        path = os.path.join(opt["out"], "models", "unit", KEY + ".glb")
        V3.export_glb(root, path)
        log(f"exported {path} ({os.path.getsize(path) // 1024} KB)")
        log("2D pack: tools/render_sprites/render_pack.py --models " + opt["out"] + " --out artpacks/rendered-devastator "
            "--types " + KEY + " " + RENDER_FLAGS)
    if opt["turntable"]:
        t2 = time.time()
        review_renders(root, opt["turntable"], opt["samples"])
        log(f"review renders in {time.time() - t2:.1f} s -> {opt['turntable']}")


if __name__ == "__main__":
    main()

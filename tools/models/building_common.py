"""Shared code of the redesigned buildings of the Reimagined pack - the recipe the owner approved on the wind traps
(docs/3d-models.md "Wind trap redesigns", "Refinery redesign v2"): the approved model's silhouette, layout and
colours, human-scale detail, real materials, the lived-in weathering. One generator per building on top of this
module -

    reimagined_wind_trap.py            ATREIDES_WIND_TRAP   (the first one, approved by the owner)
    reimagined_wind_trap_harkonnen.py  HARKONNEN_WIND_TRAP
    reimagined_wind_trap_ordos.py      ORDOS_WIND_TRAP
    reimagined_refinery_v2.py          ATREIDES_REFINERY    (the approved model's own parts + detail)
    reimagined_construction_yard_v2.py ATREIDES_CONSTRUCTION_YARD (the same; its crane's nodes and poses: REST / POSES)

(Until 2026-11-18 this module was windtrap_common.py; that name is still importable - it is this module - and the
three wind trap files use it.)

What is here: the mesh helpers every building uses (plates with real joints and stencils on any shell with S / D,
rotors and moving nodes, doors, ladders, railings, hand wheels, flanged pipes, warning plates; for the wind traps
the Atreides / Harkonnen `Scoop` with its arch bands), the fittings' palette (doors, rails, glass, hazard paint,
the lit cyan glass), the surface / weathering pass `surface()` with its two knobs WEATHERING / MOTTLE, and build() /
run() / main() (the command line). A building's file holds its palette, its part builders and what the weathering
has to know about it, and hands them over with `setup(...)`:

    import building_common as BC
    BC.setup("KEY", parts=[(label, fn(main, moving, anchors)), ...], col=COL, spec=SPEC, P=..., STAIN=..., ...)
    if __name__ == "__main__":
        BC.main()

    blender -b --factory-startup --python tools/models/<building file> -- \
        [--out DIR]          DIR/models/building/KEY.glb, KEY_lod1.glb, DIR/stats/KEY.json
        [--no-bake]          geometry only
        [--bake-size 2048] [--lod 0.34]
        [--weathering 1.0] [--mottle 1.0]   try another strength of the grime (WEATHERING / MOTTLE below)
        [--preview DIR]      quick plain-colour views (no bake)
        [--turntable DIR] [--samples 40] [--work DIR]
        [--tris]             log the triangles per part group
    install: tools/models/wind_trap_check/install.sh DIR artpacks/models-reimagined KEY

A part builder appends its objects to `main` (everything that does not move), further nodes to `moving` as
(parts, node name, origin px, axis) - a spinning part built about its local origin with the spin axis = +Z, or with
axis None a second still node (the refinery's `base`), with axis "fixed" a node whose parts stand where they are in
the footprint's px and whose origin is put at `origin px` (a pose node: the yard's crane about its post) - and effect points to `anchors` as (name, (x, y, z) px, size).

Everything is in ORIGINAL SPRITE PIXELS (1 px = 1/32 tile; x east, y north of the footprint centre, z up) on the
helpers of atreides_kit.py and the Reimagined bake (reimagined_kit.py). Nothing is copied from the original art.
"""
import json
import math
import os
import sys
import time

import bmesh
import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ.setdefault("D2K_GLOW_3D", "1")
import combat_tank_v2 as V2  # noqa: E402
import combat_tank_v3 as V3  # noqa: E402
import atreides_kit as K  # noqa: E402  (sets the building palette on import)
import refinery_pilot_fable as RF  # noqa: E402

# ---- Weathering strength: THE TWO KNOBS (docs/3d-models.md "Wind trap redesigns" -> "Weathering") -------------------
# WEATHERING  every layer of lived-in grime at once: the stains, run-off and rust streaks, sand on what faces up and
#             in the recesses, the dirty lower parts, grime in creases and joints, chipped edges, soot at the flue,
#             and the dust on the house-colour panels. 0 = the clean first test model (exactly), 1 = the approved
#             amount, 1.5 = heavy; above ~2 it turns muddy.
# MOTTLE      only the large darker / dustier patches on the shells and walls (a multiplier on top of WEATHERING):
#             0 = even paint, 1 = a little under the round-2 model's blotchiness, 1.5 = strongly stained.
# Both can be given on the command line (--weathering X --mottle Y) to try a value without editing the file. The
# approved level is the same for every house: 1.0 / 1.0.
WEATHERING = 1.0
MOTTLE = 1.0
PX = V3.PX
log = V2.log
rings_solid, new_obj = V3.rings_solid, V3.new_obj
beam, tube, obox, polar = K.beam, K.tube, K.obox, K.polar

# ---- What a house file sets (setup() below); the values here are placeholders or the Atreides ones ------------------
KEY = None
P = None                             # the scoops' layout (atreides_wind_trap.P: mouth_s, mouth_n, face_ang, half, tall,
PT = 1.6                             # deep, lip) and their section's point (Scoop; a house without scoops needs neither)
# Moving parts (separate nodes; origin on the spin axis, the axis is the node's local +Z in Blender = local +Y in the
# glb) and effect points (empties), as the house documents them (docs/3d-models.md quotes them)
NODES = {}
ANCHOR_DOC = {}
PARTS = []                           # [(label, fn(main, moving, anchors))] in build order
RIBS = ()                            # the rib flanges' t along a scoop: rust bleeds out of them (scoop_streaks)
SHELL_MATS = ()                      # the materials that get the shells' own streaks (SHELL_STREAKS)
LIP_MATS = ()                        # of those, the lip: run-off from the sill, no rib rust
FLUE_TOP = None                      # px: the open top of the exhaust flue, or a list of tops (soot); None = no soot
SHELL_STREAKS = None                 # fn(T, name, pos, cur, rough_s, g_rust, g_run) -> (cur, rough_s); set below
POLISH = ("steel",)                  # kinds whose worn upper edges are polished metal (else: paint worn lighter)
# the review renders (--turntable): reimagined_kit.review's distance, target and close-ups
REVIEW = dict(dist=6.0, target=(0, 0.6, 0.45),
              closeups=[("closeup_front", -64, 26, 4.3, 70, (0.15, 0.1, 0.42)), ("closeup_flank", -122, 22, 4.0, 70, (-0.3, 0.3, 0.42)),
                        ("closeup_game", -90, 45, 5.0, 60, (0.0, 0.7, 0.4))])
# the two extra plain-colour views of --preview (azimuth, elevation, distance, target): the south-west flank, the top
PREVIEW_VIEWS = ((-150, 24, 3.6, (-0.45, 0.1, 0.35)), (-90, 62, 5.0, (0.0, 0.6, 0.4)))
# the Reimagined bake's detail settings (reimagined_kit.bake_reimagined)
DETAIL = dict(panel_keep=0.0, rivet_keep=0.0, bevel_r=0.014, house_uv=0.6, ao_distance=0.3, dent=0.10, wear=0.3)
PREVIEW_DIST = 6.5                   # --preview: the camera's distance (tiles) in the four standard views
# Per building, beyond the shells' streaks (empty / None for the wind traps):
MAT_GRIME = {}                       # material -> the share of WEATHERING it takes (1 = all of it; white steps: less)
SURFACE_EXTRA = None                 # fn(T, name, kind, ctx, cur, rough_s, metal_s, W) -> (cur, rough_s, metal_s): the
#                                      building's own stains on top of the shared layers (the refinery's spice dust);
#                                      ctx = dict(pos, nrm: the separated position (tiles) / normal; top, upright, near,
#                                      part_h: sockets 0..1). Called only with WEATHERING > 0, never for glass.
# A building with pose variants (docs/3d-models.md "Pose variants": the construction yard's crane):
REST = None                          # fn(nodes: name -> object): the idle pose, applied after the bake, before the export
POSES = ()                           # [fn(nodes)]: pose NN -> OUT/variants/KEY_poseNN.glb (node transforms only, ready
POSE_SPEC = None                     # for the pack) + KEY.spec.json = dict(POSE_SPEC, key, poses); install.sh copies them

# ------------------------------------------------------------------------------------------------
# Materials (sRGB; (metallic, roughness, kind)): the fittings every house uses. A house adds its shells and may
# re-colour any of these (setup(col=, spec=)).

COL = dict(
    shell_core=(0.210, 0.180, 0.150),   # under the plates: the joints
    duct=(0.170, 0.170, 0.180),         # the intake duct behind the mouth: dark steel
    duct_dk=(0.050, 0.050, 0.055),
    blade=(0.360, 0.370, 0.380),        # turbine / fan blades: steel (the mouth stays a dark arch)
    slat=(0.150, 0.155, 0.170),         # louvre slats, duct fittings: dark gunmetal (the mouth stays a dark arch)
    glow_a=(0.070, 0.300, 0.350),       # the lit panels on the slats: dark cyan glass (glows: EMIT)
    glow_b=(0.060, 0.260, 0.310),
    grate=(0.270, 0.260, 0.250),        # mouth floor
    door=(0.330, 0.350, 0.370),         # door leaves: blue-grey paint
    door_fr=(0.640, 0.620, 0.580),      # frames, handles, small fittings
    win_lit=(0.200, 0.160, 0.100),      # door windows (lit at night)
    glass=(0.060, 0.080, 0.100),
    rubber=(0.070, 0.070, 0.070),       # cables, hoses, gaskets
    hz_y=(0.900, 0.700, 0.140),         # hazard yellow / black, warning plates
    hz_k=(0.100, 0.095, 0.090),
    stencil=(0.420, 0.440, 0.470),      # stencilled numbers
    rail=(0.680, 0.650, 0.580),         # railings, ladders
    red_p=(0.620, 0.130, 0.090),        # hand wheels
)
SPEC = dict(
    shell_core=(0.2, 0.75, "dark"), duct=(0.7, 0.5, "steel"), duct_dk=(0.3, 0.8, "dark"),
    blade=(0.9, 0.30, "steel"), slat=(0.55, 0.45, "steel"), glow_a=(0.0, 0.12, "glass"), glow_b=(0.0, 0.14, "glass"),
    grate=(0.7, 0.55, "steel"), door=(0.0, 0.5, "paint"), door_fr=(0.7, 0.38, "steel"), win_lit=(0.0, 0.1, "glass"),
    glass=(0.0, 0.08, "glass"), rubber=(0.0, 0.9, "rubber"), hz_y=(0.0, 0.5, "paint"), hz_k=(0.0, 0.6, "paint"),
    stencil=(0.0, 0.55, "paint"), rail=(0.7, 0.42, "steel"), red_p=(0.0, 0.45, "paint"),
)
V3.COL.update(COL)
V3.SPEC.update(SPEC)
# kinds of the kit's materials these models use, as this surface pass sees them (the kit calls every light a "dark")
KIND = dict(lamp="glass", red="glass", green="glass", glow="glass", recess="dark", foot="dark", soot="dark", burnt="dark")
# (linear RGB, the brightest channel = strength: the panels at a third of the old bars' strength - by day they are glass
# that glows a little, at night the engine's strength and the dark around them make them the building's light)
V3.V2.EMIT.update(glow_a=(0.055, 0.26, 0.32), glow_b=(0.045, 0.21, 0.26), win_lit=(0.40, 0.23, 0.07))
V3.UV_SCALE.update(shell_core=0.35, duct_dk=0.3, duct=0.6, grate=0.6, rubber=0.6, glow_a=0.8, glow_b=0.8, hz_k=0.6)
# the house paint goes through V3's tree (reimagined_kit): as clean as this model's paint
HOUSE_CLEAN = (0.35, 0.15, 0.55, 0.30, 0.25, 0.18, 0.20, 0.30)   # film, blotch, cavity, soft, grime, streak, chip, low dust
HOUSE_DIRT = (0.12, 0.22, 0.15, 0.14, 0.22, 0.20, 0.14, 0.12)    # added per unit of WEATHERING (blotch also x MOTTLE)


def house_weather():
    """The house paint's weathering (V3's tree): a bit dusty and worn with WEATHERING, never so much that the tint
    stops reading as the house colour."""
    w = [c + d * WEATHERING * (MOTTLE if i == 1 else 1.0) for i, (c, d) in enumerate(zip(HOUSE_CLEAN, HOUSE_DIRT))]
    V3.WEATHER["paint"] = tuple(min(0.8, x) for x in w)


house_weather()
V3.DIRT.update(cavity=(0.170, 0.140, 0.110), soft=(0.560, 0.480, 0.380), blotch=(0.700, 0.620, 0.500),
               grime=(0.500, 0.420, 0.330), streak=(0.520, 0.430, 0.330), chip=(0.340, 0.300, 0.270))

# the surface pass per kind: mottle, dust film, soft cavity, cavity, edge wear, chips, grime, streaks, top dust, foot sand
# (a house may add kinds of its own: setup(wear=, grime_w=))
WEAR = dict(
    paint=(0.040, 0.40, 0.34, 0.60, 0.35, 0.20, 0.28, 0.25, 0.38, 0.50),
    steel=(0.060, 0.30, 0.35, 0.60, 0.50, 0.00, 0.25, 0.12, 0.30, 0.40),
    rubber=(0.050, 0.35, 0.20, 0.40, 0.00, 0.00, 0.00, 0.00, 0.35, 0.40),
    dark=(0.040, 0.00, 0.10, 0.40, 0.00, 0.00, 0.00, 0.00, 0.12, 0.20),
    glass=(0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.05, 0.00),
)
# how much each layer of WEAR grows per unit of WEATHERING (a factor 1 + k * WEATHERING on the clean value)
WEAR_GROW = (0.0, 0.10, 0.60, 0.35, 0.30, 1.20, 1.20, 0.80, 0.25, 0.45)
# the grime layers that exist only with WEATHERING (x WEATHERING; blotch also x MOTTLE), per kind:
#            blotch run-off low    soot   rust   recess sand
GRIME_W = dict(
    paint=(0.80, 0.55, 0.50, 0.80, 0.75, 0.45),
    steel=(0.60, 0.30, 0.40, 0.80, 0.00, 0.35),
    rubber=(0.25, 0.00, 0.30, 0.60, 0.00, 0.30),
    dark=(0.12, 0.00, 0.15, 0.50, 0.00, 0.20),
)
# the grime colours (the Atreides ones: they read on cream paint; a house on other materials sets its own)
STAIN = (0.330, 0.285, 0.225)        # the large patches: dust bound with oil and damp, a grey brown
RUNOFF = (0.400, 0.330, 0.250)       # run-off below joints and overhangs
RUST = (0.500, 0.270, 0.120)         # rust bleeding out of the rib flanges
RUST_DK = (0.300, 0.170, 0.100)      # the drip right under a bolt
LOW = (0.330, 0.280, 0.215)          # grime splash towards the foot
SOOT = (0.060, 0.055, 0.050)
DUST = (0.800, 0.700, 0.540)
SAND = (0.700, 0.600, 0.440)
CAVITY = (0.170, 0.140, 0.110)
GRIME = (0.500, 0.420, 0.330)

_SETTABLE = ("P", "PT", "NODES", "ANCHOR_DOC", "RIBS", "SHELL_MATS", "LIP_MATS", "FLUE_TOP", "SHELL_STREAKS", "POLISH", "REVIEW",
             "PREVIEW_VIEWS", "DETAIL", "PREVIEW_DIST", "MAT_GRIME", "SURFACE_EXTRA", "STAIN", "RUNOFF", "RUST", "RUST_DK", "LOW", "SOOT", "DUST", "SAND", "CAVITY", "GRIME",
             "HOUSE_CLEAN", "HOUSE_DIRT", "REST", "POSES", "POSE_SPEC")


def setup(key, parts, col=None, spec=None, emit=None, uv_scale=None, kind=None, wear=None, grime_w=None, **settings):
    """A house file hands itself over: its model key, its part builders `parts` = [(label, fn(main, moving,
    anchors))] in build order, its palette (col / spec / emit / uv_scale: added to, or replacing, the fittings'),
    its own material kinds (kind: material -> kind; wear / grime_w: the strength rows of a new kind) and any of the
    module's settings by name (P, PT, NODES, ANCHOR_DOC, RIBS, SHELL_MATS, LIP_MATS, FLUE_TOP, SHELL_STREAKS, POLISH,
    REVIEW, PREVIEW_VIEWS, PREVIEW_DIST, DETAIL, MAT_GRIME, SURFACE_EXTRA, the grime colours STAIN / RUNOFF / RUST / RUST_DK / LOW / SOOT / DUST / SAND /
    CAVITY / GRIME, HOUSE_CLEAN / HOUSE_DIRT, REST / POSES / POSE_SPEC)."""
    bad = [k for k in settings if k not in _SETTABLE]
    if bad:
        raise SystemExit("building_common.setup: unknown setting " + ", ".join(bad))
    globals().update(settings, KEY=key, PARTS=list(parts))
    V3.COL.update(col or {})
    V3.SPEC.update(spec or {})
    V3.V2.EMIT.update(emit or {})
    V3.UV_SCALE.update(uv_scale or {})
    KIND.update(kind or {})
    WEAR.update(wear or {})
    GRIME_W.update(grime_w or {})
    house_weather()


class Tree:
    """The node helpers of one material's tree: surface() and the houses' streak hooks (SHELL_STREAKS) build with
    them. geo = the tree's geometry node."""

    def __init__(self, nt):
        self.nodes, self.links = nt.nodes, nt.links
        self.geo = self.nodes.new("ShaderNodeNewGeometry")

    def value(self, x):
        v = self.nodes.new("ShaderNodeValue")
        v.outputs[0].default_value = x
        return v.outputs[0]

    def rgb(self, c):
        n = self.nodes.new("ShaderNodeRGB")
        n.outputs[0].default_value = (*V2.srgb_to_linear(c), 1)
        return n.outputs[0]

    def noise(self, scale, detail=3.0, mapping=None, offset=0.0):
        nodes, links = self.nodes, self.links
        n = nodes.new("ShaderNodeTexNoise")
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = 0.55
        mp = nodes.new("ShaderNodeMapping")
        mp.inputs["Scale"].default_value = mapping or (1.0, 1.0, 1.0)
        mp.inputs["Location"].default_value = (offset, offset * 0.7, offset * 1.3)
        links.new(self.geo.outputs["Position"], mp.inputs["Vector"])
        links.new(mp.outputs[0], n.inputs["Vector"])
        return n.outputs["Fac"]

    def remap(self, sock, a_, b_, c_, d_):
        m = self.nodes.new("ShaderNodeMapRange")
        m.inputs["From Min"].default_value = a_
        m.inputs["From Max"].default_value = b_
        m.inputs["To Min"].default_value = c_
        m.inputs["To Max"].default_value = d_
        self.links.new(sock, m.inputs["Value"])
        return m.outputs[0]

    def mix(self, fac, a_, b_, blend="MIX"):
        m = self.nodes.new("ShaderNodeMix")
        m.data_type = "RGBA"
        m.blend_type = blend
        if isinstance(fac, float):
            m.inputs["Factor"].default_value = fac
        else:
            self.links.new(fac, m.inputs["Factor"])
        self.links.new(a_, m.inputs[6])
        self.links.new(b_, m.inputs[7])
        return m.outputs[2]

    def mixf(self, fac, a_, b_):
        m = self.nodes.new("ShaderNodeMix")
        m.data_type = "FLOAT"
        self.links.new(fac, m.inputs["Factor"])
        for sock, x in ((m.inputs[2], a_), (m.inputs[3], b_)):
            if isinstance(x, float):
                sock.default_value = x
            else:
                self.links.new(x, sock)
        return m.outputs[0]

    def op(self, kind_, a_, b_=None):
        m = self.nodes.new("ShaderNodeMath")
        m.operation = kind_
        m.use_clamp = kind_ in ("MULTIPLY", "ADD", "SUBTRACT")
        for sock, x in ((m.inputs[0], a_), (m.inputs[1], b_)):
            if x is None:
                continue
            if isinstance(x, float):
                sock.default_value = x
            else:
                self.links.new(x, sock)
        return m.outputs[0]

    def mul(self, a_, b_):
        return self.op("MULTIPLY", a_, b_)

    def opf(self, kind_, a_, b_=None):           # plain arithmetic (not clamped to 0..1): coordinates in px
        m = self.nodes.new("ShaderNodeMath")
        m.operation = kind_
        for sock, x in ((m.inputs[0], a_), (m.inputs[1], b_)):
            if x is None:
                continue
            if isinstance(x, (int, float)):
                sock.default_value = float(x)
            else:
                self.links.new(x, sock)
        return m.outputs[0]


def scoop_streaks(T, name, pos, cur, rough_s, g_rust, g_run):
    """SHELL_STREAKS of the Atreides / Harkonnen scoops, in shell coordinates (Scoop: t along the axis, ang across
    the arch): rust out of the rib flanges (RIBS) with a darker drip under each bolt on the flank the camera sees,
    and run-off down the lip from the louvre sill. pos = the separated position (tiles)."""
    nodes, links, geo = T.nodes, T.links, T.geo
    rgb, noise, remap, mix, mixf, mul, opf = T.rgb, T.noise, T.remap, T.mix, T.mixf, T.mul, T.opf
    fa = P["face_ang"]
    (sx, sy), (bx, by) = unit(fa), unit(fa + 90.0)
    ms, mn = P["mouth_s"], P["mouth_n"]
    s0 = [m[0] * sx + m[1] * sy for m in (ms, mn)]
    u0 = [m[0] * bx + m[1] * by for m in (ms, mn)]
    px_x, px_y = opf("DIVIDE", pos.outputs["X"], PX), opf("DIVIDE", pos.outputs["Y"], PX)
    s_w = opf("ADD", opf("MULTIPLY", px_x, sx), opf("MULTIPLY", px_y, sy))
    u_w = opf("ADD", opf("MULTIPLY", px_x, bx), opf("MULTIPLY", px_y, by))
    north = opf("GREATER_THAN", s_w, (s0[0] + s0[1]) / 2)        # which scoop (they stand side by side in s)
    s_rel = opf("SUBTRACT", s_w, mixf(north, s0[0], s0[1]))
    u_rel = opf("SUBTRACT", u_w, mixf(north, u0[0], u0[1]))       # px behind the mouth's plane
    t = opf("DIVIDE", u_rel, P["deep"])
    if g_rust and name not in LIP_MATS:
        rad = opf("MULTIPLY", opf("POWER", opf("MAXIMUM", opf("SUBTRACT", 1.0, opf("MULTIPLY", t, t)), 1e-4), 0.31), P["half"])
        ca = opf("MINIMUM", opf("MAXIMUM", opf("DIVIDE", s_rel, rad), -1.0), 1.0)
        ang = opf("DEGREES", opf("ARCCOSINE", ca))                # 90 = the crest, 180 = the south-west foot
        dmin = None                                               # px to the nearest rib flange
        for tr in RIBS:
            d_ = opf("MULTIPLY", opf("ABSOLUTE", opf("SUBTRACT", t, tr)), P["deep"])
            dmin = d_ if dmin is None else opf("MINIMUM", dmin, d_)
        inside = mul(remap(t, 0.0, 0.03, 0.0, 1.0), remap(t, 0.97, 1.0, 1.0, 0.0))
        f = opf("FRACT", opf("DIVIDE", opf("SUBTRACT", ang, 79.0), 22.3))      # 0 at a bolt, growing downhill
        drip = remap(f, 0.03, 0.62, 1.0, 0.0)
        flank = remap(ang, 96.0, 106.0, 0.0, 1.0)
        sk3 = remap(noise(34.0, 2.0, mapping=(1.0, 1.0, 0.05), offset=27.0), 0.36, 0.66, 0.0, 1.0)
        under_bolt = mul(mul(mul(remap(dmin, 0.30, 0.75, 1.0, 0.0), drip), flank), inside)
        bleed = mul(mul(mul(remap(dmin, 0.40, 2.6, 1.0, 0.0), remap(drip, 0.0, 1.0, 0.45, 1.0)),
                        mul(remap(ang, 55.0, 110.0, 0.30, 1.0), remap(sk3, 0.0, 1.0, 0.40, 1.0))), inside)
        cur = mix(mul(bleed, min(0.9, g_rust * 0.95)), cur, rgb(RUST))
        cur = mix(mul(under_bolt, min(0.9, g_rust * 1.1)), cur, rgb(RUST_DK))
        rough_s = mixf(mul(bleed, g_rust * 0.6), rough_s, 0.85)
    if g_run:                                                     # down the lip from the louvre sill
        n = nodes.new("ShaderNodeTexNoise")
        n.inputs["Scale"].default_value = 30.0
        n.inputs["Detail"].default_value = 2.0
        mp = nodes.new("ShaderNodeMapping")
        mp.inputs["Rotation"].default_value = (0.0, 0.0, -math.radians(fa + 90.0))   # the scoop's axis -> x
        mp.inputs["Scale"].default_value = (0.05, 1.0, 1.0)
        links.new(geo.outputs["Position"], mp.inputs["Vector"])
        links.new(mp.outputs[0], n.inputs["Vector"])
        lip = mul(remap(u_rel, -0.2, -10.5, 1.0, 0.12), remap(u_rel, -P["lip"] - 0.6, -P["lip"] + 0.4, 0.0, 1.0))
        lip = mul(mul(lip, remap(u_rel, 0.3, -0.2, 0.0, 1.0)), remap(n.outputs["Fac"], 0.40, 0.68, 0.0, 1.0))
        cur = mix(mul(lip, min(0.9, g_run * 1.15)), cur, rgb(RUNOFF))
    return cur, rough_s


SHELL_STREAKS = scoop_streaks


def surface(nt, name):
    """(colour, roughness, metallic) sockets: painted metal / steel / rubber / glass as they are, under the
    weathering. The clean base (WEAR): a faint tone drift, a patchy dust film, shade in the creases (ambient
    occlusion at two radii), worn upper edges (paint lighter and smoother, a few chips to the primer; steel polished),
    a little grime at the bottom of each part and run-off on walls, fine dust on up-facing surfaces, sand at the foot.
    On top, with WEATHERING (GRIME_W; nothing of it at 0): large stain patches (x MOTTLE), run-off below every joint
    and under overhangs, the shells' own streaks (SHELL_STREAKS: rust out of the rib flanges and drips under their
    bolts, streaks down the lip from the louvre sill), sand in the recesses, a dark grime splash towards the foot,
    soot round the flue. Glass stays clean."""
    base = V3.COL[name]
    metal, rough, kind = V3.SPEC[name]
    kind = KIND.get(name, kind)
    W = max(0.0, WEATHERING)
    Wm = W * MAT_GRIME.get(name, 1.0)                # this material's share of the weathering
    w_mot, w_film, w_soft, w_cav, w_wear, w_chip, w_grime, w_streak, w_dust, w_sand = (
        min(0.95, w * (1.0 + g * Wm)) if i else w for i, (w, g) in enumerate(zip(WEAR[kind], WEAR_GROW)))
    g_blot, g_run, g_low, g_soot, g_rust, g_rsand = (min(0.9, g * Wm) for g in GRIME_W.get(kind, (0.0,) * 6))
    g_blot = min(0.85, g_blot * max(0.0, MOTTLE))
    T = Tree(nt)
    nodes, links, geo = T.nodes, T.links, T.geo
    value, rgb, noise, remap, mix, mixf, op, mul = T.value, T.rgb, T.noise, T.remap, T.mix, T.mixf, T.op, T.mul
    cur = rgb(base)
    rough_s, metal_s = value(rough), value(metal)
    if kind == "glass" and not w_dust:
        return cur, rough_s, metal_s
    nrm = nodes.new("ShaderNodeSeparateXYZ")
    links.new(geo.outputs["Normal"], nrm.inputs[0])
    pos = nodes.new("ShaderNodeSeparateXYZ")
    links.new(geo.outputs["Position"], pos.inputs[0])
    top = remap(nrm.outputs["Z"], 0.55, 0.95, 0.0, 1.0)
    if kind == "glass":            # lenses and lamps stay clean but for a breath of dust on top
        d = mul(mul(top, remap(noise(9.0, offset=4.0), 0.3, 0.7, 0.3, 1.0)), w_dust)
        return mix(d, cur, rgb(DUST)), mixf(d, rough_s, 0.8), metal_s
    upright = remap(nrm.outputs["Z"], 0.65, 0.15, 0.0, 1.0)
    part_h = nodes.new("ShaderNodeAttribute")
    part_h.attribute_name = "grime_h"
    ao_w = nodes.new("ShaderNodeAmbientOcclusion")
    ao_w.inputs["Distance"].default_value = 0.065
    ao_w.samples = 12
    near = remap(ao_w.outputs["AO"], 0.85, 0.35, 0.0, 1.0)
    ao_t = nodes.new("ShaderNodeAmbientOcclusion")
    ao_t.inputs["Distance"].default_value = 0.028
    ao_t.samples = 12
    tight = remap(ao_t.outputs["AO"], 0.80, 0.30, 0.0, 1.0)
    bev = nodes.new("ShaderNodeBevel")
    bev.samples = 8
    bev.inputs["Radius"].default_value = 0.018
    dot = nodes.new("ShaderNodeVectorMath")
    dot.operation = "DOT_PRODUCT"
    links.new(bev.outputs["Normal"], dot.inputs[0])
    links.new(geo.outputs["Normal"], dot.inputs[1])
    edge = remap(dot.outputs["Value"], 0.995, 0.88, 0.0, 1.0)
    bz = nodes.new("ShaderNodeSeparateXYZ")
    links.new(bev.outputs["Normal"], bz.inputs[0])
    edge_up = mul(edge, remap(bz.outputs["Z"], 0.1, 0.6, 0.0, 1.0))
    # 1. tone: a faint mottle and a broad drift (paint is not perfectly even; bare metal a little more)
    cur = mix(1.0, cur, remap(noise(34.0), 0.36, 0.64, 1 - w_mot, 1 + w_mot * 0.6), "MULTIPLY")
    cur = mix(1.0, cur, remap(noise(5.0, 2.0, offset=3.1), 0.3, 0.7, 1 - w_mot * 1.4, 1 + w_mot * 0.7), "MULTIPLY")
    rough_s = op("ADD", rough_s, remap(noise(11.0, 2.0, offset=8.0), 0.3, 0.7, -0.05, 0.06))
    # 2. the dust film: patchy, thicker out of the recesses and towards the bottom of the part; upper edges stay clean
    if w_film:
        film_c = tuple(0.72 * b + 0.28 * d for b, d in zip(base, DUST))
        patch = mul(remap(noise(4.5, 4.0, offset=11.0), 0.38, 0.64, 0.15, 1.0), remap(noise(19.0, offset=5.0), 0.3, 0.7, 0.5, 1.0))
        film = op("MAXIMUM", patch, mul(near, 0.9))
        film = op("MAXIMUM", film, mul(remap(part_h.outputs["Fac"], 0.5, 0.0, 0.0, 1.0), 0.6))
        film = mul(mul(film, remap(edge_up, 0.0, 1.0, 1.0, 0.1)), w_film)
        cur = mix(film, cur, rgb(film_c))
        rough_s = mixf(film, rough_s, 0.85)
    # 3. shade in the creases: a soft brown out of the recesses, a dark core where parts meet
    soft_c = tuple(0.45 * b + 0.55 * g for b, g in zip(base, (0.42, 0.35, 0.27)))
    cur = mix(mul(mul(near, remap(noise(24.0, offset=7.0), 0.25, 0.75, 0.55, 1.0)), w_soft), cur, rgb(soft_c))
    cur = mix(mul(tight, w_cav), cur, rgb(CAVITY))
    # 4. worn upper edges
    if w_wear:
        wn = mul(mul(edge_up, remap(noise(15.0, 3.0, offset=9.0), 0.3, 0.7, 0.35, 1.0)), w_wear)
        if kind in POLISH:
            cur = mix(wn, cur, rgb(tuple(min(1.0, c * 1.45 + 0.05) for c in base)))
            rough_s = mixf(wn, rough_s, max(0.16, rough - 0.16))
        else:
            cur = mix(wn, cur, rgb(tuple(min(1.0, c * 1.06 + 0.035) for c in base)))
            rough_s = mixf(wn, rough_s, rough - 0.10)
    if w_chip:
        ch = mul(mul(edge, remap(noise(52.0, 4.0, offset=2.0), 0.56 - 0.05 * min(W, 2.0), 0.64 - 0.05 * min(W, 2.0), 0.0, 1.0)), w_chip)
        lum = 0.2126 * base[0] + 0.7152 * base[1] + 0.0722 * base[2]
        cur = mix(ch, cur, rgb(V3.DIRT["chip"] if lum > 0.42 else V3.DIRT["chip_light"]))
        rough_s = mixf(ch, rough_s, 0.42)
        metal_s = mixf(ch, metal_s, 0.7)
    # 5. a little grime towards the bottom of each part, run-off on upright faces
    if w_grime:
        gr = mul(mul(remap(part_h.outputs["Fac"], 0.22, 0.0, 0.0, 1.0), remap(noise(17.0, offset=13.0), 0.25, 0.7, 0.35, 1.0)), w_grime)
        cur = mix(gr, cur, rgb(GRIME))
    if w_streak:
        sk = remap(noise(30.0, 2.0, mapping=(1.0, 1.0, 0.05), offset=2.0), 0.55, 0.76, 0.0, 1.0)
        cur = mix(mul(mul(sk, upright), w_streak), cur, rgb(V3.DIRT["streak"]))
    # 5b. WEATHERING: run-off. Below the top edge of every upright part (the joint above it: water collects there and
    # runs out) and under whatever stands out above (vents, caps, cornices, the rim: occlusion looking upwards)
    if g_run:
        sk2 = remap(noise(26.0, 3.0, mapping=(1.0, 1.0, 0.06), offset=15.0), 0.40, 0.70, 0.0, 1.0)
        below = remap(part_h.outputs["Fac"], 1.0, 0.40, 1.0, 0.0)
        up_n = nodes.new("ShaderNodeVectorMath")
        up_n.operation = "ADD"
        sc_n = nodes.new("ShaderNodeVectorMath")
        sc_n.operation = "SCALE"
        links.new(geo.outputs["Normal"], sc_n.inputs[0])
        sc_n.inputs["Scale"].default_value = 0.8
        links.new(sc_n.outputs[0], up_n.inputs[0])
        up_n.inputs[1].default_value = (0.0, 0.0, 1.0)
        ao_u = nodes.new("ShaderNodeAmbientOcclusion")
        ao_u.inputs["Distance"].default_value = 0.10
        ao_u.samples = 16
        links.new(up_n.outputs[0], ao_u.inputs["Normal"])
        over = remap(ao_u.outputs["AO"], 0.62, 0.30, 0.0, 1.0)
        run = mul(op("MAXIMUM", mul(below, mul(below, sk2)), mul(over, remap(sk2, 0.0, 1.0, 0.45, 1.0))), upright)
        cur = mix(mul(run, g_run), cur, rgb(RUNOFF))
        rough_s = mixf(mul(run, g_run), rough_s, 0.8)
    # 5c. WEATHERING: the shells' own streaks, in the house's shell coordinates (scoop_streaks, or the house's own)
    if (g_rust or g_run) and name in SHELL_MATS and SHELL_STREAKS:
        cur, rough_s = SHELL_STREAKS(T, name, pos, cur, rough_s, g_rust, g_run)
    # 5d. WEATHERING: dirtier towards the foot - a dark splash of grime up to 11 px (the sand lies over it below)
    if g_low:
        lo = remap(pos.outputs["Z"], 11.0 * PX, 1.0 * PX, 0.0, 1.0)
        lo = mul(mul(lo, remap(lo, 0.0, 1.0, 0.45, 1.0)), remap(noise(8.5, 4.0, offset=31.0), 0.28, 0.72, 0.30, 1.0))
        cur = mix(mul(lo, g_low), cur, rgb(tuple(0.25 * b + 0.75 * c for b, c in zip(base, LOW))))
        rough_s = mixf(mul(lo, g_low), rough_s, 0.85)
    # 5e. WEATHERING: a little soot round the open top of the flue, heavier just below the lip
    if g_soot and FLUE_TOP:
        mp = nodes.new("ShaderNodeMapping")
        mp.inputs["Scale"].default_value = (1.0, 1.0, 0.6)             # reaches further down the pipe than sideways
        links.new(geo.outputs["Position"], mp.inputs["Vector"])
        near_top = None
        for ft in (FLUE_TOP if isinstance(FLUE_TOP[0], (tuple, list)) else (FLUE_TOP,)):   # (one top: the same tree as ever)
            dist = nodes.new("ShaderNodeVectorMath")
            dist.operation = "DISTANCE"
            links.new(mp.outputs[0], dist.inputs[0])
            dist.inputs[1].default_value = (ft[0] * PX, ft[1] * PX, ft[2] * PX * 0.6)
            n_ = remap(dist.outputs["Value"], 6.5 * PX, 1.2 * PX, 0.0, 1.0)
            near_top = n_ if near_top is None else op("MAXIMUM", near_top, n_)
        so = mul(near_top, remap(noise(14.0, 3.0, offset=37.0), 0.25, 0.75, 0.5, 1.0))
        cur = mix(mul(so, g_soot), cur, rgb(SOOT))
        rough_s = mixf(mul(so, g_soot), rough_s, 0.92)
        metal_s = mixf(mul(so, g_soot), metal_s, 0.0)
    # 6. fine dust on what faces up, sand against what stands on the ground (up to 4 px)
    d = mul(mul(top, remap(noise(7.0, 3.0, offset=4.0), 0.28, 0.7, 0.3, 1.0)), w_dust)
    cur = mix(d, cur, rgb(DUST))
    rough_s = mixf(d, rough_s, 0.9)
    metal_s = mixf(d, metal_s, 0.0)
    # 7. WEATHERING: the large stains - darker, dustier patches at two sizes with ragged edges, a little rougher.
    # Last but the blown-in sand: they show through the dust film and the dust on top (under them they vanish)
    if g_blot:
        stain_c = tuple(0.30 * b + 0.70 * c for b, c in zip(base, STAIN))
        area = remap(noise(3.0, 4.0, offset=21.0), 0.38, 0.62, 0.0, 1.0)           # where it is dirtier: 8-12 px
        blob = remap(noise(9.5, 5.0, offset=17.0), 0.47, 0.58, 0.0, 1.0)           # the stains themselves: 2-4 px
        speck = remap(noise(24.0, 3.0, offset=23.0), 0.56, 0.66, 0.0, 0.6)         # spatter between them
        bl = mul(op("MAXIMUM", op("MAXIMUM", blob, speck), mul(area, 0.38)), remap(area, 0.0, 1.0, 0.30, 1.0))
        bl = mul(mul(bl, remap(top, 0.0, 1.0, 1.0, 0.75)), g_blot)      # (a little less on what faces up: the lip, the crest)
        cur = mix(bl, cur, rgb(stain_c))
        rough_s = mixf(bl, rough_s, 0.82)
    if SURFACE_EXTRA and W:        # WEATHERING: the building's own stains (over the shared ones, under the blown sand)
        cur, rough_s, metal_s = SURFACE_EXTRA(T, name, kind, dict(pos=pos, nrm=nrm, top=top, upright=upright, near=near,
                                                                  part_h=part_h.outputs["Fac"]), cur, rough_s, metal_s, W)
    if g_rsand:                    # WEATHERING: sand blown into the recesses and corners of what faces up or leans
        rs = mul(mul(near, remap(nrm.outputs["Z"], 0.15, 0.6, 0.0, 1.0)), remap(noise(13.0, 3.0, offset=41.0), 0.25, 0.7, 0.35, 1.0))
        cur = mix(mul(rs, g_rsand), cur, rgb(SAND))
        rough_s = mixf(mul(rs, g_rsand), rough_s, 0.92)
        metal_s = mixf(mul(rs, g_rsand), metal_s, 0.0)
    low = mul(mul(remap(pos.outputs["Z"], 4.0 * PX, 0.4 * PX, 0.0, 1.0), remap(noise(10.0, offset=6.0), 0.3, 0.7, 0.4, 1.0)), w_sand)
    cur = mix(low, cur, rgb(SAND))
    rough_s = mixf(low, rough_s, 0.92)
    metal_s = mixf(low, metal_s, 0.0)
    return cur, rough_s, metal_s


# ------------------------------------------------------------------------------------------------
# The Atreides / Harkonnen scoops' geometry (px); the helpers below it take any shell with S / N / D


def unit(ang):
    return math.cos(math.radians(ang)), math.sin(math.radians(ang))


class Scoop:
    """One scoop: mouth midpoint m (x, y). Shell coordinates (t along the axis from the mouth 0 to the back tip 1,
    ang across the pointed-arch section: 0 = the north-east foot, 90 = the crest, 180 = the south-west foot, the
    flank the game camera sees); mouth coordinates (s along the mouth's base line towards north-east, u into the
    scoop, z up)."""

    def __init__(self, tag, m):
        self.tag, self.m = tag, m
        self.bx, self.by = unit(P["face_ang"] + 90.0)
        self.sx, self.sy = unit(P["face_ang"])
        self.R, self.sq, self.D = P["half"], P["tall"], P["deep"]

    def rad(self, t):
        return self.R * max(0.0, 1.0 - t * t) ** 0.31

    def at(self, t, ang, r):
        ca = math.cos(math.radians(ang))
        return Vector((self.m[0] + self.bx * self.D * t + self.sx * r * ca, self.m[1] + self.by * self.D * t + self.sy * r * ca,
                       r * (1.0 - abs(ca) ** PT) * self.sq))

    def S(self, t, ang, dr=0.0):
        return self.at(t, ang, self.rad(t) + dr)

    def N(self, t, ang):
        e = 1e-3
        dt = self.S(min(t + e, 0.999), ang) - self.S(max(t - e, 0.0), ang)
        da = self.S(t, ang + 0.2) - self.S(t, ang - 0.2)
        n = dt.cross(da)
        if n.length < 1e-9:
            return Vector((0, 0, 1))
        n.normalize()
        return n if n.dot(self.S(t, ang, 1.0) - self.S(t, ang)) > 0 else -n

    def L(self, s, u, z):
        return (self.m[0] + self.sx * s + self.bx * u, self.m[1] + self.sy * s + self.by * u, z)

    def half_at(self, r, z):
        """Half width of the arch of radius r at height z."""
        return r * max(0.0, 1.0 - z / (r * self.sq)) ** (1.0 / PT)


def plate(name, sc, t0, t1, a0, a1, lift, thick, material, na=2, nt=None, gap=0.0):
    """A plate on the shell between t0..t1 and the angles a0..a1, standing `lift` px proud, `thick` px thick, drawn
    in by gap / 2 px on every side (real joints). Open underneath (the face on the shell is never seen)."""
    if gap:
        tm, am = (t0 + t1) / 2, (a0 + a1) / 2
        per_deg = max(0.05, (sc.S(tm, am + 0.5) - sc.S(tm, am - 0.5)).length)
        t0, t1 = t0 + gap / 2 / sc.D, t1 - gap / 2 / sc.D
        a0, a1 = a0 + gap / 2 / per_deg, a1 - gap / 2 / per_deg
    nt = nt or max(1, math.ceil((t1 - t0) / 0.06))
    bm = bmesh.new()
    grid = [[(bm.verts.new(tuple(sc.S(t0 + (t1 - t0) * i / nt, a0 + (a1 - a0) * k / na, lift))),
              bm.verts.new(tuple(sc.S(t0 + (t1 - t0) * i / nt, a0 + (a1 - a0) * k / na, lift - thick))))
             for k in range(na + 1)] for i in range(nt + 1)]
    for i in range(nt):
        for k in range(na):
            bm.faces.new([grid[i][k][0], grid[i][k + 1][0], grid[i + 1][k + 1][0], grid[i + 1][k][0]])
        for k in (0, na):
            bm.faces.new([grid[i][k][0], grid[i + 1][k][0], grid[i + 1][k][1], grid[i][k][1]])
    for i in (0, nt):
        for k in range(na):
            bm.faces.new([grid[i][k][0], grid[i][k + 1][0], grid[i][k + 1][1], grid[i][k][1]])
    o = new_obj(name, bm, material)
    out = sc.S((t0 + t1) / 2, (a0 + a1) / 2, lift + 1.0) - sc.S((t0 + t1) / 2, (a0 + a1) / 2, lift)
    if o.data.polygons[0].normal.dot(out) < 0:       # (an open mesh: the normals' side is not decided by its volume)
        o.data.flip_normals()
    return o


def arch_band(name, sc, u0, u1, r_out, r_in, material, segs=28, a0=0.0, a1=180.0):
    """An arch-shaped band in the mouth's plane: between the arches of radius r_in and r_out, from depth u0 to u1."""
    rings = []
    for i in range(segs + 1):
        ang = a0 + (a1 - a0) * i / segs
        ring = []
        for (r, u) in ((r_out, u0), (r_out, u1), (r_in, u1), (r_in, u0)):
            p = sc.at(0.0, ang, r)
            ring.append((p.x + sc.bx * u, p.y + sc.by * u, p.z))
        rings.append(ring)
    bm = bmesh.new()
    rings_solid(bm, rings)
    return new_obj(name, bm, material)


def quad_bar(name, sc, s0, s1, section, material):
    """A bar across the mouth from s0 to s1 with the cross-section `section` = [(u, z)] (mouth coordinates)."""
    bm = bmesh.new()
    rings_solid(bm, [[sc.L(s, u, z) for (u, z) in section] for s in (s0, s1)])
    return new_obj(name, bm, material)


def bolt(name, p, n, size=0.5, h=0.32, material="door_fr"):
    p = Vector(p)
    return beam(name, tuple(p - Vector(n) * 0.05), tuple(p + Vector(n) * h), size, size, material)


def rotor(name, n_blades, r0, r1, chord0, chord1, thick, pitch_deg, material="blade", hub=None, tips=None, tip_mat="hz_y",
          hub_mat="slat"):
    """Blades of a turbine / fan about the local +Z axis through the origin (px). hub = (radius, z0, z1, nose z);
    tips = the length of the marked blade tips (tip_mat)."""
    out = []
    pr = math.radians(pitch_deg)
    for k in range(n_blades):
        th = 2 * math.pi * k / n_blades
        er = Vector((math.cos(th), math.sin(th), 0.0))
        et = Vector((-math.sin(th), math.cos(th), 0.0))
        ez = Vector((0, 0, 1))
        c = et * math.cos(pr) + ez * math.sin(pr)
        nn = -et * math.sin(pr) + ez * math.cos(pr)

        def ring(r, ch):
            return [tuple(er * r + c * (sc_ * ch / 2) + nn * (sn * thick / 2)) for sc_, sn in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        bm = bmesh.new()
        rings_solid(bm, [ring(r0, chord0), ring(r1, chord1)])
        out.append(new_obj(f"{name}_blade{k}", bm, material))
        if tips:
            bm = bmesh.new()
            rings_solid(bm, [ring(r1 - tips, chord1 * 1.02), ring(r1 + 0.02, chord1 * 1.02)])
            o = new_obj(f"{name}_tip{k}", bm, tip_mat)
            o.data.transform(Matrix.Translation(nn * 0.02))
            out.append(o)
    if hub:
        hr, z0, z1, zn = hub
        out.append(K.lathe(f"{name}_hub", (0.0, 0.0), [(hr, z0), (hr, z1), (hr * 0.55, (z1 + zn) / 2), (0.05, zn)], hub_mat, 12))
    return out


def moving_node(parts, name, origin_px, axis=(0, 0, 1)):
    """Join `parts` (built about the local origin with the spin axis = +Z) into a node placed at origin_px with its
    local +Z along `axis`; axis None = a still node, not turned (a second static node beside `main`)."""
    node = V3.finish_node(parts, name, (0, 0, 0))
    if axis is not None:
        q = Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized())
        node.rotation_mode = "QUATERNION"
        node.rotation_quaternion = q
    node.location = Vector(origin_px) * PX
    return node


DIGITS = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgedc", "7": "abc",
          "8": "abcdefg", "9": "abcdfg"}
SEGMENTS = dict(a=(0, 1, 0.86, 1), d=(0, 1, 0, 0.14), g=(0, 1, 0.43, 0.57), f=(0, 0.26, 0.5, 1), e=(0, 0.26, 0, 0.5),
                b=(0.74, 1, 0.5, 1), c=(0.74, 1, 0, 0.5))


def stencil(parts, name, sc, text, t_left, ang_low, w_px, h_deg, lift, material="stencil"):
    """Stencilled digits on a shell (the scoops' south-west flank): read from outside, the text runs towards the
    mouth (t falling) and up (ang falling)."""
    dt = w_px / sc.D
    for i, ch in enumerate(text):
        tl = t_left - i * dt * 1.45
        for seg in DIGITS[ch]:
            x0, x1, y0, y1 = SEGMENTS[seg]
            parts.append(plate(f"{name}_{i}{seg}", sc, tl - x1 * dt, tl - x0 * dt, ang_low - y1 * h_deg, ang_low - y0 * h_deg,
                               lift, 0.14, material, na=1, nt=1))


def door(parts, name, c, out_ang, z0=0.0, w=2.6, h=4.9, lamp=True, window=True):
    """A service door on a vertical wall: c (x, y) on the wall face, out_ang the wall's outward direction (deg).
    Frame, leaf, a lit window slit, handle, a step with a hazard edge, a lamp under a hood. Returns the lamp's
    point (x, y, z)."""
    ox, oy = unit(out_ang)
    wa = out_ang + 90.0

    def at(d, a=0.0):
        ax, ay = unit(wa)
        return (c[0] + ox * d + ax * a, c[1] + oy * d + ay * a)
    parts.append(obox(f"{name}_leaf", at(0.12), wa, w / 2, 0.14, z0 + 0.45, z0 + h, "door"))
    for i, a in enumerate((-w / 2 - 0.22, w / 2 + 0.22)):
        parts.append(obox(f"{name}_jamb{i}", at(0.2, a), wa, 0.24, 0.26, z0, z0 + h + 0.25, "door_fr"))
    parts.append(obox(f"{name}_lintel", at(0.2), wa, w / 2 + 0.46, 0.3, z0 + h, z0 + h + 0.5, "door_fr"))
    parts.append(obox(f"{name}_panel", at(0.3), wa, w / 2 - 0.45, 0.06, z0 + 0.9, z0 + h * 0.56, "door"))
    if window:
        parts.append(obox(f"{name}_win", at(0.3, -0.1), wa, w / 2 - 0.6, 0.07, z0 + h * 0.68, z0 + h * 0.88, "win_lit"))
    parts.append(obox(f"{name}_handle", at(0.42, w / 2 - 0.5), wa, 0.12, 0.1, z0 + h * 0.42, z0 + h * 0.56, "door_fr"))
    parts.append(obox(f"{name}_step", at(0.65), wa, w / 2 + 0.6, 0.65, 0.0, z0 + 0.45, "concrete"))
    for i in range(4):                                     # hazard edge on the step: yellow / black
        a = (i - 1.5) * (w + 1.2) / 4
        parts.append(obox(f"{name}_hz{i}", at(1.2, a), wa, (w + 1.2) / 8, 0.14, z0 + 0.1, z0 + 0.5, "hz_y" if i % 2 else "hz_k"))
    lp = (*at(0.75), z0 + h + 1.0)
    if lamp:
        parts.append(obox(f"{name}_hood", at(0.55), wa, 0.75, 0.6, z0 + h + 1.25, z0 + h + 1.5, "door_fr"))
        parts.append(K.ball(f"{name}_lamp", lp, 0.42, "lamp", 8))
    return lp


def railing(parts, name, pts, h=2.1, material="rail", step=2.6):
    """Posts and two rails along the polyline pts [(x, y, z)]."""
    k = 0
    for (p, q) in zip(pts[:-1], pts[1:]):
        p, q = Vector(p), Vector(q)
        n = max(1, round((q - p).length / step))
        for i in range(n + 1):
            if i == 0 and k:
                continue
            b = p + (q - p) * i / n
            parts.append(beam(f"{name}_post{k}_{i}", tuple(b), (b.x, b.y, b.z + h), 0.24, 0.24, material))
        up = Vector((0, 0, 1))
        parts.append(beam(f"{name}_top{k}", tuple(p + up * h), tuple(q + up * h), 0.3, 0.24, material))
        parts.append(beam(f"{name}_mid{k}", tuple(p + up * h * 0.52), tuple(q + up * h * 0.52), 0.18, 0.18, material))
        k += 1


def ladder(parts, name, pts_a, pts_b, material="rail", rung=1.15):
    """Two stiles along the polylines pts_a / pts_b (3D), rungs between them every `rung` px."""
    acc = 0.0
    r = 0
    for i, ((a0, a1), (b0, b1)) in enumerate(zip(zip(pts_a[:-1], pts_a[1:]), zip(pts_b[:-1], pts_b[1:]))):
        a0, a1, b0, b1 = Vector(a0), Vector(a1), Vector(b0), Vector(b1)
        parts.append(beam(f"{name}_sa{i}", tuple(a0), tuple(a1), 0.24, 0.24, material))
        parts.append(beam(f"{name}_sb{i}", tuple(b0), tuple(b1), 0.24, 0.24, material))
        ln = (a1 - a0).length
        while acc < ln:
            f = acc / ln
            parts.append(beam(f"{name}_r{r}", tuple(a0 + (a1 - a0) * f), tuple(b0 + (b1 - b0) * f), 0.2, 0.2, material))
            r += 1
            acc += rung
        acc -= ln


def handwheel(parts, name, c, z, r, material="red_p"):
    parts.append(K.lathe_loop(f"{name}_ring", c, [(r, z - 0.16), (r + 0.3, z - 0.16), (r + 0.3, z + 0.16), (r, z + 0.16)], material, 12))
    for i, a in enumerate((0.0, 90.0)):
        parts.append(beam(f"{name}_sp{i}", polar(c, r, a, z), polar(c, r, a + 180.0, z), 0.22, 0.2, material))
    parts.append(tube(f"{name}_stem", (c[0], c[1], z - 0.9), (c[0], c[1], z + 0.2), 0.25, "door_fr", 6))


def flanged_pipe(parts, name, pts, r, material, flange="copper_dk", segs=8):
    """K.pipe plus a flange pair in the middle of every run longer than 4 px."""
    parts.extend(K.pipe(name, pts, r, material, segs))
    for i, (p, q) in enumerate(zip(pts[:-1], pts[1:])):
        p, q = Vector(p), Vector(q)
        ln = (q - p).length
        if ln < 4.0:
            continue
        d = (q - p) / ln
        m = (p + q) / 2
        parts.append(tube(f"{name}_fl{i}", tuple(m - d * 0.35), tuple(m + d * 0.35), r * 1.5, flange, 8))


def warning_plate(parts, name, c, out_ang, z, size=1.5):
    """A small yellow warning triangle with a dark mark on a vertical wall."""
    ox, oy = unit(out_ang)
    ax, ay = unit(out_ang + 90.0)
    bm = bmesh.new()

    def tri(k, d):
        return [(c[0] + ox * d + ax * a * k, c[1] + oy * d + ay * a * k, z + zz * k) for (a, zz) in ((-size / 2, 0.0), (size / 2, 0.0), (0.0, size * 0.87))]
    rings_solid(bm, [tri(1.0, 0.0), tri(1.0, 0.1)])
    parts.append(new_obj(f"{name}_tri", bm, "hz_y"))
    parts.append(obox(f"{name}_mark", (c[0] + ox * 0.1, c[1] + oy * 0.1), out_ang + 90.0, 0.13, 0.05, z + size * 0.16, z + size * 0.55, "hz_k"))


# ------------------------------------------------------------------------------------------------
# Build (the house's PARTS), bake, export


def build(tris=False):
    root = bpy.data.objects.new(KEY, None)
    bpy.context.scene.collection.objects.link(root)
    main, moving, anchors = [], [], []
    groups = []

    def count(label, n0):
        if tris:
            groups.append((label, V2.tri_count(main[n0:])))
        return len(main)
    n0 = 0
    for label, fn in PARTS:
        fn(main, moving, anchors)
        n0 = count(label, n0)
    for label, n in groups:
        log(f"  {label}: {n} triangles")
    nodes = {}
    node = V3.finish_node(main, "main", (0, 0, 0))
    node.parent = root
    nodes["main"] = node
    for parts, name, origin, axis in moving:
        if tris:
            log(f"  node {name}: {V2.tri_count(parts)} triangles")
        nd = V3.finish_node(parts, name, origin) if axis == "fixed" else moving_node(parts, name, origin, axis)
        nd.parent = root
        nodes[name] = nd
    for (name, p, size) in anchors:
        e = bpy.data.objects.new(name, None)
        bpy.context.scene.collection.objects.link(e)
        e.parent = root
        e.location = Vector(p) * PX
        e.scale = (size, size, size)
    bpy.context.view_layer.update()
    return root, nodes, anchors


# ------------------------------------------------------------------------------------------------


def run(o):
    import reimagined_kit as RK
    if not KEY or not PARTS:
        raise SystemExit("building_common: run a building's file (it calls setup()), not this module")
    global WEATHERING, MOTTLE
    WEATHERING = o.get("weathering", WEATHERING)
    MOTTLE = o.get("mottle", MOTTLE)
    house_weather()
    log(f"weathering {WEATHERING:g}, mottle {MOTTLE:g}")
    V2.setup_gpu = V3.setup_device
    bpy.ops.wm.read_factory_settings(use_empty=True)
    V3._mats.clear()
    t0 = time.time()
    root, nodes, anchors = build(o.get("tris"))
    for name in list(V3._mats):
        RF.mat(name)
    tris = V2.tri_count(V2.meshes_under(root))
    log(f"built {KEY}: nodes {sorted(nodes)}, {tris} triangles in {time.time() - t0:.1f} s")
    for (name, p, size) in anchors:
        log(f"  anchor {name}: px ({p[0]:.1f}, {p[1]:.1f}, {p[2]:.1f}) size {size:g}")
    if o.get("preview"):
        import reimagined_build as RB
        if REST:
            REST(nodes)
        RB.preview(root, o["preview"], KEY, PREVIEW_DIST)
        scene = bpy.context.scene                      # two more: the south-west flank (ladder, door, number), the top
        co = scene.camera
        for i, (az, el, d, t) in enumerate(PREVIEW_VIEWS):
            a, e = math.radians(az), math.radians(el)
            t = Vector(t)
            pos = t + Vector((math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))) * d
            co.location = pos
            co.rotation_euler = (t - pos).to_track_quat("-Z", "Y").to_euler()
            scene.render.filepath = os.path.join(o["preview"], f"{KEY}_p{4 + i}.png")
            bpy.ops.render.render(write_still=True)
        return
    if not o.get("out"):
        return
    out = o["out"]
    bld = os.path.join(out, "models", "building")
    path = os.path.join(bld, KEY + ".glb")
    png_dir = o.get("work") or os.path.join(bpy.app.tempdir, "d2k_reimagined_" + KEY)
    pngs = None
    if o.get("bake", True):
        _, pngs = RK.bake_reimagined(V2.meshes_under(root), o.get("bake_size", 2048), surface, png_dir, dict(DETAIL))
    if REST:                                           # the idle pose (parts shown only in some poses: hidden)
        REST(nodes)
        bpy.context.view_layer.update()
    V3.export_glb(root, path)
    if pngs:
        RK.glb_patch(path, pngs["color"], pngs["orm"], pngs["normal"])
    log(f"exported {path} ({os.path.getsize(path) // 1024} KB, {tris} triangles)")
    lod = os.path.join(bld, KEY + "_lod1.glb")
    lt = RK.export_lod(root, lod, o.get("lod", 0.34))
    RK.strip_images(lod)
    os.makedirs(os.path.join(out, "stats"), exist_ok=True)
    json.dump(dict(key=KEY, triangles=tris, lod1=lt, glb_kb=os.path.getsize(path) // 1024, bake=o.get("bake_size", 2048),
                   nodes=sorted(nodes), anchors=[a[0] for a in anchors]),
              open(os.path.join(out, "stats", KEY + ".json"), "w"))
    if POSES:
        write_poses(root, nodes, out, path)
    if pngs and o.get("turntable"):
        RK.review(root, o["turntable"], png_dir, KEY, samples=o.get("samples", 40), dist=REVIEW["dist"], target=REVIEW["target"],
                  closeups=REVIEW["closeups"])
    log(f"total {KEY} {time.time() - t0:.1f} s")


def write_poses(root, nodes, out, main_path):
    """OUT/variants/KEY_poseNN.glb for every pose of POSES - the node transforms only (export_poses.nodes_only: what
    the pack holds; the loader needs the main glb's node list, name for name) - and KEY.spec.json."""
    import export_poses as EP
    vdir = os.path.join(out, "variants")
    os.makedirs(vdir, exist_ok=True)
    for f in os.listdir(vdir):
        if f.startswith(KEY + "_pose") or f == KEY + ".spec.json":
            os.remove(os.path.join(vdir, f))
    names = [n.get("name", "") for n in EP.read_glb(main_path)[0]["nodes"]]
    base = {n: (ob.location.copy(), ob.rotation_euler.copy(), ob.rotation_quaternion.copy(), ob.scale.copy()) for n, ob in nodes.items()}

    def reset():
        for n, ob in nodes.items():
            ob.location, ob.rotation_euler, ob.rotation_quaternion, ob.scale = (v.copy() for v in base[n])
    for k, pose in enumerate(POSES):
        reset()
        pose(nodes)
        bpy.context.view_layer.update()
        p = os.path.join(vdir, f"{KEY}_pose{k:02d}.glb")
        V3.export_glb(root, p)
        j = EP.nodes_only(EP.read_glb(p)[0])
        if [n.get("name", "") for n in j["nodes"]] != names:
            raise SystemExit(f"{p}: its nodes differ from the main model's")
        EP.write_glb(p, j)
    reset()
    bpy.context.view_layer.update()
    json.dump(dict(POSE_SPEC or {}, key=KEY, poses=len(POSES)), open(os.path.join(vdir, KEY + ".spec.json"), "w"), indent=1)
    log(f"wrote {len(POSES)} node poses + {KEY}.spec.json to {vdir}")


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    o = dict(bake=True, bake_size=2048, samples=40, lod=0.34)
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--out", "--preview", "--turntable", "--work"):
            o[a[2:]] = argv[i + 1]; i += 1
        elif a in ("--bake-size", "--samples"):
            o[a[2:].replace("-", "_")] = int(argv[i + 1]); i += 1
        elif a in ("--lod", "--weathering", "--mottle"):
            o[a[2:]] = float(argv[i + 1]); i += 1
        elif a == "--no-bake":
            o["bake"] = False
        elif a == "--tris":
            o["tris"] = True
        else:
            raise SystemExit("unknown option " + a)
        i += 1
    run(o)

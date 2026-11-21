"""Reimagined Ordos wind trap - the redesign with real materials and human-scale detail (docs/3d-models.md
"Wind trap redesigns"), the Ordos one of the three (reimagined_wind_trap.py: Atreides, approved;
reimagined_wind_trap_harkonnen.py).

The identity is the main pack's model (ordos_wind_trap.py, read from the Ordos frames): two tall brass CLAMS - bulbous
half domes standing one behind the other on the south-west / north-east diagonal, their flat mouths leaning back and
facing south-east: each mouth a dark face with three cyan light louvres (the longest at the bottom) inside a gold
arch; ribbed shells (meridian ribs, two hoops) with house-colour panels in a chequer on the west flank; at the
south-east foot a small brass pump box with a pale pipe, a round mound and a brown junction block with a house panel
and a teal orb between the clams. Same layout, proportions and footprint (ordos_wind_trap.P). What is new here:

  * the clams are real shells: a dark bronze core under lacquered BRASS PLATES with fine true joints (five close brass
    tones), slim bronze meridian ribs and hoops with studs, the house panels as enamel inlays in pale gold frames, a
    polished gold arch with studs and a teal sensor orb at its apex, a mouth that is a real recess: a bronze duct
    with a big five-blade TURBINE of pale gold, its tips lit cyan, turning in front of a glowing ring - plainly seen
    between the three slim louvres that carry the lit cyan glass (no centre post, wide gaps);
  * the round mound is a cooling pit: a big open FAN (12 px across) under a three-spoke guard in a bronze drum on a
    stone plinth - the spinning part the game camera sees at any zoom;
  * the pump box is a control kiosk (brass cabinet, a teal service door with a lit window, a control panel, a vent,
    a roof extractor fan, a pale accumulator cap with an orb), the junction a transformer block (cooling fins, the
    house panel, insulator bulbs, a steam relief vent, a slim weather mast with vane, anemometer, beacon and whip);
    pale and bronze pipes with flanges and a hand wheel, lamp posts, a teal orb on a pedestal, cargo crates;
  * the same human-scale parts as the other two (service doors with frames, a step, a lit window and a lamp,
    maintenance ladders to a roof hatch, louvred vents, junction boxes and conduits, hazard edges, warning plates,
    stencilled unit numbers 05 / 06) and the same surface pass at the same approved strength (WEATHERING 1.0,
    MOTTLE 1.0), with the grime in colours that read on brass: dark tarnish stains and run-off, verdigris bleeding
    out of the ribs and hoops (clam_streaks: the Ordos twin of the scoops' rust), a pale dusty foot.

This file is the Ordos house on windtrap_common.py (the helpers, surface(), build(), run(), the command line):

    blender -b --factory-startup --python tools/models/reimagined_wind_trap_ordos.py -- --out DIR [--work DIR]
        [--no-bake] [--bake-size 2048] [--lod 0.34] [--weathering 1.0] [--mottle 1.0] [--preview DIR]
        [--turntable DIR --samples 24] [--tris]
    install: tools/models/wind_trap_check/install.sh DIR artpacks/models-reimagined ORDOS_WIND_TRAP

Everything is in ORIGINAL SPRITE PIXELS (1 px = 1/32 tile; x east, y north of the footprint centre, z up).
Nothing is copied from the original art.
"""
import math
import os
import random
import sys

import bmesh
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import windtrap_common as WC  # noqa: E402  (helpers, the surface / weathering pass, build, run)

K = WC.K
PX = WC.PX
beam, tube, obox, polar = WC.beam, WC.tube, WC.obox, WC.polar
new_obj, rings_solid = WC.new_obj, WC.rings_solid
unit, plate, bolt, quad_bar, rotor = WC.unit, WC.plate, WC.bolt, WC.quad_bar, WC.rotor

KEY = "ORDOS_WIND_TRAP"
# the main pack's layout (ordos_wind_trap.P; not imported: ordos_kit would re-colour the kit's palette)
P = dict(
    face=-56.0,                                   # the mouths face south-east
    c_s=(-10.5, 7.5), r_s=19.5, h_s=33.0,         # the south clam: centre on the ground, radius, height
    c_n=(4.5, 43.0), r_n=19.5, h_n=32.0,          # the north clam
    cut=0.8, lean=0.2,                            # the mouth: a plane cut * r in front of the centre, leaning back `lean` px per px of height
    pump=(14.0, -8.0), pit=(16.0, 11.5), junction=(20.0, 25.0),
)
NODES = dict(
    main="everything that does not move",
    turbine_s="the south clam's intake turbine in the mouth (5 pale gold blades with lit tips; spins about the mouth's leaning axis)",
    turbine_n="the north clam's intake turbine",
    fan_0="the big cooling fan in the round pit between the clams and the kiosk (vertical axis; 12 px across)",
    fan_1="the small extractor fan on the control kiosk's roof (vertical axis)",
    vane="the wind vane on the weather mast",
    anemometer="the cup anemometer on the mast's arm",
)
ANCHOR_DOC = dict(
    fx_vent_0="the south mouth (centre of the opening)", fx_vent_1="the north mouth",
    fx_vent_2="the cooling pit's outlet: the warm puff",
    fx_smoke_0="the top of the steam relief vent on the transformer block",
    fx_beacon_0="the red beacon on the mast",
    fx_light_0="the lamp post at the kiosk", fx_light_1="the lamp post east of the cooling pit",
)

# ------------------------------------------------------------------------------------------------
# Palette: the Ordos tones (ordos_kit.COL, read from the Ordos frames) on this generator's material names. Names the
# shared helpers use (windtrap_common.COL: door, rail, slat, ...) are re-coloured; o_* are this model's own.

COL = dict(
    shell_core=(0.130, 0.095, 0.060),   # under the plates: the joints
    o_brass_a=(0.880, 0.590, 0.240),    # the shell plates: lacquered brass in close tones
    o_brass_b=(0.820, 0.550, 0.230),
    o_brass_c=(0.860, 0.560, 0.205),
    o_brass_d=(0.760, 0.505, 0.215),
    o_brass_e=(0.720, 0.510, 0.260),    # a replaced plate now and then: a paler, duller stock
    o_gold=(1.000, 0.730, 0.270),       # the arch, lids, rims: polished gold
    o_pale=(0.990, 0.890, 0.580),       # pale gold: frames of the house inlays, caps, the pale pipe
    o_rib=(0.360, 0.240, 0.115),        # meridian ribs: dark bronze
    o_hoop=(0.300, 0.200, 0.100),       # hoops, the sill
    o_bronze=(0.520, 0.340, 0.160),     # bronze fittings, pipes
    o_bronze_dk=(0.270, 0.180, 0.090),
    o_wall=(0.820, 0.555, 0.240),       # the kiosk's brass cabinet
    o_wall_dk=(0.660, 0.445, 0.205),
    o_block=(0.500, 0.365, 0.215),      # brown plating: the transformer block
    o_block_dk=(0.300, 0.215, 0.125),   # the pit's stone plinth
    o_teal=(0.150, 0.300, 0.270),       # fixed dull teal enamel (doors, crates' bands): not the house colour
    o_blade=(0.990, 0.850, 0.500),      # turbine / fan blades: pale gold, bright against the dark duct
    o_glow_a=(0.055, 0.310, 0.320),     # the lit glass on the louvres: dark cyan glass, a touch greener than the
    o_glow_b=(0.045, 0.265, 0.280),     # Atreides one (glows: EMIT)
    o_tip=(0.060, 0.330, 0.330),        # the lit blade tips
    o_ring=(0.045, 0.250, 0.260),       # the glowing ring behind a rotor
    o_orb=(0.020, 0.090, 0.085),        # the teal-black glass orbs
    o_cyan=(0.100, 0.950, 0.900),       # their glints
    slat=(0.200, 0.145, 0.090),         # the louvre blades, duct fittings: dark bronze
    duct=(0.165, 0.125, 0.090),
    grate=(0.300, 0.240, 0.170),
    door=(0.150, 0.300, 0.270),         # door leaves: teal enamel
    door_fr=(0.780, 0.640, 0.400),      # frames, handles, small fittings: pale brass
    rail=(0.640, 0.520, 0.320),         # railings, ladders
    stencil=(0.230, 0.160, 0.090),      # stencilled numbers: dark on the brass
    foot=(0.200, 0.145, 0.085),
    o_foot=(0.200, 0.145, 0.085),       # the clams' foot discs
)
SPEC = dict(
    shell_core=(0.2, 0.75, "dark"), o_brass_a=(0.2, 0.38, "paint"), o_brass_b=(0.2, 0.44, "paint"), o_brass_c=(0.2, 0.41, "paint"),
    o_brass_d=(0.2, 0.48, "paint"), o_brass_e=(0.15, 0.52, "paint"), o_gold=(0.35, 0.30, "steel"), o_pale=(0.30, 0.34, "steel"),
    o_rib=(0.5, 0.46, "steel"), o_hoop=(0.5, 0.50, "steel"), o_bronze=(0.6, 0.40, "steel"), o_bronze_dk=(0.5, 0.50, "steel"),
    o_wall=(0.2, 0.44, "paint"), o_wall_dk=(0.2, 0.50, "paint"), o_block=(0.2, 0.58, "paint"), o_block_dk=(0.0, 0.80, "paint"),
    o_teal=(0.2, 0.42, "paint"), o_blade=(0.45, 0.28, "steel"), o_glow_a=(0.0, 0.12, "glass"), o_glow_b=(0.0, 0.14, "glass"),
    o_tip=(0.0, 0.14, "glass"), o_ring=(0.0, 0.2, "glass"), o_orb=(0.3, 0.10, "glass"), o_cyan=(0.0, 0.2, "glass"),
    slat=(0.5, 0.46, "steel"), duct=(0.5, 0.55, "steel"), grate=(0.6, 0.55, "steel"), door=(0.2, 0.42, "paint"),
    door_fr=(0.6, 0.36, "steel"), rail=(0.6, 0.42, "steel"), stencil=(0.0, 0.55, "paint"), foot=(0.0, 0.8, "dark"),
    o_foot=(0.0, 0.8, "dark"),
)
# (linear RGB, the brightest channel = strength: the louvre glass as bright as the Atreides one, 0.32 / 0.26)
# (the orbs only smoulder: at the louvres' strength they are white beads by day)
EMIT = dict(o_glow_a=(0.045, 0.30, 0.32), o_glow_b=(0.036, 0.245, 0.26), o_tip=(0.04, 0.26, 0.26), o_ring=(0.022, 0.15, 0.15),
            o_orb=(0.006, 0.05, 0.046), o_cyan=(0.04, 0.28, 0.26))
UV_SCALE = dict(o_glow_a=0.8, o_glow_b=0.8, o_tip=0.6, o_ring=0.5, o_orb=0.5, o_cyan=0.3, o_block_dk=0.7, shell_core=0.2, o_foot=0.3)

# The Ordos shells are lacquered brass: a kind of its own in this surface pass (KIND; for the bake's detail pass it
# is "paint", like the other houses' plates) - it takes the grime at the paint's (approved) strength, but its worn
# edges are polished metal and it hardly chips. (Metallic 0.2 only: the engine's sky tints a strongly metallic
# surface olive, and the brass has to read warm.)
KIND = {k: "brass" for k in ("o_brass_a", "o_brass_b", "o_brass_c", "o_brass_d", "o_brass_e", "o_wall", "o_wall_dk")}
#       mottle  film  soft  cavity wear  chips grime streak dust  sand
WEAR = dict(brass=(0.050, 0.38, 0.34, 0.60, 0.45, 0.06, 0.28, 0.22, 0.38, 0.50))
#       blotch run-off low  soot  "rust" recess sand
GRIME_W = dict(brass=(0.80, 0.55, 0.50, 0.80, 0.75, 0.45))
# The grime that has to read on brass: dark tarnish (stains, run-off), verdigris where the Atreides shells rust (out
# of the ribs and hoops), a pale dusty splash at the foot, a heat tarnish round the steam vent. Strengths (WEAR_GROW,
# the tables above) and the two knobs are the shared ones.
STAIN = (0.215, 0.165, 0.100)
RUNOFF = (0.300, 0.220, 0.130)
RUST = (0.350, 0.650, 0.550)           # verdigris
RUST_DK = (0.250, 0.330, 0.270)
LOW = (0.600, 0.500, 0.360)
SOOT = (0.130, 0.105, 0.080)
CAVITY = (0.105, 0.078, 0.050)
DUST = (0.800, 0.670, 0.440)           # (warmer than the Atreides dust: a grey film turns brass khaki)
SAND = (0.700, 0.585, 0.400)
SHELL_MATS = ("o_brass_a", "o_brass_b", "o_brass_c", "o_brass_d", "o_brass_e")     # get the clams' verdigris

BAYS = 8                                # rib bays round a clam (7 ribs; the mouth's edges are the other two)
ROWS = (0.3, 1.0, 3.0, 5.0, 7.0, 8.4)   # the plate rows' edges in ninths of the meridian (t = k / 9)
HOOPS = (3.0, 5.0)                      # the two hoops (ninths)
TONES = ("o_brass_a", "o_brass_a", "o_brass_a", "o_brass_b", "o_brass_b", "o_brass_b", "o_brass_c", "o_brass_c", "o_brass_d", "o_brass_d", "o_brass_e")
# the house inlays: (bay, row) in the chequer of the main pack's model (bays 4..7 = the west / south-west flank)
INLAYS = ((5, 1), (7, 1), (4, 2), (6, 2), (5, 3), (7, 3))
STEAM_TOP = (21.6, 28.4, 12.4)          # px: the top of the steam relief vent (transformer(): on the block's top)


# ------------------------------------------------------------------------------------------------
# The clams' geometry (px)


class Clam:
    """One clam: a bulbous dome about the vertical through c (radius R at the ground, height h), cut by the mouth's
    plane. Shell coordinates as windtrap_common.plate wants them: t up the meridian (0 = the ground, 1 = the top) and
    ang round the shell (0 = the mouth's north-east edge, over the back, 180 = its south-west edge: the flank the
    game camera sees is ang 110..180). Mouth coordinates: s along the mouth towards north-east, u into the clam (at
    right angles to s, horizontal), z up - the plane leans back, so u = 0 is further in the higher up."""
    D = 44.0                            # px per unit of t (the meridian's length; plate() turns a gap in px into t with it)

    def __init__(self, tag, c, R, h):
        self.tag, self.c, self.R, self.h = tag, c, R, h
        self.fa = math.radians(P["face"])
        self.f = Vector((math.cos(self.fa), math.sin(self.fa), 0.0))        # out of the mouth
        self.s = Vector((-self.f.y, self.f.x, 0.0))                         # along the mouth, towards north-east
        self.out = (self.f + Vector((0.0, 0.0, P["lean"]))).normalized()    # the mouth's normal (out and up)
        self.up = self.out.cross(self.s).normalized()                       # up along the leaning face
        if self.up.z < 0:
            self.up = -self.up

    # -- the shell
    def rz(self, t):
        a = math.pi / 2 * min(max(t, 0.0), 0.992)
        return self.R * math.cos(a) ** 0.8, self.h * math.sin(a)

    def r_at(self, z):
        return self.R * max(0.0, 1.0 - (z / self.h) ** 2) ** 0.4

    def off(self, z, u=0.0):
        return P["cut"] * self.R - P["lean"] * z - u

    def al(self, t):
        r, z = self.rz(t)
        return math.acos(max(-1.0, min(1.0, self.off(z) / max(r, 1e-3)))) if r > self.off(z) else 0.0

    def phi(self, t, ang):
        al = self.al(t)
        return self.fa + al + (2 * math.pi - 2 * al) * ang / 180.0

    def N(self, t, ang):
        a = math.pi / 2 * min(max(t, 0.0), 0.97)
        dr = -0.8 * self.R * math.cos(a) ** -0.2 * math.sin(a)
        dz = self.h * math.cos(a)
        ph = self.phi(t, ang)
        return Vector((math.cos(ph) * dz, math.sin(ph) * dz, -dr)).normalized()

    def S(self, t, ang, lift=0.0):
        r, z = self.rz(t)
        ph = self.phi(t, ang)
        p = Vector((self.c[0] + r * math.cos(ph), self.c[1] + r * math.sin(ph), z))
        return p + self.N(t, ang) * lift if lift else p

    def per_deg(self, t, ang):
        return max(0.05, (self.S(t, ang + 0.5) - self.S(t, ang - 0.5)).length)

    # -- the mouth
    def half(self, z, u=0.0):
        """Half width of the shell's section by the plane u px behind the mouth's, at height z."""
        r = self.r_at(z)
        o = self.off(z, u)
        return math.sqrt(r * r - o * o) if r > abs(o) else 0.0

    def ztop(self, u=0.0):
        lo, hi = 0.0, self.h
        for _ in range(40):
            m = (lo + hi) / 2
            if self.r_at(m) > self.off(m, u):
                lo = m
            else:
                hi = m
        return lo

    def outline(self, u=0.0, w=0.0, n=12):
        """The opening's outline u px behind the mouth's plane, drawn in by w px in that plane (negative: out):
        [(s, z)] of its north-east half from the ground to the top (n + 1 points; the other half mirrors it)."""
        zt = self.ztop(u)
        pts = []
        for i in range(n + 1):
            z = zt * math.sin(math.pi / 2 * (i / n) ** 0.8)
            pts.append((self.half(z, u), z))
        out = []
        for i, (s, z) in enumerate(pts):
            if i == 0:
                ns, nz = 1.0, 0.0
            elif i == n:
                ns, nz = 0.0, 1.0
            else:
                ts, tz = pts[i + 1][0] - pts[i - 1][0], pts[i + 1][1] - pts[i - 1][1]
                ln = math.hypot(ts, tz)
                ns, nz = tz / ln, -ts / ln
            out.append((max(0.0, s - ns * w), max(0.0, z - nz * w)))
        return out

    def half_in(self, z, u=0.0, w=0.0):
        """Half width at height z of the outline drawn in by w px (0 above its top)."""
        ol = self.outline(u, w, 40)
        for (s0, z0), (s1, z1) in zip(ol[:-1], ol[1:]):
            if z0 <= z <= z1 and z1 > z0:
                return s0 + (s1 - s0) * (z - z0) / (z1 - z0)
        return ol[0][0] if z < ol[0][1] else 0.0

    def L(self, s, u, z):
        o = self.off(z, u)
        return (self.c[0] + self.f.x * o + self.s.x * s, self.c[1] + self.f.y * o + self.s.y * s, z)



def mouth_band(name, cl, u0, u1, w_out, w_in, material, n=12):
    """An arch-shaped band round the mouth: between the outlines drawn in by w_in (inner) and w_out (outer;
    negative = outside the shell) px, from depth u0 to u1."""
    ols = [cl.outline(u, w, n) for (w, u) in ((w_out, u0), (w_out, u1), (w_in, u1), (w_in, u0))]
    us = (u0, u1, u1, u0)
    rings = []
    for (side, i) in [(-1, i) for i in range(n + 1)] + [(1, i) for i in range(n - 1, -1, -1)]:
        rings.append([cl.L(side * ol[i][0], u, ol[i][1]) for ol, u in zip(ols, us)])
    bm = bmesh.new()
    rings_solid(bm, rings)
    return new_obj(name, bm, material)


def disc_ring(name, c3, e1, e2, axis, profile, material, segs=24):
    """A ring about `axis` through c3 (e1, e2 span its plane): the closed profile [(radius, along the axis)]."""
    bm = bmesh.new()
    vs = [[bm.verts.new(tuple(c3 + (e1 * math.cos(2 * math.pi * i / segs) + e2 * math.sin(2 * math.pi * i / segs)) * r + axis * a))
           for i in range(segs)] for (r, a) in profile]
    for a_, b_ in zip(vs, vs[1:] + vs[:1]):
        for i in range(segs):
            j = (i + 1) % segs
            bm.faces.new([a_[i], a_[j], b_[j], b_[i]])
    return new_obj(name, bm, material)


def orb(parts, name, p, r, collar=True):
    """A teal-black glass orb with a cyan glint on a bronze collar (after ordos_kit.orb): p = its centre."""
    parts.append(K.ball(f"{name}_g", p, r, "o_orb", 12))
    if collar:
        parts.append(K.cyl(f"{name}_c", (p[0], p[1]), r * 0.95, p[2] - r * 0.95, p[2] - r * 0.45, "o_bronze_dk", 10))
    parts.append(K.ball(f"{name}_h", (p[0] - r * 0.42, p[1] - r * 0.30, p[2] + r * 0.62), r * 0.4, "o_cyan", 6))


def clam_stencil(parts, name, cl, text, ang_left, t_low, w_px, h_px, lift):
    """Stencilled digits on a clam's flank: read from outside, the text runs towards the mouth's south-west edge
    (ang rising) and stands up the meridian."""
    da = w_px / cl.per_deg(t_low + h_px / cl.D / 2, ang_left)
    dt = h_px / cl.D
    for i, ch in enumerate(text):
        a0 = ang_left + i * da * 1.45
        for seg in WC.DIGITS[ch]:
            x0, x1, y0, y1 = WC.SEGMENTS[seg]
            parts.append(plate(f"{name}_{i}{seg}", cl, t_low + y0 * dt, t_low + y1 * dt, a0 + x0 * da, a0 + x1 * da, lift, 0.14, "stencil", na=1, nt=1))


def lamp_post(main, anchors, i, p):
    main.append(K.cyl(f"lamp{i}_foot", p[:2], 0.6, 0.0, 0.8, "o_bronze_dk", 8))
    main.append(tube(f"lamp{i}_post", (p[0], p[1], 0.0), (p[0], p[1], p[2] - 0.4), 0.22, "o_bronze", 6))
    main.append(K.cyl(f"lamp{i}_ring", p[:2], 0.5, p[2] - 0.9, p[2] - 0.6, "o_gold", 8))
    main.append(K.cyl(f"lamp{i}_cap", p[:2], 0.42, p[2] + 0.72, p[2] + 0.95, "o_bronze_dk", 8, 0.2))   # (small: the camera looks down on the bead)
    main.extend(K.lamp(f"lamp{i}", p, 0.6))
    anchors.append((f"fx_light_{i}", (p[0], p[1], p[2] + 0.25), 0.6))


# ------------------------------------------------------------------------------------------------
# A clam


def clam(main, moving, anchors, tag, number):
    cl = Clam(tag, P[f"c_{tag}"], P[f"r_{tag}"], P[f"h_{tag}"])
    c, R, h = cl.c, cl.R, cl.h
    rnd = random.Random(31 if tag == "s" else 37)
    wall, u_in = 1.4, 6.0
    face_deg = P["face"]
    # ---- the core: a dark bronze dome cut at the back of the intake, its wall round the mouth, the plinth
    n, steps = 24, 10
    rings = []
    for k in range(steps + 1):
        t = min(k, steps - 0.25) / steps
        r, z = cl.rz(t)
        o = cl.off(z, u_in)
        al = math.acos(max(-0.999, min(0.999, o / max(r, 1e-3)))) if r > o else 0.0
        rings.append([(c[0] + r * math.cos(cl.fa + al + (2 * math.pi - 2 * al) * i / n), c[1] + r * math.sin(cl.fa + al + (2 * math.pi - 2 * al) * i / n), z)
                      for i in range(n + 1)])
    bm = bmesh.new()
    rings_solid(bm, rings)
    main.append(new_obj(f"{tag}_shell", bm, "shell_core"))
    main.append(mouth_band(f"{tag}_wall", cl, 0.0, u_in + 0.1, 0.03, wall, "shell_core", 14))
    # (the foot is a disc of its own material with a small atlas share, the plinth a ring: what lies under the clam
    # would only take atlas space from the plates)
    main.append(K.cyl(f"{tag}_foot", c, R + 1.7, 0.0, 0.9, "o_foot", 32))
    main.append(K.lathe_loop(f"{tag}_plinth", c, [(R - 2.2, 0.85), (R + 1.0, 0.85), (R + 0.6, 1.7), (R - 2.2, 1.7)], "o_bronze_dk", 28))
    al0 = math.degrees(cl.al(0.0))
    for k in range(10):                                     # hazard edge on the apron in front of the mouth
        a0 = face_deg - al0 * 0.78 + 2 * al0 * 0.78 * k / 10
        main.append(K.arc(f"{tag}_hz{k}", c, R + 0.72, R + 1.08, a0, a0 + 2 * al0 * 0.78 / 10, 1.7, 1.84, "hz_y" if k % 2 else "hz_k", 2))
    # ---- lacquered brass plates with fine real joints, one of five close tones each
    for i, (k0, k1) in enumerate(zip(ROWS[:-1], ROWS[1:])):
        cols = BAYS * 2 if i < 3 else (BAYS if i == 3 else 4)
        for j in range(cols):
            main.append(plate(f"{tag}_pl{i}_{j}", cl, k0 / 9, k1 / 9, 180.0 * j / cols, 180.0 * (j + 1) / cols, 0.25, 0.45, rnd.choice(TONES),
                              gap=0.4, na=2 if i < 3 else 4))
    r9, z9 = cl.rz(0.93)
    main.append(K.lathe(f"{tag}_cap", c, [(r9 + 0.35, z9 - 0.3), (r9 * 0.82, z9 + 1.0), (r9 * 0.4, h + 0.25), (0.05, h + 0.4)], "o_gold", 16))
    orb(main, f"{tag}_cap_orb", (c[0], c[1], h + 0.85), 0.8)
    # ---- slim bronze ribs up the meridians and two hoops, studs where they cross (on the flank the camera sees)
    for j in range(1, BAYS):
        ang = 180.0 * j / BAYS
        da = 0.5 / cl.per_deg(0.2, ang)
        main.append(plate(f"{tag}_rib{j}", cl, 0.034, 0.925, ang - da, ang + da, 0.55, 0.5, "o_rib", na=1, nt=14))
        if j >= 4:
            for k in (1.0, 2.0, 4.0, 6.0):
                main.append(bolt(f"{tag}_rib{j}_b{k:g}", cl.S(k / 9, ang, 0.5), cl.N(k / 9, ang), 0.4, 0.3, "o_gold"))
    for i, k in enumerate(HOOPS):
        hw = 0.55 / cl.D
        main.append(plate(f"{tag}_hoop{i}", cl, k / 9 - hw, k / 9 + hw, 0.0, 180.0, 0.62, 0.55, "o_hoop", na=24, nt=1))
        for j in range(BAYS, BAYS * 2):
            ang = 180.0 * (j + 0.5) / (BAYS * 2)
            main.append(bolt(f"{tag}_hoop{i}_b{j}", cl.S(k / 9, ang, 0.58), cl.N(k / 9, ang), 0.4, 0.3, "o_gold"))
    # ---- house colour: enamel inlays in pale gold frames, in the chequer of the main pack's model
    for i, (bay, row) in enumerate(INLAYS):
        t0, t1 = ROWS[row] / 9 + 0.036, ROWS[row + 1] / 9 - 0.036
        a0, a1 = 180.0 * (bay + 0.23) / BAYS, 180.0 * (bay + 0.77) / BAYS
        dt, da = 0.42 / cl.D, 0.42 / cl.per_deg((t0 + t1) / 2, (a0 + a1) / 2)
        main.append(plate(f"{tag}_inlay{i}_fr", cl, t0 - dt, t1 + dt, a0 - da, a1 + da, 0.46, 0.4, "o_pale", na=2, nt=2))
        main.append(K.house(plate(f"{tag}_inlay{i}", cl, t0, t1, a0, a1, 0.62, 0.4, "HOUSE", na=4, nt=3), 0.38 if row != 2 else 0.33))
    # ---- the mouth: a polished gold arch with studs and an orb at its apex, the bronze sill, the duct, the turbine
    # in front of a glowing ring, three slim louvres carrying the lit glass
    main.append(mouth_band(f"{tag}_arch", cl, -0.95, 0.55, -1.05, 0.7, "o_gold", 14))
    ol = cl.outline(-0.95, -0.2, 18)
    for side in (-1, 1):
        for k in range(1, 17, 2):
            p = Vector(cl.L(side * ol[k][0], -0.95, ol[k][1]))
            main.append(bolt(f"{tag}_archb{side}_{k}", p, cl.out, 0.5, 0.32, "o_bronze_dk"))
    top = Vector(cl.L(0.0, -0.95, cl.outline(-0.95, -0.5, 4)[-1][1]))
    main.append(tube(f"{tag}_apex", tuple(top - cl.out * 0.2), tuple(top + cl.out * 0.75), 1.25, "o_bronze", 10))
    orb(main, f"{tag}_apex_orb", tuple(top + cl.out * 1.0), 0.95, collar=False)
    zf = 3.2                                                # the intake's floor
    w0 = cl.half(1.0)
    main.append(quad_bar(f"{tag}_sill", cl, -(w0 + 0.5), w0 + 0.5, [(-2.3, 1.7), (0.4, 1.7), (0.4, zf), (-1.8, zf)], "o_hoop"))
    for sgn in (-1, 1):                                     # the sill's end blocks
        main.append(quad_bar(f"{tag}_sillend{sgn}", cl, sgn * (w0 - 0.5), sgn * (w0 + 0.9), [(-2.6, 1.7), (0.5, 1.7), (0.5, zf + 0.7), (-2.1, zf + 0.7)], "o_bronze"))
    wf = cl.half_in(zf, 0.3, wall) + 0.3
    main.append(quad_bar(f"{tag}_floor", cl, -wf, wf, [(0.3, 1.0), (u_in + 0.2, 1.0), (u_in + 0.2, zf), (0.3, zf)], "grate"))
    for k in range(6):                                      # grating bars
        u = 1.0 + k * 0.85
        w = cl.half_in(zf + 0.2, u, wall) - 0.5
        main.append(quad_bar(f"{tag}_grate{k}", cl, -w, w, [(u - 0.22, zf), (u + 0.22, zf), (u + 0.22, zf + 0.2), (u - 0.22, zf + 0.2)], "slat"))
    main.append(mouth_band(f"{tag}_duct", cl, 0.3, u_in, wall - 0.1, wall + 0.3, "duct", 12))
    for k, u in enumerate((2.0, 4.6)):                      # duct stiffening hoops
        main.append(mouth_band(f"{tag}_ducth{k}", cl, u - 0.2, u + 0.2, wall + 0.2, wall + 0.75, "slat", 10))
    back = []
    for u in (u_in - 0.15, u_in + 0.05):
        ol = cl.outline(u, wall + 0.1, 10)
        back.append([cl.L(side * ol[i][0], u, ol[i][1]) for (side, i) in [(-1, i) for i in range(11)] + [(1, i) for i in range(9, 0, -1)]])
    bm = bmesh.new()
    rings_solid(bm, back)
    main.append(new_obj(f"{tag}_back", bm, "duct_dk"))
    # the turbine: a bellmouth ring on three struts, the rotor a node of its own; behind it the glowing ring
    zc, rt, ut = 14.6, 9.0, 3.5
    c3 = Vector(cl.L(0.0, ut, zc))
    e1, e2, axis = cl.s, cl.up, cl.out
    main.append(disc_ring(f"{tag}_bellmouth", c3, e1, e2, axis, [(rt + 0.3, 0.8), (rt + 1.15, 0.8), (rt + 0.9, -0.8), (rt + 0.3, -0.8)], "slat"))
    for k, a in enumerate((90.0, 205.0, 335.0)):            # struts from the ring out to the duct / down to the floor
        d = e1 * math.cos(math.radians(a)) + e2 * math.sin(math.radians(a))
        reach = rt + 1.0
        while reach < rt + 14.0:
            q = c3 + d * (reach + 0.25)
            uq = (cl.off(q.z) - (q - Vector((c[0], c[1], q.z))).dot(cl.f))
            if q.z < zf + 0.2 or abs((q - Vector((c[0], c[1], q.z))).dot(cl.s)) > cl.half_in(q.z, uq, wall + 0.2):
                break
            reach += 0.25
        main.append(beam(f"{tag}_strut{k}", tuple(c3 + d * (rt + 1.0)), tuple(c3 + d * reach), 0.5, 0.35, "slat"))
    cb = Vector(cl.L(0.0, u_in - 0.2, zc))
    main.append(disc_ring(f"{tag}_glowring", cb, e1, e2, axis, [(5.3, 0.0), (6.8, 0.0), (6.8, 0.14), (5.3, 0.14)], "o_ring", 20))
    parts = rotor(f"{tag}_turbine", 5, 1.5, rt, 2.3, 3.9, 0.32, 30.0, "o_blade", hub=(1.9, -0.7, 0.5, 2.0), tips=1.0, tip_mat="o_tip",
                  hub_mat="o_bronze")
    moving.append((parts, f"turbine_{tag}", tuple(c3), tuple(axis)))
    anchors.append((f"fx_vent_{0 if tag == 's' else 1}", cl.L(0.0, 0.5, zc), 1.0))
    # the louvres: slim bronze blades, the upper face tilted outwards, carrying the lit glass (the longest at the
    # bottom); no centre post - the turbine is seen between them
    for i, z in enumerate((9.3, 19.6, 27.2)):
        w = cl.half_in(z + 0.3, 0.3, wall) - 0.2
        a_, b_ = (-1.25, z - 0.75), (1.55, z + 0.55)
        ln = math.hypot(b_[0] - a_[0], b_[1] - a_[1])
        nu, nz = -(b_[1] - a_[1]) / ln, (b_[0] - a_[0]) / ln            # the upper face's normal (out and up)
        main.append(quad_bar(f"{tag}_slat{i}", cl, -w, w, [a_, b_, (b_[0] - nu * 0.55, b_[1] - nz * 0.55), (a_[0] - nu * 0.55, a_[1] - nz * 0.55)], "slat"))
        f0, f1 = 0.06, 0.74                                              # the lit strip on the upper face
        p0 = (a_[0] + (b_[0] - a_[0]) * f0 + nu * 0.02, a_[1] + (b_[1] - a_[1]) * f0 + nz * 0.02)
        p1 = (a_[0] + (b_[0] - a_[0]) * f1 + nu * 0.02, a_[1] + (b_[1] - a_[1]) * f1 + nz * 0.02)
        sec = [p0, p1, (p1[0] + nu * 0.14, p1[1] + nz * 0.14), (p0[0] + nu * 0.14, p0[1] + nz * 0.14)]
        s0, s1 = -(w - 0.8), w - 0.8
        nseg = max(2, round((s1 - s0) / 6.0))
        for k in range(nseg):
            x0 = s0 + (s1 - s0) * k / nseg + 0.14
            x1 = s0 + (s1 - s0) * (k + 1) / nseg - 0.14
            main.append(quad_bar(f"{tag}_lens{i}_{k}", cl, x0, x1, sec, "o_glow_a" if (k + i) % 2 == 0 else "o_glow_b"))
        for sgn in (-1, 1):                                             # end brackets to the duct
            main.append(quad_bar(f"{tag}_slatend{i}_{sgn}", cl, sgn * (w - 0.5), sgn * (w + 0.45), [(-0.7, z - 0.9), (1.3, z - 0.3), (1.3, z + 0.9), (-0.7, z + 0.3)], "o_bronze"))
    # ---- the flank the camera sees: a maintenance ladder up a rib to a roof hatch, the unit number, a service door
    # in a porch, a vent, a junction box with a conduit along the plinth
    al_ = 180.0 * 7 / BAYS
    stand = 1.1
    ts = [0.045 + 0.058 * i for i in range(12)]
    la = [tuple(cl.S(t, al_ - 0.72 / cl.per_deg(t, al_), stand)) for t in ts]
    lb = [tuple(cl.S(t, al_ + 0.72 / cl.per_deg(t, al_), stand)) for t in ts]
    WC.ladder(main, f"{tag}_ladder", la, lb)
    for k, t in enumerate((0.06, 0.25, 0.45, 0.66)):                    # stand-offs
        for sgn in (-1, 1):
            a = al_ + sgn * 0.72 / cl.per_deg(t, al_)
            main.append(beam(f"{tag}_lad_st{k}_{sgn}", tuple(cl.S(t, a, 0.3)), tuple(cl.S(t, a, stand)), 0.22, 0.22, "rail"))
    th_, ah_ = 0.725, al_ - 2.6 / cl.per_deg(0.725, al_)                 # the hatch beside the ladder's top
    hp, hn = cl.S(th_, ah_, 0.2), cl.N(th_, ah_)
    main.append(tube(f"{tag}_hatch", tuple(hp), tuple(hp + hn * 0.7), 1.45, "o_bronze", 12))
    main.append(tube(f"{tag}_hatch_lid", tuple(hp + hn * 0.7), tuple(hp + hn * 0.95), 1.2, "door", 12))
    main.append(beam(f"{tag}_hatch_bar", tuple(hp + hn * 1.1 - cl.s * 0.75), tuple(hp + hn * 1.1 + cl.s * 0.75), 0.2, 0.2, "door_fr"))
    clam_stencil(main, f"{tag}_num", cl, number, 180.0 * 7.2 / BAYS, 0.395, 1.8, 3.9, 0.4)
    ad = 180.0 * 6.5 / BAYS                                              # the porch: the middle of bay 6, under its inlay
    pd = math.degrees(cl.phi(0.05, ad))
    ox, oy = unit(pd)
    base = (c[0] + ox * (R - 0.6), c[1] + oy * (R - 0.6))
    main.append(obox(f"{tag}_porch", base, pd, 2.3, 2.15, 0.0, 7.0, "o_wall", 0.0, 0.95))
    main.append(obox(f"{tag}_porch_cap", base, pd, 2.6, 2.45, 7.0, 7.45, "o_gold"))
    door_c = (c[0] + ox * (R - 0.6 + 2.3), c[1] + oy * (R - 0.6 + 2.3))
    WC.door(main, f"{tag}_door", door_c, pd, 0.0, 2.5, 4.9)
    ax_, ay_ = unit(pd + 90.0)
    WC.warning_plate(main, f"{tag}_warn", (door_c[0] - ox * 0.04 + ax_ * 1.72, door_c[1] - oy * 0.04 + ay_ * 1.72), pd, 3.3, 1.25)
    av = 180.0 * 5.5 / BAYS                                              # a louvred vent box low on bay 5
    pv = math.degrees(cl.phi(0.05, av))
    vx, vy = unit(pv)
    main.extend(K.vent(f"{tag}_vent", (c[0] + vx * (R + 0.35), c[1] + vy * (R + 0.35)), pv + 90.0, 1.7, 2.6, 4.8, 0.55, 3, "door_fr"))
    aj = 180.0 * 7.6 / BAYS                                              # a junction box by the mouth, a conduit to the porch
    pj = math.degrees(cl.phi(0.05, aj))
    jx, jy = unit(pj)
    main.append(obox(f"{tag}_jbox", (c[0] + jx * (R + 0.3), c[1] + jy * (R + 0.3)), pj + 90.0, 0.9, 0.5, 2.3, 4.5, "door_fr"))
    cps = [(c[0] + math.cos(math.radians(a)) * (R + 0.75), c[1] + math.sin(math.radians(a)) * (R + 0.75), 2.8)
           for a in (pj + (pd - pj) * f for f in (0.0, 0.25, 0.5, 0.75, 0.9))]
    for k, (p0, p1) in enumerate(zip(cps[:-1], cps[1:])):
        main.append(tube(f"{tag}_conduit{k}", p0, p1, 0.3, "rubber", 6))
        mid = ((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2)
        main.append(obox(f"{tag}_clamp{k}", mid, math.degrees(math.atan2(mid[1] - c[1], mid[0] - c[0])), 0.5, 0.25, 2.2, 3.3, "door_fr"))
    return cl


# ------------------------------------------------------------------------------------------------
# The machinery at the south-east foot


def cooling_pit(main, moving, anchors):
    """The round mound of the main pack's model as a cooling pit: a stone plinth, a bronze drum, the big open fan."""
    c = P["pit"]
    main.append(K.cyl("pit_plinth", c, 9.0, 0.0, 3.2, "o_block_dk", 12, 8.1))
    main.append(K.cyl("pit_step", c, 8.25, 3.2, 3.5, "o_bronze_dk", 24))
    for k in range(16):                                    # hazard edge round the step
        main.append(K.arc(f"pit_hz{k}", c, 7.75, 8.2, 22.5 * k, 22.5 * (k + 1), 3.5, 3.6, "hz_y" if k % 2 else "hz_k", 2))
    main.append(K.lathe_loop("pit_drum", c, [(7.65, 3.4), (7.25, 5.35), (6.5, 5.35), (6.5, 3.9)], "o_bronze", 28))
    main.append(K.lathe_loop("pit_rim", c, [(7.4, 5.3), (7.4, 5.6), (6.4, 5.6), (6.4, 5.3)], "o_gold", 28))
    main.append(K.cyl("pit_well", c, 6.55, 3.75, 3.95, "duct_dk", 24))
    main.append(K.lathe_loop("pit_glow", c, [(4.9, 3.95), (6.0, 3.95), (6.0, 4.06), (4.9, 4.06)], "o_ring", 28))
    for k in range(8):                                     # bolts round the drum's foot
        px_, py_, _ = polar(c, 7.7, 45.0 * k + 22.5, 0.0)
        main.append(bolt(f"pit_b{k}", (px_, py_, 4.0), Vector((px_ - c[0], py_ - c[1], 0.0)).normalized(), 0.36, 0.24, "o_gold"))
    for k in range(3):                                     # the guard: three slim spokes to a hub cap
        main.append(beam(f"pit_guard{k}", polar(c, 6.9, 90.0 + 120.0 * k, 5.72), (c[0], c[1], 5.72), 0.3, 0.2, "o_bronze_dk"))
    main.append(K.cyl("pit_guard_hub", c, 0.95, 5.6, 5.92, "o_bronze_dk", 10))
    moving.append((rotor("fan_0", 5, 0.8, 6.15, 1.7, 2.9, 0.16, 24.0, "o_blade", hub=(1.1, -0.1, 0.16, 0.5), tips=0.9, tip_mat="o_tip",
                         hub_mat="o_bronze"), "fan_0", (c[0], c[1], 4.85), (0, 0, 1)))
    anchors.append(("fx_vent_2", (c[0], c[1], 6.2), 0.8))
    for k, a in enumerate((-105.0, -45.0, 15.0)):          # louvred vents in the plinth, an access step
        q = polar(c, 8.55, a)
        main.extend(K.vent(f"pit_vent{k}", q, a + 90.0, 1.5, 0.9, 2.5, 0.45, 2, "door_fr"))
    # a railing round the pit's far side (behind the fan as the game camera sees it)
    WC.railing(main, "pit_rail", [polar(c, 7.95, a, 3.5) for a in (12.0, 37.0, 62.0, 87.0, 112.0, 137.0, 162.0)], 2.0)
    q = polar(c, 9.4, -75.0)
    main.append(obox("pit_step0", q, -75.0, 0.9, 1.4, 0.0, 1.6, "concrete"))
    main.append(obox("pit_step1", polar(c, 8.6, -75.0), -75.0, 0.6, 1.4, 0.0, 3.2, "concrete"))


def kiosk(main, moving, anchors):
    """The pump box of the main pack's model as a control kiosk."""
    pc, pang, phl, phw, ph = P["pump"], 30.0, 5.4, 4.2, 6.0
    ex, ey = unit(pang)
    nx, ny = -ey, ex                                      # north (across); the camera sees the south face (-n)

    def pp(e, d, z=None):                                 # e along the kiosk, d out of the south face
        q = (pc[0] + ex * e - nx * d, pc[1] + ey * e - ny * d)
        return q if z is None else (q[0], q[1], z)
    main.append(obox("kiosk", pc, pang, phl, phw, 0.0, ph, "o_wall", 0.4, 0.95))
    main.append(obox("kiosk_plinth", pc, pang, phl + 0.3, phw + 0.3, 0.0, 1.0, "foot"))
    for k, (e, d) in enumerate(((-1, -1), (1, -1), (1, 1), (-1, 1))):          # corner pilasters
        main.append(obox(f"kiosk_pil{k}", pp(e * phl * 0.955, -d * phw * 0.945), pang, 0.5, 0.5, 0.0, ph - 0.1, "o_bronze", 0.0, 0.6))
    main.append(obox("kiosk_lid", pc, pang, phl + 0.3, phw + 0.3, ph - 0.1, ph + 0.7, "o_gold", 0.2))
    main.append(obox("kiosk_cap", pp(-1.9, 0.0), pang, 2.3, 2.0, ph + 0.7, ph + 2.3, "o_pale", 0.3, 0.85))
    orb(main, "kiosk_orb", pp(-1.9, 0.0, ph + 2.95), 0.85)
    # the roof extractor: a ring, the fan a node
    fc = pp(2.7, 0.0)
    zr = ph + 0.7
    main.append(K.lathe_loop("kfan_ring", fc, [(1.5, zr), (1.95, zr), (1.85, zr + 0.75), (1.5, zr + 0.75)], "o_bronze", 16))
    main.append(K.cyl("kfan_well", fc, 1.52, zr - 0.02, zr + 0.06, "duct_dk", 14))
    for k, a in enumerate((pang + 45.0, pang + 135.0)):
        main.append(beam(f"kfan_guard{k}", polar(fc, 1.7, a, zr + 0.8), polar(fc, 1.7, a + 180.0, zr + 0.8), 0.18, 0.14, "o_bronze_dk"))
    moving.append((rotor("fan_1", 4, 0.3, 1.36, 0.6, 1.0, 0.1, 26.0, "o_blade", hub=(0.36, -0.06, 0.1, 0.25), hub_mat="o_bronze"), "fan_1",
                   (fc[0], fc[1], zr + 0.32), (0, 0, 1)))
    # the south face: door in a porch, control panel, vent
    de, pe, ve = 1.9, -1.2, -3.7
    main.append(obox("kporch", pp(de, phw + 0.35), pang, 2.0, 0.6, 0.0, 5.75, "o_wall_dk", 0.0, 0.97))
    WC.door(main, "kdoor", pp(de, phw + 0.95), pang - 90.0, 0.0, 2.5, 4.8)
    main.append(obox("kpanel", pp(pe, phw + 0.15), pang, 0.85, 0.4, 2.4, 4.8, "o_bronze_dk"))
    main.append(obox("kpanel_face", pp(pe, phw + 0.57), pang, 0.7, 0.05, 2.6, 4.6, "door_fr"))
    for k, (dl, z, mname) in enumerate(((-0.35, 4.15, "green"), (0.0, 4.15, "green"), (0.35, 4.15, "o_cyan"), (-0.2, 3.2, "glass"), (0.25, 3.2, "glass"))):
        main.append(obox(f"kpanel_led{k}", pp(pe + dl, phw + 0.64), pang, 0.12 if z > 4 else 0.2, 0.04, z - (0.12 if z > 4 else 0.3), z + (0.12 if z > 4 else 0.3), mname))
    WC.warning_plate(main, "kpanel_warn", pp(pe, phw + 0.02), pang - 90.0, 5.0, 0.8)
    main.extend(K.vent("kvent", pp(ve, phw - 0.1), pang, 1.0, 2.6, 4.6, 0.5, 3, "door_fr"))
    # ---- plumbing: the pale pipe to the cooling pit (a valve with a hand wheel), the bronze pipe to the south clam
    WC.flanged_pipe(main, "pale", [pp(phl - 0.2, -1.0, 3.2), (22.6, -3.2, 3.2), (24.4, 4.6, 3.2), (23.4, 8.6, 2.4)], 1.15, "o_pale", "o_bronze")
    main.append(K.cyl("valve0", (23.4, 0.4), 1.55, 2.0, 4.7, "o_bronze", 10))
    WC.handwheel(main, "valve0_w", (23.4, 0.4), 5.5, 1.2)
    WC.flanged_pipe(main, "feed", [pp(-phl + 0.2, 0.6, 2.6), (4.6, -9.6, 2.6), (1.6, -8.6, 2.4)], 1.05, "o_bronze", "o_bronze_dk")
    for k, (x, y, z) in enumerate(((22.6, -3.2, 2.0), (24.4, 4.6, 2.0), (4.6, -9.6, 1.5))):          # pipe supports
        main.append(obox(f"support{k}", (x, y), 0.0, 0.5, 0.5, 0.0, z, "o_bronze_dk"))
    # ---- cargo crates, a lamp post, a cable trunk to the transformer
    for k, (x, y, a, s_, hh) in enumerate(((25.6, -6.4, 12.0, 1.5, 2.6), (27.6, -3.6, -20.0, 1.2, 2.0), (25.9, -6.2, 30.0, 1.1, 1.8))):
        z0 = 2.6 if k == 2 else 0.0
        main.append(obox(f"crate{k}", (x, y), a, s_, s_, z0, z0 + hh, "o_block", 0.08))
        main.append(obox(f"crate{k}_band", (x, y), a, s_ + 0.06, s_ * 0.22, z0 + 0.05, z0 + hh + 0.06, "o_teal"))
        main.append(obox(f"crate{k}_lid", (x, y), a, s_ * 0.8, s_ * 0.8, z0 + hh, z0 + hh + 0.12, "o_wall_dk"))
    lamp_post(main, anchors, 0, (6.6, -14.2, 7.4))
    main.extend(K.pipe("cable", [pp(phl - 1.0, -phw - 0.3, 0.7), (26.6, 1.0, 0.6), (27.4, 12.0, 0.6), (25.6, 19.4, 0.6), (24.6, 20.6, 2.0)], 0.4, "rubber", 6))


def transformer(main, moving, anchors):
    """The junction block of the main pack's model as a transformer: fins, the house panel, bulbs, the steam vent,
    the weather mast; the teal orb on a pedestal, the second lamp post."""
    jc, jang, jhl, jhw, jh = P["junction"], 40.0, 5.6, 4.2, 6.8
    ex, ey = unit(jang)
    nx, ny = -ey, ex

    def jp(e, d, z=None):                                 # e along the block, d out of the south face
        q = (jc[0] + ex * e - nx * d, jc[1] + ey * e - ny * d)
        return q if z is None else (q[0], q[1], z)
    main.append(obox("trafo", jc, jang, jhl, jhw, 0.0, jh, "o_block", 0.4, 0.95))
    main.append(obox("trafo_plinth", jc, jang, jhl + 0.3, jhw + 0.3, 0.0, 1.1, "foot"))
    main.append(obox("trafo_lid", jc, jang, jhl * 0.95 + 0.2, jhw * 0.95 + 0.2, jh - 0.1, jh + 0.5, "o_bronze_dk"))
    main.append(obox("trafo_top", jc, jang, jhl * 0.82, jhw * 0.78, jh + 0.5, jh + 1.3, "o_wall", 0.3, 0.9))
    for k, (e, d) in enumerate(((-1, -1), (1, -1), (1, 1), (-1, 1))):          # corner straps
        main.append(obox(f"trafo_strap{k}", jp(e * jhl * 0.95, -d * jhw * 0.94), jang, 0.5, 0.5, 0.0, jh - 0.2, "o_bronze_dk", 0.0, 0.6))
    main.append(obox("trafo_inlay_fr", jp(-2.4, jhw + 0.02), jang, 2.75, 0.3, 1.45, 5.65, "o_pale"))
    main.append(K.house(obox("trafo_inlay", jp(-2.4, jhw + 0.2), jang, 2.4, 0.3, 1.8, 5.3, "HOUSE"), 0.35))
    for k in range(6):                                     # cooling fins on the south face
        main.append(obox(f"trafo_fin{k}", jp(1.3 + 0.7 * k, jhw + 0.35), jang, 0.13, 0.5, 1.3, 5.8, "o_bronze_dk"))
    main.append(obox("trafo_term", jp(jhl + 0.3, 1.0), jang, 0.45, 1.2, 2.4, 4.8, "o_bronze_dk"))       # terminal box on the east end
    main.append(obox("trafo_term_lid", jp(jhl + 0.78, 1.0), jang, 0.06, 0.95, 2.7, 4.5, "door_fr"))
    zt_ = jh + 1.3
    for k, e in enumerate((-1.6, 0.2, 2.0)):               # insulator bulbs on the top
        q = jp(e, 1.2)
        main.append(K.cyl(f"bulb{k}_p", q, 0.36, zt_, zt_ + 1.5, "o_bronze_dk", 8))
        main.append(K.lathe(f"bulb{k}", q, [(0.3, zt_ + 1.3), (0.62, zt_ + 1.75), (0.68, zt_ + 2.15), (0.42, zt_ + 2.6), (0.05, zt_ + 2.85)], "o_pale", 10))
    # the top is a railed service deck (open towards the camera), a ladder up the east end
    tl_, tw_ = jhl * 0.82 - 0.3, jhw * 0.78 - 0.3
    WC.railing(main, "trafo_rail", [jp(-tl_, -tw_, zt_), jp(tl_, -tw_, zt_), jp(tl_, -2.5, zt_)], 2.0)
    WC.railing(main, "trafo_rail2", [jp(tl_, -0.7, zt_), jp(tl_, tw_, zt_)], 2.0)
    WC.ladder(main, "trafo_ladder", [jp(jhl + 0.9, -1.0, 0.15), jp(tl_ + 0.25, -1.0, zt_ + 2.0)], [jp(jhl + 0.9, -2.2, 0.15), jp(tl_ + 0.25, -2.2, zt_ + 2.0)])
    # the steam relief vent: a slim pipe with a collar and a flared lip
    sx_, sy_, sz = STEAM_TOP
    main.append(tube("steam_p", (sx_, sy_, jh + 0.4), (sx_, sy_, sz), 0.62, "o_bronze", 10))
    main.append(K.cyl("steam_col", (sx_, sy_), 0.85, jh + 2.4, jh + 3.0, "o_gold", 10))
    main.append(K.cyl("steam_lip", (sx_, sy_), 0.72, sz - 0.9, sz + 0.05, "o_bronze_dk", 10, 0.95))
    main.append(K.cyl("steam_bore", (sx_, sy_), 0.6, sz - 0.3, sz + 0.12, "burnt", 10))
    anchors.append(("fx_smoke_0", STEAM_TOP, 0.3))
    # the weather mast: vane (node), anemometer (node), beacon, whip antenna
    mx, my = jp(-3.6, -1.6)
    zt = 20.0
    main.append(K.cyl("mast_foot", (mx, my), 0.7, zt_ - 0.3, zt_ + 0.5, "door_fr", 8))
    main.append(tube("mast", (mx, my, zt_), (mx, my, zt), 0.28, "rail", 8, 0.18))
    arm0, arm1 = (mx - ex * 2.3, my - ey * 2.3, zt - 2.4), (mx + ex * 2.3, my + ey * 2.3, zt - 2.4)
    main.append(beam("mast_arm", arm0, arm1, 0.22, 0.22, "rail"))
    main.append(beam("mast_brace", (mx, my, zt - 4.2), (mx + ex * 1.5, my + ey * 1.5, zt - 2.5), 0.16, 0.16, "rail"))
    main.append(tube("mast_whip", (mx - ex * 1.2, my - ey * 1.2, zt - 2.4), (mx - ex * 1.2, my - ey * 1.2, zt + 4.2), 0.1, "o_bronze_dk", 5, 0.05))
    bp = (arm0[0], arm0[1], zt - 2.4 + 0.35)
    main.append(K.cyl("beacon_s", bp[:2], 0.36, zt - 2.3, zt - 2.05, "o_bronze_dk", 8))
    main.append(K.ball("beacon", (bp[0], bp[1], bp[2] + 0.2), 0.42, "red", 8))
    anchors.append(("fx_beacon_0", (bp[0], bp[1], bp[2] + 0.2), 0.5))
    vparts = [beam("vane_bar", (-1.7, 0.0, 0.0), (1.5, 0.0, 0.0), 0.16, 0.16, "rail"),
              obox("vane_fin", (-1.45, 0.0), 0.0, 0.62, 0.06, -0.55, 0.55, "o_teal"),
              K.ball("vane_nose", (1.6, 0.0, 0.0), 0.26, "o_gold", 6),
              tube("vane_hub", (0.0, 0.0, -0.45), (0.0, 0.0, 0.2), 0.22, "door_fr", 6)]
    moving.append((vparts, "vane", (mx, my, zt + 0.45), (0, 0, 1)))
    aparts = [tube("anem_hub", (0.0, 0.0, -0.3), (0.0, 0.0, 0.2), 0.2, "door_fr", 6)]
    for k in range(3):
        a = 120.0 * k
        aparts.append(beam(f"anem_arm{k}", (0.0, 0.0, 0.05), polar((0.0, 0.0), 1.0, a, 0.05), 0.1, 0.1, "rail"))
        aparts.append(K.ball(f"anem_cup{k}", polar((0.0, 0.0), 1.05, a, 0.05), 0.34, "o_gold", 6))
    moving.append((aparts, "anemometer", (arm1[0], arm1[1], zt - 2.4 + 0.45), (0, 0, 1)))
    # the teal orb on its pedestal (the main pack's lamp), a bronze pipe to the north clam, the second lamp post
    oc = (26.6, 27.6)
    main.append(K.cyl("orb_ped", oc, 1.15, 0.0, 4.3, "o_bronze", 10, 0.8))
    main.append(K.cyl("orb_ped_r", oc, 1.0, 4.3, 4.7, "o_gold", 10))
    orb(main, "orb", (oc[0], oc[1], 6.0), 1.55)
    WC.flanged_pipe(main, "link", [jp(-jhl + 0.2, -0.5, 3.0), (11.6, 23.4, 3.0), (9.0, 25.6, 2.6)], 1.0, "o_bronze", "o_bronze_dk")
    lamp_post(main, anchors, 1, (28.4, 10.4, 8.6))


# ------------------------------------------------------------------------------------------------
# The clams' verdigris (windtrap_common.SHELL_STREAKS)


def clam_streaks(T, name, pos, cur, rough_s, g_rust, g_run):
    """The Ordos twin of the scoops' rust: verdigris bleeding out of the meridian ribs and down from the two hoops
    of each clam, in streaks, with a darker green line right at the bronze. In the clam's coordinates (which clam:
    the nearer centre; ang round the shell as Clam.phi has it)."""
    if not g_rust:
        return cur, rough_s
    rgb, noise, remap, mix, mixf, mul, opf = T.rgb, T.noise, T.remap, T.mix, T.mixf, T.mul, T.opf
    x, y, z = (opf("DIVIDE", pos.outputs[k], PX) for k in ("X", "Y", "Z"))
    cs, cn = P["c_s"], P["c_n"]

    def d2(c):
        dx, dy = opf("SUBTRACT", x, c[0]), opf("SUBTRACT", y, c[1])
        return opf("ADD", opf("MULTIPLY", dx, dx), opf("MULTIPLY", dy, dy)), dx, dy
    (ds, dxs, dys), (dn, dxn, dyn) = d2(cs), d2(cn)
    north = opf("LESS_THAN", dn, ds)
    dx, dy = mixf(north, dxs, dxn), mixf(north, dys, dyn)
    R = P["r_s"]                                                       # (both clams: the same radius)
    h = mixf(north, P["h_s"], P["h_n"])
    zr = opf("MINIMUM", opf("DIVIDE", z, h), 0.98)
    r = opf("MULTIPLY", opf("POWER", opf("SUBTRACT", 1.0, opf("MULTIPLY", zr, zr)), 0.4), R)
    off = opf("SUBTRACT", P["cut"] * R, opf("MULTIPLY", z, P["lean"]))
    al = opf("ARCCOSINE", opf("MINIMUM", opf("MAXIMUM", opf("DIVIDE", off, r), -1.0), 1.0))
    rel = opf("SUBTRACT", opf("SUBTRACT", opf("ARCTAN2", dy, dx), math.radians(P["face"])), al)
    rel = opf("MULTIPLY", opf("FRACT", opf("DIVIDE", rel, 2 * math.pi)), 2 * math.pi)      # 0 .. 2 pi from the mouth's north-east edge
    span = opf("SUBTRACT", 2 * math.pi, opf("MULTIPLY", al, 2.0))
    f = opf("FRACT", opf("MULTIPLY", opf("DIVIDE", rel, span), float(BAYS)))                # 0 / 1 at a rib
    rib_d = opf("MULTIPLY", opf("MULTIPLY", opf("MINIMUM", f, opf("SUBTRACT", 1.0, f)), opf("DIVIDE", span, float(BAYS))), r)   # px to the nearest rib
    below = None                                                       # 1 right under a hoop, fading over 5 px down
    for k in HOOPS:
        zk = opf("MULTIPLY", h, math.sin(math.pi / 2 * k / 9))
        d_ = opf("SUBTRACT", zk, z)
        b_ = mul(remap(d_, 0.5, 5.5, 1.0, 0.0), remap(d_, 0.35, 0.6, 0.0, 1.0))
        below = b_ if below is None else opf("MAXIMUM", below, b_)
    low = remap(zr, 0.05, 0.8, 1.0, 0.45)                              # more of it lower down
    sk = remap(noise(34.0, 2.0, mapping=(1.0, 1.0, 0.05), offset=27.0), 0.36, 0.66, 0.0, 1.0)
    patch = remap(noise(6.0, 3.0, offset=33.0), 0.35, 0.65, 0.25, 1.0)
    at_rib = remap(rib_d, 0.55, 3.0, 1.0, 0.0)
    bleed = mul(mul(opf("MAXIMUM", at_rib, mul(below, remap(sk, 0.0, 1.0, 0.3, 1.0))), mul(patch, low)), remap(sk, 0.0, 1.0, 0.55, 1.0))
    line = mul(mul(opf("MAXIMUM", remap(rib_d, 0.55, 1.0, 1.0, 0.0), remap(below, 0.8, 1.0, 0.0, 1.0)), patch), low)
    cur = mix(mul(bleed, min(0.9, g_rust * 1.2)), cur, rgb(WC.RUST))
    cur = mix(mul(line, min(0.9, g_rust * 0.8)), cur, rgb(WC.RUST_DK))
    rough_s = mixf(mul(bleed, g_rust * 0.6), rough_s, 0.85)
    return cur, rough_s


# ------------------------------------------------------------------------------------------------

WC.setup(KEY, [("south clam", lambda main, moving, anchors: clam(main, moving, anchors, "s", "05")),
               ("north clam", lambda main, moving, anchors: clam(main, moving, anchors, "n", "06")),
               ("cooling pit", cooling_pit), ("kiosk", kiosk), ("transformer", transformer)],
         col=COL, spec=SPEC, emit=EMIT, uv_scale=UV_SCALE, kind=KIND, wear=WEAR, grime_w=GRIME_W, POLISH=("steel", "brass"),
         NODES=NODES, ANCHOR_DOC=ANCHOR_DOC, SHELL_MATS=SHELL_MATS, SHELL_STREAKS=clam_streaks, FLUE_TOP=STEAM_TOP,
         STAIN=STAIN, RUNOFF=RUNOFF, RUST=RUST, RUST_DK=RUST_DK, LOW=LOW, SOOT=SOOT, CAVITY=CAVITY, DUST=DUST, SAND=SAND)

if __name__ == "__main__":
    WC.main()

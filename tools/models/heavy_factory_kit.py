"""Shared layout of the redesigned heavy factories (Atreides, Harkonnen / Imperial, Ordos, Mercenary): a factory with
a real, open vehicle exit, laid out INSIDE its footprint cells so Graphics: 3D shows it at scale 1.0.

Used by tools/models/{atreides,harkonnen,ordos}_heavy_factory.py and ordos_mercenary_heavy_factory.py with their
house kit (atreides_kit / harkonnen_kit / ordos_kit: the same material names in the house's palette).

What the game does (Templates.bin, src/game/production.cpp findUnitExitBuilding, spawn.cpp kExitTile): the heavy
factory occupies the cells  . X .  /  X X X  /  X X X  /  x x x  (mask 0x7772; the last row is walkable, the two
top corners are FREE - another building may stand there). A finished unit appears on exit point 1 = cell (1, 1) and
drives south-west to exit point 2 = cell (0, 2) (the only kExitTile). In model px (x east, y north of the centroid
of the occupied cells, 73.6 px below the box top): from (0, 25.6) to (-32, -6.4).

What the originals show (frames base / main: 3057-8 Atreides, 3331-2 Harkonnen, 3605-6 Ordos, 3885-6 Mercenary):
a diamond turned about 41 degrees, on the ground 74 x 52 px about (0.5, 18) - it fits the 96 px box - with the works
on its north-west half and, in the middle of its south-west side, the pale ramp / door plates between brown gate
posts (the base frame, drawn under units): exactly where the path above crosses the wall.

Layout (t along the long axis: west corner -> south corner, s across to the north-east, px from P["c"]):
  * podium: a battered plinth in three pieces that leave a slot open - the forecourt and the garage;
    its north-west face is notched where the free cell (0, 0) cuts the diamond (parts above 2 px keep out);
  * hall: the works block over the garage; its south-west wall is the open bay door (no leaf: a rolled shutter box
    above it), the garage floor is at ground level (cell (1, 1) is no deck cell: a unit stands on the ground there)
    with a turntable ring under the spawn point;
  * apron: the pale ramp slab from the garage through the forecourt out to exit cell (0, 2), kerbs, gate posts;
  * node "base" = floor, apron and the garage's inner walls (the game draws it UNDER units: a unit is seen standing
    in the bay and drives out under the lintel); node "main" = the rest; node "glow" = emissive parts (not baked:
    their material keeps a real glTF emission): bay ceiling light strips, wall lamps, lit windows.
Anchors: fx_flue_N (stack tops), fx_light_N (the bay's ceiling strips: on the geometry), fx_weld_N (working flashes
just inside the bay door).
"""
import math

P = dict(
    ang=-41.0, c=(0.0, 23.5), hl=42.0, hw=31.0, round=12.0,    # the rounded diamond on the ground
    ph=13.0, inset=4.5,                              # podium height, batter of its outer faces
    hall=(-30.0, 13.0, -14.0, 20.0), hall_h=37.0,     # the hall: t0, t1, s0, s1; roof height
    bay_h=16.0,                                      # the garage's door / ceiling height (its plan: derive())
    apron_end=-38.0, apron_taper=3.0,                # the apron's outer end (s) and how much narrower it is there
    free_w=(-16.6, 42.2), free_e=(16.6, 42.2),       # the free cells' inner corners (model px; 0.6 px generous)
)
SPAWN, EXIT = (0.0, 25.6), (-32.0, -6.4)             # exit point 1 / 2 (cell centres), model px
WARM, LAMP = (1.0, 0.70, 0.34), (1.0, 0.82, 0.52)


def srgb_lin(c):
    return tuple(((v + 0.055) / 1.055) ** 2.4 if v > 0.04045 else v / 12.92 for v in c)


def glow_material(name, rgb, strength):
    """A plain material with a real emission (exported as emissiveFactor [+ KHR_materials_emissive_strength]); its
    name contains GLOW so the 3D renderer keeps it lit at night (model_gltf.cpp)."""
    import bpy
    m = bpy.data.materials.get(name)
    if m is None:
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        b = m.node_tree.nodes["Principled BSDF"]
        lin = srgb_lin(rgb)
        b.inputs["Base Color"].default_value = (*lin, 1)
        b.inputs["Emission Color"].default_value = (*lin, 1)
        b.inputs["Emission Strength"].default_value = strength
        b.inputs["Roughness"].default_value = 0.6
        m.diffuse_color = (*lin, 1)
    return m


def install(K, apron=None):
    """Keep the "glow" node out of the atlas bake (it would lose its emissive material). apron = an sRGB colour:
    registers the material "hf_apron" - the kits' pale materials ("white", "roof") collect drifted sand and a dust
    film near the ground, which hides a ramp slab lying on it; this one has neither (cavity dirt only)."""
    RF = K.RF
    if apron is not None:
        K.V3.COL["hf_apron"] = apron
        K.V3.SPEC["hf_apron"] = (0.0, 0.7, "dark")            # "dark": the weathering class without film, blotches and low dust
        K.V3.GRIME_SCALE["hf_apron"] = 0.5
    if getattr(RF, "_hf_patched", False):
        return
    orig = RF.bake_and_merge

    def bake(objs, size, samples=24):
        return orig([o for o in objs if not o.name.startswith("glow")], size, samples)
    RF.bake_and_merge = bake
    RF._hf_patched = True


def derive(P_):
    """The parameters with the garage laid on the unit's path: P["bay"] = (t0, t1, back wall s) round the spawn point
    and the line to the exit cell, and the hall widened where it has to enclose it."""
    P_ = dict(P_)
    cx = Ctx(None, P_, False, {})
    st, et = cx.ts(*SPAWN), cx.ts(*EXIT)
    h0, h1, hs0, hs1 = P_["hall"]
    hs0 = min(hs0, st[1] - 11.0)
    tc = st[0] + (et[0] - st[0]) * (hs0 - st[1]) / (et[1] - st[1])      # the path at the door
    tm = (st[0] + tc) / 2
    b0, b1, bs = tm - 14.2, tm + 14.2, st[1] + 13.6
    P_["bay"] = (b0, b1, bs)
    P_["hall"] = (min(h0, b0 - 4.0), max(h1, b1 + 1.5), hs0, max(hs1, bs + 4.0))
    return P_


class Ctx:
    """One build: the kit, the parameters, the three part lists and helpers in the building's own axes."""

    def __init__(self, K, P_, damaged, D):
        self.K, self.P, self.damaged, self.D = K, P_, damaged, D
        self.base, self.main, self.glow, self.anchors, self.roof_parts = [], [], [], [], []
        a = math.radians(P_["ang"])
        self.ca, self.sa = math.cos(a), math.sin(a)
        self.A = P_["ang"]

    def R(self, t, s, z=None):
        c = self.P["c"]
        p = (c[0] + self.ca * t - self.sa * s, c[1] + self.sa * t + self.ca * s)
        return p if z is None else (p[0], p[1], z)

    def ts(self, x, y):
        c = self.P["c"]
        dx, dy = x - c[0], y - c[1]
        return (dx * self.ca + dy * self.sa, -dx * self.sa + dy * self.ca)

    def tbox(self, name, t0, t1, s0, s1, z0, z1, material, inset=(0.0, 0.0, 0.0, 0.0), bev=0.0):
        """Box t0..t1 x s0..s1; inset = how far the top face is set in on the t0, t1, s0, s1 sides."""
        import bmesh
        bm = bmesh.new()
        lo = [(t0, s0), (t1, s0), (t1, s1), (t0, s1)]
        hi = [(t0 + inset[0], s0 + inset[2]), (t1 - inset[1], s0 + inset[2]), (t1 - inset[1], s1 - inset[3]), (t0 + inset[0], s1 - inset[3])]
        self.K.rings_solid(bm, [[self.R(t, s, z0) for t, s in lo], [self.R(t, s, z1) for t, s in hi]])
        return self.K.new_obj(name, bm, material, bev)

    def ob(self, name, t, s, hl, hw, z0, z1, material, bev=0.0, top=1.0, turn=0.0):
        return self.K.obox(name, self.R(t, s), self.A + turn, hl, hw, z0, z1, material, bev, top)

    def poly(self, name, pts_ts, z0, z1, material, bev=0.0):
        return self.K.prism(name, [self.R(t, s) for t, s in pts_ts], z0, z1, material, bev)

    def lit(self, o, material="GLOW_bay", rgb=LAMP, strength=2.5):
        o.data.materials.clear()
        o.data.materials.append(glow_material(material, rgb, strength))
        if not o.name.startswith("glow"):
            o.name = "glow_" + o.name
        self.glow.append(o)
        return o

    def window(self, name, t, s, hl, z0, z1, turn=0.0, proud=0.25):
        """A lit window on a wall face: centre (t, s) on the face, the wall runs along the long axis (+ turn)."""
        self.main.append(self.ob(name + "_f", t, s, hl + 0.45, proud, z0 - 0.45, z1 + 0.45, "recess", turn=turn))
        self.lit(self.ob(name, t, s, hl, proud + 0.12, z0, z1, "recess", turn=turn), "GLOW_window", WARM, 1.2)

    def house(self, o, k):
        return self.K.house(o, k)


DRESS = dict(podium="stone", podium_top="stone_dk", kerb="wall", slab="foot", apron="roof", apron_line="rib", lane=("HOUSE", 0.7),
             hall="wall", hall_rim="rim", hall_roof="block_dk", seam="recess", post="rim", post_cap="copper_dk",
             bay_wall="floor", bay_floor="concrete_dk", bay_ceil="recess", shutter="steel_dk", band=("HOUSE", 0.32),
             bastion="stone", bastion_top="block_dk", ring=("HOUSE", 0.3), slope_rib="rib", se_rib="copper_dk")


def _m(cx, o, role):
    """Give an object the material of a role (a name, or ("HOUSE", shade))."""
    m = cx.D[role]
    if isinstance(m, tuple):
        return cx.K.house(o, m[1])
    o.data.materials.clear()
    o.data.materials.append(cx.K.V3.mat(m))
    return o


def outline(P_, d=0.0, n=6):
    """The rounded diamond set in by d, counter-clockwise in (t, s): four arcs of n + 1 points - south (+t, -s),
    east (+t, +s), north (-t, +s), west (-t, -s)."""
    hl, hw, r = P_["hl"], P_["hw"], P_["round"]
    arcs = []
    for ct, cs, a0 in ((hl - r, -(hw - r), -90.0), (hl - r, hw - r, 0.0), (-(hl - r), hw - r, 90.0), (-(hl - r), -(hw - r), 180.0)):
        arcs.append([(ct + (r - d) * math.cos(math.radians(a0 + 90.0 * i / n)), cs + (r - d) * math.sin(math.radians(a0 + 90.0 * i / n))) for i in range(n + 1)])
    return arcs


def _lerp(p, q, u):
    return (p[0] + (q[0] - p[0]) * u, p[1] + (q[1] - p[1]) * u)


def _dense(p, q, step=2.0):
    """Points from p to q (both included)."""
    n = max(1, int(round(math.hypot(q[0] - p[0], q[1] - p[1]) / step)))
    return [_lerp(p, q, i / n) for i in range(n + 1)]


def free_wedges(cx):
    """inside(t, s) tests and apexes of the two free cells (0, 0) and (2, 0) in (t, s): parts above 2 px keep out."""
    ca, sa = cx.ca, cx.sa
    wt, ws = cx.ts(*cx.P["free_w"])
    et, es = cx.ts(*cx.P["free_e"])

    def in_w(t, s):          # west of and north of the corner
        dt, ds = t - wt, s - ws
        return dt * -ca + ds * sa > 0 and dt * sa + ds * ca > 0

    def in_e(t, s):          # east of and north of the corner
        dt, ds = t - et, s - es
        return dt * ca - ds * sa > 0 and dt * sa + ds * ca > 0
    return (in_w, (wt, ws)), (in_e, (et, es))


def cut_chain(lo, hi, inside, apex):
    """Cut the run of `lo` points that lies inside a wedge out of the two matching polylines: it becomes
    [crossing, apex, crossing]; the `hi` line gets the same parameters. Returns lo, hi, the cut-off triangle."""
    idx = [i for i, p in enumerate(lo) if inside(*p)]
    if not idx:
        return lo, hi, None
    i0, i1 = idx[0], idx[-1]

    def cross(pa, pb):       # pa outside, pb inside
        u0, u1 = 0.0, 1.0
        for _ in range(30):
            um = (u0 + u1) / 2
            if inside(*_lerp(pa, pb, um)):
                u1 = um
            else:
                u0 = um
        return u0
    ua, ub = cross(lo[i0 - 1], lo[i0]), cross(lo[i1 + 1], lo[i1])
    A, B = _lerp(lo[i0 - 1], lo[i0], ua), _lerp(lo[i1 + 1], lo[i1], ub)
    A2, B2 = _lerp(hi[i0 - 1], hi[i0], ua), _lerp(hi[i1 + 1], hi[i1], ub)
    return lo[:i0] + [A, apex, B] + lo[i1 + 1:], hi[:i0] + [A2, apex, B2] + hi[i1 + 1:], [A, apex, B]


def hall_pts(cx, grow=0.0, t1=None):
    """The hall's plan (t, s), grown by `grow`, up to t1 (default its south-east wall): a rectangle with a V-groove
    in its north-west wall where free cell (0, 0) reaches it."""
    h0, h1, hs0, hs1 = cx.P["hall"]
    h0, hs0, hs1 = h0 - grow, hs0 - grow, hs1 + grow
    h1 = h1 + grow if t1 is None else t1
    pts = [(h1, hs0), (h1, hs1), (h0, hs1), (h0, hs0)]
    ring = [p for a_, b_ in zip(pts, pts[1:] + pts[:1]) for p in _dense(a_, b_, 1.0)[:-1]]
    for inside, apex in free_wedges(cx):
        ring, _hi, _f = cut_chain(ring, ring, inside, apex)
    return ring


def podium(cx):
    """The three podium pieces round the slot (west, back, east), their kerbs, the low fill of the notches the free
    cells cut into the west and the back piece."""
    K, P_, main = cx.K, cx.P, cx.main
    ph, d = P_["ph"], P_["inset"]
    b0, b1, bs = P_["bay"]
    (in_w, ap_w), (in_e, ap_e) = free_wedges(cx)
    ch = {}
    for lvl, dd in (("lo", 0.0), ("hi", d)):
        S_, E_, N_, W_ = outline(P_, dd)
        hw = P_["hw"] - dd
        ch["w" + lvl] = _dense((b0, hw), N_[0])[:-1] + N_[:-1] + _dense(N_[-1], W_[0])[:-1] + W_ + [(b0, -hw)]
        ch["e" + lvl] = [(b1, -hw)] + S_ + E_ + [(b1, hw)]
        ch["b" + lvl] = [(b0, bs), (b1, bs)] + _dense((b1, hw), (b0, hw), 1.5)
    fills = []
    for k in "wb":
        for inside, apex in ((in_w, ap_w), (in_e, ap_e)):
            ch[k + "lo"], ch[k + "hi"], f = cut_chain(ch[k + "lo"], ch[k + "hi"], inside, apex)
            if f:
                fills.append(f)
    import bmesh
    for k in "wbe":
        bm = bmesh.new()
        K.rings_solid(bm, [[cx.R(t, s, 0.0) for t, s in ch[k + "lo"]], [cx.R(t, s, ph) for t, s in ch[k + "hi"]]])
        main.append(_m(cx, K.new_obj("podium_" + k, bm, "stone", 0.4), "podium"))
    for i, f in enumerate(fills):
        main.append(_m(cx, cx.poly(f"notch_fill{i}", f[::-1], 0.0, 1.8, "foot"), "slab"))
    j = 0
    for line in (ch["whi"], ch["ehi"]):
        for p, q in zip(line[:-1], line[1:]):
            if math.hypot(q[0] - p[0], q[1] - p[1]) < 0.05:
                continue
            main.append(_m(cx, K.beam(f"kerb{j}", cx.R(p[0], p[1], ph + 0.5), cx.R(q[0], q[1], ph + 0.5), 1.5, 2.2, "wall"), "kerb"))
            j += 1
    for i, t in enumerate((b0, b1)):                    # kerbs along the forecourt's two sides
        main.append(_m(cx, cx.tbox(f"kerb_in{i}", t - (1.3 if i == 0 else 0.2), t + (0.2 if i == 0 else 1.3), -P_["hw"] + d, P_["hall"][2], ph - 0.6, ph + 1.6, "wall"), "kerb"))


def layout(cx):
    """Podium, hall, garage, apron, gate - everything the four houses share. The house script adds the roof
    furniture, stacks and its own details, then calls finish()."""
    cx.P = derive(cx.P)
    K, P_, main, base = cx.K, cx.P, cx.main, cx.base
    hl, hw, ph, d = P_["hl"], P_["hw"], P_["ph"], P_["inset"]
    h0, h1, hs0, hs1 = P_["hall"]
    b0, b1, bs = P_["bay"]
    bh, H = P_["bay_h"], P_["hall_h"]
    # ---- the foundation slab (low: may lie in the free cells) and the podium
    S_, E_, N_, W_ = outline(P_, 0.0)
    main.append(_m(cx, cx.poly("slab", S_ + E_ + N_ + W_, 0.0, 0.25, "foot"), "slab"))
    podium(cx)
    # ribs down the two slopes that face the camera
    sl = hw - P_["round"]
    for i in range(5):
        s_ = -sl + 2.0 + (2 * sl - 4.0) * i / 4
        main.append(_m(cx, K.beam(f"se_rib{i}", cx.R(hl + 0.2, s_, 0.5), cx.R(hl - d + 0.2, s_, ph + 0.2), 1.4, 0.9, "rib", 0.1), "se_rib"))
    tl = hl - P_["round"]
    for i, t in enumerate([b1 + 3.5 + (tl - b1 - 3.5) * k / 3 for k in range(4)] + [b0 - 3.5 - (tl + b0 - 3.5) * k / 2 for k in range(3)]):
        main.append(_m(cx, K.beam(f"sw_rib{i}", cx.R(t, -hw - 0.2, 0.5), cx.R(t, -hw + d - 0.2, ph + 0.2), 1.1, 0.8, "rib", 0.1), "slope_rib"))
    # ---- the hall: lower walls round the garage, the solid upper part
    for nm, t0, t1, s0, s1 in (("hall_ne", b0, h1, bs, hs1), ("hall_se", b1, h1, hs0, bs)):
        main.append(_m(cx, cx.tbox(nm, t0, t1, s0, s1, ph - 0.3, bh + 0.2, "wall"), "hall"))
    main.append(_m(cx, cx.poly("hall_nw", hall_pts(cx, 0.0, b0), ph - 0.3, bh + 0.2, "wall"), "hall"))
    main.append(_m(cx, cx.poly("hall_up", hall_pts(cx), bh, H, "wall", 0.4), "hall"))
    # near-black panel seams on the two walls that face the camera
    for i in range(6):
        t = h0 + (h1 - h0) * (i + 0.5) / 6
        main.append(_m(cx, K.beam(f"hall_seam_sw{i}", cx.R(t, hs0 - 0.12, bh + 5.6), cx.R(t, hs0 - 0.12, H - 1.6), 0.35, 0.3, "recess"), "seam"))
    for i in range(5):
        s_ = hs0 + (hs1 - hs0) * (i + 0.5) / 5
        main.append(_m(cx, K.beam(f"hall_seam_se{i}", cx.R(h1 + 0.12, s_, ph + 1.0), cx.R(h1 + 0.12, s_, H - 1.6), 0.35, 0.3, "recess"), "seam"))
    # ---- the garage: floor, inner walls, ceiling (base node: under units), the turntable ring under the spawn point
    base.append(_m(cx, cx.tbox("bay_floor", b0, b1, hs0 + 0.5, bs, 0.0, 0.38, "floor"), "bay_floor"))
    base.append(_m(cx, cx.tbox("bay_back", b0, b1, bs - 0.4, bs + 0.1, 0.3, bh, "floor"), "bay_wall"))
    base.append(_m(cx, cx.tbox("bay_side_w", b0 - 0.1, b0 + 0.4, hs0 + 1.4, bs, 0.3, bh, "floor"), "bay_wall"))
    base.append(_m(cx, cx.tbox("bay_side_e", b1 - 0.4, b1 + 0.1, hs0 + 1.4, bs, 0.3, bh, "floor"), "bay_wall"))
    main.append(_m(cx, cx.tbox("bay_ceil", b0, b1, hs0 + 1.4, bs, bh - 0.5, bh + 0.05, "recess"), "bay_ceil"))
    for i in range(4):                                       # ribs on the back wall
        t = b0 + (b1 - b0) * (i + 0.5) / 4
        base.append(_m(cx, cx.tbox(f"bay_rib{i}", t - 0.5, t + 0.5, bs - 0.9, bs - 0.3, 0.3, bh - 0.5, "steel_dk"), "shutter"))
    st = cx.ts(*SPAWN)
    base.append(K.cyl("bay_table", cx.R(*st), 10.5, 0.3, 0.5, "steel_dk", 32))
    base.append(_m(cx, K.lathe_loop("bay_ring", cx.R(*st), [(10.2, 0.45), (10.2, 0.72), (8.2, 0.72), (8.2, 0.45)], "HOUSE", 32), "ring"))
    # ---- the apron: the pale ramp slab through the forecourt out onto exit cell (0, 2), lane marks
    ae, tp = P_["apron_end"], P_["apron_taper"]
    base.append(_m(cx, cx.poly("apron", [(b0, hs0 + 0.5), (b0, -hw), (b0 + tp, ae), (b1 - tp, ae), (b1, -hw), (b1, hs0 + 0.5)], 0.0, 0.5, "roof", 0.12), "apron"))
    for i, t in enumerate((b0 + 3.2, b1 - 3.2)):
        base.append(_m(cx, cx.tbox(f"apron_lane{i}", t - 1.3, t + 1.3, ae + 3.0, hs0 - 1.0, 0.45, 0.62, "HOUSE"), "lane"))
    for i in range(int((hs0 - 3.0 - (ae + 2.5)) // 6.0) + 1):
        s_ = hs0 - 3.0 - 6.0 * i
        base.append(_m(cx, cx.tbox(f"apron_joint{i}", b0 + 5.5, b1 - 5.5, s_ - 0.2, s_ + 0.2, 0.42, 0.58, "rib"), "apron_line"))
    # ---- the gate: brown posts at the forecourt mouth and the door jambs, the lintel with the rolled shutter
    for i, t in enumerate((b0 - 1.1, b1 + 1.1)):
        main.append(_m(cx, cx.ob(f"gate_post{i}", t, -hw + d + 0.6, 1.5, 1.5, 0.0, ph + 3.6, "rim", 0.25), "post"))
        main.append(_m(cx, cx.ob(f"gate_post{i}_cap", t, -hw + d + 0.6, 1.9, 1.9, ph + 3.2, ph + 4.2, "copper_dk", 0.2), "post_cap"))
        main.append(_m(cx, cx.ob(f"door_jamb{i}", t + (0.5 if i == 0 else -0.5), hs0 - 0.2, 1.0, 0.9, 0.0, bh + 1.0, "rim", 0.15), "post"))
    main.append(_m(cx, cx.tbox("door_lintel", b0 - 1.6, b1 + 1.6, hs0 - 1.1, hs0 + 0.2, bh, bh + 2.0, "rim", bev=0.15), "post"))
    main.append(_m(cx, K.tube("door_shutter", cx.R(b0 + 0.6, hs0 + 0.6, bh - 1.3), cx.R(b1 - 0.6, hs0 + 0.6, bh - 1.3), 1.25, "steel_dk", 10), "shutter"))
    main.append(_m(cx, cx.tbox("door_band", b0 - 0.6, b1 + 0.6, hs0 - 0.5, hs0 + 0.1, bh + 2.6, bh + 5.2, "HOUSE"), "band"))
    # ---- light: ceiling strips and jamb lamps in the bay (emissive, anchors on them)
    for i, t in enumerate((b0 + 7.0, b1 - 7.0)):
        cx.lit(cx.tbox(f"bay_strip{i}", t - 0.7, t + 0.7, hs0 + 4.0, bs - 3.0, bh - 0.75, bh - 0.45, "recess"))
        cx.anchors.append((f"fx_light_{i}", cx.R(t, (hs0 + bs) / 2, bh - 0.75), 1.0))
    # working flashes (render/weld_fx.hpp, replace the original welding overlay): just inside the door - the only
    # part of the bay the 45-degree view sees; (fraction across the opening, px behind the door plane, height, size)
    for i, (f, ds, z, sz) in enumerate(((0.15, 3.0, 10.5, 1.0), (0.55, 4.5, 7.0, 0.9), (0.9, 2.5, 11.5, 1.0), (0.35, 6.0, 3.5, 0.8))):
        cx.anchors.append((f"fx_weld_{i}", cx.R(b0 + 3.0 + f * (b1 - b0 - 12.0), hs0 + ds, z), sz))
    for i, t in enumerate((b0 + 0.55, b1 - 0.55)):
        cx.lit(cx.tbox(f"bay_lamp{i}", t - 0.3, t + 0.3, hs0 + 2.2, hs0 + 4.4, bh - 5.0, bh - 3.6, "recess"))
    return cx


def bastion(cx, r=6.4):
    """The round drum on the podium's west corner; returns its top centre (x, y, z)."""
    K, P_ = cx.K, cx.P
    ph = P_["ph"]
    c = cx.R(-(P_["hl"] - P_["round"]) + 0.5, -(P_["hw"] - P_["round"]) + 0.5)
    cx.main.append(_m(cx, K.cyl("bastion", c, r, ph - 0.4, ph + 1.6, "stone", 24, r - 0.5), "bastion"))
    cx.main.append(_m(cx, K.cyl("bastion_top", c, r - 1.0, ph + 1.4, ph + 2.2, "block_dk", 24), "bastion_top"))
    return (c[0], c[1], ph + 2.0)


def flat_roof(cx):
    """Rim and dark roof deck of the hall; returns the roof height."""
    h0, h1, hs0, hs1 = cx.P["hall"]
    H = cx.P["hall_h"]
    cx.main.append(_m(cx, cx.poly("hall_rim", hall_pts(cx, 0.5), H - 1.4, H + 0.6, "rim", 0.25), "hall_rim"))
    o = _m(cx, cx.poly("hall_roof", hall_pts(cx, -1.2), H, H + 1.0, "block_dk", 0.2), "hall_roof")
    cx.main.append(o)
    cx.roof_parts.append(o)
    return H + 1.0


def _clip(pts, sm, low):
    """The part of polygon pts (t, s) with s <= sm (low) or s >= sm."""
    out = []
    for p, q in zip(pts, pts[1:] + pts[:1]):
        pin, qin = (p[1] <= sm) == low or p[1] == sm, (q[1] <= sm) == low or q[1] == sm
        if pin:
            out.append(p)
        if pin != qin:
            out.append(_lerp(p, q, (sm - p[1]) / (q[1] - p[1])))
    return out


def gable_roof(cx, rise=7.0, strips=("bronze", "skirt"), deck="roof2", ridge="wall"):
    """A gabled roof along the long axis (the Mercenary shed), on the hall's (grooved) plan; returns zf(x, y) = the
    roof surface height."""
    import bmesh
    K = cx.K
    h0, h1, hs0, hs1 = cx.P["hall"]
    H = cx.P["hall_h"]
    sm, hwd = (hs0 + hs1) / 2, (hs1 - hs0) / 2 + 0.8
    wedges = free_wedges(cx)

    def zt(s_):
        return H + 0.2 + rise * (1 - min(1.0, abs(s_ - sm) / hwd))

    def free(t, s_):
        return any(inside(t, s_) for inside, _a in wedges)
    plan = hall_pts(cx, 0.8)
    for sg, low in ((-1, True), (1, False)):
        half = _clip(plan, sm, low)
        bm = bmesh.new()
        K.rings_solid(bm, [[cx.R(t, s_, H - 0.6) for t, s_ in half], [cx.R(t, s_, zt(s_)) for t, s_ in half]])
        o = K.new_obj(f"shed_roof{sg}", bm, deck)
        cx.main.append(o)
        cx.roof_parts.append(o)
        n = 12
        for i in range(n):
            t = h0 + (h1 - h0) * i / (n - 1)
            if free(t, sm + sg * (hwd - 0.2)) or free(t, sm) or free(t, sm + sg * hwd * 0.5):
                continue
            cx.main.append(K.beam(f"shed_strip{sg}_{i}", cx.R(t, sm + sg * (hwd - 0.2), H + 0.5), cx.R(t, sm, H + rise + 0.3), 1.6, 0.5, strips[i % 2]))
    t = h0 - 0.8
    while free(t - 1.0, sm) and t < h1:
        t += 0.5
    cx.main.append(K.beam("shed_ridge", cx.R(t, sm, H + rise + 0.4), cx.R(h1 + 0.8, sm, H + rise + 0.4), 1.6, 0.9, ridge))

    def zf(x, y):
        _t, s_ = cx.ts(x, y)
        return zt(s_)
    return zf


def finish(cx, anim="keep", **extra):
    """The build() result + what the spec needs."""
    nodes = dict(base=cx.base, main=cx.main)
    if cx.glow:
        nodes["glow"] = cx.glow
    return dict(nodes=nodes, anchors=cx.anchors)


def ini_lines(anchors):
    """models.ini lines for the anchors (glb axes: tiles; x east, y up, z south)."""
    return [f"anchor.{n} = {p[0] / 32:.4f} {p[2] / 32:.4f} {-p[1] / 32:.4f} {sz:g}" for n, p, sz in anchors]


def facade(cx, panel_k=0.3, vent=True):
    """House panels and lit windows on the hall's south-east wall, lit windows (and a vent) over the bay door."""
    P_ = cx.P
    h0, h1, hs0, hs1 = P_["hall"]
    b0, b1, _bs = P_["bay"]
    ph, H, bh = P_["ph"], P_["hall_h"], P_["bay_h"]
    n = max(2, int((hs1 - hs0 - 6.0) // 8.0) + 1)
    for i in range(n):
        s_ = hs0 + 5.0 + 8.0 * i
        cx.main.append(cx.K.house(cx.ob(f"hall_panel{i}", h1 + 0.25, s_, 2.6, 0.4, ph + 3.5, H - 5.5, "HOUSE", turn=90.0), panel_k))
        if i + 1 < n:
            cx.window(f"win_se{i}", h1 + 0.2, s_ + 4.0, 0.9, H - 10.5, H - 6.5, turn=90.0)
    for i, t in enumerate([b0 + 3.5 + (b1 - b0 - 7.0) * k / 3 for k in range(4)]):
        cx.window(f"win_sw{i}", t, hs0 - 0.2, 1.7, bh + 8.0, min(bh + 10.6, H - 3.0))
    if vent and b0 - h0 > 7.0:
        cx.main.extend(cx.K.vent("hall_vent", cx.R((h0 + b0) / 2, hs0 - 0.05), cx.A, 2.4, bh + 2.0, bh + 9.0))

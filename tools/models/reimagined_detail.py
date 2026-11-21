"""Reimagined models, round 2: REAL GEOMETRY detail (docs/3d-models.md "Reimagined models").

Round 1 put the detail into normal maps (panel lines from a lattice of planes, rivets, dents): crisp close up, but
at normal RTS zoom most of it vanished and the lattice drew seams in arbitrary places (a grid across the
refinery's yellow pad). Round 2 adds modelled detail to the approved designs instead, generated from the design's
own parts so that it follows them:

  * armour plates: every large flat painted face of a part is covered by raised plates with a chamfered edge
    (they catch the light and cast a hairline shadow), laid out along the face's own edges (walls: horizontal and
    vertical joints; roofs / decks: along the longest edge), with real gaps between them - the panel lines are
    those gaps now, so they only exist on large flat plates and always align with the part; never on pads, glass,
    lights, house-colour parts, decals, floors or anything near the ground;
  * bolt heads at the corners of the bigger plates;
  * buildings: a cornice ledge along the free top edge of walls and a plinth along their foot (deep shadow lines
    that read at normal zoom), louvred vents in some wall panels, railings along the free edges of high roofs,
    roof hatches / vent boxes, a down pipe on tall walls, an antenna mast on the highest roof;
  * every placement is tested against the other parts of the model (a BVH of the whole node): nothing is put
    where another part (a band, a pipe, a window, a door, a lamp) touches the face, so the design's own details
    stay on top;
  * deterministic (hashes of positions, mirrored vehicles hash |x| so both sides match) and within a triangle
    budget (largest faces first).

How: `install(profile, budget)` wraps combat_tank_v3.finish_node - every approved generator (the vehicle kit's
node(), the building kit's assemble(), the refinery pilot, the v3 tank) joins its parts through it - and adds the
detail parts (one object per material, named rd_*) to the node's parts before the join. Detail parts copy the
d2k_group of the part they sit on (the ornithopter's wings), so pose groups keep working. Nothing else changes:
node names, origins, anchors and the parts of the design are the generator's.

Everything here is authored in original sprite pixels (1 px = 1/32 tile; z up), like the generators.
"""
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import combat_tank_v3 as V3  # noqa: E402

log = V3.log

# Profiles (px). panel = target plate size, gap = joint width, t = plate thickness, ch = top chamfer,
# margin = distance kept from the face's edges (more than the host's bevel), min_area / min_dim = smallest face.
PROFILES = dict(
    building=dict(panel=6.5, aspect=2.2, gap=1.0, t=0.7, ch=0.45, margin=0.8, min_area=12.0, min_dim=3.0, low_z=3.5,
                  siding=0.42, framed=0.2, rib_pitch=1.5, roof_trim=(0.8, 0.55),
                  bolt=7.0, bolt_r=0.32, bolt_h=0.26, keep=0.9, ledge=(0.85, 1.1), plinth=(0.6, 1.4), vents=0.16, max_vents=8,
                  rails=True, rail_z=10.0, rail_area=110.0, rail_h=2.4, rail_r=0.2, hatches=14, pipes=2, antenna=True,
                  clear=0.3, kinds=("paint", "steel")),
    vehicle=dict(panel=4.6, aspect=2.4, gap=0.45, t=0.3, ch=0.2, margin=0.4, min_area=7.0, min_dim=2.2, low_z=1.6,
                 bolt=3.6, bolt_r=0.16, bolt_h=0.13, keep=1.0, ledge=None, plinth=None, vents=0.0, max_vents=0,
                 rails=False, hatches=0, pipes=0, antenna=False, clear=0.18, mirror=True),
)
# Materials never plated (pads, aprons, floors, glass, lights, decals, rubber, dark parts...) besides HOUSE*, the
# emissive ones and every non-"paint" kind.
EXCLUDE = {"pad", "pad_dk", "pad_rim", "slab", "slab_dk", "floor", "hf_apron", "apron", "glass", "cyan", "glow", "glow_dk",
           "lamp", "red", "green", "recess", "burnt", "soot", "rubble", "rock", "rock_dk", "foot", "neck", "arch_in", "tread",
           "link", "tyre", "tyre_lt", "concrete", "concrete_dk", "tur_core", "core", "sill", "cap", "cap_hi"}
# Extra materials the detail uses (added to the palette when a generator lacks them)
EXTRA_COL = dict(rd_steel=(0.420, 0.410, 0.390), rd_dark=(0.120, 0.112, 0.100), rd_rail=(0.300, 0.290, 0.275),
                 rd_copper=(0.560, 0.350, 0.170), rd_vent=(0.070, 0.066, 0.060))
EXTRA_SPEC = dict(rd_steel=(0.6, 0.42, "steel"), rd_dark=(0.2, 0.7, "dark"), rd_rail=(0.6, 0.45, "steel"),
                  rd_copper=(0.5, 0.4, "steel"), rd_vent=(0.0, 0.9, "dark"))

STATE = dict(kinds=("paint",), profile=None, budget=0, used=0, nodes={}, skip_nodes=set(), only_nodes=None, extras=None)
_orig_finish = V3.finish_node


def install(profile, budget, skip_nodes=(), only_nodes=None, extras=None, **over):
    """Wrap V3.finish_node: detail on every node built from now on. budget = triangles the detail may add in all.
    extras(parts, node_name) -> extra hand-placed parts (added before the generic pass, so they are respected)."""
    p = dict(PROFILES[profile])
    p.update(over)
    STATE.update(profile=p, budget=budget, used=0, nodes={}, skip_nodes=set(skip_nodes), only_nodes=only_nodes, extras=extras,
                 kinds=p.get("kinds", ("paint",)))
    for k, v in EXTRA_COL.items():
        V3.COL.setdefault(k, v)
        V3.SPEC.setdefault(k, EXTRA_SPEC[k])
    V3.UV_SCALE.update(rd_vent=0.3, rd_dark=0.3, rd_rail=0.3)
    V3.finish_node = _finish


def uninstall():
    V3.finish_node = _orig_finish


def _finish(objs, name, origin_px, offset_px=(0.0, 0.0, 0.0)):
    objs = list(objs)
    if STATE["extras"] is not None:
        objs += STATE["extras"](objs, name) or []
    if name not in STATE["skip_nodes"] and (STATE["only_nodes"] is None or name in STATE["only_nodes"]):
        left = STATE["budget"] - STATE["used"]
        if left > 200:
            added, tris = detail(objs, STATE["profile"], left, name)
            STATE["used"] += tris
            STATE["nodes"][name] = tris
            objs += added
    return _orig_finish(objs, name, origin_px, offset_px)


# ------------------------------------------------------------------------------------------------
# helpers


def _h(*vals):
    """Deterministic hash of numbers -> 0..1."""
    h = 2166136261
    for v in vals:
        k = int(round(v * 8.0)) & 0xFFFFFFFF
        h = ((h ^ k) * 16777619) & 0xFFFFFFFF
        h ^= h >> 13
        h = (h * 0x5bd1e995) & 0xFFFFFFFF
    h ^= h >> 15
    return (h & 0xFFFFFF) / float(0xFFFFFF)


def _mat_name(m):
    return m.name.split(".")[0] if m is not None else ""


def _platable(name):
    if not name or name.startswith("HOUSE") or name in EXCLUDE or name.startswith("rd_"):
        return False
    if name in V3.V2.EMIT:
        return False
    return V3.SPEC.get(name, (0, 0, "dark"))[2] in STATE.get("kinds", ("paint",))


class Geo:
    """Detail geometry, one bmesh per (material, group)."""

    def __init__(self):
        self.bms = {}
        self.tris = 0

    def bm(self, material, group=None):
        k = (material, group)
        if k not in self.bms:
            self.bms[k] = bmesh.new()
        return self.bms[k]

    def frustum(self, material, c, e1, e2, n, hw, hh, t, ch, base=-0.06, group=None, bottom=False):
        """A plate on a face: rectangle (half sizes hw x hh along e1 / e2) on the plane through c (normal n), raised
        to t with the top edges chamfered by ch. 10 triangles (12 with the bottom)."""
        bm = self.bm(material, group)
        cw, chh = max(hw - ch, hw * 0.25), max(hh - ch, hh * 0.25)
        lo = [c + n * base + e1 * sx * hw + e2 * sy * hh for (sx, sy) in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        hi = [c + n * t + e1 * sx * cw + e2 * sy * chh for (sx, sy) in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        vl = [bm.verts.new(p) for p in lo]
        vh = [bm.verts.new(p) for p in hi]
        centre = c + n * (t * 0.5)
        faces = [[vh[0], vh[1], vh[2], vh[3]]]
        for i in range(4):
            j = (i + 1) % 4
            faces.append([vl[i], vl[j], vh[j], vh[i]])
        if bottom:
            faces.append([vl[3], vl[2], vl[1], vl[0]])
        for f in faces:
            fc = bm.faces.new(f)
            fc.normal_update()
            if fc.normal.dot(fc.calc_center_median() - centre) < 0:
                fc.normal_flip()
        self.tris += 2 * len(faces)

    def box(self, material, c, e1, e2, n, hw, hh, h0, h1, group=None):
        """A closed box over the face rectangle from h0 to h1 along n."""
        self.frustum(material, c + n * h0, e1, e2, n, hw, hh, h1 - h0, 0.0, base=0.0, group=group, bottom=True)

    def tube(self, material, a, b, r, segs=6, group=None):
        bm = self.bm(material, group)
        a, b = Vector(a), Vector(b)
        d = (b - a).normalized()
        s = d.orthogonal().normalized()
        t = d.cross(s)
        r0 = [bm.verts.new(a + (s * math.cos(2 * math.pi * i / segs) + t * math.sin(2 * math.pi * i / segs)) * r) for i in range(segs)]
        r1 = [bm.verts.new(b + (s * math.cos(2 * math.pi * i / segs) + t * math.sin(2 * math.pi * i / segs)) * r) for i in range(segs)]
        mid = (a + b) * 0.5
        for i in range(segs):
            j = (i + 1) % segs
            f = bm.faces.new([r0[i], r0[j], r1[j], r1[i]])
            f.normal_update()
            if f.normal.dot(f.calc_center_median() - mid) < 0:
                f.normal_flip()
        for ring, end in ((r0, a), (r1, b)):
            f = bm.faces.new(ring)
            f.normal_update()
            if f.normal.dot(end - mid) < 0:
                f.normal_flip()
        self.tris += segs * 2 + 2 * (segs - 2)

    def objects(self):
        out = []
        for (material, group), bm in self.bms.items():
            if not bm.verts:
                bm.free()
                continue
            o = V3.new_obj(f"rd_{material}", bm, material)
            if group:
                o["d2k_group"] = group
                o.vertex_groups.new(name=group).add(list(range(len(o.data.vertices))), 1.0, "REPLACE")
            out.append(o)
        self.bms = {}
        return out


def _eval_tris(objs):
    deps = bpy.context.evaluated_depsgraph_get()
    n = 0
    for o in objs:
        me = o.evaluated_get(deps).data
        n += sum(len(p.vertices) - 2 for p in me.polygons)
    return n


def _bvh(objs):
    """One BVH of every part's base mesh (world px) + polygon -> part index."""
    verts, polys, owner = [], [], []
    for k, o in enumerate(objs):
        mw = o.matrix_world
        base = len(verts)
        verts.extend(mw @ v.co for v in o.data.vertices)
        for p in o.data.polygons:
            polys.append([base + i for i in p.vertices])
            owner.append(k)
    return BVHTree.FromPolygons(verts, polys, all_triangles=False, epsilon=0.0), owner


def _free(bvh, owner, me_k, pts, dist):
    """True when no OTHER part's surface lies within dist of any point."""
    for p in pts:
        for (_loc, _nrm, idx, _d) in bvh.find_nearest_range(p, dist):
            if idx is not None and owner[idx] != me_k:
                return False
    return True


def _buried(bvh, owner, me_k, p, n):
    """True when p (on a face of part me_k, normal n) lies inside another part: the first surface hit outwards
    along n belongs to another part and faces away (its inside)."""
    loc, nrm, idx, _d = bvh.ray_cast(p + n * 0.03, n, 80.0)
    return idx is not None and owner[idx] != me_k and nrm.dot(n) > 0.2


def _inside_poly(poly, x, y):
    c = False
    n = len(poly)
    for i in range(n):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % n]
        if (y0 > y) != (y1 > y) and x < x0 + (x1 - x0) * (y - y0) / (y1 - y0):
            c = not c
    return c


def _rect_in(poly, cx, cy, hw, hh):
    return all(_inside_poly(poly, cx + sx * hw, cy + sy * hh) for sx in (-1, 1) for sy in (-1, 1))


def _face_frame(f, mw, me):
    """(centre, n, e1, e2, 2D polygon, (cx, cy, hw, hh) of a large rectangle inside it) of a planar face, or None."""
    n = (mw.to_3x3() @ f.normal).normalized()
    pts = [mw @ me.vertices[i].co for i in f.vertices]
    c0 = sum(pts, Vector()) / len(pts)
    if any(abs((p - c0).dot(n)) > 0.03 for p in pts):
        return None
    if abs(n.z) < 0.92:                     # walls and slopes: joints horizontal / along the fall line
        e1 = n.cross(Vector((0, 0, 1))).normalized()
    else:                                   # roofs, decks: along the longest edge
        best, e1 = -1.0, None
        for i in range(len(pts)):
            d = pts[(i + 1) % len(pts)] - pts[i]
            d = d - n * d.dot(n)
            if d.length > best:
                best, e1 = d.length, d.normalized()
        if e1 is None or best < 1e-6:
            return None
    e2 = n.cross(e1).normalized()
    if abs(n.z) < 0.92 and e2.z < 0:
        e2 = -e2
    poly = [((p - c0).dot(e1), (p - c0).dot(e2)) for p in pts]
    xs, ys = [q[0] for q in poly], [q[1] for q in poly]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    hw, hh = (max(xs) - min(xs)) / 2, (max(ys) - min(ys)) / 2
    k = 1.0
    while k > 0.3 and not _rect_in(poly, cx, cy, hw * k, hh * k):
        k -= 0.04
    if k <= 0.3:
        return None
    return c0, n, e1, e2, poly, (cx, cy, hw * k, hh * k)


# ------------------------------------------------------------------------------------------------
# The pass


def detail(objs, P, budget, node_name=""):
    """-> (new part objects, triangles added)."""
    bpy.context.view_layer.update()
    objs = [o for o in objs if o is not None and o.type == "MESH"]
    if not objs:
        return [], 0
    bvh, owner = _bvh(objs)
    mirror = P.get("mirror", False)
    faces = []
    for k, o in enumerate(objs):
        if o.name.startswith(("rd_", "cutter")) or o.hide_render:
            continue
        mw = o.matrix_world
        mats = o.data.materials
        bev = 0.0
        for m in o.modifiers:
            if m.type == "BEVEL":
                bev = max(bev, m.width)
        for f in o.data.polygons:
            if f.area < P["min_area"] or len(f.vertices) > 8:
                continue
            mname = _mat_name(mats[f.material_index]) if f.material_index < len(mats) else ""
            if not _platable(mname):
                continue
            fr = _face_frame(f, mw, o.data)
            if fr is None:
                continue
            c0, n, e1, e2, poly, rect = fr
            if n.z < -0.3:
                continue
            if n.z > 0.92 and c0.z < P["low_z"]:
                continue
            if min(rect[2], rect[3]) * 2 < P["min_dim"]:
                continue
            faces.append((f.area, k, o, mname, c0, n, e1, e2, rect, bev))
    faces.sort(key=lambda r: -r[0])
    geo = Geo()
    stats = dict(plates=0, bolts=0, ledges=0, vents=0, rails=0, hatches=0, pipes=0, antenna=0)
    clear = P["clear"]
    vents = 0
    roofs = []
    walls = []
    for (area, k, o, mname, c0, n, e1, e2, rect, bev) in faces:
        if geo.tris > budget - 60:
            break
        group = o.get("d2k_group")
        cx, cy, hw, hh = rect
        c = c0 + e1 * cx + e2 * cy
        m = max(P["margin"], bev * 1.25)
        x0, x1, y0, y1 = -hw + m, hw - m, -hh + m, hh - m
        wall = abs(n.z) < 0.35
        if wall:
            walls.append((k, o, mname, c, n, e1, e2, hw, hh, group))
        elif n.z > 0.92:
            roofs.append((k, o, mname, c, n, e1, e2, hw, hh, group, area))
        # building walls: a cornice ledge along a free top edge, a plinth along the foot
        if wall and P.get("ledge") and hh * 2 >= 6.0:
            lt, lh = P["ledge"]
            yt = hh - lh * 0.5 - 0.1
            pc = c + e2 * yt
            probe = [pc + n * (lt + clear) + e1 * s * (hw - 0.5) for s in (-1, 0, 1)] + [c + e2 * (hh + 0.6) + n * 0.3]
            if _free(bvh, owner, k, probe[:3], clear + 0.15) and _free(bvh, owner, k, probe[3:], 0.45):
                geo.frustum(mname, pc, e1, e2, n, hw + lt * 0.9, lh * 0.5, lt, lt * 0.55, group=group)
                stats["ledges"] += 1
                y1 = min(y1, yt - lh * 0.5 - 0.4)
            zb = (c - e2 * hh).z
            if P.get("plinth") and zb < 1.5:
                pt, ph = P["plinth"]
                pc = c - e2 * (hh - ph * 0.5)
                probe = [pc + n * (pt + clear) + e1 * s * (hw - 0.5) for s in (-1, 0, 1)]
                if _free(bvh, owner, k, probe, clear + 0.15):
                    geo.frustum(mname, pc, e1, e2, n, hw + pt * 0.9, ph * 0.5, pt, pt * 0.5, group=group)
                    stats["ledges"] += 1
                    y0 = max(y0, -hh + ph + 0.4)
        # shrink the rectangle away from other parts touching the face (frames, bands, beams round a panel)
        tt = P["t"] + 0.05
        rad = clear + P["t"] * 0.5
        for _it in range(40):
            if x1 - x0 < P["min_dim"] * 0.8 or y1 - y0 < P["min_dim"] * 0.6:
                break
            moved = False
            for side in range(4):
                if side < 2:
                    xs = x0 if side == 0 else x1
                    pts = [c + n * tt + e1 * xs + e2 * (y0 + (y1 - y0) * q / 4) for q in range(5)]
                else:
                    ys = y0 if side == 2 else y1
                    pts = [c + n * tt + e2 * ys + e1 * (x0 + (x1 - x0) * q / 4) for q in range(5)]
                if not _free(bvh, owner, k, pts, rad):
                    if side == 0:
                        x0 += 0.5
                    elif side == 1:
                        x1 -= 0.5
                    elif side == 2:
                        y0 += 0.5
                    else:
                        y1 -= 0.5
                    moved = True
            if not moved:
                break
        W, H = x1 - x0, y1 - y0
        if W < P["min_dim"] * 0.8 or H < P["min_dim"] * 0.6:
            continue
        # building walls: ribbed siding or a framed panel instead of plates on some parts (one look per part)
        if wall and P.get("siding"):
            pcen = o.get("rd_hash")
            if pcen is None:
                bb = [o.matrix_world @ Vector(v) for v in o.bound_box]
                cc = sum(bb, Vector()) / 8
                pcen = _h(cc.x, cc.y, cc.z, 17)
                o["rd_hash"] = pcen
            if pcen < P["siding"] and W > 4.0 and H > 3.0:
                if _siding(geo, bvh, owner, k, mname, c, e1, e2, n, x0, x1, y0, y1, P, group):
                    stats["siding"] = stats.get("siding", 0) + 1
                    continue
            elif pcen < P["siding"] + P["framed"] and W > 4.0 and H > 4.0:
                if _framed(geo, bvh, owner, k, mname, c, e1, e2, n, x0, x1, y0, y1, P, group):
                    stats["framed"] = stats.get("framed", 0) + 1
                    continue
        # roofs of buildings: a raised trim round the free edges
        if n.z > 0.92 and P.get("roof_trim") and c.z > P["low_z"] + 2.0 and W > 5.0 and H > 5.0:
            tw, tt = P["roof_trim"]
            mid = c + e1 * ((x0 + x1) / 2) + e2 * ((y0 + y1) / 2)
            done = 0
            for (ax, al, half_out, half_al) in ((e1, e2, W / 2, H / 2), (-e1, e2, W / 2, H / 2), (e2, e1, H / 2, W / 2), (-e2, e1, H / 2, W / 2)):
                pc = mid + ax * (half_out - tw / 2)
                probe = [pc + n * (tt + 0.1) + al * q * half_al for q in (-1, -0.5, 0, 0.5, 1)]
                if _free(bvh, owner, k, probe, clear + 0.1):
                    geo.frustum(mname, pc, al, ax, n, half_al, tw / 2, tt, tt * 0.45, group=group)
                    done += 1
            if done:
                stats["trims"] = stats.get("trims", 0) + done
                x0, x1, y0, y1 = x0 + tw + 0.3, x1 - tw - 0.3, y0 + tw + 0.3, y1 - tw - 0.3
                W, H = x1 - x0, y1 - y0
                if W < P["min_dim"] or H < P["min_dim"]:
                    continue
        # plates: a grid along e1 / e2, no plate longer than aspect x its width
        n1 = max(1, int(round(W / P["panel"])))
        n2 = max(1, int(round(H / P["panel"])))
        while W / n1 > P["aspect"] * H / n2 and n1 < 12:
            n1 += 1
        while H / n2 > P["aspect"] * W / n1 and n2 < 12:
            n2 += 1
        g = P["gap"]
        pw, ph = W / n1, H / n2
        for i in range(n1):
            for j in range(n2):
                if geo.tris > budget - 30:
                    break
                px = x0 + pw * (i + 0.5)
                py = y0 + ph * (j + 0.5)
                pc = c + e1 * px + e2 * py
                hx = (abs(pc.x) if mirror else pc.x)
                if _h(hx, pc.y, pc.z, 3) > P["keep"]:
                    continue
                phw, phh = pw / 2 - g / 2, ph / 2 - g / 2
                if phw < 0.6 or phh < 0.6:
                    continue
                t = P["t"] * (0.85 + 0.3 * _h(hx, pc.y, pc.z, 5))
                probe = [pc + n * (t + 0.05) + e1 * sx * phw + e2 * sy * phh for sx in (-1, 0, 1) for sy in (-1, 0, 1)]
                if not _free(bvh, owner, k, probe, clear + t * 0.5) or _buried(bvh, owner, k, pc, n):
                    continue
                # a louvred vent instead of a plate (building walls)
                if wall and vents < P["max_vents"] and phw > 2.0 and phh > 1.6 and _h(hx, pc.y, pc.z, 11) < P["vents"]:
                    _vent(geo, pc, e1, e2, n, phw, phh, group)
                    vents += 1
                    stats["vents"] += 1
                    continue
                geo.frustum(mname, pc, e1, e2, n, phw, phh, t, P["ch"], group=group)
                stats["plates"] += 1
                if min(phw, phh) * 2 >= P["bolt"]:
                    br, bh = P["bolt_r"], P["bolt_h"]
                    ins = br * 2.2
                    for sx in (-1, 1):
                        for sy in (-1, 1):
                            bc = pc + n * t + e1 * sx * (phw - P["ch"] - ins) + e2 * sy * (phh - P["ch"] - ins)
                            geo.frustum("rd_steel", bc, e1, e2, n, br, br, bh, br * 0.45, base=-0.04, group=group)
                            stats["bolts"] += 1
    # shells made by the kit's helpers (hoops, ribs, pilasters, dome caps)
    for k, o in enumerate(objs):
        if o.get("rd_shape") and geo.tris < budget - 300:
            curved(geo, bvh, owner, k, o, P, stats)
    # buildings: railings round free roof edges, hatches / vent boxes on roofs, down pipes, an antenna
    if P.get("rails"):
        _roof_kit(geo, roofs, walls, bvh, owner, dict(P, antenna=P.get("antenna") and node_name == "main"), budget, stats)
    added = geo.objects()
    tris = _eval_tris(added) if added else 0
    log(f"  detail {node_name}: +{tris} triangles " + ", ".join(f"{k} {v}" for k, v in stats.items() if v))
    return added, tris


def _siding(geo, bvh, owner, k, mname, c, e1, e2, n, x0, x1, y0, y1, P, group):
    """Ribbed siding: a top and a bottom rail, vertical ribs between them (corrugated cladding)."""
    rt, rh, pitch, rw = 0.5, 0.7, P["rib_pitch"], 0.5
    clear = P["clear"]
    W, H = x1 - x0, y1 - y0
    mid = c + e1 * ((x0 + x1) / 2)
    ok = 0
    for y in (y1 - rh / 2, y0 + rh / 2):
        pc = mid + e2 * y
        if _free(bvh, owner, k, [pc + n * (rt + 0.05) + e1 * q * W / 2 for q in (-1, 0, 1)], clear + 0.15):
            geo.frustum(mname, pc, e1, e2, n, W / 2, rh / 2, rt, rt * 0.5, group=group)
            ok += 1
    nr = max(2, int(W / pitch))
    for i in range(nr):
        x = x0 + W * (i + 0.5) / nr
        pc = c + e1 * x + e2 * ((y0 + y1) / 2)
        hh = H / 2 - rh - 0.15
        if hh < 0.8:
            break
        if not _free(bvh, owner, k, [pc + n * 0.4 + e2 * q * hh for q in (-1, 0, 1)], clear + 0.1):
            continue
        geo.frustum(mname, pc, e1, e2, n, rw / 2, hh, 0.36, 0.2, group=group)
        ok += 1
    return ok > 2


def _framed(geo, bvh, owner, k, mname, c, e1, e2, n, x0, x1, y0, y1, P, group):
    """A framed panel: a raised frame round the face, a low plate inside (a small vent on big ones)."""
    fw, ft = 0.9, 0.55
    clear = P["clear"]
    W, H = x1 - x0, y1 - y0
    mid = c + e1 * ((x0 + x1) / 2) + e2 * ((y0 + y1) / 2)
    probe = [mid + n * (ft + 0.05) + e1 * sx * W / 2 + e2 * sy * H / 2 for sx in (-1, 0, 1) for sy in (-1, 0, 1)]
    if not _free(bvh, owner, k, probe, clear + 0.2):
        return False
    for (ox, oy, hw, hh) in ((0, H / 2 - fw / 2, W / 2, fw / 2), (0, -H / 2 + fw / 2, W / 2, fw / 2),
                             (W / 2 - fw / 2, 0, fw / 2, H / 2 - fw), (-W / 2 + fw / 2, 0, fw / 2, H / 2 - fw)):
        geo.frustum(mname, mid + e1 * ox + e2 * oy, e1, e2, n, hw, hh, ft, ft * 0.5, group=group)
    geo.frustum(mname, mid, e1, e2, n, W / 2 - fw - 0.25, H / 2 - fw - 0.25, 0.16, 0.08, group=group)
    if W > 7 and H > 5:
        geo.frustum("rd_vent", mid + e2 * (H / 4 - 0.3), e1, e2, n, min(2.2, W / 5), 0.7, 0.3, 0.1, group=group)
    return True


def _band(geo, material, ctr, axis, r, t, h, segs=24, group=None, arc=None, squash=1.0):
    """A band round an axis (a hoop): from radius r out to r + t, h long along the axis (arc=(a0, a1) deg: part
    of it, 0 = the side, 90 = up; squash scales the up direction). 6 x segs triangles."""
    axis = Vector(axis).normalized()
    if abs(axis.z) < 0.9:
        s = axis.cross(Vector((0, 0, 1))).normalized()
        u = s.cross(axis).normalized()
        if u.z < 0:
            u = -u
    else:
        s = Vector((1, 0, 0))
        u = axis.cross(s).normalized()
    bm = geo.bm(material, group)
    full = arc is None
    a0, a1 = (0.0, 360.0) if full else arc
    n = segs if full else max(2, int(segs * (a1 - a0) / 360.0)) + 1
    rings = []
    for (rr, dz) in ((r, -h / 2), (r + t, -h / 2 + t * 0.4), (r + t, h / 2 - t * 0.4), (r, h / 2)):
        ring = []
        for i in range(n):
            a = math.radians(a0 + (a1 - a0) * i / (segs if full else n - 1))
            ring.append(bm.verts.new(ctr + axis * dz + s * (math.cos(a) * rr) + u * (math.sin(a) * rr * squash)))
        rings.append(ring)
    for ra, rb in zip(rings[:-1], rings[1:]):
        for i in range(n if full else n - 1):
            j = (i + 1) % n
            f = bm.faces.new([ra[i], ra[j], rb[j], rb[i]])
            f.normal_update()
            cm = f.calc_center_median()
            out = cm - ctr - axis * (cm - ctr).dot(axis)
            if f.normal.dot(out) < 0:
                f.normal_flip()
    geo.tris += 6 * (n if full else n - 1)


def curved(geo, bvh, owner, k, o, P, stats):
    """Detail on the shells the kit's helpers made (rd_shape, set by tag_helpers): hoops or ribs on standing
    cylinders, hoops on tanks, ribs across vaults, a ring and a vent cap on domes, pilasters on curved walls."""
    shape = o.get("rd_shape")
    if not shape:
        return
    shape = json.loads(shape)
    kind = shape[0]
    mname = _mat_name(o.data.materials[0]) if o.data.materials else ""
    if not (_platable(mname) or (mname not in EXCLUDE and not mname.startswith(("HOUSE", "rd_")) and mname not in V3.V2.EMIT and V3.SPEC.get(mname, (0, 0, "dark"))[2] == "steel")):
        return
    group = o.get("d2k_group")
    clear = P["clear"]
    up = Vector((0, 0, 1))
    if kind == "cyl":                     # standing cylinder: c (x, y), r, z0, z1, r1
        _, cx, cy, r, z0, z1, r1 = shape
        if r < 2.5 or z1 - z0 < 2.5 or abs(r1 - r) > 0.3 * r:
            return
        ctr = Vector((cx, cy, 0))
        hgt = z1 - z0
        segs = max(16, min(40, int(r * 3.2)))
        if _h(cx, cy, z0, 23) < 0.55 and hgt > 4.0:    # hoops near the top and the foot (+ the middle on tall ones)
            zs = [z1 - 1.0, z0 + 1.0] + ([(z0 + z1) / 2] if hgt > 12 else [])
            for z in zs:
                pts = [Vector((cx + (r + 0.5) * math.cos(a), cy + (r + 0.5) * math.sin(a), z)) for a in [i * math.pi / 4 for i in range(8)]]
                if _free(bvh, owner, k, pts, clear + 0.1):
                    _band(geo, mname, Vector((cx, cy, z)), up, r - 0.05, 0.4, 0.9, segs, group)
                    stats["hoops"] = stats.get("hoops", 0) + 1
        else:                              # vertical ribs round it
            nrib = max(6, min(28, int(2 * math.pi * r / 3.0)))
            hh = hgt / 2 - 0.8
            for i in range(nrib):
                if hh < 1.0:
                    break
                a = 2 * math.pi * (i + 0.5) / nrib
                nn = Vector((math.cos(a), math.sin(a), 0))
                pc = ctr + nn * r + Vector((0, 0, (z0 + z1) / 2))
                e1 = Vector((-math.sin(a), math.cos(a), 0))
                if not _free(bvh, owner, k, [pc + nn * 0.5 + up * (q * hh) for q in (-1, 0, 1)], clear + 0.1):
                    continue
                geo.frustum(mname, pc - nn * 0.05, e1, up, nn, 0.32, hh, 0.45, 0.2, group=group)
                stats["ribs"] = stats.get("ribs", 0) + 1
    elif kind == "tank":                  # lying / standing tank a -> b (3D), r
        _, a, b, r = shape
        a, b = Vector(a), Vector(b)
        L = (b - a).length
        if r < 2.0 or L < 5.0:
            return
        ax = (b - a).normalized()
        nb = max(2, min(5, int(L / 7.0) + 1))
        segs = max(16, min(36, int(r * 3.0)))
        s = ax.orthogonal().normalized()
        u = ax.cross(s)
        for i in range(nb):
            t = 0.22 + 0.56 * i / max(1, nb - 1)
            p = a + (b - a) * t
            pts = [p + (s * math.cos(q) + u * math.sin(q)) * (r + 0.5) for q in [j * math.pi / 4 for j in range(8)]]
            pts = [q for q in pts if q.z > 0.6]
            if _free(bvh, owner, k, pts, clear + 0.1):
                _band(geo, mname, p, ax, r - 0.05, 0.38, 0.85, segs, group)
                stats["hoops"] = stats.get("hoops", 0) + 1
    elif kind == "vault":                 # barrel vault a -> b (x, y) on z0, r across, height h
        _, a, b, r, z0, h = shape
        a, b = Vector((a[0], a[1], z0)), Vector((b[0], b[1], z0))
        L = (b - a).length
        if r < 3.0 or L < 6.0:
            return
        ax = (b - a).normalized()
        side = ax.cross(up).normalized()
        nr = max(2, int(L / 5.5))
        segs = max(12, min(30, int(r * 2.5)))
        for i in range(nr):
            p = a + (b - a) * ((i + 0.5) / nr)
            pts = [p + side * (math.cos(q) * (r + 0.5)) + up * (math.sin(q) * (h + 0.5)) for q in [j * math.pi / 6 for j in range(1, 6)]]
            if not _free(bvh, owner, k, pts, clear + 0.1):
                continue
            _band(geo, mname, p, ax, r - 0.05, 0.4, 0.8, segs * 2, group, arc=(0.0, 180.0), squash=h / r)
            stats["ribs"] = stats.get("ribs", 0) + 1
    elif kind == "dome":                  # dome on z0: c, r, z0, h
        _, cx, cy, r, z0, h = shape
        if r < 3.0:
            return
        pts = [Vector((cx + (r + 0.5) * math.cos(a), cy + (r + 0.5) * math.sin(a), z0 + 0.5)) for a in [i * math.pi / 4 for i in range(8)]]
        if _free(bvh, owner, k, pts, clear + 0.1):
            _band(geo, mname, Vector((cx, cy, z0 + 0.45)), up, r - 0.05, 0.45, 0.9, max(16, int(r * 3)), group)
            stats["hoops"] = stats.get("hoops", 0) + 1
        top = Vector((cx, cy, z0 + h))
        if _free(bvh, owner, k, [top + up * z for z in (0.6, 1.5)], 0.5):
            rr = max(0.9, r * 0.22)
            _band(geo, "rd_steel", top + up * 0.15, up, 0.05, rr, 0.9, 12, group)
            geo.frustum("rd_vent", top + up * 0.6, Vector((1, 0, 0)), Vector((0, 1, 0)), up, rr * 0.6, rr * 0.6, 0.1, 0.0, group=group)
            stats["caps"] = stats.get("caps", 0) + 1
    elif kind == "arc":                   # curved wall: c, r0, r1, a0, a1, z0, z1 (the outer face at r1)
        _, cx, cy, r0, r1, a0, a1, z0, z1 = shape
        if z1 - z0 < 3.0 or r1 < 5.0:
            return
        span = math.radians(abs(a1 - a0)) * r1
        npil = max(2, int(span / 6.0))
        hh = (z1 - z0) / 2 - 0.4
        for i in range(npil):
            a = math.radians(a0 + (a1 - a0) * (i + 0.5) / npil)
            nn = Vector((math.cos(a), math.sin(a), 0))
            pc = Vector((cx, cy, (z0 + z1) / 2)) + nn * r1
            e1 = Vector((-math.sin(a), math.cos(a), 0))
            if not _free(bvh, owner, k, [pc + nn * 0.6 + up * (q * hh) for q in (-1, 0, 1)], clear + 0.1):
                continue
            geo.frustum(mname, pc - nn * 0.05, e1, up, nn, 0.55, hh, 0.55, 0.3, group=group)
            stats["pilasters"] = stats.get("pilasters", 0) + 1


def tag_helpers(AK):
    """Wrap the building kit's shell helpers so their objects carry their shape (rd_shape) for curved()."""
    def wrap(fn, tag):
        def w(*a, **kw):
            o = fn(*a, **kw)
            try:
                meta = tag(*a, **kw)
                if meta is not None and o is not None:
                    o["rd_shape"] = json.dumps(meta)
            except Exception:  # noqa: BLE001 (a helper called in an unusual way: no metadata)
                pass
            return o
        w.__wrapped__ = fn
        return w

    def cyl_meta(name, c, r, z0, z1, material=None, segs=24, r1=None, bev=0.0):
        return ("cyl", float(c[0]), float(c[1]), float(r), float(z0), float(z1), float(r if r1 is None else r1))

    def tank_meta(name, a, b, r, material=None, segs=20, cap=0.3, bev=0.0):
        return ("tank", tuple(map(float, a)), tuple(map(float, b)), float(r))

    def vault_meta(name, a, b, r, z0, material=None, segs=14, h=None, bev=0.0, ends=0.0):
        return ("vault", tuple(map(float, a[:2])), tuple(map(float, b[:2])), float(r), float(z0), float(r if h is None else h))

    def dome_meta(name, c, r, z0, h, material=None, segs=24, steps=6):
        return ("dome", float(c[0]), float(c[1]), float(r), float(z0), float(h))

    def arc_meta(name, c, r0, r1, a0, a1, z0, z1, material=None, n=12, bev=0.0, z1b=None):
        return ("arc", float(c[0]), float(c[1]), float(r0), float(r1), float(a0), float(a1), float(z0), float(z1 if z1b is None else min(z1, z1b)))

    def lathe_meta(name, c, profile, material=None, segs=48, bev=0.0, cap=True):
        # the straight upright stretch of the profile at its widest -> a standing cylinder
        rmax = max(r for (r, z) in profile)
        zs = [z for (r, z) in profile if r >= rmax * 0.93]
        if len(zs) < 2 or max(zs) - min(zs) < 3.0:
            return None
        return ("cyl", float(c[0]), float(c[1]), float(rmax), float(min(zs)), float(max(zs)), float(rmax))

    def sweep_meta(name, a, b, profile, material=None, segs=20, bev=0.0, squash=1.0, arc=(0.0, 360.0), point=0.0):
        a, b = Vector(a), Vector(b)
        ts = [t for (t, r) in profile]
        rmax = max(r for (t, r) in profile)
        straight = [t for (t, r) in profile if r >= rmax * 0.95]
        L = (b - a).length * (max(straight) - min(straight))
        if L < 5.0 or point > 0 or abs(squash - 1.0) > 0.05 and arc[1] - arc[0] >= 359.9:
            return None
        a2, b2 = a + (b - a) * min(straight), a + (b - a) * max(straight)
        if arc[1] - arc[0] >= 359.9:
            return ("tank", tuple(a2), tuple(b2), float(rmax))
        if abs(arc[0]) < 1 and abs(arc[1] - 180.0) < 1 and abs(a.z - b.z) < 0.01:
            return ("vault", (a2.x, a2.y), (b2.x, b2.y), float(rmax), float(a.z), float(rmax * squash))
        return None

    for mod in (AK, sys.modules.get("refinery_pilot_fable")):
        if mod is None:
            continue
        for nm, meta in (("cyl", cyl_meta), ("tank", tank_meta), ("vault", vault_meta), ("dome", dome_meta), ("arc", arc_meta),
                         ("sweep", sweep_meta), ("lathe", lathe_meta)):
            f = getattr(mod, nm, None)
            if f is not None and not hasattr(f, "__wrapped__"):
                setattr(mod, nm, wrap(f, meta))


def _vent(geo, c, e1, e2, n, hw, hh, group):
    """A louvred vent: a raised steel frame, a near-black recess, slanted slats."""
    fw = 0.45
    for (ox, oy, w, h) in ((0, hh - fw / 2, hw, fw / 2), (0, -hh + fw / 2, hw, fw / 2), (hw - fw / 2, 0, fw / 2, hh - fw), (-hw + fw / 2, 0, fw / 2, hh - fw)):
        geo.frustum("rd_steel", c + e1 * ox + e2 * oy, e1, e2, n, w, h, 0.5, 0.18, group=group)
    geo.frustum("rd_vent", c, e1, e2, n, hw - fw, hh - fw, 0.12, 0.0, group=group)
    ns = max(2, int((hh * 2 - fw * 2) / 1.1))
    for s in range(ns):
        y = -hh + fw + (hh * 2 - fw * 2) * (s + 0.5) / ns
        # a slat: tilted 35 deg out of the face (top edge out)
        tilt = (e2 * math.cos(math.radians(35)) + n * math.sin(math.radians(35))).normalized()
        nn = tilt.cross(e1).normalized()
        if nn.dot(n) < 0:
            nn = -nn
        geo.frustum("rd_steel", c + e2 * y + n * 0.25, e1, tilt, nn, hw - fw - 0.05, 0.42, 0.12, 0.0, group=group)


def _roof_kit(geo, roofs, walls, bvh, owner, P, budget, stats):
    zmax = max((r[3].z for r in roofs), default=-1)
    hatches = 0
    pipes = 0
    antenna = False
    for (k, o, mname, c, n, e1, e2, hw, hh, group, area) in roofs:
        if geo.tris > budget - 200:
            return
        if c.z < P["low_z"] + 2.0:
            continue
        # railings on the free edges of high roofs (nothing beyond the edge at roof height or just above it)
        h, r = P["rail_h"], P["rail_r"]
        rail_edges = ((e1, 1), (e1, -1), (e2, 1), (e2, -1)) if (c.z >= P["rail_z"] and area >= P["rail_area"]) else ()
        for (ax, sgn) in rail_edges:
            half_out = hw if ax is e1 else hh
            along = e2 if ax is e1 else e1
            half_al = (hh if ax is e1 else hw) - 0.9
            if half_al < 3.0:
                continue
            edge = c + ax * sgn * (half_out - 0.6)
            beyond = [c + ax * sgn * (half_out + 1.5) + along * s * half_al + n * z for s in (-0.8, 0, 0.8) for z in (0.3, 1.8)]
            on = [edge + along * s * half_al + n * z for s in (-1, -0.5, 0, 0.5, 1) for z in (0.6, h * 0.6, h)]
            if not _free(bvh, owner, k, beyond, 1.2) or not _free(bvh, owner, k, on, 0.55):
                continue
            # a drop beyond the edge (else it is a wall-to-wall joint, not a roof edge)
            hit = bvh.ray_cast(c + ax * sgn * (half_out + 1.2) + n * 0.2, -n, 1.6)
            if hit[0] is not None:
                continue
            npost = max(2, int(half_al * 2 / 3.2) + 1)
            for i in range(npost):
                p = edge + along * (-half_al + 2 * half_al * i / (npost - 1))
                geo.tube("rd_rail", p, p + n * h, r, 4, group)
            a, b = edge - along * half_al, edge + along * half_al
            geo.tube("rd_rail", a + n * h, b + n * h, r * 1.1, 4, group)
            geo.tube("rd_rail", a + n * h * 0.5, b + n * h * 0.5, r * 0.8, 4, group)
            stats["rails"] += 1
        # a hatch and / or a vent box near the middle of a big roof
        # roof equipment scattered over free spots of the roof: hatches, vent boxes, fan units, pipe stubs
        if hatches < P["hatches"] and hw > 3.2 and hh > 3.2:
            spots = []
            sp = 5.6
            nx, ny = max(1, int((hw * 2 - 3.0) / sp)), max(1, int((hh * 2 - 3.0) / sp))
            for i in range(nx):
                for j in range(ny):
                    ox = -hw + 1.5 + (hw * 2 - 3.0) * (i + 0.5) / nx
                    oy = -hh + 1.5 + (hh * 2 - 3.0) * (j + 0.5) / ny
                    q = c + e1 * ox + e2 * oy
                    spots.append((_h(q.x, q.y, q.z, 29), q))
            spots.sort(key=lambda t: t[0])
            per_roof = max(1, int(area / 70.0))
            placed = 0
            for (hv, pc) in spots:
                if placed >= per_roof or hatches >= P["hatches"] or geo.tris > budget - 120:
                    break
                if hv > 0.75:
                    continue
                s = 1.6 + 0.6 * _h(pc.x, pc.y, 41)
                probe = [pc + n * z + e1 * sx * (s + 0.5) + e2 * sy * (s + 0.5) for sx in (-1, 0, 1) for sy in (-1, 0, 1) for z in (0.9, 2.2)]
                if not _free(bvh, owner, k, probe, 0.55) or _buried(bvh, owner, k, pc, n):
                    continue
                kind = int(_h(pc.x, pc.y, pc.z, 37) * 4)
                if kind == 0:                   # a hatch: a frame, a lid, a handle
                    geo.frustum("rd_steel", pc, e1, e2, n, s, s, 0.55, 0.25, group=group)
                    geo.frustum(mname, pc + n * 0.5, e1, e2, n, s - 0.45, s - 0.45, 0.3, 0.18, group=group)
                    geo.frustum("rd_dark", pc + n * 0.8 + e2 * (s - 1.1), e1, e2, n, 0.8, 0.18, 0.25, 0.0, group=group)
                elif kind == 1:                 # a vent box with a grille top
                    geo.box("rd_steel", pc, e1, e2, n, s * 1.15, s * 0.8, 0.0, 1.6, group=group)
                    for q in range(3):
                        geo.frustum("rd_vent", pc + n * 1.6 + e1 * ((q - 1) * s * 0.7), e1, e2, n, s * 0.25, s * 0.6, 0.08, 0.0, group=group)
                elif kind == 2:                 # a fan unit: a box, a ring, a dark fan disc, a hub
                    geo.box(mname, pc, e1, e2, n, s * 1.1, s * 1.1, 0.0, 1.3, group=group)
                    _band(geo, "rd_steel", pc + n * 1.55, n, s * 0.62, 0.3, 0.5, 14, group)
                    geo.frustum("rd_vent", pc + n * 1.3, e1, e2, n, s * 0.62, s * 0.62, 0.12, 0.0, group=group)
                    geo.frustum("rd_steel", pc + n * 1.42, e1, e2, n, 0.35, 0.35, 0.25, 0.1, group=group)
                else:                           # two pipe stubs with caps on a plinth
                    geo.frustum("rd_steel", pc, e1, e2, n, s * 1.1, s * 0.6, 0.4, 0.15, group=group)
                    for q in (-1, 1):
                        a = pc + e1 * (q * s * 0.55)
                        geo.tube("rd_copper", a, a + n * 2.4, 0.45, 8, group)
                        geo.tube("rd_dark", a + n * 2.4, a + n * 2.75, 0.6, 8, group)
                hatches += 1
                placed += 1
                stats["hatches"] += 1
        if P.get("antenna") and not antenna and c.z >= zmax - 0.5 and hw > 2.5 and hh > 2.5:
            for (sx, sy) in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
                pc = c + e1 * sx * (hw - 1.4) + e2 * sy * (hh - 1.4)
                if not _free(bvh, owner, k, [pc + n * z for z in (0.5, 3.0, 6.0, 9.0)], 0.8):
                    continue
                geo.frustum("rd_steel", pc, e1, e2, n, 0.7, 0.7, 0.6, 0.2, group=group)
                geo.tube("rd_rail", pc, pc + n * 10.0, 0.18, 4, group)
                geo.tube("rd_rail", pc + n * 7.5 - e1 * 1.4, pc + n * 7.5 + e1 * 1.4, 0.12, 4, group)
                geo.tube("rd_rail", pc + n * 9.0 - e2 * 1.0, pc + n * 9.0 + e2 * 1.0, 0.12, 4, group)
                antenna = True
                stats["antenna"] += 1
                break
    # down pipes on tall free walls (from under the top to the ground), with brackets
    for (k, o, mname, c, n, e1, e2, hw, hh, group) in walls:
        if pipes >= P["pipes"] or geo.tris > budget - 100:
            break
        top, bot = c + e2 * hh, c - e2 * hh
        if hh * 2 < 12.0 or bot.z > 1.5:
            continue
        for sx in (1, -1):
            x = sx * (hw - 1.6)
            a = c + e1 * x + e2 * (hh - 1.5) + n * 0.9
            b = c + e1 * x - e2 * (hh - 0.2) + n * 0.9
            if not _free(bvh, owner, k, [a + (b - a) * t for t in (0.0, 0.25, 0.5, 0.75, 1.0)], 0.9):
                continue
            geo.tube("rd_copper", a, b, 0.5, 8, group)
            for t in (0.15, 0.5, 0.85):
                q = a + (b - a) * t
                geo.frustum("rd_steel", q - n * 0.9, e1, e2, n, 0.75, 0.25, 0.9, 0.0, group=group)
            pipes += 1
            stats["pipes"] += 1
            break

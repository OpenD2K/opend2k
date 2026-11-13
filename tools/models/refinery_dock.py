"""The refinery's dock, laid out from the game's numbers (the 3D layout of the three refineries:
refinery_pilot_fable.py, harkonnen_refinery.py, ordos_refinery.py; docs/3d-models.md "Docking").

What the game does (economy.cpp harvester states 13-16, render3d/models.cpp dockOffset / deckAt): the refinery
is 3 x 3 cells, all occupied, of which only (1, 0), (2, 0), (0, 1), (1, 1) are solid; the harvester drives onto
the DOCK cell = exit point 1 = cell (2, 1), turns on its centre to facing 12 (front to the south-east, rear to
the north-west) and unloads there (animFrame 1..10: the cover opens, the pipe telescopes out of its rear).
Model px (x east, y north of the footprint centre, z up): the dock centre is (32, 0).

The harvester model (tools/models/harvester_v1.py; HARVESTER.glb + HARVESTER_unloadNN.glb) is 39 px wide and
44 px long - larger than the cell. Measured from the glbs (refinery_dock_check.py prints them):
  * turning on the spot it sweeps a radius of 23.8 px below 8 px (front prongs; the track pods 21.2) and
    20.5 px above (the rear spine), up to 30.3 px high in the unloading poses;
  * the pipe's mouth ends 26.35 px behind its centre, 10.6 px above the surface it stands on.
So the 3D layout keeps a cylinder round the dock centre free of everything that is higher than the pad:
K_LOW below Z_LOW, K_HIGH above (1 px of air), the pad's own parts no higher than the surface the harvester
stands on, and puts the inlet socket's mouth on the pipe's axis so that the pipe ends INSERT px inside it -
the engine then has nothing to slide (dockOffset < 1 px).

Pure maths (usable without Blender; the checker imports it) + two bmesh helpers for the generators.
"""
import math

DOCK = (32.0, 0.0)          # the dock cell's centre
BOX = 48.0                  # half the footprint
K_LOW, Z_LOW = 24.8, 10.5   # keep-out radius below Z_LOW (above the pad) ...
K_HIGH, Z_HIGH = 21.5, 33.5  # ... and from there up to Z_HIGH
NOZZLE_BACK = 26.35         # the pipe's mouth behind the harvester's centre (last unloading pose)
NOZZLE_UP = 10.6            # ... and above the surface the harvester stands on
INSERT = 1.6                # how far the pipe ends inside the socket (fx_inlet_0 = mouth + INSERT into the bore)
REAR = (-math.sqrt(0.5), math.sqrt(0.5))   # where the docked harvester's rear points (north-west)


def mouth():
    """The inlet socket's mouth centre (x, y)."""
    t = NOZZLE_BACK - INSERT
    return (DOCK[0] + REAR[0] * t, DOCK[1] + REAR[1] * t)


def anchor(z):
    """fx_inlet_0 (x, y, z): where the pipe's mouth ends."""
    return (DOCK[0] + REAR[0] * NOZZLE_BACK, DOCK[1] + REAR[1] * NOZZLE_BACK, z)


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def drums(rf1, rf2, gap=2.4, edge=0.15, air=0.0):
    """Ground centres of the two drums (foot radii rf1 = the west one, rf2 = the north one) inside the footprint:
    the north drum against the north edge, as far east as the dock's keep-out lets it; the west drum against the
    west edge, `gap` px from the other."""
    y2 = BOX - rf2 - edge
    x2 = DOCK[0] - math.sqrt((K_LOW + air + rf2) ** 2 - (y2 - DOCK[1]) ** 2)
    x1 = -BOX + rf1 + edge
    y1 = y2 - math.sqrt((rf1 + rf2 + gap) ** 2 - (x2 - x1) ** 2)
    return (x1, y1), (x2, y2)


def nook(c, rc, rd):
    """The point rc from c and rd from the dock centre, on the north-east side: where the tank / standpipe goes
    (north of the pad, east of the north drum)."""
    d = dist(c, DOCK)
    a = (rc * rc - rd * rd + d * d) / (2 * d)
    h = math.sqrt(max(0.0, rc * rc - a * a))
    ux, uy = (DOCK[0] - c[0]) / d, (DOCK[1] - c[1]) / d
    return (c[0] + ux * a - uy * h, c[1] + uy * a + ux * h)


def crook(d1, d2, r):
    """The point r from both drum centres on their north-west side (the dome / small tank behind the bridge)."""
    mx, my = (d1[0] + d2[0]) / 2, (d1[1] + d2[1]) / 2
    half = dist(d1, d2) / 2
    ax, ay = (d2[0] - d1[0]) / (2 * half), (d2[1] - d1[1]) / (2 * half)
    t = math.sqrt(max(0.0, r * r - half * half))
    return (mx - ay * t, my + ax * t)


class Between:
    """Maps the parts that stand BETWEEN the drums from the 2D layout to the 3D one: the frame on the drums'
    centre line (origin = its middle) is turned and moved with the drums, distances scaled by k."""

    def __init__(self, old1, old2, new1, new2, k):
        self.o = ((old1[0] + old2[0]) / 2, (old1[1] + old2[1]) / 2)
        self.n = ((new1[0] + new2[0]) / 2, (new1[1] + new2[1]) / 2)
        self.turn = math.atan2(new2[1] - new1[1], new2[0] - new1[0]) - math.atan2(old2[1] - old1[1], old2[0] - old1[0])
        self.k = k

    def pt(self, p):
        x, y = p[0] - self.o[0], p[1] - self.o[1]
        c, s = math.cos(self.turn), math.sin(self.turn)
        q = (self.n[0] + (x * c - y * s) * self.k, self.n[1] + (x * s + y * c) * self.k)
        return q if len(p) == 2 else (q[0], q[1], p[2])

    def ang(self, deg):
        return deg + math.degrees(self.turn)


def clear(p, r=0.0, z0=0.0, z1=1e9):
    """Is a part round p (x, y) of radius r, between the heights z0..z1, outside the dock's keep-out?"""
    d = dist(p, DOCK) - r
    return (z0 >= Z_HIGH or d >= K_HIGH) and (z0 >= Z_LOW or d >= K_LOW)


# ---- the pad: a tray turned 45 degrees round the dock centre, its four corners cut where it would leave the
# footprint (reach px from the dock centre along the axes: 16 = the cell's east edge) - an elongated octagon


def pad_chamfers(c, hl, hw, reach):
    """The four cut corners of a 45-degree rectangle (half length hl along north-east, half width hw) clipped to
    |x - cx| <= reach, |y - cy| <= reach: [(centre (x, y), direction deg, half length)] of each cut edge."""
    s = math.sqrt(0.5)
    far, off = (hl + hw) * s, (hl - hw) * s
    out = []
    if far > reach:
        h = far - reach
        out = [((c[0] + reach, c[1] + off), 90.0, h), ((c[0] - reach, c[1] - off), 90.0, h),
               ((c[0] + off, c[1] + reach), 0.0, h), ((c[0] - off, c[1] - reach), 0.0, h)]
    return out


def clip_plan(objs, c, reach):
    """Cut the objects (px, before the node is joined) to |x - cx| <= reach, |y - cy| <= reach, closing the cut
    faces; objects that end up empty are removed. Returns the kept objects."""
    import bmesh
    import bpy
    from mathutils import Vector
    kept = []
    for o in objs:
        bm = bmesh.new()
        bm.from_mesh(o.data)
        for co, no in (((c[0] + reach, 0, 0), (1, 0, 0)), ((c[0] - reach, 0, 0), (-1, 0, 0)),
                       ((0, c[1] + reach, 0), (0, 1, 0)), ((0, c[1] - reach, 0), (0, -1, 0))):
            if not bm.verts:
                break
            if max(Vector(no).dot(v.co - Vector(co)) for v in bm.verts) <= 1e-5:
                continue
            res = bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], dist=1e-5,
                                         plane_co=Vector(co), plane_no=Vector(no), clear_outer=True, clear_inner=False)
            edges = [e for e in res["geom_cut"] if isinstance(e, bmesh.types.BMEdge)]
            if edges:
                bmesh.ops.holes_fill(bm, edges=edges, sides=0)
        if bm.faces:
            bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
            bm.to_mesh(o.data)
            kept.append(o)
        else:
            bpy.data.objects.remove(o)
        bm.free()
    return kept

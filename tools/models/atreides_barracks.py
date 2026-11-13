"""Atreides barracks - the original design rebuilt with tools/models/atreides_kit.py.

    blender -b --factory-startup --python tools/models/atreides_barracks.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_BARRACKS .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read (frame 3067 main, 3068 damaged, no base, no animation (no HAS_ANIMATION flag);
build-up 13 = a rock mound, kept):
  * the "hawk wing": west of the body, a bowed white spar along its high edge (from the west tip up to the body's
    roof) and overlapping brown / orange feathers fanning out from the body to a scalloped trailing edge;
  * the body: a low grey-brown block with a large white dome on its south side, a small cream dome behind it, a
    dark house-colour dome on the roof (north) and a white mast leaning up to the north-east with a dark head;
  * east: rust machinery with house-colour panels; south: the gate block (orange lintel, house-colour pilasters);
  * south-west: a small generator with red lamps and a white pipe to the body.
Free choices: canopy posts, vents, lamps, pipe runs.

The door (tools/models/barracks_door.py, redesign 2026-11-13): the game makes a trained soldier appear inside the
footprint, at px (7, 2) from its centre - under the picture's big white dome - and walks him straight south, through
the picture's gate block. So the white dome is now a white, domed GATE TOWER on that spot (an open, lit doorway
20 px high - a soldier stands 22 px tall in 3D -, door plane y = -3, the hall behind it hollow), and the gate block
became its open forecourt: a paved path between two low walls with orange copings, ending at the two house-colour
gate posts. The east machinery ends at x = 33 so the plan fits the 2 x 3 cells (Graphics: 3D: scale 1.0, was 0.96).
"""
import math
import os
import sys

import bmesh

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402
import barracks_door as BD  # noqa: E402

KEY = "ATREIDES_BARRACKS"
P = dict(
    wing_a=(-30.5, 5.5, 12.0), wing_b=(-3.5, 20.5, 24.0), wing_w=(4.0, -27.0, -15.0), wing_t=1.0, bow=4.0, droop=3.0, sun_el=44.0, sh_az=54.0,
    body=(-4.0, 30.0, 3.0, 27.0, 12.0),          # x0, x1, y0, y1, z
    hdome_c=(4.5, 35.25), hdome_r=6.0,
    mast_a=(9.0, 30.0, 12.0), mast_b=(20.0, 34.5, 32.0),
    gate=(-4.0, 12.0, -21.0, -9.0, 8.5),
    door=(7.0, -3.0, -21.0, 6.0, 11.0, 20.0),     # the gate tower: centre line x, door plane y, the path's south end, depth, width, height
)


def wing_pt(s, t, lift=0.0):
    a, b, w = P["wing_a"], P["wing_b"], P["wing_w"]
    bow = P["bow"] * 4.0 * min(1.0, max(0.0, s)) * (1.0 - min(1.0, max(0.0, s))) - P["droop"] * t * t      # the spar bows up, the feathers droop
    return (a[0] + (b[0] - a[0]) * s + w[0] * t, a[1] + (b[1] - a[1]) * s + w[1] * t, a[2] + (b[2] - a[2]) * s + w[2] * t + lift + bow)


def plate(name, st, thick, material, lift=0.0):
    """A plate in the wing's plane over the polygon st = [(s, t)]."""
    top = [wing_pt(s, t, lift) for (s, t) in st]
    bm = bmesh.new()
    K.rings_solid(bm, [[(x, y, z - thick) for (x, y, z) in top], top])
    return K.new_obj(name, bm, material)


def build(damaged=False):
    main = []
    # ---- the wing: a bowed white spar along its high edge, overlapping feathers fanning out from the body to a
    # scalloped trailing edge, drooping towards their tips
    NF = 12
    q0 = 0.42 if damaged else 0.0                         # damaged: the west part of the wing is shot away

    def tip(q):
        return (q, 0.02 + 0.98 * q ** 0.9)
    root = (0.96, 0.3)
    trail = [tip(q0 + (1.0 - q0) * i / 16) for i in range(17)]
    wing = plate("wing", [(q0, 0.0), (1.0, 0.0)] + trail[::-1], P["wing_t"], "skirt", -0.5)
    main.append(wing)
    tones = ("skirt", "fin", "orange", "skirt", "copper", "fin")
    for i in range(NF):
        q = q0 + (1.0 - q0) * (i + 0.5) / NF
        e = tip(q)
        d = (e[0] - root[0], e[1] - root[1])
        ln = math.hypot(*d)
        nx, ny = -d[1] / ln, d[0] / ln
        w0, w1 = 0.012, 0.05 + 0.02 * (i % 2)
        m = (root[0] + d[0] * 0.8, root[1] + d[1] * 0.8)
        st = [(root[0] + nx * w0, root[1] + ny * w0), (m[0] + nx * w1, m[1] + ny * w1), (e[0] + nx * w1 * 0.5, e[1] + ny * w1 * 0.5),
              (e[0] + d[0] / ln * 0.03, e[1] + d[1] / ln * 0.03), (e[0] - nx * w1 * 0.5, e[1] - ny * w1 * 0.5), (m[0] - nx * w1, m[1] - ny * w1),
              (root[0] - nx * w0, root[1] - ny * w0)]
        main.append(plate(f"feather{i}", st, 0.5, tones[i % len(tones)], 0.15 + 0.3 * (i % 3)))
        main.append(K.beam(f"quill{i}", wing_pt(root[0] + d[0] * 0.1, root[1] + d[1] * 0.1, 0.9), wing_pt(e[0] - d[0] * 0.05, e[1] - d[1] * 0.05, 0.9), 0.5, 0.4, "copper_dk"))
    ns = 8
    for i in range(ns):                                   # the spar, bowed
        sa, sb = q0 - 0.02 + (1.04 - q0) * i / ns, q0 - 0.02 + (1.04 - q0) * (i + 1) / ns
        main.append(K.beam(f"wing_spar{i}", wing_pt(sa, 0.0, 0.7), wing_pt(sb, 0.0, 0.2), 3.6, 3.6, "white", 0.4))
    if damaged:                                           # the broken spar hangs down, torn feathers at the break
        main.append(K.beam("wing_spar_broken", wing_pt(q0, 0.0, 0.4), wing_pt(q0 - 0.13, 0.28, -5.0), 3.2, 2.0, "white", 0.3))
        for i, (s0, t0, s1, t1) in enumerate(((0.44, 0.12, 0.34, 0.2), (0.46, 0.3, 0.37, 0.42), (0.5, 0.5, 0.4, 0.6))):
            main.append(K.beam(f"wing_torn{i}", wing_pt(s0, t0, 0.1), wing_pt(s1, t1, -2.5 - i), 3.0, 0.5, "rust" if i % 2 else "skirt"))
    for i, (s_, t_) in enumerate(((0.12, 0.12), (0.5, 0.4), (0.5, 0.05)) if not damaged else ((0.55, 0.4), (0.55, 0.05))):   # posts under it
        p = wing_pt(s_, t_, -P["wing_t"] - 0.5)
        main.append(K.tube(f"wing_post{i}", (p[0], p[1], 0.0), p, 0.9, "copper_dk", 8))
        main.append(K.cyl(f"wing_post_foot{i}", (p[0], p[1]), 1.8, 0.0, 1.2, "foot", 8))
    # ---- the body
    x0, x1, y0, y1, bz = P["body"]
    dx, dy, dpath, ddep, dwid, dhgt = P["door"]
    tw0, tw1 = dx - dwid / 2 - 1.6, dx + dwid / 2 + 1.6    # the gate tower's outer faces
    main.append(K.box("body_foot", x0 - 1.2, x1 + 1.2, y0, y1 + 1.2, 0.0, 2.6, "foot", 0.3))
    main.append(K.box("body_foot_w", x0 - 1.2, tw0 + 0.2, y0 - 1.2, y0 + 0.5, 0.0, 2.6, "foot", 0.3))      # (the front strip, cut away at the tower)
    main.append(K.box("body_foot_e", tw1 - 0.2, x1 + 1.2, y0 - 1.2, y0 + 0.5, 0.0, 2.6, "foot", 0.3))
    main.append(K.box("body", x0, x1, y0, y1, 2.0, bz, "stone", 3.0, 0.86))
    main.append(K.dome("mound", (15.0, 27.0), 12.0, bz - 5.0, 9.5, "stone_dk", 24))
    main.append(K.dome("mound2", (22.0, 14.0), 8.0, bz - 6.0, 7.0, "rust", 20))
    main.append(K.box("body_roof", x0 + 3.5, x1 - 3.5, y0 + 3.5, y1 - 3.5, bz - 0.4, bz + 0.8, "stone_dk", 0.6))
    main.append(K.box("terrace", x0, tw0 + 0.2, -5.0, y0 + 1.0, 0.0, 5.5, "stone", 0.8, 0.9))       # (cut away at the tower)
    main.append(K.box("terrace_e", tw1 - 0.2, x1, -5.0, y0 + 1.0, 0.0, 5.5, "stone", 2.0, 0.9))
    for i, (bx0, by0, w, d, h, mt) in enumerate(((-2.0, 14.0, 6.0, 5.0, 2.0, "rust"),)):
        main.append(K.box(f"roof_box{i}", bx0, bx0 + w, by0, by0 + d, bz + 0.6, bz + 0.6 + h, mt, 0.3))
    main.extend(K.pipe("roof_pipe", [(0.0, 24.0, bz + 1.6), (12.0, 24.0, bz + 1.6), (12.0, 15.0, bz + 1.6)], 0.9, "copper"))
    main.extend(K.vent("body_vent1", (22.0, y0 - 0.05), 0.0, 2.4, 5.0, 9.0))
    # domes: the big white one is the gate tower's cap (below)
    dc = (dx, dy + ddep / 2 + 0.9)
    main.append(K.dome("dome2", (9.0, 19.0), 4.4, bz + 0.6, 4.0, "roof", 20))
    hc = P["hdome_c"]
    main.append(K.cyl("hdome_drum", hc, P["hdome_r"] + 0.5, bz - 6.0, bz + 1.5, "wall", 20))
    main.append(K.house(K.dome("hdome", hc, P["hdome_r"], bz + 1.3, P["hdome_r"] * 1.05, "HOUSE", 20), 0.1))
    # the mast
    ma, mb = P["mast_a"], P["mast_b"]
    if damaged:                                           # the mast is snapped half way up, its head gone
        mb = tuple(ma[i] + (mb[i] - ma[i]) * 0.55 for i in range(3))
        main.append(K.beam("mast_stub", mb, (mb[0] + 4.0, mb[1] - 1.0, mb[2] - 3.5), 2.6, 2.0, "white", 0.2))
    main.append(K.beam("mast", ma, mb, 3.2, 2.4, "white", 0.3))
    main.append(K.obox("tower", (20.0, 31.0), 25.0, 5.0, 3.6, bz - 2.0, bz + (9.0 if damaged else 17.0), "skirt", 0.4, 0.45 if not damaged else 0.7))
    main.append(K.obox("tower2", (25.0, 26.0), 25.0, 3.0, 2.6, bz - 2.0, bz + 4.0, "stone_dk", 0.4, 0.7))
    if not damaged:
        main.append(K.obox("mast_head", (mb[0] + 1.0, mb[1] + 0.5), 25.0, 3.2, 2.2, mb[2] - 1.5, mb[2] + 3.0, "block_dk", 0.3))
    main.append(K.obox("mast_foot", (ma[0] + 1.0, ma[1]), 25.0, 4.0, 3.0, bz - 4.0, bz + 3.0, "stone", 0.3))
    # ---- east machinery with house panels
    main.append(K.box("mach", 17.0, 33.0, 0.0, 18.0, 0.0, 9.0, "skirt", 0.5, 0.92))
    main.append(K.obox("neck", (-2.5, 28.0), 30.0, 3.0, 4.5, 8.0, 16.0, "stone_dk", 0.6))
    main.append(K.house(K.box("mach_panel0", 21.0, 31.0, 3.0, 12.0, 8.8, 9.8, "HOUSE", 0.15), 0.13))
    main.append(K.house(K.box("mach_panel1", 16.0, 21.5, -8.0, -1.0, 4.0, 6.8, "HOUSE", 0.15), 0.15))
    main.append(K.box("mach2", 15.2, 26.0, -12.0, -3.0, 0.0, 5.0, "rust", 0.4))
    main.extend(K.pipe("mach_pipe", [(29.0, 16.0, 5.0), (29.0, 22.0, 5.0), (29.0, 22.0, 11.0)], 1.3, "copper"))
    main.extend(K.pipe("mach_pipe2", [(24.0, -6.0, 6.0), (30.0, -6.0, 6.0), (30.0, -2.0, 3.0)], 1.1, "orange"))
    main.append(K.tank("mach_tank", (21.0, 18.0, 13.5), (27.0, 22.0, 13.5), 2.6, "copper", 12, 0.4))
    # ---- the gate tower on the soldiers' spawn point and its forecourt (the picture's gate block): path, low walls, posts
    gx0, gx1, gy0, gy1, gz = P["gate"]
    dmain, glow, danch = BD.door(K, "tower", dx, dy, dpath, width=dwid, depth=ddep, height=dhgt, wall="white", trim="orange", cap=("white", 6.0),
                                 flank=4.2, anchor="fx_light_2", damaged=damaged)
    main.extend(dmain)
    main.append(K.box("fore_w", gx0 - 0.4, tw0 + 0.3, gy0 + 0.6, gy1 + 4.0, 0.0, 3.4, "skirt", 0.4))      # the gate block's west part: a low plinth
    main.append(K.box("fore_w_top", gx0 + 0.4, tw0 - 0.6, gy0 + 1.6, gy1 + 3.0, 3.3, 4.0, "rust", 0.2))
    posts = (tw0 - 0.2, tw1 + 0.2)
    for i, x in enumerate(posts):
        main.append(K.house(K.box(f"gate_pil{i}", x - 1.0, x + 1.0, gy0 - 0.6, gy0 + 1.6, 0.0, gz + 4.0, "HOUSE", 0.1), 0.17))
        main.append(K.box(f"gate_pil_foot{i}", x - 1.4, x + 1.4, gy0 - 1.0, gy0 + 2.0, 0.0, 1.6, "rust", 0.15))
    # ---- the small generator, south-west
    main.append(K.obox("genr", (-22.0, -10.5), 10.0, 6.0, 4.0, 0.0, 5.0, "stone_dk", 0.5, 0.9))
    main.append(K.obox("genr_top", (-22.0, -10.5), 10.0, 4.0, 2.6, 4.8, 6.2, "steel", 0.3))
    for i, (dx, dy) in enumerate(((-5.0, -3.5), (-1.5, -4.2), (2.0, -3.6), (5.0, -3.0))):
        main.extend(K.lamp(f"genr_lamp{i}", (-22.0 + dx, -11.0 + dy, 5.3), 0.7, "red"))
    main.append(K.beam("genr_pipe", (-22.0, -3.5, 5.0), (-3.0, -2.0, 5.0), 2.6, 2.0, "white", 0.2))
    main.extend(K.lamp("lamp0", (posts[0], gy0 + 0.5, gz + 4.3), 0.6))
    main.extend(K.lamp("lamp1", (posts[1], gy0 + 0.5, gz + 4.3), 0.6))
    if damaged:
        K.hole(main, [], wing_pt(0.8, 0.5)[:2], 3.5, wing_pt(0.8, 0.5, 0.9)[2], 5, depth=3.0, panel=("rust", "skirt"), rib="copper_dk", cut_up=8.0)
        K.scorch(main, dc, 4.0, lambda x, y: dhgt + 2.0 + 6.0 * math.sqrt(max(0.05, 1 - ((x - dc[0]) ** 2 + (y - dc[1]) ** 2) / 49.0)), 7, 4)
        K.scorch(main, (22.0, 6.0), 5.0, 10.0, 8, 4)
        K.hole(main, [], (10.0, 20.0), 4.5, bz + 0.9, 12, depth=3.0, panel=("block", "stone"))
        K.scorch(main, (2.0, 10.0), 5.0, bz + 1.0, 13, 5)
        K.wall_gash(main, (10.0, y0 - 0.2), 0.0, 5.0, 4.0, 10.0, 9)
        K.wall_gash(main, (20.0, -12.2), 0.0, 3.0, 1.5, 4.5, 10)
    return dict(nodes=dict(main=main, glow=glow),
                anchors=[("fx_light_0", (posts[0], gy0 + 0.5, gz + 4.9), 0.5), ("fx_light_1", (posts[1], gy0 + 0.5, gz + 4.9), 0.5)] + danch)


def wing_z(x, y):
    """Height of the wing's upper face over (x, y)."""
    a, b, w = P["wing_a"], P["wing_b"], P["wing_w"]
    ux, uy = b[0] - a[0], b[1] - a[1]
    det = ux * w[1] - uy * w[0]
    s = ((x - a[0]) * w[1] - (y - a[1]) * w[0]) / det
    t = (ux * (y - a[1]) - uy * (x - a[0])) / det
    return a[2] + (b[2] - a[2]) * s + w[2] * t


if __name__ == "__main__":
    BD.install(K, path=(0.900, 0.870, 0.790))
    K.run(KEY, build, spec=lambda: dict(flags=dict(shadow_azimuth=P["sh_az"], sun_elevation=P["sun_el"], fill=(0.3, 90.0, 35.0), sky_strength=0.09, sun_strength=4.2)), P=P)

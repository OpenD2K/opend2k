"""Harkonnen refinery - built with tools/models/harkonnen_kit.py on the layout of the Atreides refinery pilot
(tools/models/refinery_pilot_fable.py: the two houses' pictures overlap 0.94 - same drum centres, tank, bridge and
unloading pad on the dock cell).

    blender -b --factory-startup --python tools/models/harkonnen_refinery.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=HARKONNEN_REFINERY .venv/bin/python -I tools/models/harkonnen_check/pack.py

How the original was read (frame 3379 base: under units - the drums' feet, the dark tank, the unloading pad; 3380
main; 3381 damaged; no animation; build-up 20 kept):
  * two identical drums on a diagonal (ground centres (-21, -1) and (22.7, 35), r 24): a blackened plated wall on
    a pale concrete foot, and - unlike the Atreides' flat ribbed roofs - a DOME of dark gun-metal with thin brass
    meridian ribs and a pale sheen; a house-colour armour patch on the west drum's south-west shoulder, a house
    door in its wall, small house marks on the east dome, a house bar on its wall;
  * heavy brown chimneys: one on each dome's crown (the east one with a double-disc cap) and the tallest rising
    from the near-black tank between the drums and the pad;
  * between the drums the bridge block with its house-colour roof and a small steel dome tank behind it;
  * on the dock cell the unloading pad: a pale cream tray in a dark steel frame, turned 45 degrees; brown struts
    between pad and tank.
  * NOT in the original (as in the Atreides pilot): the harvester inlet - a steel socket on the tall chimney at
    z 16.3 pointing south-east at the dock, where the docked harvester's pipe (tools/models/harvester_v1.py) ends;
    it is in the main node, so it is drawn over the harvester. Same position as the Atreides refinery's.
Anchors: fx_flue_0..2 = the three chimney tops (the refinery's fire / smoke).
Two layouts (docs/3d-models.md "Docking"): the default build is the 3D layout (the glb in the pack: layout_3d() -
laid out on the cells round the dock, tools/models/refinery_dock.py); the pack's 2D frames are rendered from
`--set inlet_2d=1` (the picture's layout, described above).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harkonnen_kit as K  # noqa: E402
import refinery_dock as RD  # noqa: E402

KEY = "HARKONNEN_REFINERY"
P = dict(
    d1=(-20.9, -1.0), d2=(22.7, 35.0), r_foot=25.4, r_wall=23.2, wall_h=11.0, r_dome=22.2, dome_h=15.5,
    tank=(11.5, 5.0), tank_r=5.2, tank_h=12.5,
    stack_w=55.0, stack_e=60.0, stack_t=80.0,
    bridge_c=(0.5, 17.75), bridge_half=11.0, bridge_w=8.5, bridge_h=26.0,
    pad_c=(28.1, -2.3), pad_len=17.0, pad_wid=15.0,
    inlet_mouth=(14.5, 2.6), inlet_z=16.3,               # the 2D layout (inlet_2d=1): where the harvester SPRITE's pipe ends
    # the 3D layout (the pack's glb; as tools/models/refinery_pilot_fable.py inlet_3d): the socket's mouth where the
    # pipe of a harvester standing on the pad ends in Graphics: 3D - in the corner of the bridge and the east drum
    inlet_3d=(10.6, 15.2, 11.95), inlet_2d=0,
    dtank=(-5.0, 35.0),                                  # the small steel dome tank behind the bridge
    pad_h=2.3, layout=0, drum_k=1.0,                     # layout = 1: layout_3d() applied
)


def layout_3d():
    """The 3D layout (the pack's glb), as the Atreides refinery's (refinery_pilot_fable.layout_3d): the pad on the
    dock cell's centre (an octagonal tray cut to the footprint, nothing on it higher than the plate the harvester
    stands on), the harvester's turning circle free, the inlet socket where the centred harvester's pipe ends - on
    the north drum's eave. The drums are 0.8 of the picture's radius (same heights) and stand steeper, the bridge
    is shorter, the tank with the tall chimney stands north of the pad, the dome tank in the drums' north-west
    crook."""
    if P["inlet_2d"] or P["layout"]:
        return
    k = 0.8
    old = RD.dist(P["d1"], P["d2"])
    old_d1 = P["d1"]
    for key in ("r_foot", "r_wall", "r_dome"):
        P[key] *= k
    rf = P["r_foot"] + 0.6
    d1, d2 = RD.drums(rf, rf, gap=2.0, edge=0.4, air=0.4)
    tr = P["tank_r"] + 0.9
    P.update(layout=1, drum_k=k, d1=d1, d2=d2, old_d1=old_d1,
             bridge_c=((d1[0] + d2[0]) / 2, (d1[1] + d2[1]) / 2),
             bridge_half=RD.dist(d1, d2) / 2 - (old / 2 - P["bridge_half"]) * k,
             dtank=RD.crook(d1, d2, rf + 6.9 - 3.6),
             tank=RD.nook(d2, rf + tr + 0.6, RD.K_LOW + tr + 1.2),
             pad_c=RD.DOCK,
             inlet_3d=(*RD.mouth(), P["pad_h"] + RD.NOZZLE_UP))


def dome_z(c, x, y):
    r, h = P["r_dome"], P["dome_h"]
    return P["wall_h"] + 0.6 + h * math.sqrt(max(0.02, 1 - ((x - c[0]) ** 2 + (y - c[1]) ** 2) / (r * r)))


def drum(base, main, tag, c, damaged):
    rf, rw, wh, rd, dh = P["r_foot"], P["r_wall"], P["wall_h"], P["r_dome"], P["dome_h"]
    base.append(K.cyl(f"{tag}_foot", c, rf + 0.6, 0.0, 2.6, "white", 48, rf - 0.6))
    base.append(K.cyl(f"{tag}_wall", c, rf - 0.8, 2.2, wh, "stone_dk", 48, rw))
    for k in range(24):
        a = 7.5 + 15.0 * k
        base.append(K.beam(f"{tag}_pier{k}", K.polar(c, rf - 0.6, a, 2.4), K.polar(c, rw + 0.15, a, wh - 0.4), 1.5, 1.0, "stone" if k % 2 else "hk_black", 0.1))
    main.append(K.lathe_loop(f"{tag}_eave", c, [(rw + 0.9, wh - 0.6), (rw + 0.9, wh + 0.9), (rd - 0.6, wh + 0.9), (rd - 0.6, wh - 0.6)], "hk_brass", 48))
    dome = K.dome(f"{tag}_dome", c, rd, wh + 0.6, dh, "roof", 48, 9)
    main.append(dome)
    for k in range(8):                                   # brass meridian ribs
        a = 22.5 + 45.0 * k
        pts = [K.polar(c, rd * t, a) for t in (0.14, 0.4, 0.62, 0.8, 0.93, 1.0)]
        for j in range(5):
            p, q = pts[j], pts[j + 1]
            main.append(K.beam(f"{tag}_mer{k}_{j}", (*p, dome_z(c, *p) + 0.1), (*q, dome_z(c, *q) + 0.1), 0.7, 0.5, "hk_brass"))
    main.append(K.lathe_loop(f"{tag}_ring", c, [(rd * 0.62, dome_z(c, c[0] + rd * 0.62, c[1]) - 0.2), (rd * 0.62, dome_z(c, c[0] + rd * 0.62, c[1]) + 0.5),
                                               (rd * 0.58, dome_z(c, c[0] + rd * 0.58, c[1]) + 0.5), (rd * 0.58, dome_z(c, c[0] + rd * 0.58, c[1]) - 0.2)], "hk_shell", 48))
    main.append(K.cyl(f"{tag}_hub", c, 4.6, wh + dh - 0.6, wh + dh + 1.8, "copper_dk", 16))
    return dome


def shoulder_patch(main, name, c, a0, a1, t0, t1, k):
    import bmesh
    rd = P["r_dome"]
    n = 6
    pts_o = [K.polar(c, rd * t1, a0 + (a1 - a0) * j / n) for j in range(n + 1)]
    pts_i = [K.polar(c, rd * t0, a1 + (a0 - a1) * j / n) for j in range(n + 1)]
    ring = pts_o + pts_i
    bm = bmesh.new()
    K.rings_solid(bm, [[(x, y, dome_z(c, x, y) - 0.4) for (x, y) in ring], [(x, y, dome_z(c, x, y) + 0.4) for (x, y) in ring]])
    main.append(K.house(K.new_obj(name, bm, "HOUSE"), k))


def build(damaged=False):
    layout_3d()
    base, main = [], []
    d1, d2 = P["d1"], P["d2"]
    dome1 = drum(base, main, "dw", d1, damaged)
    dome2 = drum(base, main, "de", d2, damaged)
    wh, dh = P["wall_h"], P["dome_h"]
    shoulder_patch(main, "dw_patch", d1, 196.0, 232.0, 0.52, 0.86, 0.45)
    shoulder_patch(main, "de_mark0", d2, 250.0, 268.0, 0.30, 0.44, 0.3)
    shoulder_patch(main, "de_mark1", d2, 250.0, 268.0, 0.50, 0.60, 0.3)
    main.append(K.house(K.arc("dw_door", d1, P["r_wall"] + 0.2, P["r_wall"] + 1.4, 262.0, 276.0, 2.6, 9.6, "HOUSE", 4), 0.16))
    main.append(K.house(K.arc("de_bar", d2, P["r_wall"] + 0.2, P["r_wall"] + 1.4, 284.0, 290.0, 2.6, 10.0, "HOUSE", 3), 0.25))
    main.append(K.house(K.arc("dw_line", d1, P["r_dome"] * 0.985, P["r_dome"] * 0.985 + 0.9, 300.0, 345.0, wh + 1.2, wh + 2.4, "HOUSE", 8), 0.25))
    # ---- chimneys on the crowns
    tops = []
    for i, (c, h, cap) in enumerate(((d1, P["stack_w"], False), (d2, P["stack_e"], True))):
        st, top = K.stack(f"stack{i}", c, 2.7, wh + dh, h, "copper", 2)
        main.extend(st)
        if cap:                                          # the double-disc cap of the east chimney
            for j, z in enumerate((h - 9.0, h - 4.5)):
                main.append(K.cyl(f"stack{i}_disc{j}", c, 4.6, z, z + 1.4, "copper_dk", 14))
        tops.append(top)
    # ---- the near-black tank, the tall chimney on it, the harvester inlet
    tk = P["tank"]
    base.append(K.cyl("tank", tk, P["tank_r"], 0.0, P["tank_h"], "tank", 24))
    base.append(K.cyl("tank_foot", tk, P["tank_r"] + 0.9, 0.0, 1.4, "hk_black", 24))
    main.append(K.cyl("tank_collar", tk, P["tank_r"] + 0.3, P["tank_h"] - 0.4, P["tank_h"] + 1.0, "steel_dk", 24))
    sc = (tk[0] + 0.6, tk[1])
    st, top = K.stack("stack2", sc, 2.4, P["tank_h"], P["stack_t"], "copper", 3)
    main.extend(st)
    tops.append(top)
    main.append(K.tube("stack2_twin", (sc[0] - 3.0, sc[1] - 0.2, P["tank_h"]), (sc[0] - 3.0, sc[1] - 0.2, P["stack_t"] - 12.0), 1.2, "cream_pipe", 8))
    d = (math.sqrt(0.5), -math.sqrt(0.5))
    if P["inlet_2d"]:
        main.append(K.cyl("inlet_clamp", sc, 2.9, P["inlet_z"] - 2.2, P["inlet_z"] + 2.2, "copper_dk", 14))
        mouth = (sc[0] + d[0] * 3.4, sc[1] + d[1] * 3.4)   # = the Atreides pilot's inlet mouth (flue axis + 3.4 px south-east)
        main.extend(K.dock_socket("inlet", mouth, P["inlet_z"], d, 2.2))
        main.append(K.tube("inlet_strut", (mouth[0] - d[0] * 1.2, mouth[1] - d[1] * 1.2, P["inlet_z"] - 2.4), (sc[0] + d[0] * 2.4, sc[1] + d[1] * 2.4, P["tank_h"] + 0.8), 0.6, "copper_dk", 8))
    else:
        mx, my, mz = P["inlet_3d"]
        main.extend(K.dock_socket("inlet", (mx, my), mz, d, 2.2))
        bk = (mx - d[0] * 2.2, my - d[1] * 2.2)
        main.append(K.tube("inlet_collar", (bk[0] - d[0] * 2.2, bk[1] - d[1] * 2.2, mz), (bk[0] + d[0] * 0.5, bk[1] + d[1] * 0.5, mz), 3.4, "steel_dk", 14))
        iw = ((d2[0] - mx) / RD.dist(d2, (mx, my)), (d2[1] - my) / RD.dist(d2, (mx, my)))   # towards the drum's axis
        main.append(K.tube("inlet_foot", (mx - d[0] * 1.5, my - d[1] * 1.5, mz - 2.5), (mx - d[0] * 1.5 + iw[0] * 1.6, my - d[1] * 1.5 + iw[1] * 1.6, 2.6), 0.7, "copper_dk", 8))
    # ---- the bridge between the drums: house roof, the steel dome tank behind it
    bc, bl, bw, bh = P["bridge_c"], P["bridge_half"], P["bridge_w"], P["bridge_h"]
    ang = math.degrees(math.atan2(d2[1] - d1[1], d2[0] - d1[0]))
    main.append(K.obox("bridge", bc, ang, bl, bw, 0.0, bh, "stone_dk", 0.6, 0.94))
    main.append(K.obox("bridge_rim", bc, ang, bl * 0.94 + 0.4, bw * 0.94 + 0.4, bh - 1.0, bh + 0.5, "hk_black", 0.2))
    for i, t in enumerate((-5.2 * bl / 11.0, 0.0, 5.2 * bl / 11.0)):
        q = (bc[0] + math.cos(math.radians(ang)) * t, bc[1] + math.sin(math.radians(ang)) * t)
        main.append(K.plate(f"bridge_roof{i}", q, ang, 2.3 * bl / 11.0, bw * 0.84, bh + 1.3, 0.34 if i != 1 else 0.24, 1.6))
    vc = (bc[0] + 5.5, bc[1] - 6.6)
    if P["layout"]:                                      # the middle of the south-east wall
        vc = (bc[0] + math.sin(math.radians(ang)) * 8.59, bc[1] - math.cos(math.radians(ang)) * 8.59)
    main.extend(K.vent("bridge_vent", vc, ang, 3.0, 12.0, 18.0, 0.5, 3, "stone"))
    tc = P["dtank"]
    main.append(K.cyl("dtank_drum", tc, 6.6, 0.0, 16.0, "stone_dk", 20))
    main.append(K.dome("dtank", tc, 6.4, 15.6, 5.2, "roof", 20))
    main.append(K.lathe_loop("dtank_band", tc, [(6.9, 13.0), (6.9, 14.4), (6.2, 14.4), (6.2, 13.0)], "hk_brass", 20))
    # ---- the unloading pad on the dock cell (base: the harvester stands on it)
    pc, hl, hw, ph = P["pad_c"], P["pad_len"], P["pad_wid"], P["pad_h"]
    tray = [K.obox("pad_apron", pc, 45.0, hl, hw, 0.0, 0.9, "pad_dk", 0.1),
            K.obox("pad_floor", (pc[0] + 2.4, pc[1] - 2.4), 45.0, hl - 2.0, hw - 4.6, 0.0, 1.35, "pad_rim", 0.1)]
    ax, ay = math.cos(math.radians(45)), math.sin(math.radians(45))
    nx, ny = -ay, ax
    for s in (-1, 1):
        tray.append(K.obox(f"pad_rimL{s}", (pc[0] + nx * s * (hw - 0.8), pc[1] + ny * s * (hw - 0.8)), 45.0, hl, 0.8, 0.0, ph, "steel_dk", 0.25))
        tray.append(K.obox(f"pad_rimS{s}", (pc[0] + ax * s * (hl - 0.8), pc[1] + ay * s * (hl - 0.8)), 45.0, 0.8, hw, 0.0, ph, "steel_dk", 0.25))
    for k in (-0.5, 0.5):
        q = (pc[0] + 2.4 + ax * k * hl * 0.6, pc[1] - 2.4 + ay * k * hl * 0.6)
        tray.append(K.obox(f"pad_seam{k}", q, 45.0, 0.1, hw - 4.8, 1.3, 1.42, "recess"))
    if P["layout"]:
        # 3D layout: the corners cut where the tray would leave the footprint (an octagon, the cut edges with their
        # rim); the harvester stands on a round plate as high as the rims, so nothing of the pad is above it
        reach = RD.BOX - pc[0]
        tray = RD.clip_plan(tray, pc, reach)
        for i, (cc, a_, half) in enumerate(RD.pad_chamfers(pc, hl, hw, reach)):
            cc = (cc[0] - 0.8 * (1 if cc[0] > pc[0] + reach - 0.1 else -1 if cc[0] < pc[0] - reach + 0.1 else 0),
                  cc[1] - 0.8 * (1 if cc[1] > pc[1] + reach - 0.1 else -1 if cc[1] < pc[1] - reach + 0.1 else 0))
            tray.append(K.obox(f"pad_rimC{i}", cc, a_, half + 0.4, 0.8, 0.0, ph, "steel_dk", 0.25))
        tray.append(K.cyl("pad_plate", pc, 4.6, 1.2, ph, "pad_rim", 24, 4.3))
        tray.append(K.cyl("pad_plate_hub", pc, 1.6, ph - 0.1, ph + 0.03, "steel_dk", 12))
    base.extend(tray)
    if P["inlet_2d"]:                                    # (in 3D the docked harvester's rear is here)
        for i, (p, q) in enumerate((((20.0, 8.0, 14.0), (17.0, 1.0, 0.3)), ((20.0, 8.0, 14.0), (25.0, 4.0, 0.3)), ((20.0, 8.0, 14.0), (21.0, 12.0, 0.3)))):
            base.append(K.beam(f"strut{i}", p, q, 1.5, 1.5, "copper", 0.2))
        base.append(K.ball("strut_head", (20.0, 8.0, 14.4), 1.8, "copper_dk", 10))
    # ---- rubble at the south-west foot, lamp on the north-east
    rub = ((-33.0, -23.0), (-24.0, -27.0))
    if P["layout"]:                                      # the same heaps at the (smaller, moved) west drum's foot
        o1, k_ = P["old_d1"], P["drum_k"]
        rub = tuple((d1[0] + (x - o1[0]) * k_, d1[1] + (y - o1[1]) * k_) for (x, y) in rub)
    base.extend(K.rocks("rub_sw", rub[0], 6.0, 21, 6, 0.6))
    base.extend(K.rocks("rub_s", rub[1], 3.4, 22, 3, 0.6))
    main.extend(K.lamp("lamp_ne", (*K.polar(d2, P["r_dome"] * 0.9, 40.0), dome_z(d2, *K.polar(d2, P["r_dome"] * 0.9, 40.0)) + 0.6), 1.2, "glow"))
    if damaged:
        K.hole(main, [dome1], (d1[0] + 5.0, d1[1] + 2.0), 6.5, lambda x, y: dome_z(d1, x, y), 3, depth=7.0, panel=("roof", "roof2"), rib="hk_brass")
        K.hole(main, [dome2], (d2[0] - 6.0, d2[1] - 5.0), 6.0, lambda x, y: dome_z(d2, x, y), 5, depth=7.0, panel=("roof", "roof2"), rib="hk_brass")
        K.hole(main, [dome2], (d2[0] + 9.0, d2[1] + 4.0), 3.6, lambda x, y: dome_z(d2, x, y), 6, depth=6.0, panel=("roof", "roof2"), rib="hk_brass")
        K.scorch(main, bc, 6.0, bh + 2.0, 7, 5)
        K.wall_gash(main, K.polar(d1, P["r_wall"] + 0.6, 235.0), 145.0, 5.0, 3.0, 9.5, 8, 0.9)
        K.scorch(main, (d1[0] - 8.0, d1[1] - 8.0), 5.0, lambda x, y: dome_z(d1, x, y), 9, 5)
    anchors = [(f"fx_flue_{i}", t, s) for i, (t, s) in enumerate(zip(tops, (0.8, 0.9, 0.65)))]
    if not P["inlet_2d"]:                                # docs/3d-models.md "Docking": 1.6 px inside the socket's mouth
        mx, my, mz = P["inlet_3d"]
        anchors.append(("fx_inlet_0", (mx - d[0] * 1.6, my - d[1] * 1.6, mz), 1.0))
    return dict(nodes=dict(base=base, main=main), anchors=anchors)


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(                                flags=dict(shadow_azimuth=37.0, sun_elevation=50.0, fill=(0.35, 90.0, 35.0), sky_strength=0.1)), P=P)

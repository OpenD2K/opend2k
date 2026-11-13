"""Ordos refinery - the original design rebuilt with tools/models/ordos_kit.py.

    blender -b --factory-startup --python tools/models/ordos_refinery.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ORDOS_REFINERY .venv/bin/python -I tools/models/ordos_check/pack.py

How the original was read (art 42: base 3653 = drawn under units: the yellow unloading pad on the dock cell (2, 1),
the bright gold foot of the left drum, the dark ground between; main 3654; damaged 3655; no animation; build-up 20
kept):
  * two round drums: the lower left one (r 23) on a flared, brightly lit gold foot, with a pillared wall, a dark
    ribbed deck with radiating house-colour strips, a gold dome in its centre and a bronze stack; the taller right
    one (r 22) behind the dock with a mottled brown deck, a bigger gold dome and the tallest stack, a house panel
    on its east wall, teal-black glass orbs on its front;
  * between them a grey sloping duct and, behind it, a standing house-colour ring;
  * the unloading pad: a yellow tray with a brown rim turned 45 degrees, on the dock cell;
  * orange rubble at the south-west foot.
NOT in the original: the unloading inlet (ordos_kit.inlet) - the steel socket where tools/models/harvester_v1.py's
pipe ends, at the same place as on the Atreides refinery (main node: drawn over the docked harvester), on a bronze
standpipe plumbed into the right drum. Anchors fx_flue_0 / fx_flue_1 = the two open stack tops.
Two layouts (docs/3d-models.md "Docking"): the default build is the 3D layout (the glb in the pack: layout_3d() -
laid out on the cells round the dock, tools/models/refinery_dock.py); the pack's 2D frames are rendered from
`--set inlet_2d=1` (the picture's layout, described above).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ordos_kit as K  # noqa: E402
import refinery_dock as RD  # noqa: E402

KEY = "ORDOS_REFINERY"
P = dict(
    d1=(-23.0, 3.5), r1=22.0, h1=14.0, dome1=7.0, stack1=44.0,
    d2=(23.0, 33.0), r2=21.5, h2=26.0, dome2=9.5, stack2=62.0,
    pad_c=(36.0, -4.0), pad_len=18.0, pad_wid=11.0, pad_h=2.3,
    ring=(-6.0, 40.0),
    # the harvester inlet's mouth in the 3D layout (the pack's glb): where the pipe of a harvester standing on the pad
    # ends in Graphics: 3D - on the right drum's wall (docs/3d-models.md "Docking"). inlet_2d=1: the 2D layout the
    # pack's frames are rendered from (ordos_kit.inlet's default mouth: where the harvester SPRITE's pipe ends).
    inlet_3d=(13.34, 11.58, 12.8), inlet_2d=0,
    layout=0, drum_k=1.0,                                  # layout = 1: layout_3d() applied
)
B = None                                                   # 3D layout: maps the parts between the drums (RD.Between)


def layout_3d():
    """The 3D layout (the pack's glb), as the Atreides refinery's (refinery_pilot_fable.layout_3d): the pad on the
    dock cell's centre (an octagonal tray cut to the footprint, nothing on it higher than the plate the harvester
    stands on), the harvester's turning circle free (no dark ground skirt there), the inlet socket where the
    centred harvester's pipe ends - on the north drum's wall. The drums are 0.8 of the picture's radius (same
    heights) and stand steeper; the duct, its leg and the house ring between them turn with them; the standpipe
    stands north of the pad and feeds the north drum."""
    global B
    if P["inlet_2d"] or P["layout"]:
        return
    k = 0.8
    o1, o2 = P["d1"], P["d2"]
    for key in ("r1", "r2", "dome1", "dome2"):
        P[key] *= k
    d1, d2 = RD.drums(P["r1"] + 4.6, P["r2"] + 2.8, gap=1.0, edge=0.4, air=0.4)
    B = RD.Between(o1, o2, d1, d2, k)
    P.update(layout=1, drum_k=k, d1=d1, d2=d2, old_d1=o1, ring=B.pt(P["ring"]), pad_c=RD.DOCK,
             inlet_3d=(*RD.mouth(), P["pad_h"] + RD.NOZZLE_UP))


def drum(base, main, tag, c, r, h, dome_r, stack_h, strips, damaged, holes):
    main.append(K.cyl(f"{tag}_wall", c, r - 1.0, 0.0, h - 1.0, "block", 40))
    main.append(K.cyl(f"{tag}_belt", c, r - 0.6, h * 0.42, h * 0.62, "block_dk", 40))
    for k in range(20):
        a = 9.0 + 18.0 * k
        main.append(K.beam(f"{tag}_pier{k}", K.polar(c, r - 0.2, a, 1.5), K.polar(c, r - 0.2, a, h - 1.2), 1.7, 1.2, "rib" if k % 2 else "bronze"))
    main.append(K.lathe_loop(f"{tag}_rim", c, [(r - 1.4, h - 1.6), (r + 0.6, h - 1.6), (r + 0.9, h + 0.2), (r - 1.4, h + 0.6)], "roof2", 40))
    deck = K.lathe(f"{tag}_deck", c, [(r - 1.0, h - 0.4), (r - 1.4, h + 0.3), (dome_r + 3.0, h + 2.4), (dome_r, h + 2.6)], "bronze_dk" if strips else "skirt", 40)
    main.append(deck)

    def zf(x, y):
        d = math.hypot(x - c[0], y - c[1])
        return h + 0.3 + 2.2 * max(0.0, min(1.0, (r - 1.4 - d) / (r - 4.4 - dome_r)))
    for k in range(16):                                    # radial ribs; house-colour strips between them on the left drum
        a = 22.5 * k
        p, q = K.polar(c, r - 2.0, a), K.polar(c, dome_r + 2.0, a)
        main.append(K.beam(f"{tag}_drib{k}", (*p, zf(*p) + 0.1), (*q, zf(*q) + 0.1), 1.0, 0.5, "rib"))
        if strips and k % 2 == 0:
            p, q = K.polar(c, r - 4.0, a + 11.0), K.polar(c, dome_r + 5.0, a + 11.0)
            main.append(K.house(K.beam(f"{tag}_strip{k}", (*p, zf(*p) + 0.1), (*q, zf(*q) + 0.1), 1.9, 0.5, "HOUSE"), 0.4))
    main.append(K.lathe_loop(f"{tag}_dhoop", c, [(r * 0.66, zf(c[0] + r * 0.66, c[1]) - 0.1), (r * 0.72, zf(c[0] + r * 0.72, c[1]) - 0.1),
                                                 (r * 0.72, zf(c[0] + r * 0.72, c[1]) + 0.6), (r * 0.66, zf(c[0] + r * 0.66, c[1]) + 0.6)], "bronze", 40))
    main.extend(K.rdome(f"{tag}_dome", c, dome_r, h + 2.4, dome_r * 0.85, "gold", ribs=0, segs=24))
    fl, top = K.flue(f"{tag}_stack", c, 1.7, h + 2.4 + dome_r * 0.6, stack_h, "bronze", "bronze_dk")
    main.extend(fl)
    main.append(K.cyl(f"{tag}_stack_foot", c, 2.8, h + 2.4 + dome_r * 0.7, h + 2.4 + dome_r * 0.85 + 2.0, "bronze_dk", 12, 2.2))
    if damaged:
        for i, (a, d, rr) in enumerate(holes):
            K.hole(main, [deck], K.polar(c, d, a), rr, zf, (7 if strips else 17) + i, depth=4.5, panel=("bronze", "roof2"), cut_up=6.0)
    return top


def build(damaged=False):
    layout_3d()
    L3 = bool(P["layout"])

    def bt(q):                                             # a point between the drums (2D layout: as it is)
        return B.pt(q) if L3 else q
    base, main = [], []
    d1, r1, h1 = P["d1"], P["r1"], P["h1"]
    d2, r2, h2 = P["d2"], P["r2"], P["h2"]
    # the left drum's flared gold foot (base frame) and the dark ground between the drums and the pad
    base.append(K.lathe("d1_foot", d1, [(r1 + 4.6, 0.0), (r1 + 4.0, 1.6), (r1 + 1.6, 4.2), (r1 - 1.2, 4.8)], "white", 44))
    base.append(K.lathe_loop("d1_foot_band", d1, [(r1 + 1.9, 3.6), (r1 + 2.5, 3.4), (r1 + 1.9, 4.5), (r1 + 1.2, 4.6)], "roof", 44))
    for k in range(12):
        a = 185.0 + 13.0 * k
        base.append(K.beam(f"d1_foot_rib{k}", K.polar(d1, r1 + 4.4, a, 0.4), K.polar(d1, r1 + 0.6, a, 4.6), 1.1, 0.6, "roof2"))
    if not L3:                                             # (3D: the harvester turns there)
        base.extend(K.skirt("ground", [(-4.0, -12.0), (12.0, -12.0), (20.0, 2.0), (44.0, 14.0), (47.0, 30.0), (30.0, 22.0), (4.0, 22.0), (-6.0, 6.0)], 2.2, 3, 1.5,
                            material="block_dk", dark="skirt", lumps=6))
    rub = (-30.0, -20.0)
    if L3:
        rub = (d1[0] + (rub[0] - P["old_d1"][0]) * 0.9, d1[1] + (rub[1] - P["old_d1"][1]) * 0.9)
    base.extend(K.rubble("rub", rub, 6.5, 4, 6))
    top1 = drum(base, main, "d1", d1, r1, h1, P["dome1"], P["stack1"], True, damaged, ((210.0, 13.0, 5.5), (40.0, 14.0, 3.6)))
    top2 = drum(base, main, "d2", d2, r2, h2, P["dome2"], P["stack2"], False, damaged, ((250.0, 13.0, 6.0), (130.0, 14.0, 4.0)))
    main.extend(K.skirt("d2_rock", [K.polar(d2, r2 + 0.5, 20.0 * i) for i in range(18)], 4.0, 6, 2.0, lumps=10))
    main.append(K.house(K.arc("d2_panel", d2, r2 - 0.6, r2 + 0.3, -22.0, 4.0, h2 * 0.35, h2 * 0.85, "HOUSE", 6), 0.32))
    main.append(K.house(K.arc("d2_panel2", d2, r2 - 0.6, r2 + 0.3, 196.0, 212.0, h2 * 0.45, h2 * 0.8, "HOUSE", 6), 0.4))
    for i, a in enumerate((238.0, 300.0) if P["inlet_2d"] else (232.0, 282.0)):   # (3D: clear of the inlet and the dock)
        main.extend(K.orb(f"d2_orb{i}", K.polar(d2, r2 + 1.0, a, h2 * 0.42), 2.6))
    for i in range(4):
        a = 222.0 + 26.0 * i
        main.extend(K.vent(f"d2_vent{i}", K.polar(d2, r2 - 0.9, a + 13.0), a + 103.0, 2.2, h2 * 0.66, h2 * 0.84, 0.5, 2, "roof2"))
    main.append(K.house(K.arc("d1_panel", d1, r1 - 0.8, r1 + 0.1, 262.0, 284.0, 6.0, h1 - 2.0, "HOUSE", 6), 0.35))
    # the grey duct between the drums, the standing house-colour ring behind it
    a0, a1 = (-9.0, 28.0, 23.0), (9.0, 20.0, 12.0)
    dk = P["drum_k"]
    main.append(K.beam("duct", bt(a0), bt(a1), 9.0 * dk, 4.0, "grey", 0.4))
    main.append(K.beam("duct_top", bt((a0[0], a0[1], a0[2] + 2.2)), bt((a1[0], a1[1], a1[2] + 2.2)), 6.0 * dk, 0.6, "grey_dk"))
    for s in (-1, 1):
        main.append(K.beam(f"duct_rail{s}", bt((a0[0] + s * 1.6, a0[1] + s * 3.8, a0[2] + 2.4)), bt((a1[0] + s * 1.6, a1[1] + s * 3.8, a1[2] + 2.4)), 0.9, 0.9, "bronze_dk"))
    main.append(K.obox("duct_leg", bt((0.0, 24.0)), B.ang(-24.0) if L3 else -24.0, 5.0 * dk, 4.0 * dk, 0.0, 15.0, "block", 0.4))
    rc = P["ring"]
    main.append(K.cyl("ring_post", rc, 5.0, 0.0, 15.0, "block_dk", 20))
    main.append(K.house(K.lathe_loop("ring", rc, [(3.6, 14.5), (6.4, 14.5), (6.4, 18.5), (3.6, 18.5)], "HOUSE", 24), 0.33))
    main.append(K.cyl("ring_bore", rc, 3.7, 14.0, 16.6, "burnt", 20))
    main.append(K.lathe_loop("ring_lip", rc, [(6.3, 18.2), (6.9, 18.2), (6.9, 19.0), (6.3, 19.0)], "bronze_dk", 24))
    # ---- the unloading pad (base frame): a yellow tray with a brown rim, turned 45 degrees
    c, hl, hw, ph = P["pad_c"], P["pad_len"], P["pad_wid"], P["pad_h"]
    ax, ay = math.cos(math.radians(45)), math.sin(math.radians(45))
    nx, ny = -ay, ax
    tray = [K.obox("pad_floor", c, 45.0, hl - 0.8, hw - 0.8, 0.0, 1.3, "padf")]
    for s in (-1, 1):
        tray.append(K.obox(f"pad_rimL{s}", (c[0] + nx * s * (hw - 0.8), c[1] + ny * s * (hw - 0.8)), 45.0, hl, 0.9, 0.0, ph, "rim", 0.25))
        tray.append(K.obox(f"pad_rimS{s}", (c[0] + ax * s * (hl - 0.8), c[1] + ay * s * (hl - 0.8)), 45.0, 0.9, hw, 0.0, ph, "rim", 0.25))
        if L3:                                             # flush with the rim: nothing above the harvester's plate
            tray.append(K.obox(f"pad_trim{s}", (c[0] + nx * s * (hw - 0.8), c[1] + ny * s * (hw - 0.8)), 45.0, hl - 1.0, 0.35, ph - 0.5, ph + 0.02, "roof"))
        else:
            tray.append(K.obox(f"pad_trim{s}", (c[0] + nx * s * (hw - 0.8), c[1] + ny * s * (hw - 0.8)), 45.0, hl - 1.0, 0.35, ph - 0.1, ph + 0.4, "roof"))
    for k in (-0.5, 0.0, 0.5):
        tray.append(K.obox(f"pad_seam{k}", (c[0] + ax * k * hl, c[1] + ay * k * hl), 45.0, 0.1, hw - 1.6, 1.25, 1.36, "recess"))
    if L3:
        # 3D layout: the corners cut where the tray would leave the footprint (an octagon, the cut edges with their
        # rim), no bollards; the harvester stands on a round plate as high as the rims
        reach = RD.BOX - c[0]
        tray = RD.clip_plan(tray, c, reach)
        for i, (cc, a_, half) in enumerate(RD.pad_chamfers(c, hl, hw, reach)):
            cc = (cc[0] - 0.9 * (1 if cc[0] > c[0] + reach - 0.1 else -1 if cc[0] < c[0] - reach + 0.1 else 0),
                  cc[1] - 0.9 * (1 if cc[1] > c[1] + reach - 0.1 else -1 if cc[1] < c[1] - reach + 0.1 else 0))
            tray.append(K.obox(f"pad_rimC{i}", cc, a_, half + 0.5, 0.9, 0.0, ph, "rim", 0.25))
        tray.append(K.cyl("pad_plate", c, 4.6, 1.2, ph, "padf", 24, 4.3))
        tray.append(K.cyl("pad_plate_hub", c, 1.6, ph - 0.1, ph + 0.03, "bronze_dk", 12))
    base.extend(tray)
    if not L3:
        for sl in (-1, 1):
            for sw in (-1, 1):
                p = (c[0] + ax * sl * (hl - 0.9) + nx * sw * (hw - 0.9), c[1] + ay * sl * (hl - 0.9) + ny * sw * (hw - 0.9))
                base.append(K.lathe(f"pad_post{sl}{sw}", p, [(0.8, ph - 0.1), (0.8, ph + 1.0), (0.4, ph + 1.3)], "bronze_dk", 8))
    # ---- the unloading inlet: the socket, its standpipe, the pipe into the right drum
    if not L3:
        sock, back = K.inlet("inlet")
        if P["inlet_2d"]:
            main.extend(sock)
        sp = (back[0] - 1.6, back[1] + 1.6)
        zt = back[2]
    else:                                                  # the socket on the drum's wall, a collar into the wall, a foot
        sock3, b3 = K.inlet("inlet", P["inlet_3d"])
        main.extend(sock3)
        m3 = P["inlet_3d"]
        dn = math.hypot(d2[0] - b3[0], d2[1] - b3[1])
        inw = ((d2[0] - b3[0]) / dn, (d2[1] - b3[1]) / dn)
        main.append(K.tube("inlet_collar", (b3[0] + 0.5, b3[1] - 0.5, b3[2]), (b3[0] + inw[0] * 3.0, b3[1] + inw[1] * 3.0, b3[2] + 0.6), 3.0, "bronze_dk", 12))
        main.append(K.tube("inlet_foot", (m3[0] - 1.0, m3[1] + 1.0, m3[2] - 2.4), (m3[0] - 1.0 + inw[0] * 1.6, m3[1] + 1.0 + inw[1] * 1.6, 3.0), 0.8, "bronze_dk", 8))
        # the standpipe: north of the pad, east of the drum, outside the harvester's turning circle
        sp = RD.nook(d2, r2 + 2.8 + 3.2 + 0.4, RD.K_LOW + 3.2 + 1.2)
        zt = 16.3
    main.append(K.cyl("inlet_post", sp, 2.3, 0.0, zt + 3.0, "bronze", 14))
    main.append(K.cyl("inlet_post_cap", sp, 2.8, zt + 2.6, zt + 3.8, "bronze_dk", 14))
    main.append(K.cyl("inlet_post_foot", sp, 3.2, 0.0, 1.4, "bronze_dk", 14))
    if P["inlet_2d"]:
        main.append(K.tube("inlet_neck", (sp[0], sp[1], back[2]), back, 2.5, "bronze_dk", 12))
    if L3:
        dn = RD.dist(sp, d2)
        w = (d2[0] + (sp[0] - d2[0]) / dn * (r2 - 1.0), d2[1] + (sp[1] - d2[1]) / dn * (r2 - 1.0))
        main.append(K.tube("inlet_feed", (sp[0], sp[1], zt + 2.0), (w[0], w[1], zt + 2.0), 1.3, "bronze", 10))
    else:
        main.extend(K.pipe("inlet_feed", [(sp[0], sp[1], back[2] + 2.0), (sp[0] + 1.0, sp[1] + 8.0, back[2] + 2.0), (*K.polar(d2, r2 - 1.0, 232.0), back[2] + 2.0)], 1.3, "bronze"))
    if damaged:
        K.scorch(main, (0.0, 28.0), 5.0, lambda x, y: 20.5, 31, 4)
        K.wall_gash(main, K.polar(d2, r2 + 0.3, 275.0), 5.0, 6.0, 6.0, 17.0, 33, 0.9)
        K.wall_gash(main, K.polar(d1, r1 + 0.3, 300.0), 30.0, 5.0, 4.0, 11.0, 34, 0.9)
        K.scorch(base, (c[0] - 4.0, c[1] + 3.0), 4.0, 1.4, 35, 3)
    anchors = [("fx_flue_0", top1, 0.7), ("fx_flue_1", top2, 0.9)]
    if not P["inlet_2d"]:                                # docs/3d-models.md "Docking": 1.6 px inside the socket's mouth
        m3 = P["inlet_3d"]
        anchors.append(("fx_inlet_0", (m3[0] - 1.6 * math.sqrt(0.5), m3[1] + 1.6 * math.sqrt(0.5), m3[2]), 1.0))
    return dict(nodes=dict(base=base, main=main), anchors=anchors)


if __name__ == "__main__":
    K.run(KEY, build, spec=dict(flags=K.flags(0.0, shadow_azimuth=25.0)), P=P)

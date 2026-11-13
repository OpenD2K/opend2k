"""Atreides palace - the original design rebuilt with tools/models/atreides_kit.py.

    blender -b --factory-startup --python tools/models/atreides_palace.py -- [--no-bake] [--no-variants]
    D2K_BUILDING=ATREIDES_PALACE .venv/bin/python -I tools/models/atreides_check/pack.py

How the original was read (frame 3329 main, 3330 damaged, no base, no animation (no HAS_ANIMATION flag); build-up
56 kept):
  * a stepped palace turned about 47 degrees: a tall lower storey (26 px) with grey colonnaded walls and a cream
    cornice, round bastions with golden domes on its west and south corners; a lower east wing with a round end;
  * its roof a broad dark-brown terrace with a cream parapet, a row of golden lamps, a pale house stripe along
    the south-west edge, a white balcony below it;
  * an upper storey set back to the north-east along the whole length, on it a cream-rimmed raised gold roof with
    a house-colour chevron; behind, a rear block with a dark hipped roof (the palace's highest point);
  * long house-colour banners on the south-west wall, short ones on the south-east wall; sandstone rubble at the
    south and east feet.
Free choices: column count, lamps, the door.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_kit as K  # noqa: E402

KEY = "ATREIDES_PALACE"
P = dict(
    ang=-44.0, c=(-2.0, -17.0), hl=36.5, hw=21.5, h1=30.0,          # the lower storey (its long axis: west -> south corner)
    up=(-9.0, 12.5), up_hl=35.0, up_hw=14.0, h2=14.0,                # the upper storey, set back to the north-east (t, s from c)
    top=(4.0, -1.0), top_hl=15.0, top_hw=8.5, h3=6.0,              # the raised gold roof on it (t, s from the upper storey)
    rear=(-10.0, 8.0), rear_hl=17.0, rear_hw=9.0, rear_h=6.0, peak=16.0,   # the rear block with the dark hipped roof
    east=(28.0, 15.5), east_hl=11.5, east_hw=12.0, east_h=32.0,    # the lower east wing (t, s from c)
    bast=9.0, sun_el=55.0, sh_az=38.0,
)


# Night glow (docs/3d-models.md "Emission"; no new geometry: emissive twins of the materials already there): the
# terrace's golden lamps and the dome tips, lit windows = the dark bays between the columns (two of every three on
# the lower storeys, all on the upper one), the door.
GLOW = dict(gold=K.lit("orange_lit", "orange", (1.00, 0.72, 0.34)), win=K.lit("slab_lit", "slab_dk", (0.45, 0.30, 0.14), 0.11),
            door=K.lit("recess_lit", "recess", (0.90, 0.60, 0.28), 0.08))


def glow(main):
    m = {"door": GLOW["door"]}
    for i in range(7):
        m[f"gold{i}_b"] = GLOW["gold"]
    for tag in ("bast_w_tip", "bast_s_tip"):
        m[tag] = GLOW["gold"]
    for tag, n, every in (("low_sw", 14, False), ("low_se", 8, False), ("east_sw", 6, False), ("east_se", 6, False), ("up_sw", 12, True), ("up_se", 4, True)):
        for i in range(1, n):
            if every or i % 3:
                m[f"{tag}_bay{i}$"] = GLOW["win"]
    return K.relight(main, m)


def rot(c, t, s):
    a = math.radians(P["ang"])
    return (c[0] + math.cos(a) * t - math.sin(a) * s, c[1] + math.sin(a) * t + math.cos(a) * s)


def colonnade(main, tag, wc, wa, wl, depth, z0, z1, n):
    """Columns along a wall face: wc = centre of the face line, the wall runs along wa."""
    ca, sa = math.cos(math.radians(wa)), math.sin(math.radians(wa))
    for i in range(n):
        t = -wl + (i + 0.5) * 2 * wl / n
        p = (wc[0] + ca * t + sa * depth, wc[1] + sa * t - ca * depth)
        main.append(K.cyl(f"{tag}_col{i}", p, 0.9, z0, z1, "wall", 8))
        if i:                                              # the dark bay between two columns
            q = (wc[0] + ca * (t - wl / n) + sa * (depth - 0.75), wc[1] + sa * (t - wl / n) - ca * (depth - 0.75))
            main.append(K.obox(f"{tag}_bay{i}", q, wa, wl / n - 0.8, 0.25, z0 + 0.6, z1 - 0.6, "slab_dk"))
    main.append(K.obox(f"{tag}_plinth", (wc[0] + sa * depth, wc[1] - ca * depth), wa, wl, 1.3, 0.0, z0, "stone", 0.2))


def storey(main, tag, c0, hl, hw, z0, z1, roof="copper_dk", cols=(12, 6), wall="slab"):
    """A storey: grey walls with a colonnade on the two faces the camera sees, a cream cornice / parapet, a dark roof."""
    A = P["ang"]
    main.append(K.obox(f"{tag}", c0, A, hl, hw, z0, z1, wall, 0.4))
    main.append(K.obox(f"{tag}_cornice", c0, A, hl + 0.9, hw + 0.9, z1 - 1.8, z1 + 0.6, "roof", 0.3))
    main.append(K.obox(f"{tag}_roof", c0, A, hl - 1.0, hw - 1.0, z1 + 0.2, z1 + 1.0, roof, 0.2))
    colonnade(main, f"{tag}_sw", rot(c0, 0.0, -hw), A, hl - 1.5, 0.9, z0 + 1.6, z1 - 1.8, cols[0])
    colonnade(main, f"{tag}_se", rot(c0, hl, 0.0), A + 90.0, hw - 1.5, 0.9, z0 + 1.6, z1 - 1.8, cols[1])


def mottle(main, tag, c0, hl, hw, z, seed, n=14):
    """A roof's gold glints: small thin ragged plates scattered over the rectangle."""
    import random
    rnd = random.Random(seed)
    for i in range(n):
        q = rot(c0, rnd.uniform(-hl, hl), rnd.uniform(-hw, hw))
        glint = i % 3 == 0
        if not glint:               # (dark blotch plates baked near-black: the patina is left to the weathering)
            continue
        pts = K.star(q, rnd.uniform(0.7, 1.3) if glint else rnd.uniform(1.8, 3.6), seed * 31 + i, 9)
        import bmesh
        bm = bmesh.new()
        K.rings_solid(bm, [[(x, y, z - 0.2) for (x, y) in pts], [(x, y, z + (0.22 if glint else 0.12)) for (x, y) in pts]])
        main.append(K.new_obj(f"{tag}_m{i}", bm, "orange" if glint else ("rust" if i % 2 else "skirt")))


def build(damaged=False):
    main = []
    A, c, hl, hw, h1 = P["ang"], P["c"], P["hl"], P["hw"], P["h1"]
    # ---- the lower storey: its roof is the broad dark terrace
    main.append(K.obox("low_foot", c, A, hl + 1.2, hw + 1.2, 0.0, 2.0, "foot", 0.3))
    storey(main, "low", c, hl, hw, 0.0, h1, "floor", (14, 8))
    main.append(K.obox("terrace_in", rot(c, 0.0, -hw * 0.45), A, hl - 4.0, hw * 0.42, h1 + 0.8, h1 + 1.3, "copper_dk", 0.1))
    mottle(main, "terrace", rot(c, 0.0, -hw * 0.45), hl - 6.0, hw * 0.38, h1 + 1.3, 3, 22)
    main.append(K.house(K.obox("stripe", rot(c, -2.0, -hw + 2.6), A, hl - 9.0, 1.3, h1 + 0.9, h1 + 1.6, "HOUSE", 0.1), 0.8))
    for i in range(7):                                   # golden lamps in a row on the terrace
        p = rot(c, -hl + 9.0 + i * 7.0, -hw + 7.5)
        main.extend(K.lamp(f"gold{i}", (p[0], p[1], h1 + 1.6), 1.0, "orange"))
    # the white balcony and the door on the south-west wall, long house banners
    main.append(K.obox("balcony", rot(c, -10.0, -hw - 2.4), A, 6.5, 2.4, h1 - 12.0, h1 - 7.0, "white", 0.4))
    for i, t in enumerate((-20.0, -1.0, 6.0)):
        main.append(K.house(K.obox(f"banner{i}", rot(c, t, -hw - 1.9), A, 1.6, 0.4, 3.0, h1 - 4.0, "HOUSE"), 0.5))
    for i, s_ in enumerate((-12.0, -4.0, 4.0)):
        main.append(K.house(K.obox(f"banner_se{i}", rot(c, hl + 1.9, s_), A + 90.0, 1.3, 0.4, h1 - 10.0, h1 - 4.0, "HOUSE"), 0.4))
    main.append(K.obox("door", rot(c, 14.0, -hw - 1.0), A, 3.4, 0.9, 0.0, 10.0, "recess"))
    # bastions on the west and south corners (golden domes on them)
    b = P["bast"]
    for name, p in (("w", rot(c, -hl + 2.5, -hw + 2.5)), ("s", rot(c, hl - 1.0, -hw - 1.5))):
        main.append(K.cyl(f"bast_{name}", p, b, 0.0, h1 - 1.0, "slab", 24))
        main.append(K.cyl(f"bast_{name}_cornice", p, b + 0.9, h1 - 2.6, h1 - 0.4, "roof", 24))
        for k in range(10):
            main.append(K.cyl(f"bast_{name}_col{k}", K.polar(p, b + 0.5, 170.0 + 20.0 * k), 0.8, 2.0, h1 - 2.6, "white", 6))
        main.append(K.dome(f"bast_{name}_dome", p, b - 1.6, h1 - 0.8, 4.6, "copper", 20))
        main.append(K.cyl(f"bast_{name}_tip", p, 1.4, h1 + 3.0, h1 + 4.6, "orange", 10))
        main.append(K.house(K.arc(f"bast_{name}_band", p, b + 0.1, b + 0.7, 200.0, 340.0, h1 - 8.0, h1 - 5.6, "HOUSE", 10), 0.5))
    # ---- the lower east wing with its round end
    ec = rot(c, *P["east"])
    el, ew, eh = P["east_hl"], P["east_hw"], P["east_h"]
    storey(main, "east", ec, el, ew, 0.0, eh, "rust", (6, 6))
    ep = rot(ec, el - 2.0, -ew + 3.0)
    main.append(K.cyl("bast_e", ep, b, 0.0, eh - 1.0, "slab", 24))
    main.append(K.cyl("bast_e_cornice", ep, b + 0.9, eh - 2.6, eh - 0.4, "roof", 24))
    main.append(K.cyl("bast_e_roof", ep, b - 1.4, eh - 0.8, eh + 0.2, "rust", 20))
    for k in range(10):
        main.append(K.cyl(f"bast_e_col{k}", K.polar(ep, b + 0.5, 170.0 + 20.0 * k), 0.8, 2.0, eh - 2.6, "white", 6))
    main.append(K.house(K.arc("bast_e_band", ep, b + 0.1, b + 0.7, 200.0, 340.0, eh - 8.0, eh - 5.6, "HOUSE", 10), 0.5))
    # ---- the upper storey, the raised gold roof with the house chevron
    uc = rot(c, *P["up"])
    ul, uw, h2 = P["up_hl"], P["up_hw"], h1 + P["h2"]
    storey(main, "up", uc, ul, uw, h1, h2, "skirt", (12, 4), "slab")
    mottle(main, "uproof", uc, ul - 4.0, uw - 3.0, h2 + 1.0, 5, 16)
    mottle(main, "eastroof", ec, el - 3.0, ew - 3.0, eh + 1.0, 7, 10)
    tc = rot(uc, *P["top"])
    tl, tw, h3 = P["top_hl"], P["top_hw"], h2 + P["h3"]
    main.append(K.obox("top", tc, A, tl, tw, h2, h3, "roof", 0.5, 0.94))
    main.append(K.obox("top_in", tc, A, tl - 2.0, tw - 2.0, h3 - 1.4, h3 + 0.3, "copper", 0.2))
    main.append(K.obox("top_core", rot(tc, 2.0, 0.0), A, tl * 0.45, tw * 0.5, h3, h3 + 2.6, "copper", 0.3, 0.6))
    for i, s_ in enumerate((-1, 1)):
        p0, p1 = rot(tc, tl * 0.5, s_ * tw * 0.55), rot(tc, tl * 0.82, 0.0)
        main.append(K.house(K.beam(f"chevron{i}", (p0[0], p0[1], h3 + 0.6), (p1[0], p1[1], h3 + 0.6), 2.0, 1.0, "HOUSE"), 0.5))
    main.append(K.obox("chevron_block", rot(tc, tl * 0.55, 0.0), A, 3.4, tw * 0.5, h3 + 0.2, h3 + 1.6, "slab", 0.2))
    # ---- the rear block with the dark hipped roof
    rc = rot(uc, *P["rear"])
    rl, rw = P["rear_hl"], P["rear_hw"]
    z0 = h2 + P["rear_h"]
    main.append(K.obox("rear", rc, A, rl, rw, 0.0, z0, "stone_dk", 0.4))
    main.append(K.obox("rear_cornice", rc, A, rl + 0.8, rw + 0.8, z0 - 1.4, z0 + 0.5, "roof2", 0.3))
    main.append(K.obox("rear_roof", rc, A, rl + 0.3, rw + 0.3, z0 + 0.3, z0 + P["peak"], "rust", 0.2, 0.08))
    for i, (st, ss) in enumerate(((-1, -1), (1, -1), (1, 1), (-1, 1))):
        p0 = rot(rc, st * (rl + 0.3), ss * (rw + 0.3))
        p1 = rot(rc, st * rl * 0.08, ss * rw * 0.08)
        main.append(K.beam(f"rear_hip{i}", (p0[0], p0[1], z0 + 0.5), (p1[0], p1[1], z0 + P["peak"]), 1.0, 0.8, "copper"))
    main.append(K.house(K.obox("rear_panel", rot(rc, -rl - 0.3, -2.0), A + 90.0, 3.0, 0.5, h1 + 2.0, z0 - 3.0, "HOUSE"), 0.5))
    wp = rot(c, -hl + 6.0, hw - 2.0)
    main.append(K.tube("mast", (wp[0], wp[1], h1), (wp[0], wp[1], h1 + 16.0), 0.6, "copper_dk", 6))
    main.extend(K.rocks("rock_s", (-15.0, -49.0), 10.0, 3, 9, 0.9))
    main.extend(K.rocks("rock_e", (39.0, -50.0), 9.0, 5, 8, 0.9))
    main.extend(K.rocks("rock_w", (-30.0, -27.0), 6.5, 7, 6))
    if damaged:
        K.hole(main, [], rot(c, 8.0, -hw + 8.0), 6.0, h1 + 1.2, 3, depth=3.0, panel=("copper", "skirt"), rib="copper_dk")
        K.hole(main, [], rot(uc, -6.0, -2.0), 5.0, h2 + 1.2, 4, depth=3.0, panel=("copper", "roof2"), rib="copper_dk")
        K.wall_gash(main, rot(c, -4.0, -hw - 1.6), A, 8.0, 5.0, 16.0, 5, 1.2)
        K.wall_gash(main, rot(ec, el + 1.6, 2.0), A + 90.0, 6.0, 4.0, 13.0, 6, 1.2)
        K.scorch(main, rot(c, 20.0, -6.0), 6.0, h1 + 1.4, 7, 6)
    glow(main)
    dp = rot(c, 14.0, -hw - 1.7)                         # a lamp over the door (3D: a warm pool on the forecourt)
    return dict(nodes=dict(main=main), anchors=[("fx_light_0", (dp[0], dp[1], 10.6), 1.0)])


if __name__ == "__main__":
    K.run(KEY, build, spec=lambda: dict(flags=dict(shadow_azimuth=P["sh_az"], sun_elevation=P["sun_el"], fill=(0.3, 90.0, 35.0),
                                                   sky_strength=0.1, sun_strength=5.0)), P=P)

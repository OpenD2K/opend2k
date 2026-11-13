"""Sardaukar - the light infantry reference (infantry_v1.py, infantry_derive.py) in full dark armour: everything but
the rifle, the boots and the gloves is house colour; a closed helmet with a green visor; no pack.

    blender -b --factory-startup --python tools/models/infantry_sardaukar.py -- --out artpacks/models-infantry-sardaukar
    render_pack.py --models artpacks/models-infantry-sardaukar --out artpacks/rendered-infantry-sardaukar --types SARDAUKAR <RENDER_FLAGS>

How the original was read (DATA.R16 frames 1022-1293, 236 of 272 present; measurements only, infantry_check with
D2K_INF_UNIT=SARDAUKAR):
  * A broad figure (85 opaque px standing against 74), 78 % house colour in the DARK shades of the ramp (mean
    (29, 56, 88) as Atreides against the light infantry's (58, 86, 120)), 15 % black: the rifle (as the light
    infantry's, held the same way, with its navy fore-end), boots and gloves. No skin shows; the only other colour
    is a bright green visor of 2 px on the helmet's front.
  * Rows as the reference: hip fire with the flash in rows 7 / 9 / 11 and 17 / 19 / 21.
  * Ours: the armour parts thickened (limbs x 1.22, torso x 1.1 against the fitted proportions), the figure enlarged
    14 %, the uniform's tone curve darkened.
Poses: infantry_sardaukar_poses.json (started from the light infantry's and refitted).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import infantry_derive as DV  # noqa: E402
from infantry_derive import K, L, Bx, V  # noqa: E402

DARK = 0.5        # the armour's shade against the reference's uniform (the house ramp's dark half)


def parts(P):
    P = DV.drop(P, mats=("pack", "pack_dark", "roll", "pouch"))
    out = []
    for p in P:
        fx = p.get("fx")
        b, m = p["bone"], p["mat"]
        if not fx and b == "head":
            continue                                       # a closed helmet instead
        if not fx and m == "skin":
            p = dict(p, mat="glove")                       # black gloves
        elif not fx and m == "belt":
            if b == "chest":
                continue                                   # no harness, no bedroll straps
            p = dict(p, mat="glove")
        elif not fx and b == "rifle":
            p = V.stretch([p], 1.3, 1.0, 1.25)[0]          # a heavier rifle: 2 px thick in the original
        elif not fx and m in ("HOUSE", "HOUSE_plate"):
            p = dict(p, shade=p.get("shade", V.HOUSE_SHADE[m]) * DARK)
        out.append(p)
    P = out
    h = V.D["head"]
    P += V.stretch([
        L("head", "HOUSE_plate", "z", [(0.05, 0, 0.1, 1.2, 1.3), (0.6, 0, 0.15, 1.62, 1.75), (1.4, 0, 0.1, 1.78, 1.9), (2.1, 0, 0.0, 1.7, 1.8),
                                       (2.6, 0, -0.05, 1.3, 1.4), (2.9, 0, -0.05, 0.75, 0.8), (3.0, 0, -0.05, 0.2, 0.22)], segs=18, shade=DARK * 1.05),
        L("head", "HOUSE", "z", [(-0.4, 0, 0.1, 0.95, 1.0), (0.2, 0, 0.1, 1.3, 1.4)], shade=DARK * 0.6),                 # neck guard
        L("head", "HOUSE", "y", [(-1.6, 2.95, 0, 0.3, 0.3, 3), (1.2, 2.95, 0, 0.3, 0.3, 3)], segs=8, shade=DARK * 0.8),  # crest
        Bx("head", "HOUSE", (0.0, 1.55, 0.75), (1.5, 0.9, 0.9), n=3, shade=DARK * 0.55),                                 # breathing mask
        Bx("head", "visor", (0.0, 1.8, 1.5), (2.1, 0.45, 0.5), n=3)], h, h, h)
    return P


DV.setup(
    "infantry_sardaukar", "SARDAUKAR", "SARDAUKAR", parts=parts,
    classes=dict(glove=1, visor=4),
    col=dict(glove=(0.028, 0.028, 0.03), visor=(0.08, 1.0, 0.12)),
    spec=dict(glove=(0.0, 0.5, "rubber"), visor=(0.0, 0.3, "dark")),
    film=dict(glove=(0.06, 0.06, 0.06)),
    glow=dict(visor=1.2),
    extra=dict(PLAIN=("skin", "eye", "visor"), GROW=1.14,
               # the reference's light and tone curve with the uniform's output shades pulled down to the ramp's dark half
               RENDER_FLAGS=V.RENDER_FLAGS.split("--house-curve")[0] +
               "--house-curve 0:0.07,0.22:0.10,0.42:0.14,0.52:0.20,0.62:0.28,0.76:0.37,0.9:0.46,1:0.58"),
)
DV.finish(__name__)

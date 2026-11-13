"""The barracks' doorway (Atreides / Harkonnen / Ordos barracks): an open, lit door where the game makes infantry
appear, so that they visibly come out of the building in Graphics: 3D and in the 2D frames.

The game's numbers (Templates.bin; World::findUnitExitBuilding production.cpp, World::addUnit spawn.cpp): the three
barracks occupy 2 x 3 cells, the top two rows solid, the front row walkable. A trained soldier appears on exit
point 1 = cell (1, 1) on the first free infantry spot counted down from 4 - spot 4 = (7, 14) px in the cell - and
walks to exit point 2 = cell (1, 2), keeping his spot: straight south. In px from the footprint's centre (x east,
y north) that is from SPAWN = (7, 2) to EXIT = (7, -30). (If the first soldier still stands in the cell, the next
one appears on spot 3 = (7, -10): the same line.) That line is 9 px WEST of the cell's centre - it runs through the
gate the original artists drew.

  * Graphics: 3D shows the model moved onto its cells (docs/3d-models.md "Placement"; the fit's move south `fit_s`:
    0 for the Atreides barracks, 6.8 / 7.5 px for the Harkonnen / Ordos ones, whose 2D picture reaches north of
    the cells): the soldier appears at model (7, 2 + fit_s). The door plane is put a few px south of that, the hall
    behind it is hollow, so the soldier stands inside under the roof and steps out through the door.
  * 2D: the barracks art has no base frame and the sorted display list draws the building's frame over a unit
    standing in its solid rows, so the soldier is hidden until he leaves the picture's bottom edge - he appears at
    the foot of the paved path, in front of the lit doorway.

Size: the game's soldiers are large against its buildings (a 16 px sprite beside a 64 px barracks; Graphics: 3D
stands the figure upright, about 22 px tall). The Atreides and Harkonnen doorways are therefore tall gate towers
(opening 20 px high: lower, and the soldier's head would stick out through the roof in 3D); the Ordos one is a
low door under the serpent's head, whose body hides the soldier's upper half while he is inside.

door() builds, from the kit's primitives: two jambs, a lintel in the house's trim, a roof slab (or a dome: `cap`),
the dark back wall and floor of a hall `depth` deep, a pale paved path from the threshold out to `path_to` with
kerbs (0.25 px thin: infantry stand on the ground, not on decks) and optional low flank walls, two lamp beads on
the jambs, and - in the "glow" node, kept out of the atlas bake like the heavy factories' bay
(tools/models/heavy_factory_kit.py) - a warm ceiling strip and a lamp band on the back wall (materials GLOW_bay /
GLOW_window: real glTF emission). Anchor fx_light_N on the ceiling strip.
"""
import heavy_factory_kit as HF

SPAWN, EXIT = (7.0, 2.0), (7.0, -30.0)               # where a soldier appears / walks to (px from the cells' centre)


def install(K, path=(0.800, 0.780, 0.720)):
    """Registers the path's material and keeps the glow node out of the atlas bake."""
    HF.install(K, apron=path)


def door(K, name, x, yd, path_to, width=10.0, depth=6.0, height=22.0, wall="stone", trim="orange", dark="stone_dk",
         jamb=1.6, anchor="fx_light_0", damaged=False, cap=None, flank=0.0, flank_to=None, bands=True):
    """-> (main parts, glow parts, anchors). x = the door's centre line, yd = the door plane (the hall runs north of
    it), path_to = the south end of the paved path."""
    hw = width / 2
    main, glow = [], []
    yb = yd + depth                                       # the back wall's face
    # the hall: floor, back wall, side walls (their inner faces dark), roof
    main.append(K.box(f"{name}_floor", x - hw, x + hw, yd, yb, 0.0, 0.25, dark))
    main.append(K.box(f"{name}_back", x - hw - jamb, x + hw + jamb, yb, yb + 1.2, 0.0, height + 1.0, dark))
    for s in (-1, 1):
        x0, x1 = sorted((x + s * hw, x + s * (hw + jamb)))
        main.append(K.box(f"{name}_jamb{s}", x0, x1, yd, yb + 1.2, 0.0, height + 1.4, wall, 0.15))
        main.append(K.box(f"{name}_side{s}", x + s * hw - 0.06, x + s * hw + 0.06, yd + 0.3, yb, 0.25, height, dark))
        main.append(K.box(f"{name}_foot{s}", x0 - 0.3, x1 + 0.3, yd - 0.5, yd + 1.4, 0.0, 1.5, dark, 0.1))
    main.append(K.box(f"{name}_roof", x - hw - jamb - 0.3, x + hw + jamb + 0.3, yd + 0.6, yb + 1.5, height + 1.0, height + 2.2, wall, 0.25))
    if cap:                                               # a dome on the roof: (material, height)
        main.append(K.dome(f"{name}_cap", (x, (yd + yb) / 2 + 0.9), min(hw + jamb, (depth + 1.2) / 2) + 0.2, height + 2.0, cap[1], cap[0], 24))
    main.append(K.box(f"{name}_ceiling", x - hw, x + hw, yd + 0.3, yb, height, height + 0.3, dark))
    main.append(K.box(f"{name}_lintel", x - hw - jamb - 0.5, x + hw + jamb + 0.5, yd - 0.5, yd + 1.3, height, height + 2.4, trim, 0.2))
    for i, z in enumerate((height * 0.36, height * 0.68) if bands else ()):     # trim bands round the jambs
        for s in (-1, 1):
            x0, x1 = sorted((x + s * (hw - 0.1), x + s * (hw + jamb + 0.25)))
            main.append(K.box(f"{name}_band{i}{s}", x0, x1, yd - 0.25, yd + 2.0, z, z + 0.9, trim, 0.1))
    # the threshold and the paved path out of the building
    main.append(K.box(f"{name}_sill", x - hw, x + hw, yd - 0.6, yd + 0.4, 0.0, 0.4, dark))
    main.append(K.box(f"{name}_path", x - hw - 0.4, x + hw + 0.4, path_to, yd - 0.5, 0.0, 0.25, "hf_apron"))
    for s in (-1, 1):
        main.append(K.box(f"{name}_kerb{s}", x + s * (hw + 0.4) - 0.35, x + s * (hw + 0.4) + 0.35, path_to, yd - 0.6, 0.0, 0.8, dark, 0.1))
    if flank > 0.0:                                       # low walls along the path with a trim coping
        fy = path_to if flank_to is None else flank_to
        for s in (-1, 1):
            x0, x1 = sorted((x + s * (hw + 0.75), x + s * (hw + jamb + 0.4)))
            main.append(K.box(f"{name}_flank{s}", x0, x1, fy, yd - 0.4, 0.0, flank, wall, 0.15))
            main.append(K.box(f"{name}_coping{s}", x0 - 0.2, x1 + 0.2, fy - 0.2, yd - 0.4, flank, flank + 0.7, trim, 0.1))
    n = max(1, int((yd - path_to) / 4.0))
    for i in range(1, n):                                 # joints of the paving
        y = yd - (yd - path_to) * i / n
        main.append(K.box(f"{name}_joint{i}", x - hw - 0.3, x + hw + 0.3, y - 0.08, y + 0.08, 0.2, 0.3, dark))
    for s in (-1, 1):
        main.extend(K.lamp(f"{name}_lamp{s}", (x + s * (hw + jamb / 2), yd - 0.55, height + 0.4), 0.6))
    # light: a warm strip under the ceiling, a lit panel on the back wall (dimmer when damaged: half the strip)
    strip = K.box(f"glow_{name}_strip", x - (hw - 1.0) * (0.5 if damaged else 1.0), x + hw - 1.0, yb - 2.4, yb - 1.0, height - 0.45, height - 0.02, dark)
    panel = K.box(f"glow_{name}_panel", x - hw + 2.2, x + hw - 2.2, yb - 0.22, yb + 0.05, height * 0.5, height * 0.62, dark)
    for o, (mat, rgb, k) in ((strip, ("GLOW_bay", HF.LAMP, 2.5)), (panel, ("GLOW_window", HF.WARM, 1.2))):
        o.data.materials.clear()
        o.data.materials.append(HF.glow_material(mat, rgb, k))
        glow.append(o)
    return main, glow, [(anchor, (x, yb - 1.7, height - 0.5), 1.0)]

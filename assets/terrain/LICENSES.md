# Designed 3D terrain assets: sources and licences

Used by the designed terrains - Graphics: 3D "3D terrain: Designed / Designed (faithful)" and Graphics: Enhanced
"Terrain: Designed / Designed (faithful)" (`src/render3d/terrain_v2.*`, docs/3d-mode.md "Terrain v2" / "Terrain v3").
Every asset here is **CC0 1.0 Universal (public domain dedication)** - no attribution required; listed anyway.

- ambientCG: "All assets are provided under the Creative Commons CC0 1.0 Universal License"
  (https://docs.ambientcg.com/license/).
- Poly Haven: "All assets on Poly Haven are CC0" (https://polyhaven.com/license).

## Checked in (small)

`rocks/<name>.glb` - Poly Haven rock scans, decimated to ~1400 triangles, normalised (1 unit wide, base at
y = 0), geometry + UVs only. Made by `tools/fetch_terrain_assets.py --rocks` (Blender,
`tools/terrain_rocks_blender.py`).

| File | Poly Haven asset | URL |
|---|---|---|
| rocks/boulder_a.glb | Namaqualand Boulder 02 | https://polyhaven.com/a/namaqualand_boulder_02 |
| rocks/boulder_b.glb | Namaqualand Boulder 03 | https://polyhaven.com/a/namaqualand_boulder_03 |
| rocks/boulder_c.glb | Namaqualand Boulder 04 | https://polyhaven.com/a/namaqualand_boulder_04 |
| rocks/boulder_d.glb | Namaqualand Boulder 05 | https://polyhaven.com/a/namaqualand_boulder_05 |
| rocks/boulder_e.glb | Namaqualand Boulder 06 | https://polyhaven.com/a/namaqualand_boulder_06 |
| rocks/cliff_a.glb | Namaqualand Cliff 01 | https://polyhaven.com/a/namaqualand_cliff_01 |

## Downloaded by `tools/fetch_terrain_assets.py` (not in git, ~30 MB)

`rocks/<name>.jpg` - the diffuse (1k) of the rock scans above (same assets, same licence).

`materials/<role>/{albedo,normal,height,ao}.jpg` - ambientCG 1K-JPG sets (`_Color`, `_NormalGL`,
`_Displacement`, `_AmbientOcclusion`):

| Role | ambientCG asset | URL |
|---|---|---|
| sand | Ground 080 | https://ambientcg.com/a/Ground080 |
| dunes | Ground 097 | https://ambientcg.com/a/Ground097 |
| rock (buildable plateau) | Rock 029 | https://ambientcg.com/a/Rock029 |
| cliff (impassable rock) | Rock 061 | https://ambientcg.com/a/Rock061 |
| rough (rough / infantry-only rock) | Rock 049 | https://ambientcg.com/a/Rock049 |
| spice | Gravel 025 | https://ambientcg.com/a/Gravel025 |
| concrete | Concrete 040 | https://ambientcg.com/a/Concrete040 |
| crag (crags of "Designed (faithful)": cliffs, broken rock; optional) | Rock 026 | https://ambientcg.com/a/Rock026 |

At run time every material (and every rock) is tinted so its mean colour matches the mean colour of the
map's own tiles of that class ("colour lock"): nothing derived from the original game data is stored.
"Designed (faithful)" also reads the map's own tile pixels at run time for its layout (rock outlines, cliffs, tones:
`src/render3d/terrain_guide.*`), in memory only - again nothing derived from the game data is stored or shipped.

## Ground clutter pack (Reimagined; not in git)

`artpacks/ground-clutter/clutter/` (docs/3d-mode.md "Ground clutter") is built by `tools/models/clutter_kit.py` from
these Poly Haven scans (CC0 1.0, fetched by `tools/models/clutter_fetch.py`) plus procedural pieces / textures; the
pack carries the same list in its own LICENSES.md.

| Poly Haven asset | URL | Pieces |
|---|---|---|
| Namaqualand Stones 01 | https://polyhaven.com/a/namaqualand_stones_01 | pebble_a-e, gravel_a/b |
| Namaqualand Rocks 01 | https://polyhaven.com/a/namaqualand_rocks_01 | boulder_a-d, slab_a/b, rubble_a/b |
| Namaqualand Boulders 01 | https://polyhaven.com/a/namaqualand_boulders_01 | boulder_e/f |
| Rock 07 | https://polyhaven.com/a/rock_07 | boulder_g |
| Rock 09 | https://polyhaven.com/a/rock_09 | slab_c |
| Stone 01 | https://polyhaven.com/a/stone_01 | boulder_h |
| Dead Quiver Branch 01 | https://polyhaven.com/a/dead_quiver_branch_01 | deadwood_a |
| Dead Quiver Branch 02 | https://polyhaven.com/a/dead_quiver_branch_02 | deadwood_b |
| Dry Branches Medium 01 | https://polyhaven.com/a/dry_branches_medium_01 | deadwood_c |

## Photo terrain pack (Reimagined; not in git)

`artpacks/photo-terrain/` (docs/3d-mode.md "Photo terrain", docs/art-pipeline.md "Photo terrain pack") is built by
`tools/fetch_photo_terrain.py` from these Poly Haven texture scans (CC0 1.0; the list with the maps used is
`tools/photo_terrain_manifest.json`, and the pack carries its own LICENSES.md).

| Poly Haven asset | URL | Layer |
|---|---|---|
| Sandy Gravel 02 | https://polyhaven.com/a/sandy_gravel_02 | sand |
| Aerial Beach 01 | https://polyhaven.com/a/aerial_beach_01 | ripples |
| Coast Sand 01 | https://polyhaven.com/a/coast_sand_01 | gravel |
| Sandstone Cracks | https://polyhaven.com/a/sandstone_cracks | rock |
| Rock Boulder Cracked | https://polyhaven.com/a/rock_boulder_cracked | rough |
| Cliff Side | https://polyhaven.com/a/cliff_side | cliff |
| Dirty Concrete | https://polyhaven.com/a/dirty_concrete | concrete |

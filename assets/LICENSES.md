# Assets shipped with OpenD2K

Nothing in `assets/` comes from the original *Dune 2000*: no sprite, texture, model, sound or text of the
original game is stored here or derived from one - with one owner-approved exception, the comparison pictures in
`previews/` ("Preview images" below).

| Folder | What | Made by | Licence |
|---|---|---|---|
| `models/` | the built-in 3D model pack `new` (combat tanks, trike, harvester, wind traps, refineries; glTF `.glb`) | OpenD2K: procedural Blender geometry from `tools/models/build_models.py` (`combat_tank.py`, `trike.py`, `harvester.py`, `wind_trap.py`, `refinery.py`) | GPL-3.0-or-later, like the code (`LICENSE`) |
| `icon/` | the app icon (SVG sources + PNGs; also `cmake/windows/opend2k.ico`, the Android launcher mipmaps, `src/platform/app_icon_png.inc`) | OpenD2K: drawn as SVG by `tools/make_icons.py`; the letters are outlines of the Play Bold font (`third_party/fonts/play`, SIL OFL 1.1) | GPL-3.0-or-later, like the code (`LICENSE`) |
| `terrain/rocks/*.glb` | rock meshes for the designed 3D terrain | Poly Haven scans, decimated by `tools/fetch_terrain_assets.py` | CC0 1.0 (`terrain/LICENSES.md`) |
| `previews/` | the pre-rendered Graphics / welcome page previews (PNGs + `index.json`; `d2k --render-previews`, `tools/release/render_previews.sh`) | OpenD2K: screenshots of the game's preview scene | see "Preview images" below |
| `terrain/materials/`, `terrain/rocks/*.jpg` | ground textures (not in git; fetched by `tools/fetch_terrain_assets.py`, included in the release packages) | ambientCG, Poly Haven | CC0 1.0 (`terrain/LICENSES.md`) |

## Preview images

`previews/` holds the pictures the Options → Graphics page and the welcome page show for each graphics setup, so
every player sees them at once (owner decision 2026-11-13; an explicit exception to "no original game material in
the release"). Pictures drawn with OpenD2K's own art (the redesign art pack, the designed terrain) are covered by the
licence of that art (the art pack: CC BY-SA 4.0; the engine's drawing: GPL-3.0-or-later). **Preview images that
show the original 1998 art** (Graphics: Original, the original / upscaled sprites, the original tiles terrain, the
PS1 models) carry a small watermark and are listed with `"watermark": true` in `previews/index.json`: they are
screenshots of *Dune 2000* © Westwood Studios / Electronic Arts, shown for comparison; they are **not** covered by
OpenD2K's licences.

The larger redesigned art pack (`OpenD2K-redesign-pack`, rendered sprites + models) is a separate download;
licensed under CC BY-SA 4.0 (stated in the pack's own README / LICENSE.md).

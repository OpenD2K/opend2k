# Enhanced graphics: art packs and modern effects

Everything here applies to the **Enhanced** graphics mode only. The **Original** mode stays
pixel-exact. With Original selected, the art pack and effects settings are ignored: a screenshot
with `--graphics original --effects modern --art-pack <pack>` is byte-identical to one with
`--effects classic --art-pack original`.

None of it touches the simulation. Effects only read `World` state and keep their own state and
their own RNG (xorshift), so the game RNG, the saves and `--simtest` do not change.

## Settings

| setting (settings.ini) | values | default | where |
|---|---|---|---|
| `graphicsPreset` | `original`, `enhanced`, `3d`, `custom` | new players: `enhanced` (no key: the preset the values match, else `custom`) | Options → Graphics: the tabs; original SETTINGS page: "Graphics:" button. See "Presets" below |
| `artPack` | `original`, `upscaled`, or an art-pack folder name | `upscaled` (built-in default; the Enhanced / 3D presets set `original` unless a redesign pack is chosen) | Graphics page → Custom: "Art"; in-game options |
| `effects` | `classic`, `modern` | `modern` | Graphics page → Custom: "Effects"; in-game options; Original UIL game options: "Modern effects" |
| `shroud` | `classic`, `soft` | `soft` (absent key: `soft` with Modern effects, `classic` with Classic) | Graphics page → Custom: "Shroud"; in-game options (row shared with "Health") |
| `healthBars` | `selected`, `damaged`, `always` | `selected` | Graphics page → Custom → Interface: "Health bars"; in-game options |
| `panelTransparency` | `off`, `light`, `medium` | `off` | Graphics page → Custom → Interface; in-game options |
| `classicView` | `off`, `hold`, `toggle` | `hold` | Graphics page → Custom and in-game options: "Classic view key (Tab)" (touch: "Classic view (1998 button)"); see "Classic view" below |
| `terrain2d` | `original`, `upscaled`, `designed`, `faithful` | new players / the Enhanced preset: `faithful`; struct default (one-frame runs, a file without the key): `designed` (both silently `upscaled` without assets/terrain or the GPU renderer; Lite profile: `upscaled`) | Graphics page → Custom: "Terrain" (Original tiles / Upscaled / Designed / Faithful); in-game options (row shared with "Panel transparency"; Graphics: 3D shows its own "3D terrain" there) |
| `timeOfDay` | `day`, `dawn`, `dusk`, `night`, `cycle` | `day` (Enhanced / 3D presets: `cycle`) | Graphics page → Custom → Lighting: "Time of day"; in-game options (row shared with "Lighting"); see "Lighting" below |
| `lighting` | `classic`, `modern` | struct default (one-frame runs): `classic`; the Enhanced / 3D presets (and so new players): `modern`; a file without the key that names one of those presets: `modern` | Graphics page → Custom → Lighting: "Lighting"; in-game options |

### Presets
Options → Graphics (`Menu::pageGraphics`, `Settings::applyPreset` / `matchesPreset` / `activePreset`):

| preset | sets | compared |
|---|---|---|
| Original | `graphics=original` | `graphics` only (the Enhanced-only values are inert and kept) |
| Enhanced | `graphics=enhanced`, `terrain2d=faithful`, `effects=modern`, `timeOfDay=day`, `lighting=modern`, `shroud=soft`, `healthBars=selected`, `panelTransparency=off`, `classicView=hold`, `artPack` = the chosen redesign pack (`ArtPack::isRedesign`: pack.ini `kind=rendered`/`redesign`, or written by `tools/render_sprites/render_pack.py`) else `original` | all of these |
| 3D | the Enhanced values with `graphics=3d`, plus `terrain3d=faithful`, `shadows3d=on` (`models3d` left alone) | all but `terrain2d` |
| Custom | — | anything else |

A tap on a preset tab applies it. Any change to one of these values on the Custom tab or in the in-game options
marks the setup Custom (`Settings::noteGraphicsEdit`). Display values (resolution, UI scale, fullscreen, render
resolution) are not part of a preset. A new player (no settings.ini) gets the Enhanced preset in `Settings::load` (and, once the game data is there, the first-run welcome page: the three
looks with their previews and the art pack offer - `Menu::pageWelcome`, settings.ini `firstRunArtPackAsked`; without the
redesign pack the Enhanced tab reads "Art: Original sprites (no art pack)" with a "Get the art pack" button);
the struct defaults are unchanged, so one-frame / screenshot / `--simtest` runs look as before (their terrain is
`designed` unless `--terrain2d` / `--terrain3d` says otherwise). The presets' terrain became `faithful` on 2026-11-11
(owner decision; before: `designed`): a settings.ini that names the Enhanced or 3D preset and holds exactly its old
values follows the preset to the new terrain when it is loaded; a file on Custom keeps `designed`. Each preset tab shows
a preview (`platform/preset_previews.*`). **Shipped set** (owner decision 2026-11-13): `d2k --render-previews DIR`
(`tools/release/render_previews.sh`, before each release) renders every picture the menus show for the default
setups - the three presets (= the welcome cards), and for the default Original / Enhanced / 3D setup and Enhanced
without the art pack every value of every picture row of the Custom tab in that row's framing (74 pictures,
~10 MB as 256-colour PNGs) - into `assets/previews` (+ `index.json`: portable key → file, watermark flag, engine
version, pack fingerprint / content hash); the packages ship it. Pictures that show original art (Graphics:
Original, original / upscaled sprites, original tiles terrain, PS1 models) get a small "Dune 2000 © Westwood Studios
/ EA" watermark. The game shows a shipped picture when its key (`GraphicsPreviews::shippedKeyOf`: scene version,
A1V1 map + mission hash, text language, framing, settings, and for pack-drawn pictures the pack's fingerprint =
file names + sizes + pack.ini / models.ini text) matches - no rendering, no stall; a changed pack only drops its own
pictures. Anything else is rendered from the player's own data as below: a curated scene on A1V1 (an Atreides base on the plateau, a harvester on the spice,
a Harkonnen raid fought by tanks, infantry and two turrets, the barracks burning; map objects and events stripped,
`presetPreviewScene`, `SessionConfig::presetPreview`) drawn by a second, offscreen `Session` with its own Settings into a
640x400 target, the same framing for every preset (3D: tilt 50, yaw 14). The 3D tab needs a model pack (else "Needs
the 3D art pack"). The Custom tab gets a preview column: the row under the mouse (else the focused one) compared -
the current setup and the value a click would choose (`stepPictureOption`; the left arrow = the previous value), each
option with a framing that shows it (`previewShotFor`: effects / health bars / MSAA the fight close up, time of day /
lighting / HDR the base, terrain / shadows / filtering the ridge, the rest the whole frame); rows that change nothing in
the current mode say so, rows that never change the picture (display, classic view) show the current setup. Work is
spread over the menu frames (`GraphicsPreviews::work`: the map data loads on a worker thread, then a few warm-up frames
per menu frame; a placeholder meanwhile). Cost: creating the preview game is one stall (Debug, Enhanced: ~3.5 s, mostly
the designed-terrain textures; Original ~50 ms); then ~0.1-0.2 s per picture. Pictures are cached as
`<pref path>/previews/preview-<hash>.png` (hash of the scene version `kSceneVersion`, data folder, language, art /
model pack stamps, the picture's settings and framing; at most 200 files, 40 textures in memory) - later visits load
them in milliseconds. After the three presets, the common Custom comparisons are rendered in the background while the
page is open; the preview game is dropped 8 s after the last request and when a game starts. One-frame runs render
when asked (blocking); `D2K_PREVIEWS_DIR` sets the cache folder (also for one-frame runs), `D2K_PREVIEWS_ASYNC=1` keeps
the spread-over-frames path in one-frame runs, `--menu-page graphics-custom-N` shows the comparison of row N
(`PictureOption` number, e.g. 4 Effects, 5 Time of day, 2 Terrain).

Art (unit / building frames):
- `original`: the original frames, unfiltered (nearest), with the Enhanced UI.
- `upscaled`: the built-in Scale2x (4x) frames, smoothly filtered. This was the Enhanced look
  before art packs existed.
- `<folder>`: replacement images from `artpacks/<folder>/`. A frame the pack lacks uses the
  `upscaled` version.

Terrain (independent of Art and Effects; any combination works):
- `original`: the original tiles, unfiltered.
- `upscaled`: the built-in Scale2x (4x) tiles, or the chosen art pack's tiles (a tile the pack lacks
  uses the upscaled one). With Art `upscaled` this is exactly the Enhanced look from before the switch.
- `designed`: the designed terrain baked from above (see "Designed terrain" below). Option label "Designed".
- `faithful`: "Designed (faithful)", terrain v3 - the same bake laid out after the original tiles (see
  "Designed (faithful)" below). Option label "Faithful". The terrain of the Enhanced preset.

The settings take effect in a running game, through the in-game Options too.

CLI flags for one run, for screenshots: `--art-pack NAME`, `--terrain2d original|upscaled|designed|faithful`, `--effects classic|modern` and
`--shroud classic|soft` (without `--shroud`, a one-frame run follows `--effects`: Modern -> Soft).
`--frames N` runs a one-frame mode (`--screenshot`, `--smoke`) for N real-time frames, takes the
screenshot on the last one and logs the frame times. Use it to capture effects in motion and to
measure performance.

## Art packs

### Layout
```
<pack>/pack.ini                 [pack] name=..., scale=N  (texels per original pixel)
                                optional: derive_house=0|1 (default 1), derive_shadows=0|1 (default 1)
<pack>/DATA/<frame>.png         RGBA base colour; house-colour pixels as a neutral grey ramp
<pack>/DATA/<frame>.house.png   optional 8-bit house-colour intensity mask
<pack>/DATA/<frame>.shadow.png  optional 8-bit shadow mask
<pack>/DATA/hotspots.txt        optional: "<frame> <x> <y>" per line, the frame's own hotspot in texels
<pack>/<TILESET>/<tile>.png     terrain tile, e.g. BLOXBGBS/123.png (32*scale square)
```
`<frame>` is the DATA.R16 entry index and `<tile>` the tileset entry index. Folder names are matched
case-insensitively. `artpacks/` is gitignored, because packs are derived from copyrighted art.

**Search paths** (`ArtPack::searchDirs`):
1. `<pref path>/artpacks/` (`~/Library/Application Support/d2k-mac/Dune 2000/artpacks/`)
2. `artpacks/` next to the executable and up to four folders above it. For `build/d2k` that
   includes the repository root; for the app bundle, the folder that holds `Dune 2000.app`.
3. `artpacks/` in the working directory.

### Alignment
The image's top-left corner is the original frame's top-left corner. The draw position uses the
original hotspot (`anchor - R16Frame.x/y`), and the size in world pixels is
`(png.w / scale, png.h / scale)`. A pack image made at exactly `scale` times the original
therefore covers the original frame pixel for pixel. Extra width or height extends right and down.

A frame listed in `DATA/hotspots.txt` has its own geometry instead (packs rendered from 3D models,
docs/art-pipeline.md "Rendered from 3D"): texel `(x, y)` of the image lands on the frame's anchor,
so the image may be larger than the original or offset from it. `SpriteCache` gives such a frame its
own header (`width/height = png size / scale`, `x/y = hotspot / scale`, rounded to whole original
pixels; the tool writes multiples of `scale`), which every draw path (2D, 3D billboards, collapse,
repair icon) uses in place of the R16 header. House and shadow masks are never derived from the
original's palette indices for these frames.

### House colours
For a pixel with house mask `m` (0..255), the base colour's luminance is mapped onto the house's
COLOURS.BIN ramp: the 16 entries are sorted by luminance, the grey range 0..255 is stretched over the
ramp's darkest..brightest, and adjacent entries are interpolated. The result is `lerp(base, ramp, m)`.
Without a `.house.png`, the mask is derived from the original frame's palette indices 240..255,
sampled nearest at the pack resolution, unless `derive_house=0`.

### Shadows
A shadow mask value `m` multiplies the destination by `1 - m/510`: 255 halves it, like the original
index-1 shadow blit. The shadow is a separate `SDL_BLENDMODE_MOD` texture drawn before the sprite.
Without a `.shadow.png`, the mask is derived from the original's index-1 pixels, unless
`derive_shadows=0` (use that when the base PNG already has its shadow baked in).

### Build-up
The original plays per-building build-up frames (DATA.R16, 1998 art) while a building is placed and,
backwards, while it is sold. Those frames: a dark splash on the slab at the centre, dark cracks and a
few stones spreading out, a heap of brown rocks piling up until it covers the whole footprint (about
the building's size), the building's outlines showing through the heap, then the finished structure
breaking out of it in a dithered dissolve with loose rubble left along its front and sides, which
clears in the last frames. No glow, no dust particles.

A building whose main frame comes from the art pack and whose pack has no build-up frame of its own
(`MapRenderer::packBuildup`) does the same with its finished pack art (`render/buildup_fx.*`). The
progress is the same counter (`buildupCounter / (3 x build-up frames)`, smoothed between ticks), so the
duration matches and the simulation is untouched; a sale runs it backwards (the building sinks into the
rubble, the heap grows back, then sinks away).
- The heap: about 30 procedural rocks per solid footprint cell (`buildupRocks`: a jittered Halton spread,
  a mound up to ~35% of the art's height; the rock atlas `makeBuildupRockTexture` is grey, faceted, lit
  from the top left, colour-modulated with the ground colour sampled around the footprint, with a contact
  shadow each). Rocks drop in from p 0.015 to 0.35, the low ones first. A darker stain of disturbed sand
  lies under it (`buildupStain`), starting as the original's centre splash.
- The rise (`buildupReveal(p)`, from p 0.06 to 0.92): the art and its shadow stand below a cut line that
  climbs in screen space from the base line (the front edge of the art box, `tileY*32 + artHeight`) to
  the top of the art; what lies in front of the base (aprons, the cast shadow) stands from the start, and
  the base frame rises with the main art. The cut is broken, stone-shaped (`buildupJag`, value noise in
  world x, so the art, its barrel and its base break alike), and its fresh edge carries a band of the
  ground's dust fading downwards. Nothing above the cut.
- Rocks the cut line passes slide down and fade (the structure pushes the rubble off); the ones at the
  foot of the walls (the front band and the sides) settle into a low ridge and, with the rest, tumble and
  sink away between p 0.66 and 0.94, the top of the heap first.
- Modern effects add dust puffs (soft ground-coloured puffs rolling off the heap and the edge) and a few
  faint, intermittent warm sparks at the edge. Effects Classic: the rise and the rocks, no particles. At
  night the heap is lit by the time of day's grade only, plus a dim warm work light
  (`SceneLights::addBuilding`); no glow.
- Buildings with original art (no pack frame, a mod's own art) and packs with their own build-up frames
  keep the original frames. Graphics: Original never takes this path (pixel-identical).
- 3D models: `model_fs` (`walkMode.yzw` = cut height, glow band, scan drift): fragments above a
  world-space plane rising from the foundation (flat parts up to 1.5 px stand from the start) to the
  model's highest vertex become cyan scan lines, a warm band glows below it (and lights the ground at
  night); the sun-shadow caster is squashed by the progress.
- Check: `D2K_BUILDUP=<0..1>[s][g]` puts every building at that point of its build-up (`s`: of its sale;
  `g`: first adds a gallery from cell (2, 2), a row per house) before the first frame.

### Loading and filtering
- Frames are decoded lazily on first use (`SDL_LoadPNG`), tinted per house, and cached per
  (frame, house, grey) like the original frames.
- Textures use linear filtering. SDL_Renderer has no mipmaps, so `SpriteCache` builds a
  premultiplied 2x box-filtered mip chain down to 1 texel per world pixel. `Sprite::level(zoom)`
  picks the sharpest level that is minified at most 2x.
- Tiles: `TileAtlas` is built at the pack's scale (up to 8). Replacement tiles are resampled to
  `32*scale` if their size differs and get their edges extruded into the padding. Missing tiles are
  Scale2x / 4x / 8x upscaled originals. Atlases at scale 4 or more also get a half-resolution copy
  for zoomed-out views.
- Loading a pack with ~5000 frames and 800 tiles adds about 1 s at game start (mostly the atlas).

## Designed terrain (`render3d/terrain_bake.*`)

"Terrain: Designed" brings the 3D mode's designed terrain (docs/3d-mode.md "Terrain v2": CC0
materials, Poly Haven rock meshes along the cliffs, sun shadows) to the 2D view. Gameplay, the
Original mode and every other layer are untouched: decals (craters), building bases, units,
effects, shroud, selection, placement grid and rally flags draw on top exactly as with tiles.

**Bake.** At the first frame of a map, `TerrainBaker` builds the designed heightfield + materials +
rocks (its own `DesignedTerrain` instance) and renders them with SDL_gpu from straight above: an
orthographic camera looking down -y with north up, so a world point (x, h, z) lands on (x, z)
whatever its height and cell (x, y) covers exactly the rectangle its tile would. The map is cut
into chunks of 16 x 16 cells; each chunk gets a sun shadow map (2048², D32, the same sun as 3D:
NW, 40°) fitted to its ground (the depth range covers the whole map, casters up-sun included) and
is rendered with 4x MSAA into its own texture. Texels per cell: as many as fit ~48 M texels
(multiple of 16, 32..128): A5V1 / A6V1 (64x64) 96, H5V1 (70x80) 80, a 128x128 skirmish 48
(`D2K_BAKE_PPC` overrides). Each chunk also keeps a half- and a quarter-resolution copy
(SDL_Renderer has no mipmaps); the draw picks the sharpest copy minified at most 2x. GPU memory:
~190 MB at the budget (level 0 + copies), plus a 4x MSAA chunk target.

**Draw.** `MapRenderer::terrainLayer` (`render::TerrainLayer`): when set, `drawTerrain` draws the
visible chunks with the tiles' own edge rounding (`round(t * 32 * zoom) - round(cam * zoom)`, so chunk
borders sit on the same pixels as tile borders) instead of the tile atlas, then the damage decals as
before. Linear filtering, no blending.

**Detail tiles (zoomed in;** owner 2026-11-11: at Enhanced zoom 2-3 on a Retina MacBook the terrain
looked pixelated and blurry - a cell covers 128-192 output pixels there, the chunks have 48-96 texels per
cell). When a cell on the screen (`32 * cam.zoom`; the camera zoom is in output pixels, so it includes the
display scale) is over 1.1 x the chunks' texels per cell, the VISIBLE part is re-baked as detail tiles of
4 x 4 cells at a level of 64 / 96 / 128 / 192 / 256 texels per cell (the smallest level at least 0.92 x the
on-screen size, above the chunks' own): Retina zoom 2 -> 128, zoom 3 -> 192, zoom 3.5-4 -> 256 (1:1 or
nearly); non-Retina zoom 3 keeps the chunks (96 is exact there). They are drawn over the chunks with
the same edge rounding. Baking: the same passes as a partial re-bake (shadow map fitted to the tile,
the terrain rows it needs, its rocks; rendered into the MSAA target, resolved into scratch_ and copied),
the shadow softness scaled to the light frustum of the chunk around the tile (so tiles and partial
re-bakes soften shadows by the same world px as whole chunks). Scheduling (`updateDetail`, called by
`drawTerrain`, so new tiles show in the same frame): the first frame after a build bakes every visible
tile at once (part of the build pause; screenshots are sharp); later the missing tiles of the wanted
level from the view centre out, then a one-tile ring around the view, ~1 M texels per frame (one
256-tile, two 192-tiles, four 128-tiles), not until the level has been the same for 6 frames (a wheel
zoom passes through levels without baking them); meanwhile each tile shows the nearest resident level
still sharper than the chunks, else the chunks. Cache: LRU under a budget of 160 MB (never a tile of the
current frame; a tile no view used for 1800 frames is freed). Spice / concrete changes mark resident
tiles like the chunks: a tile in view re-bakes the dirty part (same per-frame budget), one out of view
is dropped. Levels below the chunks' own are never used. Env: `D2K_BAKE_DETAIL=0` (off),
`D2K_BAKE_DETAIL_MB` (budget), `D2K_BAKE_DETAIL_FRAME` (texels per frame), `D2K_BAKE_DETAIL_SYNC=0` (no
first-view sync, to watch the progressive bake), `D2K_BAKE_PAN="dx dy"` (streaming test: the terrain
alone scrolls under the view), `D2K_BAKE_TIMING=1` logs each detail bake, `D2K_BAKE_TEST=1` also
re-bakes 3 x 3-cell parts of the first view's tiles.

Measured (M4 Pro, Debug, A5V1, 2560 x 1600 output; GPU time waited for after idle): Retina zoom 2: 25
tiles of 128 px/cell (1 MB each) 6.6 M texels in 14 ms (0.57 ms per tile), 25 MB; zoom 3: 12 tiles of 192
(2.25 MB) 7.1 M texels in 13-16 ms (~1.2 ms per tile), 27 MB; zoom 4: 9 tiles of 256 (4 MB) 9.4 M texels in
15-16 ms (1.7 ms per tile), 36 MB. Peak process footprint +37 / +37 / +45 MB against detail off. With the
progressive bake (no sync; or `D2K_BAKE_PAN="6 2"`, 36 output px per frame across the map: 170 tiles
baked, 99 evicted, cache peak 160 MB) the frame time stays at the 120 Hz rate (p95 9.1 ms, 9.3 with detail
off). Partial re-bakes of tiles match their full bake (zoom 3, H5V1: max channel difference 26, mean
0.015); of chunks at zoom 1 max 18, mean 0.010 (was 24 / 0.014 before the softness scaling).

**Top-down look** (`DesignedTerrain::setTopDown`, the baker only; 3D keeps its look): seen from
above only the sunlit tops show, so the crags would come out lighter than the original tiles (whose
mean includes their drawn shadows): cliff material ×0.72, rough ×0.9, rocks at the full cliff colour;
more ridge rocks (80 % of the cliff cells instead of 35 %); face rocks closer to their own cell
(3-6 px out instead of 8-12, 20-28 px across instead of 24-36) so crags stay on impassable cells;
more light contrast (ambient 0.46 / diffuse 0.78 instead of 0.62 / 0.55, shadows keep 12 % of the
sun). The shaders' bake mode (`fog2.w = 2`) skips shroud and the 8 px map-edge fade.

**Colour** (3D and 2D, `DesignedTerrain::computeTints`): after the colour lock to the map's tile
means, sand and dunes get saturation ×1.25 around the luminance and a slight orange bias, rock ×1.15,
rough rock ×1.3 and darker / browner, cliffs ×1.2; rock scans keep a tenth (was a fifth) of their
own greyish hue. The open sand read pale / grey next to the original's warm orange.

**Changes during play.** Every 250 ms `DesignedTerrain::update` reports the cells whose spice amount
(0..7) or concrete bit changed (a building placed on concrete changes nothing: the slabs look the same
with and without one - owner, 2026-11-11); the cell
±3 (the splat blur reaches one cell, the organic spice field - below - about three) is marked dirty in
every chunk it touches. Dirty parts are re-baked as small
regions (≤160 cells per frame): shadow map fitted to the region, the terrain rows it needs (row-major
index buffer), only the rocks within reach (+ their shadow margin) from a per-job instance subset,
rendered into a scratch target and copied into the chunk, then the region of the half / quarter
copies is re-blitted. `D2K_BAKE_TEST=1` re-bakes a grid of 3x3-cell regions after the build: the
picture matches the full bake (H5V1 zoom 1: max channel difference 15, mean 0.07, no seams).
Craters: with Effects: Modern they are the modern crater decals (below); buildings cover their cells as before.

**Cost** (M4 Pro, Debug build): first use decodes the textures (~2 s, once per run, shared with no
one: the 3D mode has its own instance); per map build A5V1 ~0.6 s (CPU 0.34 s heightfield / splat /
rocks, GPU 0.22-0.29 s for 16 chunks of 1536²), H5V1 ~0.65 s (CPU 0.45, GPU 0.18-0.28 for 25
chunks of 1280²). A harvested cell's re-bake (3x3 cells, often split over 2 chunks): CPU recording
0.2-0.7 ms, GPU ~1.2-1.4 ms (`D2K_BAKE_TIMING=20`: the job 20 times in a row after the GPU went
idle, averaged; a single timed job reads 5-15 ms only because of the GPU's clock ramp). Frame cost
otherwise: one textured quad per visible chunk instead of one per tile (H5V1 `--frames 3000`, zoom
1: CPU update+render avg 1.7 ms, the same as with tiles).

**Craters** (`render/effects_craters.cpp`; Effects: Modern + Designed terrain, 2D and 3D): the
terrain damage the simulation keeps per cell (cell +9; DamageTiles) is drawn exactly where and when
the original draws its crater sprite (damage > 0x10 on a rock / build-on cell or a sand cell; skipped
under full shroud), as a soft lit dent instead of the pixel-art sprite: a procedural 2-layer atlas
(8 variants: 4 sand, 4 rock) built at start-up - a ground layer (the dent's shading, lit from the
north-west like the bake, tinted with the cell's material colour: the tile mean pushed to the
designed terrain's warmer, more saturated look) and an overlay (pale blown-out dust on the rim and
in ejecta rays, light grains, then the charred blotchy centre, soot rays and dark debris specks).
Size grows with the damage level (damage >> 4: 26..56 px sand, 24..50 px rock), soot with it too;
the variant and a ±3 px offset come from the cell. 2D: `MapRenderer::drawTerrain` draws them over
the baked chunks instead of `drawDecals`; 3D: `Renderer3D::collect` lays them as ground quads when
the designed terrain is active. Effects: Classic, Terrain Original / Upscaled, the 3D tile terrain and
the classic view key keep the original crater sprites. Check: `D2K_VFX_DEMO=craters --vfx-demo --at X Y`
writes damage levels 1..15 into a row of sand and of rock cells; `D2K_CRATER_DUMP=file.bmp` saves the atlas.

**Spice** (`spiceField` in render3d/terrain_v2_msl.hpp + shaders.hlsl; Designed and Faithful, 2D bake and
3D; owner note 2026-11-11: thick spice read as square blocks). The spice texture holds per cell (RG8,
`DesignedTerrain::buildSpiceTex`) the *presence* - the amount up to thin spice (2 of the 7 levels) - and the
*richness* - what lies above thin (thick spice 2 more levels, blooms more), blurred with a 5 x 5 binomial
kernel. The shader reads both through a three-scale noise warp (45 / 17 / 6.5 px, up to ±22 px), the presence
with a round nine-tap kernel, and breaks the richness into clumps (noise at 9 / 21 / 47 px). Thin spice is
the speckled cover; its edge towards the sand dissolves against a noise threshold (lobes and specks, no
line). Thick spice is a soft richer, redder cloud with the original's darker mounds as ROUND spots
(`spots`: jittered points on a lattice turned off the map's axes, bigger and denser where the spice is
thick) - the first version thresholded a value noise for the mounds, which read as square blocks. No cell
grid at zoom 1 or 2. It still is the simulated amount: a harvested cell thins out and clears, a field shrinks where it is
harvested.

**Concrete** (`concreteSlab` in render3d/terrain_v2_msl.hpp + shaders.hlsl; 2D bake and 3D): the
concrete bit's cells are foundation slabs, one per cell, with a crisp border where they meet sand /
rock (the splat's concrete channel is not blurred; docs/3d-mode.md "Materials"). The Concrete040
scan is colour-locked to a designed slab colour (not the concrete tiles' mean, which made pale
sand-coloured slabs), then per slab: a seam groove on the cell edges with a bevel that catches the
sun on the north-west edges and falls into shade on the others, a shade / hue variation, panel
detail picked by a hash of the cell (expansion joint across the slab, bolt plates in the corners,
drainage slots along an edge), a few faded paint stripes (some hazard-striped), weathering (grime,
oil / scorch stains, chipped corners) and sand drifted into the seams and onto the edges in patches
(the map's sand colour). Styles (`kConcreteStyles` in terrain_v2.cpp; `D2K_CONCRETE_STYLE=0..2` for
one run): 0 grey (116,116,114), 1 blue-grey (104,110,118, most panel detail and paint; default),
2 weathered warm grey (114,108,98, dusty and stained). Uniform slot 7 of the designed terrain
(`tint[7]` = dust colour + style, `lum[7]` = detail, weathering, markings, drift). Building bases
and buildings draw on top as before. Check: `D2K_VFX_DEMO=base --vfx-demo --at 6 11` (H5V1) lays a
small base with an apron of slabs.

Not yet: the bake starts on the first frame (a short pause at mission start / on switching to
Designed in battle).

## Designed (faithful) (`terrain2d=faithful`, option "Faithful"; terrain v3)

The designed materials laid out after the ORIGINAL tiles (owner, 2026-11-10/11: keep the sand and the
buildable rock; cliffs like the original's; non-buildable rock obviously different; concrete like the
original's slabs). The terrain of the Enhanced preset. "Designed" above is untouched by it (its own
fragment shader and code paths; only the organic spice is shared). The method and the 3D side are in
docs/3d-mode.md "Terrain v3"; this section is the 2D bake.

- **Layout guide** (`render3d/terrain_guide.*`): at map load the map's own tile pixels are read (in
  memory only - nothing derived from the game data is stored or shipped, like the colour lock) and turned
  into four fields at 32 (maps over 96 cells: 16) texels per cell: rock coverage (the original's sand /
  rock outline), relief height, tone (how light / dark the original paints a rock pixel) and ruggedness
  (plain vs broken rock). The shader reads it smoothly upsampled - a Catmull-Rom bicubic through a slight
  noise warp (docs/3d-mode.md "Terrain v3", `guideAt`) - so zoomed in the original's pixels do not show as
  square blocks and the rock outlines are curves, not stair-steps. ~0.25 s for a 64 x 64 map, ~1.1 s for 128 x 128 (the file is compiled optimised
  in Debug builds too); the whole faithful bake of A5V1 ~0.85 s, of a 128 x 128 skirmish ~2.7 s.
- **Sand, dunes, spice, buildable rock**: the Designed materials, colours and light (the level ground's
  detail is lit by the Designed bake's north-west sun), so they look as in Designed.
- **Passability decides the look** (owner, 2026-11-11, checkpoint 4: pale crumbling rock that tanks can
  drive over was drawn as the same dark cliff as the real wall). The cell flags the simulation itself tests
  (`World::canUnitUseSquare`, CanUnitUseSquare 0x45d990: every wheeled / tracked unit needs the drive-on bit
  0x2000, infantry the walk-on bit 0x4000) sort the rock cells:
  - *vehicles can use it* (buildable rock, shores, rough non-buildable rock): never any relief, dark tone
    or boulder. Shores show the original's ragged outline with sand in the cracks and its pale broken crust,
    its dark specks held back; buildable rock is the only calm surface. The plateau may lie high behind a
    cliff, but it lies level;
  - *no vehicle can use it and the original paints a cliff face on it*: the raised dark cliff (below);
  - *no vehicle can use it, but the original paints it pale and low* (the crumbling ends of cliffs, rubble
    at the sand's edge, outcrops on the plateau, wrecks and bones): pale and low like the original, in the
    original's own tones, and marked as blocked by a boulder field - the rock meshes, in the original's
    shaded-rock colour, with their shadows: 3 - 7 per cell, 9 - 15 px, the first one big; where infantry
    can still walk 2 - 4 smaller ones (6 - 9 px), on the original's dark lumps first; a boulder never hangs
    over into a cell vehicles can use. No blocked cell looks like open ground. (Wrecks and bones themselves
    are not drawn yet - new art.)
- **Cliffs** (owner: "impassable rock must read unmistakably as a solid raised barrier"): relief instead of
  boulder meshes, PAINTED from a height field the way the original tiles show their 3D scene. The guide
  finds the original's faces - rock whose mean tone over 13 x 13 px is 10 .. 24 % darker than plain rock
  (shaded rock, crevices); a blocked cell with faces on a third of its rock is a full cliff, with an
  eighth none. Crumbling edge cells (part sand, almost no face) stay on the ground; every other rock cell
  takes its level from its neighbours (Laplace): a mesa's plain top stands as tall as its rim, the plateau
  behind an edge cliff is level with it and slopes away gently, a cliff tapers off towards a crumbling end.
  Inside a cliff cell the height rises from the sand outline over a face 20 px wide on the south side, 10 px
  east / west, 5 px north, and meets the level of passable rock next to it over most of the cell, so no
  face ever stands on or against a passable cell. The shader turns the height into tone (relative
  luminance), which indexes the original's own palette - the map's broken-rock pixels sorted into five
  tones (crevice, shaded, mid, lit, pale rim), drawn with crisp edges:
  - a face is darker than its top (-0.38, ±0.2 by the side it looks to: lightest towards the south-west
    sun, near-black facing north-east), follows the original's big light and dark, and is built in two or
    three tiers of blocky columns: each column a lit side falling off into a dark gully, a dark ledge
    line under each tier, every tier with its own columns (no gully runs from the foot to the top);
  - level broken rock (a rim's top, a ridge's crest, rubble) is the original's own tone; a thin broken
    pale rim line where a face breaks into its top, on the sunward sides; a dark foot and a soft contact
    shadow on the sand;
  - the CC0 crag scan (ambientCG Rock 026) adds grain and small relief on the faces only.
- **Light and shadows**: the level ground (sand, plain rock) is lit as in Designed. The cliffs' shading is
  the painted tone above (the mesh's macro shape is not lit in the bake). The shadows on the sand are real:
  the baked mesh is the same relief at half the cliff height (`TerrainGuide::heightAt` with
  `Params::topDown`), cast by a sun in the south-west (238°, 42° up) through a soft filter - a cliff's
  shadow lies 8 - 16 px over the sand to its north-east, like the original's. All rock stands 4 px above
  the sand (a low step along its outline).
- **Concrete** (`concreteSlab3` in render3d/terrain_v3_msl.hpp; owner: the original's slab pattern and
  colour with the used look of the Designed concrete): two slabs per cell (32 x 16 px, a plain grid), a pale
  seam along the south / west edge and a dark joint along the north / east edge, the slab colour of this
  tileset's own concrete tiles, speckle with the original's statistics (light specks paler, dark ones
  red-brown) - and used: a shade per slab (±10 %), grime in soft blotches, dark stains on a third of the
  slabs, a hairline crack on some, chipped corners, dirt along the joints and sand drifted into them. A
  slab looks the same with and without a building on it (owner, 2026-11-11: an earlier rule drew the slabs
  under buildings and their skirt cells paler, after the original's skirt tiles, and wiped the grime with
  it). Check: `D2K_VFX_DEMO=slabs` / `slabs-built` with `--vfx-demo` lay an apron of slabs and build a wind
  trap on it after the first bake - the two frames differ only in the building and its shadow.
- **Fallback**: a tileset whose tiles cannot be classified (no plain sand cells, or sand and rock too close
  in colour) gets the Designed layout (a log line). `D2K_GUIDE_OFF=1` forces that; `D2K_GUIDE_DUMP=<dir>`
  writes the four guide fields as BMPs plus two one-pixel-per-cell maps for the passability audit
  (`guide_cells.bmp`: blocked mask, infantry-only / crumbling / outcrop, level; `guide_flags.bmp`: rock
  share, cell class, the drive-on / walk-on bits as the relief read them); `D2K_GUIDE_RES=16|32` picks the
  guide resolution.

Measured against the original and audited against the game's passability on whole maps of all six stock
tilesets (checkpoint 4 of the terrain work). Partial re-bakes match the full bake
(`D2K_BAKE_TEST=1`).

**State (2026-11-11, checkpoint 4 - the owner: good, stop here for now) and open items:**
- *Cliff polish*: the faces' columns, gullies and ledges are procedural - the original's individual facets
  and big sloped planes (the hollow east of the A5V1 pipeline) are not copied; the pale rim line is thin and
  broken; a cliff's top is the original's tone on the rough material, plainer than the original's.
- *Wasteland blacks* (BLOXWAST): the original's cliffs there go almost black (5th percentile of the
  luminance 11, spread 69; H4V1 at (31,57)); in Faithful that percentile is 34 (spread 61), so its faces
  read browner and flatter than the original's on that tileset. The five tone levels are averages of the
  map's own broken-rock pixels; a sixth, near-black level or a per-tileset face tone would fix it.
- *Blocked rock the original paints pale*: marked by boulders now. The owner may want it without boulders
  (exactly the original's look) or with a stronger mark - the counts and sizes are constants in
  `TerrainGuide::build` ("boulders").
- *Wrecks, bones, the cave of the wasteland tileset*: their cells get boulders / plain rock under the
  building sprite - new art needed.
- Man-made tiles of community tilesets (bridges) are drawn as banded rock.
- The guide is read at map start, so tiles an extended mission event changes later keep their first look.
- 3D: the same rule (faces as real geometry only on blocked cells with a face, boulder fields as meshes);
  checked on four views only, no 3D-specific tuning since checkpoint 3.

## Modern effects (`render/effects.*`)

`MapRenderer` owns an `Effects` instance when `effects=modern` (`setModernEffects`). Every frame,
`Session::render` sets the effect clock to `gameTicks + tickAccumulator`. Effect time is
`ticks / 20`, which equals real seconds at the default game speed. A pause freezes the effects, and
a faster game speed speeds them up along with the sprite animations.

### Detecting events
Each object slot remembers its last key: the type, plus the explosion frame or the bullet's
`moveSteps`. A new key, a frame that went backwards, or `moveSteps` that went up means a new
explosion or bullet. On the first frame of a view (a screenshot, after a load, after switching the
mode), the continuous emitters are pre-warmed for 2 s. Running explosions spawn their burst aged by
`frame / frames`, and bullets already in flight get no muzzle flash.

Shots come from the world's shot log instead (`World::shotEvents`, render-only, not saved: every
bullet ModelAddBullet created in the last 16 ticks with its muzzle point, aim point, firer and start
delay). Machine-gun bullets (LMG, HMG, M_LMG / M_HMG, FREMEN_S / L: 351 px per tick) are created and
land within the same tick, so they never exist when a frame is drawn and only their impacts showed
(the trike "not firing"); the log carries every shot exactly once, at its real rate. Each shot whose
fire tick (creation + delay) passed since the last update gets its muzzle flash (`spawnShot`): at the
end of the gun - the bullet starts at the firer + ShootOffset (2-5 px, inside the body), the flash is
pushed along the aim to infantry 6 px, vehicles 2 x offset + 4 px (trike / raider / quad 10, tanks 14,
devastator 18), turrets 12 px - plus, for machine guns, a tracer streak muzzle -> aim point
(1100 px/s, 24 px, with a faint full-length line for 0.06 s) and a short light (58 px, 0.08 s).
`D2K_VFX_DEMO=units --vfx-demo --at X Y` puts a column of real units (trike, raider, quad, infantry,
tanks) shooting at the ground through the real FireAt path.

### Effects
| trigger | effect |
|---|---|
| explosion class Huge / Big / Medium (classified by the Templates.bin explosion names: EX_BUILDING, EX_BANG*, EX_ROCKET, ...) | light flash, fire (alpha flames for colour, a few additive for heat), smoke plume with a delayed start and drift, streak sparks with gravity and bounce, debris chunks (Big/Huge), screen shake (Big/Huge) |
| EX_MG / EX_HMG impacts | sparks, a small dust puff tinted by the terrain, a short glow |
| MUZ_* / EX_FLASH | muzzle glow |
| EX_TRAIL / EX_SMOKE4 / EX_PUFF | nothing (the bullets draw their own trails) |
| EX_SMOKE3 (damage smoke, see "Original events") | a dark smoke puff per event: buildings bigger / denser with damage, vehicles smaller; nearly dead vehicles flare up now and then |
| EX_FLAME / EX_FLAME2 (building damage flames) | a lick of fire of varying size with a timed light, 12 % a brief flare-up with embers |
| EX_WORM / EX_WORMDUST / EX_WORMSN1..6 + the worm's sand particles | the sandworm's own sand (see "Original events"): the maw art stays, the rest is replaced |
| EX_HARVDUST* | modern harvest dust (see "Original events") |
| EX_DHM (Death Hand missile trail, one per flight tick) | smoke puff, as EX_TRAIL |
| EX_REF (refinery processing, one per unloaded spice blob) | the chimneys vent: a flame tongue with its light and embers from both chimney tops, then a column of brown industrial smoke that thickens while unloading goes on and fades after (see "Original events"; never a blast: EX_REF as "Huge" once made every unload look like a building blowing up) |
| EX_SPICEBLOOM (a bloom grows a stage) | the mound heaves: spice grains, a dust puff, a faint sand ring |
| EX_HARV (harvester unload art), EX_CASH / EX_CRATE* (crates), EX_WHITEN1 | nothing extra: animations, not explosions |
| new bullet | muzzle flash by weapon class (gun, cannon, rocket, artillery, sonic, devastator, deviator): glow, forward sparks, smoke puff; sonic: a pressure ring; deviator: a puff of house-coloured gas |
| bullets in flight | per-frame light; trails by distance travelled: rocket and artillery smoke (rockets: plus a small flame at the nozzle only), magenta plasma (devastator: a hot core trail behind each of its twin bolts, violet haze, arcing motes), house-coloured gas (deviator), ember smoke on debris |
| sonic shot (weapon SOUND, bullet flag 0x800) | a sonic beam instead of the projectile sprite (see below) |
| EX_DEVIATE (deviator hit) | a swirling cloud of the firer's house-coloured gas, a soft glow and motes (the original sprite and the conversion stay underneath) |
| EX_GREENEXP (the deviator's per-tick trail explosion) | nothing extra: the bullet trail draws the gas (the original sprite stays) |
| EX_DEVASTATOR (devastator hit) | magenta plasma burst that pulses twice (white-hot flash, then a magenta pulse ~0.09 s later: the twin bolts), three shock rings, streak motes, glow blobs, a little dark smoke, a scorch, a light kick; no flames |
| moving ground vehicles | dust trail (terrain colour; more on sand, wheeled vehicles more), tread marks on sand for tracked vehicles (fading decals, 9 s) |
| damaged vehicles / buildings | only the original's smoke events (EX_SMOKE3 / EX_FLAME above), no health thresholds |
| destroyed buildings (state 17) | the collapse sequence below; their EX_BUILDING death explosions become fireballs without their own light or shake (see "Modern weapon effects") |

#### Weapon looks
Each weapon's look follows its nature; nothing but real fire uses flames.
- **Sonic tank**: the original draws its shot as a lens distortion (`SetLensImage` 0x42ada0, bullet
  flag 0x800; re/notes/rendering.md §6a): a 16 px disc magnifying what is drawn under it 1.5x. Original
  graphics and Enhanced with Classic effects reproduce it (`MapRenderer::buildLens`, the drawn frame
  read back and bent on the CPU; continuous at other zooms). Graphics: 3D with Classic effects draws the
  same lens from the 3D scene texture after it is rendered (`Renderer3D::drawPostRefraction`: a 32-segment
  fan whose texture coordinates are centre + offset * 2/3, nearest sampling; no read-back), so no blue
  square is left anywhere. With Modern
  effects the projectile sprite is skipped (2D and 3D) and a beam is drawn from the emitter to the
  shot's head (`SonicBeam`, strips rebuilt every frame in `beams()`): a translucent rippling band on
  the ground (alpha, sky blue), 10 thin interweaving sine strands (cyan / sky / white / pale violet,
  varied width and brightness, phases flowing forward; an alpha pass for the colour on bright sand
  plus a faint additive glow), a bright core filament with a layered halo, a pulse running along
  the beam, arc wavefronts convex towards the target (`kArc`, bigger and fainter away from the
  emitter, each with a fainter echo) and motes riding the waves. When the shot ends: light-blue
  rings, a burst of motes and a ground ripple; the ribbon fades out over 0.45 s.
  **Heat haze**: the battlefield under and around the beam is bent in screen space
  (`Effects::buildHaze`): a mesh along the beam (5 px segments x 9 rows, 26 px to either side, a little
  narrower at the emitter) whose vertices stay put while their texture coordinates are displaced by up
  to 2.6 world px (two travelling sines across the beam, one along it; zero at the band's edges and
  ends, so it is seamless; fades with the beam). 2D: while any beam is on, `MapRenderer::draw` renders
  the map up to the projectiles into `hazeTex_` (same size, viewport and clip as the target), copies it
  to the target and draws the mesh from it, then the beams, particles, shroud and vignette as usual - no
  read-back, and frames without a beam take the normal path. 3D: the mesh samples the scene texture
  after the 3D picture is drawn; the beam is already in it, so the displacement is reduced to 30 % on
  the beam's own line (`coreKeep`).
- **Deviator**: a tunnel of gas in the firing side's house colour (COLOURS.BIN ramp shades 9 and 13,
  saturated; `setHouseColours`): billowing, swirling, slowly spreading `kGas` puffs with darker cores
  now and then, a faint inner glow and drifting motes; the hit releases a gas cloud of the same colour.
- **Devastator**: magenta / purple plasma (trail and hit, above).
- **Missiles**: smoke trail and a small flame at the nozzle only.
- **Guns / cannons**: tracer light and impact sparks (unchanged).

Lights use a custom blend, `dst * (1 + src)` (`SDL_ComposeCustomBlendMode(DST_COLOR, ONE, ADD)`),
so a light brightens what it falls on instead of washing bright sand out to white. If the backend
lacks custom blending, they fall back to dimmed additive. Hot cores and sparks are additive. Smoke,
dust and debris use alpha blending.

#### Destroyed buildings
DestroyBuilding 0x49ca10 leaves a building in state 17 for 30 ticks, drawn normally under its
2 x solidTiles EX_BUILDING explosions, and frees it on tick 31 (re/notes/combat.md §6). With Modern
effects `Effects` tracks each such building as a `Wreck` (from its first state-17 frame; DeadTimer 0 on
the first sight = sold, ignored) and keys everything to the time since the death (effect seconds =
ticks / 20, so the 30 ticks are 1.5 s at any game speed; a view opened mid-sequence spawns the past
events pre-aged). Size: `big` = (solid cells - 1) / 8 (turret, wall 0; windtrap 0.4; construction yard,
palace, factories 1) scales counts, sizes and speeds.

| time | what |
|---|---|
| 0 | flash on the surroundings (Light glow 5x the footprint), white-hot core, shake (0.45 + 0.55 big; big buildings up to trauma 1.25 = 9 px), 8 + 3/cell debris chunks flying out |
| 0 - 0.85 s | shockwave: a dark ring of pushed sand with a bright rim racing out to ~4x the footprint (ground mark) |
| 0.04 - 1.34 s | a chain of 3 + 0.6/cell fireballs of varied size at random solid cells (the last two bigger) |
| 0.1 - 1.4 s | the collapse (`collapseLook`): the art in 2 - 7 vertical pieces, each sinking below the base line with an accelerating fall (the middle first, staggered), its top leaning outwards, the colour charring to 45 %, trembling; turret barrels sink with the average |
| 0.1 - 1.7 s | a low dust cloud rolling out from the footprint's edges (terrain colour), dust boiling up along the base line (hides the cut) |
| 0.75 s | a second rumble and a slower wave of chunks crumbling off |
| 0.1 - 11 s | the smoke column: thick, dark (some puffs lit orange by the fire below), rising 26 - 44 px/s, drifting with the wind, thinning out after 2.5 s |
| 0.25 s - 28 s | the ruin (ground marks drawn after the building bases, `drawMarks`): scorch blotches, rubble stones, glowing embers (until ~14 s); fades out from 20 s |
| 1.2 s on | 1 + cells/3 small fires in the ruin (7 - 15 s, with smoke and a flickering light), sparks rising until 16 s |

Debris chunks (`kFxLandDust` / `kFxLandFire` / `kFxTrail` / `kFxLateFade` particle flags) arc with
gravity and bounce; their first hard landing puffs dust, 10 % start a small fire there (2.5 - 6 s),
30 % burn in flight (smoke and a flicker of flame behind them); lying chunks fade over the last
quarter of their 2.4 - 4.2 s. Fires and lights are skipped under the shroud; in 2D the shroud covers
the rest, in 3D every quad tests its map point. 3D draws the same pieces (billboard slices with the
UV sub-range, the top corners shifted along the camera's right axis) and the ground marks as terrain
quads.

#### Original events (`render/effects_events.cpp`)
Modern versions of the original's non-weapon events, each timed by the simulation's own event (the
simulation is untouched; Effects: Classic, the classic view and Graphics: Original keep the sprites).
- **Harvest dust** (EX_HARVDUST0..7: one per load step, every harvestLoadSpiceDelay ticks, at the
  harvester; the digit = facing / 4): a burst when it appears, a trickle (8/s) while the original
  animation runs. Pale sand-spice dust plumes (one denser darker core) start *behind* the harvester -
  where the original draws its dust - and drift with the wind; spice grains are thrown up and fall
  back; a low skirt of dust rolls out under the body.
- **Damage smoke / fire**: no health thresholds any more. The original spawns EX_SMOKE3 for a damaged
  vehicle every ~4 ticks while its smoke timer runs (64..191, set by each hit below half health, one
  less every 4 ticks: 13-38 s) and, for a selectable building below half health, EX_SMOKE3 / EX_FLAME
  at each BUILEXP.BIN point whose bit is set, with the point's period (3-12 ticks), the bits clearing at
  random over the timer's last 200 ticks. Each EX_SMOKE3 becomes a puff: on a building (the smoking
  building whose art box holds the point: 2 dark puffs, denser / larger with damage, started at roof
  height in 3D = the same drawn point in 2D), on a vehicle a smaller puff (under 25 % health a flame
  lick + flicker 20 % of the time). EX_FLAME: a lick of fire (size 0.65-1.15) and a timed light, 12 %
  a flare-up with embers. So smoke flares up after hits and dies out with the original's; walls,
  concrete and other structures without the kSelectable template flag never smoke or burn.
- **Walls breaking** (destroyed non-selectable structures, `Wreck::lite`): a short flash, a small kick
  (trauma 0.08), chunks (no burning / smoking ones) and dust; the EX_BUILDING bursts become small
  Medium bursts; no fireball chain, no smoke column, no ruin fires, sparks or embers, a faint scorch.
- **Spice blooms** (crate records of type 7..10; `Effects::replacesCrate` hides the bloom sprite in
  2D and 3D): a mound drawn as ground marks every frame - a dark ring of disturbed spice, a soft
  south-east shadow, a grainy dome lit from the north-west (`kMound`, spice orange, radius 5.5 + 2.6 x
  stage px), a highlight; from stage 2 a hot vent glowing with the pulse. Stage 0 (type 7) draws
  nothing, like the original (its crate art, DATA.R16 frame 106, is one pixel): it is the bloom's seed,
  and a burst bloom with regrowths left goes back to type 7 on the same cell (RecycleCrate), so the
  mound vanishes with the burst and reappears when the seed grows to stage 8. The mound breathes (rate 1.3
  + 0.45 x stage); a ripe bloom whose timer is under 300 ticks beats faster and swells, glows (a warm
  light) and seeps spice motes. Growth (EX_SPICEBLOOM): grains, a dust puff, a faint ring. **Burst**:
  a bloom of stage 8..10 leaves its record while SpiceMound's shells (groupIds[50] SPICEEXPL) start
  from its cell -> a flash (150 px), a geyser of spice (30 puffs shooting up 90-240 px/s and raining
  back), 22 clumps flung out, a low dust cloud rolling out, a dust ring and a faint shockwave, a fine
  golden haze settling for 3 s, a dark spice stain (scorch kind 4, 9 s). The shells fly as spice globs
  (shadow, clump, a thin spice-dust trail; no muzzle flash) and their EX_TANKGUN hit (detected near a
  live / just landed shell's target) is drawn as a spice splash (grains, puff, ring, stain) - no
  fireball, light or shake. The spice itself is the simulation's (the terrain shows it).
- **Capture** (CaptureBuilding: the old record dies in state 17 with DeadTimer 1 and kDontRemoveTiles,
  the copy lives on for the new owner): never a wreck - no blast, collapse, smoke or shake; a soft glow
  in the new owner's house colour (COLOURS.BIN shade 13) over the footprint, a thin ring, motes rising
  and a 0.6 s light.
- **Refinery processing** (EX_REF = groupIds[80]; economy.cpp updateUnloadingSpice, state 15: one per
  unloaded blob, harvestUnloadDelay = 60 ticks ~ 3 s apart, at refinery pos + refineryExplosionOffset
  (62, -79)). The original sprite is flames from the refinery's two chimney tops (the short west one at
  (-34.5, -9) and the tall one at (+8, -37) drawn px from that point, measured from the sprite; the same
  for every house). Modern (`Effects::spawnRefineryVent` / `updateRefineryVents`): every event sends a
  flame tongue out of each pipe (fed for ~0.8 s, hot at the lip, tapering, bending with the wind), a
  hot core, a flash light and 2-4 embers, and a thick puff of smoke; a vent record per refinery keeps a
  heat (+0.3 per event, at least 0.55, cap 1.3; holds for 3.6 s, then cools at 0.32/s) that drives a
  continuous rising, wind-drifting column of dark orange-brown smoke from both chimneys (rate, size and
  darkness grow with the heat), small flame licks and a flickering pipe glow for ~2.4 s after each burst.
  So the column thickens while a harvester keeps unloading and is gone a few seconds after the last
  blob. In 3D the chimney points stand on the building card (a point d px up the art: d cos(tilt) north
  of the south edge, d sin(tilt) high; `Effects::setCardTilt`, set by the session around the 3D effects
  update), so the flames leave the drawn chimney tops at any sprite tilt. With 3D models the vents are the
  model's `fx_flue*` anchor points instead (any number up to 8, each with its own strength; docs/3d-models.md
  "Anchors"), so they leave the redesigned refinery's own flue tops.
- **Sandworms** (`Effects::updateWorms`, the owner: "these grey dots and sparks are still old sprites"): the original
  shows a worm only through effects - while it travels (state 27, not cloaked) Worm_SpawnSandParticles drops 8
  one-pixel specks of the tile colour per tick (World::particles), now and then (rand % 80) a wormsign burst
  EX_WORMSN1..6, and when it swallows a vehicle its maw EX_WORM (the redesign pack renders it) and EX_WORMDUST. Modern
  skips the specks (`Effects::replacesWormParticles`, 2D and 3D) and the wormsign / dust sprites; driven by the worm's
  position and state: a lit hump of sand heaving along over it (kMound tinted to the terrain, a bow wave ahead, a dark
  ring of disturbed sand), furrows of pushed-aside sand settling behind it (scorch kind 5, ~5 s), ripples (kind 7, a
  sand-coloured ring), sand dust and grains; each wormsign = a spray of sand, grains, dust and a ring of heaved sand; the
  swallow = sand erupting round the maw, sand clumps and the victim's metal flung out, a sand-coloured shockwave and dust
  ring, a small camera kick, churned sand (kind 6, ~12 s) and the swallow's dust settling. The maw art stays (it is the
  worm itself; there is no SANDWORM model, so 3D draws it as before as a sprite). `D2K_VFX_DEMO=worm --vfx-demo --at 31
  50` (H5V1, `--reveal`): a worm awake 3 tiles west of three unarmed vehicles of the player (MCV, harvester, MCV).
- `D2K_VFX_DEMO=harvest|bloom|damage|capture|refinery --vfx-demo --at X Y` (H5V1: harvest 27 50, bloom 33 38,
  damage / capture 45 46, refinery 8 8) stage each case through the real simulation paths (sim_test.cpp);
  `D2K_VFX_DEMO_REFINERY="ATREIDES REFINERY"` builds another refinery template (default Harkonnen).

### Modern weapon effects (`render/effects_weapons.cpp`)
With Effects: Modern the original projectile, muzzle-flash and explosion sprites are **not drawn** in
Enhanced 2D and 3D (`Effects::replacesExplosion(type)` / `replacesBullet(weapon)`, checked by
`MapRenderer::drawExplosions` / `drawBullets` and `Renderer3D::collect`); the effects draw their own,
following the simulated objects (positions, flight, timing), so the simulation is untouched.
Kept as sprites: non-weapon animations (EX_HARV, EX_CASH, EX_CRATE*), the sandworm (EX_WORM*) and
unknown names / weapons; harvest dust, damage smoke, EX_REF and the spice bloom have modern versions
("Original events"). Classic effects, the classic view
key and Graphics: Original keep every original sprite exactly.

| what | Modern look |
|---|---|
| muzzle flash (`spawnMuzzle`, per shot from the shot log at the gun's end) | a directional flash star (`kMuzzle`, along the shot, length / width per weapon: gun 13x8, cannon 24x14, artillery 30x18, rocket backblast), a hot core glow, a timed light (gun 58 px / 0.08 s ... artillery 112 px / 0.14 s), smoke blown forward (cannon, artillery) or back (rockets); the MUZ_* / EX_TRIKEM / EX_FLASH sprites are hidden |
| machine guns (LMG, HMG, Fremen; the bullets arrive within a tick: from the shot log) | a tracer (`spawnTracer`): a 24 px bright streak flying muzzle -> target at 1100 px/s, a faint line along the path for 0.06 s |
| cannon shells (80 / 110 mm, BULLET14) | a hot slug with a short glowing streak, a faint wisp trail, a small light |
| rockets / missiles (bazooka, rocket, missile tank, hydra) | a capsule body, a flickering flame jet and nozzle glow, a dense white smoke trail (every 3 px, spreading, 1.4 - 2.3 s), a moving light |
| artillery / grenades (155 mm, GRENADE, SPICEEXPL) | a dark capsule shell, a hot glow, a faint trail, a ground shadow |
| bombs, debris | a dark falling bomb with a shadow; burning chunks (ember glow, smoke) |
| devastator, deviator, sonic | devastator: TWIN plasma bolts 8 px apart across the flight (the original DEVBULLET frames show two orbs, MUZ_DEVAS two flashes; still ONE bullet per shot in the model), twin muzzle flashes; house-coloured gas orb; sonic beam as before |
| small-arms impact (EX_MG / EX_HMG; `spawnImpact`) | armour (a vehicle / building cell, or in the air): 3-4 short sparks, a glow, a tiny light, a smoke wisp; sand: dust puffs in the terrain colour and flying grains; rock: dust, a chip and maybe one spark |
| fireballs (EX_ROCKET ... EX_BANG14, EX_155MMGUN, EX_DEATHHAND, EX_DEVDEATH; size per type, `exScale_`) | flash core + timed light; a billowing fireball of `kFire` puffs (additive white-hot cores, deep orange alpha flames, soot), colour held hot then cooling (`kFxHot`), part of it rolling up for up to 0.6 s; secondary bursts for blasts over 1.3; a smoke column released over ~1 s (first puffs lit orange) that rises and drifts; a few falling sparks (no glitter); debris chunks (vehicle deaths and bigger) with dust where they land; on the ground: a dust ring pushed out, a dust-ring mark, a scorch decal (`kScorch`, 22 - 50 s, glowing for 2 s), a bright shockwave ring for Huge ones. Sizes grow ~s^0.6 above 1 and counts with s, so big blasts keep detail |
| vehicle death (EX_WHITEN1 under the death explosion) | a burning wreck: a charred hulk mark (scorch + blackened chunks, 45 s) and a fire (9 - 14 s) with a dark smoke column and a flickering light |
| destroyed buildings | the EX_BUILDING sprites become staggered fireballs over the building (thinner where they bunch), on top of the existing collapse / smoke / ruin sequence; a shockwave ring at the death |
| building damage | smoke from 60 % health (grey, thickening and darkening, up to 3 points, turrets included), fire from 40 % growing to 10 %, flickering light; the BUILEXP smoke / flame sprites are hidden |
| damaged vehicles, EX_SMOKE*, EX_TRAIL*, EX_PUFF | the existing damage smoke and the bullet trails draw these; the sprites are hidden |
| Death Hand missile (EX_DHM, per flight tick) | thick smoke puffs, the motor flame and a light |

Particles are capped at 9000 (`kMaxParticles`); explosions and impacts thin out (x0.7 / x0.45) once
the budget is 40 % / 67 % used (`Effects::load`). Timed lights are capped at 256, ground marks at 420
(the oldest scorch goes first).

### Light list
`Effects::lights()` is the frame's list of light sources (rebuilt by every `update`): every
explosion's flash (a timed light: `addFlash(x, drawnY, z, reach, colour, life, attack)` - instant
or with an attack, then a quadratic fade; peak x 0.5 in the list) and its glow while it burns,
muzzle flashes, projectiles in flight, fires (ruins, burning wrecks, damaged buildings / vehicles),
sonic / plasma / gas hits. A `Light` is `{x, y, size, c, z}`: the drawn point (ground x, ground
y - z), the height of the source above the ground, the reach in world px and an RGBA colour whose
alpha is the intensity (255 = 1). Lights under full shroud are dropped.
- **2D** (`drawAir`): each light is a soft glow (`kGlowPM`) of diameter `size` drawn with the Light
  blend `dst * (1 + 1.6 * c * a)` after the objects, so it brightens the ground, the units and the
  buildings under it (a backend without custom blending falls back to dimmed additive).
- **3D**: `Renderer3D::buildPointLights` turns the strongest 48 visible lights into shader point
  lights (`render3d/lights.hpp`, docs/3d-mode.md "Dynamic lights"); the terrain, rocks, models and
  sprites / smoke are lit per pixel.
- **Lamps** of the time of day (vehicle head / tail lights, building lamps, floodlights) are a second,
  independent list (`render::SceneLights`, also without Modern effects; see "Lighting" below): 2D draws it
  before the effects, 3D gives it the slots the effects leave (spot lights for headlight beams). The time of
  day itself scales the sun / ambient terms (the point-light block's head) and leaves the lights additive.

Textures are all procedural (no external assets): one 2304x128 atlas of 18 cells of 128 px (glow,
smoke, spark, debris, flame, ring, soft, premultiplied glow, wave, gas, arc, line, fire, muzzle
star, tracer streak, capsule, scorch, shock ring) built from value noise at start-up.

Checking it: `--vfx-demo --at X Y` fires every weapon kind in rows and repeats vehicle deaths, a
heavy shell, a missile hit, a devastator hit and a Death Hand around the tile; `--record DIR` saves
a deterministic frame sequence (docs/STATUS.md).

Performance (M-series MacBook, 120 Hz, debug build, `--frames 600`, CPU update+render avg / p95):
4-player `--battle` (4PLAY1, 2500 ticks, zoom 1): Enhanced Classic 0.51 / 1.18 ms, Enhanced Modern
0.98 / 1.85 ms, 3D Classic 1.28 / 2.41 ms, 3D Modern 1.04 / 2.09 ms; the `--vfx-demo` stress scene
(~10 explosions and 15 shots a second on screen, zoom 1): Enhanced 2.02 / 3.88 ms, 3D 2.72 /
4.85 ms. All runs stay locked to the 120 Hz vsync (frame avg 8.34 - 8.36 ms).

### Presentation
- **Smooth turning**: vehicles with 32 direction frames ease their body and turret facing towards
  the model facing (exponential, rate 20/s; jumps of more than 6 steps snap). The closest frame is
  drawn rotated by the residual angle (at most ±5.6°), shadow included, about the unit's anchor.
- **Unit placement** (all Enhanced effects levels): units, flyers and their overlays are placed at
  their exact (interpolated) sub-pixel position, rounded once on the screen grid like the terrain;
  the frame offset is rounded separately so body, turret and cargo keep a fixed layout around the
  anchor. (Flooring to world pixels first made a unit at zoom 2 advance 2,2,2,2,2,2,4 px per frame,
  and at zoom 1.5 the turret hopped a pixel against the body with the anchor's parity.) The
  Original mode keeps the original whole-pixel placement.
- **No bobbing**: an earlier sine bob of 0.35-0.55 world px while driving was removed - on the
  whole-pixel screen grid it could only show as 1-2 px vertical hops ~20 times a second at
  GameSpeed 100, which read as the units shaking.
- **Screen shake**: big explosions add trauma, weighted by their distance from the view centre. The
  offset is `trauma² * 6 px` of smooth pseudo-noise and is applied only to the map camera (the
  HUD and the input mapping are unaffected).
- **Grade and vignette**: a faint warm multiply and a soft vignette over the map view.

## Lighting: time of day and Modern lighting (`render/lighting.*`, `render/scene_lights.*`, `render/bloom.*`)

Enhanced 2D and 3D (Original graphics never: byte-identical). Two settings, Options → Graphics → Custom →
**Lighting** (one row: "Time of day" | "Lighting") and the in-game options (Enhanced / 3D):

- **Time of day**: Day / Dawn / Dusk / Night / Cycle (the Enhanced / 3D presets' default since 2026-11-13, owner).
- **Lighting**: Classic (the old light; only the time of day changes it) / Modern (the daylight look: warm
  directional sun against a cooler sky = more lit / shade contrast, a slightly richer colour, bloom on
  highlights, a stronger sun highlight on 3D metal, faint lamps).

The Enhanced and 3D presets set Day + Modern; the struct defaults are Day + Classic, so one-frame /
screenshot runs look exactly as before unless they pass `--time-of-day day|dawn|dusk|night|cycle` /
`--lighting classic|modern`. Classic + Day is the identity (`SceneLight::identity`): nothing below runs
(measured against the commit before: Original byte-identical, Enhanced / 3D at most 2 pixels off by one level -
float rounding of the rewritten light term). Render only: no simulation state, vision or shroud changes;
everything is a function of the settings and the game clock, so screenshots and recordings stay reproducible.

### The light (`render::SceneLight`, `sceneLight()`)
Per time of day: the sun's azimuth turn and elevation factor against the view's own base sun (3D tiles /
Designed: north-west 40°; Faithful: west-south-west 48°; never below 20°, so the 2D bake's caster margins and
the 3D shadow-map fit stay valid), the sun (diffuse) and sky (ambient) colours as multipliers of the old light,
a saturation (night: towards grey, then the moonlight tints it - otherwise orange rock stays orange under a
blue light), the haze tint (3D), the lamp strength, bloom strength / threshold and the 3D specular factor:

| time | sun turn / elevation | sun colour | sky colour | saturation | lamps | bloom (Modern) |
|---|---|---|---|---|---|---|
| Day, Classic | - | 1 | 1 | 1 | 0 | 0 |
| Day, Modern | - | 1.20 1.05 0.84 | 0.86 0.90 1.00 | 1.06 | 0.12 | 0.4 (threshold 0.84) |
| Dawn | -40°, x0.62 | 1.06 0.92 0.90 (peach-pink) | 0.76 0.76 0.94 (lilac) | 0.72 | 0.55 | 0.55 (0.72) |
| Dusk | +28°, x0.6 | 1.04 0.78 0.50 (red-gold) | 0.48 0.48 0.64 | 0.78 | 0.8 | 0.55 (0.72) |
| Night | +12°, x1.1 (moon) | 0.28 0.37 0.58 | 0.25 0.31 0.48 | 0.55 | 1 | 0.75 (0.6) |

The turns stay near the base sun on purpose: the original tiles (and the Faithful relief painted after them)
and the sprites' drawn shadows are lit from one side, so a dawn sun from the east would contradict them.
**Cycle** loops Day (40 % of the cycle) → Dusk → Night (26 %) → Dawn → Day over 5400 game seconds (ticks / 20): 30 minutes at GameSpeed 100 (owner 2026-11-13), 90 minutes at the default GameSpeed. The cycle carries over between games (owner 2026-11-13): when a played game ends, its clock is kept in settings.ini `cyclePhase` and the next game starts there (previews and one-frame runs start at day and never write it)
(effect seconds = game ticks / 20, so it pauses with the game and is reproducible; smoothstep between the keys;
`D2K_CYCLE_SECONDS` sets the length, e.g. for a recording). `SceneLight::grade(ambient, diffuse, sunY)` is the
colour flat sunlit ground gets relative to the old light - the 2D grade and the 3D billboards' tint.
iPad lite profile (`deviceProfile=lite`): the colours only, no bloom, no lamps (`applyLiteProfile` also sets
Lighting Classic).

### 3D
The point-light block (`render3d/lights.hpp`, fragment uniform buffer 1 of every lit shader) got a head:
`sunColor`, `ambColor`, `spriteTint` and `info.y` = specular factor, `info.z` = saturation. Every lit shader
(terrain_fs, terrain2_fs, terrain3_fs, rock_fs, model_fs, sprite_fs; MSL + the HLSL twin, SPIR-V regenerated)
computes `satur(albedo) * (ambient * ambColor + diffuse * N.L * shadow * sunColor) + albedo * pointLights`
(`sunAmb`, `satur` in `lights_msl.hpp` / the top of `shaders.hlsl`); billboards take `spriteTint`; glTF
highlights x `info.y` x `sunColor`. The renderer turns its sun (`Renderer3D::baseSun_` → `sun_`) every frame, so
the shadow map follows the time of day (Cycle: the sun moves); the baked per-vertex visibility (shadow map off)
keeps the base sun. Haze colour x `fog`. glTF materials that are emissive but not light sources (the infantry's
flat-shaded `fx_*` skin / cloth) now follow the light (`lit` 0.35 in `model_gltf.cpp`: tinted like a billboard);
a material whose name contains GLOW / LIGHT / LAMP / FLASH keeps glowing at night (devastator `GLOW`,
thumper `fx_lamp`, muzzle `FLASH`).

Point lights may be **spot lights** now: `spot[i]` = beam direction + cos of the half angle (w < -1: a point
light); the beam fades over the outer 45 % of its cone. `buildPointLights`: the effects' lights first (strongest,
at most 2/3 of the 48 slots while lamps are on), then the lamps by priority x intensity x reach, falling off with
the distance from the view centre. Headlight cones are spot lights from the lamp tilted to reach the ground half
way along the beam; pools are point lights above their point. The lamps' flares are additive billboards pulled
14 px towards the eye (so a model's own surface does not hide them).

### 2D
- **Terrain**: the Designed / Faithful bake (`TerrainBaker::setLight`) is baked under the turned suns (relief,
  shadow and detail sun) with the sun / sky colours **divided by the 2D grade** (the bake's own ambient 0.46 /
  diffuse 0.78), so flat sunlit ground bakes at its own colour; a change re-bakes every chunk at once (A5V1
  ~0.3-0.6 s, like the first build) and drops the detail tiles. Cycle bakes the Day light once and lets the grade
  follow the clock (no re-bake every minute). Original / upscaled tiles have no bake: the grade alone.
- **Grade**: `MapRenderer` multiplies everything drawn up to the projectiles (terrain, buildings, units,
  flyers) by the grade (`SDL_BLENDMODE_MOD` rect, `lightingGrade`); the Modern effects' alpha particles (smoke,
  dust) drawn after it get the same factor (`Effects::setGrade`); their light / additive particles keep glowing.
- **Lamps** (below) are drawn next with the Light blend `dst * (1 + c)`; since dst was darkened by the grade, a
  lamp's c is divided by the grade (`SceneLights::setGrade`) - it lights the ground's own colour, as in 3D - in up
  to three passes of at most x2 (vertex colours stop at 1). Then the flares (additive), then the effects.
- **Bloom**: with Modern lighting the battlefield up to the lights is drawn into an offscreen copy of the view
  (as the sonic haze does; the haze nests inside), copied back and its bloom added.

### Sun-following sprite shadows (`render::spriteShadowWarp`, `MapRenderer::drawShadowWarped`)
With Modern lighting the sprites' drawn shadows turn and stretch with the sun like the baked terrain shadows
(owner 2026-11-13). Every unit / building shadow is a separate layer already - the art pack's `.shadow.png`
or the original frame's index-1 pixels - so this works for the redesigned art and the original sprites alike
(body, unit turrets / barrels, building turrets, overlay frames, the base frames and the Modern build-up).
- **Geometry**: the art's shadows point south-east (sun north-west, 40°; measured on the redesign pack: median
  bearing ~144°, close enough). The object gives its ground contact - units: the unit position, a box of ±2.5 px
  (infantry) / ±7 px (vehicles); buildings: the bounding box of the solid cells (`tilesOccupiedSolid`; the apron
  cells receive the shadow), 4 px inside. A shadow point that lies `s` beyond that box along the art's direction
  `d0` (the slab entry going back along `d0`; 0 on / under the box, so the shadow stays attached) is moved to
  `p - s d0 + s k d`: `d` = the sun's shadow direction (`d0` turned by `SceneLight::dAz`), `k` = cot(el) / cot(el0)
  (el = el0 x `elK`, never below 20°): a skew / stretch around the object. Frames rendered from 3D models (own
  hotspot, `Sprite::rendered3d`) were lit by a 58° sun (tools/render_sprites), so they use el0 = 58° (`kPack`;
  dawn x2.2, dusk x2.3, night x0.85); original art 40° (dawn x1.8, dusk x1.9, night x0.87).
- **Darkness**: the mask is drawn as a 12 x 12 grid (`SDL_RenderGeometry`) with the blend
  `dst x lerp(1, mask, a)` (src = DST_COLOR, dst = ONE_MINUS_SRC_ALPHA, vertex colour = a): `a` = the 2D bake's
  shade loss (ambient 0.46 / diffuse 0.78 under this light) against Day's, at most 1, times a fade of up to a
  quarter for long shadows (k 1 → 2).
- Day (dAz 0, elK 1), Classic lighting and Original graphics: inactive, the old `SDL_BLENDMODE_MOD` blit
  (pixel-identical). Not warped: flyers' ground silhouettes and the collapse of destroyed buildings.
- Checks: `D2K_NO_SHADOW_WARP=1` draws the shadows as before; `D2K_PREVIEW_SCENE=1` puts the Graphics previews'
  scene (base, tanks, infantry) on the `--map` of a one-frame run.

### Lamps (`render::SceneLights`)
Rebuilt every frame from the World (read only) for the view + 160 px; lamps on fully shrouded cells, of cloaked
or fading units and of dying buildings are left out; buildings of a side below 100 % power are dimmed and
flicker. Strength = `SceneLight::lamps` (the visible lamps themselves at least 0.2 - 0.4 of it, so tail lights and
beacons show faintly by day with Modern lighting). Blinking uses the effect clock and a per-object phase.
- **Vehicles** (by template: small = trike / quad / raider, big = harvester / MCV / devastator, else medium; the
  Modern effects' eased facing when available): a warm headlight cone ahead (64 - 92 px, ±24°) + two headlamps,
  two red tail lights + a faint red pool; harvester / MCV: an amber rotating beacon on the roof; a harvesting
  harvester: amber work lamps on both sides. These offsets were measured on the original sprites.
- **Redesigned vehicles and flyers** (the owner: "what is the floating light over the harvester?"): a unit type drawn
  from a redesigned art - a 3D model (`ModelHook::hasUnitModel`) or pack frames with their own hotspot - gets only the
  lamps its model carries as anchors (`SceneLights::addModelUnit`): `fx_light_head*` (white lamp + the headlight cone
  from their middle), `fx_light_tail*` (red + pool), `fx_light_beacon*` (rotating amber), `fx_light_work*` (amber, lit
  while harvesting, the pool thrown ahead), flyers `fx_light_port*` / `_starboard*` / `_strobe*`; flares pulled only 3 px.
  No anchors: no lamps. 3D takes the model's anchors through its placement; 2D (`PackAnchors::unitAnchors`) projects
  the pack's models.ini anchors as render_pack renders them (turned by the eased facing, orthographic 45 degrees) from the
  unit position + `anchor2d_dy` (where the rendered frames draw the model origin: render_pack's vertical centroid
  alignment, e.g. harvester 6.5 px, MCV 5; flyers add their flight height). Original-art units keep the old offsets.
  `D2K_VFX_DEMO=mcv --vfx-demo --at 6 12` (H5V1): three MCVs - one driving, one deploying at tick 120, one standing.
- **Flyers** (carryall, ornithopter, frigate): red port / green starboard lights, a white tail strobe.
- **Buildings** (per kind from the special behaviour / building group; points on the footprint): construction
  yard and factories - a floodlight pool on the apron in front, lamps on the door posts, a red beacon; refinery -
  an orange processing glow (slow pulse), beacon, amber lamp; barracks / palace - a lit door, windows (palace:
  two beacons); starport - landing lights running round the pad + a dim glow; outpost - a blinking red radar
  beacon, a green lamp; research centre - a cyan glow; repair pad - four blinking green corner lights; silo -
  an amber lamp; wind trap - a cool cyan glow; turrets - a slow red lamp. Walls / concrete: none.
- **3D models with anchors**: a building model's `fx_light*` anchors (docs/3d-models.md "Anchors"; e.g. the
  redesign's barracks, construction yards, high-tech factories) become warm lamps with a pool each instead of
  the per-type door / window points.
- **Redesigned buildings** (2026-11, the owner's "lights floating in the air"): the per-type points above were
  measured on the original sprites. The redesigned art is laid out differently (3D models shrunk / moved onto their
  cells, other roof heights, aprons in front), so the door-post / window flares and beacons hung in the air or on
  the empty ground beside the new buildings, and the 14 px pull towards the eye of the 3D flares made it worse. A
  building type drawn from a redesigned art - a 3D model (`ModelHook::hasBuildingModel`) or an art-pack frame
  rendered from one (its own hotspot in `hotspots.txt`) - gets none of them (`SceneLights::update(...,
  redesigned)`, filled by `Session::prepareLighting`): its visible lights are its own emissive surfaces
  ("Emission" below) and `fx_light*` anchors (a small halo pulled only 3 px, `Flare::pull`); the only extra light
  is one soft pool from inside the footprint that follows the glow (wind trap cyan, larger; refinery orange;
  research centre cyan; others warm and faint; none for silos / turrets). The original sprites keep the old points.
- **Star port landing lights** (the owner: "the starport had lights when the frigate arrives"): the original's
  primary star port runs its one-shot building animation while the CHOAM frigate carries its cargo in
  (re/notes/starport.md "Flight"); the 2D pack's overlay frames are those landing lights (the check pack driver's
  "dots"), and now also their own glow layers (`render_glow.py --overlay-lights`, the repair pad's guide lights too;
  `MapRenderer` queues the overlay frame's glow like the main frame's). 3D: the redesigned star port models have
  `fx_landing_N` anchors (the pack's models.ini: the lamp sockets in the pit, Atreides / Harkonnen 8, Ordos 7) and
  `SceneLights` lights them in sequence with the animation frame (green, every third amber, the last red), blinking
  over the last frames, plus a faint green pool. The smuggler star port has the same 7 anchors on its pad
  (the Ordos star port's pattern; 3D only: its 2D overlay frames are still the original's).

### Emission (glowing surfaces: lamps, energy cores, lit windows)
The owner's idea: light from inside the buildings instead of lamps beside them. The redesigned models carry glTF
emission (docs/3d-models.md "Emission"): the lamp beads, the cyan louvres / light strips (wind trap energy cores,
research centres, Ordos strips and glass orbs), red / green warning lamps, the Harkonnen red domes, the
refinery's arched windows.
- **Strength** `render::buildingGlow(world, templates, side, building, lamps, time)`: 0 without lamps (Day +
  Classic: nothing changes), `0.25 + 0.95 x SceneLight::lamps` (Day + Modern 0.36, dusk 1.0, night 1.2); off while
  building up / selling / destroyed (state 17); below half health it flickers; a side without power: off, low
  power: dim (0.22-0.44) and flickering; wind traps instead breathe with their energy (x 0.78-1.08, 2.2 s period). 3D models
  (`buildingGlow(..., dayPulse)`, owner 2026-11-13: the 2D original's wind trap visibly works by day): the wind
  trap pulses in the rhythm of the original's working animation (its `animCounter` over frames x animationSpeed
  = 30 ticks, interpolated within the tick; damaged = no animation: the 2.2 s breath), a peaked pulse p²:
  `k x (0.55 + 0.45 p²) + 0.55 x (1 - lamps) x p²` - by day 0.2 .. 0.84 (readable), at night 0.66 .. 1.2; off if
  the side has no power at all. 2D keeps the breath (its frames carry the original animation).
- **3D**: model_fs adds `hue(albedo) x emission x strength` after the light (may exceed 1: with HDR it blooms).
- **2D**: an art pack frame may have `<frame>.glow.png` (RGB emission, same size and hotspot as `<frame>.png`;
  `ArtPack::loadGlow`, `Sprite::Level::glow`, additive, mip-mapped with the frame). `MapRenderer` queues the glow of
  every building frame it draws (main and base frames) with that building's strength and adds them after the time
  of day's grade (x up to 2 in two passes), before the lamps and the bloom. By day the frames look as rendered
  (the lit surfaces are in the colour frames already), so the layer only shows with Modern lighting / at night.
  Rendered by `tools/render_sprites/render_glow.py` (below / docs/3d-models.md).

### Performance (M4 Pro, Debug, 120 Hz; 4-player skirmish `--map 4PLAY1.MAP --skirmish --skirmish-ai 3 --battle
--ticks 8000 --reveal --frames 600`, redesign pack; the machine was shared with other jobs, so +-0.3 ms is noise)

| view | Day + Classic: CPU avg / frame avg (p95) | Night + Modern: CPU avg / frame avg (p95) |
|---|---|---|
| 2D, AI base, zoom 1 (1280 x 800) | 1.36 ms / 8.49 (9.50) | 1.79 ms / 8.54 (9.25) |
| 2D, whole map, zoom 0.33 | 1.55 ms / 8.58 (9.61) | 1.86 ms / 8.52 (9.18) |
| 2D, AI base, zoom 2, Retina full (2560 x 1600) | 1.43 ms / 8.55 (9.32) | 1.64 ms / 8.50 (8.96) |
| 3D models, AI base, zoom 1 | 1.06 ms / 8.39 (8.84) | 1.22 ms / 8.35 (8.96) |
| 3D models, wide, zoom 0.4 | 1.35 ms / 8.34 (9.16) | 1.49 ms / 8.35 (9.21) |
| 3D models, wide, Retina full | 1.30 ms / 9.88 (10.64) | 1.62 ms / 11.13 (13.30) |

Everything holds the 120 Hz vsync except 3D at Retina full resolution, which already misses it at Day / Classic (GPU
bound; the night adds ~1.2 ms: 8 lamp lights + bloom). The lamp budget came from the wide 3D view: with all ~40 lamps
as lights it ran at 9.68 ms (p95 13.1); 0 / 12 lamp slots 8.34 ms (`D2K_LAMP_SLOTS`). The 2D lamps are quads (no
budget). Lite profile: no lamps, no bloom.

## Quality: HDR rendering, tone mapping, EDR output, MSAA, anisotropy, VSync (`render/hdr.*`)

Options → Graphics → Custom → **Quality** (Enhanced / 3D; Original never reads these: a `--graphics original`
screenshot is byte-identical with any value) and Display → VSync | Frame limit. settings.ini `hdr`, `hdrOutput`,
`msaa`, `aniso`, `vsync`, `fpsLimit`. **Quality** Low / Medium / High is a shortcut (`Settings::applyQuality`):

| level | HDR rendering | MSAA (3D) | anisotropy (3D) |
|---|---|---|---|
| Low (iPad lite profile) | off | 1x | off |
| Medium | on | 2x | 4x |
| High (Enhanced / 3D presets; new players) | on | 4x | 16x |

Any other mix shows as Custom. The values are not part of the preset match (like Display). The struct defaults
(one-frame / screenshot runs) are no HDR, 4x MSAA, 16x anisotropy: screenshots stay as before (Enhanced
identical, 3D at most one level off on 1 pixel in 60000 - the designed terrain had a fixed 8x before).
A settings.ini from before that names the Enhanced / 3D preset gets High and the EDR output.

### HDR rendering + tone map (`render::Hdr`, `tonemap_fs`)
With Modern lighting the battlefield is already drawn into an offscreen copy for the bloom (2D) / resolved into
its own texture (3D). HDR rendering makes that copy half float (`Hdr::createTarget`: RGBA64_FLOAT with the
**sRGB colourspace**, so SDL passes its values through raw - a float texture with the default linear colourspace
would be converted). Light blends (`dst * (1 + c)`), additive glows, lamps and the 3D point lights / emissive
materials keep values above 1. 3D: `Renderer3D::Quality::hdr` picks R16G16B16A16_FLOAT for the colour target (+ its
MSAA texture), wrapped the same way. Then:
1. `Bloom::compute` (steps 1-3 of `apply`, unchanged maths) blurs the unclamped picture;
2. `Hdr::compose` draws the scene with a custom fragment shader of SDL's GPU renderer (`SDL_CreateGPURenderState`;
   `tonemap_fs`, MSL in `shaders_msl.hpp` + HLSL twin in `shaders.hlsl` → SPIR-V; slot 0 = the scene, slot 1 = the
   bloom): `scene + srgbEncode(bloom) * strength` (the 8-bit path adds the bloom that way: SDL encodes the linear
   bloom texture) → linear light → exposure (1; `D2K_EXPOSURE` for tests) → a **hue-preserving filmic shoulder**:
   identity up to 0.8 linear (sRGB ~0.91, so sunlit sand is untouched), then `knee + room * (1 - exp(-(m - knee) / room))`
   on the largest channel, approaching the peak (1 = SDR white); pixels hotter than the peak drift towards white
   (fire cores, flashes) → sRGB.
Classic lighting in a float 3D scene: no shoulder, clamped at 1 (the old picture). Without the GPU renderer
`Hdr::available()` is false and everything stays 8-bit. The sonic haze / lens post pass samples the float scene
raw (clamped).

### EDR / HDR display output
SDL's own HDR route (`SDL_PROP_RENDERER_CREATE_OUTPUT_COLORSPACE_NUMBER` = SRGB_LINEAR, supported by the GPU
renderer) was tried and rejected: the renderer then blends **everything** in linear light (also into 8-bit
targets) - shadows got lighter, the night grade weaker, lamps duller; the whole UI changed. Instead the renderer
stays an sRGB one and:
- `App::initVideo` (only when `hdrOutput` and the game starts in Enhanced / 3D, desktop): the window is created one
  pixel short and windowed, `SDL_SetGPUSwapchainParameters(..., HDR_EXTENDED_LINEAR, present mode)` switches the
  swapchain to half-float linear, then the window gets its real size / fullscreen - the GPU renderer re-creates its
  backbuffer in the swapchain's format when the swapchain size changes (SDL_render_gpu.c `GPU_RenderPresent`). After
  the third present the backbuffer format is read back; if it is not float the swapchain goes back to SDR (logged).
  `applySync` re-applies the composition (SDL_SetRenderVSync re-sets the swapchain with SDR).
- every frame is drawn into a half-float sRGB-colourspace **canvas** (`App::beginFrame`; `render::screenTarget()`
  replaces `nullptr` for code that returns to the screen, e.g. the Original frame), so every blend is exactly the
  SDR one; `App::presentFrame` draws the canvas into the backbuffer with `tonemap_fs` as a plain sRGB → linear
  decode (no clamp), then presents.
- the tone map's peak becomes the display headroom (`SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT`, refreshed on
  `SDL_EVENT_WINDOW_HDR_STATE_CHANGED`), capped at 4x; it writes extended sRGB-coded values above 1. The UI stays at
  SDR white. A screenshot reads the canvas and clips at 1 (an EDR screenshot equals the SDR one except ~700
  highlight pixels). MacBook Pro XDR: "display headroom 16.00x", "extended range active (half-float backbuffer)".
  Windows HDR: the same SDL calls, not tested yet. The row reads "Restart to apply" until the next start.

### MSAA, anisotropy (3D) and the display
`Renderer3D::Quality {msaa, aniso, hdr}` is fixed per renderer (the pipelines depend on it); `Session::sync3d`
rebuilds the 3D renderer when it changes. MSAA 1 / 2 / 4 (lowered to what the device supports for the colour and
depth formats). Anisotropy goes to the tile-array sampler, the designed terrain's material sampler and the model
texture sampler. VSync: `SDL_SetRenderVSync` 1 / 0 (IMMEDIATE or MAILBOX) / adaptive (unsupported by the GPU
renderer: falls back to On, logged). Frame limit: frames are paced with `SDL_DelayPrecise` to the limit (also under
vsync when it is below the refresh rate); VSync Off without a limit runs uncapped. Checked headless: the 30 fps limit
gives 33.33 ms frames; VSync Off was accepted (IMMEDIATE) but a hidden window still ran at the 120 Hz display rate.

Cost (M-series, Debug, `--vfx-demo` at night, CPU update+render avg / p95, all at the 120 Hz vsync): Enhanced HDR
off 1.54 / 2.48 ms, on 1.76 / 3.38 ms; 3D MSAA 1x 2.03 / 2.85, 4x 2.27 / 3.00, 4x + HDR 2.23 / 3.00 ms.

## Soft shroud (`render/soft_shroud.*`)

`shroud=soft` replaces the original's tile-shaped edge pieces (DATA.R16 frames 38+, reverse-subtract,
`MapRenderer::drawShroud`) with a wide smooth fade into darkness. Render only: it reads
`Cell::shroud` and never changes the shroud rules; the minimap is untouched. Original graphics always
uses the original pieces (a `--graphics original` screenshot is byte-identical with either value).

- **Mask**: 2x2 texels per cell (at most 256x256 for a 128x128 map). Clear cells are 1, fully
  shrouded cells (low nibble 1) 0, edge cells (2..15) the brightness of the matching quadrant of the
  original edge piece (`1 - mean(pixel)/31`), so the outline follows the original's edge shapes.
- **Blur**: separable gaussian on the CPU, sigma 2 texels (1 cell), radius 6, clamp to edge (a
  revealed area stays lit up to the map border). Two masks are blurred: the shape above, and
  "not in or next to a fully shrouded cell".
- **Bias into revealed space**: `light = ramp(shape, 0.35, 0.95) * ramp(open, 0.5, 0.95)`
  (smoothstep ramps). Texels inside a fully shrouded cell or one texel next to one are forced to
  black, so bilinear filtering of the stretched texture is exactly black anywhere inside a fully
  shrouded cell; the "open" factor reaches 0 on that boundary, so the forced black never shows as a
  hard edge. Net result: black from half a cell outside the boundary, a ramp to full light about two
  tiles into the revealed area.
- **Texture**: black with alpha = darkness, `SDL_BLENDMODE_BLEND`, linear filtering, drawn over the
  whole map with the terrain's origin snapping (`round(cam * zoom)`), so it stays locked to the tiles
  at any zoom, with smooth scrolling and screen shake.
- **Incremental**: every frame the shroud bytes are compared with the last copy (16K bytes max);
  per 16x16-cell block, the bounding box of changed cells is re-blurred (plus the kernel radius)
  and uploaded with a sub-rect `SDL_UpdateTexture`. Results are identical to a full rebuild
  (checked during a `--battle`). Debug build: a typical update 0.2-0.4 ms, a full 64x64 rebuild
  6 ms (once per map).
- The vignette (`drawPost`) is drawn after it and simply multiplies on top; both are black, so the
  edges of the view and the shroud fade into each other without banding.

### Draw order
Terrain, then decals, then tread marks (`drawGround`), then the building bases, the sorted list,
the flyers and the projectiles, then particles and lights (`drawAir`), then the shroud, then the
vignette (`drawPost`). Effects under full shroud are not spawned, and the shroud covers the rest.

### Performance
Particles are capped at 9000 and tread marks at 4000 in a ring. Everything is drawn in four
`SDL_RenderGeometry` batches (ground, alpha, light, additive) from one 768x64 procedural texture
atlas (glow, smoke, spark, debris, flame, ring, soft, premultiplied glow, wave, gas, arc, line). A
sonic beam is about 500-800 strips; four sonic tanks and four deviators firing at once at zoom 2
measured about 1.0-2.5 ms CPU per frame on average (p50 0.6-1.0 ms), the same order as before. Measured on an M-series MacBook with a ProMotion display, vsync at 120 Hz, during a 4-player
`--battle` at zoom 1 (`--frames 600`):

| configuration | CPU update+render avg / p95 | frame avg / p95 |
|---|---|---|
| Enhanced, classic, upscaled | 1.59 / 3.82 ms | 8.39 / 9.14 ms |
| Enhanced, modern, upscaled | 1.74 / 4.81 ms | 8.40 / 9.22 ms |
| Enhanced, modern, upscaled-sample pack | 1.62 / 3.91 ms | 8.41 / 9.29 ms |

All three hold 120 Hz. The occasional spikes come from lazy texture creation.

Destroyed buildings and the sonic haze (same machine, debug build, `--frames 300/600`, CPU
update+render avg / p95): six buildings (palace, heavy factory, refinery, barracks, windtrap, large
turret) destroyed at once at zoom 2: Enhanced Classic 0.99-1.55 / 2.4-3.3 ms, Modern 1.56-1.62 /
3.1 ms, 3D Modern 1.34-1.50 / 3.0 ms (run-to-run noise is larger than the difference); 4-player
`--battle` (4PLAY1, 2500 ticks, zoom 1): Classic 1.15 / 1.98, Modern 1.66 / 2.71 ms; three sonic tanks
firing at zoom 2: Enhanced Modern (beams + haze) 2.22 / 3.48 ms vs Classic (CPU lens) 1.65 / 3.14 ms,
3D Modern 2.81 / 5.28 ms, 3D Classic (GPU lens) 1.26 / 2.43 ms. Every run stays locked to the 120 Hz
vsync (frame avg 8.33 - 8.37 ms).

## Classic view (`Session::drawClassicPeek`)

An instant before/after comparison, like the Space-bar swap of C&C Remastered: with Enhanced or 3D graphics,
**Tab** (touch: the **1998** button) draws the battlefield in the original 1998 graphics while held
(`classicView=hold`, default) or until pressed again (`toggle`); `off` disables it. Graphics: Original ignores it.
A small "Original 1998 graphics" caption (`render/ui_text`, translated) shows at the top centre.

- **Key**: Tab. The original game binds Space in battle (Game__MainLoopFrame 0x448550: `KeyIsDown1[0x20]`
  opens the in-game menu like Esc `KeyIsDown1[0x1b]`), so Space stays free for that; nothing in the original
  (dune2000.ini `[Hotkeys]`, the hard-coded checks) nor in our session uses Tab. While paused the key still
  works (the Enhanced pause menu's own Tab focus step happens as well).
- **What swaps**: only the battlefield - terrain = the original tiles, units / buildings / projectiles /
  explosions = the original 1x frames (no art pack), effects = Classic, shroud = the original edge pieces,
  selection = the original bars / brackets / pips, whole-pixel unit positions (MapRenderer `classicSelection`).
  The Enhanced panel, radar, top bar, messages, placement squares, rally flag and the selection box stay - the
  point is the battlefield comparison, and swapping the panel would change the layout under the cursor.
- **How**: a second `MapRenderer` with its own original `TileAtlas` and a 1x `SpriteCache`, set up like the
  Original mode, draws into an offscreen target at one world pixel per texel from the whole-pixel camera
  (exactly the Original mode's picture), which is scaled to the Enhanced zoom (whole factors nearest, others
  SDL pixel-art filtering, below 1 linear) and shifted by the camera's sub-pixel part so it lines up with the
  Enhanced overlays and the input mapping. The original screen shake shifts it like in the Original mode.
  3D: while it shows, the 2D original view of the same camera centre and zoom is drawn and all input maps
  through the 2D camera (`Session::view3d()` is null); the 3D camera snaps back afterwards.
- **Instant**: the original atlas is built when the session starts in Enhanced / 3D with the key enabled (or
  on switching to Enhanced) and kept until the session ends or the mode becomes Original: 800 tiles = ~3.5 MB
  texture + ~3.1 MB CPU copy, ~28 ms once. The 1x sprite textures are built lazily the first time each sprite
  is on screen (small; all of DATA.R16 once would be ~22 MB), then cached. The designed terrain chunks stay
  baked (not updated while hidden); the Modern effects keep simulating unseen so they do not jump on return.
- Render only: nothing in `World` changes; `--simtest` and saves are unaffected.
- `--classic-peek` holds the key for one-frame runs: e.g. `--map A5V1.MAP --graphics enhanced --terrain2d designed
  --classic-peek --screenshot peek.bmp` vs the same without it.

## In-game UI

The Enhanced mode's in-game UI (`render/ui_skin.*`, `render/command_panel.*`, `render/sidebar.*`,
`render/ttf_font.*`, `render/ui_text.*`; laid out by `Session::layout` / `Session::render`). A modern
skin drawn procedurally at the screen's native resolution: no original UI bitmap (UIBB / UI_ENG) is
used or stretched anywhere in the chrome. Only the unit / building icons are original content (HQ
upscale, or AI-upscaled frames of an art pack: `Session::iconSprites_`, `Session::syncUiArt` picks the
chosen pack or the first installed one; "Original" art keeps the HQ icons).

### Scale
Settings "UI scale" (`Settings::uiScale`, settings.ini `uiScale`, CLI `--ui-scale auto|100..300`;
the options row shows "UI scale" in Enhanced / 3D and "Resolution" in Original): Auto =
`UiSkin::scaleFor`: k = window height / 540 in quarter steps (1.5 at 800 rows, 1.75 at 900, 2 at
1080), capped so the 160-px column stays under 30 % of the width; a percentage = k / 100 (capped at
40 % of the width). Read every frame, so a change in the pause options applies at once. Every piece
is laid out in original pixels times k and baked at its exact pixel size.

### Text
`render/ttf_font.*`: the **Play** font (Regular / Bold, Jonas Hecksher / Playtypes, SIL Open Font
License 1.1, `third_party/fonts/play/` with its OFL.txt; Latin + Cyrillic) compiled into the binary
(`cmake/EmbedFonts.cmake` writes `build/generated/embedded_fonts.cpp`) and rasterised with
**stb_truetype** (public domain, `third_party/stb/`) at whole-pixel sizes into per-size glyph atlases
(kerning applied). Line height = 10 px x the text scale (0.8 k in quarter steps: 13 px at 800 rows,
15 at 1080). Strings are UTF-8; a string that is not valid UTF-8 is a game string (Text.UIB) in the
active code page (CP1251 Russian / CP1252) and is converted. If the font fails, the original bitmap
font is used. Our own labels go through `render::tr()` (`render/ui_text.*`: English key -> French,
German, Spanish, Russian; the language follows Settings languageText / language, set every frame by
`App`). The table is `kRows` in `ui_text.cpp` plus one `ui_text_*.inc` per area (common, menu,
options, game, cheats); `render::trf()` formats a translated printf pattern (translations keep the
conversions in order). A game name mixed into one of our strings is converted to UTF-8 first
(`formats::lang::toUtf8`), since a string is decoded either as UTF-8 or as the code page as a whole.

The modern front-end pages and our in-game pages are drawn by `platform::Ui` (`platform/ui.*`): a
`Ui::ModernScope` switches its text to the same Play font (`Menu::frame` for every page except the
original .uil ones - `MenuPage::Original` / `Campaign`; `Session::drawPauseMenu` for the Enhanced pause
menu, options and slots and, in every mode, the cheat menu; the end-of-game prompt). The original
pages (campaign flow, Graphics: Original menus and Esc menu) keep the original bitmap font and
untranslated strings. `Ui::ellipsize` cuts UTF-8 by whole characters.

### Palette
Dune 2000's own colours, kept modern and crisp: dark brushed bronze over leather brown for the
column (the grain tinted R > G > B), bronze-copper icon rims, leather-brown button plates with a warm
lit edge, warm-black wells with a copper lip, a dark brown tooltip box with the amber border, sand /
amber text and icons. The house colour stays a subtle accent: the column edge line is mixed 55 / 45
with sand-amber, its glow is faint, and the minimap corner marks are toned 35 % towards amber. The
front-end widgets (`platform::theme`) use the same browns and amber.

### Panel transparency
Settings "Panel transparency" (`Settings::panelTransparency`, settings.ini `panelTransparency`, CLI
`--panel-transparency off|light|medium`; Enhanced / 3D, not original; default Off). Light / Medium:
`UiSkin::setPanelOpacity` 0.72 / 0.5 - the column is drawn over a dark warm veil (a gradient, deeper
to the right: a cheap stand-in for a blur that keeps the icons readable) and its metal at that
opacity, the edge line stays solid; wells (tray, readouts, the minimap screen) at 0.87 / 0.78.
Icons, text, buttons and the minimap picture stay opaque. The battlefield is drawn under the
column (`Session::drawW_` = the window width, 2D and 3D), while `viewW_` stays the visible part:
camera limits, centring, clicks (`Session::overUi`), edge of the clickable map.

### Materials and pieces (`UiSkin`, all procedural; signed-distance shapes, cached per size/state/house)
| piece | look |
|---|---|
| column (`panel`) | brushed bronze over dark leather brown: per-row grain, low-frequency value noise, a soft central fold; a 1.5-px sand-amber / house-colour line with a faint glow along the battlefield edge |
| wells (`well`: tray, readouts, minimap screen) | recessed rounded rectangle, inner shadow from the top, faint lit lip at the bottom, dark rim |
| minimap frame (`frame`) | a well with house-colour corner marks |
| buttons (`button`) | rounded plate: vertical gradient, grain, 1-px lit top edge / dark bottom, soft drop shadow; vector icon (amber metal gradient, dark edge, drop shadow) and an optional bold label. Hover: lighter body, amber border and outer glow, brighter icon. Pressed: darker, inner shadow, icon pushed down, no shadow. Lit (active: repair / sell click mode, shown Special section, selected tab): pressed look with a house-colour line and glow. Disabled: flat, grey icon |
| vector icons | wrench (repair), coin with "$" (sell), shield with cross (guard), arrow (retreat), double chevron (upgrade), frigate (starport), gear (options), triangles (scroll), house / soldier / tank / star (tabs) |
| icon bezels (`bezelUnder` / `bezelInner` / `bezelOver`) | drop shadow (amber hover glow, house-colour lit glow), dark well, the picture, inner shadow, glass gloss, rounded bronze-copper rim lit from the top-left; empty slots are dim bezels |
| readouts | credits: well + amber spice gem + bold amber number; power: well + round glass lamp (`lamp`, green / yellow / red) + output / demand; timer: bold red |
| power meter (`meter`) | vertical well, filled bottom min(percent, 200) / 200 (the original rule and colours: red < 51 %, yellow < 100 %, green), segment gaps, tick at 100 % |
| radar off (`radarOff`) | dark screen with house-colour scanlines and "RADAR OFFLINE", fading in over 64 frames (the original fades its house logo, FUN_00425fa0) |
| tooltip | dark brown box, amber 1-px border, bold title line |

The production clock stays the original's (the greyed icon with the bright one revealed by an
`SDL_RenderGeometry` fan clockwise from 12 o'clock, FUN_004263b0) plus a thin progress line; READY /
HOLD keep the original pulsing green.

### Layout (original pixels x k)
- Right column: minimap frame (8, 8, 144x144), command row (repair, sell, guard, retreat; 34x24 at
  y 158), UPGRADE / STARPORT (72x20 at y 186; disabled unless something can be upgraded / there is a
  starport), then the production panel from y 212: power meter on the left, a segmented control of
  four tabs (22 tall, ready badge, progress line), the tray with the 2-column icon grid, two scroll
  buttons at the bottom.
- Top: OPTIONS at the top-left, the mission timer centred over the battlefield, the power and credits
  readouts against the column.
- No selection panel: health shows as bars above the selected units and buildings (MapRenderer,
  health-bar setting; a selected building gets corner brackets around its whole footprint -
  tilesOccupiedAll, skirt included - and the bar centred above its art, plus the PRIMARY "1"; a
  selected own refinery / silo shows the side's spice storage as pips under the bar - the original's
  N - 1 pips, filled = min(spice * N / capacity, N - 1), N = 16 / 8; a selected harvester shows its
  load as 7 pips, green loaded / black empty like the original). Commands: repair / sell (click mode) /
  guard / retreat / UPGRADE / STARPORT on the column, deploy / self-destruct / thump / stealth with D
  or a second click on the selected unit, primary with a second click on a selected factory, building
  upgrades on the Special tab's list. Control groups stay on the keys (Ctrl+N assigns, N recalls).
- The panels sit over the battlefield (which keeps the full height); clicks, the wheel and the
  cursor over them never reach the map (`Session::overUi`).

Screenshot flags that help: `--window WxH`, `--ui-scale P`, `--select`, `--ticks N`, `--sidebar-page
starport|upgrade|select-upgradable|select-building|select-buildings`, `--cursor-at X Y` (tooltips,
hover glow).

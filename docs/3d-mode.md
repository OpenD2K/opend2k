# 3D mode (Graphics: 3D), stage 1: 2.5D battlefield

`Settings::graphics = ThreeD` (main menu Options → Graphics: Original / Enhanced / **3D**, or
`--graphics 3d`). It is the Enhanced mode (sidebar, top bar, radar, menus) with the 2D map replaced
by a 3D battlefield whose objects are the **OpenD2K 3D art pack** (see "Objects: models only, no original
sprites" below; without the pack the mode is not offered). The simulation is untouched: the renderer reads the
`World` (positions interpolated by `render/motion.*` as in 2D) and never writes it.

Code: `src/render3d/` (no game rules; the session only hands it the world, a camera pose and the
view size).

| File | What |
|---|---|
| `gpu.*` | App renderer creation (SDL GPU renderer, fallback), SDL_Texture → SDL_GPUTexture, forced submit |
| `math3d.hpp` | Vec3 / Mat4 (column-major, right-handed, Y up, clip depth 0..1) |
| `camera3d.*` | RTS camera: look-at point, zoom, yaw, tilt; easing; project / ray |
| `terrain.*` | Heightfield from the cell flags, normals, baked soft sun shadows, height query, ray cast |
| `terrain_v2.*`, `terrain_guide.*` | The designed terrains: Designed (v2) and Faithful (v3, laid out by a guide read from the original tiles) |
| `renderer3d.*` | Pipelines, textures, scene collection (sprites), render pass, picking, overlays |
| `shaders_msl.hpp` | The MSL shaders (compiled at runtime by SDL_gpu's Metal backend) |
| `shaders.hlsl` | The same shaders in HLSL → SPIR-V (`shaders_spirv.inc`, checked in) and DXIL (dxc on Windows) |
| `shader_blobs.*` | Picks MSL / DXIL / SPIR-V per GPU device and creates the shaders |

## SDL_gpu + SDL_Renderer on one window

SDL 3.4 (installed: 3.4.20) has `SDL_CreateGPURenderer(device, window)`: an `SDL_Renderer` that
draws with SDL_gpu. The app now creates its renderer with it (`render3d::createWindowAndRenderer`),
so there is one window, one swapchain and one `SDL_GPUDevice`:

- every `SDL_Texture` the 2D code makes (sprite frames with house colours / HQ / art packs, the tile
  atlas, the soft shroud mask) is an `SDL_GPUTexture` the 3D pass samples directly
  (`SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER`) — no second copy of any art;
- the 3D pass records its own command buffer into its own render targets (colour with 1x / 2x / 4x MSAA
  per Settings::msaa - R8G8B8A8, or R16G16B16A16_FLOAT with HDR rendering (docs/enhanced-graphics.md "Quality") -
  + D32 depth with an 8-bit stencil (D32_FLOAT_S8_UINT, else D24_UNORM_S8_UINT, else D32 without one; the stencil marks
  units on walkable building cells: docs/3d-models.md "Walkable cells"), resolved into an `SDL_GPUTexture`), submits it, and that texture is wrapped as an
  `SDL_Texture` (`SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER`, not owned by SDL) which the session
  draws into the map view with `SDL_RenderTexture`; the Enhanced UI is drawn on top as usual;
- ordering: both command buffers go to the same queue; ours is submitted before the renderer's
  (which is submitted at `SDL_RenderPresent`), so the composite always sees this frame's picture.
  The one catch: the GPU renderer records texture uploads into *its* pending command buffer, so a
  texture created this frame would be sampled before its upload ran. `submitRendererWork()` forces
  the renderer to submit (a 1x1 `SDL_RenderReadPixels`, which submits and waits) — only on frames
  where a texture not seen before is used (warm-up, new sprite frames, a rebuilt atlas).

Original and Enhanced look the same with the GPU renderer as with SDL's Metal renderer (checked
pixel-wise on A1V1: the only differences are a wall-clock animation that also differs between two
Metal runs). Fallbacks: `SDL_RENDER_DRIVER=...` or `D2K_RENDERER=sdl` use SDL's default renderer;
if the GPU renderer cannot be created the default one is used. Then (or if the device takes none of the built shader formats)
Graphics 3D logs `3D is not available here - showing the Enhanced battlefield instead` and the
session draws the Enhanced 2D map.

### Direct3D 12 / validation rules

**Anisotropic filtering is off on Direct3D 12** (`createWindowAndRenderer`): with SDL 3.4.20 an anisotropic
sampler in the 3D mode broke the device - right after the tile array every texture / buffer creation failed
with `DXGI_ERROR_INVALID_CALL` and the game crashed (all terrain styles; 2D was fine). Bisected on an RTX 3080
(2026-11-13) with `D2K_GPU_FEATURES=clamp` / `=aniso` (one feature only): depth clamping alone works,
anisotropy alone crashes. With anisotropy off, D3D12 passes `SDL_RENDER_GPU_DEBUG=1` silently in all three
terrain styles and a 6000-frame battle. `D2K_D3D12_ANISO=1` turns it back on (to retest a newer SDL). Vulkan,
the Windows default, keeps anisotropy.
**HDR is off on Direct3D 12 too** (`gpuHdrOk()`): the half-float 3D scene failed to create a pipeline
(`E_INVALIDARG`: a black battlefield) and the extended-range output swapchain crashed (command list close
failed). D3D12 runs an 8-bit scene and SDR output; `D2K_D3D12_HDR=1` retests it. When D3D12 is the requested
backend (Graphics API setting / `SDL_GPU_DRIVER`) the HDR-output start-up (a window one pixel short, resized at
the first frames) is skipped too - it crashed there. Known gap (accepted for 0.0.1): Auto falling back to
D3D12 on a PC without a Vulkan driver still takes that start-up when HDR output is on (the backend is only
known after the window exists).
Hidden windows (one-frame runs: `--screenshot`, `--simtest`, tests) never start HDR output: a hidden window
acquires no swapchain image, so the GPU renderer's backbuffer kept the one-pixel-short size while the output
reported the full size (viewport / scissor past the target: SDL asserts, Vulkan `VK_ERROR_DEVICE_LOST`).

Run with `SDL_RENDER_GPU_DEBUG=1` (SDL's GPU validation; add `SDL_ASSERT=always_ignore` so an assert
logs instead of prompting; `MTL_DEBUG_LAYER=1` for Metal's own) — it must stay silent in all three
graphics modes. Rules it enforces that bit us:
- device features: SDL's GPU renderer turns depth clamping and anisotropy off unless asked.
  `createWindowAndRenderer` asks for both and falls back without them (`D2K_GPU_FEATURES=0` forces the
  fallback); pipelines use `enable_depth_clip = gpuDepthClip()` (clamped as before when available)
  and samplers enable anisotropy only if `gpuAnisotropy()`. The log line `Renderer: SDL GPU renderer
  on ... (depth clamp on|off, anisotropy on|off)` says which;
- no SAMPLER usage on integer formats (the tile index map is R32_FLOAT);
- SDL 3.4's D3D12 backend gives a command buffer shader-visible heaps of 2048 sampler descriptors; a
  draw writes one per bound sampler when the bindings changed, a blit one. Crossing a heap boundary
  with a multi-sampler draw writes past the heap and leaves tables bound earlier in the old heap
  (device removed: DXGI_ERROR_INVALID_CALL, then NULL uniform buffers inside SDL). So: keep bulk GPU
  work (thousands of blits / mip levels) in its own command buffers and keep per-frame sampler
  rebinds well under ~2000 (SDL skips a rebind of the same texture + sampler).

Rejected alternatives: a second device/window (no texture sharing, two swapchains), reading the 3D
frame back to the CPU (far too slow), `SDL_GPURenderState` (custom fragment shaders only — no
depth, no 3D vertex input).

## Shaders

Two sources, kept in step by hand (same entry points, uniform blocks and maths):

- **MSL** in `shaders_msl.hpp` (terrain, sprites) and `models_msl.hpp` (models, shadows) —
  `SDL_GPU_SHADERFORMAT_MSL`, compiled at run time by SDL_gpu's Metal backend (macOS, iPadOS). Slots per
  `SDL_CreateGPUShader`: uniforms `[[buffer(n)]]`, textures `[[texture(n)]]` with matching `[[sampler(n)]]`,
  vertices via `[[stage_in]]`.
- **HLSL** in `shaders.hlsl` (every entry point: `terrain_vs/fs`, `sprite_vs/fs`, `model_vs`,
  `model_shadow_vs`, `model_fs`, `shadow_fs`, the designed terrains' `terrain2_fs`, `terrain3_fs`, `rock_vs/fs`,
  `depth_fs`), registers per SDL's rules (vertex uniforms `b0 space1`; fragment
  textures/samplers `t[n]/s[n] space2`, uniforms `b0 space3`; attributes `TEXCOORD<location>`). Built offline:
  - **SPIR-V** (Vulkan: Windows, Linux; also MoltenVK) — `tools/compile_shaders.sh` runs glslangValidator with
    `--auto-sampled-textures` (each texture + sampler pair becomes the combined image sampler SDL's Vulkan backend
    binds; spaces become descriptor sets) and writes `shaders_spirv.inc`, which is checked in, so no build needs a
    shader compiler. Re-run it after editing `shaders.hlsl` (`brew install glslang spirv-tools`).
  - **DXIL** (Direct3D 12, Windows) — `cmake/CompileShadersDXIL.cmake` runs `dxc` (Windows SDK / Vulkan SDK) at
    build time → `<build>/generated/shaders_dxil.inc`, `D2K_HAVE_DXIL`. `D2K_DXIL=AUTO|ON|OFF`, `D2K_DXC=<path>`.

`shader_blobs.*` picks per device: MSL, else DXIL, else SPIR-V (`SDL_GetGPUShaderFormats`). `gpu.cpp` creates
the GPU renderer asking only for devices that accept the formats this build carries
(`SDL_PROP_RENDERER_CREATE_GPU_SHADERS_*`): a Windows build without dxc therefore gets Vulkan instead of
Direct3D 12; if no such device exists it retries with any GPU device (2D works, 3D falls back to Enhanced).
`SDL_GPU_DRIVER=vulkan|direct3d12|metal` forces a driver.

Verified on macOS: the SPIR-V path through Vulkan/MoltenVK
(`SDL_GPU_DRIVER=vulkan SDL_VULKAN_LIBRARY=/opt/homebrew/lib/libMoltenVK.dylib ./build/d2k --map A1V1.MAP
--graphics 3d [--models3d new] --screenshot out.bmp`) renders the same picture as Metal (mean difference
0.002/255, no pixel off by more than 16 — MSAA resolve). DXIL is compiled only on Windows (no dxc on macOS) and
is verified by the Windows CI job / a Windows build.

## Terrain

`Terrain::build` classifies every cell from its flags (the TILEATR bits copied in at setup):

- **sand** (sand bit, incl. spice and dunes): level 0; spice is raised 0.6 px per amount step
  (re-checked ~2.5x per second, so harvested fields flatten);
- **dunes** (terrain type 2): + two crossing low-frequency sine waves (7 px, crests ~5 tiles apart),
  weighted by a blurred dune mask so they fade out at the edges;
- **rock** (passable and not sand, or build-on / concrete / building cells): a plateau
  (`kRockHeight` 22 px) — with a 3-cell ramp where rock meets sand through passable cells (vehicles
  drive up a slope), full height elsewhere;
- **impassable, not sand** (cliff faces, outcrops): their level is relaxed between the fixed
  neighbours (80 Gauss-Seidel sweeps of the 4-neighbour mean), so a cliff band becomes a steep
  slope from the plateau down to the sand; impassable cells 2+ cells from anything passable rise
  0.35 levels per cell (max 3): outcrops / mesas. Impassable cells whose tile colour is much nearer
  this map's mean sand colour than its mean rock colour (crater floors) count as sand.

Mesh: 2 vertices per cell edge (cell centres are vertices: a cell's own level is exact there, so
narrow cliff bands stay steep), alternating diagonals. Per vertex: position, normal (central
differences), and sun visibility from a soft heightfield shadow march towards the sun (NW, 40°;
penumbra widens with distance) — baked, no shadow map. A 128x128 map is 66k vertices / 131k
triangles, one draw call.

Texturing: the session's `TileAtlas` (upscaled tiles or the art pack's) is blitted into a
`texture2d_array` (one layer per tile, ≤128² texels, full mip chain, generated on the GPU; built in
batches of 64 layers in a COLOR_TARGET scratch array, one command buffer each, and copied into the
SAMPLER-only array - see "Direct3D 12 / validation rules" above), and an R32_FLOAT texture holds each
cell's tile index as an exact integer (read with `Load`/`read()`, never filtered; an integer format may
not be sampled) (re-uploaded when tiles change: spice, concrete, rubble). The fragment shader projects the tiles top-down (`world xz / 32`), samples with gradients
of the continuous coordinate (no mip seams at tile borders). Light: ambient 0.62 + diffuse 0.55 ×
N·L × shadow, normalised so flat ground shows the tile colours exactly as in 2D; slopes facing the
sun get brighter, the others darker. Shroud: the Enhanced soft shroud mask (`SoftShroud::mask`)
multiplies terrain and objects (a sprite takes the darkness of its ground point, so tall sprites do
not leak past the edge). Distance haze towards a dusty fog colour (also the clear colour).

### Building foundations

Every terrain style levels the ground under buildings (`Terrain::updateFoundations`, called every frame by
`Renderer3D::render`; a sorted list of footprints is compared, so it costs nothing until a building is
placed or removed). Per alive building: its occupied cells (`tilesOccupiedAll`) get one height, the mean
**natural** height of their vertices (`hNat_`, the heightfield before foundations, kept per vertex because
the faithful relief comes from the layout guide that only exists during `build`); vertices within
`kFoundationSkirt` = 14 px of a footprint blend back to the natural height (smoothstep of the distance to
the nearest footprint cell; where two footprints meet, their heights are averaged). Only the rectangle of
the footprints that changed (+ the skirt) is recomputed, then its normals and baked sun visibility (+ the
shadow march's reach), and the mesh is re-uploaded. Models stand on that height
(`Terrain::foundationAt`, docs/3d-models.md "Placement"); units on the footprint stand on it too. Before,
building models were placed at the art box's highest terrain point, so on uneven ground (the designed
plateau noise, a building straddling a rock edge on the original-tiles terrain) parts of them hovered. The
2D Enhanced terrain bake (`terrain_bake.cpp`) keeps the natural relief (`enableFoundations(false)`).

## Terrain v2 ("3D terrain: Designed")

Options → **3D terrain: Original tiles / Designed** (3D only, same row as Panel transparency; also in the
in-game options; `settings.ini terrain3d=tiles|designed|faithful`; `--terrain3d tiles|designed|faithful`). The 3D
preset and new players: Faithful ("Terrain v3" below); the struct default (one-frame runs): Designed.
Original tiles is the terrain described above, unchanged (checked pixel-wise). Designed replaces the stretched
32 px tiles with designed terrain in the spirit of OpenCNC3D; gameplay is untouched (shapes follow the cells).
Code: `render3d/terrain_v2.*` (`DesignedTerrain`), shaders `terrain_v2_msl.hpp` + the "designed terrain" part of
`shaders.hlsl` (`terrain2_fs` with `terrain_vs`, `rock_vs` / `rock_fs`).

**Assets** (`assets/terrain/`, all CC0, sources in `assets/terrain/LICENSES.md`): ambientCG 1K-JPG material sets
(albedo, OpenGL normal, displacement, AO) for sand / dunes / rock / cliff / rough / spice / concrete and Poly Haven
rock scans (Namaqualand boulders + cliff). `tools/fetch_terrain_assets.py` downloads the textures (~28 MB, stdlib
only; gitignored); the rock meshes (decimated to ~1400 triangles in Blender by `--rocks` /
`tools/terrain_rocks_blender.py`, ~50 KB each) are checked in. JPEGs are decoded with stb_image
(`third_party/stb`). Missing assets → a log line and the original tiles. Copied into the app bundle / install tree.

**Heightfield** (`Terrain::build(..., designed = true)`): 4 vertices per cell (the original-tiles mode keeps 2),
the same levels plus noise — an uneven plateau top (±1.7 px), curved asymmetric dune ridges (long windward
rise, short lee side, crests ~4.5 cells apart, bent by noise), a gentle swell of open sand, craggy rough rock
(terrain types 3 / 5) and rugged cliff bands. Every impassable rock band is raised to at least the plateau level
(a ridge / plateau rim; its faces are covered by rock meshes) instead of sloping down to the sand. Spice does not
move the ground (it is a material); `refreshSpice` only recomputes the changed region anyway.

**Materials**: per cell from the flags — concrete bit → concrete; sand class (incl. sand-looking impassable cells)
→ dunes (terrain 2) or sand; rock class → rough (terrain 3 / 5, walk-only) or rock; impassable → cliff. Two RGBA8
splat textures, 8 texels per cell: the material is looked up through a noise warp (organic boundaries, ≤0.6 cell),
blurred twice (radius ¼ cell); concrete is neither warped nor blurred: its binary channel, bilinearly filtered,
crosses 0.5 exactly on the cell edges and the shader thresholds it there (a one-pixel ramp), so slabs end in a crisp
border instead of bleeding into the rock; the slabs themselves are drawn by `concreteSlab` (docs/enhanced-graphics.md
"Concrete"). Concrete changes rebuild only the changed cells' part of the splat. Spice: an RG8 texture per cell
(presence = the amount up to thin spice, richness = what lies above it, blurred over two cells; follows
harvesting), read as an organic field (`spiceField`: three-scale noise warp, round kernel, the richness broken
into clumps - docs/enhanced-graphics.md "Spice"), broken into grains, with darker reddish mounds in the cores
of thick spice. Shader per pixel: for the materials with weight > 0 — anti-tiling sampling
(Inigo Quilez's technique 3: a low-frequency noise switches between 8 offset copies of the texture), triplanar on
steep rock / cliff / rough, normal map with a per-material strength, then a **height blend** (each material's
displacement in its albedo alpha: sand fills the cracks of the rock), a large-scale brightness variation, the same
light / shroud / haze / edge fade as the tile shader.

**Colour lock**: each material is tinted so its mean colour equals the mean colour of this map's own tiles of
that class (from the session's tile atlas at run time — nothing derived from the game data is stored): half per
channel, half by luminance (unifies the scans' hues). Spice's difference to the sand is exaggerated (×2.4; its
tiles are sand with specks). Each tileset keeps its own palette and mood.

**Modelled cliffs**: for every impassable cell, one rock per face that drops ≥6 px to a neighbour: an elongated
rock-scan mesh (40–56 px along the face, 24–36 across, long axis along the face ±17°, random variant and 180°
flip) from 5 px below the low side to 1–8 px above the ridge, plus sometimes one on the ridge (always on cells
without a face: mesa tops). Rough rock gets 0–3 small stones per cell. Instanced (one draw per rock type, 6),
rock textures recoloured by luminance to the cliff colour with a fifth of their own hue, per-instance brightness,
contact darkening near the ground, baked sun visibility of the terrain at the rock. Rocks are 1k–5k instances per
map (A5V1 ≈ 1100, a 128×128 skirmish ≈ 5200).

**Cost** (M4 Pro, Debug build, 4PLAY10 128×128 skirmish, `--battle`, zoom 0.45, tilt 50°, 600 frames):
CPU update+render avg 2.74 ms (tiles: 2.80), frames locked to vsync. First use: textures decoded in ~2 s
(Debug; once per run), terrain build ~1.3 s on a 128×128 map in Debug (well under that in Release).

Colour (owner, 2026-11-09): after the lock, sand / dunes are pushed warmer (saturation ×1.25, slight orange
bias), rock ×1.15, rough rock more saturated and darker brown, cliffs ×1.2; rock scans keep a tenth of their own
hue (was a fifth). The same terrain is baked top-down for Graphics: Enhanced (docs/enhanced-graphics.md
"Designed terrain"; the shaders' `fog2.w = 2` = bake mode: no shroud, no map-edge fade).

Not yet: sprites' depth vs rocks uses the heightfield (a unit right behind a tall
rock can be partly hidden by it). Rock shadows: see "Sun shadows" below.

## Terrain v3 ("3D terrain: Faithful" - Designed (faithful))

Option value `faithful` (label "Faithful"; the terrain of the 3D and Enhanced presets since 2026-11-11). The
designed materials laid out after the ORIGINAL tiles: the sand and the buildable rock are the Designed
ones; cliffs, non-buildable rock and concrete follow the original (owner notes 2026-11-10/11).
Code: `render3d/terrain_guide.*` (`TerrainGuide`),
`DesignedTerrain` with `TerrainStyle::Faithful` (`terrain_v2.*`), the fragment shader `terrain3_fs`
(`terrain_v3_msl.hpp`; HLSL twin in `shaders.hlsl`; its source is the head of the Designed shader - structs,
noise, anti-tiled sampling, shadows, spice - plus its own body, so `terrain2_fs` is not touched).

**Why a guide.** The original tiles are pictures of a 3D scene (seen obliquely from the south, lit from the
south-west): where the rock ends, where a cliff stands and how wide its face is, where rock is plain and
where broken, is in their pixels, not in the cell flags. `TerrainGuide::build` reads the map's tiles at run
time - like the colour lock, in memory only, nothing stored or shipped - and makes four fields (RGBA8 texture
with mips, 32 texels per cell, 16 on maps over 96 cells; the CPU copy is freed after the upload):

- **rock** - a tile pixel is sand or rock by its chromaticity between the map's mean sand and mean plain-rock
  colours (3 x 3 blur; 99 % of the plain tiles' pixels come out right); sand counts only when it connects to
  open sand (flood fill), so pale facets inside a cliff stay rock. Cells with the sand flag are all sand,
  build-on / concrete / building cells all plain rock;
- **tone** - (luminance - plain rock's) / plain rock's, thin features a little emphasised;
- **rugged** - local contrast (5 x 5 luminance std against plain rock's own grain) or dark: 0 plain, 1 broken;
- **height** - 0 sand .. 1 a cliff top. All rock stands 4 / 30 above the sand (a step 8 px wide along its
  outline). The rest follows the game's passability (owner, 2026-11-11, checkpoint 4): only a cell no
  vehicle can use - no drive-on bit, the terrain test of `World::canUnitUseSquare` for every wheeled and
  tracked unit - may carry a face, and only where the original paints one:
  - *faces*: rock whose mean tone over 13 x 13 px is 10 .. 24 % darker than plain rock (the original's
    shaded rock and crevices). The share of face in a blocked cell's rock is its *strength* (an eighth: 0,
    a third: 1); infantry-only rock and outcrops enclosed by the plateau have none;
  - *level* per cell: sand, vehicle-passable shores and crumbling edge cells (part sand, strength under
    0.35) 0; every other rock cell is relaxed (Laplace) between them and the strengths - the plateau behind
    an edge cliff is level with it and slopes away gently, a mesa's plain top stands as tall as its rim, a
    cliff tapers off towards a crumbling end;
  - *inside a cell*: passable rock lies at the bilinear level of the passable cells (no relief from a cliff
    beside it reaches in). A blocked cell rises from the sand outline across a face profile - an
    anisotropic distance (Dial), a face being 12 px wide on the south side, 9 px east / west, 8 px north
    (2D bake: 20 / 10 / 5) - to its level, and meets the level of passable rock next to it over 24 px.
  For the 2D bake `heightAt()` (the mesh) is the same relief at half height: seen from above the mesh only
  casts the shadows.

It also measures the map's colours: plain sand, plain rock, and the broken rock's five tone levels (edges at
tone -0.58 / -0.30 / -0.07 / +0.20), keeps the per-cell blocked mask (`cliffCells()`, into the splat's
alpha: the shader's gate for faces, dark tones and the rim) and lists the boulders: every blocked cell that
does not stand tall (level under 0.45) gets a boulder field - 3 - 7 rocks of 9 - 15 px, or 2 - 4 of 6 - 9 px
where infantry can walk - on the original's dark lumps first, then spread over its rock, and around the
middle of a cell whose art is no rock at all (wrecks, bones); a boulder is kept inside the blocked cells (it
does not hang over into a cell vehicles can use). No blocked cell is left looking like open
ground; no cell a vehicle can use gets relief, dark rock or a boulder (audit: README of the review folder).

**Heightfield** (`Terrain::build(..., guide)`): the designed grid and sand relief (dunes, swell); the rock's
height is the guide's x 30 px (`kCliffHeight`). Plateaus lie 4 px above the sand, cliffs and ridges are
real geometry; the plateau behind an edge cliff is level with the rim and slopes away gently.

**Shader** (`terrain3_fs`): sand <-> rock on the guide's outline (a crisp ramp, height-blended: sand lies in
the cracks); plain rock = the Designed rock material; broken rock on level ground = the rough material in
the original's own tone levels (pale crust, dark lumps; on cells vehicles can use the dark specks are held
back); a face - where the relief climbs on a blocked cell - = cliff shading in tone units that index the
original's palette (docs/enhanced-graphics.md "Designed (faithful)": darker than its top, tiers of blocky
columns with gullies and ledge lines, the original's big light and dark, a pale rim, a dark foot), the
crag scan (material layer 7, ambientCG Rock 026) for grain.
The guide is read smoothly upsampled (`guideAt`, owner 2026-11-11: zoomed in, its texels - the original's
pixels - showed as square blocks and stair-steps, a bilinear tap's kinks cut sharp by the tone levels and
the rock outline): while a guide texel covers more than about a pixel, a Catmull-Rom bicubic (5 bilinear
taps, clamped; interpolating, so the original's single-pixel light and dark keep their strength), all reads
through a slight noise warp (< half a texel, ~3 texels across) that bends the original's pixel diagonals
into curves; minified, the plain trilinear tap. 2D bake and 3D alike; at zoom 1 the bake changes by a mean
0.9 levels (A5V1), invisible side by side.
In 3D the faces are real geometry, so the side-dependent part of the shading is left to the light.
Concrete: the original's 32 x 16 slabs, used, the same with and without a building on them. The boulders
are the rock meshes, in the original's shaded-rock colour.

**Light** (lead, 2026-11-11): with the faithful terrain the whole 3D scene - terrain, sun shadow map, models -
is lit from the south-west (238°, 48° up; `DesignedTerrain::faithfulSun3D`, `Renderer3D::sun_`,
`Terrain::setSun` for the baked visibility) like the original tiles and the 2D bake, so south and west
faces are lit and the shadows fall to the north-east. Designed and the original tiles keep the north-west
sun. (Sprite billboards keep their drawn shadows, which point south-east as in the original art.)

**Cost**: guide ~0.25 s (64 x 64) / ~1.1 s (128 x 128) at map start (`terrain_guide.cpp` is compiled
optimised in Debug builds too); one more texture (16 MB with mips for 64 x 64) and ~10 guide reads per
rock pixel (5 taps each while magnified; 3D A5V1 at 2560 x 1600 stays at the 120 Hz frame time). Without the crag texture (older asset folders: run tools/fetch_terrain_assets.py) the cliff
material stands in. A tileset that cannot be classified gets the Designed layout.

State and open items (checkpoint 4, 2026-11-11): docs/enhanced-graphics.md "Designed (faithful)" - cliff polish,
the wasteland tileset's near-black faces, the boulder mark on pale blocked rock, wrecks / bones art. Nothing
3D-specific was tuned since checkpoint 3.

## Sun shadows (shadow map)

Options → **Sun shadows** (3D only, same row as Factory rally points; `settings.ini shadows3d=on|off`;
`--shadows3d on|off`; default on). Works with both terrains. Off = the baked soft heightfield shadows of stage 1.

- **Light**: the existing sun (`Terrain::sunDirection`, NW, 40°). One 4096² depth texture (D32F, else D16),
  orthographic along the sun, **fitted every frame to the visible ground** (`Renderer3D::fitLight`: the view
  corners' rays hit y = 0 and y = highest terrain + 64; depth range = the whole map box, so every caster between
  the sun and a visible point is inside — in an orthographic light view a caster and its shadow share the light
  x / y). Extents rounded up to 64 px and the origin snapped to whole texels (no edge crawl while panning). Zoomed
  in it is a few texels per world px, at the widest zoom on a 128×128 map about one.
- **Depth pass** (own render pass before the scene, no colour target, slope-scaled depth bias, no culling):
  terrain (both styles, `terrain_vs` + `depth_fs`), the designed terrain's rock instances (`rock_vs`), 3D models
  (PS1 / glTF; `ModelHook::drawShadowDepth`, opaque ones only). Sprite billboards do not cast: they keep their
  original drawn shadows (the art's shadow masks as decals).
- **Receivers**: `terrain_fs`, `terrain2_fs`, `rock_fs`, `model_fs`, `sprite_fs` — 3×3 PCF of hardware 2×2
  compares (comparison sampler, `sample_compare` / `SampleCmpLevelZero`; the HLSL is a macro, `SHADOW_PCF`), 1 world
  px of extra bias. The sun's diffuse term keeps 12 % in full shadow (`kShadowKeep`; ambient untouched). Models:
  the PS1 shadow polygons / projected glTF shadows are skipped while the shadow map is on. Billboards (units,
  infantry, buildings, effects in alpha mode) take the shadow of their ground point (+3 px) for the whole card:
  units darken in a cliff's shade; decals per corner.
- **Cost**: one extra depth pass (terrain mesh + ~1-5k rock instances + models). M4 Pro, 4PLAY10 128×128
  skirmish, `--battle`, zoom 0.45, tilt 50°, 600 frames: CPU update+render avg 1.97 ms on / 2.15 ms off (noise),
  frame p50 9.05 ms in every configuration (tiles / designed, shadows on / off) — vsync-bound. 64 MB of GPU memory
  for the shadow texture.

## Sun glare (Modern lighting, low sun)
`Renderer3D::drawSunGlare` (owner 2026-11-13): when the camera looks towards a low sun, a subtle lens glare over
the finished picture (after the tone map / bloom, SDL_Renderer, additive; no shader). The sun never stands in the
view (the camera looks >= 24° below the horizon, the sun >= 20° above), so it is the light from just outside the
frame: a wide faint wash + a warmer core at the screen edge towards the sun (the sun's direction in the camera's
right / up), and five faint ghosts (rings, slightly tinted) on the axis from that edge point through the centre.
Strength = facing^1.6 (the view's horizontal direction against the sun's azimuth) x tilt (smoothstep 14°..60°)
x low sun (smoothstep of `elK` 0.9 → 0.66: dawn / dusk 1, Day and Night 0) x (0.75 + 0.25 bloom / 0.5) x 1.15
with HDR; the colour is the sun's. No option: part of Lighting: Modern; Classic, Day and Night draw nothing.

## Objects: models only, no original sprites (2026-11-13)

Owner: "we should not allow original sprites to go to 3D mode, because they don't work well". So:

- Every unit / building is drawn from 3D models: the OpenD2K 3D art pack's glTF models (`Settings::models3d`
  New, the default and the 3D preset's) or the PS1 disc's (PS1; real 3D models, no infantry). The old choice
  "Sprites" is gone; settings.ini `models3d=sprites` reads as `new` (docs/3d-models.md).
- **The 3D art pack** = an `artpacks/<name>/` folder with `pack.ini`, `models/` and `DATA/`
  (`render::ArtPack::threeDPack(Settings::modelPack)`: the chosen `modelPack` when it is such a pack, else the
  first by name). In 3D the objects' 2D art is that pack whatever the "Art" row says (`Session::syncVisuals`;
  the 3D tiles still follow the "Art" row). A type without a model is a billboard of the pack's **redesigned
  frame**; a frame the pack lacks is not drawn (`Renderer3D::objectSprite`; `D2K_3D_ORIGINAL_FALLBACK=1`
  draws the original for debugging). **Exception - community campaigns' own art** (docs/community-campaigns.md
  §8): a frame a mission's mods folder changed, or a type whose template / art it changed, has no model or
  redesign; it is a billboard of the campaign's own sprite (log: `billboard of the campaign's own sprite`).
  Each such type is logged once: `3D fallback: unit 22 FR NON STEALTH: no
  model, billboard of the redesigned art-pack frame (frame ...)`. After loading the models the session logs
  `3D coverage: N unit / building types without a model, M of them without a redesigned frame`
  (`D2K_3D_COVERAGE=1`: one line per type).
- **No 3D art pack installed** (`Settings::modelPackInstalled()` false, e.g. a public build without the redesign
  pack): Options → Graphics shows the 3D tab with the note "needs the OpenD2K 3D art pack" (`Settings::kNeeds3dPackNote`,
  translated) instead of applying the preset, the Custom page's mode choice offers only Original / Enhanced (the
  "Mode" and "3D" headers carry the note), the original SETTINGS page's preset button skips 3D, and a saved or
  `--graphics 3d` choice runs as Enhanced with the log line `Graphics: 3D needs the OpenD2K 3D art pack ... using
  Enhanced`. The built-in `assets/models` pack (13 procedural models, no frames) does not count; it can still be
  picked with `modelPack=new` while a full 3D art pack supplies the billboards.
- Coverage with `opend2k-redesign` (2026-11-13, D2K_VFX_DEMO allbuildings x 3 houses, walls, exits, units):
  every unit and building type has a model except **FR NON STEALTH** (the Fremen's non-stealth variant: a billboard
  of its redesigned frame); concrete never becomes an object. With PS1 models: the infantry (light infantry,
  trooper, engineer, thumper, Sardaukar, Fremen x 2, saboteur, grenadier, MP Sardaukar), the Fremen barracks and the
  three walls are billboards of redesigned frames.
- Not objects, still from the original art in 3D: **crates** (ground decals; Modern effects draw the spice bloom
  mound), the **craters** (terrain damage decals), the repair wrench icon, and with **Classic effects** the
  explosions and projectiles (14 explosion and 10 projectile types in the weapons gallery; Modern effects - the 3D
  preset - replace them all). Sandworms have no sprite (as in 2D). The classic view key (Tab) still shows the 2D
  1998 battlefield.

## Objects

`Renderer3D::collect` mirrors `MapRenderer::draw` (same frames, same rules, citing the same
original functions):

- **Billboards** (screen-aligned quads): units (body + turret; with Modern effects the eased
  facing picks the frame and the residual angle turns the quad, as the 2D `blitUnit`), infantry,
  flyers / carried units / delivery cargo at their height, bullets at their height, explosions
  (Normal / Half / Additive / Subtract / Brighten / Red- and Green-tint as add + subtract passes
  with channel masks), the repair wrench, and **buildings**: upright, standing on the footprint
  edge nearest the camera with their columns centred on the footprint (at yaw 0 exactly the 2D
  placement relative to the south edge), sorted by the footprint centre. Billboards are sorted
  back to front and write no depth.
- **Billboard depth** (the "fragmented sprites" bug): a whole quad takes ONE depth. Stage 1 used
  the depth of the anchor (the unit's ground point), but the part of the image below the anchor
  covers ground *in front of* the object, which is nearer to the eye — so the terrain hid the
  lower half of every vehicle / infantry / the front of buildings (the image looked cut into
  pieces and the shadow detached). Now `pushBillboard` pulls the quad towards the eye until its
  lower edge is in front of the ground it covers: three eye rays through the bottom corners are
  intersected with the heightfield (`terrainDepthThrough`, a short march + bisection around the
  corner height), at least the flat-ground amount `below * tan(tilt)`, at most 24 px more (so
  real hills / plateaus in front still hide what is behind them). The vertex shader is unchanged
  (the anchor is moved along the eye ray to that depth).
- **Walls and concrete** lie flat on the ground as decals (thin; as standing cards they read as
  panels). Everything else stays an upright card (tried decals with a lean: foreshortened
  buildings and units drawn over buildings standing behind them read worse).
- **Facing relative to the camera**: the frame for unit/turret/building-turret facing
  `(facing - yaw/11.25°) & 31`, i.e. what the camera sees; the ground shadow keeps the world
  facing (only relevant once models unlock the yaw).
- **Decals** (ground-aligned, draped on the terrain or flat at a footprint's height, drawn first in
  2D order): craters, tread marks, building base slabs, the sandworm sand spray, crates, sprite
  shadow masks (multiply, like the 2D shadow blit), flyer / projectile silhouettes, selection rings
  (own green, others red), selected buildings' footprint frames, effect light glows.
- **Overlays** (SDL_Renderer, after the composite): health bars / harvester load at the projected
  positions (Settings::healthBars rules as Enhanced), placement footprint cells lying on the
  terrain, the cheat spawn / reinforcement preview, the factory rally flag + line (projected),
  the Modern vignette / grade (`Effects::drawPost`).

Batches: consecutive quads with the same texture and blend pipeline are one draw call.

### Shroud and the map edge

Same visibility as 2D, and nothing ever shows against the void:

- objects on a fully shrouded cell (units, flyers, explosions, bullets, crates, craters, effects,
  selection rings, health bars) and buildings whose whole footprint is shrouded are **not drawn
  at all** (with their shadows and bases); anything outside the map (arriving carryalls /
  frigates) is hidden as well;
- every remaining pixel takes the shroud darkness of the map point it covers **in the 2D view**
  (billboards carry the 2D top-left of their image per corner; decals the ground under them), so
  partly revealed buildings are cut exactly like by the 2D overlay; fully dark pixels are
  discarded (no black silhouettes); pixels whose 2D point lies outside the map are hidden;
- the clear colour (beyond the map) is black, the terrain fades to black in its outer 8 px, and
  the distance haze is darkened by the shroud too (shrouded ground stays black at any distance).

2D (Enhanced / Original): the map draw is clipped to the map rectangle, so a view larger than a
small map (zoomed out) no longer shows effects / flyers / shadows beyond its right or bottom
edge.

### Modern effects in 3D

`Settings::effects` applies as in 2D: the session advances the same `render::Effects`
(`MapRenderer::updateEffects`, emitter culling with a ground rectangle twice the 2D view) and
`Renderer3D::addEffects` draws its primitives in world space through the bridge in
`effects.hpp` (`particles()`, `treads()`, `lights()`, `particleLook`, `texRect`...):
particles = camera-facing quads at their height above the terrain (alpha / additive / Light =
`dst * (1 + src)` pipelines; sparks stretched along their projected velocity; depth like the
sprites, flat-ground estimate), tread marks = rotated decals, ground marks (scorch, wreck hulks,
shockwave / dust rings, ruins) = terrain quads, screen shake = an offset of the look-at point after
the camera easing, the vignette / warm grade over the composite. With Modern effects the original
explosion / muzzle / projectile sprites are skipped (`Effects::replacesExplosion` / `replacesBullet`).
Classic = none of it. With 3D models, effects that leave a part of a model (the refinery's flue tops)
use the model's anchor points instead of their 2D offsets (docs/3d-models.md "Anchors").

### Dynamic lights

The effects' light list (`Effects::lights()`: explosions and their flashes, muzzle flashes,
projectiles, fires, burning wrecks; docs/enhanced-graphics.md "Light list") becomes real point
lights: `Renderer3D::buildPointLights` keeps the lights on screen and not under the shroud, the
strongest (intensity x reach) first, at most `kMaxPointLights` = 48 (`render3d/lights.hpp`), each
lifted 8 px + 6 % of its reach above its point (so the ground right under it is lit from above),
radius = 0.55 x reach, colour x intensity x 1.35 (matches the 2D glow on flat ground). The block
(`PointLightsU`: count + 48 x position/radius + 48 x colour, 1552 bytes) is fragment uniform
buffer 1 of `terrain_fs`, `terrain2_fs`, `rock_fs`, `model_fs` and `sprite_fs` (MSL `[[buffer(1)]]`,
code in `render3d/lights_msl.hpp`; HLSL `b1 space3` at the top of `shaders.hlsl`): pushed once after
the main pass begins (and by the designed terrain / models from the same block; the Enhanced 2D
terrain bake pushes an empty one). Each light adds `colour * (1 - d²/r²)² * wrapped N·L` to the
sun term (terrain wrap 0.3, models / rocks 0.25, billboards 0.8 with an up normal, so units,
buildings and smoke are lit too) before fog and shroud. Cost: one loop over the active lights per
fragment (typically 5 - 25).

**Time of day / Modern lighting** (docs/enhanced-graphics.md "Lighting"): the block (now 64 + 48 x 48 bytes)
has a head - sun colour, sky colour, billboard tint, specular factor, saturation - that every lit shader applies
(`sunAmb`, `satur`), and per light a spot direction + cone (`spot[]`; w < -1 = point light). The renderer
turns its sun per frame from the style's base sun (`baseSun_`), so the shadow map follows the time of day.
The lamps (`render::SceneLights`: headlight cones as spot lights, tail lights, work lamps, building lamps,
`fx_light*` anchors of models) get the slots the effects leave, at most 16 at ~1 Mpx and 8 at 4 Mpx
(every lit fragment loops over the block: 40+ lights cost ~0.7 ms at 1280 x 800 and dropped a wide night view
below 120 Hz), nearest to the view centre first; their flares are additive billboards. Bloom over the
composite (`render::Bloom`).

### Hook for real models

`render3d::ModelHook` (renderer3d.hpp): `active()`, `hasUnitModel(type)`,
`hasBuildingModel(type)`. `Renderer3D::setModelHook` installs it; objects whose type has a model
are skipped by the sprite path (billboard, shadow, base) and left to the model pass, and
`yawUnlocked()` = `active()`.

## Camera and input

The session keeps its 2D camera (`cam_`: top-left + zoom) as the source of truth for the look-at
point (the view centre) and zoom, so the minimap, `--at`, mission "change viewport" events, saves,
scroll clamping and centring on the base work unchanged. Yaw and tilt are session state too
(`camYaw_`, `camTilt_`). The minimap's view frame is the camera's ground footprint
(`Session::viewFootprint3D`: the visible battlefield's edges through `Camera3D::groundPoint`, on the
flat plane through the look-at point, rays past the horizon stopped at the far plane, clipped to the
map), a trapezoid turned with the yaw; a minimap click puts the clicked point under the visible
centre (`Session::centreViewOn`, yaw / tilt kept). The scroll clamp turns the view's half extents
with the yaw, so every map edge stays reachable at any yaw (yaw 0 = the 2D clamp). `Camera3D` eases towards that pose (~60–90 ms time constants; snapped on
the first frame, loads, restarts and graphics switches). Distance = the 2D zoom's visible height at
a 30° FOV, so zoom levels match the 2D mode at the look-at point.

| Input | 3D action |
|---|---|
| arrows / edge scroll / right drag | pan along the screen axes (turned by the yaw) |
| wheel | zoom towards the terrain point under the cursor (0.3–4) |
| Ctrl+wheel, Page Up / Page Down | tilt (sprites: 20°..50° from vertical; models: 0..66°) |
| Q / E (held), middle drag (x) | rotate — only with 3D models; sprites lock the yaw to north-up |
| middle drag (y) | tilt |
| Home | back to north-up, default tilt (35°) |

**Sprite mode camera limits** (`Renderer3D::constrainPose`, applied to the session's yaw / tilt
every frame): the sprites are pre-rendered from one direction, so turning the camera only
rotated the terrain under cards that kept facing the viewer ("the textures stay in place").
Without models the yaw is fixed at 0 and the tilt kept at 20..50° from vertical (default 35°),
where upright cards still read as the 2D art. A `ModelHook` reporting `active()` unlocks yaw
and the full tilt range.

Every screen → world conversion of the session goes through `Session::screenToWorld`: 2D = the old
`cam_.x + x / zoom`; 3D = `Renderer3D::pick` (ray from the eye through the pixel, marched over the
heightfield + bisection; a unit drawn within its radius of the cursor wins and returns its exact
position). Selection boxes test the units' projected positions against the screen rectangle. So
selection, orders, Ctrl-attack, repair/sell/target modes, building placement, cheat spawning,
rally clicks and the cursor choice all work as in 2D.

Command line (reproducible shots): `--graphics 3d --camera-tilt DEG --camera-yaw DEG` (clamped as
above) with the usual `--map/--at/--zoom/--ticks/--reveal/--screenshot/--frames`.

## Performance (M4 Pro, 120 Hz, 1728x1080 window)

After the depth / shroud / effects pass: 4PLAY1 skirmish, `--battle --ticks 8000`, AI base
close-up at zoom 1.5, Modern effects, 600 frames: 3D CPU update+render avg 0.86 / p95 2.10 ms
(Enhanced 2D 0.54 / 1.31 ms), frames locked to vsync (avg 8.33 ms) in both.

Stage 1 numbers:

`--frames 600` on 4PLAY10 (128x128, 4-player skirmish, `--battle`, revealed):

| Mode / view | CPU update+render avg / p95 | Frame avg / p50 |
|---|---|---|
| Enhanced 2D, zoom 1 | 0.55 / 1.28 ms | 8.41 / 8.33 ms |
| 3D, zoom 1, tilt 40° | 0.44 / 1.44 ms | 8.38 / 8.32 ms |
| 3D, zoom 0.45, tilt 60°, yaw 30° (most of the map) | 0.46 / 1.60 ms | 8.40 / 8.33 ms |
| 3D, after 6000 ticks (AI bases built), zoom 0.4, tilt 55° | 0.86 / 1.32 ms | 8.54 / 8.33 ms |

All locked to the 120 Hz vsync; the only long frame is the first one (texture warm-up + the forced
renderer submit).

## Limitations (stage 1)

- Sprites are the 2D art: billboards do not foreshorten with the tilt (a building's image is a
  little taller than its foreshortened footprint at 50°), and buildings show their one 2D view.
- Overlapping billboards sort per object (no per-pixel depth between sprites); a unit right behind
  a building is hidden by it (as in 2D). Particles are not depth-softened (no depth read-back
  with MSAA); they are depth tested like sprites.
- A few effect trails store their 2D drawn point (y - height) as ground position, so rocket smoke
  sits a few pixels north of the rocket's ground track at high tilts.
- Original shroud edges are not used (always the soft mask).
- Heights are a heuristic from the flags (map makers' impassable decoration on rock becomes
  outcrops; bridges become raised causeways).
- Metal only (MSL); camera yaw/tilt is not saved in save games.

## Next: real 3D models

- **Format**: glTF 2.0 (`.glb`) loaded with **cgltf** (MIT, single header) — meshes, PBR materials,
  skins and animations; Blender exports it, AI image-to-3D tools produce it.
- **Pack layout**: like art packs, `<pack>/models/<unit|building>/<template name>.glb` (+ a
  `models.ini` with scale / pivot / turret node / muzzle points), anything missing falls back to
  the billboard sprite — so models can arrive one by one.
- **House colours**: a mask texture (or a material named `house`) whose luminance is tinted with
  the house's COLOURS.BIN ramp in the shader (the same rule as art packs' `.house.png`).
- **Animation**: rigid node animation is enough for most: body yaw = facing (32 steps → smooth),
  turret node yaw = facingTurret, recoil on `kFired`, harvester unload / refinery / building
  animations as glTF clips driven by the existing counters (`animFrame`, `buildupCounter`,
  `animCounter`); infantry as skinned clips (walk / fire / die by `animType`).
- **Rendering**: instanced draws per model, a real shadow map (directional, PCF) for models and
  terrain (replacing the baked terrain shadows and the sprite shadow decals), depth-written opaque
  passes, then the billboards / effects; selection outlines from the model silhouettes.
- **Done (first models): docs/3d-models.md** — "3D models: New (art pack) / PS1 (disc)" (Sprites removed 2026-11-13):
  the PS1 disc's models decoded at runtime, glTF packs via cgltf, the AI image-to-3D experiment.

# 3D mode (Graphics: 3D), stage 1: 2.5D battlefield

`Settings::graphics = ThreeD` (main menu Options → Graphics: Original / Enhanced / **3D**, or
`--graphics 3d`). It is the Enhanced mode (sidebar, top bar, radar, menus) with the 2D map replaced
by a 3D battlefield whose objects are the **OpenD2K 3D art pack** (see "Objects: models only, no original
sprites" below; without the pack the mode is not offered). The simulation is untouched: the renderer reads the
`World` (positions interpolated by `render/motion.*` as in 2D) and never writes it.

**Presets** (owner decision 2026-11-18, docs/ROADMAP.md): Options → Graphics offers Original / Enhanced /
**3D (Reimagined)** / **3D Classic** / Custom. 3D = Faithful terrain (terrain v3) with the Reimagined look; 3D Classic =
the original 1998 tiles on the heightfield ("3D terrain: Original tiles"), Reimagined off; both use the art pack's models
and sun shadows. The **Designed** terrain (v2) and plain non-Reimagined **Faithful** are legacy modes, still selectable in
Custom but frozen: new visual upgrades target Reimagined (and 3D Classic where trivial), not them. The sections below
describe all terrain modes as they were built.

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
| `cinematic_fx.*`, `cinematic_msl.hpp` | "Reimagined" cinematic particles (see "Reimagined (experimental)") |

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
failed). D3D12 runs an 8-bit scene and SDR output; `D2K_D3D12_HDR=1` retests it. The same holds for Enhanced (2D):
with modern lighting its half-float lighting composite gave a black battlefield in a visible window (a hidden
one-frame window rendered fine), so `Session::hdrIfOn` returns nothing on D3D12 (8-bit lighting).
**The likely common cause (found 2026-11-20 on the Legion, 0.0.3 test):** every draw through the custom fragment shader of
SDL's GPU renderer (`SDL_GPURenderState`: `render/hdr.*` tone map) fails on D3D12 - `CreateGraphicsPipelineState`
returns `E_INVALIDARG` each frame (visible only with `SDL_RENDER_GPU_DEBUG=1`) and the draw is dropped, whatever the
target format. It also hit the Reimagined **Realistic grade** on the 8-bit scene (`gradePass`: a nearly black battlefield
on D3D12, Vulkan fine) and any **display adjustment** (brightness / contrast / gamma / saturation not 0: the whole frame
goes through `Hdr::presentCanvas` - a black screen). Checked and equal to SDL's own `texture_rgba.frag`: the input
signature (COLOR0, TEXCOORD0 at registers 0 / 1, SDL's vertex shader outputs them the same way), the register spaces
of the root signature (t/s space2, b0 space3), and the DXIL signature; the exact reason needs the D3D12 SDK layers
(not installed on the Legion). So `render3d::gpuRenderStateOk()` is false on D3D12 and `Hdr::available()` says no:
the callers draw without it (no Realistic grade, no display adjustments on D3D12). `D2K_D3D12_RENDERSTATE=1` retests
it (a newer SDL). When D3D12 is the requested
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
placed or removed). Models stand on the levelled height (`Terrain::foundationAt`, docs/3d-models.md
"Placement"); units on the footprint stand on it too. Before foundations existed, building models were placed at
the art box's highest terrain point, so on uneven ground parts of them hovered. The 2D Enhanced terrain bake
(`terrain_bake.cpp`) keeps the natural relief (`enableFoundations(false)`). Heights come from `hNat_` (the
heightfield before foundations, kept per vertex because the faithful relief comes from the layout guide that
only exists during `build`).

- **Level** (`setFoundations`): per alive building, its occupied cells (`tilesOccupiedAll`) get one height, the
  `kFoundationLevel` = 0.3 quantile of the natural heights of their vertices. A low quantile sets a building on
  sloping ground into the slope (a cut on the uphill side) rather than onto a fill that stands out over the
  ground below as a mound.
- **Neighbours**: two footprints a gap apart may differ by at most (`kFoundationRamp` + `kFoundationRampOver`) x the
  gap, so their ramps agree. Touching footprints where a cell units drive over (an apron: `tilesOccupiedAll`
  minus `tilesOccupiedSolid`, e.g. the construction yard's bottom row) meets the other may differ by
  `kFoundationTouchStep` = 3 px: a small lip, not a wall. Solid against solid is free (a turret beside a wall
  keeps its own level; nothing drives over that border). Every pair over its limit is pulled together (64
  passes at most), the smaller footprint moving more. One shared height per touching group was tried: a base
  on a slope then sank or rose by the whole relief at its far end (30-55 px cuts).
- **Ramps** (`applyFoundations`): two bounds per vertex, geodesic cones from the footprints: U = min over
  footprints (F + the cheapest path cost to it), L = max (F - that cost), where a path step costs its length x
  the rate of the ground it crosses, max(`kFoundationRamp` = 0.3, the natural slope over +-8 px +
  `kFoundationRampOver` = 0.12). Two multi-source Dijkstra runs (8-neighbourhood) over the rectangle plus
  `kFoundationReach` = 160 px. The natural height is clamped into [L, U]: kept where it already lies inside
  (gentle ground near a building stays as it is), else the bound - a ramp from the footprint's edge down (fill)
  or up (cut) that meets the natural ground. U and L change by at most the local rate per px, so the result is
  never steeper than max(0.3, natural slope + 0.12): on the passable ground of the Reimagined guide (ramps
  <= `TerrainGuide::kReimRamp` 0.35) at most about 0.47. On a cliff face the rate follows the face, so the cone
  climbs (or drops) with it: a cliff beside a building keeps its face and top. Footprint vertices are exactly F
  (where footprints touch: the middle of their heights; likewise where two cones disagree). Paths end at
  `kFoundationReach`; the last 32 px blend back to the natural height. Only the vertices whose height changed
  (+ the shadow march's reach) get new normals and baked sun visibility; the mesh is re-uploaded.
- **Why** (owner, 2026-11-17, 3D Reimagined: "when I build some building it ... could make some hills where the
  units can go up", a refinery south of the construction yard on 2PLAY1). The first version levelled to the mean
  and blended back over a fixed 14 px skirt. Reimagined's relief is 1.6-2.4x taller and its guide lets passable
  ground ramp up to 0.35 towards the cliff tops, so buildable cells can be anything but flat (the refinery's
  site there spans 6..51 px). The mean then sat 15-25 px off the natural ground at the footprint's edges, and the
  14 px skirt made it a terrace wall: 2.77 px per px on drive-on cells, 1141 sample pairs steeper than 0.5 with
  the base built (`--place-all`). Adjacent buildings levelled apart gave a further step on the yard's apron.
- **Measured** (`terrain_scan --refinery` / `--place-all` / `--measure-foundations`: drive-on cells without a
  building within 3 cells of a footprint, Reimagined, the slope between samples 4 px apart that the
  foundations made steeper than the natural ground): 2PLAY1 refinery steepest 2.77 -> 0.53, pairs > 0.5:
  920 -> 19; 2PLAY1 base 2.77 -> 0.53 (1141 -> 19); 2PLAY5 base 2.53 -> 0.62 (1434 -> 206); 2PLAY3 base
  1.53 -> 0.52 (930 -> 7); campaign bases (A4V1, A8V1, H5V1, H7V1, H9V1, O6V1, O8V1, O9V1, A9V2) pairs > 0.5
  down 8-30x (e.g. H5V1 4767 -> 506; the steepest that remain, up to 1.7, are on naturally rugged spots). No
  new hollows (the added pit depth stays <= 6.2 px, was up to 11.3). Building the terrain with the foundations costs a
  little more (A9V2 Reimagined, Debug: +440 ms over the no-foundation build, was +46); one building placed in play
  re-levels its neighbourhood in ~55-70 ms in a Debug build (much less in Release).
- **Not covered**: the faithful shader (`terrain3_fs`) still lights rock with the guide's macro slope, not
  the mesh normal, so a cut or fill shows in the silhouette, the shadows and the units' height, not in the
  rock's shading.

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

## Reimagined (experimental) - first milestone of ROADMAP §4R

Options → Graphics → 3D: **Reimagined look (experimental)** (also in the in-game options next to Classic view;
`settings.ini reimagined=on|off`; `--reimagined on|off`). Default off, not part of any preset; Original, Enhanced
and the plain 3D are unchanged (the 3D picture with it off matched the pre-change build to within 1/255). The
owner's references: the *Defcon Zero* trailers (tall cliffs with long cast shadows over pale ochre sand,
gravel, rubble at the foot of rock, resource fields as scattered granules, big lit smoke / dust, layered
explosions). Two parts:

**Terrain** (needs 3D terrain: Faithful; `Terrain::setReimagined`, `terrain3_fs` with `F3U::reim`):
- *Taller relief*: `Terrain::reliefHeight` - plateaus 1.6x, cliff tops up to 2.4x `kCliffHeight` (~72 px, about
  2.3 cells), the faces near-vertical. The shader's macro normal / slope use the same curve (`macroH`). Gameplay,
  picking and building foundations follow the mesh as before.
- *Lower sun*: `DesignedTerrain::reimaginedSun3D` (232°, 30° up; faithful: 238°, 48°) - the shadow map casts
  ~120 px shadows from the cliffs onto the sand and the units (terrain self-shadowing is the same shadow map).
- *Palette*: one warm low-saturation family - sand pale ochre, rock greyer with its original dark tones lifted
  (faces most), so sand and rock read apart by height, texture and shadow rather than by hue; a large-scale tone
  variation of the open ground.
- *Procedural detail* (no textures, all in the shader, faded by the pixel size): wind ripples on open sand, gravel
  (`pebbles`: domed stones on a jittered lattice with their own normals), rubble along the foot of rock (from the
  guide's blurred rock coverage; only a look - no geometry, nothing on cells vehicles use changes), rock cracks in
  patches, strata bands with ledge lines across cliff faces, cavity occlusion (ground lower than its surroundings:
  crevices, cliff feet), spice as thousands of rust granules (denser and darker where thick) with a faint glow and
  view-dependent sparkles.
- *Light*: shadows tinted towards the sky's blue (not black), drifting **dust haze** veils (2026-11-17; they
  replaced the round-1 cloud shadows - Arrakis has no clouds; "Dust haze" below), a nearer distance haze (fog starts at 1.35x the camera
  distance instead of 1.8x).
- HLSL twin in `shaders.hlsl` (same functions; SPIR-V regenerated).

**Terrain, round 2 (2026-11-16)** - the round-1 cliffs were grey stretched curtains (anything sampled by x / z
smears down a vertical face). Now:
- *Sculpted cliffs* (`Terrain::reimaginedRelief`, `terrain.cpp`): the Reimagined mesh is a 4 px grid (`sub() == 8`).
  On tall cliffs only (the guide reaches >= 0.25 within 8 px) the faithful relief is cut into rock: rounded columns
  7-17 px wide with V-shaped gullies along the outline (the face recedes up to 7 px: the height read that far
  downhill), a coarse irregular warp (4 px), sandstone ledges (the height stepped into 11-16 px strata where the
  face is steep), a talus skirt (the foot keeps 6-14 px of the unsculpted profile). Every change only LOWERS the
  faithful relief: cells vehicles use get no new relief; plateau tops and the low shore steps are untouched.
- *Adaptive index buffer* (`Terrain::buildAdaptiveIndices`): an 8 px quad stays 2 triangles unless it reaches above
  12 px and a 4 px vertex deviates > 0.15 px from its bilinear surface (the cliff faces); a coarse quad next to a
  fine one is a fan around its centre vertex that takes the fine side's middle vertex (no T-junctions, no cracks).
  H5V1: 314k triangles (round 1: 179k). Picking / `height()` read the 4 px grid: identical on fine quads, within a
  fraction of a px on coarse ones. The baked sun march (shadow map off) uses 20 steps of 12 px on this grid.
  The same index buffer also holds a *far range* (every 8 px quad as 2 triangles, `Terrain::drawRange`): the
  renderer draws it (main + shadow pass) while a world px is under 1.5 screen px at the look-at point - the 4 px
  faces would be all cost, no visible detail there (the 8 px quads still sample the sculpted heights; up to a few
  px off the picked surface on cliff faces at that zoom, under a few screen px).
- *Sandstone faces* (`terrain3_fs` `reimCliff` / `sandstone`, MSL + HLSL): every steep mesh face (n.y < 0.78) is
  shaded in WORLD space on the two vertical projections (u along the face, v = height; a single projection on most
  faces): beds ~14 px (ochre, some rust, a few pale buff; wavy), each bed's lit lip and dark undercut, vertical
  fractures through several beds, fluting (rounded 5-10 px columns, in patches), varnish streaks, a darker foot and
  scree / fallen blocks on the talus. The faithful triplanar crag taps are skipped there (`reimFace`).
  `F3U::reim[3].z` switches it.
- *Soft long shadows* (`shadowSoft`): contact hardening from compares only (no raw depth reads, so the same on
  Metal / Vulkan / D3D12): a fast path (the pixel + 4 taps 7 px away agree: open ground or a shadow's core); else
  4 taps x 3 depth thresholds (0 / 40 / 120 px above the receiver) estimate the casters' distance, the penumbra grows
  0.03 px per px of it (0.6 .. 7 px), 8 rotated Poisson taps. `reim[3].x` = world px per shadow texel (from
  `lightVP`), `reim[3].y` = the growth (0 = the plain 3 x 3 PCF).
- *Light and palette*: the Reimagined terrain is lit with less sky and more sun (ambient 0.42, diffuse 0.98, still
  normalised so sunlit level ground keeps its brightness): cast shadows ~0.5 of the lit sand. Shadows get a
  stronger sky blue (`reim[2].z` 1.6), now also on faces turned from the sun. Sand a warm golden ochre, rock a
  rust-brown, spice deeper orange-red, darker where thick, mottled clumps + grit at every zoom (it went flat at
  mid zoom). Reimagined off: unchanged (screenshots identical apart from the run-to-run building animations).

**Passable ground (2026-11-16)** - owner: an MCV "dropped into a pit" on A9V2 (a 1-cell sand tongue reaching into
a rock plateau). Root cause, in the guide (not the sculpting): its height channel was not continuous on the cells
ground units use. The tiles' sand pixels (classified by colour, connected to the open sand) lie at 0 even where they
reach into passable rock cells, while the passable plateau around them is relaxed up to the level of its tall cliffs
(g 0.5..0.9). At the save's spot: g 0.61 -> 0.14 -> 0.01 -> 0.16 -> 0.49 across 32 px, all on drive-on cells - plain
faithful 18 -> 4 -> 0 -> 5 -> 14 px (a notch the eye hardly reads), Reimagined (1.6..2.4x) 38 -> 7 -> 0 -> 8 -> 28 px:
a 30-40 px hole 16-24 px wide. Every map has them (plateau sand pockets, tongues, shore cells below raised rock).
Fix (Reimagined only; `TerrainGuide::Params::reimagined`, `DesignedTerrain::prepareGuide(.., reimagined)`,
terrain_guide.cpp `encodeReimagined`): the guide's height channel is built FOR the Reimagined curve
(`Terrain::reliefHeight(g, true)` / the shader's `macroH` read it unchanged, so the mesh, picking, unit heights and the
shader's slopes agree):
- On the cells ground units use (every cell but the guide's cliff cells; plateau outcrops count as ground) the level
  is the passable cells' bilinear level with the sand cells at 0 - also on the tiles' sand pixels there (pale sand
  inside raised rock is a sand patch on the plateau, not a hole). Sand pixels inside cliff cells meet the passable
  neighbours' level like the rock beside them.
- Target height R = the Reimagined curve of that relief; on those cells it is replaced by its lower envelope L rising
  `TerrainGuide::kReimRamp` = 0.35 px per px (~19 degrees) along passable paths (chamfer passes): plateaus keep
  their full tall height where they can ramp down to their shores gently, and two passable points differ by at most
  0.35 x the passable path between them - no walls, no holes. What that lowers fades into the cliff cells beside it
  over 24 px (taken from the nearest passable pixel), so a cliff meets the ground and rises to its full height behind.
- Cliff cells keep the full Reimagined relief (and the round-2 sculpting). Stored as the curve's inverse (8 bits).
Measured with `terrain_scan` (tests/terrain_scan.cpp, all 85 campaign + skirmish maps, 4 px samples on drive-on cells;
slope = rise per px between neighbouring passable samples, pit = how far a sample lies below the highest point within
32 px in every one of 8 directions, cell pit = a cell centre below all its drive-on neighbours' centres):

| relief | max slope | samples > 1 (walls) | max pit | pit samples > 8 px | max cell pit | cells > 8 px |
|---|---|---|---|---|---|---|
| plain faithful (unchanged) | 3.74 | 27367 | 21.0 | 553 | 23.0 | 137 |
| Reimagined before | 17.18 | 133155 | 57.0 | 2754 | 51.9 | 214 |
| Reimagined after | 0.49 | 0 | 12.9 | 3635 | 11.2 | 288 |

The remaining "pits" are gentle bowls, no holes: a shore cell at the sand's level with the taller plateau ramping up
around it at 0.35 (at most ~0.35 x 32 px; more of them than in the plain relief, whose plateaus are lower). A
smaller kReimRamp makes them shallower (0.25: ~8 px) at the cost of lower plateaus near their shores. The guide takes
~0.6 s longer on a 128 x 128 map (Debug).
Plain faithful and Reimagined off are unchanged (the guide is built exactly as before).

**Ground clutter (2026-11-16)** - the biggest remaining gap to the *Defcon Zero* references (their ground is full of
small 3D stones, scrub and debris, each casting a little shadow). Reimagined only (`clutterOn_` in `Renderer3D::render`:
designed + faithful terrain with `Terrain::reimagined()`); Options → Graphics → 3D: **Ground detail** Off / Sparse /
Dense (settings.ini `groundDetail=off|sparse|dense`, `--ground-detail`; default Dense; Sparse = half the pieces). Code:
`render3d/ground_clutter.*` (`GroundClutter`), shaders `clutter_vs` / `clutter_fs` (`render3d/clutter_msl.hpp` + HLSL
twin in `shaders.hlsl`, SPIR-V regenerated).
- *Assets*: the ground clutter pack `artpacks/ground-clutter/clutter/` (gitignored; `clutter.ini` manifest, 30 piece
  types x 2 LODs as glb, 11 texture layers 512 px) made by `tools/models/clutter_kit.py` (Blender, CPU, deterministic)
  from Poly Haven CC0 scans fetched by `tools/models/clutter_fetch.py` (Namaqualand Stones 01 / Rocks 01 / Boulders 01,
  Rock 07, Rock 09, Stone 01, Dead Quiver Branch 01 / 02, Dry Branches Medium 01 - URLs in the pack's LICENSES.md)
  plus procedural pieces (dry shrubs with dead foliage clumps, straw tufts, a torn metal sheet, the rib cage of a
  long-dead machine) and two procedural textures (bark / dry foliage, rust with sun-bleached paint). Scans decimated
  (LOD 0 / LOD 1 triangles: pebble 80 / 16, slab 100 / 24, gravel patch ~370 / 70, boulder 500 / 64, rubble pile ~700 /
  110, shrub 700 / 130, dead wood ~400 / 70, scrap 136-400 / 50-90), normalised (1 unit = the piece's width, ground at y = 0), albedo = diffuse x the
  scan's AO. Without the pack nothing is drawn (logged once).
- *Placement* (`GroundClutter::place`, once per map / terrain build, ~2 ms): hashes of the cell coordinates (same every
  load), the cell flags and the terrain mesh's heights. **Cells no unit uses** (neither drive-on nor walk-on): along
  every side that meets ground units use, 3 slots of boulders / talus rubble / a dry shrub leaning against the foot of
  the face, inside the cell; then 6 candidates per cell weighted by "foot" (relief rising behind the point within
  18-36 px, gentle slope) and "ledge" (flat, little above it): boulders, rubble, shrubs, dead wood, rarely scrap (~1 %);
  nothing on near-vertical faces (slope > 1.3). A big piece is placed only where every cell its circle touches is
  unused by units and not excluded (shrunk twice to fit, else it becomes gravel / a pebble). **Cells units use**:
  only pebbles, gravel patches, flat slabs and tufts, squashed to stay under `kPassableMaxHeight` = 3 px - dense next
  to rock that units cannot use (distance 1-2 cells), slabs and pebbles on plateau rock, sparse pebbles / tufts on open
  sand, fewer on dunes. Every piece is sunk so no part of its base floats (the base plane, sheared by the class's
  share of the ground slope, under the ground at 8 footprint points; at most 3/4 of its height). The load logs the
  count per class and the check "tallest piece over cells units use" (H5V1: under 3 px).
- *Never on* buildings, walls, concrete, building footprints (`Terrain::foundationAt`) or spice: excluded at placement
  and re-checked every frame (`hidden_`, the piece's centre + 4 points of its circle) - a building placed later or
  spice that grows hides the clutter under it. Foundations re-level the ground: the pieces follow
  (`refreshHeights`). Shrouded like the terrain (fragments under full shroud discarded, the edge fade, the haze).
  Not pickable: picking reads the terrain mesh only; the simulation never sees it.
- *Drawing*: welded indexed meshes, one instanced draw per piece type per pass; instances sorted into 8 x 8-cell
  chunks, frustum-culled per chunk (box with the chunk's height range) for the camera and the sun's view, uploaded per
  frame (64 bytes each). LOD 1 below 2 screen px per world px; below 0.9 no pebbles / tufts, below 0.75 no gravel /
  slabs; below 1.6 the small flat pieces cast no shadow. The pieces cast into the sun shadow map (`clutter_vs` +
  `depth_fs`, always LOD 1: the depth bias covers the difference).
  `clutter_fs`: rock layers recoloured by luminance to the map's Reimagined rock (from the colour lock,
  `DesignedTerrain::materialColour`) a little darker (desert varnish), some of the scan's hue, sand dust on the top
  faces; wood a bleached grey-brown; plants and rust as authored; lit like the rocks with the Reimagined light (sky
  0.42, sun 0.98), contact darkening at the base, the sky's blue in shadow; two-sided (blades, sheets).
  `D2K_CLUTTER_STATS=1` logs the zoom, LOD and drawn / casting counts; `D2K_CLUTTER_PACK=<dir>` picks another pack
  folder (a missing one = no clutter: the A/B switch used for the numbers below).
- *Cost* (M4 Pro, Release, 2560x1440, Quality High = MSAA 4x + HDR, 3D resolution 100 %, vsync off; other agents'
  GPU jobs ran on the machine, so the cost is measured inside one run: `D2K_GPU_TIMING=1 D2K_CLUTTER_AB=1` switches
  the clutter on / off in alternate 30-frame blocks and logs the 3D GPU pass per state, 2280 frames): H5V1 zoom 1
  (normal, cliff-heavy; 3763 pieces drawn, 215k triangles + 204k in the shadow pass) +0.66 ms; A1V1 base zoom 2
  +0.21 ms; H5V1 zoom 0.6 (zoomed out) +0.79 ms; `D2K_VFX_DEMO=bigfight` zoom 2.5 +0.62 ms. Whole frames against
  dev (same scenes, back to back, noisy): H5V1 zoom 1 16.0 -> 15.7 ms, zoom 0.6 14.1 -> 14.9 ms, big fight 24.0 ->
  23.9 ms. The first version (1200 / 300-triangle boulders, triangle soup, the shadow pass at the scene's LOD) cost
  1.1-1.4 ms: the cost is the many small triangles, not the pixels (no shadow sampling: -0.1 ms; half the pieces:
  -0.3 ms), hence the welded indexed meshes, lighter LODs, LOD 1 in the shadow pass and no gravel / slabs below 0.75.
  Placement ~2 ms per map, the per-frame cull + upload well under 1 ms CPU.
- Evidence: ~/Desktop/d2k-compare/ground-clutter/README.md.

**Cinematic particles** (needs Modern effects; `render3d/cinematic_fx.*`, shaders `fx_vs` / `fx_fs` in
`cinematic_msl.hpp` + `shaders.hlsl`): a render-only GPU-instanced particle system on top of the Modern effects
(second round 2026-11-15: everything procedural, weapons, lights). Inputs: `render::Effects::recordEvents` (set by
the session while the option is on) makes the effects log this frame's events - explosions (by size), impacts,
vehicle and building deaths, and for weapons `Muzzle` (a shot leaves the barrel: point + aim), `Tracer` (a
machine-gun round, start -> end) and `Projectile` (a shell / rocket in flight this frame, keyed per bullet;
`FxEvent::weapon` = `Effects::WeaponClass`); `Effects::smokeSources` lists burning buildings' fire points.
- *Look, all procedural* from one tileable 3D noise texture (64^3 RGBA8: Perlin-Worley value + its gradient, 1 MB,
  built on a worker thread in ~0.5 s; particles appear once it is ready). Puffs are a sphere eroded by animated
  noise (the edge eats in as they age), with a normal from the sphere + the noise gradient, lit by the sun (wrapped,
  self-shadowed by the density towards the sun, weaker where the cloud is thin so rims stay light), the sky (darker
  underside), light let through thin parts, and fires nearby from below; they fade softly into the ground.
- *Explosions*: a white-hot flash, a hot core plus fireball puffs with a temperature ramp in HDR (white-yellow ->
  orange -> dark red -> soot; the dense middle stays hot longest, the hot part breaks up and shrinks as it cools),
  sparks streaking with gravity (a third land as glowing embers), lit debris chunks (big blasts: some trail smoke),
  a shock dust ring rolling out on the ground, a scorch mark (burnt, ragged, embers while fresh; fades over ~30 s),
  a smoke column. Each fireball adds a short point light to the 3D light grid (`CinematicFx::lights`, candidates
  `src == 2` in `Renderer3D::buildPointLights`; radius 34-60 px by size, 90-140 px for a building).
- *No double blasts*: the Modern effects tag the sprites of a weapon blast (`Effects::kFxBlast`, inherited by its
  trail / landing puffs) and their fireball lights (`Effects::Light::blast`); while the cinematic particles run the
  3D renderer skips both (they draw and light the blast themselves). 2D never looks at the tags.
- *Building death*: several blasts over ~1.3 s around the footprint after the first big one, a long burning column.
- *Weapons*: HDR muzzle stars pointing along the shot, machine-gun tracers, glowing shell streaks, rocket smoke
  trails with a motor glow.
- *Smoke*: burning wrecks and ruins send up bigger, longer columns; impacts throw dust on sand and sparks on armour;
  ground vehicles leave dust trails (thin on rock, none on concrete). Drop-in landings (building placement) feed the
  dust ring; with Reimagined on, the Modern effects' own landing dust is switched off (`Effects::setDropInDust`).
- *Drawing*: one draw call; camera-facing quads from `vertex_id` (no vertex buffer; one instance buffer of 128 bytes
  per particle), sparks stretched along their on-screen velocity, ground marks lying on the terrain (corner heights
  per instance); sorted back to front (ground marks first), premultiplied alpha (light-only kinds write alpha 0 =
  additive). Budget 7000 particles (spawns thin out as it fills, the nearest-to-dying is replaced); its own xorshift
  RNG; time stops when the game ticks stop.

**Models (2026-11-16, first Reimagined models)**: with Reimagined on, the Reimagined model pack
(`artpacks/models-reimagined`) overlays the 3D models: so far the Atreides combat tank, construction yard, refinery
and wind trap, with PBR maps (ORM + normal map: rounded edges, panel lines, rivets, wear; the house colour as a
mask) drawn by `model_pbr_fs`, and simplified versions for small views. docs/3d-models.md "Reimagined models".

**Third round (2026-11-16, "effects v3")** - scale, smoke, cost, ground:
- *Scale per explosion*: everything of a blast is sized from one fireball radius `CinematicFx::fireballRadius(k)` =
  24 px x k^0.78 (k = `FxEvent::scale`, now clamped 0.3..8 instead of 0.6..4): ~13 px for the small bursts of a
  building's death chain, 24 for a tank shell / rocket (a fireball ~50 px across - a tank is ~32), ~42 for a vehicle
  death or a howitzer shell, ~52 for a building, ~85 for the Devastator, ~112 for the Death Hand. Fewer, bigger
  puffs than round 2 (a tank-shell blast: ~23 puffs instead of ~33); the hot phase lasts longer (0.8-1.2 s) and
  cools to black soot. Building deaths: the follow-up blasts are bigger with it; the glow 130-220 px.
- *Smoke*: puffs carry a soot amount (`Part::soot`, instance aux.w): column smoke and cooled fireballs are dense
  dark brown-black (albedo 0.13 -> 0.4 as it thins), darkest at the base (shader: x0.6 near the ground -> x1 at
  ~80 px up), stronger contrast (a steeper sun term, darker underside, less light through thin parts), firelight
  caught in the base. Columns linger 9-16 s and lean further downwind the higher they rise (wind 14 px/s,
  x0.35 at the ground .. x2.4 high up). Burning wrecks / ruins (`addBurner`: one column per spot - a wreck hit
  again is stoked, not doubled) add a low flickering fire light (lights the ground and the base of the column).
- *Ground*: scorch marks are craters now - a burnt bowl with an ejecta rim in the ground's dust colour (carried in
  the instance velocity), shaded from a radial normal - 2.3-2.8 R across, ~1 minute, embers while fresh; a new mark
  near a fresh one renews / widens it instead of stacking (`scorchMark`). The shock dust ring is for big hits only
  (k >= 1.4), 5 R wide.
- *Half-resolution pass*: puffs and big glows (90+ % of the particle pixels) are drawn at half resolution in their
  own pass after the scene pass: a half-size depth target is filled with the scene's terrain (`terrainDepthPipe_`),
  rocks and models (their depth-only shadow pipelines from the camera; models only round the smoke: `ModelFrame::
  depthCull`, the half set's bounds + 96 px), the puffs are drawn on a half-size RGBA16F target (depth tested), then
  `fxc_vs` / `fxc_fs` blend it (premultiplied, bilinear) over the resolved scene before bloom / tone map. Ground
  marks, sparks, debris, tracers and muzzle stars stay in the MSAA scene pass (sharp, under the units).
  `D2K_FX_HALF=0` draws everything in the scene pass (the comparison); `D2K_FX_STATS=1` logs particles and covered
  pixels per set. Without the depth pipeline / targets it falls back to the scene pass.
- `D2K_VFX_DEMO=tankfight` (sim_test.cpp, with `--vfx-demo --at 40 40` on H5V1): two tank groups 7 tiles apart
  fighting, a wind trap destroyed at tick 200 - the scale / video check.

**Artillery hits and the smoke flicker (2026-11-16)**:
- *Artillery* (`FxEvent::Artillery`, `CinematicFx::artillery`; the Modern effects' `Effects::spawnArtillery` for
  Enhanced 2D and the plain 3D): EX_155MMGUN (the siege tank's howitzer; the grenadier's grenade at 0.6 size -
  a hit where a grenade in flight comes down, `Effects::grenadeTargets_`; the crate trap) is no longer a fireball
  (it was ExClass::Big 0.85). On the ground: a white flash for a few frames + a 0.2 s light, a fountain of sand
  and earth thrown up steeply (puffs 90..260 px/s up with drag and gravity: the column tops out ~30-110 px, the
  low puffs spread wide), a grit spray and clods of earth falling back, rolling ground dust, a shock ring
  (4.5 R), a brown-grey dust / smoke cloud drifting with the wind for 4-6 s and the crater (a 0.35 s glow, no
  embers: `scorchMark(..., embers)`). On armour: the flash, sparks and a small burst of dark smoke. Its Modern
  sprites are tagged `kFxBlast` (the cinematic version replaces them). Rockets, deaths and bombs keep their
  fireballs; tank shells (EX_TANKGUN) stay heavy impacts (`ExClass::Shell`). `D2K_VFX_DEMO=artillery`
  (sim_test.cpp): howitzer / tank-shell / rocket rows, two grenadiers, a howitzer shell onto a parked tank.
- *Smoke flicker* - root cause: the draw order of overlapping puffs changed from frame to frame. (1) The
  cinematic sort key was one float, depth + 1e9 / -1e8 per set: at 1e8 a float's step is 8 px (128 px at 1e9),
  so puffs within 8 px of depth tied, and `std::sort` (unstable) over a list reshuffled every frame by
  swap-removal put tied puffs in a different order each frame - a pale dust puff over / under a dark column,
  alternating. Now `CinematicFx::Order` = {set, depth, spawn id} with the spawn id as the tie-break (stable).
  (2) The Modern effects (Enhanced 2D and plain 3D) draw their particle list as it is (3D breaks depth ties by
  it): dead particles were swap-removed (the newest puff jumped under older ones) and a spawn at the 9000 budget
  replaced a random live particle; now the list is compacted in order and a spawn at the budget is dropped.
  Measured on fixed-step recordings (60 fps, 1920x1080), mean |L(t+1) - 2 L(t) + L(t-1)| over the smoke region:
  Reimagined tank fight 1.89 -> 0.81 (pixels over 12/255: 3.1 % -> 1.1 %), bombardment 1.78 -> 0.93
  (2.9 % -> 1.2 %), plain 3D tank fight 0.32 -> 0.28; the rest is real motion (muzzle flashes, explosions). The
  particle budgets were never reached in these scenes (~1000 cinematic particles of 7000), and the
  half-resolution pass was not involved. Cost unchanged (frame time pinned at the 120 Hz display in both builds;
  3D GPU pass tank fight 4.48 -> 4.50 ms, bombardment 2.41 -> 2.55 ms, within the run-to-run noise).
  Evidence: ~/Desktop/d2k-compare/artillery-and-smoke-flicker/README.md.

**Effects intensity (2026-11-16)** - "with a lot of tanks it's just smoke and explosions": the cinematic particles
follow Options → Graphics → Effects intensity (docs/enhanced-graphics.md "Effects intensity"; `render/fx_budget.hpp`).
`CinematicFx::update` copies the Modern effects' busy level; `add()` budgets every event particle (`budgetSpawn`):
puffs capped in time (Balanced 3 s calm .. 1.2 s busy - a blast's own 9-16 s column now clears within ~3 s; the
look before the cap is unchanged), smaller / thinner / fewer / sandier as the view gets busy, sparks and debris
thinned; lasting columns (`addBurner`, building fire points; `lasting_`) merge within 48 px, at most a few per
screen (the oldest die down over 4 s), rate / life x(1 - 0.35 busy). `seeThrough()` fades puffs in front of units
(`unitPins()`, filled by `Renderer3D` from the world before `prepare`; the player's selected units / buildings
more), applied with the cap fade to the instance alpha (no shader change). Cinematic = no budget (light
see-through only). Bigfight (32 tanks a side, `D2K_VFX_DEMO=bigfight`, zoom 2.5), share of the view changed > 48/255
by the effects / unit pixels kept within 40/255: Cinematic 16.8 % / 70.0 %, Balanced 6.8 % / 86.5 %, Clean
5.1 % / 88.9 %; bombardment 24.8 / 10.1 / 6.6 % covered. Evidence: ~/Desktop/d2k-compare/effects-readability/README.md.

**Lit smoke (2026-11-18)** - "the smoke still reads as dark, unlit, round balls" (docs/reimagined-audit.md gap 5):
- *The unlit part was not ours.* Half of the dark smoke in a Reimagined scene came from the Modern effects' own
  sprites (drawn as plain billboards by `Renderer3D::addEffects`, no lighting): the columns of burning buildings
  (render/effects_fire.cpp), the damage smoke of vehicles and buildings (EX_SMOKE3, `Effects::spawnDamageSmoke`), the
  smoke of small fires and of a dead building's ruin (render/effects.cpp). With `recordEvents_` (Reimagined) these
  sprites are now tagged `kFxBlast` (the renderer skips them while the cinematic particles run) and the cinematic
  particles draw that smoke: building fire points and ruins as before (`smokeSources`, burners; the fire points'
  rate x1.45, they carry the whole column now), damage smoke and small fires from a new event `FxEvent::Smoke`
  (one puff per original smoke event; `armour` = a building's smoke point). Tags and events only: the Modern
  effects' RNG stream and every other presentation are untouched.
- *One plume lighting for every puff* (`fx_fs`, the ground dust's sun-lit mode generalised; `soot` = how much light
  the puff swallows): wrapped sun diffuse on a normal from the puff's shape + the noise gradient, self-shadowed by
  the density towards the sun (one extra noise sample, as before; soot shadows itself more), sky light from above
  (cooler), the sunlit ground's warm bounce on the underside, firelight from below (the effect lights, as before:
  at dusk and at night that is most of what the smoke shows); thin edges scatter the sun - a little on the sun side,
  a bright rim when the sun stands behind the smoke (`pow(-dot(sun, toEye), 3)`); soot stands in its own shade at
  its base (the first 80 px) and in its dense core. No term is ever 0 in daylight: the darkest fresh soot comes out
  a dark brown, not black.
- *Shape*: the outline is no longer the quad's disc - the noise's value and gradient push the puff's own
  coordinates about before the sphere falloff (`qw`; more as the puff ages), then the erosion eats in; the noise
  amplitude grows with age (wisps), and the noise scale differs per puff (x0.86..1.16 from the seed) so no two puffs
  carry the same grain. Same three noise samples per pixel as before.
- *Colour by kind and age* (`CinematicFx::sootColour`, `smokePuff(..., Smoke kind)`; albedo before the lighting):
  fresh soot (fireballs, burners, falling flyers) (0.18, 0.14, 0.105) -> warm grey (0.54, 0.5, 0.45), three quarters
  of the way by half its life (`1 - (1 - t)^2`); a burning building's column dense brown-grey (0.27, 0.205, 0.15) ->
  (0.5, 0.45, 0.39), alpha 0.7; a hulk's column lighter and thinner (darkness 0.45, alpha 0.5); damage smoke a thin
  grey wisp (darkness 0.25-0.65 with the damage, alpha 0.34, 1.6-2.4 s). Smoke born at the ground takes up the
  ground's dust (`mixGround`: `dustTint` there x0.8, a share of 0.25-0.3 fading out by 60 px up): a column's foot is
  brown dust, its top smoke. The dust of blasts, impacts, artillery and crashes is `dustTint` of the ground it hits
  (spice orange, sand, rock grit) instead of the fixed sand / rock pair.
- *Not changed*: motion (buoyant rise with drag, growth easing out over the life, the wind lean - calm at the ground,
  x2.4 high up), the Effects intensity budget (caps, thinning, see-through over units; damage smoke follows the
  column rate), the half-resolution pass, fireball heat ramp, sparks, debris, ground marks.
- Checks: `D2K_VFX_DEMO=fire|tankfight|tankhit|wrecks|crash|bigfight` with `--effects-intensity cinematic|balanced`
  and `--time-of-day dusk|night`.
- Cost (this Mac, Debug build, 1280 x 800, 3D resolution 100 %, vsync off, `D2K_GPU_TIMING=1`, the old and the new
  binary run alternately; other jobs were running, +-0.4 ms from run to run): `D2K_VFX_DEMO=fire` (8 burning
  buildings) 3D GPU pass 4.3-5.0 ms -> 3.8-5.0 ms, frame 8.6-8.8 -> 8.6-8.9 ms; `bigfight` zoom 2.5 3D GPU pass
  7.4-8.4 -> 7.8-8.3 ms, frame 15.5-17.5 -> 15.6-17.5 ms. The shader samples the noise three times a pixel as before;
  the smoke the sprites drew is now half-resolution puffs (fire: 1.6 -> 2.3 Mpx of half-resolution quads).
- Other presentations: Original, Enhanced and 3D without Reimagined render the same frames as before (the map area
  bit for bit; the fire demo's "RADAR OFFLINE" panel differs from run to run with one binary).

**Cost** (M4 Pro, Debug build, 2560x1440, MSAA 4x, HDR, vsync off, `D2K_GPU_TIMING=1`), first milestone: A1V1 base
3D GPU pass 3.49 → 4.55 ms; H5V1 `--vfx-demo` bombardment 6.34 → 9.06 ms. Second round, H5V1 `--vfx-demo` at zoom
2: 3D GPU pass ~10-11 ms → ~11-12 ms, frame 14.3 → 15.6-16.0 ms avg (~63 fps; p95 18.5 ms); `--battle` 14.9 ms avg
(67 fps). Reimagined off: 10.2 ms. The SPIR-V (Vulkan) build renders the same picture through MoltenVK.
Third round (Release build, 2560x1440, 3D resolution fixed 100 %, rest as above; round 2 and round 3 run alternately,
`--smoke --frames 1800`): H5V1 `--vfx-demo` bombardment at zoom 2: frame 10.36 → 9.47 ms avg (97 → 106 fps), p95
12.4 → 10.6 ms (81 → 94 fps); `D2K_VFX_DEMO=tankfight` at zoom 2.5: 9.14 → 8.70 ms (109 → 115 fps), p95 11.6 → 9.9 ms;
3D GPU pass (bombardment) ~11.2 → ~10.0 ms. The half-resolution pass alone (`D2K_FX_HALF=0` vs on) saves ~1 ms; its
depth of only the models round the smoke is within the noise (all models on screen cost ~0.8 ms). Plain Modern effects without the
particles: 8.4 ms GPU. (Measure with `--resolution3d 100`: Auto lowers the 3D resolution of GPU-bound runs.)

Open (next steps): real cliff meshes (overhangs and free-standing columns need more than a heightfield; the
sandstone is still a pattern on a smooth sculpted sheet up close),
volumetric lighting of the smoke from the shadow map, smoke casting shadows,
crater geometry (the craters are shaded decals), more clutter variety (scrub variants, debris by map theme), wind per map, a dedicated colour grade per
map; smoke is lit per puff (no light marching through the column: a puff does not shade the one behind it), a
column seen close is still a string of overlapping billows (no flipbook / volume), a depth-aware upsample of
the half-resolution pass (bilinear now: a 1-px soft halo where smoke meets a model's silhouette).
Comparison shots and an honest assessment: ~/Desktop/d2k-compare/reimagined-v1/README.md (first milestone),
~/Desktop/d2k-compare/reimagined-effects-v2/README.md (effects, second round),
~/Desktop/d2k-compare/reimagined-effects-v3/README.md (effects, third round),
~/Desktop/d2k-compare/reimagined-terrain-v2/README.md (terrain, second round).

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

### Flyers (flight ground, 2026-11-17)

**2026-11-18:** cruising flyers keep **one map-wide height** (`flightCeiling`: the 98th percentile of the
terrain heights, cached per terrain) instead of riding up and down over every hill; the glide slope below only
matters over rarer, higher spikes, and landing / lift-off still blend to the plain ground. Their **facing eases**
(tau 90 ms, the short way round; `ModelRenderer::easeInfantryYaw`, flyer entries) instead of snapping one of 32
slots per tick; a carryall and its cargo turn together.

The sim flies carryalls, ornithopters, frigates (and the Death Hand) at the original's 2D altitude
(`posZ` low byte: 60 px cruising, 10 px when it lands on / lifts off a drop or pickup cell; a
carried unit hangs 10 px under its carryall). Measured from the terrain under the flyer's centre, a big
model (a carryall with a harvester under it) sank into a cliff next to it (Reimagined cliffs reach
~72 px, higher than the flight path) and jumped by a cliff's height at every cliff edge. Render-only
fix (`render3d/flight.hpp`, the sim is untouched): the altitude is measured from a **flight ground** -
the highest terrain over a world-fixed 16 px lattice around the flyer, where a sample farther than
36 px (a carryall + cargo's half size and a margin) counts 0.8 px lower per px (a glide slope: the
flyer climbs ahead of a cliff and comes down after it). Fixed lattice + the glide term + samples that
only enter beyond 36 + 100 / 0.8 px (where they cannot yet be the maximum) make it continuous as the
flyer moves: no pops. Between 50 px and 10 px of altitude it blends (smoothstep) to the plain ground
under the centre, so a delivery still touches down exactly on its cell. The same height
(`flyerGround` / `flyerLift`, a carried unit uses its carryall's altitude) is used by the model
(`ModelRenderer::unitPlacement`: body, delivery cargo, picking `rayUnit`, `unitAnchors` for muzzle
flashes), the sprite billboards (`Renderer3D::addFlyers`: body + cargo), the screen-radius picking
and the health bars; the projected shadow and the sprite silhouette stay on the real ground. Both
terrain styles use it (the plain faithful relief only lifts flyers a little); the 2D presets are
unaffected. Check: `D2K_VFX_DEMO=carryall D2K_VFX_DEMO_SITE=15,13 D2K_VFX_DEMO_EDGE=2 ./build/d2k
--map A1V1.MAP --graphics 3d --reimagined on --reveal --zoom 2 --at 15 17 --camera-tilt 60
--camera-yaw 90 --vfx-demo --record DIR --frames 800 --record-fps 30` (a carryall reinforcement of a
harvester flying up the rock spine from the south edge; `D2K_VFX_DEMO_UNIT=name` another unit).

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
  Reimagined with **World beyond the map** on (below): no edge fade, and the land beyond the map never
  depends on exploration: it sits at one fixed, dim fog level (`shroudAt`, below), with a soft blend
  over ~3 cells from the map edge's own shroud. Objects stay hidden outside the map as before.

2D (Enhanced / Original): the map draw is clipped to the map rectangle, so a view larger than a
small map (zoomed out) no longer shows effects / flyers / shadows beyond its right or bottom
edge.

### World beyond the map (Reimagined, 2026-11-18)

**2026-11-18 (owner's choice): "Near" follows exploration for a few cells.** With the shroud on, the land beyond an
explored edge opens like the map (the edge's own darkness, blurred sideways - no stripes) for ~3 cells and fades to full
darkness by ~5.5 cells; beyond an unexplored edge it stays black (`shroudAt`, `edge.x` = 1);
the setting **World beyond the map: Off / Near / Full** picks it (settings.ini `mapSurroundings` + `mapSurroundingsFull`,
`--map-surroundings off|band|full`; `D2K_SURROUND_FULL=1` forces Full): Full keeps the earlier whole landscape to the
horizon (`edge.x` = 2, gentle vignette). With
`--reveal` (no shroud) the whole landscape shows as before.

Not original, render only, Reimagined only (needs 3D terrain: Faithful, like the Reimagined terrain itself). Owner:
on a modern screen a small map looked like a tabletop standing in a black void. Now the battlefield sits inside a
larger desert: a decorative, non-playable landscape around the map, a subtle boundary, and a little camera travel
past the edges. Setting **World beyond the map** (Options -> Graphics -> 3D, and in the in-game options next to
Aerial haze in 3D; settings.ini `mapSurroundings` on|off, on by default, not part of a preset; `--map-surroundings
on|off`; env `D2K_SURROUND_OFF=1`). Off - and in Original, Enhanced and the plain 3D - everything is as before (black
void, the 8 px edge fade, the old camera clamp): one-frame screenshots of those are byte-identical to the build
before it. Nothing in src/game changed.

**Geometry** (`render3d/surround.*`, class `Surround`; built once per terrain build, ~40-50 ms, by
`Renderer3D::render` right after the terrain):
- A ring `Surround::kMarginCells` = 20 cells wide on an 8 px grid round the map's rectangle, then a flat *apron*
  (4 trapezoids out to 60000 px: enough for any zoom at the steepest tilt, where the top ray still meets the ground
  ~2.6 camera distances away - the horizon is never in view, so no pixel is left to the black clear colour).
- *Next to the edge* the ground is the map's own, **mirrored** across the edge: the ring's heights are
  `Terrain::height` at the mirrored point, and `terrain3_fs` reads the map's guide / splat / spice textures through
  mirrored-repeat samplers. So type, height and texture continue exactly: a rock edge goes on as rock, sand as
  sand, a cliff that crosses the edge keeps going, a levelled building foundation at the edge goes on as level
  ground. (Stretching the edge texels outward - a clamp - smears every outline into streaks; a mirror keeps the
  map's own detail at full resolution.) No concrete and (after ~1 cell) no spice out there.
- *The seam*: the ring's first row ARE the map mesh's edge vertices (position, normal, baked sun copied from
  `Terrain::vertices()`), including the 4 px vertices where the map draws its edge quad fine
  (`Terrain::quadFine`, the adaptive index buffer) - the row is a fan there, so no T-junction and no crack; the far
  index range (8 px quads everywhere) has its own plain first row (`Surround::drawRange(far)` mirrors
  `Terrain::drawRange`). While the surround is on, the map's own draw uses the mirror samplers too, so both sides
  filter the edge texels alike. When the map's heights change (a building placed or removed near the edge:
  `Terrain::updateFoundations`) `Surround::refresh` re-reads the mirrored zone and the seam (only when the terrain
  was re-uploaded that frame; a no-op compare otherwise).
- *Further out* (blend weight 0 for the first 1-2 cells, 1 from ~3-4; the weight is noisy, so no line shows. A
  mirror kept longer read as a copied block - a ledge inside the map came back as a ledge parallel to the edge) the
  ground is a **procedural desert** (`Surround::farAt`), read at coordinates shifted by a hash of the map's cell
  classes and levels (the same map always gets the same land; the game's RNG is never touched):
  - the edge's own rock carried on outwards - the map's rock cells and their heights, blurred, read under a cell
    inside the edge (so it is the edge's rock and level that run out) - as plateaus that end 90-470 px out in a
    cliff (tall rock) or a low shore (the 6 px buildable rock). So that it does not read as a block pushed out of
    the map: the read point is warped at two scales (up to ~115 px + 30 px, growing with distance), a noise
    threshold eats into the outline (bays, spurs, rounded corners), the rock narrows outwards, the end distance
    changes quickly along the edge, and tall rock gets a lower terrace along its rim and a slow swell of its top;
  - free-standing mesas (30-90 px tall, ~100 px across) and small outcrops, held back near the map by a slow
    ramp of the noise threshold (never by a cut: their outline is the noise's, not a front parallel to the edge);
  - dunes across the wind (`CinematicFx::kWind`; crests ~230 px apart, a long windward side and a short lee side,
    bent and broken up by noise, up to 20 px) over a broad swell; lower near the map, none on rock;
  - everything flattens to 0 before the ring's outer edge (from a distance that wanders, so nothing ends along a
    line parallel to the map), where the apron takes over as endless plain sand.
- *Nothing out there hides the battlefield*: the ring never rises faster than `Surround::kOcclusionSlope` = 0.42 px
  per px from the map edge's own height (the cotangent of the steepest tilt, 66 degrees, is 0.445). A mirrored
  cliff right outside the edge therefore becomes a ramp up to its plateau; what the map's ground shows through
  a pixel is never covered by scenery, so picking (which ignores the ring) and the picture agree.

**Shader** (`terrain3_fs`, MSL + HLSL; the same pipeline, textures and light as the map - sun shadows cast and
received, dust haze, aerial haze, point lights; one more draw call + one in the shadow pass):
- `F3U::reim[2].y` = the ring's margin (px), `.w` = the mode: 0 off, 1 the map with the surround on, 2 the ring.
- A ninth texture, the *far texture* (RGBA8, one texel per ring vertex: R rock coverage, G relief height in the
  guide's units via the inverse of `Terrain::reliefHeight`, B the blend weight, A the cliff mask). `guideX` returns
  the mirrored guide blended into (R, G, a procedural tone / ruggedness) by B; the splat's sand becomes dunes and
  its cliff mask the far texture's by the same weight. Where the weight is 1 the map's guide is not read at all. So
  the procedural land gets the same materials: rippled sand, plain / broken rock, sandstone faces on every steep
  mesh face (`reimCliff`), rubble at the foot of rock.
- `fog2.w` = 3 (shroud, no edge fade; 2 = neither) for terrain, rocks and clutter: the map no longer fades to black
  in its outer 8 px.
- **The boundary**: the land beyond is 15 % darker and 22 % greyer (ramped in over 20 px), and a faint pale line
  (+5 %, ~2 px, wider but weaker zoomed out) runs along the edge. Nothing permanent glows.
- **Shroud** (2026-11-18, modern-RTS style; owner: one explored edge cell lit a strip to the horizon, an unexplored
  one cast a black stripe outward). The land beyond the map **never depends on exploration**: it is drawn at one
  fixed, dim, slightly desaturated fog level - shroud darkness `PointLightsU::edge[1]` = 0.5, and the boundary tint
  is then 30 % greyer / 12 % darker (instead of 22 % / 15 %), together ~44 % of the lit land, not near-black.
  - *The blend*: at the edge itself the outside takes the map's own shroud value (no seam); over ~3 cells (92 px)
    outward it blends into the fixed level - an unexplored edge fades from black into the dim land, an explored edge
    darkens from the map's brightness into it. The edge value is blurred along the edge (7 taps, Gaussian, reach
    growing from 0 at the edge to ~2.5 cells), so no per-cell stripes or columns; in a corner region the taps along
    the two edges are weighted by the direction (continuous with the side regions). The map itself is unchanged:
    no shroud value inside the map is altered (no peeking at unexplored edge cells).
  - *Further out*: a gentle vignette (up to 22 % darker from ~4 to ~35 cells out, `edge[2]`) and the land fades
    into the distance haze with its distance from the map (up to 30 %, `edge[3]`; terrain and clutter: `edgeHazeK`),
    both under the fog darkness, so nothing out there glows.
  - At mission start the map is black while the outside shows at the fog level: only the map's outline is visible
    (the radar shows it anyway). Intended.
  - One rule for everything that reads the shroud: the shared helper `shroudAt` (MSL, `lights_msl.hpp`) / macro
    `SHROUD_AT` (HLSL, top of `shaders.hlsl`) gives the darkness of a world point; `PointLightsU::edge` (x = on)
    is set only with the surround on and the shroud shown (all 0 otherwise: the mask exactly as before, so
    every other mode is byte-identical). Used by `terrain3_fs`, `rock_fs`, `clutter_fs` (fog2.w 3), `model_fs`,
    `model_pbr_fs` and `sprite_fs` (relief decals; other sprite pixels outside the map are still hidden, as are
    objects outside the map - arriving carryalls etc.). The cinematic particles never read the shroud (their emitters
    are skipped under the shroud on the CPU, `CinematicFx` `hidden`). With `--reveal` (no shroud) the outside is as before (tint only).
- The sun shadow map's fit (`Renderer3D::fitLight`) reaches over the ring while the surround is on (its mesas cast
  onto the map and take the map's shadows); a view inside the map fits as before.

**Ground clutter** (`GroundClutter::placeBeyond`): a sparser scatter on the ring - mostly pebbles / gravel / tufts
on sand, rubble and boulders at the foot of its rock, flat stones on rock tops (1600-2000 pieces per map; its own
list and chunks, never hidden by a cell, the shroud of the edge through the shader, greyed like the ground). The
faithful terrain's boulder meshes, models, effects and track marks stay inside the map.

**Camera** (3D only, only while the setting is on - `Session::surroundWanted`): the scroll clamp lets the view go
`Session::kSurroundScroll` = 5 cells past each map edge (the yaw-turned half extents as before, so every edge
stays reachable at any yaw), so a base on the edge is no longer jammed against the screen border or under the
sidebar. 2D (Original, Enhanced) and 3D without the surround keep the old clamp exactly.

**Input** - unchanged game logic: `Session::screenToWorld` / `Renderer3D::pick` do not know the ring (a ray past
the map still ends on the y = 0 plane as before, and no object lives out there), so a click outside the map gives
exactly the order it gave before, or none. New, feedback only: `Renderer3D::outsidePoint` (the ray did not reach
the map's ground -> march over the ring) marks the point with a small **red cross** (`render::OrderFeedback::
addOutside` / `hoverOutside`): a click out there pops a cross that fades within 0.8 s; the moving pointer drags a
fainter one that fades 0.45 s after it rests or leaves.

**Cost** (M4 Pro, Debug build, 1280x800, 3D resolution 100 %, `D2K_GPU_TIMING=1`): ring 2PLAY1 (64x64) 109.9k
vertices / 217k triangles, O7V1 (80x80) 130.9k / 259k, a 128x128 map 193.9k / 379k. GPU memory: vertex buffer
3.1 / 3.7 / 5.4 MB, index buffer 2.6 / 3.1 / 4.5 MB, far texture 0.7 / 0.9 / 1.8 MB; CPU: the vertices again +
the procedural heights (0.4-0.8 MB) + the far texture (the index list is freed after the upload). 3D GPU pass:
a view inside the map 5.28 -> 5.35 ms (within the noise: the ring is clipped); a tilted view at the top edge with
half the screen beyond the map 5.5 -> 5.9 ms (those pixels were black before). No per-frame allocation.

**Check**: `./build/d2k --map O7V1.MAP --graphics 3d --reimagined on --terrain3d faithful --reveal --frames 200
--at 0 0 --screenshot out.bmp` (a campaign base in the map's corner; add `--map-surroundings off` for the old
picture), `--map 2PLAY1.MAP --skirmish ... --camera-tilt 60 --zoom 0.45 --at 32 30` (the whole small map in its
desert), `--camera-tilt 0 --zoom 0.3` (from above); the red cross: `D2K_DEMO_CLICKS="1.0:R:520:130" ... --record-fps
30 --frames 38`.

**Not done / known**: the far rock is plainer than the map's (no original tile detail to read out there) and its
cliffs are an 8 px mesh (no sculpted columns); the mirror can be recognised where a distinctive shape lies within
2 cells of the edge; spice fields are cut at the edge; the minimap does not show the land beyond.

### Realistic grade (Reimagined, 2026-11-18)

**Default since 2026-11-21: Off (the vivid look).** The owner: with photo terrain, ambient occlusion and the
redesigned models the vivid colours read right for Dune; the grade stays as the optional, Defcon-like look
(Options -> Graphics -> 3D tab "Realistic grade", the Custom tab, the in-game options).

Batch 1 of the visual audit against the *Defcon Zero* trailers (docs/reimagined-audit.md, gaps 1, 4, 7 and the
shader-only part of 2): the Reimagined picture read as cheaper mainly because of **value and colour** - pale cream
models on saturated orange sand and near-neon spice, all at about the same value. With the grade the ground is pale
and calm, the objects are darker and worn, the shadows deep and neutral. Shader / code only, no new assets.

Options → Graphics → 3D and the in-game options in 3D: **Realistic grade** (`settings.ini naturalGrade=on|off`,
default on; `--natural-grade on|off`; env `D2K_GRADE_OFF=1`). It needs the Reimagined terrain (3D terrain: Faithful +
Reimagined); render only, not part of a preset. **Off = the vivid look from before, unchanged**: every shader change
is behind one flag, the splat's extra channels stay 0 and the picture's grade pass is not run (compared with the
pre-change build: Original, Enhanced, the tile 3D byte-identical; Faithful 3D and Reimagined with the grade off
equal within the run-to-run noise of their animations, a frame of the new build matching a frame of the old one
byte for byte).

**1. The picture's grade** (`tonemap_fs naturalGrade`, `render/hdr.* Grade`; `Renderer3D::render` end). The final
pass of the 3D picture (the tone map of the HDR scene; without HDR the same shader runs over the 8-bit scene just
for the grade: `Options::gradePass`). After the shoulder, before the display adjustments, on linear light and by one
luminance curve (hue-preserving; the UI is not graded):
- saturation -22 % (`Grade::desaturate`);
- a filmic toe `y (y + 0.35 t) / (y + t)`, t = 0.035: the darks sink (linear 0.03 loses a third, 0.1 a sixth), the mids
  and lights keep their place - cast shadows and shaded sides go deep;
- the shadows a little cooler (below linear 0.2: red -7 %, blue +7 %);
- the highlights lifted 3 %.
By night the time of day's own blue grade keeps its colour and units stay readable (`Grade::natural(day)`: less
desaturation, a smaller toe, no cooling).
`D2K_GRADE="desat,toe,cool,lift"` overrides the four numbers (tuning).
With Lighting: Modern the day's warm cast is held back (`render::calmDayCast`, session): the sun a pale straw yellow
(1.10, 1.07, 0.89) instead of orange (1.20, 1.05, 0.84), the haze halfway to neutral, no extra saturation; dawn,
dusk and night keep their colours.

**2. The ground palette** (`terrain3_fs reimagine` / `reimCliff`, `F3U::reim[3].w`; MSL + HLSL):
- *sand*: pale ochre / khaki - 32 % of the tiles' saturation (vivid: 42 %), tint (1.0, 0.95, 0.845);
- *spice*: rust / cinnamon instead of near-pure orange - recoloured by luminance to (1.42, 0.84, 0.52) x 0.9, thick
  spice (1.42, 0.78, 0.46) x 0.72, the granules darker rust, the orange glow cut to a third (`reim[0].y` 0.07). Still
  the most saturated thing on the ground; thick is clearly darker than thin;
- *buildable rock*: 40 % less albedo contrast (its luminance pulled towards the plain rock's mean tone, the cracks
  mostly in the cavity light instead of the albedo), a warm khaki-brown instead of rust; the normals and the cavity
  occlusion keep the detail. Rock stays darker and rougher than sand (building, worms);
- *cliff faces*: the sandstone 40 % less saturated (the same family as the ground, no orange wall);
- *concrete*: a cooler grey (reads man-made against the khaki rock);
- loose stones (`rock_fs` tints) and the ground clutter (`GroundClutter::draw`) follow the palette.
The world beyond the map is drawn by the same shader, so the land out there matches without its own code.

**3. Models** (`model_fs`, `model_pbr_fs`; `PointLightsU::grade` / `dustColor`, `ModelFU::gplane` / `dust`):
- the albedo x 0.7 (a third darker); the house colour keeps its strength (its luminance is taken before the
  darkening) and gets 35 % more chroma, so it stays an accent after the picture's grade;
- **sand dust** (`sandDustK`): fine sand in the ground's colour on up-facing surfaces (patchy, 14-44 %) and over the
  foot of each model (the bottom 15 % of its height: buildings 3-12 px, vehicles 2.5-6 px above their ground plane;
  not flyers), matt on PBR models (roughness up, metal off). The patches use the model's uv, so they stay on a moving
  unit. Light and fine, not grime (the owner rejected a heavy-grime redesign: "grime doesn't suit cream buildings").
  `D2K_GRADE_MODELS="albedo,dust"` overrides (tuning).

**4. Grounding**:
- *Buildings and walls*: no new texture or pass - the designed terrain's splat B (4 px per texel, map-sized) has two
  spare channels. `DesignedTerrain::setGroundOcclusion(true)` fills B.b with the cells a building (1.0) or a wall
  (0.7) stands on, blurred (the splat's 2 x box blur: +-16 px), and B.a with the concrete's coverage, blurred. It is
  rebuilt with the splat: the whole map when the setting is switched, afterwards only the cells round a building
  that appears or goes (the same 2.5 Hz check that follows spice and concrete; changes far apart rebuild their own
  8 x 8 cell blocks, not the rectangle between them; `D2K_SPLAT_LOG=1` logs each rebuild). `terrain3_fs` multiplies the sky
  light by `1 - 0.62 smoothstep(0.04, 0.72, B.b)` (and the sun by a fifth of that): a soft dark band about 10 px wide
  where walls meet the ground, deeper inside cast shadows.
- *Units* move every frame, so they are not in the texture: one soft dark blot per ground unit
  (`Renderer3D::addContactShadows`: a decal quad turned with the unit, 1.5 x its hull from the model's running
  gear, alpha 0.42, infantry 0.3), under the track marks' layer of the batch. Not under flyers, carried units,
  sandworms or anything cloaked / stealthed.
- *Sand over the slabs' edge* (`terrain3_fs`, from B.a): no straight grid line where concrete ends - in places
  tongues of sand reach 10-15 px over the slabs and break up into grains, elsewhere the edge stays bare, a thin veil
  further in, a little drifted sand on the ground along the slabs.

**Cost** (this Mac, 1280 x 800, Debug build, `D2K_GPU_TIMING=1`, 20 tanks + a base, Modern lighting + HDR): the 3D
GPU pass 4.4-5.5 ms with the grade and 4.4-5.2 ms without (inside the run-to-run spread), CPU update + render 7.8 ms
either way. With HDR the grade is a few instructions in the existing tone-map pass; without HDR one extra full-screen
pass. No per-frame allocation (the blots go into the existing decal batch). The occlusion is rebuilt on the CPU only
when buildings change: 5.8 ms for a 6 x 6 cell base appearing, 30 ms for 20 buildings at once (Debug), then one
splat upload.

**Check**: `D2K_VFX_DEMO=reimagined ./build/d2k --map 2PLAY1.MAP --skirmish --graphics 3d --reimagined on --terrain3d
faithful --reveal --frames 200 --at 10 17 --zoom 2.6 --vfx-demo --screenshot out.bmp` (tanks and a base on sand; add
`--natural-grade off` for the old picture, `--lighting modern --hdr on` for the HDR path), `D2K_VFX_DEMO=harvest ...
--at 10 9` (spice with harvesters), `--map H5V1.MAP ... --at 5 22 --zoom 3.4` (a base on rock: the slabs' edges).

**Not done / known**: the dust band follows the model's ground plane, so on a building whose model stands on a plinth
the band lies on the plinth's side only; the occlusion uses the footprint's cells, so a model much smaller than its
footprint (wind trap) gets a band a few px away from its walls; unit blots are not clipped by a building's deck (a
harvester on the refinery's pad has its blot under the pad); the sprites of types without a model are darkened only
by the picture's grade. Asset work (baked AO / wear, gap 2) and SSAO are later batches.

### Vehicle wrecks (Reimagined, 2026-11-18)

Setting "Vehicle wrecks" (`settings.ini vehicleWrecks`, on by default; `--vehicle-wrecks on|off`, env
`D2K_WRECKS_OFF=1`; Options → Graphics → 3D and the in-game options in 3D). It needs Reimagined, Modern effects
and 3D models (the cinematic particles and a model to leave behind). Off = a vehicle's death exactly as before.
`render3d/wrecks.*` (state), `render3d/models.*` (the hulk's draws), `render3d/cinematic_fx.*` (its fire). The same
setting covers shot-down flyers ("Aircraft crashes" below).

The owner's report: at a slow GameSpeed a destroyed tank "just disappears". The original removes a destroyed unit
from the world and frees its cell, and that stays exactly so - nothing in `src/game` changed. The picture alone
keeps a short-lived wreck:

- **Fireball.** The death explosion's fireball is 1.5x the radius, it cooks off twice more (three times for the
  large class) within ~0.9 s, and hot glowing debris flies out in arcs trailing smoke. Its soot thins out in 60 %
  of the usual time and it leaves no lingering column of its own (the hulk smokes), so the hulk shows after ~1 s.
  The scorch mark keeps its old size.
- **Hulk.** The vehicle's own model stays where it was drawn (the pose of its last frame: Smooth driving's /
  the Modern effects' eased facings, the hull's lean), burnt black-brown with rusty patches (`wreckChar` in
  `lights_msl.hpp` / `shaders.hlsl`, driven by `dust.zw` of the model shaders' fragment block), ember veins
  glowing in patches and, on PBR models, in the seams (the baked occlusion), flickering and dying down. Small
  flames lick over it, a thin grey-brown smoke column rises off its top and leans with the wind, a small fire
  light flickers on the ground. The hull hops with the blast and comes down askew (the lighter the vehicle the
  more).
- **Turret pop.** A model with a turret part loses it 0.55 s after the blast: it flies up ~20-30 px, tumbles
  top over and lands upside down 12-25 px from its ring, trailing smoke, with a puff of dust where it lands.
  Vehicles without a turret part (trike, quad, harvester, MCV) only hop and lean.
- **End.** After 7 s (trike, quad, raider), 8.5 s (tanks) or 10 s (harvester, MCV, Devastator; + up to 0.8 s)
  the hulk sinks ~40 % of its height over the last 1.8 s and crumbles away (a noise dissolve with a glowing
  edge, no alpha fade). The scorch mark stays (~1 min) and a few dark pieces of debris lie about for 16-24 s.
- **Not an obstacle.** A live unit within ~25 px of the hulk (infantry: 11 px) makes it crumble away in 0.22 s
  and it never comes back; the turret where it lies is checked on its own. Units drive through the cell as they
  always did.

**Telling a death from the other ways a unit leaves the map.** DestroyUnit 0x49cde0 alone starts EX_WHITEN1 (the
white flash under the death explosion) on the unit's own pixel and leaves the unit in state Dead for its dead-state
timer (10 ticks). The Modern effects already log that flash as `FxEvent::VehicleDeath`. A hulk is made only for a
ground vehicle (not infantry, not flying / carried / hidden) that is in state Dead within 2.5 px of such an event.
So: a worm's meal, a deviated or captured unit's old slot and a delivery carryall flying off are state Dead
without the flash; a harvester in the refinery, a unit on the repair pad or under a carryall never reach state
Dead; a load or restart resets the pool (`ModelRenderer::settle`, or the tick counter running backwards). The
dying unit, still in the pool for those ticks, hands the hulk its drawn pose and is not drawn a second time.

**Time.** Real seconds only: `Wrecks::pose` is a pure function of the hulk's age (the frame time, frozen while the
game is paused), so the sequence is the same at GameSpeed 75 and 100.

**Budget.** 24 hulks (`Wrecks::kMax`), the one nearest its end is replaced; no allocation per frame. The blast's
particles go through the Effects intensity budget like any explosion; only the newest hulks up to the column cap
smoke (the others keep their flames), at the budget's column rate / life. A hulk replaces the 16-22 s burning
column a vehicle's death had, with a column about a third of its width: a big fight has less standing smoke
than before.

**Checks.** `D2K_VFX_DEMO=wrecks` (sim_test.cpp): a row of trike, quad, combat tank, siege tank, missile tank,
harvester, MCV and Devastator destroyed at tick 40 (`D2K_VFX_DEMO_KILL`, `_STAGGER=N` ticks apart, `_UNIT=NAME`
one vehicle on the site, `_DRIVE=N` a tank drives through the wrecks N ticks later). `--game-speed N` sets the
GameSpeed of a run.

**Not done / known**: the turret's flight starts inside the thinning fireball, so at a close zoom it is seen only
from about halfway; infantry is untouched; a vehicle type without a model keeps its old death; the hulk's char
pattern follows the model's uv, so models with very uneven texel density get uneven vein sizes; a crumbling hulk
stops casting its shadow at 35 %. Flyers: "Aircraft crashes" below.

### Aircraft crashes (Reimagined, 2026-11-18)

Part of the setting "Vehicle wrecks" (no row of its own; the same `--vehicle-wrecks` / `D2K_WRECKS_OFF=1`, the same
needs: Reimagined, Modern effects, 3D models). Off = a flyer's death exactly as before. `render3d/wrecks.*`
(`Hulk::crash`, `Wrecks::crashOf`, the fall in `Wrecks::pose`), `render3d/models.*` (`poseWreck` / `addWrecks`),
`render3d/cinematic_fx.*` (`crashHit`, `crashTrail`, `crashImpact`).

The owner's report: a destroyed aircraft just vanishes in mid-air. The original removes a destroyed flyer where it
is hit (DestroyUnit 0x49cde0: the flash and the death explosion at that point, drifting on along its heading; the
unit flies on in state Dead for its dead-state timer, then is gone; what a carryall carries is destroyed with it,
a delivery's cargo is never made). All of that stays exactly so - nothing in `src/game` changed, nothing falls in
the simulation, the crash does no damage. The picture alone:

- **Hit.** A fire burst on the hull where it flew (a small fireball, no ground dust, its soot clears fast), sparks,
  and 3-5 glowing parts thrown off with the flyer's way on, trailing smoke.
- **Fall**, 0.7-1.6 s by the height it falls (`0.55 + drop / 57` s: 1.6 s from the cruising 60 px, ~0.7 s for a
  carryall shot at its 10 px landing height). It keeps some of its way on and arcs down (forward speed easing off,
  sink speed growing): an **ornithopter** goes into a flat spin that winds up (~1.5 turns) and banks over to
  60-80 degrees, nose dropping, its wing beat stopped in the pose it was hit in; a **carryall** (and a frigate,
  should one ever be destroyed) noses down ~30 degrees and rolls slowly ~25 degrees. The hull chars on the way,
  flames lick over it, and it drags fire, sparks and a smoke trail that is black at the hull and thins to grey
  (lit puffs: the sun lights them like any smoke). Its shadow (the shadow map's, cast by the falling model) runs
  in to the impact point.
- **Where.** 38-60 px (1-2 tiles) on along its heading from the death point, less from low down (a carryall
  landing / lifting off comes down almost where it is). The point is clamped 20 px inside the map; if that cell is
  a building, the nearest open point along the heading (+-16, +-32, +48 px) is taken; never higher than the flyer
  was. If no open point is found (over the middle of a base) it comes down where it is and leaves **no hulk** - the
  impact only (a hulk must not stand in a building).
- **Impact** at the terrain height of that point (`Terrain::height`, as the wrecks and the flight ground use): an
  explosion about a vehicle's death in size with its shock ring, rolling dust, thrown sand, sparks and debris, a
  ring of dust puffs, a scorch mark, one cook-off, glowing parts thrown on along its way.
- **Hulk.** The model lies where it came down, askew and part buried, charred, burning with small flames and a
  thin smoke column, for 4.5-5.5 s (shorter than a ground vehicle's 7-10 s), then sinks and crumbles away like any
  hulk; the same not-an-obstacle rule (a live unit within ~25 px: gone in 0.22 s; not checked while it is in the
  air).
- **Cargo.** What the carryall carried falls with it - 10 px under it, let go, tumbling a little - and hits with
  a smaller burst of its own ~15-20 px beside the carrier and a little short of it. A vehicle leaves its own hulk
  (turret on; 5-6 s). A carried unit of a pickup is a unit (it dies in the same DestroyUnit call); a delivery's
  cargo is no unit: the first type of the delivery's list, as it was drawn under the carryall. Infantry cargo: no
  hulk.

**Telling a shot-down flyer from the other ways a flyer goes.** The same test as for ground vehicles, in a second
pass: DestroyUnit starts its flash (EX_WHITEN1, logged as `FxEvent::VehicleDeath`) for a flyer too - on the ground
under it - and leaves it in state Dead with the dead-state timer at 10. A crash is made only for a unit with the
flying flag (not the Death Hand: flag 0x20000 / its behaviour), in state Dead with the timer still >= 5, with a
death explosion in its template and a 3D model, where that flash lies on its heading line (within 3 px across,
40 px behind .. 16 px ahead: the dead flyer flies on and the flash drifts on, each at its own pace). So: a
delivery carryall or a frigate that has flown off the map is put in state Dead without DestroyUnit - no flash, and
it is outside the map (hidden) anyway; a carryall putting its cargo down or picking one up never is in state Dead;
the Death Hand ends in its warhead without the flash; a flyer whose death point is under the shroud is skipped
like any hulk. The frigate: the air defence never targets it (the flyer spiral of FindNearestEnemy skips its
type), so it normally cannot be shot down; if area damage ever destroys one it falls like a carryall.

**The ground under the hit.** The Modern effects started a wreck fire and a hulk mark on the ground under every
death flash - right for a tank, wrong under a flyer that now comes down somewhere else. `Effects::airCrashes(true)`
(set by the session for the 3D frame when the crashes are on, cleared after the effects' update) skips them for a
flash with the explosion's drift flag (only DestroyUnit of a flyer sets it) and for the flash of what a carryall
carried (no drift: a dead unit with the carried flag on the flash's pixel). 2D and wrecks-off: unchanged.

**Time / budget.** As the wrecks: `Wrecks::pose` is a pure function of the hulk's age in real seconds (frozen while
paused; the same at GameSpeed 75 and 100). A crash is one hulk of the pool of 24 (a carrier and its cargo: two);
the particles go through the cinematic particle pool and its intensity budget; nothing is allocated per frame.

**Checks.** `D2K_VFX_DEMO=crash` (sim_test.cpp): flyers of the player destroyed in flight (the real DestroyUnit) at
tick 40 (`D2K_VFX_DEMO_KILL`). `D2K_VFX_DEMO_UNIT=ORNITHOPTER` | `CARRYALL` (carrying a harvester as after a pickup;
`D2K_VFX_DEMO_CARGO=NAME` another unit) | `EMPTY` | `DELIVERY` (a carryall reinforcement to the site, destroyed
within 5 tiles of it); default an ornithopter and a loaded carryall 6 tiles apart. `D2K_VFX_DEMO_LOG=1` logs the
deaths. E.g. `D2K_VFX_DEMO=crash D2K_VFX_DEMO_UNIT=ORNITHOPTER ./build/d2k --map H5V1.MAP --graphics 3d --reimagined
on --terrain3d faithful --effects modern --lighting modern --hdr on --reveal --zoom 3 --camera-tilt 55 --at 21 34
--vfx-demo --record DIR --frames 160 --record-fps 30` (the death is at frame ~60).

**Not done / known**: the fall is a fixed arc, not physics - it does not use the flyer's real speed (a hovering
carryall at cruising height still travels 1-2 tiles) and it passes through a cliff or a building that stands
between the death point and the landing point; a flyer shot down over the middle of a large base leaves only the
impact (on the roof it is over, drawn at the terrain height); the wings of the ornithopter stop rather than
flutter; a flyer type without a 3D model keeps its old death, but with the crashes on it no longer leaves the fire
on the ground under it; the house-colour stripes stay bright on the falling hull until the impact chars it; no
sound of its own (the original's death explosion sound plays at the hit, nothing at the impact).

### Building life (Reimagined, 2026-11-18)

An idle base used to stand still. With Reimagined, Modern effects and 3D models, building models now move and give off
smoke, driven by state the game already has. There is no setting row: it is part of Reimagined's cinematic effects.
`D2K_BUILDING_LIFE_OFF=1` turns it off; Reimagined is then pixel-identical to the build without it. Render-only: the
World is read, never changed, and the game RNG is not used (spawn jitter uses the cinematic particles' own RNG; phases
come from a hash of the building's slot). Code: `render3d/models_life.cpp`
(`ModelRenderer::buildLife` / `buildingLife`, `Renderer3D::buildingLifeFx`) and `CinematicFx::buildingLife`.

**Timing.** Everything runs on real seconds (the frame's dt, `ModelFrame::frameDt`). The clock freezes when no game
tick has come for 0.2 s (paused), the same rule as the cinematic particles. Speeds are therefore the same at
GameSpeed 75 and 100. Spinner turns are integrated per building (`LifeState`, side x 1000 + slot, reset when the
slot's type / timeAdded changes), so a change of speed never jumps.

**Moving parts by node name.** These rules apply to building models only. A part must be a glTF node of its own,
with its origin on the spin axis. The axis is the node's local **+Y** (Blender local +Z). The meshes of the node and of
its children move with it, and a spinner on a spinner is composed. The pose goes through the joint palette, as wheels
and pose variants do (`model_vs`, no shader change). The CPU mesh is read once at the upload, before `MeshPart::slim`.

| name prefix | motion | speed |
|---|---|---|
| `turbine*`, `fan*`, `rotor*`, `propeller*` | spin (5 / 8 / 7 / 7 rad/s at full speed) | the building's power: a power plant runs at 0.3 + 0.7 x health; any other building is also scaled by the base's power percent. It eases up over ~2 s and down over ~1.4 s, and is 0 while the building is built up / sold / dropping in / assembling |
| `anemometer*` | spin, 4.5 rad/s with slow gusts | the wind: always, except during build-up |
| `vane*` | turned so its tail (area-weighted centre of its triangles) points downwind (13, -6) | swings about ±9° with the gusts |
| `dish*`, `radar*` | turn, 0.9 rad/s | while the original animates it (animation flags permanent/once + HAS_ANIMATION, at least half health: the outpost's radar). It replaces the original's stepped pose variants (smooth instead of 10 steps) |

A `turbine` / `fan` / `rotor` / `propeller` part may be modelled with another thin axis (a fan disc thin along local X).
It then spins about its thinnest local axis if that axis is clearly thinner than local Y (< 0.5x). If its vertices are
not centred on the axis (centre offset > 0.3 x radius), it is no rotor and stays still; the built-in pack's pipe run
named `turbine` is such a case. Found parts are logged at the upload: `3D models: KEY: building life: ...`.

**Turret idle scan.** A building turret (template turns: `barrelArt` or `actLikeTurret`, model with a turret part) whose
game facing has not changed for 3 s starts a slow look-around. That needs state 2 (normal), no build-up, and power when
the template requires it. The offset is ±0.5 rad, a sine of period ~22 s that dwells at its ends, faded in over 2 s.
When the game turns the facing (a target), the offset eases back to 0 with tau 80 ms, so the game's aim takes over at
once. Muzzle anchors keep the game facing, so the offset is ~0 by the time a turret fires.

**Effect anchors.** `Renderer3D::buildingLifeFx` reads these per visible, standing building, at most 48 per frame.
Per building type, the anchor set is looked up once.

| anchor | when | effect |
|---|---|---|
| `fx_smoke*` (else `fx_flue*`, never on a refinery) | the building works (production holds the animation flag *permanent*: factories, barracks, palaces) | a thin light-grey lit wisp (`kStackWisp`: 6 puffs/s x sqrt(size), ~5 s, leaning downwind) |
| same | a power plant (`powerConsumption < 0`) standing | faint white steam (`kSteam`: 5/s, ~2 s) |
| `fx_vent*` | works / produces power | a barely visible warm puff (`kVentHeat`, alpha 0.07) |
| `fx_beacon*` | standing | a red blink every 1.9 s (0.16 s), each beacon in its own phase: a small HDR flash point. At night it also gives a small red point light (x the time of day's lamp strength) |
| `fx_light*` | (unchanged) | the night lamps of `SceneLights` |

The refinery's flues are left to the Modern effects' refinery vent: flames and smoke while it unloads, already placed
on `fx_flue*`. Strength = the anchor's size x (0.5 + 0.5 x health). Life puffs are spawned as lasting smoke, so the
transient-smoke cap (1 s at the busier intensities) does not cut a wisp before it rises. Their rate follows the effects
intensity's column rate (at least 0.5) x the particle load.

**What got something** (pack models of 2026-11-18):
- Reimagined ATREIDES_WIND_TRAP: `turbine_s`, `turbine_n`, `fan_0`, `anemometer`, `vane`; steam from `fx_smoke_0`; heat
  from `fx_vent_0..2`; beacon `fx_beacon_0`. The turbines sit behind the louvres and are hard to see from the default
  view; the cooler fan is plainly visible.
- Reimagined HARKONNEN_WIND_TRAP (docs/3d-models.md "Wind trap redesigns"): `turbine_s`, `turbine_n`, `fan_0`
  (the extractor on the generator drum, plainly visible), `fan_1` (the bunker's wall fan), `anemometer`, `vane`; steam
  from `fx_smoke_0/1` (the two stacks); heat from `fx_vent_0..2`; beacons `fx_beacon_0/1`.
- Reimagined ORDOS_WIND_TRAP (the same section): `fan_0` (the big fan of the cooling pit, 12 px across: seen at any
  zoom), `turbine_s`, `turbine_n` (in the open mouths between three slim louvres: pale gold blades with lit tips in
  front of a glowing ring, plainly visible), `fan_1` (the kiosk's roof extractor), `anemometer`, `vane`; steam from
  `fx_smoke_0` (the relief vent on the transformer); heat from `fx_vent_0..2`; beacon `fx_beacon_0`.
- Reimagined ATREIDES_REFINERY (docs/3d-models.md "Refinery redesign v2"): `fan_2` (the cooler in the north-east
  corner, 5 px across: the one seen at the default zoom), `fan_0`, `fan_1` (the extractors on the two roofs),
  `anemometer`, `vane` (the mast on the bridge); heat from `fx_vent_0..2` while it works; beacons `fx_beacon_0` (the
  mast) and `fx_beacon_1` (the tallest flue's rim); lamps `fx_light_0..3`. Its flues `fx_flue_0..4` are unchanged
  and stay with the refinery vent effect.
- Reimagined ATREIDES_CONSTRUCTION_YARD (docs/3d-models.md "Construction yard redesign v2"): `fan_0` (the cooler in
  the north-east corner, 4.6 px), `fan_1` (the crane house's roof extractor), `anemometer`, `vane` (the mast on the
  conveyor's end block); a wisp from `fx_smoke_0` (the crane engine's exhaust) while the yard works; heat from
  `fx_vent_0..1`; beacons `fx_beacon_0` (the mast) and `fx_beacon_1` (the crane's pivot); lamps `fx_light_0..5`. Its
  crane keeps its pose variants (the pack's own, for the new node list).
- Outposts with a `dish` node: Atreides (Reimagined + main), Harkonnen, Ordos and the two special ones. The dish turns
  smoothly.
- Atreides light factory (`fx_flue_0/1`) and Atreides heavy factory (`fx_flue_0`): stack wisps while producing.
- Main-pack Harkonnen / Atreides wind traps (`fx_flue_0`): steam (seen without Reimagined models, or with
  `D2K_MODEL_PREVIOUS`).
- Medium and large gun turrets, and every model with a turret part: the idle scan.

Checks: `D2K_LIFE_LOG=1` logs every animated building's state (clock, speed, integrated turns, turret offset). Scenes:
- `D2K_VFX_DEMO=reimagined D2K_VFX_DEMO_ANCHOR="40 31"` on H5V1 (`--at 36 28 --zoom 5`): the wind traps;
- the same with `D2K_VFX_DEMO_BASE=full D2K_VFX_DEMO_WORK=1`: every Atreides building, with the factories holding
  the producing flag. The light factory is at tiles 30, 43 (`--at 35 45`).

Not yet:
- no heat-haze refraction (the vent puffs stand in for it);
- the dish ignores the outpost's own stepped variants;
- the turbines' speed follows health, not the side's actual power balance (a wind trap always "produces").

### Photo terrain (Reimagined, 2026-11-18)

The second finding of the audit against the *Defcon Zero* trailers (docs/reimagined-audit.md, gaps 7 and 10): after
the Realistic grade our ground still read as **procedural patterns** - fingerprint ripples, dotted gravel, brick-like
strata - where Defcon shows photographed ground. With the photo terrain the detail of every ground material is a real
CC0 photogrammetry scan; the layout, the palette and the spice stay ours.

Options → Graphics → 3D and the in-game options in 3D: **Photo terrain** (`settings.ini photoTerrain=on|off`, default
on; `--photo-terrain on|off`; env `D2K_PHOTO_OFF=1`). It needs the Reimagined terrain (3D terrain: Faithful +
Reimagined) **and the photo terrain pack**; render only, not part of a preset. Without the pack (or with one of its
files missing or unreadable) the setting does nothing and the procedural Reimagined ground is drawn - one log line
says so. Off = the procedural ground from before.

**The pack** (`artpacks/photo-terrain/`, 78 MB, not in git; docs/art-pipeline.md "Photo terrain pack"): seven Poly
Haven scans (CC0 1.0), 2048 x 2048, built by `tools/fetch_photo_terrain.py` from `tools/photo_terrain_manifest.json`:

| Layer | Scan | Used for | World px per repeat |
|---|---|---|---|
| 0 sand | Sandy Gravel 02 | the open desert | 160 |
| 1 ripples | Aerial Beach 01 | wind-rippled sand: dune cells | 200 |
| 2 gravel | Coast Sand 01 | stony sand: strewn stones everywhere, patches, the foot of rock, under the spice | 220 |
| 3 rock | Sandstone Cracks | buildable rock | 150 |
| 4 rough | Rock Boulder Cracked | the rock the original draws broken (rims, infantry-only rock) | 130 |
| 5 cliff | Cliff Side | cliff faces (two vertical projections) | 104 |
| 6 concrete | Dirty Concrete | the slabs | 96 |

Per layer three block-compressed, mip-mapped `.d2kt` files, uploaded as they are into three texture arrays
(`render3d/photo_terrain.*`, loaded on the first Reimagined frame that wants them, ~0.4 s): colour (BC1), normal x / y
(BC5), and occlusion / roughness / height (BC1). 78 MB of GPU memory; a device without BC textures decodes them to
RGBA8 from the second mip on. The scales are larger than life on purpose (a 2 m scan over 3-5 cells): at an RTS
camera's distance a true-scale scan is one flat colour; these keep cracks, stones and ripples a few pixels wide at the
default zoom and real grain zoomed in. `D2K_PHOTO_SCALE="s0,...,s6"` overrides them, `D2K_PHOTO_LOOK="share,normal,
sheen"` the scans' share in the colour, the normal strength and the sun sheen, `D2K_PHOTO_PACK=<dir>` the folder.

**Shader** (`terrain3_fs`: `photoTap`, `photoAdd`, the photo block of `reimagine`, `reimCliff`; HLSL twin in
`shaders.hlsl`; `F3U::photo`; texture slots 9-11):
- *The palette stays.* A scan gives `colour / its mean colour` (the mean is in the file's header), multiplied into the
  colour the terrain had: the colour lock, the Reimagined / graded palette, the original's tone levels on broken
  rock, the spice. Only 55 % of the scan's own hue differences are kept. The old 1K scans still give that colour and
  the height blend between materials, but 70 % of their own light and dark and 75 % of their normals are taken out
  (`calm`), so the detail is the photograph's alone. Small gains keep the palette's brightness (rock +20 %, sand
  +5-10 %, concrete +22 %: the scans' cavities and rougher normals take light away) and broad warm / cool patches keep a
  whole plateau from being one flat brown. Mean picture brightness against the procedural ground: within 1-4 %.
- *What replaces what*: the procedural wind ripples, pebble dots and grain on sand -> the sand scan, the rippled scan
  on dune cells, and the stony scan shown **by its own height**: everywhere its tallest stones lie strewn over the
  fine sand (27 % cover), in noise patches / along the foot of rock / under the spice nearly all of it; the rock's
  noise-contour cracks -> the cracked sandstone scan; the strata bands on top-down faces -> the fractured rock scan;
  the procedural `sandstone()` of the mesh's cliff faces -> the layered sandstone scan projected on the two vertical
  axes (strata horizontal, nothing stretched down a face) in the same ochre, with its own normal and cavity light;
  the slabs keep `concreteSlab3` (seams, stains, sand over the edges) and take the concrete scan's grain.
- *Spice stays procedural* (the organic field, rust granules, mounds, thick darker than thin): under it only the
  stony scan's grain shows, without its colour.
- *No visible repeat*: (1) the converter flattens every scan's large-scale brightness (what is larger than a sixth
  of the scan), so a repeat has no blotch to recognise; (2) `photoTap` switches between offset copies of the scan by
  a low-frequency noise (as `sampleTiled`; two copies are read only inside a transition, so most pixels cost one tap
  per texture); (3) a second, 3.4 x larger and turned copy of the colour modulates the first - detail that survives
  zooming out. Checked at zoom 0.55-3.
- *Normals, occlusion, roughness*: the scans' normals light the ripples, cracks and strata (strength per layer); their
  occlusion (rescaled by the converter so open surface = 1) goes into the cavity light; `1 - roughness` gives a
  faint sheen of the low sun on smoother rock (`photo[0].w`).
- With the calm photographed rock the guide's 8-bit height steps showed as contour lines under the low sun (they were
  hidden in the procedural mottle): relief slopes under 0.2 px / px are left to the mesh in photo mode.

Track marks, scorch marks, contact blots and the placement ring are decals over the terrain and unchanged; the ground
clutter and the loose stones keep their palette. The world beyond the map draws through the same shader with the same
textures, so the land out there matches.

**Cost** (this Mac, 1280 x 800, Debug build, Modern lighting + HDR, `D2K_GPU_TIMING=1 D2K_PHOTO_AB=1`: on / off in
alternate 30-frame blocks of one run): A5V1 base 5.70 ms with, 5.80 ms without; 2PLAY1 zoom 0.6 5.44 / 5.70 ms; at
2560 x 1600 12.98 / 13.35 ms - not slower (the procedural pebbles and sandstone it replaces cost a little more than
the taps do).

**Check**: `./build/d2k --map 2PLAY1.MAP --skirmish --graphics 3d --reimagined on --terrain3d faithful --effects
modern --lighting modern --hdr on --reveal --screenshot out.bmp` (sand, rock, spice, cliffs; `--zoom 3 --at 12 14`
close up, `--zoom 0.55` wide, `--photo-terrain off` for the procedural ground), `--map A5V1.MAP` / `O7V1.MAP` (bases
on rock, slabs), `--map H5V1.MAP` (the other tileset).

**Not done / known**: the cliff scan is softer than the ground at close zoom (104 px per repeat over faces up to 70 px
tall); the stony scan carries a little dark organic debris (its darkest values are lifted); the plateau's large mesh
facets show as faint straight tone edges on the calmer rock; the scans' roughness is used only for the sun sheen;
Reimagined with the photo terrain off differs from the previous build in a handful of pixels by 1 / 255 (the shader
was recompiled; Original, Enhanced and 3D without Reimagined are byte-identical).

### Ambient occlusion (Reimagined, 2026-11-18)

The other half of the same audit round (gap 4, "SSAO only later"): objects lacked the soft darkening in corners and
under overhangs that makes them sit on the ground. Now the 3D view has screen-space ambient occlusion: dark creases
where a building meets the ground and between its parts, under tank hulls, in cliff crevices and between rocks.

Options → Graphics → 3D and the in-game options in 3D: **Ambient occlusion** (`settings.ini ambientOcclusion=on|off`,
default on; `--ambient-occlusion on|off`; env `D2K_SSAO_OFF=1`). Reimagined terrain only; render only, not part of a
preset. Off = no pass, no texture, the shaders' occlusion term is exactly 1.

**Passes** (`render3d/ssao.*`, `ssao_msl.hpp`; `Renderer3D::render` before the scene pass):
1. *Depth pre-pass*, half resolution: terrain, loose stones and every model through their depth-only pipelines
   (the shadow map's, but **without its slope bias** - terrain, stones and models alike) from the camera into a
   sampleable depth texture. (Fix 2026-11-18, the owner's "flickering blotches" on rock at a low tilt: the terrain
   and the stones were drawn with the shadow map's slope-scaled bias, which pushes each triangle back by its own
   screen-space depth slope - neighbouring triangles of bumpy rock got depth steps that the taps read as occluders:
   dark seams along the mesh's triangle edges and blotches on broken rock / slabs, changing shape whenever the
   camera moved because the slopes are the camera's.)
2. *Occlusion* (`ssao_fs`, a full-screen triangle with `fxc_vs`): per pixel the view-space position and normal from
   the depth's own differences (the nearer neighbour on each axis, so no normal buffer), then 10 taps on a rotated
   spiral within 14 world px on the screen. A tap counts by how high it stands above the pixel's tangent plane (the
   sine of its elevation, minus a 0.16 bias that ignores gentle slopes), fading out with its distance in space over
   a 48 px **reach** - the roof two storeys above the foot of a wall still counts, nothing far in front or behind
   does, so a unit gets no halo on the ground beyond it. Zoomed so far out that the radius is under a pixel: none.
3. *Blur* (`ssaoblur_fs`): 5 x 5, weighted by depth with a tolerance that follows the tilted ground's own depth
   slope: the rotation's noise goes, edges stay.

**Use**: `PointLightsU::ssao` (the block every lit shader already has: xy = pixel -> uv, z = strength 1) and one
more texture slot in `terrain3_fs` (12), `rock_fs` (3), `model_fs` (3) and `model_pbr_fs` (5); `ssaoAt()` /
`SSAO_AT`. It scales the **sky light only** - the sun's term is untouched, so sunlit faces keep their brightness and
the creases deepen most in shadow. No double darkening: on the terrain the darker of it and the Realistic grade's
building occlusion (`min`, the same crease), on rocks the darker of it and their own baked value, on PBR models times
the baked map (fine cavities times broad ones), and the blot under each ground unit is drawn at 55 % while the
occlusion is on. The sprites of types without a model, the clutter and the particles do not take it.

**Sun shadows**: the contact-hardening filter asked for with this already exists on the terrain (`shadowSoft`,
"Reimagined": the penumbra grows with the distance to the caster); the models' own 3 x 3 PCF is unchanged.

**Cost** (this Mac, 1280 x 800, Debug build, Modern lighting + HDR, `D2K_GPU_TIMING=1 D2K_SSAO_AB=1`): A5V1 base
+0.35 ms (5.52 ms with, 5.17 without), 2PLAY1 zoom 0.6 +0.17 ms, the big-fight demo at zoom 2 +0.23 ms; at 2560 x 1600
(A5V1) +0.74 ms. Most of it is the depth pre-pass's geometry; the two
full-screen passes run over a quarter of the pixels. `D2K_SSAO="radius,strength,bias,share,reach"` overrides the
numbers (tuning).

**Check**: `./build/d2k --map A5V1.MAP --graphics 3d --reimagined on --terrain3d faithful --effects modern --lighting
modern --hdr on --reveal --zoom 2 --screenshot out.bmp` with and without `--ambient-occlusion off`.

**Not done / known**: half resolution, so a crease's edge is 2 px coarse (visible only on flat colour, zoomed in);
only what the camera sees occludes - the ground behind a building (north of it) gets its crease from the building
occlusion, not from this; small relief on broken rock gets short horizontal shadows (slopes facing away from the
camera); no temporal filter (none needed at 10 taps + blur, and nothing to ghost).

### Spice fields in 3D (Reimagined, 2026-11-18)

Gap 3 of the audit against the *Defcon Zero* trailers (docs/reimagined-audit.md): the spice was a flat cinnamon stain
on the ground, where Defcon's resource field is three-dimensional (lit chunks with cast shadows, the harvester sitting
in it). Melange is spice sand, so the field is built from **drifts, mounds and clumps, not crystals**. Reimagined
terrain only (3D terrain: Faithful + Reimagined); Original, Enhanced, Designed, plain Faithful and Reimagined off are
unchanged (byte-identical against the previous build). No new setting: the clumps belong to **Ground detail** (Off =
no clumps, Sparse = half of them), and the relief stays at every Ground detail.

**1. Relief in the mesh** (`Terrain::spiceRelief`, `terrain.cpp`). The Reimagined heightfield (4 px grid) adds the
spice's height to the natural one. Per cell it reads the *presence* (the amount up to thin spice: 2 of 7) and the
*richness* (what lies above thin: thick spice 1, blooms up to 1.25), sampled bilinearly between the cell centres:
- an envelope that is 0 where the presence drops to half - on the border between a spice cell and a bare one, so the
  cells beside a field never move (the clutter there keeps its height) - and fades out within ~24 px of the map's edge
  (the world beyond mirrors the edge and draws no spice);
- inside it a low base layer (0.6 px + 1 px x richness), soft mounds (~34 px, 0.8 + 2.6 px x richness) and wind drifts
  across the sand ripples' wind (0.8, 0.6): crests ~30 px apart, a long windward rise and a short lee slope, 0.5 +
  1.4 px x richness. Thin spice is ~2 px at most, thick ~7 px, a bloom ~8.5 px.
- the quads that carry spice relief are drawn at 4 px (`Terrain::quadWantsFine`: before, only tall relief was), so the
  drawn surface is the one `Terrain::height` returns. Units, the harvester's wheels, track marks, contact blots and
  the clumps all stand on it; picking reads the same mesh.
- *Harvesting*: the renderer's existing ~2.5 Hz spice check (`Terrain::refreshSpice`) recomputes only the changed
  cells (grouped in 8 x 8 cell blocks: two harvesters far apart are two small boxes): heights one cell round a change
  plus the foundations, normals and the baked sun visibility down-sun as far as the shadow march reaches. Only those
  vertex rows are uploaded (`Terrain::dirtyRows`, a partial upload in `Renderer3D::render`); a bloom's new relief on
  coarse quads rebuilds the index buffer (`Terrain::indexGen`). The natural height without the spice is kept per
  vertex (`hBase_`, 1 MB on a 64 x 64 map), so no guide is needed after the build.

**2. Fine relief in the shader** (`terrain3_fs`, after `reimagine()`; `spiceRelH`, MSL + HLSL): small drifts across
the wind (crests ~10 px apart) and crumbly lumps, up to ~1 px on thick spice, fading out when a world px is under ~1/3
of a screen px. Its normal is taken from three taps, and two taps towards the sun test whether the relief rises above
the sun's ray (30 degrees): those points lose up to 55 % of their light (self-shadow on the lee of each crumb). The
mesh's mounds and drifts are lit by the mesh normal and cast into the sun shadow map like any terrain.

**3. Ragged edge** (`terrain3_fs`, Reimagined only): beyond the field's noisy cover, stray specks and small patches of
spice on the sand (round spots of 7 px cells, in noise patches, where the presence is just above 0).

**4. Clumps** (`GroundClutter::placeSpice` / `refreshSpice`, `ground_clutter.*`): instanced low lumps, flat crusts and
small drifts in the spice's colour, drawn with the clutter's pipeline and shaders (no shader change).
- *Meshes and texture are procedural* (`GroundClutter::addSpicePieces`, no assets, so they also appear without the
  clutter pack): a polar grid with an irregular outline; the lump a heap of two or three lobes with every vertex a
  little up or down (two shapes, 0.34 / 0.3 of their width tall, LOD 0 / 1: 70 / 18 triangles), the crust a flat cake
  (0.13, 60 / 18), the drift a little dune long across the wind with a short lee side (0.24, 84 / 24). A 512 px grey
  grain layer (large pale grains and dark pores: a clump shows the whole layer) in the clutter's texture array, tinted
  to rust / cinnamon a little darker than the spice ground: (0.64, 0.33, 0.17) with the Realistic grade, (0.74, 0.37,
  0.16) without.
- *Placement* per cell from hashes of its coordinates (never the game's RNG): on a spice cell of amount a, ~0.8 P +
  1.7 R clusters (P presence, R richness; thin 0-1, thick 2-3) - a main lump, crust or drift with 1-4 crumbs round it -
  drifts turned with the wind, taller and darker where it is thick; on a bare sand cell next to a field, now and then a
  small stray towards the spice. Every clump stays under `kPassableMaxHeight` (units drive through spice), follows the
  ground's slope and is bedded about a third of its height in the spice (its darker foot hidden).
- *Per chunk* (8 x 8 cells): each chunk keeps its own instance list and height range. On the spice check the chunks
  round every cell whose amount changed (its strays lie on the neighbours) are placed again - a harvested cell's
  clumps get fewer and lower and are gone at 0, a bloom brings new ones. 0.03-0.06 ms per chunk.
- *Bounded*: at most 640 per chunk and 24 000 per map (2PLAY1: ~2900; Sparse halves it). Drawn from 0.6 screen px per
  world px, casting into the sun shadow map from 1.2; hidden on buildings, walls, concrete and footprints (not on spice,
  unlike the rest of the clutter); LOD 1 below 2 screen px per world px as the clutter.

**Cost** (this Mac, 1280 x 800, Debug build, Modern lighting + HDR, `D2K_GPU_TIMING=1`): GPU time was noisy because other agents' GPU jobs ran on the machine. Back-to-back runs of dev and the new build over the 2PLAY1 field gave 5.0-5.2 ms against 4.5-5.7 ms at zoom 1, and 2.8-4.2 ms against 3.6-5.8 ms at zoom 3: within the spread, no clear cost. The clutter A/B in one run (`D2K_CLUTTER_AB=1`: rocks and clumps together) measured +0.16 ms at zoom 1 (LOD 1, 1360 pieces drawn) and -0.04 ms at zoom 3 (noise). The shader's fine relief adds 5 noise evaluations on spice pixels only. The mesh gains ~53k triangles for the fine spice quads (2PLAY1: 303k -> 356k), and the terrain build takes ~90 ms longer (Debug). CPU: re-placing a chunk takes 0.03-0.06 ms; a relief refresh covers one box per changed 8 x 8 block and uploads only those vertex rows, at the 2.5 Hz check. Nothing is allocated per frame (the chunk lists keep their capacity).

**Check**: `D2K_VFX_DEMO=harvest ./build/d2k --map 2PLAY1.MAP --skirmish --graphics 3d --reimagined on --terrain3d
faithful --lighting modern --hdr on --reveal --vfx-demo --at 10 9 --zoom 2.2 --frames 300 --screenshot out.bmp`
(harvesters in a field; `--frames 2500` after harvesting; `--zoom 3 --at 8 9` close up; `--ground-detail off`: the
relief without the clumps); `D2K_VFX_DEMO=bloom` for blooms. `D2K_SPLAT_LOG=1` logs every re-placed chunk and the
relief refresh.

**Not done / known**: the clumps are lit by the clutter shader, so their foot gets its contact darkening (hidden
mostly by bedding them in); the fine relief's self-shadow is an occlusion of all light, not only the sun's; the mesh's
spice relief changes in steps of the game's amount (thick -> thin -> gone: two visible steps per cell), refreshed at
2.5 Hz; the ragged strays are drawn by the shader and the clump strays by hash, so they do not line up; the harvester
model's flat underside may clip the top of a tall mound on thick spice (its wheels follow `Terrain::height`).

### Modern effects in 3D

`Settings::effects` applies as in 2D: the session advances the same `render::Effects`
(`MapRenderer::updateEffects`, emitter culling with a ground rectangle twice the 2D view) and
`Renderer3D::addEffects` draws its primitives in world space through the bridge in
`effects.hpp` (`particles()`, `treads()`, `lights()`, `particleLook`, `texRect`...):
particles = camera-facing quads at their height above the terrain (alpha / additive / Light =
`dst * (1 + src)` pipelines; sparks stretched along their projected velocity; depth like the
sprites, flat-ground estimate), track marks = lit relief strips ("Track marks" below), ground marks (scorch, wreck hulks,
shockwave / dust rings, ruins) = terrain quads, screen shake = an offset of the look-at point after
the camera easing, the vignette / warm grade over the composite. With Modern effects the original
explosion / muzzle / projectile sprites are skipped (`Effects::replacesExplosion` / `replacesBullet`).
Classic = none of it. With 3D models, effects that leave a part of a model (the refinery's flue tops)
use the model's anchor points instead of their 2D offsets (docs/3d-models.md "Anchors").

### Ground marks on slopes (2026-11-19)

Owner report: a wrecked tank's scorch "floats in the air" beside a rock edge. Every ground mark used to be one quad
whose four corners sat on the terrain: a scorch ~50-110 px wide (2-3 tiles) spanned a slope, a bump or a cliff edge
as a single plane, so where the ground fell away under it the mark hung in the air (and where the ground rose it was
buried). `D2K_FX_MARK_LOG=1` measured up to 41 px between the ground and the corner plane next to an H5V1 spire.
Now ground marks follow the terrain: the mark's square is split into an n x n grid (cells about two terrain steps
wide, n <= 8) with heights sampled per grid point, and stays one quad where the ground under it is within 0.5 px of
its corner plane (flat ground draws as before).
- Cinematic marks (scorch / crater, shock ring, ground dust shadow): `CinematicFx::emitGroundMark` emits one
  instance per cell with its own corner heights, measured each frame at the size drawn now (a ring grows; the old
  `Part::ch` were measured at the final size); `extra.y = n * 256 + row * 16 + column` tells `fx_vs` which part of
  the mark (q range) the instance is.
- Renderer decals (`decalTex` on the terrain, `groundQuad`: Modern effects' building scorch / rubble / rings,
  crater decals, contact blots): `Renderer3D::pushGroundInst`, the same grid with sub-rects of the uv.
- Marks never climb walls: per grid point `Terrain::markFade` = smoothstep(0.55, 0.85, the normal's up component)
  x (1 - smoothstep(4, 8, ground height - the centre's height)), so a crater at a spire's foot leaves soot at the
  base only, not a blackened face. Cinematic marks pack it into the corner floats (height in 1/4 px + fade x 0.99
  as the fraction; `fx_vs` scales the alpha); renderer decals use `Inst::ka`. Scorch / craters / Modern ground
  marks fade; contact blots, unit shadows and selection rings (`decalTex`) do not. A fade inside a mark forces
  the grid even on flat ground.
- `D2K_MARK_GRID=0`: one quad per mark as before (comparison).

### Track marks (2026-11-17)

The vehicles' trails are imprints in the sand, not paint (`render3d/track_marks.*`,
`Renderer3D::addTrackMarks`; Modern effects, every 3D look):
- **Trails, not stamps.** `render::Effects` records each ground vehicle's trail as linked nodes
  (`Effects::Tread`: drawn position + hull heading, the px driven and the heading turned along the trail,
  ground softness, unit type): one every 4 px driven or ~7 degrees turned, in a ring of 8192 near the view
  (the view + 512 px). Each node points at the one before it (`prev` serial), so the 3D view joins them into
  continuous strips that bend with the drawn path (Smooth driving's curves included); a vehicle turning on
  the spot lays nodes with only the heading changed: its bands sweep arcs, drawn with the smeared "scuff"
  pattern (a scuffed ring when it turns round).
- **Per vehicle bands.** One strip per track / tyre band: from the model's running gear
  (`DriveGear::contacts`: ground wheels and track loops, model_drive.hpp; a glTF's skeleton `wheel*` nodes
  that touch the ground, not the harvester's drum), overlapping contacts merged per side, mirror pairs made
  equal, track belts 50 % wider than the links found. Without a model: a table by unit name
  (`fallbackTrackContact`). `D2K_TRACK_LOG=1` logs the bands per type. Patterns: tracks = a flat floor with
  grouser ridges (4 per period), small tyres = chevron lugs (trike / raider: three bands), wide tyres =
  rounded floor with broad diagonal lugs (harvester: its two rows of balloon tyres, MCV, quad).
- **Lit as depressions.** The relief texture (one column per pattern, one period high, mipmapped,
  repeating along the trail) holds a normal map (groove walls, grousers / lugs, a raised berm of pushed sand
  along both edges), a tone (pressed sand a little darker, the berm lighter) and a ragged coverage edge. It is
  drawn with the `kRelief` pipe: blend `dst * 2 * src`, sprite_fs mode 3 shades the normal by this frame's
  sun (passed per strip in its own frame in the vertex colour) relative to flat ground, with the terrain's
  ambient / sun split and the sun shadow map - so the wall facing the sun is lit, the other in shade, and
  the marks follow the time of day. 0.5 = the ground unchanged, so the sand's own colour shows through.
- **Where.** Ground softness per node is bilinear over the cell centres: sand / dunes / spice 1, rock and
  concrete 0. A trail fades out over a sand edge, ends on rock, and starts afresh (fading in) on the next
  sand; nothing on rock or concrete.
- **Fading.** Full for ~11 s, gone at 45 s (smooth); a trail's first node eases in; when the ring is about to
  reuse a node, the oldest fade early (many vehicles). Each node wanders a little (lateral line, width) so
  long straight runs are not ruler lines.
- 2D (Enhanced) keeps its tread stamps (the `stamp` nodes: tracked vehicles on sand, 9 s).

### Atmosphere: dust haze, vehicle dust, placement dust, aerial haze (Reimagined, 2026-11-17 / 18)

Not original, render only, Reimagined only (Original, Enhanced 2D and the plain 3D are byte-identical to before: checked
with one-frame screenshots of H5V1 against the previous build). After the *Defcon Zero* trailers (a bright desert that
looks alive). Settings, both on by default and not part of a preset (Options -> Graphics -> 3D, and in game next to
Ground detail in 3D): **Dust haze** (settings.ini `dustHaze` on|off, `--dust-haze on|off`, env `D2K_HAZE_OFF=1`) and
**Ground dust** (vehicle dust trails + placement dust ring; settings.ini `groundDust` on|off, `--ground-dust on|off`,
env `D2K_GROUND_DUST_OFF=1`; needs Modern effects, scaled by Effects intensity: Clean 0.6, Balanced 0.8, Cinematic 1).
One wind for everything: `CinematicFx::kWindX/kWindZ` (13, -6 px/s: east-north-east) - smoke and dust drift with it,
the haze veils move along it.

**Dust haze** (replaces the round-1 cloud shadows of `terrain3_fs`: Arrakis has a cloudless sky). Wind-blown dust high
up: broad soft veils, long along the wind (~950 px) and narrow across it (~250 px, meandering), drifting with the wind
at `kHazeSpeed` = 15 world px per game second (game time: frozen while paused, so screenshots repeat). Under a veil the
light is dimmed by up to ~14 % and warmed towards sand (multiplier (0.92, 0.86, 0.75) at the core, soft edges, ~40 % of
the map under some haze at any time). At night (the time of day's lamps on) half as strong.
- One procedural function of the world position + the drift, `dustHaze` (MSL `lights_msl.hpp`, HLSL top of
  `shaders.hlsl`): three octaves of value noise in wind-aligned coordinates, a smoothstep of the sum. Its parameters
  ride in the point light block every lit shader already binds (`PointLightsU::haze`: wind direction xy, px drifted,
  strength; the block grew 96 -> 112 bytes), so terrain (`terrain_fs`, `terrain2_fs`, `terrain3_fs`), rock meshes
  (`rock_fs`), ground clutter (`clutter_fs`), models (`model_fs`, `model_pbr_fs`) and billboards (`sprite_fs`, at
  the sprite's ground point) all pass under the same veil. Strength 0 (off, every non-Reimagined view, the 2D
  terrain bake) returns before any noise.
- Cost: ~16 hashes per lit fragment while on.

**Vehicle dust trails** (`CinematicFx::vehicleDust` / `trailDust`; they replace the earlier single-puff trail). For
every tracked ground vehicle (`Effects::unitVisual`: no infantry, no flyers) from its drawn position (tick
interpolation and Smooth driving's curves included): an eased speed (px/s) and heading of motion; two plumes from
the rear corners (one per track side, alternating), thrown back a little, rising, growing from ~5 to 26-34 px x speed
and drifting with the wind; they hang and settle for 2.5-3.5 s (x 0.9-1.04 with speed: ~2.3-3.6 s), thinning out over
the last ~78 % of their life (fade + edge erosion), so a trail dissolves instead of popping. Speed factor = speed / 40 px/s
(0.25 .. 1.4): faster = more puffs per px, bigger, denser (a harvester ~17 px/s leaves a thin trail, a trike
~52 px/s a thick plume). Ground softness bilinear over the cell centres like the track marks: sand / dunes / spice
full, rock a faint grey grit (x 0.2), none on concrete slabs. Not thinned by the busy budget (Effects intensity
scales it directly); the particle pool (`kMaxParticles`) and `load()` bound it.

**Ground dust colour, light and shadow** (round 2, 2026-11-18; all under Ground dust - other puffs are unchanged).
- Colour by ground (`CinematicFx::dustTint`, sampled at spawn: the trail at the vehicle's rear, the ring at each
  puff's edge point): bilinear over the cell centres of three airborne-dust albedos - spice / spice fields
  (0.94, 0.6, 0.36) orange-rust (thin spice partly sand: weight = amount / 2), sand / dunes (0.95, 0.84, 0.65) warm
  sand, rock / concrete edges (0.74, 0.68, 0.6) grey-brown grit; 12 % paler at the end of a puff's life. Lighter and a
  little less saturated than the lit ground, as dust in the air is.
- Plume lighting (`fx_fs`, a `Part::dust` weight sent as `corners.x` of a puff instance, 0 for every other puff):
  the same sun, sky and self-shadow inputs as the other puffs, but sun-weighted - `pow(N.L * 0.6 + 0.4, 1.5)` times the
  self-shadow (85 % of it at full density), less of the flat fill of thin parts (0.08 instead of 0.3), sky light
  0.5 .. 1.0 from underside to top; the sun term warmed (1.08, 1, 0.88), the sky term cooled (0.94, 0.97, 1.04). The
  sunlit top / sun side come out brighter than the ground, the underside and the side away from the sun darker and a
  little cooler, never black.
- Soft shadow: for every visible ground-dust puff `prepare()` adds one extra instance (mode 6, `kShadow`; not a stored
  particle, the pool is unchanged) drawn with the ground marks in the scene pass: a ragged soft dark blob lying on the
  terrain (corner heights like the scorch marks), where the sun ray through the puff's upper part (height + 0.3 x
  size) meets the ground - so it peeks out on the side away from the sun instead of hiding under the puff - 1.15 x the
  puff's size (wider the higher it hangs), alpha = puff alpha x 0.6 (at most 0.4), fainter with height, faded by the
  fog. Premultiplied black: it only darkens the ground. Not drawn with the sun below ~3 degrees.
- Cost: one more instance per dust puff in the same draw call (2 noise samples per pixel of a ground blob), 5 terrain
  height lookups per dust puff per frame on the CPU; no allocations (the instance array and the order list are the
  ones that already grow to the frame's size).

**Placement dust ring** (`CinematicFx::placementRings` / `placementRing`). Render-side detection, nothing in src/game:
every building slot is remembered with its `timeAdded`; a slot first seen (or holding a new building) with its
build-up just started (`kBuildup`, counter <= 12) raises a short ring: low dust rolling out of the footprint edges
(the solid cells' box), a few slower billows at the corners, a faint ground ring; it rolls out in ~1 s, then hangs and settles (ring puffs 1.8-2.8 s, billows 2.4-3.4 s, coloured by
the ground at their edge point, lit and shadowed like the trails).
The buildings standing when a world is first seen (mission start, a load, a restart) raise none. An MCV deploying is
a new construction yard, so it gets one too. With Building placement: Drop-in the landing raises its own dust
(`dropIn`), so no ring there. Check: `D2K_RING_DEMO=<s>` rings every building in view every s effect seconds, e.g.
`D2K_RING_DEMO=2 D2K_BUILDUP=0.6g D2K_BUILDUP_AT=27,12 D2K_BUILDUP_ROW="REFINERY,HEAVY FACTORY,WIND TRAP"
D2K_BUILDUP_HOUSES=1 ./build/d2k --map H5V1.MAP --graphics 3d --terrain3d faithful --models3d new --reimagined on
--effects modern --reveal --zoom 2.4 --camera-tilt 50 --at 31 14 --record-fps 30 --frames 76 --screenshot out.bmp`.
Dust trails: `D2K_FACING_DEMO="ATREIDES COMBAT TANK,TRIKE" D2K_FACING_DEMO_COLS=2 D2K_FACING_DEMO_AT=6,29
D2K_FACING_DEMO_MOVE=22,0 ./build/d2k --map 2PLAY1.MAP --skirmish --graphics 3d --terrain3d faithful --reimagined on
--effects modern --reveal --record-fps 30 --frames 120 --at 16 32 --zoom 2.2 --screenshot out.bmp` (one-frame runs
use the struct defaults: pass `--terrain3d faithful`, or the Reimagined terrain is not used).

**Aerial haze** (round 2, 2026-11-18; Reimagined only, on by default, not part of a preset: Options -> Graphics -> 3D
and the in-game options in 3D; settings.ini `aerialHaze` on|off, `--aerial-haze on|off`, env `D2K_AERIAL_OFF=1`).
Aerial perspective by view distance (not screen y, so zoom / tilt / yaw behave), and a gentle day bloom.
- Haze: the fog term every 3D shader already applies (`mix(colour, fogColor, smoothstep(start, end, distance to the
  eye))` in terrain_fs / terrain2_fs / terrain3_fs / rock_fs / clutter_fs / model_fs / model_pbr_fs / sprite_fs and
  the cinematic particles) is re-aimed while on (`Renderer3D::render`, after the camera update): start = 0.8 x the
  camera distance D (the default tilt's bottom edge is ~0.87 D, its top ~1.27 D), end = 2 D, and capped: the shaders
  scale the fog factor by `aerialK = 1 - PointLightsU::info.w` (the particles: `1 - fog2.y`), info.w = 1 - cap, cap
  = 0.5 by day (`kAerialCap`), x (1 - 0.55 x night). Result: nothing at the near edge, ~3-5 % mid screen, ~15 % at the
  far edge of the default view, more the further a view is tilted (top of a 60 degree view ~25-35 %), never more than
  50 %. Colour: a pale warm desert sky (0.9, 0.81, 0.69) x (0.5 + 0.5 x the sun's colour) x the time of day's fog tint
  (dawn / dusk / night tint it; night = dark blue and weak). Replaces the Reimagined fog (1.35 D .. 6 D towards the
  dim brown kFog) while on. info.w = 0 / fog2.y = 0 everywhere else, so the fog is bit-identical there (x 1.0).
- Bloom: the existing `render::Bloom` (quarter / eighth resolution, 4-level tent blur chain, ~12 small draws) at
  strength max(Modern lighting's, 0.24 x day) and threshold min(Modern lighting's, 0.8 by day) (`kAerialBloom*`):
  a soft glow on the brightest sand and highlights. With Classic lighting the session now creates the Bloom for it
  too (not on the lite profile). Night keeps the Modern lighting's own (stronger) bloom.
- Cost: no extra shader work for the haze (one multiply per fragment); the bloom pass when it was not on already.
- Check: `./build/d2k --map 2PLAY1.MAP --skirmish --graphics 3d --reimagined on --terrain3d faithful --reveal
  --camera-tilt 60 --at 32 36 --frames 30 --screenshot on.bmp` and the same with `--aerial-haze off`.

### Dynamic lights

The effects' light list (`Effects::lights()`: explosions and their flashes, muzzle flashes,
projectiles, fires, burning wrecks; docs/enhanced-graphics.md "Light list") and the lamps of the time of
day (`render::SceneLights`: headlight cones as spot lights, work lamps, beacons' pools, building lamps,
`fx_light*` anchors of models) become real lights. Effect lights sit 8 px + 6 % of their reach above their
point (so the ground right under it is lit from above), radius = 0.55 x reach, colour x intensity x 1.35
(matches the 2D glow on flat ground); a headlight cone is a spot light from the lamp tilted to reach the
ground half way along its beam (radius 1.1 x its length, the beam fading over the outer 45 % of its cone),
a pool a point light above its point. Each light adds `colour * (1 - d²/r²)² * wrapped N·L` to the sun term
(terrain wrap 0.3, models / rocks 0.25, billboards 0.8 with an up normal, so units, buildings and smoke are
lit too) before fog and shroud.

**Clustered forward lighting** (2026-11-14; the owner, 5K ultrawide: "not all units use lights when there are
a lot in one frame, and when I move the view the lights switch off on the sides of the screen"). The lights
used to be a uniform block of 48 that every lit fragment looped over, so the lamps were capped at 20 at ~1 Mpx
and 10 at 4 Mpx and picked nearest the view centre - the rest of a big army, and every lamp towards the sides
of a wide view, went dark and popped on / off while panning. Now (`render3d/lights.hpp`):
- `Renderer3D::buildPointLights` takes every light whose reach touches the view and is not under the shroud.
  Within the budget (`kMaxGridLights` = 768 desktop, 192 phones / tablets; `D2K_MAX_LIGHTS` overrides for
  tests) all of them are drawn. Past it the ones covering most of the view win - score = priority x intensity
  x sqrt(screen coverage of the light's sphere, clipped to the view) x reach, effects x 4 - so the choice
  spans the whole view, not the distance from its centre; lamps the budget takes or drops **fade in / out
  over 0.3 s** (`Lamp::id` = object kind / side / index + the lamp's number; a lamp first seen starts at its
  target, so nothing fades where nothing popped). Headlight beams of units driving together are merged into
  one beam per cluster only far past the budget (more than twice it).
- `LightGrid::build` bins them into a grid of square cells on the ground (world x / z, 32 px = one map cell,
  larger on huge views: at most 128 x 128 cells) over the lights' footprints - a point light's sphere, a
  beam's cone (a circle around its middle that holds the cone, about half the sphere) - in score order, at
  most `kMaxLightsPerCell` = 40 per cell (20 on phones; the first, most important ones stay).
- One storage buffer (uint4: the lights, 3 x uint4 each; one uint per cell = first index << 8 | count; the
  index lists) is uploaded every frame in its own copy pass (`render3d/light_buffer.hpp`, cycled) and bound
  as **fragment storage buffer 0** of `terrain_fs`, `terrain2_fs`, `terrain3_fs`, `rock_fs`, `model_fs` and
  `sprite_fs` (MSL `[[buffer(<uniform count>)]]` = 2, terrain3 3; HLSL `t<sampler count>, space2` = t3 / t4 /
  t7 / t8, one `StructuredBuffer<uint4>` per count with the lookup instantiated per buffer by a macro; SPIR-V
  set 2, binding = sampler count). The 2D terrain bake and model passes without a grid bind a 64-byte
  stand-in (`count` 0: nothing is read).
- The uniform block (`PointLightsU`, fragment uniform buffer 1, 96 bytes) keeps the frame's light (sun
  colour, sky colour, billboard tint, specular factor, saturation; `sunAmb`, `satur`) and the grid's
  placement; a fragment finds its cell from its world position and loops over that cell's lights only.
  `D2K_LIGHT_LOG=1` logs candidates / drawn / grid size / the fullest cell every 60 frames.

Cost (M4 Pro, LG 5120 x 1924, 3D resolution 100 %, a H5V1 army of ~330 tanks at night: 237 lights in view):
16.5 ms without lights, 17.9 ms with all 237 (+1.4 ms); the old block with its 10 lamps was 19.0 ms. Numbers for
the owner's saves: "Performance (lights)" below.

**Time of day / Modern lighting** (docs/enhanced-graphics.md "Lighting"): the renderer turns its sun per frame
from the style's base sun (`baseSun_`), so the shadow map follows the time of day. The lamps' flares are
additive billboards. Bloom over the composite (`render::Bloom`). The lamps' list (`SceneLights::update`) is
built for the camera's whole ground footprint (the 3D view rectangle grown to the footprint's corners and
edge middles, last frame's camera), so the lamps of a wide or tilted view's sides are there too.

### Drop-in build-up (Building placement: Drop-in, 2026-11-15)

Not original, render only, Graphics: 3D with building models (ROADMAP §4R second reference: buildings that
drop in and unfold with a dust cloud). Settings "Building placement" (Options -> Graphics -> 3D; settings.ini
`buildingPlacement` rise|dropin, `--building-placement`): **Rise from rock** = the cut plane rising through the
model (docs/enhanced-graphics.md "Build-up"), **Drop-in** = the default of the Enhanced / 3D presets (Original:
Rise; not part of the preset match; struct default Rise, so one-frame runs stay as before). Since 2026-11-16 the
presets use Assemble instead (next section); Drop-in stays selectable.

Driven by the original's build-up counter exactly like Rise (`render::buildupProgress`, smoothed between ticks):
the same duration, a pose that is a pure function of the counter (selling plays it backwards: the lamps go
out, it folds and lifts off). Timeline over the progress p (`render/building_dropin.hpp`):

| p | what |
|---|------|
| 0 .. 0.24 | falls folded, accelerating, from 110 px + its height; a soft shadow grows under it (Effects ground mark) |
| 0.24 | lands: a short squash of the folded box; dust ring rolling out of the footprint, billows at the edges, pebbles (each kicks a puff where it lands), a ring of pushed sand, a darker patch that fades over 3.2 s; the thud = S_BUILDCONC (the original's concrete placement sound), positional, one per frame, only landings in view |
| 0.31 .. 0.92 | unfolds: `model_vs` cuts the model into four height bands, folded to 28 % each, extending in turn from the lowest (ease-out-back overshoot) - a telescoping unfold; the footprint spreads from 90 % to full size in the second half |
| 0.84 .. 1 | the lamps (`fx_light` anchors, scene lights) and the emissive surfaces fade in |

- The construction yard (an MCV deploying where it stands) does not fall: it unfolds from the ground with a
  lighter puff. Walls and concrete slabs keep the Rise path; buildings without a model keep their billboard build-up.
- The models have no articulated parts (the glTF nodes are `base`, `main`, `glow` and `fx_*` anchors), so the
  unfold is generic (height bands in the vertex shader), not per-node. Per-node animation (towers, cranes) needs
  named sub-nodes in the packs first.
- Code: `render/building_dropin.*` (the pose, `DropInSource`), `ModelRenderer::buildingPlacement` (lift, squash,
  spread - so anchors follow), `ModelRenderer::collect` (unfold uniform = `skin.y/z` of the model vertex block;
  MSL `unfoldY` / HLSL `UnfoldY`), `render/effects_dropin.cpp` (dust, marks, `Effects::dropInImpacts()`),
  `SceneLights::setDropIn` (lamp fade), `Session::playDropInSounds`.
- Hook for the Reimagined cinematic particles: `Effects::setDropInDust(false)` drops the built-in burst; the
  landings are listed every update in `Effects::dropInImpacts()` (footprint, cell, sand colour).
- Checking it: `D2K_BUILDUP=0g D2K_BUILDUP_AT=27,12 D2K_BUILDUP_ROW="REFINERY,HEAVY FACTORY" D2K_BUILDUP_HOUSES=1
  ./build/d2k --map H5V1.MAP --graphics 3d --models3d new --effects modern --reveal --zoom 3.2 --camera-tilt 50
  --at 30 14 --building-placement dropin --record DIR --frames 120` (the gallery of the `D2K_BUILDUP` check, placed
  at a cell, with its own row of buildings), `D2K_BUILDUP=<p>` for one pose.
- The 3D picture with Rise is not bit-identical to before: the changed model vertex shader compiles a little
  differently (a few hundred shadow-edge pixels differ); Original / Enhanced screenshots are byte-identical.

### Assemble build-up (Building placement: Assemble, 2026-11-16)

Not original, render only, Graphics: 3D with building models (the art pack's and, with Reimagined, the
`models-reimagined` ones). Owner: Drop-in "doesn't look good" - buildings should be built up from the ground
part by part like in *Defcon Zero* (reference analysis: ~/Desktop/d2k-compare/building-assemble/README.md).
Settings "Building placement" Rise from rock / Drop-in / **Assemble** (settings.ini `buildingPlacement`
rise|dropin|assemble, `--building-placement`); Assemble is the Enhanced / 3D presets' default (Original: Rise;
not part of the preset match). A settings.ini from before that names the Enhanced / 3D preset and still has the
old preset default `dropin` (no `buildingPlacementRev=2` marker, written since) moves to Assemble.

Driven by the original's build-up counter like Rise / Drop-in (`render::buildupProgress`, smoothed between
ticks): the duration is exact, the pose a pure function of the counter; selling plays it backwards.

- **Parts** (`render3d/model_assembly.*`, once per building model at upload): the packs' models are one mesh,
  but made of 100-300 separate pieces (triangle islands sharing no corner). Corners are welded (1/64 px,
  union-find), pieces smaller than 7 % of the model (or < 8 triangles) go into the piece that holds them, then
  the smallest groups merge into their nearest neighbour until sqrt(triangles) / 7 (6..22) parts remain
  (~16 per model in opend2k-redesign). A model of one piece falls back to 3-6 height layers that extrude in turn
  (a rising cut with a glowing edge). Each part gets a motion: on the ground and big / central -> **Extrude**
  (grows out of its foot behind a glowing cut), small outer ground pieces -> **Slide** out of the core, thin tall
  ones -> **Hinge** (swing up from lying outward), raised wide / flat ones -> **Lower** (lowered in with a small
  bounce), raised towers -> **Telescope** (rise out of the roof, a hot ring where they come out). Order:
  bottom-up (foot height + a quarter of the height), side pieces later; windows overlap (2.6 / n of the span),
  a part never starts before what it stands on has nearly arrived; all fitted into progress 0.05 .. 0.90.
- **Vertex data**: the model vertex grew by 20 bytes (attribute 8 = the part's foot xyz + parameter, attribute 9
  = UBYTE4_NORM t0, duration, motion, direction; 0 = no part, units). Other meshes of the model (house, turret,
  pose variants, the Reimagined `_lod1`) take the part whose box holds each triangle.
- **Shaders**: `model_vs` / `model_shadow_vs` (MSL `assemble`, HLSL `Assemble`; skin.w = 1 + progress, 0 = off -
  the plain path is unchanged): a part before its window collapses onto its foot (not drawn), moves during it,
  stands after it; the varyings pbr.z / pbr.w carry the cut-edge distance and the seat weld glow, which
  `model_fs` / `model_pbr_fs` add to the build-up band glow (orange, 55 % on the cut caps). The sun shadow map
  and the projected shadows move with the parts.
- **Effects** (`render/effects_assemble.cpp`, Modern effects; `render::AssemblySource` = the model renderer,
  `Effects::setAssembly`): a glowing amber survey outline with bright corners on the footprint (fades by 0.24),
  dust rising along the footprint when the work starts and drifting round the base while it goes on, a darker
  worked-ground patch; sparks along the rising cuts / telescope rings; where each part seats a weld flash
  (light), a spark burst and (ground pieces) dust. The lamps (`fx_light`, emission) fade in from 0.84 (the
  scene lights read it through `DropInSource::buildingDropIn`).
- **No animation for buildings that already stand**: the model renderer settles every building in build-up when
  it first sees a world and after `ModelRenderer::settle()` (Session: a new world, a loaded save), so a mission's
  start and a save load show them finished (Rise / Drop-in unchanged); `D2K_BUILDUP` checks are exempt.
- Walls and concrete keep the Rise cut; buildings without a model keep their billboard build-up; Enhanced 2D is
  unchanged.
- Checking it: `D2K_BUILDUP=0g D2K_BUILDUP_AT=27,12 D2K_BUILDUP_ROW="REFINERY,HEAVY FACTORY,WIND TRAP"
  D2K_BUILDUP_HOUSES=3 ./build/d2k --map H5V1.MAP --graphics 3d --models3d new --effects modern --reveal
  --zoom 2.4 --camera-tilt 50 --at 31 18 --building-placement assemble --record DIR --frames 150`
  (+ `--reimagined on` for the Reimagined models); `D2K_BUILDUP=<p>g` for one pose.

### Smooth driving in 3D (2026-11-15)

With Smooth driving on (Options → Graphics → Effects row, on in the Enhanced / 3D presets; docs/STATUS.md
"Smooth driving", `render/smooth_drive.*`) the unit models get the same drawn path and facings as the 2D
sprites (`ModelRenderer::drawnFacing` prefers `SmoothDrive::out`, also with Classic effects; the Effects'
`UnitVisual` facings are replaced too, so muzzles / headlights / cinematic effects follow). 3D-only extras:
- **Wheels**: glTF nodes named `wheel_*`, `sprocket_*`, `idler_*` (the built-in `new` models: combat tanks,
  trike, harvester) turn about their local x axis (the axle) by the distance driven / their radius
  (`ModelRenderer::wheelPalette`: the farthest vertex bound to the node from the axle; the radius and the
  node's rest matrix are found once per model). The unit gets its own skinning palette (identity for every
  other node; no cache, appended to the frame palette, no allocation once it has grown). An animated pose
  (a docking harvester) keeps its own palette. Tracks are geometry without a texture: nothing to scroll.
- **Rock bob**: Vehicle animation: Modern adds a low suspension bob (≤ 0.35 px lift, ≤ 0.7° pitch, two
  waves of the distance driven) while driving over rock - 3D only (`VehicleMotion::update(..., bumps)`);
  2D would draw it as whole-pixel hops.
- **Slopes** were already followed (`unitPlacement`: the terrain normal from ±8 px samples).

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
with the yaw, so every map edge stays reachable at any yaw (yaw 0 = the 2D clamp; with Reimagined's World beyond the map on, 5 cells past each edge -
"World beyond the map"). `Camera3D` eases towards that pose (~60–90 ms time constants; snapped on
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
heightfield + bisection). **Picking**: the object drawn under the cursor wins, nearest first along the
ray; the ground point only when nothing is hit (or a ridge is nearer). Building models: ray against
their triangles (rest pose, through this frame's body / turret placement; `ModelHook::rayBuilding`,
per-model boxes of 32-triangle runs keep it cheap) - a hit returns the centre of the building's
footprint cell nearest the hit (cells without a unit on them first), so a click on any visible part
of the model - a tower or chimney rising over the ground behind it, an overhang - targets the
building exactly as a click on its cells in 2D (attack, dock, repair, select, sell, cursor). Units:
their model's box (`ModelHook::rayUnit`), else a screen radius around their ground point; returns the
unit's exact position. Before this a tall model's upper part picked the ground behind it: Ctrl-click
gave an attack-ground order on an empty cell (the red ring; the units do fire there, as the original's
AttackTile 0x496ff0 - `testForceAttackGround`), a plain click a move. `D2K_PICK_LOG=1` logs every pick
and the order a battlefield click gave. Selection boxes test the units' projected positions against the screen rectangle. So
selection, orders, Ctrl-attack, repair/sell/target modes, building placement, cheat spawning,
rally clicks and the cursor choice all work as in 2D.

Command line (reproducible shots): `--graphics 3d --camera-tilt DEG --camera-yaw DEG` (clamped as
above) with the usual `--map/--at/--zoom/--ticks/--reveal/--screenshot/--frames`.

## Performance (lights)

Clustered lights (2026-11-14, "Dynamic lights" above), M4 Pro, VSync off, windowed, the owner's 3D settings,
p50 frame time (fps); before = the 48-light block with 10 lamp slots at 5K / ~11 on the built-in display:

| scene (3D resolution) | lights in view after | before | after |
|---|---|---|---|
| LG 5120x1924, SAVE4 night (100 %) | ~58 | 13.37 ms (75) | 13.39 ms (75) |
| LG 5120x1924, SAVE4 dusk (100 %) | | 13.39 ms (75) | 13.44 ms (74) |
| LG 5120x1924, SAVE5 night (100 %) | | 10.40 ms (96) | 10.09 ms (99) |
| LG 5120x1924, SAVE5 dusk (100 %) | | 10.45 ms (96) | 10.07 ms (99) |
| built-in 3456x2100, SAVE4 night (Auto) | | 9.02 ms (111) | 9.23 ms (108) |
| built-in 3456x2100, SAVE5 night (Auto) | | 6.07 ms (165) | 5.94 ms (168) |
| LG 5120x1924, H5V1 + ~330 tanks night (100 %) | 237 | 19.05 ms (52) | 17.86 ms (56); no lights 16.45 |

Every lamp in view is lit now at the same or lower cost (the grid visits ~0-13 lights per pixel where the old
block looped over all of them). The rest of the army scene's frame is its 330 tank models. Scripts, logs and
pictures: ~/Desktop/d2k-compare/many-lights/README.md.

## Performance (2026-11-14: 5K ultrawide, the owner's saves)

Owner: "in 3D on this Mac it works slowly" (M4 Pro; LG UltraWide 100 Hz, 5120x1924 px at Render resolution
Full; built-in 120 Hz XDR; the 3D preset, Quality High). Measured with `D2K_FRAME_LOG` (+ `D2K_DISPLAY`,
`D2K_QUIT_AFTER`), VSync off, windowed, Xcode's Metal System Trace (`xctrace record --template 'Metal System
Trace' --attach PID`) for the GPU split; numbers and the scripts' description: ~/Desktop/d2k-compare/3d-perf/
README.md. The GPU was the whole cost (CPU 1-2 ms a frame). What it was and what changed:

- **Rocks drawn without culling** (the faithful terrain's ~1000 loose stones, ~1.4k triangles each, twice a
  frame: shadow map + scene, ~6 ms of vertex work). `DesignedTerrain::cullRocks`: per frame the instances in
  the camera's frustum and in the sun's (the shadow map's) are copied into two small instance buffers
  (`viewRocks()` / `shadowRocks()`); the full set only until the first upload. -8 ms on the 5K view.
- **terrain3_fs** (the faithful terrain's fragment shader, by far the biggest per-pixel cost): skips work whose
  result is known to be zero - the spice field (9 taps + 15 noises + the mound spots) where no spice lies
  within reach, the slope / rim guide taps (6 bicubic reads = 30 taps) where no relief lies within reach (both
  as flags per cell in the spice texture's B / A channels, `DesignedTerrain::buildSpiceTex`, dilated by the
  taps' reach), and (MSL) whole SIMD groups under full shroud / off the map (`simd_all`). Picture identical
  (screenshot diffs: only moving units differ). -7 ms on the 5K view.
- **Model textures had no mipmaps** (1-2k textures on units a few dozen pixels big: cache misses, shimmer).
  Now a full mip chain + trilinear sampling; opaque models also draw through a pipeline without blending
  (`meshOpaquePipe_`: blended fragments are never hidden-surface-removed on Apple GPUs). A busy base with a
  few dozen tanks: 21.0 -> 13.9 ms at 100 %.
- Indexed model meshes (equal corners shared at upload: 3.9 M -> 1.2 M vertices for the redesign pack).
- **3D resolution** (Options -> Graphics -> Quality, `resolution3d` auto|100|75|50, `--resolution3d`; not part
  of Quality): the 3D pass at a share of the view's pixels per axis, scaled up into the view (the UI and the
  overlays stay sharp). The targets are allocated at the largest share and a smaller one renders into their
  top-left part (viewport), so the share can change every frame without a hitch. **Auto**
  (`platform/resolution3d.hpp`): 100 % up to 6 MP (MSAA 4x; 7.5 at 2x, 9 without), so a 5120x1924 view starts
  at 75 %; then while the frames are GPU-bound and slower than the display rate it steps down 5 % at a time
  (to 60 %), and after steady frames probes a step back up (waiting longer after each failed probe).
  One-frame runs keep the cap (reproducible). Render resolution (the whole window: Full / Half) is untouched.
- Quality High stays the 3D preset's default: MSAA 2x saves ~15 % at the same resolution, but Auto's lower
  share costs less in sharpness than the jaggies of 2x on the models.

Motion (owner: "tank movements not smooth"): the unit models now turn by the eased facing of the Modern
effects (`Effects::unitVisual`, as the 2D sprites, headlights and muzzles do; `ModelHook::setEffects`) instead
of the 32 simulation steps; positions were already interpolated between ticks (render/motion.*). Vehicle
animation: no idling tremble and no random bumps (see docs/enhanced-graphics.md "Vehicle animation").

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

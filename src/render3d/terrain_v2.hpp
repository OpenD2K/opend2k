#pragma once
// "Terrain v2": the designed terrain of Graphics: 3D (Settings "3D terrain: Designed";
// docs/3d-mode.md "Terrain v2"). Replaces the stretched original 32 px tiles with:
//
//   - per-cell MATERIALS from the cell flags (sand / dunes / rock / cliff / rough rock / spice /
//     concrete) -> splat textures (8 texels per cell, noisy blurred boundaries) blending tiling
//     CC0 PBR textures (ambientCG; albedo + normal + height), height-blended, anti-tiled,
//     triplanar on steep rock, COLOUR-LOCKED to the map's own tiles (each material is tinted so
//     its mean matches the mean colour of the original tiles of that class: the tileset's mood
//     and palette are kept);
//   - spice as a per-cell amount texture (follows harvesting), drawn as grains over the sand;
//   - MODELLED CLIFFS: instanced rock-scan meshes (Poly Haven, CC0, decimated; assets/terrain/
//     rocks) placed along the impassable rock bands with random variant / yaw / scale, reaching
//     from the sand up to the plateau; small stones on rough (infantry-only) rock.
//
// The heightfield itself is render3d/terrain.* built with designed = true (finer grid + noise).
// Assets: assets/terrain/ (tools/fetch_terrain_assets.py downloads the textures; the rock meshes
// are checked in). Without them the option falls back to the original tiles. Render-only.
//
// TerrainStyle::Faithful ("Designed (faithful)", Terrain v3; docs/3d-mode.md "Terrain v3"): the
// same materials laid out after the ORIGINAL tiles - a guide read from the map's own tile pixels
// at run time (render3d/terrain_guide.*) gives the rock outline, the cliffs' relief and where
// rock is broken; cliffs are relief (terrain3_fs) instead of boulder meshes - only on cells no
// vehicle can use -, the boulders mark the blocked rock that does not stand as a cliff, concrete
// is the original's 32 x 16 slabs. The Designed style is untouched by it (its own shader, its
// own code paths).

#include "render3d/light_buffer.hpp"
#include "render3d/lights.hpp"
#include "render3d/math3d.hpp"
#include "render3d/terrain_guide.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace d2k::game {
class World;
}
namespace d2k::render {
class TileAtlas;
}

namespace d2k::render3d {

class Terrain;

// Settings "3D terrain" / "Terrain": Designed, or Designed (faithful) (see above).
enum class TerrainStyle { Designed, Faithful };

class PhotoTerrain;

class DesignedTerrain {
public:
    enum Material { kSand, kDunes, kRock, kCliff, kRough, kSpice, kConcrete, kMaterials };
    // Texture layers: the materials, then the faithful style's crag rock (jagged cliff rock for
    // the cliffs and broken rock; assets/terrain/materials/crag, optional - without it the layer
    // holds the cliff material). The Designed style never samples it.
    static constexpr int kCragLayer = kMaterials;
    static constexpr int kLayers = kMaterials + 1;
    static constexpr int kSplatRes = 8;  // splat texels per cell edge
    static constexpr int kRockTypes = 6;

    // assets/terrain (next to the executable / the bundle's Resources / the working directory),
    // "" when its material textures are missing.
    static std::string assetsDir();

    // nullptr (with a log line) when the assets or the pipelines are missing.
    // aniso: anisotropic filtering of the material textures (1 = off).
    static std::unique_ptr<DesignedTerrain> create(SDL_GPUDevice* dev, SDL_GPUTextureFormat color,
                                                   SDL_GPUTextureFormat depth, SDL_GPUSampleCount samples, int aniso = 8);
    ~DesignedTerrain();
    DesignedTerrain(const DesignedTerrain&) = delete;
    DesignedTerrain& operator=(const DesignedTerrain&) = delete;

    // Graphics: Enhanced bakes it from straight above (render3d/terrain_bake.*): darker crags and
    // more rocks on the ridges (the tops are all that shows). Before build().
    void setTopDown(bool on) { topDown_ = on; }
    // Designed or Designed (faithful). Before prepareGuide() / build().
    void setStyle(TerrainStyle s) { style_ = s; }
    TerrainStyle style() const { return style_; }
    // Faithful: reads the layout guide from the map's tiles - call it before Terrain::build,
    // which takes its relief from the guide. nullptr for Designed, or when the tiles cannot be
    // classified (the map then gets the Designed layout). reimagined (3D): the guide's heights for the
    // Reimagined relief (TerrainGuide::Params::reimagined; Terrain::setReimagined must match).
    const TerrainGuide* prepareGuide(const game::World& w, const render::TileAtlas* atlas, bool reimagined = false);
    // The faithful layout is in use (the style is Faithful and the guide could be built).
    bool faithfulActive() const { return style_ == TerrainStyle::Faithful && guide_.valid(); }
    // The sun of the faithful top-down bake: the original tiles are lit from the south-west.
    // The shadows are cast by a lower sun from the same side (longer, like the original's).
    static Vec3 faithfulBakeSun();
    static Vec3 faithfulBakeShadowSun();
    // Graphics: 3D with the faithful terrain: the whole scene (terrain, shadow map, models) is
    // lit from the south-west like the original tiles, so the cliffs' faces match the 2D bake.
    static Vec3 faithfulSun3D() { return faithfulBakeSun(); }
    // "Reimagined" (docs/3d-mode.md): the same side, lower - long cliff shadows.
    static Vec3 reimaginedSun3D();
    // A new map / a rebuilt heightfield (t built with designed = true). tileRgb: mean colour
    // (0..255) of every tile of the map's tileset, 3 floats each (the colour lock).
    void build(const game::World& w, const Terrain& t, const std::vector<float>& tileRgb);
    // Spice amounts and concrete changed? (cheap; uploads follow in prepare). changed: gets the
    // indices (y * width + x) of the cells whose look changed.
    void update(const game::World& w, std::vector<int>* changed = nullptr);
    // "Realistic grade" (docs/3d-mode.md; Reimagined): ground occlusion. On: the splat's spare channels carry the
    // buildings' / walls' cells and the concrete's coverage, both blurred (B.b, B.a; terrain3_fs darkens the sky
    // light round a building's foot and lays sand over the slabs' edges). Off (the default): both stay 0, the
    // splat exactly as before. Returns true when it changed: call update() to rebuild the splat.
    bool setGroundOcclusion(bool on);
    // "Photo terrain" (docs/3d-mode.md "Photo terrain"; render3d/photo_terrain.*): loads the pack on the first
    // call; false when it is not installed (the procedural ground stays).
    bool ensurePhoto();
    // The sand's colour (0..1) in the Reimagined palette (terrain3_fs reimagine), vivid or graded: the dust on
    // the clutter and on the models.
    void reimaginedSand(bool graded, float out[3]) const;
    // Mean colour (0..255, 3 floats per tile) of every tile of an atlas: build()'s tileRgb.
    static std::vector<float> tileMeans(const render::TileAtlas* atlas);
    // Uploads (its own copy pass) and mip generation, before the render pass.
    void prepare(SDL_GPUCommandBuffer* cmd);

    struct Frame {
        Mat4 viewProj;
        Vec3 eye, sun;
        float ambient = 0.62f, diffuse = 0.55f, darkest = 0.42f;
        float fogColor[3] = {}, fogStart = 0, fogEnd = 0;
        float mapW = 0, mapH = 0;
        bool shroudOn = true;
        bool bake = false;  // baked for Graphics: Enhanced (render3d/terrain_bake.*): no shroud, no edge fade
        SDL_GPUTexture* shroud = nullptr;
        SDL_GPUSampler* linearClamp = nullptr;
        // Sun shadow map (Renderer3D): shadow = (on, texel, -, depth bias).
        Mat4 lightVP;
        SDL_GPUTexture* shadowMap = nullptr;
        SDL_GPUSampler* shadowSampler = nullptr;
        float shadow[4] = {};
        // Dynamic lights (render3d/lights.hpp); nullptr = none (the 2D bake). lightGrid: fragment storage buffer 0
        // (nullptr = a stand-in).
        const PointLightsU* lights = nullptr;
        SDL_GPUBuffer* lightGrid = nullptr;
        // Faithful only. The faithful 2D bake lights its relief (cliffs, broken rock) with `sun`
        // (the original tiles' own light, from the WSW) but the level ground's fine detail (sand
        // ripples, plain rock) with detailSun, the Designed bake's sun, so sand and buildable
        // rock look exactly as in Designed. detailSun.y <= 0: everything is lit by `sun` (3D).
        Vec3 detailSun{0, 0, 0};
        float reliefAmbient = 0, reliefDiffuse = 0;  // the relief's light (0 = ambient / diffuse)
        // "Reimagined" (faithful only; docs/3d-mode.md): the procedural detail, taller relief (time = effect
        // seconds). (The drifting dust haze is in the point light block: PointLightsU::haze.)
        bool reimagined = false;
        float time = 0;
        // "Realistic grade" (docs/3d-mode.md; with reimagined): the calmer palette, rust spice, ground occlusion
        // and sand over the slabs' edges (F3U::reim[3].w); the stones and the clutter follow the palette.
        bool naturalGrade = false;
        // "Photo terrain" (docs/3d-mode.md; with reimagined, and only when ensurePhoto() is true): the scans of
        // the photo terrain pack instead of the procedural detail.
        bool photo = false;
        // "Ambient occlusion" (docs/3d-mode.md; render3d/ssao.*): the screen-space occlusion texture the terrain
        // and the rocks scale their sky light by (its switch and scale: PointLightsU::ssao). nullptr = none.
        SDL_GPUTexture* ssao = nullptr;
        SDL_GPUSampler* ssaoSampler = nullptr;
        // "World beyond the map" (Reimagined; docs/3d-mode.md, render3d/surround.*): 0 off; 1 = on, the
        // map's own draw (no edge fade, the faint edge line; its textures through the mirror sampler so
        // the edge filters as the land beyond does); 2 = the land beyond (the ring's mesh: the map's
        // textures mirrored across the edge, blended into the desert of surroundFar; no rocks).
        int surround = 0;
        float surroundMargin = 0;                 // the ring's width (world px)
        SDL_GPUTexture* surroundFar = nullptr;    // Surround::texels()
        SDL_GPUSampler* linearMirror = nullptr;   // linear, mirrored repeat, no mips
    };
    // One rock instance (the instance vertex layout of rock_vs).
    struct RockInst {
        float pos[3], yaw;
        float scale[3], layer;
        float sun, ground, top, bright;
    };
    // A subset of the rocks in another instance buffer (same layout): per rock type the first
    // instance and the count (render3d/terrain_bake.*: only the rocks near the baked region).
    struct RockSet {
        SDL_GPUBuffer* vb = nullptr;
        std::uint32_t first[kRockTypes] = {}, count[kRockTypes] = {};
    };
    // The terrain mesh (Renderer3D's buffers, TerrainVertex layout; indices [firstIndex,
    // firstIndex + indexCount)) with the materials, then the rocks (all, or `rocks`). Returns the
    // draw calls.
    int draw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, SDL_GPUBuffer* vb, SDL_GPUBuffer* ib,
             std::uint32_t indexCount, const Frame& f, std::uint32_t firstIndex = 0, const RockSet* rocks = nullptr);
    // fog2.w of the terrain / rock / clutter fragment blocks: 0 no shroud, 1 shroud, 2 no shroud and no edge
    // fade (the 2D bake), 3 shroud without the edge fade (the world beyond the map is drawn there).
    static float surroundFog2(const Frame& f);
    // The rocks as casters in the sun shadow map's depth pass (format: the shadow map's). depthBias false: a
    // depth seen from the camera (the ambient occlusion's pre-pass) - without the shadow map's slope bias.
    int drawShadow(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Mat4& lightVP, SDL_GPUTextureFormat fmt,
                   const RockSet* rocks = nullptr, bool depthBias = true);
    // Per frame, before prepare() (docs/3d-mode.md "Performance"): the rock instances inside the
    // view (viewProj) and inside the sun's view (lightVP; nullptr = no shadow pass) - each set is
    // uploaded by prepare() and drawn instead of all of them (a 128 x 128 map has ~1000 stones of
    // ~1.4k triangles: drawing them all twice a frame was ~6 ms of vertex work on an M4 Pro).
    void cullRocks(const Mat4& viewProj, const Mat4* lightVP);
    const RockSet* viewRocks() const { return culled_ ? &viewSet_ : nullptr; }
    const RockSet* shadowRocks() const { return culledShadow_ ? &shadowSet_ : nullptr; }
    int rockCount() const { return int(inst_.size()); }
    const std::vector<RockInst>& rocks() const { return inst_; }  // sorted by type (layer)
    // The map's colour (0..255, the colour lock's target) of a Material (render3d/ground_clutter.* takes its palette).
    const float* materialColour(int m) const { return colour_[std::clamp(m, 0, kMaterials - 1)]; }
    // Largest horizontal reach of a rock from its position (world px), for culling.
    static constexpr float kRockReach = 40.0f;

private:
    explicit DesignedTerrain(SDL_GPUDevice* dev);
    bool init(SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth, SDL_GPUSampleCount samples);
    bool loadAssets();
    // Splat texels of the cells [x0, x1] x [y0, y1] (inclusive; neighbours as far as the blur reaches).
    void buildSplat(int x0, int y0, int x1, int y1);
    void placeRocks(const game::World& w, const Terrain& t);
    void placeStones(const Terrain& t);  // faithful: the guide's boulders (blocked rock that is no cliff)
    void computeTints(const game::World& w, const std::vector<float>& tileRgb);
    void faithfulLook(const std::vector<float>& tileRgb);  // faithful: colour targets + the F3U block
    bool ensureFaithPipeline();


    SDL_GPUDevice* dev_;
    // CPU data waiting for the first upload.
    static constexpr int kTex = 1024;
    std::vector<std::uint32_t> albedoCpu_[kLayers], normalCpu_[kLayers], rockTexCpu_[kRockTypes];
    std::vector<float> rockVerts_;  // pos3 nrm3 uv2 per vertex, triangle lists of all types
    std::uint32_t rockFirst_[kRockTypes] = {}, rockCount_[kRockTypes] = {};
    float rockHeight_[kRockTypes] = {};  // mesh height (px) at scale 1 (1 unit = 32 px wide)
    bool rockLongZ_[kRockTypes] = {};    // the mesh is longer along z than along x
    float texMean_[kLayers][3] = {}, rockMean_[kRockTypes][3] = {};
    bool cragLoaded_ = false;
    // Per map.
    int cw_ = 0, ch_ = 0;
    std::vector<std::uint8_t> cellMat_;       // Material per cell (spice not included)
    std::vector<std::uint32_t> splatA_, splatB_;  // RGBA8: sand dunes rock cliff | rough concrete
    std::vector<std::uint8_t> spice_;         // per cell 0..255 (amount 0..7)
    // The spice texture, RG8 per cell: R = presence (the amount up to thin spice, 2 levels),
    // G = richness (the amount above thin, blurred over two cells: thick spice lies in soft
    // patches, not in cell blocks). Rebuilt from spice_ when it changes.
    std::vector<std::uint8_t> spiceTexCpu_;  // RGBA8 per cell: presence, richness, spice near, relief near
    void buildSpiceTex();
    std::vector<std::uint8_t> reliefNear_;  // faithful: per cell, relief within 2 cells (built with the guide)
    static std::vector<std::uint8_t> dilateCells(const std::vector<std::uint8_t>& in, int w, int h, int r);
    std::vector<std::uint32_t> concreteSig_;  // per cell: concrete bit (rebuild the splat when it changes)
    // Ground occlusion (setGroundOcclusion): per cell 0 / 1 a wall / 2 a building's solid cell.
    std::vector<std::uint8_t> occSig_;
    bool groundOcc_ = false, occAll_ = false;  // occAll_: switched - rebuild the whole splat at the next update()
    std::vector<int> blockBox_;  // update(): the changed cells' box per 8 x 8 cell block (x0, y0, x1, y1; -1 = none)
    std::vector<RockInst> inst_;              // sorted by layer
    std::uint32_t instFirst_[kRockTypes] = {}, instCount_[kRockTypes] = {};
    float tint_[kMaterials][4] = {};          // rgb colour lock, a = world px per texture repeat
    float lum_[kMaterials][4] = {};           // rgb luminance colour lock, a = normal strength
    float rockTint_[kRockTypes][4] = {};
    float colour_[kMaterials][3] = {};       // the colour lock's targets (0..255)
    // Concrete slab look (terrain2_fs concreteSlab; uniform slot 7): [0] = dust / drift colour rgb
    // (0..1), a = style; [1] = panel detail, weathering, markings, sand drift (0..1).
    float concreteLook_[2][4] = {};
    bool topDown_ = false;
    bool splatDirty_ = false, spiceDirty_ = false, instDirty_ = false, staticUploaded_ = false;
    // Faithful (Terrain v3).
    TerrainStyle style_ = TerrainStyle::Designed;
    TerrainGuide guide_;
    float faithU_[13][4] = {};                  // the map's part of the F3U block of terrain3_fs
    bool guideDirty_ = false;

    // GPU.
    SDL_GPUGraphicsPipeline* terrainPipe_ = nullptr;
    SDL_GPUGraphicsPipeline* rockPipe_ = nullptr;
    SDL_GPUGraphicsPipeline* rockDepthPipe_ = nullptr;  // created on the first shadow pass
    SDL_GPUGraphicsPipeline* rockViewDepthPipe_ = nullptr;  // the same without the bias (drawShadow depthBias false)
    bool rockDepthFailed_ = false;
    SDL_GPUSampler* matSampler_ = nullptr;
    SDL_GPUTexture* albedoArr_ = nullptr;
    SDL_GPUTexture* normalArr_ = nullptr;
    SDL_GPUTexture* rockArr_ = nullptr;
    SDL_GPUTexture* splatATex_ = nullptr;
    SDL_GPUTexture* splatBTex_ = nullptr;
    SDL_GPUTexture* spiceTex_ = nullptr;
    int splatW_ = 0, splatH_ = 0;
    SDL_GPUGraphicsPipeline* faithPipe_ = nullptr;  // terrain_vs + terrain3_fs, created on first use
    bool faithPipeFailed_ = false;
    std::unique_ptr<PhotoTerrain> photo_;  // "Photo terrain" (ensurePhoto)
    bool photoTried_ = false;
    SDL_GPUTextureFormat colorFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID, depthFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUSampleCount samples_ = SDL_GPU_SAMPLECOUNT_1;
    int aniso_ = 8;
    SDL_GPUTexture* guideTex_ = nullptr;
    SDL_GPUSampler* guideSampler_ = nullptr;
    SDL_GPUSampler* guideMirror_ = nullptr;  // the guide mirrored across the map's edge (the world beyond the map)
    int guideW_ = 0, guideH_ = 0;
    SDL_GPUBuffer* rockVB_ = nullptr;
    SDL_GPUBuffer* instVB_ = nullptr;
    LightGridBuffer lightFallback_;  // fragment storage buffer 0 without a light grid (the 2D bake)
    std::uint32_t instCap_ = 0;
    // cullRocks: the visible subsets (CPU lists sorted by type, their buffers, the sets drawn).
    std::vector<RockInst> viewInst_, shadowInst_;
    SDL_GPUBuffer* viewVB_ = nullptr;
    SDL_GPUBuffer* shadowVB_ = nullptr;
    std::uint32_t viewCap_ = 0, shadowCap_ = 0;
    RockSet viewSet_, shadowSet_;
    bool culled_ = false, culledShadow_ = false, cullUpload_ = false;
    void uploadCulled(SDL_GPUCommandBuffer* cmd);
};

}  // namespace d2k::render3d

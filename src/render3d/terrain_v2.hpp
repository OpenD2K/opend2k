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

#include "render3d/lights.hpp"
#include "render3d/math3d.hpp"
#include "render3d/terrain_guide.hpp"

#include <SDL3/SDL.h>

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
    // classified (the map then gets the Designed layout).
    const TerrainGuide* prepareGuide(const game::World& w, const render::TileAtlas* atlas);
    // The faithful layout is in use (the style is Faithful and the guide could be built).
    bool faithfulActive() const { return style_ == TerrainStyle::Faithful && guide_.valid(); }
    // The sun of the faithful top-down bake: the original tiles are lit from the south-west.
    // The shadows are cast by a lower sun from the same side (longer, like the original's).
    static Vec3 faithfulBakeSun();
    static Vec3 faithfulBakeShadowSun();
    // Graphics: 3D with the faithful terrain: the whole scene (terrain, shadow map, models) is
    // lit from the south-west like the original tiles, so the cliffs' faces match the 2D bake.
    static Vec3 faithfulSun3D() { return faithfulBakeSun(); }
    // A new map / a rebuilt heightfield (t built with designed = true). tileRgb: mean colour
    // (0..255) of every tile of the map's tileset, 3 floats each (the colour lock).
    void build(const game::World& w, const Terrain& t, const std::vector<float>& tileRgb);
    // Spice amounts and concrete changed? (cheap; uploads follow in prepare). changed: gets the
    // indices (y * width + x) of the cells whose look changed.
    void update(const game::World& w, std::vector<int>* changed = nullptr);
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
        // Dynamic point lights (render3d/lights.hpp); nullptr = none (the 2D bake).
        const PointLightsU* lights = nullptr;
        // Faithful only. The faithful 2D bake lights its relief (cliffs, broken rock) with `sun`
        // (the original tiles' own light, from the WSW) but the level ground's fine detail (sand
        // ripples, plain rock) with detailSun, the Designed bake's sun, so sand and buildable
        // rock look exactly as in Designed. detailSun.y <= 0: everything is lit by `sun` (3D).
        Vec3 detailSun{0, 0, 0};
        float reliefAmbient = 0, reliefDiffuse = 0;  // the relief's light (0 = ambient / diffuse)
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
    // The rocks as casters in the sun shadow map's depth pass (format: the shadow map's).
    int drawShadow(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Mat4& lightVP, SDL_GPUTextureFormat fmt,
                   const RockSet* rocks = nullptr);
    int rockCount() const { return int(inst_.size()); }
    const std::vector<RockInst>& rocks() const { return inst_; }  // sorted by type (layer)
    // Largest horizontal reach of a rock from its position (world px), for culling.
    static constexpr float kRockReach = 40.0f;

private:
    explicit DesignedTerrain(SDL_GPUDevice* dev) : dev_(dev) {}
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
    std::vector<std::uint8_t> spiceTexCpu_;
    void buildSpiceTex();
    std::vector<std::uint32_t> concreteSig_;  // per cell: concrete bit (rebuild the splat when it changes)
    std::vector<RockInst> inst_;              // sorted by layer
    std::uint32_t instFirst_[kRockTypes] = {}, instCount_[kRockTypes] = {};
    float tint_[kMaterials][4] = {};          // rgb colour lock, a = world px per texture repeat
    float lum_[kMaterials][4] = {};           // rgb luminance colour lock, a = normal strength
    float rockTint_[kRockTypes][4] = {};
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
    SDL_GPUTextureFormat colorFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID, depthFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUSampleCount samples_ = SDL_GPU_SAMPLECOUNT_1;
    int aniso_ = 8;
    SDL_GPUTexture* guideTex_ = nullptr;
    SDL_GPUSampler* guideSampler_ = nullptr;
    int guideW_ = 0, guideH_ = 0;
    SDL_GPUBuffer* rockVB_ = nullptr;
    SDL_GPUBuffer* instVB_ = nullptr;
    std::uint32_t instCap_ = 0;
};

}  // namespace d2k::render3d

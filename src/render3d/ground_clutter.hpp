#pragma once
// Reimagined ground clutter (docs/3d-mode.md "Ground clutter"): small meshes scattered over the Reimagined terrain
// (Graphics: 3D, 3D terrain: Faithful, Reimagined on) - boulders, talus rubble, dry shrubs, dead wood and rare
// sun-bleached scrap on the cells no unit uses (cliffs, their feet and ledges), and on the cells units use only
// what stays under ~3 px: pebbles, gravel patches, flat slabs, tiny dry tufts. Render-only: placed per map from a
// hash of the cell coordinates (the same every load), driven by the cell flags and the terrain's heights; hidden
// on buildings / walls / concrete / building footprints (also ones built later) and on spice (it grows and is
// harvested); shrouded like the terrain; never pickable (picking reads the terrain only).
//
// Assets: the ground clutter pack, artpacks/ground-clutter/clutter/ (clutter.ini, <piece>.glb, <piece>_lod1.glb,
// tex/<layer>.png), made by tools/models/clutter_kit.py from Poly Haven CC0 scans + procedural pieces. Without it
// nothing is drawn. Drawing: one instanced draw per piece type (LOD 0 or 1 by zoom), frustum-culled per chunk of
// cells for the camera and the sun's view, casting into the sun shadow map.

#include "render3d/light_buffer.hpp"
#include "render3d/lights.hpp"
#include "render3d/math3d.hpp"
#include "render3d/terrain_v2.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace d2k::game {
class World;
}

namespace d2k::render3d {

class Terrain;
class Surround;

class GroundClutter {
public:
    // Piece classes (clutter.ini "piece ... <class>"). The first four are the only ones on cells units use.
    enum Class : std::uint8_t { kPebble, kGravel, kSlab, kTuft, kBoulder, kRubble, kShrub, kDeadwood, kScrap, kClasses };
    static constexpr int kMaxLayers = 12;
    static constexpr int kMaxTypes = 48;
    static constexpr float kPassableMaxHeight = 3.0f;  // world px: the tallest clutter on a cell units use

    // The pack folder (…/ground-clutter/clutter with clutter.ini), "" when not installed. D2K_CLUTTER_PACK
    // overrides it (a folder holding clutter.ini).
    static std::string packDir();
    // nullptr (with a log line) when the pack or the pipelines are missing.
    static std::unique_ptr<GroundClutter> create(SDL_GPUDevice* dev, SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth,
                                                 SDL_GPUSampleCount samples, int aniso);
    ~GroundClutter();
    GroundClutter(const GroundClutter&) = delete;
    GroundClutter& operator=(const GroundClutter&) = delete;

    // A new map / terrain: places everything (terrain built, designed: its palette). density 1 = High, 0.5 = Low.
    void build(const game::World& w, const Terrain& t, const DesignedTerrain& d, float density);
    // The terrain's heights changed (building foundations): the pieces follow the ground.
    void refreshHeights(const Terrain& t);
    // Per frame, before prepare(): what is hidden now (buildings, footprints, walls, concrete, spice), then the
    // pieces inside the camera's view and the sun's (lightVP; nullptr = no shadow pass). pxPerWorld: screen px per
    // world px at the look-at point (LOD 1 and no pebbles / tufts when zoomed far out).
    void cull(const game::World& w, const Terrain& t, const Mat4& viewProj, const Mat4* lightVP, float pxPerWorld);
    // Uploads (own copy pass), before the render pass.
    void prepare(SDL_GPUCommandBuffer* cmd);
    // In the scene pass (after the terrain): f as for the designed terrain. Returns the draw calls.
    int draw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const DesignedTerrain::Frame& f);
    // In the sun shadow map's depth pass (format: the shadow map's).
    int drawShadow(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Mat4& lightVP, SDL_GPUTextureFormat fmt);

    // The world beyond the map (render3d/surround.*, docs/3d-mode.md): a sparser scatter on the land out there
    // (stones, rubble at the foot of its rock, a few shrubs) - its own list, outside the map's cells and
    // chunks. After build(); again whenever the surround was rebuilt; nullptr = none.
    void placeBeyond(const Terrain& t, const Surround* s);
    // The surround's heights changed (its mirrored zone follows the map's foundations).
    void refreshBeyond(const Surround& s);

    // Spice clumps (docs/3d-mode.md "Spice fields in 3D"): low cinnamon lumps, crusts and small drifts on the spice
    // cells (denser and taller on thick spice) and a few strays on the sand just beyond a field. Procedural meshes
    // and texture (no pack needed), placed per cell from a hash of its coordinates, per chunk of kChunk x kChunk
    // cells. build() places them all; refreshSpice() (on the renderer's ~2.5 Hz spice check) re-places only the
    // chunks round cells whose spice amount changed (harvested: fewer and lower, gone at 0; a bloom: new ones).
    void refreshSpice(const game::World& w, const Terrain& t);
    int spiceCount() const { return spiceTotal_; }

    int instanceCount() const { return int(inst_.size()); }
    int visibleCount() const { return int(view_.size()); }
    int shadowCount() const { return int(shadow_.size()); }
    float density() const { return density_; }

    // One instance (the instance vertex layout of clutter_vs; 64 bytes).
    struct Inst {
        float pos[3], yaw;
        float scale[3], layer;
        float sun, height, dust, bright;
        float slopeX, slopeZ, hue, pad;
    };

private:
    explicit GroundClutter(SDL_GPUDevice* dev) : dev_(dev) {}
    bool load(const std::string& dir);
    bool init(SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth, SDL_GPUSampleCount samples, int aniso);
    void place(const game::World& w, const Terrain& t);
    void addSpicePieces();  // the clumps' meshes (types_) and texture layer, after the pack
    void placeSpice(const game::World& w, const Terrain& t, int chunk);  // one chunk's clumps

    struct Type {
        std::string name;
        int layer = 0;
        Class cls = kPebble;
        float heightRatio = 0.3f;  // height above the ground / width (mesh, scale 1)
        std::uint32_t first[2] = {}, count[2] = {};  // indices of LOD 0 / LOD 1 in the index buffer
    };
    struct Meta {
        std::int32_t cell;     // y * width + x of the cell under the centre
        std::uint8_t type;
        std::uint8_t cls;
        float sink;            // below the ground at the centre (px)
        float conform;         // share of the ground's slope the piece follows
        float reach;           // horizontal radius (px) for culling
    };

    SDL_GPUDevice* dev_;
    // Pack (CPU until the first upload).
    std::vector<Type> types_;
    std::vector<float> verts_;  // pos3 nrm3 uv2 per vertex, all types and LODs (welded)
    std::vector<std::uint32_t> idx_;  // triangle lists into verts_
    int layers_ = 0;
    std::vector<std::uint32_t> layerPx_[kMaxLayers];  // RGBA8 kTex x kTex
    float layerMeanL_[kMaxLayers] = {};
    std::uint8_t layerKind_[kMaxLayers] = {};  // 0 rock, 1 wood, 2 plant, 3 metal
    static constexpr int kTex = 512;
    std::vector<int> byClass_[kClasses];  // type indices per class

    // Per map.
    int cw_ = 0, ch_ = 0;
    float density_ = 1.0f;
    std::vector<Inst> inst_;  // sorted by chunk
    std::vector<Meta> meta_;
    static constexpr int kChunk = 8;  // cells per chunk edge
    int chW_ = 0, chH_ = 0;
    std::vector<std::uint32_t> chunkFirst_, chunkCount_;
    std::vector<float> chunkLo_, chunkHi_;  // y range per chunk
    std::vector<std::uint8_t> hidden_;      // per cell, this frame
    float tint_[kMaxLayers][4] = {}, look_[kMaxLayers][4] = {}, sand_[3] = {};
    // This frame: the visible instances (sorted by type), per type first / count, LOD.
    std::vector<Inst> view_, shadow_;
    std::uint32_t viewFirst_[kMaxTypes] = {}, viewCount_[kMaxTypes] = {}, shadowFirst_[kMaxTypes] = {},
                  shadowCount_[kMaxTypes] = {};
    int lod_ = 0;
    bool shadowCulled_ = false;
    std::vector<Inst> perType_[kMaxTypes];
    // Beyond the map (placeBeyond): instances sorted by chunk, each chunk with its box.
    struct BeyondChunk {
        std::uint32_t first = 0, count = 0;
        float x0 = 0, z0 = 0, x1 = 0, z1 = 0, lo = 0, hi = 0;
    };
    std::vector<Inst> beyond_;
    std::vector<Meta> beyondMeta_;
    std::vector<BeyondChunk> beyondChunks_;
    // Spice clumps: per chunk (kChunk cells, chW_ x chH_ of the map's) its instances and height range.
    static constexpr int kSpiceTypes = 4;               // lump, lump (2nd shape), crust, drift
    static constexpr int kSpicePerChunkMax = 640;       // ~10 per cell of a chunk full of thick spice
    static constexpr int kSpiceMax = 24000;             // over the whole map
    int spiceType_[kSpiceTypes] = {-1, -1, -1, -1};     // indices into types_
    int spiceLayer_ = -1;                               // its texture layer
    float spiceTint_[2][4] = {};                        // the layer's tint: vivid, Realistic grade
    int spiceChW_ = 0, spiceChH_ = 0;
    int spiceTotal_ = 0;
    std::vector<std::uint8_t> spiceAmt_;                // per cell, the amount the clumps were placed for
    std::vector<std::uint8_t> spiceDirty_;              // per chunk (scratch)
    std::vector<std::vector<Inst>> spiceChunk_;
    std::vector<std::vector<std::uint8_t>> spiceTy_;    // per chunk, the type (into types_) of each instance
    std::vector<float> spiceLo_, spiceHi_;

    // GPU.
    SDL_GPUGraphicsPipeline* pipe_ = nullptr;
    SDL_GPUGraphicsPipeline* depthPipe_ = nullptr;  // created on the first shadow pass
    bool depthFailed_ = false;
    SDL_GPUSampler* sampler_ = nullptr;
    SDL_GPUTexture* texArr_ = nullptr;
    SDL_GPUBuffer* meshVB_ = nullptr;
    SDL_GPUBuffer* meshIB_ = nullptr;
    SDL_GPUBuffer* viewVB_ = nullptr;
    SDL_GPUBuffer* shadowVB_ = nullptr;
    std::uint32_t viewCap_ = 0, shadowCap_ = 0;
    SDL_GPUTransferBuffer* stage_ = nullptr;
    std::uint32_t stageCap_ = 0;
    bool staticUploaded_ = false;
    LightGridBuffer lightFallback_;
};

}  // namespace d2k::render3d

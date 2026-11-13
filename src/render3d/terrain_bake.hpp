#pragma once
// The designed terrain (Terrain v2, render3d/terrain_v2.*) for Graphics: Enhanced (2D)
// (docs/enhanced-graphics.md "Designed terrain"; Settings "Terrain: Designed").
//
// The 3D designed terrain - CC0 materials, rock-scan meshes along the cliffs, sun shadows - is
// rendered once per map from straight above with an ORTHOGRAPHIC camera (a world point (x, h, z)
// lands on (x, z) whatever its height, so cell (x, y) covers exactly the rectangle the 2D tiles
// would) into chunk textures of 16 x 16 cells (ppc texels per cell, from a memory budget), each
// with a sun shadow map fitted to the chunk. MapRenderer draws the chunks instead of the tile
// atlas (render::TerrainLayer); everything else (decals, units, buildings, shroud, overlays) is
// drawn on top exactly as before. Spice and concrete changes re-bake only the chunks they touch
// (throttled: a few chunks per frame). Render-only: the World is read, never changed.
//
// Zoomed in (a cell over ~1.1 x the chunks' texels on the screen: Enhanced zoom 2-4 on a Retina
// display), the VISIBLE part is re-baked sharper as detail tiles of 4 x 4 cells at 64 .. 256
// texels per cell (the level from the camera zoom, which is in output pixels, so it includes the
// display scale): baked progressively from the view centre out (a texel budget per frame; the
// chunks show meanwhile), kept in an LRU cache under a memory budget, re-baked in part like the
// chunks when spice / concrete changes under them.
//
// Needs the SDL GPU renderer (render3d/gpu.hpp) and assets/terrain; create() returns nullptr
// otherwise and the session keeps the tiles.

#include "render/lighting.hpp"
#include "render/map_renderer.hpp"
#include "render3d/lights.hpp"
#include "render3d/math3d.hpp"
#include "render3d/terrain.hpp"
#include "render3d/terrain_v2.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace d2k::render {
class TileAtlas;
}

namespace d2k::render3d {

class TerrainBaker : public render::TerrainLayer {
public:
    static constexpr int kChunkCells = 16;
    static constexpr int kMipCopies = 3;  // full, half and quarter resolution (SDL_Renderer has no mipmaps)
    static constexpr int kTileCells = 4;  // detail tiles (zoomed in): 4 x 4 cells
    static constexpr int kDetailLevels = 5;
    static constexpr int kDetailPpc[kDetailLevels] = {64, 96, 128, 192, 256};  // texels per cell

    static std::unique_ptr<TerrainBaker> create(SDL_Renderer* r);
    // Designed or Designed (faithful) (Settings "Terrain"); a change re-bakes on the next update.
    void setStyle(TerrainStyle s) {
        if (s == style_) return;
        style_ = s;
        world_ = nullptr;
    }
    TerrainStyle style() const { return style_; }
    // The light the terrain is baked under (time of day / Modern lighting, render/lighting.hpp):
    // the sun's turn / elevation, and the sun / sky colours DIVIDED by the 2D grade the map gets
    // afterwards (MapRenderer), so flat sunlit ground bakes at its own colour and the grade brings
    // it to the time of day. A change re-bakes every chunk at once on the next update (~0.3-0.6 s)
    // and drops the detail tiles; Classic Day = the old bake exactly.
    struct Light {
        float dAz = 0, elK = 1;
        float sun[3] = {1, 1, 1}, amb[3] = {1, 1, 1};
        float sat = 1;
        bool operator==(const Light&) const = default;
    };
    void setLight(const Light& l) {
        if (l == light_) return;
        light_ = l;
        relight_ = true;
    }
    ~TerrainBaker() override;
    TerrainBaker(const TerrainBaker&) = delete;
    TerrainBaker& operator=(const TerrainBaker&) = delete;

    // Every frame before the map is drawn: builds for a new world / atlas (tile means = the colour
    // lock), re-bakes the chunks whose cells changed. False while nothing can be drawn.
    bool update(const game::World& w, const render::TileAtlas* atlas);
    bool drawTerrain(SDL_Renderer* r, const render::Camera& cam, int viewW, int viewH) override;

    int texelsPerCell() const { return ppc_; }
    double lastBuildMs() const { return buildMs_; }   // full bake (CPU build + GPU, waited for)
    double lastRebakeMs() const { return rebakeMs_; }  // last re-bake (CPU recording; + GPU with D2K_BAKE_TIMING=1)
    int lastRebakeCells() const { return rebakeCells_; }
    // Detail tiles: the level the view wants (texels per cell, 0 = none), resident bytes, budget.
    int detailPpc() const { return wantLevel_ >= 0 ? kDetailPpc[wantLevel_] : 0; }
    std::size_t detailBytes() const { return detailBytes_; }
    std::size_t detailBudget() const { return detailBudget_; }

private:
    TerrainBaker(SDL_Renderer* r, SDL_GPUDevice* dev) : r_(r), dev_(dev) {}
    bool init();
    void releaseChunks();
    void releaseTiles();
    bool allocateChunks();

    // A baked texture: cells [cx0, cx0 + cw) x [cy0, cy0 + ch) at ppc texels per cell. Its texture
    // is a square of `cells` cells (a chunk / tile at the map's right or bottom edge keeps the full
    // size; the cells past the edge stay black and are never drawn). Chunks have the half and
    // quarter copies, detail tiles only level 0.
    struct Surface {
        int cx0 = 0, cy0 = 0, cw = 0, ch = 0;  // cells
        int cells = kChunkCells, ppc = 64, levels = kMipCopies;
        SDL_GPUTexture* gpu[kMipCopies] = {};
        SDL_Texture* sdl[kMipCopies] = {};
        int dx0 = 0, dy0 = 0, dx1 = 0, dy1 = 0;  // dirty cells [dx0, dx1) x [dy0, dy1); empty = clean
        bool dirty() const { return dx1 > dx0 && dy1 > dy0; }
        void markAll() { dx0 = cx0; dy0 = cy0; dx1 = cx0 + cw; dy1 = cy0 + ch; }
        void mark(int x0, int y0, int x1, int y1) {
            x0 = std::max(x0, cx0); y0 = std::max(y0, cy0);
            x1 = std::min(x1, cx0 + cw); y1 = std::min(y1, cy0 + ch);
            if (x1 <= x0 || y1 <= y0) return;
            if (!dirty()) { dx0 = x0; dy0 = y0; dx1 = x1; dy1 = y1; return; }
            dx0 = std::min(dx0, x0); dy0 = std::min(dy0, y0); dx1 = std::max(dx1, x1); dy1 = std::max(dy1, y1);
        }
        int px() const { return cells * ppc; }
    };
    using Chunk = Surface;
    struct Tile : Surface {
        std::uint64_t lastUsed = 0;  // frame
        bool baked = false;
    };
    static std::uint64_t tileKey(int level, int tx, int ty) {
        return (std::uint64_t(level) << 48) | (std::uint64_t(std::uint32_t(ty)) << 24) | std::uint32_t(tx);
    }

    // A region to (re)bake: cells [x0, x1) x [y0, y1) of one surface.
    struct Job {
        Surface* s;
        int x0, y0, x1, y1;
    };
    // Records the jobs into `cmd`: uploads, then per job the sun shadow map and the terrain into
    // the surface (a whole chunk straight into its texture; a part or a tile through scratch_ and
    // a copy).
    void bake(SDL_GPUCommandBuffer* cmd, const std::vector<Job>& jobs);
    // depthRange: the light's depth range; extent: the light frustum's width x height (world px).
    Mat4 fitLight(float x0, float z0, float x1, float z1, float& depthRange, float* extent = nullptr) const;
    bool makeSurfaceTextures(Surface& s);
    void releaseSurface(Surface& s);
    // Detail tiles for the view: picks the level, bakes the missing / changed visible tiles (a
    // texel budget per frame; everything at once on the first frame after a build), evicts.
    void updateDetail(const render::Camera& cam, int viewW, int viewH);
    void evictTiles(std::size_t budget);

    SDL_Renderer* r_;
    SDL_GPUDevice* dev_;
    std::unique_ptr<DesignedTerrain> designed_;
    TerrainStyle style_ = TerrainStyle::Designed;
    Vec3 sun_ = Terrain::sunDirection();  // the bake's sun (faithful: the original tiles' own light)
    Vec3 shadowSun_ = Terrain::sunDirection();  // the sun that casts the shadows (faithful: lower)
    Vec3 detailSun_ = Terrain::sunDirection();  // faithful: the level ground's detail (the Designed sun)
    Vec3 baseSun_ = Terrain::sunDirection(), baseShadowSun_ = Terrain::sunDirection();  // before the time of day
    Light light_;
    bool relight_ = false;
    PointLightsU bakeLights_;  // no point lights; the head carries light_'s colours
    void applyLight();         // light_ -> sun_, shadowSun_, detailSun_, bakeLights_
    Terrain terrain_;
    const game::World* world_ = nullptr;
    const render::TileAtlas* atlas_ = nullptr;
    int mapW_ = 0, mapH_ = 0;  // cells
    float terrainTop_ = 0;
    int ppc_ = 64;             // texels per cell
    int chunkPx_ = 1024;       // kChunkCells * ppc_
    std::vector<Chunk> chunks_;
    int chunksX_ = 0, chunksY_ = 0;
    bool ready_ = false;
    Uint64 lastCheck_ = 0;
    std::vector<int> changed_;
    double buildMs_ = 0, rebakeMs_ = 0;
    int rebakeCells_ = 0;
    bool timing_ = false;

    // Detail tiles (zoomed in).
    std::unordered_map<std::uint64_t, Tile> tiles_;
    std::size_t detailBytes_ = 0;
    std::size_t detailBudget_ = 0;       // bytes (D2K_BAKE_DETAIL_MB)
    std::uint64_t detailTexelsPerFrame_ = 0;  // progressive bake budget (D2K_BAKE_DETAIL_FRAME)
    std::uint64_t frame_ = 0;
    int wantLevel_ = -1;                 // kDetailPpc index the view wants, -1 none
    std::uint64_t wantSince_ = 0;        // frame the wanted level last changed
    bool syncDetail_ = false;            // bake every visible tile at once (first frame after a build)
    bool detailOff_ = false;             // D2K_BAKE_DETAIL=0
    bool bakeTest_ = false;              // D2K_BAKE_TEST: partial re-bakes of the tiles too
    int targetPx_ = 0;                   // size of msaa_ / scratch_ / depth_ (chunks and tiles fit)
    std::uint64_t tilesBaked_ = 0, tilesEvicted_ = 0;  // statistics (logged at the end)
    std::size_t peakDetailBytes_ = 0;

    // GPU.
    SDL_GPUTextureFormat colorFormat_ = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUTextureFormat depthFormat_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    SDL_GPUTextureFormat shadowFormat_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    SDL_GPUSampleCount samples_ = SDL_GPU_SAMPLECOUNT_1;
    static constexpr int kShadowSize = 2048;
    SDL_GPUTexture* msaa_ = nullptr;     // chunk-sized colour target (MSAA), resolved into the chunk
    SDL_GPUTexture* scratch_ = nullptr;  // chunk-sized: a partial re-bake lands here, then is copied
    SDL_GPUTexture* depth_ = nullptr;
    SDL_GPUTexture* shadow_ = nullptr;
    SDL_GPUTexture* clearMask_ = nullptr;  // the shroud slot: alpha 0
    SDL_GPUSampler* linearClamp_ = nullptr;
    SDL_GPUSampler* shadowSampler_ = nullptr;
    SDL_GPUGraphicsPipeline* terrainDepthPipe_ = nullptr;
    SDL_GPUBuffer* vb_ = nullptr;
    SDL_GPUBuffer* ib_ = nullptr;
    SDL_GPUBuffer* rockSets_ = nullptr;  // the rocks near each job's region, one after the other
    std::uint32_t rockSetCap_ = 0;       // instances
    std::uint32_t indexCount_ = 0;
    bool meshDirty_ = true, maskUploaded_ = false;
};

}  // namespace d2k::render3d

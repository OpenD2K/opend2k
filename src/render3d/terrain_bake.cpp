#include "render3d/terrain_bake.hpp"

#include "game/world.hpp"
#include "render/tileset.hpp"
#include "render3d/gpu.hpp"
#include "render3d/shader_blobs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace d2k::render3d {

namespace {

struct TerrainVU {
    float viewProj[16];
};

constexpr float kShadowKeep = 0.12f;  // diffuse light kept in full sun shadow (as Renderer3D)
// Texels of the full-resolution chunks: about 190 MB for the full set + its half / quarter copies.
constexpr double kTexelBudget = 48.0 * 1024 * 1024;
constexpr float kEyeHeight = 4000.0f;  // orthographic camera height above the ground (world px)
// Detail tiles: memory budget (MB) and texels baked per frame while zoomed in.
constexpr int kDetailBudgetMB = 160;
constexpr std::uint64_t kDetailTexelsPerFrame = 1u << 20;
// A resident tile no view has used for this many frames is freed (~15-30 s).
constexpr std::uint64_t kTileIdleFrames = 1800;

SDL_GPUTexture* makeTarget(SDL_GPUDevice* dev, SDL_GPUTextureFormat f, int w, int h, SDL_GPUTextureUsageFlags use,
                           SDL_GPUSampleCount samples = SDL_GPU_SAMPLECOUNT_1) {
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = f;
    ci.usage = use;
    ci.width = Uint32(w);
    ci.height = Uint32(h);
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.sample_count = samples;
    SDL_GPUTexture* t = SDL_CreateGPUTexture(dev, &ci);
    if (!t) SDL_Log("Designed terrain (2D): texture %dx%d failed: %s", w, h, SDL_GetError());
    return t;
}

}  // namespace

std::unique_ptr<TerrainBaker> TerrainBaker::create(SDL_Renderer* r) {
    SDL_GPUDevice* dev = gpuDeviceOf(r);
    if (!dev) {
        SDL_Log("Terrain: Designed needs the SDL GPU renderer - using the tiles");
        return nullptr;
    }
    if (!deviceHasShaders(dev)) {
        SDL_Log("Terrain: Designed - no shaders for GPU driver %s in this build - using the tiles", SDL_GetGPUDeviceDriver(dev));
        return nullptr;
    }
    std::unique_ptr<TerrainBaker> p(new TerrainBaker(r, dev));
    if (!p->init()) return nullptr;
    return p;
}

TerrainBaker::~TerrainBaker() {
    if (tilesBaked_)
        SDL_Log("Terrain: Designed (2D) detail tiles: %llu baked, %llu evicted, peak %.1f MB resident (budget %zu MB)",
                (unsigned long long)tilesBaked_, (unsigned long long)tilesEvicted_, double(peakDetailBytes_) / (1024.0 * 1024.0),
                detailBudget_ >> 20);
    releaseTiles();
    releaseChunks();
    for (SDL_GPUTexture* t : {msaa_, depth_, shadow_, clearMask_})
        if (t) SDL_ReleaseGPUTexture(dev_, t);
    for (SDL_GPUBuffer* b : {vb_, ib_, rockSets_})
        if (b) SDL_ReleaseGPUBuffer(dev_, b);
    if (terrainDepthPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, terrainDepthPipe_);
    if (linearClamp_) SDL_ReleaseGPUSampler(dev_, linearClamp_);
    if (shadowSampler_) SDL_ReleaseGPUSampler(dev_, shadowSampler_);
}

bool TerrainBaker::init() {
    timing_ = SDL_getenv("D2K_BAKE_TIMING") != nullptr;
    // Detail tiles: a memory budget (the visible tiles at the sharpest level take 30 - 70 MB on a
    // 2560 x 1600 view, the rest is a cache for scrolling back) and a progressive bake budget per
    // frame (a 4 x 4-cell tile at 256 texels per cell is 1 M texels).
    detailOff_ = SDL_getenv("D2K_BAKE_DETAIL") && std::atoi(SDL_getenv("D2K_BAKE_DETAIL")) == 0;
    bakeTest_ = SDL_getenv("D2K_BAKE_TEST") != nullptr;
    detailBudget_ = std::size_t(kDetailBudgetMB) << 20;
    if (const char* e = SDL_getenv("D2K_BAKE_DETAIL_MB")) detailBudget_ = std::size_t(std::clamp(std::atoi(e), 8, 2048)) << 20;
    detailTexelsPerFrame_ = kDetailTexelsPerFrame;
    if (const char* e = SDL_getenv("D2K_BAKE_DETAIL_FRAME"))
        detailTexelsPerFrame_ = std::uint64_t(std::clamp(std::atoll(e), 1LL << 16, 1LL << 26));
    const SDL_GPUTextureUsageFlags dsUse = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    if (SDL_GPUTextureSupportsFormat(dev_, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, dsUse))
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    else if (SDL_GPUTextureSupportsFormat(dev_, SDL_GPU_TEXTUREFORMAT_D24_UNORM, SDL_GPU_TEXTURETYPE_2D, dsUse))
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    else
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    shadowFormat_ = SDL_GPUTextureSupportsFormat(dev_, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                                 dsUse | SDL_GPU_TEXTUREUSAGE_SAMPLER)
                        ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
                        : SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    // 4x MSAA: clean rock silhouettes from above.
    if (SDL_GPUTextureSupportsSampleCount(dev_, colorFormat_, SDL_GPU_SAMPLECOUNT_4) &&
        SDL_GPUTextureSupportsSampleCount(dev_, depthFormat_, SDL_GPU_SAMPLECOUNT_4))
        samples_ = SDL_GPU_SAMPLECOUNT_4;
    designed_ = DesignedTerrain::create(dev_, colorFormat_, depthFormat_, samples_);
    if (!designed_) return false;
    designed_->setTopDown(true);

    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    linearClamp_ = SDL_CreateGPUSampler(dev_, &si);
    si.enable_compare = true;
    si.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    shadowSampler_ = SDL_CreateGPUSampler(dev_, &si);
    shadow_ = makeTarget(dev_, shadowFormat_, kShadowSize, kShadowSize,
                         SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
    clearMask_ = makeTarget(dev_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 1, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);

    SDL_GPUShader* vs = createShader(dev_, ShaderId::TerrainVs, 0, 1);
    SDL_GPUShader* fs = createShader(dev_, ShaderId::DepthFs, 0, 0);
    if (vs && fs) {
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs;
        pi.fragment_shader = fs;
        SDL_GPUVertexBufferDescription vbd{0, sizeof(TerrainVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute at[3] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
                                        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
                                        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, 24}};
        pi.vertex_input_state = {&vbd, 1, at, 3};
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.enable_depth_clip = gpuDepthClip();
        pi.rasterizer_state.enable_depth_bias = true;
        pi.rasterizer_state.depth_bias_constant_factor = 2.0f;
        pi.rasterizer_state.depth_bias_slope_factor = 2.5f;
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = true;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        pi.target_info.num_color_targets = 0;
        pi.target_info.depth_stencil_format = shadowFormat_;
        pi.target_info.has_depth_stencil_target = true;
        terrainDepthPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
    }
    for (SDL_GPUShader* s : {vs, fs})
        if (s) SDL_ReleaseGPUShader(dev_, s);
    if (!linearClamp_ || !shadowSampler_ || !shadow_ || !clearMask_ || !terrainDepthPipe_) {
        SDL_Log("Terrain: Designed (2D) setup failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool TerrainBaker::makeSurfaceTextures(Surface& s) {
    const SDL_GPUTextureUsageFlags use = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    for (int l = 0; l < s.levels; ++l) {
        const int px = s.px() >> l;
        s.gpu[l] = makeTarget(dev_, colorFormat_, px, px, use);
        if (!s.gpu[l]) return false;
        SDL_PropertiesID p = SDL_CreateProperties();
        SDL_SetPointerProperty(p, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, s.gpu[l]);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_ABGR8888);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, px);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, px);
        s.sdl[l] = SDL_CreateTextureWithProperties(r_, p);
        SDL_DestroyProperties(p);
        if (!s.sdl[l]) {
            SDL_Log("Designed terrain (2D): chunk texture failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetTextureBlendMode(s.sdl[l], SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(s.sdl[l], SDL_SCALEMODE_LINEAR);
    }
    return true;
}

void TerrainBaker::releaseSurface(Surface& s) {
    for (int l = 0; l < kMipCopies; ++l) {
        if (s.sdl[l]) SDL_DestroyTexture(s.sdl[l]);
        if (s.gpu[l]) SDL_ReleaseGPUTexture(dev_, s.gpu[l]);
        s.sdl[l] = nullptr;
        s.gpu[l] = nullptr;
    }
}

void TerrainBaker::releaseChunks() {
    for (Chunk& c : chunks_) releaseSurface(c);
    chunks_.clear();
    for (SDL_GPUTexture** t : {&msaa_, &scratch_, &depth_}) {
        if (*t) SDL_ReleaseGPUTexture(dev_, *t);
        *t = nullptr;
    }
}

void TerrainBaker::applyLight() {
    relight_ = false;
    render::SceneLight l;
    l.dAz = light_.dAz;
    l.elK = light_.elK;
    auto turn = [&](const Vec3& v) {
        const float b[3] = {v.x, v.y, v.z};
        float o[3];
        l.sunDirection(b, o);
        return normalize(Vec3{o[0], o[1], o[2]});
    };
    sun_ = turn(baseSun_);
    shadowSun_ = turn(baseShadowSun_);
    detailSun_ = turn(Terrain::sunDirection());
    bakeLights_.clear();
    const float one[3] = {1, 1, 1};
    bakeLights_.setScene(light_.sun, light_.amb, one, 1, light_.sat);
}

void TerrainBaker::releaseTiles() {
    for (auto& [k, t] : tiles_) releaseSurface(t);
    tiles_.clear();
    detailBytes_ = 0;
    wantLevel_ = -1;
}

bool TerrainBaker::allocateChunks() {
    releaseTiles();
    releaseChunks();
    chunksX_ = (mapW_ + kChunkCells - 1) / kChunkCells;
    chunksY_ = (mapH_ + kChunkCells - 1) / kChunkCells;
    const double cells = double(chunksX_) * chunksY_ * kChunkCells * kChunkCells;
    ppc_ = std::clamp(int(std::sqrt(kTexelBudget / std::max(1.0, cells)) / 16) * 16, 32, 128);
    if (const char* e = SDL_getenv("D2K_BAKE_PPC")) ppc_ = std::clamp(std::atoi(e), 8, 128);
    chunkPx_ = kChunkCells * ppc_;
    // The render targets take a chunk or a detail tile (a part of either lands in scratch_ first).
    targetPx_ = detailOff_ ? chunkPx_ : std::max(chunkPx_, kTileCells * kDetailPpc[kDetailLevels - 1]);
    if (samples_ != SDL_GPU_SAMPLECOUNT_1) {
        msaa_ = makeTarget(dev_, colorFormat_, targetPx_, targetPx_, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, samples_);
        if (!msaa_) return false;
    }
    depth_ = makeTarget(dev_, depthFormat_, targetPx_, targetPx_, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, samples_);
    scratch_ = makeTarget(dev_, colorFormat_, targetPx_, targetPx_, SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
    if (!depth_ || !scratch_) return false;
    chunks_.resize(std::size_t(chunksX_) * chunksY_);
    for (int cy = 0; cy < chunksY_; ++cy)
        for (int cx = 0; cx < chunksX_; ++cx) {
            Chunk& c = chunks_[std::size_t(cy) * chunksX_ + cx];
            c.cx0 = cx * kChunkCells;
            c.cy0 = cy * kChunkCells;
            c.cw = std::min(kChunkCells, mapW_ - c.cx0);
            c.ch = std::min(kChunkCells, mapH_ - c.cy0);
            c.cells = kChunkCells;
            c.ppc = ppc_;
            c.levels = kMipCopies;
            c.markAll();
            if (!makeSurfaceTextures(c)) return false;
        }
    return true;
}

// The sun's orthographic view fitted to one chunk's ground (receivers between y = 0 and the
// highest terrain + 64; casters share the light x / y with their shadows, the depth range covers
// the whole map box).
Mat4 TerrainBaker::fitLight(float x0, float z0, float x1, float z1, float& depthRange, float* extent) const {
    const Vec3 L = shadowSun_;
    const float top = terrainTop_ + 64;
    const Vec3 c{(x0 + x1) * 0.5f, top * 0.5f, (z0 + z1) * 0.5f};
    const Mat4 view = lookAt(c + L * 4000.0f, c, Vec3{0, 1, 0});
    float lx0 = 1e30f, lx1 = -1e30f, ly0 = 1e30f, ly1 = -1e30f;
    for (int i = 0; i < 8; ++i) {
        const Vec4 v = view * Vec4{(i & 1) ? x1 : x0, (i & 2) ? top : 0.0f, (i & 4) ? z1 : z0, 1};
        lx0 = std::min(lx0, v.x); lx1 = std::max(lx1, v.x);
        ly0 = std::min(ly0, v.y); ly1 = std::max(ly1, v.y);
    }
    const float mapW = float(mapW_ * 32), mapH = float(mapH_ * 32);
    float zn = 1e30f, zf = -1e30f;
    for (int i = 0; i < 8; ++i) {
        const Vec4 v = view * Vec4{(i & 1) ? mapW + 64 : -64.0f, (i & 2) ? top + 64 : -16.0f, (i & 4) ? mapH + 64 : -64.0f, 1};
        zn = std::min(zn, -v.z);
        zf = std::max(zf, -v.z);
    }
    const float pad = 16;
    depthRange = std::max(1.0f, zf - zn);
    if (extent) {
        extent[0] = lx1 - lx0 + 2 * pad;
        extent[1] = ly1 - ly0 + 2 * pad;
    }
    return orthoRH(lx0 - pad, lx1 + pad, ly0 - pad, ly1 + pad, std::max(1.0f, zn - 1), zf + 1) * view;
}

bool TerrainBaker::update(const game::World& w, const render::TileAtlas* atlas) {
    const Uint64 t0 = SDL_GetTicksNS();
    // A new map / atlas: full build (a failed build is not retried until one of them changes).
    if (world_ != &w || atlas_ != atlas || mapW_ != w.width() || mapH_ != w.height()) {
        ready_ = false;
        world_ = &w;
        atlas_ = atlas;
        mapW_ = w.width();
        mapH_ = w.height();
        if (mapW_ <= 0 || mapH_ <= 0) return false;
        const std::vector<float> rgb = DesignedTerrain::tileMeans(atlas);
        // Faithful: the layout guide from the map's tiles first (the heightfield takes its relief
        // from it); its bake is lit like the original tiles, from the west-south-west.
        designed_->setStyle(style_);
        const TerrainGuide* guide = designed_->prepareGuide(w, atlas);
        baseSun_ = guide ? DesignedTerrain::faithfulBakeSun() : Terrain::sunDirection();
        baseShadowSun_ = guide ? DesignedTerrain::faithfulBakeShadowSun() : baseSun_;
        applyLight();
        terrain_.enableFoundations(false);  // the 2D bake keeps the natural relief under buildings
        terrain_.build(w, rgb, true, guide);
        designed_->build(w, terrain_, rgb);
        terrainTop_ = 0;
        for (const TerrainVertex& tv : terrain_.vertices()) terrainTop_ = std::max(terrainTop_, tv.y);
        meshDirty_ = true;
        if (!allocateChunks()) {
            releaseChunks();
            return false;
        }
        const double cpuMs = double(SDL_GetTicksNS() - t0) / 1e6;
        std::vector<Job> all;
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            const Chunk& c = chunks_[i];
            all.push_back({&chunks_[i], c.cx0, c.cy0, c.cx0 + c.cw, c.cy0 + c.ch});
        }
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        if (!cmd) return false;
        bake(cmd, all);
        for (Chunk& c : chunks_) c.dx1 = c.dx0;  // clean
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence) {
            SDL_WaitForGPUFences(dev_, true, &fence, 1);
            SDL_ReleaseGPUFence(dev_, fence);
        }
        buildMs_ = double(SDL_GetTicksNS() - t0) / 1e6;
        SDL_Log("Terrain: Designed%s (2D) %dx%d cells, %d chunks of %d px (%d px per cell, %dx MSAA): build %.0f ms "
                "(CPU %.0f, GPU bake %.0f)",
                designed_->faithfulActive() ? " (faithful)" : "", mapW_, mapH_, int(chunks_.size()), chunkPx_, ppc_,
                samples_ == SDL_GPU_SAMPLECOUNT_4 ? 4 : 1, buildMs_, cpuMs, buildMs_ - cpuMs);
        {
            // GPU memory of the chunks (level 0 + the half / quarter copies) and the render targets.
            const double mb = 1.0 / (1024.0 * 1024.0);
            const double chunkMb = double(chunks_.size()) * chunkPx_ * chunkPx_ * 4.0 * (1.0 + 0.25 + 0.0625) * mb;
            const int ns = samples_ == SDL_GPU_SAMPLECOUNT_4 ? 4 : 1;
            // MSAA colour + its depth (both multisampled), the scratch target, the shadow map (D32).
            const double targetMb =
                (double(targetPx_) * targetPx_ * 4.0 * ((ns > 1 ? ns : 0) + ns + 1) + double(kShadowSize) * kShadowSize * 4.0) * mb;
            SDL_Log("Terrain: Designed (2D) GPU memory: chunks %.0f MB, render targets %.0f MB, detail tiles up to %zu MB "
                    "(levels 64..256 px per cell, 4 x 4-cell tiles, %llu texels per frame)",
                    chunkMb, targetMb, detailBudget_ >> 20, (unsigned long long)detailTexelsPerFrame_);
        }
        ready_ = true;
        // The first view's detail tiles at once (the build pauses anyway); D2K_BAKE_DETAIL_SYNC=0:
        // progressively like later views (to measure that).
        syncDetail_ = !(SDL_getenv("D2K_BAKE_DETAIL_SYNC") && std::atoi(SDL_getenv("D2K_BAKE_DETAIL_SYNC")) == 0);
        lastCheck_ = t0;
        if (bakeTest_) {  // self-check: partial re-bakes must match the full bake
            for (int y = 2; y + 3 < mapH_; y += 7)
                for (int x = 3; x + 3 < mapW_; x += 6)
                    for (int cy = std::max(0, (y - 1) / kChunkCells); cy <= std::min(chunksY_ - 1, (y + 3) / kChunkCells); ++cy)
                        for (int cx = std::max(0, (x - 1) / kChunkCells); cx <= std::min(chunksX_ - 1, (x + 3) / kChunkCells); ++cx)
                            chunks_[std::size_t(cy) * chunksX_ + cx].mark(x, y, x + 3, y + 3);
        }
        return true;
    }
    if (!ready_) return false;
    // The time of day / lighting changed: re-bake every chunk under the new light at once (like
    // the build: one pause), drop the detail tiles (re-baked for the view on this frame's draw).
    if (relight_) {
        applyLight();
        std::vector<Job> all;
        for (std::size_t i = 0; i < chunks_.size(); ++i) {
            const Chunk& c = chunks_[i];
            all.push_back({&chunks_[i], c.cx0, c.cy0, c.cx0 + c.cw, c.cy0 + c.ch});
        }
        if (SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_)) {
            bake(cmd, all);
            for (Chunk& c : chunks_) c.dx1 = c.dx0;  // clean
            if (SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd)) {
                SDL_WaitForGPUFences(dev_, true, &fence, 1);
                SDL_ReleaseGPUFence(dev_, fence);
            }
        }
        releaseTiles();
        syncDetail_ = true;
        SDL_Log("Terrain: re-baked under a new light in %.0f ms", double(SDL_GetTicksNS() - t0) / 1e6);
    }
    // Spice harvested / grown, concrete laid or destroyed: mark the cells around them (the splat
    // blur reaches under a cell into its neighbours; the organic spice field - filtering, kernel
    // and warp of the presence under two cells, the blurred richness about three: terrain_v2_msl.hpp
    // spiceField) in every chunk they touch.
    if (t0 - lastCheck_ > 250000000ull) {
        lastCheck_ = t0;
        changed_.clear();
        designed_->update(w, &changed_);
        for (int i : changed_) {
            const int x = i % mapW_, y = i / mapW_;
            constexpr int kReach = 3;  // cells
            for (int cy = std::max(0, (y - kReach) / kChunkCells); cy <= std::min(chunksY_ - 1, (y + kReach) / kChunkCells); ++cy)
                for (int cx = std::max(0, (x - kReach) / kChunkCells); cx <= std::min(chunksX_ - 1, (x + kReach) / kChunkCells); ++cx)
                    chunks_[std::size_t(cy) * chunksX_ + cx].mark(x - kReach, y - kReach, x + kReach + 1, y + kReach + 1);
            // The detail tiles there too, at every level that is resident.
            if (!tiles_.empty())
                for (int ty = std::max(0, (y - kReach) / kTileCells); ty <= (y + kReach) / kTileCells; ++ty)
                    for (int tx = std::max(0, (x - kReach) / kTileCells); tx <= (x + kReach) / kTileCells; ++tx)
                        for (int l = 0; l < kDetailLevels; ++l) {
                            auto it = tiles_.find(tileKey(l, tx, ty));
                            if (it != tiles_.end()) it->second.mark(x - kReach, y - kReach, x + kReach + 1, y + kReach + 1);
                        }
        }
    }
    // Re-bake only the dirty part of each dirty chunk (a harvested cell: 3 x 3 cells), a few per
    // frame so a big change (a spice bloom, a slab of concrete) spreads over some frames.
    std::vector<Job> todo;
    constexpr int kMaxCellsPerFrame = 160;
    int cells = 0;
    for (std::size_t i = 0; i < chunks_.size() && cells < kMaxCellsPerFrame; ++i) {
        Chunk& c = chunks_[i];
        if (!c.dirty()) continue;
        todo.push_back({&c, c.dx0, c.dy0, c.dx1, c.dy1});
        cells += (c.dx1 - c.dx0) * (c.dy1 - c.dy0);
        c.dx1 = c.dx0;
    }
    if (!todo.empty()) {
        Uint64 tg = t0;
        if (timing_) {  // measure the re-bake alone: let the frames already queued finish first
            SDL_WaitForGPUIdle(dev_);
            tg = SDL_GetTicksNS();
        }
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        if (!cmd) return ready_;
        bake(cmd, todo);
        // D2K_BAKE_TIMING=N (> 1): the same re-bake N times in a row, timed as one (a single small
        // job is too short to time against the GPU's clock ramp after idling).
        const int reps = timing_ ? std::max(1, std::atoi(SDL_getenv("D2K_BAKE_TIMING"))) : 1;
        for (int k = 1; k < reps; ++k) bake(cmd, todo);
        const double recordMs = double(SDL_GetTicksNS() - tg) / 1e6;
        if (timing_) {
            SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
            if (fence) {
                SDL_WaitForGPUFences(dev_, true, &fence, 1);
                SDL_ReleaseGPUFence(dev_, fence);
            }
        } else {
            SDL_SubmitGPUCommandBuffer(cmd);
        }
        rebakeMs_ = double(SDL_GetTicksNS() - tg) / 1e6 / reps;
        rebakeCells_ = cells;
        if (timing_)
            SDL_Log("Terrain: Designed (2D) re-baked %d region(s), %d cells in %.2f ms (CPU recording %.2f ms)",
                    int(todo.size()), cells, rebakeMs_, recordMs);
    }
    return ready_;
}

void TerrainBaker::bake(SDL_GPUCommandBuffer* cmd, const std::vector<Job>& jobs) {
    designed_->prepare(cmd);
    // The rocks that can show in (or cast a shadow into) each job's region: one subset per job,
    // all in one instance buffer. Margin: a rock's own reach + its shadow (at 40 degrees about
    // 1.2 x its height above the ground, < 64 px).
    constexpr float kMargin = DesignedTerrain::kRockReach + 72.0f;
    const auto& all = designed_->rocks();
    std::vector<DesignedTerrain::RockInst> sets;
    std::vector<DesignedTerrain::RockSet> rs(jobs.size());
    for (std::size_t j = 0; j < jobs.size(); ++j) {
        const Job& jb = jobs[j];
        const float x0 = float(jb.x0 * 32) - kMargin, x1 = float(jb.x1 * 32) + kMargin;
        const float z0 = float(jb.y0 * 32) - kMargin, z1 = float(jb.y1 * 32) + kMargin;
        for (const auto& ri : all) {
            if (ri.pos[0] < x0 || ri.pos[0] > x1 || ri.pos[2] < z0 || ri.pos[2] > z1) continue;
            const int l = std::clamp(int(ri.layer), 0, DesignedTerrain::kRockTypes - 1);
            if (rs[j].count[l]++ == 0) rs[j].first[l] = std::uint32_t(sets.size());
            sets.push_back(ri);  // `all` is sorted by layer, so each type's run stays contiguous
        }
    }
    const bool rockUpload = !sets.empty();
    if (rockUpload && sets.size() > rockSetCap_) {
        if (rockSets_) SDL_ReleaseGPUBuffer(dev_, rockSets_);
        rockSetCap_ = std::uint32_t(std::max<std::size_t>(sets.size(), 1024) * 3 / 2);
        SDL_GPUBufferCreateInfo bi{};
        bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bi.size = Uint32(rockSetCap_ * sizeof(DesignedTerrain::RockInst));
        rockSets_ = SDL_CreateGPUBuffer(dev_, &bi);
        if (!rockSets_) rockSetCap_ = 0;
    }
    for (auto& r : rs) r.vb = rockSets_;

    {
        std::vector<SDL_GPUTransferBuffer*> temps;
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        auto upload = [&](const void* data, Uint32 size, SDL_GPUBuffer* dstBuf, SDL_GPUTexture* dstTex) {
            SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0};
            SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
            if (!tb) return;
            std::memcpy(SDL_MapGPUTransferBuffer(dev_, tb, false), data, size);
            SDL_UnmapGPUTransferBuffer(dev_, tb);
            if (dstBuf) {
                SDL_GPUTransferBufferLocation src{tb, 0};
                SDL_GPUBufferRegion reg{dstBuf, 0, size};
                SDL_UploadToGPUBuffer(cp, &src, &reg, false);
            } else {
                SDL_GPUTextureTransferInfo src{tb, 0, 1, 1};
                SDL_GPUTextureRegion reg{};
                reg.texture = dstTex;
                reg.w = reg.h = reg.d = 1;
                SDL_UploadToGPUTexture(cp, &src, &reg, false);
            }
            temps.push_back(tb);
        };
        if (!maskUploaded_) {
            const std::uint32_t clear = 0;
            upload(&clear, 4, nullptr, clearMask_);
            maskUploaded_ = true;
        }
        if (meshDirty_) {
            meshDirty_ = false;
            const auto& tv = terrain_.vertices();
            const auto& ti = terrain_.indices();
            const Uint32 vbytes = Uint32(tv.size() * sizeof(TerrainVertex)), ibytes = Uint32(ti.size() * 4);
            for (SDL_GPUBuffer** b : {&vb_, &ib_}) {
                if (*b) SDL_ReleaseGPUBuffer(dev_, *b);
                *b = nullptr;
            }
            SDL_GPUBufferCreateInfo bi{};
            bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            bi.size = vbytes;
            vb_ = SDL_CreateGPUBuffer(dev_, &bi);
            bi.usage = SDL_GPU_BUFFERUSAGE_INDEX;
            bi.size = ibytes;
            ib_ = SDL_CreateGPUBuffer(dev_, &bi);
            indexCount_ = std::uint32_t(ti.size());
            if (vb_ && vbytes) upload(tv.data(), vbytes, vb_, nullptr);
            if (ib_ && ibytes) upload(ti.data(), ibytes, ib_, nullptr);
        }
        if (rockUpload && rockSets_)
            upload(sets.data(), Uint32(sets.size() * sizeof(DesignedTerrain::RockInst)), rockSets_, nullptr);
        SDL_EndGPUCopyPass(cp);
        for (auto* tb : temps) SDL_ReleaseGPUTransferBuffer(dev_, tb);
    }
    if (!vb_ || !ib_ || indexCount_ == 0) return;

    // The terrain mesh rows a region needs (the index buffer is row-major: one row of quads after
    // the other), with the shadow margin.
    const int gw = terrain_.gridW(), gh = terrain_.gridH();
    const float step = terrain_.step();
    auto rows = [&](float z0, float z1, std::uint32_t& first, std::uint32_t& count) {
        const int r0 = std::clamp(int(std::floor(z0 / step)), 0, gh - 2);
        const int r1 = std::clamp(int(std::ceil(z1 / step)), r0 + 1, gh - 1);
        first = std::uint32_t(r0) * std::uint32_t(gw - 1) * 6;
        count = std::uint32_t(r1 - r0) * std::uint32_t(gw - 1) * 6;
    };

    const float mapW = float(mapW_ * 32), mapH = float(mapH_ * 32);
    for (std::size_t j = 0; j < jobs.size(); ++j) {
        const Job& jb = jobs[j];
        Surface& c = *jb.s;
        const int ppc = c.ppc;
        const DesignedTerrain::RockSet* rocks = rockSets_ ? &rs[j] : nullptr;
        // A whole chunk / tile renders its full square (cells past the map edge stay black and
        // are never drawn); a part renders exactly its cells.
        const bool whole = jb.x0 == c.cx0 && jb.y0 == c.cy0 && jb.x1 == c.cx0 + c.cw && jb.y1 == c.cy0 + c.ch;
        const bool wholeChunk = whole && c.levels == kMipCopies && c.cells == kChunkCells;
        const int rx1 = whole ? c.cx0 + c.cells : jb.x1, ry1 = whole ? c.cy0 + c.cells : jb.y1;
        const float x0 = float(jb.x0 * 32), z0 = float(jb.y0 * 32), x1 = float(rx1 * 32), z1 = float(ry1 * 32);
        const int pw = (rx1 - jb.x0) * ppc, ph = (ry1 - jb.y0) * ppc;
        std::uint32_t firstIdx = 0, idxCount = 0;

        // 1. Sun shadow map of the region.
        float range = 1, extent[2] = {1, 1};
        const Mat4 lightVP = fitLight(x0, z0, x1, z1, range, extent);
        // The shadows' softness is set in shadow-map UV units, so it would shrink with the region
        // (a part, a detail tile): scale it to the light frustum of the whole chunk the region
        // lies in, so every bake has the same soft edge in world px as the chunks.
        float softScale = 1;
        if (!wholeChunk) {
            const int ccx = (jb.x0 / kChunkCells) * kChunkCells, ccy = (jb.y0 / kChunkCells) * kChunkCells;
            float r2 = 1, ec[2] = {1, 1};
            fitLight(float(ccx * 32), float(ccy * 32), float((ccx + kChunkCells) * 32), float((ccy + kChunkCells) * 32), r2, ec);
            softScale = std::sqrt((ec[0] * ec[1]) / std::max(1.0f, extent[0] * extent[1]));
        }
        SDL_GPUDepthStencilTargetInfo sti{};
        sti.texture = shadow_;
        sti.clear_depth = 1;
        sti.load_op = SDL_GPU_LOADOP_CLEAR;
        sti.store_op = SDL_GPU_STOREOP_STORE;
        sti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        sti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        if (SDL_GPURenderPass* sp = SDL_BeginGPURenderPass(cmd, nullptr, 0, &sti)) {
            SDL_BindGPUGraphicsPipeline(sp, terrainDepthPipe_);
            TerrainVU lvu;
            std::memcpy(lvu.viewProj, lightVP.m, sizeof lvu.viewProj);
            SDL_PushGPUVertexUniformData(cmd, 0, &lvu, sizeof lvu);
            SDL_GPUBufferBinding vb{vb_, 0};
            SDL_BindGPUVertexBuffers(sp, 0, &vb, 1);
            SDL_GPUBufferBinding ib{ib_, 0};
            SDL_BindGPUIndexBuffer(sp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            // Casters up-sun (north-west) of the region: the light comes from z - 1.2 h. (The
            // faithful bake's sun stands in the WSW: its casters lie a little to the south.)
            rows(z0 - 160.0f, z1 + (designed_->faithfulActive() ? 64.0f : 16.0f), firstIdx, idxCount);
            SDL_DrawGPUIndexedPrimitives(sp, idxCount, 1, firstIdx, 0, 0);
            designed_->drawShadow(sp, cmd, lightVP, shadowFormat_, rocks);
            SDL_EndGPURenderPass(sp);
        }

        // 2. The terrain from straight above: east = right, north = up, heights flattened.
        const Vec3 eye{0, kEyeHeight, 0};
        const Mat4 view = lookAt(eye, Vec3{0, 0, 0}, Vec3{0, 0, -1});
        const Mat4 proj = orthoRH(x0, x1, -z1, -z0, kEyeHeight - terrainTop_ - 256, kEyeHeight + 256);
        DesignedTerrain::Frame f;
        f.viewProj = proj * view;
        f.eye = Vec3{(x0 + x1) * 0.5f, kEyeHeight, (z0 + z1) * 0.5f};
        f.sun = sun_;
        f.darkest = kShadowKeep;
        if (designed_->faithfulActive()) {
            // The level ground's detail under the Designed sun (sand and plain rock as in
            // Designed), the relief under the original's; softer shadows and less contrast
            // between a cliff's lit and shaded sides, as in the original tiles.
            f.detailSun = detailSun_;
            f.darkest = 0.3f;
            f.reliefAmbient = 0.7f;
            f.reliefDiffuse = 0.5f;
        }
        // More contrast than the 3D view (whose tilt shows the shaded faces): seen from above the
        // relief reads only through light and shadow.
        f.ambient = 0.46f;
        f.diffuse = 0.78f;
        f.fogStart = 1e8f;
        f.fogEnd = 2e8f;
        f.mapW = mapW;
        f.mapH = mapH;
        f.shroudOn = false;
        f.bake = true;
        f.shroud = clearMask_;
        f.linearClamp = linearClamp_;
        f.lightVP = lightVP;
        f.shadowMap = shadow_;
        f.shadowSampler = shadowSampler_;
        // Texel size for the PCF taps: wider than the real texel (soft, not razor edges).
        // (Faithful: softer still - the original's shadows on the sand have no hard edge; the
        // wider taps need more depth bias, or level ground under the low sun shades itself.)
        const bool soft = designed_->faithfulActive();
        const float shadowSoft[4] = {1, (soft ? 4.0f : 2.0f) * softScale / float(kShadowSize), f.darkest,
                                     (soft ? 3.5f : 1.0f) / range};
        std::memcpy(f.shadow, shadowSoft, sizeof shadowSoft);
        // Straight into the texture when it is as big as the render targets (a whole chunk);
        // else through scratch_ and a copy.
        const bool direct = whole && c.px() == targetPx_;
        SDL_GPUTexture* dst = direct ? c.gpu[0] : scratch_;
        SDL_GPUColorTargetInfo cti{};
        cti.texture = msaa_ ? msaa_ : dst;
        cti.clear_color = SDL_FColor{0, 0, 0, 1};
        cti.load_op = SDL_GPU_LOADOP_CLEAR;
        cti.store_op = msaa_ ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE;
        cti.resolve_texture = msaa_ ? dst : nullptr;
        SDL_GPUDepthStencilTargetInfo dti{};
        dti.texture = depth_;
        dti.clear_depth = 1;
        dti.load_op = SDL_GPU_LOADOP_CLEAR;
        dti.store_op = SDL_GPU_STOREOP_DONT_CARE;
        dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        if (SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &cti, 1, &dti)) {
            const SDL_GPUViewport vp{0, 0, float(pw), float(ph), 0, 1};
            SDL_SetGPUViewport(pass, &vp);
            rows(z0 - 8.0f, z1 + 8.0f, firstIdx, idxCount);
            f.lights = &bakeLights_;  // no point lights; the time of day's colours
            designed_->draw(pass, cmd, vb_, ib_, idxCount, f, firstIdx, rocks);
            SDL_EndGPURenderPass(pass);
        }
        const int ox = (jb.x0 - c.cx0) * ppc, oy = (jb.y0 - c.cy0) * ppc;
        if (!direct) {  // the part into its place in the surface
            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTextureLocation src{};
            src.texture = scratch_;
            SDL_GPUTextureLocation to{};
            to.texture = c.gpu[0];
            to.x = Uint32(ox);
            to.y = Uint32(oy);
            SDL_CopyGPUTextureToTexture(cp, &src, &to, Uint32(pw), Uint32(ph), 1, false);
            SDL_EndGPUCopyPass(cp);
        }

        // 3. Half and quarter resolution copies (2x linear downsamples) of the region.
        for (int l = 1; l < c.levels; ++l) {
            SDL_GPUBlitInfo bi{};
            bi.source.texture = c.gpu[l - 1];
            bi.source.x = Uint32(ox >> (l - 1));
            bi.source.y = Uint32(oy >> (l - 1));
            bi.source.w = Uint32(pw >> (l - 1));
            bi.source.h = Uint32(ph >> (l - 1));
            bi.destination.texture = c.gpu[l];
            bi.destination.x = Uint32(ox >> l);
            bi.destination.y = Uint32(oy >> l);
            bi.destination.w = Uint32(pw >> l);
            bi.destination.h = Uint32(ph >> l);
            bi.load_op = SDL_GPU_LOADOP_LOAD;
            bi.filter = SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(cmd, &bi);
        }
    }
}

void TerrainBaker::evictTiles(std::size_t budget) {
    // Least recently used first; never a tile the current frame uses.
    while (detailBytes_ > budget) {
        auto lru = tiles_.end();
        for (auto it = tiles_.begin(); it != tiles_.end(); ++it)
            if (it->second.lastUsed < frame_ && (lru == tiles_.end() || it->second.lastUsed < lru->second.lastUsed)) lru = it;
        if (lru == tiles_.end()) break;
        detailBytes_ -= std::size_t(lru->second.px()) * lru->second.px() * 4;
        ++tilesEvicted_;
        releaseSurface(lru->second);
        tiles_.erase(lru);
    }
}

void TerrainBaker::updateDetail(const render::Camera& cam, int viewW, int viewH) {
    ++frame_;
    if (detailOff_ || !ready_ || cam.zoom <= 0) return;
    // The level: a cell's size on the screen in output pixels (the camera zoom includes the
    // display scale), at most ~8 % magnified; none while the chunks are sharp enough.
    const float need = 32.0f * cam.zoom;
    int want = -1;
    if (need > float(ppc_) * 1.1f) {
        for (int l = 0; l < kDetailLevels && want < 0; ++l)
            if (kDetailPpc[l] > ppc_ && float(kDetailPpc[l]) >= need * 0.92f) want = l;
        if (want < 0 && kDetailPpc[kDetailLevels - 1] > ppc_) want = kDetailLevels - 1;
    }
    if (want != wantLevel_) {
        wantLevel_ = want;
        wantSince_ = frame_;
    }
    const int tilesX = (mapW_ + kTileCells - 1) / kTileCells, tilesY = (mapH_ + kTileCells - 1) / kTileCells;
    const float tilePx = 32.0f * kTileCells;
    const float wx0 = cam.x, wy0 = cam.y, wx1 = cam.x + float(viewW) / cam.zoom, wy1 = cam.y + float(viewH) / cam.zoom;
    const int tx0 = std::clamp(int(std::floor(wx0 / tilePx)), 0, tilesX - 1);
    const int ty0 = std::clamp(int(std::floor(wy0 / tilePx)), 0, tilesY - 1);
    const int tx1 = std::clamp(int(std::floor((wx1 - 0.01f) / tilePx)), 0, tilesX - 1);
    const int ty1 = std::clamp(int(std::floor((wy1 - 0.01f) / tilePx)), 0, tilesY - 1);

    const bool sync = syncDetail_;
    syncDetail_ = false;
    const std::uint64_t budget = sync ? ~std::uint64_t(0) : detailTexelsPerFrame_;
    std::uint64_t texels = 0;
    std::vector<Job> jobs;
    std::vector<Tile*> fresh;
    // 1. Spice / concrete changed under resident tiles: the part of a tile in view is re-baked
    //    (it shows its old picture until then); tiles out of view are dropped (baked anew when
    //    they come back into view).
    auto collectDirty = [&](std::uint64_t limit) {
        for (auto it = tiles_.begin(); it != tiles_.end();) {
            Tile& t = it->second;
            if (t.dirty()) {
                if (t.lastUsed + 2 < frame_) {
                    detailBytes_ -= std::size_t(t.px()) * t.px() * 4;
                    ++tilesEvicted_;
                    releaseSurface(t);
                    it = tiles_.erase(it);
                    continue;
                }
                const std::uint64_t n = std::uint64_t(t.dx1 - t.dx0) * std::uint64_t(t.dy1 - t.dy0) * std::uint64_t(t.ppc) * t.ppc;
                if (texels == 0 || texels + n <= limit) {
                    jobs.push_back({&t, t.dx0, t.dy0, t.dx1, t.dy1});
                    texels += n;
                    t.dx1 = t.dx0;
                }
            }
            ++it;
        }
    };
    collectDirty(budget);
    // 2. Missing tiles of the wanted level: the view from its centre out, then a ring of one tile
    //    around it (scrolling); not while the zoom is still changing level (a wheel / pinch).
    constexpr std::uint64_t kSettleFrames = 6;
    if (want >= 0 && (sync || frame_ - wantSince_ >= kSettleFrames)) {
        struct Cand {
            int tx, ty;
            float d;
        };
        std::vector<Cand> cand;
        const float mx = (wx0 + wx1) * 0.5f / tilePx - 0.5f, my = (wy0 + wy1) * 0.5f / tilePx - 0.5f;
        for (int ty = std::max(0, ty0 - 1); ty <= std::min(tilesY - 1, ty1 + 1); ++ty)
            for (int tx = std::max(0, tx0 - 1); tx <= std::min(tilesX - 1, tx1 + 1); ++tx) {
                const bool vis = tx >= tx0 && tx <= tx1 && ty >= ty0 && ty <= ty1;
                if (sync && !vis) continue;
                auto it = tiles_.find(tileKey(want, tx, ty));
                if (it != tiles_.end()) {
                    it->second.lastUsed = frame_;
                    continue;
                }
                const float d = std::hypot(float(tx) - mx, float(ty) - my) + (vis ? 0.0f : 1000.0f);
                cand.push_back({tx, ty, d});
            }
        std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
        const int ppc = kDetailPpc[want];
        const std::size_t bytes = std::size_t(kTileCells * ppc) * std::size_t(kTileCells * ppc) * 4;
        for (const Cand& cd : cand) {
            const std::uint64_t n = std::uint64_t(kTileCells * kTileCells) * std::uint64_t(ppc) * std::uint64_t(ppc);
            if (texels > 0 && texels + n > budget) break;
            evictTiles(detailBudget_ > bytes ? detailBudget_ - bytes : 0);
            if (detailBytes_ + bytes > detailBudget_) break;  // the view alone fills the budget
            const std::uint64_t key = tileKey(want, cd.tx, cd.ty);
            Tile& t = tiles_[key];
            t.cx0 = cd.tx * kTileCells;
            t.cy0 = cd.ty * kTileCells;
            t.cw = std::min(kTileCells, mapW_ - t.cx0);
            t.ch = std::min(kTileCells, mapH_ - t.cy0);
            t.cells = kTileCells;
            t.ppc = ppc;
            t.levels = 1;
            t.lastUsed = frame_;
            if (!makeSurfaceTextures(t)) {
                releaseSurface(t);
                tiles_.erase(key);
                detailOff_ = true;  // out of memory: the chunks only from now on
                SDL_Log("Terrain: Designed (2D) detail tiles off");
                break;
            }
            detailBytes_ += bytes;
            peakDetailBytes_ = std::max(peakDetailBytes_, detailBytes_);
            ++tilesBaked_;
            jobs.push_back({&t, t.cx0, t.cy0, t.cx0 + t.cw, t.cy0 + t.ch});
            fresh.push_back(&t);
            texels += n;
        }
    }
    auto run = [&](const char* what) {
        if (jobs.empty()) return;
        Uint64 t0 = SDL_GetTicksNS();
        if (timing_) {
            SDL_WaitForGPUIdle(dev_);
            t0 = SDL_GetTicksNS();
        }
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        if (!cmd) return;
        bake(cmd, jobs);
        const double recordMs = double(SDL_GetTicksNS() - t0) / 1e6;
        if (timing_) {
            SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
            if (fence) {
                SDL_WaitForGPUFences(dev_, true, &fence, 1);
                SDL_ReleaseGPUFence(dev_, fence);
            }
            SDL_Log("Terrain: Designed (2D) detail %s: %d job(s), %.2f M texels at %d px per cell in %.2f ms (CPU "
                    "recording %.2f ms); resident %zu tiles, %.1f MB",
                    what, int(jobs.size()), double(texels) / 1e6, wantLevel_ >= 0 ? kDetailPpc[wantLevel_] : 0,
                    double(SDL_GetTicksNS() - t0) / 1e6, recordMs, tiles_.size(), double(detailBytes_) / (1024.0 * 1024.0));
        } else {
            SDL_SubmitGPUCommandBuffer(cmd);
        }
        for (Tile* t : fresh) t->baked = true;
        fresh.clear();
        jobs.clear();
    };
    run(sync ? "first view" : "tiles");
    if (sync && bakeTest_) {  // self-check: partial re-bakes of the tiles must match their full bake
        for (int y = 2; y + 3 < mapH_; y += 7)
            for (int x = 3; x + 3 < mapW_; x += 6)
                for (auto& [k, t] : tiles_) t.mark(x, y, x + 3, y + 3);
        texels = 0;
        collectDirty(~std::uint64_t(0));
        run("test re-bake");
    }
    // Free what no view has used for a while, and keep under the budget.
    for (auto it = tiles_.begin(); it != tiles_.end();) {
        if (it->second.lastUsed + kTileIdleFrames < frame_) {
            detailBytes_ -= std::size_t(it->second.px()) * it->second.px() * 4;
            ++tilesEvicted_;
            releaseSurface(it->second);
            it = tiles_.erase(it);
        } else {
            ++it;
        }
    }
    evictTiles(detailBudget_);
}

bool TerrainBaker::drawTerrain(SDL_Renderer* r, const render::Camera& viewCam, int viewW, int viewH) {
    if (!ready_ || chunks_.empty()) return false;
    // D2K_BAKE_PAN="dx dy": a streaming test - the terrain alone scrolls dx, dy world px per frame
    // under the view (bounces at the map edges), so detail tiles are baked and evicted on the way.
    render::Camera cam = viewCam;
    if (const char* e = SDL_getenv("D2K_BAKE_PAN")) {
        float dx = 0, dy = 0;
        if (std::sscanf(e, "%f %f", &dx, &dy) >= 1) {
            auto bounce = [](float v, float range) {
                if (range <= 0) return 0.0f;
                const float m = std::fmod(std::fabs(v), 2 * range);
                return m < range ? m : 2 * range - m;
            };
            const float rx = mapW_ * 32.0f - float(viewW) / cam.zoom, ry = mapH_ * 32.0f - float(viewH) / cam.zoom;
            cam.x = bounce(cam.x + dx * float(frame_), rx);
            cam.y = bounce(cam.y + dy * float(frame_), ry);
        }
    }
    // The tiles' screen mapping (MapRenderer::drawTerrain): every cell edge at round(t * 32 * zoom)
    // minus the rounded camera origin, so the chunks sit exactly where the tiles would.
    const double ts = 32.0 * double(cam.zoom);
    const double originX = std::round(cam.x * double(cam.zoom));
    const double originY = std::round(cam.y * double(cam.zoom));
    auto edge = [&](int t, double origin) { return float(std::round(t * ts) - origin); };
    // The sharpest copy that is minified at most 2x.
    int level = 0;
    while (level + 1 < kMipCopies && float(ppc_ >> level) > 2.0f * 32.0f * cam.zoom + 1e-3f) ++level;
    const float texPerCell = float(ppc_ >> level);
    for (const Chunk& c : chunks_) {
        const float left = edge(c.cx0, originX), right = edge(c.cx0 + c.cw, originX);
        const float top = edge(c.cy0, originY), bottom = edge(c.cy0 + c.ch, originY);
        if (right <= 0 || bottom <= 0 || left >= float(viewW) || top >= float(viewH)) continue;
        const SDL_FRect src{0, 0, float(c.cw) * texPerCell, float(c.ch) * texPerCell};
        const SDL_FRect dst{left, top, right - left, bottom - top};
        SDL_RenderTexture(r, c.sdl[level], &src, &dst);
    }
    // Zoomed in: the detail tiles over the chunks - the wanted level, else (while it bakes) the
    // resident level nearest to it that is still sharper than the chunks.
    updateDetail(cam, viewW, viewH);
    if (wantLevel_ < 0 || tiles_.empty()) return true;
    const float tilePx = 32.0f * kTileCells;
    const int tilesX = (mapW_ + kTileCells - 1) / kTileCells, tilesY = (mapH_ + kTileCells - 1) / kTileCells;
    const int tx0 = std::clamp(int(std::floor(cam.x / tilePx)), 0, tilesX - 1);
    const int ty0 = std::clamp(int(std::floor(cam.y / tilePx)), 0, tilesY - 1);
    const int tx1 = std::clamp(int(std::floor((cam.x + float(viewW) / cam.zoom - 0.01f) / tilePx)), 0, tilesX - 1);
    const int ty1 = std::clamp(int(std::floor((cam.y + float(viewH) / cam.zoom - 0.01f) / tilePx)), 0, tilesY - 1);
    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
            Tile* best = nullptr;
            for (int k = 0; k < 2 * kDetailLevels && !best; ++k) {
                const int l = wantLevel_ + ((k & 1) ? -(k + 1) / 2 : k / 2);  // want, want - 1, want + 1, ...
                if (l < 0 || l >= kDetailLevels || kDetailPpc[l] <= ppc_) continue;
                auto it = tiles_.find(tileKey(l, tx, ty));
                if (it != tiles_.end() && it->second.baked) best = &it->second;
            }
            if (!best) continue;
            best->lastUsed = frame_;
            const float left = edge(best->cx0, originX), right = edge(best->cx0 + best->cw, originX);
            const float top = edge(best->cy0, originY), bottom = edge(best->cy0 + best->ch, originY);
            const SDL_FRect src{0, 0, float(best->cw * best->ppc), float(best->ch * best->ppc)};
            const SDL_FRect dst{left, top, right - left, bottom - top};
            SDL_RenderTexture(r, best->sdl[0], &src, &dst);
        }
    return true;
}

}  // namespace d2k::render3d

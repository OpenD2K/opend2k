#include "render3d/ground_clutter.hpp"

#include "formats/model3d.hpp"
#include "game/world.hpp"
#include "render/art_pack.hpp"
#include "render3d/gpu.hpp"
#include "render3d/model_gltf.hpp"
#include "render3d/shader_blobs.hpp"
#include "render3d/surround.hpp"
#include "render3d/terrain.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "../../third_party/stb/stb_image.h"

namespace d2k::render3d {

namespace fs = std::filesystem;

namespace {

struct VU {
    float viewProj[16];
};
// Fragment uniform buffer 0 of clutter_fs (FU in render3d/clutter_msl.hpp / ClutterFU in shaders.hlsl).
struct FU {
    float sunDir[4], light[4], fogColor[4], fog[4], fog2[4];
    float tint[GroundClutter::kMaxLayers][4];
    float look[GroundClutter::kMaxLayers][4];
    float dust[4];
    float lightVP[16];
    float shadow[4];
};

float hashf(int x, int y, int salt) {
    std::uint32_t h = std::uint32_t(x) * 0x8da6b343u ^ std::uint32_t(y) * 0xd8163841u ^ std::uint32_t(salt) * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return float(h & 0xFFFFFF) / float(0xFFFFFF);
}

GroundClutter::Class classFromName(const std::string& s, bool& ok) {
    static const char* kNames[GroundClutter::kClasses] = {"pebble", "gravel", "slab", "tuft", "boulder",
                                                          "rubble", "shrub", "deadwood", "scrap"};
    for (int i = 0; i < GroundClutter::kClasses; ++i)
        if (s == kNames[i]) {
            ok = true;
            return GroundClutter::Class(i);
        }
    ok = false;
    return GroundClutter::kPebble;
}

float lum(const float* c) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; }

}  // namespace

std::string GroundClutter::packDir() {
    std::error_code ec;
    if (const char* e = std::getenv("D2K_CLUTTER_PACK"); e && *e)
        return fs::exists(fs::path(e) / "clutter.ini", ec) ? std::string(e) : std::string();
    for (const auto& d : render::ArtPack::searchDirs()) {
        const fs::path p = fs::path(d) / "ground-clutter" / "clutter";
        if (fs::exists(p / "clutter.ini", ec)) return p.string();
    }
    return {};
}

std::unique_ptr<GroundClutter> GroundClutter::create(SDL_GPUDevice* dev, SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth,
                                                     SDL_GPUSampleCount samples, int aniso) {
    const std::string dir = packDir();
    // (Without the pack only the procedural spice clumps: docs/3d-mode.md "Spice fields in 3D".)
    if (dir.empty())
        SDL_Log("3D: ground clutter pack not installed (artpacks/ground-clutter/clutter) - Reimagined without clutter, "
                "only the spice clumps");
    std::unique_ptr<GroundClutter> p(new GroundClutter(dev));
    if (!p->load(dir) || !p->init(color, depth, samples, aniso)) return nullptr;
    return p;
}

GroundClutter::~GroundClutter() {
    for (SDL_GPUGraphicsPipeline* pp : {pipe_, depthPipe_})
        if (pp) SDL_ReleaseGPUGraphicsPipeline(dev_, pp);
    if (sampler_) SDL_ReleaseGPUSampler(dev_, sampler_);
    if (texArr_) SDL_ReleaseGPUTexture(dev_, texArr_);
    for (SDL_GPUBuffer* b : {meshVB_, meshIB_, viewVB_, shadowVB_})
        if (b) SDL_ReleaseGPUBuffer(dev_, b);
    if (stage_) SDL_ReleaseGPUTransferBuffer(dev_, stage_);
}

bool GroundClutter::load(const std::string& dir) {
    const Uint64 t0 = SDL_GetTicksNS();
    std::ifstream in;
    if (!dir.empty()) in.open(fs::path(dir) / "clutter.ini");
    std::string line;
    while (in && std::getline(in, line)) {
        std::istringstream ls(line);
        std::string kw;
        if (!(ls >> kw) || kw[0] == '#') continue;
        if (kw == "layer") {
            int idx = -1;
            std::string file, kind;
            ls >> idx >> file >> kind;
            if (idx < 0 || idx >= kMaxLayers) continue;
            int w = 0, h = 0, n = 0;
            stbi_uc* px = stbi_load((fs::path(dir) / file).string().c_str(), &w, &h, &n, 4);
            if (!px) {
                SDL_Log("3D: ground clutter: cannot read %s", file.c_str());
                continue;
            }
            auto& dst = layerPx_[idx];
            dst.resize(std::size_t(kTex) * kTex);
            double sum = 0;
            for (int y = 0; y < kTex; ++y)
                for (int x = 0; x < kTex; ++x) {
                    const stbi_uc* s = &px[(std::size_t(y * h / kTex) * w + std::size_t(x * w / kTex)) * 4];
                    dst[std::size_t(y) * kTex + x] =
                        std::uint32_t(s[0]) | (std::uint32_t(s[1]) << 8) | (std::uint32_t(s[2]) << 16) | 0xFF000000u;
                    sum += 0.299 * s[0] + 0.587 * s[1] + 0.114 * s[2];
                }
            stbi_image_free(px);
            layerMeanL_[idx] = float(sum / double(kTex * kTex) / 255.0);
            layerKind_[idx] = kind == "wood" ? 1 : kind == "plant" ? 2 : kind == "metal" ? 3 : 0;
            layers_ = std::max(layers_, idx + 1);
        } else if (kw == "piece") {
            Type t;
            std::string cls;
            ls >> t.name >> t.layer >> cls >> t.heightRatio;
            bool ok = false;
            t.cls = classFromName(cls, ok);
            if (!ok || t.layer < 0 || t.layer >= kMaxLayers || int(types_.size()) >= kMaxTypes) continue;
            bool good = true;
            for (int lod = 0; lod < 2 && good; ++lod) {
                formats::Model3D m;
                std::string err;
                GltfAdjust adj;
                adj.geometryOnly = true;
                const fs::path f = fs::path(dir) / (t.name + (lod ? "_lod1.glb" : ".glb"));
                if (!loadGltfModel(f.string(), adj, m, err)) {
                    if (lod == 1 && t.count[0] > 0) {  // no LOD 1: LOD 0 again
                        t.first[1] = t.first[0];
                        t.count[1] = t.count[0];
                        break;
                    }
                    SDL_Log("3D: ground clutter: %s: %s", f.filename().string().c_str(), err.c_str());
                    good = false;
                    break;
                }
                t.first[lod] = std::uint32_t(idx_.size());
                // The loader gives a triangle soup in world px at 1 unit = 32 px (the kit's unit is the piece's
                // width): welded into indexed vertices (a third of the vertex shader work).
                std::map<std::array<float, 8>, std::uint32_t> seen;
                for (const formats::MeshPart* part : {&m.body, &m.turret})
                    for (const formats::MeshVertex& v : part->tris) {
                        const std::array<float, 8> vals = {v.pos[0] / 32.0f, v.pos[1] / 32.0f, v.pos[2] / 32.0f, v.nrm[0],
                                                           v.nrm[1],         v.nrm[2],          v.uv[0],          v.uv[1]};
                        auto [it, fresh] = seen.emplace(vals, std::uint32_t(verts_.size() / 8));
                        if (fresh) verts_.insert(verts_.end(), vals.begin(), vals.end());
                        idx_.push_back(it->second);
                    }
                t.count[lod] = std::uint32_t(idx_.size()) - t.first[lod];
            }
            if (good && t.count[0] > 0) types_.push_back(t);
        }
    }
    for (int i = 0; i < int(types_.size()); ++i)
        if (!layerPx_[types_[std::size_t(i)].layer].empty()) byClass_[types_[std::size_t(i)].cls].push_back(i);
    int usable = 0;
    for (auto& v : byClass_) usable += int(v.size());
    if (usable == 0 || layers_ == 0) {  // (no usable pack: the spice clumps alone)
        types_.clear();
        verts_.clear();
        idx_.clear();
        for (auto& v : byClass_) v.clear();
        for (auto& v : layerPx_) v.clear();
        layers_ = 0;
        usable = 0;
    }
    if (!dir.empty())
        SDL_Log("3D: ground clutter pack %s: %d piece types (%zu vertices, %zu triangles), %d texture layers in %.0f ms", dir.c_str(),
                usable, verts_.size() / 8, idx_.size() / 3, layers_, double(SDL_GetTicksNS() - t0) / 1e6);
    addSpicePieces();
    return !types_.empty() && layers_ > 0;
}

bool GroundClutter::init(SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth, SDL_GPUSampleCount samples, int aniso) {
    SDL_GPUShader* vs = createShader(dev_, ShaderId::ClutterVs, 0, 1);
    SDL_GPUShader* fs = createShader(dev_, ShaderId::ClutterFs, 3, 2, 1);
    bool ok = vs && fs;
    if (ok) {
        SDL_GPUColorTargetDescription ctd{};
        ctd.format = color;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = vs;
        pi.fragment_shader = fs;
        SDL_GPUVertexBufferDescription vbd[2] = {{0, 8 * 4, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
                                                 {1, sizeof(Inst), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0}};
        SDL_GPUVertexAttribute at[7] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},  {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
                                        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24}, {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0},
                                        {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16}, {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 32},
                                        {6, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 48}};
        pi.vertex_input_state = {vbd, 2, at, 7};
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;  // two-sided (blades, plates)
        pi.rasterizer_state.enable_depth_clip = gpuDepthClip();
        pi.multisample_state.sample_count = samples;
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = true;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        pi.target_info.color_target_descriptions = &ctd;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth;
        pi.target_info.has_depth_stencil_target = true;
        pipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!pipe_) SDL_Log("3D: ground clutter pipeline failed: %s", SDL_GetError());
        ok = pipe_ != nullptr;
    }
    for (SDL_GPUShader* s : {vs, fs})
        if (s) SDL_ReleaseGPUShader(dev_, s);
    if (!ok) return false;
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.max_lod = 16;
    si.enable_anisotropy = gpuAnisotropy() && aniso > 1;
    si.max_anisotropy = float(std::clamp(aniso, 1, 16));
    sampler_ = SDL_CreateGPUSampler(dev_, &si);
    int levels = 1;
    while ((kTex >> levels) >= 1) ++levels;
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ci.width = ci.height = Uint32(kTex);
    ci.layer_count_or_depth = Uint32(std::max(layers_, 2));
    ci.num_levels = Uint32(levels);
    texArr_ = SDL_CreateGPUTexture(dev_, &ci);
    SDL_GPUBufferCreateInfo bi{};
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size = Uint32(verts_.size() * 4);
    meshVB_ = SDL_CreateGPUBuffer(dev_, &bi);
    bi.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    bi.size = Uint32(idx_.size() * 4);
    meshIB_ = SDL_CreateGPUBuffer(dev_, &bi);
    if (!sampler_ || !texArr_ || !meshVB_ || !meshIB_) SDL_Log("3D: ground clutter: GPU resources failed: %s", SDL_GetError());
    return sampler_ && texArr_ && meshVB_ && meshIB_;
}

// ------------------------------------------------------------------------------------------------ placement

void GroundClutter::build(const game::World& w, const Terrain& t, const DesignedTerrain& d, float density) {
    density_ = std::clamp(density, 0.0f, 1.0f);
    // The palette (docs/3d-mode.md "Reimagined": terrain3_fs reimagine) from the map's colour lock: the stones are the
    // rust-brown Reimagined rock, the dust the golden sand.
    auto reim = [](const float* c, float out[3]) {
        const float L = lum(c);
        static constexpr float kWarm[3] = {1.08f, 0.98f, 0.83f};
        for (int k = 0; k < 3; ++k) out[k] = (L + (c[k] - L) * 0.47f) * kWarm[k] / 255.0f;
    };
    float sand[3], rockBase[3], rock[3];
    reim(d.materialColour(DesignedTerrain::kSand), sand);
    reim(d.materialColour(DesignedTerrain::kRock), rockBase);
    {
        const float L = lum(d.materialColour(DesignedTerrain::kRock)) / 255.0f;
        static constexpr float kRust[3] = {1.2f, 0.96f, 0.76f};
        for (int k = 0; k < 3; ++k) rock[k] = rockBase[k] * 0.4f + L * kRust[k] * 1.05f * 0.6f;
    }
    std::memcpy(sand_, sand, sizeof sand_);
    for (int l = 0; l < kMaxLayers; ++l) {
        const float mL = std::max(0.02f, layerMeanL_[l]);
        float* tn = tint_[l];
        float* lk = look_[l];
        switch (layerKind_[l]) {
            case 0:  // rock: recoloured by luminance to the Reimagined rock, a little of the scan's hue
                // (darker than the rock ground: desert varnish - loose stones read against sand and rock alike)
                for (int k = 0; k < 3; ++k) tn[k] = rock[k] * 0.74f / mL;
                tn[3] = mL;
                lk[0] = 0.15f, lk[1] = 0.85f, lk[2] = 1.0f;
                break;
            case 1: {  // dead wood: sun-bleached grey-brown
                static constexpr float kWood[3] = {0.37f, 0.32f, 0.27f};
                for (int k = 0; k < 3; ++k) tn[k] = kWood[k] / mL;
                tn[3] = mL;
                lk[0] = 0.2f, lk[1] = 0.9f, lk[2] = 0.5f;
                break;
            }
            case 2:  // plants: the procedural colours, warmed a little towards the map's sand
                for (int k = 0; k < 3; ++k) tn[k] = 0.92f * (1.0f + 0.3f * (sand[k] / std::max(0.05f, lum(sand)) - 1.0f));
                tn[3] = -1;
                lk[2] = 0.3f;
                break;
            default:  // metal: the rust as authored
                tn[0] = tn[1] = tn[2] = 1.0f;
                tn[3] = -1;
                lk[2] = 0.6f;
                break;
        }
    }
    // The spice clumps' layer: recoloured by luminance to a rust / cinnamon a little darker than the spice ground
    // (terrain3_fs reimagine: thin (1.42, 0.84, 0.52) x 0.9 of the luminance, the granules (0.62, 0.3, 0.15)), so
    // a clump reads as a lump of the same stuff in its own shadow; vivid: the round-2 orange-red.
    if (spiceLayer_ >= 0) {
        const float mL = std::max(0.02f, layerMeanL_[spiceLayer_]);
        static constexpr float kVivid[3] = {0.74f, 0.37f, 0.16f}, kGraded[3] = {0.64f, 0.33f, 0.17f};
        for (int k = 0; k < 3; ++k) {
            spiceTint_[0][k] = kVivid[k] / mL;
            spiceTint_[1][k] = kGraded[k] / mL;
        }
        spiceTint_[0][3] = spiceTint_[1][3] = mL;
        std::memcpy(tint_[spiceLayer_], spiceTint_[0], sizeof spiceTint_[0]);
        float* lk = look_[spiceLayer_];
        lk[0] = 0.0f, lk[1] = 0.85f, lk[2] = 0.0f, lk[3] = 0.0f;
    }
    place(w, t);
    // Spice clumps: every chunk.
    {
        const Uint64 ts = SDL_GetTicksNS();
        spiceChW_ = (cw_ + kChunk - 1) / kChunk;
        spiceChH_ = (ch_ + kChunk - 1) / kChunk;
        const std::size_t nCh = std::size_t(spiceChW_) * spiceChH_;
        spiceChunk_.assign(nCh, {});
        spiceTy_.assign(nCh, {});
        spiceLo_.assign(nCh, 1e9f);
        spiceHi_.assign(nCh, -1e9f);
        spiceDirty_.assign(nCh, 0);
        spiceAmt_.assign(std::size_t(cw_) * ch_, 0);
        spiceTotal_ = 0;
        for (int y = 0; y < ch_; ++y)
            for (int x = 0; x < cw_; ++x) spiceAmt_[std::size_t(y) * cw_ + x] = std::uint8_t(std::clamp(w.cell(x, y).spice(), 0, 255));
        for (int c = 0; c < int(nCh); ++c) placeSpice(w, t, c);
        SDL_Log("3D: spice clumps: %d in %.1f ms", spiceTotal_, double(SDL_GetTicksNS() - ts) / 1e6);
    }
    placeBeyond(t, nullptr);  // (the renderer places it again for the new map)
}

void GroundClutter::place(const game::World& w, const Terrain& t) {
    namespace cf = game::cell_flags;
    const Uint64 t0 = SDL_GetTicksNS();
    cw_ = w.width();
    ch_ = w.height();
    inst_.clear();
    meta_.clear();
    const std::size_t nCells = std::size_t(cw_) * ch_;
    hidden_.assign(nCells, 0);
    if (density_ <= 0.0f || types_.empty()) return;
    const auto& kinds = t.cellKinds();
    auto flags = [&](int x, int y) { return w.cell(x, y).flags; };
    auto inMap = [&](int x, int y) { return x >= 0 && y >= 0 && x < cw_ && y < ch_; };
    auto passable = [&](int x, int y) { return !inMap(x, y) || (flags(x, y) & (cf::kDriveOn | cf::kWalkOn)) != 0; };
    auto excluded = [&](int x, int y) {
        if (!inMap(x, y)) return true;
        const game::Cell& c = w.cell(x, y);
        return (c.flags & (cf::kBuilding | cf::kWall | cf::kConcrete)) != 0 || c.spice() > 0 ||
               t.foundationAt(x, y, -1e9f) > -1e8f;
    };
    // Distance (cells, 8-connected, up to 3) to the nearest cell no unit uses.
    std::vector<std::uint8_t> dImp(nCells, 3);
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x)
            if (!passable(x, y)) dImp[std::size_t(y) * cw_ + x] = 0;
    for (int pass = 0; pass < 3; ++pass)
        for (int y = 0; y < ch_; ++y)
            for (int x = 0; x < cw_; ++x) {
                std::uint8_t& v = dImp[std::size_t(y) * cw_ + x];
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (inMap(x + dx, y + dy)) v = std::min<std::uint8_t>(v, std::uint8_t(dImp[std::size_t(y + dy) * cw_ + x + dx] + 1));
            }
    const auto& verts = t.vertices();
    auto sunAt = [&](float x, float z) {
        const int gx = std::clamp(int(std::lround(x / t.step())), 0, t.gridW() - 1);
        const int gz = std::clamp(int(std::lround(z / t.step())), 0, t.gridH() - 1);
        return verts[std::size_t(gz) * t.gridW() + gx].sun;
    };
    static constexpr float kConform[kClasses] = {1.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.8f, 0.25f, 0.9f, 0.8f};
    // Picks a type of a class (by a hash); -1 if the pack has none.
    auto pick = [&](Class c, float r) {
        const auto& v = byClass_[c];
        return v.empty() ? -1 : v[std::min(v.size() - 1, std::size_t(r * float(v.size())))];
    };
    // Every cell a circle (centre, radius) touches satisfies `ok`.
    auto footprint = [&](float px, float pz, float r, auto ok) {
        const float d = r * 0.7071f;
        const float pts[9][2] = {{0, 0}, {r, 0}, {-r, 0}, {0, r}, {0, -r}, {d, d}, {-d, d}, {d, -d}, {-d, -d}};
        for (const auto& p : pts) {
            const int cx = int(std::floor((px + p[0]) / 32)), cy = int(std::floor((pz + p[1]) / 32));
            if (!ok(cx, cy)) return false;
        }
        return true;
    };
    auto add = [&](int type, float px, float pz, float size, float squash, float extraSink, float dust, int salt, int cx, int cy) {
        const Type& ty = types_[std::size_t(type)];
        const float h = t.height(px, pz);
        const float conform = kConform[ty.cls];
        const float reach = size * 0.5f;
        const float e = std::max(2.0f, reach * 0.5f);
        float sx = (t.height(px + e, pz) - t.height(px - e, pz)) / (2 * e);
        float sz = (t.height(px, pz + e) - t.height(px, pz - e)) / (2 * e);
        const float lim = 1.2f;
        sx = std::clamp(sx * conform, -lim, lim);
        sz = std::clamp(sz * conform, -lim, lim);
        // Sunk so that no part of its base floats: the (sheared) base plane under the ground at every footprint point,
        // but never buried by more than ~3/4 of its height (a piece at the foot of a face leans into it).
        const float pieceH = std::max(0.5f, size * squash * ty.heightRatio);
        float sink = 0;
        for (int a = 0; a < 8; ++a) {
            const float an = float(a) * 0.785398f, dx = std::cos(an) * reach * 0.85f, dz = std::sin(an) * reach * 0.85f;
            sink = std::max(sink, h + sx * dx + sz * dz - t.height(px + dx, pz + dz));
        }
        sink = std::min(sink + extraSink, std::max(extraSink, pieceH * 0.75f));
        Inst in{};
        in.pos[0] = px;
        in.pos[1] = h - sink;
        in.pos[2] = pz;
        in.yaw = hashf(cx * 7 + salt, cy * 13, 401) * 6.2831853f;
        in.scale[0] = in.scale[2] = size;
        in.scale[1] = size * squash;
        in.layer = float(ty.layer);
        in.sun = sunAt(px, pz);
        in.height = pieceH;
        in.dust = dust;
        in.bright = ty.cls <= kSlab ? 0.62f + 0.45f * hashf(cx, cy, 500 + salt) : 0.8f + 0.3f * hashf(cx, cy, 500 + salt);
        in.slopeX = sx;
        in.slopeZ = sz;
        in.hue = hashf(cx, cy, 600 + salt) * 2 - 1;
        inst_.push_back(in);
        meta_.push_back({std::int32_t(cy * cw_ + cx), std::uint8_t(type), std::uint8_t(ty.cls), sink, conform, reach});
    };
    // A small piece (passable-safe: under kPassableMaxHeight above the ground).
    auto addSmall = [&](Class c, float px, float pz, float size, float dust, int salt, int cx, int cy, float rType) {
        const int type = pick(c, rType);
        if (type < 0) return;
        const float hr = types_[std::size_t(type)].heightRatio;
        float squash = c == kTuft ? 0.8f + 0.4f * hashf(cx, cy, 700 + salt) : 0.75f + 0.35f * hashf(cx, cy, 700 + salt);
        if (size * squash * hr > kPassableMaxHeight) squash = kPassableMaxHeight / (size * hr);
        // Not over a building / concrete / spice cell beside it.
        if (!footprint(px, pz, size * 0.5f, [&](int x, int y) { return !excluded(x, y); })) return;
        add(type, px, pz, size, squash, c == kGravel || c == kSlab ? 0.2f : 0.0f, dust, salt, cx, cy);
    };
    const float dens = density_;
    int counts[kClasses] = {};
    // A big piece (boulder, rubble, shrub, dead wood, scrap): only where every cell it touches is one no unit uses
    // (shrunk up to twice to fit). False when it does not fit.
    auto placeBig = [&](Class c, float px, float pz, float size, int salt, int cx, int cy, float rt) {
        const int type = pick(c, rt);
        if (type < 0) return false;
        for (int tries = 0; tries < 3; ++tries) {
            if (footprint(px, pz, size * 0.5f, [&](int x, int y) { return inMap(x, y) && !passable(x, y) && !excluded(x, y); })) {
                const float squash = c == kBoulder ? 0.7f + 0.5f * hashf(cx, cy, 24 + salt) : 0.85f + 0.3f * hashf(cx, cy, 24 + salt);
                const float dust = layerKind_[types_[std::size_t(type)].layer] == 0 ? 0.15f + 0.35f * hashf(cx, cy, 25 + salt) : 0.3f;
                add(type, px, pz, size, squash, c == kBoulder ? 0.6f + 0.05f * size * hashf(cx, cy, 26 + salt) : 0.3f, dust, salt, cx, cy);
                ++counts[c];
                return true;
            }
            size *= 0.65f;
            if (size < 6) break;
        }
        return false;
    };
    for (int cy = 0; cy < ch_; ++cy)
        for (int cx = 0; cx < cw_; ++cx) {
            if (excluded(cx, cy)) continue;
            const std::size_t ci = std::size_t(cy) * cw_ + cx;
            const float x0 = float(cx) * 32, z0 = float(cy) * 32;
            if (!passable(cx, cy)) {
                // The foot of the rock along every side that meets ground units use: boulders and talus rubble
                // leaning against it, now and then a dry shrub (the face rises inside this cell: the pieces stay in it).
                static constexpr int kDx[4] = {1, -1, 0, 0}, kDy[4] = {0, 0, 1, -1};
                for (int sd = 0; sd < 4; ++sd) {
                    const int nx = cx + kDx[sd], ny = cy + kDy[sd];
                    if (!inMap(nx, ny) || !passable(nx, ny) || excluded(nx, ny)) continue;
                    for (int k = 0; k < 3; ++k) {
                        const int salt = 1000 + sd * 50 + k * 7;
                        if (hashf(cx, cy, salt) > 0.72f * dens) continue;
                        const float r = hashf(cx, cy, salt + 1), rs = hashf(cx, cy, salt + 2), rt = hashf(cx, cy, salt + 3);
                        const Class c = r < 0.3f ? kRubble : r < 0.86f ? kBoulder : r < 0.95f ? kShrub : kDeadwood;
                        float size = c == kRubble ? 16 + rs * 10 : c == kBoulder ? 7 + rs * rs * 12 : c == kShrub ? 9 + rs * 4 : 11 + rs * 5;
                        // Along the side (3 slots), in from it by half the piece + a little.
                        const float along = (float(k) + 0.15f + 0.7f * hashf(cx, cy, salt + 4)) * (32.0f / 3.0f);
                        const float in = size * 0.5f + 1.0f + 3.0f * hashf(cx, cy, salt + 5);
                        float px, pz;
                        if (kDx[sd] != 0) {
                            px = kDx[sd] > 0 ? x0 + 32 - in : x0 + in;
                            pz = z0 + along;
                        } else {
                            pz = kDy[sd] > 0 ? z0 + 32 - in : z0 + in;
                            px = x0 + along;
                        }
                        placeBig(c, px, pz, size, salt, cx, cy, rt);
                    }
                }
                // Cliffs, their feet and ledges: the big pieces (only where every cell they touch is unused by units).
                for (int k = 0; k < 6; ++k) {
                    const int salt = k * 17;
                    const float px = x0 + 3 + hashf(cx, cy, 10 + salt) * 26, pz = z0 + 3 + hashf(cx, cy, 11 + salt) * 26;
                    const float h = t.height(px, pz);
                    const float e = 3.0f;
                    const float gx = (t.height(px + e, pz) - t.height(px - e, pz)) / (2 * e);
                    const float gz = (t.height(px, pz + e) - t.height(px, pz - e)) / (2 * e);
                    const float s = std::sqrt(gx * gx + gz * gz);
                    if (s > 1.3f) continue;  // a near-vertical face: nothing sticks there
                    // Relief above the point (a cliff rising behind it: its foot / talus).
                    float above = 0;
                    for (int a = 0; a < 8; ++a) {
                        const float an = float(a) * 0.785398f;
                        for (float r : {18.0f, 36.0f}) above = std::max(above, t.height(px + std::cos(an) * r, pz + std::sin(an) * r) - h);
                    }
                    const float foot = std::clamp(above / 36.0f, 0.0f, 1.0f) * (1.0f - std::clamp((s - 0.45f) / 0.85f, 0.0f, 1.0f));
                    const float ledge = s < 0.4f && above < 10.0f ? 1.0f : 0.0f;
                    const float p = (0.08f + 0.5f * foot + 0.16f * ledge) * dens;
                    if (hashf(cx, cy, 20 + salt) > p) continue;
                    const float r = hashf(cx, cy, 21 + salt), rt = hashf(cx, cy, 22 + salt), rs = hashf(cx, cy, 23 + salt);
                    Class c;
                    if (foot > 0.4f) c = r < 0.24f ? kRubble : r < 0.66f ? kBoulder : r < 0.76f ? kShrub : r < 0.8f ? kDeadwood
                                       : r < 0.81f ? kScrap : r < 0.9f ? kGravel : kPebble;
                    else c = r < 0.38f ? kBoulder : r < 0.6f ? kShrub : r < 0.65f ? kDeadwood : r < 0.658f ? kScrap
                           : r < 0.82f ? kGravel : kPebble;
                    if ((c == kShrub || c == kDeadwood || c == kScrap) && s > 0.45f) c = kBoulder;  // plants / scrap on level ground
                    float size = 0;
                    switch (c) {
                        case kBoulder: size = 8 + rs * rs * (foot > 0.4f ? 22 : 14); break;
                        case kRubble: size = 22 + rs * 16; break;
                        case kShrub: size = 9 + rs * 8; break;
                        case kDeadwood: size = 12 + rs * 9; break;
                        case kScrap: size = 14 + rs * 12; break;
                        default: break;
                    }
                    if (size > 0) {
                        if (placeBig(c, px, pz, size, salt, cx, cy, rt)) continue;
                        c = r < 0.5f ? kGravel : kPebble;
                    }
                    const float sz = c == kGravel ? 10 + rs * 10 : 3 + rs * 4;
                    addSmall(c, px, pz, sz, 0.3f + 0.4f * rt, salt, cx, cy, rt);
                    ++counts[c];
                }
                continue;
            }
            // Cells units use: only flat, small things.
            const int d = dImp[ci];
            const bool rockCell = ci < kinds.size() && kinds[ci] == Terrain::kRockCell;
            const bool dunes = w.cell(cx, cy).terrain() == 2;
            int n;
            float p;
            if (d == 1) n = 4, p = 0.5f;
            else if (d == 2) n = 3, p = 0.26f;
            else if (rockCell) n = 2, p = 0.2f;
            else n = 2, p = dunes ? 0.03f : 0.06f;
            for (int k = 0; k < n; ++k) {
                const int salt = 200 + k * 17;
                if (hashf(cx, cy, 30 + salt) > p * dens) continue;
                const float px = x0 + 2 + hashf(cx, cy, 31 + salt) * 28, pz = z0 + 2 + hashf(cx, cy, 32 + salt) * 28;
                const float r = hashf(cx, cy, 33 + salt), rs = hashf(cx, cy, 34 + salt), rt = hashf(cx, cy, 35 + salt);
                Class c;
                if (d <= 2) c = r < 0.33f ? kGravel : r < 0.7f ? kPebble : r < 0.85f ? kSlab : kTuft;
                else if (rockCell) c = r < 0.45f ? kSlab : r < 0.8f ? kPebble : r < 0.95f ? kGravel : kTuft;
                else c = r < 0.5f ? kPebble : r < 0.66f ? kGravel : r < 0.9f ? kTuft : kSlab;
                float size;
                switch (c) {
                    case kGravel: size = 10 + rs * (d <= 2 ? 12 : 8); break;
                    case kSlab: size = 6 + rs * 9; break;
                    case kTuft: size = 4 + rs * 3; break;
                    default: size = 3 + rs * (d == 1 ? 4.0f : 3.0f); break;
                }
                const float dust = c == kSlab ? 0.05f + 0.15f * rt : rockCell ? 0.15f + 0.25f * rt : 0.2f + 0.35f * rt;
                addSmall(c, px, pz, size, dust, salt, cx, cy, rt);
                ++counts[c];
            }
        }
    // Chunks (kChunk x kChunk cells) for culling; instances sorted by chunk.
    chW_ = (cw_ + kChunk - 1) / kChunk;
    chH_ = (ch_ + kChunk - 1) / kChunk;
    const std::size_t nCh = std::size_t(chW_) * chH_;
    std::vector<std::uint32_t> order(inst_.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = std::uint32_t(i);
    auto chunkOf = [&](std::size_t i) {
        const int c = meta_[i].cell;
        return std::size_t((c / cw_) / kChunk) * chW_ + std::size_t((c % cw_) / kChunk);
    };
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return chunkOf(a) < chunkOf(b); });
    std::vector<Inst> si(inst_.size());
    std::vector<Meta> sm(meta_.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        si[i] = inst_[order[i]];
        sm[i] = meta_[order[i]];
    }
    inst_.swap(si);
    meta_.swap(sm);
    chunkFirst_.assign(nCh, 0);
    chunkCount_.assign(nCh, 0);
    chunkLo_.assign(nCh, 1e9f);
    chunkHi_.assign(nCh, -1e9f);
    for (std::size_t i = 0; i < inst_.size(); ++i) {
        const std::size_t c = chunkOf(i);
        if (chunkCount_[c]++ == 0) chunkFirst_[c] = std::uint32_t(i);
    }
    refreshHeights(t);  // (the chunks' height ranges)
    // Check of the readability rule: the tallest piece over any cell units use (top above the lowest ground under it).
    float tallest = 0;
    for (std::size_t i = 0; i < inst_.size(); ++i) {
        const Inst& in = inst_[i];
        const float r = meta_[i].reach;
        const float pts[5][2] = {{0, 0}, {r, 0}, {-r, 0}, {0, r}, {0, -r}};
        bool touches = false;
        float low = 1e9f;
        for (const auto& p : pts) {
            touches = touches || passable(int(std::floor((in.pos[0] + p[0]) / 32)), int(std::floor((in.pos[2] + p[1]) / 32)));
            low = std::min(low, t.height(in.pos[0] + p[0], in.pos[2] + p[1]));
        }
        if (touches) tallest = std::max(tallest, in.pos[1] + in.height - std::max(low, in.pos[1]));
    }
    SDL_Log("3D: ground clutter: tallest piece over cells units use %.1f px", tallest);
    SDL_Log("3D: ground clutter: %zu pieces (pebble %d, gravel %d, slab %d, tuft %d, boulder %d, rubble %d, shrub %d, "
            "dead wood %d, scrap %d) in %.0f ms",
            inst_.size(), counts[kPebble], counts[kGravel], counts[kSlab], counts[kTuft], counts[kBoulder], counts[kRubble],
            counts[kShrub], counts[kDeadwood], counts[kScrap], double(SDL_GetTicksNS() - t0) / 1e6);
}

// The scatter beyond the map (docs/3d-mode.md "World beyond the map"): per cell of the surround's ring a few
// hashed slots - mostly empty on open sand, more at the foot of rock (rubble, boulders, a shrub now and
// then), flat stones on rock tops. Nothing out there is driven over, so no size rule; sparser than the map.
void GroundClutter::placeBeyond(const Terrain& t, const Surround* s) {
    beyond_.clear();
    beyondMeta_.clear();
    beyondChunks_.clear();
    if (!s || !s->valid() || density_ <= 0.0f || types_.empty() || cw_ <= 0 || ch_ <= 0) return;
    const int mc = Surround::kMarginCells;
    const float mapW = float(cw_) * 32.0f, mapH = float(ch_) * 32.0f;
    const float reachOut = Surround::margin() - 130.0f;  // (beyond that the ring is flat sand)
    auto H = [&](float x, float z) { return s->height(x, z); };
    auto pick = [&](Class c, float r) {
        const auto& v = byClass_[c];
        return v.empty() ? -1 : v[std::min(v.size() - 1, std::size_t(r * float(v.size())))];
    };
    static constexpr float kConform[kClasses] = {1.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.8f, 0.25f, 0.9f, 0.8f};
    std::vector<std::uint32_t> key;
    const int chW = (cw_ + 2 * mc + kChunk - 1) / kChunk;
    for (int cy = -mc; cy < ch_ + mc; ++cy)
        for (int cx = -mc; cx < cw_ + mc; ++cx) {
            if (cx >= 0 && cy >= 0 && cx < cw_ && cy < ch_) continue;
            const float x0 = float(cx) * 32, z0 = float(cy) * 32;
            const float ox = std::max({-(x0 + 16), x0 + 16 - mapW, 0.0f}), oz = std::max({-(z0 + 16), z0 + 16 - mapH, 0.0f});
            if (std::max(ox, oz) > reachOut) continue;
            for (int k = 0; k < 3; ++k) {
                const int salt = 3000 + k * 19;
                const float px = x0 + 2 + hashf(cx, cy, salt) * 28, pz = z0 + 2 + hashf(cx, cy, salt + 1) * 28;
                if (px > -6 && pz > -6 && px < mapW + 6 && pz < mapH + 6) continue;  // not over the map's edge
                const float h = H(px, pz);
                const float e = 3.0f;
                const float gx = (H(px + e, pz) - H(px - e, pz)) / (2 * e), gz = (H(px, pz + e) - H(px, pz - e)) / (2 * e);
                const float slope = std::sqrt(gx * gx + gz * gz);
                if (slope > 1.3f) continue;  // a near-vertical face
                float above = 0;
                for (int a = 0; a < 8; ++a) {
                    const float an = float(a) * 0.785398f;
                    for (float r : {18.0f, 36.0f}) above = std::max(above, H(px + std::cos(an) * r, pz + std::sin(an) * r) - h);
                }
                const float foot = std::clamp(above / 36.0f, 0.0f, 1.0f) * (1.0f - std::clamp((slope - 0.45f) / 0.85f, 0.0f, 1.0f));
                const float rock = s->rockAt(t, px, pz);
                if (hashf(cx, cy, salt + 2) > (0.035f + 0.4f * foot + 0.06f * rock) * density_) continue;
                const float r = hashf(cx, cy, salt + 3), rs = hashf(cx, cy, salt + 4), rt = hashf(cx, cy, salt + 5);
                Class c;
                if (foot > 0.4f) c = r < 0.28f ? kRubble : r < 0.74f ? kBoulder : r < 0.84f ? kShrub : r < 0.88f ? kDeadwood : kGravel;
                else if (rock > 0.5f) c = r < 0.4f ? kSlab : r < 0.72f ? kPebble : r < 0.88f ? kGravel : kBoulder;
                else c = r < 0.45f ? kPebble : r < 0.63f ? kGravel : r < 0.88f ? kTuft : kBoulder;
                if ((c == kShrub || c == kDeadwood) && slope > 0.45f) c = kBoulder;
                float size = 0;
                switch (c) {
                    case kBoulder: size = 7 + rs * rs * (foot > 0.4f ? 20 : 9); break;
                    case kRubble: size = 20 + rs * 14; break;
                    case kShrub: size = 9 + rs * 7; break;
                    case kDeadwood: size = 12 + rs * 8; break;
                    case kGravel: size = 10 + rs * 9; break;
                    case kSlab: size = 6 + rs * 9; break;
                    case kTuft: size = 4 + rs * 3; break;
                    default: size = 3 + rs * 3; break;
                }
                const int type = pick(c, rt);
                if (type < 0) continue;
                const Type& ty = types_[std::size_t(type)];
                const float conform = kConform[ty.cls], reach = size * 0.5f;
                const float ee = std::max(2.0f, reach * 0.5f);
                const float sx = std::clamp((H(px + ee, pz) - H(px - ee, pz)) / (2 * ee) * conform, -1.2f, 1.2f);
                const float sz = std::clamp((H(px, pz + ee) - H(px, pz - ee)) / (2 * ee) * conform, -1.2f, 1.2f);
                const float squash = c == kBoulder ? 0.7f + 0.5f * hashf(cx, cy, salt + 6) : 0.8f + 0.35f * hashf(cx, cy, salt + 6);
                const float pieceH = std::max(0.5f, size * squash * ty.heightRatio);
                const float extra = c == kBoulder ? 0.6f + 0.05f * size * hashf(cx, cy, salt + 7) : c == kGravel || c == kSlab ? 0.2f : c == kRubble ? 0.3f : 0.0f;
                float sink = 0;
                for (int a = 0; a < 8; ++a) {
                    const float an = float(a) * 0.785398f, dx = std::cos(an) * reach * 0.85f, dz = std::sin(an) * reach * 0.85f;
                    sink = std::max(sink, h + sx * dx + sz * dz - H(px + dx, pz + dz));
                }
                sink = std::min(sink + extra, std::max(extra, pieceH * 0.75f));
                Inst in{};
                in.pos[0] = px;
                in.pos[1] = h - sink;
                in.pos[2] = pz;
                in.yaw = hashf(cx * 7 + salt, cy * 13, 401) * 6.2831853f;
                in.scale[0] = in.scale[2] = size;
                in.scale[1] = size * squash;
                in.layer = float(ty.layer);
                in.sun = 1.0f;
                in.height = pieceH;
                in.dust = layerKind_[ty.layer] == 0 ? 0.15f + 0.35f * rt : 0.3f;
                in.bright = ty.cls <= kSlab ? 0.62f + 0.45f * hashf(cx, cy, 500 + salt) : 0.8f + 0.3f * hashf(cx, cy, 500 + salt);
                in.slopeX = sx;
                in.slopeZ = sz;
                in.hue = hashf(cx, cy, 600 + salt) * 2 - 1;
                beyond_.push_back(in);
                beyondMeta_.push_back({0, std::uint8_t(type), std::uint8_t(ty.cls), sink, conform, reach});
                key.push_back(std::uint32_t(((cy + mc) / kChunk) * chW + (cx + mc) / kChunk));
            }
        }
    // Sorted into chunks (kChunk x kChunk cells of the ring) for culling.
    std::vector<std::uint32_t> order(beyond_.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = std::uint32_t(i);
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return key[a] < key[b]; });
    std::vector<Inst> si(beyond_.size());
    std::vector<Meta> sm(beyond_.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        si[i] = beyond_[order[i]];
        sm[i] = beyondMeta_[order[i]];
        if (i == 0 || key[order[i]] != key[order[i - 1]]) beyondChunks_.push_back({std::uint32_t(i), 0});
        ++beyondChunks_.back().count;
    }
    beyond_.swap(si);
    beyondMeta_.swap(sm);
    refreshBeyond(*s);
    SDL_Log("3D: ground clutter: %zu pieces beyond the map in %zu chunks", beyond_.size(), beyondChunks_.size());
}

void GroundClutter::refreshBeyond(const Surround& s) {
    for (BeyondChunk& c : beyondChunks_) {
        c.x0 = c.z0 = c.lo = 1e9f;
        c.x1 = c.z1 = c.hi = -1e9f;
        for (std::uint32_t i = c.first; i < c.first + c.count; ++i) {
            Inst& in = beyond_[i];
            const Meta& m = beyondMeta_[i];
            in.pos[1] = s.height(in.pos[0], in.pos[2]) - m.sink;
            const float r = m.reach * 1.2f + 2;
            c.x0 = std::min(c.x0, in.pos[0] - r);
            c.x1 = std::max(c.x1, in.pos[0] + r);
            c.z0 = std::min(c.z0, in.pos[2] - r);
            c.z1 = std::max(c.z1, in.pos[2] + r);
            c.lo = std::min(c.lo, in.pos[1] - r);
            c.hi = std::max(c.hi, in.pos[1] + in.height + r);
        }
    }
}

void GroundClutter::refreshHeights(const Terrain& t) {
    std::fill(chunkLo_.begin(), chunkLo_.end(), 1e9f);
    std::fill(chunkHi_.begin(), chunkHi_.end(), -1e9f);
    for (std::size_t i = 0; i < inst_.size(); ++i) {
        Inst& in = inst_[i];
        const Meta& m = meta_[i];
        const float h = t.height(in.pos[0], in.pos[2]);
        in.pos[1] = h - m.sink;
        const int c = m.cell;
        const std::size_t ch = std::size_t((c / cw_) / kChunk) * chW_ + std::size_t((c % cw_) / kChunk);
        chunkLo_[ch] = std::min(chunkLo_[ch], in.pos[1] - m.reach * 1.2f - 2);
        chunkHi_[ch] = std::max(chunkHi_[ch], in.pos[1] + in.height + m.reach * 1.2f + 2);
    }
    for (std::size_t c = 0; c < spiceChunk_.size(); ++c) {  // the spice clumps (Inst::pad = their sink)
        float lo = 1e9f, hi = -1e9f;
        for (Inst& in : spiceChunk_[c]) {
            in.pos[1] = t.height(in.pos[0], in.pos[2]) - in.pad;
            lo = std::min(lo, in.pos[1] - 2);
            hi = std::max(hi, in.pos[1] + in.height + 2);
        }
        spiceLo_[c] = lo;
        spiceHi_[c] = hi;
    }
}

// ------------------------------------------------------------------------------------------------ per frame

void GroundClutter::cull(const game::World& w, const Terrain& t, const Mat4& viewProj, const Mat4* lightVP, float pxPerWorld) {
    namespace cf = game::cell_flags;
    view_.clear();
    shadow_.clear();
    shadowCulled_ = lightVP != nullptr;
    if ((inst_.empty() && beyond_.empty()) || w.width() != cw_ || w.height() != ch_) return;
    // Hidden now: buildings (and their whole footprint, also ones built after the map was loaded), walls, concrete, spice.
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            const game::Cell& c = w.cell(x, y);
            // (1 = hidden for the clutter; 2 = also for the spice clumps: a building, a wall, concrete, a footprint.)
            const bool built = (c.flags & (cf::kBuilding | cf::kWall | cf::kConcrete)) != 0 || t.foundationAt(x, y, -1e9f) > -1e8f;
            hidden_[std::size_t(y) * cw_ + x] = built ? 3 : c.spice() > 0 ? 1 : 0;
        }
    auto hiddenAt = [&](float x, float z) {
        const int cx = int(std::floor(x / 32)), cy = int(std::floor(z / 32));
        return cx < 0 || cy < 0 || cx >= cw_ || cy >= ch_ || hidden_[std::size_t(cy) * cw_ + cx];
    };
    // Zoomed far out: LOD 1, no pebbles / tufts (a few screen px), and the small flat pieces cast no shadow.
    lod_ = pxPerWorld < 2.0f ? 1 : 0;
    const bool tiny = pxPerWorld >= 0.9f;
    const bool flatSmall = pxPerWorld >= 0.75f;  // gravel / slabs: a few screen px below this
    const bool smallShadows = pxPerWorld >= 1.6f;
    // The spice clumps: a few px wide - drawn from 0.6 screen px per world px, casting from 1.2.
    const bool spiceShow = pxPerWorld >= 0.6f, spiceShadows = pxPerWorld >= 1.2f;
    auto planes = [](const Mat4& m, Vec4 (&pl)[4]) {
        for (int i = 0; i < 4; ++i) {
            const int row = i / 2;
            const float sgn = (i & 1) ? -1.0f : 1.0f;
            Vec4 p{m.at(3, 0) + sgn * m.at(row, 0), m.at(3, 1) + sgn * m.at(row, 1), m.at(3, 2) + sgn * m.at(row, 2),
                   m.at(3, 3) + sgn * m.at(row, 3)};
            const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            if (len > 0) p = Vec4{p.x / len, p.y / len, p.z / len, p.w / len};
            pl[i] = p;
        }
    };
    auto run = [&](const Mat4& m, bool shadowPass, std::vector<Inst>& out, std::uint32_t* first, std::uint32_t* count) {
        Vec4 pl[4];
        planes(m, pl);
        for (auto& v : perType_) v.clear();
        for (int cy = 0; cy < chH_; ++cy)
            for (int cx = 0; cx < chW_; ++cx) {
                const std::size_t c = std::size_t(cy) * chW_ + cx;
                if (chunkCount_[c] == 0) continue;
                // The chunk's box (its pieces reach over its edge by up to ~20 px).
                const float x0 = float(cx * kChunk * 32) - 24, x1 = float((cx + 1) * kChunk * 32) + 24;
                const float z0 = float(cy * kChunk * 32) - 24, z1 = float((cy + 1) * kChunk * 32) + 24;
                const float y0 = chunkLo_[c], y1 = chunkHi_[c];
                bool in = true;
                for (const Vec4& p : pl) {
                    const float px = p.x > 0 ? x1 : x0, py = p.y > 0 ? y1 : y0, pz = p.z > 0 ? z1 : z0;
                    if (p.x * px + p.y * py + p.z * pz + p.w < 0) {
                        in = false;
                        break;
                    }
                }
                if (!in) continue;
                for (std::uint32_t i = chunkFirst_[c]; i < chunkFirst_[c] + chunkCount_[c]; ++i) {
                    const Meta& mt = meta_[i];
                    const bool small = mt.cls <= kTuft;
                    if ((mt.cls == kPebble || mt.cls == kTuft) && !tiny) continue;
                    if ((mt.cls == kGravel || mt.cls == kSlab) && !flatSmall) continue;
                    if (shadowPass && small && !smallShadows) continue;
                    const Inst& ins = inst_[i];
                    const float r = mt.reach;
                    if (hidden_[std::size_t(mt.cell)] || hiddenAt(ins.pos[0] + r, ins.pos[2]) || hiddenAt(ins.pos[0] - r, ins.pos[2]) ||
                        hiddenAt(ins.pos[0], ins.pos[2] + r) || hiddenAt(ins.pos[0], ins.pos[2] - r))
                        continue;
                    perType_[mt.type].push_back(ins);
                }
            }
        // The spice clumps (placeSpice), by their own chunks.
        if (spiceShow && (!shadowPass || spiceShadows))
            for (int cy = 0; cy < spiceChH_; ++cy)
                for (int cx = 0; cx < spiceChW_; ++cx) {
                    const std::size_t c = std::size_t(cy) * spiceChW_ + cx;
                    if (c >= spiceChunk_.size() || spiceChunk_[c].empty()) continue;
                    const float x0 = float(cx * kChunk * 32) - 12, x1 = float((cx + 1) * kChunk * 32) + 12;
                    const float z0 = float(cy * kChunk * 32) - 12, z1 = float((cy + 1) * kChunk * 32) + 12;
                    const float y0 = spiceLo_[c], y1 = spiceHi_[c];
                    bool in = true;
                    for (const Vec4& p : pl) {
                        const float px = p.x > 0 ? x1 : x0, py = p.y > 0 ? y1 : y0, pz = p.z > 0 ? z1 : z0;
                        if (p.x * px + p.y * py + p.z * pz + p.w < 0) {
                            in = false;
                            break;
                        }
                    }
                    if (!in) continue;
                    const std::vector<Inst>& v = spiceChunk_[c];
                    const std::vector<std::uint8_t>& ty = spiceTy_[c];
                    for (std::size_t i = 0; i < v.size(); ++i) {
                        const Inst& ins = v[i];
                        const int hx = int(ins.pos[0] / 32), hz = int(ins.pos[2] / 32);
                        if (hx < 0 || hz < 0 || hx >= cw_ || hz >= ch_ || (hidden_[std::size_t(hz) * cw_ + hx] & 2)) continue;
                        perType_[ty[i]].push_back(ins);
                    }
                }
        // Beyond the map (placeBeyond): never hidden by a cell (the shader takes the edge's shroud).
        for (const BeyondChunk& bc : beyondChunks_) {
            bool in = true;
            for (const Vec4& p : pl) {
                const float px = p.x > 0 ? bc.x1 : bc.x0, py = p.y > 0 ? bc.hi : bc.lo, pz = p.z > 0 ? bc.z1 : bc.z0;
                if (p.x * px + p.y * py + p.z * pz + p.w < 0) {
                    in = false;
                    break;
                }
            }
            if (!in) continue;
            for (std::uint32_t i = bc.first; i < bc.first + bc.count; ++i) {
                const Meta& mt = beyondMeta_[i];
                if ((mt.cls == kPebble || mt.cls == kTuft) && !tiny) continue;
                if ((mt.cls == kGravel || mt.cls == kSlab) && !flatSmall) continue;
                if (shadowPass && mt.cls <= kTuft && !smallShadows) continue;
                perType_[mt.type].push_back(beyond_[i]);
            }
        }
        for (int ty = 0; ty < int(types_.size()); ++ty) {
            first[ty] = std::uint32_t(out.size());
            out.insert(out.end(), perType_[ty].begin(), perType_[ty].end());
            count[ty] = std::uint32_t(out.size()) - first[ty];
        }
    };
    run(viewProj, false, view_, viewFirst_, viewCount_);
    if (lightVP) run(*lightVP, true, shadow_, shadowFirst_, shadowCount_);
    // D2K_CLUTTER_STATS=1: the zoom and what is drawn (measurement).
    static const bool stats = [] {
        const char* e = std::getenv("D2K_CLUTTER_STATS");
        return e && e[0] == '1';
    }();
    static int frame = 0;
    if (stats && ++frame % 120 == 0) {
        std::size_t tv = 0, ts = 0;
        for (int ty = 0; ty < int(types_.size()); ++ty) {
            tv += std::size_t(viewCount_[ty]) * types_[std::size_t(ty)].count[lod_] / 3;
            ts += std::size_t(shadowCount_[ty]) * types_[std::size_t(ty)].count[1] / 3;
        }
        SDL_Log("3D: ground clutter: %.2f screen px per world px, LOD %d, %zu drawn (%zu triangles), %zu casting (%zu triangles)",
                pxPerWorld, lod_, view_.size(), tv, shadow_.size(), ts);
    }
}

void GroundClutter::prepare(SDL_GPUCommandBuffer* cmd) {
    const Uint32 vBytes = Uint32(view_.size() * sizeof(Inst)), sBytes = Uint32(shadow_.size() * sizeof(Inst));
    const Uint32 staticBytes = staticUploaded_ ? 0 : Uint32(verts_.size() * 4);
    const Uint32 total = vBytes + sBytes;
    auto grow = [&](SDL_GPUBuffer*& b, std::uint32_t& cap, Uint32 bytes) {
        if (bytes == 0 || (b && bytes <= cap)) return;
        if (b) SDL_ReleaseGPUBuffer(dev_, b);
        std::uint32_t c = std::max<std::uint32_t>(64 * 1024, cap);
        while (c < bytes) c *= 2;
        SDL_GPUBufferCreateInfo bi{};
        bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bi.size = c;
        b = SDL_CreateGPUBuffer(dev_, &bi);
        cap = b ? c : 0;
    };
    grow(viewVB_, viewCap_, vBytes);
    grow(shadowVB_, shadowCap_, sBytes);
    if (!staticUploaded_) {
        // The meshes and the textures, once (its own transfer buffers).
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        std::vector<SDL_GPUTransferBuffer*> temps;
        auto stage = [&](const void* data, Uint32 size) -> SDL_GPUTransferBuffer* {
            SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0};
            SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
            if (!tb) return nullptr;
            void* p = SDL_MapGPUTransferBuffer(dev_, tb, false);
            std::memcpy(p, data, size);
            SDL_UnmapGPUTransferBuffer(dev_, tb);
            temps.push_back(tb);
            return tb;
        };
        if (SDL_GPUTransferBuffer* tb = stage(verts_.data(), staticBytes)) {
            SDL_GPUTransferBufferLocation src{tb, 0};
            SDL_GPUBufferRegion reg{meshVB_, 0, staticBytes};
            SDL_UploadToGPUBuffer(cp, &src, &reg, false);
        }
        if (SDL_GPUTransferBuffer* tb = stage(idx_.data(), Uint32(idx_.size() * 4))) {
            SDL_GPUTransferBufferLocation src{tb, 0};
            SDL_GPUBufferRegion reg{meshIB_, 0, Uint32(idx_.size() * 4)};
            SDL_UploadToGPUBuffer(cp, &src, &reg, false);
        }
        for (int l = 0; l < layers_; ++l) {
            if (layerPx_[l].empty()) continue;
            if (SDL_GPUTransferBuffer* tb = stage(layerPx_[l].data(), Uint32(layerPx_[l].size() * 4))) {
                SDL_GPUTextureTransferInfo src{tb, 0, Uint32(kTex), Uint32(kTex)};
                SDL_GPUTextureRegion reg{};
                reg.texture = texArr_;
                reg.layer = Uint32(l);
                reg.w = reg.h = Uint32(kTex);
                reg.d = 1;
                SDL_UploadToGPUTexture(cp, &src, &reg, false);
            }
        }
        SDL_EndGPUCopyPass(cp);
        for (auto* tb : temps) SDL_ReleaseGPUTransferBuffer(dev_, tb);
        SDL_GenerateMipmapsForGPUTexture(cmd, texArr_);
        for (auto& v : layerPx_) std::vector<std::uint32_t>().swap(v);
        staticUploaded_ = true;
    }
    if (total == 0) return;
    if (!stage_ || total > stageCap_) {
        if (stage_) SDL_ReleaseGPUTransferBuffer(dev_, stage_);
        std::uint32_t c = std::max<std::uint32_t>(128 * 1024, stageCap_);
        while (c < total) c *= 2;
        SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, c, 0};
        stage_ = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        stageCap_ = stage_ ? c : 0;
    }
    auto* p = stage_ ? static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(dev_, stage_, true)) : nullptr;
    if (!p) {
        view_.clear();
        shadow_.clear();
        return;
    }
    if (vBytes) std::memcpy(p, view_.data(), vBytes);
    if (sBytes) std::memcpy(p + vBytes, shadow_.data(), sBytes);
    SDL_UnmapGPUTransferBuffer(dev_, stage_);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    if (vBytes && viewVB_) {
        SDL_GPUTransferBufferLocation src{stage_, 0};
        SDL_GPUBufferRegion reg{viewVB_, 0, vBytes};
        SDL_UploadToGPUBuffer(cp, &src, &reg, true);
    }
    if (sBytes && shadowVB_) {
        SDL_GPUTransferBufferLocation src{stage_, vBytes};
        SDL_GPUBufferRegion reg{shadowVB_, 0, sBytes};
        SDL_UploadToGPUBuffer(cp, &src, &reg, true);
    }
    SDL_EndGPUCopyPass(cp);
    if (!viewVB_) view_.clear();
    if (!shadowVB_) shadow_.clear();
}

int GroundClutter::draw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const DesignedTerrain::Frame& f) {
    if (view_.empty() || !staticUploaded_ || !viewVB_) return 0;
    // The Reimagined light (as DesignedTerrain::draw): less sky, more sun.
    const float ambient = f.reimagined ? 0.42f : f.ambient, diffuse = f.reimagined ? 0.98f : f.diffuse;
    SDL_BindGPUGraphicsPipeline(pass, pipe_);
    bindLightGrid(pass, f.lightGrid, lightFallback_, dev_);
    {
        static PointLightsU none{};
        SDL_PushGPUFragmentUniformData(cmd, 1, f.lights ? f.lights : &none, sizeof(PointLightsU));
    }
    VU vu;
    std::memcpy(vu.viewProj, f.viewProj.m, sizeof vu.viewProj);
    SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
    FU fu{};
    const float flat = 1.0f / (ambient + diffuse * f.sun.y);
    const float s[4] = {f.sun.x, f.sun.y, f.sun.z, 0}, l[4] = {ambient, diffuse, flat, f.darkest},
                fc[4] = {f.fogColor[0], f.fogColor[1], f.fogColor[2], 1}, fo[4] = {f.eye.x, f.eye.y, f.eye.z, f.fogStart},
                f2[4] = {f.fogEnd, f.mapW, f.mapH, DesignedTerrain::surroundFog2(f)};
    std::memcpy(fu.sunDir, s, sizeof s);
    std::memcpy(fu.light, l, sizeof l);
    std::memcpy(fu.fogColor, fc, sizeof fc);
    std::memcpy(fu.fog, fo, sizeof fo);
    std::memcpy(fu.fog2, f2, sizeof f2);
    std::memcpy(fu.tint, tint_, sizeof fu.tint);
    std::memcpy(fu.look, look_, sizeof fu.look);
    fu.dust[0] = sand_[0];
    fu.dust[1] = sand_[1];
    fu.dust[2] = sand_[2];
    if (f.naturalGrade) {
        // Realistic grade (docs/3d-mode.md): the pieces follow the calmer ground palette (terrain3_fs reimagine) -
        // the stones the greyer rock (rust 1.2 / 0.96 / 0.76 -> 1.12 / 0.97 / 0.83), the dust the paler sand.
        static constexpr float kRock[3] = {1.12f / 1.2f, 0.97f / 0.96f, 0.83f / 0.76f};
        static constexpr float kSand[3] = {1.0f / 1.08f, 0.95f / 0.98f, 0.845f / 0.83f};
        for (int l = 0; l < kMaxLayers; ++l)
            if (layerKind_[l] == 0)
                for (int k = 0; k < 3; ++k) fu.tint[l][k] *= kRock[k];
        for (int k = 0; k < 3; ++k) fu.dust[k] *= kSand[k];
    }
    if (spiceLayer_ >= 0) std::memcpy(fu.tint[spiceLayer_], spiceTint_[f.naturalGrade ? 1 : 0], sizeof spiceTint_[0]);
    fu.dust[3] = f.reimagined ? 1.6f : 0.0f;
    std::memcpy(fu.lightVP, f.lightVP.m, sizeof fu.lightVP);
    std::memcpy(fu.shadow, f.shadow, sizeof fu.shadow);
    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
    SDL_GPUBufferBinding vbs[2] = {{meshVB_, 0}, {viewVB_, 0}};
    SDL_BindGPUVertexBuffers(pass, 0, vbs, 2);
    SDL_GPUBufferBinding ib{meshIB_, 0};
    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_GPUTextureSamplerBinding sb[3] = {{texArr_, sampler_}, {f.shroud, f.linearClamp}, {f.shadowMap, f.shadowSampler}};
    SDL_BindGPUFragmentSamplers(pass, 0, sb, 3);
    int calls = 0;
    for (int ty = 0; ty < int(types_.size()); ++ty) {
        const Type& t = types_[std::size_t(ty)];
        if (viewCount_[ty] == 0 || t.count[lod_] == 0) continue;
        SDL_DrawGPUIndexedPrimitives(pass, t.count[lod_], viewCount_[ty], t.first[lod_], 0, viewFirst_[ty]);
        ++calls;
    }
    return calls;
}

int GroundClutter::drawShadow(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Mat4& lightVP, SDL_GPUTextureFormat fmt) {
    if (shadow_.empty() || !staticUploaded_ || !shadowVB_) return 0;
    if (!depthPipe_ && !depthFailed_) {
        SDL_GPUShader* vs = createShader(dev_, ShaderId::ClutterVs, 0, 1);
        SDL_GPUShader* fs = createShader(dev_, ShaderId::DepthFs, 0, 0);
        if (vs && fs) {
            SDL_GPUGraphicsPipelineCreateInfo pi{};
            pi.vertex_shader = vs;
            pi.fragment_shader = fs;
            SDL_GPUVertexBufferDescription vbd[2] = {{0, 8 * 4, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
                                                     {1, sizeof(Inst), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0}};
            SDL_GPUVertexAttribute at[7] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},  {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
                                            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24}, {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0},
                                            {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16}, {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 32},
                                            {6, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 48}};
            pi.vertex_input_state = {vbd, 2, at, 7};
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
            pi.target_info.depth_stencil_format = fmt;
            pi.target_info.has_depth_stencil_target = true;
            depthPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        }
        for (SDL_GPUShader* s2 : {vs, fs})
            if (s2) SDL_ReleaseGPUShader(dev_, s2);
        if (!depthPipe_) {
            SDL_Log("3D: ground clutter shadow pipeline failed: %s", SDL_GetError());
            depthFailed_ = true;
            return 0;
        }
    }
    if (!depthPipe_) return 0;
    SDL_BindGPUGraphicsPipeline(pass, depthPipe_);
    VU vu;
    std::memcpy(vu.viewProj, lightVP.m, sizeof vu.viewProj);
    SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
    SDL_GPUBufferBinding vbs[2] = {{meshVB_, 0}, {shadowVB_, 0}};
    SDL_BindGPUVertexBuffers(pass, 0, vbs, 2);
    SDL_GPUBufferBinding ib{meshIB_, 0};
    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    int calls = 0;
    for (int ty = 0; ty < int(types_.size()); ++ty) {
        const Type& t = types_[std::size_t(ty)];
        // LOD 1 always: the shadow map's texels are about a world px; the depth bias covers the difference to LOD 0.
        if (shadowCount_[ty] == 0 || t.count[1] == 0) continue;
        SDL_DrawGPUIndexedPrimitives(pass, t.count[1], shadowCount_[ty], t.first[1], 0, shadowFirst_[ty]);
        ++calls;
    }
    return calls;
}

// ------------------------------------------------------------------------------------------------ spice clumps
// docs/3d-mode.md "Spice fields in 3D". Melange is spice sand: the clumps are low drifted lumps, flat crusts and
// small wind drifts of it, not crystals.

namespace {

// Smooth periodic noise round a ring (0..1, period 1 in t), for the clumps' irregular outlines.
float ringNoise(float t, int seed) {
    float v = 0;
    for (int o = 0; o < 3; ++o) {
        const int n = 3 << o;  // 3, 6, 12 bumps round the ring
        const float u = t * float(n);
        const int i = int(std::floor(u));
        const float f = u - float(i), sf = f * f * (3 - 2 * f);
        const float a = hashf(((i % n) + n) % n, seed, 900 + o), b = hashf((((i + 1) % n) + n) % n, seed, 900 + o);
        v += (a + (b - a) * sf) / float(1 << o);
    }
    return v / 1.75f;
}

}  // namespace

void GroundClutter::addSpicePieces() {
    if (int(types_.size()) + kSpiceTypes > kMaxTypes) return;
    // The texture layer: grey grain (fine speckles and darker crumbs) - the tint gives the colour.
    spiceLayer_ = std::min(layers_, kMaxLayers - 1);
    {
        auto& px = layerPx_[spiceLayer_];
        px.assign(std::size_t(kTex) * kTex, 0);
        double sum = 0;
        for (int y = 0; y < kTex; ++y)
            for (int x = 0; x < kTex; ++x) {
                auto vn = [&](float period, int salt) {  // tileable value noise (period divides kTex)
                    const int n = int(float(kTex) / period);
                    const float u = float(x) / period, v = float(y) / period;
                    const int i = int(u), j = int(v);
                    const float fx = u - float(i), fy = v - float(j);
                    auto h = [&](int a, int b) { return hashf(a % n, b % n, salt); };
                    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
                    return (h(i, j) * (1 - sx) + h(i + 1, j) * sx) * (1 - sy) + (h(i, j + 1) * (1 - sx) + h(i + 1, j + 1) * sx) * sy;
                };
                // (A clump is a few px wide and shows the whole layer: its blotches are large - pale grains, dark pores.)
                float L = 0.52f + (vn(128.0f, 1) - 0.5f) * 0.42f + (vn(64.0f, 2) - 0.5f) * 0.3f + (vn(16.0f, 5) - 0.5f) * 0.14f +
                          (hashf(x, y, 3) - 0.5f) * 0.1f;
                if (vn(32.0f, 4) > 0.7f) L *= 0.7f;  // darker crumbs
                const int c = std::clamp(int(L * 255.0f + 0.5f), 0, 255);
                px[std::size_t(y) * kTex + x] = std::uint32_t(c) | (std::uint32_t(c) << 8) | (std::uint32_t(c) << 16) | 0xFF000000u;
                sum += double(c) / 255.0;
            }
        layerMeanL_[spiceLayer_] = float(sum / double(kTex * kTex));
        layerKind_[spiceLayer_] = 4;
        layers_ = std::max(layers_, spiceLayer_ + 1);
    }
    // The meshes (1 unit = the piece's width, ground at y = 0, a skirt a little below it): a polar grid - a centre
    // and rings out to an irregular outline - with a height profile per kind.
    //   0, 1 lump: a soft rounded heap (height ~0.4 of its width), two outlines;
    //   2 crust: a flat cake with a rounded edge and an uneven top (0.13);
    //   3 drift: a small dune, long across the wind (x), a gentle windward side (+z), a short lee slope (-z) (0.24).
    static constexpr float kRatio[kSpiceTypes] = {0.34f, 0.3f, 0.13f, 0.24f};
    for (int kind = 0; kind < kSpiceTypes; ++kind) {
        Type t;
        t.name = kind < 2 ? "spice_lump" : kind == 2 ? "spice_crust" : "spice_drift";
        t.layer = spiceLayer_;
        t.cls = kPebble;
        t.heightRatio = kRatio[kind];
        for (int lod = 0; lod < 2; ++lod) {
            const int rings = lod ? 2 : kind == 2 ? 3 : 4;
            const int segs = lod ? (kind == 3 ? 8 : 6) : kind == 3 ? 12 : kind == 2 ? 12 : 10;
            const std::uint32_t base = std::uint32_t(verts_.size() / 8);
            std::vector<float> pos;  // x y z per vertex
            auto shape = [&](float ang, float rho) {  // rho 0 centre .. 1 the outline: x, y, z
                const float tA = ang / 6.2831853f;
                const float outline =
                    0.5f * (kind == 2 ? 0.8f + 0.4f * ringNoise(tA, 40 + kind) : 0.72f + 0.5f * ringNoise(tA, 40 + kind));
                float x = std::cos(ang) * rho * outline, z = std::sin(ang) * rho * outline;
                float y;
                if (kind < 2) {
                    // A heap of two or three lobes (not an egg), crumbly: every vertex a little up or down.
                    float hgt = 0;
                    for (int l = 0; l < 3; ++l) {
                        const float la = hashf(l, kind, 810) * 6.2831853f, lr = l == 0 ? 0.0f : 0.16f + 0.1f * hashf(l, kind, 811);
                        const float rr = l == 0 ? 0.46f : 0.24f + 0.08f * hashf(l, kind, 812), lh = l == 0 ? 1.0f : 0.62f + 0.25f * hashf(l, kind, 813);
                        const float dx = x - std::cos(la) * lr, dz = z - std::sin(la) * lr;
                        const float q = std::max(0.0f, 1.0f - (dx * dx + dz * dz) / (rr * rr));
                        hgt = std::max(hgt, lh * std::pow(q, 0.7f));
                    }
                    const float edge = std::clamp((1.0f - rho) * 4.0f, 0.0f, 1.0f);  // (down to the skirt at the outline)
                    y = kRatio[kind] * std::max(hgt, 0.25f * edge) * edge *
                        (0.82f + 0.36f * hashf(int(ang * 100.0f), int(rho * 100.0f), 814 + kind));
                } else if (kind == 2) {
                    const float edge = 1.0f - std::clamp((rho - 0.62f) / 0.38f, 0.0f, 1.0f);
                    y = kRatio[2] * (edge * edge * (3 - 2 * edge)) * (0.86f + 0.14f * rho + 0.12f * (ringNoise(tA * 3.0f, 60) - 0.5f));
                } else {
                    x *= 1.25f;                 // long across the wind
                    z *= z < 0 ? 0.42f : 0.8f;  // the lee side short and steep, the windward side long
                    const float q = std::max(0.0f, 1.0f - rho * rho);
                    y = kRatio[3] * std::pow(q, z < 0 ? 0.45f : 0.9f) * (0.85f + 0.3f * ringNoise(tA, 70));
                }
                if (rho >= 0.999f) y = -0.04f;  // the skirt: no gap at the ground
                return std::array<float, 3>{x, y, z};
            };
            const auto c = shape(0.0f, 0.0f);
            pos.insert(pos.end(), c.begin(), c.end());
            for (int r = 1; r <= rings; ++r)
                for (int sgi = 0; sgi < segs; ++sgi) {
                    const float ang = (float(sgi) + ((r & 1) ? 0.0f : 0.5f)) * 6.2831853f / float(segs);
                    const auto v = shape(ang, float(r) / float(rings));
                    pos.insert(pos.end(), v.begin(), v.end());
                }
            const std::size_t nv = pos.size() / 3;
            std::vector<std::uint32_t> tri;
            auto ringV = [&](int r, int sgi) {
                return r == 0 ? 0u : std::uint32_t(1 + (r - 1) * segs + ((sgi % segs) + segs) % segs);
            };
            for (int sgi = 0; sgi < segs; ++sgi) tri.insert(tri.end(), {0u, ringV(1, sgi + 1), ringV(1, sgi)});
            for (int r = 1; r < rings; ++r)
                for (int sgi = 0; sgi < segs; ++sgi) {
                    // (ring r + 1 is turned half a segment against ring r)
                    const std::uint32_t a = ringV(r, sgi), b = ringV(r, sgi + 1);
                    if (r & 1) {  // the outer ring's vertex sgi sits half a segment past the inner one's
                        const std::uint32_t o0 = ringV(r + 1, sgi), o1 = ringV(r + 1, sgi - 1);
                        tri.insert(tri.end(), {a, b, o0, a, o0, o1});
                    } else {
                        const std::uint32_t o0 = ringV(r + 1, sgi + 1), o1 = ringV(r + 1, sgi);
                        tri.insert(tri.end(), {a, b, o0, a, o0, o1});
                    }
                }
            // Smooth normals: the faces' (wound to face up) summed per vertex.
            std::vector<float> nrm(nv * 3, 0.0f);
            for (std::size_t i = 0; i < tri.size(); i += 3) {
                const float* p0 = &pos[tri[i] * 3];
                const float* p1 = &pos[tri[i + 1] * 3];
                const float* p2 = &pos[tri[i + 2] * 3];
                const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
                float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                if (n[1] < 0) {  // (wound the other way: flip the triangle too)
                    std::swap(tri[i + 1], tri[i + 2]);
                    for (float& k : n) k = -k;
                }
                for (int k = 0; k < 3; ++k)
                    for (int j = 0; j < 3; ++j) nrm[tri[i + std::size_t(k)] * 3 + std::size_t(j)] += n[j];
            }
            for (std::size_t v = 0; v < nv; ++v) {
                float* n = &nrm[v * 3];
                const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (len > 1e-8f) n[0] /= len, n[1] /= len, n[2] /= len;
                else n[0] = 0, n[1] = 1, n[2] = 0;
                const float vals[8] = {pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2], n[0], n[1], n[2], pos[v * 3] + 0.5f, pos[v * 3 + 2] + 0.5f};
                verts_.insert(verts_.end(), vals, vals + 8);
            }
            t.first[lod] = std::uint32_t(idx_.size());
            for (const std::uint32_t i : tri) idx_.push_back(base + i);
            t.count[lod] = std::uint32_t(tri.size());
        }
        spiceType_[kind] = int(types_.size());
        types_.push_back(t);
    }
}

// One chunk's clumps (deterministic: hashes of the cell coordinates, never the game's RNG). On a spice cell of
// amount a: presence P = min(a, 2) / 2, richness R = (a - 2) / 2 (thick 1, blooms more); ~1.5 P + 3.5 R clumps
// (thin 1-2, thick ~5, a bloom 6), taller and more often drifts where it is rich. On a bare sand cell next to a
// field: now and then a small stray lump on the side towards the spice. Every clump stays under
// kPassableMaxHeight (units drive through spice) and sits on the terrain (its spice relief included), sunk
// about a quarter of its height.
void GroundClutter::placeSpice(const game::World& w, const Terrain& t, int chunk) {
    namespace cf = game::cell_flags;
    if (chunk < 0 || chunk >= int(spiceChunk_.size()) || spiceType_[0] < 0) return;
    std::vector<Inst>& out = spiceChunk_[std::size_t(chunk)];
    std::vector<std::uint8_t>& outTy = spiceTy_[std::size_t(chunk)];
    spiceTotal_ -= int(out.size());
    out.clear();
    outTy.clear();
    const int cx0 = (chunk % spiceChW_) * kChunk, cy0 = (chunk / spiceChW_) * kChunk;
    auto amt = [&](int x, int y) { return x < 0 || y < 0 || x >= cw_ || y >= ch_ ? 0 : int(spiceAmt_[std::size_t(y) * cw_ + x]); };
    auto blocked = [&](int x, int y) {
        const game::Cell& c = w.cell(x, y);
        return (c.flags & (cf::kBuilding | cf::kWall | cf::kConcrete)) != 0 || t.foundationAt(x, y, -1e9f) > -1e8f;
    };
    const auto& verts = t.vertices();
    auto sunAt = [&](float x, float z) {
        const int gx = std::clamp(int(std::lround(x / t.step())), 0, t.gridW() - 1);
        const int gz = std::clamp(int(std::lround(z / t.step())), 0, t.gridH() - 1);
        return verts[std::size_t(gz) * t.gridW() + gx].sun;
    };
    const float dens = density_;
    const float windYaw = std::atan2(0.8f, -0.6f);  // mesh x along the crest (across the wind 0.8, 0.6), lee side downwind
    auto add = [&](int kind, float px, float pz, float size, float heightK, float bright, int cx, int cy, int salt) {
        if (int(out.size()) >= kSpicePerChunkMax || spiceTotal_ + int(out.size()) >= kSpiceMax) return;
        const Type& ty = types_[std::size_t(spiceType_[kind])];
        float sy = size * heightK;
        if (sy * ty.heightRatio > kPassableMaxHeight - 0.3f) sy = (kPassableMaxHeight - 0.3f) / ty.heightRatio;
        const float pieceH = std::max(0.3f, sy * ty.heightRatio);
        const float h = t.height(px, pz), e = std::max(2.0f, size * 0.3f);
        const float sx = std::clamp((t.height(px + e, pz) - t.height(px - e, pz)) / (2 * e), -1.0f, 1.0f);
        const float sz = std::clamp((t.height(px, pz + e) - t.height(px, pz - e)) / (2 * e), -1.0f, 1.0f);
        const float sink = pieceH * (kind == 2 ? 0.35f : 0.3f);  // (bedded in the spice: its darker foot hidden)
        Inst in{};
        in.pos[0] = px;
        in.pos[1] = h - sink;
        in.pos[2] = pz;
        in.yaw = kind == 3 ? windYaw + (hashf(cx, cy, 1501 + salt) - 0.5f) * 0.7f : hashf(cx, cy, 1501 + salt) * 6.2831853f;
        in.scale[0] = in.scale[2] = size;
        in.scale[1] = sy;
        in.layer = float(spiceLayer_);
        in.sun = sunAt(px, pz);
        in.height = pieceH;
        in.dust = 0.0f;
        in.bright = bright;
        in.slopeX = sx;
        in.slopeZ = sz;
        in.hue = (hashf(cx, cy, 1502 + salt) * 2 - 1) * 0.6f;
        in.pad = sink;
        out.push_back(in);
        outTy.push_back(std::uint8_t(spiceType_[kind]));
    };
    for (int cy = cy0; cy < std::min(ch_, cy0 + kChunk); ++cy)
        for (int cx = cx0; cx < std::min(cw_, cx0 + kChunk); ++cx) {
            if (blocked(cx, cy)) continue;
            const int a = amt(cx, cy);
            const float x0 = float(cx) * 32, z0 = float(cy) * 32;
            if (a > 0) {
                const float P = float(std::min(a, 2)) * 0.5f, R = std::clamp(float(a - 2) * 0.5f, 0.0f, 1.25f);
                // Clusters: a main piece (a lump, a crust or a small drift) with a few crumbs round it.
                const int n = std::min(4, int((0.8f * P + 1.7f * R) * dens + hashf(cx, cy, 1400)));
                const float heightK = 0.6f + 0.25f * P + 0.3f * std::min(R, 1.0f);
                for (int k = 0; k < n; ++k) {
                    const int salt = k * 11;
                    const float px = x0 + 3.0f + hashf(cx, cy, 1410 + salt) * 26, pz = z0 + 3.0f + hashf(cx, cy, 1411 + salt) * 26;
                    const float r = hashf(cx, cy, 1412 + salt), rs = hashf(cx, cy, 1413 + salt);
                    const int kind = r < 0.58f - 0.12f * R ? (r < 0.29f ? 0 : 1) : r < 0.84f - 0.08f * R ? 2 : 3;
                    float size;
                    switch (kind) {
                        case 2: size = 5 + rs * 5 + 2 * R; break;
                        case 3: size = 7 + rs * 5 + 3 * R; break;
                        default: size = 3.0f + rs * 2.5f + 1.2f * R; break;
                    }
                    const float bright = (0.84f + 0.28f * hashf(cx, cy, 1414 + salt)) * (1.0f - 0.1f * std::min(R, 1.0f));
                    add(kind, px, pz, size, heightK, bright, cx, cy, salt);
                    const int crumbs = int(1.0f + hashf(cx, cy, 1415 + salt) * (1.5f + 2.0f * R));
                    for (int j = 0; j < crumbs; ++j) {
                        const int s2 = salt * 7 + j * 3 + 1;
                        const float an = hashf(cx, cy, 1416 + s2) * 6.2831853f, d = size * 0.45f + 1.0f + hashf(cx, cy, 1417 + s2) * 3.5f;
                        const float qx = std::clamp(px + std::cos(an) * d, x0 + 0.5f, x0 + 31.5f);
                        const float qz = std::clamp(pz + std::sin(an) * d, z0 + 0.5f, z0 + 31.5f);
                        add(j & 1 ? 1 : 0, qx, qz, 1.5f + hashf(cx, cy, 1418 + s2) * 1.4f, heightK, bright * (0.92f + 0.16f * hashf(cx, cy, 1419 + s2)),
                            cx, cy, s2 + 40);
                    }
                }
                continue;
            }
            // A bare cell beside a field: strays on the side towards the spice (sand only).
            if (!(w.cell(cx, cy).flags & cf::kSand)) continue;
            int sx = 0, sz = 0, nb = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if ((dx || dy) && amt(cx + dx, cy + dy) > 0) sx += dx, sz += dy, ++nb;
            if (nb == 0) continue;
            for (int k = 0; k < 2; ++k) {
                const int salt = 300 + k * 13;
                if (hashf(cx, cy, 1420 + salt) > (0.1f + 0.045f * float(nb)) * dens) continue;
                // In the half of the cell that faces the spice.
                const float bx = sx == 0 ? 0.5f : sx > 0 ? 0.72f : 0.28f, bz = sz == 0 ? 0.5f : sz > 0 ? 0.72f : 0.28f;
                const float px = x0 + 32 * std::clamp(bx + (hashf(cx, cy, 1421 + salt) - 0.5f) * 0.5f, 0.05f, 0.95f);
                const float pz = z0 + 32 * std::clamp(bz + (hashf(cx, cy, 1422 + salt) - 0.5f) * 0.5f, 0.05f, 0.95f);
                const float rs = hashf(cx, cy, 1423 + salt);
                add(rs < 0.3f ? 0 : 2, px, pz, rs < 0.3f ? 2.0f + 2.0f * rs : 3.5f + 3.0f * rs, 0.45f, 0.85f + 0.25f * rs, cx, cy, salt);
            }
        }
    float lo = 1e9f, hi = -1e9f;
    for (const Inst& in : out) {
        lo = std::min(lo, in.pos[1] - 2);
        hi = std::max(hi, in.pos[1] + in.height + 2);
    }
    spiceLo_[std::size_t(chunk)] = lo;
    spiceHi_[std::size_t(chunk)] = hi;
    spiceTotal_ += int(out.size());
}

void GroundClutter::refreshSpice(const game::World& w, const Terrain& t) {
    if (spiceChunk_.empty() || w.width() != cw_ || w.height() != ch_ || spiceAmt_.size() != std::size_t(cw_) * ch_) return;
    bool any = false;
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            const std::uint8_t a = std::uint8_t(std::clamp(w.cell(x, y).spice(), 0, 255));
            std::uint8_t& old = spiceAmt_[std::size_t(y) * cw_ + x];
            if (a == old) continue;
            old = a;
            // (Its strays lie on the neighbours: their chunks too.)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = std::clamp(x + dx, 0, cw_ - 1), ny = std::clamp(y + dy, 0, ch_ - 1);
                    spiceDirty_[std::size_t(ny / kChunk) * spiceChW_ + std::size_t(nx / kChunk)] = 1;
                }
            any = true;
        }
    if (!any) return;
    const Uint64 t0 = SDL_GetTicksNS();
    int n = 0;
    for (std::size_t c = 0; c < spiceDirty_.size(); ++c)
        if (spiceDirty_[c]) {
            spiceDirty_[c] = 0;
            placeSpice(w, t, int(c));
            ++n;
        }
    static const bool log = std::getenv("D2K_SPLAT_LOG") != nullptr;
    if (log) SDL_Log("3D: spice clumps: %d chunk(s) re-placed in %.2f ms, %d clumps", n, double(SDL_GetTicksNS() - t0) / 1e6, spiceTotal_);
}

}  // namespace d2k::render3d

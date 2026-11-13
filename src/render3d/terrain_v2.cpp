#include "render3d/terrain_v2.hpp"

#include "formats/model3d.hpp"
#include "game/world.hpp"
#include "render/tileset.hpp"
#include "render3d/gpu.hpp"
#include "render3d/model_gltf.hpp"
#include "render3d/shader_blobs.hpp"
#include "render3d/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <cstring>
#include <atomic>
#include <filesystem>
#include <map>
#include <thread>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_FAILURE_STRINGS
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "../../third_party/stb/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace fs = std::filesystem;

namespace d2k::render3d {

namespace {

// Material folders (tools/fetch_terrain_assets.py MATERIALS), in Material order.
constexpr const char* kMaterialDirs[DesignedTerrain::kLayers] = {"sand", "dunes", "rock", "cliff", "rough", "spice", "concrete",
                                                              "crag"};
// World pixels per texture repeat (a cell is 32 px).
constexpr float kMaterialScale[DesignedTerrain::kMaterials] = {224, 150, 150, 120, 80, 60, 72};
// Normal map strength per material (sand stays calm, rock rugged).
constexpr float kNormalStrength[DesignedTerrain::kMaterials] = {0.45f, 0.6f, 0.8f, 0.9f, 0.9f, 0.6f, 0.5f};
// Rock meshes (assets/terrain/rocks/<name>.glb + .jpg; fetch_terrain_assets.py ROCKS).
constexpr const char* kRockNames[DesignedTerrain::kRockTypes] = {"boulder_a", "boulder_b", "boulder_c",
                                                                 "boulder_d", "boulder_e", "cliff_a"};
// Colour-lock fallbacks (0..255) when the map has no cell of a class.
constexpr float kDefaultColour[DesignedTerrain::kMaterials][3] = {
    {214, 172, 112}, {220, 178, 116}, {170, 110, 64}, {120, 76, 46}, {150, 100, 62}, {205, 110, 50}, {150, 140, 125}};

// Concrete foundation slab looks (docs/enhanced-graphics.md "Concrete"): the slab colour (0..255,
// replaces the colour lock to the concrete tiles' mean, which read as pale sand-coloured slabs),
// then panel detail, weathering, paint markings, sand drift (0..1). D2K_CONCRETE_STYLE=0..4 picks
// one (default kDefaultConcreteStyle).
struct ConcreteStyle {
    const char* name;
    float rgb[3];
    float detail, weathering, markings, drift;
};
constexpr ConcreteStyle kConcreteStyles[] = {
    {"grey", {116, 116, 114}, 0.6f, 0.5f, 0.08f, 0.5f},
    {"blue-grey", {104, 110, 118}, 1.0f, 0.45f, 0.16f, 0.45f},
    {"weathered", {114, 108, 98}, 0.5f, 0.95f, 0.06f, 0.85f},
    // Weathered, warm sand-grey like the original concrete tiles.
    {"weathered-sand", {168, 146, 112}, 0.5f, 0.9f, 0.05f, 0.9f},
    // Close to the original concrete tiles' colour (median 206,137,77 sampled from them), slightly muted.
    {"original-tan", {196, 142, 92}, 0.45f, 0.7f, 0.04f, 0.8f},
};
constexpr int kDefaultConcreteStyle = 4;  // original-tan: owner's pick 2026-11-09

int concreteStyleIndex() {
    if (const char* e = std::getenv("D2K_CONCRETE_STYLE")) {
        const int i = std::atoi(e);
        if (i >= 0 && i < int(std::size(kConcreteStyles))) return i;
    }
    return kDefaultConcreteStyle;
}

struct TerrainVU {
    float viewProj[16];
};
struct Terrain2FU {
    float sunDir[4], light[4], fogColor[4], fog[4], fog2[4];
    float tint[8][4];
    float lum[8][4];
    float params[4];
    float lightVP[16];
    float shadow[4];
};
struct RockFU {
    float sunDir[4], light[4], fogColor[4], fog[4], fog2[4];
    float tint[8][4];
    float lightVP[16];
    float shadow[4];
};
// Fragment uniform buffer 2 of terrain3_fs (F3U in render3d/terrain_v3_msl.hpp / shaders.hlsl).
struct FaithU {
    float guide[4];        // cliff height (px), guide texels per world px, 1 = baked from above, tone relief (px)
    float level[5][4];     // rgb = colour of the broken rock's tone level (0..1), a = its upper tone edge (level 4: raggedness)
    float cragTex[4];      // 1 / the crag texture's mean luminance, contrast (gamma), normal strength, world px per repeat
    float roughTex[4];     // 1 / the rough texture's mean luminance, contrast (gamma), unused x 2
    float shade[4];        // a face's tone: against its top, + by the side it looks to; the rim's strength, fine detail
    float face[4];         // gullies (strength), ledge line, darker foot (tone units), contact shadow on the sand
    float slab[4], seamLight[4], seamDark[4];
    float detailSun[4];    // xyz = the sun of the level ground's fine detail, w = 1 / its flat-ground light
    float reliefLight[4];  // ambient, diffuse, 1 / flat-ground light of the broken rock (lit by sunDir), w unused
};
enum { kFGuide, kFLevel0, kFCragTex = kFLevel0 + 5, kFRoughTex, kFShade, kFFace, kFSlab, kFSeamLight, kFSeamDark, kFMapRows };

// The original's concrete tiles (spawn.cpp kConcreteTiles): the faithful slabs take their colour.
// (Concrete looks the same with or without a building on it: owner, 2026-11-11 - an earlier
// version drew the slabs under buildings paler, like the original's skirt tiles.)
constexpr int kConcreteTileIds[3] = {0x28B, 0x29F, 0x2B3};

float hash2(int x, int y, int salt) {
    std::uint32_t h = std::uint32_t(x) * 0x8da6b343u ^ std::uint32_t(y) * 0xd8163841u ^ std::uint32_t(salt) * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return float(h & 0xFFFFFF) / float(0xFFFFFF);
}
float valueNoise(float x, float y, int salt) {
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = int(fx), iy = int(fy);
    const float tx = x - fx, ty = y - fy;
    const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    const float a = hash2(ix, iy, salt), b = hash2(ix + 1, iy, salt), c = hash2(ix, iy + 1, salt), d = hash2(ix + 1, iy + 1, salt);
    return (a * (1 - sx) + b * sx) * (1 - sy) + (c * (1 - sx) + d * sx) * sy;
}

// Decodes an image file to `channels` 8-bit channels at kTex x kTex (nearest resample if the
// file has another size). Empty on failure.
std::vector<std::uint8_t> loadImage(const fs::path& p, int channels, int size) {
    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load(p.string().c_str(), &w, &h, &n, channels);
    if (!px) return {};
    std::vector<std::uint8_t> out(std::size_t(size) * size * channels);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const int sx = x * w / size, sy = y * h / size;
            std::memcpy(&out[(std::size_t(y) * size + x) * channels], &px[(std::size_t(sy) * w + sx) * channels], std::size_t(channels));
        }
    stbi_image_free(px);
    return out;
}

SDL_GPUTexture* makeTexture(SDL_GPUDevice* dev, SDL_GPUTextureFormat f, int w, int h, int layers, int levels) {
    SDL_GPUTextureCreateInfo ci{};
    ci.type = layers > 1 ? SDL_GPU_TEXTURETYPE_2D_ARRAY : SDL_GPU_TEXTURETYPE_2D;
    ci.format = f;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (levels > 1 ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
    ci.width = Uint32(w);
    ci.height = Uint32(h);
    ci.layer_count_or_depth = Uint32(layers);
    ci.num_levels = Uint32(levels);
    SDL_GPUTexture* t = SDL_CreateGPUTexture(dev, &ci);
    if (!t) SDL_Log("3D terrain: texture %dx%dx%d failed: %s", w, h, layers, SDL_GetError());
    return t;
}

int mipLevels(int size) {
    int levels = 1;
    while ((size >> levels) >= 1) ++levels;
    return levels;
}

}  // namespace

std::vector<float> DesignedTerrain::tileMeans(const render::TileAtlas* atlas) {
    std::vector<float> rgb;
    if (!atlas) return rgb;
    rgb.reserve(std::size_t(atlas->tileCount()) * 3);
    for (int t = 0; t < atlas->tileCount(); ++t) {
        float s[3] = {};
        for (int y = 0; y < 32; y += 2)
            for (int x = 0; x < 32; x += 2) {
                const formats::Rgba p = atlas->pixel(t, x, y);
                s[0] += float(p & 0xFF);
                s[1] += float((p >> 8) & 0xFF);
                s[2] += float((p >> 16) & 0xFF);
            }
        for (float v : s) rgb.push_back(v / 256.0f);
    }
    return rgb;
}

std::string DesignedTerrain::assetsDir() {
    auto ok = [](const fs::path& p) {
        std::error_code ec;
        return fs::is_regular_file(p / "materials" / "sand" / "albedo.jpg", ec);
    };
    if (const char* base = SDL_GetBasePath()) {
        fs::path p(base);
        for (int up = 0; up < 6 && !p.empty(); ++up) {
            if (ok(p / "assets" / "terrain")) return (p / "assets" / "terrain").string();
            if (p == p.parent_path()) break;
            p = p.parent_path();
        }
    }
    std::error_code ec;
    const fs::path c = fs::current_path(ec) / "assets" / "terrain";
    return ok(c) ? c.string() : std::string();
}

std::unique_ptr<DesignedTerrain> DesignedTerrain::create(SDL_GPUDevice* dev, SDL_GPUTextureFormat color,
                                                         SDL_GPUTextureFormat depth, SDL_GPUSampleCount samples, int aniso) {
    std::unique_ptr<DesignedTerrain> p(new DesignedTerrain(dev));
    p->aniso_ = std::max(1, aniso);
    if (!p->loadAssets() || !p->init(color, depth, samples)) return nullptr;
    return p;
}

DesignedTerrain::~DesignedTerrain() {
    for (SDL_GPUTexture* t : {albedoArr_, normalArr_, rockArr_, splatATex_, splatBTex_, spiceTex_, guideTex_})
        if (t) SDL_ReleaseGPUTexture(dev_, t);
    if (faithPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, faithPipe_);
    if (guideSampler_) SDL_ReleaseGPUSampler(dev_, guideSampler_);
    for (SDL_GPUBuffer* b : {rockVB_, instVB_})
        if (b) SDL_ReleaseGPUBuffer(dev_, b);
    if (terrainPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, terrainPipe_);
    if (rockPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, rockPipe_);
    if (rockDepthPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, rockDepthPipe_);
    if (matSampler_) SDL_ReleaseGPUSampler(dev_, matSampler_);
}

bool DesignedTerrain::loadAssets() {
    const std::string dir = assetsDir();
    if (dir.empty()) {
        SDL_Log("3D terrain: Designed needs assets/terrain (run tools/fetch_terrain_assets.py) - using the original tiles");
        return false;
    }
    const Uint64 t0 = SDL_GetTicksNS();
    // The JPEG decodes are most of the time: all of them run up front on worker threads, the loops
    // below take the results (the same images as decoding them in order).
    std::map<std::string, std::vector<std::uint8_t>> decoded;
    {
        std::vector<std::pair<fs::path, int>> want;
        for (int m = 0; m < kLayers; ++m) {
            const fs::path d = fs::path(dir) / "materials" / kMaterialDirs[m];
            want.insert(want.end(), {{d / "albedo.jpg", 3}, {d / "normal.jpg", 3}, {d / "height.jpg", 1}, {d / "ao.jpg", 1}});
        }
        for (int r = 0; r < kRockTypes; ++r) want.emplace_back((fs::path(dir) / "rocks" / kRockNames[r]).replace_extension(".jpg"), 4);
        for (const auto& [f, ch] : want) decoded[f.string() + "|" + std::to_string(ch)];
        std::atomic<std::size_t> next{0};
        auto worker = [&] {
            for (std::size_t j; (j = next.fetch_add(1)) < want.size();)
                decoded[want[j].first.string() + "|" + std::to_string(want[j].second)] = loadImage(want[j].first, want[j].second, kTex);
        };
        std::vector<std::thread> pool;
        const unsigned n = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
        for (unsigned t = 1; t < n; ++t) pool.emplace_back(worker);
        worker();
        for (auto& t : pool) t.join();
    }
    auto image = [&](const fs::path& f, int ch) {
        auto it = decoded.find(f.string() + "|" + std::to_string(ch));
        return it != decoded.end() ? std::move(it->second) : loadImage(f, ch, kTex);
    };
    for (int m = 0; m < kLayers; ++m) {
        const fs::path d = fs::path(dir) / "materials" / kMaterialDirs[m];
        const auto col = image(d / "albedo.jpg", 3);
        const auto nrm = image(d / "normal.jpg", 3);
        auto hgt = image(d / "height.jpg", 1);
        auto ao = image(d / "ao.jpg", 1);
        if (m == kCragLayer && (col.empty() || nrm.empty())) {
            // Optional (the faithful style's crags): the cliff material stands in.
            albedoCpu_[m] = albedoCpu_[kCliff];
            normalCpu_[m] = normalCpu_[kCliff];
            for (int k = 0; k < 3; ++k) texMean_[m][k] = texMean_[kCliff][k];
            continue;
        }
        if (col.empty() || nrm.empty()) {
            SDL_Log("3D terrain: missing textures in %s - using the original tiles", d.string().c_str());
            return false;
        }
        if (m == kCragLayer) cragLoaded_ = true;
        const std::size_t n = std::size_t(kTex) * kTex;
        if (hgt.empty()) hgt.assign(n, 128);
        if (ao.empty()) ao.assign(n, 255);
        albedoCpu_[m].resize(n);
        normalCpu_[m].resize(n);
        double sum[3] = {};
        for (std::size_t i = 0; i < n; ++i) {
            const float a = 0.45f + 0.55f * float(ao[i]) / 255.0f;
            std::uint32_t c = 0;
            for (int k = 0; k < 3; ++k) {
                const float v = float(col[i * 3 + std::size_t(k)]) * a;
                sum[k] += v;
                c |= std::uint32_t(std::clamp(v, 0.0f, 255.0f)) << (8 * k);
            }
            albedoCpu_[m][i] = c | (std::uint32_t(hgt[i]) << 24);
            normalCpu_[m][i] = std::uint32_t(nrm[i * 3]) | (std::uint32_t(nrm[i * 3 + 1]) << 8) |
                               (std::uint32_t(nrm[i * 3 + 2]) << 16) | 0xFF000000u;
        }
        for (int k = 0; k < 3; ++k) texMean_[m][k] = float(sum[k] / double(n));
    }
    // Rock meshes + their scanned colour (a type that is missing is not placed).
    int rocks = 0;
    for (int r = 0; r < kRockTypes; ++r) {
        const fs::path base = fs::path(dir) / "rocks" / kRockNames[r];
        formats::Model3D m;
        std::string err;
        auto tex = image(fs::path(base).replace_extension(".jpg"), 4);
        if (tex.empty() || !loadGltfModel(fs::path(base).replace_extension(".glb").string(), GltfAdjust{}, m, err)) continue;
        rockFirst_[r] = std::uint32_t(rockVerts_.size() / 8);
        float top = 0, ext[2][2] = {{1e9f, -1e9f}, {1e9f, -1e9f}};
        for (const formats::MeshPart* part : {&m.body, &m.turret})
            for (const formats::MeshVertex& v : part->tris) {
                const float vals[8] = {v.pos[0], v.pos[1], v.pos[2], v.nrm[0], v.nrm[1], v.nrm[2], v.uv[0], v.uv[1]};
                rockVerts_.insert(rockVerts_.end(), vals, vals + 8);
                top = std::max(top, v.pos[1]);
                for (int a = 0; a < 2; ++a) {
                    ext[a][0] = std::min(ext[a][0], v.pos[a * 2]);
                    ext[a][1] = std::max(ext[a][1], v.pos[a * 2]);
                }
            }
        rockLongZ_[r] = ext[1][1] - ext[1][0] > ext[0][1] - ext[0][0];
        rockCount_[r] = std::uint32_t(rockVerts_.size() / 8) - rockFirst_[r];
        rockHeight_[r] = std::max(top, 4.0f);
        rockTexCpu_[r].resize(std::size_t(kTex) * kTex);
        double sum[3] = {};
        for (std::size_t i = 0; i < rockTexCpu_[r].size(); ++i) {
            std::uint32_t c;
            std::memcpy(&c, &tex[i * 4], 4);
            rockTexCpu_[r][i] = c | 0xFF000000u;
            for (int k = 0; k < 3; ++k) sum[k] += double(tex[i * 4 + std::size_t(k)]);
        }
        for (int k = 0; k < 3; ++k) rockMean_[r][k] = float(sum[k] / double(rockTexCpu_[r].size()));
        ++rocks;
    }
    SDL_Log("3D terrain: Designed - %d materials, %d rock meshes (%zu vertices) from %s in %.0f ms", int(kMaterials),
            rocks, rockVerts_.size() / 8, dir.c_str(), double(SDL_GetTicksNS() - t0) / 1e6);
    return true;
}

bool DesignedTerrain::init(SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth, SDL_GPUSampleCount samples) {
    colorFormat_ = color;
    depthFormat_ = depth;
    samples_ = samples;
    SDL_GPUShader* tvs = createShader(dev_, ShaderId::TerrainVs, 0, 1);
    SDL_GPUShader* tfs = createShader(dev_, ShaderId::Terrain2Fs, 7, 2);
    SDL_GPUShader* rvs = createShader(dev_, ShaderId::RockVs, 0, 1);
    SDL_GPUShader* rfs = createShader(dev_, ShaderId::RockFs, 3, 2);
    bool ok = tvs && tfs && rvs && rfs;
    SDL_GPUColorTargetDescription ctd{};
    ctd.format = color;
    auto common = [&](SDL_GPUGraphicsPipelineCreateInfo& pi) {
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.enable_depth_clip = gpuDepthClip();
        pi.multisample_state.sample_count = samples;
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = true;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        pi.target_info.color_target_descriptions = &ctd;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depth;
        pi.target_info.has_depth_stencil_target = true;
    };
    if (ok) {
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = tvs;
        pi.fragment_shader = tfs;
        SDL_GPUVertexBufferDescription vbd{0, sizeof(TerrainVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute at[3] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
                                        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
                                        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, 24}};
        pi.vertex_input_state = {&vbd, 1, at, 3};
        common(pi);
        terrainPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!terrainPipe_) SDL_Log("3D terrain: pipeline failed: %s", SDL_GetError());
        ok = terrainPipe_ != nullptr;
    }
    if (ok) {
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = rvs;
        pi.fragment_shader = rfs;
        SDL_GPUVertexBufferDescription vbd[2] = {{0, 8 * 4, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
                                                 {1, sizeof(RockInst), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0}};
        SDL_GPUVertexAttribute at[6] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
                                        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
                                        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},
                                        {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0},
                                        {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16},
                                        {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 32}};
        pi.vertex_input_state = {vbd, 2, at, 6};
        common(pi);
        rockPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!rockPipe_) SDL_Log("3D terrain: rock pipeline failed: %s", SDL_GetError());
        ok = rockPipe_ != nullptr;
    }
    for (SDL_GPUShader* s : {tvs, tfs, rvs, rfs})
        if (s) SDL_ReleaseGPUShader(dev_, s);
    if (!ok) return false;

    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    si.max_lod = 16;
    si.enable_anisotropy = gpuAnisotropy() && aniso_ > 1;
    si.max_anisotropy = float(aniso_);
    matSampler_ = SDL_CreateGPUSampler(dev_, &si);
    const int levels = mipLevels(kTex);
    albedoArr_ = makeTexture(dev_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, kTex, kTex, kLayers, levels);
    normalArr_ = makeTexture(dev_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, kTex, kTex, kLayers, levels);
    rockArr_ = makeTexture(dev_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, kTex, kTex, kRockTypes, levels);
    if (!rockVerts_.empty()) {
        SDL_GPUBufferCreateInfo bi{};
        bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bi.size = Uint32(rockVerts_.size() * 4);
        rockVB_ = SDL_CreateGPUBuffer(dev_, &bi);
    }
    return matSampler_ && albedoArr_ && normalArr_ && rockArr_;
}

// The faithful top-down bake's sun: from the south-west / west-south-west, 48 degrees up - where
// the original tiles' own light comes from (their cliff faces are brightest facing W / SW, darkest
// facing E; their shadows lie on the sand north-east of the cliffs, 8-14 px wide).
Vec3 DesignedTerrain::faithfulBakeSun() {
    const float e = radians(48.0f), az = radians(238.0f);  // compass: 0 = north, 90 = east
    return normalize(Vec3{std::sin(az) * std::cos(e), std::sin(e), -std::cos(az) * std::cos(e)});
}

// The sun that casts the faithful bake's shadows: the same side, 42 degrees up - a cliff's shadow
// then lies 8 - 16 px over the sand to its north-east, as in the original tiles (the mesh's
// crests are a little lower than the relief they stand for).
Vec3 DesignedTerrain::faithfulBakeShadowSun() {
    const float e = radians(42.0f), az = radians(238.0f);
    return normalize(Vec3{std::sin(az) * std::cos(e), std::sin(e), -std::cos(az) * std::cos(e)});
}

const TerrainGuide* DesignedTerrain::prepareGuide(const game::World& w, const render::TileAtlas* atlas) {
    guide_.clear();
    if (style_ != TerrainStyle::Faithful) return nullptr;
    TerrainGuide::Params p;
    if (!topDown_) {  // 3D: the camera's tilt shows the faces, they can stand steeper
        p.faceSouth = 12.0f;
        p.faceNorth = 8.0f;
        p.faceEastWest = 9.0f;
    } else {  // the original's south faces are 16 - 24 px wide; a ridge keeps a lit top band beside one
        p.faceSouth = 20.0f;
        p.faceNorth = 5.0f;
        p.faceEastWest = 10.0f;
        p.topDown = true;
    }
    if (!guide_.build(w, atlas, p)) return nullptr;
    if (!ensureFaithPipeline()) {
        guide_.clear();
        return nullptr;
    }
    return &guide_;
}

bool DesignedTerrain::ensureFaithPipeline() {
    if (faithPipe_) return true;
    if (faithPipeFailed_) return false;
    SDL_GPUShader* vs = createShader(dev_, ShaderId::TerrainVs, 0, 1);
    SDL_GPUShader* fs = createShader(dev_, ShaderId::Terrain3Fs, 8, 3);
    if (vs && fs) {
        SDL_GPUColorTargetDescription ctd{};
        ctd.format = colorFormat_;
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
        pi.multisample_state.sample_count = samples_;
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = true;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        pi.target_info.color_target_descriptions = &ctd;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depthFormat_;
        pi.target_info.has_depth_stencil_target = true;
        faithPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!faithPipe_) SDL_Log("3D terrain: faithful pipeline failed: %s", SDL_GetError());
    }
    for (SDL_GPUShader* sh : {vs, fs})
        if (sh) SDL_ReleaseGPUShader(dev_, sh);
    if (faithPipe_ && !guideSampler_) {
        SDL_GPUSamplerCreateInfo si{};
        si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
        si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        si.max_lod = 16;
        guideSampler_ = SDL_CreateGPUSampler(dev_, &si);
    }
    if (!faithPipe_ || !guideSampler_) {
        SDL_Log("3D terrain: Designed (faithful) is not available here - using the Designed layout");
        faithPipeFailed_ = true;
        return false;
    }
    return true;
}

void DesignedTerrain::build(const game::World& w, const Terrain& t, const std::vector<float>& tileRgb) {
    namespace cf = game::cell_flags;
    cw_ = w.width();
    ch_ = w.height();
    const auto& kinds = t.cellKinds();
    cellMat_.assign(std::size_t(cw_) * ch_, kSand);
    concreteSig_.assign(cellMat_.size(), 0);
    spice_.assign(cellMat_.size(), 0);
    const bool faith = faithfulActive();
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            const std::size_t i = std::size_t(y) * cw_ + x;
            const game::Cell& c = w.cell(x, y);
            const bool built = (c.flags & (cf::kBuilding | cf::kWall | cf::kConcrete)) != 0;
            const bool walkOnly = (c.flags & cf::kWalkOn) && !(c.flags & cf::kDriveOn) && !built;
            std::uint8_t m;
            if (c.flags & cf::kConcrete) m = kConcrete;
            else if (i < kinds.size() && kinds[i] == Terrain::kSandCell) m = c.terrain() == 2 ? kDunes : kSand;
            else if (i < kinds.size() && kinds[i] == Terrain::kRockCell)
                m = (c.terrain() == 3 || c.terrain() == 5 || walkOnly) ? kRough : kRock;
            else m = kCliff;
            cellMat_[i] = m;
            concreteSig_[i] = c.flags & cf::kConcrete;
            spice_[i] = std::uint8_t(std::min(7, c.spice()) * 255 / 7);
        }
    computeTints(w, tileRgb);
    if (faith) faithfulLook(tileRgb);
    splatA_.assign(std::size_t(cw_) * kSplatRes * std::size_t(ch_) * kSplatRes, 0);
    splatB_.assign(splatA_.size(), 0);
    buildSplat(0, 0, cw_ - 1, ch_ - 1);
    if (faith) placeStones(t);
    else placeRocks(w, t);
    if (faith) {  // the guide texture (with mips: the blurred relief, minification in 3D)
        if (guide_.width() != guideW_ || guide_.height() != guideH_ || !guideTex_) {
            if (guideTex_) SDL_ReleaseGPUTexture(dev_, guideTex_);
            guideW_ = guide_.width();
            guideH_ = guide_.height();
            guideTex_ = makeTexture(dev_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, guideW_, guideH_, 1,
                                    mipLevels(std::max(guideW_, guideH_)));
        }
        guideDirty_ = true;
    }
    // (Re)create the per-map textures.
    const int sw = cw_ * kSplatRes, sh = ch_ * kSplatRes;
    if (sw != splatW_ || sh != splatH_ || !splatATex_) {
        for (SDL_GPUTexture** tx : {&splatATex_, &splatBTex_, &spiceTex_}) {
            if (*tx) SDL_ReleaseGPUTexture(dev_, *tx);
            *tx = nullptr;
        }
        splatATex_ = makeTexture(dev_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, sw, sh, 1, 1);
        splatBTex_ = makeTexture(dev_, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, sw, sh, 1, 1);
        spiceTex_ = makeTexture(dev_, SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, cw_, ch_, 1, 1);
        splatW_ = sw;
        splatH_ = sh;
    }
    splatDirty_ = spiceDirty_ = instDirty_ = true;
}

void DesignedTerrain::update(const game::World& w, std::vector<int>* changed) {
    namespace cf = game::cell_flags;
    if (w.width() != cw_ || w.height() != ch_) return;
    int x0 = cw_, y0 = ch_, x1 = -1, y1 = -1;
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            const std::size_t i = std::size_t(y) * cw_ + x;
            const game::Cell& c = w.cell(x, y);
            const std::uint8_t s = std::uint8_t(std::min(7, c.spice()) * 255 / 7);
            if (s != spice_[i]) {
                spice_[i] = s;
                spiceDirty_ = true;
                if (changed) changed->push_back(int(i));
            }
            const std::uint32_t sig = c.flags & cf::kConcrete;
            if (sig != concreteSig_[i]) {
                concreteSig_[i] = sig;
                if (sig) cellMat_[i] = kConcrete;
                else if (cellMat_[i] == kConcrete) cellMat_[i] = kRock;
                if (changed) changed->push_back(int(i));
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
        }
    if (x1 >= 0) {  // concrete laid / destroyed: only that part of the splat
        buildSplat(x0, y0, x1, y1);
        splatDirty_ = true;
    }
}

// The spice texture (terrain_v2_msl.hpp spiceField): presence = the amount up to thin spice (2 of
// the 7 levels) per cell; richness = what lies above thin (thick spice: 2 more levels, blooms
// more), blurred with a 5 x 5 binomial kernel so thick spice forms soft patches with denser
// cores instead of cell blocks.
void DesignedTerrain::buildSpiceTex() {
    const std::size_t n = std::size_t(cw_) * ch_;
    spiceTexCpu_.assign(n * 2, 0);
    std::vector<float> rich(n), tmp(n);
    for (std::size_t i = 0; i < n; ++i) {
        const int levels = (int(spice_[i]) * 7 + 127) / 255;
        spiceTexCpu_[i * 2] = std::uint8_t(std::min(levels, 2) * 127);
        rich[i] = float(std::max(levels - 2, 0)) / 2.0f;  // 1 = thick spice
    }
    static constexpr float kW[5] = {1 / 16.0f, 4 / 16.0f, 6 / 16.0f, 4 / 16.0f, 1 / 16.0f};
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            float s = 0;
            for (int k = -2; k <= 2; ++k) s += rich[std::size_t(y) * cw_ + std::size_t(std::clamp(x + k, 0, cw_ - 1))] * kW[k + 2];
            tmp[std::size_t(y) * cw_ + x] = s;
        }
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            float s = 0;
            for (int k = -2; k <= 2; ++k) s += tmp[std::size_t(std::clamp(y + k, 0, ch_ - 1)) * cw_ + x] * kW[k + 2];
            spiceTexCpu_[(std::size_t(y) * cw_ + x) * 2 + 1] = std::uint8_t(std::clamp(s * 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
}

void DesignedTerrain::computeTints(const game::World& w, const std::vector<float>& tileRgb) {
    double sum[kMaterials][3] = {}, cnt[kMaterials] = {};
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            const game::Cell& c = w.cell(x, y);
            const std::size_t t = std::size_t(c.tile & 0x0FFF) * 3;
            if (t + 2 >= tileRgb.size()) continue;
            int m = cellMat_[std::size_t(y) * cw_ + x];
            if (c.spice() > 0) m = kSpice;
            for (int k = 0; k < 3; ++k) sum[m][k] += tileRgb[t + std::size_t(k)];
            cnt[m] += 1;
        }
    float target[kMaterials][3];
    auto have = [&](int m) { return cnt[m] >= 4; };
    for (int m = 0; m < kMaterials; ++m)
        for (int k = 0; k < 3; ++k) target[m][k] = have(m) ? float(sum[m][k] / cnt[m]) : kDefaultColour[m][k];
    for (int k = 0; k < 3; ++k) {
        if (!have(kDunes) && have(kSand)) target[kDunes][k] = target[kSand][k] * 1.02f;
        if (!have(kSand) && have(kDunes)) target[kSand][k] = target[kDunes][k] * 0.98f;
        if (!have(kRough) && have(kRock)) target[kRough][k] = target[kRock][k] * 0.92f;
        if (!have(kCliff) && have(kRock)) target[kCliff][k] = target[kRock][k] * 0.7f;
        // Spice tiles are sand with orange specks: their mean is only a little off the sand, the
        // grains drawn here should carry the whole difference.
        if (have(kSpice)) target[kSpice][k] = target[kSand][k] + (target[kSpice][k] - target[kSand][k]) * 2.4f;
        target[kSpice][k] = std::clamp(target[kSpice][k], 20.0f, 250.0f);
    }
    // Warmer than the plain means (the owner's comparison with the original): the scans' detail
    // (lighter crests, bluish-grey shading) made the open sand read pale / grey next to the
    // original's warm orange, and rough rock grey next to its dark brown crags. Saturation is
    // raised around the luminance, then a gentle orange bias / darkening.
    auto warm = [&](int m, float sat, float r, float g, float b) {
        float* t = target[m];
        const float L = 0.299f * t[0] + 0.587f * t[1] + 0.114f * t[2];
        const float mul[3] = {r, g, b};
        for (int k = 0; k < 3; ++k) t[k] = std::clamp((L + (t[k] - L) * sat) * mul[k], 8.0f, 252.0f);
    };
    warm(kSand, 1.25f, 0.98f, 0.95f, 0.9f);
    warm(kDunes, 1.25f, 0.98f, 0.95f, 0.9f);
    warm(kRock, 1.15f, 1.0f, 0.97f, 0.92f);
    warm(kRough, 1.3f, 0.88f, 0.84f, 0.8f);
    warm(kCliff, 1.2f, 0.95f, 0.92f, 0.88f);
    // Concrete: a designed slab colour (grey / blue-grey), not the tiles' mean.
    const int cs = concreteStyleIndex();
    const ConcreteStyle& style = kConcreteStyles[cs];
    for (int k = 0; k < 3; ++k) target[kConcrete][k] = style.rgb[k];
    for (int k = 0; k < 3; ++k) concreteLook_[0][k] = std::clamp(target[kSand][k] * 1.02f / 255.0f, 0.0f, 1.0f);
    concreteLook_[0][3] = float(cs);
    concreteLook_[1][0] = style.detail;
    concreteLook_[1][1] = style.weathering;
    concreteLook_[1][2] = style.markings;
    concreteLook_[1][3] = style.drift;
    if (topDown_) {
        // Seen from straight above (Graphics: Enhanced) the sunlit tops dominate and the shaded
        // faces hardly show, so the crags would come out lighter than the original's tiles (whose
        // mean includes their drawn shadows): darker cliff / rough materials and rocks.
        for (int k = 0; k < 3; ++k) {
            target[kCliff][k] *= 0.72f;
            target[kRough][k] *= 0.9f;
        }
    }
    SDL_Log("3D terrain: colour lock sand %.0f,%.0f,%.0f rock %.0f,%.0f,%.0f cliff %.0f,%.0f,%.0f spice %.0f,%.0f,%.0f",
            target[kSand][0], target[kSand][1], target[kSand][2], target[kRock][0], target[kRock][1], target[kRock][2],
            target[kCliff][0], target[kCliff][1], target[kCliff][2], target[kSpice][0], target[kSpice][1], target[kSpice][2]);
    for (int m = 0; m < kMaterials; ++m) {
        for (int k = 0; k < 3; ++k) tint_[m][k] = std::clamp(target[m][k] / std::max(texMean_[m][k], 1.0f), 0.15f, 4.0f);
        tint_[m][3] = kMaterialScale[m];
        const float meanL = std::max(4.0f, 0.299f * texMean_[m][0] + 0.587f * texMean_[m][1] + 0.114f * texMean_[m][2]);
        for (int k = 0; k < 3; ++k) lum_[m][k] = target[m][k] / meanL;
        lum_[m][3] = kNormalStrength[m];
    }
    // Rocks: recoloured by luminance (the scans carry grey lichen and their own hues): albedo =
    // target colour * texel luminance / mean luminance (rock_fs), a = mean luminance (0..1).
    for (int r = 0; r < kRockTypes; ++r) {
        const float meanL = std::max(4.0f, 0.299f * rockMean_[r][0] + 0.587f * rockMean_[r][1] + 0.114f * rockMean_[r][2]);
        for (int k = 0; k < 3; ++k) rockTint_[r][k] = target[kCliff][k] * (topDown_ ? 1.0f : 0.95f) / meanL;
        rockTint_[r][3] = meanL / 255.0f;
    }
}

void DesignedTerrain::buildSplat(int rx0, int ry0, int rx1, int ry1) {
    // Channel per material (spice is separate): sand dunes rock cliff | rough concrete.
    constexpr int kChan[kMaterials] = {0, 1, 2, 3, 4, -1, 5};
    const int W = cw_ * kSplatRes;
    // Work area: the cells asked for + 2 (the warp reaches 0.6 cell, the blur half a cell);
    // written back: the cells + 1.
    const int px0 = std::max(0, rx0 - 2), py0 = std::max(0, ry0 - 2), px1 = std::min(cw_ - 1, rx1 + 2), py1 = std::min(ch_ - 1, ry1 + 2);
    const int LW = (px1 - px0 + 1) * kSplatRes, LH = (py1 - py0 + 1) * kSplatRes;
    const int ox = px0 * kSplatRes, oy = py0 * kSplatRes;
    std::vector<float> ch[6];
    for (auto& c : ch) c.assign(std::size_t(LW) * LH, 0.0f);
    for (int ly = 0; ly < LH; ++ly)
        for (int lx = 0; lx < LW; ++lx) {
            const float u = (float(lx + ox) + 0.5f) / kSplatRes, v = (float(ly + oy) + 0.5f) / kSplatRes;
            // Noisy boundaries: look the material up at a warped point (up to ~0.4 cell away).
            const float wu = (valueNoise(u * 0.9f, v * 0.9f, 1) - 0.5f) * 0.6f + (valueNoise(u * 2.7f, v * 2.7f, 2) - 0.5f) * 0.3f;
            const float wv = (valueNoise(u * 0.9f, v * 0.9f, 3) - 0.5f) * 0.6f + (valueNoise(u * 2.7f, v * 2.7f, 4) - 0.5f) * 0.3f;
            const int cx = std::clamp(int(std::floor(u + wu)), 0, cw_ - 1), cy = std::clamp(int(std::floor(v + wv)), 0, ch_ - 1);
            // Concrete slabs keep the cell edges exactly (no warp): they are laid per cell.
            const int ux = std::clamp(int(u), 0, cw_ - 1), uy = std::clamp(int(v), 0, ch_ - 1);
            int m = cellMat_[std::size_t(cy) * cw_ + cx];
            if (cellMat_[std::size_t(uy) * cw_ + ux] == kConcrete) m = kConcrete;
            else if (m == kConcrete) m = cellMat_[std::size_t(uy) * cw_ + ux];
            const int c = kChan[m];
            if (c >= 0) ch[c][std::size_t(ly) * LW + lx] = 1.0f;
        }
    // Soft edges: separable box blurs (radius 2 texels = a quarter cell; twice). Concrete stays
    // binary: bilinear filtering of it crosses 0.5 exactly on the cell edges, and terrain2_fs
    // thresholds it there for a crisp slab border (no blur bleeding into the rock / sand).
    std::vector<float> tmp(std::size_t(LW) * LH);
    constexpr int R = 2;
    for (int ci = 0; ci < 5; ++ci)
        for (int pass = 0; pass < 2; ++pass) {
            auto& c = ch[ci];
            for (int y = 0; y < LH; ++y)
                for (int x = 0; x < LW; ++x) {
                    float s = 0;
                    for (int k = -R; k <= R; ++k) s += c[std::size_t(y) * LW + std::size_t(std::clamp(x + k, 0, LW - 1))];
                    tmp[std::size_t(y) * LW + x] = s / (2 * R + 1);
                }
            for (int y = 0; y < LH; ++y)
                for (int x = 0; x < LW; ++x) {
                    float s = 0;
                    for (int k = -R; k <= R; ++k) s += tmp[std::size_t(std::clamp(y + k, 0, LH - 1)) * LW + x];
                    c[std::size_t(y) * LW + x] = s / (2 * R + 1);
                }
        }
    auto q = [](float v) { return std::uint32_t(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f)); };
    const std::vector<std::uint8_t>* cliffCells =
        faithfulActive() && guide_.cliffCells().size() == std::size_t(cw_) * ch_ ? &guide_.cliffCells() : nullptr;
    const int wx0 = std::max(0, rx0 - 1) * kSplatRes, wy0 = std::max(0, ry0 - 1) * kSplatRes;
    const int wx1 = (std::min(cw_ - 1, rx1 + 1) + 1) * kSplatRes, wy1 = (std::min(ch_ - 1, ry1 + 1) + 1) * kSplatRes;
    for (int y = wy0; y < wy1; ++y)
        for (int x = wx0; x < wx1; ++x) {
            const std::size_t l = std::size_t(y - oy) * LW + std::size_t(x - ox), g = std::size_t(y) * W + x;
            splatA_[g] = q(ch[0][l]) | (q(ch[1][l]) << 8) | (q(ch[2][l]) << 16) | (q(ch[3][l]) << 24);
            splatB_[g] = q(ch[4][l]) | (q(ch[5][l]) << 8);
            if (cliffCells) {
                // Faithful: A.a = the cells no vehicle can use (binary per cell, unwarped and
                // unblurred like the concrete: the cliff look ends exactly at their borders).
                const std::size_t ci = std::size_t(y / kSplatRes) * cw_ + std::size_t(x / kSplatRes);
                splatA_[g] = (splatA_[g] & 0x00FFFFFFu) | ((*cliffCells)[ci] ? 0xFF000000u : 0u);
            }
        }
}

// Faithful: the colour targets of the materials the faithful layout uses differently, and the
// F3U block. Sand, dunes, plain rock and spice keep the Designed targets (computeTints).
void DesignedTerrain::faithfulLook(const std::vector<float>& tileRgb) {
    auto setTarget = [&](int m, const float* rgb) {
        for (int k = 0; k < 3; ++k) tint_[m][k] = std::clamp(rgb[k] / std::max(texMean_[m][k], 1.0f), 0.15f, 4.0f);
        const float meanL = std::max(4.0f, 0.299f * texMean_[m][0] + 0.587f * texMean_[m][1] + 0.114f * texMean_[m][2]);
        for (int k = 0; k < 3; ++k) lum_[m][k] = rgb[k] / meanL;
    };
    // Concrete: this tileset's own slabs.
    float slab[3] = {204, 147, 90};
    {
        double s[3] = {};
        int n = 0;
        for (int t : kConcreteTileIds)
            if (std::size_t(t) * 3 + 2 < tileRgb.size()) {
                for (int k = 0; k < 3; ++k) s[k] += tileRgb[std::size_t(t) * 3 + std::size_t(k)];
                ++n;
            }
        if (n > 0)
            for (int k = 0; k < 3; ++k) slab[k] = float(s[k] / n);
    }
    setTarget(kConcrete, slab);
    // Boulders: the original's shaded rock (its dark lumps), so a boulder field stands out from
    // the pale crumbling rock it lies on.
    for (int r = 0; r < kRockTypes; ++r) {
        const float meanL = std::max(4.0f, 0.299f * rockMean_[r][0] + 0.587f * rockMean_[r][1] + 0.114f * rockMean_[r][2]);
        for (int k = 0; k < 3; ++k) rockTint_[r][k] = (guide_.levelColour(1)[k] * 0.3f + guide_.levelColour(2)[k] * 0.7f) / meanL;
        rockTint_[r][3] = meanL / 255.0f;
    }

    auto set = [&](int row, float x, float y, float z, float w) {
        faithU_[row][0] = x;
        faithU_[row][1] = y;
        faithU_[row][2] = z;
        faithU_[row][3] = w;
    };
    set(kFGuide, Terrain::kCliffHeight, float(guide_.res()) / float(TerrainGuide::kPx), topDown_ ? 1.0f : 0.0f, 2.0f);
    // The broken rock's tone levels: this map's own colours; a = the level's upper tone edge
    // (the pale level: how ragged the contours are, in tone units).
    for (int i = 0; i < TerrainGuide::kLevels; ++i) {
        const float* c = guide_.levelColour(i);
        set(kFLevel0 + i, c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, i < TerrainGuide::kLevels - 1 ? TerrainGuide::kLevelEdge[i] : 0.07f);
    }
    auto invMeanL = [&](int layer) {
        const float* tm = texMean_[layer];
        return 255.0f / std::max(4.0f, 0.299f * tm[0] + 0.587f * tm[1] + 0.114f * tm[2]);
    };
    set(kFCragTex, invMeanL(kCragLayer), cragLoaded_ ? 1.3f : 1.1f, 1.0f, cragLoaded_ ? 44.0f : kMaterialScale[kCliff]);
    set(kFRoughTex, invMeanL(kRough), 1.2f, 0.0f, 0.0f);
    // A face's shading. Baked from above it is the whole relief (measured on the original tiles:
    // a face is 0.6 - 0.76 as light as a top, lightest looking west / south-west; a little darker
    // here, for a cliff that reads at a glance); in 3D the real light does the sides, the faces
    // are only darker rock. Then the gullies, the ledge, the foot and the contact shadow.
    if (topDown_) {
        set(kFShade, -0.38f, 0.2f, 0.85f, 0.3f);
        set(kFFace, 1.0f, 0.16f, 0.14f, 0.3f);
    } else {
        set(kFShade, -0.36f, 0.0f, 0.7f, 0.3f);
        set(kFFace, 1.0f, 0.14f, 0.12f, 0.2f);
    }
    set(kFSlab, slab[0] / 255.0f, slab[1] / 255.0f, slab[2] / 255.0f, 0.45f);  // a: speckle (the original: 6.6 % of red)
    {   // seams, relative to the slab as in the original's concrete tiles: the pale line
        // (221,183,125 on a 206,148,90 slab) and a dark line (a little darker than the original's
        // 155,95,56: dirt in the joints)
        static constexpr float kPale[3] = {1.07f, 1.24f, 1.39f};
        float l[3], d[3];
        for (int k = 0; k < 3; ++k) {
            l[k] = std::min(1.0f, slab[k] * kPale[k] / 255.0f);
            d[k] = slab[k] * (k == 0 ? 0.56f : k == 1 ? 0.46f : 0.42f) / 255.0f;
        }
        set(kFSeamLight, l[0], l[1], l[2], 0.0f);
        set(kFSeamDark, d[0], d[1], d[2], 0.0f);
    }
}

// Faithful: no boulder cliffs (the cliffs are relief); the boulder scans lie where the guide
// lists them - on rock no vehicle can use that does not stand as a tall cliff.
void DesignedTerrain::placeStones(const Terrain& t) {
    inst_.clear();
    int avail[kRockTypes], nAvail = 0;
    for (int r = 0; r < kRockTypes; ++r)
        if (rockCount_[r] > 0) avail[nAvail++] = r;
    if (nAvail > 0) {
        const auto& verts = t.vertices();
        for (const TerrainGuide::Stone& st : guide_.stones()) {
            const int hx = int(st.x), hy = int(st.y);
            const float r3 = hash2(hx, hy, 100), r6 = hash2(hx, hy, 120), r7 = hash2(hx, hy, 130);
            const int type = avail[std::min(nAvail - 1, int(r3 * float(nAvail)))];
            const float s = st.size / 32.0f;
            const float ground = t.height(st.x, st.y);
            const float sy = s * (0.5f + 0.35f * r6);
            RockInst ri{};
            ri.pos[0] = st.x;
            ri.pos[1] = ground - 1.2f;
            ri.pos[2] = st.y;
            ri.yaw = r6 * 6.2831853f * 7;
            ri.scale[0] = ri.scale[2] = s;
            ri.scale[1] = sy;
            ri.layer = float(type);
            const int gx = std::clamp(int(std::lround(st.x / t.step())), 0, t.gridW() - 1);
            const int gz = std::clamp(int(std::lround(st.y / t.step())), 0, t.gridH() - 1);
            ri.sun = verts[std::size_t(gz) * t.gridW() + gx].sun;
            ri.ground = ground;
            ri.top = ground - 1.2f + sy * rockHeight_[type];
            ri.bright = 0.9f + 0.2f * r7;
            inst_.push_back(ri);
        }
    }
    std::stable_sort(inst_.begin(), inst_.end(), [](const RockInst& a, const RockInst& b) { return a.layer < b.layer; });
    std::fill(std::begin(instCount_), std::end(instCount_), 0u);
    for (std::size_t i = 0; i < inst_.size(); ++i) {
        const int l = int(inst_[i].layer);
        if (instCount_[l]++ == 0) instFirst_[l] = std::uint32_t(i);
    }
    SDL_Log("3D terrain: %zu loose stones", inst_.size());
}

void DesignedTerrain::placeRocks(const game::World& w, const Terrain& t) {
    inst_.clear();
    const auto& kinds = t.cellKinds();
    if (kinds.size() != std::size_t(cw_) * ch_) return;
    auto kindAt = [&](int x, int y) {
        return kinds[std::size_t(std::clamp(y, 0, ch_ - 1)) * cw_ + std::size_t(std::clamp(x, 0, cw_ - 1))];
    };
    const auto& verts = t.vertices();
    auto sunAt = [&](float x, float z) {
        const int gx = std::clamp(int(std::lround(x / t.step())), 0, t.gridW() - 1);
        const int gz = std::clamp(int(std::lround(z / t.step())), 0, t.gridH() - 1);
        return verts[std::size_t(gz) * t.gridW() + gx].sun;
    };
    int avail[kRockTypes], nAvail = 0;
    for (int r = 0; r < kRockTypes; ++r)
        if (rockCount_[r] > 0) avail[nAvail++] = r;
    if (nAvail == 0) return;
    auto add = [&](float px, float pz, float base, float size, float sy, float yaw, int type, float ground, float top) {
        RockInst ri{};
        ri.pos[0] = px;
        ri.pos[1] = base;
        ri.pos[2] = pz;
        ri.yaw = yaw;
        ri.scale[0] = ri.scale[2] = size / 32.0f;
        ri.scale[1] = sy;
        ri.layer = float(type);
        ri.sun = sunAt(px, pz);
        ri.ground = ground;
        ri.top = top;
        ri.bright = 0.88f + 0.24f * hash2(int(px), int(pz), 7);
        inst_.push_back(ri);
    };
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            const std::uint8_t mat = cellMat_[std::size_t(y) * cw_ + x];
            if (kindAt(x, y) == Terrain::kCliffCell && mat == kCliff) {
                // Modelled cliff: one elongated rock along every face of the band that drops to
                // lower ground (long axis along the face, base below the low side, top a little
                // above the ridge), plus now and then one on the ridge itself.
                const float cx = float(x) * 32 + 16, cz = float(y) * 32 + 16;
                const float ridge = t.height(cx, cz);
                const int dx4[4] = {1, -1, 0, 0}, dy4[4] = {0, 0, 1, -1};
                int faces = 0;
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx4[k], ny = y + dy4[k];
                    if (nx < 0 || ny < 0 || nx >= cw_ || ny >= ch_) continue;
                    const float low = t.height(float(nx) * 32 + 16, float(ny) * 32 + 16);
                    if (ridge - low < 6) continue;
                    ++faces;
                    const float r1 = hash2(x, y, 10 + k), r3 = hash2(x, y, 30 + k), r4 = hash2(x, y, 40 + k),
                                r5 = hash2(x, y, 50 + k), r6 = hash2(x, y, 60 + k), r7 = hash2(x, y, 70 + k);
                    const float tx = float(-dy4[k]), tz = float(dx4[k]);  // along the face
                    // Out towards the face (from above: less, so the crags stay on their own cells).
                    const float off = topDown_ ? 3 + r5 * 3 : 8 + r5 * 4;
                    const float px = cx + float(dx4[k]) * off + tx * (r1 - 0.5f) * 10;
                    const float pz = cz + float(dy4[k]) * off + tz * (r1 - 0.5f) * 10;
                    const int type = avail[std::min(nAvail - 1, int(r3 * float(nAvail)))];
                    const float along = 40 + r4 * 16, across = topDown_ ? 20 + r7 * 8 : 24 + r7 * 12;
                    const float base = low - 5;
                    const float top = ridge + 1 + r6 * 7;
                    const float sy = std::max(0.6f, (top - base) / rockHeight_[type]);
                    float yaw = rockLongZ_[type] ? std::atan2(-tx, tz) : std::atan2(tz, tx);
                    yaw += (r6 - 0.5f) * 0.6f + (r7 > 0.5f ? 3.14159265f : 0.0f);
                    RockInst ri{};
                    ri.pos[0] = px;
                    ri.pos[1] = base;
                    ri.pos[2] = pz;
                    ri.yaw = yaw;
                    ri.scale[0] = (rockLongZ_[type] ? across : along) / 32.0f;
                    ri.scale[2] = (rockLongZ_[type] ? along : across) / 32.0f;
                    ri.scale[1] = sy;
                    ri.layer = float(type);
                    ri.sun = sunAt(px, pz);
                    ri.ground = low;
                    ri.top = base + sy * rockHeight_[type];
                    ri.bright = 0.88f + 0.24f * r5;
                    inst_.push_back(ri);
                }
                if (faces == 0 || hash2(x, y, 5) < (topDown_ ? 0.8f : 0.35f)) {
                    const float r1 = hash2(x, y, 11), r2 = hash2(x, y, 21), r3 = hash2(x, y, 31), r4 = hash2(x, y, 41),
                                r6 = hash2(x, y, 61);
                    const float px = cx + (r1 - 0.5f) * 14, pz = cz + (r2 - 0.5f) * 14;
                    const int type = avail[std::min(nAvail - 1, int(r3 * float(nAvail)))];
                    const float size = faces == 0 ? 34 + r4 * 18 : 22 + r4 * 12;
                    const float ground = t.height(px, pz);
                    const float sy = size / 32.0f * (0.5f + 0.5f * r6);
                    add(px, pz, ground - 6, size, sy, r6 * 6.2831853f * 3, type, ground - 6,
                        ground - 6 + sy * rockHeight_[type]);
                }
            } else if (mat == kRough) {
                // Rough (infantry-only) rock: a few small stones.
                for (int k = 0; k < 3; ++k) {
                    if (hash2(x, y, 70 + k) > 0.6f) continue;
                    const float r1 = hash2(x, y, 80 + k), r2 = hash2(x, y, 90 + k), r3 = hash2(x, y, 100 + k),
                                r4 = hash2(x, y, 110 + k), r6 = hash2(x, y, 120 + k);
                    const float px = float(x) * 32 + 4 + r1 * 24, pz = float(y) * 32 + 4 + r2 * 24;
                    const int type = avail[std::min(nAvail - 1, int(r3 * float(nAvail)))];
                    const float size = 9 + r4 * 12, s = size / 32.0f;
                    const float ground = t.height(px, pz);
                    const float sy = s * (0.8f + 0.5f * r6);
                    add(px, pz, ground - 1.5f, size, sy, r6 * 6.2831853f * 7, type, ground, ground - 1.5f + sy * rockHeight_[type]);
                }
            }
        }
    (void)w;
    std::stable_sort(inst_.begin(), inst_.end(), [](const RockInst& a, const RockInst& b) { return a.layer < b.layer; });
    std::fill(std::begin(instCount_), std::end(instCount_), 0u);
    for (std::size_t i = 0; i < inst_.size(); ++i) {
        const int l = int(inst_[i].layer);
        if (instCount_[l]++ == 0) instFirst_[l] = std::uint32_t(i);
    }
    SDL_Log("3D terrain: %zu rocks", inst_.size());
}

void DesignedTerrain::prepare(SDL_GPUCommandBuffer* cmd) {
    if (staticUploaded_ && !splatDirty_ && !spiceDirty_ && !instDirty_ && !guideDirty_) return;
    std::vector<SDL_GPUTransferBuffer*> temps;
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    auto staging = [&](const void* data, Uint32 size) -> SDL_GPUTransferBuffer* {
        SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0};
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        if (!tb) return nullptr;
        void* p = SDL_MapGPUTransferBuffer(dev_, tb, false);
        std::memcpy(p, data, size);
        SDL_UnmapGPUTransferBuffer(dev_, tb);
        temps.push_back(tb);
        return tb;
    };
    auto uploadTex = [&](SDL_GPUTexture* dst, const void* data, Uint32 bytes, int w, int h, int layer) {
        if (!dst) return;
        SDL_GPUTransferBuffer* tb = staging(data, bytes);
        if (!tb) return;
        SDL_GPUTextureTransferInfo src{tb, 0, Uint32(w), Uint32(h)};
        SDL_GPUTextureRegion reg{};
        reg.texture = dst;
        reg.layer = Uint32(layer);
        reg.w = Uint32(w);
        reg.h = Uint32(h);
        reg.d = 1;
        SDL_UploadToGPUTexture(cp, &src, &reg, false);
    };
    auto uploadBuf = [&](SDL_GPUBuffer* dst, const void* data, Uint32 bytes) {
        if (!dst || bytes == 0) return;
        SDL_GPUTransferBuffer* tb = staging(data, bytes);
        if (!tb) return;
        SDL_GPUTransferBufferLocation src{tb, 0};
        SDL_GPUBufferRegion reg{dst, 0, bytes};
        SDL_UploadToGPUBuffer(cp, &src, &reg, false);
    };
    const bool firstUpload = !staticUploaded_;
    if (firstUpload) {
        const Uint32 bytes = Uint32(kTex) * kTex * 4;
        for (int m = 0; m < kLayers; ++m) {
            uploadTex(albedoArr_, albedoCpu_[m].data(), bytes, kTex, kTex, m);
            uploadTex(normalArr_, normalCpu_[m].data(), bytes, kTex, kTex, m);
        }
        for (int r = 0; r < kRockTypes; ++r)
            if (!rockTexCpu_[r].empty()) uploadTex(rockArr_, rockTexCpu_[r].data(), bytes, kTex, kTex, r);
        uploadBuf(rockVB_, rockVerts_.data(), Uint32(rockVerts_.size() * 4));
    }
    if (splatDirty_) {
        uploadTex(splatATex_, splatA_.data(), Uint32(splatA_.size() * 4), splatW_, splatH_, 0);
        uploadTex(splatBTex_, splatB_.data(), Uint32(splatB_.size() * 4), splatW_, splatH_, 0);
    }
    if (spiceDirty_) {
        buildSpiceTex();
        uploadTex(spiceTex_, spiceTexCpu_.data(), Uint32(spiceTexCpu_.size()), cw_, ch_, 0);
    }
    const bool guideUpload = guideDirty_ && guideTex_ && guide_.valid();
    if (guideUpload) uploadTex(guideTex_, guide_.texels().data(), Uint32(guide_.texels().size() * 4), guideW_, guideH_, 0);
    if (instDirty_ && !inst_.empty()) {
        const std::uint32_t n = std::uint32_t(inst_.size());
        if (n > instCap_) {
            if (instVB_) SDL_ReleaseGPUBuffer(dev_, instVB_);
            SDL_GPUBufferCreateInfo bi{};
            bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            bi.size = Uint32(n * sizeof(RockInst));
            instVB_ = SDL_CreateGPUBuffer(dev_, &bi);
            instCap_ = instVB_ ? n : 0;
        }
        uploadBuf(instVB_, inst_.data(), Uint32(n * sizeof(RockInst)));
    }
    SDL_EndGPUCopyPass(cp);
    for (auto* tb : temps) SDL_ReleaseGPUTransferBuffer(dev_, tb);
    if (firstUpload) {
        for (SDL_GPUTexture* t : {albedoArr_, normalArr_, rockArr_})
            if (t) SDL_GenerateMipmapsForGPUTexture(cmd, t);
        for (auto& v : albedoCpu_) std::vector<std::uint32_t>().swap(v);
        for (auto& v : normalCpu_) std::vector<std::uint32_t>().swap(v);
        for (auto& v : rockTexCpu_) std::vector<std::uint32_t>().swap(v);
        staticUploaded_ = true;
    }
    if (guideUpload) {
        SDL_GenerateMipmapsForGPUTexture(cmd, guideTex_);
        guide_.releaseTexels();  // on the GPU now (16 MB for a 64 x 64 map)
    }
    splatDirty_ = spiceDirty_ = instDirty_ = guideDirty_ = false;
}

int DesignedTerrain::draw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, SDL_GPUBuffer* vb, SDL_GPUBuffer* ib,
                          std::uint32_t indexCount, const Frame& f, std::uint32_t firstIndex, const RockSet* rocks) {
    {  // dynamic point lights (fragment uniform buffer 1 of terrain2_fs / rock_fs)
        static PointLightsU none{};
        SDL_PushGPUFragmentUniformData(cmd, 1, f.lights ? f.lights : &none, sizeof(PointLightsU));
    }
    if (!staticUploaded_ || !splatATex_ || !splatBTex_ || !spiceTex_) return 0;
    int calls = 0;
    TerrainVU vu;
    std::memcpy(vu.viewProj, f.viewProj.m, sizeof vu.viewProj);
    const float flat = 1.0f / (f.ambient + f.diffuse * f.sun.y);
    auto common = [&](float* sunDir, float* light, float* fogColor, float* fog, float* fog2) {
        const float s[4] = {f.sun.x, f.sun.y, f.sun.z, 0}, l[4] = {f.ambient, f.diffuse, flat, f.darkest},
                    fc[4] = {f.fogColor[0], f.fogColor[1], f.fogColor[2], 1}, fo[4] = {f.eye.x, f.eye.y, f.eye.z, f.fogStart},
                    f2[4] = {f.fogEnd, f.mapW, f.mapH, f.bake ? 2.0f : f.shroudOn ? 1.0f : 0.0f};
        std::memcpy(sunDir, s, sizeof s);
        std::memcpy(light, l, sizeof l);
        std::memcpy(fogColor, fc, sizeof fc);
        std::memcpy(fog, fo, sizeof fo);
        std::memcpy(fog2, f2, sizeof f2);
    };
    const bool faith = faithfulActive() && faithPipe_ && guideTex_;
    if (vb && ib && indexCount > 0) {
        SDL_BindGPUGraphicsPipeline(pass, faith ? faithPipe_ : terrainPipe_);
        SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
        Terrain2FU fu{};
        common(fu.sunDir, fu.light, fu.fogColor, fu.fog, fu.fog2);
        for (int m = 0; m < kMaterials; ++m) {
            std::memcpy(fu.tint[m], tint_[m], sizeof tint_[m]);
            std::memcpy(fu.lum[m], lum_[m], sizeof lum_[m]);
        }
        std::memcpy(fu.tint[7], concreteLook_[0], sizeof concreteLook_[0]);
        std::memcpy(fu.lum[7], concreteLook_[1], sizeof concreteLook_[1]);
        fu.params[1] = 0.22f;  // height-blend depth
        fu.params[2] = 0.16f;  // macro variation
        fu.params[3] = 0.5f;   // half of the colour lock by luminance (unifies the scans' hues)
        std::memcpy(fu.lightVP, f.lightVP.m, sizeof fu.lightVP);
        std::memcpy(fu.shadow, f.shadow, sizeof fu.shadow);
        SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
        SDL_GPUBufferBinding vbb{vb, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vbb, 1);
        SDL_GPUBufferBinding ibb{ib, 0};
        SDL_BindGPUIndexBuffer(pass, &ibb, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_GPUTextureSamplerBinding sb[8] = {{albedoArr_, matSampler_}, {normalArr_, matSampler_},
                                              {splatATex_, f.linearClamp}, {splatBTex_, f.linearClamp},
                                              {spiceTex_, f.linearClamp}, {f.shroud, f.linearClamp},
                                              {f.shadowMap, f.shadowSampler}, {guideTex_, guideSampler_}};
        if (faith) {
            FaithU u3;
            static_assert(sizeof faithU_ == kFMapRows * 4 * sizeof(float) && sizeof faithU_ <= sizeof(FaithU));
            std::memcpy(&u3, faithU_, sizeof faithU_);
            const Vec3 ds = f.detailSun.y > 0 ? f.detailSun : f.sun;
            const float ra = f.reliefAmbient > 0 ? f.reliefAmbient : f.ambient, rd = f.reliefDiffuse > 0 ? f.reliefDiffuse : f.diffuse;
            const float dsU[4] = {ds.x, ds.y, ds.z, 1.0f / (f.ambient + f.diffuse * ds.y)};
            const float rlU[4] = {ra, rd, 1.0f / (ra + rd * f.sun.y), 0};
            std::memcpy(u3.detailSun, dsU, sizeof dsU);
            std::memcpy(u3.reliefLight, rlU, sizeof rlU);
            SDL_PushGPUFragmentUniformData(cmd, 2, &u3, sizeof u3);
        }
        SDL_BindGPUFragmentSamplers(pass, 0, sb, faith ? 8 : 7);
        SDL_DrawGPUIndexedPrimitives(pass, indexCount, 1, firstIndex, 0, 0);
        ++calls;
    }
    SDL_GPUBuffer* ivb = rocks ? rocks->vb : instVB_;
    const std::uint32_t* ifirst = rocks ? rocks->first : instFirst_;
    const std::uint32_t* icount = rocks ? rocks->count : instCount_;
    if (!inst_.empty() && ivb && rockVB_) {
        SDL_BindGPUGraphicsPipeline(pass, rockPipe_);
        SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
        RockFU fu{};
        common(fu.sunDir, fu.light, fu.fogColor, fu.fog, fu.fog2);
        for (int r = 0; r < kRockTypes; ++r) std::memcpy(fu.tint[r], rockTint_[r], sizeof rockTint_[r]);
        std::memcpy(fu.lightVP, f.lightVP.m, sizeof fu.lightVP);
        std::memcpy(fu.shadow, f.shadow, sizeof fu.shadow);
        SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof fu);
        SDL_GPUBufferBinding vbs[2] = {{rockVB_, 0}, {ivb, 0}};
        SDL_BindGPUVertexBuffers(pass, 0, vbs, 2);
        SDL_GPUTextureSamplerBinding sb[3] = {{rockArr_, matSampler_}, {f.shroud, f.linearClamp},
                                              {f.shadowMap, f.shadowSampler}};
        SDL_BindGPUFragmentSamplers(pass, 0, sb, 3);
        for (int r = 0; r < kRockTypes; ++r) {
            if (icount[r] == 0 || rockCount_[r] == 0) continue;
            SDL_DrawGPUPrimitives(pass, rockCount_[r], icount[r], rockFirst_[r], ifirst[r]);
            ++calls;
        }
    }
    return calls;
}

int DesignedTerrain::drawShadow(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const Mat4& lightVP,
                                SDL_GPUTextureFormat fmt, const RockSet* rocks) {
    SDL_GPUBuffer* ivb = rocks ? rocks->vb : instVB_;
    const std::uint32_t* ifirst = rocks ? rocks->first : instFirst_;
    const std::uint32_t* icount = rocks ? rocks->count : instCount_;
    if (inst_.empty() || !ivb || !rockVB_ || !staticUploaded_) return 0;
    if (!rockDepthPipe_ && !rockDepthFailed_) {
        SDL_GPUShader* vs = createShader(dev_, ShaderId::RockVs, 0, 1);
        SDL_GPUShader* fs = createShader(dev_, ShaderId::DepthFs, 0, 0);
        if (vs && fs) {
            SDL_GPUGraphicsPipelineCreateInfo pi{};
            pi.vertex_shader = vs;
            pi.fragment_shader = fs;
            SDL_GPUVertexBufferDescription vbd[2] = {{0, 8 * 4, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
                                                     {1, sizeof(RockInst), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0}};
            SDL_GPUVertexAttribute at[6] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
                                            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
                                            {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},
                                            {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0},
                                            {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16},
                                            {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 32}};
            pi.vertex_input_state = {vbd, 2, at, 6};
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
            rockDepthPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        }
        for (SDL_GPUShader* s2 : {vs, fs})
            if (s2) SDL_ReleaseGPUShader(dev_, s2);
        if (!rockDepthPipe_) {
            SDL_Log("3D terrain: rock shadow pipeline failed: %s", SDL_GetError());
            rockDepthFailed_ = true;
            return 0;
        }
    }
    if (!rockDepthPipe_) return 0;
    SDL_BindGPUGraphicsPipeline(pass, rockDepthPipe_);
    TerrainVU vu;
    std::memcpy(vu.viewProj, lightVP.m, sizeof vu.viewProj);
    SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof vu);
    SDL_GPUBufferBinding vbs[2] = {{rockVB_, 0}, {ivb, 0}};
    SDL_BindGPUVertexBuffers(pass, 0, vbs, 2);
    int calls = 0;
    for (int r = 0; r < kRockTypes; ++r) {
        if (icount[r] == 0 || rockCount_[r] == 0) continue;
        SDL_DrawGPUPrimitives(pass, rockCount_[r], icount[r], rockFirst_[r], ifirst[r]);
        ++calls;
    }
    return calls;
}

}  // namespace d2k::render3d

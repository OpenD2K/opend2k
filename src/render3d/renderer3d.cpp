#include "render3d/renderer3d.hpp"

#include "game/random.hpp"
#include "game/world.hpp"
#include "render/art_pack.hpp"
#include "render/bloom.hpp"
#include "render/hdr.hpp"
#include "render/effects.hpp"
#include "render/lighting.hpp"
#include "render/mod_art.hpp"
#include "render/scene_lights.hpp"
#include "render/soft_shroud.hpp"
#include "render/sprites.hpp"
#include "render/tileset.hpp"
#include "render/weld_fx.hpp"
#include "render3d/gpu.hpp"
#include "render3d/shader_blobs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace d2k::render3d {

namespace {

constexpr std::uint32_t kInitialQuads = 8192;
constexpr int kFrameDigitOne = 18;  // DATA.R16 group digits (17 + n, map_renderer.cpp): "1" = PRIMARY
constexpr int kSpriteFloats = 19;  // pos3 anchor3 uv2 color4 params4 shadowPoint3
constexpr float kShadowKeep = 0.12f;  // diffuse light kept in full sun shadow (shadow map)

struct TerrainVU {
    float viewProj[16];
};
struct TerrainFU {
    float sunDir[4];
    float light[4];
    float fogColor[4];
    float fog[4];
    float fog2[4];
    float lightVP[16];
    float shadow[4];
};
struct SpriteVU {
    float viewProj[16];
    float eye[4];
    float fog[4];
    float lightVP[16];
};
struct SpriteFU {
    float mode[4];
    float fogColor[4];
    float shadow[4];
};

// Depth-only passes of the sun shadow map: no culling (open scans, flat billboards), slope bias.
SDL_GPURasterizerState shadowRasterizer() {
    SDL_GPURasterizerState r{};
    r.fill_mode = SDL_GPU_FILLMODE_FILL;
    r.cull_mode = SDL_GPU_CULLMODE_NONE;
    r.enable_depth_clip = gpuDepthClip();
    r.enable_depth_bias = true;
    r.depth_bias_constant_factor = 2.0f;
    r.depth_bias_slope_factor = 2.5f;
    return r;
}

constexpr float kFogR = 0.50f, kFogG = 0.42f, kFogB = 0.33f;

SDL_GPUColorTargetBlendState blendFor(int pipe) {
    SDL_GPUColorTargetBlendState b{};
    b.enable_blend = true;
    b.color_blend_op = SDL_GPU_BLENDOP_ADD;
    b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    switch (pipe) {
    case 0:  // straight alpha
        b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case 1:  // additive (Additive explosions / bullets, Brighten and the tints' add pass)
        b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        break;
    case 2:  // subtractive (Subtract, the tints' subtract pass): dst - src
        b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        b.color_blend_op = SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
        break;
    case 4:  // light (Modern effects glows): dst * (1 + src), brightens without washing out
        b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
        b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        break;
    default:  // multiply (sprite shadow masks: white = no change, grey halves)
        b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
        b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        break;
    }
    return b;
}

}  // namespace

std::unique_ptr<Renderer3D> Renderer3D::create(SDL_Renderer* r, render::SpriteCache& sprites,
                                               const formats::Templates& templates, const Quality& quality) {
    SDL_GPUDevice* dev = gpuDeviceOf(r);
    if (!dev) {
        SDL_Log("3D mode: the renderer is not the SDL GPU renderer (%s)", r ? SDL_GetRendererName(r) : "none");
        return nullptr;
    }
    if (!deviceHasShaders(dev)) {
        // e.g. Direct3D 12 in a build without dxc (no DXIL): render3d/gpu.cpp normally asks for
        // Vulkan then; SDL_GPU_DRIVER (environment) can force a driver.
        SDL_Log("3D mode: no shaders for GPU driver %s in this build (formats 0x%x, built 0x%x)",
                SDL_GetGPUDeviceDriver(dev), unsigned(SDL_GetGPUShaderFormats(dev)), unsigned(builtShaderFormats()));
        return nullptr;
    }
    std::unique_ptr<Renderer3D> p(new Renderer3D(r, dev, sprites, templates));
    p->quality_ = quality;
    if (!p->init()) return nullptr;
    return p;
}

Renderer3D::Renderer3D(SDL_Renderer* r, SDL_GPUDevice* dev, render::SpriteCache& sprites, const formats::Templates& t)
    : r_(r), dev_(dev), sprites_(sprites), t_(t) {
    // Terrain damage decal jitter, as MapRenderer::drawDecals (Setup__LoadGraphicsFile 0x466140).
    game::Random rng(1);
    for (int i = 0; i < 143137 + 64; ++i) rng.next();
    for (int i = 0; i < 16; ++i) {
        decalJitterX_[i] = static_cast<int>(static_cast<std::uint32_t>(rng.next()) % 12) - 12;
        decalJitterY_[i] = static_cast<int>(static_cast<std::uint32_t>(rng.next()) % 12) - 12;
    }
}

Renderer3D::~Renderer3D() {
    releaseTargets();
    if (weldTex_) SDL_DestroyTexture(weldTex_);
    if (glareGlowTex_) SDL_DestroyTexture(glareGlowTex_);
    if (glareGhostTex_) SDL_DestroyTexture(glareGhostTex_);
    auto rel = [&](SDL_GPUTexture*& t) { if (t) SDL_ReleaseGPUTexture(dev_, t); t = nullptr; };
    auto relb = [&](SDL_GPUBuffer*& b) { if (b) SDL_ReleaseGPUBuffer(dev_, b); b = nullptr; };
    rel(tileArray_);
    rel(tileMap_);
    rel(white_);
    rel(clearMask_);
    rel(ring_);
    rel(shadowTex_);
    relb(terrainVB_);
    relb(terrainIB_);
    relb(spriteVB_);
    relb(quadIB_);
    if (spriteTB_) SDL_ReleaseGPUTransferBuffer(dev_, spriteTB_);
    if (terrainPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, terrainPipe_);
    if (terrainDepthPipe_) SDL_ReleaseGPUGraphicsPipeline(dev_, terrainDepthPipe_);
    if (shadowSampler_) SDL_ReleaseGPUSampler(dev_, shadowSampler_);
    for (auto* p : spritePipe_)
        if (p) SDL_ReleaseGPUGraphicsPipeline(dev_, p);
    if (linearClamp_) SDL_ReleaseGPUSampler(dev_, linearClamp_);
    if (tileSampler_) SDL_ReleaseGPUSampler(dev_, tileSampler_);
    if (pointClamp_) SDL_ReleaseGPUSampler(dev_, pointClamp_);
}

bool Renderer3D::init() {
    // With a stencil when the device has one (the model pass marks units on walkable building
    // cells: docs/3d-models.md "Walkable cells"); the same depth precision first.
    const SDL_GPUSampleCount wantMsaa = quality_.msaa >= 4 ? SDL_GPU_SAMPLECOUNT_4
                                        : quality_.msaa >= 2 ? SDL_GPU_SAMPLECOUNT_2
                                                             : SDL_GPU_SAMPLECOUNT_1;
    auto usable = [&](SDL_GPUTextureFormat fmt) {
        return SDL_GPUTextureSupportsFormat(dev_, fmt, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET) &&
               (wantMsaa == SDL_GPU_SAMPLECOUNT_1 || SDL_GPUTextureSupportsSampleCount(dev_, fmt, wantMsaa));
    };
    if (usable(SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT))
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    else if (usable(SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT))
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    else if (SDL_GPUTextureSupportsFormat(dev_, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                     SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    else if (SDL_GPUTextureSupportsFormat(dev_, SDL_GPU_TEXTUREFORMAT_D24_UNORM, SDL_GPU_TEXTURETYPE_2D,
                                          SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    else
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    // HDR: a half-float scene (values above 1 survive until the tone map), when the device can
    // render to it, sample it and multisample it as asked.
    const SDL_GPUSampleCount want = quality_.msaa >= 4 ? SDL_GPU_SAMPLECOUNT_4 : quality_.msaa >= 2 ? SDL_GPU_SAMPLECOUNT_2
                                                                                                  : SDL_GPU_SAMPLECOUNT_1;
    if (quality_.hdr && gpuHdrOk() &&
        SDL_GPUTextureSupportsFormat(dev_, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                     SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER) &&
        (want == SDL_GPU_SAMPLECOUNT_1 || SDL_GPUTextureSupportsSampleCount(dev_, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, want)))
        colorFormat_ = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    for (SDL_GPUSampleCount sc : {SDL_GPU_SAMPLECOUNT_4, SDL_GPU_SAMPLECOUNT_2}) {
        if (int(sc) > int(want)) continue;
        if (SDL_GPUTextureSupportsSampleCount(dev_, colorFormat_, sc) && SDL_GPUTextureSupportsSampleCount(dev_, depthFormat_, sc)) {
            samples_ = sc;
            break;
        }
    }

    SDL_GPUShader* tvs = createShader(dev_, ShaderId::TerrainVs, 0, 1);
    SDL_GPUShader* tfs = createShader(dev_, ShaderId::TerrainFs, 4, 2);
    SDL_GPUShader* svs = createShader(dev_, ShaderId::SpriteVs, 0, 1);
    SDL_GPUShader* sfs = createShader(dev_, ShaderId::SpriteFs, 3, 2);
    bool ok = tvs && tfs && svs && sfs;

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = colorFormat_;
    if (ok) {
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = tvs;
        pi.fragment_shader = tfs;
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
        terrainPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
        if (!terrainPipe_) SDL_Log("3D: terrain pipeline failed: %s", SDL_GetError());
        ok = terrainPipe_ != nullptr;
    }
    if (ok) {
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = svs;
        pi.fragment_shader = sfs;
        SDL_GPUVertexBufferDescription vbd{0, kSpriteFloats * 4, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute at[6] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
                                        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
                                        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},
                                        {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 32},
                                        {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 48},
                                        {5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 64}};
        pi.vertex_input_state = {&vbd, 1, at, 6};
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.enable_depth_clip = gpuDepthClip();
        pi.multisample_state.sample_count = samples_;
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = false;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        pi.target_info.color_target_descriptions = &ctd;
        pi.target_info.num_color_targets = 1;
        pi.target_info.depth_stencil_format = depthFormat_;
        pi.target_info.has_depth_stencil_target = true;
        for (int p = 0; p < kPipeCount && ok; ++p) {
            ctd.blend_state = blendFor(p);
            spritePipe_[p] = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
            if (!spritePipe_[p]) {
                SDL_Log("3D: sprite pipeline %d failed: %s", p, SDL_GetError());
                ok = false;
            }
        }
    }
    for (SDL_GPUShader* s : {tvs, tfs, svs, sfs})
        if (s) SDL_ReleaseGPUShader(dev_, s);
    if (!ok) return false;

    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    linearClamp_ = SDL_CreateGPUSampler(dev_, &si);
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    si.max_lod = 16;
    // Anisotropic filtering (Quality::aniso): the tiles at a tilt stay sharp into the distance.
    si.enable_anisotropy = gpuAnisotropy() && quality_.aniso > 1;
    si.max_anisotropy = float(std::max(1, quality_.aniso));
    tileSampler_ = SDL_CreateGPUSampler(dev_, &si);
    si.enable_anisotropy = false;
    si.max_anisotropy = 0;
    si.min_filter = si.mag_filter = SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.max_lod = 0;
    pointClamp_ = SDL_CreateGPUSampler(dev_, &si);
    if (!linearClamp_ || !tileSampler_ || !pointClamp_) return false;

    auto tex2d = [&](SDL_GPUTextureFormat f, int w, int h) {
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = f;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ci.width = Uint32(w);
        ci.height = Uint32(h);
        ci.layer_count_or_depth = 1;
        ci.num_levels = 1;
        return SDL_CreateGPUTexture(dev_, &ci);
    };
    white_ = tex2d(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 1, 1);
    clearMask_ = tex2d(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 1, 1);
    ring_ = tex2d(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 64, 64);
    if (!white_ || !clearMask_ || !ring_) return false;

    if (!initShadows()) return false;
    shroud_ = std::make_unique<render::SoftShroud>(r_, sprites_);
    SDL_Log("3D mode: SDL_gpu %s, %dx MSAA, anisotropy %dx, %s scene", SDL_GetGPUDeviceDriver(dev_),
            samples_ == SDL_GPU_SAMPLECOUNT_4 ? 4 : samples_ == SDL_GPU_SAMPLECOUNT_2 ? 2 : 1, std::max(1, quality_.aniso),
            hdrScene() ? "half-float (HDR)" : "8-bit");
    return true;
}

// Sun shadow map (docs/3d-mode.md "Sun shadows"): a depth texture the terrain / rock / model
// shaders sample with a comparison sampler (PCF), and the terrain's depth-only pipeline.
bool Renderer3D::initShadows() {
    const SDL_GPUTextureUsageFlags use = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    shadowFmt_ = SDL_GPUTextureSupportsFormat(dev_, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, use)
                     ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
                     : SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = shadowFmt_;
    ci.usage = use;
    ci.width = ci.height = Uint32(kShadowSize);
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    shadowTex_ = SDL_CreateGPUTexture(dev_, &ci);
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.enable_compare = true;
    si.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;  // 1 = lit (the point is not behind the stored depth)
    shadowSampler_ = SDL_CreateGPUSampler(dev_, &si);
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
        pi.rasterizer_state = shadowRasterizer();
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = true;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        pi.target_info.num_color_targets = 0;
        pi.target_info.depth_stencil_format = shadowFmt_;
        pi.target_info.has_depth_stencil_target = true;
        terrainDepthPipe_ = SDL_CreateGPUGraphicsPipeline(dev_, &pi);
    }
    for (SDL_GPUShader* s : {vs, fs})
        if (s) SDL_ReleaseGPUShader(dev_, s);
    if (!shadowTex_ || !shadowSampler_ || !terrainDepthPipe_) {
        SDL_Log("3D: shadow map failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

// The light's view: orthographic along the sun, its x/y range fitted to the ground the camera
// sees (the view corners' rays hit the planes y = 0 and y = the highest terrain + 64), its depth
// range the whole map box (every caster between the sun and a visible point is inside: in an
// orthographic projection a caster and its shadow share the light-space x / y). The extents are
// rounded up to 64 px and the origin snapped to whole texels, so panning does not make the edges
// crawl. depthRange: world px from near to far (for the depth bias).
Mat4 Renderer3D::fitLight(float mapW, float mapH, float& depthRange) const {
    const Vec3 L = sun_;
    const float top = terrainTop_ + 64;
    Vec3 pts[16];
    int n = 0;
    const float W = float(cam_.viewW()), Hh = float(cam_.viewH());
    const float sxs[4] = {0, W, 0, W}, sys[4] = {0, 0, Hh, Hh};
    for (int k = 0; k < 4; ++k) {
        Vec3 o, d;
        cam_.ray(sxs[k], sys[k], o, d);
        for (float plane : {0.0f, top}) {
            Vec3 p = d.y < -1e-3f ? o + d * std::max(0.0f, (o.y - plane) / -d.y) : o + d * 20000.0f;
            p.x = std::clamp(p.x, -64.0f, mapW + 64);
            p.z = std::clamp(p.z, -64.0f, mapH + 64);
            p.y = plane;
            pts[n++] = p;
        }
    }
    Vec3 c{};
    for (int i = 0; i < n; ++i) c += pts[i] * (1.0f / float(n));
    const Mat4 view = lookAt(c + L * 4000.0f, c, Vec3{0, 1, 0});
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
    for (int i = 0; i < n; ++i) {
        const Vec4 v = view * Vec4{pts[i].x, pts[i].y, pts[i].z, 1};
        x0 = std::min(x0, v.x); x1 = std::max(x1, v.x);
        y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
    }
    float zn = 1e30f, zf = -1e30f;
    for (int i = 0; i < 8; ++i) {
        const Vec4 v = view * Vec4{(i & 1) ? mapW + 64 : -64.0f, (i & 2) ? top + 64 : -16.0f, (i & 4) ? mapH + 64 : -64.0f, 1};
        zn = std::min(zn, -v.z);
        zf = std::max(zf, -v.z);
    }
    const float pad = 24;  // PCF footprint
    float ew = std::ceil((x1 - x0 + 2 * pad) / 64.0f) * 64.0f, eh = std::ceil((y1 - y0 + 2 * pad) / 64.0f) * 64.0f;
    const float tx = ew / kShadowSize, ty = eh / kShadowSize;
    const float l = std::floor((x0 - pad) / tx) * tx, b = std::floor((y0 - pad) / ty) * ty;
    depthRange = std::max(1.0f, zf - zn);
    return orthoRH(l, l + ew, b, b + eh, std::max(1.0f, zn - 1), zf + 1) * view;
}

void Renderer3D::setAtlas(render::TileAtlas& atlas) {
    if (atlas_ == &atlas) return;
    atlas_ = &atlas;
    atlasDirty_ = true;
}

void Renderer3D::releaseTargets() {
    if (sceneSdl_) SDL_DestroyTexture(sceneSdl_);
    sceneSdl_ = nullptr;
    for (SDL_GPUTexture** t : {&sceneTex_, &msaaTex_, &depthTex_}) {
        if (*t) SDL_ReleaseGPUTexture(dev_, *t);
        *t = nullptr;
    }
    tw_ = th_ = 0;
}

bool Renderer3D::ensureTargets(int w, int h) {
    if (w == tw_ && h == th_ && sceneSdl_) return true;
    releaseTargets();
    if (w <= 0 || h <= 0) return false;
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.width = Uint32(w);
    ci.height = Uint32(h);
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.format = colorFormat_;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    sceneTex_ = SDL_CreateGPUTexture(dev_, &ci);
    if (samples_ != SDL_GPU_SAMPLECOUNT_1) {
        ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        ci.sample_count = samples_;
        msaaTex_ = SDL_CreateGPUTexture(dev_, &ci);
    }
    ci.format = depthFormat_;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    ci.sample_count = samples_;
    depthTex_ = SDL_CreateGPUTexture(dev_, &ci);
    if (!sceneTex_ || !depthTex_ || (samples_ != SDL_GPU_SAMPLECOUNT_1 && !msaaTex_)) {
        SDL_Log("3D: render targets failed: %s", SDL_GetError());
        releaseTargets();
        return false;
    }
    // Hand the resolved picture to the SDL_Renderer (it does not take ownership).
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, sceneTex_);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, hdrScene() ? SDL_PIXELFORMAT_RGBA64_FLOAT : SDL_PIXELFORMAT_ABGR8888);
    // The shaders write sRGB-coded values: SDL passes them through (no linear -> sRGB conversion
    // of a float texture) when the scene is drawn or sampled as usual (render/hdr.hpp).
    if (hdrScene()) SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    sceneSdl_ = SDL_CreateTextureWithProperties(r_, p);
    SDL_DestroyProperties(p);
    if (!sceneSdl_) {
        SDL_Log("3D: scene texture failed: %s", SDL_GetError());
        releaseTargets();
        return false;
    }
    SDL_SetTextureBlendMode(sceneSdl_, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(sceneSdl_, SDL_SCALEMODE_NEAREST);
    tw_ = w;
    th_ = h;
    return true;
}

// The session's atlas (upscaled tiles / art-pack tiles) -> one array layer per tile with a full
// mip chain, so tilted views filter properly and no tile ever bleeds into its neighbour.
// The array itself is SAMPLER only: the tiles are blitted and mipmapped in batches of kBatch
// layers in a small COLOR_TARGET scratch array and copied over (the same blits as before), one
// command buffer per batch. Direct3D 12 broke right after the old single pass (~800 layers x 7
// mips as colour targets = 5600 render-target views, and 5600 blits in one command buffer): SDL's
// D3D12 backend has 2048 sampler descriptors per heap per command buffer, every blit takes one,
// and when a draw binding several samplers straddles the end of a heap it writes past it and
// the descriptor tables bound before the heap switch go stale (SDL 3.4 "FIXME: need to error
// on overflow") - the device is removed. A batch needs kBatch * levels blits, well below 2048.
void Renderer3D::buildTileArray() {
    atlasDirty_ = false;
    if (tileArray_) SDL_ReleaseGPUTexture(dev_, tileArray_);
    tileArray_ = nullptr;
    tileArrayCount_ = 0;
    if (!atlas_ || atlas_->tileCount() <= 0) return;
    SDL_GPUTexture* src = gpuTextureOf(atlas_->texture());
    if (!src) return;
    const int scale = atlas_->scale();
    const int size = std::min(32 * scale, 128);
    int levels = 1;
    while ((size >> levels) >= 2) ++levels;
    const int count = atlas_->tileCount();
    constexpr int kBatch = 64;
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = ci.height = Uint32(size);
    ci.layer_count_or_depth = Uint32(count);
    ci.num_levels = Uint32(levels);
    tileArray_ = SDL_CreateGPUTexture(dev_, &ci);
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ci.layer_count_or_depth = Uint32(std::min(count, kBatch));
    SDL_GPUTexture* scratch = tileArray_ ? SDL_CreateGPUTexture(dev_, &ci) : nullptr;
    if (!tileArray_ || !scratch) {
        SDL_Log("3D: tile array failed: %s", SDL_GetError());
        if (tileArray_) SDL_ReleaseGPUTexture(dev_, tileArray_);
        tileArray_ = nullptr;
        return;
    }
    tileArrayCount_ = count;
    for (int base = 0; base < count; base += kBatch) {
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
        if (!cmd) {
            SDL_Log("3D: tile array command buffer failed: %s", SDL_GetError());
            break;
        }
        const int n = std::min(kBatch, count - base);
        for (int i = 0; i < n; ++i) {
            const int t = base + i;
            SDL_GPUBlitInfo bi{};
            bi.source.texture = src;
            bi.source.x = Uint32(((t % render::TileAtlas::kColumns) * render::TileAtlas::kStride + render::TileAtlas::kPad) * scale);
            bi.source.y = Uint32(((t / render::TileAtlas::kColumns) * render::TileAtlas::kStride + render::TileAtlas::kPad) * scale);
            bi.source.w = bi.source.h = Uint32(32 * scale);
            bi.destination.texture = scratch;
            bi.destination.layer_or_depth_plane = Uint32(i);
            bi.destination.w = bi.destination.h = Uint32(size);
            bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
            bi.filter = SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(cmd, &bi);
        }
        if (levels > 1) SDL_GenerateMipmapsForGPUTexture(cmd, scratch);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        for (int i = 0; i < n; ++i)
            for (int l = 0; l < levels; ++l) {
                SDL_GPUTextureLocation from{scratch, Uint32(l), Uint32(i), 0, 0, 0};
                SDL_GPUTextureLocation to{tileArray_, Uint32(l), Uint32(base + i), 0, 0, 0};
                const Uint32 e = Uint32(std::max(1, size >> l));
                SDL_CopyGPUTextureToTexture(cp, &from, &to, e, e, 1, false);
            }
        SDL_EndGPUCopyPass(cp);
        SDL_SubmitGPUCommandBuffer(cmd);
    }
    SDL_ReleaseGPUTexture(dev_, scratch);  // SDL frees it once the command buffers have run
    SDL_Log("3D: %d tiles, %dx%d texels, %d mip levels", tileArrayCount_, size, size, levels);
}

const render::Sprite* Renderer3D::sprite(int frame, int house) { return sprites_.get(frame, house); }

void Renderer3D::noteOriginalArt(const char* kind, int type, int frame) {
    const render::ArtPack* pack = sprites_.artPack();
    if (frame < 0 || (pack && pack->hasFrame(frame))) return;
    if (pack && pack->modArt() && pack->modArt()->ownFrame(frame)) return;  // the campaign's own art
    const long long key = (1ll << 50) ^ (static_cast<long long>(kind[0]) << 40) ^ (static_cast<long long>(type) << 1);
    if (fallbackLogged_.insert(key).second) SDL_Log("3D original art: %s %d (frame %d, not in the art pack)", kind, type, frame);
}

const render::Sprite* Renderer3D::objectSprite(int frame, int house, const char* kind, int type, const char* name) {
    if (frame < 0) return nullptr;
    const render::ArtPack* pack = sprites_.artPack();
    const bool redesigned = pack && pack->hasFrame(frame);
    static const bool original = [] {
        const char* e = std::getenv("D2K_3D_ORIGINAL_FALLBACK");
        return e && e[0] == '1';
    }();
    // A community campaign's own art (render/mod_art.hpp; the exception to "no original sprites in
    // 3D"): a frame the campaign changed, or any frame of a type whose template it changed, is a
    // billboard of the campaign's sprite - there is no model or redesign of it.
    const render::ModArt* mod = pack ? pack->modArt() : nullptr;
    const bool campaign = !redesigned && mod &&
                          (mod->ownFrame(frame) || (kind[0] == 'b' ? mod->touchedBuilding(type) : mod->touchedUnit(type)));
    const long long key = (static_cast<long long>(kind[0]) << 40) ^ (static_cast<long long>(kind[1]) << 32) ^
                          (static_cast<long long>(type) << 1) ^ (redesigned ? 1 : 0);
    if (fallbackLogged_.insert(key).second)
        SDL_Log("3D fallback: %s %d %s: no model, %s (frame %d)", kind, type, name ? name : "", redesigned
                ? "billboard of the redesigned art-pack frame" : campaign ? "billboard of the campaign's own sprite"
                : original ? "ORIGINAL sprite (D2K_3D_ORIGINAL_FALLBACK)" : "no redesigned frame - not drawn", frame);
    if (!redesigned && !original && !campaign) return nullptr;
    return sprites_.get(frame, house);
}

SDL_GPUTexture* Renderer3D::tex(SDL_Texture* t) {
    if (!t) return nullptr;
    if (seen_.insert(t).second) newTextures_ = true;
    return gpuTextureOf(t);
}

float Renderer3D::ppwAt(float wx, float wy, float z) const {
    const Vec3 p{wx, terrain_.height(wx, wy) + z, wy};
    return cam_.pixelsPerUnit(cam_.depthOf(p));
}

bool Renderer3D::cellHidden(int cx, int cy) const {
    if (!frameWorld_ || !frameWorld_->inBounds(cx, cy)) return true;
    return frameShroud_ && (frameWorld_->cell(cx, cy).shroud & 0xF) == 1;
}

bool Renderer3D::buildingHidden(const game::Building& b) const {
    const auto& t = t_.bin.buildings[b.type];
    const int fw = std::max(1, int(t.artWidth) / 32), fh = std::max(1, int(t.artHeight) / 32);
    for (int y = 0; y < fh; ++y)
        for (int x = 0; x < fw; ++x)
            if (!cellHidden(b.tileX + x, b.tileY + y)) return false;
    return true;
}

void Renderer3D::constrainPose(float& yaw, float& tilt) const {
    if (yawUnlocked()) {
        tilt = std::clamp(tilt, Camera3D::kMinTilt, Camera3D::kMaxTilt);
        return;
    }
    yaw = 0;
    tilt = std::clamp(tilt, Camera3D::kSpriteMinTilt, Camera3D::kSpriteMaxTilt);
}

bool Renderer3D::buildingAnchors(const game::World& w, int side, int building, std::string_view prefix,
                                 std::vector<render::EffectAnchor>& out) const {
    out.clear();
    if (!models_ || !models_->active() || !terrainBuilt_) return false;
    std::vector<ModelHook::AnchorWorld> pts;
    if (!models_->buildingAnchors(w, terrain_, side, building, prefix, pts)) return false;
    for (const auto& p : pts)
        out.push_back(render::EffectAnchor{p.pos.x, p.pos.z, p.pos.y - terrain_.height(p.pos.x, p.pos.z), p.size});
    return !out.empty();
}

bool Renderer3D::unitAnchors(const game::World& w, int side, int unit, std::string_view prefix,
                             std::vector<render::EffectAnchor>& out) const {
    out.clear();
    if (!models_ || !models_->active() || !terrainBuilt_) return false;
    std::vector<ModelHook::AnchorWorld> pts;
    if (!models_->unitAnchors(w, terrain_, side, unit, prefix, pts)) return false;
    for (const auto& p : pts)
        out.push_back(render::EffectAnchor{p.pos.x, p.pos.z, p.pos.y - terrain_.height(p.pos.x, p.pos.z), p.size});
    return !out.empty();
}

bool Renderer3D::terrainDepthThrough(const Vec3& p, float& depth) const {
    const Vec3 E = cam_.eye();
    const Vec3 d = normalize(p - E);
    if (d.y > -1e-3f) return false;
    // Search the heights p.y + 40 .. p.y - 60 (a sprite's lower edge is near the ground).
    const float yHi = p.y + 40, yLo = std::max(p.y - 60, -2.0f);
    float t0 = std::max(0.0f, (E.y - yHi) / -d.y);
    const float t1 = (E.y - yLo) / -d.y;
    auto above = [&](float t) {
        const Vec3 q = E + d * t;
        return q.y > terrain_.height(q.x, q.z);
    };
    if (!above(t0)) {
        depth = cam_.depthOf(E + d * t0);
        return true;
    }
    const float step = 4.0f;
    for (float t = t0 + step; t <= t1 + step; t += step) {
        if (!above(t)) {
            float lo = t - step, hi = t;
            for (int i = 0; i < 6; ++i) {
                const float mid = 0.5f * (lo + hi);
                if (above(mid)) lo = mid;
                else hi = mid;
            }
            depth = cam_.depthOf(E + d * hi);
            return true;
        }
    }
    return false;
}

// Camera-facing quad. Image pixel (u, v) from its top-left sits at
// anchor + right * (ox + u) - up * (oy + v) (rotated by `rot` about the anchor).
//
// Depth: the whole quad takes ONE depth (no per-pixel depth between a flat picture and the
// terrain it stands on). Using the anchor's own depth cut every sprite in half: the part of the
// image below the anchor covers the ground in FRONT of the object, which is nearer to the eye,
// so the terrain hid the lower half of vehicles and the front of buildings (the "fragmented"
// sprites). The quad is therefore pulled towards the eye until its lower edge is in front of
// the ground it covers (ray casts through the bottom corners; at least the flat-ground amount
// below * tan(tilt), at most 24 px more), so only real hills in front still hide it.
bool Renderer3D::pushBillboard(SDL_GPUTexture* t, const Vec3& base, float ox, float oy, float w, float h, float rot,
                               Pipe pipe, float r, float g, float b, float a, float flash, float bias, float g2x,
                               float g2y, bool rayDepth, float u0, float v0, float u1, float v1) {
    if (!t) return false;
    const float depth = cam_.depthOf(base);
    if (depth <= 1) return false;
    float sx = 0, sy = 0;
    if (!cam_.project(base, sx, sy)) return false;
    const float ppw = cam_.pixelsPerUnit(depth);
    const float ext = std::max({std::abs(ox), std::abs(ox + w), std::abs(oy), std::abs(oy + h)});
    const float margin = ext * ppw + 8;
    if (sx < -margin || sy < -margin || sx > cam_.viewW() + margin || sy > cam_.viewH() + margin) return false;
    Inst in;
    in.tex = t;
    in.pipe = pipe;
    in.billboard = true;
    const Vec3 R = cam_.right(), U = cam_.up(), E = cam_.eye();
    const float cr = std::cos(rot * kPi / 180), sr = std::sin(rot * kPi / 180);
    for (int k = 0; k < 4; ++k) {
        const float du = (k & 1) ? w : 0, dv = (k & 2) ? h : 0;
        const float px = ox + du, py = oy + dv;
        const float rx = px * cr - py * sr, ry = px * sr + py * cr;
        in.c[k] = base + R * rx - U * ry;
        in.s[k] = base + Vec3{0, 3, 0};  // the whole card takes the shadow of its ground point
        in.gx[k] = g2x + du;
        in.gy[k] = g2y + dv;
    }
    float zd = depth - bias;
    const float below = oy + h;
    if (below > 0) {
        const float tilt = std::clamp(cam_.pose().tilt, Camera3D::kMinTilt, Camera3D::kMaxTilt);
        const float flat = below * std::tan(tilt);
        float want = depth - flat - 2;
        if (rayDepth) {
            float m = 1e30f;
            for (float f : {0.0f, 0.5f, 1.0f}) {
                float d = 0;
                if (terrainDepthThrough(base + R * (ox + f * w) - U * below, d)) m = std::min(m, d);
            }
            if (m < 1e29f) want = std::max(std::min(m, depth - flat) - 2, depth - flat - 24);
        }
        zd = std::min(zd, want);
    }
    zd = std::max(zd, 2.0f);
    in.anchor = E + (base - E) * (zd / depth);
    in.u0 = u0;
    in.v0 = v0;
    in.u1 = u1;
    in.v1 = v1;
    in.r = r;
    in.g = g;
    in.b = b;
    in.a = a;
    in.flash = flash;
    in.depth = depth - bias;
    in.seq = int(insts_.size());
    insts_.push_back(in);
    return true;
}

void Renderer3D::billboard(const render::Sprite* sp, float gx, float gy, float z, float ox, float oy, Pipe pipe,
                           float alpha, float flash, float bias, float r, float g, float b, float rot, float g2x,
                           float g2y) {
    if (!sp || !sp->texture) return;
    const Vec3 base{gx, terrain_.height(gx, gy) + z, gy};
    const float ppw = cam_.pixelsPerUnit(std::max(1.0f, cam_.depthOf(base)));
    const SDL_Texture* lt = sp->levels.empty() ? sp->texture : sp->level(ppw).texture;
    SDL_GPUTexture* t = tex(const_cast<SDL_Texture*>(lt));
    if (std::isnan(g2x)) {
        g2x = gx + ox;
        g2y = gy - z + oy;
    }
    pushBillboard(t, base, ox, oy, sp->drawW, sp->drawH, rot, pipe, r, g, b, alpha, flash, bias, g2x, g2y, true);
}

// A ground-aligned sprite: image pixel (u, v) lies at world (x0 + u, y0 + v) on the terrain
// (or at the height flatY when >= 0).
void Renderer3D::decal(const render::Sprite* sp, bool shadowMask, float x0, float y0, Pipe pipe, float flatY,
                       float alpha, float flash, float r, float g, float b) {
    if (!sp) return;
    const float cx = x0 + sp->drawW * 0.5f, cy = y0 + sp->drawH * 0.5f;
    const float ppw = ppwAt(cx, cy, 0);
    const render::Sprite::Level* lv = sp->levels.empty() ? nullptr : &sp->level(ppw);
    SDL_Texture* t = shadowMask ? (lv ? lv->shadow : sp->shadow) : (lv ? lv->texture : sp->texture);
    if (!t) return;
    SDL_GPUTexture* gt = tex(t);
    if (!gt) return;
    const std::size_t n = insts_.size();
    decalTex(gt, x0, y0, sp->drawW, sp->drawH, pipe, flatY, r, g, b, alpha);
    if (insts_.size() > n) insts_.back().flash = flash;
}

void Renderer3D::decalTex(SDL_GPUTexture* t, float x0, float y0, float w, float h, Pipe pipe, float flatY, float r,
                          float g, float b, float a) {
    if (!t) return;
    const Vec3 c{x0 + w * 0.5f, flatY >= 0 ? flatY : terrain_.height(x0 + w * 0.5f, y0 + h * 0.5f), y0 + h * 0.5f};
    float sx = 0, sy = 0;
    if (!cam_.project(c, sx, sy)) return;
    const float ppw = cam_.pixelsPerUnit(std::max(1.0f, cam_.depthOf(c)));
    const float margin = (std::max(w, h) + 40) * ppw + 40;
    if (sx < -margin || sy < -margin || sx > cam_.viewW() + margin || sy > cam_.viewH() + margin) return;
    Inst in;
    in.tex = t;
    in.pipe = pipe;
    for (int k = 0; k < 4; ++k) {
        const float x = x0 + ((k & 1) ? w : 0), zz = y0 + ((k & 2) ? h : 0);
        in.c[k] = Vec3{x, flatY >= 0 ? flatY : terrain_.height(x, zz) + 1.0f, zz};
        in.s[k] = in.c[k] + Vec3{0, 1.5f, 0};
        in.gx[k] = x;
        in.gy[k] = zz;
    }
    in.r = r;
    in.g = g;
    in.b = b;
    in.a = a;
    in.seq = int(insts_.size());
    insts_.push_back(in);
}

void Renderer3D::groundQuad(SDL_GPUTexture* t, float cx, float cy, float hw, float hh, float angle, Pipe pipe, float r,
                            float g, float b, float a, float u0, float v0, float u1, float v1) {
    if (!t) return;
    const float ca = std::cos(angle), sa = std::sin(angle);
    Inst in;
    in.tex = t;
    in.pipe = pipe;
    for (int k = 0; k < 4; ++k) {
        const float lx = (k & 1) ? hw : -hw, ly = (k & 2) ? hh : -hh;
        const float x = cx + lx * ca - ly * sa, zz = cy + lx * sa + ly * ca;
        in.c[k] = Vec3{x, terrain_.height(x, zz) + 1.2f, zz};
        in.s[k] = in.c[k] + Vec3{0, 1.5f, 0};
        in.gx[k] = x;
        in.gy[k] = zz;
    }
    in.u0 = u0;
    in.v0 = v0;
    in.u1 = u1;
    in.v1 = v1;
    in.r = r;
    in.g = g;
    in.b = b;
    in.a = a;
    in.seq = int(insts_.size());
    insts_.push_back(in);
}

// ---------------------------------------------------------------------------------------------
// Scene: the objects of MapRenderer::draw (same frames, same rules), placed in 3D. Visibility
// follows the 2D renderer: nothing on a fully shrouded cell and nothing outside the map is
// drawn (the 2D shroud overlay covers those; here they are skipped, and every remaining pixel
// takes the shroud darkness of the map point it covers in the 2D view).

void Renderer3D::collect(const game::World& w, const Options& o) {
    insts_.clear();
    frameWorld_ = &w;
    frameShroud_ = o.showShroud;
    fx_ = o.effects;
    packAnchors_ = o.packAnchors;
    lenses_.clear();
    // Decals first, in the 2D draw order: craters, building bases, crates, buildings (sorted like
    // AddToDisplayList 0x42aa90), then unit shadows, silhouettes and selection rings.
    // Modern effects on the designed terrain: modern craters (same cells and damage levels,
    // render/effects_craters.cpp) instead of the original crater sprites.
    const bool modernCraters = o.effects && o.effects->craterTexture() && designedTerrainActive();
    if (modernCraters) {
        std::vector<render::Effects::CraterDecal> craters;
        o.effects->craterDecals(w, o.showShroud, 0, 0, w.width(), w.height(), craters);
        if (SDL_GPUTexture* ct = tex(o.effects->craterTexture())) {
            for (const auto& d : craters) {
                const float h = d.size * 0.5f;
                groundQuad(ct, d.x, d.y, h, h, 0, kAlpha, float(d.tint & 0xFF) / 255.0f, float((d.tint >> 8) & 0xFF) / 255.0f,
                           float((d.tint >> 16) & 0xFF) / 255.0f, 1.0f, d.u0, d.v0, d.u1, d.v1);
                groundQuad(ct, d.x, d.y, h, h, 0, kAlpha, 1, 1, 1, d.soot, d.u0, d.sv0, d.u1, d.sv1);
            }
        }
    }
    for (int y = 0; y < w.height() && !modernCraters; ++y)
        for (int x = 0; x < w.width(); ++x) {
            const auto& c = w.cell(x, y);
            if (c.damage <= 0x10) continue;
            if (o.showShroud && (c.shroud & 0xF) == 1) continue;
            const int idx = (c.damage >> 4) + 16 * ((x + y) & 1);
            int frame;
            if (c.flags & 0x8000) frame = 114 + idx;        // rock crater set
            else if (c.flags & 0x10000) frame = 146 + idx;  // sand crater set
            else continue;
            const int j = (x + y) & 15;
            decal(sprite(frame, -1), false, float(x * 32 + decalJitterX_[j]), float(y * 32 + decalJitterY_[j]), kAlpha);
        }
    // Tread marks (Modern effects) lie on the ground under everything else.
    if (o.effects) {
        const render::Effects& fx = *o.effects;
        if (SDL_GPUTexture* at = tex(fx.atlasTexture())) {
            float u0, v0, u1, v1;
            render::Effects::texRect(render::Effects::kSoft, u0, v0, u1, v1);
            const std::uint32_t c = render::Effects::treadColour();
            const float cr = float(c & 0xFF) / 255, cg = float((c >> 8) & 0xFF) / 255, cb = float((c >> 16) & 0xFF) / 255;
            for (const auto& t : fx.treads()) {
                const float a = render::Effects::treadAlpha(t);
                if (a <= 0.003f || pointHidden(t.x, t.y)) continue;
                const float px = -std::sin(t.angle) * 5, py = std::cos(t.angle) * 5;
                groundQuad(at, t.x + px, t.y + py, 2.6f, 1.5f, t.angle, kAlpha, cr, cg, cb, a, u0, v0, u1, v1);
                groundQuad(at, t.x - px, t.y - py, 2.6f, 1.5f, t.angle, kAlpha, cr, cg, cb, a, u0, v0, u1, v1);
            }
        }
    }
    // Building bases (pass A, 0x427d50).
    for (const auto& side : w.sides())
        for (int i = side.firstBuilding; i >= 0; i = side.nextObject(i)) {
            const auto& bld = side.buildings[i];
            if (!bld.alive || buildingHidden(bld)) continue;
            if (models_ && models_->active() && models_->hasBuildingModel(bld.type)) continue;
            const auto& t = t_.bin.buildings[bld.type];
            if (t.buildingArt < 0 || t.buildingArt >= formats::kMaxBuildingArt || (bld.flags & 0x100000)) continue;
            if ((bld.flags & 0x6) && bld.buildupCounter / 3 < t.buildupFramesToShow) continue;
            const int house = side.house;
            const int first = t_.buildingArtFirstFrame[t.buildingArt];
            const int n = t_.bin.buildingArtDirections[t.buildingArt];
            int frame = first;
            const char* bn = t_.bin.buildingNames[bld.type];
            if (!objectSprite(first + 1, house, "building", bld.type, bn) && bld.health * 2 < static_cast<std::int32_t>(t.hitPoints) && n > 0)
                frame = first + 1 + n;
            const int fw = std::max(1, int(t.artWidth) / 32), fh = std::max(1, int(t.artHeight) / 32);
            const float top = terrain_.maxHeight(bld.tileX, bld.tileY, bld.tileX + fw - 1, bld.tileY + fh - 1) + 0.6f;
            if (const render::Sprite* sp = objectSprite(frame, house, "building base", bld.type, bn))
                decal(sp, false, float(bld.tileX * 32 - sp->frame->x), float(bld.tileY * 32 + t.artHeight - sp->frame->y), kAlpha, top);
        }
    // Modern effects: shockwaves, scorch, rubble and embers of destroyed buildings (on the ground).
    if (o.effects && !o.effects->groundMarks().empty())
        if (SDL_GPUTexture* at = tex(o.effects->atlasTexture())) {
            using FX = render::Effects;
            for (const FX::GroundMark& m : o.effects->groundMarks()) {
                if (pointHidden(m.x, m.y)) continue;
                float u0, v0, u1, v1;
                FX::texRect(m.tex, u0, v0, u1, v1);
                const float r = float(m.c & 0xFF) / 255, g = float((m.c >> 8) & 0xFF) / 255, b = float((m.c >> 16) & 0xFF) / 255,
                            a = float(m.c >> 24) / 255;
                groundQuad(at, m.x, m.y, m.hw, m.hh, m.rot, m.blend == FX::Blend::Add ? kAdd : m.blend == FX::Blend::Light ? kLight : kAlpha,
                           r, g, b, a, u0, v0, u1, v1);
            }
        }
    // Sandworm sand spray (Display__DrawParticles 0x45d550): one-pixel specks of the tile colour (Modern effects:
    // the worm's own sand instead, render/effects_events.cpp).
    if (w.particleCount() > 0 && !(o.effects && render::Effects::replacesWormParticles()))
        for (const auto& p : w.particles()) {
            if (!p.active) continue;
            const int ix = int(std::lround(p.x)), iy = int(std::lround(p.y));
            if (cellHidden(ix >> 5, iy >> 5) || !atlas_) continue;
            const formats::Rgba c = atlas_->pixel(p.tile, p.tilePx, p.tilePy);
            auto ch = [](std::uint32_t v8) { return float((std::uint32_t(float(v8 >> 3) * 0.85f) << 3)) / 255.0f; };
            decalTex(white_, float(ix), float(iy), 1.4f, 1.4f, kAlpha, -1, ch(c & 0xFF), ch((c >> 8) & 0xFF), ch((c >> 16) & 0xFF), 1);
        }
    for (const auto& c : w.crates()) {
        if (!c.active || cellHidden(c.x, c.y)) continue;
        if (o.effects && render::Effects::replacesCrate(c.type)) continue;  // Modern draws the bloom mound
        // (Crates: no model and no redesigned frame in the 3D art pack yet - the one object still drawn from the
        // original art, a ground decal; docs/3d-mode.md "Objects".)
        noteOriginalArt("crate", c.type, 102 + c.image);
        decal(sprite(102 + c.image, -1), false, float(c.x * 32), float(c.y * 32), kAlpha);
    }
    {
        struct Entry { int key; int order; const game::Building* b; };
        std::vector<Entry> list;
        int order = 0;
        for (const auto& side : w.sides())
            for (int bi = side.firstBuilding; bi >= 0; bi = side.nextObject(bi)) {
                const auto& b = side.buildings[bi];
                if (!b.alive || buildingHidden(b)) continue;
                if (models_ && models_->active() && models_->hasBuildingModel(b.type)) continue;  // drawn by the model pass
                const auto& t = t_.bin.buildings[b.type];
                int key = b.tileY * 32 + t.artHeight;
                if (t.flags & 0x200000) key -= 32;
                list.push_back({key, order++, &b});
            }
        std::sort(list.begin(), list.end(),
                  [](const Entry& a, const Entry& b) { return a.key != b.key ? a.key < b.key : a.order > b.order; });
        for (const auto& e : list) addBuilding(w, *e.b);
    }
    // Units (ground units, infantry drawn behind vehicles too): billboards + ground shadows.
    for (const auto& side : w.sides())
        for (int ui = side.firstUnit; ui >= 0; ui = side.nextObject(ui)) {
            const auto& u = side.units[ui];
            if (!u.alive || (u.flags & 0x200140)) continue;
            if (pointHidden(float(game::fixToPixel(u.posX)), float(game::fixToPixel(u.posY)))) continue;
            if (models_ && models_->active() && models_->hasUnitModel(u.type)) continue;  // drawn by the model pass
            addUnit(w, u, o);
        }
    addFlyers(w);
    // Explosions (Blitexplosions 0x4282f0): billboards at their height; modes as the 2D blit.
    {
        using namespace game::explosion_flags;
        for (const auto& side : w.sides())
            for (int i = side.firstExplosion; i >= 0; i = side.nextObject(i)) {
                const auto& e = side.explosions[i];
                if (!e.alive || (e.flags & (kHidden | kBlinkOff)) || e.flags & 0x800) continue;
                if (e.type < 0 || e.type >= formats::kMaxExplosions) continue;
                if (o.effects && o.effects->replacesExplosion(e.type)) continue;  // Modern draws its own
                const int house = (e.flags & 0x8) ? side.house : -1;
                const int frame = t_.animationArtFirstFrame[e.type] + e.frame;
                const float gx = float(game::fixToPixel(e.posX)), gy = float(game::fixToPixel(e.posY));
                if (pointHidden(gx, gy)) continue;
                const float z = float(game::fixToPixel(e.posZ));
                const bool intensity = (e.flags & (0x2 | 0x4 | 0x40 | 0x80)) != 0;
                const render::Sprite* sp = intensity ? sprites_.getIntensity(frame) : sprite(frame, house);
                if (!sp) continue;
                noteOriginalArt("explosion", e.type, frame);
                const float ox = -float(sp->frame->x), oy = -float(sp->frame->y);
                constexpr float kBias = 10;  // in front of the object it hits
                if (e.flags & 0x2) billboard(sp, gx, gy, z, ox, oy, kSub, 1, 0, kBias);
                else if (e.flags & 0x4) billboard(sp, gx, gy, z, ox, oy, kAdd, 1, 0, kBias);
                else if (e.flags & 0x10) billboard(sp, gx, gy, z, ox, oy, kAlpha, 0.5f, 0, kBias);
                else if (e.flags & 0x40) {
                    billboard(sp, gx, gy, z, ox, oy, kAdd, 1, 0, kBias, 1, 0, 0);
                    billboard(sp, gx, gy, z, ox, oy, kSub, 1, 0, kBias, 0, 1, 1);
                } else if (e.flags & 0x80) {
                    billboard(sp, gx, gy, z, ox, oy, kAdd, 1, 0, kBias, 0, 1, 0);
                    billboard(sp, gx, gy, z, ox, oy, kSub, 1, 0, kBias, 1, 0, 0);
                } else if (e.flags & 0x200) billboard(sp, gx, gy, z, ox, oy, kAdd, 1, 0, kBias);
                else billboard(sp, gx, gy, z, ox, oy, kAlpha, 1, 0, kBias);
            }
    }
    // Bullets (BlitBullets 0x428070): silhouette on the ground, the projectile at its height.
    {
        using namespace game::weapon_flags;
        for (const auto& side : w.sides())
            for (int i = side.firstBullet; i >= 0; i = side.nextObject(i)) {
                const auto& b = side.bullets[i];
                if (!b.alive || b.delay != 0 || b.type < 0 || b.type >= formats::kMaxWeapons) continue;
                // Modern draws its own projectiles and the sonic beam (as 2D).
                if (o.effects && ((b.flags & kRangeLimited) || o.effects->replacesBullet(b.type))) continue;
                const int art = t_.bin.weapons[b.type].projectileArt;
                if (art >= formats::kMaxWeapons) continue;
                const render::Sprite* sp = sprite(t_.projectileArtFirstFrame[art] + b.animFrame, -1);
                if (!sp) continue;
                noteOriginalArt("projectile", b.type, t_.projectileArtFirstFrame[art] + b.animFrame);
                const float gx = float(game::fixToPixel(b.posX)), gy = float(game::fixToPixel(b.posY));
                if (pointHidden(gx, gy)) continue;
                if (b.flags & kRangeLimited) {
                    // The sonic shot (flag 0x800): not its blue square but the lens it stands for, a
                    // 16 px disc magnifying the picture under it 1.5x (drawPostRefraction).
                    const Vec3 base{gx, terrain_.height(gx, gy) + float(game::fixToPixel(b.posZ)), gy};
                    float sx = 0, sy = 0;
                    if (!cam_.project(base, sx, sy)) continue;
                    const float ppw = cam_.pixelsPerUnit(std::max(1.0f, cam_.depthOf(base)));
                    const float half = float(sp->frame->width) * 0.5f;
                    lenses_.push_back({sx + (half - float(sp->frame->x)) * ppw, sy + (half - float(sp->frame->y)) * ppw, half * ppw});
                    continue;
                }
                if (b.flags & kShadow) decal(sp, false, gx - sp->frame->x, gy - sp->frame->y, kAlpha, -1, 0.5f, 0, 0, 0, 0);
                billboard(sp, gx, gy, float(game::fixToPixel(b.posZ)), -float(sp->frame->x), -float(sp->frame->y),
                          (b.flags & kAdditive) ? kAdd : kAlpha, 1, 0, 14);
            }
    }
    if (o.effects) addEffects(*o.effects);
    if (o.lamps) addLampFlares(*o.lamps);
    addWeldFlashes(w);
    // Selection rings (decals) under selected units; buildings get a footprint frame.
    for (int si = 0; si < game::kMaxSides; ++si) {
        const auto& side = w.sides()[std::size_t(si)];
        const bool own = si == o.playerSide;
        const float cr = own ? 0.55f : 1.0f, cg = own ? 1.0f : 0.35f, cb = own ? 0.6f : 0.3f;
        for (int ui = side.firstUnit; ui >= 0; ui = side.nextObject(ui)) {
            const auto& u = side.units[ui];
            if (!u.alive || !u.selected || (u.flags & 0x200000)) continue;
            const float gx = float(game::fixToPixel(u.posX)), gy = float(game::fixToPixel(u.posY));
            if (pointHidden(gx, gy)) continue;
            const float r = t_.bin.units[u.type].isInfantry ? 11.0f : 18.0f;
            decalTex(ring_, gx - r, gy - r, 2 * r, 2 * r, kAlpha, -1, cr, cg, cb, 0.95f);
        }
        for (int bi = side.firstBuilding; bi >= 0; bi = side.nextObject(bi)) {
            const auto& b = side.buildings[bi];
            if (!b.alive || !b.selected || buildingHidden(b)) continue;
            const auto& t = t_.bin.buildings[b.type];
            const int fw = std::max(1, int(t.artWidth) / 32), fh = std::max(1, int(t.artHeight) / 32);
            const float top = terrain_.maxHeight(b.tileX, b.tileY, b.tileX + fw - 1, b.tileY + fh - 1) + 1.0f;
            const float x0 = b.tileX * 32.0f, y0 = b.tileY * 32.0f, x1 = x0 + fw * 32.0f, y1 = y0 + fh * 32.0f, k = 2;
            decalTex(white_, x0, y0, x1 - x0, k, kAlpha, top, cr, cg, cb, 0.9f);
            decalTex(white_, x0, y1 - k, x1 - x0, k, kAlpha, top, cr, cg, cb, 0.9f);
            decalTex(white_, x0, y0, k, y1 - y0, kAlpha, top, cr, cg, cb, 0.9f);
            decalTex(white_, x1 - k, y0, k, y1 - y0, kAlpha, top, cr, cg, cb, 0.9f);
        }
    }
}

// Modern effects (render/effects.hpp) in world space: particles as camera-facing quads at their
// height above the terrain (alpha / additive / light blending, depth tested like the sprites),
// light glows as decals on the terrain (dst * (1 + light)). The particle state is the 2D
// effect system's; only the projection differs.
void Renderer3D::addEffects(const render::Effects& fx) {
    SDL_GPUTexture* at = tex(fx.atlasTexture());
    if (!at) return;
    using FX = render::Effects;
    auto rgbaF = [](std::uint32_t c, float& r, float& g, float& b, float& a) {
        r = float(c & 0xFF) / 255;
        g = float((c >> 8) & 0xFF) / 255;
        b = float((c >> 16) & 0xFF) / 255;
        a = float(c >> 24) / 255;
    };
    // (The light list is not drawn as glows here: buildPointLights hands it to the shaders.)
    const Vec3 R = cam_.right(), U = cam_.up();
    auto one = [&](const FX::Particle& p) {
        float size = 0;
        std::uint32_t c = 0;
        if (!FX::particleLook(p, size, c)) return;
        if (pointHidden(p.x, p.y)) return;
        float r, g, b, a, u0, v0, u1, v1;
        rgbaF(c, r, g, b, a);
        if (a <= 0.002f) return;
        FX::texRect(p.tex, u0, v0, u1, v1);
        const Pipe pipe = p.blend == FX::Blend::Add ? kAdd : p.blend == FX::Blend::Light ? kLight : kAlpha;
        const Vec3 base{p.x, terrain_.height(p.x, p.y) + p.z, p.y};
        float w = size, h = size, rot = p.rot * 180 / kPi;
        if (p.streak) {  // stretched along the on-screen velocity
            const Vec3 vel{p.vx, p.vz, p.vy};
            const float svx = dot(vel, R), svy = -dot(vel, U);
            const float sp = std::hypot(svx, svy);
            w = std::max(size, std::min(size * 6, sp * 0.03f));
            rot = std::atan2(svy, svx) * 180 / kPi;
        } else if (p.aligned) {  // a ground-plane direction (wavefronts, beam strips, tracers) on screen
            const Vec3 d{std::cos(p.rot), 0, std::sin(p.rot)};
            const float svx = dot(d, R), svy = -dot(d, U);
            rot = std::atan2(svy, svx) * 180 / kPi;
            if (p.len > 0) w = p.len * std::max(0.2f, std::hypot(svx, svy));
        } else if (p.len > 0) {  // a strip with a screen rotation (falling bombs)
            w = p.len;
        }
        pushBillboard(at, base, -w * 0.5f, -h * 0.5f, w, h, rot, pipe, r, g, b, a, 0, 6, p.x - w * 0.5f,
                      p.y - p.z - h * 0.5f, false, u0, v0, u1, v1);
    };
    for (const auto& p : fx.beams()) one(p);
    for (const auto& p : fx.particles()) one(p);
}

// The Modern effects' light list (render::Effects::Light: drawn point, height, reach, colour with
// the intensity in alpha) -> the shaders' point light block: lights on screen and not under the
// shroud, the strongest (intensity x reach) first, at most kMaxPointLights. Each sits a little
// above its point so the ground right under it is lit from above.
void Renderer3D::buildPointLights(const Options& o, int viewW, int viewH) {
    // (The block's head - the time of day's colours - was set at the start of render().)
    int n = 0;
    auto onScreen = [&](const Vec3& p, float reach) {
        float sx = 0, sy = 0;
        if (!cam_.project(p, sx, sy)) return false;
        const float rpx = reach * cam_.pixelsPerUnit(std::max(1.0f, cam_.depthOf(p)));
        return !(sx < -rpx || sy < -rpx || sx > float(viewW) + rpx || sy > float(viewH) + rpx);
    };
    // Lamps of the time of day (render/scene_lights.*) take the slots the effects leave, at least
    // a third of them while lamps are on.
    const bool lampsOn = o.lamps && !o.lamps->lamps().empty();
    const int fxCap = lampsOn ? kMaxPointLights * 2 / 3 : kMaxPointLights;
    if (o.effects) {
        const auto& list = o.effects->lights();
        lightPick_.clear();
        for (int i = 0; i < int(list.size()); ++i) {
            const auto& l = list[std::size_t(i)];
            const float a = float(l.c >> 24) / 255.0f;
            if (a < 0.02f || l.size < 2) continue;
            const float gy = l.y + l.z;  // ground point under the source
            if (pointHidden(l.x, gy)) continue;
            if (!onScreen(Vec3{l.x, terrain_.height(l.x, gy) + l.z, gy}, l.size * 0.5f)) continue;
            lightPick_.emplace_back(a * l.size, i);
        }
        const std::size_t m = std::min<std::size_t>(lightPick_.size(), std::size_t(fxCap));
        std::partial_sort(lightPick_.begin(), lightPick_.begin() + std::ptrdiff_t(m), lightPick_.end(),
                          [](const auto& x, const auto& y) { return x.first > y.first; });
        constexpr float kGain = 1.35f;  // matches the 2D glow (Light blend, premultiplied x 1.6) on flat ground
        for (std::size_t k = 0; k < m; ++k) {
            const auto& l = list[std::size_t(lightPick_[k].second)];
            const float gy = l.y + l.z;
            const float lift = 8 + 0.06f * l.size;
            const float a = float(l.c >> 24) / 255.0f * kGain;
            lightsU_.posRadius[n][0] = l.x;
            lightsU_.posRadius[n][1] = terrain_.height(l.x, gy) + l.z + lift;
            lightsU_.posRadius[n][2] = gy;
            lightsU_.posRadius[n][3] = l.size * 0.55f + lift;
            lightsU_.color[n][0] = float(l.c & 0xFF) / 255.0f * a;
            lightsU_.color[n][1] = float((l.c >> 8) & 0xFF) / 255.0f * a;
            lightsU_.color[n][2] = float((l.c >> 16) & 0xFF) / 255.0f * a;
            lightsU_.spot[n][3] = -2;
            ++n;
        }
    }
    if (lampsOn && n < kMaxPointLights) {
        // Priority x intensity x reach, falling off with the distance from the view centre.
        const auto& lamps = o.lamps->lamps();
        const Vec3 c{cam_.pose().cx, 0, cam_.pose().cy};
        lightPick_.clear();
        for (int i = 0; i < int(lamps.size()); ++i) {
            const auto& l = lamps[std::size_t(i)];
            if (pointHidden(l.x, l.y)) continue;
            const Vec3 p{l.x, terrain_.height(l.x, l.y) + l.z, l.y};
            if (!onScreen(p, l.reach * 0.6f)) continue;
            const float d = std::hypot(l.x - c.x, l.y - c.z);
            lightPick_.emplace_back(l.priority * l.a * l.reach / (1.0f + d / 480.0f), i);
        }
        // Budget: every lit fragment loops over the whole block, so the lamps get at most 16 slots
        // at ~1 Mpx, fewer on bigger views (8 at 4 Mpx: a Retina full-screen view); the nearest
        // to the view centre win, the rest keep their flares. D2K_LAMP_SLOTS overrides (tests).
        static const int lampSlotsEnv = SDL_getenv("D2K_LAMP_SLOTS") ? std::atoi(SDL_getenv("D2K_LAMP_SLOTS")) : -1;
        const float mpx = float(viewW) * float(viewH) / 1.0e6f;
        const int lampSlots = lampSlotsEnv >= 0 ? lampSlotsEnv : std::clamp(int(std::lround(16.0f / std::sqrt(std::max(1.0f, mpx)))), 8, 16);
        const std::size_t m = std::min<std::size_t>(lightPick_.size(), std::size_t(std::max(0, std::min(lampSlots, kMaxPointLights - n))));
        std::partial_sort(lightPick_.begin(), lightPick_.begin() + std::ptrdiff_t(m), lightPick_.end(),
                          [](const auto& x, const auto& y) { return x.first > y.first; });
        for (std::size_t k = 0; k < m; ++k) {
            const auto& l = lamps[std::size_t(lightPick_[k].second)];
            const float g = terrain_.height(l.x, l.y);
            if (l.kind == render::Lamp::Cone) {
                // A beam from the lamp, tilted down to reach the ground about half way along it.
                const float ahead = l.reach * 0.5f;
                const Vec3 d = normalize(Vec3{l.dirX * ahead, -l.z - 2, l.dirY * ahead});
                lightsU_.posRadius[n][0] = l.x;
                lightsU_.posRadius[n][1] = g + l.z + 2;
                lightsU_.posRadius[n][2] = l.y;
                lightsU_.posRadius[n][3] = l.reach * 1.1f;
                lightsU_.spot[n][0] = d.x;
                lightsU_.spot[n][1] = d.y;
                lightsU_.spot[n][2] = d.z;
                lightsU_.spot[n][3] = std::cos(std::min(1.3f, l.spread * 1.15f));
                const float a = l.a * 1.8f;  // a beam is narrow: brighter than a pool
                lightsU_.color[n][0] = l.r * a;
                lightsU_.color[n][1] = l.g * a;
                lightsU_.color[n][2] = l.b * a;
            } else {
                const float lift = 6 + 0.05f * l.reach;
                lightsU_.posRadius[n][0] = l.x;
                lightsU_.posRadius[n][1] = g + l.z + lift;
                lightsU_.posRadius[n][2] = l.y;
                lightsU_.posRadius[n][3] = l.reach * 0.55f + lift;
                lightsU_.spot[n][3] = -2;
                const float a = l.a * 1.05f;
                lightsU_.color[n][0] = l.r * a;
                lightsU_.color[n][1] = l.g * a;
                lightsU_.color[n][2] = l.b * a;
            }
            ++n;
        }
    }
    lightsU_.info[0] = float(n);
}

// Weld flashes (render/weld_fx.hpp): a blue-white halo and a white core at the anchor the arc is
// on this tick, pulled towards the eye like the lamp flares (the bay's walls do not swallow them).
void Renderer3D::addWeldFlashes(const game::World& w) {
    if (!models_ || !models_->active()) return;
    for (int si = 0; si < int(w.sides().size()); ++si) {
        const auto& side = w.sides()[std::size_t(si)];
        for (int bi = side.firstBuilding; bi >= 0; bi = side.nextObject(bi)) {
            const auto& b = side.buildings[std::size_t(bi)];
            if (!b.alive || !(b.flags & 0x30) || (b.flags & 0x6) || buildingHidden(b)) continue;
            if (!models_->hasBuildingModel(b.type) || !buildingAnchors(w, si, bi, "fx_weld", weldTmp_)) continue;
            render::WeldFlash fl;
            static const bool all = std::getenv("D2K_WELD_ALL") != nullptr;  // check: every anchor lit, steady
            if (all) fl = render::WeldFlash{0, 1, 1};
            else if (!render::weldFlash(w, b, t_, int(weldTmp_.size()), fl)) continue;
            if (!weldTex_) weldTex_ = render::makeWeldGlowTexture(r_);
            SDL_GPUTexture* t = tex(weldTex_);
            if (!t) return;
            for (std::size_t ai = 0; ai < weldTmp_.size(); ++ai) {
                if (!all && int(ai) != fl.anchor) continue;
                const render::EffectAnchor& an = weldTmp_[ai];
                const Vec3 base{an.x, terrain_.height(an.x, an.y) + an.z, an.y};
                auto glow = [&](float size, const float* c, float a) {
                    const float s = size * an.size * fl.size;
                    pushBillboard(t, base, -s * 0.5f, -s * 0.5f, s, s, 0, kAdd, c[0], c[1], c[2], std::min(1.0f, a), 0, 14,
                                  an.x - s * 0.5f, an.y - an.z - s * 0.5f, false);
                };
                glow(26, render::kWeldHalo, 0.75f * fl.a);
                glow(9, render::kWeldCore, fl.a);
            }
        }
    }
}

// The lamps themselves (render/scene_lights.*): small additive glows at the lamp, in front of the
// object they sit on (pulled towards the eye) so a model's own surface does not hide them.
void Renderer3D::addLampFlares(const render::SceneLights& l) {
    SDL_GPUTexture* t = tex(l.flareTexture());
    if (!t) return;
    for (const render::Flare& f : l.flares()) {
        if (pointHidden(f.x, f.y)) continue;
        const Vec3 base{f.x, terrain_.height(f.x, f.y) + f.z, f.y};
        const float s = f.size * 2;
        pushBillboard(t, base, -s * 0.5f, -s * 0.5f, s, s, 0, kAdd, f.r, f.g, f.b, std::min(1.0f, f.a), 0, f.pull,
                      f.x - s * 0.5f, f.y - f.z - s * 0.5f, false);
    }
}

void Renderer3D::addBuilding(const game::World& w, const game::Building& bld) {
    // MapRenderer::drawBuilding (BlitVehicles 0x428e30), laid on the footprint.
    const auto& b = t_.bin.buildings[bld.type];
    if (b.buildingArt < 0 || b.buildingArt >= formats::kMaxBuildingArt) return;
    const int house = w.sides()[bld.side].house;
    auto linksWithWall = [&](int x, int y) {
        if (!w.inBounds(x, y)) return false;
        const auto& c = w.cell(x, y);
        if (c.buildingSide < 0) return false;
        const auto& other = w.sides()[c.buildingSide].buildings[c.buildingIndex];
        if (other.tileX != x || other.tileY != y) return false;
        const int sb = t_.bin.buildings[other.type].specialBehavior;
        return sb == 14 || sb == 16;
    };
    static constexpr int kWallFrameMapping[16] = {0, 2, 3, 12, 1, 6, 11, 7, 4, 13, 5, 9, 15, 10, 8, 14};
    const int first = t_.buildingArtFirstFrame[b.buildingArt];
    const int n = t_.bin.buildingArtDirections[b.buildingArt];
    int f = 0, wallFrame = 0;
    if (b.specialBehavior == 14 || b.specialBehavior == 16) {
        int m = 0;
        if (linksWithWall(bld.tileX - 1, bld.tileY)) m |= 1;
        if (linksWithWall(bld.tileX, bld.tileY - 1)) m |= 2;
        if (linksWithWall(bld.tileX + 1, bld.tileY)) m |= 4;
        if (linksWithWall(bld.tileX, bld.tileY + 1)) m |= 8;
        f = wallFrame = kWallFrameMapping[m];
    } else if (b.specialBehavior == 5) {
        const auto& sd = w.sides()[bld.side];
        if (sd.maxStorage > 0) {
            const auto lvl = static_cast<std::uint64_t>(static_cast<std::uint32_t>(sd.spice)) * 4500u /
                             static_cast<std::uint32_t>(sd.maxStorage) / 1500u;
            f = static_cast<int>(std::min<std::uint64_t>(lvl, static_cast<std::uint64_t>(std::max(1, n) - 1)));
        }
    }
    int mainFrame = first + 1 + f;
    const char* bn = t_.bin.buildingNames[bld.type];
    auto objSprite = [&](int frame) { return objectSprite(frame, house, "building", bld.type, bn); };
    if (bld.health * 2 < static_cast<std::int32_t>(b.hitPoints) && objSprite(first + 1 + n + wallFrame))
        mainFrame = first + 1 + n + wallFrame;
    const bool hasBarrel = b.barrelArt >= 0 && b.barrelArt < formats::kMaxBuildingArt;
    const int anchorX = bld.tileX * 32, anchorY = bld.tileY * 32 + b.artHeight;
    const int shiftX = hasBarrel ? b.exitPoint1X : 0, shiftY = hasBarrel ? b.exitPoint1Y : 0;
    const float flash = (bld.flicker & 2) ? 0.43f : 0.0f;
    const render::Sprite* mainSp = objSprite(mainFrame);
    const render::Sprite* barrelSp =
        hasBarrel ? objSprite(t_.buildingArtFirstFrame[b.barrelArt] + 1 + b.directionFrames[(bld.facing - yawSteps_) & 31])
                  : nullptr;
    const render::Sprite* animSp = nullptr;
    bool animAdditive = false;
    if (bld.flags & 0x6) {
        const int k = bld.buildupCounter / 3;
        const render::Sprite* bsp =
            b.buildupArt < formats::kMaxBuildingTypes ? objSprite(t_.buildupArtFirstFrame[b.buildupArt] + k) : nullptr;
        if (!mainSp || k < b.buildupFramesToShow) {
            mainSp = bsp;
            barrelSp = nullptr;
        } else {
            animSp = bsp;
        }
    } else if ((bld.flags & 0x30) && (bld.flags & 0x40) && bld.health * 2 >= static_cast<std::int32_t>(b.hitPoints)) {
        const int a = b.buildingAnimation;
        if (a >= 0 && a < formats::kMaxBuildingTypes) {
            const int k = bld.animCounter / std::max(1, int(t_.bin.buildings[a].animationSpeed));
            if (k < t_.bin.buildingAnimationFrames[a]) animSp = objSprite(t_.buildingAnimationFirstFrame[a] + k);
            animAdditive = (b.flags & 0x800000) != 0;
        }
    }
    const int fw = std::max(1, int(b.artWidth) / 32), fh = std::max(1, int(b.artHeight) / 32);
    const float top = terrain_.maxHeight(bld.tileX, bld.tileY, bld.tileX + fw - 1, bld.tileY + fh - 1) + 0.6f;
    // Walls and concrete are thin: they lie on the ground as decals (a standing card reads as a
    // panel). Everything else stands upright on the footprint edge nearest to the camera (at
    // yaw 0 the south edge, i.e. exactly the 2D placement relative to it), columns centred on
    // the footprint, sorted by the footprint centre; the shadow pixels lie flat on the ground.
    const bool flatOnGround = b.specialBehavior == game::building_behaviour::kWall ||
                              b.specialBehavior == game::building_behaviour::kConcrete;
    const float cxw = bld.tileX * 32.0f + fw * 16.0f, cyw = bld.tileY * 32.0f + fh * 16.0f;
    const float yaw = cam_.pose().yaw, sy = std::sin(yaw), cy = std::cos(yaw);
    const float reach = std::abs(fw * 16.0f * sy) + std::abs(fh * 16.0f * cy);
    const float ax3 = cxw - sy * reach, ay3 = cyw + cy * reach;  // nearest edge (ground forward = (sin, -cos))
    const float centreDepth = cam_.depthOf(Vec3{cxw, top, cyw});
    // Modern effects: a destroyed building collapses (render::Effects::collapseLook, as 2D).
    render::Effects::CollapseLook cl;
    const bool collapsing = fx_ && bld.state == 17 && fx_->collapseLook(bld.side, bld.index, cl);
    auto layCollapsed = [&](const render::Sprite* sp, int ax, int ay, float bias, bool whole) {
        const float x0 = float(ax - sp->frame->x) + cl.dx, y0 = float(ay - sp->frame->y) + cl.dy;
        if (sp->shadow && cl.sink < 0.45f) decal(sp, true, x0, y0, kMul);
        const float gz = top - terrain_.height(ax3, ay3);
        const Vec3 base{ax3, top, ay3};
        const float ppw = cam_.pixelsPerUnit(std::max(1.0f, cam_.depthOf(base)));
        const SDL_Texture* lt = sp->levels.empty() ? sp->texture : sp->level(ppw).texture;
        SDL_GPUTexture* t = tex(const_cast<SDL_Texture*>(lt));
        if (!t) return;
        (void)gz;
        const float ox = x0 - cxw, oy = y0 - (bld.tileY * 32.0f + fh * 32.0f);
        const float k = cl.shade;
        const Vec3 R = cam_.right();
        const int n = whole ? 1 : cl.pieces;
        for (int i = 0; i < n; ++i) {
            render::Effects::CollapseLook::Piece p = whole ? render::Effects::CollapseLook::Piece{0, 1, cl.sink, 0} : cl.piece[i];
            const float pw = (p.u1 - p.u0) * sp->drawW, ph = (1 - p.sink) * sp->drawH;
            if (ph < 0.5f) continue;
            const std::size_t before = insts_.size();
            if (flatOnGround) {
                decalTex(t, x0 + p.u0 * sp->drawW, y0 + p.sink * sp->drawH, pw, ph, kAlpha, top + bias, k, k * 0.93f, k * 0.86f, 1);
                if (insts_.size() > before) {
                    Inst& in = insts_.back();
                    in.u0 = p.u0;
                    in.u1 = p.u1;
                    in.v1 = 1 - p.sink;
                }
                continue;
            }
            pushBillboard(t, base, ox + p.u0 * sp->drawW, oy + p.sink * sp->drawH, pw, ph, 0, kAlpha, k, k * 0.93f, k * 0.86f, 1, 0,
                          4 + bias, x0 + p.u0 * sp->drawW, y0 + p.sink * sp->drawH, true, p.u0, 0, p.u1, 1 - p.sink);
            if (insts_.size() > before) {
                Inst& in = insts_.back();
                in.depth = centreDepth - bias;
                in.c[0] = in.c[0] + R * p.lean;  // the top leans outwards
                in.c[1] = in.c[1] + R * p.lean;
            }
        }
    };
    auto lay = [&](const render::Sprite* sp, int ax, int ay, Pipe pipe, float fl, float bias) {
        if (!sp) return;
        if (collapsing) {
            if (pipe == kAlpha && bias < 0.03f) layCollapsed(sp, ax, ay, bias, bias > 0.01f);  // no animation overlay
            return;
        }
        const float x0 = float(ax - sp->frame->x), y0 = float(ay - sp->frame->y);
        if (sp->shadow) decal(sp, true, x0, y0, kMul);
        if (flatOnGround) {
            decal(sp, false, x0, y0, pipe, top + bias, 1, fl);
            return;
        }
        const std::size_t n = insts_.size();
        billboard(sp, ax3, ay3, top - terrain_.height(ax3, ay3), x0 - cxw, y0 - (bld.tileY * 32.0f + fh * 32.0f), pipe, 1, fl,
                  4 + bias, 1, 1, 1, 0, x0, y0);
        if (insts_.size() > n) insts_.back().depth = centreDepth - bias;
    };
    lay(mainSp, anchorX + shiftX, anchorY + shiftY, kAlpha, flash, 0);
    lay(barrelSp, anchorX + shiftX, anchorY + shiftY, kAlpha, flash, 0.02f);
    // A rendered pack's redesigned building with fx_weld anchors: weld flashes on its card instead
    // of the original overlay (render/weld_fx.hpp; as MapRenderer::drawBuilding).
    if (animSp && !(bld.flags & 0x6) && !collapsing && packAnchors_ &&
        packAnchors_->buildingAnchors(w, bld.side, bld.index, "fx_weld", weldTmp_)) {
        animSp = nullptr;
        render::WeldFlash fl;
        if (render::weldFlash(w, bld, t_, int(weldTmp_.size()), fl)) {
            if (!weldTex_) weldTex_ = render::makeWeldGlowTexture(r_);
            if (SDL_GPUTexture* t = tex(weldTex_)) {
                const render::EffectAnchor& an = weldTmp_[std::size_t(fl.anchor)];
                const float gx = an.x, gy = an.y - an.z;  // the point on the 2D art
                const Vec3 base{ax3, top, ay3};
                for (int pass = 0; pass < 2; ++pass) {
                    const float s = (pass == 0 ? 26.0f : 9.0f) * an.size * fl.size;
                    const float* c = pass == 0 ? render::kWeldHalo : render::kWeldCore;
                    const std::size_t n0 = insts_.size();
                    pushBillboard(t, base, gx - cxw - s * 0.5f, gy - (bld.tileY * 32.0f + fh * 32.0f) - s * 0.5f, s, s, 0, kAdd,
                                  c[0], c[1], c[2], std::min(1.0f, (pass == 0 ? 0.75f : 1.0f) * fl.a), 0, 4.05f, gx - s * 0.5f,
                                  gy - s * 0.5f, true);
                    if (insts_.size() > n0) insts_.back().depth = centreDepth - 0.05f;
                }
            }
        }
    }
    if (animSp) lay(animSp, anchorX, anchorY, animAdditive ? kAdd : kAlpha, animAdditive ? 0.0f : flash, 0.04f);
    if ((bld.flags & 0x1) && ((static_cast<std::uint32_t>(bld.index) + w.gameTicks()) & 0x20)) {
        const render::Sprite* ref = mainSp ? mainSp : objSprite(first);
        const render::Sprite* icon = sprite(3, -1);  // repair wrench
        if (ref && icon)
            billboard(icon, cxw, cyw, top - terrain_.height(cxw, cyw) + 30, -icon->frame->width / 2.0f,
                      -icon->frame->height / 2.0f, kAlpha, 1, 0, 40);
    }
}

void Renderer3D::addUnit(const game::World& w, const game::Unit& unit, const Options& o) {
    // MapRenderer::drawUnit: the body / turret frames, but turned by the camera yaw (the frame
    // shows the unit as seen from the camera); the ground shadow keeps the world facing. With
    // Modern effects the eased facing picks the frame and the residual angle turns the
    // billboard (as the 2D blitUnit).
    const auto& u = t_.bin.units[unit.type];
    if (u.specialBehavior == 5) return;  // sandworms: no sprite (see MapRenderer::drawUnit)
    const int house = w.sides()[unit.side].house;
    const float gx = float(game::fixToPixel(unit.posX)), gy = float(game::fixToPixel(unit.posY));
    const float z = float(game::fixToPixel(unit.posZ));
    float flash = (unit.flicker & 2) ? 0.43f : 0.0f, alpha = 1;
    if (unit.flags & 0x4) {
        if (unit.side != o.playerSide) return;
        alpha = 0.5f;
        flash = 0;
    } else if (unit.stealthTimer != 0) {
        alpha = 0.5f;
        flash = 0;
    }
    // A unit on the deck of a building model (the docked harvester on the refinery's pad) stands
    // on top of it, pulled a little more towards the eye so the pad's own surface does not cut
    // it; tall parts of the model still hide it (ModelHook::groundOffset).
    const float deck = models_ && models_->active() ? models_->groundOffset(w, gx, gy) : 0.0f;
    // On a walkable front-row cell of a building model: pulled further, over that model's parts
    // there (model units do this exactly with the stencil: ModelRenderer::draw).
    const float deckBias = deck > 0 ? 3.0f : models_ && models_->active() ? models_->walkableBias(w, gx, gy) : 0.0f;
    const render::Effects::UnitVisual* vis = o.effects ? o.effects->unitVisual(unit.side, unit.index) : nullptr;
    auto eased = [&](float f, int& dir32, float& degrees) {
        const float r = std::round(f);
        dir32 = int(r) & 31;
        degrees = (f - r) * 11.25f;
    };
    if (u.unitArt >= 0 && u.unitArt < formats::kMaxUnitArt) {
        const int dirs = t_.bin.unitArtDirectionFrames[u.unitArt];
        const int base = t_.unitArtFirstFrame[u.unitArt] + unit.animFrame * dirs;
        int f32 = unit.facing & 31;
        float rot = 0;
        if (vis) eased(vis->facing, f32, rot);
        if (dirs != 32) rot = 0;
        int dir = u.directionFrames[(f32 - yawSteps_) & 31], worldDir = u.directionFrames[f32];
        if (unit.animType > 8) dir = worldDir = unit.animType - 9;
        int frame = base + dir, shadowFrame = base + worldDir;
        if (u.specialBehavior == 1 && unit.animFrame != 0) {
            const int a = static_cast<std::uint8_t>(t_.bin.groupIds[64]);
            if (a < formats::kMaxExplosions) frame = shadowFrame = t_.animationArtFirstFrame[a] + unit.animFrame;
            rot = 0;
        }
        const char* un = t_.bin.unitNames[unit.type];
        if (const render::Sprite* sh = objectSprite(shadowFrame, house, "unit", unit.type, un); sh && sh->shadow && alpha >= 1)
            decal(sh, true, gx - sh->frame->x, gy - sh->frame->y, kMul);
        if (const render::Sprite* sp = objectSprite(frame, house, "unit", unit.type, un))
            billboard(sp, gx, gy, z + deck, -float(sp->frame->x), -float(sp->frame->y), kAlpha, alpha, flash, deckBias, 1, 1,
                      1, rot);
    }
    if (u.barrelArt >= 0 && u.barrelArt < formats::kMaxUnitArt) {
        int f32 = unit.facingTurret & 31;
        float rot = 0;
        if (vis) eased(vis->turret, f32, rot);
        if (t_.bin.unitArtDirectionFrames[u.barrelArt] != 32) rot = 0;
        const int frame = t_.unitArtFirstFrame[u.barrelArt] + u.directionFrames[(f32 - yawSteps_) & 31];
        if (const render::Sprite* sp = objectSprite(frame, house, "unit turret", unit.type, t_.bin.unitNames[unit.type]))
            billboard(sp, gx, gy, z + deck, -float(sp->frame->x), -float(sp->frame->y), kAlpha, alpha, flash,
                      0.5f + deckBias, 1, 1, 1, rot);
    }
}

// Display__DrawFlyingUnits 0x42a5d0: flyers with a ground silhouette, carried units and the
// cargo of a delivery carryall, at their height above the terrain. Flyers over shrouded cells
// or outside the map (arriving carryalls / frigates) are not drawn, as in 2D.
void Renderer3D::addFlyers(const game::World& w) {
    auto frameOf = [&](int type, int animFrame, int facing) -> int {
        if (type < 0 || type >= formats::kMaxUnitTypes) return -1;
        const auto& t = t_.bin.units[type];
        if (t.unitArt < 0 || t.unitArt >= formats::kMaxUnitArt) return -1;
        const int dirs = t_.bin.unitArtDirectionFrames[t.unitArt];
        return t_.unitArtFirstFrame[t.unitArt] + animFrame * dirs + t.directionFrames[facing & 31];
    };
    auto height = [](std::int32_t z) { return float(game::fixToPixel(z) & 0xff); };
    for (const auto& side : w.sides())
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const auto& u = side.units[i];
            if (!u.alive || !(u.flags & 0x140) || (u.flags & 0x200000)) continue;
            const float gx = float(game::fixToPixel(u.posX)), gy = float(game::fixToPixel(u.posY));
            if (pointHidden(gx, gy)) continue;
            if (models_ && models_->active() && models_->hasUnitModel(u.type)) continue;
            const char* fn = t_.bin.unitNames[u.type];
            const render::Sprite* body = objectSprite(frameOf(u.type, u.animFrame, u.facing - yawSteps_), side.house, "flyer", u.type, fn);
            if (!body) continue;
            if ((u.flags & 0x40) && !(u.flags & 0x20000))
                if (const render::Sprite* sh = objectSprite(frameOf(u.type, u.animFrame, u.facing), side.house, "flyer", u.type, fn))
                    decal(sh, false, gx - sh->frame->x, gy - sh->frame->y, kAlpha, -1, 0.5f, 0, 0, 0, 0);
            billboard(body, gx, gy, height(u.posZ), -float(body->frame->x), -float(body->frame->y), kAlpha);
            if (u.flags & 0x20000000) {
                const auto& d = side.deliveries[std::size_t(u.lastMoved % 10)];
                const int cargo = d.units[0];
                if (cargo >= 0xfe) continue;
                if (const render::Sprite* cs = objectSprite(frameOf(cargo, u.animFrame, u.facing - yawSteps_), side.house, "carried unit", cargo,
                                                            cargo < formats::kMaxUnitTypes ? t_.bin.unitNames[cargo] : ""))
                    billboard(cs, gx, gy, height(u.posZ - 0xa0000), -float(cs->frame->x), -float(cs->frame->y), kAlpha, 1, 0, -0.5f);
            }
        }
}

// ---------------------------------------------------------------------------------------------

bool Renderer3D::ensureDesigned() {
    if (!designed_ && !designedFailed_) {
        designed_ = DesignedTerrain::create(dev_, colorFormat_, depthFormat_, samples_, quality_.aniso);
        designedFailed_ = designed_ == nullptr;
    }
    return designed_ != nullptr;
}

void Renderer3D::render(const game::World& w, const CameraPose& want, int viewW, int viewH, float dt, const Options& o) {
    const Uint64 t0 = SDL_GetTicksNS();
    drawCalls_ = 0;
    if (!ensureTargets(viewW, viewH)) return;
    bool snap = o.snapCamera;
    const bool designed = o.designedTerrain && ensureDesigned();
    const TerrainStyle style = designed && o.faithfulTerrain ? TerrainStyle::Faithful : TerrainStyle::Designed;
    if (world_ != &w || !terrainBuilt_ || terrain_.cellsW() != w.width() || terrain_.cellsH() != w.height() ||
        terrain_.designed() != designed || (designed && designed_->style() != style)) {
        world_ = &w;
        const std::vector<float> rgb = DesignedTerrain::tileMeans(atlas_);
        const Uint64 tb = SDL_GetTicksNS();
        // Faithful: the layout guide from the map's tiles first (the heightfield's relief).
        const TerrainGuide* guide = nullptr;
        if (designed) {
            designed_->setStyle(style);
            guide = designed_->prepareGuide(w, atlas_);
        }
        // The faithful terrain is lit from the south-west (like the original tiles and its 2D bake).
        baseSun_ = guide ? DesignedTerrain::faithfulSun3D() : Terrain::sunDirection();
        sun_ = baseSun_;
        terrain_.setSun(baseSun_);  // the baked visibility (shadow map off) keeps the style's own sun
        terrain_.build(w, rgb, designed, guide);
        if (designed) designed_->build(w, terrain_, rgb);
        terrainTop_ = 0;
        for (const TerrainVertex& tv : terrain_.vertices()) terrainTop_ = std::max(terrainTop_, tv.y);
        SDL_Log("3D: terrain (%s) %dx%d vertices in %.0f ms",
                !designed ? "original tiles" : guide ? "designed, faithful" : "designed", terrain_.gridW(), terrain_.gridH(),
                double(SDL_GetTicksNS() - tb) / 1e6);
        terrainBuilt_ = true;
        terrainDirty_ = true;
        tileMapCpu_.assign(std::size_t(w.width()) * w.height(), 0xFFFF);
        if (tileMap_) SDL_ReleaseGPUTexture(dev_, tileMap_);
        SDL_GPUTextureCreateInfo ci{};
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        // Tile indices as exact floats (0..65535): an integer format may not have SAMPLER usage
        // (SDL_gpu validation; D3D12/Vulkan), and the shaders only Load / read() it, never filter.
        ci.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ci.width = Uint32(w.width());
        ci.height = Uint32(w.height());
        ci.layer_count_or_depth = 1;
        ci.num_levels = 1;
        tileMap_ = SDL_CreateGPUTexture(dev_, &ci);
        snap = true;
    } else if (t0 - lastSpiceCheck_ > 400000000ull) {  // spice mounds: re-check ~2.5 times a second
        lastSpiceCheck_ = t0;
        if (terrain_.refreshSpice(w)) terrainDirty_ = true;
        if (designed) designed_->update(w);
    }
    // Buildings placed / removed: level the ground under them (Terrain::updateFoundations; every
    // frame, so a new building never stands on the old relief).
    if (terrain_.updateFoundations(w)) {
        terrainDirty_ = true;
        for (const TerrainVertex& tv : terrain_.vertices()) terrainTop_ = std::max(terrainTop_, tv.y);
    }
    // Tile indices (spice fields, concrete, rubble change tiles during play).
    const int tileCount = atlas_ ? atlas_->tileCount() : 0;
    for (int y = 0; y < w.height(); ++y)
        for (int x = 0; x < w.width(); ++x) {
            int tile = w.cell(x, y).tile;
            if (tile >= tileCount) tile = 0;
            std::uint16_t& old = tileMapCpu_[std::size_t(y) * w.width() + x];
            if (old != tile) { old = std::uint16_t(tile); tileMapDirty_ = true; }
        }

    // Camera: the look-at height is the mean terrain height around the centre (no bobbing over
    // single cliffs).
    float gsum = 0;
    for (int j = -2; j <= 2; ++j)
        for (int i = -2; i <= 2; ++i) gsum += terrain_.height(want.cx + i * 64.0f, want.cy + j * 64.0f);
    CameraPose pose = want;
    constrainPose(pose.yaw, pose.tilt);
    cam_.setShake(o.shakeX, o.shakeY);
    cam_.setViewport(viewW, viewH);
    cam_.update(pose, gsum / 25.0f, dt, snap);
    yawSteps_ = int(std::lround(cam_.pose().yaw / (2 * kPi) * 32)) & 31;

    // The time of day turns the sun (render/lighting.hpp) and tints the sky, the haze and the
    // billboards; everything lit reads the colours from the point light block's head.
    {
        sun_ = baseSun_;
        float tint[3] = {1, 1, 1};
        fogRgb_[0] = kFogR;
        fogRgb_[1] = kFogG;
        fogRgb_[2] = kFogB;
        lightsU_.clear();
        if (o.light && !o.light->identity) {
            const float base[3] = {baseSun_.x, baseSun_.y, baseSun_.z};
            float s[3];
            o.light->sunDirection(base, s);
            sun_ = normalize(Vec3{s[0], s[1], s[2]});
            o.light->grade(0.62f, 0.55f, sun_.y, tint);
            for (int i = 0; i < 3; ++i) fogRgb_[i] *= o.light->fog[i];
            lightsU_.setScene(o.light->sun, o.light->amb, tint, o.light->spec, o.light->sat);
        }
    }
    // Scene.
    newTextures_ = false;
    lamps_ = o.lamps;
    collect(w, o);
    SDL_GPUTexture* shroudTex = clearMask_;
    if (o.showShroud && shroud_) {
        if (SDL_Texture* m = shroud_->mask(w)) {
            if (seen_.insert(m).second) newTextures_ = true;
            if (SDL_GPUTexture* g = gpuTextureOf(m)) shroudTex = g;
        }
    }
    if (atlasDirty_ && atlas_ && seen_.insert(atlas_->texture()).second) newTextures_ = true;
    // Textures created this frame were uploaded into the renderer's command buffer: submit it
    // first so this frame samples them (render3d/gpu.hpp).
    if (newTextures_) submitRendererWork(r_);

    // Billboards back to front; decals keep their 2D order and go first.
    std::stable_partition(insts_.begin(), insts_.end(), [](const Inst& a) { return !a.billboard; });
    auto firstBb = std::find_if(insts_.begin(), insts_.end(), [](const Inst& a) { return a.billboard; });
    std::sort(firstBb, insts_.end(), [](const Inst& a, const Inst& b) {
        if (std::abs(a.depth - b.depth) > 0.01f) return a.depth > b.depth;
        return a.seq < b.seq;
    });

    // Vertices.
    const Vec3 E = cam_.eye();
    verts_.resize(insts_.size() * 4 * kSpriteFloats);
    float* v = verts_.data();
    for (const Inst& in : insts_) {
        for (int k = 0; k < 4; ++k) {
            const Vec3& c = in.c[k];
            const Vec3& an = in.billboard ? in.anchor : c;
            const float vals[kSpriteFloats] = {c.x, c.y, c.z, an.x, an.y, an.z,
                                               (k & 1) ? in.u1 : in.u0, (k & 2) ? in.v1 : in.v0,
                                               in.r, in.g, in.b, in.a,
                                               in.billboard ? 1.0f : 0.0f, in.flash, in.gx[k], in.gy[k],
                                               in.s[k].x, in.s[k].y, in.s[k].z};
            std::memcpy(v, vals, sizeof vals);
            v += kSpriteFloats;
        }
    }
    const std::uint32_t quads = std::uint32_t(insts_.size());
    if (quads > spriteCapacity_) {
        std::uint32_t cap = std::max(kInitialQuads, spriteCapacity_);
        while (cap < quads) cap *= 2;
        if (spriteVB_) SDL_ReleaseGPUBuffer(dev_, spriteVB_);
        if (quadIB_) SDL_ReleaseGPUBuffer(dev_, quadIB_);
        if (spriteTB_) SDL_ReleaseGPUTransferBuffer(dev_, spriteTB_);
        SDL_GPUBufferCreateInfo bi{};
        bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bi.size = cap * 4 * kSpriteFloats * 4;
        spriteVB_ = SDL_CreateGPUBuffer(dev_, &bi);
        bi.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        bi.size = cap * 6 * 4;
        quadIB_ = SDL_CreateGPUBuffer(dev_, &bi);
        SDL_GPUTransferBufferCreateInfo tbi{};
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbi.size = cap * 4 * kSpriteFloats * 4;
        spriteTB_ = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        spriteCapacity_ = cap;
        // Quad indices, uploaded once per growth (below).
    }

    if (atlasDirty_) buildTileArray();  // its own command buffers, submitted before this frame's
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev_);
    if (!cmd) return;
    if (designed) designed_->prepare(cmd);

    // Uploads: one copy pass with temporary transfer buffers for the rare ones.
    std::vector<SDL_GPUTransferBuffer*> temps;
    auto upload = [&](SDL_GPUCopyPass* cp, const void* data, Uint32 size, SDL_GPUBuffer* dst) {
        SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0};
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        if (!tb) return;
        void* p = SDL_MapGPUTransferBuffer(dev_, tb, false);
        std::memcpy(p, data, size);
        SDL_UnmapGPUTransferBuffer(dev_, tb);
        SDL_GPUTransferBufferLocation src{tb, 0};
        SDL_GPUBufferRegion reg{dst, 0, size};
        SDL_UploadToGPUBuffer(cp, &src, &reg, false);
        temps.push_back(tb);
    };
    auto uploadTex = [&](SDL_GPUCopyPass* cp, const void* data, Uint32 size, SDL_GPUTexture* dst, int w2, int h2) {
        SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0};
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tbi);
        if (!tb) return;
        void* p = SDL_MapGPUTransferBuffer(dev_, tb, false);
        std::memcpy(p, data, size);
        SDL_UnmapGPUTransferBuffer(dev_, tb);
        SDL_GPUTextureTransferInfo src{tb, 0, Uint32(w2), Uint32(h2)};
        SDL_GPUTextureRegion reg{};
        reg.texture = dst;
        reg.w = Uint32(w2);
        reg.h = Uint32(h2);
        reg.d = 1;
        SDL_UploadToGPUTexture(cp, &src, &reg, false);
        temps.push_back(tb);
    };
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    if (!smallTexturesUploaded_) {
        smallTexturesUploaded_ = true;
        const std::uint32_t white = 0xFFFFFFFFu, clear = 0;
        uploadTex(cp, &white, 4, white_, 1, 1);
        uploadTex(cp, &clear, 4, clearMask_, 1, 1);
        std::vector<std::uint32_t> ring(64 * 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const float dx = (x + 0.5f) / 32.0f - 1, dy = (y + 0.5f) / 32.0f - 1, r = std::sqrt(dx * dx + dy * dy);
                const float a = std::clamp(1 - std::abs(r - 0.86f) / 0.09f, 0.0f, 1.0f);
                ring[std::size_t(y) * 64 + x] = (std::uint32_t(a * 255) << 24) | 0x00FFFFFFu;
            }
        uploadTex(cp, ring.data(), 64 * 64 * 4, ring_, 64, 64);
    }
    if (terrainDirty_) {
        terrainDirty_ = false;
        const auto& tv = terrain_.vertices();
        const auto& ti = terrain_.indices();
        const Uint32 vbytes = Uint32(tv.size() * sizeof(TerrainVertex)), ibytes = Uint32(ti.size() * 4);
        if (!terrainVB_ || terrainIndexCount_ != ti.size()) {
            if (terrainVB_) SDL_ReleaseGPUBuffer(dev_, terrainVB_);
            if (terrainIB_) SDL_ReleaseGPUBuffer(dev_, terrainIB_);
            SDL_GPUBufferCreateInfo bi{};
            bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            bi.size = vbytes;
            terrainVB_ = SDL_CreateGPUBuffer(dev_, &bi);
            bi.usage = SDL_GPU_BUFFERUSAGE_INDEX;
            bi.size = ibytes;
            terrainIB_ = SDL_CreateGPUBuffer(dev_, &bi);
            if (terrainIB_) upload(cp, ti.data(), ibytes, terrainIB_);
            terrainIndexCount_ = std::uint32_t(ti.size());
        }
        if (terrainVB_) upload(cp, tv.data(), vbytes, terrainVB_);
    }
    if (tileMapDirty_ && tileMap_) {
        tileMapDirty_ = false;
        const std::vector<float> idx(tileMapCpu_.begin(), tileMapCpu_.end());
        uploadTex(cp, idx.data(), Uint32(idx.size() * 4), tileMap_, w.width(), w.height());
    }
    if (quads > 0 && spriteTB_ && spriteVB_) {
        // Quad index buffer: (re)filled whenever the buffers grew.
        if (quadIndexCap_ != spriteCapacity_ && quadIB_) {
            std::vector<std::uint32_t> idx(std::size_t(spriteCapacity_) * 6);
            for (std::uint32_t q = 0; q < spriteCapacity_; ++q) {
                const std::uint32_t b = q * 4;
                const std::uint32_t six[6] = {b, b + 1, b + 2, b + 2, b + 1, b + 3};
                std::memcpy(&idx[std::size_t(q) * 6], six, sizeof six);
            }
            upload(cp, idx.data(), Uint32(idx.size() * 4), quadIB_);
            quadIndexCap_ = spriteCapacity_;
        }
        const Uint32 bytes = quads * 4 * kSpriteFloats * 4;
        void* p = SDL_MapGPUTransferBuffer(dev_, spriteTB_, true);
        std::memcpy(p, verts_.data(), bytes);
        SDL_UnmapGPUTransferBuffer(dev_, spriteTB_);
        SDL_GPUTransferBufferLocation src{spriteTB_, 0};
        SDL_GPUBufferRegion reg{spriteVB_, 0, bytes};
        SDL_UploadToGPUBuffer(cp, &src, &reg, true);
    }
    SDL_EndGPUCopyPass(cp);
    for (auto* tb : temps) SDL_ReleaseGPUTransferBuffer(dev_, tb);

    // Render pass.
    SDL_GPUColorTargetInfo cti{};
    cti.texture = msaaTex_ ? msaaTex_ : sceneTex_;
    cti.clear_color = SDL_FColor{0, 0, 0, 1};  // beyond the map: black, like the 2D border
    cti.load_op = SDL_GPU_LOADOP_CLEAR;
    cti.store_op = msaaTex_ ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE;
    cti.resolve_texture = msaaTex_ ? sceneTex_ : nullptr;
    SDL_GPUDepthStencilTargetInfo dti{};
    dti.texture = depthTex_;
    dti.clear_depth = 1;
    dti.load_op = SDL_GPU_LOADOP_CLEAR;
    dti.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dti.clear_stencil = 0;
    dti.stencil_load_op = depthFormat_ == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT ||
                                  depthFormat_ == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT
                              ? SDL_GPU_LOADOP_CLEAR
                              : SDL_GPU_LOADOP_DONT_CARE;
    dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    const Mat4& vp = cam_.viewProj();
    const float fogStart = cam_.distance() * 1.8f, fogEnd = cam_.distance() * 6.0f;
    const float mapW = float(w.width() * 32), mapH = float(w.height() * 32);
    // Real 3D models (render3d/models.*): uploads now, meshes right after the terrain.
    ModelFrame mf;
    const bool drawModels = models_ && models_->active();
    if (drawModels) {
        mf.world = &w;
        mf.terrain = &terrain_;
        mf.camera = &cam_;
        mf.viewProj = vp;
        mf.eye = E;
        mf.sun = sun_;
        mf.fogColor[0] = fogRgb_[0]; mf.fogColor[1] = fogRgb_[1]; mf.fogColor[2] = fogRgb_[2];
        mf.fogStart = fogStart;
        mf.fogEnd = fogEnd;
        mf.shroud = shroudTex;
        mf.linearClamp = linearClamp_;
        mf.shroudOn = o.showShroud;
        mf.mapW = mapW;
        mf.mapH = mapH;
        mf.playerSide = o.playerSide;
        mf.colorFormat = colorFormat_;
        mf.depthFormat = depthFormat_;
        mf.samples = samples_;
        mf.aniso = quality_.aniso;
        // before prepare(): it collects the draws, and a building's emission strength needs these two
        mf.lamps = o.light && !o.light->identity ? o.light->lamps : 0.0f;
        mf.time = double(w.gameTicks()) / 20.0;
        models_->prepare(dev_, cmd, mf);
    }
    // Sun shadow map (docs/3d-mode.md "Sun shadows"): a depth pass from the sun over the terrain,
    // the rocks and the models; everything drawn afterwards samples it.
    const Vec3 sunDir = sun_;
    float shadowU[4] = {0, 1.0f / float(kShadowSize), kShadowKeep, 0};
    Mat4 lightVP;
    shadowMs_ = 0;
    if (o.shadowMap && terrainVB_ && terrainIB_) {
        const Uint64 ts = SDL_GetTicksNS();
        float range = 1;
        lightVP = fitLight(mapW, mapH, range);
        shadowU[0] = 1;
        shadowU[3] = 1.0f / range;  // 1 world px of extra depth bias
        mf.lightViewProj = lightVP;
        mf.shadowFormat = shadowFmt_;
        SDL_GPUDepthStencilTargetInfo sti{};
        sti.texture = shadowTex_;
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
            SDL_GPUBufferBinding vb{terrainVB_, 0};
            SDL_BindGPUVertexBuffers(sp, 0, &vb, 1);
            SDL_GPUBufferBinding ib{terrainIB_, 0};
            SDL_BindGPUIndexBuffer(sp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_DrawGPUIndexedPrimitives(sp, terrainIndexCount_, 1, 0, 0, 0);
            ++drawCalls_;
            if (designed) drawCalls_ += designed_->drawShadow(sp, cmd, lightVP, shadowFmt_);
            if (drawModels) drawCalls_ += models_->drawShadowDepth(sp, cmd, mf);
            SDL_EndGPURenderPass(sp);
        }
        shadowMs_ = double(SDL_GetTicksNS() - ts) / 1e6;
    }
    mf.shadowMap = shadowTex_;
    mf.shadowSampler = shadowSampler_;
    std::memcpy(mf.shadow, shadowU, sizeof shadowU);
    const bool shadowOn = shadowU[0] > 0.5f;
    buildPointLights(o, viewW, viewH);
    mf.lights = &lightsU_;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &cti, 1, &dti);
    if (pass) {
        // Dynamic point lights: fragment uniform buffer 1 of terrain_fs / sprite_fs (the designed
        // terrain and the models push theirs from the same block).
        SDL_PushGPUFragmentUniformData(cmd, 1, &lightsU_, sizeof lightsU_);
        if (designed) {
            DesignedTerrain::Frame df;
            df.viewProj = vp;
            df.eye = E;
            df.sun = sun_;
            df.fogColor[0] = fogRgb_[0]; df.fogColor[1] = fogRgb_[1]; df.fogColor[2] = fogRgb_[2];
            df.fogStart = fogStart;
            df.fogEnd = fogEnd;
            df.mapW = mapW;
            df.mapH = mapH;
            df.shroudOn = o.showShroud;
            df.shroud = shroudTex;
            df.linearClamp = linearClamp_;
            df.lightVP = lightVP;
            df.shadowMap = shadowTex_;
            df.shadowSampler = shadowSampler_;
            std::memcpy(df.shadow, shadowU, sizeof shadowU);
            if (shadowOn) df.darkest = kShadowKeep;
            df.lights = &lightsU_;
            drawCalls_ += designed_->draw(pass, cmd, terrainVB_, terrainIB_, terrainIndexCount_, df);
        } else if (terrainVB_ && terrainIB_ && tileArray_ && tileMap_) {
            SDL_BindGPUGraphicsPipeline(pass, terrainPipe_);
            TerrainVU tvu;
            std::memcpy(tvu.viewProj, vp.m, sizeof tvu.viewProj);
            SDL_PushGPUVertexUniformData(cmd, 0, &tvu, sizeof tvu);
            const Vec3 sun = sun_;
            const float amb = 0.62f, dif = 0.55f;
            TerrainFU tfu{{sun.x, sun.y, sun.z, 0},
                          {amb, dif, 1.0f / (amb + dif * sun.y), shadowOn ? kShadowKeep : 0.42f},
                          {fogRgb_[0], fogRgb_[1], fogRgb_[2], 1},
                          {E.x, E.y, E.z, fogStart},
                          {fogEnd, mapW, mapH, o.showShroud ? 1.0f : 0.0f},
                          {},
                          {shadowU[0], shadowU[1], shadowU[2], shadowU[3]}};
            std::memcpy(tfu.lightVP, lightVP.m, sizeof tfu.lightVP);
            SDL_PushGPUFragmentUniformData(cmd, 0, &tfu, sizeof tfu);
            SDL_GPUBufferBinding vb{terrainVB_, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            SDL_GPUBufferBinding ib{terrainIB_, 0};
            SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_GPUTextureSamplerBinding sb[4] = {{tileArray_, tileSampler_}, {tileMap_, pointClamp_}, {shroudTex, linearClamp_},
                                                  {shadowTex_, shadowSampler_}};
            SDL_BindGPUFragmentSamplers(pass, 0, sb, 4);
            SDL_DrawGPUIndexedPrimitives(pass, terrainIndexCount_, 1, 0, 0, 0);
            ++drawCalls_;
        }
        if (drawModels) drawCalls_ += models_->draw(pass, cmd, mf);
        if (quads > 0 && spriteVB_ && quadIB_) {
            SpriteVU svu;
            std::memcpy(svu.viewProj, vp.m, sizeof svu.viewProj);
            svu.eye[0] = E.x; svu.eye[1] = E.y; svu.eye[2] = E.z; svu.eye[3] = fogStart;
            svu.fog[0] = fogEnd; svu.fog[1] = svu.fog[2] = svu.fog[3] = 0;
            std::memcpy(svu.lightVP, lightVP.m, sizeof svu.lightVP);
            // Sprites in full shadow: as bright as flat ground in full shadow relative to sunlit ground.
            const float amb = 0.62f, dif = 0.55f;
            const float spriteFloor = (amb + dif * sunDir.y * kShadowKeep) / (amb + dif * sunDir.y);
            SDL_PushGPUVertexUniformData(cmd, 0, &svu, sizeof svu);
            SDL_GPUBufferBinding vb{spriteVB_, 0};
            SDL_GPUBufferBinding ib{quadIB_, 0};
            int curPipe = -1;
            for (std::uint32_t i = 0; i < quads;) {
                const Inst& in = insts_[i];
                std::uint32_t j = i + 1;
                while (j < quads && insts_[j].tex == in.tex && insts_[j].pipe == in.pipe) ++j;
                if (in.pipe != curPipe) {
                    curPipe = in.pipe;
                    SDL_BindGPUGraphicsPipeline(pass, spritePipe_[curPipe]);
                    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
                    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    const float mode = curPipe == kAlpha ? 0.0f : curPipe == kMul ? 2.0f : 1.0f;  // kLight: as additive
                    SpriteFU sfu{{mode, o.showShroud ? 1.0f : 0.0f, mapW, mapH}, {fogRgb_[0], fogRgb_[1], fogRgb_[2], 1},
                                 {shadowU[0], shadowU[1], spriteFloor, shadowU[3]}};
                    SDL_PushGPUFragmentUniformData(cmd, 0, &sfu, sizeof sfu);
                }
                SDL_GPUTextureSamplerBinding sb[3] = {{in.tex, linearClamp_}, {shroudTex, linearClamp_},
                                                      {shadowTex_, shadowSampler_}};
                SDL_BindGPUFragmentSamplers(pass, 0, sb, 3);
                SDL_DrawGPUIndexedPrimitives(pass, (j - i) * 6, 1, i * 6, 0, 0);
                ++drawCalls_;
                i = j;
            }
        }
        SDL_EndGPURenderPass(pass);
    }
    // D2K_GPU_TIMING=1 (measurement only: stalls the CPU every frame): the 3D pass's GPU time,
    // submit -> fence (SDL_gpu has no timestamp queries), averaged and logged every 120 frames.
    static const bool gpuTiming = [] {
        const char* e = std::getenv("D2K_GPU_TIMING");
        return e && e[0] == '1';
    }();
    if (gpuTiming) {
        const Uint64 s0 = SDL_GetTicksNS();
        if (SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd)) {
            SDL_WaitForGPUFences(dev_, true, &fence, 1);
            SDL_ReleaseGPUFence(dev_, fence);
        }
        static double sum = 0, worst = 0;
        static int n = 0;
        const double ms = double(SDL_GetTicksNS() - s0) / 1e6;
        sum += ms;
        worst = std::max(worst, ms);
        if (++n == 120) {
            SDL_Log("3D GPU pass: avg %.2f ms, max %.2f ms (120 frames, submit -> fence); CPU build %.2f ms", sum / n, worst,
                    double(s0 - t0) / 1e6);
            sum = worst = 0;
            n = 0;
        }
    } else {
        SDL_SubmitGPUCommandBuffer(cmd);
    }

    const SDL_FRect dst{0, 0, float(viewW), float(viewH)};
    const bool bloomOn = o.bloom && o.light && o.light->bloom > 0.001f;
    if (hdrScene() && o.hdr) {
        // HDR scene: bloom from the unclamped values, both through the tone map (render/hdr.*);
        // without the Modern lighting just clamped (the picture from before).
        SDL_Texture* glow = bloomOn ? o.bloom->compute(sceneSdl_, viewW, viewH, o.light->bloomThreshold) : nullptr;
        if (!o.hdr->compose(sceneSdl_, viewW, viewH, glow, bloomOn ? o.light->bloom : 0.0f, dst, o.light != nullptr))
            SDL_RenderTexture(r_, sceneSdl_, nullptr, &dst);
        drawPostRefraction(viewW, viewH);
    } else {
        SDL_RenderTexture(r_, sceneSdl_, nullptr, &dst);
        drawPostRefraction(viewW, viewH);
        // Modern lighting: bloom around the brightest parts of the picture (render/bloom.*).
        if (bloomOn) o.bloom->apply(sceneSdl_, viewW, viewH, dst, o.light->bloom, o.light->bloomThreshold);
    }
    drawSunGlare(viewW, viewH, o);
    cpuMs_ = double(SDL_GetTicksNS() - t0) / 1e6;
}

namespace {
// The glare's sprites (white, alpha; additive): kind 0 a soft gaussian glow, 1 a flare ghost (a faint
// disc with a soft brighter rim).
SDL_Texture* makeGlareTexture(SDL_Renderer* r, int kind) {
    constexpr int N = 128;
    std::vector<std::uint32_t> px(std::size_t(N) * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const float dx = (float(x) + 0.5f) / (N * 0.5f) - 1, dy = (float(y) + 0.5f) / (N * 0.5f) - 1;
            const float d = std::sqrt(dx * dx + dy * dy);
            float a = 0;
            if (kind == 0) a = std::exp(-(d / 0.42f) * (d / 0.42f)) * std::clamp((1.0f - d) * 3.0f, 0.0f, 1.0f);
            else a = (0.3f + 0.7f * std::exp(-((d - 0.82f) / 0.1f) * ((d - 0.82f) / 0.1f))) * std::clamp((1.0f - d) * 8.0f, 0.0f, 1.0f);
            const std::uint32_t A = std::uint32_t(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
            px[std::size_t(y) * N + std::size_t(x)] = (A << 24) | 0xFFFFFFu;  // ABGR8888: white, alpha
        }
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, N, N);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, px.data(), N * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}
}  // namespace

// The sun never stands in the view (the camera looks down at least 24 degrees below the horizon, the
// sun at least 20 above it), so the glare is a lens effect from just outside the frame: strength =
// how far the view's horizontal direction turns towards the sun's azimuth x how low the camera looks
// (tilt) x how low the sun stands (only the low suns of dawn / dusk: 0 at Day and at Night, whose
// elevation factors are >= 1) x a little more with bloom / HDR. Colour = the sun's colour.
float Renderer3D::drawSunGlare(int viewW, int viewH, const Options& o) {
    if (!o.light || !o.light->modern || o.light->identity || viewW <= 0 || viewH <= 0) return 0;
    auto smooth = [](float e0, float e1, float x) {
        const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    };
    const float low = smooth(0.9f, 0.66f, o.light->elK);
    if (low <= 0) return 0;
    const Vec3 f = cam_.forward();
    const float fh = std::hypot(f.x, f.z), sh = std::hypot(sun_.x, sun_.z);
    if (fh < 1e-4f || sh < 1e-4f) return 0;
    const float facing = std::max(0.0f, (f.x * sun_.x + f.z * sun_.z) / (fh * sh));
    const float tilt = smooth(0.25f, 1.05f, cam_.pose().tilt);
    const float hdrK = hdrScene() && o.hdr ? 1.15f : 1.0f;
    const float k = std::pow(facing, 1.6f) * tilt * low * (0.75f + 0.25f * std::min(1.0f, o.light->bloom / 0.5f)) * hdrK;
    if (k < 0.01f) return 0;
    if (!glareGlowTex_) glareGlowTex_ = makeGlareTexture(r_, 0);
    if (!glareGhostTex_) glareGhostTex_ = makeGlareTexture(r_, 1);
    if (!glareGlowTex_ || !glareGhostTex_) return 0;
    // The sun's direction on the screen (x right, y down) from the view centre, and where that ray
    // leaves the frame.
    float dx = dot(sun_, cam_.right()), dy = -dot(sun_, cam_.up());
    const float dl = std::hypot(dx, dy);
    if (dl < 1e-4f) return 0;
    dx /= dl;
    dy /= dl;
    const float cx = viewW * 0.5f, cy = viewH * 0.5f;
    const float tEdge = std::min(std::fabs(dx) > 1e-4f ? cx / std::fabs(dx) : 1e9f, std::fabs(dy) > 1e-4f ? cy / std::fabs(dy) : 1e9f);
    const float ex = cx + dx * tEdge, ey = cy + dy * tEdge;
    const float m = float(std::min(viewW, viewH));
    // Sun colour, normalised to its brightest channel (dawn: peach, dusk: red-gold).
    float c[3] = {o.light->sun[0], o.light->sun[1], o.light->sun[2]};
    const float cm = std::max({c[0], c[1], c[2], 1e-3f});
    for (float& v : c) v /= cm;
    auto spot = [&](SDL_Texture* t, float x, float y, float size, float r, float g, float b, float a) {
        const SDL_FRect d{x - size * 0.5f, y - size * 0.5f, size, size};
        SDL_SetTextureColorModFloat(t, r, g, b);
        SDL_SetTextureAlphaModFloat(t, std::clamp(a, 0.0f, 1.0f));
        SDL_RenderTexture(r_, t, nullptr, &d);
    };
    // The glow: a wide faint wash and a warmer core just beyond the edge.
    spot(glareGlowTex_, ex, ey, 2.4f * m, c[0], c[1] * 0.95f, c[2] * 0.85f, 0.2f * k);
    spot(glareGlowTex_, ex + dx * 0.08f * m, ey + dy * 0.08f * m, 1.1f * m, 1.0f, 0.5f + 0.45f * c[1], 0.3f + 0.5f * c[2], 0.28f * k);
    // Ghosts on the axis from the (off-screen) sun through the centre: position along it (0 = the
    // edge point, 1 = the centre, 2 = the mirrored edge), size (x the short side), tint, alpha.
    struct Ghost { float at, size, r, g, b, a; };
    static constexpr Ghost kGhosts[] = {{0.55f, 0.07f, 1.0f, 0.85f, 0.6f, 0.10f}, {1.25f, 0.05f, 0.7f, 1.0f, 0.8f, 0.08f},
                                        {1.5f, 0.13f, 0.6f, 0.75f, 1.0f, 0.06f},  {1.85f, 0.2f, 1.0f, 0.7f, 0.5f, 0.045f},
                                        {2.15f, 0.09f, 0.8f, 0.6f, 1.0f, 0.06f}};
    for (const Ghost& g : kGhosts) {
        const float x = ex + (cx - ex) * g.at, y = ey + (cy - ey) * g.at;
        spot(glareGhostTex_, x, y, g.size * m, g.r * c[0], g.g * (0.5f + 0.5f * c[1]), g.b * (0.4f + 0.6f * c[2]), g.a * k);
    }
    return k;
}

// Screen-space refraction from the scene texture (the GPU picture is already a texture, so no
// read-back is needed): the Classic sonic lens - Display__InitLensTable 0x42ace0: inside the
// radius a pixel at offset d from the centre samples centre + d * 2/3, outside itself; a fan is
// exact for that linear map (nearest sampling keeps the pixels crisp) - and the Modern beams'
// heat haze (Effects::buildHaze; the beam is in the picture, so its own line is bent less).
void Renderer3D::drawPostRefraction(int viewW, int viewH) {
    if (!sceneSdl_ || tw_ <= 0 || th_ <= 0) return;
    const float iw = 1.0f / float(tw_), ih = 1.0f / float(th_);
    if (fx_ && fx_->hasHaze()) {
        fx_->buildHaze(
            postV_, postI_,
            [&](float wx, float wy, float& sx, float& sy) { return worldToScreen(wx, wy, 2, sx, sy); }, float(tw_),
            float(th_), true);
        if (!postI_.empty()) {
            SDL_SetTextureScaleMode(sceneSdl_, SDL_SCALEMODE_LINEAR);
            SDL_RenderGeometry(r_, sceneSdl_, postV_.data(), int(postV_.size()), postI_.data(), int(postI_.size()));
            SDL_SetTextureScaleMode(sceneSdl_, SDL_SCALEMODE_NEAREST);
        }
    }
    if (lenses_.empty()) return;
    postV_.clear();
    postI_.clear();
    constexpr int kSeg = 32;
    const SDL_FColor white{1, 1, 1, 1};
    for (const Lens& l : lenses_) {
        if (l.sx < -l.r || l.sy < -l.r || l.sx > viewW + l.r || l.sy > viewH + l.r) continue;
        const int c = int(postV_.size());
        postV_.push_back({{l.sx, l.sy}, white, {l.sx * iw, l.sy * ih}});
        for (int i = 0; i <= kSeg; ++i) {
            const float a = 6.2831853f * float(i) / kSeg, dx = std::cos(a) * l.r, dy = std::sin(a) * l.r;
            postV_.push_back({{l.sx + dx, l.sy + dy}, white, {(l.sx + dx * (2.0f / 3.0f)) * iw, (l.sy + dy * (2.0f / 3.0f)) * ih}});
            if (i > 0) postI_.insert(postI_.end(), {c, c + i, c + i + 1});
        }
    }
    if (!postI_.empty()) SDL_RenderGeometry(r_, sceneSdl_, postV_.data(), int(postV_.size()), postI_.data(), int(postI_.size()));
}

// ---------------------------------------------------------------------------------------------
// Picking and overlays.

bool Renderer3D::screenToGround(float sx, float sy, float& wx, float& wy) const {
    if (!terrainBuilt_) return false;
    Vec3 o, d, hit;
    cam_.ray(sx, sy, o, d);
    if (!terrain_.raycast(o, d, hit)) return false;
    wx = hit.x;
    wy = hit.z;
    return true;
}

bool Renderer3D::pick(const game::World& w, float sx, float sy, float& wx, float& wy) const {
    if (!screenToGround(sx, sy, wx, wy)) return false;
    // A unit drawn under the cursor wins over the terrain point behind it.
    float best = 1e9f;
    for (const auto& side : w.sides())
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const auto& u = side.units[i];
            if (!u.alive || (u.flags & 0x600000)) continue;
            const bool inf = t_.bin.units[u.type].isInfantry;
            const float gx = float(game::fixToPixel(u.posX)), gy = float(game::fixToPixel(u.posY));
            const float z = float(game::fixToPixel(u.posZ) & 0xff);
            const Vec3 p{gx, terrain_.height(gx, gy) + z, gy};
            float px = 0, py = 0;
            if (!cam_.project(p, px, py)) continue;
            const float r = (inf ? 8.0f : 14.0f) * cam_.pixelsPerUnit(cam_.depthOf(p));
            const float d = std::hypot(px - sx, py - sy);
            if (d < r && d < best) {
                best = d;
                wx = gx;
                wy = gy;
            }
        }
    return true;
}

bool Renderer3D::worldToScreen(float wx, float wy, float above, float& sx, float& sy) const {
    return cam_.project(Vec3{wx, terrain_.height(wx, wy) + above, wy}, sx, sy);
}

void Renderer3D::fillGroundRect(float x0, float y0, float x1, float y1, SDL_FColor fill, SDL_FColor outline) const {
    SDL_Vertex v[4];
    const float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
    SDL_FPoint pts[5];
    for (int k = 0; k < 4; ++k) {
        float sx = 0, sy = 0;
        if (!worldToScreen(xs[k], ys[k], 1.0f, sx, sy)) return;
        v[k] = SDL_Vertex{{sx, sy}, fill, {0, 0}};
        pts[k] = {sx, sy};
    }
    pts[4] = pts[0];
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r_, nullptr, v, 4, idx, 6);
    SDL_SetRenderDrawColorFloat(r_, outline.r, outline.g, outline.b, outline.a);
    SDL_RenderLines(r_, pts, 5);
}

void Renderer3D::drawOverlays(const game::World& w, const Options& o) {
    if (!terrainBuilt_) return;
    frameWorld_ = &w;
    frameShroud_ = o.showShroud;
    auto bar = [&](float cx, float y, float halfW, float h, float frac) {
        const SDL_FRect back{std::round(cx - halfW), std::round(y), std::round(2 * halfW), std::round(h)};
        SDL_SetRenderDrawColor(r_, 0, 0, 0, 180);
        SDL_RenderFillRect(r_, &back);
        if (frac > 0.5f) SDL_SetRenderDrawColor(r_, 40, 220, 60, 255);
        else if (frac > 0.25f) SDL_SetRenderDrawColor(r_, 240, 200, 40, 255);
        else SDL_SetRenderDrawColor(r_, 230, 40, 30, 255);
        const SDL_FRect fill{back.x, back.y, std::round(back.w * frac), back.h};
        SDL_RenderFillRect(r_, &fill);
    };
    for (const auto& side : w.sides()) {
        // Buildings: as 2D Enhanced (MapRenderer::drawHealthBars): selected ones always, others
        // per the setting (Damaged / Always); own selected refinery / silo: the storage pips.
        for (int i = side.firstBuilding; i >= 0; i = side.nextObject(i)) {
            const auto& b = side.buildings[i];
            if (!b.alive || b.state == 17 || (b.flags & 0x100000)) continue;
            const auto& t = t_.bin.buildings[b.type];
            if (t.specialBehavior == game::building_behaviour::kConcrete || t.hitPoints == 0) continue;
            if (buildingHidden(b)) continue;
            const float frac = std::clamp(float(b.health) / float(t.hitPoints), 0.f, 1.f);
            if (!b.selected && (o.healthBars == 0 || (o.healthBars == 1 && frac >= 1.0f))) continue;
            const int fw = std::max(1, int(t.artWidth) / 32), fh = std::max(1, int(t.artHeight) / 32);
            const float top = terrain_.maxHeight(b.tileX, b.tileY, b.tileX + fw - 1, b.tileY + fh - 1);
            // Billboard: the top edge of the art box. Model: above the model's top over the middle
            // of its occupied cells (stays on the building whichever way the camera turns).
            Vec3 c{b.tileX * 32.0f + fw * 16.0f, top + 4, b.tileY * 32.0f};
            const float mh = models_ && models_->active() ? models_->buildingModelHeight(b.type) : -1.0f;
            if (mh >= 0) {
                const std::uint32_t m = t.tilesOccupiedAll ? t.tilesOccupiedAll : t.tilesOccupiedSolid;
                int n = 0, sx = 0, sy = 0;
                for (int k = 0; k < 16; ++k)
                    if ((m >> k) & 1) ++n, sx += k % 4, sy += k / 4;
                c = Vec3{b.tileX * 32.0f + (n ? (float(sx) / float(n) + 0.5f) * 32 : fw * 16.0f), top + mh + 6,
                         b.tileY * 32.0f + (n ? (float(sy) / float(n) + 0.5f) * 32 : fh * 16.0f)};
            }
            float sx = 0, sy = 0;
            if (!cam_.project(c, sx, sy)) continue;
            const float ppw = cam_.pixelsPerUnit(cam_.depthOf(c));
            const float z = std::round(std::clamp(std::sqrt(ppw), 1.0f, 2.0f) * 2) / 2;
            const float halfW = std::max(10.0f, (fw * 16.0f - 3) * ppw), barH = std::round(3 * z);
            const float barY = sy - 4 * z - (mh >= 0 ? barH : 0);
            bar(sx, barY, halfW, barH, frac);
            if (b.selected && b.side == o.playerSide && side.maxStorage > 0 &&
                (t.specialBehavior == game::building_behaviour::kRefinery || t.specialBehavior == game::building_behaviour::kSilo)) {
                // The original's selected-building pips: N - 1 slots, filled = min(spice * N / capacity, N - 1).
                const int n = t.specialBehavior == game::building_behaviour::kRefinery ? 16 : 8;
                const int filled = int(std::min<std::int64_t>(std::int64_t(side.spice) * n / side.maxStorage, n - 1));
                const float gap = std::max(1.0f, std::round(z)), segW = (2 * std::round(halfW) - float(n - 2) * gap) / float(n - 1);
                for (int k = 0; k < n - 1; ++k) {
                    const SDL_FRect seg{std::round(sx - halfW + float(k) * (segW + gap)), std::round(barY + barH + 2 * z),
                                        std::max(1.0f, std::round(segW)), barH};
                    if (k < filled) SDL_SetRenderDrawColor(r_, 240, 140, 30, 255);
                    else SDL_SetRenderDrawColor(r_, 0, 0, 0, 180);
                    SDL_RenderFillRect(r_, &seg);
                }
            }
            // PRIMARY (the original's "1" marker): a small "1" left of the bar.
            if (b.selected && (b.flags & 0x80) && b.side == o.playerSide)
                if (const render::Sprite* sp = sprites_.get(kFrameDigitOne, -1)) {
                    const float sc = std::max(1.0f, std::round(z));
                    const SDL_FRect d{std::round(sx - halfW - sp->width * sc - 2 * z), std::round(barY + barH / 2 - sp->height * sc / 2),
                                      sp->width * sc, sp->height * sc};
                    SDL_RenderTexture(r_, sp->texture, nullptr, &d);
                }
        }
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const auto& u = side.units[i];
            if (!u.alive || (u.flags & 0x1200100) || u.state == game::UnitState::Dead) continue;
            if (!u.selected && o.healthBars != 2) continue;
            if ((u.flags & 0x4) && u.side != o.playerSide) continue;
            if (pointHidden(float(game::fixToPixel(u.posX)), float(game::fixToPixel(u.posY)))) continue;
            const auto& t = t_.bin.units[u.type];
            if (t.hitPoints == 0) continue;
            const float gx = float(game::fixToPixel(u.posX)), gy = float(game::fixToPixel(u.posY));
            const float h = t.isInfantry ? 9.0f : 14.0f;
            const Vec3 base{gx, terrain_.height(gx, gy) + float(game::fixToPixel(u.posZ) & 0xff), gy};
            float sx = 0, sy = 0;
            if (!cam_.project(base, sx, sy)) continue;
            const float ppw = cam_.pixelsPerUnit(cam_.depthOf(base));
            const float z = std::round(std::clamp(std::sqrt(ppw), 1.0f, 2.0f) * 2) / 2;
            // Sprite height (h + 6), or above a 3D model's top when that is higher on screen.
            float lift = (h + 6) * ppw;
            const float mh = models_ && models_->active() ? models_->unitModelHeight(u.type) : -1.0f;
            float tx = 0, ty = 0;
            if (mh >= 0 && cam_.project(Vec3{base.x, base.y + mh + 3, base.z}, tx, ty)) lift = std::max(lift, sy - ty + 2 * z);
            bar(sx, sy - lift - 3 * z, h * ppw, std::round(3 * z),
                std::clamp(float(u.health) / float(t.hitPoints), 0.f, 1.f));
            if (u.selected && t.specialBehavior == 1 && u.side == o.playerSide) {  // harvester load
                const int load = std::min<int>(u.byte29, 7);
                const float gap = std::round(z), segW = (2 * h * ppw - 6 * gap) / 7.f;
                for (int k = 0; k < 7; ++k) {
                    const SDL_FRect seg{std::round(sx - h * ppw + k * (segW + gap)), std::round(sy - lift + 1 * z),
                                        std::round(segW), std::round(2 * z)};
                    if (k < load) SDL_SetRenderDrawColor(r_, 240, 140, 30, 255);
                    else SDL_SetRenderDrawColor(r_, 0, 0, 0, 180);
                    SDL_RenderFillRect(r_, &seg);
                }
            }
        }
    }
}

}  // namespace d2k::render3d

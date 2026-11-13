#include "render/hdr.hpp"

#include "render3d/gpu.hpp"
#include "render3d/shader_blobs.hpp"

#include <algorithm>
#include <cstdlib>

namespace d2k::render {

namespace {
bool gExtended = false;
float gHeadroom = 1.0f;
SDL_Texture* gScreen = nullptr;

// Uniform block of tonemap_fs (16-byte rows).
struct TmUniforms {
    float exposure, peak, knee, bloomStrength;
    float bloomU, bloomV, inLinear, outLinear;
    float desaturate, pad0, pad1, pad2;
};
}  // namespace

void setHdrOutput(bool extended, float headroom) {
    gExtended = extended;
    gHeadroom = std::max(1.0f, headroom);
}
bool hdrOutputExtended() { return gExtended; }
float hdrOutputHeadroom() { return gExtended ? gHeadroom : 1.0f; }
void setScreenTarget(SDL_Texture* t) { gScreen = t; }
SDL_Texture* screenTarget() { return gScreen; }

Hdr::Hdr(SDL_Renderer* r) : r_(r) {
    if (const char* e = std::getenv("D2K_EXPOSURE")) exposure_ = std::clamp(float(std::atof(e)), 0.1f, 8.0f);
}

Hdr::~Hdr() {
    if (state_) SDL_DestroyGPURenderState(state_);
    if (presentState_) SDL_DestroyGPURenderState(presentState_);
    if (black_) SDL_DestroyTexture(black_);
    if (dev_) {
        if (shader_) SDL_ReleaseGPUShader(dev_, shader_);
        if (sampler_) SDL_ReleaseGPUSampler(dev_, sampler_);
    }
}

bool Hdr::available() {
    if (!tried_) {
        tried_ = true;
        ok_ = init();
        if (!ok_) SDL_Log("HDR rendering: unavailable on this renderer (%s) - 8-bit lighting", SDL_GetError());
    }
    return ok_;
}

bool Hdr::init() {
    dev_ = render3d::gpuDeviceOf(r_);
    if (!dev_ || !render3d::deviceHasShaders(dev_)) return false;
    shader_ = render3d::createShader(dev_, render3d::ShaderId::TonemapFs, 2, 1);
    if (!shader_) return false;
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SDL_CreateGPUSampler(dev_, &si);
    black_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
    if (!sampler_ || !black_) return false;
    const Uint32 px = 0xFF000000u;
    SDL_UpdateTexture(black_, nullptr, &px, 4);
    return ensureState(nullptr);
}

bool Hdr::ensureState(SDL_Texture* bloom) {
    SDL_GPUTexture* t = render3d::gpuTextureOf(bloom ? bloom : black_);
    if (!t) return false;
    if (state_ && stateBloom_ == t) return true;
    if (state_) SDL_DestroyGPURenderState(state_);
    state_ = nullptr;
    SDL_GPUTextureSamplerBinding b{t, sampler_};
    SDL_GPURenderStateCreateInfo ci{};
    ci.fragment_shader = shader_;
    ci.num_sampler_bindings = 1;
    ci.sampler_bindings = &b;
    state_ = SDL_CreateGPURenderState(r_, &ci);
    stateBloom_ = state_ ? t : nullptr;
    return state_ != nullptr;
}

SDL_Texture* Hdr::createTarget(SDL_Renderer* r, int w, int h) {
    const SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA64_FLOAT);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB);
    SDL_Texture* t = SDL_CreateTextureWithProperties(r, p);
    SDL_DestroyProperties(p);
    return t;
}

bool Hdr::compose(SDL_Texture* scene, int srcW, int srcH, SDL_Texture* bloom, float bloomStrength, const SDL_FRect& dst,
                  bool tonemap) {
    if (!scene || srcW <= 0 || srcH <= 0 || !available()) return false;
    if (!ensureState(bloom) && !(bloom && ensureState(nullptr))) return false;
    if (stateBloom_ != render3d::gpuTextureOf(bloom ? bloom : black_)) bloom = nullptr;  // fell back to black
    float tw = 0, th = 0;
    SDL_GetTextureSize(scene, &tw, &th);
    TmUniforms u{};
    u.exposure = tonemap ? exposure_ : 1.0f;
    // SDR: the shoulder starts at 80 % linear (sRGB ~0.91: sunlit sand stays as it was) and ends at
    // white. EDR: it ends at the headroom (at most 4x SDR white: brighter is uncomfortable).
    u.peak = gExtended && tonemap ? std::clamp(gHeadroom, 1.0f, 4.0f) : 1.0f;
    u.knee = tonemap ? 0.8f : u.peak;
    u.bloomStrength = bloom ? std::max(0.0f, bloomStrength) : 0.0f;
    u.bloomU = tw / float(srcW);
    u.bloomV = th / float(srcH);
    u.inLinear = 0.0f;   // the scene holds sRGB-coded values (the renderer is an sRGB one)
    u.outLinear = 0.0f;  // sRGB-coded (EDR: extended above 1; presentCanvas decodes it)
    u.desaturate = 0.5f;
    SDL_SetGPURenderStateFragmentUniforms(state_, 0, &u, sizeof u);
    SDL_BlendMode blend = SDL_BLENDMODE_NONE;
    SDL_GetTextureBlendMode(scene, &blend);
    SDL_SetTextureBlendMode(scene, SDL_BLENDMODE_NONE);
    SDL_SetGPURenderState(r_, state_);
    const SDL_FRect src{0, 0, float(srcW), float(srcH)};
    SDL_RenderTexture(r_, scene, &src, &dst);
    SDL_SetGPURenderState(r_, nullptr);
    SDL_SetTextureBlendMode(scene, blend);
    return true;
}

bool Hdr::presentCanvas(SDL_Texture* canvas, const SDL_FRect& dst) {
    if (!canvas || !available()) return false;
    if (!presentState_) {
        SDL_GPUTextureSamplerBinding b{render3d::gpuTextureOf(black_), sampler_};
        SDL_GPURenderStateCreateInfo ci{};
        ci.fragment_shader = shader_;
        ci.num_sampler_bindings = 1;
        ci.sampler_bindings = &b;
        presentState_ = SDL_CreateGPURenderState(r_, &ci);
        if (!presentState_) return false;
        TmUniforms u{};
        u.exposure = 1;
        u.peak = u.knee = 1e4f;  // no shoulder, no clamp: a plain sRGB -> linear decode
        u.bloomU = u.bloomV = 1;
        u.outLinear = 1;
        SDL_SetGPURenderStateFragmentUniforms(presentState_, 0, &u, sizeof u);
    }
    SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(canvas, SDL_SCALEMODE_NEAREST);
    SDL_SetGPURenderState(r_, presentState_);
    SDL_RenderTexture(r_, canvas, nullptr, &dst);
    SDL_SetGPURenderState(r_, nullptr);
    return true;
}

}  // namespace d2k::render

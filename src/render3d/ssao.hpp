#pragma once
// Screen-space ambient occlusion of the 3D view (docs/3d-mode.md "Ambient occlusion"; Settings::ambientOcclusion,
// Reimagined only): the soft darkening where a building meets the ground, between a building's parts, under a
// tank's hull, in a cliff's crevices and between rocks.
//
// Per frame, before the scene pass: the renderer draws a half-size depth of the scene (terrain, rocks, models
// through their depth-only pipelines from the camera) into depth(); compute() turns it into the share of the sky
// each pixel sees (ssao_fs) and blurs that depth-aware (ssaoblur_fs) into result(). The lit shaders (terrain3_fs,
// rock_fs, model_fs, model_pbr_fs) sample result() at their own pixel and scale their AMBIENT term by it - the
// sun's light is untouched (lights_msl.hpp ssaoAt, PointLightsU::ssao).

#include <SDL3/SDL.h>

namespace d2k::render3d {

class Ssao {
public:
    explicit Ssao(SDL_GPUDevice* dev) : dev_(dev) {}
    ~Ssao();
    Ssao(const Ssao&) = delete;
    Ssao& operator=(const Ssao&) = delete;

    // Targets for a scene target of w x h (half that size), the depth in depthFmt (the depth-only pipelines'
    // format). False when the device cannot do it (then never again).
    bool ensure(int w, int h, SDL_GPUTextureFormat depthFmt);
    SDL_GPUTexture* depth() const { return depth_; }
    SDL_GPUTexture* result() const { return ao_[1]; }
    SDL_GPUSampler* sampler() const { return linear_; }
    int width() const { return w_; }
    int height() const { return h_; }

    struct Params {
        float tanX = 1, tanY = 1;      // tan of half the horizontal / vertical field of view
        float zNear = 1, zFar = 1000;  // the projection's planes
        float viewW = 1, viewH = 1;    // the half-resolution viewport used inside the targets (px)
        float radius = 14;             // world px: the taps' reach on the screen
        float reach = 48;              // world px: how far (in space) an occluder still counts
        float strength = 4.5f;
        float bias = 0.16f;
    };
    // After the depth pre-pass: the occlusion + its blur (two full-screen passes).
    void compute(SDL_GPUCommandBuffer* cmd, const Params& p);

private:
    void release();
    SDL_GPUDevice* dev_;
    SDL_GPUTexture* depth_ = nullptr;
    SDL_GPUTexture* ao_[2] = {nullptr, nullptr};  // raw, blurred
    SDL_GPUGraphicsPipeline* aoPipe_ = nullptr;
    SDL_GPUGraphicsPipeline* blurPipe_ = nullptr;
    SDL_GPUSampler* nearest_ = nullptr;
    SDL_GPUSampler* linear_ = nullptr;
    SDL_GPUTextureFormat depthFmt_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUTextureFormat aoFmt_ = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    int w_ = 0, h_ = 0;
    bool failed_ = false;
};

}  // namespace d2k::render3d

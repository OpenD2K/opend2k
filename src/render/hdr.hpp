#pragma once
// HDR rendering + tone mapping for the Modern lighting (docs/enhanced-graphics.md "Quality").
//
// The battlefield (2D: MapRenderer's offscreen copy; 3D: Renderer3D's resolved scene) is drawn into
// a half-float target instead of an 8-bit one, so light blends, additive glows, lamps and the 3D
// point lights keep values above 1. Bloom (render/bloom.*) is computed from those values; then this
// class draws the scene with a custom fragment shader of SDL's GPU renderer (SDL_GPURenderState,
// tonemap_fs in render3d/shaders_msl.hpp / shaders.hlsl): scene + bloom -> linear light -> exposure
// -> a hue-preserving filmic shoulder (identity up to a knee, then an exponential roll-off to the
// peak) -> sRGB-coded output. SDR output: peak = SDR white.
//
// EDR / HDR display output (Settings::hdrOutput; App): the renderer stays an ordinary sRGB one (so
// every blend is exactly as before - SDL's linear output colourspace would blend in linear light
// and change the whole picture), but the frame is drawn into a half-float "canvas" (screenTarget)
// and the window's swapchain is switched to SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR. The
// tone map then rolls off to the display's headroom (capped at 4x) instead of 1, writing extended
// sRGB-coded values; presentCanvas() finally decodes the canvas to linear light into the (float)
// backbuffer. The UI stays at SDR white (1.0); highlights of the battlefield go above it.
//
// Needs the GPU renderer (render3d/gpu.hpp); elsewhere available() is false and the callers keep
// the 8-bit path. Float targets are created with the sRGB colourspace, so SDL passes their values
// through untouched (no colourspace conversion) when they are drawn or sampled as usual.

#include <SDL3/SDL.h>

namespace d2k::render {

// The display output (App): extended = EDR / HDR output is on (the frame goes through the canvas);
// headroom = SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT (1 = SDR). Refreshed when the window's HDR state changes.
void setHdrOutput(bool extended, float headroom);
bool hdrOutputExtended();
float hdrOutputHeadroom();
// The frame's "screen" render target: the EDR canvas, or nullptr (the window). Code that returns to
// the screen after drawing into its own target uses this instead of nullptr.
void setScreenTarget(SDL_Texture* t);
SDL_Texture* screenTarget();

class Hdr {
public:
    explicit Hdr(SDL_Renderer* r);
    ~Hdr();
    Hdr(const Hdr&) = delete;
    Hdr& operator=(const Hdr&) = delete;

    // The tone map shader could be created (GPU renderer). Tried once.
    bool available();

    // A w x h half-float render target whose values SDL passes through raw (sRGB colourspace).
    static SDL_Texture* createTarget(SDL_Renderer* r, int w, int h);

    // Draws srcW x srcH texels of `scene` (from its top-left) to `dst` of the current target, with
    // `bloom` (Bloom::compute; may be null) x bloomStrength added. tonemap false = no shoulder,
    // just clamped at SDR white (Classic lighting in a float scene: the picture from before).
    // Returns false (nothing drawn) when unavailable.
    bool compose(SDL_Texture* scene, int srcW, int srcH, SDL_Texture* bloom, float bloomStrength, const SDL_FRect& dst,
                 bool tonemap);
    // EDR output: draws the canvas (extended sRGB-coded values) to `dst` of the current target (the
    // float backbuffer) as linear light. False when unavailable.
    bool presentCanvas(SDL_Texture* canvas, const SDL_FRect& dst);

private:
    bool init();
    bool ensureState(SDL_Texture* bloom);
    SDL_Renderer* r_;
    SDL_GPUDevice* dev_ = nullptr;
    SDL_GPUShader* shader_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;
    SDL_GPURenderState* state_ = nullptr;
    SDL_GPURenderState* presentState_ = nullptr;  // its own uniforms (the frame's other draw uses state_)
    SDL_GPUTexture* stateBloom_ = nullptr;  // the bloom texture the state binds
    SDL_Texture* black_ = nullptr;          // bound when there is no bloom
    bool tried_ = false, ok_ = false;
    float exposure_ = 1.0f;
};

}  // namespace d2k::render

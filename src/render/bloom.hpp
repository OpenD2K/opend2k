#pragma once
// Bloom for the Modern lighting (docs/enhanced-graphics.md "Lighting"): a soft glow around the
// brightest parts of the battlefield picture (explosions, muzzle flashes, lamps, sun glints),
// added back over it. SDL_Renderer only (works on every backend with custom blend modes):
// the picture is copied to a quarter (Retina: an eighth) of its size, a threshold is subtracted (dst - t), then it is blurred by
// a chain of linear 2x downsamples with a 4-tap tent at each level and accumulated back up; the
// result is added over the view. ~12 small draws per frame.

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_FRect;

namespace d2k::render {

class Bloom {
public:
    explicit Bloom(SDL_Renderer* r);
    ~Bloom();
    Bloom(const Bloom&) = delete;
    Bloom& operator=(const Bloom&) = delete;

    // Adds the bloom of `scene` (the frame's battlefield; srcW x srcH texels used from its top-left)
    // over `dst` of the current render target. strength 0 = nothing; threshold 0..1 per channel.
    // Restores the render target, viewport and clip. False if the backend cannot do it.
    bool apply(SDL_Texture* scene, int srcW, int srcH, const SDL_FRect& dst, float strength, float threshold);
    // The blurred glow of `scene` alone (steps 1-3 of apply; render/hdr.* adds it before the tone
    // map): a texture covering the srcW x srcH scene, or null. Restores the target, viewport, clip.
    SDL_Texture* compute(SDL_Texture* scene, int srcW, int srcH, float threshold);

private:
    static constexpr int kLevels = 4;
    bool ensure(int w, int h);
    void release();
    SDL_Renderer* r_;
    SDL_Texture* lv_[kLevels] = {};
    SDL_Texture* tmp_[kLevels] = {};
    SDL_Texture* white_ = nullptr;
    int lw_[kLevels] = {}, lh_[kLevels] = {};
    int w_ = 0, h_ = 0;
    bool failed_ = false;
};

}  // namespace d2k::render

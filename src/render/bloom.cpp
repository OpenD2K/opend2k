#include "render/bloom.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace d2k::render {

namespace {
// dst + src (alpha untouched) and dst - src (the threshold).
SDL_BlendMode addMode() {
    static const SDL_BlendMode m = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                                              SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    return m;
}
SDL_BlendMode subMode() {
    static const SDL_BlendMode m =
        SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT, SDL_BLENDFACTOR_ZERO,
                                   SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    return m;
}
}  // namespace

Bloom::Bloom(SDL_Renderer* r) : r_(r) {}

Bloom::~Bloom() {
    release();
    if (white_) SDL_DestroyTexture(white_);
}

void Bloom::release() {
    for (int i = 0; i < kLevels; ++i) {
        if (lv_[i]) SDL_DestroyTexture(lv_[i]);
        if (tmp_[i]) SDL_DestroyTexture(tmp_[i]);
        lv_[i] = tmp_[i] = nullptr;
    }
    w_ = h_ = 0;
}

bool Bloom::ensure(int w, int h) {
    if (failed_) return false;
    if (w == w_ && h == h_ && lv_[0]) return true;
    release();
    if (!white_) {
        white_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
        if (!white_) return false;
        const Uint32 px = 0xFFFFFFFFu;
        SDL_UpdateTexture(white_, nullptr, &px, 4);
    }
    // Half-float targets where the backend has them (no banding in the faint glow), else 8 bit.
    // Level 0 at a quarter of the view (an eighth on views over 3 Mpx): the glow is wide and soft,
    // so its first level need not be finer, and the fill cost stays small on Retina views.
    const int div = w * h > 3000000 ? 8 : 4;
    int lw = std::max(1, w / div), lh = std::max(1, h / div);
    for (int i = 0; i < kLevels; ++i) {
        for (SDL_Texture** t : {&lv_[i], &tmp_[i]}) {
            *t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA64_FLOAT, SDL_TEXTUREACCESS_TARGET, lw, lh);
            if (!*t) *t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, lw, lh);
            if (!*t) {
                SDL_Log("Bloom: no render targets (%s) - off", SDL_GetError());
                failed_ = true;
                release();
                return false;
            }
            SDL_SetTextureScaleMode(*t, SDL_SCALEMODE_LINEAR);
        }
        lw_[i] = lw;
        lh_[i] = lh;
        lw = std::max(1, lw / 2);
        lh = std::max(1, lh / 2);
    }
    if (!SDL_SetTextureBlendMode(lv_[0], addMode()) || !SDL_SetTextureBlendMode(white_, subMode())) {
        SDL_Log("Bloom: no custom blend modes (%s) - off", SDL_GetError());
        failed_ = true;
        release();
        return false;
    }
    w_ = w;
    h_ = h;
    return true;
}

bool Bloom::apply(SDL_Texture* scene, int srcW, int srcH, const SDL_FRect& dst, float strength, float threshold) {
    if (strength <= 0.001f) return false;
    if (!compute(scene, srcW, srcH, threshold)) return false;
    float left = strength;
    while (left > 0.001f) {
        const float m = std::min(1.0f, left);
        SDL_SetTextureBlendMode(tmp_[0], addMode());
        SDL_SetTextureColorModFloat(tmp_[0], m, m, m);
        const SDL_FRect s{0, 0, float(lw_[0]), float(lh_[0])};
        SDL_RenderTexture(r_, tmp_[0], &s, &dst);
        left -= m;
    }
    SDL_SetTextureColorModFloat(tmp_[0], 1, 1, 1);
    return true;
}

SDL_Texture* Bloom::compute(SDL_Texture* scene, int srcW, int srcH, float threshold) {
    if (!scene || srcW < 16 || srcH < 16) return nullptr;
    if (!ensure(srcW, srcH)) return nullptr;
    SDL_Texture* target = SDL_GetRenderTarget(r_);
    SDL_Rect vp{}, clip{};
    const bool vpSet = SDL_RenderViewportSet(r_);
    SDL_GetRenderViewport(r_, &vp);
    const bool clipOn = SDL_RenderClipEnabled(r_);
    if (clipOn) SDL_GetRenderClipRect(r_, &clip);
    SDL_BlendMode sceneBlend = SDL_BLENDMODE_NONE;
    SDL_ScaleMode sceneScale = SDL_SCALEMODE_NEAREST;
    SDL_GetTextureBlendMode(scene, &sceneBlend);
    SDL_GetTextureScaleMode(scene, &sceneScale);

    auto into = [&](SDL_Texture* t) {
        SDL_SetRenderTarget(r_, t);
        SDL_SetRenderViewport(r_, nullptr);
        SDL_SetRenderClipRect(r_, nullptr);
        SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
        SDL_RenderClear(r_);
    };
    auto draw = [&](SDL_Texture* src, int sw, int sh, int dw, int dh, SDL_BlendMode mode, float mod, float ox = 0,
                    float oy = 0) {
        SDL_SetTextureBlendMode(src, mode);
        SDL_SetTextureColorModFloat(src, mod, mod, mod);
        const SDL_FRect s{0, 0, float(sw), float(sh)};
        const SDL_FRect d{ox, oy, float(dw), float(dh)};
        SDL_RenderTexture(r_, src, &s, &d);
        SDL_SetTextureColorModFloat(src, 1, 1, 1);
    };
    // 1. Quarter size, minus the threshold.
    into(lv_[0]);
    SDL_SetTextureScaleMode(scene, SDL_SCALEMODE_LINEAR);
    draw(scene, srcW, srcH, lw_[0], lh_[0], SDL_BLENDMODE_NONE, 1);
    SDL_SetTextureBlendMode(scene, sceneBlend);
    SDL_SetTextureScaleMode(scene, sceneScale);
    {
        const float t = std::clamp(threshold, 0.0f, 1.0f);
        SDL_SetTextureColorModFloat(white_, t, t, t);
        const SDL_FRect all{0, 0, float(lw_[0]), float(lh_[0])};
        SDL_RenderTexture(r_, white_, nullptr, &all);
    }
    // 2. Down: a 4-tap tent at each level (blur), then half size for the next.
    for (int i = 0; i < kLevels; ++i) {
        into(tmp_[i]);
        for (int k = 0; k < 4; ++k)
            draw(lv_[i], lw_[i], lh_[i], lw_[i], lh_[i], addMode(), 0.25f, (k & 1) ? 1.0f : -1.0f, (k & 2) ? 1.0f : -1.0f);
        if (i + 1 < kLevels) {
            into(lv_[i + 1]);
            draw(tmp_[i], lw_[i], lh_[i], lw_[i + 1], lh_[i + 1], SDL_BLENDMODE_NONE, 1);
        }
    }
    // 3. Up: each wider level added into the next finer one.
    for (int i = kLevels - 1; i > 0; --i) {
        SDL_SetRenderTarget(r_, tmp_[i - 1]);
        SDL_SetRenderViewport(r_, nullptr);
        SDL_SetRenderClipRect(r_, nullptr);
        draw(tmp_[i], lw_[i], lh_[i], lw_[i - 1], lh_[i - 1], addMode(), 0.85f);
    }
    // 4. Back to the caller's target (apply adds tmp_[0] over the view).
    SDL_SetRenderTarget(r_, target);
    SDL_SetRenderViewport(r_, vpSet ? &vp : nullptr);
    SDL_SetRenderClipRect(r_, clipOn ? &clip : nullptr);
    return tmp_[0];
}

}  // namespace d2k::render

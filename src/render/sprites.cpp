#include "render/sprites.hpp"

#include "render/art_pack.hpp"
#include "render/upscale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace d2k::render {

namespace {

int luma(formats::Rgba c) { return (int(c & 0xFF) * 77 + int((c >> 8) & 0xFF) * 150 + int((c >> 16) & 0xFF) * 29) >> 8; }

// The house's COLOURS.BIN ramp sampled by luminance (0..255), interpolated between the entries
// sorted by their own luminance: tints a neutral grey ramp into the house colour shades.
struct HouseRamp {
    std::array<formats::Rgba, 16> c{};
    std::array<int, 16> l{};
    explicit HouseRamp(const std::array<formats::Rgba, 16>& ramp) {
        std::array<int, 16> idx{};
        for (int i = 0; i < 16; ++i) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return luma(ramp[a]) < luma(ramp[b]); });
        for (int i = 0; i < 16; ++i) {
            c[i] = ramp[idx[i]];
            l[i] = luma(c[i]);
        }
    }
    formats::Rgba at(int lum) const {
        // Stretch the grey ramp's range onto the house ramp's (grey 0..255 -> darkest..brightest).
        const float t = float(l[0]) + float(lum) / 255.0f * float(l[15] - l[0]);
        int i = 0;
        while (i < 14 && float(l[i + 1]) < t) ++i;
        const float span = float(std::max(1, l[i + 1] - l[i]));
        const float f = std::clamp((t - float(l[i])) / span, 0.0f, 1.0f);
        auto ch = [&](int s) {
            const float a = float((c[i] >> s) & 0xFF), b = float((c[i + 1] >> s) & 0xFF);
            return std::uint32_t(std::lround(a + (b - a) * f)) << s;
        };
        return ch(0) | ch(8) | ch(16);
    }
};

std::uint32_t maskValue(const ArtImage& m, int x, int y, int w, int h) {
    if (m.empty()) return 0;
    const int sx = std::min(m.w - 1, x * m.w / std::max(1, w)), sy = std::min(m.h - 1, y * m.h / std::max(1, h));
    const std::uint32_t c = m.px[static_cast<std::size_t>(sy) * m.w + sx];
    return (c & 0xFF) * (c >> 24) / 255;
}

}  // namespace

const Sprite::Level& Sprite::level(float zoom) const {
    for (const auto& l : levels)
        if (l.scale <= 2.0f * zoom + 1e-3f) return l;
    return levels.back();
}

SpriteCache::SpriteCache(SDL_Renderer* renderer, const formats::R16File& file,
                         const formats::HouseColours& colours)
    : renderer_(renderer), file_(file), colours_(colours) {}

SpriteCache::~SpriteCache() { clear(); }

void SpriteCache::clear() {
    for (auto& [key, s] : cache_)
        for (auto& l : s.levels) {
            if (l.texture) SDL_DestroyTexture(l.texture);
            if (l.shadow) SDL_DestroyTexture(l.shadow);
            if (l.glow) SDL_DestroyTexture(l.glow);
        }
    cache_.clear();
    for (auto& [key, s] : subCache_)
        for (auto& l : s.levels)
            if (l.texture) SDL_DestroyTexture(l.texture);
    subCache_.clear();
    packHeaders_.clear();
}

void SpriteCache::setHq(bool hq) {
    if (hq == hq_) return;
    hq_ = hq;
    clear();  // rebuilt lazily at the new resolution
}

void SpriteCache::setArtPack(const ArtPack* pack) {
    if (pack == pack_) return;
    pack_ = pack;
    clear();
}

void SpriteCache::upload(Sprite& s, std::vector<formats::Rgba>& px, std::vector<formats::Rgba>& shadow, int tw, int th,
                         float scale, bool linear, std::vector<formats::Rgba>* glow) {
    const SDL_ScaleMode mode = linear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
    auto make = [&](const std::vector<formats::Rgba>& data, int w, int h, SDL_BlendMode blend) -> SDL_Texture* {
        SDL_Texture* t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!t) return nullptr;
        SDL_UpdateTexture(t, nullptr, data.data(), w * 4);
        SDL_SetTextureBlendMode(t, blend);
        SDL_SetTextureScaleMode(t, mode);
        return t;
    };
    int w = tw, h = th;
    float sc = scale;
    while (true) {
        Sprite::Level l;
        l.scale = sc;
        l.texture = make(px, w, h, SDL_BLENDMODE_BLEND);
        if (!shadow.empty()) l.shadow = make(shadow, w, h, SDL_BLENDMODE_MOD);
        if (glow && !glow->empty()) l.glow = make(*glow, w, h, SDL_BLENDMODE_ADD);
        s.levels.push_back(l);
        // Mips only for smoothed textures above 1 texel per world pixel.
        if (!linear || sc <= 1.01f || w < 4 || h < 4) break;
        int nw = 0, nh = 0;
        px = downsample2(px, w, h, nw, nh);
        if (!shadow.empty()) shadow = downsample2(shadow, w, h, nw, nh);
        if (glow && !glow->empty()) *glow = downsample2(*glow, w, h, nw, nh);
        w = nw;
        h = nh;
        sc *= 0.5f;
    }
    s.texture = s.levels.front().texture;
    s.shadow = s.levels.front().shadow;
}

// Art-pack frame: base RGBA, house tint (mask from the pack, or derived from the original frame's
// house pixels), shadow mask (from the pack, or derived from the original's index-1 pixels).
bool SpriteCache::buildFromPack(Sprite& s, const formats::R16Frame& f, int frameIndex, int house, bool grey) {
    ArtImage base, houseMask, shadowMask;
    if (!pack_->loadFrame(frameIndex, base, houseMask, shadowMask)) return false;
    const int w = base.w, h = base.h;
    const float scale = float(pack_->scale());
    std::vector<formats::Rgba> px = std::move(base.px);
    std::vector<formats::Rgba> shadow;
    // A pack hotspot (hotspots.txt) means the image does not share the original frame's geometry:
    // draw it with its own header (offsets in world pixels) and never derive masks from the
    // original's palette indices.
    int hx = 0, hy = 0;
    const bool ownHotspot = pack_->hotspot(frameIndex, hx, hy);
    const bool pal8 = f.bpp == 8 && !f.empty() && !ownHotspot;
    auto origIndex = [&](int x, int y) -> int {
        const int ox = std::min(f.width - 1, int(float(x) / scale)), oy = std::min(f.height - 1, int(float(y) / scale));
        if (ox < 0 || oy < 0) return 0;
        return f.pixels[static_cast<std::size_t>(oy) * f.width + ox];
    };
    if (house >= 0 && house < formats::kHouseCount) {
        const HouseRamp ramp(colours_[house]);
        const bool derive = houseMask.empty() && pal8 && pack_->deriveHouse();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                auto& c = px[static_cast<std::size_t>(y) * w + x];
                if ((c >> 24) == 0) continue;
                std::uint32_t m = 0;
                if (!houseMask.empty()) m = maskValue(houseMask, x, y, w, h);
                else if (derive) m = origIndex(x, y) >= 240 ? 255 : 0;
                if (m == 0) continue;
                const formats::Rgba t = ramp.at(luma(c));
                auto mix = [&](int sft) {
                    const std::uint32_t a = (c >> sft) & 0xFF, b = (t >> sft) & 0xFF;
                    return ((a * (255 - m) + b * m) / 255) << sft;
                };
                c = (c & 0xFF000000u) | mix(0) | mix(8) | mix(16);
            }
    }
    const bool deriveShadow = shadowMask.empty() && pal8 && pack_->deriveShadows();
    if (!shadowMask.empty() || deriveShadow) {
        shadow.assign(px.size(), 0xFFFFFFFFu);
        bool any = false;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                std::uint32_t m = 0;
                if (!shadowMask.empty()) m = maskValue(shadowMask, x, y, w, h);
                else m = origIndex(x, y) == 1 ? 255 : 0;
                if (m == 0) continue;
                // 255 halves the destination (multiply by 128/255), like the original shadow.
                const std::uint32_t v = 255 - m * 127 / 255;
                shadow[static_cast<std::size_t>(y) * w + x] = 0xFF000000u | v << 16 | v << 8 | v;
                any = true;
            }
        if (!any) shadow.clear();
    }
    if (grey)
        for (auto& c : px) {
            if ((c >> 24) == 0) continue;
            const std::uint32_t v = std::min(255, luma(c) / 2 + 24);
            c = (c & 0xFF000000u) | v << 16 | v << 8 | v;
        }
    if (ownHotspot) {
        formats::R16Frame& hd = packHeaders_[frameIndex];
        hd.type = 1;
        hd.bpp = f.empty() ? 8 : f.bpp;
        hd.width = hd.frameWidth = std::max(1, int(std::lround(float(w) / scale)));
        hd.height = hd.frameHeight = std::max(1, int(std::lround(float(h) / scale)));
        hd.x = int(std::lround(float(hx) / scale));
        hd.y = int(std::lround(float(hy) / scale));
        s.frame = &hd;
    }
    s.width = f.empty() || ownHotspot ? int(float(w) / scale) : f.width;
    s.height = f.empty() || ownHotspot ? int(float(h) / scale) : f.height;
    s.drawW = float(w) / scale;
    s.drawH = float(h) / scale;
    s.fromPack = true;
    s.rendered3d = ownHotspot;
    // The emission layer (docs/enhanced-graphics.md "Emission"): premultiplied, drawn additively (ADD x alpha).
    std::vector<formats::Rgba> glow;
    ArtImage glowImg;
    if (pack_->loadGlow(frameIndex, glowImg) && glowImg.w == w && glowImg.h == h) {
        bool any = false;
        for (std::uint32_t c : glowImg.px)
            if (c & 0xFFFFFFu) any = true;
        if (any) {
            glow = std::move(glowImg.px);
            for (auto& c : glow) c = (c & 0xFFFFFFu) ? (c | 0xFF000000u) : 0u;  // rgb = emission, opaque where any
        }
    }
    upload(s, px, shadow, w, h, scale, true, &glow);
    return s.texture != nullptr;
}

const Sprite* SpriteCache::get(int frameIndex, int house, bool grey) {
    if (frameIndex < 0 || frameIndex >= frameCount()) return nullptr;
    const long long key = (static_cast<long long>(frameIndex) * 16 + (house + 1)) * 2 + (grey ? 1 : 0);
    if (auto it = cache_.find(key); it != cache_.end()) return it->second.texture ? &it->second : nullptr;

    Sprite s;
    const auto& f = file_.frames[frameIndex];
    s.frame = &f;
    if (pack_ && pack_->hasFrame(frameIndex) && buildFromPack(s, f, frameIndex, house, grey)) {
        auto [it, _] = cache_.emplace(key, std::move(s));
        return &it->second;
    }
    s = Sprite{};
    s.frame = &f;
    if (!f.empty()) {
        s.width = f.width;
        s.height = f.height;
        s.drawW = float(f.width);
        s.drawH = float(f.height);
        std::vector<formats::Rgba> px(static_cast<std::size_t>(f.width) * f.height);
        std::vector<formats::Rgba> shadow;  // white everywhere, grey where shadow
        if (f.bpp == 16) {
            px = formats::decodeFrame(file_, f, /*zeroIsTransparent=*/true);
        } else {
            const formats::Palette* pal = f.palette >= 0 ? &file_.palettes[f.palette] : nullptr;
            for (std::size_t i = 0; i < px.size(); ++i) {
                const std::uint8_t p = f.pixels[i];
                if (p == 0) px[i] = 0;
                else if (p == 1) {
                    px[i] = 0;
                    if (shadow.empty()) shadow.assign(px.size(), 0xFFFFFFFFu);
                    shadow[i] = 0xFF808080u;
                }
                else if (p >= 240 && house >= 0 && house < formats::kHouseCount) px[i] = colours_[house][p - 240];
                else px[i] = pal ? (*pal)[p] : (0xFF000000u | p * 0x010101u);
            }
        }
        if (grey)
            for (auto& c : px) {
                if ((c >> 24) == 0) continue;
                const int r5 = (c & 0xFF) >> 3, g5 = ((c >> 8) & 0xFF) >> 3, b5 = ((c >> 16) & 0xFF) >> 3;
                const std::uint32_t v5 = static_cast<std::uint32_t>(std::min(31, (r5 + g5 + b5) / 6 + 3));
                const std::uint32_t v = v5 << 3 | v5 >> 2;
                c = (c & 0xFF000000u) | v << 16 | v << 8 | v;
            }
        const int k = hq_ ? 4 : 1;
        if (hq_) {
            px = scale4x(px, f.width, f.height);
            if (!shadow.empty()) shadow = scale4x(shadow, f.width, f.height);
        }
        upload(s, px, shadow, f.width * k, f.height * k, float(k), hq_);
    }
    auto [it, _] = cache_.emplace(key, std::move(s));
    return it->second.texture ? &it->second : nullptr;
}

const Sprite* SpriteCache::getIntensity(int frameIndex) {
    if (frameIndex < 0 || frameIndex >= frameCount()) return nullptr;
    if (auto it = subCache_.find(frameIndex); it != subCache_.end()) return it->second.texture ? &it->second : nullptr;
    Sprite s;
    const auto& f = file_.frames[frameIndex];
    s.frame = &f;
    if (!f.empty() && f.bpp == 8) {
        s.width = f.width;
        s.height = f.height;
        s.drawW = float(f.width);
        s.drawH = float(f.height);
        std::vector<formats::Rgba> px(static_cast<std::size_t>(f.width) * f.height);
        for (std::size_t i = 0; i < px.size(); ++i) {
            const std::uint32_t v = std::min<std::uint32_t>(f.pixels[i], 31) * 255 / 31;
            px[i] = 0xFF000000u | v << 16 | v << 8 | v;
        }
        const int k = hq_ ? 4 : 1;
        if (hq_) px = scale4x(px, f.width, f.height);
        std::vector<formats::Rgba> noShadow;
        upload(s, px, noShadow, f.width * k, f.height * k, float(k), hq_);
    }
    auto [it, _] = subCache_.emplace(frameIndex, std::move(s));
    return it->second.texture ? &it->second : nullptr;
}

}  // namespace d2k::render

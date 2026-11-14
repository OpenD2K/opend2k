#include "render/sprites.hpp"

#include "render/art_pack.hpp"
#include "render/frame_probe.hpp"
#include "render/upscale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
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

// Worker threads preparing queued frames (setPrefetchGroups).
struct SpriteCache::Async {
    struct Job {
        long long key;
        int frame, house;
        bool grey;
    };
    std::mutex m;
    std::condition_variable work, done;
    std::deque<Job> queue;
    std::unordered_set<long long> queued;  // in the queue
    std::unordered_set<long long> running;
    std::unordered_map<long long, std::pair<int, Prepared>> ready;  // key -> (frame, pixels)
    std::vector<std::thread> threads;
    std::uint64_t gen = 0;
    bool stop = false;
};

const Sprite::Level& Sprite::level(float zoom) const {
    for (const auto& l : levels)
        if (l.scale <= 2.0f * zoom + 1e-3f) return l;
    return levels.back();
}

SpriteCache::SpriteCache(SDL_Renderer* renderer, const formats::R16File& file,
                         const formats::HouseColours& colours)
    : renderer_(renderer), file_(file), colours_(colours) {}

SpriteCache::~SpriteCache() {
    if (std::getenv("D2K_SPRITE_LOG") && !cache_.empty()) {
        std::size_t bytes = 0;
        for (auto& [key, sp] : cache_)
            for (auto& l : sp.levels)
                for (SDL_Texture* t : {l.texture, l.shadow, l.glow})
                    if (t) bytes += std::size_t(t->w) * std::size_t(t->h) * 4;
        SDL_Log("sprite cache: %zu sprites, %.0f MB of textures (peak %.0f MB, budget %s, %llu evicted)", cache_.size(),
                double(bytes) / (1 << 20), double(peakBytes_) / (1 << 20),
                budget_ ? (std::to_string(budget_ >> 20) + " MB").c_str() : "none", (unsigned long long)evicted_);
    }
    stopAsync();
    clear();
}

void SpriteCache::clear() {
    if (async_) {  // queued / prepared frames are of the old art; wait for the running ones (they read the pack)
        std::unique_lock<std::mutex> l(async_->m);
        ++async_->gen;
        async_->queue.clear();
        async_->queued.clear();
        async_->ready.clear();
        async_->done.wait(l, [&] { return async_->running.empty(); });
    }
    for (auto& [key, s] : cache_)
        for (auto& l : s.levels) {
            if (l.texture) SDL_DestroyTexture(l.texture);
            if (l.shadow) SDL_DestroyTexture(l.shadow);
            if (l.glow) SDL_DestroyTexture(l.glow);
        }
    cache_.clear();
    bytes_ = 0;
    for (auto& [key, s] : subCache_)
        for (auto& l : s.levels)
            if (l.texture) SDL_DestroyTexture(l.texture);
    subCache_.clear();
    packHeaders_.clear();
}

void SpriteCache::setHq(bool hq) {
    if (hq == hq_) return;
    clear();  // rebuilt lazily at the new resolution (first: workers read hq_ / pack_)
    hq_ = hq;
}

void SpriteCache::setArtPack(const ArtPack* pack) {
    if (pack == pack_) return;
    clear();
    pack_ = pack;
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
bool SpriteCache::prepareFromPack(Prepared& s, const formats::R16Frame& f, int frameIndex, int house, bool grey) const {
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
        formats::R16Frame& hd = s.header;
        hd.type = 1;
        hd.bpp = f.empty() ? 8 : f.bpp;
        hd.width = hd.frameWidth = std::max(1, int(std::lround(float(w) / scale)));
        hd.height = hd.frameHeight = std::max(1, int(std::lround(float(h) / scale)));
        hd.x = int(std::lround(float(hx) / scale));
        hd.y = int(std::lround(float(hy) / scale));
        s.ownHeader = true;
    }
    s.width = f.empty() || ownHotspot ? int(float(w) / scale) : f.width;
    s.height = f.empty() || ownHotspot ? int(float(h) / scale) : f.height;
    s.drawW = float(w) / scale;
    s.drawH = float(h) / scale;
    s.fromPack = true;
    s.rendered3d = ownHotspot;
    {  // the opaque bounds (alpha > 1/2)
        int top = h, bottom = -1, left = w, right = -1;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if ((px[static_cast<std::size_t>(y) * w + x] >> 24) > 127) {
                    top = std::min(top, y);
                    bottom = y;
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
        const bool any = bottom >= 0;
        s.opaqueTop = any ? float(top) / scale : 0;
        s.opaqueBottom = any ? float(bottom + 1) / scale : s.drawH;
        s.opaqueLeft = any ? float(left) / scale : 0;
        s.opaqueRight = any ? float(right + 1) / scale : s.drawW;
    }
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
    mips(s, px, shadow, &glow, w, h, scale, true);
    return true;
}

// The level chain upload() makes, as pixels.
void SpriteCache::mips(Prepared& p, std::vector<formats::Rgba>& px, std::vector<formats::Rgba>& shadow,
                       std::vector<formats::Rgba>* glow, int tw, int th, float scale, bool linear) {
    p.linear = linear;
    int w = tw, h = th;
    float sc = scale;
    while (true) {
        Prepared::Lvl l;
        l.w = w;
        l.h = h;
        l.scale = sc;
        l.px = px;
        l.shadow = shadow;
        if (glow) l.glow = *glow;
        p.levels.push_back(std::move(l));
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
}

SpriteCache::Prepared SpriteCache::prepare(int frameIndex, int house, bool grey) const {
    Prepared s;
    const auto& f = file_.frames[frameIndex];
    if (pack_ && pack_->hasFrame(frameIndex) && prepareFromPack(s, f, frameIndex, house, grey)) {
        s.ok = true;
        return s;
    }
    s = Prepared{};
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
        mips(s, px, shadow, nullptr, f.width * k, f.height * k, float(k), hq_);
        s.ok = true;
    }
    return s;
}

const Sprite* SpriteCache::finish(long long key, int frameIndex, Prepared& p) {
    Sprite s;
    s.frame = &file_.frames[frameIndex];
    if (p.ok) {
        s.width = p.width;
        s.height = p.height;
        s.drawW = p.drawW;
        s.drawH = p.drawH;
        s.fromPack = p.fromPack;
        s.rendered3d = p.rendered3d;
        s.opaqueTop = p.opaqueTop;
        s.opaqueBottom = p.opaqueBottom;
        s.opaqueLeft = p.opaqueLeft;
        s.opaqueRight = p.opaqueRight;
        if (p.ownHeader) {
            formats::R16Frame& hd = packHeaders_[frameIndex];
            hd = p.header;
            s.frame = &hd;
        }
        const SDL_ScaleMode mode = p.linear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
        auto make = [&](const std::vector<formats::Rgba>& data, int w, int h, SDL_BlendMode blend) -> SDL_Texture* {
            SDL_Texture* t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
            if (!t) return nullptr;
            SDL_UpdateTexture(t, nullptr, data.data(), w * 4);
            SDL_SetTextureBlendMode(t, blend);
            SDL_SetTextureScaleMode(t, mode);
            return t;
        };
        for (const Prepared::Lvl& lv : p.levels) {
            Sprite::Level l;
            l.scale = lv.scale;
            l.texture = make(lv.px, lv.w, lv.h, SDL_BLENDMODE_BLEND);
            if (!lv.shadow.empty()) l.shadow = make(lv.shadow, lv.w, lv.h, SDL_BLENDMODE_MOD);
            if (!lv.glow.empty()) l.glow = make(lv.glow, lv.w, lv.h, SDL_BLENDMODE_ADD);
            s.levels.push_back(l);
        }
        if (!s.levels.empty()) {
            s.texture = s.levels.front().texture;
            s.shadow = s.levels.front().shadow;
        }
        for (const Sprite::Level& l : s.levels)
            for (SDL_Texture* t : {l.texture, l.shadow, l.glow})
                if (t) s.bytes += std::size_t(t->w) * std::size_t(t->h) * 4;
        bytes_ += s.bytes;
        peakBytes_ = std::max(peakBytes_, bytes_);
    }
    s.lastUsed = frame_;
    auto [it, _] = cache_.emplace(key, std::move(s));
    return it->second.texture ? &it->second : nullptr;
}

void SpriteCache::stopAsync() {
    if (!async_) return;
    {
        std::lock_guard<std::mutex> l(async_->m);
        async_->stop = true;
    }
    async_->work.notify_all();
    for (auto& t : async_->threads) t.join();
    async_.reset();
}

void SpriteCache::setPrefetchGroups(std::vector<int> groupStarts) {
    std::sort(groupStarts.begin(), groupStarts.end());
    groupStarts.erase(std::unique(groupStarts.begin(), groupStarts.end()), groupStarts.end());
    groupStarts_ = std::move(groupStarts);
    if (groupStarts_.empty() || async_) return;
    async_ = std::make_unique<Async>();
    const unsigned hw = std::max(2u, std::thread::hardware_concurrency());
    const unsigned n = std::clamp(hw / 3, 1u, 3u);
    for (unsigned i = 0; i < n; ++i)
        async_->threads.emplace_back([this] {
            Async& a = *async_;
            std::unique_lock<std::mutex> l(a.m);
            while (true) {
                a.work.wait(l, [&] { return a.stop || !a.queue.empty(); });
                if (a.stop) return;
                const Async::Job j = a.queue.front();
                a.queue.pop_front();
                a.queued.erase(j.key);
                a.running.insert(j.key);
                const std::uint64_t gen = a.gen;
                l.unlock();
                Prepared p = prepare(j.frame, j.house, j.grey);
                l.lock();
                a.running.erase(j.key);
                if (gen == a.gen) a.ready.emplace(j.key, std::make_pair(j.frame, std::move(p)));
                a.done.notify_all();
            }
        });
}

void SpriteCache::prefetch(int frameIndex, int house, int radius) {
    if (!async_ || (!pack_ && !hq_) || frameIndex < 0 || frameIndex >= frameCount()) return;
    auto it = std::upper_bound(groupStarts_.begin(), groupStarts_.end(), frameIndex);
    if (it == groupStarts_.begin()) return;
    // The neighbours inside the art group (the next / previous facings of a turning hull or
    // turret, the next frames of an animation), nearest first; not the whole group: a group can
    // be hundreds of frames (all facings x all animation frames), most never drawn.
    const int first = *(it - 1);
    const int end = std::min(it == groupStarts_.end() ? frameCount() : *it, frameCount());
    std::lock_guard<std::mutex> l(async_->m);
    bool any = false;
    for (int d = radius; d >= 0; --d)
        for (const int f : {frameIndex - d, frameIndex + d}) {
            if (f < first || f >= end || (d == 0 && f != frameIndex)) continue;
            const long long key = (static_cast<long long>(f) * 16 + (house + 1)) * 2;
            if (cache_.count(key) || async_->running.count(key) || async_->ready.count(key)) continue;
            if (file_.frames[std::size_t(f)].empty() && !(pack_ && pack_->hasFrame(f))) continue;
            if (async_->queued.count(key))  // queued already: move it to the front
                for (auto q = async_->queue.begin(); q != async_->queue.end(); ++q)
                    if (q->key == key) {
                        async_->queue.erase(q);
                        break;
                    }
            async_->queue.push_front({key, f, house, false});
            async_->queued.insert(key);
            any = true;
        }
    // Keep the queue short: what was wanted long ago (the view moved on) is dropped.
    while (async_->queue.size() > 512) {
        async_->queued.erase(async_->queue.back().key);
        async_->queue.pop_back();
    }
    if (any) async_->work.notify_all();
}

void SpriteCache::setMemoryBudget(std::size_t bytes, int prefetchRadius) {
    budget_ = bytes;
    radius_ = std::clamp(prefetchRadius, 0, kNeighbours);
}

void SpriteCache::evict() {
    std::vector<std::pair<std::uint64_t, long long>> old;
    for (const auto& [key, sp] : cache_)
        if (sp.lastUsed + 2 < frame_ && sp.bytes) old.emplace_back(sp.lastUsed, key);
    std::sort(old.begin(), old.end());
    const std::size_t target = budget_ / 10 * 9;
    for (const auto& [used, key] : old) {
        if (bytes_ <= target) break;
        auto it = cache_.find(key);
        for (auto& l : it->second.levels)
            for (SDL_Texture* t : {l.texture, l.shadow, l.glow})
                if (t) SDL_DestroyTexture(t);
        bytes_ -= it->second.bytes;
        cache_.erase(it);
        ++evicted_;
    }
}

void SpriteCache::pump(double budgetMs) {
    ++frame_;
    if (budget_ && bytes_ > budget_) {
        FrameProbe::Scope probe("spriteEvict");
        evict();
    }
    if (!async_) return;
    const Uint64 t0 = SDL_GetTicksNS(), budget = Uint64(budgetMs * 1e6);
    while (SDL_GetTicksNS() - t0 < budget) {
        long long key = 0;
        std::pair<int, Prepared> item;
        {
            std::lock_guard<std::mutex> l(async_->m);
            if (async_->ready.empty()) return;
            auto it = async_->ready.begin();
            key = it->first;
            item = std::move(it->second);
            async_->ready.erase(it);
        }
        const Uint64 f0 = SDL_GetTicksNS();
        if (!cache_.count(key)) finish(key, item.first, item.second);
        static const bool logLoads = std::getenv("D2K_SPRITE_LOG") != nullptr;
        if (logLoads && SDL_GetTicksNS() - f0 > 2000000)
            SDL_Log("sprite: upload of frame %d (%d levels, %dx%d) took %.2f ms", item.first, int(item.second.levels.size()),
                    item.second.levels.empty() ? 0 : item.second.levels[0].w, item.second.levels.empty() ? 0 : item.second.levels[0].h,
                    double(SDL_GetTicksNS() - f0) / 1e6);
    }
}

const Sprite* SpriteCache::get(int frameIndex, int house, bool grey) {
    if (frameIndex < 0 || frameIndex >= frameCount()) return nullptr;
    const long long key = (static_cast<long long>(frameIndex) * 16 + (house + 1)) * 2 + (grey ? 1 : 0);
    if (auto it = cache_.find(key); it != cache_.end()) {
        it->second.lastUsed = frame_;
        return it->second.texture ? &it->second : nullptr;
    }
    FrameProbe::Scope probe("spriteLoad");
    if (async_) {
        std::unique_lock<std::mutex> l(async_->m);
        // Being prepared: wait for it (sooner than starting over); queued: take it out, build it now.
        if (async_->running.count(key)) {
            FrameProbe::Scope w("spriteWait");
            async_->done.wait(l, [&] { return !async_->running.count(key); });
        }
        if (auto it = async_->ready.find(key); it != async_->ready.end()) {
            Prepared p = std::move(it->second.second);
            async_->ready.erase(it);
            l.unlock();
            const Sprite* s = finish(key, frameIndex, p);
            prefetch(frameIndex, house, radius_);
            return s;
        }
        if (async_->queued.erase(key))
            for (auto q = async_->queue.begin(); q != async_->queue.end(); ++q)
                if (q->key == key) {
                    async_->queue.erase(q);
                    break;
                }
    }
    if (FrameProbe::get().on) {
        FrameProbe::get().add("syncLoads", 1000000);  // a count (1 "ms" each)
        static const bool logLoads = std::getenv("D2K_SPRITE_LOG") != nullptr;
        if (logLoads) SDL_Log("sprite: sync build of frame %d house %d%s", frameIndex, house, grey ? " grey" : "");
    }
    Prepared p = prepare(frameIndex, house, grey);
    const Sprite* s = finish(key, frameIndex, p);
    if (!grey) prefetch(frameIndex, house, radius_);
    return s;
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

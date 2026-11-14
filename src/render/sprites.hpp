#pragma once
// Lazily-built GPU textures for DATA.R16 frames, recoloured per house.
//
// 8bpp pixel conventions (per D2kEditor _resourcefile.pas):
//   0        transparent
//   1        shadow (halves the destination; separate mask texture)
//   240..255 house colour shades (COLOURS.BIN[house][index-240])
//   other    frame palette colour

#include "formats/bintables.hpp"
#include "formats/r16.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace d2k::render {

class ArtPack;

struct Sprite {
    SDL_Texture* texture = nullptr;
    // Shadow pixels (index 1) as a separate mask: drawn first with multiply blending at
    // 50% grey, which halves the destination exactly like the original shadow blit.
    SDL_Texture* shadow = nullptr;
    int width = 0, height = 0;  // original frame size (world pixels)
    // Size drawn in world pixels: the original size, or an art-pack image's size / pack scale.
    float drawW = 0, drawH = 0;
    const formats::R16Frame* frame = nullptr;  // header fields for offsets
    // Mip chain (Enhanced): levels[0] = {texture, shadow}; each further level is a premultiplied
    // 2x box downsample, used when the texture would be minified more than 2x on screen.
    // glow: the art pack's emission layer (<frame>.glow.png, additive), nullptr = none.
    struct Level { SDL_Texture* texture = nullptr; SDL_Texture* shadow = nullptr; float scale = 1; SDL_Texture* glow = nullptr; };
    std::vector<Level> levels;
    bool fromPack = false;
    // An art-pack frame rendered from a 3D model (its own hotspot): its shadow was cast by that
    // renderer's sun (58 degrees high), not the original art's (render::SpriteShadowWarp::kPack).
    bool rendered3d = false;
    // Bounds of the opaque pixels (world px from the top of the drawn frame; pack frames only, else
    // 0 / drawH): the ground line of a unit's silhouette shadow (MapRenderer::drawSilhouetteShadow).
    float opaqueTop = 0, opaqueBottom = 0, opaqueLeft = 0, opaqueRight = 0;
    std::size_t bytes = 0;        // its textures (the memory budget)
    std::uint64_t lastUsed = 0;   // SpriteCache frame of the last get()
    // The level to draw at `zoom` screen pixels per world pixel.
    const Level& level(float zoom) const;
};

class SpriteCache {
public:
    SpriteCache(SDL_Renderer* renderer, const formats::R16File& file, const formats::HouseColours& colours);
    ~SpriteCache();
    SpriteCache(const SpriteCache&) = delete;
    SpriteCache& operator=(const SpriteCache&) = delete;

    // Returns nullptr for empty/out-of-range frames. house < 0 keeps palette colours.
    const Sprite* get(int frameIndex, int house, bool grey = false);
    // Raw-index ("intensity") variant for the arithmetic explosion blits, where the 8-bit index
    // is an amount added to / subtracted from each 5-bit destination channel instead of a colour:
    // Image__BlitTImageSubtractive 0x4310c0 (flag 2, table 0x790110 = max(c - i, 0)), Brighten
    // 0x433340 (flag 4, table 0x6d8da8 = min(c + i, 31)), RedTint 0x4337d0 (flag 0x40: R + i,
    // G - i, B - i) and GreenTint 0x433c50 (flag 0x80: R - i, G + i). Stored as grey
    // (min(i, 31) * 255 / 31); the caller picks the blend. HQ / mips like normal frames; never
    // taken from an art pack.
    const Sprite* getIntensity(int frameIndex);
    // grey: the original's greyed sidebar icon (Image__BlitTImage_4326B0: every channel
    // (r + g + b) / 6 + 3 in 5-bit units), cached separately.
    // HQ mode: textures are built 4x upscaled (Scale2x twice) and sampled linearly.
    void setHq(bool hq);
    bool hq() const { return hq_; }
    // Enhanced art pack (replacement images, lazily loaded); nullptr = none. The cache does not
    // own the pack; frames the pack lacks keep the (HQ) original.
    void setArtPack(const ArtPack* pack);
    const ArtPack* artPack() const { return pack_; }
    const formats::HouseColours& colours() const { return colours_; }
    int frameCount() const { return static_cast<int>(file_.frames.size()); }
    const formats::R16Frame* frame(int i) const { return i >= 0 && i < frameCount() ? &file_.frames[i] : nullptr; }

    // Background preparation (Enhanced: an art-pack frame is a PNG decode, a house tint and a
    // mip chain - a few ms each, which drawn on first use made the game stutter whenever new
    // frames appeared: a turret turning, a unit moving off in a new direction, an explosion).
    // groupStarts: the first frames of every unit / building / projectile / animation art. A frame
    // drawn for the first time queues its neighbours in its art group (kNeighbours each side) on
    // worker threads; prefetch() queues frames ahead of use. get() then only uploads (a frame not
    // ready yet is built at once, as before - the same pixels either way).
    static constexpr int kNeighbours = 4;
    void setPrefetchGroups(std::vector<int> groupStarts);
    void prefetch(int frameIndex, int house, int radius);
    // Upload prepared frames for up to budgetMs (once per frame, before drawing); then, over the
    // memory budget, evict the least recently drawn sprites.
    void pump(double budgetMs);
    // Texture memory budget (bytes; 0 = unlimited, the desktop). Over it, sprites no frame drew
    // for the last 2 frames are freed oldest first down to 90 % (built again when needed). Touch
    // profiles also prefetch fewer neighbours (radius).
    void setMemoryBudget(std::size_t bytes, int prefetchRadius);
    std::size_t textureBytes() const { return bytes_; }

private:
    // A sprite's pixels, built without the renderer (thread-safe: reads the file, colours, pack).
    struct Prepared {
        struct Lvl {
            int w = 0, h = 0;
            float scale = 1;
            std::vector<formats::Rgba> px, shadow, glow;
        };
        bool ok = false;
        int width = 0, height = 0;
        float drawW = 0, drawH = 0;
        bool fromPack = false, rendered3d = false, ownHeader = false;
        float opaqueTop = 0, opaqueBottom = 0, opaqueLeft = 0, opaqueRight = 0;
        formats::R16Frame header{};
        bool linear = false;
        std::vector<Lvl> levels;
    };
    Prepared prepare(int frameIndex, int house, bool grey) const;
    bool prepareFromPack(Prepared& p, const formats::R16Frame& f, int frameIndex, int house, bool grey) const;
    static void mips(Prepared& p, std::vector<formats::Rgba>& px, std::vector<formats::Rgba>& shadow, std::vector<formats::Rgba>* glow,
                     int tw, int th, float scale, bool linear);
    const Sprite* finish(long long key, int frameIndex, Prepared& p);
    struct Async;
    std::unique_ptr<Async> async_;
    std::vector<int> groupStarts_;
    void stopAsync();
    SDL_Renderer* renderer_;
    const formats::R16File& file_;
    const formats::HouseColours& colours_;
    std::unordered_map<long long, Sprite> cache_;
    std::unordered_map<int, Sprite> subCache_;
    // Headers of art-pack frames with their own hotspot (ArtPack::hotspot), by frame index; node
    // based, so Sprite::frame can point into it.
    std::unordered_map<int, formats::R16Frame> packHeaders_;
    bool hq_ = false;
    const ArtPack* pack_ = nullptr;
    std::size_t budget_ = 0, bytes_ = 0, peakBytes_ = 0;
    std::uint64_t frame_ = 0, evicted_ = 0;
    int radius_ = kNeighbours;
    void evict();
    void clear();
    void upload(Sprite& s, std::vector<formats::Rgba>& px, std::vector<formats::Rgba>& shadow, int tw, int th,
                float scale, bool linear, std::vector<formats::Rgba>* glow = nullptr);
};

}  // namespace d2k::render

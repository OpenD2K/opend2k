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

#include <string>
#include <unordered_map>
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

private:
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
    void clear();
    bool buildFromPack(Sprite& s, const formats::R16Frame& f, int frameIndex, int house, bool grey);
    void upload(Sprite& s, std::vector<formats::Rgba>& px, std::vector<formats::Rgba>& shadow, int tw, int th,
                float scale, bool linear, std::vector<formats::Rgba>* glow = nullptr);
};

}  // namespace d2k::render

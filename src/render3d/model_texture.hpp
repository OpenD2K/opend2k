#pragma once
// GPU-ready model textures (docs/memory.md): a decoded RGBA8 image -> its whole mip chain built on the
// CPU (2x2 box filter, as the GPU's mip generation) -> block-compressed where the device samples BC
// formats (BC1 / BC3 colour, BC5 normal maps: 4-8x less memory than RGBA8), else RGBA8. Levels the
// game's closest zoom never samples (a small model's atlas with far more texels per world pixel than
// screen pixels) are dropped. Built on the loader threads; the renderer uploads `data` as it is and
// frees it. No SDL / GPU calls here (pure, tested by game_test).

#include <cstdint>
#include <vector>

namespace d2k::render3d {

enum class TexFormat : std::uint8_t { RGBA8, BC1, BC3, BC5 };

// What the target device samples (filled from the GPU device: modelTextureCaps).
struct TexCaps {
    bool bc = false;  // BC1, BC3 and BC5 sampled textures (desktop GPUs, Apple silicon Macs)
};
// (render3d/gpu.hpp modelTextureCaps: from the device.)

enum class TexKind : std::uint8_t {
    Color,   // base colour, alpha = cut-out + emission (BC1 when every texel is opaque, else BC3)
    Data,    // ORM + house mask in alpha (BC3)
    Normal,  // tangent-space normal map: x, y kept (BC5), z rebuilt by the shader
};

struct PreparedTexture {
    TexFormat format = TexFormat::RGBA8;
    int w = 0, h = 0, levels = 0;  // level 0 size, number of levels in `data`
    std::vector<std::uint8_t> data;  // level 0 first, each level tightly packed (rows of texels / 4x4 blocks)
    bool empty() const { return data.empty(); }
    std::size_t levelOffset(int level) const;
    std::size_t levelBytes(int level) const;
    int levelW(int level) const { return w >> level > 0 ? w >> level : 1; }
    int levelH(int level) const { return h >> level > 0 ? h >> level : 1; }
    std::size_t bytes() const { return data.size(); }
};

// The texels per world pixel a model texture needs at most: the game's closest zoom is 4 screen px per
// world px (8 output px on a Retina display at full render resolution); 16 keeps every texel that can
// reach a pixel there with a margin for uneven atlas islands.
constexpr float kMaxTexelsPerWorldPx = 16.0f;

// rgba: w x h texels, R in the lowest byte. texelsPerPx: the base level's average texels per world pixel
// on the model (0 = unknown: keep every level). Levels are dropped from the top while the next one still
// has >= kMaxTexelsPerWorldPx and is at least 256 texels.
PreparedTexture prepareTexture(const std::vector<std::uint32_t>& rgba, int w, int h, TexKind kind, float texelsPerPx,
                               const TexCaps& caps);

// Block encoders (exposed for tests): one 4x4 block of RGBA8 texels (R lowest byte, row-major).
void encodeBc1(const std::uint32_t px[16], std::uint8_t out[8]);
void encodeBc4(const std::uint8_t v[16], std::uint8_t out[8]);
// Decoders for tests / checks.
void decodeBc1(const std::uint8_t in[8], std::uint32_t px[16]);
void decodeBc4(const std::uint8_t in[8], std::uint8_t v[16]);

}  // namespace d2k::render3d

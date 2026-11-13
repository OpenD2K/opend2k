#pragma once
// Dune 2000 .R8 / .R16 image archives (DATA.R16, BLOX*.R16, MOUSE.R16, ...).
// Format documented in docs/formats.md.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::formats {

using Rgba = std::uint32_t;  // 0xAABBGGRR (little-endian RGBA8 bytes)
using Palette = std::array<Rgba, 256>;

struct R16Frame {
    // 0 = empty entry (no pixels, width/height 0), 1 = may carry its own palette,
    // 2 = reuses the previous palette.
    std::uint8_t type = 0;
    bool empty() const { return type == 0 || width == 0 || height == 0; }
    int width = 0, height = 0;
    int x = 0, y = 0;        // origin offsets as stored in the file
    int bpp = 0;             // 8 (paletted) or 16 (RGB555)
    int frameWidth = 0, frameHeight = 0;
    int palette = -1;        // index into R16File::palettes for 8bpp frames, -1 if none
    std::vector<std::uint8_t> pixels;  // raw: w*h bytes (8bpp) or w*h*2 bytes (16bpp LE)
};

struct R16File {
    std::vector<R16Frame> frames;
    std::vector<Palette> palettes;
};

enum class R16Layout {
    Plain,  // DATA.R16, BLOX*.R16, MOUSE.R16
    Ui,     // UI_*.R16: 8 extra bytes before every 3rd entry among the first 45
};

// Throws std::runtime_error on malformed data.
R16File parseR16(std::span<const std::uint8_t> data, R16Layout layout = R16Layout::Plain);
R16File loadR16(const std::string& path, R16Layout layout = R16Layout::Plain);

// Convert RGB555 to opaque RGBA8.
Rgba rgb555ToRgba(std::uint16_t c);

// Decode a frame to RGBA8 (w*h pixels). Colour/index 0 becomes transparent
// when `zeroIsTransparent` is set.
std::vector<Rgba> decodeFrame(const R16File& file, const R16Frame& frame, bool zeroIsTransparent);

}  // namespace d2k::formats

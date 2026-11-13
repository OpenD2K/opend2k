#pragma once
// Truevision .TGA images (data/UI_GFX/*.TGA menu backdrops and buttons): uncompressed (type 2)
// or RLE (type 10) true-colour, 16/24/32 bpp, either row order. Decoded to RGBA8.

#include "formats/r16.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::formats {

struct Image {
    int width = 0, height = 0;
    std::vector<Rgba> pixels;  // row-major, top row first, 0xAABBGGRR
};

// Throws std::runtime_error on unsupported/malformed files.
Image parseTga(std::span<const std::uint8_t> data);
Image loadTga(const std::string& path);

}  // namespace d2k::formats

#pragma once
// Dune 2000 bitmap fonts (FONT*.FNT / FONTCOL*.FNT) + bin/FONT*.BIN character map.
//
// .FNT is a dump of the in-memory font record (0x408 bytes: header + 256 runtime glyph
// pointers, ignored) followed by glyphs {u32 width, u32 height, width*height bytes},
// starting at `firstChar`. Pixel 0 = transparent; other values are colour indexes
// (FONT.FNT uses 1 = ink; FONTCOL.FNT uses a palette, FONTCOL.FPL).
// FONT*.BIN maps a character code to its glyph code (Graphlib__LoadFontFile 0x42e450);
// a mapped code of 0x20 is a space. Layout from BlitFontChars 0x42dc00 /
// GetStringPixelWidth 0x42dfb0.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::formats {

struct Glyph {
    int width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
};

struct Font {
    int spaceWidth = 0;
    int firstChar = 0;
    int spacing = 0;  // extra pixels after each glyph
    int height = 0;
    std::vector<Glyph> glyphs;               // glyph for code firstChar + i
    std::array<std::uint8_t, 256> charMap{}; // character -> glyph code

    const Glyph* glyphFor(unsigned char c) const;
    int textWidth(const std::string& text) const;
};

// FONTCOL.FPL font palette (Graphlib_font_pal 0x42e1c0): the game uses the first 16 u32 entries,
// each converted with GetColor16bit (byte 2 = red, byte 1 = green, byte 0 = blue). Returned as
// opaque RGBA8 (0xAABBGGRR); index 0 is the transparent glyph background.
using FontPalette = std::array<std::uint32_t, 16>;
FontPalette parseFontPalette(std::span<const std::uint8_t> fpl);
FontPalette loadFontPalette(const std::string& path);

Font parseFont(std::span<const std::uint8_t> fnt, std::span<const std::uint8_t> charMap);
Font loadFont(const std::string& fntPath, const std::string& binPath);

}  // namespace d2k::formats

#include "formats/font.hpp"

#include "formats/io.hpp"

namespace d2k::formats {

namespace {
constexpr std::size_t kFontRecordSize = 0x408;
}

const Glyph* Font::glyphFor(unsigned char c) const {
    const int code = charMap[c];
    const int i = code - firstChar;
    if (code == 0x20 || i < 0 || i >= static_cast<int>(glyphs.size())) return nullptr;
    return &glyphs[i];
}

int Font::textWidth(const std::string& text) const {
    int w = 0;
    for (unsigned char c : text) {
        const Glyph* g = glyphFor(c);
        w += g ? g->width + spacing : spaceWidth;
    }
    return w;
}

Font parseFont(std::span<const std::uint8_t> fnt, std::span<const std::uint8_t> charMap) {
    if (fnt.size() < kFontRecordSize) throw std::runtime_error("FNT: too small");
    Font f;
    f.spaceWidth = fnt[1];
    f.firstChar = fnt[2];
    f.spacing = fnt[3];
    f.height = fnt[4];
    ByteReader rd(fnt.subspan(kFontRecordSize));
    while (rd.remaining() >= 8) {
        Glyph g;
        g.width = static_cast<int>(rd.u32());
        g.height = static_cast<int>(rd.u32());
        if (g.width < 0 || g.height < 0 || g.width > 256 || g.height > 256)
            throw std::runtime_error("FNT: bad glyph size");
        auto px = rd.bytes(static_cast<std::size_t>(g.width) * g.height);
        g.pixels.assign(px.begin(), px.end());
        f.glyphs.push_back(std::move(g));
    }
    // Default identity map (as the game initialises before reading FONT.BIN).
    for (int i = 0; i < 256; ++i) f.charMap[i] = static_cast<std::uint8_t>(i < 0x20 ? 0x20 : i);
    if (charMap.size() == 256)
        for (int i = 0; i < 256; ++i) f.charMap[i] = charMap[i];
    return f;
}

FontPalette parseFontPalette(std::span<const std::uint8_t> fpl) {
    if (fpl.size() < 64) throw std::runtime_error("FPL: too small");
    FontPalette p{};
    for (int i = 0; i < 16; ++i) {
        const std::uint32_t b = fpl[i * 4], g = fpl[i * 4 + 1], r = fpl[i * 4 + 2];
        // GetColor16bit keeps 5 bits per channel; expand like rgb555ToRgba.
        const std::uint32_t r5 = r >> 3, g5 = g >> 3, b5 = b >> 3;
        p[i] = 0xFF000000u | ((b5 << 3 | b5 >> 2) << 16) | ((g5 << 3 | g5 >> 2) << 8) | (r5 << 3 | r5 >> 2);
    }
    return p;
}

FontPalette loadFontPalette(const std::string& path) { return parseFontPalette(readFile(path)); }

Font loadFont(const std::string& fntPath, const std::string& binPath) {
    const auto fnt = readFile(fntPath);
    const auto map = binPath.empty() ? std::vector<std::uint8_t>{} : readFile(binPath);
    return parseFont(fnt, map);
}

}  // namespace d2k::formats

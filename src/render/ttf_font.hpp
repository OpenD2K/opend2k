#pragma once
// TrueType text for the Enhanced in-game UI: a font compiled into the binary (third_party/fonts,
// SIL OFL 1.1; cmake/EmbedFonts.cmake) rasterised with stb_truetype (public domain) at whole-pixel
// sizes into per-size glyph atlases (white, alpha = coverage; tinted per draw). Strings are UTF-8;
// a string that is not valid UTF-8 is a game string in the active code page (CP1251 for the
// Russian Text.UIB, else CP1252; formats/language.hpp), so both render, Cyrillic included.

#include "render/text.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace d2k::render {

// The fonts compiled into the binary by name ("play_regular", "play_bold"); nullptr if missing.
const unsigned char* embeddedFont(const char* name, std::size_t& size);

class TtfFont {
public:
    // data must outlive the font (the embedded arrays do).
    TtfFont(SDL_Renderer* r, const unsigned char* data, std::size_t size);
    ~TtfFont();
    TtfFont(const TtfFont&) = delete;
    TtfFont& operator=(const TtfFont&) = delete;
    bool ok() const { return ok_; }

    // px: the line height in pixels (ascent - descent), snapped to whole pixels. (x, y) = the
    // top-left of the line box. Returns the advance width.
    float draw(std::string_view s, float x, float y, float px, Colour c);
    float width(std::string_view s, float px);
    float lineHeight(float px) const { return float(int(px + 0.5f)); }

    // UTF-8 (or, when not valid UTF-8, the active code page) to code points.
    static std::vector<std::uint32_t> decode(std::string_view s);

private:
    struct Glyph {
        int x = 0, y = 0, w = 0, h = 0;  // atlas rectangle
        float xoff = 0, yoff = 0;        // offset from the pen on the baseline
        float adv = 0;
    };
    struct Atlas {
        SDL_Texture* tex = nullptr;
        int penX = 1, penY = 1, rowH = 0;
        float scale = 0, ascent = 0;
        std::unordered_map<std::uint32_t, Glyph> glyphs;
    };
    Atlas& atlas(int px);
    const Glyph* glyph(Atlas& a, int px, std::uint32_t cp);

    SDL_Renderer* r_;
    struct Info;
    std::unique_ptr<Info> info_;
    bool ok_ = false;
    std::unordered_map<int, Atlas> atlases_;
};

}  // namespace d2k::render

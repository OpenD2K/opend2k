#pragma once
// Draws text with an original Dune 2000 bitmap font (FONT*.FNT). Glyphs are baked into
// one white texture atlas and tinted per draw call.

#include "formats/font.hpp"

#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace d2k::render {

struct Colour {
    unsigned char r = 255, g = 255, b = 255, a = 255;
};

class TextRenderer {
public:
    TextRenderer(SDL_Renderer* r, const formats::Font& font);
    ~TextRenderer();
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // Draws `text` with its top-left at (x, y). Returns the width drawn in pixels.
    float draw(const std::string& text, float x, float y, Colour c = {}, float scale = 1.0f, bool shadow = true);
    // UTF-8 text (our labels, community text from the catalog) is converted to the active code
    // page first (formats::lang::forBitmapFont); code page bytes are drawn as they are.
    float width(const std::string& text, float scale = 1.0f) const;
    float height(float scale = 1.0f) const { return font_.height * scale; }

private:
    struct GlyphRect { int x = 0, y = 0, w = 0, h = 0; };
    float drawRun(const std::string& text, float x, float y, Colour c, float scale);

    SDL_Renderer* r_;
    const formats::Font& font_;
    SDL_Texture* atlas_ = nullptr;
    std::vector<GlyphRect> rects_;  // per glyph in font_.glyphs
};

}  // namespace d2k::render

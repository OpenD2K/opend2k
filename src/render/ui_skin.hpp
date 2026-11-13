#pragma once
// Materials and drawing primitives of the Enhanced in-game UI (docs/enhanced-graphics.md "In-game
// UI"). A modern skin drawn procedurally at the screen's native resolution - no original UI
// bitmap is stretched anywhere in the chrome:
//   - panels: dark brushed metal (per-row grain + low-frequency noise), a thin glowing house-colour
//     accent along the battlefield edge;
//   - wells (tray, readouts, minimap screen): recessed rounded rectangles with inner shadows;
//   - buttons: rounded volumetric plates (gradient body, 1-px top highlight, drop shadow) with
//     vector icons rasterised from signed-distance shapes (wrench, coin, shield, arrow, chevrons,
//     ship, gear, ...) and smooth hover / pressed / active / disabled lighting;
//   - icon bezels for the production slots (steel rim, glass gloss, inner shadow);
//   - text in an embedded TrueType font (Play, SIL OFL; render/ttf_font.*) with Cyrillic, falling
//     back to the original bitmap font.
// Sizes are in "original pixels" times k (the UI scale, set per frame from the window size).
// Every procedural piece is baked once per size / state at the exact pixel size and cached.
// The production icons themselves (unit / building pictures) are content: they come from the
// sprite cache (HQ upscale or an AI art pack).

#include "formats/bintables.hpp"
#include "render/sprites.hpp"
#include "render/text.hpp"
#include "render/ttf_font.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2k::render {

class UiSkin {
public:
    UiSkin(SDL_Renderer* r, TextRenderer& text, const formats::HouseColours& colours);
    ~UiSkin();
    UiSkin(const UiSkin&) = delete;
    UiSkin& operator=(const UiSkin&) = delete;

    SDL_Renderer* renderer() const { return r_; }

    // UI scale: screen pixels per original pixel (1.5 at 1280x800, 2 at 1920x1080).
    static float scaleFor(int outW, int outH);
    void setScale(float k);
    float k() const { return k_; }
    float px(float v) const { return std::round(v * k_); }  // original pixels -> screen pixels

    // --- text: the TrueType UI font (or the bitmap font if it failed to load) ---
    // scale: 1 = a 10-px line; ts() = the default for the current k.
    float ts() const { return ts_; }
    float textH(float scale = 0) const;
    float lineH() const { return textH(); }
    float textW(const std::string& s, float scale = 0, bool bold = false);
    // Light text with a soft drop shadow; returns the width.
    float label(const std::string& s, float x, float y, Colour c, float scale = 0, bool bold = false);

    void setHouse(int house);
    int house() const { return house_; }
    Colour accent() const { return accent_; }  // the house colour (light)
    Colour accentDark() const { return accentDark_; }
    Colour houseColour(int house) const;       // a light shade of any house's COLOURS.BIN ramp

    // --- primitives ---
    void fill(const SDL_FRect& r, Colour c);
    void outline(const SDL_FRect& r, Colour c, float t = 1);
    // The side column: brushed dark bronze with a sand / house accent along its left edge.
    void panel(const SDL_FRect& r);
    // A recessed area (tray, readouts, the minimap screen).
    void well(const SDL_FRect& r);
    // Settings "Panel transparency": 1 = opaque (default). The column (panel) is drawn at this
    // opacity over a dark veil; wells (tray, minimap screen) at wellOpacity. Set per frame.
    void setPanelOpacity(float panel, float well) { panelOpacity_ = panel; wellOpacity_ = well; }
    bool panelTranslucent() const { return panelOpacity_ < 1.0f; }
    // The minimap frame: a well with house-colour corner marks.
    void frame(const SDL_FRect& r);

    enum class Icon {
        None, Repair, Sell, Guard, Retreat, Upgrade, Starport, Options, Up, Down,
        Buildings, Infantry, Vehicles, Special,
        // Phone layout glyphs (docs/ios.md "Phone layout").
        Menu, Eye, EyeOff, Radar, ChevronLeft, ChevronRight, Stop, Deploy, Close, Pin, Bolt,
    };
    enum class State { Normal, Hot, Pressed, Disabled, Lit };
    // A volumetric plate with a vector icon (left of the label when there is one) and a label.
    void button(const SDL_FRect& r, Icon icon, State s, const std::string& text = "");

    // A bare vector icon (no plate) in a size x size box at (x, y), tinted `c`, with a soft dark
    // shadow so it reads over the battlefield (phone layout top bar and buttons).
    void glyph(const SDL_FRect& r, Icon icon, Colour c);
    // Global opacity for everything drawn after it (fills, labels, baked pieces, sprites, glyphs):
    // the phone layout's semi-transparent HUD that fades when idle. 1 = opaque (default); reset it.
    void setAlpha(float a) { alpha_ = std::clamp(a, 0.0f, 1.0f); }
    float alpha() const { return alpha_; }

    // Icon bezel (production slots): bezelUnder (drop shadow / hover glow, dark well), the picture
    // inside bezelInner, then bezelOver (inner shadow, glass gloss, steel rim).
    enum class Bezel { Normal, Hot, Pressed, Disabled, Lit, Empty };
    SDL_FRect bezelInner(const SDL_FRect& r) const;
    void bezelUnder(const SDL_FRect& r, Bezel s);
    void bezelOver(const SDL_FRect& r, Bezel s);

    // A round glass lamp (status light).
    void lamp(float cx, float cy, float d, Colour c);
    // Vertical meter (power): frac 0..1 of the height filled in c, a tick at `mark`.
    void meter(const SDL_FRect& r, float frac, Colour c, float mark = -1);
    // Segmented bar (n segments, frac 0..1): health colours (original thresholds) or one colour.
    void segBar(const SDL_FRect& r, float frac, int n, bool health = true, Colour c = {});
    // Production clock (FUN_004263b0): `grey` icon with the bright one revealed clockwise from
    // 12 o'clock up to frac.
    void clock(const Sprite* bright, const Sprite* grey, const SDL_FRect& dst, float frac);
    // A sprite drawn smoothly into dst.
    void sprite(const Sprite* s, const SDL_FRect& dst, Uint8 alpha = 255);
    // The minimap screen while the radar is off: scanlines and a house-colour caption.
    void radarOff(const SDL_FRect& r, float fade, const std::string& caption);
    // Tooltip box (title line first) left of / below the mouse, kept inside (0,0,maxX,maxY).
    struct TipLine { std::string text; Colour colour; };
    void tooltip(const std::vector<TipLine>& lines, float mouseX, float mouseY, float maxX, float maxY);

    static constexpr Colour kGold{240, 190, 100, 255};
    static constexpr Colour kSand{236, 222, 196, 255};
    static constexpr Colour kDim{160, 148, 128, 255};
    static constexpr Colour kGood{120, 230, 110, 255};
    static constexpr Colour kBad{240, 96, 70, 255};
    static constexpr Colour kWarn{250, 205, 80, 255};

private:
    struct Tex {
        SDL_Texture* t = nullptr;
        int w = 0, h = 0;
        int pad = 0;  // margin around the piece (shadows / glows)
    };
    Tex upload(const std::vector<std::uint32_t>& px, int w, int h);
    void drop(Tex& t);
    // Procedural pieces baked at the exact size: see baked() in the .cpp for the kinds.
    const Tex& baked(int kind, int w, int h, int state, int extra = 0);
    void drawBaked(const Tex& t, const SDL_FRect& r);
    TtfFont* font(bool bold) const;

    SDL_Renderer* r_;
    TextRenderer& text_;
    const formats::HouseColours& colours_;
    std::unique_ptr<TtfFont> regular_, bold_;
    float k_ = 1.5f, ts_ = 1;
    float panelOpacity_ = 1.0f, wellOpacity_ = 1.0f;
    float alpha_ = 1.0f;
    Uint8 al(Uint8 a) const { return Uint8(std::lround(a * alpha_)); }
    int house_ = 0;
    Colour accent_{120, 170, 255, 255}, accentDark_{40, 70, 140, 255};
    std::unordered_map<std::uint64_t, Tex> baked_;
    float bakedK_ = 0;
};

}  // namespace d2k::render

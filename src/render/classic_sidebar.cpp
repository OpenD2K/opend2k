#include "render/classic_sidebar.hpp"

#include "render/build_icons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace d2k::render {

namespace {

// UI_ENG.R16: 15 buttons x {i32 x, i32 y, image shown, image pressed, 16bpp background}
// (Setup__LoadUI_LanguageFile 0x460ec0 -> _SidebarButtons 0x5162c8, stride 0x18), then the
// power bar images, the two banners, the three radar house logos and the radar background.
// The x/y pairs are the 8 extra bytes formats::R16Layout::Ui skips; values from the file.
struct ButtonDef { int x, y; };
constexpr std::array<ButtonDef, 15> kButtons = {{
    {502, 374}, {531, 373}, {566, 374}, {595, 373},  // 0..3 left up/down, right up/down
    {493, 138}, {518, 138}, {549, 141}, {573, 138}, {599, 138},  // 4 repair 5 sell 6 orb 7 guard 8 retreat
    {499, 167}, {563, 167},  // 9 UPGRADE, 10 STARPORT
    {0, 0},                  // 11 OPTIONS
    {499, 167}, {563, 167},  // 12 MAIN (upgrade page), 13 MAIN (starport page)
    {501, 374},              // 14 PURCHASE
}};
enum Btn { kLUp, kLDown, kRUp, kRDown, kRepair, kSell, kOrb, kGuard, kRetreat, kUpgrade, kStarport, kOptions,
           kMainUpgrade, kMainStarport, kPurchase };
constexpr int kFramePowerGrey = 45;  // _SideBarPowerImages[0..3]: grey, red, yellow, green (10x200)
constexpr int kFrameRadarLogo = 51;  // _RadarHouseImages: Atreides, Harkonnen, Ordos (128x128)
constexpr int kFrameRadarBack = 54;  // DAT_00782768: radar background (128x128)

// Layout globals (.data defaults 0x4e41dc.., the 640x400 frame of UIBB.R16).
constexpr int kMiniMapX = 497, kRadarY = 8, kRadarSize = 128;
constexpr int kIconW = 60, kIconH = 47;  // _SidebarIconWidth, SideBarIconHeight
constexpr int kPanelLeftX = 501, kPanelRightX = 563, kPanelsY = 184;
constexpr int kPowerX = 485, kPowerY = 172, kPowerH = 200;
constexpr int kCreditsX = 390, kCreditsY = 0;
constexpr int kTimerX = 0x1a4;  // Display__DrawMissionTimer 0x4255e0
// DATA.R16 technical frames (Graphics__LoadR16Resource 0x4606d0 order).
constexpr int kDataFrameRepairIcon = 3;   // DAT_006dc468
constexpr int kDataFrameEmptyIcon = 4;    // DAT_006d60d0
constexpr int kDataFrameDigit0 = 27;      // DAT_006e5720[10]
constexpr int kDataFrameDigitBlank = 37;  // DAT_006e5748
constexpr int kDataFrameGroupDigit0 = 17; // DAT_006e2df0[10]: small digits 0..9 (starport picks)

// Unit behaviours handled by the special-weapon click path (palace weapons / airstrike).
bool specialUnit(int beh) { return beh == 4 || beh == 9 || beh == 10 || beh == 12; }

formats::Rgba packRgb(int r, int g, int b) {
    return 0xFF000000u | (std::uint32_t(b) << 16) | (std::uint32_t(g) << 8) | std::uint32_t(r);
}
// GetColor16bit: 8-bit channels truncated to 5 bits.
formats::Rgba rgb24(std::uint32_t rgb) {
    auto q = [](std::uint32_t c) { c >>= 3; return int(c << 3 | c >> 2); };
    return packRgb(q((rgb >> 16) & 0xff), q((rgb >> 8) & 0xff), q(rgb & 0xff));
}

}  // namespace

ClassicSidebar::ClassicSidebar(SDL_Renderer* r, Assets assets) : r_(r), assets_(std::move(assets)) {
    ui_.resize(assets_.ui.frames.size());
    if (assets_.uibb.size() >= std::size_t(kBaseW) * kBaseH * 2) {
        std::vector<formats::Rgba> px(std::size_t(kBaseW) * kBaseH);
        for (std::size_t i = 0; i < px.size(); ++i)
            px[i] = formats::rgb555ToRgba(std::uint16_t(assets_.uibb[i * 2] | assets_.uibb[i * 2 + 1] << 8));
        uibbPx_ = std::move(px);
    }
    // FONTCOL glyph atlas: one texture with the palette colours (timer, messages) and one mask
    // (index 1 black outline, the rest white) for the colour-cycled Ready/Hold text.
    if (const formats::Font* f = assets_.fontCol) {
        constexpr int kAtlasW = 512;
        int x = 0, y = 0, rowH = 0;
        glyphRects_.resize(f->glyphs.size());
        for (std::size_t i = 0; i < f->glyphs.size(); ++i) {
            const auto& g = f->glyphs[i];
            if (x + g.width + 1 > kAtlasW) { x = 0; y += rowH + 1; rowH = 0; }
            glyphRects_[i] = {x, y, g.width, g.height};
            x += g.width + 1;
            rowH = std::max(rowH, g.height);
        }
        const int h = std::max(1, y + rowH);
        std::vector<formats::Rgba> pal(std::size_t(kAtlasW) * h, 0), mask(pal.size(), 0);
        for (std::size_t i = 0; i < f->glyphs.size(); ++i) {
            const auto& g = f->glyphs[i];
            const auto& rc = glyphRects_[i];
            for (int gy = 0; gy < g.height; ++gy)
                for (int gx = 0; gx < g.width; ++gx) {
                    const std::uint8_t p = g.pixels[std::size_t(gy) * g.width + gx];
                    if (!p) continue;
                    const std::size_t o = std::size_t(rc.y + gy) * kAtlasW + rc.x + gx;
                    pal[o] = assets_.fontPalette[p & 15];
                    mask[o] = p == 1 ? 0xFF000000u : 0xFFFFFFFFu;
                }
        }
        textAtlas_ = makeTexture(pal, kAtlasW, h);
        textMask_ = makeTexture(mask, kAtlasW, h);
    }
    for (std::size_t i = 0; i < kButtons.size(); ++i) buttons_[i] = {kButtons[i].x, kButtons[i].y, 0};
}

ClassicSidebar::~ClassicSidebar() {
    auto drop = [](Tex& t) { if (t.t) SDL_DestroyTexture(t.t); t.t = nullptr; };
    drop(frame_);
    for (auto& t : ui_) drop(t);
    for (auto& [k, t] : icons_) drop(t);
    for (auto& w : work_) drop(w.tex);
    drop(textAtlas_);
    drop(textMask_);
    drop(radar_);
}

ClassicSidebar::Tex ClassicSidebar::makeTexture(const std::vector<formats::Rgba>& px, int w, int h, bool streaming) {
    Tex t;
    t.w = w;
    t.h = h;
    t.t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888,
                            streaming ? SDL_TEXTUREACCESS_STREAMING : SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t.t) return t;
    if (!px.empty()) SDL_UpdateTexture(t.t, nullptr, px.data(), w * 4);
    SDL_SetTextureBlendMode(t.t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t.t, SDL_SCALEMODE_NEAREST);
    return t;
}

const ClassicSidebar::Tex& ClassicSidebar::uiFrame(int i) {
    static const Tex kNone{};
    if (i < 0 || i >= int(ui_.size())) return kNone;
    Tex& t = ui_[i];
    if (!t.t) {
        const auto& f = assets_.ui.frames[i];
        if (f.empty()) return kNone;
        // 8bpp images are colour-keyed on index 0; the 16bpp backgrounds, power bars and the
        // radar pictures (Image__BlitTImageRaw) are opaque.
        const bool opaque = f.bpp == 16 || i >= kFrameRadarLogo;
        t = makeTexture(formats::decodeFrame(assets_.ui, f, !opaque), f.width, f.height);
    }
    return t;
}

// Sidebar icon pixels (DATA.R16, 8bpp): 0 transparent, 1 shadow, 240..255 house colours.
// grey: Image__BlitTImage_4326B0 -> every channel (r+g+b)/6 + 3 (5-bit units).
std::vector<formats::Rgba> ClassicSidebar::iconPixels(int frame, int house, bool grey) const {
    const auto& file = assets_.data;
    if (frame < 0 || frame >= int(file.frames.size()) || file.frames[frame].empty()) return {};
    const auto& f = file.frames[frame];
    std::vector<formats::Rgba> px;
    if (f.bpp == 16) {
        px = formats::decodeFrame(file, f, true);
    } else {
        px.resize(std::size_t(f.width) * f.height);
        const formats::Palette* pal = f.palette >= 0 ? &file.palettes[f.palette] : nullptr;
        for (std::size_t i = 0; i < px.size(); ++i) {
            const std::uint8_t p = f.pixels[i];
            if (p == 0) px[i] = 0;
            else if (p == 1) px[i] = 0x80000000u;
            else if (p >= 240 && house >= 0 && house < formats::kHouseCount) px[i] = assets_.colours[house][p - 240];
            else px[i] = pal ? (*pal)[p] : (0xFF000000u | p * 0x010101u);
        }
    }
    if (grey)
        for (auto& c : px) {
            if ((c >> 24) != 0xFF) continue;
            const int r5 = (c & 0xFF) >> 3, g5 = ((c >> 8) & 0xFF) >> 3, b5 = ((c >> 16) & 0xFF) >> 3;
            const int v = std::min(31, (r5 + g5 + b5) / 6 + 3);
            const int v8 = v << 3 | v >> 2;
            c = packRgb(v8, v8, v8);
        }
    return px;
}

const ClassicSidebar::Tex& ClassicSidebar::icon(int frame, int house, bool grey) {
    static const Tex kNone{};
    const long long key = (static_cast<long long>(frame) * 16 + (house + 1)) * 2 + (grey ? 1 : 0);
    auto it = icons_.find(key);
    if (it == icons_.end()) {
        Tex t;
        if (frame >= 0 && frame < int(assets_.data.frames.size()) && !assets_.data.frames[frame].empty()) {
            const auto& f = assets_.data.frames[frame];
            t = makeTexture(iconPixels(frame, house, grey), f.width, f.height);
        }
        it = icons_.emplace(key, t).first;
    }
    return it->second.t ? it->second : kNone;
}

// The production "clock" (FUN_004263b0): the work image starts as the greyed icon and the
// bright icon is copied back in a clockwise wedge from 12 o'clock, around the icon centre, up
// to progress >> 6 degrees (0..360 for 0..0x5a00).
const ClassicSidebar::Tex& ClassicSidebar::workIcon(int slot, int frame, int house, int degrees) {
    static const Tex kNone{};
    Work& w = work_[std::size_t(slot)];
    if (w.frame == frame && w.house == house && w.degrees == degrees && w.tex.t) return w.tex;
    const auto bright = iconPixels(frame, house, false), grey = iconPixels(frame, house, true);
    if (bright.empty()) return kNone;
    const auto& f = assets_.data.frames[frame];
    if (!w.tex.t || w.tex.w != f.width || w.tex.h != f.height) {
        if (w.tex.t) SDL_DestroyTexture(w.tex.t);
        w.tex = makeTexture({}, f.width, f.height, true);
    }
    std::vector<formats::Rgba> px(grey);
    const float cx = f.width / 2, cy = f.height / 2;
    for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x) {
            float a = std::atan2(x + 0.5f - cx, cy - (y + 0.5f)) * 180.0f / 3.14159265f;
            if (a < 0) a += 360.0f;
            if (a < float(degrees)) px[std::size_t(y) * f.width + x] = bright[std::size_t(y) * f.width + x];
        }
    if (w.tex.t) SDL_UpdateTexture(w.tex.t, nullptr, px.data(), f.width * 4);
    w.frame = frame;
    w.house = house;
    w.degrees = degrees;
    return w.tex;
}

void ClassicSidebar::blit(const Tex& t, int x, int y) {
    if (!t.t) return;
    const SDL_FRect dst{float(x), float(y), float(t.w), float(t.h)};
    SDL_RenderTexture(r_, t.t, nullptr, &dst);
}

void ClassicSidebar::blitPart(const Tex& t, int sx, int sy, int w, int h, int dx, int dy) {
    if (!t.t || w <= 0 || h <= 0) return;
    const SDL_FRect src{float(sx), float(sy), float(w), float(h)};
    const SDL_FRect dst{float(dx), float(dy), float(w), float(h)};
    SDL_RenderTexture(r_, t.t, &src, &dst);
}

void ClassicSidebar::fill(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    const SDL_FRect rc{float(x), float(y), float(w), float(h)};
    SDL_RenderFillRect(r_, &rc);
}

// FUN_0042d140: a one-pixel frame for the rect (x0, y0, x1, y1): verticals at x0 and x1-1,
// horizontals at y0 and y1-1.
void ClassicSidebar::frameRect(int x0, int y0, int x1, int y1, SDL_Color c) {
    fill(x0, y0, 1, y1 - y0, c);
    fill(x0, y1 - 1, x1 - x0, 1, c);
    fill(x1 - 1, y0, 1, y1 - y0, c);
    fill(x0, y0, x1 - x0, 1, c);
}

void ClassicSidebar::layout(int gameW, int gameH) {
    scale_ = 1;
    const int gw = std::max(kBaseW, gameW), gh = std::max(kBaseH, gameH);
    const bool changed = gw != gw_ || gh != gh_ || !frame_.t;
    gw_ = gw;
    gh_ = gh;
    dx_ = gw_ - kBaseW;
    // SideBarIconCount = (GameHeight - 212) / SideBarIconHeight (4 at 400).
    iconCount_ = (gh_ - (kPanelsY + 28)) / kIconH;
    extraRows_ = gh_ - kBaseH;
    if (changed) buildFrame();
}

// The UIBB.R16 frame for game resolutions above 640x400, built the way the community high-res
// patch extends it (ref/d2k-patches high-res, UIBB_<w>x<h>.R16 generator; reimplemented):
//  - the 640x400 picture anchored at the top-right corner;
//  - the options bar left of it: columns 71..375 of the original bar (the part between the
//    OPTIONS button and the credits box) tiled from x = 0 up to the credits box; the OPTIONS
//    button is drawn on top from UI_ENG.R16 at (0, 0);
//  - the sidebar below y = 183: the build block (rows 183..341, icon columns cleared to the
//    strip colour, the power bar's 100 % arrow removed) tiled down to the bottom piece;
//  - the bottom piece (scroll arrow area, the last 29 rows) at the bottom.
// At exactly 640x400 the original picture is used unchanged.
void ClassicSidebar::buildFrame() {
    if (frame_.t) SDL_DestroyTexture(frame_.t);
    frame_ = {};
    if (uibbPx_.empty()) return;
    if (gw_ == kBaseW && gh_ == kBaseH) {
        frame_ = makeTexture(uibbPx_, kBaseW, kBaseH);
        return;
    }
    constexpr int kBarFrom = 71, kBarLen = 305;           // options bar tile (x 71..375)
    constexpr int kBlockY = 183, kBlockH = 159;           // build block rows 183..341
    constexpr int kBottomH = 29;                          // bottom piece (rows 371..399)
    std::vector<formats::Rgba> src(uibbPx_);
    // Remove the power bar's arrow (6x6 at 485,268) by copying the pixels 8 rows above.
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 6; ++x)
            src[std::size_t(268 + y) * kBaseW + 485 + x] = src[std::size_t(260 + y) * kBaseW + 485 + x];
    const formats::Rgba strip = formats::rgb555ToRgba(0x0c02);
    std::vector<formats::Rgba> px(std::size_t(gw_) * gh_, 0xFF000000u);
    auto at = [&](int x, int y) -> formats::Rgba& { return px[std::size_t(y) * gw_ + x]; };
    // Corner: the original at the top right (rows 0..182 of the sidebar, the credits box).
    for (int y = 0; y < kBaseH && y < gh_; ++y)
        for (int x = 0; x < kBaseW; ++x) at(dx_ + x, y) = uibbPx_[std::size_t(y) * kBaseW + x];
    // Options bar. The tiles are phased so that they meet the corner picture's column 71 at
    // x = dx + 71 without a jump (the patch starts them at x = 0, which leaves a seam there).
    for (int y = 0; y < kOptionsBarH; ++y)
        for (int x = 0; x < dx_ + kBarFrom; ++x) {
            const int col = kBarFrom + ((x - (dx_ + kBarFrom)) % kBarLen + kBarLen) % kBarLen;
            at(x, y) = uibbPx_[std::size_t(y) * kBaseW + col];
        }
    // Battlefield area of the corner picture: black (the map is drawn over it).
    for (int y = kOptionsBarH; y < gh_; ++y)
        for (int x = 0; x < gw_ - kSidebarW; ++x) at(x, y) = 0xFF000000u;
    // Sidebar build block, tiled.
    const int sx = gw_ - kSidebarW;
    for (int y = kBlockY; y < gh_; ++y) {
        const int by = kBlockY + (y - kBlockY) % kBlockH;
        for (int x = 0; x < kSidebarW; ++x) {
            const bool icons = (x >= 21 && x < 81) || (x >= 83 && x < 143);
            at(sx + x, y) = icons ? strip : src[std::size_t(by) * kBaseW + 480 + x];
        }
    }
    // Bottom piece.
    for (int y = 0; y < kBottomH; ++y)
        for (int x = 0; x < kSidebarW; ++x)
            at(sx + x, gh_ - kBottomH + y) = uibbPx_[std::size_t(kBaseH - kBottomH + y) * kBaseW + 480 + x];
    frame_ = makeTexture(px, gw_, gh_);
}

SDL_Rect ClassicSidebar::battlefield() const {
    return SDL_Rect{0, kOptionsBarH * scale_, (gw_ - kSidebarW) * scale_, (gh_ - kOptionsBarH) * scale_};
}

bool ClassicSidebar::inBattlefield(float sx, float sy) const {
    const SDL_Rect b = battlefield();
    return sx >= b.x && sy >= b.y && sx < b.x + b.w && sy < b.y + b.h;
}

int ClassicSidebar::bx(int i) const { return buttons_[i].x >= 480 ? buttons_[i].x + dx_ : buttons_[i].x; }
int ClassicSidebar::by(int i) const { return buttons_[i].y >= 370 ? buttons_[i].y + extraRows_ : buttons_[i].y; }

bool ClassicSidebar::radarOn(const game::World& w, int side) const { return w.radarActive(side); }

int ClassicSidebar::stripCount(int strip, const game::World& w, int side) const {
    return int(strip == 0 ? w.buildableBuildings(side).size() : w.buildableUnits(side).size());
}

// Button states for this frame (the Game_Init defaults plus the per-frame checks in the main
// loop: STARPORT needs a starport, UPGRADE needs something upgradable; the pages swap them).
void ClassicSidebar::updateButtons(const game::World& w, int side) {
    std::array<bool, 15> vis{};
    vis[kOptions] = true;
    for (int i : {kRepair, kSell, kOrb, kGuard, kRetreat}) vis[i] = true;
    if (page_ == Page::Normal) {
        for (int i : {kLUp, kLDown, kRUp, kRDown}) vis[i] = true;
        vis[kUpgrade] = !w.upgradableBuildings(side).empty();
        const auto& s = w.sides()[side];
        for (int i = s.firstBuilding; i != -1; i = s.nextObject(i))
            if (w.buildingTemplate(s.buildings[i]).specialBehavior == game::building_behaviour::kStarport) vis[kStarport] = true;
    } else if (page_ == Page::Starport) {
        // STARPORT click (0x4433a0): the scroll arrows and STARPORT give way to MAIN / PURCHASE.
        vis[kMainStarport] = vis[kPurchase] = true;
        vis[kUpgrade] = !w.upgradableBuildings(side).empty();
        if (!w.starportOpen(side)) page_ = Page::Normal;  // (the original keeps the page; nothing to buy)
    } else {
        vis[kMainUpgrade] = true;
    }
    // A press whose release never reached us (e.g. OPTIONS opens the pause menu, which takes the
    // release) must not leave the button drawn pressed.
    if (pressed_ >= 0 && !(SDL_GetMouseState(nullptr, nullptr) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK))) pressed_ = -1;
    for (int i = 0; i < 15; ++i) buttons_[i].state = vis[i] ? (pressed_ == i ? 2 : 1) : 0;
    if (pressed_ >= 0 && !vis[pressed_]) pressed_ = -1;
}

// HandleSidebarButton 0x443040: inside the shown image and on a non-zero pixel.
int ClassicSidebar::buttonAt(float gx, float gy) const {
    for (int i = 0; i < 15; ++i) {
        if (buttons_[i].state != 1 && buttons_[i].state != 2) continue;
        const int fi = i * 3;
        if (fi >= int(assets_.ui.frames.size())) continue;
        const auto& f = assets_.ui.frames[fi];
        const int x = int(gx) - bx(i), y = int(gy) - by(i);
        if (x < 0 || y < 0 || x >= f.width || y >= f.height || f.empty()) continue;
        const std::size_t o = std::size_t(y) * f.width + x;
        const bool solid = f.bpp == 8 ? f.pixels[o] != 0 : (f.pixels[o * 2] | f.pixels[o * 2 + 1]) != 0;
        if (solid) return i;
    }
    return -1;
}

void ClassicSidebar::drawBackdrop() {
    if (!frame_.t) {
        fill(0, 0, gw_, kOptionsBarH, {60, 40, 25, 255});
        fill(gw_ - kSidebarW, 0, kSidebarW, gh_, {60, 40, 25, 255});
        return;
    }
    // The options bar and the sidebar (the battlefield part of the picture is under the map).
    blitPart(frame_, 0, 0, gw_ - kSidebarW, kOptionsBarH, 0, 0);
    blitPart(frame_, gw_ - kSidebarW, 0, kSidebarW, gh_, gw_ - kSidebarW, 0);
}

// FUN_00426000 (radar on: outpost and power >= 100 %) / FUN_00425fa0 (off: the house logo
// fades in over 64 frames, ColourFadeIn 0x4259f0).
void ClassicSidebar::drawRadar(const game::World& w, int side, float camX, float camY) {
    const int rx = kMiniMapX + dx_, ry = kRadarY;
    blit(uiFrame(kFrameRadarBack), rx, ry);
    if (!radarOn(w, side)) {
        int house = w.sides()[side].house;
        if (house < 0 || house > 2) house = 0;
        const Tex& logo = uiFrame(kFrameRadarLogo + house);
        if (logo.t) {
            const int a = std::min(radarFade_, 64);
            SDL_SetTextureAlphaMod(logo.t, Uint8(a * 255 / 64));
            blit(logo, rx, ry);
            SDL_SetTextureAlphaMod(logo.t, 255);
        }
        if (radarFade_ < 0x41) ++radarFade_;
        return;
    }
    radarFade_ = 0;
    const int mw = w.width(), mh = w.height();
    if (mw <= 0 || mh <= 0) return;
    if (!radar_.t || radar_.w != mw || radar_.h != mh) {
        if (radar_.t) SDL_DestroyTexture(radar_.t);
        radar_ = makeTexture({}, mw, mh, true);
    }
    // Radar_Rebuild 0x462140 colours (FUN_0044acc0); shrouded cells stay black (_RadarMap1 is
    // cleared and filled as the shroud lifts).
    const formats::Rgba kSpice = rgb24(0x646464), kSand = rgb24(0x1e1e1e), kDrive = rgb24(0x3c3232),
                        kRock = rgb24(0x464646), kWorm = rgb24(0xc6b5a5);
    auto sideColour = [&](int s) {
        int house = s >= 0 && s < int(w.sides().size()) ? w.sides()[s].house : 0;
        if (house < 0 || house >= formats::kHouseCount) house = 0;
        return assets_.colours[house][8];  // FUN_00461fb0: COLOURS.BIN shade 8
    };
    radarPx_.assign(std::size_t(mw) * mh, 0xFF000000u);
    for (int y = 0; y < mh; ++y)
        for (int x = 0; x < mw; ++x) {
            const auto& c = w.cell(x, y);
            if ((c.shroud & 0xF) == 1) continue;
            const std::uint32_t f = c.flags;
            formats::Rgba col;
            if (f & game::cell_flags::kBuilding) col = sideColour(int(f & game::cell_flags::kOwnerMask));
            else if (f & game::cell_flags::kSpiceMask) col = kSpice;
            else if (f & game::cell_flags::kSand) col = kSand;
            else if (f & game::cell_flags::kDriveOn) col = kDrive;
            else col = kRock;
            radarPx_[std::size_t(y) * mw + x] = col;
        }
    // Units: side colour (sandworms their own), on fully revealed cells, enemy cloaked units
    // hidden, flying units only when ornithopters.
    for (int s = 0; s < int(w.sides().size()); ++s) {
        const auto& sd = w.sides()[s];
        const formats::Rgba col = sideColour(s);
        for (int i = sd.firstUnit; i != -1; i = sd.nextObject(i)) {
            const auto& u = sd.units[i];
            const int x = u.tileX(), y = u.tileY();
            if (x < 0 || y < 0 || x >= mw || y >= mh || (w.cell(x, y).shroud & 0xF) != 0) continue;
            if (s != side && (u.flags & game::unit_flags::kCloaked)) continue;
            const int beh = w.unitTemplate(u).specialBehavior;
            if (beh == game::behaviour::kSandworm) radarPx_[std::size_t(y) * mw + x] = kWorm;
            else if (!(u.flags & game::unit_flags::kFlying) || beh == game::behaviour::kOrnithopter)
                radarPx_[std::size_t(y) * mw + x] = col;
        }
    }
    SDL_UpdateTexture(radar_.t, nullptr, radarPx_.data(), mw * 4);
    const int ox = (kRadarSize - mw) / 2, oy = (kRadarSize - mh) / 2;
    SDL_SetTextureBlendMode(radar_.t, SDL_BLENDMODE_NONE);
    blit(radar_, rx + ox, ry + oy);
    // View rectangle (Image__DrawRectClip, colour 0x00c800).
    const int x0 = int(camX) / 32 + ox + rx, y0 = int(camY) / 32 + oy + ry;
    const int x1 = (gw_ - kSidebarW) / 32 - 1 + x0, y1 = (gh_ - kOptionsBarH) / 32 + y0;
    const SDL_Color green{0, 200, 0, 255};
    fill(x0, y0, x1 - x0 + 1, 1, green);
    fill(x0, y1, x1 - x0 + 1, 1, green);
    fill(x0, y0, 1, y1 - y0 + 1, green);
    fill(x1, y0, 1, y1 - y0 + 1, green);
}

// FUN_00425860: the grey bar, then the bottom `percent` pixels (max 200) in red (< 51 %),
// yellow (< 100 %) or green.
void ClassicSidebar::drawPower(const game::World& w, int side) {
    const int px = kPowerX + dx_, py = kPowerY + extraRows_;  // high-res: anchored to the bottom
    blit(uiFrame(kFramePowerGrey), px, py);
    const int p = int(std::min<std::uint32_t>(w.economy(side).powerPercent, kPowerH));
    const int band = p < 100 ? (p < 0x33 ? 1 : 2) : 3;
    const Tex& bar = uiFrame(kFramePowerGrey + band);
    if (p > 0) blitPart(bar, 0, kPowerH - p, bar.w, p, px, py + kPowerH - p);
}

// FUN_00425560 / FUN_004254a0: 8 digits, 10 px apart, leading zeros blank.
void ClassicSidebar::drawCredits(const game::World& w, int side) {
    std::uint32_t v = std::uint32_t(std::clamp(w.economy(side).credits, 0, 9999999));
    bool lead = false;
    int x = kCreditsX + dx_;
    for (std::uint32_t div = 10000000; div != 0; div /= 10) {
        const std::uint32_t d = v / div;
        v -= d * div;
        if (d != 0) lead = true;
        const int frame = (lead || div == 1) ? kDataFrameDigit0 + int(d) : kDataFrameDigitBlank;
        blit(icon(frame, -1, false), x, kCreditsY);
        x += 10;
    }
}

void ClassicSidebar::drawText(const std::string& text, int x, int y, const SDL_Color* tint) {
    const formats::Font* f = assets_.fontCol;
    const Tex& atlas = tint ? textMask_ : textAtlas_;
    if (!f || !atlas.t) return;
    if (tint) SDL_SetTextureColorMod(atlas.t, tint->r, tint->g, tint->b);
    for (unsigned char ch : text) {
        const formats::Glyph* g = f->glyphFor(ch);
        if (!g) { x += f->spaceWidth; continue; }
        const SDL_Rect& rc = glyphRects_[std::size_t(g - f->glyphs.data())];
        blitPart(atlas, rc.x, rc.y, rc.w, rc.h, x, y);
        x += g->width + f->spacing;
    }
    if (tint) SDL_SetTextureColorMod(atlas.t, 255, 255, 255);
}

// Display__DrawNormalSideBarMenu 0x426e80.
void ClassicSidebar::drawStrips(const game::World& w, int side) {
    const auto& T = assets_.templates.bin;
    const int house = w.sides()[side].house;
    // Ready/Hold font colour: r = b = 20 + t, g = 210 + t, t cycling 0..40 per drawn frame.
    const SDL_Color pulse{Uint8(20 + pulse_), Uint8(210 + pulse_), Uint8(20 + pulse_), 255};
    const int stripBottom = kPanelsY + kIconH * iconCount_;
    auto blitClipped = [&](const Tex& t, int x, int y) {  // the strip's clip rect (local_40)
        if (!t.t) return;
        const int top = std::max(y, kPanelsY), bot = std::min(y + t.h, stripBottom);
        if (bot > top) blitPart(t, 0, top - y, t.w, bot - top, x, top);
    };
    auto label = [&](const std::string& s, int x, int y) {
        drawText(s, x + (kIconW - textWidth(s)) / 2, y + 0x10, &pulse);
    };
    auto readyFrame = [&](int x, int y) { frameRect(x, y, x + kIconW - 1, y + kIconH - 1, pulse); };

    for (int strip = 0; strip < 2; ++strip) {
        const int count = stripCount(strip, w, side);
        // Main loop clamp before drawing (0x448550).
        if (count < iconCount_ + scroll_[strip]) scroll_[strip] = count > iconCount_ ? count - iconCount_ : 0;
        int first = scroll_[strip], n = iconCount_, y0 = kPanelsY;
        const bool anim = prevScroll_[strip] != scroll_[strip];  // half-step frame after a scroll
        if (anim) {
            y0 -= kIconH / 2;
            ++n;
            if (prevScroll_[strip] < scroll_[strip]) --first;
        }
        prevScroll_[strip] = scroll_[strip];
        const int x = (strip == 0 ? kPanelLeftX : kPanelRightX) + dx_;
        if (strip == 0) {
            const auto list = w.buildableBuildings(side);
            const game::QueueState q = w.buildingQueue(side);
            for (int i = 0; i < n; ++i) {
                const int idx = first + i, y = y0 + i * kIconH;
                if (idx < 0 || idx >= int(list.size())) { blitClipped(icon(kDataFrameEmptyIcon, -1, false), x, y); continue; }
                const auto& opt = list[std::size_t(idx)];
                if (!q.active) { blitClipped(icon(opt.iconFrame, house, false), x, y); continue; }
                if (buildingIconGreyed(q, opt.type)) { blitClipped(icon(opt.iconFrame, house, true), x, y); continue; }
                blitClipped(workIcon(0, opt.iconFrame, house, q.progress >> 6), x, y);
                if (anim) continue;
                if (q.onHold) label(assets_.holdText, x, y);
                if (q.progress >= game::kBuildDone) {
                    label(assets_.readyText, x, y);
                    readyFrame(x, y);
                }
            }
        } else {
            const auto list = w.buildableUnits(side);
            const auto queues = w.unitQueues(side);
            for (int i = 0; i < n; ++i) {
                const int idx = first + i, y = y0 + i * kIconH;
                if (idx < 0 || idx >= int(list.size())) { blitClipped(icon(kDataFrameEmptyIcon, -1, false), x, y); continue; }
                const auto& opt = list[std::size_t(idx)];
                // A factory of the same kind is busy (or a palace weapon): others are greyed.
                const game::QueueState* mine = nullptr;
                const bool busy = unitIconBusy(w, queues, opt.type, &mine);
                const bool counts = !anim && y >= kPanelsY && y + kIconH <= stripBottom;
                if (!busy || (!mine && extendedQueue && !specialUnit(T.units[opt.type].specialBehavior))) {
                    blitClipped(icon(opt.iconFrame, house, false), x, y);
                    if (counts) drawQueueCount(w, side, opt.type, house, x, y);
                    continue;
                }
                if (!mine) { blitClipped(icon(opt.iconFrame, house, true), x, y); continue; }
                const int slot = std::clamp(mine->slot, 0, 9) + 1;
                blitClipped(workIcon(slot, opt.iconFrame, house, mine->progress >> 6), x, y);
                if (anim) continue;
                if (counts) drawQueueCount(w, side, opt.type, house, x, y);
                if (mine->onHold) label(assets_.holdText, x, y);
                if (mine->progress >= game::kBuildDone) {
                    label(assets_.readyText, x, y);
                    readyFrame(x, y);
                }
            }
        }
    }
}

// Not original (extended production queue, re/notes/economy.md §10): the type's queued count
// in the starport page's picked-count digits (DAT_006e2df0) at the icon's top-left, or an
// infinity mark (two 4x5 loops, the digits have no such glyph) with a dark outline. Shown in
// Extended mode for any queued type; in Original mode only while the type has queue entries.
void ClassicSidebar::drawQueueCount(const game::World& w, int side, int type, int house, int x, int y) {
    const game::AutoQueueState st = w.autoQueueFor(side, type);
    if (!st.infinite && (st.count == 0 || (!extendedQueue && st.count <= 1))) return;
    if (st.infinite) {
        static const char* const kMark[5] = {".##...##.", "#..#.#..#", "#...#...#", "#..#.#..#", ".##...##."};
        const SDL_Color shadow{0, 0, 0, 255}, ink{255, 255, 255, 255};  // like the digits
        for (int pass = 0; pass < 2; ++pass)
            for (int r = 0; r < 5; ++r)
                for (int c = 0; c < 9; ++c) {
                    if (kMark[r][c] != '#') continue;
                    if (pass == 0) {
                        for (int oy = -1; oy <= 1; ++oy)
                            for (int ox = -1; ox <= 1; ++ox) fill(x + 4 + c + ox, y + 5 + r + oy, 1, 1, shadow);
                    } else {
                        fill(x + 4 + c, y + 5 + r, 1, 1, ink);
                    }
                }
        return;
    }
    const int n = std::min(st.count, game::kAutoMaxCount);
    int dx = x + 4;
    if (n >= 10) {
        const Tex& t = icon(kDataFrameGroupDigit0 + n / 10, house, false);
        blit(t, dx, y + 4);
        dx += t.w > 0 ? std::min(t.w, 6) : 6;  // (the digit frames carry blank columns)
    }
    blit(icon(kDataFrameGroupDigit0 + n % 10, house, false), dx, y + 4);
}

// Display__DrawUpgradeSideBarMenu 0x4278a0: 8 slots (2 columns x 4 rows), no scrolling.
void ClassicSidebar::drawUpgradePage(const game::World& w, int side) {
    const int house = w.sides()[side].house;
    auto list = w.upgradableBuildings(side);
    const game::QueueState q = w.upgradeQueue(side);
    if (q.active && std::none_of(list.begin(), list.end(), [&](const game::BuildOption& o) { return o.type == q.type; }))
        list.push_back({q.type, assets_.templates.bin.buildings[q.type].buildingGroup, q.cost,
                        assets_.templates.firstBuildingIconFrame + q.type, 0});
    // Rows below the 4th (high-res): empty slots (the high-res patch's DrawEmptySideBarIcons).
    for (int i = 0; i < std::max(4, iconCount_) * 2; ++i) {
        const int x = ((i & 1) ? kPanelRightX : kPanelLeftX) + dx_, y = (i >> 1) * kIconH + kPanelsY;
        if (i >= int(list.size())) { blit(icon(kDataFrameEmptyIcon, -1, false), x, y); continue; }
        const auto& opt = list[std::size_t(i)];
        if (!q.active) blit(icon(opt.iconFrame, house, false), x, y);
        else if (opt.type != q.type) blit(icon(opt.iconFrame, house, true), x, y);
        else {
            blit(workIcon(11, opt.iconFrame, house, q.progress >> 6), x, y);
            if (q.onHold) drawText(assets_.holdText, x + (kIconW - textWidth(assets_.holdText)) / 2, y + 0x10);
        }
    }
}

// Display__DrawStarportSideBarMenu 0x427690: the 8 starport units (left/right, 4 rows), greyed
// when out of stock, while a frigate is on its way or with more than 5 picked; the picked count
// as a small digit (DAT_006e2df0) at the slot's top-left.
void ClassicSidebar::drawStarportPage(const game::World& w, int side) {
    const int house = w.sides()[side].house;
    const auto list = w.starportUnits(side);
    const int rows = std::max(4, iconCount_);
    for (int i = 0; i < rows * 2; ++i) {
        const int x = ((i & 1) ? kPanelRightX : kPanelLeftX) + dx_, y = (i >> 1) * kIconH + kPanelsY;
        if (i >= int(list.size()) || list[std::size_t(i)].type < 0) {
            blit(icon(kDataFrameEmptyIcon, -1, false), x, y);
            continue;
        }
        const auto& o = list[std::size_t(i)];
        blit(icon(o.iconFrame, house, !o.available), x, y);
        blit(icon(kDataFrameGroupDigit0 + std::clamp(o.ordered, 0, 9), house, false), x + 4, y + 4);
    }
}

// FUN_0044b5c0 / FUN_00423b40: a name box and, with a price, a "$%d" box below it (one pixel
// overlap, the shared edge cleared to black), anchored at (x0, y0), moved up so the bottom stays
// at or above limitY and left so the right edge stays at or left of limitX.
void ClassicSidebar::drawTooltipBox(const std::string& name, int price, int x0, int y0, int limitX, int limitY,
                                    SDL_Color c) {
    const int fh = assets_.fontCol ? assets_.fontCol->height : 12;
    const std::string priceText = price > 0 ? "$" + std::to_string(price) : std::string();  // 0 = no price
    const int nameR = x0 + textWidth(name) + 5;
    int nameB = y0 + fh + 2, priceT = nameB - 1, priceR = x0, bottom = nameB;
    if (!priceText.empty()) {
        priceR = x0 + textWidth(priceText) + 5;
        bottom = priceT + fh;
    }
    const int right = priceText.empty() ? nameR : std::max(nameR, priceR);
    if (bottom > limitY) {
        const int d = limitY - bottom;
        y0 += d;
        nameB += d;
        priceT += d;
        bottom += d;
    }
    const int dx = right > limitX ? limitX - right : 0;
    const int x = x0 + dx;
    auto box = [&](int l, int t, int r, int b) {  // BlitBeveledRectRGB: black inside, coloured edge
        fill(l, t, r - l, b - t, {0, 0, 0, 255});
        frameRect(l, t, r, b, c);
    };
    box(x, y0, nameR + dx, nameB);
    if (!priceText.empty()) {
        box(x, priceT, priceR + dx, bottom);
        fill(x + 1, priceT, std::min(priceR - x0, nameR - x0) - 2, 1, {0, 0, 0, 255});
        drawText(priceText, x + 3, priceT, &c);
    }
    drawText(name, x + 3, y0, &c);
}

// FUN_0044b5c0 (ToolTips, called from the main loop while no page is open) + FUN_00423b40.
//  - Sidebar strips, at once: building strip -> building group name + GetBuildingCost; unit
//    strip -> unit group name + w__GetUnitCost; starport page -> unit name + current price;
//    upgrade page -> building group name + the template's upgrade cost for the current level
//    (no price when the group cannot be upgraded). Anchored at the icon centre, kept left of
//    the icon column (left strip: SideBarPanelLeftUIPosX, right strip: ...RightUIPosX).
//  - After the mouse rested 18 frames (DAT_0051756c > 0x11; counted only while it does not
//    move, reset when a tooltip disappears): REPAIR / SELLBUILDING / MapToggle (not in the
//    campaign, GameType != 0) / GUARD / RETREAT over the shown buttons, ending at the button's
//    x at y 160; the power bar "Power %d%%" at the mouse, ending at the bar; on the battlefield
//    at the mouse + 14: "Unrevealed Terrain" on fully shrouded cells, a unit (own / allied: its
//    group name; enemy / neutral: "Enemy Unit" / "Neutral Unit", none for cloaked enemies), a
//    building (own / allied: group name, else "Enemy Structure" / "Neutral Structure"), else
//    the tile's tooltip (TILEATR text id, e.g. "Spice") in grey.
//  Colours: _tooltipcolor_yellow 0xe6e61e, _tooltipcolor_gray 0x969696 (Game_Init 0x4612a0).
void ClassicSidebar::drawTooltip(const game::World& w, int side, float camX, float camY, float mouseX, float mouseY) {
    const SDL_Color yellow{0xe6, 0xe6, 0x1e, 255}, grey{0x96, 0x96, 0x96, 255};
    const int gx = int(mouseX) / scale_, gy = int(mouseY) / scale_;
    // Rest counter (world position of the mouse, so scrolling counts as moving).
    const int wx = gx + int(camX), wy = gy + int(camY);
    const Uint64 now = SDL_GetTicks();
    const int steps = tipAt_ ? int((now - tipAt_) / 16) : 0;
    if (steps > 0 || !tipAt_) tipAt_ = tipAt_ ? tipAt_ + Uint64(steps) * 16 : now;
    const bool rested = (wx == tipX_ && wy == tipY_) || tipForce_;
    if (rested) tipRest_ += steps;
    if (tipForce_) tipRest_ = std::max(tipRest_, 0x12);
    tipForce_ = false;
    tipX_ = wx;
    tipY_ = wy;
    bool shown = false;
    const auto& T = assets_.templates.bin;
    auto text = [&](const std::string& key) -> std::string {
        if (assets_.text)
            if (const std::string* t = assets_.text->findNoCase(key)) return *t;  // GetTextID
        return key;
    };
    auto textId = [&](int id) -> std::string {
        if (!assets_.text || id < 0 || id >= int(assets_.text->entries.size())) return {};
        return assets_.text->entries[std::size_t(id)].second;
    };
    auto unitName = [&](int type) { return text(T.unitGroupNames[T.units[type].unitGroup]); };
    auto buildingName = [&](int type) { return text(T.buildingGroupNames[T.buildings[type].buildingGroup]); };
    auto show = [&](const std::string& name, int price, int x0, int y0, int limitX, SDL_Color c) {
        if (name.empty()) return;
        drawTooltipBox(name, price, x0, y0, limitX, gh_, c);
        shown = true;
    };
    const int bfW = gw_ - kSidebarW;
    const int lx = kPanelLeftX + dx_, rx = kPanelRightX + dx_;
    const int stripH = kIconH * iconCount_;
    if (gx >= 1 && gx < bfW && gy > kOptionsBarH && gy < gh_) {
        if (rested && tipRest_ >= 0x12) {
            const int tx = wx >> 5, ty = (wy - kOptionsBarH) >> 5;
            if (tx >= 0 && ty >= 0 && tx < w.width() && ty < w.height()) {
                const int ax = gx + 14, ay = gy + 14;
                int s = -1, idx = -1;
                const std::uint8_t sh = w.cell(tx, ty).shroud & 0xF;
                if (sh == 1 && shroudShown) {
                    show(text("Unrevealed"), -1, ax, ay, bfW, yellow);
                } else if (const game::Unit* u = w.unitAt(wx, wy - kOptionsBarH, s, idx)) {
                    const std::uint8_t d = w.diplomacy(side, s);
                    if (d == 0) show(unitName(u->type), -1, ax, ay, bfW, yellow);
                    else if (!(u->flags & game::unit_flags::kCloaked))
                        show(text(d == 2 ? "NeutralUnit" : "EnemyUnit"), -1, ax, ay, bfW, yellow);
                } else if (const game::Building* b = w.buildingAt(tx, ty, s, idx)) {
                    const std::uint8_t d = w.diplomacy(side, s);
                    if (d == 0) show(buildingName(b->type), -1, ax, ay, bfW, yellow);
                    else show(text(d == 2 ? "NeutralStructure" : "EnemyStructure"), -1, ax, ay, bfW, yellow);
                } else if (assets_.tileAtr) {
                    const unsigned tile = w.cell(tx, ty).tile;
                    if (tile < unsigned(formats::kTileAtrEntries))
                        show(textId(assets_.tileAtr->tooltips[tile]), -1, ax, ay, bfW, grey);
                }
            }
        }
    } else if (gx > lx && gx < rx + kIconW && gy > kPanelsY && gy < kPanelsY + stripH) {
        const int col = (gx - lx) / kIconW, row = (gy - kPanelsY) / kIconH;
        const int ax = kIconW / 2 + lx + kIconW * col, ay = kIconH / 2 + kPanelsY + kIconH * row;
        if (page_ == Page::Starport) {
            const int slot = std::min(col, 1) + row * 2;
            const auto list = w.starportUnits(side);
            if (slot < int(list.size()) && list[std::size_t(slot)].type >= 0)
                show(unitName(list[std::size_t(slot)].type), list[std::size_t(slot)].price, ax, ay, (slot & 1) ? rx : lx,
                     yellow);
        } else if (page_ == Page::Upgrade) {
            const int slot = std::min(std::min(col, 1) + row * 2, 7);
            auto list = w.upgradableBuildings(side);  // all pass CanSideUpgradeBuildingGroup
            const std::size_t upgradable = list.size();
            const game::QueueState q = w.upgradeQueue(side);
            if (q.active && std::none_of(list.begin(), list.end(), [&](const game::BuildOption& o) { return o.type == q.type; }))
                list.push_back({q.type, T.buildings[q.type].buildingGroup, q.cost, 0, 0});
            if (slot < int(list.size())) {
                const auto& o = list[std::size_t(slot)];
                const int lvl = std::clamp(int(w.sides()[side].upgrades[std::size_t(o.group)]), 0, 2);
                const int price = std::size_t(slot) < upgradable ? int(T.buildings[o.type].costUpgrade[lvl]) : 0;
                show(buildingName(o.type), price, ax, ay, (slot & 1) ? rx : lx, yellow);
            }
        } else if (gx < lx + kIconW) {
            const auto list = w.buildableBuildings(side);
            const int idx = scroll_[0] + row;
            if (idx >= 0 && idx < int(list.size()))
                show(buildingName(list[std::size_t(idx)].type), list[std::size_t(idx)].cost, ax, ay, lx, yellow);
        } else if (gx > rx) {
            const auto list = w.buildableUnits(side);
            const int idx = scroll_[1] + row;
            if (idx >= 0 && idx < int(list.size()))
                show(unitName(list[std::size_t(idx)].type), list[std::size_t(idx)].cost, ax, ay, rx, yellow);
        }
    } else if (rested && tipRest_ >= 0x12) {
        // HandleSidebarButton(n, 0): only buttons in state 1 (shown, not pressed).
        auto over = [&](int i) { return buttons_[i].state == 1 && buttonAt(float(gx), float(gy)) == i; };
        const char* key = nullptr;
        int btn = -1;
        if (over(kRepair)) { key = "Repair"; btn = kRepair; }
        else if (over(kSell)) { key = "SellBuilding"; btn = kSell; }
        else if (skirmish && over(kOrb)) { key = "MapToggle"; btn = kOrb; }
        else if (over(kGuard)) { key = "Guard"; btn = kGuard; }
        else if (over(kRetreat)) { key = "Retreat"; btn = kRetreat; }
        if (key) {
            show(text(key), -1, bx(btn), 0xa0, bx(btn), yellow);
        } else {
            const int px = kPowerX + dx_, py = kPowerY + extraRows_;
            const Tex& bar = uiFrame(kFramePowerGrey);
            if (gx >= px && gy >= py && gx <= px + bar.w && gy <= gh_) {
                const std::string p = text("Power") + " " + std::to_string(w.economy(side).powerPercent) + "%";
                show(p, -1, gx, gy, px, yellow);
            }
        }
    }
    if (shown) tipVisible_ = true;
    else if (tipVisible_) {
        tipVisible_ = false;
        tipRest_ = 0;
    }
}

// FUN_00427cc0: first the 16bpp backgrounds of the hidden buttons, then the shown (state 1)
// or pressed (state 2) images. The original only redraws on a state change; the result on
// screen is the same.
void ClassicSidebar::drawButtons() {
    for (int i = 0; i < 15; ++i)
        if (buttons_[i].state == 0) blit(uiFrame(i * 3 + 2), bx(i), by(i));
    for (int i = 0; i < 15; ++i)
        if (buttons_[i].state != 0) blit(uiFrame(i * 3 + buttons_[i].state - 1), bx(i), by(i));
}

void ClassicSidebar::draw(const game::World& w, int side, float camX, float camY, const std::vector<std::string>& messages,
                          int cursorMode, float mouseX, float mouseY) {
    const Uint64 now = SDL_GetTicks();
    if (now - pulseAt_ >= 16) {  // one step per original frame at 60 fps
        pulse_ = (pulse_ + int((now - pulseAt_) / 16)) % 41;
        pulseAt_ = now;
    }
    updateButtons(w, side);
    SDL_SetRenderViewport(r_, nullptr);
    SDL_SetRenderClipRect(r_, nullptr);
    SDL_SetRenderScale(r_, float(scale_), float(scale_));
    drawBackdrop();
    drawRadar(w, side, camX, camY);
    drawPower(w, side);
    drawCredits(w, side);
    if (page_ == Page::Normal) drawStrips(w, side);
    else if (page_ == Page::Starport) drawStarportPage(w, side);
    else drawUpgradePage(w, side);
    drawButtons();
    if (tooltips) drawTooltip(w, side, camX, camY, mouseX, mouseY);
    // Display__DrawMissionTimer 0x4255e0: timer, then the message lines (newest first).
    if (w.missionTimer() >= 0) {
        const int t = w.missionTimer();
        char buf[32];
        std::snprintf(buf, sizeof buf, "%02d:%02d", t / 0x5dc, (t / 0x19) % 0x3c);
        drawText(buf, kTimerX + dx_, kOptionsBarH + 4);
    }
    int y = kOptionsBarH + 4;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it, y += 0x14) drawText(*it, 8, y);
    if (cursorMode == 2 || cursorMode == 3) {
        const int mx = int(mouseX) / scale_ + 10, my = int(mouseY) / scale_ + 10;
        if (cursorMode == 2) blit(icon(kDataFrameRepairIcon, -1, false), mx, my);
        else drawText("$", mx, my);
    }
    SDL_SetRenderScale(r_, 1.0f, 1.0f);
}

bool ClassicSidebar::radarCell(float gx, float gy, const game::World& w, int& cx, int& cy) const {
    const int mw = w.width(), mh = w.height();
    const int ox = (kRadarSize - mw) / 2, oy = (kRadarSize - mh) / 2;
    const int rx = kMiniMapX + dx_, ry = kRadarY;
    const int x = int(gx), y = int(gy);
    // 0x4433a0: strictly inside (MiniMapUIPosX + ox, MiniMapUIPosX - ox + 128).
    if (!(rx + ox < x && x < rx - ox + kRadarSize && ry + oy < y && y < ry - oy + kRadarSize)) return false;
    cx = x - rx - ox;
    cy = y - ry - oy;
    return cx >= 0 && cy >= 0 && cx < mw && cy < mh;
}

bool ClassicSidebar::radarToWorld(float sx, float sy, const game::World& w, int side, int& wx, int& wy) const {
    int cx = 0, cy = 0;
    if (!radarOn(w, side) || !radarCell(sx / scale_, sy / scale_, w, cx, cy)) return false;
    wx = cx * 32;
    wy = cy * 32;
    return true;
}

void ClassicSidebar::scrollStrip(int strip, int delta, bool page, const game::World& w, int side) {
    const int count = stripCount(strip, w, side);
    int& s = scroll_[strip];
    if (delta > 0) {
        if (count <= iconCount_ + s) { sounds_.push_back("S_ENDSCROLL"); return; }
        s = page ? std::min(s + iconCount_, count - iconCount_) : s + 1;
    } else {
        if (s < 1) { sounds_.push_back("S_ENDSCROLL"); return; }
        s = page ? std::max(0, s - iconCount_) : s - 1;
    }
    sounds_.push_back("S_ICONSCROLL");
}

ClassicAction ClassicSidebar::mouseDown(float sx, float sy, int button, const game::World& w, int side, bool unitsSelected) {
    using A = ClassicAction;
    const float gx = sx / scale_, gy = sy / scale_;
    updateButtons(w, side);
    const int b = buttonAt(gx, gy);
    if (button == SDL_BUTTON_LEFT && b >= 0) {
        pressed_ = b;
        buttons_[b].state = 2;
        switch (b) {
        case kLUp: scrollStrip(0, -1, false, w, side); break;
        case kLDown: scrollStrip(0, 1, false, w, side); break;
        case kRUp: scrollStrip(1, -1, false, w, side); break;
        case kRDown: scrollStrip(1, 1, false, w, side); break;
        case kRepair: sounds_.push_back("BUTTON"); return {A::RepairMode};
        case kSell: sounds_.push_back("BUTTON"); return {A::SellMode};
        case kGuard: return {A::Guard};
        case kRetreat: return {A::Retreat};
        case kUpgrade:
            if (page_ == Page::Starport) {  // leaving the starport page cancels the picks
                page_ = Page::Upgrade;
                return w.starportPickedTotal(side) ? A{A::StarportCancel} : A{};
            }
            page_ = Page::Upgrade;
            break;
        case kMainUpgrade: page_ = Page::Normal; break;
        case kStarport: page_ = Page::Starport; break;
        case kMainStarport:
            page_ = Page::Normal;
            return w.starportPickedTotal(side) ? A{A::StarportCancel} : A{};
        case kPurchase:
            page_ = Page::Normal;
            return w.starportPickedTotal(side) ? A{A::StarportPurchase} : A{};
        case kOptions: return {A::Options};
        default: break;  // orb (MP score view): not implemented
        }
        return {};
    }
    if (button == SDL_BUTTON_RIGHT && b >= kLUp && b <= kRDown) {  // right click: a page
        pressed_ = b;
        scrollStrip(b < kRUp ? 0 : 1, (b == kLDown || b == kRDown) ? 1 : -1, true, w, side);
        return {};
    }
    // Icon strips (left: x < SideBarPanelLeftUIPosX + width, else right).
    const int lx = kPanelLeftX + dx_, rx = kPanelRightX + dx_;
    if (gx > lx && gx < rx + kIconW && gy > kPanelsY && gy < kPanelsY + kIconH * iconCount_) {
        const int row = int(gy - kPanelsY) / kIconH;
        const bool left = gx < lx + kIconW;
        const auto& T = assets_.templates.bin;
        if (page_ == Page::Starport) {  // 0x4433a0: only while no frigate is on its way
            if (row >= 4 || w.starportBusy(side)) return {};
            const auto list = w.starportUnits(side);
            const int slot = row * 2 + (left ? 0 : 1);
            if (slot >= int(list.size()) || list[std::size_t(slot)].type < 0) return {};
            return {button == SDL_BUTTON_RIGHT ? A::StarportUnpick : A::StarportPick, list[std::size_t(slot)].type};
        }
        if (page_ == Page::Upgrade) {
            if (row >= 4) return {};
            auto list = w.upgradableBuildings(side);
            const int slot = row * 2 + (left ? 0 : 1);
            const game::QueueState q = w.upgradeQueue(side);
            if (button == SDL_BUTTON_RIGHT) {
                // GenerateUpgradeCancelOrder(the slot's building type); ModelUpgradeCancel only
                // acts when it is the type being upgraded. The page shows that one last when it
                // is no longer upgradable (drawUpgradePage).
                if (q.active && std::none_of(list.begin(), list.end(), [&](const game::BuildOption& o) { return o.type == q.type; }))
                    list.push_back({q.type, T.buildings[q.type].buildingGroup, q.cost, 0, 0});
                if (slot < int(list.size())) return {A::CancelUpgrade, list[std::size_t(slot)].type};
                return {};
            }
            if (slot < int(list.size())) return {A::Upgrade, list[std::size_t(slot)].group};
            return {};
        }
        if (left) {
            const auto list = w.buildableBuildings(side);
            const int idx = scroll_[0] + row;
            const game::QueueState q = w.buildingQueue(side);
            if (button == SDL_BUTTON_RIGHT)
                return idx < int(list.size()) ? A{A::CancelBuilding, list[std::size_t(idx)].type} : A{};
            if (q.active && q.ready) return {A::BeginPlacement, q.type};
            return idx < int(list.size()) ? A{A::BuildBuilding, list[std::size_t(idx)].type} : A{};
        }
        const auto list = w.buildableUnits(side);
        const int idx = scroll_[1] + row;
        if (idx >= int(list.size())) return {};
        const int type = list[std::size_t(idx)].type;
        const int beh = T.units[type].specialBehavior;
        if (button == SDL_BUTTON_RIGHT) return specialUnit(beh) ? A{} : A{A::CancelUnit, type};
        if (specialUnit(beh)) {
            const auto q = w.unitQueueFor(side, type);
            if (!q.active || !q.ready) return {};
            if (beh == 9 || beh == 10) return {A::TargetWeapon, beh};
            return {A::SpecialWeapon, beh};
        }
        return {A::BuildUnit, type};
    }
    int cx = 0, cy = 0;
    if (radarCell(gx, gy, w, cx, cy)) {
        if (button == SDL_BUTTON_RIGHT) return {A::Deselect};
        if (button == SDL_BUTTON_LEFT && radarOn(w, side)) {
            if (unitsSelected) return {A::RadarMove, -1, cx, cy};
            return {A::RadarCentre, -1, cx * 32, cy * 32};
        }
    }
    return {};
}

void ClassicSidebar::mouseUp(int) { pressed_ = -1; }

void ClassicSidebar::wheel(float sx, float sy, float dy, const game::World& w, int side) {
    const float gx = sx / scale_, gy = sy / scale_;
    const int lx = kPanelLeftX + dx_, rx = kPanelRightX + dx_;
    if (page_ != Page::Normal || dy == 0) return;
    if (!(gx > lx && gx < rx + kIconW && gy > kPanelsY && gy < kPanelsY + kIconH * iconCount_)) return;
    scrollStrip(gx < lx + kIconW ? 0 : 1, dy < 0 ? 1 : -1, false, w, side);
}

}  // namespace d2k::render

#include "platform/cheat_menu.hpp"

#include "platform/ui.hpp"
#include "render/sprites.hpp"
#include "render/ui_text.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::platform {

namespace {

using render::tr;
using render::trf;

constexpr int kKindCredits = 1, kKindTech = 2, kKindBuilding = 3, kKindUnit = 4, kKindInstant = 5;
int key(int kind, int side, int type = 0) { return kind * 1000 + side * 100 + type; }

bool inside(const SDL_FRect& r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

const char* const kTabNames[] = {"Money", "Tech level", "Build list", "Delivery", "Reinforce", "Spawn", "Toggles"};
const char* const kTabIds[] = {"money", "tech", "build", "deliver", "reinforce", "spawn", "toggles"};
// Reinforcement delay presets (seconds of game time).
constexpr int kDelays[] = {0, 5, 10, 20, 30, 45, 60, 90, 120, 180, 300, 600, 900, 1200, 1800};
const char* const kEdgeNames[] = {"Nearest", "North", "East", "South", "West"};

Colour rgba(formats::Rgba c) {
    // Rgba is 0xAABBGGRR.
    return Colour{static_cast<unsigned char>(c & 0xff), static_cast<unsigned char>((c >> 8) & 0xff),
                  static_cast<unsigned char>((c >> 16) & 0xff), 255};
}

}  // namespace

const char* CheatMenu::houseName(int house) {
    static const char* const kNames[8] = {"Atreides", "Harkonnen", "Ordos",       "Emperor",
                                          "Fremen",   "Smugglers", "Mercenaries", "Sandworm"};
    return tr(house >= 0 && house < 8 ? kNames[house] : "Neutral");
}

std::string CheatMenu::sideLabel(const game::World& w, int side, int playerSide) const {
    const game::Side& s = w.sides()[static_cast<std::size_t>(side)];
    int units = 0, buildings = 0;
    for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) ++units;
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) ++buildings;
    std::string l = std::to_string(side) + ": " + houseName(s.house);
    if (side == playerSide) l += tr(" (you)");
    else if (w.ai(side).enabled()) l += tr(" (AI)");
    if (units + buildings == 0) l += tr(" - empty");
    else l += trf(" - %d bld / %d units", buildings, units);
    return l;
}

int CheatMenu::colourOf(const game::World& w, int side) const {
    const int c = w.playerColor(side);
    if (c >= 0) return c;
    return std::clamp(w.sides()[static_cast<std::size_t>(side)].house, 0, 7);
}

int CheatMenu::shown(int k, int worldValue) {
    auto it = pending_.find(k);
    if (it == pending_.end()) return worldValue;
    if (it->second == worldValue) {
        pending_.erase(it);
        return worldValue;
    }
    return it->second;
}

void CheatMenu::open(const game::World& w, int playerSide) {
    if (side_ < 0 || side_ >= game::kMaxSides) side_ = playerSide;
    if (spawn_.side < 0 || spawn_.side >= game::kMaxSides) spawn_.side = playerSide;
    deliverCount_.resize(formats::kMaxUnitTypes, 0);
    reinfCount_.resize(formats::kMaxUnitTypes, 0);
    creditsSlider_ = std::clamp(w.credits(side_) / 100, 0, 2000);
    resetFocus_ = true;
    placing_ = false;
    placeReinforce_ = false;
    status_.clear();
}

void CheatMenu::setTab(const std::string& name) {
    for (int i = 0; i < kTabCount; ++i)
        if (name == kTabIds[i]) tab_ = i;
    if (name == "spawn-buildings") {  // (screenshots: the Spawn section in building mode)
        tab_ = kSpawn;
        spawnBuildings_ = true;
    }
    resetFocus_ = true;
    sectionScroll_ = 0;
}

std::string CheatMenu::name(const Cell& c) const {
    std::string n = c.building ? t_.buildingName(c.type) : t_.unitName(c.type);
    return n.empty() ? trf(c.building ? "Building %d" : "Unit %d", c.type) : n;
}

std::vector<CheatMenu::Cell> CheatMenu::unitCells(bool deliverable) const {
    std::vector<Cell> v;
    for (int i = 0; i < t_.bin.unitCount && i < formats::kMaxUnitTypes; ++i) {
        if (t_.unitName(i).empty()) continue;
        const int beh = t_.bin.units[i].specialBehavior;
        if (deliverable && (beh == game::behaviour::kFrigate || beh == game::behaviour::kDeathHand)) continue;
        v.push_back({i, false});
    }
    return v;
}

std::vector<CheatMenu::Cell> CheatMenu::buildingCells() const {
    std::vector<Cell> v;
    for (int i = 0; i < t_.bin.buildingCount && i < formats::kMaxBuildingTypes; ++i)
        if (!t_.buildingName(i).empty()) v.push_back({i, true});
    return v;
}

template <class Badge, class Tint>
int CheatMenu::grid(Ui& ui, const SDL_FRect& area, const std::vector<Cell>& cells, float& scroll,
                    render::SpriteCache* icons, int house, Badge badge, Tint tint, int& hovered) {
    SDL_Renderer* r = ui.renderer();
    hovered = -1;
    const float s = ui.bodyScale;
    const float cw = std::round(std::max(64.0f, area.w / std::max(1.0f, std::floor(area.w / (70 * std::max(1.0f, s / 1.5f)))))),
                ch = std::round(cw * 0.8f);
    const int cols = std::max(1, int(area.w / cw));
    const int rows = (int(cells.size()) + cols - 1) / cols;
    const float maxScroll = std::max(0.0f, rows * ch - area.h);
    if (inside(area, ui.mouseX(), ui.mouseY())) scroll -= ui.takeWheel() * ch;
    scroll -= ui.touchScroll(area);  // touch: a vertical drag scrolls the grid
    scroll = std::clamp(scroll, 0.0f, maxScroll);
    ui.fill(area, Colour{0, 0, 0, 110});
    SDL_Rect prevClip{};
    const bool hadClip = SDL_RenderClipEnabled(r);
    if (hadClip) SDL_GetRenderClipRect(r, &prevClip);
    // Clip in pixels of the current target (the Ui draws in the same coordinates).
    const SDL_Rect clip{int(area.x), int(area.y), int(area.w), int(area.h)};
    SDL_SetRenderClipRect(r, &clip);
    int clicked = -1;
    const bool mouseIn = inside(area, ui.mouseX(), ui.mouseY());
    for (int i = 0; i < int(cells.size()); ++i) {
        const SDL_FRect c{area.x + (i % cols) * cw + 2, area.y + (i / cols) * ch - scroll + 2, cw - 4, ch - 4};
        if (c.y + c.h < area.y || c.y > area.y + area.h) continue;
        const Cell& cell = cells[static_cast<std::size_t>(i)];
        const int frame = cell.building ? t_.firstBuildingIconFrame + cell.type : t_.firstUnitIconFrame + cell.type;
        ui.fill(c, Colour{24, 18, 12, 230});
        if (const render::Sprite* sp = icons ? icons->get(frame, house) : nullptr) {
            const float k = std::min(c.w / sp->width, c.h / sp->height);
            const SDL_FRect d{c.x + (c.w - sp->width * k) / 2, c.y + (c.h - sp->height * k) / 2, sp->width * k,
                              sp->height * k};
            SDL_ScaleMode prev = SDL_SCALEMODE_NEAREST;
            SDL_GetTextureScaleMode(sp->texture, &prev);
            SDL_SetTextureScaleMode(sp->texture, SDL_SCALEMODE_LINEAR);
            SDL_RenderTexture(r, sp->texture, nullptr, &d);
            SDL_SetTextureScaleMode(sp->texture, prev);
        } else {
            ui.text(ui.ellipsize(name(cell), c.w - 6, 1), c.x + 3, c.y + 3, theme::kDim, 1, Align::Left, false);
        }
        const Colour tc = tint(i);
        if (tc.a) {
            ui.outline(c, tc, std::max(2.0f, s));
            ui.fill(c, Colour{tc.r, tc.g, tc.b, 40});
        }
        const std::string b = badge(i);
        if (!b.empty()) {
            const float bs = std::max(1.0f, ui.bodyScale - 1);
            const float bw = ui.textWidth(b, bs) + 8 * bs, bh = ui.textHeight(bs) + 2 * bs;
            const SDL_FRect br{c.x + c.w - bw - 1, c.y + c.h - bh - 1, bw, bh};
            ui.fill(br, Colour{0, 0, 0, 200});
            ui.text(b, br.x + 4 * bs, br.y + bs, tc.a ? tc : theme::kText, bs, Align::Left, false);
        }
        bool hot = false;
        if (ui.hotspot(c, &hot) && (mouseIn || !ui.clicked())) clicked = i;
        if (hot && (mouseIn || !inside(c, ui.mouseX(), ui.mouseY()))) hovered = i;
    }
    if (hadClip) SDL_SetRenderClipRect(r, &prevClip);
    else SDL_SetRenderClipRect(r, nullptr);
    if (maxScroll > 0) {  // scroll bar
        const float bh = area.h * area.h / (rows * ch);
        ui.fill(SDL_FRect{area.x + area.w - 4, area.y + (area.h - bh) * scroll / maxScroll, 4, bh}, theme::kEdge);
    }
    return clicked;
}

CheatMenu::Result CheatMenu::frame(Ui& ui, game::World& w, int playerSide, render::SpriteCache* icons, int outW,
                                   int outH) {
    if (side_ < 0) open(w, playerSide);
    if (resetFocus_) {
        ui.resetFocus(0);
        resetFocus_ = false;
    }
    const float s = Ui::bodyScaleFor(outW, outH);
    ui.textScale = Ui::labelScaleFor(outW, outH);
    ui.bodyScale = ui.textScale;
    ui.fill(SDL_FRect{0, 0, float(outW), float(outH)}, Colour{0, 0, 0, 150});
    const float bw = std::min(1040 * s, outW - 16.0f), bh = std::min(720 * s, outH - 16.0f);
    const SDL_FRect box{std::round(outW / 2.0f - bw / 2), std::round(outH / 2.0f - bh / 2), bw, bh};
    ui.panel(box);
    const float ts = ui.textScale + 1, pad = 18 * s, rowH = std::round(ui.textHeight(ui.textScale) + 14 * s);
    ui.text(tr("CHEATS"), box.x + pad, box.y + 14 * s, theme::kTitle, ts);
    if (w.cheatsUsed()) ui.text(tr("cheats used in this game"), box.x + bw - pad, box.y + 18 * s, theme::kDim, ui.textScale, Align::Right);

    // Side the page acts on (all sections).
    float y = box.y + 14 * s + ui.textHeight(ts) + 12 * s;
    std::vector<std::string> sides;
    for (int i = 0; i < game::kMaxSides; ++i) sides.push_back(sideLabel(w, i, playerSide));
    const SDL_FRect sideRow{box.x + pad, y, bw - 2 * pad - rowH - 10 * s, rowH};
    ui.choice(sideRow, tr("Side"), side_, sides);
    if (colours_) {
        const Colour c = rgba((*colours_)[static_cast<std::size_t>(colourOf(w, side_))][8]);
        ui.fill(SDL_FRect{sideRow.x + sideRow.w + 10 * s, y + 4 * s, rowH - 8 * s, rowH - 8 * s}, c);
    }
    y += rowH + 10 * s;
    // Tabs.
    const float tw = (bw - 2 * pad - (int(kTabCount) - 1) * 6 * s) / int(kTabCount);
    // One label size for all tabs: the window's label scale, one step smaller when a name does not fit.
    float tabScale = 1;
    for (const char* n : kTabNames)
        if (ui.textScale > 1 && ui.textWidth(tr(n), ui.textScale) > tw - 12 * s) tabScale = (ui.textScale - 1) / ui.textScale;
    for (int i = 0; i < kTabCount; ++i) {
        const SDL_FRect r{box.x + pad + i * (tw + 6 * s), y, tw, rowH};
        const std::string label = ui.ellipsize(tr(kTabNames[i]), tw - 8 * s, ui.textScale * tabScale);
        if (ui.button(r, label, i == tab_ ? ButtonStyle::Primary : ButtonStyle::Normal, true, tabScale) && tab_ != i) {
            tab_ = i;
            status_.clear();
            sectionScroll_ = sectionContentH_ = 0;
        }
    }
    y += rowH + 14 * s;
    const float footH = rowH + 2 * pad;
    const SDL_FRect area{box.x + pad, y, bw - 2 * pad, box.y + bh - footH - y};
    Result res = Result::None;
    // Sections without a grid scroll when the window is too small for them (content height
    // measured in the previous frame).
    auto scrolled = [&](auto&& draw) {
        const float y0 = ui.beginScroll(area, sectionScroll_, sectionContentH_, std::max(4.0f, 5 * s));
        sectionContentH_ = draw(SDL_FRect{area.x, y0, area.w, std::max(area.h, sectionContentH_)});
        ui.endScroll();
    };
    switch (tab_) {
    case kMoney: scrolled([&](const SDL_FRect& a) { return drawMoney(ui, w, a, s); }); break;
    case kTech: scrolled([&](const SDL_FRect& a) { return drawTech(ui, w, a, s); }); break;
    case kBuild: drawBuild(ui, w, area, s, icons); break;
    case kDeliver: drawDeliver(ui, w, area, s, icons); break;
    case kReinforce:
        if (drawReinforce(ui, w, area, s, icons)) res = Result::Place;
        break;
    case kSpawn:
        if (drawSpawn(ui, w, area, s, icons)) {
            placeReinforce_ = false;
            res = Result::Place;
        }
        break;
    case kToggles: scrolled([&](const SDL_FRect& a) { return drawToggles(ui, w, a, s); }); break;
    }
    // Footer.
    const float fy = box.y + bh - rowH - pad;
    if (!status_.empty())
        ui.text(ui.ellipsize(status_, bw - 2 * pad - 220 * s, ui.textScale), box.x + pad, fy + (rowH - ui.textHeight(ui.textScale)) / 2,
                theme::kAccent, ui.textScale);
    if (ui.button(SDL_FRect{box.x + bw - pad - 200 * s, fy, 200 * s, rowH}, tr("Close"), ButtonStyle::Primary) || ui.takeBack())
        res = Result::Close;
    if (res == Result::Place) placing_ = true;
    return res;
}

float CheatMenu::drawMoney(Ui& ui, game::World& w, const SDL_FRect& a, float s) {
    const float rowH = std::round(ui.textHeight(ui.textScale) + 14 * s);
    const int real = w.credits(side_);
    const int credits = shown(key(kKindCredits, side_), real);
    float y = a.y;
    std::string line = trf("Credits: %d", credits);
    if (credits != real) line += trf("   (now %d, applied when the game runs)", real);
    ui.text(line, a.x, y, theme::kText, ui.textScale + 1);
    y += ui.textHeight(ui.textScale + 1) + 18 * s;
    const int presets[] = {1000, 5000, 10000, 50000, 100000};
    const float pw = std::min(std::max(150 * s, ui.textWidth("+100000", ui.textScale) + 24 * s), (a.w - 5 * 8 * s) / 6);
    for (int i = 0; i < 5; ++i) {
        const SDL_FRect r{a.x + i * (pw + 8 * s), y, pw, rowH};
        if (ui.button(r, "+" + std::to_string(presets[i]))) {
            w.orderCheatAddCredits(side_, presets[i]);
            pending_[key(kKindCredits, side_)] = credits + presets[i];
            status_ = trf("+%d credits", presets[i]);
        }
    }
    if (ui.button(SDL_FRect{a.x + 5 * (pw + 8 * s), y, pw, rowH}, tr("Zero"))) {
        w.orderCheatSetCredits(side_, 0);
        pending_[key(kKindCredits, side_)] = 0;
        status_ = trf("Credits set to %d", 0);
    }
    y += rowH + 24 * s;
    ui.text(tr("Exact amount"), a.x, y, theme::kDim, ui.textScale);
    y += ui.textHeight(ui.textScale) + 8 * s;
    const float setW = std::max(180 * s, ui.textWidth(tr("Set exactly"), ui.textScale) + 30 * s);
    const float sw = std::min(620 * s + ui.textWidth(tr("Credits"), ui.textScale), a.w - setW - 12 * s);
    ui.slider(SDL_FRect{a.x, y, sw, rowH}, tr("Credits"), creditsSlider_, 0, 2000, std::to_string(creditsSlider_ * 100));
    if (ui.button(SDL_FRect{a.x + sw + 12 * s, y, setW, rowH}, tr("Set exactly"))) {
        w.orderCheatSetCredits(side_, creditsSlider_ * 100);
        pending_[key(kKindCredits, side_)] = creditsSlider_ * 100;
        status_ = trf("Credits set to %d", creditsSlider_ * 100);
    }
    y += rowH + 18 * s;
    y += ui.wrapped(tr("Credits are cash plus stored spice. Setting an exact amount also drops pending (not yet "
                       "counted) payouts. Left/Right on the slider step by 100."),
                    SDL_FRect{a.x, y, a.w, a.y + a.h - y}, theme::kDim, ui.bodyScale);
    return y - a.y;
}

float CheatMenu::drawTech(Ui& ui, game::World& w, const SDL_FRect& a, float s) {
    const float rowH = std::round(ui.textHeight(ui.textScale) + 14 * s);
    const int real = w.sides()[static_cast<std::size_t>(side_)].techLevel;
    const int tech = shown(key(kKindTech, side_), real);
    float y = a.y;
    ui.text(trf("Tech level: %d", tech), a.x, y, theme::kText, ui.textScale + 1);
    y += ui.textHeight(ui.textScale + 1) + 18 * s;
    const float bw = std::min(90 * s, (a.w - 8 * 8 * s) / 9);
    for (int lvl = 1; lvl <= 9; ++lvl) {
        const SDL_FRect r{a.x + (lvl - 1) * (bw + 8 * s), y, bw, rowH * 1.4f};
        if (ui.button(r, std::to_string(lvl), lvl == tech ? ButtonStyle::Primary : ButtonStyle::Normal)) {
            w.orderCheatTechLevel(side_, lvl);
            pending_[key(kKindTech, side_)] = lvl;
            status_ = trf("Tech level %d", lvl);
        }
    }
    y += rowH * 1.4f + 18 * s;
    return y - a.y + ui.wrapped(tr("The tech level gates sidebar options and upgrades (prerequisite buildings are still needed). "
                  "Campaign missions use 1-8; level 9 also unlocks construction yards. Use Build list to force "
                  "single buildings and units on or off."),
               SDL_FRect{a.x, y, a.w, a.y + a.h - y}, theme::kDim, ui.bodyScale);
}

void CheatMenu::drawBuild(Ui& ui, game::World& w, const SDL_FRect& a, float s, render::SpriteCache* icons) {
    const float rowH = std::round(ui.textHeight(ui.textScale) + 14 * s);
    bool& units = buildUnits_;
    const float bw = 180 * s;
    if (ui.button(SDL_FRect{a.x, a.y, bw, rowH}, tr("Buildings"), !units ? ButtonStyle::Primary : ButtonStyle::Normal)) units = false;
    if (ui.button(SDL_FRect{a.x + bw + 8 * s, a.y, bw, rowH}, tr("Units"), units ? ButtonStyle::Primary : ButtonStyle::Normal)) units = true;
    const float rw = std::max(bw, ui.textWidth(tr("Reset all"), ui.textScale) + 40 * s);
    if (ui.button(SDL_FRect{a.x + a.w - rw, a.y, rw, rowH}, tr("Reset all"))) {
        w.orderCheatResetOverrides(side_);
        for (int t = 0; t < 100; ++t) {
            pending_[key(kKindBuilding, side_, t)] = 0;
            pending_[key(kKindUnit, side_, t)] = 0;
        }
        status_ = tr("All overrides back to the original rules");
    }
    const float ly = a.y + rowH + 8 * s;
    // The legend wraps (up to two lines) so translations keep the whole sentence.
    const std::string legend = tr("Click: Default -> Allowed (green) -> Blocked (red); blue = on the sidebar now. Allowed ignores "
                                  "prerequisites, tech level and house.");
    const float lh = std::min(ui.wrapped(legend, SDL_FRect{a.x, ly, a.w, 1e6f}, theme::kDim, ui.bodyScale, 0, false),
                              2 * (ui.textHeight(ui.bodyScale) + 3 * ui.bodyScale));
    ui.wrapped(legend, SDL_FRect{a.x, ly, a.w, lh}, theme::kDim, ui.bodyScale);
    const SDL_FRect g{a.x, ly + lh + 8 * s, a.w, a.y + a.h - (ly + lh + 8 * s) - ui.textHeight(ui.bodyScale) - 10 * s};
    const std::vector<Cell> cells = units ? unitCells(false) : buildingCells();
    const auto bo = w.buildableBuildings(side_);
    const auto uo = w.buildableUnits(side_);
    auto mode = [&](const Cell& c) {
        const int real = static_cast<int>(c.building ? w.buildingOverride(side_, c.type) : w.unitOverride(side_, c.type));
        return shown(key(c.building ? kKindBuilding : kKindUnit, side_, c.type), real);
    };
    auto listed = [&](const Cell& c) {
        const auto& v = c.building ? bo : uo;
        return std::any_of(v.begin(), v.end(), [&](const game::BuildOption& o) { return o.type == c.type; });
    };
    int hovered = -1;
    const int clicked = grid(
        ui, g, cells, scroll_[0], icons, colourOf(w, side_),
        [&](int i) {
            const int m = mode(cells[i]);
            return m == 1 ? std::string(tr("ON")) : m == 2 ? std::string(tr("OFF")) : std::string();
        },
        [&](int i) {
            const int m = mode(cells[i]);
            return m == 1   ? Colour{90, 230, 110, 255}
                   : m == 2 ? Colour{240, 80, 60, 255}
                   : listed(cells[i]) ? Colour{110, 160, 255, 150}
                                      : Colour{0, 0, 0, 0};
        },
        hovered);
    if (clicked >= 0) {
        const Cell& c = cells[static_cast<std::size_t>(clicked)];
        const int next = (mode(c) + 1) % 3;
        const auto m = static_cast<game::BuildOverride>(next);
        if (c.building) w.orderCheatBuildingOverride(side_, c.type, m);
        else w.orderCheatUnitOverride(side_, c.type, m);
        pending_[key(c.building ? kKindBuilding : kKindUnit, side_, c.type)] = next;
        status_ = name(c) + ": " + tr(next == 1 ? "allowed" : next == 2 ? "blocked" : "default");
    }
    if (hovered >= 0) {
        const Cell& c = cells[static_cast<std::size_t>(hovered)];
        const int m = mode(c);
        const std::string st = tr(m == 1 ? "allowed" : m == 2 ? "blocked" : listed(c) ? "default (on the sidebar now)" : "default (not available now)");
        ui.text(ui.ellipsize(name(c) + " - " + st, a.w, ui.bodyScale), a.x, g.y + g.h + 6 * s, theme::kText, ui.bodyScale);
    }
}

void CheatMenu::drawDeliver(Ui& ui, game::World& w, const SDL_FRect& a, float s, render::SpriteCache* icons) {
    const float rowH = std::round(ui.textHeight(ui.textScale) + 14 * s);
    const bool starport = w.starportOpen(side_);
    std::string how = tr(starport ? "By starport frigate (no stock, price or 6-unit limits)" : "By carryall to the base (no starport)");
    const int queued = w.cheatDeliveriesPending(side_);
    if (queued) how += trf("  -  %d waiting for a trip", queued);
    ui.text(ui.ellipsize(how, a.w, ui.bodyScale), a.x, a.y, theme::kDim, ui.bodyScale);
    const float gy = a.y + ui.textHeight(ui.bodyScale) + 8 * s;
    const SDL_FRect g{a.x, gy, a.w, a.y + a.h - gy - rowH - 12 * s - ui.textHeight(ui.bodyScale) - 6 * s};
    const std::vector<Cell> cells = unitCells(true);
    deliverCount_.resize(formats::kMaxUnitTypes, 0);
    int hovered = -1;
    const int clicked = grid(
        ui, g, cells, scroll_[1], icons, colourOf(w, side_),
        [&](int i) {
            const int n = deliverCount_[static_cast<std::size_t>(cells[i].type)];
            return n ? std::to_string(n) : std::string();
        },
        [&](int i) {
            if (cells[i].type == deliverSel_) return Colour{240, 184, 80, 255};
            return deliverCount_[static_cast<std::size_t>(cells[i].type)] ? Colour{90, 230, 110, 255} : Colour{0, 0, 0, 0};
        },
        hovered);
    if (clicked >= 0) {
        deliverSel_ = cells[static_cast<std::size_t>(clicked)].type;
        const bool less = (SDL_GetModState() & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL)) != 0;
        int& n = deliverCount_[static_cast<std::size_t>(deliverSel_)];
        n = std::clamp(n + (less ? -1 : 1), 0, 999);
    }
    if (hovered >= 0)
        ui.text(ui.ellipsize(name(cells[static_cast<std::size_t>(hovered)]) + tr("   (click +1, Shift+click -1)"), a.w, ui.bodyScale),
                a.x, g.y + g.h + 4 * s, theme::kText, ui.bodyScale);
    const float by = a.y + a.h - rowH;
    float x = a.x;
    const float sb = 70 * s;
    const bool sel = deliverSel_ >= 0 && deliverSel_ < formats::kMaxUnitTypes;
    const std::string selName = sel ? name(Cell{deliverSel_, false}) : std::string(tr("(pick a unit)"));
    ui.text(ui.ellipsize(selName, 200 * s, ui.textScale), x, by + (rowH - ui.textHeight(ui.textScale)) / 2, theme::kText,
            ui.textScale);
    x += 210 * s;
    auto adj = [&](const char* label, int d) {
        if (ui.button(SDL_FRect{x, by, sb, rowH}, label, ButtonStyle::Normal, sel) && sel) {
            int& n = deliverCount_[static_cast<std::size_t>(deliverSel_)];
            n = std::clamp(n + d, 0, 999);
        }
        x += sb + 6 * s;
    };
    adj("-10", -10);
    adj("-1", -1);
    adj("+1", 1);
    adj("+10", 10);
    int total = 0;
    for (int n : deliverCount_) total += n;
    if (ui.button(SDL_FRect{x, by, 110 * s, rowH}, tr("Clear"), ButtonStyle::Normal, total > 0)) std::fill(deliverCount_.begin(), deliverCount_.end(), 0);
    x += 116 * s;
    const std::string label = trf(total == 1 ? "Deliver %d unit" : "Deliver %d units", total);
    if (ui.button(SDL_FRect{x, by, a.x + a.w - x, rowH}, ui.ellipsize(label, a.x + a.w - x - 12 * s, ui.textScale), ButtonStyle::Primary, total > 0)) {
        for (int t = 0; t < formats::kMaxUnitTypes; ++t)
            if (deliverCount_[static_cast<std::size_t>(t)] > 0) w.orderCheatDeliver(side_, t, deliverCount_[static_cast<std::size_t>(t)]);
        status_ = trf(starport ? "%d units ordered for free (frigate)" : "%d units ordered for free (carryall)", total);
        std::fill(deliverCount_.begin(), deliverCount_.end(), 0);
    }
}

bool CheatMenu::drawSpawn(Ui& ui, game::World& w, const SDL_FRect& a, float s, render::SpriteCache* icons) {
    const float rowH = std::round(ui.textHeight(ui.textScale) + 14 * s);
    const float bw = 180 * s;
    if (ui.button(SDL_FRect{a.x, a.y, bw, rowH}, tr("Units"), !spawnBuildings_ ? ButtonStyle::Primary : ButtonStyle::Normal) &&
        spawnBuildings_) {
        spawnBuildings_ = false;
        spawn_.type = -1;
    }
    if (ui.button(SDL_FRect{a.x + bw + 8 * s, a.y, bw, rowH}, tr("Buildings"),
                  spawnBuildings_ ? ButtonStyle::Primary : ButtonStyle::Normal) &&
        !spawnBuildings_) {
        spawnBuildings_ = true;
        spawn_.type = -1;
    }
    const float ox = a.x + 2 * bw + 24 * s, ow = a.x + a.w - ox;
    // One label + one value each (the toggle shows On/Off itself); widths from the measured text.
    const float ts = ui.textScale;
    if (spawnBuildings_)
        ui.toggle(SDL_FRect{ox, a.y, std::min(ow, ui.textWidth(tr("Lay concrete"), ts) + ui.textWidth(tr("Off"), ts) + 100 * ts), rowH},
                  tr("Lay concrete"), spawn_.concrete);
    else
        ui.slider(SDL_FRect{ox, a.y, std::min(ow, std::max(420 * s, (ui.textWidth(tr("Count per click"), ts) + 40 * s) / 0.46f)), rowH},
                  tr("Count per click"), spawn_.count, 1, 50, std::to_string(spawn_.count));
    const float gy = a.y + rowH + 10 * s;
    const SDL_FRect g{a.x, gy, a.w, a.y + a.h - gy - rowH - 12 * s - ui.textHeight(ui.bodyScale) - 6 * s};
    const std::vector<Cell> cells = spawnBuildings_ ? buildingCells() : unitCells(false);
    int hovered = -1;
    const int clicked = grid(
        ui, g, cells, scroll_[2], icons, colourOf(w, side_), [&](int) { return std::string(); },
        [&](int i) { return cells[i].type == spawn_.type ? Colour{240, 184, 80, 255} : Colour{0, 0, 0, 0}; }, hovered);
    if (clicked >= 0) spawn_.type = cells[static_cast<std::size_t>(clicked)].type;
    if (hovered >= 0) ui.text(ui.ellipsize(name(cells[static_cast<std::size_t>(hovered)]), a.w, ui.bodyScale), a.x, g.y + g.h + 4 * s, theme::kText, ui.bodyScale);
    const float by = a.y + a.h - rowH;
    spawn_.side = side_;
    spawn_.building = spawnBuildings_;
    const bool ok = spawn_.type >= 0;
    const std::string what = ok ? (spawnBuildings_ ? name(Cell{spawn_.type, true})
                                                   : std::to_string(spawn_.count) + " x " + name(Cell{spawn_.type, false}))
                                : std::string(tr("Pick a type"));
    ui.text(ui.ellipsize(trf("%s for %s (side %d)", what.c_str(), houseName(w.sides()[static_cast<std::size_t>(side_)].house), side_),
                         a.w - 280 * s, ui.textScale),
            a.x, by + (rowH - ui.textHeight(ui.textScale)) / 2, theme::kText, ui.textScale);
    return ui.button(SDL_FRect{a.x + a.w - 260 * s, by, 260 * s, rowH}, ui.ellipsize(tr("Place on map"), 248 * s, ui.textScale), ButtonStyle::Primary, ok) && ok;
}

float CheatMenu::drawToggles(Ui& ui, game::World& w, const SDL_FRect& a, float s) {
    const float ts = ui.textScale;
    const float rowH = std::round(ui.textHeight(ts) + 14 * s);
    float y = a.y;
    const char* reveal = tr("Reveal the whole map");
    if (ui.button(SDL_FRect{a.x, y, std::min(a.w, std::max(300 * s, ui.textWidth(reveal, ts) + 40 * s)), rowH}, reveal)) {
        w.orderCheatRevealMap();
        status_ = tr("Map revealed");
    }
    y += rowH + 12 * s;
    const int real = w.instantBuild(side_) ? 1 : 0;
    bool on = shown(key(kKindInstant, side_), real) != 0;
    // Label on the left, On/Off + switch on the right (Ui::toggle); the row is as wide as the
    // measured text needs, so they never overlap.
    const char* label = tr("Instant build for this side");
    const float tw = std::min(a.w, ui.textWidth(label, ts) + ui.textWidth(tr("Off"), ts) + 100 * ts);
    if (ui.toggle(SDL_FRect{a.x, y, tw, rowH}, label, on)) {
        w.orderCheatInstantBuild(side_, on);
        pending_[key(kKindInstant, side_)] = on ? 1 : 0;
        status_ = tr(on ? "Instant build on" : "Instant build off");
    }
    y += rowH + 18 * s;
    y += ui.wrapped(tr("Reveal: removes the shroud for you (it does not come back). Instant build: buildings, units and "
                       "upgrades finish in one tick; they are still paid for. Ctrl+Shift+C opens this page during the game."),
                    SDL_FRect{a.x, y, a.w, a.y + a.h - y}, theme::kDim, ui.bodyScale);
    return y - a.y;
}

std::vector<std::uint8_t> CheatMenu::reinforceUnits() const {
    std::vector<std::uint8_t> v;
    for (int t = 0; t < int(reinfCount_.size()); ++t)
        v.insert(v.end(), static_cast<std::size_t>(reinfCount_[static_cast<std::size_t>(t)]), static_cast<std::uint8_t>(t));
    return v;
}

std::uint32_t CheatMenu::reinforceDelayTicks() const {
    return static_cast<std::uint32_t>(kDelays[std::clamp(reinfWhen_, 0, int(std::size(kDelays)) - 1)]) *
           static_cast<std::uint32_t>(tps_);
}

std::string CheatMenu::timeText(std::uint32_t ticks) const {
    const std::uint32_t secs = (ticks + static_cast<std::uint32_t>(tps_) - 1) / static_cast<std::uint32_t>(tps_);
    char buf[32];
    SDL_snprintf(buf, sizeof buf, "%u:%02u", secs / 60, secs % 60);
    return buf;
}

// Reinforcements: pick units (grid, like Delivery), when (now / in N s or min of game time),
// the entry edge, then order them to the side's base or to a point picked on the map. The
// world turns them into carryall deliveries when due (World::orderCheatReinforce).
bool CheatMenu::drawReinforce(Ui& ui, game::World& w, const SDL_FRect& a, float s, render::SpriteCache* icons) {
    const float ts = ui.textScale, bs = ui.bodyScale;
    const float rowH = std::round(ui.textHeight(ts) + 14 * s), gap = 8 * s;
    reinfCount_.resize(formats::kMaxUnitTypes, 0);
    const float colW = std::clamp(a.w * 0.42f, std::min(a.w * 0.5f, 300 * s), 560 * s);
    const float gw = a.w - colW - 16 * s;
    // Left: unit grid with counts.
    ui.text(ui.ellipsize(tr("Click a unit: +1, Shift+click: -1"), gw, bs), a.x, a.y, theme::kDim, bs);
    const float gy = a.y + ui.textHeight(bs) + gap;
    const SDL_FRect g{a.x, gy, gw, a.y + a.h - gy - rowH - 12 * s - ui.textHeight(bs) - 6 * s};
    const std::vector<Cell> cells = unitCells(true);
    int hovered = -1;
    const int clicked = grid(
        ui, g, cells, scroll_[3], icons, colourOf(w, side_),
        [&](int i) {
            const int n = reinfCount_[static_cast<std::size_t>(cells[i].type)];
            return n ? std::to_string(n) : std::string();
        },
        [&](int i) {
            if (cells[i].type == reinfSel_) return Colour{240, 184, 80, 255};
            return reinfCount_[static_cast<std::size_t>(cells[i].type)] ? Colour{90, 230, 110, 255} : Colour{0, 0, 0, 0};
        },
        hovered);
    if (clicked >= 0) {
        reinfSel_ = cells[static_cast<std::size_t>(clicked)].type;
        const bool less = (SDL_GetModState() & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL)) != 0;
        int& n = reinfCount_[static_cast<std::size_t>(reinfSel_)];
        n = std::clamp(n + (less ? -1 : 1), 0, 200);
    }
    if (hovered >= 0)
        ui.text(ui.ellipsize(name(cells[static_cast<std::size_t>(hovered)]), gw, bs), a.x, g.y + g.h + 4 * s, theme::kText, bs);
    int total = 0;
    for (int n : reinfCount_) total += n;
    {  // -1 / +1 / Clear for the selected type
        const float by = a.y + a.h - rowH;
        const bool sel = reinfSel_ >= 0 && reinfSel_ < formats::kMaxUnitTypes;
        const float sb = std::max(60 * s, ui.textWidth("+1", ts) + 24 * s), cw = std::max(100 * s, ui.textWidth(tr("Clear"), ts) + 30 * s);
        float x = a.x + gw - (2 * sb + cw + 12 * s);
        const std::string selName = sel ? name(Cell{reinfSel_, false}) : std::string(tr("(pick a unit)"));
        ui.text(ui.ellipsize(selName, std::max(0.0f, x - a.x - 8 * s), ts), a.x, by + (rowH - ui.textHeight(ts)) / 2, theme::kText, ts);
        for (int d : {-1, 1}) {
            if (ui.button(SDL_FRect{x, by, sb, rowH}, d < 0 ? "-1" : "+1", ButtonStyle::Normal, sel) && sel) {
                int& n = reinfCount_[static_cast<std::size_t>(reinfSel_)];
                n = std::clamp(n + d, 0, 200);
            }
            x += sb + 6 * s;
        }
        if (ui.button(SDL_FRect{x, by, cw, rowH}, tr("Clear"), ButtonStyle::Normal, total > 0))
            std::fill(reinfCount_.begin(), reinfCount_.end(), 0);
    }
    // Right: when, where, order, pending list.
    const float cx = a.x + a.w - colW;
    float y = a.y;
    std::vector<std::string> when;
    for (int d : kDelays) when.push_back(d == 0 ? std::string(tr("Now")) : trf("in %s", timeText(static_cast<std::uint32_t>(d) * tps_).c_str()));
    std::vector<std::string> edges;
    for (const char* e : kEdgeNames) edges.push_back(tr(e));
    ui.choice(SDL_FRect{cx, y, colW, rowH}, tr("When"), reinfWhen_, when);
    y += rowH + 4 * s;
    ui.choice(SDL_FRect{cx, y, colW, rowH}, tr("From edge"), reinfEdge_, edges);
    y += rowH + gap;
    std::string what;
    for (int t = 0; t < int(reinfCount_.size()); ++t) {
        const int n = reinfCount_[static_cast<std::size_t>(t)];
        if (n) what += (what.empty() ? "" : ", ") + std::to_string(n) + " " + name(Cell{t, false});
    }
    if (what.empty()) what = tr("No units picked");
    const int carryalls = (total + 24) / 25;
    if (total)
        what = trf(total == 1 ? "%d unit (%d carryall): " : carryalls == 1 ? "%d units (%d carryall): " : "%d units (%d carryalls): ",
                   total, carryalls) + what;
    ui.text(ui.ellipsize(what, colW, bs), cx, y, total ? theme::kText : theme::kDim, bs);
    y += ui.textHeight(bs) + gap;
    const std::string sideName = houseName(w.sides()[static_cast<std::size_t>(side_)].house);
    bool place = false;
    if (ui.button(SDL_FRect{cx, y, colW, rowH}, ui.ellipsize(trf("Order - drop at the %s base", sideName.c_str()), colW - 12 * s, ts),
                  ButtonStyle::Primary, total > 0)) {
        if (w.orderCheatReinforce(side_, reinforceUnits(), reinforceDelayTicks(), -1, -1, reinfEdge_ - 1)) {
            status_ = trf("%d units ordered for %s (%s)", total, sideName.c_str(), when[static_cast<std::size_t>(reinfWhen_)].c_str());
            statusOrdered_ = true;
            std::fill(reinfCount_.begin(), reinfCount_.end(), 0);
        }
    }
    y += rowH + 4 * s;
    if (ui.button(SDL_FRect{cx, y, colW, rowH}, ui.ellipsize(tr("Order - pick the drop point..."), colW - 12 * s, ts),
                  ButtonStyle::Normal, total > 0)) {
        place = true;  // the click on the battlefield orders them (click())
        placeReinforce_ = true;
    }
    y += rowH + gap;
    // Pending reinforcements of all sides (countdown in game time; cancel).
    const auto& list = w.cheatReinforcements();
    const std::uint32_t now = w.gameTicks();
    ui.text(list.empty() ? std::string(tr("Pending - none")) : trf("Pending (%d)", int(list.size())), cx, y, theme::kDim, bs);
    y += ui.textHeight(bs) + 4 * s;
    std::vector<ListRow> rows;
    for (const auto& r : list) {
        ListRow row;
        row.text = trf(r.units.size() == 1 ? "%s (%d): %d unit" : "%s (%d): %d units",
                       houseName(w.sides()[static_cast<std::size_t>(r.side)].house), r.side, int(r.units.size()));
        if (r.x >= 0) row.text += trf(" to %d,%d", r.x, r.y);
        row.detail = r.due > now ? trf("in %s", timeText(r.due - now).c_str()) : std::string(tr("arriving"));
        rows.push_back(std::move(row));
    }
    const float listH = a.y + a.h - y - rowH - 6 * s;
    if (reinfListSel_ >= int(rows.size())) reinfListSel_ = int(rows.size()) - 1;
    if (listH > 20 * s) ui.list(SDL_FRect{cx, y, colW, listH}, rows, reinfListSel_, reinfListScroll_);
    const bool canCancel = reinfListSel_ >= 0 && reinfListSel_ < int(list.size());
    if (ui.button(SDL_FRect{cx, a.y + a.h - rowH, colW, rowH}, ui.ellipsize(tr("Cancel selected"), colW - 12 * s, ts), ButtonStyle::Normal, canCancel) && canCancel) {
        w.orderCheatCancelReinforcement(list[static_cast<std::size_t>(reinfListSel_)].id);
        status_ = tr("Reinforcement cancelled");
    }
    if (w.pendingOrders() > 0 && statusOrdered_) {
        status_ += tr(" (when the game runs)");
        statusOrdered_ = false;
    }
    return place;
}

// --- Placement mode ----------------------------------------------------------------------

void CheatMenu::placementTile(float wx, float wy, int& tx, int& ty) const {
    tx = int(std::floor(wx / 32));
    ty = int(std::floor(wy / 32));
    if (!spawn_.building || spawn_.type < 0) return;
    const auto& t = t_.bin.buildings[spawn_.type];
    if (t.specialBehavior == game::building_behaviour::kConcrete) return;
    const int fw = std::max(1, t.artWidth / 32), fh = std::max(1, t.artHeight / 32);
    tx -= (fw - 1) / 2;
    ty -= (fh - 1) / 2;
}

bool CheatMenu::click(game::World& w, float wx, float wy, bool keep) {
    if (placing_ && placeReinforce_) {
        const int tx = int(std::floor(wx / 32)), ty = int(std::floor(wy / 32));
        const bool ok = w.orderCheatReinforce(side_, reinforceUnits(), reinforceDelayTicks(), tx, ty, reinfEdge_ - 1);
        if (ok && !keep) {
            placing_ = placeReinforce_ = false;
            std::fill(reinfCount_.begin(), reinfCount_.end(), 0);
        }
        return ok;
    }
    if (!placing_ || spawn_.type < 0) return false;
    int tx = 0, ty = 0;
    placementTile(wx, wy, tx, ty);
    const bool ok = spawn_.building ? w.orderCheatSpawnBuilding(spawn_.side, spawn_.type, tx, ty, spawn_.concrete)
                                    : w.orderCheatSpawnUnit(spawn_.side, spawn_.type, tx, ty, spawn_.count);
    if (ok && !keep) placing_ = false;
    return ok;
}

void CheatMenu::drawHint(Ui& ui, float x, float y) const {
    const std::string hint = placementHint();
    if (hint.empty()) return;
    // The Play font (UTF-8 + Cyrillic: the hint is translated) at a size that follows the output.
    Ui::ModernScope modern(ui);
    // Size from the current target: the window (Enhanced / 3D) or the game-resolution frame
    // (Original, drawn into a 640x400-class texture and scaled up: about 10 px there).
    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(ui.renderer(), &ow, &oh);
    SDL_Rect vp{0, 0, ow, oh};
    SDL_GetRenderViewport(ui.renderer(), &vp);
    const float s = ow <= 1024 ? 0.8f : Ui::bodyScaleFor(ow, oh), h = ui.textHeight(s);
    const float w = ui.textWidth(hint, s);
    // Keep it inside the battlefield (the cursor may be near the right / top edge).
    x = std::max(2.0f, std::min(x, float(vp.w) - w - 4));
    y = std::max(2.0f, y - h);
    ui.fill(SDL_FRect{x - 3, y - 1, w + 6, h + 2}, Colour{0, 0, 0, 170});
    ui.text(hint, x, y, Colour{255, 226, 170, 255}, s, Align::Left, false);
}

void CheatMenu::drawPlacement(SDL_Renderer* r, Ui& ui, const game::World& w, float wx, float wy, float camX, float camY,
                              float zoom, render::SpriteCache* markers) const {
    if (placing_ && placeReinforce_) {  // the drop tile
        const int tx = int(std::floor(wx / 32)), ty = int(std::floor(wy / 32));
        const SDL_FRect rc{std::round((tx * 32.0f - camX) * zoom), std::round((ty * 32.0f - camY) * zoom), 32 * zoom, 32 * zoom};
        const bool ok = w.inBounds(tx, ty);
        SDL_SetRenderDrawColor(r, ok ? 240 : 230, ok ? 184 : 50, ok ? 80 : 40, 110);
        SDL_RenderFillRect(r, &rc);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 160);
        SDL_RenderRect(r, &rc);
        drawHint(ui, rc.x, rc.y - 2);
        return;
    }
    if (!placing_ || spawn_.type < 0) return;
    int tx = 0, ty = 0;
    placementTile(wx, wy, tx, ty);
    std::array<bool, 16> ok{};
    std::uint32_t mask = 1;
    if (spawn_.building) {
        w.canCheatSpawnBuilding(spawn_.type, tx, ty, &ok);
        const auto& t = t_.bin.buildings[spawn_.type];
        mask = t.specialBehavior == game::building_behaviour::kConcrete ? t.tilesOccupiedSolid : t.tilesOccupiedAll;
    } else {
        ok[0] = w.inBounds(tx, ty);
    }
    for (int i = 0; i < 16; ++i) {
        if (!((mask >> i) & 1)) continue;
        const SDL_FRect rc{std::round(((tx + i % 4) * 32.0f - camX) * zoom), std::round(((ty + i / 4) * 32.0f - camY) * zoom),
                           32 * zoom, 32 * zoom};
        if (markers) {
            if (const render::Sprite* sp = markers->get(ok[i] ? 0 : 1, -1)) {
                SDL_RenderTexture(r, sp->texture, nullptr, &rc);
                continue;
            }
        }
        if (ok[i]) SDL_SetRenderDrawColor(r, spawn_.building ? 60 : 240, spawn_.building ? 220 : 184, spawn_.building ? 90 : 80, 90);
        else SDL_SetRenderDrawColor(r, 230, 50, 40, 110);
        SDL_RenderFillRect(r, &rc);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 120);
        SDL_RenderRect(r, &rc);
    }
    drawHint(ui, std::round((tx * 32.0f - camX) * zoom), std::round((ty * 32.0f - camY) * zoom) - 2);
}

void CheatMenu::drawPlacement(Ui& ui, const game::World& w, float wx, float wy,
                              const std::function<void(int, int, SDL_FColor, SDL_FColor)>& cell,
                              const std::function<bool(float, float, float&, float&)>& project) const {
    if (!placing_) return;
    auto c8 = [](int r8, int g8, int b8, int a8) { return SDL_FColor{r8 / 255.f, g8 / 255.f, b8 / 255.f, a8 / 255.f}; };
    int tx = 0, ty = 0;
    std::array<bool, 16> ok{};
    std::uint32_t mask = 1;
    if (placeReinforce_) {
        tx = int(std::floor(wx / 32));
        ty = int(std::floor(wy / 32));
        ok[0] = w.inBounds(tx, ty);
        cell(tx, ty, ok[0] ? c8(240, 184, 80, 110) : c8(230, 50, 40, 110), c8(255, 255, 255, 160));
    } else {
        if (spawn_.type < 0) return;
        placementTile(wx, wy, tx, ty);
        if (spawn_.building) {
            w.canCheatSpawnBuilding(spawn_.type, tx, ty, &ok);
            const auto& t = t_.bin.buildings[spawn_.type];
            mask = t.specialBehavior == game::building_behaviour::kConcrete ? t.tilesOccupiedSolid : t.tilesOccupiedAll;
        } else {
            ok[0] = w.inBounds(tx, ty);
        }
        for (int i = 0; i < 16; ++i) {
            if (!((mask >> i) & 1)) continue;
            const SDL_FColor fill = ok[i] ? (spawn_.building ? c8(60, 220, 90, 90) : c8(240, 184, 80, 90)) : c8(230, 50, 40, 110);
            cell(tx + i % 4, ty + i / 4, fill, c8(255, 255, 255, 120));
        }
    }
    float hx = 0, hy = 0;
    if (!project(tx * 32.0f, ty * 32.0f, hx, hy)) return;
    drawHint(ui, std::round(hx), std::round(hy) - 2);
}

std::string CheatMenu::placementHint() const {
    if (placeReinforce_) {
        int total = 0;
        for (int n : reinfCount_) total += n;
        return render::trf("Reinforcement drop point: %d units (side %d) - Shift: keep, right click/Esc: stop", total, side_);
    }
    if (spawn_.type < 0) return {};
    const std::string what = spawn_.building ? t_.buildingName(spawn_.type)
                                             : std::to_string(spawn_.count) + " x " + t_.unitName(spawn_.type);
    return render::trf("Spawn %s (side %d) - Shift: keep, right click/Esc: stop", what.c_str(), spawn_.side);
}

}  // namespace d2k::platform

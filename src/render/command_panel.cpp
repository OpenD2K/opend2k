#include "render/command_panel.hpp"

#include "render/ui_text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace d2k::render {

namespace {
using Icon = UiSkin::Icon;
using State = UiSkin::State;
// Layout in original pixels, relative to the column's left edge (x) and the window top (y).
constexpr float kRadarX = 8, kRadarY = 8, kRadarSize = 144;  // minimap frame
constexpr float kRow1Y = 158, kRow1H = 24, kRow2Y = 186, kRow2H = 20;
struct Def { float x, y, w, h; Icon icon; };
constexpr Def kDefs[] = {
    {8, kRow1Y, 34, kRow1H, Icon::Repair},     {46, kRow1Y, 34, kRow1H, Icon::Sell},
    {84, kRow1Y, 34, kRow1H, Icon::Guard},     {122, kRow1Y, 34, kRow1H, Icon::Retreat},
    {8, kRow2Y, 72, kRow2H, Icon::Upgrade},    {84, kRow2Y, 72, kRow2H, Icon::Starport},
    {4, 3, 70, 17, Icon::Options},  // top-left of the window
};
}  // namespace

void CommandPanel::layout(float panelX, int outW, int outH) {
    panelX_ = panelX;
    outW_ = outW;
    outH_ = outH;
}

SDL_FRect CommandPanel::radarRect() const {
    const float in = skin_.px(4);
    return {panelX_ + skin_.px(kRadarX) + in, skin_.px(kRadarY) + in, skin_.px(kRadarSize) - 2 * in, skin_.px(kRadarSize) - 2 * in};
}

SDL_FRect CommandPanel::buttonRect(int b) const {
    const Def& d = kDefs[b];
    const float x0 = b == kOptions ? 0 : panelX_;
    return {x0 + skin_.px(d.x), skin_.px(d.y), skin_.px(d.w), skin_.px(d.h)};
}

// The original shows UPGRADE while something can be upgraded (FUN_00446760) and STARPORT with a
// starport; here they stay in place and are disabled otherwise.
bool CommandPanel::enabled(int b, const game::World& w, int side) const {
    if (b == kUpgrade) return !w.upgradableBuildings(side).empty() || w.upgradeQueue(side).active;
    if (b == kStarport) return w.starportOpen(side);
    return true;
}

int CommandPanel::hit(float x, float y) const {
    for (int b = 0; b < kCount; ++b) {
        const SDL_FRect rc = buttonRect(b);
        if (x >= rc.x && y >= rc.y && x < rc.x + rc.w && y < rc.y + rc.h) return b;
    }
    return -1;
}

SDL_FRect CommandPanel::creditsRect() const {
    const float w = skin_.px(92);
    return {panelX_ - w - skin_.px(6), skin_.px(3), w, skin_.px(17)};
}

SDL_FRect CommandPanel::powerRect() const {
    const SDL_FRect c = creditsRect();
    const float w = skin_.px(84);
    return {c.x - w - skin_.px(4), c.y, w, c.h};
}

SDL_FRect CommandPanel::timerRect() const {
    const float w = skin_.px(64);
    return {std::round(panelX_ / 2 - w / 2), skin_.px(3), w, skin_.px(17)};
}

bool CommandPanel::contains(float x, float y) const {
    auto in = [&](const SDL_FRect& r) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; };
    if (x >= panelX_ && y < blockBottom()) return true;
    if (in(buttonRect(kOptions)) || in(creditsRect()) || in(powerRect())) return true;
    return timerShown_ && in(timerRect());
}

void CommandPanel::drawBlock(const game::World& w, int side, int cursorMode, bool upgradeLit, bool starportLit, float mouseX,
                             float mouseY) {
    // The whole column's background (the production panel draws on top of it).
    skin_.panel({panelX_, 0, float(outW_) - panelX_, float(outH_)});
    skin_.frame({panelX_ + skin_.px(kRadarX), skin_.px(kRadarY), skin_.px(kRadarSize), skin_.px(kRadarSize)});
    const int hot = hit(mouseX, mouseY);
    static const char* const kLabels[kCount] = {"", "", "", "", "UPGRADE", "STARPORT", "OPTIONS"};
    for (int b = 0; b < kCount; ++b) {
        if (b == kOptions) continue;
        const bool on = enabled(b, w, side);
        const bool lit = (b == kRepair && cursorMode == 2) || (b == kSell && cursorMode == 3) || (b == kUpgrade && upgradeLit) ||
                         (b == kStarport && starportLit);
        const State s = !on ? State::Disabled : pressed_ == b ? State::Pressed : lit ? State::Lit : hot == b ? State::Hot : State::Normal;
        skin_.button(buttonRect(b), kDefs[b].icon, s, *kLabels[b] ? tr(kLabels[b]) : "");
    }
    // A gentle pulse on UPGRADE while upgrades wait (the original only shows the button).
    if (enabled(kUpgrade, w, side) && !upgradeLit && !w.upgradeQueue(side).active) {
        const float p = 0.5f + 0.5f * std::sin(float(SDL_GetTicks()) / 300.0f);
        const SDL_FRect rc = buttonRect(kUpgrade);
        const Colour a = skin_.accent();
        skin_.fill({rc.x + skin_.px(3), rc.y + rc.h - skin_.px(3), rc.w - skin_.px(6), std::max(1.0f, skin_.px(1))},
                   Colour{a.r, a.g, a.b, Uint8(80 + 150 * p)});
    }
}

void CommandPanel::drawTopBar(const game::World& w, int side, float mouseX, float mouseY) {
    const int hot = hit(mouseX, mouseY);
    skin_.button(buttonRect(kOptions), Icon::Options, pressed_ == kOptions ? State::Pressed : hot == kOptions ? State::Hot : State::Normal,
                 tr("OPTIONS"));

    // Credits (FUN_004254a0): CashReal + SpiceReal clamped to 9999999.
    const auto eco = w.economy(side);
    const SDL_FRect c = creditsRect();
    skin_.well(c);
    const float cy = c.y + c.h / 2;
    // A small spice gem: an amber diamond.
    {
        const float d = skin_.px(4.5f), x = c.x + skin_.px(8);
        SDL_Vertex v[4];
        const SDL_FColor top{1.0f, 0.86f, 0.5f, 1}, bot{0.75f, 0.42f, 0.12f, 1};
        v[0] = {{x, cy - d}, top, {0, 0}};
        v[1] = {{x + d * 0.8f, cy}, bot, {0, 0}};
        v[2] = {{x, cy + d}, bot, {0, 0}};
        v[3] = {{x - d * 0.8f, cy}, top, {0, 0}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(skin_.renderer(), nullptr, v, 4, idx, 6);
    }
    const std::string cr = std::to_string(std::clamp(eco.credits, 0, 9999999));
    const float sc = skin_.ts() * 1.15f;
    skin_.label(cr, c.x + c.w - skin_.px(5) - skin_.textW(cr, sc, true), cy - skin_.textH(sc) / 2, Colour{255, 214, 130, 255}, sc, true);

    // Power: a lamp (green / yellow / red like the power bar) and output / demand.
    const SDL_FRect p = powerRect();
    skin_.well(p);
    const int pct = int(eco.powerPercent);
    const Colour lamp = pct >= 100 ? Colour{90, 230, 90, 255} : pct > 50 ? Colour{240, 200, 50, 255} : Colour{240, 60, 40, 255};
    skin_.lamp(p.x + skin_.px(8), p.y + p.h / 2, skin_.px(8), lamp);
    const std::string pw = std::to_string(eco.powerOutput) + "/" + std::to_string(eco.powerDemand);
    float ps = skin_.ts();
    const float room = p.w - skin_.px(19);
    while (ps > 0.7f && skin_.textW(pw, ps) > room) ps -= 0.125f;
    const Colour pc = pct >= 100 ? UiSkin::kSand : pct > 50 ? UiSkin::kWarn : UiSkin::kBad;
    skin_.label(pw, p.x + skin_.px(15) + (room - skin_.textW(pw, ps)) / 2, p.y + (p.h - skin_.textH(ps)) / 2, pc, ps);

    // Mission timer (FUN_004255e0: "%02d:%02d" of t / 1500, t / 25 % 60), red like FONTCOL.
    timerShown_ = w.missionTimer() >= 0;
    if (timerShown_) {
        const SDL_FRect t = timerRect();
        skin_.well(t);
        const int ticks = w.missionTimer();
        char buf[16];
        std::snprintf(buf, sizeof buf, "%02d:%02d", ticks / 1500, ticks / 25 % 60);
        const float ts = skin_.ts() * 1.1f;
        skin_.label(buf, t.x + (t.w - skin_.textW(buf, ts, true)) / 2, t.y + (t.h - skin_.textH(ts)) / 2, Colour{255, 90, 64, 255}, ts, true);
    }
}

CommandAction CommandPanel::mouseDown(float x, float y, int button, const game::World& w, int side) {
    if (button != SDL_BUTTON_LEFT) return {};
    const int b = hit(x, y);
    if (b < 0 || !enabled(b, w, side)) return {};
    pressed_ = b;
    switch (b) {
    case kRepair: return {CommandAction::RepairMode};
    case kSell: return {CommandAction::SellMode};
    case kGuard: return {CommandAction::Guard};
    case kRetreat: return {CommandAction::Retreat};
    case kUpgrade: return {CommandAction::Upgrade};
    case kStarport: return {CommandAction::Starport};
    case kOptions: return {CommandAction::Options};
    default: return {};
    }
}

std::vector<UiSkin::TipLine> CommandPanel::tooltipAt(float x, float y, const game::World& w, int side) const {
    const Colour hint{170, 155, 125, 255};
    const auto eco = w.economy(side);
    auto in = [&](const SDL_FRect& r) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; };
    if (in(powerRect()))
        return {{std::string(tr("Power")) + " " + std::to_string(eco.powerPercent) + "%", UiSkin::kGold},
                {std::string(tr("Output")) + " " + std::to_string(eco.powerOutput) + "   " + tr("Demand") + " " + std::to_string(eco.powerDemand), hint},
                {tr("Below 100%: slower building, no radar"), hint}};
    if (in(creditsRect()))
        return {{tr("Credits"), UiSkin::kGold},
                {std::string(tr("Cash")) + " " + std::to_string(eco.cash) + "   " + tr("Spice") + " " + std::to_string(eco.spice) + " / " + std::to_string(eco.storage), hint},
                {tr("Refineries and silos store the spice"), hint}};
    if (timerShown_ && in(timerRect())) return {{tr("Mission timer"), UiSkin::kGold}};
    const bool on = enabled(hit(x, y), w, side);
    switch (hit(x, y)) {
    case kRepair: return {{tr("Repair"), UiSkin::kGold}, {tr("Then click a building (or a damaged vehicle)"), hint}};
    case kSell: return {{tr("Sell"), UiSkin::kGold}, {tr("Then click one of your buildings"), hint}};
    case kGuard: return {{tr("Guard"), UiSkin::kGold}, {tr("Selected units hold and guard their area"), hint}};
    case kRetreat: return {{tr("Retreat"), UiSkin::kGold}, {tr("Selected units fall back to the base"), hint}};
    case kUpgrade: return {{tr("Upgrades"), UiSkin::kGold}, {on ? tr("Shows the building upgrades") : tr("Nothing can be upgraded now"), hint}};
    case kStarport: return {{tr("Starport"), UiSkin::kGold}, {on ? tr("Order units at today's prices (CHOAM)") : tr("Needs a Starport"), hint}};
    case kOptions: return {{tr("Options"), UiSkin::kGold}, {tr("Pause menu (Esc)"), hint}};
    default: return {};
    }
}

}  // namespace d2k::render

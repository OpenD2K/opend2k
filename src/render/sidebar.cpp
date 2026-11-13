#include "render/sidebar.hpp"

#include "render/build_icons.hpp"
#include "render/ui_text.hpp"
#include "formats/language.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace d2k::render {

namespace {
// Scroll buttons in the footer: direction per index (up, down).
constexpr int kArrowDir[2] = {-1, 1};
const char* const kTabNames[4] = {"Buildings", "Infantry", "Vehicles", "Special"};

bool inRect(const SDL_FRect& r, float x, float y) { return r.w > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

// "HARKONNEN COMBAT TANK" -> "Harkonnen Combat Tank" (names missing from Text.UIB).
std::string titleCase(std::string s) {
    bool start = true;
    for (auto& c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        c = static_cast<char>(start ? std::toupper(u) : std::tolower(u));
        start = !std::isalnum(u);
    }
    return s;
}

bool launchable(int beh) {
    return beh == game::behaviour::kSaboteur || beh == game::behaviour::kFremen || beh == game::behaviour::kOrnithopter ||
           beh == game::behaviour::kDeathHand;
}
}  // namespace

bool Sidebar::contains(float x, float y) const { return inRect(area_, x, y) || inRect(power_, x, y); }

void Sidebar::scrollBy(float px) {
    const int t = int(tab_);
    const float maxS = std::max(0.0f, contentH_ - (tray_.h - skin_.px(6)));
    scroll_[t] = std::clamp(scroll_[t] + px, 0.0f, maxS);
}

void Sidebar::scroll(float x, float y, float wheel) {
    if (!contains(x, y)) return;
    scrollBy(-wheel * std::max(1.0f, rowPitch_ / 2));
}

// The upgrade list like the original's UPGRADE page (Display__DrawUpgradeSideBarMenu 0x4278a0):
// every building group that can be upgraded now (CanSideUpgradeBuildingGroup), plus the one
// being upgraded when it no longer qualifies.
std::vector<game::BuildOption> Sidebar::upgradeList(const game::World& w, int side) const {
    auto list = w.upgradableBuildings(side);
    const game::QueueState q = w.upgradeQueue(side);
    if (q.active && std::none_of(list.begin(), list.end(), [&](const game::BuildOption& o) { return o.type == q.type; })) {
        const auto& T = *w.rules().templates;
        const int g = T.bin.buildings[q.type].buildingGroup;
        list.push_back({q.type, g, q.cost, T.firstBuildingIconFrame + q.type, int(w.sides()[side].upgrades[std::size_t(g)])});
    }
    return list;
}

std::vector<Sidebar::Item> Sidebar::items(Tab t, const game::World& w, int side) const {
    std::vector<Item> out;
    const auto& T = w.rules().templates->bin;
    auto unit = [&](const game::BuildOption& o) {
        Item it;
        it.kind = Item::Unit;
        it.type = o.type;
        it.group = o.group;
        it.iconFrame = o.iconFrame;
        it.cost = o.cost;
        return it;
    };
    switch (t) {
    case Tab::Buildings:
        for (const auto& o : w.buildableBuildings(side)) {
            Item it;
            it.kind = Item::Building;
            it.type = o.type;
            it.group = o.group;
            it.iconFrame = o.iconFrame;
            it.cost = o.cost;
            out.push_back(it);
        }
        break;
    case Tab::Infantry:
    case Tab::Vehicles:
        for (const auto& o : w.buildableUnits(side)) {
            const auto& ut = T.units[o.type];
            if (specialBuildUnit(ut.specialBehavior) || (ut.isInfantry != 0) != (t == Tab::Infantry)) continue;
            out.push_back(unit(o));
        }
        break;
    case Tab::Special: {
        auto header = [&](Section s) {
            Item h;
            h.kind = Item::Header;
            h.section = s;
            out.push_back(h);
        };
        bool first = true;
        for (const auto& o : w.buildableUnits(side)) {
            if (!specialBuildUnit(T.units[o.type].specialBehavior)) continue;
            if (first) header(Section::Palace);
            first = false;
            out.push_back(unit(o));
        }
        const auto ups = upgradeList(w, side);
        if (!ups.empty()) header(Section::Upgrades);
        for (const auto& o : ups) {
            Item it;
            it.kind = Item::Upgrade;
            it.type = o.type;
            it.group = o.group;
            it.iconFrame = o.iconFrame;
            it.cost = o.cost;
            it.level = o.upgradeLevel;
            out.push_back(it);
        }
        if (w.starportOpen(side)) {
            header(Section::Starport);
            Item p;
            p.kind = Item::Purchase;
            out.push_back(p);
            p.kind = Item::Cancel;
            out.push_back(p);
            for (const auto& o : w.starportUnits(side)) {
                if (o.type < 0) continue;
                Item it;
                it.kind = Item::Port;
                it.type = o.type;
                it.iconFrame = o.iconFrame;
                it.cost = o.price;
                it.stock = o.stock;
                it.ordered = o.ordered;
                it.available = o.available;
                out.push_back(it);
            }
        }
        break;
    }
    }
    return out;
}

// --- drawing ---

void Sidebar::drawSlotIcon(const SDL_FRect& rc, int frame, bool greyed, const game::QueueState& q, int house,
                           UiSkin::Bezel st) {
    const Sprite* bright = sprites_.get(frame, house);
    const Sprite* grey = sprites_.get(frame, house, true);
    // The icon sits in a bezel: drop shadow / hover glow, the picture, then glass and steel rim.
    skin_.bezelUnder(rc, st);
    SDL_FRect in = skin_.bezelInner(rc);
    if (st == UiSkin::Bezel::Pressed) in.y += std::max(1.0f, std::round(skin_.k() * 0.5f));
    // Greyed exactly like the original sidebar (0x426e80 / Image__BlitTImage_4326B0); the building
    // or unit in production as the original clock wedge (bright from 12 o'clock up to progress).
    if (q.active && !q.ready && !greyed) skin_.clock(bright, grey, in, q.fraction);
    else skin_.sprite(greyed ? grey : bright, in);
    // A thin progress line along the bottom while it builds.
    if (q.active && !q.ready && !greyed) {
        const float h = std::max(2.0f, skin_.px(1.5f));
        const SDL_FRect pb{in.x, in.y + in.h - h, in.w, h};
        skin_.fill(pb, Colour{0, 0, 0, 170});
        const Colour a = skin_.accent();
        skin_.fill({pb.x, pb.y, std::round(pb.w * q.fraction), pb.h}, Colour{a.r, a.g, a.b, 255});
    }
    skin_.bezelOver(rc, st);
}

// Ready / Hold (Display__DrawNormalSideBarMenu): in the original pulsing green (r = b = 20 + t,
// g = 210 + t, t 0..40 per drawn frame), centred on a dark pill, the slot framed in the same colour.
void Sidebar::drawReadyText(const SDL_FRect& slot, const char* text) {
    const SDL_FRect rc = skin_.bezelInner(slot);
    const int t = int(SDL_GetTicks() / 16 % 41);
    const Colour c{Uint8(20 + t), Uint8(210 + t), Uint8(20 + t), 255};
    const float one = std::max(1.0f, std::round(skin_.k() * 0.75f));
    skin_.outline(rc, c, one);
    const float ts = skin_.ts();
    const float tw = skin_.textW(text, ts, true), th = skin_.textH(ts);
    const SDL_FRect back{std::round(rc.x + (rc.w - tw) / 2 - skin_.px(4)), std::round(rc.y + rc.h * 0.36f - th / 2 - skin_.px(1)),
                         std::round(tw + skin_.px(8)), std::round(th + skin_.px(2))};
    skin_.fill(back, Colour{0, 0, 0, 170});
    skin_.label(text, back.x + skin_.px(4), back.y + skin_.px(1), c, ts, true);
}

// Not original (extended production queue, re/notes/economy.md §10): a badge at the icon's
// top-left with the queued count, or an infinity mark (a lemniscate).
void Sidebar::drawQueueCount(const SDL_FRect& slot, const game::AutoQueueState& st) {
    const SDL_FRect rc = skin_.bezelInner(slot);
    const float ts = skin_.ts(), th = skin_.textH(ts);
    const std::string n = std::to_string(st.count);
    const float bw = st.infinite ? th * 1.7f : skin_.textW(n, ts, true) + skin_.px(6), bh = th + skin_.px(1);
    const SDL_FRect badge{rc.x + skin_.px(2), rc.y + skin_.px(2), std::round(bw), std::round(bh)};
    skin_.fill(badge, Colour{0, 0, 0, 190});
    skin_.outline(badge, UiSkin::kGold, std::max(1.0f, std::round(skin_.k() * 0.6f)));
    if (!st.infinite) {
        skin_.label(n, badge.x + skin_.px(3), badge.y + skin_.px(0.5f), UiSkin::kGold, ts, true);
        return;
    }
    const float cx = badge.x + bw / 2, cy = badge.y + bh / 2, a = bw * 0.38f;
    constexpr int kN = 48;
    SDL_FPoint pts[kN + 1];
    SDL_SetRenderDrawColor(skin_.renderer(), UiSkin::kGold.r, UiSkin::kGold.g, UiSkin::kGold.b, 255);
    for (float o : {-0.5f, 0.0f, 0.5f}) {
        for (int i = 0; i <= kN; ++i) {
            const float t = float(i) * 6.2831853f / kN, sn = std::sin(t), d = 1 + sn * sn;
            pts[i] = {cx + a * std::cos(t) / d, cy + o + a * 1.15f * sn * std::cos(t) / d};
        }
        SDL_RenderLines(skin_.renderer(), pts, kN + 1);
    }
}

void Sidebar::drawItem(const Item& it, const game::World& w, int side, bool hover) {
    const int house = w.sides()[side].house;
    const SDL_FRect& rc = it.rc;
    const SDL_FRect ir = skin_.bezelInner(rc);  // the picture inside the bezel
    const float ts = skin_.ts() * 0.92f;
    using B = UiSkin::Bezel;
    using S = UiSkin::State;
    const B bz = hover ? (pressed_ ? B::Pressed : B::Hot) : B::Normal;
    auto cornerText = [&](const std::string& s, bool right, bool bottom, Colour c) {
        const float tw = skin_.textW(s, ts, true), th = skin_.textH(ts);
        const float x = right ? ir.x + ir.w - tw - skin_.px(2) : ir.x + skin_.px(2);
        const float y = bottom ? ir.y + ir.h - th - skin_.px(1) : ir.y + skin_.px(1);
        skin_.fill({std::round(x - skin_.px(2)), std::round(y), std::round(tw + skin_.px(4)), std::round(th)}, Colour{0, 0, 0, 140});
        skin_.label(s, x, y, c, ts, true);
    };
    switch (it.kind) {
    case Item::Header: {
        // Section title with house-colour rules either side.
        const char* title = tr(it.section == Section::Upgrades ? "UPGRADES" : it.section == Section::Starport ? "STARPORT" : "PALACE");
        const float sc = skin_.ts();
        const float tw = skin_.textW(title, sc, true), cy = std::round(rc.y + rc.h / 2);
        const float tx = std::round(rc.x + (rc.w - tw) / 2);
        const Colour a = skin_.accent();
        const float t = std::max(1.0f, std::round(skin_.k() * 0.6f));
        skin_.fill({rc.x + skin_.px(2), cy, tx - skin_.px(5) - rc.x - skin_.px(2), t}, Colour{a.r, a.g, a.b, 200});
        skin_.fill({tx + tw + skin_.px(5), cy, rc.x + rc.w - skin_.px(2) - (tx + tw + skin_.px(5)), t}, Colour{a.r, a.g, a.b, 200});
        skin_.label(title, tx, rc.y + (rc.h - skin_.textH(sc)) / 2, UiSkin::kGold, sc, true);
        return;
    }
    case Item::Purchase: {
        const bool ok = w.starportPickedTotal(side) > 0 && !w.starportBusy(side);
        skin_.button(rc, UiSkin::Icon::Starport, !ok ? S::Disabled : hover ? (pressed_ ? S::Pressed : S::Hot) : S::Normal, tr("PURCHASE"));
        return;
    }
    case Item::Cancel: {
        const bool busy = w.starportBusy(side), picked = w.starportPickedTotal(side) > 0;
        const std::string status = busy ? std::string(tr("FRIGATE ON ITS WAY")) : std::string(tr("ORDER")) + " $" + std::to_string(w.starportOrderValue(side));
        skin_.label(status, rc.x + skin_.px(3), rc.y + (rc.h - skin_.lineH()) / 2, busy ? UiSkin::kWarn : UiSkin::kSand);
        const SDL_FRect cr{rc.x + rc.w - skin_.px(46), rc.y, skin_.px(46), rc.h};
        skin_.button(cr, UiSkin::Icon::None, !picked ? S::Disabled : hover ? S::Hot : S::Normal, tr("CANCEL"));
        return;
    }
    case Item::Building: {
        const game::QueueState bq = w.buildingQueue(side);
        game::QueueState q;
        if (bq.active && bq.type == it.type) q = bq;
        drawSlotIcon(rc, it.iconFrame, buildingIconGreyed(bq, it.type), q, house, bz);
        cornerText(std::to_string(it.cost), true, true, UiSkin::kGold);
        if (q.ready) drawReadyText(rc, tr("READY"));
        else if (q.onHold) drawReadyText(rc, tr("HOLD"));
        break;
    }
    case Item::Unit: {
        const auto queues = w.unitQueues(side);
        const game::QueueState* mine = nullptr;
        const bool busy = unitIconBusy(w, queues, it.type, &mine);
        const bool special = specialBuildUnit(w.rules().templates->bin.units[it.type].specialBehavior);
        const game::QueueState q = w.unitQueueFor(side, it.type);
        drawSlotIcon(rc, it.iconFrame, busy && !mine && (!extendedQueue || special), q, house, bz);
        if (!special) cornerText(std::to_string(it.cost), true, true, UiSkin::kGold);
        if (q.active && q.ready) drawReadyText(rc, tr("READY"));
        else if (q.active && q.onHold) drawReadyText(rc, tr("HOLD"));
        const game::AutoQueueState st = w.autoQueueFor(side, it.type);
        if (st.infinite || st.count > 1 || (extendedQueue && st.count == 1)) drawQueueCount(rc, st);
        break;
    }
    case Item::Upgrade: {
        const game::QueueState uq = w.upgradeQueue(side);
        game::QueueState q;
        if (uq.active && uq.type == it.type) q = uq;
        drawSlotIcon(rc, it.iconFrame, uq.active && uq.type != it.type, q, house, bz);
        cornerText(trf("LV %d>%d", it.level, it.level + 1), false, false, Colour{200, 255, 180, 255});
        cornerText(std::to_string(it.cost), true, true, UiSkin::kGold);
        if (q.onHold) drawReadyText(rc, tr("HOLD"));
        break;
    }
    case Item::Port: {
        game::QueueState none;
        drawSlotIcon(rc, it.iconFrame, false, none, house, it.ordered ? B::Lit : !it.available ? B::Disabled : bz);
        if (!it.available) skin_.fill(ir, Colour{0, 0, 0, 140});
        if (it.ordered) cornerText("x" + std::to_string(it.ordered), true, false, UiSkin::kGood);
        cornerText(std::to_string(it.stock) + " " + tr("LEFT"), false, false, UiSkin::kSand);
        cornerText(std::to_string(it.cost), true, true, UiSkin::kGold);
        break;
    }
    }
}

void Sidebar::drawPower(const game::World& w, int side) {
    // The original's power bar (FUN_00425860): the bottom min(percent, 200) / 200 filled in red
    // (< 51 %), yellow (< 100 %) or green; here a modern meter with a tick at 100 %.
    const int pct = std::min(200, int(w.economy(side).powerPercent));
    const Colour c = pct < 51 ? Colour{230, 70, 50, 255} : pct < 100 ? Colour{240, 200, 60, 255} : Colour{90, 220, 90, 255};
    skin_.meter(power_, pct / 200.0f, c, 0.5f);
}

void Sidebar::drawTabs(const game::World& w, int side, float mouseX, float mouseY) {
    static const UiSkin::Icon kIcons[4] = {UiSkin::Icon::Buildings, UiSkin::Icon::Infantry, UiSkin::Icon::Vehicles,
                                           UiSkin::Icon::Special};
    const game::QueueState bq = w.buildingQueue(side);
    const auto queues = w.unitQueues(side);
    const auto& T = w.rules().templates->bin;
    for (int i = 0; i < 4; ++i) {
        const Tab t = Tab(i);
        const bool empty = items(t, w, side).empty();
        const SDL_FRect rc = tabRc_[i];
        const bool sel = t == tab_;
        const bool hot = !sel && !empty && inRect(rc, mouseX, mouseY);
        // A segmented control: the selected segment lit in the house colour.
        using S = UiSkin::State;
        skin_.button(rc, kIcons[i], sel ? S::Lit : empty ? S::Disabled : hot ? S::Hot : S::Normal);
        // Progress line of the tab's running production and a badge with the ready count.
        int ready = 0;
        float progress = -1;
        if (t == Tab::Buildings && bq.active) {
            if (bq.ready) ++ready;
            else progress = bq.fraction;
        }
        if (t == Tab::Infantry || t == Tab::Vehicles || t == Tab::Special) {
            for (const auto& q : queues) {
                const auto& ut = T.units[q.type];
                const bool sp = specialBuildUnit(ut.specialBehavior);
                const Tab qt = sp ? Tab::Special : ut.isInfantry ? Tab::Infantry : Tab::Vehicles;
                if (qt != t) continue;
                if (sp && q.ready) ++ready;
                else if (!q.ready && progress < 0) progress = q.fraction;
            }
        }
        if (t == Tab::Special) {
            const game::QueueState uq = w.upgradeQueue(side);
            if (uq.active && !uq.onHold && progress < 0) progress = uq.fraction;
        }
        if (progress >= 0) {
            const float h = std::max(2.0f, skin_.px(1.5f));
            const SDL_FRect pb{rc.x + skin_.px(4), rc.y + rc.h - h - skin_.px(2), rc.w - skin_.px(8), h};
            skin_.fill(pb, Colour{0, 0, 0, 200});
            const Colour a = skin_.accent();
            skin_.fill({pb.x, pb.y, std::round(pb.w * progress), pb.h}, Colour{a.r, a.g, a.b, 255});
        }
        if (ready > 0) {
            const std::string n = std::to_string(ready);
            const float ts = skin_.ts() * 0.9f, th = skin_.textH(ts);
            const float bw = std::max(th, skin_.textW(n, ts, true) + skin_.px(4));
            const SDL_FRect b{std::round(rc.x + rc.w - bw + skin_.px(2)), std::round(rc.y - skin_.px(3)), std::round(bw), std::round(th)};
            const int pulse = int(SDL_GetTicks() / 16 % 41);
            skin_.fill({b.x - 1, b.y - 1, b.w + 2, b.h + 2}, Colour{10, 30, 6, 255});
            skin_.fill(b, Colour{Uint8(30 + pulse), Uint8(170 + pulse), Uint8(30 + pulse), 255});
            skin_.label(n, b.x + (b.w - skin_.textW(n, ts, true)) / 2, b.y, Colour{255, 255, 255, 255}, ts, true);
        }
    }
}

SDL_FRect Sidebar::arrowRect(int i) const {
    const float w = std::floor((tray_.w - skin_.px(4)) / 2);
    return {std::round(tray_.x + i * (w + skin_.px(4))), footer_.y + skin_.px(3), w, footer_.h - skin_.px(6)};
}

void Sidebar::drawFooter(float mouseX, float mouseY) {
    const int hot = arrowAt(mouseX, mouseY);
    const float maxS = std::max(0.0f, contentH_ - (tray_.h - skin_.px(6)));
    const float s = scroll_[int(tab_)];
    using S = UiSkin::State;
    for (int i = 0; i < 2; ++i) {
        const bool can = i == 0 ? s > 0 : s < maxS - 0.5f;
        skin_.button(arrowRect(i), i == 0 ? UiSkin::Icon::Up : UiSkin::Icon::Down,
                     !can ? S::Disabled : pressedArrow_ == i ? S::Pressed : hot == i ? S::Hot : S::Normal);
    }
}

int Sidebar::arrowAt(float x, float y) const {
    for (int i = 0; i < 2; ++i)
        if (inRect(arrowRect(i), x, y)) return i;
    return -1;
}

void Sidebar::draw(const SDL_FRect& area, const game::World& w, int side, float mouseX, float mouseY) {
    area_ = area;
    const float k = skin_.k();
    footer_ = SDL_FRect{area.x, area.y + area.h - skin_.px(26), area.w, skin_.px(26)};
    // The column background is the command panel's; a fine groove separates the sections.
    skin_.fill({area.x + skin_.px(6), area.y, area.w - skin_.px(12), std::max(1.0f, std::round(k * 0.6f))}, Colour{0, 0, 0, 140});
    skin_.fill({area.x + skin_.px(6), area.y + std::max(1.0f, std::round(k * 0.6f)), area.w - skin_.px(12), 1}, Colour{255, 240, 210, 30});

    // Power meter along the left edge.
    power_ = SDL_FRect{area.x + skin_.px(4), area.y + skin_.px(4), skin_.px(7), footer_.y - skin_.px(2) - (area.y + skin_.px(4))};
    drawPower(w, side);

    // Tabs: pick the first tab with something in it when the current one is empty.
    if (items(tab_, w, side).empty()) {
        for (int i = 0; i < 4; ++i)
            if (!items(Tab(i), w, side).empty()) { tab_ = Tab(i); break; }
    }
    if (tab_ != Tab::Special) section_ = Section::None;
    const float tabsX = area.x + skin_.px(15), tabsW = skin_.px(141), tabGap = skin_.px(2);
    const float tabW = std::floor((tabsW - 3 * tabGap) / 4);
    for (int i = 0; i < 4; ++i)
        tabRc_[i] = SDL_FRect{std::round(tabsX + i * (tabW + tabGap)), area.y + skin_.px(5), tabW, skin_.px(22)};
    tray_ = SDL_FRect{tabsX, area.y + skin_.px(31), tabsW, footer_.y - skin_.px(2) - (area.y + skin_.px(31))};
    skin_.well(tray_);
    drawTabs(w, side, mouseX, mouseY);

    // Grid layout (2 columns of 60:47 icons; headers / purchase rows span the tray).
    laid_ = items(tab_, w, side);
    const float pad = skin_.px(4), gap = skin_.px(3);
    const SDL_FRect inner{tray_.x + pad, tray_.y + pad, tray_.w - 2 * pad, tray_.h - 2 * pad};
    const float cellW = std::floor((inner.w - gap) / 2), cellH = std::round(cellW * 47 / 60);
    rowPitch_ = cellH + gap;
    float y = 0;
    int col = 0;
    float jumpY = -1;
    for (auto& it : laid_) {
        const bool wide = it.kind == Item::Header || it.kind == Item::Purchase || it.kind == Item::Cancel;
        if (wide) {
            if (col) { y += rowPitch_; col = 0; }
            const float h = it.kind == Item::Header ? skin_.px(16) : it.kind == Item::Purchase ? skin_.px(22) : skin_.px(16);
            if (it.kind == Item::Header && it.section == section_ && jumpY < 0) jumpY = y;
            it.rc = {inner.x, y, inner.w, h};
            y += h + gap;
            continue;
        }
        it.rc = {inner.x + col * (cellW + gap), y, cellW, cellH};
        if (++col == 2) { col = 0; y += rowPitch_; }
    }
    if (col) y += rowPitch_;
    contentH_ = y;
    const int t = int(tab_);
    if (jumpPending_ && jumpY >= 0) scroll_[t] = jumpY;
    jumpPending_ = false;
    scrollBy(0);  // clamp
    const float s = scroll_[t];
    // Clip to the tray (bezel shadows may spill into its padding).
    const SDL_Rect clip{int(tray_.x + 1), int(tray_.y + 1), int(std::ceil(tray_.w - 2)), int(std::ceil(tray_.h - 2))};
    SDL_SetRenderClipRect(skin_.renderer(), &clip);
    // Empty slots below the list fill the tray.
    if (tab_ != Tab::Special || laid_.empty()) {
        const int rows = int(std::ceil((inner.h + s) / rowPitch_));
        const int used = int(laid_.size());
        for (int i = used; i < rows * 2; ++i) {
            const SDL_FRect rc{inner.x + (i % 2) * (cellW + gap), inner.y + (i / 2) * rowPitch_ - s, cellW, cellH};
            skin_.bezelUnder(rc, UiSkin::Bezel::Empty);
            skin_.bezelOver(rc, UiSkin::Bezel::Empty);
        }
    }
    for (auto& it : laid_) {
        it.rc.y += inner.y - s;
        if (it.rc.y + it.rc.h < inner.y || it.rc.y > inner.y + inner.h) continue;
        const bool hover = it.kind != Item::Header && it.kind != Item::Cancel && inRect(it.rc, mouseX, mouseY) &&
                           inRect(inner, mouseX, mouseY);
        drawItem(it, w, side, hover || (it.kind == Item::Cancel && inRect({it.rc.x + it.rc.w - skin_.px(46), it.rc.y, skin_.px(46), it.rc.h}, mouseX, mouseY)));
    }
    if (laid_.empty()) {
        const char* msg = tr("NOTHING TO BUILD");
        const float tw = skin_.textW(msg), tx = inner.x + (inner.w - tw) / 2, ty = inner.y + rowPitch_ / 2 - skin_.lineH() / 2;
        skin_.fill({std::round(tx - skin_.px(4)), std::round(ty - skin_.px(2)), std::round(tw + skin_.px(8)),
                    std::round(skin_.lineH() + skin_.px(4))}, Colour{6, 5, 4, 220});
        skin_.label(msg, tx, ty, UiSkin::kDim);
    }
    SDL_SetRenderClipRect(skin_.renderer(), nullptr);
    // Scroll hint: a soft shadow at the tray edge that has more content beyond it.
    if (s > 0) for (int i = 0; i < 4; ++i) skin_.fill({inner.x, inner.y + i * k, inner.w, k}, Colour{0, 0, 0, Uint8(120 - 30 * i)});
    if (s + inner.h < contentH_ - 1)
        for (int i = 0; i < 4; ++i) skin_.fill({inner.x, inner.y + inner.h - (i + 1) * k, inner.w, k}, Colour{0, 0, 0, Uint8(120 - 30 * i)});
    drawFooter(mouseX, mouseY);
}

// --- input ---

SidebarAction Sidebar::click(float x, float y, int button, const game::World& w, int side) {
    if (!contains(x, y)) return {};
    const bool picked = w.starportPickedTotal(side) > 0;
    for (int i = 0; i < 4; ++i) {
        if (!inRect(tabRc_[i], x, y) || button != SDL_BUTTON_LEFT) continue;
        if (Tab(i) == tab_ || items(Tab(i), w, side).empty()) return {};
        const bool leaving = tab_ == Tab::Special;
        tab_ = Tab(i);
        section_ = Section::None;
        return leaving && picked ? SidebarAction{SidebarAction::StarportCancel} : SidebarAction{};
    }
    if (const int a = arrowAt(x, y); a >= 0) {
        pressedArrow_ = a;
        scrollBy(kArrowDir[a] * rowPitch_ * (button == SDL_BUTTON_RIGHT ? 3 : 1));  // right: a page
        return {};
    }
    if (!inRect(tray_, x, y)) return {};
    for (const auto& it : laid_) {
        if (!inRect(it.rc, x, y)) continue;
        pressed_ = button == SDL_BUTTON_LEFT;
        switch (it.kind) {
        case Item::Header: return {};
        case Item::Purchase:
            if (button == SDL_BUTTON_LEFT && picked && !w.starportBusy(side)) return {SidebarAction::StarportPurchase};
            return {};
        case Item::Cancel:
            if (x >= it.rc.x + it.rc.w - skin_.px(46) && picked) return {SidebarAction::StarportCancel};
            return {};
        case Item::Port:
            if (w.starportBusy(side)) return {};
            return {button == SDL_BUTTON_RIGHT ? SidebarAction::StarportUnpick : SidebarAction::StarportPick, it.type};
        case Item::Upgrade: {
            // Original page: left = GenerateUpgradePickOrder (starts it, or resumes it from hold),
            // right = GenerateUpgradeCancelOrder for that type (hold, then cancel).
            const game::QueueState uq = w.upgradeQueue(side);
            if (button == SDL_BUTTON_RIGHT)
                return uq.active && uq.type == it.type ? SidebarAction{SidebarAction::CancelUpgrade, it.type} : SidebarAction{};
            if (uq.active && uq.type != it.type) return {};
            return {SidebarAction::Upgrade, it.group};
        }
        case Item::Building: {
            const auto q = w.buildingQueue(side);
            const bool mine = q.active && q.type == it.type;
            if (button == SDL_BUTTON_RIGHT) return mine ? SidebarAction{SidebarAction::CancelBuilding, it.type} : SidebarAction{};
            if (mine && q.ready) return {SidebarAction::BeginPlacement, it.type};
            return {SidebarAction::BuildBuilding, it.type};
        }
        case Item::Unit: {
            // Palace weapons / air strike are built automatically and launched when ready.
            const int beh = w.rules().templates->bin.units[it.type].specialBehavior;
            if (launchable(beh)) {
                if (button == SDL_BUTTON_RIGHT || !w.unitQueueFor(side, it.type).ready) return {};
                if (beh == game::behaviour::kOrnithopter || beh == game::behaviour::kDeathHand)
                    return {SidebarAction::TargetWeapon, beh};
                return {SidebarAction::SpecialWeapon, beh};
            }
            if (button == SDL_BUTTON_RIGHT) return {SidebarAction::CancelUnit, it.type};
            return {SidebarAction::BuildUnit, it.type};
        }
        }
    }
    return {};
}

// --- names and tooltips ---

// The names the original's tooltips show (FUN_0044b5c0): Text.UIB entry of the template's group
// name (_UnitGroupTextIds / _BuildingGroupTextIds, GetTextID is case-insensitive).
std::string unitDisplayName(const game::World& w, const formats::StringTable* strings, int type) {
    const auto& T = *w.rules().templates;
    if (strings)
        if (const std::string* t = strings->findNoCase(T.bin.unitGroupNames[T.bin.units[type].unitGroup])) return *t;
    return titleCase(T.unitName(type));
}

std::string buildingDisplayName(const game::World& w, const formats::StringTable* strings, int type) {
    const auto& T = *w.rules().templates;
    if (strings)
        if (const std::string* t = strings->findNoCase(T.bin.buildingGroupNames[T.bin.buildings[type].buildingGroup]))
            return *t;
    return titleCase(T.buildingName(type));
}

// The panel's TrueType font takes UTF-8: the Text.UIB names converted from the text code page.
std::string Sidebar::unitName(const game::World& w, int type) const {
    return formats::lang::toUtf8(unitDisplayName(w, strings_, type), formats::lang::activeCodepage());
}
std::string Sidebar::buildingName(const game::World& w, int type) const {
    return formats::lang::toUtf8(buildingDisplayName(w, strings_, type), formats::lang::activeCodepage());
}

void Sidebar::drawTooltip(float x, float y, const game::World& w, int side, float maxX, float maxY) {
    if (!contains(x, y)) return;
    const Colour gold = UiSkin::kGold, hint{170, 155, 125, 255}, good = UiSkin::kGood;
    std::vector<UiSkin::TipLine> lines;
    for (int i = 0; i < 4 && lines.empty(); ++i) {
        if (!inRect(tabRc_[i], x, y)) continue;
        const int n = int(items(Tab(i), w, side).size());
        lines.push_back({tr(kTabNames[i]), gold});
        lines.push_back({n ? tr("Click to show this tab") : tr("Nothing to build here yet"), hint});
    }
    if (lines.empty() && inRect(power_, x, y)) {
        const auto eco = w.economy(side);
        lines = {{std::string(tr("Power")) + " " + std::to_string(eco.powerPercent) + "%", gold},
                 {std::string(tr("Output")) + " " + std::to_string(eco.powerOutput) + "   " + tr("Demand") + " " + std::to_string(eco.powerDemand), hint}};
    }
    if (lines.empty() && arrowAt(x, y) >= 0) lines = {{tr("Scroll"), gold}, {tr("Right click: a page"), hint}};
    if (lines.empty() && inRect(tray_, x, y)) {
        const Item* hit = nullptr;
        for (const auto& it : laid_)
            if (inRect(it.rc, x, y)) hit = &it;
        if (!hit) return;
        const Item& it = *hit;
        switch (it.kind) {
        case Item::Header:
            if (it.section == Section::Upgrades) lines = {{tr("Building upgrades"), gold}, {tr("Upgrades unlock new units and structures"), hint}};
            else if (it.section == Section::Starport) lines = {{tr("Starport"), gold}, {tr("Order units at today's prices (CHOAM)"), hint}};
            else lines = {{tr("PALACE"), gold}, {tr("Special weapons, launched when charged"), hint}};
            break;
        case Item::Purchase: lines = {{tr("Purchase"), gold}, {tr("Pay and send the frigate with the picks"), hint}}; break;
        case Item::Cancel: lines = {{tr("Cancel"), gold}, {tr("Drop all picks"), hint}}; break;
        case Item::Upgrade: {
            const game::QueueState uq = w.upgradeQueue(side);
                        // Game names are in the active code page: UTF-8 before mixing them with our labels.
            const std::string name = formats::lang::toUtf8(buildingName(w, it.type), formats::lang::activeCodepage());
            lines.push_back({trf("Upgrade %s", name.c_str()), gold});
            lines.push_back({trf("Level %d to %d", it.level, it.level + 1) + "   $" + std::to_string(it.cost), gold});
            if (uq.active && uq.type == it.type) {
                lines.push_back({uq.onHold ? tr("On hold") : trf("Upgrading %d%%", int(uq.fraction * 100)), good});
                lines.push_back({uq.onHold ? tr("Left: resume   Right: cancel") : tr("Right: hold"), hint});
            } else if (uq.active) {
                lines.push_back({tr("Another upgrade is running"), hint});
            } else {
                lines.push_back({tr("Left: upgrade"), hint});
            }
            break;
        }
        case Item::Port:
            lines.push_back({unitName(w, it.type), gold});
            lines.push_back({"$" + std::to_string(it.cost) + "   " + trf("%d in stock", it.stock), gold});
            lines.push_back({tr("Left: pick   Right: unpick"), hint});
            break;
        case Item::Building: {
            const game::QueueState q = w.buildingQueue(side);
            lines.push_back({buildingName(w, it.type), gold});
            lines.push_back({"$" + std::to_string(it.cost), gold});
            if (q.active && q.type == it.type) {
                if (q.ready) lines.push_back({tr("Ready - left click, then place it"), good});
                else if (q.onHold) lines.push_back({tr("On hold - left: resume, right: cancel"), hint});
                else lines.push_back({trf("Building %d%%   Right: hold", int(q.fraction * 100)), hint});
            } else {
                lines.push_back({tr("Left: build"), hint});
            }
            break;
        }
        case Item::Unit: {
            const int beh = w.rules().templates->bin.units[it.type].specialBehavior;
            lines.push_back({unitName(w, it.type), gold});
            if (specialBuildUnit(beh)) {
                const bool ready = w.unitQueueFor(side, it.type).ready;
                lines.push_back({ready ? tr("Ready - left click to launch") : tr("Charging (palace / airstrike)"), ready ? good : hint});
            } else {
                lines.push_back({"$" + std::to_string(it.cost), gold});
                if (extendedQueue) lines.push_back({tr("Left: +1  Shift: +10  Right: repeat"), hint});
                else lines.push_back({tr("Left: build   Right: hold / cancel"), hint});
            }
            break;
        }
        }
    }
    skin_.tooltip(lines, x, y, maxX, maxY);
}

}  // namespace d2k::render

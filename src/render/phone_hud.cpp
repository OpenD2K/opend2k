#include "render/phone_hud.hpp"

#include "render/build_icons.hpp"
#include "render/ui_text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace d2k::render {

namespace {

bool inRect(const SDL_FRect& r, float x, float y) { return r.w > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

// The visual rectangle grown to a touch area of at least `minW` x `minH`, centred on it.
SDL_FRect grow(const SDL_FRect& v, float minW, float minH) {
    const float w = std::max(v.w, minW), h = std::max(v.h, minH);
    return {v.x + v.w / 2 - w / 2, v.y + v.h / 2 - h / 2, w, h};
}

constexpr Colour kGlyph{240, 226, 198, 255};
constexpr Colour kGlyphDim{150, 140, 124, 200};

}  // namespace

void PhoneHud::layout(const Params& p, const game::World& w, int side) {
    p_ = p;
    pt_ = p.ppp;
    icon_ = std::round(p.iconPt * pt_);
    const float P = pt_, gap = std::round(8 * P), minT = 44 * P;
    const float barH = icon_ + std::round(6 * P);
    const SDL_FRect& sa = p.safe;
    bar_ = {0, 0, float(p.outW), sa.y + barH};
    elems_.clear();
    drawerBtns_.clear();
    radar_ = drawer_ = sideArea_ = tab_ = tabTouch_ = credits_ = power_ = timer_ = SDL_FRect{};
    const float by = sa.y + std::round(3 * P);
    const float touchBottom = std::max(bar_.y + bar_.h + std::round(10 * P), minT);
    // A bar element: the touch area reaches the top edge and below the bar, >= 44 points wide.
    auto barElem = [&](const SDL_FRect& v, PhoneHudAction::Kind k, int type = -1) {
        Elem e;
        e.v = v;
        const SDL_FRect g = grow(v, minT, minT);
        e.t = {g.x, 0, g.w, touchBottom};
        e.kind = k;
        e.type = type;
        elems_.push_back(e);
        return &elems_.back();
    };

    if (p.hidden) {
        const float s = std::round(icon_ * 0.8f);
        Elem e;
        e.v = {sa.x + sa.w - s - std::round(4 * P), by, s, s};
        e.t = {e.v.x - 14 * P, 0, float(p.outW) - (e.v.x - 14 * P), std::max(e.v.y + e.v.h + 14 * P, minT)};
        e.kind = PhoneHudAction::ShowUi;
        elems_.push_back(e);
        bar_ = {};
        return;
    }

    // --- left group: menu, click-mode chip, credits, power, timer ---
    float x = sa.x + std::round(4 * P);
    barElem({x, by, icon_, icon_}, PhoneHudAction::Menu);
    x += icon_ + gap;
    modeLabel_.clear();
    if (p.placing) modeLabel_ = tr("PLACE");
    else if (p.cursorMode == 2) modeLabel_ = tr("REPAIR");
    else if (p.cursorMode == 3) modeLabel_ = tr("SELL");
    else if (p.cursorMode == 4 || p.cursorMode == 5) modeLabel_ = tr("TARGET");
    if (!modeLabel_.empty()) {
        const float ts = icon_ * 0.5f / 10;
        const float wChip = std::round(icon_ * 0.8f + skin_.textW(modeLabel_, ts, true) + 10 * P);
        barElem({x, by, wChip, icon_}, PhoneHudAction::CancelMode);
        x += wChip + gap;
    }
    const auto eco = w.economy(side);
    {
        const float ts = icon_ * 0.56f / 10;
        const std::string cr = std::to_string(std::clamp(eco.credits, 0, 9999999));
        const float tw = std::max(skin_.textW(cr, ts, true), skin_.textW("00000", ts, true));
        credits_ = {x, by, std::round(icon_ * 0.5f + 4 * P + tw), icon_};
        x += credits_.w + gap;
    }
    power_ = {x, by, std::round(icon_ * 0.6f + 30 * P), icon_};
    x += power_.w + gap;
    timerShown_ = w.missionTimer() >= 0;
    if (timerShown_) {
        const float ts = icon_ * 0.52f / 10;
        timer_ = {x, by, std::round(skin_.textW("00:00", ts, true) + 4 * P), icon_};
        x += timer_.w + gap;
    }
    const float leftEnd = x;

    // --- right group (right to left): hide UI, radar, specials ---
    float xr = sa.x + sa.w - std::round(4 * P);
    xr -= icon_;
    barElem({xr, by, icon_, icon_}, PhoneHudAction::HideUi);
    xr -= gap + icon_;
    barElem({xr, by, icon_, icon_}, PhoneHudAction::Radar);
    const float slotW = std::round(icon_ * 60 / 47), slotGap = std::round(4 * P);
    const auto& T = w.rules().templates->bin;
    const auto opts = w.buildableUnits(side);
    std::vector<game::BuildOption> specials, quick;
    for (const auto& o : opts) {
        if (specialBuildUnit(T.units[o.type].specialBehavior)) specials.push_back(o);
        else quick.push_back(o);
    }
    xr -= gap;
    if (specials.size() > 3) specials.resize(3);
    for (auto it = specials.rbegin(); it != specials.rend(); ++it) {
        xr -= slotW;
        Elem* e = barElem({xr, by, slotW, icon_}, PhoneHudAction::Special, it->type);
        e->beh = T.units[it->type].specialBehavior;
        e->frame = it->iconFrame;
        xr -= slotGap;
    }
    const float rightStart = xr - gap;

    // --- quick-build strip: pinned first (pin order), then the most ordered, then sidebar order ---
    auto pinIndex = [&](int type) {
        const auto f = std::find(p.pinned.begin(), p.pinned.end(), type);
        return f == p.pinned.end() ? 1 << 20 : int(f - p.pinned.begin());
    };
    auto count = [&](int type) {
        if (!p.counts) return 0;
        const auto f = p.counts->find(type);
        return f == p.counts->end() ? 0 : f->second;
    };
    std::stable_sort(quick.begin(), quick.end(), [&](const game::BuildOption& a, const game::BuildOption& b) {
        const int pa = pinIndex(a.type), pb = pinIndex(b.type);
        if (pa != pb) return pa < pb;
        return count(a.type) > count(b.type);
    });
    if (p.freezeOrder && !lastOrder_.empty()) {  // known types keep their places, new ones follow
        auto last = [&](int type) {
            const auto f = std::find(lastOrder_.begin(), lastOrder_.end(), type);
            return f == lastOrder_.end() ? 1 << 20 : int(f - lastOrder_.begin());
        };
        std::stable_sort(quick.begin(), quick.end(), [&](const game::BuildOption& a, const game::BuildOption& b) { return last(a.type) < last(b.type); });
    }
    lastOrder_.clear();
    for (const auto& o : quick) lastOrder_.push_back(o.type);
    int n = std::min<int>(p.maxQuick, int(quick.size()));
    const float avail = rightStart - leftEnd;
    while (n > 0 && n * slotW + (n - 1) * slotGap > avail) --n;
    if (n > 0) {
        const float total = n * slotW + (n - 1) * slotGap;
        float x0 = std::round(p.outW / 2.0f - total / 2);  // centred on the screen when there is room
        if (x0 < leftEnd || x0 + total > rightStart) x0 = std::round(leftEnd + (avail - total) / 2);
        for (int i = 0; i < n; ++i) {
            Elem* e = barElem({x0 + i * (slotW + slotGap), by, slotW, icon_}, PhoneHudAction::BuildUnit, quick[std::size_t(i)].type);
            e->frame = quick[std::size_t(i)].iconFrame;
        }
    }

    // --- radar (top left, under the bar; the map's aspect) ---
    if (p.radarActive && p.radarMode != 2) {
        const float size = std::round(icon_ * (p.radarMode == 1 ? 6.8f : 3.8f));
        const float mw = float(std::max(1, p.mapW)), mh = float(std::max(1, p.mapH));
        float rw = size, rh = size;
        if (mw >= mh) rh = std::round(size * mh / mw);
        else rw = std::round(size * mw / mh);
        const float maxH = sa.y + sa.h - barBottom() - icon_ * 2.6f;
        if (rh > maxH && maxH > 0) {
            rw = std::round(rw * maxH / rh);
            rh = std::round(maxH);
        }
        // Below the bar's touch areas, so a tap near the menu button never lands on both.
        radar_ = {sa.x + std::round(6 * P), std::max(barBottom() + std::round(6 * P), touchBottom + std::round(2 * P)), rw, rh};
    }

    // --- drawer (right edge) and its pull tab ---
    const float tabW = std::round(icon_ * 0.6f), tabH = std::round(icon_ * 2.0f);
    const float midY = std::round((barBottom() + sa.y + sa.h) / 2 - tabH / 2);
    if (p.drawerOpen) {
        const float x0 = std::round(sa.x + sa.w - p.drawerW);
        drawer_ = {x0, barBottom(), float(p.outW) - x0, float(p.outH) - barBottom()};
        const float bw = std::floor((p.drawerW - skin_.px(8) - 5 * skin_.px(2)) / 6), bh = skin_.px(22);
        static const PhoneHudAction::Kind kinds[6] = {PhoneHudAction::Repair,  PhoneHudAction::Sell,     PhoneHudAction::Guard,
                                                      PhoneHudAction::Retreat, PhoneHudAction::Upgrades, PhoneHudAction::Starport};
        for (int i = 0; i < 6; ++i) {
            Elem e;
            e.v = {x0 + skin_.px(4) + i * (bw + skin_.px(2)), drawer_.y + skin_.px(4), bw, bh};
            e.t = {e.v.x - skin_.px(1), e.v.y - skin_.px(4), bw + skin_.px(2), bh + skin_.px(6)};
            e.kind = kinds[i];
            drawerBtns_.push_back(e);
        }
        const float sy = drawer_.y + skin_.px(30);
        sideArea_ = {x0, sy, p.drawerW, std::max(skin_.px(60), sa.y + sa.h - sy)};
        tab_ = {x0 - tabW, midY, tabW, tabH};
        tabTouch_ = {tab_.x - icon_, tab_.y - icon_, tabW + icon_, tabH + 2 * icon_};
    } else {
        tab_ = {sa.x + sa.w - tabW, midY, tabW, tabH};
        // Reaches the screen edge (also the unsafe strip beyond the notch side).
        tabTouch_ = {tab_.x - icon_ * 0.9f, tab_.y - icon_, float(p.outW) - (tab_.x - icon_ * 0.9f), tabH + 2 * icon_};
    }
}

std::vector<int> PhoneHud::quickTypes() const {
    std::vector<int> out;
    for (const auto& e : elems_)
        if (e.kind == PhoneHudAction::BuildUnit) out.push_back(e.type);
    return out;
}

const PhoneHud::Elem* PhoneHud::elemAt(float x, float y) const {
    const Elem* best = nullptr;
    float bestD = 1e30f;
    for (const auto& e : elems_) {
        if (!inRect(e.t, x, y)) continue;
        const float dx = x - (e.v.x + e.v.w / 2), dy = y - (e.v.y + e.v.h / 2);
        // Distance to the visual rectangle (0 inside), so a wide chip wins inside itself.
        const float ex = std::max(0.0f, std::abs(dx) - e.v.w / 2), ey = std::max(0.0f, std::abs(dy) - e.v.h / 2);
        const float d = ex * ex + ey * ey;
        if (d < bestD) {
            bestD = d;
            best = &e;
        }
    }
    return best;
}

bool PhoneHud::inDrawer(float x, float y) const { return p_.drawerOpen && !p_.hidden && inRect(drawer_, x, y); }

bool PhoneHud::onDrawerTab(float x, float y) const { return !p_.hidden && inRect(tabTouch_, x, y); }

bool PhoneHud::contains(float x, float y) const {
    if (elemAt(x, y)) return true;
    if (p_.hidden) return false;
    if (inRect(bar_, x, y) || inRect(tabTouch_, x, y)) return true;
    for (const auto& e : drawerBtns_)
        if (inRect(e.t, x, y)) return true;
    return false;
}

int PhoneHud::quickTypeAt(float x, float y) const {
    const Elem* e = elemAt(x, y);
    return e && e->kind == PhoneHudAction::BuildUnit ? e->type : -1;
}

PhoneHudAction PhoneHud::hit(float x, float y, int button) const {
    if (!p_.hidden) {
        for (const auto& e : drawerBtns_)
            if (inRect(e.t, x, y)) return button == SDL_BUTTON_LEFT ? PhoneHudAction{e.kind} : PhoneHudAction{PhoneHudAction::Absorb};
        if (inRect(tabTouch_, x, y) && !elemAt(x, y)) return {button == SDL_BUTTON_LEFT ? PhoneHudAction::Drawer : PhoneHudAction::Absorb};
    }
    const Elem* e = elemAt(x, y);
    if (!e) return {inRect(bar_, x, y) ? PhoneHudAction::Absorb : PhoneHudAction::None};
    if (e->kind == PhoneHudAction::BuildUnit) return {button == SDL_BUTTON_RIGHT ? PhoneHudAction::CancelUnit : PhoneHudAction::BuildUnit, e->type};
    if (button != SDL_BUTTON_LEFT) return {PhoneHudAction::Absorb};
    return {e->kind, e->type};
}

// --- drawing ---

void PhoneHud::progressRing(const SDL_FRect& r, float frac, Colour c, float t) {
    // Clockwise around the rectangle from the top centre.
    const float w = r.w, h = r.h, L = 2 * (w + h);
    float left = std::clamp(frac, 0.0f, 1.0f) * L;
    auto seg = [&](float len, auto&& draw) {
        const float d = std::min(left, len);
        if (d > 0) draw(d);
        left -= d;
    };
    seg(w / 2, [&](float d) { skin_.fill({r.x + w / 2, r.y, d, t}, c); });
    seg(h, [&](float d) { skin_.fill({r.x + w - t, r.y, t, d}, c); });
    seg(w, [&](float d) { skin_.fill({r.x + w - d, r.y + h - t, d, t}, c); });
    seg(h, [&](float d) { skin_.fill({r.x, r.y + h - d, t, d}, c); });
    seg(w / 2, [&](float d) { skin_.fill({r.x, r.y, d, t}, c); });
}

void PhoneHud::drawUnitIcon(const Elem& e, const game::World& w, int side, bool special, bool extendedQueue) {
    const SDL_FRect& rc = e.v;
    const float P = pt_;
    const int house = w.sides()[side].house;
    const auto queues = w.unitQueues(side);
    const game::QueueState* mine = nullptr;
    const bool busy = unitIconBusy(w, queues, e.type, &mine);
    const bool greyed = busy && !mine && (!extendedQueue || special);
    const game::QueueState q = w.unitQueueFor(side, e.type);
    const Sprite* bright = icons_.get(e.frame, house);
    const Sprite* grey = icons_.get(e.frame, house, true);
    skin_.fill(rc, Colour{0, 0, 0, 120});
    const float in = std::max(1.0f, std::round(P));
    const SDL_FRect ir{rc.x + in, rc.y + in, rc.w - 2 * in, rc.h - 2 * in};
    const bool building = q.active && !q.ready && !greyed;
    if (building) skin_.clock(bright, grey, ir, q.fraction);
    else skin_.sprite(special && !q.ready ? grey : greyed ? grey : bright, ir);
    const float t = std::max(1.0f, std::round(1.0f * P));
    skin_.outline(rc, Colour{255, 236, 200, 70}, t);
    if (building) {
        const Colour a = skin_.accent();
        progressRing(rc, q.fraction, Colour{a.r, a.g, a.b, 255}, std::max(2.0f, std::round(1.6f * P)));
    }
    const float ts = icon_ * 0.36f / 10;
    if (q.active && q.ready) {  // READY: the original's pulsing green
        const int pulse = int(SDL_GetTicks() / 16 % 41);
        skin_.outline(rc, Colour{Uint8(20 + pulse), Uint8(210 + pulse), Uint8(20 + pulse), 255}, std::max(2.0f, std::round(1.6f * P)));
    } else if (q.active && q.onHold) {
        const char* hold = tr("HOLD");
        const float tw = skin_.textW(hold, ts, true), th = skin_.textH(ts);
        const SDL_FRect b{std::round(rc.x + (rc.w - tw) / 2 - 2 * P), std::round(rc.y + (rc.h - th) / 2), std::round(tw + 4 * P), std::round(th)};
        skin_.fill(b, Colour{0, 0, 0, 170});
        skin_.label(hold, b.x + 2 * P, b.y, Colour{60, 240, 60, 255}, ts, true);
    }
    if (special) {  // a small gold star tells the palace / air strike slot from the build strip
        const float sz = std::round(icon_ * 0.38f);
        skin_.glyph({rc.x - sz * 0.2f, rc.y - sz * 0.25f, sz, sz}, UiSkin::Icon::Special, UiSkin::kGold);
    }
    if (!special) {
        const game::AutoQueueState st = w.autoQueueFor(side, e.type);
        if (st.infinite || st.count > 1 || (extendedQueue && st.count == 1)) {
            const float th = skin_.textH(ts);
            const std::string nstr = std::to_string(st.count);
            const float bw = st.infinite ? th * 1.6f : skin_.textW(nstr, ts, true) + 4 * P;
            const SDL_FRect b{std::round(rc.x + rc.w - bw), std::round(rc.y + rc.h - th), std::round(bw), std::round(th)};
            skin_.fill(b, Colour{0, 0, 0, 200});
            if (!st.infinite) {
                skin_.label(nstr, b.x + 2 * P, b.y, UiSkin::kGold, ts, true);
            } else {  // a lemniscate
                const float cx = b.x + b.w / 2, cy = b.y + b.h / 2, a = b.w * 0.38f;
                constexpr int kN = 32;
                SDL_FPoint pts[kN + 1];
                SDL_SetRenderDrawColor(skin_.renderer(), UiSkin::kGold.r, UiSkin::kGold.g, UiSkin::kGold.b, Uint8(255 * skin_.alpha()));
                for (float o : {-0.5f, 0.0f, 0.5f}) {
                    for (int i = 0; i <= kN; ++i) {
                        const float tt = float(i) * 6.2831853f / kN, sn = std::sin(tt), d = 1 + sn * sn;
                        pts[i] = {cx + a * std::cos(tt) / d, cy + o + a * 1.15f * sn * std::cos(tt) / d};
                    }
                    SDL_RenderLines(skin_.renderer(), pts, kN + 1);
                }
            }
        }
        if (std::find(p_.pinned.begin(), p_.pinned.end(), e.type) != p_.pinned.end()) {
            const float s = std::round(icon_ * 0.42f);
            skin_.glyph({rc.x - s * 0.2f, rc.y - s * 0.25f, s, s}, UiSkin::Icon::Pin, UiSkin::kGold);
        }
    }
}

void PhoneHud::drawDrawer(const game::World& w, int side, bool upgradesLit, bool starportLit, float mouseX, float mouseY) {
    if (!p_.drawerOpen || p_.hidden) return;
    skin_.panel(drawer_);
    static const UiSkin::Icon icons[6] = {UiSkin::Icon::Repair,  UiSkin::Icon::Sell,    UiSkin::Icon::Guard,
                                          UiSkin::Icon::Retreat, UiSkin::Icon::Upgrade, UiSkin::Icon::Starport};
    const bool canUpgrade = !w.upgradableBuildings(side).empty() || w.upgradeQueue(side).active;
    const bool port = w.starportOpen(side);
    for (std::size_t i = 0; i < drawerBtns_.size(); ++i) {
        const Elem& e = drawerBtns_[i];
        using S = UiSkin::State;
        const bool on = (e.kind != PhoneHudAction::Upgrades || canUpgrade) && (e.kind != PhoneHudAction::Starport || port);
        const bool lit = (e.kind == PhoneHudAction::Repair && p_.cursorMode == 2) || (e.kind == PhoneHudAction::Sell && p_.cursorMode == 3) ||
                         (e.kind == PhoneHudAction::Upgrades && upgradesLit) || (e.kind == PhoneHudAction::Starport && starportLit);
        const bool hot = inRect(e.v, mouseX, mouseY);
        skin_.button(e.v, icons[i], !on ? S::Disabled : lit ? S::Lit : hot ? S::Hot : S::Normal);
    }
}

void PhoneHud::draw(const game::World& w, int side, float alpha, bool extendedQueue) {
    const float P = pt_;
    skin_.setAlpha(alpha);
    if (p_.hidden) {  // only the show tab
        for (const auto& e : elems_) {
            skin_.fill({e.v.x - 3 * P, e.v.y - 3 * P, e.v.w + 6 * P, e.v.h + 6 * P}, Colour{0, 0, 0, 90});
            skin_.glyph(e.v, UiSkin::Icon::Eye, Colour{240, 226, 198, 200});
        }
        skin_.setAlpha(1);
        return;
    }
    // The bar: a dark band fading downwards, a hairline at its bottom.
    const int strips = 6;
    for (int i = 0; i < strips; ++i) {
        const float y0 = bar_.y + bar_.h * i / strips, y1 = bar_.y + bar_.h * (i + 1) / strips;
        skin_.fill({bar_.x, std::round(y0), bar_.w, std::round(y1) - std::round(y0)}, Colour{8, 6, 4, Uint8(150 - 12 * i)});
    }
    skin_.fill({bar_.x, bar_.y + bar_.h - std::max(1.0f, std::round(P * 0.5f)), bar_.w, std::max(1.0f, std::round(P * 0.5f))},
               Colour{255, 226, 170, 50});
    // Drawer tab.
    {
        skin_.fill(tab_, Colour{10, 8, 6, 150});
        const Colour a = skin_.accent();
        skin_.outline(tab_, Colour{a.r, a.g, a.b, 140}, std::max(1.0f, std::round(P)));
        const float g = std::round(tab_.w * 0.95f);
        skin_.glyph({tab_.x + (tab_.w - g) / 2, tab_.y + (tab_.h - g) / 2, g, g},
                    p_.drawerOpen ? UiSkin::Icon::ChevronRight : UiSkin::Icon::ChevronLeft, kGlyph);
    }
    const auto eco = w.economy(side);
    for (const auto& e : elems_) {
        switch (e.kind) {
        case PhoneHudAction::Menu: skin_.glyph(e.v, UiSkin::Icon::Menu, kGlyph); break;
        case PhoneHudAction::HideUi: skin_.glyph(e.v, UiSkin::Icon::EyeOff, kGlyph); break;
        case PhoneHudAction::Radar:
            skin_.glyph(e.v, UiSkin::Icon::Radar, !p_.radarActive ? kGlyphDim : p_.radarMode == 1 ? skin_.accent() : p_.radarMode == 2 ? kGlyphDim : kGlyph);
            break;
        case PhoneHudAction::CancelMode: {
            const Colour a = skin_.accent();
            skin_.fill(e.v, Colour{a.r, a.g, a.b, 90});
            skin_.outline(e.v, Colour{a.r, a.g, a.b, 230}, std::max(1.0f, std::round(P)));
            const float g = std::round(icon_ * 0.62f);
            skin_.glyph({e.v.x + 4 * P, e.v.y + (e.v.h - g) / 2, g, g}, UiSkin::Icon::Close, kGlyph);
            const float ts = icon_ * 0.5f / 10;
            skin_.label(modeLabel_, e.v.x + 6 * P + g, e.v.y + (e.v.h - skin_.textH(ts)) / 2, Colour{255, 240, 210, 255}, ts, true);
            break;
        }
        case PhoneHudAction::BuildUnit: drawUnitIcon(e, w, side, false, extendedQueue); break;
        case PhoneHudAction::Special: drawUnitIcon(e, w, side, true, extendedQueue); break;
        default: break;
        }
    }
    // Credits: a spice gem and the amount.
    {
        const float cy = credits_.y + credits_.h / 2, d = icon_ * 0.2f, gx = credits_.x + icon_ * 0.22f;
        SDL_Vertex v[4];
        const float al = skin_.alpha();
        const SDL_FColor top{1.0f, 0.86f, 0.5f, al}, bot{0.75f, 0.42f, 0.12f, al};
        v[0] = {{gx, cy - d}, top, {0, 0}};
        v[1] = {{gx + d * 0.8f, cy}, bot, {0, 0}};
        v[2] = {{gx, cy + d}, bot, {0, 0}};
        v[3] = {{gx - d * 0.8f, cy}, top, {0, 0}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(skin_.renderer(), nullptr, v, 4, idx, 6);
        const float ts = icon_ * 0.56f / 10;
        const std::string cr = std::to_string(std::clamp(eco.credits, 0, 9999999));
        skin_.label(cr, credits_.x + icon_ * 0.5f + 2 * P, cy - skin_.textH(ts) / 2, Colour{255, 214, 130, 255}, ts, true);
    }
    // Power: a bolt and a short bar (the original's percent / 200 fill, red / yellow / green).
    {
        const int pct = std::min(200, int(eco.powerPercent));
        const Colour c = pct < 51 ? Colour{235, 80, 56, 255} : pct < 100 ? Colour{240, 200, 60, 255} : Colour{100, 225, 100, 255};
        const float g = std::round(icon_ * 0.6f);
        skin_.glyph({power_.x, power_.y + (power_.h - g) / 2, g, g}, UiSkin::Icon::Bolt, c);
        const float bh = std::max(3.0f, std::round(icon_ * 0.22f)), bx = power_.x + g + 3 * P, bw = power_.x + power_.w - bx;
        const SDL_FRect bar{bx, std::round(power_.y + (power_.h - bh) / 2), bw, bh};
        skin_.fill(bar, Colour{0, 0, 0, 170});
        skin_.fill({bar.x, bar.y, std::round(bar.w * pct / 200.0f), bar.h}, c);
        skin_.fill({std::round(bar.x + bar.w / 2), bar.y - P, std::max(1.0f, std::round(P)), bar.h + 2 * P}, Colour{255, 255, 255, 150});
    }
    if (timerShown_) {
        const int ticks = w.missionTimer();
        char buf[16];
        std::snprintf(buf, sizeof buf, "%02d:%02d", ticks / 1500, ticks / 25 % 60);
        const float ts = icon_ * 0.52f / 10;
        skin_.label(buf, timer_.x + 2 * P, timer_.y + (timer_.h - skin_.textH(ts)) / 2, Colour{255, 90, 64, 255}, ts, true);
    }
    skin_.setAlpha(1);
}

}  // namespace d2k::render

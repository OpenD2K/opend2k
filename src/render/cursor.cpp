#include "render/cursor.hpp"

#include "render/sprites.hpp"
#include "render/upscale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace d2k::render {

const char* cursorName(Cursor c) {
    static const char* const kNames[kCursorCount] = {
        "arrow",      "move",       "attack",     "no-move",    "enter",      "select",     "radar-move",
        "no-sell",    "no-repair",  "air-strike", "sell",       "repair",     "deploy",     "no-deploy",
        "scroll-n",   "scroll-ne",  "scroll-e",   "scroll-se",  "scroll-s",   "scroll-sw",  "scroll-w",
        "scroll-nw",  "noscroll-n", "noscroll-ne", "noscroll-e", "noscroll-se", "noscroll-s", "noscroll-sw",
        "noscroll-w", "noscroll-nw", "death-hand", "sabotage",  "infantry-move"};
    const int i = static_cast<int>(c);
    return i >= 0 && i < kCursorCount ? kNames[i] : "?";
}

// The cursor half of HandleBattlefieldClick 0x4433a0 (re/notes/ui.md "Cursors"). The helpers
// mirror the small selection queries at 0x4a57f0..0x4a5a10 (all over MySideID's unit list).
Cursor pickCursor(const game::World& w, const CursorQuery& q) {
    using namespace game;
    namespace cf = cell_flags;
    if (q.side < 0 || q.side >= kMaxSides) return Cursor::Arrow;
    const Side& me = w.sides()[q.side];
    auto beh = [&](const Unit& u) { return int(w.unitTemplate(u).specialBehavior); };
    auto anySel = [&](auto&& pred) {
        for (int i = me.firstUnit; i != -1; i = me.nextObject(i))
            if (me.units[i].selected && pred(me.units[i])) return true;
        return false;
    };
    const bool unitsSelected = anySel([](const Unit&) { return true; });  // IsAnyUnitSelected
    auto armed = [&](const Unit& u) { return w.unitTemplate(u).primaryWeapon != -1; };  // IsAnyArmedUnitSelected 0x4a58b0
    auto withBehaviour = [&](int b) { return anySel([&](const Unit& u) { return beh(u) == b; }); };  // 0x4a57f0
    auto allBehaviour = [&](int b) {  // AllSelectedUnitsHaveBehavior 0x4a59c0
        bool any = false;
        for (int i = me.firstUnit; i != -1; i = me.nextObject(i)) {
            if (!me.units[i].selected) continue;
            if (beh(me.units[i]) != b) return false;
            any = true;
        }
        return any;
    };

    // Outside the battlefield (sidebar, options bar): the pointer, or the radar-move ring over
    // a working radar while units are selected.
    if (!q.overBattlefield) return q.overRadar && unitsSelected ? Cursor::RadarMove : Cursor::Arrow;

    const int tx = q.worldX >> 5, ty = q.worldY >> 5;
    if (!w.inBounds(tx, ty)) return Cursor::Arrow;
    const std::uint32_t f = w.cell(tx, ty).flags;
    const int owner = static_cast<int>(f & cf::kOwnerMask);
    int bs = -1, bi = -1, us = -1, ui = -1;
    auto bBeh = [&](const Building& b) { return int(w.buildingTemplate(b).specialBehavior); };

    switch (q.mode) {
    case 0: break;
    case 2: {  // repair: own damaged repairable building, or own damaged vehicle when a repair pad exists
        if (const Building* b = w.buildingAt(tx, ty, bs, bi)) {
            const auto& t = w.buildingTemplate(*b);
            return bs == q.side && b->health < std::int32_t(t.hitPoints) && (t.flags & building_flags::kSelectable)
                       ? Cursor::Repair
                       : Cursor::NoRepair;
        }
        const Unit* u = w.unitAt(tx << 5, ty << 5, us, ui, false);
        if (!u || us != q.side) return Cursor::NoRepair;
        bool pad = false;  // IsBuildingWithBehaviorBuilt(MySideID, 6)
        for (int i = me.firstBuilding; i != -1 && !pad; i = me.nextObject(i))
            pad = bBeh(me.buildings[i]) == building_behaviour::kRepairPad;
        const auto& t = w.unitTemplate(*u);
        return pad && u->health < std::int32_t(t.hitPoints) && !t.isInfantry ? Cursor::Repair : Cursor::NoRepair;
    }
    case 3: {  // sell: any own building
        const Building* b = w.buildingAt(tx, ty, bs, bi);
        return b && bs == q.side ? Cursor::Sell : Cursor::NoSell;
    }
    case 4: return Cursor::AirStrike;
    case 5: return Cursor::DeathHand;
    default: return Cursor::Arrow;  // 1: placing a building (the footprint markers show the rest)
    }

    if (q.shroud && w.shrouded(tx, ty)) return unitsSelected ? Cursor::Move : Cursor::Arrow;

    if (!unitsSelected) {
        // Nothing but maybe a building selected: select hints; a selected own turret shows
        // where it can fire (GetSelectedBuildingWithBarrel 0x4a5860).
        const Building* turret = nullptr;
        bool buildingSelected = false;
        for (int i = me.firstBuilding; i != -1; i = me.nextObject(i)) {
            const Building& b = me.buildings[i];
            if (!b.selected) continue;
            buildingSelected = true;
            if (!turret && w.buildingTemplate(b).barrelArt != -1) turret = &b;
        }
        if (!(f & cf::kOccupied)) return Cursor::Arrow;
        // A wall/turret anchor tile only counts when it is a turret.
        auto wallOrTurret = [&](Cursor otherwise) {
            const Building* b = w.buildingAt(tx, ty, bs, bi);
            return b && bBeh(*b) == building_behaviour::kTurret ? Cursor::Select : otherwise;
        };
        if (turret) {
            if (w.diplomacy(q.side, owner) != 0 || q.ctrl) {
                const Building* b = w.buildingAt(tx, ty, bs, bi);
                if (!b) return w.buildingCanAttackTile(*turret, tx, ty) ? Cursor::Attack : Cursor::Arrow;
                if ((b->selected && bs == q.side) || !w.buildingInRangeOf(*turret, *b)) return Cursor::Arrow;
                return Cursor::Attack;
            }
            if (!(f & cf::kWall)) return Cursor::Select;
            return wallOrTurret(Cursor::Arrow);
        }
        if (!buildingSelected) {
            if (!(f & cf::kWall)) {
                if (owner == q.side || !(f & cf::kAnyUnit)) return Cursor::Select;
                return w.unitAt(tx << 5, ty << 5, us, ui, true) ? Cursor::Select : Cursor::Arrow;  // cloaked: no
            }
            return wallOrTurret(Cursor::Arrow);
        }
        const Building* b = w.buildingAt(tx, ty, bs, bi);
        if (!b) return Cursor::Select;
        if (!b->selected || bs != q.side) {
            if (!(f & cf::kWall)) return Cursor::Select;
            return wallOrTurret(Cursor::Arrow);
        }
        // The selected building itself: "make primary" when another of its kind exists
        // (MoreProductionBuildingsOfSameGroupExist 0x4432f0) and it is not primary yet.
        const auto& t = w.buildingTemplate(*b);
        bool producer = false;
        switch (t.specialBehavior) {
        case 2: case 7: case 8: case 9: case 0xb: case 0xc: case 0xd: case 0x11: case 0x12: case 0x13: producer = true; break;
        default: break;
        }
        const bool several = t.buildingGroup < me.buildingsExistPerGroup.size() && me.buildingsExistPerGroup[t.buildingGroup] > 1;
        return producer && several && !(b->flags & building_flags::kPrimary) ? Cursor::Deploy : Cursor::Arrow;
    }

    // Units selected. Cloaked enemy units are invisible to the cursor.
    const bool enemyUnitsHere = owner != q.side && (f & cf::kAnyUnit);
    const Unit* u = w.unitAt(q.worldX, q.worldY, us, ui, enemyUnitsHere);
    if (u && (u->flags & unit_flags::kExcluded)) u = nullptr;
    if (u && (u->flags & unit_flags::kCloaked) && w.diplomacy(q.side, us) != 0) u = nullptr;

    if (!u) {
        const Building* b = w.buildingAt(tx, ty, bs, bi);
        if (!b) {  // open ground
            if (q.ctrl && anySel(armed)) return Cursor::Attack;
            if (!(f & cf::kDriveOn)) {
                return (f & cf::kWalkOn) && anySel([&](const Unit& x) { return w.unitTemplate(x).isInfantry != 0; })
                           ? Cursor::InfantryMove
                           : Cursor::NoMove;
            }
            if (!(f & cf::kWalkOn)) return Cursor::NoMove;
            if ((f & cf::kSpiceMask) && allBehaviour(behaviour::kHarvester)) return Cursor::Attack;  // harvest
            return Cursor::Move;
        }
        const int bb = bBeh(*b);
        auto friendly = [&] {
            if (!(f & cf::kWall)) return Cursor::Select;
            return bb == building_behaviour::kTurret ? Cursor::Select : Cursor::NoMove;
        };
        if (bs == q.side && !q.ctrl) {
            // Modern: a right click on an own building just moves; the left click selects it.
            const Cursor enter = q.modernControls ? Cursor::Select : Cursor::Enter;
            if (bb == building_behaviour::kRepairPad && anySel([&](const Unit& x) { return !w.unitTemplate(x).isInfantry; }))
                return enter;
            if (allBehaviour(behaviour::kHarvester) && bb == building_behaviour::kRefinery) return enter;
            return friendly();
        }
        if (w.diplomacy(q.side, bs) == 0 && !q.ctrl) return friendly();
        if (anySel(armed)) return Cursor::Attack;
        if (withBehaviour(behaviour::kSaboteur))
            return bs != q.side && (b->flags & 0x200) ? Cursor::Sabotage : Cursor::NoMove;
        if (withBehaviour(behaviour::kEngineer) && bs != q.side && (b->flags & 0x400)) return Cursor::Enter;  // capture
        return Cursor::NoMove;
    }

    const bool own = us == q.side;
    auto armedExceptThis = [&] {  // IsAnyArmedUnitSelectedExceptThis 0x4a58f0
        return anySel([&](const Unit& x) { return &x != u && armed(x); });
    };
    if (!(u->selected && own && !q.ctrl)) {
        if (w.diplomacy(q.side, us) == 0 && !q.ctrl) return Cursor::Select;
        if (q.alt && w.unitTemplate(*u).isInfantry && !own) {  // Alt: crushers drive over infantry
            if (anySel([&](const Unit& x) { return w.unitTemplate(x).canCrushInfantry != 0; }) && (f & cf::kDriveOn))
                return Cursor::Move;
            return anySel(armed) ? Cursor::Attack : Cursor::NoMove;
        }
        return armedExceptThis() ? Cursor::Attack : Cursor::NoMove;
    }
    // A selected own unit: Shift toggles it; deployers show whether a click deploys them.
    if (q.shift || q.modernControls) return Cursor::Select;
    switch (beh(*u)) {
    case behaviour::kMcv: return w.canDeployMcv(q.side, ui) ? Cursor::Deploy : Cursor::NoDeploy;
    case behaviour::kDevastator: return Cursor::Deploy;  // self-destruct
    case behaviour::kThumper: return (f & cf::kSand) ? Cursor::Deploy : Cursor::NoDeploy;
    case behaviour::kSaboteur: return u->byte29 >= 0xa0 ? Cursor::Deploy : Cursor::NoDeploy;
    default: return Cursor::Arrow;
    }
}

Cursor scrollCursor(Cursor current, int dx, int dy, bool blockedX, bool blockedY) {
    int id = static_cast<int>(current);
    if (dy > 0) id = blockedY ? 0x1a : 0x12;
    else if (dy < 0) id = blockedY ? 0x16 : 0x0e;
    if (dx != 0) {
        const bool right = dx > 0;
        if (blockedX) {
            if (id == 0x1a) id = right ? 0x19 : 0x1b;       // both edges blocked: blocked diagonal
            else if (id == 0x16) id = right ? 0x17 : 0x1d;
            else if (id != 0x0e && id != 0x12) id = right ? 0x18 : 0x1c;  // vertical scroll wins
        } else {
            if (id == 0x12) id = right ? 0x11 : 0x13;
            else if (id == 0x0e) id = right ? 0x0f : 0x15;
            else id = right ? 0x10 : 0x14;
        }
    }
    return static_cast<Cursor>(id);
}

CursorRenderer::CursorRenderer(SDL_Renderer* r, const std::string& mouseR16Path)
    : r_(r), file_(formats::loadR16(mouseR16Path)) {
    if (file_.frames.size() < std::size_t(kCursorCount * kCursorFrames))
        throw std::runtime_error("MOUSE.R16 has too few frames");
    cache_ = std::make_unique<SpriteCache>(r_, file_, colours_);
    start_ = SDL_GetTicks();
}

CursorRenderer::~CursorRenderer() {
    if (current_) SDL_SetCursor(SDL_GetDefaultCursor());
    for (auto& [k, c] : system_) SDL_DestroyCursor(c);
}

SDL_Cursor* CursorRenderer::makeSystem(Cursor c, float scale, bool hq, float density) {
    const int id = std::clamp(static_cast<int>(c), 0, kCursorCount - 1);
    constexpr int kCell = 2 * kCursorHotspot;  // 48
    const int k = hq ? 4 : 1;
    density = std::max(1.0f, density);
    // Sizes: the 100 % image (points: `scale` points per cursor pixel) and the display's pixels.
    const int basePx = std::max(kCell / 2, int(std::lround(kCell * scale)));
    const int fullPx = std::max(basePx, int(std::lround(basePx * density)));
    std::vector<SDL_Surface*> owned;
    SDL_CursorFrameInfo frames[kCursorFrames]{};
    bool ok = true;
    for (int step = 0; step < kCursorFrames && ok; ++step) {
        const formats::R16Frame& f = file_.frames[std::size_t(id * kCursorFrames + step)];
        SDL_Surface* cell = SDL_CreateSurface(kCell * k, kCell * k, SDL_PIXELFORMAT_ABGR8888);
        if (!cell) { ok = false; break; }
        owned.push_back(cell);
        SDL_ClearSurface(cell, 0, 0, 0, 0);
        if (!f.empty()) {
            std::vector<formats::Rgba> px(std::size_t(f.width) * f.height, 0);
            if (f.bpp == 16) {
                px = formats::decodeFrame(file_, f, /*zeroIsTransparent=*/true);
            } else {
                const formats::Palette* pal = f.palette >= 0 ? &file_.palettes[std::size_t(f.palette)] : nullptr;
                for (std::size_t i = 0; i < px.size(); ++i) {
                    const std::uint8_t p = f.pixels[i];
                    px[i] = p == 0 ? 0u : p == 1 ? 0x80000000u : pal ? (*pal)[p] : (0xFF000000u | p * 0x010101u);
                }
            }
            if (hq) px = scale4x(px, f.width, f.height);
            // The frame's top-left in the 48 x 48 cell: the hotspot (24, 24) is at (24 + x, 24 + y) in the frame.
            const int ox = -f.x * k, oy = -f.y * k, fw = f.width * k, fh = f.height * k;
            auto* dst = static_cast<std::uint8_t*>(cell->pixels);
            for (int y = 0; y < fh; ++y) {
                const int cy = oy + y;
                if (cy < 0 || cy >= cell->h) continue;
                for (int x = 0; x < fw; ++x) {
                    const int cx = ox + x;
                    if (cx < 0 || cx >= cell->w) continue;
                    std::memcpy(dst + std::size_t(cy) * cell->pitch + std::size_t(cx) * 4, &px[std::size_t(y) * fw + x], 4);
                }
            }
        }
        const SDL_ScaleMode mode = hq ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
        SDL_Surface* base = SDL_ScaleSurface(cell, basePx, basePx, mode);
        SDL_Surface* full = basePx == fullPx ? nullptr : SDL_ScaleSurface(cell, fullPx, fullPx, mode);
        if (!base || (basePx != fullPx && !full)) { ok = false; break; }
        owned.push_back(base);
        if (full) {
            SDL_AddSurfaceAlternateImage(base, full);
            SDL_DestroySurface(full);  // (the base keeps a reference)
        }
        frames[step].surface = base;
        frames[step].duration = 50;  // FUN_00423a20: one step per 3 frames at 60 fps
    }
    SDL_Cursor* cur = nullptr;
    if (ok) {
        const int hot = basePx / 2;
        cur = SDL_CreateAnimatedCursor(frames, kCursorFrames, hot, hot);
        if (!cur) SDL_Log("Cursor %s: system cursor failed (%s) - drawn by the game", cursorName(c), SDL_GetError());
    }
    for (SDL_Surface* sf : owned) SDL_DestroySurface(sf);
    return cur;
}

bool CursorRenderer::showSystem(Cursor c, float scale, bool hq, float density) {
    const std::uint64_t key = std::uint64_t(std::clamp(static_cast<int>(c), 0, kCursorCount - 1)) |
                              std::uint64_t(std::lround(scale * 64)) << 8 | std::uint64_t(hq) << 30 |
                              std::uint64_t(std::lround(density * 64)) << 32;
    auto it = system_.find(key);
    if (it == system_.end()) it = system_.emplace(key, makeSystem(c, scale, hq, density)).first;
    if (!it->second) return false;
    if (it->second != current_) {
        SDL_SetCursor(it->second);
        current_ = it->second;
    }
    return true;
}

void CursorRenderer::draw(Cursor c, float x, float y, float scale, bool hq) {
    const int id = std::clamp(static_cast<int>(c), 0, kCursorCount - 1);
    // FUN_00423a20: step = drawn frames / 3 (mod 8); at 60 fps that is 50 ms per step.
    const int step = int((SDL_GetTicks() - start_) / 50) % kCursorFrames;
    cache_->setHq(hq);
    const Sprite* sp = cache_->get(id * kCursorFrames + step, -1);
    if (!sp || !sp->frame) return;
    float ox = 0, oy = 0;
    if (hq) {
        ox = x - (kCursorHotspot + sp->frame->x) * scale;
        oy = y - (kCursorHotspot + sp->frame->y) * scale;
    } else {  // whole game pixels, like the original mouse position (scale may be fractional)
        scale = std::max(1.0f, scale);
        ox = std::round((std::floor(x / scale) - kCursorHotspot - sp->frame->x) * scale);
        oy = std::round((std::floor(y / scale) - kCursorHotspot - sp->frame->y) * scale);
    }
    const SDL_FRect dst{ox, oy, sp->width * scale, sp->height * scale};
    if (sp->shadow) SDL_RenderTexture(r_, sp->shadow, nullptr, &dst);
    SDL_RenderTexture(r_, sp->texture, nullptr, &dst);
}

}  // namespace d2k::render

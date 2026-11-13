// The local player's shroud (re/notes/shroud.md): RevealCircle 0x44e3b0, UpdateShroudInRegion
// 0x44cd10, RevealMap 0x44c480, FUN_0044e330 (building sight), FUN_0044cff0 (unit sight, one
// eighth of the units per tick), RevealTilesSeenByBuildingsAndUnits 0x49f4d0. The original's
// map-visibility cheat check (GetMapVisState / _cheatstates) and the radar refresh done by
// UpdateShroudInRegion are not modelled (no gameplay effect / renderer side).

#include "game/world.hpp"

#include <algorithm>

namespace d2k::game {

namespace {

// kShroudEdgeTiles 0x4e0b18: shroud-edge sprite id per 8-neighbour "shrouded" mask
// (NW=1, N=2, NE=4, W=8, E=0x10, SW=0x20, S=0x40, SE=0x80).
constexpr std::uint8_t kShroudEdgeTiles[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  13, 0,  0,  0,  13,  //
    0,  0,  0,  0,  0,  0,  12, 12, 0,  0,  0,  13, 0,  0,  12, 3,   //
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  13, 0,  0,  0,  13,  //
    0,  0,  0,  0,  0,  0,  12, 12, 0,  0,  0,  13, 0,  0,  12, 3,   //
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  13, 0,  0,  0,  13,  //
    0,  0,  0,  0,  0,  0,  12, 12, 0,  0,  0,  13, 0,  0,  12, 3,   //
    0,  0,  0,  0,  0,  0,  0,  0,  11, 11, 11, 5,  11, 11, 11, 5,   //
    0,  0,  0,  0,  0,  0,  12, 12, 11, 11, 11, 5,  11, 11, 15, 2,   //
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  13, 0,  0,  0,  13,  //
    0,  0,  0,  0,  0,  0,  12, 12, 0,  0,  0,  13, 0,  0,  12, 3,   //
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  13, 0,  0,  0,  13,  //
    0,  0,  0,  0,  0,  0,  12, 12, 0,  0,  0,  13, 0,  0,  12, 3,   //
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  13, 0,  0,  0,  13,  //
    10, 10, 10, 10, 10, 10, 6,  6,  10, 10, 10, 14, 10, 10, 6,  4,   //
    0,  0,  0,  0,  0,  0,  0,  0,  11, 11, 11, 5,  11, 11, 11, 5,   //
    10, 10, 10, 10, 10, 10, 6,  6,  8,  8,  8,  7,  8,  8,  9,  1,   //
};

// Built-in copy of the CIRCLES.BIN masks (used when Rules::circles is null): for radius r,
// row half-widths of the revealed span, rows -r..r (the masks are symmetric).
constexpr std::uint8_t kCircleHalfWidth[8][15] = {
    {0},
    {0, 1, 0},
    {1, 2, 2, 2, 1},
    {1, 2, 3, 3, 3, 2, 1},
    {1, 3, 3, 4, 4, 4, 3, 3, 1},
    {2, 3, 4, 5, 5, 5, 5, 5, 4, 3, 2},
    {2, 4, 5, 5, 6, 6, 6, 6, 6, 5, 5, 4, 2},
    {2, 4, 5, 6, 6, 7, 7, 7, 7, 7, 6, 6, 5, 4, 2},
};

std::uint8_t builtinMask(int r, int row, int col) {
    const int hw = kCircleHalfWidth[r][row];
    const int dx = col - r;
    return (dx >= -hw && dx <= hw) ? 0x00 : 0xFF;
}

std::size_t circleOffset(int r) {
    std::size_t o = 0;
    for (int i = 0; i < r; ++i) o += static_cast<std::size_t>(2 * i + 1) * (2 * i + 1);
    return o;
}

}  // namespace

// One entry of the CIRCLES.BIN mask of radius r (0..7): 0 = inside the circle, 0xFF = outside
// (_ptr_circle_1x1grid; also used by the community Spice Bloom event).
std::uint8_t World::circleMask(int r, int row, int col) const {
    r = std::clamp(r, 0, 7);
    const int n = 2 * r + 1;
    if (rules_.circles && rules_.circles->size() >= 680)
        return (*rules_.circles)[circleOffset(r) + static_cast<std::size_t>(row) * n + col];
    return builtinMask(r, row, col);
}

// RevealMap 0x44c480: every cell's shroud byte = 0.
void World::revealMap() {
    for (auto& c : cells_) c.shroud = 0;
}

// RevealCircle 0x44e3b0: AND the (2r+1)^2 mask centred on (x, y) into the shroud bytes
// (cells off the map skipped), then UpdateShroudInRegion over the circle plus one cell.
void World::revealCircle(int x, int y, int radius) {
    // The original DebugFatals for radius > 7 ("Shroud circles only defined up to 7").
    const int r = std::clamp(radius, 0, 7);
    const int n = 2 * r + 1;
    const bool fromFile = rules_.circles && rules_.circles->size() >= 680;
    const std::size_t base = circleOffset(r);
    for (int row = 0; row < n; ++row)
        for (int col = 0; col < n; ++col) {
            const std::uint8_t m = fromFile ? (*rules_.circles)[base + static_cast<std::size_t>(row) * n + col]
                                            : builtinMask(r, row, col);
            const unsigned cx = static_cast<unsigned>(x - r + col), cy = static_cast<unsigned>(y - r + row);
            if (cx < static_cast<unsigned>(width_) && cy < static_cast<unsigned>(height_))
                cells_[static_cast<std::size_t>(cy) * width_ + cx].shroud &= m;
        }
    updateShroudInRegion(std::max(x - r - 1, 0), std::max(y - r - 1, 0), x + 2 + r, y + 2 + r);
}

// UpdateShroudInRegion 0x44cd10 over columns x0..x1-1, rows y0..y1-1 (clamped to the map).
// Every cell still (partly) shrouded gets its edge sprite from the mask of shrouded
// neighbours; off-map neighbours count as shrouded. The cells are rewritten in place in
// row-major order, so a cell cleared here (table value 0) already counts as clear for the
// cells after it - as in the original.
void World::updateShroudInRegion(int x0, int y0, int x1, int y1) {
    const int w = width_ & 0xFF, h = height_ & 0xFF;
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, w);
    y1 = std::min(y1, h);
    auto sh = [&](int x, int y) { return (cells_[static_cast<std::size_t>(y) * width_ + x].shroud & 0xF) != 0; };
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            Cell& c = cells_[static_cast<std::size_t>(y) * width_ + x];
            if ((c.shroud & 0xF) == 0) continue;
            std::uint32_t m = 0;
            if (x < 1) {
                m = 0x29;
            } else {
                if (sh(x - 1, y)) m = 0x08;
                if (y < h - 1 && sh(x - 1, y + 1)) m |= 0x20;
                if (y > 0 && sh(x - 1, y - 1)) m |= 0x01;
            }
            if (x < w - 1) {
                if (sh(x + 1, y)) m |= 0x10;
                if (y > 0 && sh(x + 1, y - 1)) m |= 0x04;
                if (y < h - 1 && sh(x + 1, y + 1)) m |= 0x80;
            } else {
                m |= 0x94;
            }
            if (y < h - 1) {
                if (sh(x, y + 1)) m |= 0x40;
            } else {
                m |= 0xE0;
            }
            if (y < 1)
                m |= 0x07;
            else if (sh(x, y - 1))
                m |= 0x02;
            c.shroud = static_cast<std::uint8_t>((((y & 1) * 2) + (x & 1)) * 0x10 + kShroudEdgeTiles[m]);
        }
}

// FUN_0044e330: reveal around a building of `type` whose top-left tile is (x, y): circle of
// the template's sight radius centred at (x + artWidth/64, y + artHeight/64).
void World::revealBuildingType(int type, int x, int y) {
    const auto& t = rules_.templates->bin.buildings[type & 0xFF];
    revealCircle((t.artWidth >> 6) + (x & 0xFF), (t.artHeight >> 6) + (y & 0xFF), t.sightRadius);
}

// FUN_0044cff0 (main loop after ModelTick): DAT_0054804c++; for every side whose diplomacy
// towards the local player is 0 (itself and allies), every unit with MyIndex % 8 equal to the
// counter % 8, a non-zero sight radius and not being carried reveals around its BlockTo.
void World::revealUnitsSlice() {
    ++revealCounter_;
    const int slot = revealCounter_ % 8;  // signed remainder, as the original
    for (int s = 0; s < kMaxSides; ++s) {
        if (diplomacy_[s][mySide_ & 7] != 0) continue;
        Side& side = sides_[s];
        for (int i = side.firstUnit; i != -1; i = side.nextObject(i)) {
            const Unit& u = side.units[i];
            const int sight = unitTemplate(u).sightRadius;
            if ((u.index & 7) == slot && sight != 0 && !(u.flags & unit_flags::kCarried))
                revealCircle(u.blockToX & 0xFF, u.blockToY & 0xFF, sight);
        }
    }
}

// RevealTilesSeenByBuildingsAndUnits 0x49f4d0 (Game_Init): all units (8 slices), then every
// building of `side` (tile from its position: PosX / 32, (PosY - artHeight) / 32).
void World::revealTilesSeenByBuildingsAndUnits(int side) {
    for (int i = 0; i < 8; ++i) revealUnitsSlice();
    Side& s = sides_[side & 7];
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
        const Building& b = s.buildings[i];
        const int py = (b.posY >> 16) - buildingTemplate(b).artHeight;
        revealBuildingType(b.type, (b.posX >> 16) / 32, py / 32);
    }
}

// FUN_00446690 (main loop, every tick): while the screen shakes, a random -2..2 offset.
void World::updateScreenShake() {
    if (screenShakes_ == 0) return;
    if (--screenShakes_ != 0)
        screenShakeOffset_ = static_cast<int>(static_cast<std::uint32_t>(rng_.next()) % 5) - 2;
    else
        screenShakeOffset_ = 0;
}

}  // namespace d2k::game

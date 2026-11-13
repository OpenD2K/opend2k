// Economy: credits (cash + stored spice, paid out through "drips"), refinery/silo storage,
// power, building repair, the harvester states of UpdateUnit, free-harvester deliveries,
// carryalls (FlyUnit, delivery drop-off, harvester pickup queue) and the starport stock/price
// upkeep (it draws random numbers every 8th tick per side, so it is kept for RNG fidelity).
// References: re/notes/economy.md; addresses in the comments.

#include "game/world.hpp"

#include "game/geometry.hpp"
#include "game/tables.hpp"

#include <algorithm>

namespace d2k::game {

using namespace cell_flags;
using namespace unit_flags;

namespace {
inline int px16(std::int32_t v) { return v / 65536; }  // (v + (v>>31 & 0xffff)) >> 16
inline int div32(int v) { return v / 32; }
inline std::int32_t iabs(std::int32_t v) { return v < 0 ? -v : v; }
constexpr std::uint16_t kSandTiles[8] = {48, 49, 50, 51, 68, 69, 70, 71};  // 0x4e9520
constexpr std::uint16_t kThinSpiceTile = 0x319, kThickSpiceTile = 0x2ed;

// Carryall flags (Unit +0x08) used by the delivery/pickup logic.
constexpr std::uint32_t kCarrying = 0x100;    // carryall: has cargo; cargo: is carried (kCarried)
constexpr std::uint32_t kAssigned = 0x200;    // carryall: owns a pickup-queue entry / delivery
constexpr std::uint32_t kDelivery = 0x800;    // carryall of a Delivery (not counted as a carryall)
constexpr std::uint32_t kLeaving = 0x4000;    // done: fly off the map
}  // namespace

// =======================================================================================
// Credits

// FUN_0046c720 "Spend": spice is used before cash; the change goes into the drips.
bool World::spend(int side, std::int32_t amount) {
    Side& s = sides_[side];
    const std::int32_t sd = s.spiceDrip, sr = s.spice;
    if (s.cash + sd + s.cashDrip + sr < amount) return false;
    if (amount < sd + sr) {
        s.spiceDrip = sd - amount;
        return true;
    }
    s.cashDrip -= amount - (sd + sr);
    s.spiceDrip = -sr;
    return true;
}

// FUN_0046c7d0 "UpdateCashDrip": opposite-signed drips cancel, then each drip pays out
// max(10, (|spiceDrip| + |cashDrip|) / 16) per tick.
void World::updateCashDrip(Side& s) {
    const std::int32_t c = s.cashDrip;
    bool merge = false;
    std::int32_t sp = 0;
    bool fold = false;
    if (c < 0 && s.spiceDrip > 0) {
        sp = s.spiceDrip;
        if (iabs(c) < sp) merge = true;
        else { s.cashDrip = sp + c; fold = true; }
    } else if (c >= 1 && s.spiceDrip < 0) {
        sp = s.spiceDrip;
        if (c < iabs(sp)) merge = true;
        else { s.cashDrip = c + sp; fold = true; }
    }
    if (merge) {
        s.spiceDrip = sp + c;
        s.spice -= c;
        s.cash += c;
        s.cashDrip = 0;
    } else if (fold) {
        s.cash -= sp;
        s.spice += sp;
        s.spiceDrip = 0;
    }
    std::int32_t step = (iabs(s.spiceDrip) + iabs(s.cashDrip)) / 16;
    if (step < 10) step = 10;
    if (s.cashDrip != 0) {
        if (s.id == mySide_) speech("S_MONEYTICK", false, 15);
        const std::int32_t d = s.cashDrip;
        if (step < d) { s.cash += step; s.cashDrip = d - step; }
        else if (step < iabs(d)) { s.cash -= step; s.cashDrip = d + step; }
        else { s.cashDrip = 0; s.cash += d; }
    }
    if (s.spiceDrip != 0) {
        if (s.id == mySide_) speech("S_MONEYTICK", false, 15);
        const std::int32_t d = s.spiceDrip;
        if (step < d) { s.spice += step; s.spiceDrip = d - step; }
        else if (step < iabs(d)) { s.spice -= step; s.spiceDrip = d + step; }
        else { s.spiceDrip = 0; s.spice += d; }
    }
}

// FUN_0044abe0: drips, then storage = 2000 per refinery + 1500 per silo; spice above it is lost.
void World::updateStorage() {
    for (auto& s : sides_) {
        updateCashDrip(s);
        s.maxStorage = 0;
        for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
            const int beh = buildingTemplate(s.buildings[i]).specialBehavior;
            if (beh == building_behaviour::kRefinery) s.maxStorage += 2000;
            if (beh == building_behaviour::kSilo) s.maxStorage += 1500;
        }
        const std::uint32_t cap = static_cast<std::uint32_t>(s.maxStorage);
        if (cap < static_cast<std::uint32_t>(s.spice)) {
            if (s.spiceDrip < 0) s.spiceDrip = (s.spiceDrip - s.maxStorage) + s.spice;
            else s.spiceDrip = 0;
            s.spice = s.maxStorage;
        }
    }
}

// =======================================================================================
// Power: FUN_0044aae0 calls FUN_0046ca00 for all sides, then plays the local player's
// power-down / power-up / radar-online sounds.

void World::updatePower() {
    for (auto& s : sides_) {
        s.powerOutput = s.powerDemand = s.powerPercent = 0;
        if (s.firstBuilding == -1) continue;
        for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
            const Building& b = s.buildings[i];
            const auto& t = buildingTemplate(b);
            const std::int32_t p = t.powerConsumption;
            if (p < 0) {
                const std::uint32_t healthPct = static_cast<std::uint32_t>(b.health * 100) / t.hitPoints;
                s.powerOutput += (static_cast<std::uint32_t>(-p) * healthPct) / 100;
            } else {
                s.powerDemand += static_cast<std::uint32_t>(p);
            }
        }
        if (s.powerOutput != 0) {  // the base 200 only counts when something produces power
            s.powerOutput += 200;
            s.powerDemand += 200;
        }
        s.powerPercent = s.powerDemand != 0 ? (s.powerOutput * 100) / s.powerDemand : 200;
    }
    Side& me = sides_[mySide_ & 7];
    if (me.powerPercentPrev != 0xffff) {
        auto hasOutpost = [&] {
            for (int i = me.firstBuilding; i != -1; i = me.nextObject(i))
                if (buildingTemplate(me.buildings[i]).specialBehavior == building_behaviour::kOutpost) return true;
            return false;
        };
        if (me.powerPercent < 100 && me.powerPercentPrev > 99) speech("S_POWERDOWN", false);
        if (me.powerPercent > 99) {
            if (me.powerPercentPrev < 100) {
                if (!hasOutpost()) speech("S_POWERUP", false);
                else { speech("S_RADARONLINE", false); radarOnline_ = true; }
            } else if (!hasOutpost()) {
                radarOnline_ = false;
            } else if (!radarOnline_) {
                speech("S_RADARONLINE", false);
                radarOnline_ = true;
            }
        }
    }
    // UNCERTAIN: the original copies the percent into +0x245b4 in the sidebar code
    // (FUN_00425860, once per drawn frame); we do it once per tick, for all sides.
    for (auto& s : sides_) s.powerPercentPrev = s.powerPercent;
}

// FUN_0044a9f0: one side per tick (gGameTicks & 7) repairs its buildings flagged 0x1.
void World::updateBuildingRepair() {
    const int sideId = static_cast<int>(gameTicks_ & 7);
    Side& s = sides_[sideId];
    const std::int32_t v = static_cast<std::int32_t>(vars().buildingRepairValue);
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
        Building& b = s.buildings[i];
        if (!(b.flags & building_flags::kRepairing)) continue;
        const auto& t = buildingTemplate(b);
        if (b.health < static_cast<std::int32_t>(t.hitPoints)) {
            const std::uint32_t cost = buildingCost(b.type, 0, sideId);
            std::uint32_t amount = ((cost * static_cast<std::uint32_t>(v)) >> 1) / t.hitPoints;
            if (amount == 0) amount = 1;
            if (static_cast<std::uint32_t>(s.cash + s.spice) < amount) {
                b.flags &= ~building_flags::kRepairing;
            } else {
                spend(sideId, static_cast<std::int32_t>(amount));  // ("DoRepairs") result ignored
                b.health += v;
                if (static_cast<std::int32_t>(t.hitPoints) <= b.health) {
                    b.health = static_cast<std::int32_t>(t.hitPoints);
                    b.flags &= ~building_flags::kRepairing;
                }
            }
        } else {
            b.flags &= ~building_flags::kRepairing;
        }
    }
}

// =======================================================================================
// Refineries and spice

// GetRefineryIndex 0x4a5e80: the refinery with the fewest harvesters assigned. The count is
// kept in the refineries' ReloadDelay byte (+0x37), which refineries do not otherwise use.
int World::getRefineryIndex(int side) {
    Side& s = sides_[side];
    bool any = false;
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i))
        if (buildingTemplate(s.buildings[i]).specialBehavior == building_behaviour::kRefinery) {
            s.buildings[i].reloadDelay = 0;
            any = true;
        }
    if (!any) return kNoIndex;
    for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) {
        const Unit& u = s.units[i];
        if (unitTemplate(u).specialBehavior != behaviour::kHarvester) continue;
        Building* r = building(side, u.refineryIndex);
        if (r && buildingTemplate(*r).specialBehavior == building_behaviour::kRefinery) ++r->reloadDelay;
    }
    int best = kNoIndex;
    std::uint8_t fewest = 0xff;
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
        const Building& b = s.buildings[i];
        if (buildingTemplate(b).specialBehavior == building_behaviour::kRefinery && b.reloadDelay < fewest) {
            best = i;
            fewest = b.reloadDelay;
        }
    }
    return best;
}

// GetNearestBuildingWithBehavior 0x4a5f80: Manhattan distance to the building's Max corner
// (+0x18/+0x19), u16 compare; returns the building's exit point 1 tile.
Building* World::nearestBuildingWithBehaviour(int x, int y, int side, int beh, int& ox, int& oy) {
    Side& s = sides_[side];
    Building* best = nullptr;
    int bestD = 0x4000;
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
        Building& b = s.buildings[i];
        const auto& t = buildingTemplate(b);
        if (t.specialBehavior != beh) continue;
        const int d = std::abs((b.maxY & 0xff) - (y & 0xff)) + std::abs((b.maxX & 0xff) - (x & 0xff));
        if (static_cast<std::uint16_t>(d) < static_cast<std::uint16_t>(bestD)) {
            ox = static_cast<std::uint8_t>(b.tileX + t.exitPoint1X);
            oy = static_cast<std::uint8_t>(b.tileY + t.exitPoint1Y);
            bestD = d;
            best = &b;
        }
    }
    return best;
}

// The docking tile of a refinery: building top-left (from the position) + exit point 1.
void World::refineryDock(const Building& b, int& x, int& y) const {
    const auto& t = buildingTemplate(b);
    x = static_cast<std::uint8_t>(div32(px16(b.posX)) + t.exitPoint1X);
    y = static_cast<std::uint8_t>(div32(px16(b.posY) - t.artHeight) + t.exitPoint1Y);
}

// FUN_0044d970: unless (x,y) holds spice level 3 or 4, look in the 3x3 block around it for a
// cell with level >= 3; the last one found (row-major) wins. Quirk: the "not the start cell"
// test compares against the already updated position.
bool World::betterSpiceNearby(int& x, int& y) const {
    const std::uint32_t lvl = cell(x, y).flags & kSpiceMask;
    if (lvl == 0x300000 || lvl == 0x400000) return false;
    const int x0 = std::max(x - 1, 0), x1 = std::min(x + 2, width_);
    const int y0 = std::max(y - 1, 0), y1 = std::min(y + 2, height_);
    bool found = false;
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx)
            if ((cell(xx, yy).flags & kSpiceMask) > 0x2fffff && (xx != x || yy != y)) {
                x = xx;
                y = yy;
                found = true;
            }
    return found;
}

// FUN_0044d760: the remembered spot if it still has spice, else the spice cell (free of
// units/buildings) nearest to it; ties go to the one nearest to the refinery. Quirk: the
// refinery tile comes from PosY, which includes the art height offset.
bool World::findNearestSpice(const Unit& u, int& ox, int& oy) {
    const int rx = u.rememberX & 0xff, ry = u.rememberY & 0xff;
    if (inBounds(rx, ry) && (cell(rx, ry).flags & kSpiceMask)) {
        ox = rx;
        oy = ry;
        return true;
    }
    int refX = rx, refY = ry;
    if (const Building* r = building(u.side, u.refineryIndex);
        r && buildingTemplate(*r).specialBehavior == building_behaviour::kRefinery) {
        refX = div32(px16(r->posX));
        refY = div32(px16(r->posY));
    }
    int best = height_ + width_, bestTie = height_ + width_;
    for (int y = 0; y < height_; ++y)
        for (int x = 0; x < width_; ++x) {
            const std::uint32_t f = cell(x, y).flags;
            if (!(f & kSpiceMask) || (f & kOccupied)) continue;
            const int d = std::abs(ry - y) + std::abs(rx - x);
            const int tie = std::abs(refX - x) + std::abs(refY - y);
            if (d < best || (d == best && tie < bestTie)) {
                best = d;
                bestTie = tie;
                ox = x;
                oy = y;
            }
        }
    if (height_ + width_ <= best) return false;
    betterSpiceNearby(ox, oy);
    return true;
}

// tile_driveon_49E290: in the 3x3 block around (x,y), the last free drivable cell; failing
// that the last drivable cell of `side` that is not a plain building tile (returns false).
bool World::findDriveOnTileNear(int side, int& x, int& y) const {
    const int y0 = std::max((y & 0xff) - 1, 0), y1 = std::min((y & 0xff) + 2, height_);
    const int x0 = std::max((x & 0xff) - 1, 0), x1 = std::min((x & 0xff) + 2, width_);
    bool found = false;
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
            const std::uint32_t f = cell(xx, yy).flags;
            if ((f & kOccupied) == 0 && (f & kDriveOn)) {
                x = xx;
                y = yy;
                found = true;
            }
        }
    if (found) return true;
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
            const std::uint32_t f = cell(xx, yy).flags;
            if (static_cast<int>(f & kOwnerMask) == (side & 0xff) && (f & kOccupied) != kBuilding && (f & kDriveOn)) {
                x = xx;
                y = yy;
            }
        }
    return false;
}

// GetNearestFreeTileForUnit 0x4a60e0: square spiral (W, S, E, N legs) out to `radius`; a free
// walk+drive cell not on the last row/column.
bool World::nearestFreeTile(int& x, int& y, int radius) const {
    static constexpr int kDx[4] = {-1, 0, 1, 0}, kDy[4] = {0, 1, 0, -1}, kInc[4] = {0, 1, 0, 1};  // 0x4eb2d8..
    const int limit = (radius & 0xff) * 2 + 2;
    int cx = x & 0xff, cy = y & 0xff, dir = 0;
    std::uint8_t leg = 1, count = 1;
    do {
        cx += kDx[dir];
        cy += kDy[dir];
        if (cx >= 0 && cy >= 0 && cx < width_ - 1 && cy < height_ - 1 &&
            (cell(cx, cy).flags & (kOccupied | kDriveOn | kWalkOn)) == (kDriveOn | kWalkOn)) {
            x = cx;
            y = cy;
            return true;
        }
        if (--count == 0) {
            leg = static_cast<std::uint8_t>(leg + kInc[dir]);
            dir = (dir + 1) & 3;
            count = leg;
        }
    } while (leg < limit);
    return false;
}

// =======================================================================================
// Harvester states (UpdateUnit 0x497b90 cases 9..14 = states 11..16)

// LAB_004999dd: a harvester far from its target (squared tile distance > HarvesterDriveDistance)
// and standing still asks for a carryall.
void World::harvesterAirliftCheck(Unit& u) {
    const int dx = u.blockFromX - u.targetX, dy = u.blockFromY - u.targetY;
    if (static_cast<int>(vars().harvesterDriveDistance) < dx * dx + dy * dy && u.posSteps == 0 &&
        !(u.flags & kBlockToMarked)) {
        Side& s = sides_[u.side];
        const int pos = pickupQueuePos(s, u);
        if (pos != -1 && addToPickupQueue(s, u, pos, 100)) unitAdjustState(u, UnitState::AwaitingAirlift);
    }
}

void World::updateMovingToHarvest(Unit& u) {
    if (!moveUnit(u)) {
        unitAdjustState(u, UnitState::Harvesting);
        return;
    }
    harvesterAirliftCheck(u);
}

void World::updateHarvesting(Unit& u) {
    if (u.animDelay != 0) {
        --u.animDelay;
        return;
    }
    if (u.byte29 > 6) {  // full (7 blobs)
        unitAdjustState(u, UnitState::MovingToRefinery);
        return;
    }
    int lx = u.blockFromX, ly = u.blockFromY;
    if (betterSpiceNearby(lx, ly)) {
        bool go = canUnitUseSquare(lx, ly, u, u.side, 0);
        if (!go) {
            int vs = 0, vi = 0;
            const Unit* v = getUnitOnTile(lx << 5, ly << 5, vs, vi, false);
            go = v && unitTemplate(*v).specialBehavior != behaviour::kHarvester;
        }
        if (go) {
            u.targetX = u.rememberX = lx;
            u.targetY = u.rememberY = ly;
        }
        unitAdjustState(u, UnitState::MovingToHarvest);  // blocked by a harvester: target unchanged
        return;
    }
    u.rememberX = lx;
    u.rememberY = ly;
    Cell& c = cell(u.blockFromX, u.blockFromY);
    const std::uint32_t lvl = c.flags & kSpiceMask;
    if (lvl == 0) {
        if (findNearestSpice(u, lx, ly)) {
            u.targetX = u.rememberX = lx;
            u.targetY = u.rememberY = ly;
            unitAdjustState(u, UnitState::MovingToHarvest);
        } else if (u.byte29 != 0) {
            unitAdjustState(u, UnitState::MovingToRefinery);
        } else {
            unitAdjustState(u, UnitState::Idle);
        }
        return;
    }
    // One level per load step (re/notes/map-and-spawn.md §1.6).
    const std::uint32_t rest = c.flags & ~kSpiceMask;
    switch (lvl >> kSpiceShift) {
    case 1:
        c.flags = rest;
        c.tile = kSandTiles[static_cast<std::uint32_t>(rng_.next()) & 7];  // (radar repainted)
        break;
    case 2: c.flags = rest | 0x100000; c.tile = kThinSpiceTile; break;
    case 3:
        // Map rule InfiniteSpice skips this level's clearing store (patch hack at 0x4995a5), so
        // the OR of 0x200000 leaves the cell at level 3: it never runs out.
        c.flags = (rulesExt().infiniteSpice ? c.flags : rest) | 0x200000;
        c.tile = kThinSpiceTile;
        break;
    case 4: c.flags = rest | 0x300000; c.tile = kThickSpiceTile; break;
    case 5: c.flags = rest | 0x400000; c.tile = kThickSpiceTile; break;
    default: c.flags = rest | 0x200000; c.tile = kThickSpiceTile; break;  // 6, 7 -> 2 (quirk)
    }
    updateSpiceInRegion(u.blockFromX - 1, u.blockFromY - 1, u.blockFromX + 2, u.blockFromY + 2);
    u.animDelay = static_cast<std::uint8_t>(vars().harvestLoadSpiceDelay);
    ++u.byte29;
    const std::uint8_t dust =
        static_cast<std::uint8_t>(rules_.templates->bin.groupIds[group_ids::kHarvesterDust0 + ((u.facing & 0xff) >> 2)]);
    if (dust < 0x40) addExplosion(u.side, dust, px16(u.posX), px16(u.posY), 0);
}

void World::updateMovingToRefinery(Unit& u) {
    Building* r = building(u.side, u.refineryIndex);
    if (!r || buildingTemplate(*r).specialBehavior != building_behaviour::kRefinery) {
        int ox = 0, oy = 0;
        r = nearestBuildingWithBehaviour(u.blockFromX, u.blockFromY, u.side, building_behaviour::kRefinery, ox, oy);
        if (!r) {
            if (u.posSteps == 0) unitAdjustState(u, UnitState::Idle);
            else moveUnit(u);
        } else {
            u.targetX = ox;
            u.targetY = oy;
            u.refineryIndex = r->index;
            if (!moveUnit(u)) {
                u.animFrame = 0;
                unitAdjustState(u, UnitState::EnteringRefinery);
            }
        }
    } else {
        int dx = 0, dy = 0;
        refineryDock(*r, dx, dy);
        u.targetX = dx;
        u.targetY = dy;
        moveUnit(u);
        if ((u.blockFromX == dx && u.blockFromY == dy) ||
            ((u.flags & kBlockToMarked) && u.blockToX == dx && u.blockToY == dy)) {
            u.animFrame = 0;
            unitAdjustState(u, UnitState::EnteringRefinery);
        }
        // An idle/guarding friendly unit parked on the dock is asked to move away.
        int vs = 0, vi = 0;
        Unit* v = getUnitOnTile(dx << 5, dy << 5, vs, vi, false);
        if (v && unitCanTakeOrders(v, false) && (v->state == UnitState::Idle || v->state == UnitState::Guarding) &&
            diplomacy(u.side, vs) == 0)
            unitAdjustState(*v, UnitState::MovingAway);
    }
    harvesterAirliftCheck(u);
}

void World::updateEnteringRefinery(Unit& u) {
    if (u.posSteps != 0) {
        moveUnit(u);
        return;
    }
    if (u.byte29 == 0) {
        unitAdjustState(u, UnitState::LeavingRefinery);
        return;
    }
    if (const Building* r = building(u.side, u.refineryIndex);
        r && buildingTemplate(*r).specialBehavior == building_behaviour::kRefinery) {
        int dx = 0, dy = 0;
        refineryDock(*r, dx, dy);
        if (u.blockFromX == dx && u.blockFromY == dy) {
            if (!rotateBody(u, 12)) {  // face south-east, then play the docking animation
                if (u.animDelay == 0) {
                    u.animDelay = 5;
                    u.animFrame = static_cast<std::uint8_t>(u.animFrame + 1);
                    const int unload = static_cast<std::uint8_t>(rules_.templates->bin.groupIds[group_ids::kHarvesterUnload]);
                    if (animationFrames(unload) - 1 <= u.animFrame) {
                        unitAdjustState(u, UnitState::UnloadingSpice);
                        u.animDelay = static_cast<std::uint8_t>(vars().harvestUnloadDelay);
                    }
                } else {
                    --u.animDelay;
                }
            }
            return;
        }
    }
    unitAdjustState(u, UnitState::LeavingRefinery);
}

// Every harvestUnloadDelay ticks one blob (harvestBlobValue spice) goes into the side's spice
// drip (FUN_0046c790); credits rise as UpdateCashDrip pays it out, capped by the storage.
void World::updateUnloadingSpice(Unit& u) {
    if (u.animDelay != 0) {
        --u.animDelay;
        return;
    }
    if (const Building* r = building(u.side, u.refineryIndex);
        r && buildingTemplate(*r).specialBehavior == building_behaviour::kRefinery) {
        int dx = 0, dy = 0;
        refineryDock(*r, dx, dy);
        if (u.blockFromX == dx && u.blockFromY == dy) {
            u.animDelay = static_cast<std::uint8_t>(vars().harvestUnloadDelay);
            if (u.byte29 != 0) {
                const std::int32_t blob = static_cast<std::int32_t>(vars().harvestBlobValue);
                addSpice(u.side, blob);
                sides_[u.side].spiceHarvested += blob;
                --u.byte29;
                const std::int8_t fx = rules_.templates->bin.groupIds[group_ids::kRefineryProcessing];
                if (fx != -1)
                    addExplosion(u.side, static_cast<std::uint8_t>(fx), px16(r->posX) + vars().refineryExplosionOffsetX,
                                 px16(r->posY) + vars().refineryExplosionOffsetY, 0);
                return;
            }
        }
    }
    unitAdjustState(u, UnitState::LeavingRefinery);
}

void World::updateLeavingRefinery(Unit& u) {
    if (u.animDelay != 0) {
        --u.animDelay;
        return;
    }
    u.animDelay = 5;
    if (u.animFrame != 0) {  // undock animation backwards
        --u.animFrame;
        return;
    }
    int x = 0, y = 0;
    if (findNearestSpice(u, x, y)) {
        u.targetX = x;
        u.targetY = y;
        unitAdjustState(u, UnitState::MovingToHarvest);
        return;
    }
    findDriveOnTileNear(u.side, u.targetX, u.targetY);
    unitAdjustState(u, UnitState::MovingToHarvest);
}

// UpdateUnit case 0x10 (AWAITING_AIRLIFT): back to idle once the queue entry is gone.
void World::updateAwaitingAirlift(Unit& u) {
    const Side& s = sides_[u.side];
    bool found = false;
    for (int i = 0; i < s.pickupCount; ++i) {
        const PickupEntry& e = s.pickup[i];
        if (e.unitIndex == u.index && e.state != 0 && e.x == u.blockFromX && e.y == u.blockFromY) found = true;
    }
    if (!found) unitAdjustState(u, UnitState::Idle);
    u.lastMoved = 0;
    if (unitIsHiddenStealth(u)) unitDecloak(u);
    const bool onSlot = static_cast<std::uint32_t>(u.index & 7) == (gameTicks_ & 7);
    if ((onSlot || u.enemyIndex != kNoIndex) && !unitIsHiddenStealth(u)) unitAutoTarget(u);
}

// =======================================================================================
// Carryall pickup queue

// CSide__GetQueuePos 0x46cca0: first free entry, -1 when there is none or no carryall.
int World::pickupQueuePos(const Side& s, const Unit& u) const {
    if (u.state == UnitState::AwaitingAirlift) return 0;
    if ((s.carryallCount & 0xff) == 0) return -1;
    for (int i = 0; i < s.pickupCount; ++i)
        if (s.pickup[i].state == 0) return i;
    return -1;
}

// CSide__AddToQueue 0x46cd00.
bool World::addToPickupQueue(Side& s, const Unit& u, int pos, int priority) {
    for (int i = 0; i < s.pickupCount; ++i) {
        PickupEntry& e = s.pickup[i];
        if (e.unitIndex == u.index && e.state != 0) {
            if (e.state != 1) return false;
            e.state = 0;
        }
    }
    PickupEntry& e = s.pickup[pos & 31];
    e.state = 1;
    e.origState = u.state;
    e.waitTicks = 0;
    e.x = u.blockFromX;
    e.y = u.blockFromY;
    e.destX = u.targetX;
    e.destY = u.targetY;
    e.unitIndex = u.index;
    e.priority = priority;
    return true;
}

// FUN_0046ce00 / FUN_0046ce70: the first waiting entry for which this carryall is the nearest
// idle (non-delivery) carryall.
int World::pickupEntryFor(const Side& s, const Unit& c) const {
    auto dist = [](const Unit& v, int x, int y) {
        const int dx = (v.posX / 32) / 65536 - x, dy = (v.posY / 32) / 65536 - y;
        return static_cast<std::uint32_t>(dx * dx + dy * dy);
    };
    for (int i = 0; i < s.pickupCount; ++i) {
        const PickupEntry& e = s.pickup[i];
        if (e.state != 1) continue;
        const std::uint32_t mine = dist(c, e.x & 0xff, e.y & 0xff);
        bool nearest = true;
        for (int j = s.firstUnit; j != -1; j = s.nextObject(j)) {
            const Unit& o = s.units[j];
            if (unitTemplate(o).specialBehavior == behaviour::kCarryall && o.state == UnitState::Idle &&
                !(o.flags & kDelivery) && dist(o, e.x & 0xff, e.y & 0xff) < mine)
                nearest = false;
        }
        if (nearest) return i;
    }
    return -1;
}

// FUN_0046d180 (one side per tick, gGameTicks & 7): shrink the queue, drop entries whose unit
// died or left the waiting states, and give up after 0x4f checks.
void World::updatePickupQueue(Side& s) {
    const int n = s.pickupCount;
    if (s.carryallsAlive < n && n > 0 && s.pickup[n - 1].state == 0) s.pickupCount = n - 1;
    for (int i = 0; i < s.pickupCount; ++i) {
        PickupEntry& e = s.pickup[i];
        if (e.state == 3) e.state = 0;
        if (e.state == 0) continue;
        Unit& v = s.units[static_cast<std::size_t>(e.unitIndex) % kObjectsPerSide];
        if (v.state == UnitState::Dead) {
            e.state = 3;
            unitAdjustState(v, e.origState);
        } else if (v.state != UnitState::AwaitingAirlift && v.state != UnitState::Airlifted) {
            e.state = 3;
        }
        if ((e.state == 1 || e.state == 2) && ++e.waitTicks > 0x4f) {
            unitAdjustState(v, e.origState);
            e.state = 3;
        }
    }
}

// =======================================================================================
// Deliveries (free harvester of a new refinery, reinforcements)

void World::queueDelivery(Side& s, int x, int y, int unitType, int type, int deployAction) {
    int i = 0;
    while (i < 10 && s.deliveries[i].active) ++i;
    if (i >= 10) return;  // DebugFatal "Too many deliveries"
    Delivery& d = s.deliveries[i];
    d.active = true;
    d.started = false;
    d.x = x & 0xff;
    d.y = y & 0xff;
    d.time = gameTicks_;
    d.deployAction = deployAction;
    d.entryEdge = -1;  // (not original field)
    d.type = type;
    d.units[0] = static_cast<std::uint8_t>(unitType);
    d.units[1] = 0xff;
}

// Carryall_GetEntryPoint 0x49e6b0: the nearest map edge, `extra + 4` tiles outside it, as
// 16-bit coordinates with the original's unsigned comparisons. Returns the facing.
int World::carryallEntryPoint(int& x, int& y, int extra) const {
    const int b = (extra + 4) & 0xff;
    const std::uint32_t ux = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(x)));
    const std::uint32_t uy = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(y)));
    std::uint32_t p = ux;
    if (width_ / 2 < static_cast<std::int32_t>(ux)) p = static_cast<std::uint32_t>(width_) - ux;
    std::uint32_t q = uy;
    if (height_ / 2 < static_cast<std::int32_t>(uy)) q = static_cast<std::uint32_t>(height_) - uy;
    if (q <= p) {
        if (static_cast<std::int32_t>(uy) <= height_ / 2) {
            y = static_cast<std::int16_t>(-b);
            return 0x10;
        }
        y = static_cast<std::int16_t>(b + height_);
        return 0;
    }
    if (static_cast<std::int32_t>(ux) <= width_ / 2) {
        x = static_cast<std::int16_t>(-b);
        return 8;
    }
    x = static_cast<std::int16_t>(b + width_);
    return 0x18;
}

// Not original (cheat reinforcements): like carryallEntryPoint, but from a given edge (0 N,
// 1 E, 2 S, 3 W), 4 tiles outside it, in line with the drop tile.
int World::edgeEntryPoint(int& x, int& y, int edge) const {
    switch (edge & 3) {
    case 0: y = -4; return 0x10;
    case 1: x = width_ + 4; return 0x18;
    case 2: y = height_ + 4; return 0;
    default: x = -4; return 8;
    }
}

// FUN_0046def0 (one side per tick): send the carryall of every due delivery.
void World::updateDeliveries(Side& s) {
    const auto& T = rules_.templates->bin;
    for (int i = 0; i < 10; ++i) {
        Delivery& d = s.deliveries[i];
        if (s.id == mySide_ && d.type == 2 && d.active && !d.started) frigateSpeech(s, d);
        if (!d.active || d.started || gameTicks_ < d.time) continue;
        d.started = true;
        int ex = d.x, ey = d.y;
        const int dir = d.entryEdge >= 0 ? edgeEntryPoint(ex, ey, d.entryEdge) : carryallEntryPoint(ex, ey, 0);
        int idx = -1;
        if (d.type == 1) {
            idx = addUnit(s.id, static_cast<std::uint8_t>(T.groupIds[group_ids::kCarryall]), ex & 0xff, ey & 0xff, ex & 0xff,
                          ey & 0xff, 0, 0);
            if (idx < 0) return;
            --s.carryallCount;  // delivery carryalls are not counted
            Unit& c = s.units[idx];
            --s.unitsBuilt;
            --s.unitsBuiltPerType[c.type];
            c.targetX = d.x;
            c.targetY = d.y;
            c.posX = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int16_t>(ex)) << 21);
            c.posY = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int16_t>(ey)) << 21);
            c.facing = c.facingTurret = dir;
            if (d.deployAction != 4) mentat(s.id, "REINFORCE", true, 0, 1);
        } else if (d.type == 2) {
            // Starport order: one frigate at a time (a frigate still alive postpones it), from the
            // map edge nearest to the drop point (starport tile x + 1, y - 2) to that point.
            const int frigate = static_cast<std::uint8_t>(T.groupIds[group_ids::kFrigate]);
            if (s.unitsExistPerType[frigate] != 0) {
                d.started = false;
                continue;
            }
            const int pi = s.primaryBuilding[7];
            if (pi == kNoIndex) {  // starport gone: refund what is still booked (the delivery stays stuck)
                starportRefund(s);
                return;
            }
            const Building& sp = s.buildings[static_cast<std::size_t>(pi) % kObjectsPerSide];
            const int sx = div32(px16(sp.posX)) + 1, sy = div32(px16(sp.posY)) - 2;
            ex = sx;
            ey = sy;
            const int fdir = carryallEntryPoint(ex, ey, 0);
            idx = addUnit(s.id, frigate, ex & 0xff, ey & 0xff, ex & 0xff, ey & 0xff, 0, 0);
            if (idx < 0) continue;  // (no return here, unlike the carryall)
            Unit& c = s.units[idx];
            --s.unitsBuilt;
            --s.unitsBuiltPerType[c.type];
            c.posX = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int16_t>(ex)) << 21);
            c.posY = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int16_t>(ey)) << 21);
            c.targetX = sx & 0xff;
            c.targetY = sy & 0xff;
            c.facing = c.facingTurret = fdir;
            s.starportFrigateBusy = true;
        } else {
            return;
        }
        Unit& c = s.units[idx];
        c.lastMoved = static_cast<std::uint8_t>(i);  // +0x2c: delivery index
        c.flags |= 0x8000000 | kDelivery | kAssigned | kCarrying;
        int count = 0;
        bool end = false;
        for (int k = 0; k < 40; ++k) {
            if (d.units[k] == 0xff || end) end = true;
            else ++count;
        }
        if (count == 1 && d.type == 1) c.flags |= 0x20000000;
        unitAdjustState(c, UnitState::Moving);
    }
}

// FUN_0049e3e0: a delivery carryall low over its target drops the next unit every 4 ticks
// (8 for the frigate). Returns false when done.
bool World::carryallDrop(Unit& c) {
    Side& s = sides_[c.side];
    const int di = c.lastMoved % 10;
    Delivery& d = s.deliveries[di];
    const int type = d.type;
    const std::uint32_t period = type != 2 ? 4 : 8;
    if (gameTicks_ % period != 0) return true;
    const int x = c.blockToX & 0xff, y = c.blockToY & 0xff;
    const std::uint32_t f = inBounds(x, y) ? cell(x, y).flags : 0;
    if (((f & kOccupied) && type == 1) || ((f & kAnyUnit) && type == 2)) {
        c.lastFired = static_cast<std::uint8_t>(c.lastFired + 1);
        if (c.lastFired > 0x28 && d.type == 1) {
            unitAdjustState(c, UnitState::FlyingUp);  // try again after climbing
            return true;
        }
        if (c.lastFired < 0x51) return true;
        d.active = false;
        return false;
    }
    c.lastFired = 0;
    int k = 0;
    while (k < 39 && d.units[k] == 0xfe) ++k;
    if (d.units[k] == 0xff) {
        d.active = false;
        return false;
    }
    // Easy difficulty: every second unit of an enemy delivery is skipped.
    if (!(difficulty == 0 && (k & 1) && diplomacy(c.side, mySide_) != 0)) {
        int lx = x, ly = y;
        const int idx = addUnit(c.side, d.units[k], x, y, x, y, 0, 0);
        if (idx >= 0) {
            Unit& v = s.units[idx];
            v.facing = c.facing;
            v.tag = d.tag;  // community patch Mod__UnitDeliver: delivered units get the delivery's tag
            const int beh = unitTemplate(v).specialBehavior;
            if (beh == behaviour::kHarvester) {
                v.state = UnitState::MovingToHarvest;
                v.oldState = UnitState::MovingToHarvest;
                if (type == 2) {
                    v.refineryIndex = getRefineryIndex(c.side);
                } else if (Building* r = nearestBuildingWithBehaviour(lx, ly, c.side, building_behaviour::kRefinery, lx, ly)) {
                    v.refineryIndex = r->index;
                }
            } else {
                v.oldState = UnitState::Idle;
            }
            if (type == 2) {
                if (beh != behaviour::kCarryall) unitAdjustState(v, UnitState::LeavingRepairPad);
                if (const Building* sp = building(c.side, s.primaryBuilding[7])) rallyUnitLeft(c.side, idx, *sp);  // not original
            } else {
                findDriveOnTileNear(c.side, lx, ly);
                v.targetX = lx;
                v.targetY = ly;
                unitAdjustState(v, UnitState::Moving);
            }
            v.flags |= 0x40000;
            if (d.units[k + 1] == 0xff) v.flags |= 0xc0000;
            v.byte2E = static_cast<std::uint8_t>(di);
        }
    }
    d.units[k] = 0xfe;
    return true;
}

// =======================================================================================
// Flying

void World::markFlyer(int x, int y, bool on) {  // MarkTileFlyingUnit 0x49f370 / FUN_0049f330
    x &= 0xff;
    y &= 0xff;
    if (x >= width_ || y >= height_) return;
    if (on) cell(x, y).flags |= kFlyer;
    else cell(x, y).flags &= ~kFlyer;
}

// FlyUnit 0x494fd0. Flyers ignore tiles and routes: they head for the Target tile centre
// (heading in byte 0x55, body turning towards it), move along the body facing by the sin/cos
// table, creep to the centre once over the target tile (returns false when there), and keep
// BlockFrom/BlockTo (bytes) on the pixel tile. `hover`: circle at template speed.
bool World::flyUnit(Unit& u, bool hover) {
    const auto& t = unitTemplate(u);
    const int beh = t.specialBehavior;
    std::uint32_t spd = u.speed;
    bool noTurn = false;
    if (u.state == UnitState::Dead) {
        noTurn = true;
    } else if (hover) {
        spd = t.speed;
    } else {
        if (beh == behaviour::kOrnithopter || beh == behaviour::kBird) {  // wing flap counters
            if (u.animDelay == 0) {
                u.animDelay = 4;
                const int b = u.animFrame & 0xff;
                if (u.lying == 0) {
                    if (b < 2) {
                        u.animFrame = b + 1;
                    } else {
                        u.lying = 1;
                        u.animDelay = 0x14;
                    }
                } else if (b == 0) {
                    u.lying = 0;
                } else {
                    u.animFrame = b - 1;
                }
            } else {
                --u.animDelay;
            }
        }
        // A leaving delivery carryall dies once off the map (bytes: x < 0 wraps to >= 252).
        if ((width_ + 4 <= (u.blockToX & 0xff) || height_ + 4 <= (u.blockToY & 0xff)) && (u.flags & kDelivery) &&
            (u.flags & kLeaving)) {
            unitAdjustState(u, UnitState::Dead);
            return true;
        }
        const std::int32_t tcx = (u.targetX & 0xff) * 0x200000 + 0x100000;
        const std::int32_t tcy = (u.targetY & 0xff) * 0x200000 + 0x100000;
        const int ttx = tcx >> 21, tty = tcy >> 21;
        const int pxX = px16(u.posX), pxY = px16(u.posY);
        if (beh != behaviour::kOrnithopter && ttx == div32(pxX) && tty == div32(pxY)) {
            bool moved = false;
            const int cx = static_cast<int>(static_cast<std::uint32_t>(tcx >> 16) / 2);
            if (pxX / 2 < cx) { u.posX += 0x10000; moved = true; }
            else if (cx < pxX / 2) { u.posX -= 0x10000; moved = true; }
            const int cy = static_cast<int>(static_cast<std::uint32_t>(tcy >> 16) / 2);
            if (pxY / 2 < cy) { u.posY += 0x10000; return true; }
            if (cy < pxY / 2) { u.posY -= 0x10000; moved = true; }
            return moved;
        }
        const int dyT = tty - div32(pxY), dxT = ttx - div32(pxX);
        const int d2 = dyT * dyT + dxT * dxT;
        if (!(u.flags & 0x10000) && (u.flags & 0xb00) && d2 < 0x11) {
            spd = spd < 0x10001 ? 0x10000 : static_cast<std::uint32_t>(static_cast<std::int64_t>(static_cast<double>(spd) * 0.95));
        } else {
            spd = t.speed;
        }
        if (beh == behaviour::kOrnithopter) {
            if (d2 < 0x20) {
                if (d2 < 0x10 && (gameTicks_ & 7) == 0 && u.byte29 != 0) {
                    addBullet(u.side, t.primaryWeapon, 0, u.index, pxX, pxY, pxX, pxY, kNoIndex, -1);
                    --u.byte29;
                }
                noTurn = true;
            }
            if (u.byte29 == 0) {
                noTurn = true;
                u.flags |= kLeaving;
            }
        }
        if (ttx == div32(px16(u.posX)) || tty == div32(px16(u.posY))) u.flags &= ~0x400u;
        const int ang = atan4096(px16(u.posX - tcx), px16(tcy - u.posY));
        u.facingTurret = (ang >> 7) & 31;
        u.flags |= 0x400;
    }
    u.speed = spd;
    if (!noTurn) rotateFlyerBody(u, u.facingTurret);
    const int idx = (u.facing & 31) * kSinCosMags + static_cast<int>((spd & 0x7ffffff) >> 11);
    u.posX += sinTableFlat(idx);
    u.posY -= cosTableFlat(idx);
    markFlyer(u.blockToX, u.blockToY, false);
    const int nx = div32(px16(u.posX)), ny = div32(px16(u.posY));
    u.blockToX = u.blockFromX = nx & 0xff;
    u.blockToY = u.blockFromY = ny & 0xff;
    markFlyer(nx, ny, true);
    u.posSteps = 0;
    return true;
}

// UpdateUnit case 0 for carryalls (and frigates): pick up waiting units, carry them, deliver.
void World::updateCarryallIdle(Unit& u) {
    Side& s = sides_[u.side];
    const std::uint32_t f = u.flags;
    if (!(f & kDelivery)) {
        if (!(f & kCarrying)) {
            if (!(f & kAssigned)) {
                const int e = pickupEntryFor(s, u);
                if (e == -1) {
                    if (static_cast<std::uint32_t>(u.index & 7) == (gameTicks_ & 7))
                        u.facingTurret = (u.facingTurret + 1) & 31;
                    flyUnit(u, true);  // circle
                } else {
                    PickupEntry& pe = s.pickup[e];
                    u.targetX = pe.x;
                    u.targetY = pe.y;
                    u.enemyIndex = pe.unitIndex;
                    u.enemySide = static_cast<std::uint8_t>(pe.origState);  // state to give the cargo back
                    u.byte29 = static_cast<std::uint8_t>(e);
                    pe.state = 2;
                    pe.carryallIndex = u.index;
                    unitAdjustState(u, UnitState::Moving);
                    u.flags |= kAssigned;
                }
            } else {
                unitAdjustState(u, UnitState::FlyingDown);
            }
        } else {
            PickupEntry& pe = s.pickup[static_cast<std::size_t>(static_cast<std::int8_t>(u.byte29)) & 31];
            if (u.blockToX == pe.destX && u.blockToY == pe.destY) {
                unitAdjustState(u, UnitState::FlyingDown);
            } else {
                u.flags = f & ~kAssigned;
                u.targetX = pe.destX;
                u.targetY = pe.destY;
                unitAdjustState(u, UnitState::Moving);
            }
        }
        return;
    }
    if ((f & kCarrying) && !(f & kLeaving)) {
        if (u.blockToX == (u.targetX & 0xff) && u.blockToY == (u.targetY & 0xff)) unitAdjustState(u, UnitState::FlyingDown);
        else unitAdjustState(u, UnitState::Moving);
        return;
    }
    if (!(f & kAssigned)) {  // empty: leave over the nearest edge
        int tx = u.blockToX & 0xff, ty = u.blockToY & 0xff;
        carryallEntryPoint(tx, ty, 0);
        u.targetX = tx & 0xff;
        u.targetY = ty & 0xff;
        unitAdjustState(u, UnitState::Moving);
    }
}

// UpdateUnit case 0x13 (FLYING_DOWN): descend to 10 px, pick up / drop the cargo or the
// delivery, then climb again.
void World::updateFlyingDown(Unit& u) {
    Side& s = sides_[u.side];
    const std::uint32_t f = u.flags;
    if (f & kDelivery) {
        if (unitTemplate(u).specialBehavior == behaviour::kFrigate && !(f & kLeaving)) {
            const int pi = s.primaryBuilding[7];
            if (pi == kNoIndex) {  // starport gone: leave with the cargo
                u.flags = (f & ~kAssigned) | kLeaving;
                u.speed = unitTemplate(u).speed;
                unitAdjustState(u, UnitState::FlyingUp);
                return;
            }
            // The primary starport moved (destroyed / another one made primary): fly there.
            const Building& sp = s.buildings[static_cast<std::size_t>(pi) % kObjectsPerSide];
            const int sx = (div32(px16(sp.posX)) + 1) & 0xff, sy = (div32(px16(sp.posY)) - 2) & 0xff;
            if (sx != (u.blockToX & 0xff) || sy != (u.blockToY & 0xff)) {
                unitAdjustState(u, UnitState::FlyingUp);
                u.speed = unitTemplate(u).speed;
                u.targetX = sx;
                u.targetY = sy;
                return;
            }
        }
        if (u.posZ == 0xa0000 || unitTemplate(u).specialBehavior != behaviour::kCarryall ||
            canUnitUseSquare(u.targetX & 0xff, u.targetY & 0xff, u, u.side, 0)) {
            if (u.posZ < 0xa0001) {
                if (!carryallDrop(u)) {
                    if (unitTemplate(u).specialBehavior == behaviour::kFrigate) {
                        s.starportFrigateBusy = false;  // the starport takes orders again
                        s.starportSpent = 0;            // CSide__ClearStarportBusy 0x46eaf0
                    }
                    unitAdjustState(u, UnitState::FlyingUp);
                    u.flags = (u.flags & ~0x300u) | kLeaving;
                    if (ai_[u.side & 7].b(1) == 1) aiDeliveryDone(u.side, u.lastMoved);
                }
            } else {
                u.posZ -= 0x10000;
            }
        } else {
            int tx = u.targetX & 0xff, ty = u.targetY & 0xff;
            nearestFreeTile(tx, ty, 8);
            u.targetX = tx;
            u.targetY = ty;
            unitAdjustState(u, UnitState::FlyingUp);
        }
        return;
    }
    PickupEntry& pe = s.pickup[static_cast<std::size_t>(static_cast<std::int8_t>(u.byte29)) & 31];
    if (pe.state == 3 && !(f & kCarrying)) {
        u.flags &= ~kAssigned;
        unitAdjustState(u, UnitState::FlyingUp);
        return;
    }
    if (u.enemyIndex == kNoIndex) return;  // DebugFatal "EnemyIndex out of range"
    if (f & kCarrying) {
        if (!canUnitUseSquare(u.targetX, u.targetY, u, u.side, 0)) {
            nearestFreeTile(u.targetX, u.targetY, 8);
            pe.destX = u.targetX;
            pe.destY = u.targetY;
            unitAdjustState(u, UnitState::FlyingUp);
            return;
        }
    }
    Unit& v = s.units[static_cast<std::size_t>(u.enemyIndex) % kObjectsPerSide];
    if (u.posZ == 0x3c0000 && !(f & kCarrying) && !rotateFlyerBody(u, v.facing)) return;
    if (u.posZ >= 0xa0001) {
        u.posZ -= 0x10000;
        return;
    }
    if (!(f & kCarrying)) {
        if (!(v.flags & kCarrying) && v.state == UnitState::AwaitingAirlift) {
            std::uint32_t& c = cell(v.blockFromX, v.blockFromY).flags;  // lift the vehicle off its cell
            c &= ~kVehicle;
            if (c & kInfantryMask) c = (c & ~kOwnerMask) | ((c >> kInfantryOwnerShift) & 7);
            u.flags = (u.flags & ~0x4000000u) | kCarrying;
            v.selected = false;
            v.flags |= kCarrying;
            unitAdjustState(v, UnitState::Airlifted);
            s.pickup[u.byte29 & 31].state = 4;
        } else {
            u.flags = (u.flags & ~kAssigned) | kLeaving;
        }
        unitAdjustState(u, UnitState::FlyingUp);
        return;
    }
    v.flags &= ~kCarrying;
    v.posZ = 0;
    unitAdjustState(v, static_cast<UnitState>(u.enemySide & 0xff));
    std::uint32_t& c = cell(v.blockFromX, v.blockFromY).flags;
    c = (c & ~kOwnerMask) | static_cast<std::uint32_t>(u.side) | kVehicle;
    if (unitTemplate(v).isInfantry) {
        std::uint32_t& c2 = cell(u.blockFromX, u.blockFromY).flags;
        c2 = (c2 & 0xf1ffffffu) | (static_cast<std::uint32_t>(u.side) << kInfantryOwnerShift);
    }
    s.pickup[u.byte29 & 31].state = 0;
    u.flags &= ~kCarrying;
    unitAdjustState(u, UnitState::FlyingUp);
}

void World::updateFlyingUp(Unit& u) {  // case 0x14
    if (u.posZ < 0x3c0000) {
        u.posZ += 0x10000;
        return;
    }
    unitAdjustState(u, UnitState::Idle);
}

// End of UpdateUnit: a carryall carrying a unit drags it along 10 px below itself.
void World::dragCargo(Unit& c) {
    if (unitTemplate(c).specialBehavior != behaviour::kCarryall || (c.flags & kDelivery) || !(c.flags & kCarrying)) return;
    if (c.enemyIndex == kNoIndex) return;  // DebugFatal
    Unit& v = sides_[c.side].units[static_cast<std::size_t>(c.enemyIndex) % kObjectsPerSide];
    if (!(v.flags & kCarrying)) return;
    v.blockToX = c.blockToX;
    v.blockToY = c.blockToY;
    v.blockFromX = c.blockFromX;
    v.blockFromY = c.blockFromY;
    v.posX = c.posX;
    v.posSteps = 0;
    v.posY = c.posY;
    v.posZ = c.posZ - 0xa0000;
    v.facing = c.facing;
    v.facingTurret = c.facingTurret;
}

// =======================================================================================
// Starport: stock and prices, purchasing, frigate deliveries (re/notes/starport.md).

// FUN_0046e7a0 (Game_Init, all 8 sides): the starport unit list (one per unit group that has
// availableInStarport, house version) and the upgrade list; clears the order/price/stock arrays.
void World::initStarport(Side& s) {
    const auto& T = rules_.templates->bin;
    s.starportTypes.fill(-1);
    int n = 0;
    for (int t = 0; t < T.unitCount && n < 8; ++t) {
        if (!T.units[t].availableInStarport) continue;
        bool dup = false;
        for (int k = 0; k < n; ++k)
            if (T.units[s.starportTypes[k]].unitGroup == T.units[t].unitGroup) dup = true;
        if (!dup) s.starportTypes[n++] = sideVersionOfUnit(s, T.units[t].unitGroup, false);
    }
    s.starportOrdered.fill(0);
    s.starportPrice.fill(0);
    s.starportStock.fill(0);
    s.starportInitialised = true;
}

// FUN_0046ddc0 (one side per tick): price fluctuation every starportUpdateDelay/8 calls (only
// while nothing is ordered) and stock increases every starportStockIncreaseDelay/8 calls.
void World::updateStarport(Side& s) {
    if (!s.starportInitialised) return;
    const auto& V = vars();
    if (s.starportPriceTimer == 0) {
        for (int i = 0; i < formats::kMaxUnitTypes; ++i)
            if (s.starportOrdered[i] != 0) return;
        for (int t : s.starportTypes) {
            if (t < 0) continue;  // (vanilla always has 8 entries)
            const std::uint32_t cost = unitCost(t, s.id);
            const std::uint32_t var = static_cast<std::uint32_t>(cost * V.starportCostVariationPercent) / 100;
            if (var == 0) continue;  // (division by zero in the original; never in vanilla data)
            const std::uint32_t r = static_cast<std::uint32_t>(rng_.next());
            s.starportPrice[t] = static_cast<int>(r % var) - static_cast<int>(var) + static_cast<int>(cost);
        }
        s.starportPriceTimer = V.starportUpdateDelay >> 3;
    } else {
        --s.starportPriceTimer;
    }
    if (s.starportStockTimer != 0) {
        --s.starportStockTimer;
        return;
    }
    for (int t : s.starportTypes) {
        if (t < 0) continue;
        if (static_cast<std::uint32_t>(s.starportStock[t]) < 5 &&
            (static_cast<std::uint32_t>(rng_.next()) & 100) <= V.starportStockIncreaseProb)  // quirk: & not %
            ++s.starportStock[t];
    }
    s.starportStockTimer = V.starportStockIncreaseDelay >> 3;
}

// ModelStarportPick 0x456fd0 (order 0x29): at most 6 units per order, 5 of a type, from the
// stock; the price is paid now (LOWMONEY when it cannot be).
void World::starportPick(int side, int type) {
    Side& s = sides_[side & 7];
    std::uint32_t total = 0;
    // An empty slot (-1) reads the int just before +0x24268 in the original: SpiceDrip
    // (+0x24264). Vanilla always lists 8 types; with fewer (mods) this keeps the quirk instead
    // of indexing a wrong type (size_t(-1) % 60 was type 15).
    for (int t : s.starportTypes)
        total += static_cast<std::uint32_t>(t >= 0 && t < formats::kMaxUnitTypes ? s.starportOrdered[static_cast<std::size_t>(t)]
                                                                                 : s.spiceDrip);
    type &= 0xffff;
    if (type >= formats::kMaxUnitTypes) return;
    if (total >= 6 || s.starportStock[type] == 0 || static_cast<std::uint32_t>(s.starportOrdered[type]) >= 5) return;
    if (!spend(side, s.starportPrice[type])) {
        mentat(side, "LOWMONEY", true);
        return;
    }
    s.starportSpent += s.starportPrice[type];
    ++s.starportOrdered[type];
    --s.starportStock[type];
}

// ModelStarportUnpick 0x4570d0 (0x2a): one unit back to the stock, its price refunded.
void World::starportUnpick(int side, int type) {
    Side& s = sides_[side & 7];
    type &= 0xffff;
    if (type >= formats::kMaxUnitTypes || s.starportOrdered[type] == 0) return;
    --s.starportOrdered[type];
    ++s.starportStock[type];
    returnCredits(side, s.starportPrice[type]);
    s.starportSpent -= s.starportPrice[type];
}

// ModelStarportPurchase 0x457140 (0x2b): the picked units become a frigate delivery (type 2,
// due in starportFrigateDelay ticks, drop point filled in when the frigate starts). Without a
// primary starport the money comes back (the picks stay - original behaviour).
void World::starportPurchase(int side) {
    Side& s = sides_[side & 7];
    s.starportPurchasePending = false;
    if (s.primaryBuilding[7] == kNoIndex) {
        starportRefund(s);
        return;
    }
    int i = 0;
    while (i < 10 && s.deliveries[i].active) ++i;
    if (i >= 10) return;  // DebugFatal "Too many deliveries"
    Delivery& d = s.deliveries[i];
    d.active = true;
    d.started = false;
    d.x = d.y = 0;
    d.time = vars().starportFrigateDelay + gameTicks_;
    d.type = 2;
    int n = 0;
    for (int t = 0; t < formats::kMaxUnitTypes; ++t)
        for (int k = 0; k < s.starportOrdered[t] && n < 40; ++k) d.units[static_cast<std::size_t>(n++)] = static_cast<std::uint8_t>(t);
    // (DebugFatal "kMaxDeliverySize exceeded" past 40; never with at most 6 picks)
    if (n < 40) d.units[static_cast<std::size_t>(n)] = 0xff;
    s.starportOrdered.fill(0);
    if (n == 0) {
        d.active = false;
        return;
    }
    s.starportFrigateBusy = true;
}

// ModelStarportCancel 0x4572f0 (0x2c): every pick back to the stock and refunded.
void World::starportCancel(int side) {
    Side& s = sides_[side & 7];
    for (int t = 0; t < formats::kMaxUnitTypes; ++t) {
        s.starportStock[t] += s.starportOrdered[t];
        returnCredits(side, s.starportPrice[t] * s.starportOrdered[t]);
        s.starportOrdered[t] = 0;
    }
    s.starportSpent = 0;
}

bool World::starportRefund(Side& s) {  // CSide__ReturnMoneyFromStarportOrder 0x46eac0
    if (s.starportSpent == 0) return false;
    returnCredits(s.id, s.starportSpent);
    s.starportSpent = 0;
    return true;
}

// CSide__ReassignPrimaryBuilding 0x46d9a0, starport slot without a successor: refund the booked
// money and drop the frigate deliveries that have not been flown yet.
void World::starportLost(Side& s) {
    s.starportFrigateBusy = false;
    starportRefund(s);
    for (auto& d : s.deliveries)
        if (d.type == 2) d.active = false;
}

// FUN_0046def0 (local player): "frigate arriving" countdown FRIGATEIN5..2 by the time left,
// FRIGATEIN1 when due; requested every call, the sound queue's repeat guard of
// starportFrigateDelay/2 ticks per sample keeps each line from repeating.
void World::frigateSpeech(Side& s, const Delivery& d) {
    const std::uint32_t D = vars().starportFrigateDelay;
    int stage = 1;
    if (gameTicks_ < d.time) {
        const std::uint32_t left = d.time - gameTicks_;
        stage = left < (D >> 2) ? 2 : left < (D >> 1) ? 3 : left < ((D * 3) >> 2) ? 4 : 5;
    }
    static const char* const kNames[6] = {"", "FRIGATEIN1", "FRIGATEIN2", "FRIGATEIN3", "FRIGATEIN4", "FRIGATEIN5"};
    mentat(s.id, kNames[stage], true, static_cast<int>(D >> 1));
}

// ---------------------------------------------------------------------------------------
// Starport UI API (queries + order generation with the UI-side filters)

bool World::starportOpen(int side) const { return side >= 0 && side < kMaxSides && sides_[side].primaryBuilding[7] != kNoIndex; }
bool World::starportBusy(int side) const { return side >= 0 && side < kMaxSides && sides_[side].starportFrigateBusy; }

int World::starportPickedTotal(int side) const {
    if (side < 0 || side >= kMaxSides) return 0;
    const Side& s = sides_[side];
    int n = 0;
    for (int t : s.starportTypes)
        if (t >= 0) n += s.starportOrdered[static_cast<std::size_t>(t)];
    return n;
}

std::vector<StarportOption> World::starportUnits(int side) const {
    std::vector<StarportOption> out;
    if (side < 0 || side >= kMaxSides) return out;
    const Side& s = sides_[side];
    const int total = starportPickedTotal(side);
    for (int t : s.starportTypes) {
        StarportOption o;
        if (t >= 0 && t < formats::kMaxUnitTypes) {
            o.type = t;
            o.iconFrame = rules_.templates->firstUnitIconFrame + t;
            o.price = s.starportPrice[t];
            o.stock = s.starportStock[t];
            o.ordered = s.starportOrdered[t];
            o.available = o.stock != 0 && !s.starportFrigateBusy && total <= 5;
        }
        out.push_back(o);
    }
    return out;
}

// The sidebar click handler 0x4433a0 only picks while no frigate is on its way (+0x262a5);
// GenerateStarportPickOrder itself drops picks while a purchase is pending (+0x262a4).
bool World::orderStarportPick(int side, int type) {
    if (side < 0 || side >= kMaxSides || type < 0 || type >= formats::kMaxUnitTypes) return false;
    const Side& s = sides_[side];
    if (s.starportFrigateBusy || s.starportPurchasePending) return false;
    orders_.push_back({Order::StarportPick, side, kNoIndex, type, 0});
    return true;
}

bool World::orderStarportUnpick(int side, int type) {
    if (side < 0 || side >= kMaxSides || type < 0 || type >= formats::kMaxUnitTypes) return false;
    if (sides_[side].starportFrigateBusy) return false;  // (0x4433a0: right click only while idle)
    orders_.push_back({Order::StarportUnpick, side, kNoIndex, type, 0});
    return true;
}

// PURCHASE button: only when something is picked (IsAnyStarportUnitPicked 0x443370).
bool World::orderStarportPurchase(int side) {
    if (side < 0 || side >= kMaxSides) return false;
    Side& s = sides_[side];
    if (s.starportPurchasePending) return false;
    bool any = false;
    for (int v : s.starportOrdered) any = any || v != 0;
    if (!any) return false;
    s.starportPurchasePending = true;
    orders_.push_back({Order::StarportPurchase, side, kNoIndex, 0, 0});
    uiMentat(side, "PORTORDER", false);
    return true;
}

// MAIN / UPGRADE buttons on the starport page with picks: cancel (H/O/S_CANCELLED).
bool World::orderStarportCancel(int side) {
    if (side < 0 || side >= kMaxSides) return false;
    bool any = false;
    for (int v : sides_[side].starportOrdered) any = any || v != 0;
    if (!any) return false;
    orders_.push_back({Order::StarportCancel, side, kNoIndex, 0, 0});
    uiMentat(side, "CANCELLED", false);
    return true;
}

// ---------------------------------------------------------------------------------------
// Crates and spice blooms (gCrates 0x517b98; re/notes/crates.md)

int World::freeCrateIndex() const {  // GetFreeCrateIndex 0x44e790
    for (int i = 0; i < kMaxCrates; ++i)
        if (!crates_[i].active) return i;
    return -1;
}

// PlaceCrate 0x44e8e0: fill the first free record and set the cell's crate bit.
void World::placeCrate(int x, int y, std::uint32_t timer, int type, int image, int respawns) {
    const int i = freeCrateIndex();
    if (i < 0) return;
    Crate& c = crates_[i];
    c.active = true;
    c.x = static_cast<std::uint8_t>(x);
    c.y = static_cast<std::uint8_t>(y);
    c.timer = timer;
    c.type = static_cast<std::uint8_t>(type);
    c.image = static_cast<std::uint8_t>(image);
    c.respawns = static_cast<std::uint8_t>(respawns);
    if (std::uint32_t* f = linearCellFlags(c.x, c.y)) *f |= kCrate;
}

// FUN_0044e970: type of the active record on (x, y), 0xff when none (no side effects).
int World::crateTypeAt(int x, int y, int& index) const {
    index = 0xff;
    for (int i = 0; i < kMaxCrates; ++i) {
        const Crate& c = crates_[i];
        if (c.x == static_cast<std::uint32_t>(x) && c.y == static_cast<std::uint32_t>(y) && c.active) {
            index = i;
            return c.type;
        }
    }
    return 0xff;
}

// GetCrateFromMap 0x44e9d0: the type of the active record on (x, y), 0xb when none. Side
// effects (also when the AI merely looks at a crate, CAI__DoStopStartStuff): a bloom of stage
// 8..10 is recycled, an ordinary crate (0..6) is removed; a new bloom (7) stays.
int World::getCrateFromMap(int x, int y) {
    for (int i = 0; i < kMaxCrates; ++i) {
        Crate& c = crates_[i];
        if (c.x != static_cast<std::uint32_t>(x) || c.y != static_cast<std::uint32_t>(y) || !c.active) continue;
        const int t = c.type;
        if (t == 7) return t;
        if (t > 7 && t < 0xb) {
            recycleCrate(i);
            return t;
        }
        c.active = false;
        if (std::uint32_t* f = linearCellFlags(c.x, c.y)) *f &= ~kCrate;
        return t;
    }
    return 0xb;
}

// RecycleCrate 0x44f110: a bloom with regrowths left starts again as a new bloom (image 4, timer
// 2000..2999: one rand); otherwise the record is freed.
void World::recycleCrate(int index) {
    Crate& c = crates_[index & 0xff];
    if (c.respawns != 0) {
        --c.respawns;
        c.image = 4;
        c.timer = static_cast<std::uint32_t>(rng_.next()) % 1000 + 2000;
        c.type = 7;
        return;
    }
    c.active = false;
    if (std::uint32_t* f = linearCellFlags(c.x, c.y)) *f &= ~kCrate;
}

// SpiceMound 0x44ef10: size*size*2 tries (3 rand each) of a random offset (direction rand & 31,
// distance rand % size via the sin/cos table); on sand/spice inside the map a spice shell
// (groupIds[50]) is fired from the mound to that tile (2 more rand, side = next participating
// side, round robin from side 1).
void World::spiceMound(int x, int y, int size) {
    const std::uint32_t n = static_cast<std::uint32_t>(size & 0xff);
    const auto& G = rules_.templates->bin.groupIds;
    std::uint8_t side = 0;
    for (std::uint32_t k = n * n * 2; k != 0; --k) {
        const std::uint32_t r1 = static_cast<std::uint32_t>(rng_.next());
        const std::uint32_t r2 = static_cast<std::uint32_t>(rng_.next());
        const std::uint32_t r3 = static_cast<std::uint32_t>(rng_.next());
        const int dir = static_cast<int>(r1 & 0x1f), m = static_cast<int>(r3 % n);
        const int tx = sinTable(dir, m) / 2048 + (x & 0xff);
        const int ty = cosTable(dir, m) / 2048 + (y & 0xff);
        if (tx <= 0 || tx >= width_ || ty <= 0 || ty >= height_) continue;
        if (!(cell(tx, ty).flags & 0x710000u)) continue;
        do side = static_cast<std::uint8_t>((side + 1) & 7);
        while (!sides_[side].participates);
        const std::uint32_t r4 = static_cast<std::uint32_t>(rng_.next());
        const std::uint32_t r5 = static_cast<std::uint32_t>(rng_.next());
        addBullet(side, static_cast<std::uint8_t>(G[50]), static_cast<int>(r2 & 0xf), kNoIndex, (x & 0xff) * 32 + 16,
                  (y & 0xff) * 32 + 16, static_cast<int>(r5 & 0x1f) + tx * 32, static_cast<int>(r4 & 0x1f) + ty * 32);
    }
    playNamed("S_SPICEMOUND", 0, x, y);
}

// FUN_0044ea70 (main loop, every tick): count down every record. On expiry: ordinary crates
// vanish; a bloom of stage 7..9 grows one stage when its cell is free of units and spice
// (explosion groupIds[63]) and reloads its timer (rand); a ripe bloom (10) bursts into a spice
// mound when free, else reloads. Then one random crate is added when fewer than
// _NumCratesAllowed ordinary crates exist.
void World::updateCrates() {
    int crateCount = 0;
    const auto& G = rules_.templates->bin.groupIds;
    for (int i = 0; i < kMaxCrates; ++i) {
        Crate& c = crates_[i];
        if (!c.active) continue;
        if (c.type < 6) ++crateCount;
        if (--c.timer != 0) continue;
        const std::uint32_t* fp = linearCellFlags(c.x, c.y);
        const std::uint32_t f = fp ? *fp : 0;
        if (c.type < 7 || c.type > 10) {
            c.active = false;
            if (std::uint32_t* p = linearCellFlags(c.x, c.y)) *p &= ~kCrate;
        } else if (c.type < 10) {
            if ((f & 0x7003f8u) == 0) {
                ++c.image;
                ++c.type;
                // (The original also walks to the next participating side here, but the
                // explosion uses gGameTicks & 7 as its side.)
                if (G[63] != -1)
                    addExplosion(static_cast<int>(gameTicks_ & 7), static_cast<std::uint8_t>(G[63]), c.x * 32 + 16,
                                 c.y * 32, 0);
            }
            c.timer = static_cast<std::uint32_t>(rng_.next()) % 1000 + 2000;
        } else if ((f & 0x7003f8u) == 0) {
            spiceMound(c.x, c.y, 6);
            recycleCrate(i);
        } else {
            c.timer = static_cast<std::uint32_t>(rng_.next()) % 1000 + 2000;
        }
    }
    if (crateCount < numCratesAllowed_) spawnRandomCrate();
}

// FUN_0044e7c0 / FUN_0044e800: a random drivable+walkable empty tile (2 rand per try), a random
// type from the cumulative table 0x4e0e18 {50, 65, 70, 75, 95, 100} (1 rand), 5000 ticks.
void World::spawnRandomCrate() {
    static constexpr std::uint8_t kCrateOdds[6] = {50, 65, 70, 75, 95, 100};
    int x = 0, y = 0;
    for (;;) {
        x = static_cast<std::uint8_t>(static_cast<std::uint32_t>(rng_.next()) % static_cast<std::uint32_t>(width_));
        y = static_cast<std::uint8_t>(static_cast<std::uint32_t>(rng_.next()) % static_cast<std::uint32_t>(height_));
        const std::uint32_t* f = linearCellFlags(x, y);
        if (f && (*f & 0x73f8u) == 0x6000u) break;
    }
    const std::uint8_t r = static_cast<std::uint8_t>(static_cast<std::uint32_t>(rng_.next()) % 100);
    int type = 5;
    for (int i = 0; i < 6; ++i)
        if (r < kCrateOdds[i]) {
            type = i;
            break;
        }
    placeCrate(x, y, 5000, type, 0, 0);
}

// MakeUnitsStealthInRange: units of `side` within distance^2 < 9: saboteurs get a full stealth
// charge, other units become stealth units (unless already hidden).
void World::makeUnitsStealthInRange(int x, int y, int side) {
    Side& S = sides_[side & 7];
    for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
        Unit& u = S.units[i];
        const int dx = u.blockFromX - (x & 0xff), dy = u.blockFromY - (y & 0xff);
        if (static_cast<std::uint32_t>(dx * dx + dy * dy) >= 9) continue;
        if (unitTemplate(u).specialBehavior == behaviour::kSaboteur) {
            u.byte29 = 0xa0;
        } else if (!unitIsHiddenStealth(u)) {
            u.stealthTimer = 0;
            u.flags |= kStealth;
        }
    }
}

// Skirmish colour assignment (Game__MainLoopFrame 0x44a54e, GameType 1, after srand and before
// Setup_SpawnMapObjects): slot 0 (the player) keeps gNetPlayerColor; slots 1..5 draw rand() & 7
// until it is neither 4 nor 7 nor the colour of an earlier slot or of the player.
void World::assignSkirmishColours(int playerColor) {
    constexpr int kSlots = 6;  // NetPlayersColor 0x6b922d..0x6b9395, stride 0x3c
    playerColors_[0] = playerColor & 0xff;
    for (int i = 1; i < kSlots; ++i) {
        bool clash = false;
        do {
            int c = 0;
            do c = rng_.next() & 7;
            while (c == 4 || c == 7);
            playerColors_[i] = c;
            clash = false;
            for (int j = 0; j < i; ++j)
                if (playerColors_[j] == c || playerColors_[0] == c) clash = true;
        } while (clash);
    }
}

}  // namespace d2k::game

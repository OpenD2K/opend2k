// Ground unit movement (re/notes/movement.md): MoveUnit 0x494260, GetNextSquare 0x4936a0,
// CanUnitUseSquare 0x45d990, GetSpeed 0x49f3b0, RotateUnitBody/Turret 0x493eb0/0x493f40,
// infantry animation 0x494050/0x4940d0, tile queries 0x4a5200/0x4a5270/0x4a54a0/0x4a5570,
// FindFreeAdjacentSquare 0x45dee0, AskUnitsOnTileToMoveAway 0x497af0, crushing 0x49fa50/0x49f8f0.
// The movement core never calls rand(); the infantry idle animation and turret wander do.

#include "game/geometry.hpp"
#include "game/tables.hpp"
#include "game/world.hpp"

#include <cstdlib>
#include <utility>

namespace d2k::game {

using namespace cell_flags;
using namespace unit_flags;

namespace {
// Infantry sub-cell pixel offsets inside a cell (MoveUnit table 0x4d7028 == 0x4c93e8).
constexpr int kSpotOffset[5][2] = {{16, 21}, {23, 14}, {23, 26}, {7, 26}, {7, 14}};
int animFirst(int t) { return kInfAnim[t & 15][0]; }
int animLast(int t) { return kInfAnim[t & 15][1]; }
}  // namespace

// ---------------------------------------------------------------------------------------
// Queries

bool World::unitIsMoving(const Unit& u) const {  // 0x49e140
    return u.posSteps != 0 || u.routeSteps != 0 || u.state == UnitState::Moving;
}

bool World::unitIsHiddenStealth(const Unit& u) const {  // 0x49f5f0
    if (!(u.flags & kStealth)) return false;
    if (u.flags & kCloaked) return true;
    return u.stealthTimer > 1;
}

bool World::unitCanTakeOrders(const Unit* u, bool fromPlayer) const {  // 0x49f4a0
    if (!u) return false;
    if (u->state == UnitState::AwaitingAirlift && !fromPlayer) return false;
    if (u->state == UnitState::LeavingBuilding || u->state == UnitState::Invalid) return false;
    return (u->flags & kExcluded) == 0;
}

bool World::unitOccupiesTile(const Unit& u, int x, int y) const {  // 0x4a5200
    const bool on = (u.blockFromX == x && u.blockFromY == y) ||
                    ((u.flags & kBlockToMarked) && u.blockToX == x && u.blockToY == y);
    if (!on) return false;
    const int beh = unitTemplate(u).specialBehavior;
    if (beh == 2 || beh == 8 || beh == 9 || beh == 10 || beh == 5) return false;
    return (u.flags & kExcluded) == 0;
}

bool World::buildingOccupiesTile(const Building& b, int x, int y) const {  // 0x4a5570
    if (x < b.tileX || x > b.maxX || y < b.tileY || y > b.maxY) return false;
    const auto& t = rules_.templates->bin.buildings[b.type];
    return (t.tilesOccupiedSolid >> ((y - b.tileY) * 4 + (x - b.tileX))) & 1;
}

// GetUnitOnTile 0x4a5270 (pixel inputs). Vehicles: the side's ground non-infantry unit whose
// BlockFrom (or marked BlockTo) is the cell. Infantry: the nearest (Manhattan px) on the cell.
const Unit* World::getUnitOnTile(int px, int py, int& sideOut, int& indexOut, bool skipCloaked) const {
    const int cx = px >> 5, cy = py >> 5;
    indexOut = -1;
    if (!inBounds(cx, cy)) {  // the original clamps to 127 (byte); we require in-map cells
        sideOut = 0xFF;
        return nullptr;
    }
    const std::uint32_t f = cell(cx, cy).flags;
    const int owner = static_cast<int>(f & kOwnerMask);
    sideOut = owner;
    const Side& s = sides_[owner];
    auto onCell = [&](const Unit& u) {
        return (u.blockFromX == cx && u.blockFromY == cy) ||
               ((u.flags & kBlockToMarked) && u.blockToX == cx && u.blockToY == cy);
    };
    if (f & kOccupied) {
        if (!(f & kVehicle)) {
            if (f & kInfantryMask) {
                std::uint32_t best = 0x800;
                for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) {
                    const Unit& u = s.units[i];
                    if (!unitTemplate(u).isInfantry) continue;
                    if (skipCloaked && (u.flags & kCloaked)) continue;
                    if (!onCell(u) || (u.flags & kExcluded)) continue;
                    const int dy = (u.posY >> 16) - py, dx = (u.posX >> 16) - px;
                    const std::uint32_t d = static_cast<std::uint32_t>((dy < 0 ? -dy : dy) + (dx < 0 ? -dx : dx));
                    if (d < best) {
                        indexOut = u.index;
                        best = d;
                    }
                }
                return indexOut == -1 ? nullptr : &s.units[indexOut];
            }
        } else {
            for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) {
                const Unit& u = s.units[i];
                if (!onCell(u)) continue;
                const auto& t = unitTemplate(u);
                const int beh = t.specialBehavior;
                if (t.isInfantry || beh == 2 || beh == 8 || beh == 9 || beh == 10 || beh == 5) continue;
                if (u.flags & kExcluded) continue;
                if (skipCloaked && (u.flags & kCloaked)) return nullptr;
                indexOut = u.index;
                return &u;
            }
        }
    }
    sideOut = 0xFF;
    return nullptr;
}

Unit* World::getUnitOnTile(int px, int py, int& sideOut, int& indexOut, bool skipCloaked) {
    return const_cast<Unit*>(std::as_const(*this).getUnitOnTile(px, py, sideOut, indexOut, skipCloaked));
}

// GetNextUnitOnTile 0x4a54a0: continues after `index` (kNoIndex = from the list head).
Unit* World::getNextUnitOnTile(int x, int y, int side, int& index) {
    if (side < 0 || side >= kMaxSides) return nullptr;
    Side& s = sides_[side];
    int i = index == kNoIndex ? s.firstUnit : s.nextObject(index);
    for (; i != -1; i = s.nextObject(i)) {
        Unit& u = s.units[i];
        const bool on = (u.blockToX == x && u.blockToY == y) || (u.blockFromX == x && u.blockFromY == y);
        if (!on || unitTemplate(u).specialBehavior == behaviour::kSandworm || (u.flags & kExcluded)) continue;
        index = u.index;
        return &u;
    }
    return nullptr;
}

// FindFreeSpotForInfantry 0x44da90: first free spot bit among 0x200, 0x100, 0x80, 0x40, 0x20.
static std::uint32_t findFreeSpotForInfantry(std::uint32_t f) {
    for (std::uint32_t bit = 0x200; bit >= 0x20; bit >>= 1)
        if (!(f & bit)) return bit;
    return 0;
}

// CanUnitUseSquare 0x45d990. Modes: 0 strict, 1 moving friendlies passable, 2 all
// friendlies passable, 3 units ignored (walls only).
bool World::canUnitUseSquare(int x, int y, const Unit& u, int side, int mode) const {
    const auto& t = unitTemplate(u);
    const bool crush = t.canCrushInfantry != 0;
    const bool inf = t.isInfantry != 0;
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return false;
    const std::uint32_t c = cell(x, y).flags;
    const std::uint32_t occ = c & kOccupied;
    const int own = static_cast<int>(c & kOwnerMask);
    auto enemy = [&](int o) { return diplomacy(side, o) != 0; };
    if (t.specialBehavior == behaviour::kSandworm) return (c & kSand) && !(c & kWorm);
    if (!inf && !(c & kAnyUnit) && (c & kBuilding) && (c & kExitTile) && own == side) return true;
    // Special states that may enter a building / enemy cell.
    if (u.state == UnitState::MovingToRepairPad) {
        if (!(c & kAnyUnit) && (c & kBuilding) && own == side) {
            const Building* b = const_cast<World*>(this)->building(side, u.repairPadIndex);
            if (b && buildingOccupiesTile(*b, x, y)) return true;
        }
    } else if (u.state == UnitState::AttackingBuilding) {
        if (c & kBuilding) {
            const Building* b = const_cast<World*>(this)->building(u.enemySide, u.enemyIndex);
            if (b && buildingOccupiesTile(*b, x, y)) return true;
        }
    } else if (u.state == UnitState::AttackingUnit) {
        if (chebyshev(u.blockFromX, u.blockFromY, x, y) > 1 && (c & kAnyUnit)) {
            const Unit* e = const_cast<World*>(this)->unit(u.enemySide, u.enemyIndex);
            if (e && unitOccupiesTile(*e, x, y)) return true;
        }
    }
    auto crushRule = [&] { return !(c & kVehicle) && (c & kInfantryMask) && crush; };
    auto movingUnitThere = [&] {
        int s2 = 0, i2 = 0;
        const Unit* u2 = getUnitOnTile(x << 5, y << 5, s2, i2, false);
        return u2 && unitIsMoving(*u2);
    };
    switch (mode) {
    case 0:
        if (!inf) {
            if (!(c & kDriveOn) || (c & (kVehicle | kBuilding))) return false;
            if (!(c & kInfantryMask)) return true;
            return crush && enemy(own);
        }
        if (!(c & kWalkOn) || (c & (kVehicle | kBuilding))) return false;
        if (!(c & kInfantryMask)) return true;
        return own == side && findFreeSpotForInfantry(c) != 0;
    case 1:
        if (inf) {
            if (!(c & kWalkOn) || (c & kBuilding)) return false;
            if (occ == 0) return true;
            if ((c & kInfantryMask) && own == side && findFreeSpotForInfantry(c)) return true;
            if (enemy(own)) return false;
            return movingUnitThere();
        }
        if (!(c & kDriveOn) || (c & kBuilding)) return false;
        if (occ == 0) return true;
        if (!enemy(own)) return movingUnitThere();
        return crushRule();
    case 2:
        if (inf) {
            if (!(c & kWalkOn) || (c & kBuilding)) return false;
            if (occ == 0) return true;
            return !enemy(own);
        }
        if (!(c & kDriveOn) || (c & kBuilding)) return false;
        if (occ == 0 || !enemy(own)) return true;
        return crushRule();
    case 3:
        if (!(c & (inf ? kWalkOn : kDriveOn))) return false;
        if (!(c & kWall)) return !(c & kBuilding);
        return enemy(own);  // routes through enemy walls
    default: return false;  // DebugFatal "Invalid routeMode"
    }
}

// GetSpeed 0x49f3b0: from the cell the unit is LEAVING (BlockFrom).
std::int32_t World::getSpeed(const Unit& u) const {
    const auto& t = unitTemplate(u);
    const std::uint32_t c = cell(u.blockFromX, u.blockFromY).flags;
    std::int32_t s;
    if (c & kConcrete) {
        s = static_cast<std::int32_t>(static_cast<double>(t.speed) * 1.1);  // double @0x4d7060
    } else {
        const int terr = static_cast<int>(c >> kTerrainShift);
        const int st = t.speedType > 3 ? 3 : t.speedType;  // DebugFatal if > 3
        const float f = rules_.speed ? rules_.speed->values[terr][st] : 1.0f;
        s = static_cast<std::int32_t>(static_cast<double>(t.speed) * static_cast<double>(f));
    }
    return s;  // (DebugFatal "theSpeed == 0" when 0)
}

// MoveUnit's speed for a tile step (disassembly 0x494677..0x494725). Quirk: below 1/3 health
// the factor is 0.85, between 1/3 and 1/2 it is 0.66.
std::int32_t World::stepSpeed(const Unit& u) const {
    const auto& t = unitTemplate(u);
    if (!t.isInfantry) {
        const std::int32_t hp = static_cast<std::int32_t>(t.hitPoints);
        if (u.health * 3 < hp) return static_cast<std::int32_t>(static_cast<double>(getSpeed(u)) * static_cast<double>(0.85f));
        if (u.health * 2 < hp) return static_cast<std::int32_t>(static_cast<double>(getSpeed(u)) * static_cast<double>(0.66f));
        return getSpeed(u);
    }
    std::int32_t s = getSpeed(u);
    if (u.animType == 5) s = static_cast<std::int32_t>(static_cast<std::uint32_t>(s) >> 1);  // crawling
    return s;
}

// ---------------------------------------------------------------------------------------
// Turning and animation

bool World::rotateBody(Unit& u, int want) {  // 0x493eb0: false once facing `want`
    if (u.facing == want) return false;
    setInfantryAnim(u, 0);  // infantry stop walking while they turn
    if (u.turnDelay != 0) {
        --u.turnDelay;
        return true;
    }
    const int f = u.facing;
    const int d = f - want < 0 ? want - f : f - want;
    int step;
    if (d < 16) step = f <= want ? 1 : -1;
    else step = want < f ? 1 : -1;
    u.facing = (f + step) & 31;
    u.turnDelay = unitTemplate(u).unitRotationSpeed;
    return true;
}

void World::rotateTurret(Unit& u, int want) {  // 0x493f40
    if (u.flags & kFixedBarrel) {
        u.facingTurret = u.facing;
        return;
    }
    u.facingTurretTarget = want;
    const int f = u.facingTurret;
    if (f == want) return;
    if (u.turretTurnDelay != 0) {
        --u.turretTurnDelay;
        return;
    }
    const int d = f - want < 0 ? want - f : f - want;
    int step;
    if (d < 16) step = f <= want ? 1 : -1;
    else step = want < f ? 1 : -1;
    u.facingTurret = (f + step) & 31;
    u.turretTurnDelay = unitTemplate(u).barrelRotationSpeed;
}

// 0x4954b0 (also used to turn idle infantry before a fidget).
bool World::rotateFlyerBody(Unit& u, int want) {
    const int f = u.facing;
    if (want == f) return true;
    if (u.turnDelay == 0) {
        u.turnDelay = unitTemplate(u).unitRotationSpeed;
        const int d1 = (want - f + 32) & 31, d2 = (f - want + 32) & 31;
        u.facing = d1 < d2 ? (f + 1) & 31 : (f - 1) & 31;
        return false;
    }
    --u.turnDelay;
    return false;
}

void World::setInfantryAnim(Unit& u, int anim) {  // 0x494050
    if (!unitTemplate(u).isInfantry) return;
    if (anim != 0 || (u.animType < 14 && ((u.animType != 2 && u.animType != 6) || u.enemyIndex == kNoIndex))) {
        u.animType = anim;
        if (u.lying && anim < 3) u.animType = (anim + 4) & 0xFF;
        if (u.animFrame < animFirst(u.animType) || u.animFrame > animLast(u.animType)) u.animFrame = animFirst(u.animType);
    }
}

// AdvanceInfantryAnim 0x4940d0: one frame every 3 ticks; at the end of a cycle the
// animation resets to standing / prone, with random lying down and idle fidgets (RNG).
void World::advanceInfantryAnim(Unit& u) {
    if (!unitTemplate(u).isInfantry) return;
    u.animDelay = static_cast<std::uint8_t>(u.animDelay + 1);
    if (u.animDelay <= 2) return;
    u.animDelay = 0;
    u.animFrame = (u.animFrame + 1) & 0xFF;
    const int t = u.animType;
    if (u.animFrame >= animFirst(t) && u.animFrame <= animLast(t)) return;
    if (t > 8 && t < 14) {  // deaths hold their last frame
        u.animFrame = animLast(t);
        return;
    }
    if (u.lastAttacked == 0) {
        if (static_cast<std::uint32_t>(rng_.next()) % 5 == 0) {
            u.lying = 1;
            u.animType = 4;
        }
    } else if (u.lying == 1 && u.lastAttacked < 0x28) {
        u.animType = 4;
    } else {
        u.lying = 0;
        u.animType = 0;
    }
    if (u.lastAttacked == 0xFF) {
        const std::uint32_t r = static_cast<std::uint32_t>(rng_.next());
        if (r % 400 < 2) {
            if (u.facing < 8) {
                rotateFlyerBody(u, 8);
                u.animFrame = animFirst(u.animType);
                return;
            }
            if (u.facing > 24) {
                rotateFlyerBody(u, 24);
                u.animFrame = animFirst(u.animType);
                return;
            }
            u.animType = static_cast<int>(r % 400) + 14;
        }
    }
    u.animFrame = animFirst(u.animType);
}

void World::idleTurretWander(Unit& u) {  // 0x494210
    if (!unitTemplate(u).primaryWeaponUseBarrel) return;  // template +0x26: "has a turret"
    if (u.facingTurret == u.facingTurretTarget) {
        const std::uint32_t r = static_cast<std::uint32_t>(rng_.next());
        if (r < 31) u.facingTurretTarget = static_cast<int>(r & 31);
    }
    rotateTurret(u, u.facingTurretTarget);
}

// ---------------------------------------------------------------------------------------
// Neighbourhood helpers

// FindFreeAdjacentSquare 0x45dee0: facing direction first, then +4k / -4k for k = 1..4.
bool World::findFreeAdjacentSquare(int x, int y, const Unit& u, int side, int& outX, int& outY, int mode) const {
    int d = u.facing & ~3;
    auto test = [&](int dir) {
        outX = x;
        outY = y;
        stepInDirection(outX, outY, dir);
        return canUnitUseSquare(outX, outY, u, side, mode);
    };
    if (test(d)) return true;
    int d1 = d, d2 = d;
    for (int k = 0;; ++k) {
        d1 += 4;
        if (d1 > 31) d1 = 0;
        if (test(d1)) return true;
        d2 -= 4;
        if (d2 < 0) d2 = 28;
        if (test(d2)) return true;
        if (k + 1 > 3) return false;
    }
}

// AskUnitsOnTileToMoveAway 0x497af0.
void World::askUnitsOnTileToMoveAway(const Unit& caller, int x, int y) {
    int vs = 0, vi = 0;
    if (!getUnitOnTile(x << 5, y << 5, vs, vi, false)) return;
    int index = kNoIndex;
    for (;;) {
        Unit* w = getNextUnitOnTile(x, y, vs, index);
        if (!w) break;
        if (unitCanTakeOrders(w, false) && w->state != UnitState::Moving && w != &caller)
            unitAdjustState(*w, UnitState::MovingAway);
    }
}

// PickupCrate 0x49ebf0: called when BlockTo has the crate bit.
// Returns true only for the "units" crate (the caller re-plans its route). Worms never pick up.
bool World::pickupCrate(Unit& u) {
    const auto& T = rules_.templates->bin;
    if (unitTemplate(u).specialBehavior == behaviour::kSandworm) return false;
    const int bx = u.blockToX, by = u.blockToY;
    const std::uint32_t* f = linearCellFlags(bx, by);
    if (!f || !(*f & kCrate)) return false;
    const int type = getCrateFromMap(bx, by);
    if (type == 0xb) return false;
    const int side = u.side;
    const int cx = bx * 32 + 16, cy = by * 32 + 16;
    auto boom = [&](int g) {  // explosion groupIds[g] on the crate; its sound for the local player
        const int e = static_cast<std::uint8_t>(T.groupIds[g]);
        if (e == 0xff) return false;
        addExplosion(side, e, cx, cy, 0);
        if (side == mySide_ && static_cast<std::size_t>(e) < explosionCount())
            playSound(explosionTemplate(e).sound, bx, by);
        return true;
    };
    switch (type) {
    case 0:  // cash
        addCash(side, static_cast<std::int32_t>(vars().crateCash));
        boom(54);
        break;
    case 1:  // explosion
        boom(58);
        if (T.groupIds[23] != -1) damageTiles(cx, cy, 0, static_cast<std::uint8_t>(T.groupIds[23]), side, kNoIndex, false);
        break;
    case 2:  // reveal the map
        boom(53);
        if (side == mySide_) revealMap();
        break;
    case 3:  // shroud the map again
        boom(55);
        if (side == mySide_) {
            for (auto& c : cells_) c.shroud = 1;
            revealTilesSeenByBuildingsAndUnits(side);
        }
        break;
    case 4: {  // units: one vehicle, or five infantry, of random groups of this side's version
        const Side& S = sides_[side];
        const std::uint32_t groups = T.unitGroupCount ? T.unitGroupCount : 1;
        auto beh = [&](int t) { return t >= 0 ? static_cast<int>(T.units[t].specialBehavior) : 0; };
        auto inf = [&](int t) { return t >= 0 && T.units[t].isInfantry != 0; };
        int t = -1;
        for (;;) {
            do t = sideVersionOfUnit(S, static_cast<int>(static_cast<std::uint32_t>(rng_.next()) % groups & 0xff), false);
            while (beh(t) == behaviour::kSandworm);
            const int b = beh(t);
            if (b != behaviour::kFrigate && b != behaviour::kOrnithopter && b != behaviour::kCarryall &&
                b != behaviour::kDeathHand)
                break;
        }
        if (!inf(t)) {
            if (t >= 0) addUnit(side, t, bx, by, bx, by);
            return true;
        }
        for (int k = 0; k < 5; ++k) {
            do t = sideVersionOfUnit(S, static_cast<int>(static_cast<std::uint32_t>(rng_.next()) % groups & 0xff), false);
            while (!inf(t));
            addUnit(side, t, bx, by, bx, by);
        }
        return true;
    }
    case 5:  // stealth
        makeUnitsStealthInRange(bx, by, side);
        boom(56);
        break;
    case 7: break;  // a new bloom: nothing
    case 8: spiceMound(bx, by, 4); break;
    case 9: spiceMound(bx, by, 5); break;
    case 10: spiceMound(bx, by, 6); break;
    default: break;  // DebugFatal "Someone picked up an unsupported crate"
    }
    return false;
}

// ---------------------------------------------------------------------------------------
// Crushing (UpdateUnit prologue)

// InfantryCrushCheck 0x49fa50: every unit of the cell owner's side on the same BlockFrom
// within 16 px (Chebyshev) crushes the infantry. The loop does not check that the crusher is
// a vehicle (faithful).
void World::crushCheck(Unit& u) {
    const int owner = static_cast<int>(cell(u.blockFromX, u.blockFromY).flags & kOwnerMask);
    Side& s = sides_[owner];
    for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) {
        const Unit& v = s.units[i];
        if (v.blockFromX != u.blockFromX || v.blockFromY != u.blockFromY) continue;
        const int dx = std::abs((u.posX >> 16) - (v.posX >> 16)), dy = std::abs((u.posY >> 16) - (v.posY >> 16));
        if ((dx > dy ? dx : dy) < 16) crushInfantry(u, owner, v.index);
    }
}

// CrushInfantry 0x49f8f0.
void World::crushInfantry(Unit& u, int killerSide, int killerIndex) {
    if (u.state == UnitState::Dead || (u.flags & kExcluded)) return;
    u.attackerSide = static_cast<std::uint8_t>(killerSide);
    u.attackerIndex = killerIndex;
    ++sides_[u.side].unitsLost;
    ++sides_[u.side].unitsLostPerType[u.type];
    // (+0x24c98 per type and side kill statistics are not modelled)
    ++sides_[killerSide & 7].unitsKilled;  // +0x24c90 of the crushing side
    setInfantryAnim(u, 13);
    destroyUnit(u);
    playNamed("S_CRUSHMAN", 0, u.blockFromX, u.blockFromY);
    u.flags |= kGone;
    cell(u.blockFromX, u.blockFromY).flags &= ~(0x20u << u.cellspotFrom);
    if (u.flags & kBlockToMarked) cell(u.blockToX, u.blockToY).flags &= ~(0x20u << u.cellspotTo);
}

// ---------------------------------------------------------------------------------------
// GetNextSquare 0x4936a0

void World::storeRoute(Unit& u, const PathPoint* p, int len) {
    // Route[RouteSteps-1-i] = p[i], RouteSteps = min(len-1, 12) (the original's unsigned
    // clamp turns len 0 into 12; never reached since callers check len).
    unsigned n = static_cast<unsigned>(len - 1);
    if (n > 11) n = 12;
    u.routeSteps = static_cast<int>(n);
    for (unsigned i = 0; i < n; ++i) {
        u.routeX[n - 1 - i] = static_cast<std::uint8_t>(p[i].x);
        u.routeY[n - 1 - i] = static_cast<std::uint8_t>(p[i].y);
    }
}

void World::getNextSquare(Unit& u) {
    const int side = u.side, idx = u.index;
    const int fx = u.blockFromX, fy = u.blockFromY, tx = u.targetX, ty = u.targetY;
    const Passable mode0 = [&](int x, int y) { return canUnitUseSquare(x, y, u, side, 0); };
    if (u.routeRetryDelay != 0) --u.routeRetryDelay;
    if (fx == tx && fy == ty) {
        u.routeSteps = 0;
        u.routeMode = 0;
        return;
    }
    if (u.routeMode != 0 && u.routeMode != 1 && u.state == UnitState::Moving && std::abs(tx - fx) < 3 &&
        std::abs(ty - fy) < 3) {  // stuck near the goal: accept where we are
        u.routeSteps = 0;
        u.routeMode = 0;
        u.targetX = fx;
        u.targetY = fy;
        return;
    }
    int nx = fx, ny = fy;
    // Adopt a fresh route found while blocked (stores from p[0]: p[len-1] is not kept).
    auto adopt = [&](const PathResult& r) {
        u.routeMode = 0;
        u.routeTotalSteps = r.len & 0xFFFF;
        storeRoute(u, r.path, r.len);
    };
    if (u.routeSteps != 0) {
        const int rx = u.routeX[u.routeSteps - 1], ry = u.routeY[u.routeSteps - 1];
        // (DebugFatal if the next route cell is not adjacent)
        if (canUnitUseSquare(rx, ry, u, side, 0)) {
            u.routeRetryDelay = 0;
            --u.routeSteps;
            if (u.routeTotalSteps) --u.routeTotalSteps;
            nx = rx;
            ny = ry;
            if (u.routeBlocked) {
                u.routeBlocked = false;
                if (u.routeMode != 1) u.routeMode = 0;
            }
        } else if (!canUnitUseSquare(rx, ry, u, side, u.routeMode)) {
            // Blocked even under the current relaxed mode.
            bool waited = false;
            if (u.routeMode == 0) {
                PathResult r = pathFinder_.findPath(fx, fy, tx, ty, mode0);
                if (r.status == kPathReached) {
                    pathFinder_.smoothPath(fx, fy, r.path, r.len, mode0);
                    if (r.len < u.routeTotalSteps + 10) adopt(r);
                    else u.routeMode = 1;
                    waited = true;  // next = F: wait one tick
                }
            }
            if (!waited) u.routeSteps = 0;  // drop the route
        } else {
            // Blocked in mode 0 but passable in RouteMode: a moving friendly is in the way.
            if (u.routeRetryDelay == 0 && u.state != UnitState::AttackingTileOnRoute) {
                PathResult r = pathFinder_.findPath(fx, fy, tx, ty, mode0);
                bool found = false;
                if (r.status == kPathReached) {
                    pathFinder_.smoothPath(fx, fy, r.path, r.len, mode0);
                    found = r.status == kPathReached;
                }
                if (found) {
                    if (r.len < u.routeTotalSteps + 10) {
                        adopt(r);
                    } else if (u.routeBlocked) {
                        if (u.routeTotalSteps < 10) u.routeSteps = 0;
                        else u.routeTotalSteps -= 10;
                    }
                } else if (!u.routeBlocked) {
                    u.routeRetryDelay = (idx & 0x3F) + 800;  // first time: queue behind the unit
                } else if (u.routeMode < 2) {
                    ++u.routeMode;
                } else if (u.state == UnitState::MovingToRefinery) {
                    u.routeMode = 0;
                } else {  // give up
                    u.routeSteps = 0;
                    u.routeMode = 0;
                    u.targetX = fx;
                    u.targetY = fy;
                }
            }
            const std::uint32_t c = cell(rx, ry).flags;
            if (u.routeMode != 0 && u.routeMode != 1 && (c & kOccupied)) {
                if (diplomacy(side, static_cast<int>(c & kOwnerMask)) == 1) {  // enemy in the way
                    u.rememberX = u.targetX;
                    u.rememberY = u.targetY;
                    u.oldState = u.state;
                    u.targetX = rx;
                    u.targetY = ry;
                    // (DebugFatal if the unit has no primary weapon; UnitAdjustState refuses then)
                    unitAdjustState(u, UnitState::AttackingTileOnRoute);
                } else {
                    askUnitsOnTileToMoveAway(u, rx, ry);
                }
            }
            if (u.routeRetryDelay == 0) u.routeRetryDelay = (idx & 0xF) + 0x50;
            u.routeBlocked = true;
        }
    } else if (u.routeRetryDelay == 0) {
        // No route stored: search with the current RouteMode.
        const int mode = u.routeMode;
        const Passable passMode = [&, mode](int x, int y) { return canUnitUseSquare(x, y, u, side, mode); };
        PathResult r = pathFinder_.findPath(fx, fy, tx, ty, passMode);
        pathFinder_.smoothPath(fx, fy, r.path, r.len, mode0);
        nx = r.path[0].x;
        ny = r.path[0].y;
        const PathPoint* store = r.path + 1;  // Route[] takes p[1..]
        bool check = false;
        if (r.status == kPathReached) {
            check = r.len != 0;
        } else if (r.len == 0) {  // no progress possible: escalate
            ++u.routeMode;
            u.routeRetryDelay = (idx & 7) + 8;
            nx = fx;
            ny = fy;
            if (u.routeMode > 3 || (u.routeMode == 3 && unitTemplate(u).primaryWeapon == -1)) {
                u.routeSteps = 0;
                u.routeMode = 0;
                u.targetX = fx;
                u.targetY = fy;
                u.routeRetryDelay = (idx & 0xF) + 0x50;
            }
        } else {
            check = true;  // partial path to the closest reachable point
        }
        if (check && !canUnitUseSquare(r.path[0].x, r.path[0].y, u, side, 0)) {
            store = r.path;  // p = p-1: keep p[0] in the route and wait
            ++r.len;
            nx = fx;
            ny = fy;
        }
        u.routeTotalSteps = r.len & 0xFFFF;
        if (r.len != 0) {
            unsigned n = static_cast<unsigned>(r.len - 1);
            if (n > 11) n = 12;
            u.routeSteps = static_cast<int>(n);
            for (unsigned i = 0; i < n; ++i) {
                u.routeX[n - 1 - i] = static_cast<std::uint8_t>(store[i].x);
                u.routeY[n - 1 - i] = static_cast<std::uint8_t>(store[i].y);
            }
        }
    }
    u.blockToX = nx;
    u.blockToY = ny;
}

// ---------------------------------------------------------------------------------------
// MoveUnit 0x494260: one tick of ground movement. Returns false when arrived
// (pos_steps == 0 && BlockFrom == Target), true while moving, turning or waiting.

bool World::moveUnit(Unit& u) {
    if (u.posSteps == 0 && u.blockFromX == u.targetX && u.blockFromY == u.targetY) return false;
    const auto& t = unitTemplate(u);
    const int side = u.side, idx = u.index;
    const bool isInf = t.isInfantry != 0;
    const bool notWorm = t.specialBehavior != behaviour::kSandworm;

    if (u.posSteps == 0) {
        // At a tile centre: choose and reserve the next tile.
        if (u.routeRetryDelay != 0 &&
            (static_cast<std::uint32_t>(idx & 7) == (gameTicks_ & 7) || u.enemyIndex != kNoIndex) &&
            !unitIsHiddenStealth(u) && u.state != UnitState::AttackingTileOnRoute && notWorm)
            unitAutoTarget(u);  // looks for targets while waiting
        if (u.blockFromX == u.blockToX && u.blockFromY == u.blockToY) {
            getNextSquare(u);
            if ((cell(u.blockToX, u.blockToY).flags & kCrate) && notWorm && pickupCrate(u)) {
                u.routeSteps = 0;
                getNextSquare(u);
            } else if (u.state == UnitState::Dead) {
                return true;
            }
            if (u.blockFromX == u.blockToX && u.blockFromY == u.blockToY) return true;  // waiting
            u.flags &= ~kBlockToMarked;
        }
        const int dir = getDir8(u.blockFromX, u.blockFromY, u.blockToX, u.blockToY);
        const int diff = (u.facing - dir + 32) & 31;
        if (diff == 0) u.flags &= ~kTurningInPlace;
        if (!((u.flags & kTurningInPlace) || (diff >= 7 && diff <= 24))) {
            if (!canUnitUseSquare(u.blockToX, u.blockToY, u, side, 0) || (u.flags & kRecheckRoute)) {
                if (!(u.flags & kRecheckRoute)) {
                    u.posSteps = 0;
                    u.routeMode = 1;  // next search treats moving friendlies as passable
                } else {
                    u.flags &= ~kRecheckRoute;
                    bool blocked = false;
                    for (int i = 0; i < u.routeSteps && !blocked; ++i)
                        if (!canUnitUseSquare(u.routeX[i], u.routeY[i], u, side, 0)) blocked = true;
                    u.routeMode = blocked ? 1 : 0;
                    u.posSteps = 0;
                }
                u.routeSteps = 0;  // drop the route; GetNextSquare re-plans next tick
                u.blockToX = u.blockFromX;
                u.blockToY = u.blockFromY;
                return true;
            }
            u.flags |= kBlockToMarked;
            Cell& to = cell(u.blockToX, u.blockToY);
            int spot = 0;
            if (!isInf) {
                u.stepTargetX = u.blockToX * 0x200000 + 0x100000;
                u.stepTargetY = u.blockToY * 0x200000 + 0x100000;
            } else {
                spot = u.cellspotFrom;
                if (to.flags & kOccupied & (0x20u << spot)) {  // same sub-spot taken: TL, DL, DR, TR, MID
                    std::uint32_t bit = 0x200;
                    spot = 4;
                    while (bit >= 0x20 && (bit & to.flags & kOccupied)) {
                        bit >>= 1;
                        --spot;
                    }
                    if (bit < 0x20) spot = 0;  // DebugFatal "Can't find a free slot"; unreachable (mode 0 checked)
                }
                u.stepTargetX = (kSpotOffset[spot][0] + u.blockToX * 32) << 16;
                u.stepTargetY = (kSpotOffset[spot][1] + u.blockToY * 32) << 16;
                u.cellspotTo = spot;
            }
            // Speed for the whole tile step; steps from the Chebyshev distance (no snapping).
            std::int32_t sp = stepSpeed(u);
            if (sp <= 0) sp = 1;  // the original DebugFatals on zero speed
            u.speed = static_cast<std::uint32_t>(sp);
            const std::uint32_t ddx = static_cast<std::uint32_t>(std::abs(u.posX - u.stepTargetX));
            const std::uint32_t ddy = static_cast<std::uint32_t>(std::abs(u.posY - u.stepTargetY));
            const std::uint32_t dist = static_cast<std::int32_t>(ddy) < static_cast<std::int32_t>(ddx) ? ddx : ddy;
            const std::uint32_t steps = dist / u.speed;  // (DebugFatal if > 255)
            u.posSteps = static_cast<std::uint8_t>(steps);
            u.posStepsMax = static_cast<std::uint8_t>(u.posSteps >> 1);
            // Reserve BlockTo.
            if (!isInf && notWorm) {
                to.flags = (to.flags & ~kOwnerMask) | static_cast<std::uint32_t>(side) | kVehicle;
            } else if (!isInf) {
                to.flags |= kWorm;
            } else {
                to.flags = (to.flags & ~kOwnerMask) | (0x20u << spot) | static_cast<std::uint32_t>(side);
                to.flags = (to.flags & 0xF1FFFFFFu) | (static_cast<std::uint32_t>(side) << kInfantryOwnerShift);
            }
        }
    }

    // STEP
    const bool atRest = u.blockFromX == u.blockToX && u.blockFromY == u.blockToY;
    const int dir = atRest ? u.facing : getDir8(u.blockFromX, u.blockFromY, u.blockToX, u.blockToY);
    const int diff = (u.facing - dir + 32) & 31;
    if (diff == 0) u.flags &= ~kTurningInPlace;
    const bool canMove = !(u.flags & kTurningInPlace) && (diff < 7 || diff > 24);
    if (!canMove) {
        if (u.flags & kStateChanged) u.flags = (u.flags & 0x3FFFFFFFu) | kRecheckRoute;
        u.flags |= kTurningInPlace;
    }
    rotateBody(u, dir);
    if (t.primaryWeaponUseBarrel && (u.blockFromX != u.targetX || u.blockFromY != u.targetY))
        rotateTurret(u, getDir8(u.blockFromX, u.blockFromY, u.targetX, u.targetY));  // at the final target
    if (canMove) {
        u.flags &= 0x3FFFFFFFu;
        if (isInf) setInfantryAnim(u, 1);  // walk
        const std::uint32_t s = u.speed, h = s >> 1;
        const int x = u.posX >> 16;
        if (x < static_cast<int>((static_cast<std::uint32_t>(u.stepTargetX) - h) >> 16)) u.posX += static_cast<std::int32_t>(s);
        else if (static_cast<int>((h + static_cast<std::uint32_t>(u.stepTargetX)) >> 16) < x) u.posX -= static_cast<std::int32_t>(s);
        const int y = u.posY >> 16;
        if (y < static_cast<int>((static_cast<std::uint32_t>(u.stepTargetY) - h) >> 16)) u.posY += static_cast<std::int32_t>(s);
        else if (static_cast<int>((h + static_cast<std::uint32_t>(u.stepTargetY)) >> 16) < y) u.posY -= static_cast<std::int32_t>(s);
        u.posSteps = static_cast<std::uint8_t>(u.posSteps - 1);
        if (u.posSteps < u.posStepsMax) {
            // Halfway: hand the unit over to the new cell.
            u.posStepsMax = 0;
            std::uint32_t& from = cell(u.blockFromX, u.blockFromY).flags;
            if (!isInf) {
                if (notWorm) {
                    from &= ~kVehicle;
                    if (from & kInfantryMask) from = (from & ~kOwnerMask) | ((from >> kInfantryOwnerShift) & 7);
                } else {
                    from &= ~kWorm;
                }
            } else {
                from &= ~(0x20u << u.cellspotFrom);  // owner bits left stale (faithful)
                u.cellspotFrom = u.cellspotTo;
            }
            u.blockFromX = u.blockToX;
            u.blockFromY = u.blockToY;
            u.flags &= ~kBlockToMarked;
        }
    }
    return true;
}

}  // namespace d2k::game

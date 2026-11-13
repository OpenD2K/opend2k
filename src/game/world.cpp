// Object pools, the model tick and the UpdateUnit state machine.
// References: ModelUpdate 0x458e90, UpdateUnit 0x497b90, UnitAdjustState 0x49deb0,
// ProcessOrder 0x457810, CSide pool 0x46b990/0x46bd40/0x46bd60/0x46bdb0.
// Combat lives in combat.cpp / projectiles.cpp / buildings.cpp, harvesting/carryalls/credits in
// economy.cpp (also the starport, re/notes/starport.md), production in production.cpp, the
// computer player in ai.cpp. Palace weapons and the ornithopter air strike are at the end.

#include "game/world.hpp"

#include "game/geometry.hpp"
#include "game/tables.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace d2k::game {

using namespace cell_flags;
using namespace unit_flags;

namespace {
// CSide__FreeObject 0x46bdb0 zeroes the first 0x7C bytes of the (shared) slot. Our object kinds
// live in separate arrays, so every kind's view of the slot is zeroed; the pool index and side
// (not part of those bytes) are kept.
template <class T>
void zeroWipe(T& v, int index, int side) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::memset(static_cast<void*>(&v), 0, sizeof(T));
    v.index = index;
    v.side = side;
}

bool equalsNoCaseAscii(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if ((*a | 0x20) != (*b | 0x20)) return false;
    return *a == *b;
}
}  // namespace

// ---------------------------------------------------------------------------------------
// CSide object pool

void Side::initPool() {
    units.assign(kObjectsPerSide, Unit{});
    buildings.assign(kObjectsPerSide, Building{});
    bullets.assign(kObjectsPerSide, Bullet{});
    explosions.assign(kObjectsPerSide, Explosion{});
    links.fill(ObjectLink{});
    firstUnit = lastUnit = firstBuilding = lastBuilding = -1;
    firstBullet = lastBullet = firstExplosion = lastExplosion = -1;
    // CSide::Init chains slots 0..999 and sets the free-list top to slot 999; allocation
    // follows `prev`, so the first object gets index 999, then 998, ...
    freeList.resize(kObjectsPerSide);
    for (int i = 0; i < kObjectsPerSide; ++i) freeList[i] = i;
    for (int i = 0; i < kObjectsPerSide; ++i) {
        units[i].index = i;
        buildings[i].index = i;
        bullets[i].index = i;
        explosions[i].index = i;
    }
}

int Side::allocObject() {
    if (freeList.empty()) return -1;
    const int i = freeList.back();
    freeList.pop_back();
    links[i] = ObjectLink{};
    return i;
}

// LinkObject: append to the kind's list (insertion order = update order).
void Side::linkObject(int index, SlotKind kind) {
    int* first = nullptr;
    int* last = nullptr;
    switch (kind) {
    case SlotKind::Unit: first = &firstUnit; last = &lastUnit; break;
    case SlotKind::Building: first = &firstBuilding; last = &lastBuilding; break;
    case SlotKind::Bullet: first = &firstBullet; last = &lastBullet; break;
    case SlotKind::Explosion: first = &firstExplosion; last = &lastExplosion; break;
    default: return;
    }
    auto& l = links[index];
    l.next = -1;
    l.prev = *last;
    l.kind = kind;
    if (*last == -1) *first = index;
    else links[*last].next = index;
    *last = index;
}

// FreeObject: unlink from its kind's list and push the slot on the free-list top (LIFO).
void Side::freeObject(int index) {
    auto& l = links[index];
    int* first = nullptr;
    int* last = nullptr;
    switch (l.kind) {
    case SlotKind::Unit: first = &firstUnit; last = &lastUnit; break;
    case SlotKind::Building: first = &firstBuilding; last = &lastBuilding; break;
    case SlotKind::Bullet: first = &firstBullet; last = &lastBullet; break;
    case SlotKind::Explosion: first = &firstExplosion; last = &lastExplosion; break;
    default: return;
    }
    if (l.prev == -1) *first = l.next;
    else links[l.prev].next = l.next;
    if (l.next == -1) *last = l.prev;
    else links[l.next].prev = l.prev;
    l = ObjectLink{};
    // Zero wipe (alive = false; a homing bullet then reads PosX/PosY/state 0 like the original).
    zeroWipe(units[index], index, id);
    zeroWipe(buildings[index], index, id);
    zeroWipe(bullets[index], index, id);
    zeroWipe(explosions[index], index, id);
    freeList.push_back(index);
}

// ---------------------------------------------------------------------------------------
// Construction

void World::initSides(const formats::MisFile* mission) {
    for (int s = 0; s < kMaxSides; ++s) {
        auto& side = sides_[s];
        side.id = s;
        side.initPool();
        side.primaryBuilding.fill(kNoIndex);
        side.starportTypes.fill(-1);
        for (auto& d : side.deliveries) d.units.fill(0xff);  // CSide::Init
        side.powerPercentPrev = 0xffff;  // UNCERTAIN: see updatePower
        if (mission) {
            side.house = mission->houseId[s];
            side.startingCredits = mission->startingMoney[s];
            side.cash = static_cast<std::int32_t>(mission->startingMoney[s]);  // CSide::InitCash
            side.techLevel = mission->techLevel[s];
            for (int o = 0; o < kMaxSides; ++o) diplomacy_[s][o] = mission->allegiance[s][o];
        } else {
            side.house = s;
            for (int o = 0; o < kMaxSides; ++o) diplomacy_[s][o] = s == o ? 0 : 1;  // default: 1 off-diagonal
        }
    }
}

World::World(int width, int height, Rules rules, int mySide)
    : width_(width), height_(height), rules_(rules), mySide_(mySide) {
    cells_.resize(static_cast<std::size_t>(width_) * height_);
    initSides(nullptr);
    aiInit(nullptr);
    initEconomy(false);
}

World::World(const formats::MapFile& map, const formats::MisFile& mission, const formats::TileAtr& tileAtr,
             const formats::TileData& tileData, Rules rules, int mySide, int scriptSide)
    : width_(map.width), height_(map.height), rules_(rules), mySide_(mySide & 7) {
    cells_.resize(static_cast<std::size_t>(width_) * height_);
    initSides(&mission);
    aiInit(&mission);             // InitAI per side, then Mission__LoadObjectives copies the AI segments
    if (scriptSide >= 0 && (scriptSide & 7) != mySide_) {  // play as another side (not original)
        scriptSide_ = scriptSide & 7;
        setupPlayAs(mission);
    }
    loadMissionEvents(mission);   // Mission__LoadObjectives: events, conditions, timer
    spawnMapObjects(map, tileAtr, tileData);
    if (ext_.active) extInitMap(map, tileAtr);  // community patch: backup tiles (setupmapstuff.c)
    initEconomy(true);
    gameTicks_ = 0;  // Game_Init 0x44861e
}

// Skirmish Game_Init (GameType 1, not networked): as the campaign path, with the lobby values
// that Mission__LoadObjectives applies after reading _SPAWN.MIS (tech level for every side,
// starting credits gNetStartingCredits for the player and 7000 / 10500 / 14000 by difficulty
// for the other sides, the player's house) and the skirmish pass of Setup_SpawnMapObjects.
World::World(const formats::MapFile& map, const formats::MisFile& mission, const formats::TileAtr& tileAtr,
             const formats::TileData& tileData, Rules rules, const SkirmishSetup& setup)
    : width_(map.width), height_(map.height), rules_(rules), mySide_(0) {
    skirmish = true;
    difficulty = setup.difficulty;
    // Game__MainLoopFrame: srand, then the colour loop (draws rand), then Game_Init.
    rng_.reseed(setup.seed);
    assignSkirmishColours(setup.playerColor);
    if (setup.crates) {  // CratesPerPlayer: only with more than one network player
        const formats::Vars& v = vars();
        numCratesAllowed_ = v.numberOfCrates;
    }
    cells_.resize(static_cast<std::size_t>(width_) * height_);
    initSides(&mission);
    for (int s = 0; s < kMaxSides; ++s) {
        Side& side = sides_[s];
        side.techLevel = setup.techLevel & 0xFF;
        std::uint32_t credits = setup.startingCredits;
        if (s != mySide_) credits = difficulty == 1 ? 0x2904u : difficulty == 2 ? 14000u : 7000u;
        side.startingCredits = credits;
        side.cash = static_cast<std::int32_t>(credits);
    }
    sides_[mySide_].house = setup.playerHouse;  // UNCERTAIN: other non-AI sides keep the .MIS house
    aiInit(&mission);
    timerValue_ = mission.timeLimit;  // (MP maps have no events)
    spawnMapObjects(map, tileAtr, tileData, &setup);
    initEconomy(true);
    gameTicks_ = 0;
}

// Game_Init 0x448550 after Setup_SpawnMapObjects: per side the sidebar icons, CSide::InitCash
// (credits from the .MIS, already set by initSides) and empty build queues; then the starport
// lists of all 8 sides (FUN_0046e7a0, campaign/new game only).
void World::initEconomy(bool mission) {
    for (int s = 0; s < kMaxSides; ++s) {
        Side& S = sides_[s];
        updateIcons(s);
        S.prevUnitIcons = S.unitIcons.size();
        S.prevBuildingIcons = S.buildingIcons.size();
        S.buildingQueue = ProductionSlot{};
        for (auto& q : S.unitQueue) q = ProductionSlot{};
        S.upgradeQueue = ProductionSlot{};
    }
    // ActivateAI for every enabled computer player (uses the RNG), then the initial reveal
    // around the local player's objects, then the starport lists.
    for (int s = 0; s < kMaxSides; ++s)
        if (ai_[s].enabled()) aiActivate(s);
    if (mission) revealTilesSeenByBuildingsAndUnits(mySide_);
    if (mission)
        for (auto& S : sides_) initStarport(S);
    evaluateAllExists();  // FUN_0044f770
}

Unit* World::unit(int side, int index) {
    if (side < 0 || side >= kMaxSides || index < 0 || index >= kObjectsPerSide) return nullptr;
    auto& s = sides_[side];
    return s.links[index].kind == SlotKind::Unit ? &s.units[index] : nullptr;
}

Building* World::building(int side, int index) {
    if (side < 0 || side >= kMaxSides || index < 0 || index >= kObjectsPerSide) return nullptr;
    auto& s = sides_[side];
    return s.links[index].kind == SlotKind::Building ? &s.buildings[index] : nullptr;
}

// ---------------------------------------------------------------------------------------
// Orders

void World::orderMove(int side, int unitIndex, int tileX, int tileY) {
    if (!inBounds(tileX, tileY)) return;
    orders_.push_back({Order::Move, side, unitIndex, tileX, tileY});
}

void World::orderStop(int side, int unitIndex) { orders_.push_back({Order::Stop, side, unitIndex, 0, 0}); }

void World::orderAttackUnit(int side, int unitIndex, int targetSide, int targetIndex) {
    orders_.push_back({Order::AttackUnit, side, unitIndex, 0, 0, targetSide, targetIndex});
}

void World::orderAttackBuilding(int side, int unitIndex, int targetSide, int targetIndex) {
    orders_.push_back({Order::AttackBuilding, side, unitIndex, 0, 0, targetSide, targetIndex});
}

void World::orderAttackTile(int side, int unitIndex, int tileX, int tileY) {
    if (!inBounds(tileX, tileY)) return;
    orders_.push_back({Order::AttackTile, side, unitIndex, tileX, tileY});
}

const Unit* World::unitAt(int pixelX, int pixelY, int& sideOut, int& indexOut) const {
    return getUnitOnTile(pixelX, pixelY, sideOut, indexOut, false);
}

// GetBuildingOnTile_0 0x4a5640: the owner's (cell flags & 7) first non-concrete building
// whose solid footprint covers the tile.
const Building* World::buildingAt(int tileX, int tileY, int& sideOut, int& indexOut) const {
    indexOut = kNoIndex;
    if (!inBounds(tileX, tileY)) return nullptr;
    const std::uint32_t f = cell(tileX, tileY).flags;
    if (!(f & kBuilding)) return nullptr;
    const int owner = static_cast<int>(f & kOwnerMask);
    const Side& s = sides_[owner];
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
        const Building& b = s.buildings[i];
        if (buildingTemplate(b).specialBehavior == building_behaviour::kConcrete) continue;
        if (!buildingOccupiesTile(b, tileX, tileY)) continue;
        sideOut = owner;
        indexOut = i;
        return &b;
    }
    return nullptr;
}

void World::decayFlicker() {
    for (auto& s : sides_) {
        for (int i = s.firstUnit; i != -1; i = s.nextObject(i))
            if (s.units[i].flicker) --s.units[i].flicker;
        for (int i = s.firstBuilding; i != -1; i = s.nextObject(i))
            if (s.buildings[i].flicker) --s.buildings[i].flicker;
    }
}

void World::orderBuildingAttackUnit(int side, int buildingIndex, int targetSide, int targetIndex) {
    orders_.push_back({Order::BuildingAttackUnit, side, buildingIndex, 0, 0, targetSide, targetIndex});
}

void World::orderBuildingAttackBuilding(int side, int buildingIndex, int targetSide, int targetIndex) {
    orders_.push_back({Order::BuildingAttackBuilding, side, buildingIndex, 0, 0, targetSide, targetIndex});
}

// GetSelectedBuildingWithBarrel 0x4a5860.
const Building* World::selectedBarrelBuilding(int side) const {
    if (side < 0 || side >= kMaxSides) return nullptr;
    const Side& s = sides_[side];
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i))
        if (s.buildings[i].selected && buildingTemplate(s.buildings[i]).barrelArt != -1) return &s.buildings[i];
    return nullptr;
}

// The order part of the battlefield click handler 0x4433a0 (mode 0) for `side`; see world.hpp.
SmartOrder World::smartOrder(int side, int px, int py, bool forceAttack, bool forceMove) {
    const int tx = px >> 5, ty = py >> 5;
    if (!inBounds(tx, ty) || side < 0 || side >= kMaxSides) return SmartOrder::None;
    Side& s = sides_[side];
    // The Generate*Order functions collect at most 64 selected units, in list order.
    auto forSelected = [&](auto&& filter, auto&& issue) {
        int n = 0;
        for (int i = s.firstUnit; i != -1 && n < 64; i = s.nextObject(i)) {
            const Unit& u = s.units[i];
            if (!u.selected || !filter(u)) continue;
            issue(i);
            ++n;
        }
        return n;
    };
    // GenerateBuildingAttack{Unit,Building}Order: selected own barrel buildings not dying (max 64).
    auto forSelectedBarrels = [&](auto&& issue) {
        int n = 0;
        for (int i = s.firstBuilding; i != -1 && n < 64; i = s.nextObject(i)) {
            const Building& b = s.buildings[i];
            if (!b.selected || b.state == 17 || buildingTemplate(b).barrelArt == -1) continue;
            issue(i);
            ++n;
        }
        return n;
    };
    // PlayUnitResponse 0x49f240: the selected unit type with the highest voice priority.
    auto respond = [&](int kind) {
        std::uint32_t best = 0;
        unitResponseType_ = -1;
        for (int i = s.firstUnit; i != -1; i = s.nextObject(i))
            if (s.units[i].selected && best <= unitTemplate(s.units[i]).voicePriority) {
                best = unitTemplate(s.units[i]).voicePriority;
                unitResponseType_ = s.units[i].type;
            }
        unitResponseKind_ = kind;
    };
    auto anySelected = [&](auto&& pred) {
        for (int i = s.firstUnit; i != -1; i = s.nextObject(i))
            if (s.units[i].selected && pred(s.units[i])) return true;
        return false;
    };
    const bool anyUnits = anySelected([](const Unit&) { return true; });  // IsAnyUnitSelected 0x4a57c0
    auto isArmed = [&](const Unit& u) { return unitTemplate(u).primaryWeapon != -1; };
    auto armed = [&](const Unit& u) { return u.state != UnitState::Dead && isArmed(u); };
    auto alive = [](const Unit& u) { return u.state != UnitState::Dead; };
    const std::uint32_t cf = cell(tx, ty).flags;

    // Unit under the cursor: skip cloaked ones when another side's units are on the cell; a
    // cloaked non-allied unit counts as no unit.
    const bool skipCloaked = static_cast<int>(cf & kOwnerMask) != side && (cf & kAnyUnit) != 0;
    int us = 0, ui = 0;
    Unit* target = getUnitOnTile(px, py, us, ui, skipCloaked);
    if (target && ((target->flags & kExcluded) || ((target->flags & kCloaked) && diplomacy(side, us) != 0)))
        target = nullptr;
    if (target) {
        if ((us == side || diplomacy(side, us) == 0) && !forceAttack) return SmartOrder::Friendly;
        if (anyUnits) {
            target->flicker = 7;  // SetUnitToFlicker 0x49f310
            respond(1);
            // Alt on another side's infantry with a crusher selected and a drivable cell:
            // GenerateUnitMoveOrder to the cell instead of an attack.
            if (forceMove && unitTemplate(*target).isInfantry && us != side &&
                anySelected([&](const Unit& u) { return unitTemplate(u).canCrushInfantry != 0; }) && (cf & kDriveOn)) {
                forSelected(alive, [&](int i) { orderMove(side, i, tx, ty); });
                return SmartOrder::Move;
            }
            forSelected(armed, [&](int i) { orderAttackUnit(side, i, us, ui); });  // 0x4596f0
            return SmartOrder::AttackUnit;
        }
        const Building* turret = selectedBarrelBuilding(side);
        if (!turret || !canBuildingAttackTile(*turret, tx, ty, false)) return SmartOrder::None;
        target->flicker = 7;
        forSelectedBarrels([&](int i) { orderBuildingAttackUnit(side, i, us, ui); });  // 0x459d20
        return SmartOrder::TurretAttackUnit;
    }
    int bs = 0, bi = 0;
    if (const Building* cb = buildingAt(tx, ty, bs, bi)) {
        Building& b = sides_[bs].buildings[bi];
        if ((bs == side || diplomacy(side, bs) == 0) && !forceAttack) return SmartOrder::Friendly;
        if (const Building* turret = selectedBarrelBuilding(side)) {  // checked before the units
            if (!buildingInRangeOf(*turret, *cb)) return SmartOrder::None;
            b.flicker = 7;  // SetBuildingToFlicker 0x49f320
            forSelectedBarrels([&](int i) { orderBuildingAttackBuilding(side, i, bs, bi); });  // 0x459de0
            return SmartOrder::TurretAttackBuilding;
        }
        if (!anyUnits) return SmartOrder::None;
        if (bs == side && !anySelected(isArmed)) return SmartOrder::None;
        auto allHave = [&](int beh) {  // AllSelectedUnitsHaveBehavior
            bool any = false;
            for (int i = s.firstUnit; i != -1; i = s.nextObject(i))
                if (s.units[i].selected) {
                    any = true;
                    if (unitTemplate(s.units[i]).specialBehavior != beh) return false;
                }
            return any;
        };
        if (allHave(behaviour::kSaboteur) && !(b.flags & 0x200)) return SmartOrder::None;
        if (allHave(behaviour::kEngineer) && !(b.flags & 0x400)) return SmartOrder::None;
        // GenerateUnitAttackBuildingOrder 0x459800.
        auto canAttack = [&](const Unit& u) {
            const int beh = unitTemplate(u).specialBehavior;
            if (u.state == UnitState::Dead || beh == behaviour::kHarvester || beh == behaviour::kMcv ||
                beh == behaviour::kThumper)
                return false;
            return side != bs || (beh != behaviour::kEngineer && beh != behaviour::kSaboteur);
        };
        b.flicker = 7;
        forSelected(canAttack, [&](int i) { orderAttackBuilding(side, i, bs, bi); });
        respond(1);
        return SmartOrder::AttackBuilding;
    }
    if (!forceAttack) {  // GenerateUnitMoveOrder 0x459450
        if (!anyUnits) return SmartOrder::None;
        forSelected(alive, [&](int i) { orderMove(side, i, tx, ty); });
        respond(1);
        return SmartOrder::Move;
    }
    // GenerateUnitAttackTileOrder 0x4598d0 (after IsAnyArmedUnitSelected); no building version.
    if (!anySelected(isArmed)) return SmartOrder::None;
    forSelected(armed, [&](int i) { orderAttackTile(side, i, tx, ty); });
    respond(1);
    return SmartOrder::AttackTile;
}

// ProcessOrder cases 0xb / 0xc: the target must still be a unit (not dead/carried/flying) /
// a building; the ordering building needs a barrel, must not be the target and not dying
// (Building__AdjustState 0x49e120 refuses state 17).
void World::processBuildingAttackOrder(const Order& o) {
    Building* b = building(o.side, o.unitIndex);
    if (!b || buildingTemplate(*b).barrelArt == -1 || b->state == 17) return;
    if (o.side == o.targetSide && o.unitIndex == o.targetIndex) return;
    if (o.kind == Order::BuildingAttackUnit) {
        const Unit* t = unit(o.targetSide, o.targetIndex);
        if (!t || (t->flags & kExcluded)) return;
        b->state = 4;
    } else {
        if (!building(o.targetSide, o.targetIndex)) return;
        b->state = 5;
    }
    b->enemySide = o.targetSide;
    b->enemyIndex = o.targetIndex;
}

void World::processOrder(const Order& o) {
    if (o.side < 0 || o.side >= kMaxSides) return;
    switch (o.kind) {  // economy / production orders (production.cpp)
    case Order::SetPrimary: setBuildingAsPrimary(o.side, o.unitIndex); return;  // case 0xd
    case Order::RepairBuilding:  // FUN_00456fb0
        if (Building* b = building(o.side, o.unitIndex)) b->flags ^= building_flags::kRepairing;
        return;
    case Order::SellBuilding: sellBuilding(o.side, o.unitIndex); return;
    case Order::BuildingPick: buildingPick(o.side, o.x); return;
    case Order::BuildingCancel: buildingCancel(o.side, o.x); return;
    case Order::PlaceBuilding: placeBuilding(o.side, o.targetIndex, o.x, o.y); return;
    case Order::UnitPick: unitPick(o.side, o.x); return;
    case Order::UnitCancel: unitCancel(o.side, o.x); return;
    case Order::AutoAdd: autoAdd(o.side, o.x, o.y); return;
    case Order::AutoInfinite: autoInfinite(o.side, o.x); return;
    case Order::AutoRemove: autoRemove(o.side, o.x); return;
    case Order::UpgradePick: upgradePick(o.side, o.x); return;
    case Order::UpgradeCancel: upgradeCancel(o.side, o.x); return;
    case Order::StarportPick: starportPick(o.side, o.x); return;      // case 0x16
    case Order::StarportUnpick: starportUnpick(o.side, o.x); return;  // case 0x17
    case Order::StarportPurchase: starportPurchase(o.side); return;   // case 0x18
    case Order::StarportCancel: starportCancel(o.side); return;       // case 0x19
    case Order::SetRallyPoint: setRallyPoint(o.side, o.unitIndex, o.x, o.y); return;  // not original
    case Order::BuildingAttackUnit:      // case 0xb (order 0x1e)
    case Order::BuildingAttackBuilding:  // case 0xc (order 0x1f)
        processBuildingAttackOrder(o);
        return;
    default: break;
    }
    if (o.kind >= Order::CheatSetCredits) {  // not original (cheats.cpp)
        processCheatOrder(o);
        return;
    }
    if (o.kind == Order::SpecialWeapon) {  // case 0x1d (0x30)
        specialWeapon(o.side, o.targetIndex, o.x, o.y);
        return;
    }
    Unit* u = unit(o.side, o.unitIndex);
    if (!u) return;
    if (o.kind == Order::DockRefinery) {  // case 4 (0x17): harvesters go to the given refinery
        if (unitCanTakeOrders(u, true) && unitTemplate(*u).specialBehavior == behaviour::kHarvester &&
            unitAdjustState(*u, UnitState::MovingToRefinery)) {
            u->oldState = UnitState::None;
            u->refineryIndex = o.targetIndex;
        }
        return;
    }
    if (o.kind == Order::RepairUnit) {  // case 0xe (0x21): vehicles to a repair pad
        if (unitCanTakeOrders(u, false) && !unitTemplate(*u).isInfantry &&
            unitAdjustState(*u, UnitState::MovingToRepairPad)) {
            u->oldState = UnitState::None;
            u->repairPadIndex = o.targetIndex;
        }
        return;
    }
    if (o.kind == Order::Guard) {  // case 5 (0x18): guard where it stands
        if (unitCanTakeOrders(u, true) && unitAdjustState(*u, UnitState::Guarding)) {
            u->oldState = UnitState::None;
            u->rememberX = u->blockToX;
            u->rememberY = u->blockToY;
            u->enemyIndex = kNoIndex;
        }
        return;
    }
    if (o.kind == Order::Retreat) {  // case 7 (0x1a): harvesters home to a refinery, others to (x, y)
        if (!unitCanTakeOrders(u, true)) return;
        const auto& T = rules_.templates->bin;
        const int refineryGroup = T.groupIds[group_ids::kRefinery];
        if (unitTemplate(*u).specialBehavior == behaviour::kHarvester && refineryGroup >= 0 &&
            sides_[o.side].buildingsExistPerGroup[refineryGroup & 0xff] != 0) {
            unitAdjustState(*u, UnitState::MovingToRefinery);
        } else if (unitAdjustState(*u, UnitState::Moving)) {
            u->targetX = o.x;
            u->targetY = o.y;
        }
        return;
    }
    if (o.kind == Order::Scatter) {  // case 6 (0x19): ExecScatterOrder 0x457580
        if (!unitCanTakeOrders(u, true)) return;
        const int st = static_cast<int>(u->state);
        if (st == 0x19 || st == 0x1a || st == 8 || st == 9 || st == 0x14 || st == 0x13 || st == 0x17) return;
        const int fx = u->blockFromX & 0xff, fy = u->blockFromY & 0xff;
        int cx = fx, cy = fy;
        bool found = false;
        for (int k = 0; k < 8 && !found; ++k) {
            const int dir = (rng_.next() & 7) << 2;
            cx = fx;
            cy = fy;
            stepInDirection(cx, cy, dir);
            found = canUnitUseSquare(cx, cy, *u, o.side, 0);
        }
        if (found || findFreeAdjacentSquare(fx, fy, *u, o.side, cx, cy, 0)) {
            if (!unitAdjustState(*u, UnitState::Moving)) return;
        } else {
            if (!findFreeAdjacentSquare(fx, fy, *u, o.side, cx, cy, 2)) return;
            if (!unitAdjustState(*u, UnitState::MovingAway)) return;
            u->oldState = UnitState::Moving;
        }
        u->targetX = cx & 0xff;
        u->targetY = cy & 0xff;
        return;
    }
    if (o.kind == Order::Deploy) {  // case 0x1c (0x2f)
        if (!unitCanTakeOrders(u, true)) return;
        const int beh = unitTemplate(*u).specialBehavior;
        if (beh == behaviour::kMcv) {
            if (unitAdjustState(*u, UnitState::Deploying)) u->oldState = UnitState::None;
        } else if (beh == behaviour::kDevastator) {
            if (unitAdjustState(*u, UnitState::Selfdestruct)) {
                u->oldState = UnitState::None;
                u->byte29 = vars().devastatorExplodeDelay;
            }
        } else if (beh == behaviour::kThumper) {
            if (unitAdjustState(*u, UnitState::Thumpering)) u->oldState = UnitState::None;
        } else if (beh == behaviour::kSaboteur && !(u->flags & kStealth)) {
            u->flags |= kStealth;
            playNamed("S_STEALTHUP", 0, u->blockToX, u->blockToY);
        }
        return;
    }
    if (o.kind == Order::AttackUnit) {
        // ProcessOrder case 0x14: the target must be a unit that is not dead/carried/flying-excluded.
        const Unit* t = unit(o.targetSide, o.targetIndex);
        if (!t || (t->flags & kExcluded)) return;
        if (!unitCanTakeOrders(u, true) || unitTemplate(*u).primaryWeapon == -1) return;
        if (o.side == o.targetSide && o.unitIndex == o.targetIndex) return;
        if (unitAdjustState(*u, UnitState::AttackingUnit)) {
            u->oldState = UnitState::None;
            u->enemySide = o.targetSide;
            u->enemyIndex = o.targetIndex;
        }
        return;
    }
    if (o.kind == Order::AttackBuilding) {  // case 0x15
        if (!building(o.targetSide, o.targetIndex) || !unitCanTakeOrders(u, true)) return;
        if (unitAdjustState(*u, UnitState::AttackingBuilding)) {
            u->oldState = UnitState::None;
            u->enemySide = o.targetSide;
            u->enemyIndex = o.targetIndex;
        }
        return;
    }
    if (o.kind == Order::AttackTile) {  // case 0x16
        if (!unitCanTakeOrders(u, true) || unitTemplate(*u).primaryWeapon == -1) return;
        if (unitAdjustState(*u, UnitState::AttackingTile)) {
            u->oldState = UnitState::None;
            u->targetX = o.x;
            u->targetY = o.y;
        }
        return;
    }
    if (o.kind == Order::Move) {
        // ProcessOrder case 0x13 (move): every listed unit gets the same target tile.
        if (!unitCanTakeOrders(u, true)) return;
        const auto& t = unitTemplate(*u);
        if (t.specialBehavior == behaviour::kHarvester && (cell(o.x, o.y).flags & kSpiceMask)) {
            if (unitAdjustState(*u, UnitState::MovingToHarvest)) {
                u->oldState = UnitState::None;
                u->targetX = u->rememberX = o.x;
                u->targetY = u->rememberY = o.y;
            }
        } else if (unitAdjustState(*u, UnitState::Moving)) {
            u->oldState = UnitState::None;
            u->targetX = o.x;
            u->targetY = o.y;
        }
        // BlockFrom/BlockTo/pos_steps are untouched: a unit in mid-step finishes the step.
    } else {
        // ExecStopOrder 0x4576b0: finish the current step and stop.
        switch (u->state) {
        case UnitState::Repairing: case UnitState::LeavingRepairPad: case UnitState::LeavingBuilding:
        case UnitState::MovingAway: case UnitState::Invalid: case UnitState::Airlifted:
        case UnitState::Infiltrating: return;
        default: break;
        }
        unitAdjustState(*u, UnitState::Moving);
        u->oldState = UnitState::None;
        u->targetX = u->blockToX;
        u->targetY = u->blockToY;
    }
}

// ---------------------------------------------------------------------------------------
// Tick

void World::tick() {
    ++gameTicks_;  // main loop 0x449a5e: before the model update
    sounds_ = std::move(uiSounds_);  // speech of UI clicks since the last tick
    uiSounds_.clear();
    std::erase_if(shots_, [&](const ShotEvent& e) { return e.tick + std::uint32_t(e.delay) + kShotEventTicks < gameTicks_; });
    if (timerValue_ != -1) --timerValue_;  // _gTimerValue
    for (int s = 0; s < kMaxSides; ++s)    // DoAI FUN_0041b610 for every enabled CAI
        if (ai_[s].enabled()) aiUpdate(s);
    checkEvents();                  // Mission__CheckEvents 0x453b90
    uncloakRemainingStealthUnit();  // 0x455480
    updateCrates();            // FUN_0044ea70 (crates / spice blooms)
    autoPickSpecialWeapons();  // FUN_004466e0
    updateScreenShake();       // FUN_00446690
    // ModelTick 0x44aca0: orders (FUN_0045d230), ModelUpdate, building repair, power, credits.
    // UNCERTAIN: the original queues orders with a tick stamp (Orderdata_add); we run the
    // queued orders right before ModelUpdate.
    const std::vector<Order> orders = std::move(orders_);
    orders_.clear();
    for (const auto& o : orders) processOrder(o);
    updateCheats();  // not original: free cheat deliveries waiting for a delivery slot
    updateRally();   // not original: units that left a factory with a rally point
    modelUpdate();
    updateBuildingRepair();  // FUN_0044a9f0
    updatePower();           // FUN_0044aae0
    updateStorage();         // FUN_0044abe0
    revealUnitsSlice();      // FUN_0044cff0
    updateProduction();      // FUN_004467e0
    Side& s = sides_[gameTicks_ & 7];
    if (s.participates) {
        updatePickupQueue(s);  // FUN_0046d180
        updateStarport(s);     // FUN_0046ddc0
        updateDeliveries(s);   // FUN_0046def0
    }
    setDeadOrder();   // Map__SetDeadOrder 0x44c520
    updateGameEnd();  // win/lose block
    updateParticles();  // FUN_0045d4f0 (main loop 0x44a057, after the model tick)
}

// ModelUpdate 0x458e90: per side 0..7, units, buildings, bullets, explosions, each in list
// order; an object whose update returns 0 is freed. The next link is read before updating.
void World::modelUpdate() {
    for (auto& side : sides_) {
        for (int i = side.firstUnit; i != -1;) {
            const int next = side.nextObject(i);
            Unit& u = side.units[i];
            if (!updateUnit(u)) freeUnit(side, u);
            i = next;
        }
        for (int i = side.firstBuilding; i != -1;) {
            const int next = side.nextObject(i);
            Building& b = side.buildings[i];
            if (!updateBuilding(b)) freeBuilding(side, b);
            i = next;
        }
        for (int i = side.firstBullet; i != -1;) {
            const int next = side.nextObject(i);
            if (!updateBullet(side.bullets[i])) side.freeObject(i);
            i = next;
        }
        for (int i = side.firstExplosion; i != -1;) {
            const int next = side.nextObject(i);
            if (!updateExplosion(side.explosions[i])) side.freeObject(i);
            i = next;
        }
    }
}

// ModelUpdate: a sold building (flag 0x20000) refunds half its cost (a refinery: half of its
// cost minus a harvester's), then the slot is freed and the sidebar icons are updated.
void World::freeBuilding(Side& side, Building& b) {
    if (b.flags & building_flags::kWasSold) {
        std::uint32_t v = buildingCost(b.type, 0, side.id);
        if (buildingTemplate(b).specialBehavior == building_behaviour::kRefinery)
            v -= unitCost(static_cast<std::uint8_t>(rules_.templates->bin.groupIds[group_ids::kHarvester]), side.id);
        addCash(side.id, static_cast<std::int32_t>(v >> 1));
    }
    side.freeObject(b.index);
    updateIcons(side.id);
}

void World::freeUnit(Side& side, Unit& u) {
    const auto& t = unitTemplate(u);
    if (t.specialBehavior == behaviour::kHarvester) --side.harvesterCount;
    else if (t.specialBehavior == behaviour::kCarryall && !(u.flags & 0x800)) --side.carryallCount;
    if (t.specialBehavior == behaviour::kCarryall) --side.carryallsAlive;
    u.state = UnitState::None;
    --side.unitsExistPerType[u.type];
    side.freeObject(u.index);
}

// ---------------------------------------------------------------------------------------
// UnitAdjustState 0x49deb0

bool World::unitAdjustState(Unit& u, UnitState ns) {
    const auto& t = unitTemplate(u);
    const int s = static_cast<int>(ns);
    switch (t.specialBehavior) {
    case 1: case 3: case 4: case 5: case 6: case 7: case 11: case 12: case 13: case 14: case 16: break;
    case 2: case 8: case 9: case 10:  // carryall, frigate, ornithopter, death hand
        switch (s) {
        case 3: case 4: case 5: case 6: case 8: case 9: case 10: case 11: case 12: case 13: case 14:
        case 15: case 16: case 18: case 19: case 23: case 24: case 25: case 26: case 27: case 28:
        case 29: case 30: case 31: case 32: return false;
        default: break;
        }
        break;
    default:
        switch (s) {
        case 11: case 12: case 13: case 14: case 15: case 16: case 21: case 22: case 23: case 27:
        case 28: case 29: case 30: case 31: case 32: return false;
        default: break;
        }
    }
    if (t.primaryWeapon == -1) {
        if (s == 3 || s == 4 || s == 6 || s == 10) return false;
        if (s == 5 && t.specialBehavior != 4 && t.specialBehavior != 3) return false;
    }
    u.routeSteps = 0;
    u.flags = (u.flags & 0x3fffffffu) | kStateChanged;
    u.routeTotalSteps = 0;
    u.routeBlocked = false;
    if (s != 10) u.routeMode = 0;
    u.routeRetryDelay = 0;
    const int cur = static_cast<int>(u.state);
    if (cur == s) return true;
    if (cur == 17) return false;
    if ((cur == 31 || cur == 23 || cur == 30) && s != 17) return false;
    if (s == 9 && (cur == 25 || cur == 26 || cur == 11)) return false;
    if (cur == 14) {
        if (s != 17 && s != 15 && s != 16) return false;
    } else if (cur == 15) {
        if (s != 17 && s != 16) return false;
    } else if (cur == 16) {
        if (s != 17 && s != 11) return false;
        if (u.animFrame != 0 && s == 11) return false;  // byte 0x1e
    }
    if (s == 17) u.selected = false;
    if (s == 9) u.oldState = cur == 18 ? UnitState::Idle : u.state;
    if (s == 2 && u.oldState != UnitState::None) {
        u.state = u.oldState == UnitState::AwaitingAirlift ? UnitState::Idle : u.oldState;
        u.oldState = UnitState::None;
        return true;
    }
    u.state = ns;
    return true;
}

// ---------------------------------------------------------------------------------------
// UpdateUnit 0x497b90

bool World::updateUnit(Unit& u) {
    const auto& t = unitTemplate(u);
    const int sideId = u.side;
    const int idx = u.index;
    const bool isInf = t.isInfantry != 0;
    const bool onSlot = static_cast<std::uint32_t>(idx & 7) == (gameTicks_ & 7);

    // Infantry standing on a vehicle's cell is drawn behind it and may be crushed.
    if (!isInf || !(cell(u.blockFromX, u.blockFromY).flags & kVehicle)) {
        u.flags &= ~kDrawnBehind;
    } else {
        u.flags |= kDrawnBehind;
        crushCheck(u);
    }
    if (isInf) advanceInfantryAnim(u);
    if ((u.flags & 0x800000) && u.health * 2 < static_cast<std::int32_t>(t.hitPoints) && (gameTicks_ & 3) == 0)
        ++u.health;  // self-repair
    // Saboteur stealth charge (0x497c60..0x497cd0), every 16 ticks: byte 0x29 charges up to 0xa0
    // while not stealthed, drains by 4 while stealthed; empty -> stealth ends (decloak).
    if (t.specialBehavior == behaviour::kSaboteur && (gameTicks_ & 0xf) == 0) {
        const std::uint32_t f = u.flags;
        const std::uint8_t c = u.byte29;
        if (!(f & kStealth)) {
            if (c < 0xa0) u.byte29 = static_cast<std::uint8_t>(c + 1);
        } else if (c == 0) {
            if (f & kCloaked) {
                u.flags = f & ~kStealth;
                unitDecloak(u);
            }
        } else {
            u.byte29 = c > 4 ? static_cast<std::uint8_t>(c - 4) : 0;
        }
    }
    if (u.reloadDelay != 0) --u.reloadDelay;
    updateStealth(u);  // Unit_UpdateStealth 0x49f610

    // Drop dead attackers / targets.
    if (u.attackerIndex != kNoIndex && u.attackerSide < kMaxSides) {
        const Side& as = sides_[u.attackerSide];
        const auto kind = as.links[u.attackerIndex].kind;
        if (kind == SlotKind::Unit) {
            if (as.units[u.attackerIndex].state == UnitState::Dead) u.attackerIndex = kNoIndex;
        } else if (kind == SlotKind::Building) {
            if (as.buildings[u.attackerIndex].state == 17) u.attackerIndex = kNoIndex;
        }
    }
    if (t.specialBehavior != behaviour::kCarryall && u.enemyIndex != kNoIndex) {
        bool drop = true;
        if (u.enemySide >= 0 && u.enemySide < kMaxSides) {
            const Side& es = sides_[u.enemySide];
            const auto kind = es.links[u.enemyIndex].kind;
            if (kind == SlotKind::Unit) {
                const Unit& e = es.units[u.enemyIndex];
                drop = e.state == UnitState::Dead ||
                       ((e.flags & 0x400104) && diplomacy(sideId, u.enemySide) != 0) ||
                       ((e.flags & kCarried) && !(e.flags & kFlying));
            } else if (kind == SlotKind::Building) {
                drop = es.buildings[u.enemyIndex].state == 17;
            }
        }
        if (drop) u.enemyIndex = kNoIndex;
    }
    // Damaged vehicles smoke (byte 0x27, set by DamageTiles below half health).
    if (u.smokeTimer != 0 && !isInf) {
        if (static_cast<std::int32_t>(t.hitPoints >> 1) < u.health) {
            u.smokeTimer = 0;
        } else {
            if ((((static_cast<std::uint32_t>(rng_.next()) & 1) + gameTicks_) & 3) == 0) {
                const std::uint32_t ry = static_cast<std::uint32_t>(rng_.next());
                const std::uint32_t rx = static_cast<std::uint32_t>(rng_.next());
                addExplosion(sideId, rules_.templates->bin.groupIds[59],  // EX_SMOKE3
                             static_cast<int>(rx & 7) - 4 + u.posX / 65536, static_cast<int>(ry & 3) - 2 + u.posY / 65536,
                             u.posZ);
            }
            if ((gameTicks_ & 3) == 0) --u.smokeTimer;
        }
    }

    if (onSlot && !(u.flags & kFlying)) {
        if (u.blockFromX == fixToTile(u.posX) && u.blockFromY == fixToTile(u.posY)) {
            if (u.lastMoved < 0xFF) ++u.lastMoved;
        } else {
            u.lastMoved = 0;
        }
    }
    if (!(u.flags & kAttacked)) {
        if (onSlot && !(u.flags & kCarried)) {
            if (u.lastAttacked < 0xFF) ++u.lastAttacked;
            if (u.lastAttacked > 100) u.flags &= ~0x8u;
            // A deviated unit returns to its old side once the timer (+0x2e) has run out.
            if ((u.flags & kDeviated) && deviatedOwnerAlive(u)) {
                if (u.byte2E != 0) {
                    --u.byte2E;
                } else if (!unitTileOccupiedByBuilding(u) && !(u.flags & kGone)) {
                    u.flags = (u.flags & ~kDeviated) | 0x80;
                    if (sideId != u.oldSide) changeUnitOwner(sideId, u.oldSide, idx, false);
                }
            }
        }
    } else {
        u.lastAttacked = 0;
        u.flags &= ~kAttacked;
        if (deviatedOwnerAlive(u)) u.byte2E = u.byte2E < 0x1e ? 0 : static_cast<std::uint8_t>(u.byte2E - 0x1e);
    }
    if (!(u.flags & kFired)) {
        if (onSlot && u.lastFired < 0xFF) ++u.lastFired;
    } else {
        u.lastFired = 0;
        u.flags &= ~kFired;
    }

    switch (u.state) {
    case UnitState::Idle: updateIdle(u); break;
    case UnitState::Guarding: updateGuarding(u); break;
    case UnitState::AttackingUnit:
    case UnitState::AttackingBuilding:
        if (u.posSteps != 0) {
            moveUnit(u);
        } else if (!(u.state == UnitState::AttackingUnit ? attackUnit(u) : attackBuilding(u))) {
            // Target gone: stop where we are.
            unitAdjustState(u, UnitState::Moving);
            u.oldState = UnitState::None;
            u.enemyIndex = kNoIndex;
        }
        break;
    case UnitState::AttackingTile:
        if (!attackTile(u)) unitAdjustState(u, UnitState::Idle);
        break;
    case UnitState::Moving:
        if (u.flags & kDeathHand) {
            if (!deathHandFlight(u)) unitAdjustState(u, UnitState::Dead);
        } else if (!(u.flags & kFlying)) {
            if (moveUnit(u)) break;
            // Arrived (re/notes/movement.md §1.4).
            const UnitState old = u.oldState;
            if (old != UnitState::None && old != UnitState::Moving && old != UnitState::AwaitingAirlift)
                unitAdjustState(u, old);
            else
                unitAdjustState(u, UnitState::Idle);
        } else {
            // A frigate carrying its cargo animates the primary starport.
            if (t.specialBehavior == behaviour::kFrigate && (u.flags & kCarried) &&
                sides_[sideId].primaryBuilding[7] != kNoIndex)
                sides_[sideId].buildings[static_cast<std::size_t>(sides_[sideId].primaryBuilding[7]) % kObjectsPerSide].flags |=
                    building_flags::kAnimOnce;
            if (u.flags & 0x200) {
                const PickupEntry& e = sides_[sideId].pickup[u.byte29 & 31];
                if (e.state == 3 && !(u.flags & kCarried)) {  // pickup cancelled
                    u.flags &= ~0x200u;
                    unitAdjustState(u, UnitState::Idle);
                    break;
                }
            }
            if (!flyUnit(u, false)) unitAdjustState(u, UnitState::Idle);
        }
        break;
    case UnitState::LeavingBuilding: updateLeavingBuilding(u); break;
    case UnitState::MovingAway: updateMovingAway(u); break;
    case UnitState::AttackingTileOnRoute:
        if (u.posSteps == 0) {
            const std::uint32_t c = cell(u.targetX, u.targetY).flags;
            if ((c & kOccupied) == 0 || static_cast<int>(c & kOwnerMask) == sideId) {
                u.targetX = u.rememberX;
                u.targetY = u.rememberY;
                u.routeMode = 0;
                unitAdjustState(u, u.oldState);
            } else {
                attackTile(u);  // move into range and fire at the blocker
            }
        } else {
            moveUnit(u);
        }
        break;
    case UnitState::MovingToHarvest: updateMovingToHarvest(u); break;
    case UnitState::Harvesting: updateHarvesting(u); break;
    case UnitState::MovingToRefinery: updateMovingToRefinery(u); break;
    case UnitState::EnteringRefinery: updateEnteringRefinery(u); break;
    case UnitState::UnloadingSpice: updateUnloadingSpice(u); break;
    case UnitState::LeavingRefinery: updateLeavingRefinery(u); break;
    case UnitState::AwaitingAirlift: updateAwaitingAirlift(u); break;
    case UnitState::Airlifted: break;  // dragged by the carryall
    case UnitState::FlyingDown: updateFlyingDown(u); break;
    case UnitState::FlyingUp: updateFlyingUp(u); break;
    case UnitState::Deploying: updateDeploying(u); break;
    case UnitState::Dead:
        if (!updateDead(u)) return false;
        break;
    case UnitState::Wandering: updateWormWandering(u); break;   // case 0x19
    case UnitState::Eating: updateWormEating(u); break;         // case 0x1a
    case UnitState::Sleeping: updateWormSleeping(u); break;     // case 0x1b
    case UnitState::Selfdestruct: updateSelfdestruct(u); break; // case 0x1d
    case UnitState::Thumpering: updateThumper(u); break;        // case 0x1e
    case UnitState::State33:                                     // case 0x1f
        if (u.byte29 != 0) --u.byte29;
        else unitAdjustState(u, UnitState::Idle);
        break;
    case UnitState::Infiltrating: updateInfiltrating(u); break;           // case 0x15
    case UnitState::MovingToRepairPad: updateMovingToRepairPad(u); break;  // case 0x16
    case UnitState::Repairing: updateRepairing(u); break;                  // case 0x17
    case UnitState::LeavingRepairPad: updateLeavingRepairPad(u); break;    // case 0x18
    default:
        break;
    }
    dragCargo(u);  // a carryall carrying a unit drags it along
    return true;
}

// UpdateUnit case 0 (Idle).
void World::updateIdle(Unit& u) {
    const auto& t = unitTemplate(u);
    if (t.isInfantry) setInfantryAnim(u, 0);
    if (t.specialBehavior == behaviour::kDeathHand) unitAdjustState(u, UnitState::Moving);
    if (t.specialBehavior == behaviour::kBird) {
        u.targetX = static_cast<int>(static_cast<std::uint32_t>(rng_.next()) % static_cast<std::uint32_t>(width_));
        u.targetY = static_cast<int>(static_cast<std::uint32_t>(rng_.next()) % static_cast<std::uint32_t>(height_));
        unitAdjustState(u, UnitState::Moving);
    } else if (t.specialBehavior == behaviour::kCarryall || t.specialBehavior == behaviour::kFrigate) {
        const UnitState before = u.state;
        const std::uint32_t f = u.flags;
        updateCarryallIdle(u);
        // A delivery carryall that starts descending leaves the Idle case directly ("break").
        if ((f & 0x800) && (f & kCarried) && !(f & 0x4000) && u.state == UnitState::FlyingDown && before == UnitState::Idle)
            return;
    }
    if (t.specialBehavior == behaviour::kHarvester) {
        if (cell(u.blockFromX, u.blockFromY).flags & kSpiceMask) unitAdjustState(u, UnitState::Harvesting);
        return;
    }
    const bool onSlot = static_cast<std::uint32_t>(u.index & 7) == (gameTicks_ & 7);
    if (!onSlot && u.enemyIndex == kNoIndex) return;
    if (!unitIsHiddenStealth(u) && !unitAutoTarget(u)) idleTurretWander(u);
}

// UpdateUnit case 1 (Guarding). RememberPos is the guard post.
void World::updateGuarding(Unit& u) {
    const auto& t = unitTemplate(u);
    const int enemy = u.enemyIndex;
    if (enemy != kNoIndex && u.posSteps == 0) {
        const Unit* e = (u.enemySide >= 0 && u.enemySide < kMaxSides) ? unit(u.enemySide, enemy) : nullptr;
        if (e) {
            // Tile distance from pixel positions, truncated toward zero.
            const int dx = (u.posX - e->posX) / 32 / 65536, dy = (u.posY - e->posY) / 32 / 65536;
            if (static_cast<std::uint32_t>(dx * dx + dy * dy) < 36 &&
                unitTemplate(*e).specialBehavior != behaviour::kSandworm) {
                if ((e->flags & 0x140) == 0) attackUnit(u);
                else if (u.reloadDelay == 0) fireAt(u, e->posX / 65536, e->posY / 65536, u.enemySide, enemy, 1);
            } else {
                u.enemyIndex = kNoIndex;
                u.enemySide = 0xFF;
            }
        } else if (building(u.enemySide, enemy)) {
            attackBuilding(u);
        }
        return;
    }
    if (u.posSteps != 0) {
        u.targetX = u.rememberX;
        u.targetY = u.rememberY;
        moveUnit(u);
        return;
    }
    const bool onSlot = static_cast<std::uint32_t>(u.index & 7) == (gameTicks_ & 7);
    if (!onSlot && enemy == kNoIndex) {
        moveUnit(u);
        return;
    }
    if (unitAutoTarget(u)) {
        moveUnit(u);
        return;
    }
    // Chase enemies within 192 px of the guard post (map rule guardModeRadius).
    int ex = 0, ey = 0, es = 0, ei = kNoIndex;
    if (findNearestEnemy(u.rememberX * 32 + 16, u.rememberY * 32 + 16, u.side, rulesExt().guardModeRadius, ex, ey, es,
                         ei, t.primaryWeapon,
                         t.secondaryWeapon, false, false, true)) {
        u.enemySide = es;
        u.enemyIndex = ei;
        attackUnit(u);
        return;
    }
    u.targetX = u.rememberX;
    u.targetY = u.rememberY;
    u.routeSteps = 0;  // forces a re-plan every check tick
    if (moveUnit(u)) return;
    if (t.isInfantry) setInfantryAnim(u, 0);
    if (!(u.flags & kStealth) && !unitAutoTarget(u)) idleTurretWander(u);
}

// UpdateUnit case 7 (MOVING_AWAY, state 9).
void World::updateMovingAway(Unit& u) {
    if (u.posSteps != 0) {
        moveUnit(u);
        return;
    }
    int cx = 0, cy = 0;
    if (findFreeAdjacentSquare(u.blockFromX, u.blockFromY, u, u.side, cx, cy, 0)) {
        u.targetX = cx;
        u.targetY = cy;
        if (u.oldState == UnitState::Guarding) {
            u.rememberX = cx;
            u.rememberY = cy;
        }
        if (moveUnit(u)) {
            if (u.flags & kBlockToMarked) unitAdjustState(u, UnitState::Moving);
            return;
        }
    } else {
        bool pushed = false;
        for (int d = 0; d < 32; d += 4) {
            int nx = u.blockFromX, ny = u.blockFromY;
            stepInDirection(nx, ny, d);
            // Deviation: the original clamps off-map pixel coordinates to cell 127 (reading an
            // unrelated cell); we treat off-map neighbours as empty.
            if (!inBounds(nx, ny)) continue;
            int vs = 0, vi = 0;
            Unit* v = getUnitOnTile(nx << 5, ny << 5, vs, vi, false);
            if (v && diplomacy(u.side, vs) == 0 && unitCanTakeOrders(v, false) && v->state != UnitState::Moving &&
                v->state != UnitState::LeavingRepairPad && v->state != UnitState::MovingToHarvest &&
                v->state != UnitState::MovingAway) {
                unitAdjustState(*v, UnitState::MovingAway);
                pushed = true;
            }
        }
        if (pushed) return;
    }
    const UnitState old = u.oldState;
    if (old != UnitState::None && old != UnitState::AwaitingAirlift) unitAdjustState(u, old);
    else unitAdjustState(u, UnitState::Idle);
}

// UpdateUnit case 6 (LEAVING_BUILDING, state 8): walk straight to Target with MoveUnitSimple;
// a friendly unit in the way is asked to move, an enemy one only blocks if the cell is free.
void World::updateLeavingBuilding(Unit& u) {
    const auto& t = unitTemplate(u);
    if (t.specialBehavior == behaviour::kCarryall) {
        unitAdjustState(u, UnitState::Idle);
        return;
    }
    int vs = 0, vi = 0;
    Unit* v = getUnitOnTile(u.targetX << 5, u.targetY << 5, vs, vi, false);
    if (v) {
        if (vs == u.side) {
            if (unitTemplate(*v).isInfantry && t.isInfantry) {
                const std::uint32_t f = cell(u.targetX, u.targetY).flags;
                bool freeSpot = false;
                for (std::uint32_t bit = 0x200; bit >= 0x20 && !freeSpot; bit >>= 1) freeSpot = !(f & bit);
                if (freeSpot || (u.blockToX == u.targetX && u.blockToY == u.targetY)) v = nullptr;
            }
        } else if (diplomacy(u.side, vs) != 0 && canUnitUseSquare(u.targetX, u.targetY, u, u.side, 0)) {
            v = nullptr;
        }
    }
    if (!v || v == &u) {
        if (!moveUnitSimple(u, true, true))
            unitAdjustState(u, t.specialBehavior == behaviour::kHarvester ? UnitState::Harvesting : UnitState::Idle);
    } else if (diplomacy(u.side, vs) == 0) {
        askUnitsOnTileToMoveAway(u, u.targetX, u.targetY);
    }
    if (u.posSteps != 0 || unitTileOccupiedByBuilding(u)) return;
    unitAdjustState(u, t.specialBehavior == behaviour::kHarvester ? UnitState::Harvesting : UnitState::Idle);
}

// UpdateUnit case 0xf (DEAD): count down, then release the occupied cells and free the slot.
bool World::updateDead(Unit& u) {
    const auto& t = unitTemplate(u);
    if (u.deadStateTimer == 46 && equalsNoCaseAscii(rules_.templates->bin.unitNames[u.type], "Grenadier"))
        damageTiles(u.blockFromX << 5, u.blockFromY << 5, 0, 0x21, u.side, kNoIndex, false);  // grenades go off
    if (u.deadStateTimer != 0) {
        --u.deadStateTimer;
        if (u.flags & kFlying) {  // dead flyers keep flying while the timer runs
            if (u.flags & kDeathHand) {
                deathHandFlight(u);
            } else {
                flyUnit(u, false);
            }
        }
        return true;
    }
    if (u.flags & kFlying) markFlyer(u.blockFromX, u.blockFromY, false);  // FUN_0049f330
    if ((u.flags & 0x400040) == 0) {
        auto releaseVehicle = [](std::uint32_t& c) {
            c &= ~kVehicle;
            if (c & kInfantryMask) c = (c & ~kOwnerMask) | ((c >> kInfantryOwnerShift) & 7);
        };
        if (!t.isInfantry) {
            releaseVehicle(cell(u.blockFromX, u.blockFromY).flags);
            if (u.flags & kBlockToMarked) releaseVehicle(cell(u.blockToX, u.blockToY).flags);
        } else {
            cell(u.blockFromX, u.blockFromY).flags &= ~(0x20u << u.cellspotFrom);
            if (u.flags & kBlockToMarked) cell(u.blockToX, u.blockToY).flags &= ~(0x20u << u.cellspotTo);
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------
// Sandworms (UpdateUnit cases 0x19..0x1b), self-destruct (0x1d) and thumper (0x1e).
// A worm sleeps underground (flag 4 set every tick; the prologue's Unit_UpdateStealth clears it
// again for non-stealth units, so a wandering worm is visible), wakes up at full health with a
// random destination, wanders there and eats the first non-infantry unit on its tile on arrival,
// then digests (Eating) and picks the next target: a working thumper, else the nearest vehicle on
// sand (or sleeps when it ate SandWormAppetite units), else a random tile.

// FUN_0044d500: the nearest (Manhattan, u16) thumpering thumper on sand; row-major scan, first
// minimum wins.
bool World::findThumper(int fromX, int fromY, int& ox, int& oy) {
    const std::uint32_t W = static_cast<std::uint32_t>(width_), H = static_cast<std::uint32_t>(height_);
    std::uint32_t best = W + H;
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x) {
            const std::uint32_t f = cell(static_cast<int>(x), static_cast<int>(y)).flags;
            if (!(f & kSand) || !(f & kInfantryMask)) continue;
            int vs = 0, vi = 0;
            const Unit* v = getUnitOnTile(static_cast<int>(x * 32), static_cast<int>(y * 32), vs, vi, false);
            if (!v || unitTemplate(*v).specialBehavior != behaviour::kThumper || v->state != UnitState::Thumpering)
                continue;
            const std::uint32_t d = static_cast<std::uint32_t>(std::abs((fromX & 0xff) - static_cast<int>(x)) +
                                                               std::abs((fromY & 0xff) - static_cast<int>(y)));
            if ((d & 0xffff) < (best & 0xffff)) {
                ox = static_cast<int>(x);
                oy = static_cast<int>(y);
                best = d;
            }
        }
    return static_cast<int>(best & 0xffff) < static_cast<int>(H + W);
}

// FUN_0044d630: a vehicle on the given cell, else the nearest (Manhattan, u16) cell with a
// vehicle on sand.
bool World::findVehicleOnSand(int fromX, int fromY, int& ox, int& oy) const {
    fromX &= 0xff;
    fromY &= 0xff;
    if (const std::uint32_t* f = const_cast<World*>(this)->linearCellFlags(fromX, fromY); f && (*f & kVehicle)) {
        ox = fromX;
        oy = fromY;
        return true;
    }
    const std::uint32_t W = static_cast<std::uint32_t>(width_), H = static_cast<std::uint32_t>(height_);
    std::uint32_t best = W + H;
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x) {
            const std::uint32_t f = cell(static_cast<int>(x), static_cast<int>(y)).flags;
            if (!(f & kSand) || !(f & kVehicle)) continue;
            const std::uint32_t d =
                static_cast<std::uint32_t>(std::abs(fromX - static_cast<int>(x)) + std::abs(fromY - static_cast<int>(y)));
            if ((d & 0xffff) < (best & 0xffff)) {
                ox = static_cast<int>(x);
                oy = static_cast<int>(y);
                best = d;
            }
        }
    return static_cast<int>(best & 0xffff) < static_cast<int>(H + W);
}

// Worm_SpawnSandParticles 0x45d730: sand spray around a moving worm. 4 tries of a random offset
// (-12..12)^2; inside r^2 < 0x90 two particles at worm + offset: one drifting outwards
// (|v| = rand % 200 * 0.001 px/tick per axis, sign of the offset, life rand % 30 + 10) and one
// standing (life rand % 30 + 50). Both take the colour of the tile pixel under the worm itself.
void World::wormDust(int px, int py) {
    for (int i = 0; i < 4; ++i) {
        const int dx = 12 - static_cast<int>(static_cast<std::uint32_t>(rng_.next()) % 25);
        const int dy = 12 - static_cast<int>(static_cast<std::uint32_t>(rng_.next()) % 25);
        if (dx * dx + dy * dy < 0x90) {
            const float x = static_cast<float>(dx + (px & 0xffff)), y = static_cast<float>(dy + (py & 0xffff));
            // (float10)(r % 200) * (float10)0.001 rounded to float
            float vx = static_cast<float>(static_cast<long double>(static_cast<std::uint32_t>(rng_.next()) % 200) * 0.001L);
            float vy = static_cast<float>(static_cast<long double>(static_cast<std::uint32_t>(rng_.next()) % 200) * 0.001L);
            if (dx < 0) vx = -vx;
            if (dy < 0) vy = -vy;
            const int cx = (px & 0xffff) >> 5, cy = (py & 0xffff) >> 5;
            const std::uint16_t tile = inBounds(cx, cy) ? cell(cx, cy).tile : 0;
            const std::uint32_t life1 = static_cast<std::uint32_t>(rng_.next()) % 0x1e + 10;
            addParticle(x, y, vx, vy, life1, tile, px & 0x1f, py & 0x1f);
            const std::uint32_t life2 = static_cast<std::uint32_t>(rng_.next()) % 0x1e + 0x32;
            addParticle(x, y, 0, 0, life2, tile, px & 0x1f, py & 0x1f);
        }
    }
}

// FUN_0045d460: first free record, ignored when all 4096 are in use.
void World::addParticle(float x, float y, float vx, float vy, std::uint32_t life, std::uint16_t tile, int tpx, int tpy) {
    if (particleCount_ >= kMaxParticles) return;
    for (auto& p : particles_) {
        if (p.active) continue;
        p = Particle{x, y, vx, vy, life & 0xffff, tile, static_cast<std::uint8_t>(tpx), static_cast<std::uint8_t>(tpy), true};
        ++particleCount_;
        return;
    }
}

// FUN_0045d4f0 (end of every game tick): life 0 -> freed, else x += vx, y += vy, life--.
void World::updateParticles() {
    if (particleCount_ == 0) return;
    for (auto& p : particles_) {
        if (!p.active) continue;
        if (p.life == 0) {
            p.active = false;
            --particleCount_;
        } else {
            p.x = p.vx + p.x;
            --p.life;
            p.y = p.vy + p.y;
        }
    }
}

// State 27 (case 0x19).
void World::updateWormWandering(Unit& u) {
    const auto& t = unitTemplate(u);
    if (u.health < static_cast<std::int32_t>(t.hitPoints >> 1)) {
        unitAdjustState(u, UnitState::Sleeping);
        u.animDelay = vars().sandWormShotSleep;
        return;
    }
    if (!moveUnit(u)) {
        int vs = 0, vi = 0;
        Unit* v = getUnitOnTile(u.blockFromX << 5, u.blockFromY << 5, vs, vi, false);
        if (!v || unitTemplate(*v).isInfantry) {
            u.flags &= ~kCloaked;
            u.animDelay = 0;
            unitAdjustState(u, UnitState::Eating);
            return;
        }
        unitAdjustState(*v, UnitState::Dead);  // swallowed: no DestroyUnit, no debris
        ++sides_[vs].unitsLost;
        ++sides_[vs].unitsLostPerType[v->type];
        u.animDelay = 0x32;
        u.flags &= ~kCloaked;
        unitAdjustState(u, UnitState::Eating);
        const int px2 = u.posX / 65536, py2 = u.posY / 65536;
        const int de = t.deathExplosion;
        if (de >= 0 && static_cast<std::size_t>(de) < explosionCount())
            playSound(explosionTemplate(de).sound, px2 / 32, py2 / 32);
        if (vs == mySide_) mentat(vs, "WORMATTACK", true);
        addExplosion(u.side, static_cast<std::uint8_t>(de), px2, py2, 0);
        addExplosion(u.side, static_cast<std::uint8_t>(de + 1), px2, py2, 0);
        ++u.byte29;  // units eaten
        return;
    }
    if (u.routeMode == 0) {
        if (!(u.flags & kCloaked) && (u.posStepsMax == 0 || u.posSteps < u.posStepsMax * 2)) {
            wormDust(u.posX / 65536, u.posY / 65536);  // position after MoveUnit
            if (static_cast<std::uint32_t>(rng_.next()) % 0x50 == 0) {
                const std::uint32_t r = static_cast<std::uint32_t>(rng_.next()) % 6;
                const int ex = static_cast<std::uint8_t>(rules_.templates->bin.groupIds[73 + r]);
                const int wx = u.posX / 65536, wy = u.posY / 65536;
                addExplosion(u.side, ex, wx, wy, 0);
                // Only when the worm is inside the battlefield view and its cell is not shrouded:
                // the wormsign speech (2000-tick repeat guard) and the explosion's sound unless
                // it is already playing. The view test is the frontend's (onScreen events).
                if ((cell(u.blockFromX, u.blockFromY).shroud) == 0) {
                    SoundEvent e = mentatEvent("WORMSIGN", true, 2000);  // no side check
                    e.onScreen = true;
                    e.spotX = wx;
                    e.spotY = wy;
                    sounds_.push_back(e);
                    if (static_cast<std::size_t>(ex) < explosionCount())
                        playSoundIfVisible(explosionTemplate(ex).sound, wx, wy);
                }
            }
        }
        return;
    }
    // Stuck (route escalated): pick a free neighbour, or go to sleep.
    u.flags &= ~kCloaked;
    u.routeMode = 0;
    u.animDelay = 0;
    int cx = 0, cy = 0;
    if (!findFreeAdjacentSquare(u.blockFromX, u.blockFromY, u, u.side, cx, cy, 0)) {
        unitAdjustState(u, UnitState::Sleeping);
        u.animDelay = vars().sandWormShotSleep;
    } else {
        u.targetX = cx;
        u.targetY = cy;
    }
}

// State 28 (case 0x1a).
void World::updateWormEating(Unit& u) {
    if (u.animDelay != 0) {
        --u.animDelay;
        return;
    }
    int x = 0, y = 0, x2 = 0, y2 = 0;
    if (findThumper(u.blockFromX, u.blockFromY, x, y)) {
        if (findVehicleOnSand(x, y, x2, y2) && std::abs((x & 0xff) - (x2 & 0xff)) < 5 &&
            std::abs((y & 0xff) - (y2 & 0xff)) < 5) {
            u.targetX = x2;
            u.targetY = y2;
        } else {
            u.targetX = x;
            u.targetY = y;
        }
        unitAdjustState(u, UnitState::Wandering);
        return;
    }
    if (findVehicleOnSand(u.blockFromX, u.blockFromY, x, y)) {
        if (u.byte29 < vars().sandWormAppetite) {
            u.targetX = x;
            u.targetY = y;
            unitAdjustState(u, UnitState::Wandering);
        } else {
            unitAdjustState(u, UnitState::Sleeping);
            u.animDelay = vars().sandWormFedSleep;
        }
        return;
    }
    u.targetX = static_cast<std::uint8_t>(static_cast<std::uint32_t>(rng_.next()) % static_cast<std::uint32_t>(width_));
    u.targetY = static_cast<std::uint8_t>(static_cast<std::uint32_t>(rng_.next()) % static_cast<std::uint32_t>(height_));
    unitAdjustState(u, UnitState::Wandering);
}

// State 29 (case 0x1b): +0x25 counts down in units of 101 ticks (counter in +0x1b).
void World::updateWormSleeping(Unit& u) {
    u.flags |= kCloaked;
    if (u.animDelay != 0) {
        u.turretTurnDelay = static_cast<std::uint8_t>(u.turretTurnDelay + 1);
        if (u.turretTurnDelay > 100) {
            u.turretTurnDelay = 0;
            --u.animDelay;
        }
        return;
    }
    u.health = static_cast<std::int32_t>(unitTemplate(u).hitPoints);
    u.targetX = static_cast<std::uint8_t>(static_cast<std::uint32_t>(rng_.next()) % static_cast<std::uint32_t>(width_));
    u.targetY = static_cast<std::uint8_t>(static_cast<std::uint32_t>(rng_.next()) % static_cast<std::uint32_t>(height_));
    unitAdjustState(u, UnitState::Wandering);
}

// State 31 (case 0x1d, Devastator): finish the step, then smoke (rand % 3, 2nd rand picks the
// explosion) while +0x29 counts down from DevastatorExplodeDelay; at 0 it dies and DamageTiles
// with the self-destruct weapon (groupIds[51]).
void World::updateSelfdestruct(Unit& u) {
    if (u.posSteps != 0) {
        u.targetX = u.rememberX;
        u.targetY = u.rememberY;
        moveUnit(u);
        return;
    }
    const auto& G = rules_.templates->bin.groupIds;
    const int px = u.posX / 65536, py = u.posY / 65536;
    if (static_cast<std::uint32_t>(rng_.next()) % 3 == 0) {
        const std::uint32_t r = static_cast<std::uint32_t>(rng_.next()) % 3;
        addExplosion(u.side, static_cast<std::uint8_t>(G[81 + r]), px, py, 0);
    }
    if (u.byte29 != 0) {
        --u.byte29;
        return;
    }
    unitAdjustState(u, UnitState::Dead);
    addExplosion(u.side, static_cast<std::uint8_t>(G[84]), px, py, 0);
    ++sides_[u.side].unitsLost;
    ++sides_[u.side].unitsLostPerType[u.type];
    damageTiles(px, py, 6, static_cast<std::uint8_t>(G[51]), u.side, u.index, false);
}

// State 32 (case 0x1e, thumper infantry): turn to face 8, then loop the thump animation (8);
// every 60 cycles a 3-tick pause; an explosion (groupIds[79]) with its sound on each thump.
void World::updateThumper(Unit& u) {
    if (u.posSteps != 0) {
        u.targetX = u.rememberX;
        u.targetY = u.rememberY;
        moveUnit(u);
        return;
    }
    if (rotateBody(u, 8)) return;
    setInfantryAnim(u, 8);
    const int first = kInfAnimFirst(u.animType);
    if (u.animFrame == first + 1) {
        u.byte29 = static_cast<std::uint8_t>(u.byte29 + 1);
        if (u.byte29 < 0x3c) {
            u.animDelay = 0;
        } else {
            u.byte29 = 0;
            u.animDelay = 3;
        }
    }
    if (u.animFrame == first) {
        const int ex = static_cast<std::uint8_t>(rules_.templates->bin.groupIds[79]);
        const int px = u.posX / 65536, py = u.posY / 65536;
        addExplosion(u.side, ex, px, py, 0);
        if (cell(u.blockFromX, u.blockFromY).shroud == 0 && static_cast<std::size_t>(ex) < explosionCount())
            playSoundIfVisible(explosionTemplate(ex).sound, px, py);
    }
}

// ---------------------------------------------------------------------------------------
// Unit repair pads (UpdateUnit cases 0x16..0x18; ProcessOrder case 0xe sets state 24).

// State 24 (case 0x16): drive to the repair pad's dock (exit point 1); a missing pad is
// replaced by the nearest one; far away (> RepairDriveDistance) the unit asks for a carryall.
void World::updateMovingToRepairPad(Unit& u) {
    const int side = u.side;
    auto arrived = [&] {
        if (side == mySide_) {
            mentat(side, "REPAIRUNIT", false);
            if (sides_[side].cash + sides_[side].spice == 0) mentat(side, "LOWMONEY", true);
        }
        unitAdjustState(u, UnitState::Repairing);
    };
    Building* pad = building(side, u.repairPadIndex);
    if (!pad || buildingTemplate(*pad).specialBehavior != building_behaviour::kRepairPad) {
        int x = 0, y = 0;
        pad = nearestBuildingWithBehaviour(u.blockFromX, u.blockFromY, side, building_behaviour::kRepairPad, x, y);
        if (!pad) {
            if (u.posSteps == 0) unitAdjustState(u, UnitState::Idle);
            else moveUnit(u);
        } else {
            u.targetX = x;
            u.targetY = y;
            u.repairPadIndex = pad->index;
            if (!moveUnit(u)) {
                arrived();
                return;
            }
        }
    } else {
        const auto& bt = buildingTemplate(*pad);
        const int x = static_cast<std::uint8_t>(pad->posX / 65536 / 32 + bt.exitPoint1X);
        const int y = static_cast<std::uint8_t>((pad->posY / 65536 - bt.artHeight) / 32 + bt.exitPoint1Y);
        u.targetX = x;
        u.targetY = y;
        if (!moveUnit(u) || (u.blockFromX == x && u.blockFromY == y) ||
            ((u.flags & kBlockToMarked) && u.blockToX == x && u.blockToY == y)) {
            arrived();
            return;
        }
    }
    if (u.posSteps != 0 || (u.flags & kBlockToMarked)) return;
    const int dx = u.blockFromX - u.targetX, dy = u.blockFromY - u.targetY;
    if (dx * dx + dy * dy <= static_cast<int>(vars().repairDriveDistance)) return;
    Side& s = sides_[side];
    const int pos = pickupQueuePos(s, u);
    if (pos != -1 && addToPickupQueue(s, u, pos, 100)) unitAdjustState(u, UnitState::AwaitingAirlift);
}

// State 25 (case 0x17): on the pad, UnitRepairValue hp per tick for
// max(1, ((cost * value) / maxHP) >> 1) credits; the pad animates. Full health or no pad of
// ours under the unit -> leave (26).
void World::updateRepairing(Unit& u) {
    if (u.posSteps != 0) {
        moveUnit(u);
        return;
    }
    const auto& t = unitTemplate(u);
    const int side = u.side;
    if (static_cast<std::int32_t>(t.hitPoints) == u.health) {
        u.oldState = UnitState::Idle;
        if (side == mySide_) mentat(side, "UNITREPAIRED", false);
        unitAdjustState(u, UnitState::LeavingRepairPad);
        return;
    }
    int bs = 0, bi = 0;
    const Building* cb = buildingAt(u.blockFromX, u.blockFromY, bs, bi);
    if (cb && bs == side) {
        const std::uint32_t value = vars().unitRepairValue;
        std::uint32_t c = (unitCost(u.type, side) * value) / (t.hitPoints ? t.hitPoints : 1);
        c = (c & 0xfffffffeu) < 2 ? 1 : c >> 1;
        if (spend(side, static_cast<std::int32_t>(c))) {
            sides_[bs].buildings[bi].flags |= building_flags::kAnimOnce;
            u.health += static_cast<std::int32_t>(value);
            if (static_cast<std::int32_t>(t.hitPoints) < u.health) {
                u.health = static_cast<std::int32_t>(t.hitPoints);
            } else if (side == mySide_) {
                const std::uint32_t cr = static_cast<std::uint32_t>(sides_[side].cash + sides_[side].spice);
                if (cr == 0) {
                    if (!lowMoneyWarned_) {
                        lowMoneyWarned_ = true;
                        mentat(side, "LOWMONEY", true);
                    }
                } else if (cr > 99) {
                    lowMoneyWarned_ = false;
                }
            }
        }
        return;
    }
    unitAdjustState(u, UnitState::LeavingRepairPad);
}

// State 26 (case 0x18): drive off the pad to a free neighbour (LeavingBuilding); when boxed in,
// every 50 ticks a random neighbour (2 rand) that is a friendly unit is asked to move away.
void World::updateLeavingRepairPad(Unit& u) {
    if (u.animDelay != 0) --u.animDelay;
    int x = 0, y = 0;
    if (findFreeAdjacentSquare(u.blockFromX, u.blockFromY, u, u.side, x, y, 0)) {
        u.targetX = x;
        u.targetY = y;
        unitAdjustState(u, UnitState::LeavingBuilding);
        return;
    }
    if (u.animDelay != 0) return;
    const int nx = static_cast<std::uint8_t>(static_cast<std::uint32_t>(rng_.next()) % 3 + u.blockFromX - 1);
    const int ny = static_cast<std::uint8_t>(static_cast<std::uint32_t>(rng_.next()) % 3 + u.blockFromY - 1);
    if (!inBounds(nx, ny)) return;  // (the original reads an off-map cell there)
    int vs = 0, vi = 0;
    Unit* v = getUnitOnTile(nx << 5, ny << 5, vs, vi, false);
    if (v && v != &u && diplomacy(u.side, vs) == 0 && unitCanTakeOrders(v, false)) {
        unitAdjustState(*v, UnitState::MovingAway);
        u.animDelay = 0x32;
    }
}

// ---------------------------------------------------------------------------------------
// Palace weapons (ProcessOrder case 0x1d) and the Death Hand missile.

void World::specialWeapon(int side, int beh, int tileX, int tileY) {
    Side& S = sides_[side & 7];
    const auto& T = rules_.templates->bin;
    bool found = false;
    for (auto& q : S.unitQueue)
        if (q.type >= 0 && T.units[q.type].specialBehavior == (beh & 0xffff) && q.progress == kBuildDone) {
            q.type = -1;
            q.placing = false;
            found = true;
        }
    if (!found) return;
    if (beh == behaviour::kDeathHand) {
        const std::uint32_t acc = vars().deathHandAccuracy;
        const std::uint32_t m = acc * 2 ? acc * 2 : 1;  // (rand % 0 in the original with accuracy 0)
        int x = static_cast<int>(static_cast<std::uint32_t>(tileX & 0xffff) - static_cast<std::uint32_t>(rng_.next()) % m + acc);
        int y = static_cast<int>(static_cast<std::uint32_t>(tileY & 0xffff) - static_cast<std::uint32_t>(rng_.next()) % m + acc);
        x = std::clamp(x, 0, width_ - 1);
        y = std::clamp(y, 0, height_ - 1);
        launchDeathHand(side, x, y);
        return;
    }
    if (beh == behaviour::kOrnithopter) {
        int hx = 0, hy = 0;
        homeViewPosition(side, hx, hy);
        launchOrnithopters(side, (hx / 32) & 0xffff, (hy / 32) & 0xffff, tileX & 0xff, tileY & 0xff);
        return;
    }
    int sx = 0, sy = 0, tx = 0, ty = 0;
    if (beh == behaviour::kSaboteur) {
        const int type = static_cast<std::uint8_t>(T.groupIds[group_ids::kSaboteur]);
        if (findUnitExitBuilding(type, side, sx, sy, tx, ty, true)) addUnit(side, type, sx, sy, tx, ty, 0, 0);
    } else if (beh == behaviour::kFremen) {
        const int type = static_cast<std::uint8_t>(T.groupIds[group_ids::kFremen]);
        if (findUnitExitBuilding(type, side, sx, sy, tx, ty, true))
            for (int k = 0; k < vars().numberOfFremen; ++k) addUnit(side, type, sx, sy, tx, ty, 0, 0);
    }
}

// CSide_46CF10 (param_3 = 0): the view position the side's "home" key would jump to - the top-
// left pixel of a battlefield view centred on the primary construction yard (else the building
// with the lowest building group, else the last unit / the last MCV), clamped to the map. The
// view size is the original's default (kBattleFieldWidth/Height); ProcessOrder only uses it for
// the ornithopter entry point.
void World::homeViewPosition(int side, int& px, int& py) const {
    const Side& S = sides_[side & 7];
    const int maxX = width_ * 32 - kBattleFieldWidth - 1, maxY = height_ * 32 - kBattleFieldHeight - 1;
    const Building* b = nullptr;
    if (S.primaryBuilding[0] == kNoIndex) {
        for (int i = S.firstBuilding; i != -1; i = S.nextObject(i))
            if (!b || buildingTemplate(S.buildings[i]).buildingGroup < buildingTemplate(*b).buildingGroup) b = &S.buildings[i];
    } else {
        b = &S.buildings[static_cast<std::size_t>(S.primaryBuilding[0]) % kObjectsPerSide];
    }
    if (b) {
        // TImage +0xc (hotspot Y) of normal frame 0 when the building has one, else of the base
        // image. Without loaded art (tests): the template art height (= the uncropped hotspot).
        const ArtSize as = buildingImageSize(*b);
        const int imgH = as.yoff ? as.yoff : as.height;
        int x = b->posX / 65536 - kBattleFieldWidth / 2;
        int y = b->posY / 65536 - imgH / 2 - kBattleFieldHeight / 2;
        if (x < 1) x = 0;
        if (y < 1) y = 0;
        if (maxX <= x) x = maxX;
        if (maxY <= y) y = maxY;
        px = x;
        py = y;
        return;
    }
    const Unit* u = nullptr;
    for (int i = S.firstUnit; i != -1; i = S.nextObject(i))
        if (!u || unitTemplate(S.units[i]).specialBehavior == behaviour::kMcv) u = &S.units[i];
    if (u) {
        int x = u->posX / 65536 - kBattleFieldWidth / 2;
        int y = u->posY / 65536 - kBattleFieldHeight / 2;
        if (x < 1) x = 0;
        if (y < 1) y = 0;
        if (maxX <= x) x = maxX;
        if (maxY <= y) y = maxY;
        px = x;
        py = y;
        return;
    }
    px = 0;  // BattleFieldPosX/Y: the current view (no buildings, no units: nothing to strike with)
    py = 0;
}

// Ornithopter_LaunchStrike 0x49e770: three ornithopters (groupIds[18]) enter 4, 7 and 10 tiles
// outside the map edge nearest to the home position, 60 px high, with 12 shots each (+0x29),
// flags 0x10800 (delivery-style flyer that dies off the map, no slow-down), heading for the
// target in state 7. FlyUnit fires every 8th tick within 4 tiles and turns them away when empty.
void World::launchOrnithopters(int side, int homeX, int homeY, int tx, int ty) {
    const int type = static_cast<std::uint8_t>(rules_.templates->bin.groupIds[18]);
    for (int k = 0; k < 3; ++k) {
        const int idx = addUnit(side, type, 5, 5, 5, 5, 0, 0);
        if (idx < 0) continue;
        Unit& o = sides_[side & 7].units[static_cast<std::size_t>(idx)];
        int ex = homeX, ey = homeY;
        const int dir = carryallEntryPoint(ex, ey, k * 3);
        o.posX = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int16_t>(ex)) << 21);
        o.targetX = tx;
        o.posZ = 0x3c0000;
        o.posY = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int16_t>(ey)) << 21);
        o.targetY = ty;
        o.byte29 = 0xc;
        o.flags |= 0x10800;
        unitAdjustState(o, UnitState::Moving);
        o.facing = dir;
        o.facingTurret = dir;
    }
}

// FUN_0049e860: the missile (groupIds[19]) starts at the primary palace (+0x260aa, exit point 2
// as pixel offset), waits 0x14 ticks (flag 0x200000, byte 0x29), then flies to the target.
void World::launchDeathHand(int side, int tileX, int tileY) {
    Side& S = sides_[side & 7];
    const int pi = S.primaryBuilding[9];
    if (pi == kNoIndex) return;
    Building& pal = S.buildings[static_cast<std::size_t>(pi) % kObjectsPerSide];
    const auto& bt = buildingTemplate(pal);
    const int bx = pal.posX / 65536 / 32, by = pal.posY / 65536 / 32;
    const int type = static_cast<std::uint8_t>(rules_.templates->bin.groupIds[19]);
    const int ui = addUnit(side, type, bx, by, bx, by, bt.exitPoint2X, bt.exitPoint2Y);
    if (ui < 0) return;
    Unit& m = S.units[static_cast<std::size_t>(ui)];
    m.targetX = tileX & 0xff;
    m.targetY = tileY & 0xff;
    m.byte29 = 0x14;
    m.flags |= kNotDrawn;
    unitAdjustState(m, UnitState::Moving);
    pal.flags |= building_flags::kAnimOnce;
    sounds_.push_back(mentatEvent("DEATHLAUNCHED", true, 0, 1));  // every side hears it
}

// FUN_00494cd0: after the launch delay, smoke trail (groupIds[61]); straight up until 2 tiles
// above the map, then it turns around over the target column and comes down; over the target
// row it explodes once (flag 0x8000): death explosion + groupIds[62], and 40 bullets of its
// weapon in random directions (4 rand each: direction, delay, distance, fall speed).
bool World::deathHandFlight(Unit& u) {
    if (u.flags & kNotDrawn) {
        if (u.byte29 == 0) u.flags &= ~kNotDrawn;
        else --u.byte29;
        return true;
    }
    const auto& G = rules_.templates->bin.groupIds;
    const auto& t = unitTemplate(u);
    if (G[61] != -1) addExplosion(u.side, static_cast<std::uint8_t>(G[61]), u.posX / 65536, u.posY / 65536, u.posZ);
    int idx = static_cast<int>((u.speed & 0x7ffffff) >> 11) + (u.facing & 0xff) * 512;
    u.posX += sinTableFlat(idx);
    u.posY -= cosTableFlat(idx);
    if ((u.facing & 0xff) == 0) {
        if ((u.posY / 32) / 65536 < -2) {
            u.facing = 0x10;
            u.posX = static_cast<std::int32_t>(static_cast<std::uint32_t>(u.targetX & 0xff) << 21);
        }
        return true;
    }
    if (!(static_cast<std::int32_t>(static_cast<std::uint32_t>(u.targetY & 0xff) * 0x200000) <= u.posY) ||
        (u.flags & kFixedBarrel))
        return true;
    u.flags |= kFixedBarrel;  // 0x8000: exploded
    const int px = u.posX / 65536, py = u.posY / 65536;
    addExplosion(u.side, static_cast<std::uint8_t>(t.deathExplosion), px, py, u.posZ, explosion_flags::kDrift, 0,
                 sinTableFlat(idx), -cosTableFlat(idx));
    if (G[62] != -1) {
        idx = static_cast<int>((u.speed & 0x7ffffff) >> 11) + (u.facing & 0xff) * 512;
        addExplosion(u.side, static_cast<std::uint8_t>(G[62]), px, py, u.posZ, explosion_flags::kDrift, 0,
                     sinTableFlat(idx), -cosTableFlat(idx));
    }
    for (int k = 0; k < 40; ++k) {
        const std::uint8_t r1 = static_cast<std::uint8_t>(rng_.next());
        const std::uint8_t r2 = static_cast<std::uint8_t>(rng_.next());
        const std::uint8_t r3 = static_cast<std::uint8_t>(rng_.next());
        const int j = (r1 & 0x1f) * 512 + r3;
        const int bx = u.posX / 65536, by = u.posY / 65536;
        const int bi = addBullet(u.side, t.primaryWeapon, r2 & 7, u.index, bx, by, sinTableFlat(j) + bx,
                                 cosTableFlat(j) + by, kNoIndex, -1);
        if (bi >= 0) sides_[u.side].bullets[static_cast<std::size_t>(bi)].fallSpeed =
                         static_cast<std::int32_t>(static_cast<std::uint32_t>(rng_.next()) & 0xffff);
    }
    return false;
}

}  // namespace d2k::game

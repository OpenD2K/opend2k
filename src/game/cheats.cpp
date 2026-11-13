// Cheats (not part of the original game). Everything goes through the order queue
// (World::orders_), so a cheat is executed at the start of a tick like any player order:
// deterministic, replay- and save-safe. The state lives in World::cheats_ (CheatState) and is
// saved with the world. The mechanisms reused are the original ones: ModelAddUnit /
// ModelAddBuilding / ModelAddConcrete, carryall and starport-frigate deliveries
// (Delivery records, FUN_0046def0), RevealMap.

#include "game/world.hpp"

#include <algorithm>

namespace d2k::game {

using namespace cell_flags;

namespace {

bool isSpecialWeapon(int beh) {  // same set as production.cpp
    return beh == behaviour::kOrnithopter || beh == behaviour::kDeathHand || beh == behaviour::kSaboteur ||
           beh == behaviour::kFremen;
}
// Flyers / worms: nothing can carry them; a delivery spawns them directly at the drop point.
bool spawnsDirectly(int beh) {
    return beh == behaviour::kCarryall || beh == behaviour::kFrigate || beh == behaviour::kOrnithopter ||
           beh == behaviour::kDeathHand || beh == behaviour::kSandworm;
}
constexpr int kTripSize = 39;      // Delivery::units holds 40: 39 units + the 0xFF end marker (carryallDrop needs it)
constexpr int kMaxDeliver = 999;   // per order

}  // namespace

// ---------------------------------------------------------------------------------------
// Order generation (UI side)

bool World::orderCheatSetCredits(int side, std::int32_t credits) {
    if (side < 0 || side >= kMaxSides) return false;
    orders_.push_back({Order::CheatSetCredits, side, kNoIndex, std::max<std::int32_t>(credits, 0), 0});
    return true;
}

bool World::orderCheatAddCredits(int side, std::int32_t delta) {
    if (side < 0 || side >= kMaxSides) return false;
    orders_.push_back({Order::CheatAddCredits, side, kNoIndex, delta, 0});
    return true;
}

bool World::orderCheatTechLevel(int side, int level) {
    if (side < 0 || side >= kMaxSides || level < 0 || level > 9) return false;
    orders_.push_back({Order::CheatTechLevel, side, kNoIndex, level, 0});
    return true;
}

bool World::orderCheatBuildingOverride(int side, int type, BuildOverride mode) {
    if (side < 0 || side >= kMaxSides || type < 0 || type >= rules_.templates->bin.buildingCount ||
        type >= formats::kMaxBuildingTypes)
        return false;
    orders_.push_back({Order::CheatBuildingOverride, side, kNoIndex, type, static_cast<int>(mode)});
    return true;
}

bool World::orderCheatUnitOverride(int side, int type, BuildOverride mode) {
    if (side < 0 || side >= kMaxSides || type < 0 || type >= rules_.templates->bin.unitCount ||
        type >= formats::kMaxUnitTypes)
        return false;
    orders_.push_back({Order::CheatUnitOverride, side, kNoIndex, type, static_cast<int>(mode)});
    return true;
}

bool World::orderCheatResetOverrides(int side) {
    if (side < 0 || side >= kMaxSides) return false;
    orders_.push_back({Order::CheatResetOverrides, side, kNoIndex, 0, 0});
    return true;
}

bool World::orderCheatDeliver(int side, int type, int count) {
    if (side < 0 || side >= kMaxSides || type < 0 || type >= rules_.templates->bin.unitCount ||
        type >= formats::kMaxUnitTypes || count <= 0)
        return false;
    orders_.push_back({Order::CheatDeliver, side, kNoIndex, type, std::min(count, kMaxDeliver)});
    return true;
}

bool World::orderCheatRevealMap() {
    orders_.push_back({Order::CheatRevealMap, mySide_, kNoIndex, 0, 0});
    return true;
}

bool World::orderCheatInstantBuild(int side, bool on) {
    if (side < 0 || side >= kMaxSides) return false;
    orders_.push_back({Order::CheatInstantBuild, side, kNoIndex, on ? 1 : 0, 0});
    return true;
}

bool World::orderCheatSpawnUnit(int side, int type, int tileX, int tileY, int count) {
    if (side < 0 || side >= kMaxSides || type < 0 || type >= rules_.templates->bin.unitCount ||
        type >= formats::kMaxUnitTypes || !inBounds(tileX, tileY) || count <= 0)
        return false;
    Order o{Order::CheatSpawnUnit, side, std::min(count, kMaxDeliver), tileX, tileY};
    o.targetIndex = type;
    orders_.push_back(o);
    return true;
}

bool World::orderCheatSpawnBuilding(int side, int type, int tileX, int tileY, bool concrete) {
    if (side < 0 || side >= kMaxSides || !canCheatSpawnBuilding(type, tileX, tileY)) return false;
    Order o{Order::CheatSpawnBuilding, side, concrete ? 1 : 0, tileX, tileY};
    o.targetIndex = type;
    orders_.push_back(o);
    return true;
}

bool World::orderCheatReinforce(int side, const std::vector<std::uint8_t>& units, std::uint32_t delayTicks, int x, int y,
                                int edge) {
    if (side < 0 || side >= kMaxSides || units.empty() || units.size() > kMaxDeliver) return false;
    if (x >= 0 && !inBounds(x, y)) return false;
    for (std::uint8_t t : units)
        if (t >= rules_.templates->bin.unitCount || t >= formats::kMaxUnitTypes) return false;
    Order o{Order::CheatReinforce, side, std::clamp(edge, -1, 3), x < 0 ? -1 : x, x < 0 ? -1 : y};
    o.targetIndex = static_cast<int>(std::min<std::uint32_t>(delayTicks, 0x7fffffff));
    o.payload = units;
    orders_.push_back(std::move(o));
    return true;
}

bool World::orderCheatCancelReinforcement(int id) {
    orders_.push_back({Order::CheatCancelReinforcement, mySide_, kNoIndex, id, 0});
    return true;
}

// The footprint (all occupied tiles of the 4x4 template mask) must be on the map, below the
// never-buildable top row, and free of buildings, units and crates. Terrain and adjacency to
// the side's base are ignored. Concrete/walls: any free tile of the mask is enough.
bool World::canCheatSpawnBuilding(int type, int x, int y, std::array<bool, 16>* tileOk) const {
    if (tileOk) tileOk->fill(false);
    if (type < 0 || type >= rules_.templates->bin.buildingCount || type >= formats::kMaxBuildingTypes) return false;
    const auto& t = rules_.templates->bin.buildings[type];
    const bool concrete = t.specialBehavior == building_behaviour::kConcrete;
    const std::uint32_t mask = concrete ? t.tilesOccupiedSolid : t.tilesOccupiedAll;
    bool all = true, any = false;
    std::uint32_t bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            if (!(mask & bit)) continue;
            const int cx = x + c, cy = y + r;
            const bool ok = cx >= 0 && cy >= 1 && cx < width_ && cy < height_ &&
                            (cell(cx, cy).flags & (kOccupied | kCrate)) == 0 &&
                            (!concrete || !(cell(cx, cy).flags & kConcrete));
            if (tileOk) (*tileOk)[static_cast<std::size_t>(r * 4 + c)] = ok;
            all = all && ok;
            any = any || ok;
        }
    return concrete ? any : (all && mask != 0);
}

BuildOverride World::buildingOverride(int side, int type) const {
    if (side < 0 || side >= kMaxSides || type < 0 || type >= formats::kMaxBuildingTypes) return BuildOverride::Default;
    return static_cast<BuildOverride>(cheats_.buildingOverride[side][type]);
}

BuildOverride World::unitOverride(int side, int type) const {
    if (side < 0 || side >= kMaxSides || type < 0 || type >= formats::kMaxUnitTypes) return BuildOverride::Default;
    return static_cast<BuildOverride>(cheats_.unitOverride[side][type]);
}

int World::cheatDeliveriesPending(int side) const {
    return side >= 0 && side < kMaxSides ? static_cast<int>(cheats_.deliveryBacklog[side].size()) : 0;
}

// ---------------------------------------------------------------------------------------
// Execution (start of the tick)

void World::processCheatOrder(const Order& o) {
    Side& s = sides_[o.side];
    const auto& T = rules_.templates->bin;
    cheats_.used = true;
    switch (o.kind) {
    case Order::CheatSetCredits: {
        // credits() = CashReal + SpiceReal; the pending drips would change it later, so they go.
        const std::int32_t want = std::max(o.x, 0);
        s.spice = std::clamp(s.spice, 0, want);
        s.cash = want - s.spice;
        s.cashDrip = s.spiceDrip = 0;
        return;
    }
    case Order::CheatAddCredits: {
        const std::int64_t total = static_cast<std::int64_t>(s.cash) + s.spice + o.x;
        if (total < 0) s.cash = -s.spice;
        else s.cash = static_cast<std::int32_t>(std::min<std::int64_t>(total - s.spice, 0x7fffffff - s.spice));
        return;
    }
    case Order::CheatTechLevel:
        s.techLevel = o.x;
        updateIcons(o.side);
        return;
    case Order::CheatBuildingOverride:
        if (o.x >= 0 && o.x < formats::kMaxBuildingTypes && o.y >= 0 && o.y <= 2) {
            cheats_.buildingOverride[o.side][o.x] = static_cast<std::uint8_t>(o.y);
            updateIcons(o.side);
        }
        return;
    case Order::CheatUnitOverride:
        if (o.x >= 0 && o.x < formats::kMaxUnitTypes && o.y >= 0 && o.y <= 2) {
            cheats_.unitOverride[o.side][o.x] = static_cast<std::uint8_t>(o.y);
            updateIcons(o.side);
        }
        return;
    case Order::CheatResetOverrides:
        cheats_.buildingOverride[o.side].fill(0);
        cheats_.unitOverride[o.side].fill(0);
        updateIcons(o.side);
        return;
    case Order::CheatDeliver: {
        if (o.x < 0 || o.x >= T.unitCount) return;
        if (spawnsDirectly(T.units[o.x].specialBehavior)) {
            int x = 0, y = 0;
            cheatDeliveryTarget(o.side, x, y);
            for (int k = 0; k < o.y; ++k) addUnit(o.side, o.x, x, y, x, y, 0, 0);
            return;
        }
        auto& b = cheats_.deliveryBacklog[o.side];
        b.insert(b.end(), static_cast<std::size_t>(std::clamp(o.y, 0, kMaxDeliver)), static_cast<std::uint8_t>(o.x));
        return;
    }
    case Order::CheatRevealMap:
        revealMap();
        return;
    case Order::CheatReinforce: {
        ScheduledReinforcement r;
        r.id = cheats_.nextReinforcementId++;
        r.side = o.side;
        r.ordered = gameTicks_;
        r.due = gameTicks_ + static_cast<std::uint32_t>(std::max(o.targetIndex, 0));
        r.x = o.x;
        r.y = o.y;
        r.edge = o.unitIndex;
        for (std::uint8_t t : o.payload)
            if (t < T.unitCount) r.units.push_back(t);
        if (!r.units.empty()) cheats_.reinforcements.push_back(std::move(r));
        return;
    }
    case Order::CheatCancelReinforcement:
        std::erase_if(cheats_.reinforcements, [&](const ScheduledReinforcement& r) { return r.id == o.x; });
        return;
    case Order::CheatInstantBuild:
        cheats_.instantBuild[o.side] = o.x ? 1 : 0;
        return;
    case Order::CheatSpawnUnit: {
        const int type = o.targetIndex;
        if (type < 0 || type >= T.unitCount || !inBounds(o.x, o.y)) return;
        const bool air = spawnsDirectly(T.units[type].specialBehavior);
        for (int k = 0; k < o.unitIndex; ++k) {
            int x = o.x, y = o.y;
            const std::uint32_t f = cell(x, y).flags;
            if (!air && (f & (kOccupied | kDriveOn | kWalkOn)) != (kDriveOn | kWalkOn) && !nearestFreeTile(x, y, 15))
                return;  // no room left
            if (addUnit(o.side, type, x, y, x, y, 0, 0) < 0) return;  // pool full
        }
        return;
    }
    case Order::CheatSpawnBuilding: {
        const int type = o.targetIndex;
        if (!canCheatSpawnBuilding(type, o.x, o.y)) return;
        const auto& t = T.buildings[type];
        if (t.specialBehavior == building_behaviour::kConcrete) {
            addConcrete(o.side, type, o.x, o.y, t.tilesOccupiedSolid);
            if (o.side == mySide_) playNamed("S_BUILDCONC", 0, o.x, o.y);
        } else if (addBuilding(o.side, type, o.x, o.y, o.unitIndex != 0, false, false) >= 0) {
            if (o.side == mySide_) playNamed("S_BUILDUP", 0, o.x, o.y);
        }
        return;
    }
    default: return;
    }
}

// Where free deliveries land without a starport: a free tile near the side's base centre
// (CSide__FindBestBasePosition), else near its units, else the map centre.
bool World::cheatDeliveryTarget(int side, int& x, int& y) const {
    if (!findBestBasePosition(side, x, y)) {
        x = width_ / 2;
        y = height_ / 2;
    }
    x = std::clamp(x, 1, width_ - 2);
    y = std::clamp(y, 1, height_ - 2);
    const std::uint32_t f = cell(x, y).flags;
    if ((f & (kOccupied | kDriveOn | kWalkOn)) == (kDriveOn | kWalkOn)) return true;
    return nearestFreeTile(x, y, 20);
}

// Every tick after the orders: hand backlog units to free delivery slots, 39 per trip. With a
// primary starport they come by frigate (one frigate at a time, the delivery code waits for
// the previous one to leave), otherwise by carryall to a free tile near the base.
void World::updateCheats() {
    // Scheduled reinforcements: when due, carryall deliveries like the .MIS Reinforcement
    // event (startReinforcement), 25 units per carryall; units that find no free delivery slot
    // wait for the next tick. Air units / worms nothing can carry appear at the drop tile.
    const auto& T = rules_.templates->bin;
    for (std::size_t ri = 0; ri < cheats_.reinforcements.size();) {
        ScheduledReinforcement& r = cheats_.reinforcements[ri];
        if (gameTicks_ < r.due) {
            ++ri;
            continue;
        }
        int x = r.x, y = r.y;
        if (x < 0 || !inBounds(x, y)) cheatDeliveryTarget(r.side, x, y);
        std::vector<std::uint8_t> carried;
        for (std::uint8_t t : r.units) {
            if (spawnsDirectly(T.units[t].specialBehavior)) addUnit(r.side, t, x, y, x, y, 0, 0);
            else carried.push_back(t);
        }
        int trip = 0;
        std::size_t done = 0;
        while (done < carried.size()) {
            const int n = std::min<int>(25, static_cast<int>(carried.size() - done));
            int dx = x, dy = y;
            for (int k = 0; k < trip; ++k) {  // several carryalls: spread the drop tiles
                int nx = std::clamp(dx + 3, 1, width_ - 2), ny = dy;
                if (nearestFreeTile(nx, ny, 6)) dx = nx, dy = ny;
            }
            const int slot = startReinforcement(r.side, dx & 0xff, dy & 0xff, carried.data() + done, n, 0, 1);
            if (slot < 0) break;
            sides_[r.side].deliveries[slot].entryEdge = static_cast<std::int8_t>(r.edge);
            done += static_cast<std::size_t>(n);
            ++trip;
        }
        if (done >= carried.size()) {
            cheats_.reinforcements.erase(cheats_.reinforcements.begin() + static_cast<std::ptrdiff_t>(ri));
            continue;
        }
        r.units.assign(carried.begin() + static_cast<std::ptrdiff_t>(done), carried.end());  // rest: next tick
        r.x = x;
        r.y = y;
        ++ri;
    }
    for (int side = 0; side < kMaxSides; ++side) {
        auto& backlog = cheats_.deliveryBacklog[side];
        if (backlog.empty()) continue;
        Side& s = sides_[side];
        const bool starport = s.primaryBuilding[7] != kNoIndex;
        int trip = 0;
        while (!backlog.empty()) {
            int i = 0;
            while (i < 10 && s.deliveries[i].active) ++i;
            if (i >= 10) break;  // all slots busy: next tick
            Delivery& d = s.deliveries[i];
            d = Delivery{};
            d.active = true;
            d.time = gameTicks_;
            if (starport) {
                d.type = 2;  // drop point filled in from the starport when the frigate starts
            } else {
                int x = 0, y = 0;
                cheatDeliveryTarget(side, x, y);
                // Several carryalls at once: spread the drop points so they do not queue on one tile.
                for (int n = 0; n < trip; ++n) {
                    int nx = std::clamp(x + 3, 1, width_ - 2), ny = y;
                    if (nearestFreeTile(nx, ny, 6)) x = nx, y = ny;
                }
                d.type = 1;
                d.x = x & 0xff;
                d.y = y & 0xff;
            }
            const int n = std::min<int>(kTripSize, static_cast<int>(backlog.size()));
            d.units.fill(0xff);
            std::copy_n(backlog.begin(), n, d.units.begin());
            backlog.erase(backlog.begin(), backlog.begin() + n);
            ++trip;
        }
    }
}

// End of updateIcons: Blocked templates leave the sidebar lists, Allowed ones join them
// (the type itself, whatever its house, prerequisites or tech level).
void World::applyBuildOverrides(int side) {
    const auto& T = rules_.templates->bin;
    Side& s = sides_[side];
    const auto& bo = cheats_.buildingOverride[side];
    const auto& uo = cheats_.unitOverride[side];
    auto blocked = [](std::uint8_t m) { return m == static_cast<std::uint8_t>(BuildOverride::Blocked); };
    auto allowed = [](std::uint8_t m) { return m == static_cast<std::uint8_t>(BuildOverride::Allowed); };
    std::erase_if(s.buildingIcons, [&](int t) { return t >= 0 && t < formats::kMaxBuildingTypes && blocked(bo[t]); });
    std::erase_if(s.unitIcons, [&](int t) { return t >= 0 && t < formats::kMaxUnitTypes && blocked(uo[t]); });
    for (int t = 0; t < T.buildingCount && t < formats::kMaxBuildingTypes; ++t)
        if (allowed(bo[t]) && s.buildingIcons.size() < 60 &&
            std::find(s.buildingIcons.begin(), s.buildingIcons.end(), t) == s.buildingIcons.end())
            s.buildingIcons.push_back(t);
    for (int t = 0; t < T.unitCount && t < formats::kMaxUnitTypes; ++t)
        if (allowed(uo[t]) && s.unitIcons.size() < 60 &&
            std::find(s.unitIcons.begin(), s.unitIcons.end(), t) == s.unitIcons.end())
            s.unitIcons.push_back(t);
}

// updateProduction with no factory the unit can leave from: an Allowed unit is still built
// (template speed, difficulty-scaled, paid progressively) and arrives as a free delivery.
// Returns false to let the original drop the slot.
bool World::cheatProductionNoFactory(int side, ProductionSlot& q) {
    const auto& T = rules_.templates->bin;
    if (unitOverride(side, q.type) != BuildOverride::Allowed) return false;
    const auto& ut = T.units[q.type];
    if (isSpecialWeapon(ut.specialBehavior)) return false;
    if (q.onHold) return true;
    if (q.progress < kBuildDone) {
        std::uint32_t step = std::max<std::uint32_t>(1, (ut.buildSpeed * difficultySpeedPercent(side)) / 100);
        if (cheats_.instantBuild[side]) step = kBuildDone;
        if (!productionStep(side, q, step, static_cast<std::int32_t>(unitCost(q.type, side)), false) ||
            q.progress <= kBuildDone - 1)
            return true;
        q.progress = kBuildDone;
    }
    cheats_.deliveryBacklog[side].push_back(static_cast<std::uint8_t>(q.type));
    q.type = -1;
    mentat(side, "UNITREADY", true);
    return true;
}

}  // namespace d2k::game

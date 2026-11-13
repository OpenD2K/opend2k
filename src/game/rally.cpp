// Factory rally points (NOT ORIGINAL; the 1998 game has none). A player sets a tile for one of
// their factories (barracks, light / heavy factory, high-tech, WOR, starport); a unit that
// leaves that factory - or is dropped by the frigate at that starport - gets an ordinary move
// order (ProcessOrder case 0x13) to the tile once it has finished leaving, i.e. when it would
// otherwise stand idle. Everything is order-driven and saved (World::rallyPoints_ /
// rallyPending_), so it is deterministic. World::rallyPointsEnabled (the "Factory rally points"
// setting) off: the points stay but are not used.

#include "game/world.hpp"

#include <algorithm>

namespace d2k::game {

using namespace cell_flags;

namespace {
constexpr std::uint32_t kRallyTimeout = 0x400;  // ticks a unit may take to leave its factory
}  // namespace

// Factories: the buildings units leave from (SetBuildingAsPrimary slots 1-7: light / heavy
// factory, high-tech, the second high-tech kind, barracks, WOR, starport).
bool World::canHaveRallyPoint(int side, int buildingIndex) const {
    if (side < 0 || side >= kMaxSides || buildingIndex < 0 || buildingIndex >= kObjectsPerSide) return false;
    const Side& s = sides_[side];
    if (s.links[buildingIndex].kind != SlotKind::Building) return false;
    const Building& b = s.buildings[buildingIndex];
    if (!b.alive) return false;
    const int beh = buildingTemplate(b).specialBehavior;
    return beh >= building_behaviour::kLightFactory && beh <= building_behaviour::kStarport;
}

bool World::orderSetRallyPoint(int side, int buildingIndex, int tileX, int tileY) {
    if (!canHaveRallyPoint(side, buildingIndex) || !inBounds(tileX, tileY)) return false;
    orders_.push_back({Order::SetRallyPoint, side, buildingIndex, tileX, tileY});
    return true;
}

bool World::orderClearRallyPoint(int side, int buildingIndex) {
    if (side < 0 || side >= kMaxSides) return false;
    orders_.push_back({Order::SetRallyPoint, side, buildingIndex, -1, -1});
    return true;
}

bool World::rallyPoint(int side, int buildingIndex, int& tileX, int& tileY) const {
    if (!canHaveRallyPoint(side, buildingIndex)) return false;
    const Building& b = sides_[side].buildings[buildingIndex];
    for (const RallyPoint& p : rallyPoints_)
        if (p.side == side && p.building == buildingIndex && p.timeAdded == b.timeAdded) {
            tileX = p.x;
            tileY = p.y;
            return true;
        }
    return false;
}

void World::setRallyPoint(int side, int buildingIndex, int x, int y) {
    // Points of buildings that are gone (or whose slot holds a newer building) are dropped here.
    std::erase_if(rallyPoints_, [&](const RallyPoint& p) {
        if (p.side == side && p.building == buildingIndex) return true;
        return !canHaveRallyPoint(p.side, p.building) ||
               sides_[p.side].buildings[p.building].timeAdded != p.timeAdded;
    });
    if (x < 0 || !canHaveRallyPoint(side, buildingIndex) || !inBounds(x, y)) return;
    rallyPoints_.push_back({side, buildingIndex, sides_[side].buildings[buildingIndex].timeAdded, x, y});
}

void World::rallyUnitLeft(int side, int unitIndex, const Building& factory) {
    if (unitIndex < 0) return;
    int x = 0, y = 0;
    if (!rallyPoint(side, factory.index % kObjectsPerSide, x, y)) return;
    rallyPending_.push_back({side, unitIndex, gameTicks_, x, y});
}

void World::updateRally() {
    for (std::size_t i = 0; i < rallyPending_.size();) {
        const RallyPending p = rallyPending_[i];
        Unit* u = unit(p.side, p.unit);
        bool done = true;
        if (u && u->alive && u->state != UnitState::Dead && rallyPointsEnabled && gameTicks_ - p.since < kRallyTimeout) {
            switch (u->state) {
            case UnitState::LeavingBuilding:
            case UnitState::LeavingRepairPad:  // starport drop
            case UnitState::MovingAway:
                done = false;  // still on its way out
                break;
            case UnitState::Idle:
            case UnitState::Guarding:
                processOrder({Order::Move, p.side, p.unit, p.x, p.y});
                break;
            case UnitState::Harvesting:
            case UnitState::MovingToHarvest:  // harvesters only follow a rally point on spice
                if (cell(p.x, p.y).flags & kSpiceMask) processOrder({Order::Move, p.side, p.unit, p.x, p.y});
                break;
            default: break;  // given another order meanwhile
            }
        }
        if (done) rallyPending_.erase(rallyPending_.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
    }
}

}  // namespace d2k::game

// Attack-move (NOT ORIGINAL; the 1998 game has none - re/notes/combat.md §1 "Moving units").
// The player holds A and clicks the ground: the selected armed ground units walk there and fight
// whatever they meet on the way. In the original a unit in state 7 (Moving) only looks for targets
// while it waits for a blocked route cell (MoveUnit 0x494260 -> UnitAutoTarget) and never chases;
// an attack-move adds, on the unit's own check tick ((index & 7) == (ticks & 7), like Idle /
// Guarding), the original searches with the unit's weapon range:
//   1. FindNearestEnemy 0x4a4900 (ground units, start tile skipped, cloaked skipped),
//   2. the enemy that hit it within the last 8 ticks (Unit+0x32 attacker, LastAttacked == 0) when
//      it is no further than the leash - so a longer-ranged shooter is answered,
//   3. FindNearestEnemyBuilding 0x4a4fd0, turrets first, then any other (never walls),
// and puts the unit into the original attack states 4 / 5 (AttackUnit 0x496b00 chases and
// fires, AttackBuilding 0x496d10). When the target is gone the attack state ends in state 7 at
// the unit's own tile; the unit then resumes the walk. A chase that takes it more than
// kAttackMoveLeash tiles from where the engagement began is dropped. At the destination (state 2
// Idle) the unit is put on guard (ProcessOrder case 5: guard post = BlockTo).
//
// Everything is order-driven (World::orderAttackMove, executed at the start of the next tick)
// and the records are saved (World::attackMoves_), so it is deterministic: no float, no RNG
// beyond the original's own calls (FireAt's inaccuracy through AttackUnit). Nothing here runs
// while no unit is attack-moving, so games without it are unchanged.

#include "game/world.hpp"

#include <algorithm>

namespace d2k::game {

using namespace cell_flags;
using namespace unit_flags;

namespace {

bool isAircraft(int beh) {
    return beh == behaviour::kCarryall || beh == behaviour::kFrigate || beh == behaviour::kOrnithopter ||
           beh == behaviour::kDeathHand;
}

}  // namespace

void World::orderAttackMove(int side, int unitIndex, int tileX, int tileY) {
    if (!inBounds(tileX, tileY)) return;
    orders_.push_back({Order::AttackMove, side, unitIndex, tileX, tileY});
}

int World::orderGroupAttackMove(int side, int tileX, int tileY) {
    if (!inBounds(tileX, tileY)) return 0;
    const std::vector<int> group = orderGroup(side);  // max orderUnitLimit() (64 = the original)
    for (int i : group) orderAttackMove(side, i, tileX, tileY);
    return static_cast<int>(group.size());
}

bool World::isAttackMoving(int side, int unitIndex) const {
    return std::any_of(attackMoves_.begin(), attackMoves_.end(),
                       [&](const AttackMove& r) { return r.side == side && r.unit == unitIndex; });
}

void World::cancelAttackMove(int side, int unitIndex) {
    std::erase_if(attackMoves_, [&](const AttackMove& r) { return r.side == side && r.unit == unitIndex; });
}

void World::processAttackMoveOrder(const Order& o) {
    Unit* u = unit(o.side, o.unitIndex);
    if (!u || !inBounds(o.x, o.y)) return;
    cancelAttackMove(o.side, o.unitIndex);
    const auto& t = unitTemplate(*u);
    // Unarmed units (harvesters, MCVs, engineers ...) and aircraft: an ordinary move order.
    if (t.primaryWeapon == -1 || isAircraft(t.specialBehavior) || (u->flags & kFlying)) {
        processOrder({Order::Move, o.side, o.unitIndex, o.x, o.y});
        return;
    }
    // ProcessOrder case 0x13 for an armed unit.
    if (!unitCanTakeOrders(u, true) || !unitAdjustState(*u, UnitState::Moving)) return;
    u->oldState = UnitState::None;
    u->targetX = o.x;
    u->targetY = o.y;
    u->enemyIndex = kNoIndex;
    AttackMove r;
    r.side = o.side;
    r.unit = o.unitIndex;
    r.type = u->type;
    r.destX = o.x;
    r.destY = o.y;
    attackMoves_.push_back(r);
}

// The target searches of the check tick (see the top of the file). True = engaged.
bool World::attackMoveAcquire(Unit& u, AttackMove& r) {
    const auto& t = unitTemplate(u);
    const auto& W = rules_.templates->bin.weapons;
    std::uint32_t range = W[t.primaryWeapon].range;
    if (t.secondaryWeapon != -1 && range < W[t.secondaryWeapon].range) range = W[t.secondaryWeapon].range;
    const int cx = u.blockFromX * 32 + 16, cy = u.blockFromY * 32 + 16;
    auto engage = [&](UnitState st, int es, int ei) {
        if (!unitAdjustState(u, st)) return false;
        u.oldState = UnitState::None;
        u.enemySide = es;
        u.enemyIndex = ei;
        r.phase = 1;
        r.leashX = u.blockFromX;
        r.leashY = u.blockFromY;
        return true;
    };
    // 1. Ground units in weapon range (a fixed range: no flyer / worm search).
    int x = 0, y = 0, es = 0, ei = kNoIndex;
    if (findNearestEnemy(cx, cy, u.side, range, x, y, es, ei, t.primaryWeapon, t.secondaryWeapon, false, false, false)) {
        const Unit* e = unit(es, ei);
        if (e && !(e->flags & kExcluded) && unitTemplate(*e).specialBehavior != behaviour::kSandworm)
            return engage(UnitState::AttackingUnit, es, ei);
    }
    // 2. Whoever just hit it (LastAttacked counts ticks/8 since the last hit), within the leash.
    if (u.lastAttacked == 0 && u.attackerIndex != kNoIndex && u.attackerSide < kMaxSides &&
        diplomacy(u.side, u.attackerSide) == 1) {
        const int as = u.attackerSide, ai = u.attackerIndex;
        const int lim = kAttackMoveLeash * kAttackMoveLeash;
        if (const Unit* a = unit(as, ai)) {
            const int dx = a->blockFromX - u.blockFromX, dy = a->blockFromY - u.blockFromY;
            if (a->state != UnitState::Dead && !(a->flags & (kExcluded | kCloaked)) && dx * dx + dy * dy <= lim &&
                unitTemplate(*a).specialBehavior != behaviour::kSandworm)
                return engage(UnitState::AttackingUnit, as, ai);
        } else if (const Building* b = building(as, ai)) {
            const int dx = b->tileX - u.blockFromX, dy = b->tileY - u.blockFromY;
            if (b->state != 17 && dx * dx + dy * dy <= lim) return engage(UnitState::AttackingBuilding, as, ai);
        }
    }
    // 3. Enemy buildings in weapon range: turrets first, then any (FindNearestEnemyBuilding never
    // returns walls).
    for (bool turretsOnly : {true, false})
        if (findNearestEnemyBuilding(cx, cy, u.side, x, y, es, ei, t.primaryWeapon, t.secondaryWeapon, turretsOnly))
            return engage(UnitState::AttackingBuilding, es, ei);
    return false;
}

void World::updateAttackMoves() {
    for (std::size_t i = 0; i < attackMoves_.size();) {
        AttackMove& r = attackMoves_[i];
        Unit* u = unit(r.side, r.unit);
        bool keep = u && u->alive && u->state != UnitState::Dead && u->type == r.type;
        if (keep) {
            auto resume = [&] {
                if (!unitAdjustState(*u, UnitState::Moving)) {
                    keep = false;
                    return;
                }
                u->oldState = UnitState::None;
                u->targetX = r.destX;
                u->targetY = r.destY;
                u->enemyIndex = kNoIndex;
                r.phase = 0;
            };
            const bool onSlot = static_cast<std::uint32_t>(u->index & 7) == (gameTicks_ & 7);
            switch (u->state) {
            case UnitState::Moving:
                if (r.phase == 1) resume();  // the target is gone (attack state ended at its tile)
                else if (onSlot && !unitIsHiddenStealth(*u)) attackMoveAcquire(*u, r);
                break;
            case UnitState::AttackingTileOnRoute:  // the original's own detours (state 10 / 9)
            case UnitState::MovingAway:
                break;
            case UnitState::AttackingUnit:
            case UnitState::AttackingBuilding: {
                if (r.phase == 0) {
                    r.phase = 1;
                    r.leashX = u->blockFromX;
                    r.leashY = u->blockFromY;
                }
                const int dx = u->blockFromX - r.leashX, dy = u->blockFromY - r.leashY;
                if (dx * dx + dy * dy > kAttackMoveLeash * kAttackMoveLeash) resume();  // chased too far
                break;
            }
            case UnitState::Idle:  // arrived (or the route gave up): guard there, ProcessOrder case 5
                if (unitAdjustState(*u, UnitState::Guarding)) {
                    u->oldState = UnitState::None;
                    u->rememberX = u->blockToX;
                    u->rememberY = u->blockToY;
                    u->enemyIndex = kNoIndex;
                }
                keep = false;
                break;
            default:  // guarding already, carried, repairing ...: no longer attack-moving
                keep = false;
                break;
            }
        }
        if (keep) ++i;
        else attackMoves_.erase(attackMoves_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

}  // namespace d2k::game

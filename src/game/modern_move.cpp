// "Unit movement: Modern" (not original; docs/modern-movement.md). A free-angle mover for ground
// vehicles that replaces MoveUnit 0x494260 for them when World::modernMovement() is on:
//  - positions stay the 16.16 pixels of the original; the body has a fine heading (65536 per
//    turn) that turns at the original rate (one 32nd per RotationSpeed + 1 ticks) while the
//    vehicle drives; speed accelerates / brakes towards the original speed of the cell it is on
//    (SPEED.BIN terrain factor, concrete, the damage rule of MoveUnit: StepSpeed);
//  - paths: an 8-way A* over the static passability (terrain, buildings, walls; occupied cells
//    only cost more), string-pulled with a line-of-sight test that keeps ~10 px clear of
//    blocked cells, so vehicles drive straight lines at any angle;
//  - no tile reservation ahead: a vehicle owns only the cell under its centre (the original
//    cell bits, so targeting, crushing, building placement and the infantry's movement work
//    unchanged), brakes for vehicles ahead / occupied cells, follows the vehicle in front,
//    asks idle friends to make room, re-plans round blockers, breaks head-on meetings with a
//    fixed tie-break (one pulls over off the other's lane and waits); a group move order gives every
//    vehicle its own tile round the target (applyModernFormations), and a vehicle whose goal
//    tile is taken near the end picks the nearest free one;
//  - the attack states brake to a stop as soon as their target is in range (the original fires
//    only from a tile centre, posSteps == 0: here = standing still), the turret aims at the
//    target while the hull drives.
// Pure integer maths, fixed iteration orders: deterministic like the rest of the model.

#include "game/geometry.hpp"
#include "game/world.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <queue>

namespace d2k::game {

using namespace cell_flags;
using namespace unit_flags;

namespace {

// sin over a quarter turn, 1024 steps, Q16 (computed at compile time with a Q30 Taylor series:
// no floating point anywhere).
constexpr std::int64_t mulQ30(std::int64_t a, std::int64_t b) { return (a * b) / (std::int64_t(1) << 30); }
constexpr std::array<std::int32_t, 1025> makeSinQuarter() {
    std::array<std::int32_t, 1025> t{};
    constexpr std::int64_t kPiQ30 = 3373259426ll;  // pi * 2^30
    for (int a = 0; a <= 1024; ++a) {
        const std::int64_t x = kPiQ30 * a / 2048;
        const std::int64_t x2 = mulQ30(x, x);
        std::int64_t term = x, sum = x;
        for (int n = 1; n <= 8; ++n) {
            term = -mulQ30(term, x2) / ((2 * n) * (2 * n + 1));
            sum += term;
        }
        t[std::size_t(a)] = static_cast<std::int32_t>((sum + (1 << 13)) >> 14);
    }
    return t;
}
constexpr std::array<std::int32_t, 1025> kSinQ = makeSinQuarter();
static_assert(kSinQ[0] == 0 && kSinQ[1024] == 65536 && kSinQ[512] == 46341);

// sin / cos of a heading (65536 per turn), Q16.
std::int32_t sinH(std::int32_t h) {
    const int i = (h >> 4) & 4095, r = i & 1023;
    switch (i >> 10) {
    case 0: return kSinQ[std::size_t(r)];
    case 1: return kSinQ[std::size_t(1024 - r)];
    case 2: return -kSinQ[std::size_t(r)];
    default: return -kSinQ[std::size_t(1024 - r)];
    }
}
std::int32_t cosH(std::int32_t h) { return sinH(h + 16384); }

// Signed heading difference in -32768..32767.
int wrapH(int d) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(d & 0xFFFF)); }

std::uint64_t isqrt64(std::uint64_t v) {
    std::uint64_t r = 0, bit = std::uint64_t(1) << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

std::int32_t dist16(std::int32_t dx, std::int32_t dy) {
    return static_cast<std::int32_t>(isqrt64(std::uint64_t(std::int64_t(dx) * dx + std::int64_t(dy) * dy)));
}

// Heading (65536 per turn) of the vector (dx, dy) in 16.16 px (y down): the original's GetFacing
// convention through Atan4096, both components scaled into its 15-bit input range.
int headingOf(std::int32_t dx, std::int32_t dy) {
    std::int64_t a = -std::int64_t(dx), b = dy;
    while (a > 32767 || a < -32767 || b > 32767 || b < -32767) {
        a /= 2;
        b /= 2;
    }
    return (atan4096(static_cast<int>(a), static_cast<int>(b)) << 4) & 0xFFFF;
}

// Speed reachable when braking over `dist` (both 16.16) with deceleration `decel` (16.16 per tick).
std::int32_t brakeSpeed(std::int32_t dist, std::int32_t decel) {
    if (dist <= 0) return 0;
    return static_cast<std::int32_t>(isqrt64(std::uint64_t(2) * std::uint64_t(decel) * std::uint64_t(dist)));
}

constexpr std::int32_t kClearPx = 10;                // line of sight keeps this far from blocked cells
constexpr std::int32_t kFollowGap = toFix(26);       // centre distance kept to the vehicle ahead
constexpr int kDeg = 65536 / 360;                    // heading units per degree (182)

std::int32_t centreOf(int tile) { return tileCentre(tile); }

}  // namespace

// ---------------------------------------------------------------------------------------
// Queries

bool World::isModernVehicle(const Unit& u) const {
    if (!modernMovement_) return false;
    const auto& t = unitTemplate(u);
    if (t.isInfantry) return false;
    switch (t.specialBehavior) {
    case behaviour::kCarryall: case behaviour::kSandworm: case behaviour::kFrigate: case behaviour::kOrnithopter:
    case behaviour::kDeathHand: case behaviour::kBird: return false;
    default: break;
    }
    return (u.flags & (kFlying | kDeathHand)) == 0;
}

// The cell rules of CanUnitUseSquare 0x45d990 for a vehicle without the units: terrain the
// vehicle may drive on, no building or wall, except the own building exit / dock tiles and the
// target building of the repair-pad / attack states.
bool World::modernStaticPass(int x, int y, const Unit& u) const {
    if (!inBounds(x, y)) return false;
    const std::uint32_t c = cell(x, y).flags;
    const int own = static_cast<int>(c & kOwnerMask);
    if ((c & kBuilding) && (c & kExitTile) && own == u.side) return true;
    if (c & kBuilding) {
        World& w = const_cast<World&>(*this);
        if (u.state == UnitState::MovingToRepairPad && own == u.side) {
            const Building* b = w.building(u.side, u.repairPadIndex);
            if (b && buildingOccupiesTile(*b, x, y)) return true;
        } else if (u.state == UnitState::AttackingBuilding) {
            const Building* b = w.building(u.enemySide, u.enemyIndex);
            if (b && buildingOccupiesTile(*b, x, y)) return true;
        }
        return false;
    }
    return (c & kDriveOn) != 0;
}

// Straight line a -> b (16.16 px) with kClearPx of clearance on every side passes only cells
// the vehicle may drive on.
bool World::modernLineClear(const Unit& u, std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by) const {
    const std::int64_t dx = std::int64_t(bx) - ax, dy = std::int64_t(by) - ay;
    const std::int64_t len = std::max(std::llabs(dx), std::llabs(dy)) >> 16;
    const int n = static_cast<int>(len / 6) + 1;
    int lastX = -1000, lastY = -1000;
    // A plan round the units in the way (kAvoidUnits) keeps its lines off their cells too.
    const bool avoid = (u.mm.flags & ModernMotion::kAvoidUnits) != 0;
    auto pass = [&](int cx, int cy) {
        if (!modernStaticPass(cx, cy, u)) return false;
        if (avoid && (cx != u.blockFromX || cy != u.blockFromY) && (cell(cx, cy).flags & kAnyUnit) &&
            chebyshev(cx, cy, u.blockFromX, u.blockFromY) <= 4)
            return false;
        return true;
    };
    for (int i = 0; i <= n; ++i) {
        const int px = static_cast<int>((ax + dx * i / n) >> 16), py = static_cast<int>((ay + dy * i / n) >> 16);
        for (int k = 0; k < 4; ++k) {
            const int cx = (px + ((k & 1) ? kClearPx : -kClearPx)) >> 5, cy = (py + ((k & 2) ? kClearPx : -kClearPx)) >> 5;
            if (cx == lastX && cy == lastY) continue;
            if (!pass(cx, cy)) return false;
            if (k == 3) lastX = cx, lastY = cy;
        }
    }
    return true;
}

// The vehicle whose centre is on cell (x, y) (vehicleAt_, rebuilt at every tick start and kept up
// to date by modernEnterCell), or null.
const Unit* World::modernVehicleAt(int x, int y) const {
    if (!inBounds(x, y) || vehicleAt_.size() != cells_.size()) return nullptr;
    const std::int32_t v = vehicleAt_[std::size_t(y) * std::size_t(width_) + std::size_t(x)];
    if (v <= 0) return nullptr;
    const int s = (v - 1) >> 16, i = (v - 1) & 0xFFFF;
    if (s < 0 || s >= kMaxSides || i >= int(sides_[s].units.size())) return nullptr;
    const Unit& u = sides_[s].units[std::size_t(i)];
    if (!u.alive || u.state == UnitState::Dead || (u.flags & kExcluded) || u.blockFromX != x || u.blockFromY != y)
        return nullptr;
    return &u;
}

void World::modernIndexVehicles() {
    vehicleAt_.assign(cells_.size(), 0);
    vehicleAtTick_ = gameTicks_;
    for (int s = 0; s < kMaxSides; ++s) {
        const Side& side = sides_[s];
        for (int i = side.firstUnit; i != -1; i = side.nextObject(i)) {
            const Unit& u = side.units[std::size_t(i)];
            if (!isModernVehicle(u) || (u.flags & kExcluded) || u.state == UnitState::Dead || !inBounds(u.blockFromX, u.blockFromY))
                continue;
            vehicleAt_[std::size_t(u.blockFromY) * std::size_t(width_) + std::size_t(u.blockFromX)] = ((s << 16) | i) + 1;
        }
    }
}

// The attack states stop as soon as the target is in range (they fire only standing).
bool World::modernWantsStop(const Unit& u) const {
    World& w = const_cast<World&>(*this);
    switch (u.state) {
    case UnitState::AttackingUnit: {
        const Unit* e = u.enemySide >= 0 && u.enemySide < kMaxSides ? w.unit(u.enemySide, u.enemyIndex) : nullptr;
        return e && e->state != UnitState::Dead && canUnitAttackTile(u, e->blockFromX, e->blockFromY, false);
    }
    case UnitState::AttackingBuilding: {
        const Building* b = u.enemySide >= 0 && u.enemySide < kMaxSides ? w.building(u.enemySide, u.enemyIndex) : nullptr;
        if (!b || b->state == 17) return false;
        int x = 0, y = 0;
        closestBuildingTile(u.blockFromX, u.blockFromY, *b, x, y);
        return canUnitAttackTile(u, x, y, false);
    }
    case UnitState::AttackingTile:
    case UnitState::AttackingTileOnRoute:
        return canUnitAttackTile(u, u.targetX, u.targetY, false) &&
               !(u.blockFromX == u.targetX && u.blockFromY == u.targetY);
    default: return false;
    }
}

// ---------------------------------------------------------------------------------------
// Planning

// A* (8 neighbours, octile costs, no corner cutting) from the vehicle's cell to its target over
// modernStaticPass; cells holding a vehicle cost more (kAvoidUnits: those near the start are
// closed). An unreachable target becomes the closest reached cell. The cell path is then
// string-pulled into at most kModernWaypoints corners. Returns false when there is no step to
// take (the vehicle stays: target = its cell).
bool World::modernPlan(Unit& u) {
    ModernMotion& m = u.mm;
    const int W = width_, H = height_;
    const std::size_t N = std::size_t(W) * std::size_t(H);
    if (astarG_.size() != N) {
        astarG_.assign(N, 0);
        astarFrom_.assign(N, -1);
        astarSeen_.assign(N, 0);
        astarStamp_ = 0;
    }
    if (++astarStamp_ == 0) {
        std::fill(astarSeen_.begin(), astarSeen_.end(), 0u);
        astarStamp_ = 1;
    }
    const int sx = u.blockFromX, sy = u.blockFromY;
    const int tx = std::clamp(u.targetX, 0, W - 1), ty = std::clamp(u.targetY, 0, H - 1);
    const int start = sy * W + sx, goal = ty * W + tx;
    const bool avoid = (m.flags & ModernMotion::kAvoidUnits) != 0;
    auto heur = [&](int c) {
        const int dx = std::abs(c % W - tx), dy = std::abs(c / W - ty);
        return 10 * std::max(dx, dy) + 4 * std::min(dx, dy);
    };
    // Min-heap on (f, h, cell): unique keys, so the order never depends on the heap's internals.
    std::priority_queue<std::uint64_t, std::vector<std::uint64_t>, std::greater<std::uint64_t>> open;
    auto push = [&](int c, int g) {
        const int h = heur(c);
        open.push((std::uint64_t(std::uint32_t(g + h)) << 40) | (std::uint64_t(std::uint32_t(h)) << 16) | std::uint64_t(c));
    };
    astarSeen_[std::size_t(start)] = astarStamp_;
    astarG_[std::size_t(start)] = 0;
    astarFrom_[std::size_t(start)] = -1;
    push(start, 0);
    int best = start, bestH = heur(start), bestG = 0, end = -1;
    static constexpr int kDx[8] = {0, 1, 0, -1, 1, 1, -1, -1};
    static constexpr int kDy[8] = {-1, 0, 1, 0, -1, 1, 1, -1};
    while (!open.empty()) {
        const std::uint64_t key = open.top();
        open.pop();
        const int cc = static_cast<int>(key & 0xFFFF);
        const int f = static_cast<int>(key >> 40);
        const int g = astarG_[std::size_t(cc)];
        if (g + heur(cc) != f) continue;  // a stale entry
        const int h = heur(cc);
        if (h < bestH || (h == bestH && g < bestG)) best = cc, bestH = h, bestG = g;
        if (cc == goal) {
            end = goal;
            break;
        }
        const int cx = cc % W, cy = cc / W;
        // Open for the search: static passability; with kAvoidUnits the cells holding a unit near
        // the start are closed (diagonal steps may not cut past them either).
        auto open = [&](int x, int y) {
            if (!inBounds(x, y) || !modernStaticPass(x, y, u)) return false;
            return !(avoid && (x != sx || y != sy) && (x != tx || y != ty) && (cells_[std::size_t(y * W + x)].flags & kAnyUnit) &&
                     chebyshev(x, y, sx, sy) <= 4);
        };
        for (int d = 0; d < 8; ++d) {
            const int nx = cx + kDx[d], ny = cy + kDy[d];
            if (!open(nx, ny)) continue;
            if (d >= 4 && (!open(cx + kDx[d], cy) || !open(cx, cy + kDy[d]))) continue;
            const int n = ny * W + nx;
            int cost = d >= 4 ? 14 : 10;
            const std::uint32_t nf = cells_[std::size_t(n)].flags;
            if ((nf & kAnyUnit) && n != goal) cost += (nf & kVehicle) ? 16 : 6;
            const int ng = g + cost;
            if (astarSeen_[std::size_t(n)] == astarStamp_ && astarG_[std::size_t(n)] <= ng) continue;
            astarSeen_[std::size_t(n)] = astarStamp_;
            astarG_[std::size_t(n)] = ng;
            astarFrom_[std::size_t(n)] = cc;
            push(n, ng);
        }
    }
    if (end < 0) {
        if (avoid) {  // closed in by the units round it: plan through them (they move on)
            m.flags &= static_cast<std::uint8_t>(~ModernMotion::kAvoidUnits);
            return modernPlan(u);
        }
        end = best;  // unreachable: the closest cell reached
    }
    m.wpCount = m.wpIndex = 0;
    m.flags &= static_cast<std::uint8_t>(~(ModernMotion::kPartial | ModernMotion::kYield));
    u.targetX = end % W;
    u.targetY = end / W;
    m.goalTileX = static_cast<std::int16_t>(u.targetX);
    m.goalTileY = static_cast<std::int16_t>(u.targetY);
    m.goalX = centreOf(u.targetX);
    m.goalY = centreOf(u.targetY);
    if (end == start) return false;
    // The cell path, start excluded.
    std::vector<int> path;
    for (int c = end; c != start && c >= 0; c = astarFrom_[std::size_t(c)]) path.push_back(c);
    std::reverse(path.begin(), path.end());
    // String pulling: from the anchor, the farthest path cell in sight (doubling, then halving),
    // that cell becomes a corner and the next anchor.
    std::int32_t ax = u.posX, ay = u.posY;
    const int len = static_cast<int>(path.size());
    auto cx = [&](int i) { return centreOf(path[std::size_t(i)] % W); };
    auto cy = [&](int i) { return centreOf(path[std::size_t(i)] / W); };
    int i = 0;
    while (i < len) {
        int ok = i, step = 1, bad = len;
        while (ok + step < len) {
            const int k = ok + step;
            if (modernLineClear(u, ax, ay, cx(k), cy(k))) {
                ok = k;
                step *= 2;
            } else {
                bad = k;
                break;
            }
        }
        // Halve between the last cell in sight and the first one not.
        int lo = ok, hi = bad;
        while (hi - lo > 1) {
            const int mid = (lo + hi) / 2;
            if (modernLineClear(u, ax, ay, cx(mid), cy(mid))) lo = mid;
            else hi = mid;
        }
        ok = lo;
        if (m.wpCount == kModernWaypoints) {
            m.flags |= ModernMotion::kPartial;
            break;
        }
        m.wpX[m.wpCount] = static_cast<std::uint8_t>(path[std::size_t(ok)] % W);
        m.wpY[m.wpCount] = static_cast<std::uint8_t>(path[std::size_t(ok)] / W);
        ++m.wpCount;
        ax = cx(ok);
        ay = cy(ok);
        i = ok + 1;
    }
    return true;
}

// The goal tile is taken (a vehicle standing on it, a building put there): the nearest free tile
// round it (rings out to 4) that no other vehicle of the side is heading for.
bool World::modernPickFreeGoal(Unit& u) {
    const int tx = u.targetX, ty = u.targetY;
    const Side& side = sides_[u.side];
    auto claimed = [&](int x, int y) {
        for (int i = side.firstUnit; i != -1; i = side.nextObject(i)) {
            const Unit& v = side.units[std::size_t(i)];
            if (&v != &u && v.posSteps != 0 && v.targetX == x && v.targetY == y && isModernVehicle(v)) return true;
        }
        return false;
    };
    for (int r = 1; r <= 4; ++r) {
        int bx = -1, by = -1;
        std::int64_t bd = 0;
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const int x = tx + dx, y = ty + dy;
                if (!modernStaticPass(x, y, u) || (cell(x, y).flags & (kAnyUnit | kBuilding)) || claimed(x, y)) continue;
                const std::int64_t ddx = std::int64_t(centreOf(x) - u.posX) >> 8, ddy = std::int64_t(centreOf(y) - u.posY) >> 8;
                const std::int64_t d = ddx * ddx + ddy * ddy;
                if (bx < 0 || d < bd) bx = x, by = y, bd = d;
            }
        if (bx >= 0) {
            u.targetX = bx;
            u.targetY = by;
            return true;
        }
    }
    return false;
}

// Head-on meetings (two vehicles each stopped by the other). Without a tie-break both re-plan round
// each other on the same tick with the same search, pick the same side, turn back towards each
// other and meet again - forever (owner report 2026-11-19). The one that yields pulls over to a
// free cell off the other's lane and waits there; the other holds without re-planning.

// States whose target stays put while the vehicle drives (the plan to it may be replaced by a
// pull-over detour); the attack states retarget every tick and keep the right of way.
bool World::modernCanYield(const Unit& u) const {
    switch (u.state) {
    case UnitState::Moving: case UnitState::MovingAway: case UnitState::Guarding: case UnitState::Idle:
    case UnitState::MovingToHarvest: case UnitState::MovingToRefinery: case UnitState::Wandering:
        return true;
    default: return false;
    }
}

// Another vehicle within a tile of this one is driving (the one we pulled over for still passing).
bool World::modernMovingVehicleNear(const Unit& u) const {
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const Unit* v = modernVehicleAt(u.blockFromX + dx, u.blockFromY + dy);
            if (v && v != &u && v->mm.speed > 0) return true;
        }
    return false;
}

// Pull over for `v`: the nearest free cell (breadth-first over free drivable cells, up to 8 steps,
// fixed neighbour order) at least ~28 px off v's lane (its position through its remaining corners)
// that no other vehicle of the side is heading for. The path to it becomes a partial plan flagged
// kYield (the target and goalTile stay, so no re-plan replaces it). False when there is none.
bool World::modernPullOver(Unit& u, const Unit& v) {
    constexpr int R = 8, D = 2 * R + 1;
    // v's lane in whole px.
    std::vector<std::pair<std::int64_t, std::int64_t>> lane;
    lane.push_back({v.posX >> 16, v.posY >> 16});
    for (int i = v.mm.wpIndex; i < v.mm.wpCount; ++i) lane.push_back({v.mm.wpX[i] * 32 + 16, v.mm.wpY[i] * 32 + 16});
    if (lane.size() == 1) lane.push_back({u.posX >> 16, u.posY >> 16});
    {  // and on past us in v's direction (its plan may end short)
        const auto& [lx, ly] = lane[lane.size() - 2];
        const auto& [ex, ey] = lane.back();
        const std::int64_t ddx = ex - lx, ddy = ey - ly, len = std::max<std::int64_t>(1, std::max(std::llabs(ddx), std::llabs(ddy)));
        if (lane.size() == 2) lane.push_back({ex + ddx * 64 / len, ey + ddy * 64 / len});
    }
    auto onLane = [&](int x, int y) {
        const std::int64_t px = x * 32 + 16, py = y * 32 + 16;
        for (std::size_t k = 0; k + 1 < lane.size(); ++k) {
            const std::int64_t ax = lane[k].first, ay = lane[k].second, bx = lane[k + 1].first, by = lane[k + 1].second;
            const std::int64_t ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey;
            std::int64_t qx = ax, qy = ay;
            if (l2 > 0) {
                const std::int64_t tq = std::clamp<std::int64_t>((px - ax) * ex + (py - ay) * ey, 0, l2);
                qx = ax + ex * tq / l2;
                qy = ay + ey * tq / l2;
            }
            if ((px - qx) * (px - qx) + (py - qy) * (py - qy) < 28 * 28) return true;
        }
        return false;
    };
    const Side& side = sides_[u.side];
    auto claimed = [&](int x, int y) {
        for (int i = side.firstUnit; i != -1; i = side.nextObject(i)) {
            const Unit& o = side.units[std::size_t(i)];
            if (&o != &u && o.targetX == x && o.targetY == y && isModernVehicle(o)) return true;
        }
        return false;
    };
    const int sx = u.blockFromX, sy = u.blockFromY;
    std::array<std::int16_t, D * D> from;
    from.fill(-2);
    auto idx = [&](int x, int y) { return (y - sy + R) * D + (x - sx + R); };
    auto free = [&](int x, int y) {
        return std::abs(x - sx) <= R && std::abs(y - sy) <= R && modernStaticPass(x, y, u) && !(cell(x, y).flags & kAnyUnit);
    };
    static constexpr int kDx[8] = {0, 1, 0, -1, 1, 1, -1, -1};
    static constexpr int kDy[8] = {-1, 0, 1, 0, -1, 1, 1, -1};
    std::array<std::int16_t, D * D> queue;
    std::array<std::uint8_t, D * D> depth{};
    int head = 0, tail = 0, found = -1;
    from[std::size_t(idx(sx, sy))] = -1;
    queue[std::size_t(tail++)] = static_cast<std::int16_t>(idx(sx, sy));
    while (head < tail && found < 0) {
        const int c = queue[std::size_t(head++)];
        const int cx = sx - R + c % D, cy = sy - R + c / D;
        if (depth[std::size_t(c)] >= R) continue;
        for (int d = 0; d < 8; ++d) {
            const int nx = cx + kDx[d], ny = cy + kDy[d];
            if (!free(nx, ny) || from[std::size_t(idx(nx, ny))] != -2) continue;
            if (d >= 4 && (!free(cx + kDx[d], cy) || !free(cx, cy + kDy[d]))) continue;
            const int n = idx(nx, ny);
            from[std::size_t(n)] = static_cast<std::int16_t>(c);
            depth[std::size_t(n)] = static_cast<std::uint8_t>(depth[std::size_t(c)] + 1);
            if (!onLane(nx, ny) && !claimed(nx, ny)) {
                found = n;
                break;
            }
            queue[std::size_t(tail++)] = static_cast<std::int16_t>(n);
        }
    }
    if (found < 0) return false;
    int path[R + 1], len = 0;
    for (int c = found; c != idx(sx, sy) && len <= R; c = from[std::size_t(c)]) path[len++] = c;
    ModernMotion& m = u.mm;
    m.wpCount = static_cast<std::uint8_t>(len);
    m.wpIndex = 0;
    for (int k = 0; k < len; ++k) {
        const int c = path[len - 1 - k];
        m.wpX[std::size_t(k)] = static_cast<std::uint8_t>(sx - R + c % D);
        m.wpY[std::size_t(k)] = static_cast<std::uint8_t>(sy - R + c / D);
    }
    m.flags = static_cast<std::uint8_t>((m.flags | ModernMotion::kPartial | ModernMotion::kYield) &
                                        ~(ModernMotion::kAvoidUnits | ModernMotion::kStopping));
    m.stall = 0;
    return true;
}

// The vehicle's centre moved into cell (nx, ny): the original cell bookkeeping of MoveUnit's
// reservation + halfway handover in one go (vehicle bit and owner on the new cell, the old
// cell's owner back to its infantry), and the crate pickup of entering a cell.
void World::modernEnterCell(Unit& u, int nx, int ny) {
    std::uint32_t& from = cell(u.blockFromX, u.blockFromY).flags;
    if (!modernVehicleAt(u.blockFromX, u.blockFromY) || modernVehicleAt(u.blockFromX, u.blockFromY) == &u) {
        from &= ~kVehicle;
        if (from & kInfantryMask) from = (from & ~kOwnerMask) | ((from >> kInfantryOwnerShift) & 7);
        if (vehicleAt_.size() == cells_.size())
            vehicleAt_[std::size_t(u.blockFromY) * std::size_t(width_) + std::size_t(u.blockFromX)] = 0;
    }
    std::uint32_t& to = cell(nx, ny).flags;
    to = (to & ~kOwnerMask) | static_cast<std::uint32_t>(u.side) | kVehicle;
    u.blockFromX = u.blockToX = nx;
    u.blockFromY = u.blockToY = ny;
    if (vehicleAt_.size() == cells_.size())
        vehicleAt_[std::size_t(ny) * std::size_t(width_) + std::size_t(nx)] = ((u.side << 16) | u.index) + 1;
    if ((to & kCrate) && pickupCrate(u)) u.mm.goalTileX = -1;  // units crate: re-plan round the new units
}

// ---------------------------------------------------------------------------------------
// Group move orders: every vehicle its own tile round the target, keeping the group's shape.

void World::applyModernFormations() {
    std::vector<FormationEntry> batch = std::move(formationBatch_);
    formationBatch_.clear();
    std::vector<char> done(batch.size(), 0);
    for (std::size_t a = 0; a < batch.size(); ++a) {
        if (done[a]) continue;
        std::vector<Unit*> group;
        for (std::size_t b = a; b < batch.size(); ++b) {
            if (done[b] || batch[b].side != batch[a].side || batch[b].x != batch[a].x || batch[b].y != batch[a].y) continue;
            done[b] = 1;
            Unit* u = unit(batch[b].side, batch[b].index);
            if (u && isModernVehicle(*u) && u->state == UnitState::Moving && u->targetX == batch[b].x &&
                u->targetY == batch[b].y && std::find(group.begin(), group.end(), u) == group.end())
                group.push_back(u);
        }
        const int n = static_cast<int>(group.size());
        if (n < 2) continue;
        const int tx = batch[a].x, ty = batch[a].y;
        // The group's centre and its offsets (px), squeezed to about sqrt(n) tiles across.
        std::int64_t cx = 0, cy = 0;
        for (const Unit* u : group) cx += u->posX >> 16, cy += u->posY >> 16;
        cx /= n;
        cy /= n;
        int spread = 1;
        while (spread * spread < n) ++spread;
        const std::int64_t limit = std::int64_t(spread) * 32;
        std::int64_t far = 1;
        for (const Unit* u : group)
            far = std::max<std::int64_t>({far, std::llabs((u->posX >> 16) - cx), std::llabs((u->posY >> 16) - cy)});
        // Free tiles round the target, nearest rings first.
        std::vector<std::pair<int, int>> slots;
        auto isMember = [&](int x, int y) {
            for (const Unit* u : group)
                if (u->blockFromX == x && u->blockFromY == y) return true;
            return false;
        };
        for (int r = 0; r <= 8 && int(slots.size()) < 2 * n; ++r)
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const int x = tx + dx, y = ty + dy;
                    if (!modernStaticPass(x, y, *group[0])) continue;
                    if ((cell(x, y).flags & (kAnyUnit | kBuilding)) && !isMember(x, y)) continue;
                    slots.push_back({x, y});
                }
        if (slots.empty()) continue;
        // Wanted tile per unit; those nearest the centre choose first.
        std::vector<std::array<std::int64_t, 4>> want;  // {order key, wantX px, wantY px, unit}
        for (int k = 0; k < n; ++k) {
            const Unit* u = group[std::size_t(k)];
            std::int64_t ox = (u->posX >> 16) - cx, oy = (u->posY >> 16) - cy;
            if (far > limit) ox = ox * limit / far, oy = oy * limit / far;
            want.push_back({ox * ox + oy * oy, tx * 32 + 16 + ox, ty * 32 + 16 + oy, k});
        }
        std::sort(want.begin(), want.end());
        std::vector<char> used(slots.size(), 0);
        for (const auto& w : want) {
            std::size_t pick = slots.size();
            std::int64_t bd = 0;
            for (std::size_t s = 0; s < slots.size(); ++s) {
                if (used[s]) continue;
                const std::int64_t dx = slots[s].first * 32 + 16 - w[1], dy = slots[s].second * 32 + 16 - w[2];
                const std::int64_t d = dx * dx + dy * dy;
                if (pick == slots.size() || d < bd) pick = s, bd = d;
            }
            if (pick == slots.size()) break;
            used[pick] = 1;
            Unit* u = group[std::size_t(w[3])];
            u->targetX = slots[pick].first;
            u->targetY = slots[pick].second;
        }
    }
}

// ---------------------------------------------------------------------------------------
// One tick of a modern vehicle (called through moveUnit). Returns false when it has arrived
// (stands on its target tile), true while driving, turning or waiting - MoveUnit's contract.
// posSteps = 1 while it is under way, 0 standing (the original's "at a tile centre" for the
// callers that fire / plan only from rest).

bool World::moveUnitModern(Unit& u) {
    ModernMotion& m = u.mm;
    const auto& t = unitTemplate(u);
    const int side = u.side;
    // Anything that moved the unit or turned its body (MoveUnitSimple leaving a factory, a
    // carryall drop, FireAt turning a turretless unit, a load) restarts from rest / that facing.
    if (m.heading < 0 || (((m.heading + 1024) >> 11) & 31) != (u.facing & 31)) m.heading = (u.facing & 31) << 11;
    if (u.posX != m.lastX || u.posY != m.lastY) {
        m.speed = 0;
        m.goalTileX = -1;
    }
    if ((u.flags & kBlockToMarked) && (u.blockToX != u.blockFromX || u.blockToY != u.blockFromY)) {
        // An original step (MoveUnitSimple) in progress: let it finish where it is going.
        u.targetX = u.blockToX;
        u.targetY = u.blockToY;
    }
    if (u.flags & kStateChanged) {  // a new order / state (a plan to the same tile stays: docking, repair pads)
        u.flags &= 0x3FFFFFFFu;
        m.flags &= static_cast<std::uint8_t>(~ModernMotion::kAvoidUnits);
        m.stall = 0;
    }
    // The attack states chase: the target follows the enemy every tick (AttackUnit / AttackBuilding
    // set it only from rest - posSteps 0 - which a modern vehicle on the move is not).
    if (u.state == UnitState::AttackingUnit && u.enemySide >= 0 && u.enemySide < kMaxSides) {
        if (const Unit* e = unit(u.enemySide, u.enemyIndex); e && e->state != UnitState::Dead && inBounds(e->blockToX, e->blockToY)) {
            u.targetX = e->blockToX;
            u.targetY = e->blockToY;
        }
    } else if (u.state == UnitState::AttackingBuilding && u.enemySide >= 0 && u.enemySide < kMaxSides) {
        if (const Building* b = building(u.enemySide, u.enemyIndex); b && b->state != 17)
            closestBuildingTile(u.blockFromX, u.blockFromY, *b, u.targetX, u.targetY);
    }
    u.flags &= ~kTurningInPlace;
    if (m.repath) --m.repath;
    const int fixedSelf = fixToTile(u.posX), fixedSelfY = fixToTile(u.posY);
    if (inBounds(fixedSelf, fixedSelfY) && (fixedSelf != u.blockFromX || fixedSelfY != u.blockFromY) &&
        !(u.flags & kBlockToMarked)) {
        // (a position set by someone else without the cell bookkeeping: adopt the cell)
        modernEnterCell(u, fixedSelf, fixedSelfY);
    }
    auto finish = [&](bool moving) {
        u.posSteps = moving ? 1 : 0;
        u.posStepsMax = 0;
        u.stepTargetX = m.goalX;
        u.stepTargetY = m.goalY;
        m.lastX = u.posX;
        m.lastY = u.posY;
        u.facing = ((m.heading + 1024) >> 11) & 31;
    };
    // Waiting for targets while stuck (MoveUnit's UnitAutoTarget when RouteRetryDelay is set).
    if (m.stall != 0 && (static_cast<std::uint32_t>(u.index & 7) == (gameTicks_ & 7) || u.enemyIndex != kNoIndex) &&
        !unitIsHiddenStealth(u) && u.state != UnitState::AttackingTileOnRoute && u.state == UnitState::Moving) {
        unitAutoTarget(u);
        if (u.flags & kStateChanged) {  // it engaged: the attack state drives from the next tick
            finish(m.speed != 0);
            return true;
        }
    }

    // ---- plan ----
    if (u.targetX != m.goalTileX || u.targetY != m.goalTileY) {
        const bool throttle = m.goalTileX >= 0 && m.repath > 0 && m.wpCount != 0 &&
                              (u.state == UnitState::AttackingUnit || u.state == UnitState::AttackingBuilding);
        if (!throttle) {
            m.repath = 8;
            m.flags &= static_cast<std::uint8_t>(~(ModernMotion::kStopping | ModernMotion::kPartial | ModernMotion::kYield));
            if (u.targetX == u.blockFromX && u.targetY == u.blockFromY) {
                m.goalTileX = static_cast<std::int16_t>(u.targetX);
                m.goalTileY = static_cast<std::int16_t>(u.targetY);
                m.goalX = u.posX;
                m.goalY = u.posY;
                m.wpCount = m.wpIndex = 0;
                if (m.speed == 0) {  // already there
                    finish(false);
                    return false;
                }
                m.flags |= ModernMotion::kStopping;  // brake where it is
            } else if (!modernPlan(u)) {
                m.speed = 0;
                finish(false);
                return false;  // nowhere to go: stays (target = its cell)
            }
        }
    } else if (m.speed == 0 && u.targetX == u.blockFromX && u.targetY == u.blockFromY &&
               ((m.flags & ModernMotion::kStopping) || (u.posX == m.goalX && u.posY == m.goalY))) {
        m.flags &= static_cast<std::uint8_t>(~ModernMotion::kStopping);
        finish(false);
        return false;  // arrived
    }
    const bool stopping = (m.flags & ModernMotion::kStopping) != 0;

    // Pulled over for a vehicle met head-on: wait on the pull-over cell until nothing drives past
    // within a tile (at least 12 ticks, at most 150), then plan to the target again.
    if ((m.flags & ModernMotion::kYield) && m.wpCount != 0 && m.wpIndex + 1 >= m.wpCount && m.speed == 0 &&
        u.posX == centreOf(m.wpX[m.wpCount - 1]) && u.posY == centreOf(m.wpY[m.wpCount - 1])) {
        ++m.stall;
        if ((m.stall >= 12 && !modernMovingVehicleNear(u)) || m.stall >= 150) {
            m.flags &= static_cast<std::uint8_t>(~(ModernMotion::kYield | ModernMotion::kPartial));
            m.goalTileX = -1;  // plan to the target from the next tick
            m.stall = 0;
        }
        finish(false);
        return true;
    }

    // Near the goal and it is taken by a vehicle standing there (or now blocked): another tile.
    const bool mayRetarget = u.state == UnitState::Moving || u.state == UnitState::MovingAway ||
                             u.state == UnitState::Guarding || u.state == UnitState::MovingToHarvest;
    if (!stopping && mayRetarget && chebyshev(u.blockFromX, u.blockFromY, u.targetX, u.targetY) <= 2 &&
        (u.targetX != u.blockFromX || u.targetY != u.blockFromY)) {
        const Unit* v = modernVehicleAt(u.targetX, u.targetY);
        const std::uint32_t gf = inBounds(u.targetX, u.targetY) ? cell(u.targetX, u.targetY).flags : 0;
        const bool taken = (v && v != &u && v->posSteps == 0) ||
                           (!v && (gf & kAnyUnit) && !canUnitUseSquare(u.targetX, u.targetY, u, side, 0)) ||
                           !modernStaticPass(u.targetX, u.targetY, u);
        if (taken) {
            if (modernPickFreeGoal(u)) {
                if (!modernPlan(u)) {
                    m.speed = 0;
                    finish(false);
                    return false;
                }
            } else if (m.speed == 0) {  // nothing free round it: stop here
                u.targetX = u.blockFromX;
                u.targetY = u.blockFromY;
                m.goalTileX = static_cast<std::int16_t>(u.targetX);
                m.goalTileY = static_cast<std::int16_t>(u.targetY);
                m.wpCount = m.wpIndex = 0;
                finish(false);
                return false;
            }
        }
    }

    // ---- steer ----
    const bool wantStop = stopping || modernWantsStop(u);
    // The point to drive at: the next corner; skip ahead once the one after it is in sight.
    std::int32_t px = m.goalX, py = m.goalY;
    bool final = true, pullOver = false;
    if (!stopping) {
        while (m.wpIndex < m.wpCount) {
            const std::int32_t wx = centreOf(m.wpX[m.wpIndex]), wy = centreOf(m.wpY[m.wpIndex]);
            const bool last = m.wpIndex + 1 == m.wpCount;
            const std::int32_t d = dist16(wx - u.posX, wy - u.posY);
            if (last && !(m.flags & ModernMotion::kPartial)) {  // the goal itself
                px = wx, py = wy;
                break;
            }
            if (last && (m.flags & ModernMotion::kYield)) {  // the pull-over cell: stop on its centre
                px = wx, py = wy;
                final = false;
                pullOver = true;
                break;
            }
            bool skip = d < toFix(10);
            if (!skip && d < toFix(64)) {
                const std::int32_t nx = last ? m.goalX : centreOf(m.wpX[m.wpIndex + 1]);
                const std::int32_t ny = last ? m.goalY : centreOf(m.wpY[m.wpIndex + 1]);
                skip = !last && modernLineClear(u, u.posX, u.posY, nx, ny);
            }
            if (!skip) {
                px = wx, py = wy;
                final = false;
                break;
            }
            ++m.wpIndex;
            if (last) {  // the end of a partial plan: plan the rest
                m.goalTileX = -1;
                finish(m.speed != 0);
                return true;
            }
        }
    }
    const std::int32_t toGoal = dist16(m.goalX - u.posX, m.goalY - u.posY);
    const std::int32_t toPoint = dist16(px - u.posX, py - u.posY);
    const int desired = (stopping || toPoint < toFix(1) / 2) ? m.heading : headingOf(px - u.posX, py - u.posY);
    const bool wheeled = t.speedType == 2;
    const int rate = std::max(1, 2048 / (int(t.unitRotationSpeed) + 1));
    int err = wrapH(desired - m.heading);
    m.heading = (m.heading + std::clamp(err, -rate, rate)) & 0xFFFF;
    err = wrapH(desired - m.heading);
    const int aerr = std::abs(err);

    // ---- speed ----
    std::int32_t maxS = std::max<std::int32_t>(stepSpeed(u), 0x2000);
    {
        // The original moves both axes at Speed (a diagonal tile step takes as long as a straight
        // one): the same time per tile of Chebyshev distance at any heading keeps the balance.
        const std::int32_t s = std::abs(sinH(m.heading)), c = std::abs(cosH(m.heading));
        maxS = static_cast<std::int32_t>(std::int64_t(maxS) * 65536 / std::max<std::int32_t>(std::max(s, c), 46341));
    }
    const std::int32_t base = std::max<std::int32_t>(static_cast<std::int32_t>(t.speed), 0x4000);
    const int accelDiv = (t.specialBehavior == behaviour::kHarvester || t.specialBehavior == behaviour::kMcv) ? 16
                         : wheeled ? 6 : 10;
    const std::int32_t accel = std::max<std::int32_t>(base / accelDiv, 0x800);
    const std::int32_t decel = std::max<std::int32_t>(base / 5, 0x1000);
    std::int32_t want = wantStop ? 0 : maxS;
    if (!wantStop) {
        // Slow into turns; tracked vehicles pivot on the spot for sharp ones, wheeled ones arc.
        if (aerr > 60 * kDeg) {
            want = (wheeled && toGoal > toFix(48) && m.speed > 0) ? maxS / 3 : 0;
        } else if (aerr > 15 * kDeg) {
            want = static_cast<std::int32_t>(std::int64_t(want) * (60 * kDeg - (aerr - 15 * kDeg) * 4 / 5) / (60 * kDeg));
        }
        if (final) want = std::min(want, std::max<std::int32_t>(brakeSpeed(toGoal, decel), toGoal > 0 ? 0x1000 : 0));
        if (pullOver) want = std::min(want, std::max<std::int32_t>(brakeSpeed(toPoint, decel), toPoint > 0 ? 0x1000 : 0));
    }
    // Cells ahead holding a unit we may not enter, and the vehicle in front: brake in time.
    int blockX = -1, blockY = -1;
    if (want > 0) {
        const std::int32_t hx = sinH(m.heading), hy = -cosH(m.heading);
        const std::int32_t look = std::min<std::int32_t>(toFix(48), toFix(18) + std::int32_t(std::int64_t(m.speed) * m.speed / (2 * decel)));
        int lastCx = u.blockFromX, lastCy = u.blockFromY;
        for (std::int32_t s = toFix(6); s <= look; s += toFix(6)) {
            const int cx = fixToTile(u.posX + std::int32_t(std::int64_t(hx) * s >> 16));
            const int cy = fixToTile(u.posY + std::int32_t(std::int64_t(hy) * s >> 16));
            if (cx == lastCx && cy == lastCy) continue;
            lastCx = cx, lastCy = cy;
            if (!inBounds(cx, cy)) break;
            if (final && cx == u.targetX && cy == u.targetY && (cell(cx, cy).flags & kAnyUnit) == 0) continue;
            if ((cell(cx, cy).flags & kAnyUnit) && !canUnitUseSquare(cx, cy, u, side, 0)) {
                // A vehicle driving on the same way is followed (below), not braked for.
                if (const Unit* v = modernVehicleAt(cx, cy); v && v != &u && v->mm.speed > 0 && v->posSteps != 0 &&
                    (std::int64_t(sinH(v->mm.heading)) * hx + std::int64_t(-cosH(v->mm.heading)) * hy) >> 16 > 32768)
                    continue;
                want = std::min(want, brakeSpeed(s - toFix(14), decel));
                blockX = cx, blockY = cy;
                break;
            }
        }
        // Following: the nearest vehicle ahead within ~1.5 tiles of the line.
        const int ax = fixToTile(u.posX + 24 * hx), ay = fixToTile(u.posY + 24 * hy);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const Unit* v = modernVehicleAt(ax + dx, ay + dy);
                if (!v || v == &u) continue;
                const std::int64_t vx = std::int64_t(v->posX - u.posX), vy = std::int64_t(v->posY - u.posY);
                const std::int64_t along = (vx * hx + vy * hy) >> 16, lat = std::llabs((vx * hy - vy * hx) >> 16);
                // (one about beside it - less than 10 px ahead - does not hold it back: the cells keep them apart)
                if (along <= toFix(10) || lat >= toFix(20) || along >= toFix(48)) continue;
                std::int32_t lim = brakeSpeed(static_cast<std::int32_t>(along) - kFollowGap, decel);
                if (v->posSteps != 0 && v->mm.speed > 0) {
                    const std::int64_t same = (std::int64_t(sinH(v->mm.heading)) * hx + std::int64_t(-cosH(v->mm.heading)) * hy) >> 16;
                    if (same > 0) lim += static_cast<std::int32_t>(std::int64_t(v->mm.speed) * same >> 16);
                }
                if (lim < want) {
                    want = lim;
                    if (want == 0 && blockX < 0) blockX = v->blockFromX, blockY = v->blockFromY;
                }
            }
    }
    if (m.speed < want) m.speed = std::min(want, m.speed + accel);
    else m.speed = std::max(want, m.speed - decel);

    // ---- move ----
    bool arrived = false;
    std::int32_t nx = u.posX, ny = u.posY;
    if (m.speed > 0) {
        nx = u.posX + std::int32_t(std::int64_t(m.speed) * sinH(m.heading) >> 16);
        ny = u.posY - std::int32_t(std::int64_t(m.speed) * cosH(m.heading) >> 16);
        if (!stopping && final && toGoal <= m.speed && aerr < 30 * kDeg) {  // reaches the goal this tick
            nx = m.goalX, ny = m.goalY;
            m.speed = 0;
            arrived = true;
        }
        if (pullOver && toPoint <= m.speed && aerr < 30 * kDeg) {  // on the pull-over cell's centre: wait there
            nx = px, ny = py;
            m.speed = 0;
        }
    } else if (!stopping && final && toGoal != 0 && toGoal < toFix(2) && !wantStop) {
        nx = m.goalX, ny = m.goalY;  // creep the last pixel
        arrived = true;
    } else if (pullOver && toPoint != 0 && toPoint < toFix(2) && !wantStop) {
        nx = px, ny = py;
    }
    // The cell the centre moves into must be free: else slide along the cell edge (one axis of the
    // step) when that is free - a vehicle clipping the corner of an occupied / blocked cell glides
    // past it instead of stopping.
    int staticHit = 0, unitX = -1, unitY = -1;
    auto enterable = [&](std::int32_t x, std::int32_t y) {
        const int tx = fixToTile(x), ty = fixToTile(y);
        if (tx == u.blockFromX && ty == u.blockFromY) return true;
        if (!inBounds(tx, ty) || !modernStaticPass(tx, ty, u)) {
            ++staticHit;
            return false;
        }
        if (canUnitUseSquare(tx, ty, u, side, 0)) return true;
        if (unitX < 0) unitX = tx, unitY = ty;
        return false;
    };
    bool blocked = false;
    if (!enterable(nx, ny)) {
        const bool xFirst = std::abs(nx - u.posX) >= std::abs(ny - u.posY);
        const std::int32_t ax1 = xFirst ? nx : u.posX, ay1 = xFirst ? u.posY : ny;
        const std::int32_t ax2 = xFirst ? u.posX : nx, ay2 = xFirst ? ny : u.posY;
        if ((ax1 != u.posX || ay1 != u.posY) && enterable(ax1, ay1)) nx = ax1, ny = ay1;
        else if ((ax2 != u.posX || ay2 != u.posY) && enterable(ax2, ay2)) nx = ax2, ny = ay2;
        else blocked = true;
        arrived = false;
        if (!blocked) m.speed -= m.speed / 4;  // scraping along the edge
        if (unitX >= 0 && blockX < 0) blockX = unitX, blockY = unitY;
        if (blocked && staticHit && unitX < 0) m.goalTileX = -1;  // something new in the way (a building): plan again
    }
    const int ntx = fixToTile(nx), nty = fixToTile(ny);
    if (blocked) {
        m.speed = 0;
        arrived = false;
    } else {
        u.posX = nx;
        u.posY = ny;
        if (ntx != u.blockFromX || nty != u.blockFromY) modernEnterCell(u, ntx, nty);
        if (stopping && (u.targetX != u.blockFromX || u.targetY != u.blockFromY)) {
            u.targetX = u.blockFromX;  // braking carried it into the next cell: stop there
            u.targetY = u.blockFromY;
            m.goalTileX = static_cast<std::int16_t>(u.targetX);
            m.goalTileY = static_cast<std::int16_t>(u.targetY);
        }
    }

    // ---- stuck behind someone ----
    if (m.speed == 0 && !wantStop && !arrived && aerr <= 60 * kDeg && blockX >= 0) {
        ++m.stall;
        const Unit* v = modernVehicleAt(blockX, blockY);
        int vs = 0, vi = 0;
        const Unit* any = v ? v : getUnitOnTile(blockX << 5, blockY << 5, vs, vi, false);
        const int owner = any ? any->side : static_cast<int>(cell(blockX, blockY).flags & kOwnerMask);
        const bool friendly = diplomacy(side, owner) == 0;
        if (friendly && any && (m.stall == 6 || m.stall % 40 == 0) && any->posSteps == 0 &&
            (any->state == UnitState::Idle || any->state == UnitState::Guarding))
            askUnitsOnTileToMoveAway(u, blockX, blockY);  // make room
        // Head-on with a friendly vehicle stopped by us (it faces us within 60 degrees and is stuck
        // too): a fixed tie-break instead of both re-planning round each other the same way. The
        // yielder (one that may give up its plan; of two, the higher side/pool index) pulls over off
        // the other's lane; the other holds its plan and does not re-plan while the yielder clears
        // the way (it tries to pull over itself after 48 ticks, re-plans as before after 96).
        bool holdForYield = false, pulledOver = false;
        if (friendly && v && v != &u && !(m.flags & ModernMotion::kYield)) {
            if (v->mm.flags & ModernMotion::kYield) {
                holdForYield = m.stall < 150;  // it is pulling over: let it
            } else if (v->mm.speed == 0 && v->mm.stall > 0) {
                const std::int64_t dx = std::int64_t(u.posX) - v->posX, dy = std::int64_t(u.posY) - v->posY;
                const std::int64_t along = (dx * sinH(v->mm.heading) - dy * cosH(v->mm.heading)) >> 16;
                if (along * 2 > dist16(static_cast<std::int32_t>(dx), static_cast<std::int32_t>(dy))) {
                    const int keyU = (u.side << 16) | u.index, keyV = (v->side << 16) | v->index;
                    const bool yields = modernCanYield(u) && (!modernCanYield(*v) || keyU > keyV);
                    if (yields) {
                        pulledOver = m.stall >= 6 && (m.stall - 6) % 12 == 0 && modernPullOver(u, *v);
                    } else {
                        pulledOver = m.stall == 48 && modernCanYield(u) && modernPullOver(u, *v);
                        holdForYield = m.stall < 96;
                    }
                }
            }
        }
        if (pulledOver) {
            // (drives to the pull-over cell from the next tick)
        } else if (!friendly && m.stall == 30 && t.primaryWeapon != -1 && u.state == UnitState::Moving) {
            // An enemy in the way: attack it, then go on (GetNextSquare's state 10 rule).
            u.rememberX = u.targetX;
            u.rememberY = u.targetY;
            u.oldState = u.state;
            u.targetX = blockX;
            u.targetY = blockY;
            unitAdjustState(u, UnitState::AttackingTileOnRoute);
        } else if (m.stall % 24 == 0 && !holdForYield) {
            if (chebyshev(u.blockFromX, u.blockFromY, u.targetX, u.targetY) <= 2 && m.stall >= 72) {
                u.targetX = u.blockFromX;  // close enough and still blocked: stay
                u.targetY = u.blockFromY;
            } else {
                m.flags |= ModernMotion::kAvoidUnits;  // round the vehicles in the way
                m.goalTileX = -1;
            }
        }
    } else if (m.speed > 0) {
        m.stall = 0;
        if (m.flags & ModernMotion::kAvoidUnits) {
            // (kept until the next plan: a re-plan for another reason routes normally again)
        }
    }

    // ---- turret: at the enemy while the hull drives ----
    if (t.primaryWeaponUseBarrel) {
        int want32 = ((m.heading + 1024) >> 11) & 31;
        const int ux = u.posX >> 16, uy = u.posY >> 16;
        if (u.state == UnitState::AttackingUnit) {
            if (const Unit* e = u.enemySide >= 0 && u.enemySide < kMaxSides ? unit(u.enemySide, u.enemyIndex) : nullptr)
                want32 = getFacing(ux, uy, e->posX >> 16, e->posY >> 16);
        } else if (u.state == UnitState::AttackingTile || u.state == UnitState::AttackingTileOnRoute) {
            want32 = getFacing(ux, uy, u.targetX * 32 + 16, u.targetY * 32 + 16);
        } else if (u.state == UnitState::AttackingBuilding) {
            if (const Building* b = u.enemySide >= 0 && u.enemySide < kMaxSides ? building(u.enemySide, u.enemyIndex) : nullptr) {
                int bx = 0, by = 0;
                closestBuildingTile(u.blockFromX, u.blockFromY, *b, bx, by);
                want32 = getFacing(ux, uy, bx * 32 + 16, by * 32 + 16);
            }
        }
        rotateTurret(u, want32);
    }

    if (arrived || (stopping && m.speed == 0)) {
        m.flags &= static_cast<std::uint8_t>(~ModernMotion::kStopping);
        if (stopping) {
            u.targetX = u.blockFromX;
            u.targetY = u.blockFromY;
            m.goalTileX = static_cast<std::int16_t>(u.targetX);
            m.goalTileY = static_cast<std::int16_t>(u.targetY);
        }
        m.goalX = u.posX;
        m.goalY = u.posY;
        finish(false);
        return !(u.blockFromX == u.targetX && u.blockFromY == u.targetY);
    }
    // Under way (or turning towards the way) unless held at rest by the attack range.
    const bool underWay = m.speed != 0 || (!wantStop && aerr != 0) || (!wantStop && toPoint != 0 && blockX < 0);
    finish(underWay);
    return true;
}

}  // namespace d2k::game

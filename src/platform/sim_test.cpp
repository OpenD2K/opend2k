#include "platform/sim_test.hpp"

#include <cstring>
#include <iterator>
#include <string>

#include "game/save_check.hpp"
#include "render/motion.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

namespace d2k::platform {

namespace {

// The side controlled by the local player in the movement/combat checks.
constexpr int kPlayerSide = 0;

// Headless combat check (part of --simtest): the player's first armed ground unit attacks the
// nearest enemy unit; tick until something dies and log shots, hits and deaths.
bool runCombatTest(const GameData& g) {
    using namespace d2k::game;
    World world(g.map, g.mission, g.tileAtr, g.tileData, rulesOf(g), kPlayerSide);
    Unit* attacker = nullptr;
    Unit* target = nullptr;
    int best = 1 << 30;
    // The player's units first; if the player has none, any side's.
    for (int pass = 0; pass < 2 && !attacker; ++pass)
    for (int as = 0; as < kMaxSides; ++as) {
        if ((pass == 0) != (as == kPlayerSide)) continue;
        Side& me = world.sides()[as];
    for (int i = me.firstUnit; i != -1; i = me.nextObject(i)) {
        Unit& u = me.units[i];
        const auto& t = world.unitTemplate(u);
        if (t.primaryWeapon < 0 || (u.flags & 0x40) || t.specialBehavior != 0) continue;
        for (int s = 0; s < kMaxSides; ++s) {
            if (world.diplomacy(as, s) != 1) continue;
            Side& es = world.sides()[s];
            for (int j = es.firstUnit; j != -1; j = es.nextObject(j)) {
                Unit& e = es.units[j];
                const int beh = world.unitTemplate(e).specialBehavior;
                if ((e.flags & 0x400140) || beh == 5 || beh == 2 || beh == 8) continue;
                const int d = std::abs(e.blockFromX - u.blockFromX) + std::abs(e.blockFromY - u.blockFromY);
                if (d < best) {
                    best = d;
                    attacker = &u;
                    target = &e;
                }
            }
        }
    }
    }
    if (!attacker) {
        SDL_Log("simtest: combat: no armed player unit / enemy unit pair on this map");
        return true;
    }
    const int as = attacker->side, ai = attacker->index, ts = target->side, ti = target->index;
    SDL_Log("simtest: combat: %s (side %d #%d at %d,%d, %d hp) attacks %s (side %d #%d at %d,%d, %d hp)",
            g.templates.unitName(attacker->type).c_str(), as, ai, attacker->blockFromX, attacker->blockFromY,
            attacker->health, g.templates.unitName(target->type).c_str(), ts, ti, target->blockFromX, target->blockFromY,
            target->health);
    world.orderAttackUnit(as, ai, ts, ti);
    int shots = 0, deaths = 0, lines = 0;
    std::vector<std::int32_t> lastHp(kMaxSides * kObjectsPerSide, -1);
    for (int s = 0; s < kMaxSides; ++s)
        for (int i = world.sides()[s].firstUnit; i != -1; i = world.sides()[s].nextObject(i))
            lastHp[static_cast<std::size_t>(s * kObjectsPerSide + i)] = world.sides()[s].units[i].health;
    bool targetDead = false;
    int firstDeath = 0;
    for (int t = 1; t <= 6000 && !targetDead && !firstDeath; ++t) {
        world.tick();
        const Unit* a = world.unit(as, ai);
        // FireAt sets ReloadDelay to the weapon delay after UpdateUnit decremented it.
        if (a && a->reloadDelay != 0 && a->reloadDelay == world.unitTemplate(*a).primaryWeaponLongDelay) {
            ++shots;
            if (lines++ < 40) SDL_Log("  t=%4d shot %d by #%d (state %d)", t, shots, ai, static_cast<int>(a->state));
        }
        for (int s = 0; s < kMaxSides; ++s) {
            Side& sd = world.sides()[s];
            for (int i = sd.firstUnit; i != -1; i = sd.nextObject(i)) {
                const Unit& u = sd.units[i];
                auto& hp = lastHp[static_cast<std::size_t>(s * kObjectsPerSide + i)];
                if (hp >= 0 && u.health != hp) {
                    if (u.state == UnitState::Dead && hp > 0) {
                        ++deaths;
                        firstDeath = t;
                        SDL_Log("  t=%4d %s side %d #%d DIES (%d -> 0)", t, g.templates.unitName(u.type).c_str(), s, i, hp);
                        if (s == ts && i == ti) targetDead = true;
                    } else if (lines++ < 40) {
                        SDL_Log("  t=%4d %s side %d #%d hit: %d -> %d", t, g.templates.unitName(u.type).c_str(), s, i, hp,
                                u.health);
                    }
                }
                hp = u.health;
            }
        }
        if (!targetDead) {
            const Unit* e = world.unit(ts, ti);
            if (!e || e->state == UnitState::Dead) targetDead = true;
        }
        if (targetDead) SDL_Log("simtest: combat: target destroyed after %d ticks, %d shots by the attacker, %d deaths", t,
                                shots, deaths);
    }
    if (!targetDead && firstDeath)
        SDL_Log("simtest: combat: first death after %d ticks (%d shots by the attacker)", firstDeath, shots);
    else if (!targetDead)
        SDL_Log("simtest: combat: nobody died within 6000 ticks (%d shots)", shots);
    return targetDead || deaths > 0;
}

// Simtest helper: build `type` with the player's construction yard, wait until it is ready
// and place it at the first valid spot spiralling out from the player's first building.
// Returns the ticks spent, or -1.
int buildAndPlace(d2k::game::World& world, int type, int& px, int& py) {
    const int kPlayerSide = world.mySide();
    using namespace d2k::game;
    if (!world.orderBuildBuilding(kPlayerSide, type)) return -1;
    int t = 0;
    while (!world.buildingQueue(kPlayerSide).ready && t < 20000) {
        world.tick();
        ++t;
    }
    const Side& me = world.sides()[kPlayerSide];
    if (!world.buildingQueue(kPlayerSide).ready || me.firstBuilding == -1) return -1;
    for (int r = 0; r < 14; ++r)
        for (int bi = me.firstBuilding; bi != -1; bi = me.nextObject(bi)) {
            const Building& c = me.buildings[bi];
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    if (!world.canPlaceBuilding(kPlayerSide, type, c.tileX + dx, c.tileY + dy)) continue;
                    px = c.tileX + dx;
                    py = c.tileY + dy;
                    if (!world.orderPlaceBuilding(kPlayerSide, type, px, py)) return -1;
                    world.tick();
                    return t + 1;
                }
        }
    SDL_Log("simtest: no valid placement for building type %d near the base", type);
    return -1;
}

int sidebarBuilding(const d2k::game::World& world, int behaviour) {
    const int kPlayerSide = world.mySide();
    for (const auto& o : world.buildableBuildings(kPlayerSide))
        if (world.rules().templates->bin.buildings[o.type].specialBehavior == behaviour) return o.type;
    return -1;
}

// Headless economy check (part of --simtest): run the mission and log the player's credits,
// storage, power and harvester activity; succeeds when harvesters unload spice. Without a
// refinery the player first builds a wind trap and a refinery (whose free harvester arrives
// by carryall).
bool runEconomyTest(const GameData& g) {
    using namespace d2k::game;
    const int kPlayerSide = g.playerSide;
    World world(g.map, g.mission, g.tileAtr, g.tileData, rulesOf(g), kPlayerSide);
    const Side& me = world.sides()[kPlayerSide];
    const auto e0 = world.economy(kPlayerSide);
    SDL_Log("simtest: economy: start credits %d (cash %d spice %d), storage %d", e0.credits, e0.cash, e0.spice,
            e0.storage);
    bool haveRefinery = false;
    for (int i = me.firstBuilding; i != -1; i = me.nextObject(i))
        haveRefinery |= world.buildingTemplate(me.buildings[i]).specialBehavior == building_behaviour::kRefinery;
    int t0 = 0;
    if (!haveRefinery) {
        for (const int beh : {0, building_behaviour::kRefinery}) {  // wind trap (behaviour 0, produces power), refinery
            int type = -1;
            for (const auto& o : world.buildableBuildings(kPlayerSide)) {
                const auto& bt = world.rules().templates->bin.buildings[o.type];
                if (bt.specialBehavior == beh && (beh != 0 || bt.powerConsumption < 0)) type = o.type;
            }
            int x = 0, y = 0;
            const int t = type >= 0 ? buildAndPlace(world, type, x, y) : -1;
            if (t < 0) {
                SDL_Log("simtest: economy: could not build behaviour %d", beh);
                return false;
            }
            t0 += t;
            SDL_Log("simtest: economy: built %s at %d,%d after %d ticks, credits %d", g.templates.buildingName(type).c_str(),
                    x, y, t0, world.credits(kPlayerSide));
        }
    }
    std::vector<UnitState> prev(kObjectsPerSide, UnitState::Dummy);
    int trips = 0, firstUnload = -1, minCredits = world.credits(kPlayerSide), carryalls = 0;
    const int startHarvested = me.spiceHarvested;
    constexpr int kTicks = 9000;
    for (int t = 1; t <= kTicks; ++t) {
        world.tick();
        for (int i = me.firstUnit; i != -1; i = me.nextObject(i)) {
            const Unit& u = me.units[i];
            if (world.unitTemplate(u).specialBehavior == behaviour::kCarryall && prev[i] == UnitState::Dummy) ++carryalls;
            if (u.state == UnitState::UnloadingSpice && prev[i] != UnitState::UnloadingSpice) {
                ++trips;
                if (firstUnload < 0) firstUnload = t;
            }
            prev[i] = u.state;
        }
        for (int i = 0; i < kObjectsPerSide; ++i)
            if (me.links[i].kind != SlotKind::Unit) prev[i] = UnitState::Dummy;
        minCredits = std::min(minCredits, world.credits(kPlayerSide));
        if (t % 1000 == 0) {
            const auto e = world.economy(kPlayerSide);
            int n = 0, loads = 0;
            for (int i = me.firstUnit; i != -1; i = me.nextObject(i))
                if (world.unitTemplate(me.units[i]).specialBehavior == behaviour::kHarvester) {
                    ++n;
                    loads += me.units[i].byte29;
                }
            SDL_Log("simtest: economy: tick %5d credits %5d (cash %5d spice %4d/%4d) power %u%% harvesters %d "
                    "(carrying %d blobs), unload trips %d, spice harvested %d",
                    t0 + t, e.credits, e.cash, e.spice, e.storage, e.powerPercent, n, loads, trips, me.spiceHarvested);
        }
    }
    SDL_Log("simtest: economy: %d carryall(s) seen, %d unload trips (first after %d ticks), %d spice harvested, "
            "credits %d -> %d",
            carryalls, trips, firstUnload, me.spiceHarvested - startHarvested, minCredits, world.credits(kPlayerSide));
    // Credits must have risen at some point, unless the stored spice was lost with the
    // refinery/silos (the mission's AI attacks the base; its reinforcements hunt since the
    // delivery groups get their deploy mode, UpdateUnit case 0x13).
    return trips > 0 && me.spiceHarvested > startHarvested &&
           (world.credits(kPlayerSide) > minCredits || world.economy(kPlayerSide).storage == 0);
}

// Headless production check (part of --simtest): build the first sidebar building and unit of
// the player, place the building next to the base and wait for the unit to leave its factory.
bool runProductionTest(const GameData& g) {
    using namespace d2k::game;
    const int kPlayerSide = g.playerSide;
    World world(g.map, g.mission, g.tileAtr, g.tileData, rulesOf(g), kPlayerSide);
    world.sides()[kPlayerSide].cash += 20000;  // enough for anything on the sidebar
    world.tick();
    SDL_Log("simtest: production: %zu buildings and %zu units on the sidebar, credits %d",
            world.buildableBuildings(kPlayerSide).size(), world.buildableUnits(kPlayerSide).size(),
            world.credits(kPlayerSide));
    bool ok = true;
    const Side& me = world.sides()[kPlayerSide];
    // Buildings: a wind trap, then a barracks (gives infantry) if the sidebar offers them.
    for (const int beh : {0, 11}) {
        const int type = sidebarBuilding(world, beh);
        if (type < 0) continue;
        int count = 0, after = 0;
        for (int i = me.firstBuilding; i != -1; i = me.nextObject(i)) ++count;
        const int before = world.credits(kPlayerSide);
        int x = 0, y = 0;
        const int t = buildAndPlace(world, type, x, y);
        for (int i = me.firstBuilding; i != -1; i = me.nextObject(i)) ++after;
        SDL_Log("simtest: production: %s ready+placed after %d ticks at %d,%d, buildings %d -> %d, credits %d -> %d "
                "(cost %u)",
                g.templates.buildingName(type).c_str(), t, x, y, count, after, before, world.credits(kPlayerSide),
                world.buildingCost(type, 0, kPlayerSide));
        ok = ok && t > 0 && after == count + 1;
    }
    const auto units = world.buildableUnits(kPlayerSide);
    if (!units.empty()) {
        const int utype = units.front().type;
        int countBefore = 0;
        for (int i = me.firstUnit; i != -1; i = me.nextObject(i)) countBefore += me.units[i].type == utype;
        const int before = world.credits(kPlayerSide);
        world.orderBuildUnit(kPlayerSide, utype);
        int t = 0, countAfter = countBefore;
        while (countAfter == countBefore && t < 20000) {
            world.tick();
            ++t;
            countAfter = 0;
            for (int i = me.firstUnit; i != -1; i = me.nextObject(i)) countAfter += me.units[i].type == utype;
        }
        SDL_Log("simtest: production: %s %s after %d ticks, credits %d -> %d (cost %u)", g.templates.unitName(utype).c_str(),
                countAfter > countBefore ? "left the factory" : "NOT BUILT", t, before, world.credits(kPlayerSide),
                world.unitCost(utype, kPlayerSide));
        ok = ok && countAfter > countBefore;
    } else {
        SDL_Log("simtest: production: no unit on the sidebar");
    }
    return ok;
}

// Save/load determinism (part of --simtest): run the mission 2000 ticks with scripted player
// orders and the computer players active, save, load into a fresh world, run both 2000 more
// ticks and compare the complete state.
bool runSaveTest(const GameData& g) {
    using namespace d2k::game;
    const int side = g.playerSide;
    auto make = [&]() { return std::make_unique<World>(g.map, g.mission, g.tileAtr, g.tileData, rulesOf(g), side); };
    const RoundTripReport r =
        saveRoundTrip(make, 2000, 2000, [side](World& w, std::uint32_t step) { scriptedOrders(w, side, step); });
    SDL_Log("simtest: save/load round trip (side %d, 2000 + 2000 ticks): %s%s - world %zu bytes, file %zu bytes, "
            "%d pending orders saved, hash %016llx",
            side, r.ok ? "identical" : "FAILED: ", r.error.c_str(), r.saveBytes, r.fileBytes, r.pendingOrdersSaved,
            static_cast<unsigned long long>(r.hashAtEnd));
    return r.ok;
}

// "Unit movement: Modern" (not original; game/modern_move.cpp) part of --simtest: a group of the
// player's vehicles crosses the map, a battle with every armed unit runs with consistent cell
// occupancy, and a Modern save / load round trip stays identical.
bool runModernTest(const GameData& g) {
    using namespace d2k::game;
    const int side = g.playerSide;
    auto make = [&]() {
        auto w = std::make_unique<World>(g.map, g.mission, g.tileAtr, g.tileData, rulesOf(g), side);
        w->setModernMovement(true);
        return w;
    };
    bool ok = true;
    {
        auto wp = make();
        World& w = *wp;
        std::vector<int> group;
        const Side& me = w.sides()[side];
        for (int i = me.firstUnit; i != -1 && group.size() < 10; i = me.nextObject(i))
            if (w.isModernVehicle(me.units[i]) && me.units[i].state == UnitState::Idle) group.push_back(i);
        if (!group.empty()) {
            // The farthest cell (up to 30 tiles) a flood fill of drivable cells reaches from the first one.
            const Unit& u0 = me.units[group[0]];
            const int sx = u0.blockFromX, sy = u0.blockFromY;
            std::vector<int> dist(std::size_t(w.width()) * w.height(), -1);
            std::vector<std::pair<int, int>> q{{sx, sy}};
            dist[std::size_t(sy) * w.width() + sx] = 0;
            int bx = sx, by = sy, best = 0;
            for (std::size_t qi = 0; qi < q.size(); ++qi) {
                const auto [x, y] = q[qi];
                const int cheb = std::max(std::abs(x - sx), std::abs(y - sy));
                if (cheb > best && cheb <= 30) best = cheb, bx = x, by = y;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (!w.inBounds(nx, ny) || dist[std::size_t(ny) * w.width() + nx] >= 0) continue;
                        const std::uint32_t f = w.cell(nx, ny).flags;
                        if (!(f & cell_flags::kDriveOn) || (f & cell_flags::kBuilding)) continue;
                        dist[std::size_t(ny) * w.width() + nx] = dist[std::size_t(y) * w.width() + x] + 1;
                        q.push_back({nx, ny});
                    }
            }
            for (int i : group) w.orderMove(side, i, bx, by);
            int done = -1;
            for (int t = 1; t <= 4000 && done < 0; ++t) {
                w.tick();
                bool all = true;
                for (int i : group) {
                    const Unit* u = w.unit(side, i);
                    all = all && (!u || u->state != UnitState::Moving);
                }
                if (all) done = t;
            }
            int near = 0;
            for (int i : group)
                if (const Unit* u = w.unit(side, i); u && std::max(std::abs(u->blockFromX - bx), std::abs(u->blockFromY - by)) <= 4) ++near;
            SDL_Log("simtest: modern: %zu vehicles %d,%d -> %d,%d (%d tiles): %s after %d ticks, %d within 4 tiles of the target",
                    group.size(), sx, sy, bx, by, best, done > 0 ? "all stopped" : "STILL MOVING", done, near);
            ok = ok && done > 0;
        }
    }
    {
        auto wp = make();
        World& w = *wp;
        orderBattle(w);
        int bad = 0, deaths = 0;
        std::vector<int> seen(std::size_t(w.width()) * w.height());
        for (int t = 0; t < 3000; ++t) {
            w.tick();
            if (t % 10 != 0) continue;
            std::fill(seen.begin(), seen.end(), 0);
            for (int s = 0; s < kMaxSides; ++s)
                for (int i = w.sides()[s].firstUnit; i != -1; i = w.sides()[s].nextObject(i)) {
                    const Unit& u = w.sides()[s].units[i];
                    if (u.state == UnitState::Dead) ++deaths;
                    if (!w.isModernVehicle(u) || (u.flags & 0x400140) || u.state == UnitState::Dead) continue;
                    const std::uint32_t f = w.cell(u.blockFromX, u.blockFromY).flags;
                    if (!(f & cell_flags::kVehicle) || int(f & 7) != s) ++bad;
                    if (++seen[std::size_t(u.blockFromY) * w.width() + u.blockFromX] > 1) ++bad;
                }
        }
        SDL_Log("simtest: modern: battle 3000 ticks, %s cell occupancy (%d bad samples), %d dead-unit samples", bad ? "BROKEN" : "consistent",
                bad, deaths);
        ok = ok && bad == 0;
    }
    const RoundTripReport r = saveRoundTrip(make, 2000, 2000, [side](World& w, std::uint32_t step) { scriptedOrders(w, side, step); });
    SDL_Log("simtest: modern: save/load round trip (2000 + 2000 ticks): %s%s - world %zu bytes, hash %016llx", r.ok ? "identical" : "FAILED: ",
            r.error.c_str(), r.saveBytes, static_cast<unsigned long long>(r.hashAtEnd));
    return ok && r.ok;
}

}  // namespace

// Headless movement check (--simtest): build the World from the real map, pick a ground unit,
// order it to a free tile some distance away and tick until it arrives at the tile centre.
// Render-side motion smoothing must not change the simulation: two copies of the mission (with
// the battle demo orders) tick in lockstep, one with apply()/restore() between ticks; every
// unit/bullet/explosion position and the RNG must stay identical.
bool runMotionTest(const GameData& g) {
    using namespace d2k::game;
    World a(g.map, g.mission, g.tileAtr, g.tileData, rulesOf(g), kPlayerSide);
    World b(g.map, g.mission, g.tileAtr, g.tileData, rulesOf(g), kPlayerSide);
    orderBattle(a);
    orderBattle(b);
    render::MotionInterpolator motion;
    int moved = 0;
    for (int t = 0; t < 1500; ++t) {
        motion.beforeTick(b);
        a.tick();
        b.tick();
        for (const float alpha : {0.25f, 0.5f, 0.9f}) {
            // (with the Smooth camera ease of the drawn positions, and the picking pass after it)
            motion.apply(b, alpha, 1.0f / 180.0f);
            for (int s = 0; s < kMaxSides; ++s)
                for (int i = b.sides()[s].firstUnit; i != -1; i = b.sides()[s].nextObject(i))
                    moved += b.sides()[s].units[i].posX != a.sides()[s].units[i].posX;
            motion.restore(b);
            motion.applyDrawn(b);
            motion.restoreDrawn(b);
        }
        bool same = a.rng().seed() == b.rng().seed();
        for (int s = 0; s < kMaxSides && same; ++s) {
            const Side &sa = a.sides()[s], &sb = b.sides()[s];
            for (std::size_t i = 0; i < sa.units.size() && same; ++i)
                same = sa.units[i].posX == sb.units[i].posX && sa.units[i].posY == sb.units[i].posY &&
                       sa.units[i].posZ == sb.units[i].posZ;
            for (std::size_t i = 0; i < sa.bullets.size() && same; ++i)
                same = sa.bullets[i].posX == sb.bullets[i].posX && sa.bullets[i].posY == sb.bullets[i].posY;
            for (std::size_t i = 0; i < sa.explosions.size() && same; ++i)
                same = sa.explosions[i].posX == sb.explosions[i].posX && sa.explosions[i].posY == sb.explosions[i].posY;
        }
        if (!same) {
            SDL_Log("simtest: motion smoothing CHANGED the simulation at tick %d", t + 1);
            return false;
        }
    }
    SDL_Log("simtest: motion smoothing: 1500 ticks bit-identical, %d interpolated unit draws", moved);
    return true;
}

int runSimTest(const GameData& g) {
    using namespace d2k::game;
    World world(g.map, g.mission, g.tileAtr, g.tileData, rulesOf(g), kPlayerSide);
    int failures = 0, tested = 0;
    for (int wantInfantry = 0; wantInfantry < 2; ++wantInfantry) {
        Unit* pick = nullptr;
        for (int s = 0; s < kMaxSides && !pick; ++s) {
            auto& side = world.sides()[s];
            for (int i = side.firstUnit; i != -1 && !pick; i = side.nextObject(i)) {
                Unit& u = side.units[i];
                const auto& t = world.unitTemplate(u);
                const int beh = t.specialBehavior;
                if (beh == 2 || beh == 5 || beh == 8 || beh == 9 || beh == 10 || beh == 15) continue;
                if ((t.isInfantry != 0) == (wantInfantry != 0) && (u.flags & 0x40) == 0) pick = &u;
            }
        }
        if (!pick) continue;
        // Farthest free cell (mode 0) within 12 tiles that a flood fill reaches.
        const int sx = pick->blockFromX, sy = pick->blockFromY;
        std::vector<int> dist(static_cast<std::size_t>(world.width()) * world.height(), -1);
        std::vector<std::pair<int, int>> q{{sx, sy}};
        dist[static_cast<std::size_t>(sy) * world.width() + sx] = 0;
        int bx = sx, by = sy, best = 0;
        for (std::size_t qi = 0; qi < q.size(); ++qi) {
            const auto [x, y] = q[qi];
            const int d = dist[static_cast<std::size_t>(y) * world.width() + x];
            const int cheb = std::max(std::abs(x - sx), std::abs(y - sy));
            if (cheb > best && cheb <= 12) { best = cheb; bx = x; by = y; }
            if (d >= 40) continue;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (!world.inBounds(nx, ny) || dist[static_cast<std::size_t>(ny) * world.width() + nx] >= 0) continue;
                    if (!world.canUnitUseSquare(nx, ny, *pick, pick->side, 0)) continue;
                    dist[static_cast<std::size_t>(ny) * world.width() + nx] = d + 1;
                    q.push_back({nx, ny});
                }
        }
        ++tested;
        const int side = pick->side, index = pick->index;
        world.orderMove(side, index, bx, by);
        int arrivedAt = -1;
        for (int t = 1; t <= 3000; ++t) {
            world.tick();
            const Unit* u = world.unit(side, index);
            if (t > 2 && u && u->posSteps == 0 && u->blockFromX == bx && u->blockFromY == by &&
                u->state != UnitState::Moving) {
                arrivedAt = t;
                break;
            }
        }
        const Unit* u = world.unit(side, index);
        // MoveUnit never snaps to the centre: the step count is Chebyshev/Speed truncated and
        // an axis stops within Speed/2, so a unit may end up to ~1.5 Speed off (faithful).
        const int tol = u ? static_cast<int>((u->speed >> 16) + (u->speed >> 17)) + 1 : 0;
        const bool centred = u && (wantInfantry || (std::abs(fixToPixel(u->posX) - bx * 32 - 16) <= tol &&
                                                    std::abs(fixToPixel(u->posY) - by * 32 - 16) <= tol));
        SDL_Log("simtest: %s %s (side %d #%d) %d,%d -> %d,%d: %s after %d ticks, pos px (%d,%d)%s",
                wantInfantry ? "infantry" : "vehicle", g.templates.unitName(pick->type).c_str(), side, index, sx, sy,
                bx, by, arrivedAt > 0 ? "arrived" : "DID NOT ARRIVE", arrivedAt, u ? fixToPixel(u->posX) : -1,
                u ? fixToPixel(u->posY) : -1, centred ? "" : " (not near the tile centre)");
        if (arrivedAt < 0 || !centred) ++failures;
    }
    if (tested == 0) {
        SDL_Log("simtest: no ground unit on this map");
        return 1;
    }
    if (!runCombatTest(g)) ++failures;
    if (!runEconomyTest(g)) ++failures;
    if (!runProductionTest(g)) ++failures;
    if (!runMotionTest(g)) ++failures;
    if (!runSaveTest(g)) ++failures;
    if (!runModernTest(g)) ++failures;  // not original: Modern movement
    return failures ? 1 : 0;
}

void orderBattle(game::World& world) {
    using namespace d2k::game;
    for (int as = 0; as < kMaxSides; ++as) {
        Side& me = world.sides()[as];
        for (int i = me.firstUnit; i != -1; i = me.nextObject(i)) {
            Unit& u = me.units[i];
            const auto& t = world.unitTemplate(u);
            if (t.primaryWeapon < 0 || (u.flags & 0x40) || t.specialBehavior != 0) continue;
            int best = 1 << 30, bs = -1, bi = -1;
            for (int s = 0; s < kMaxSides; ++s) {
                if (world.diplomacy(as, s) != 1) continue;
                Side& es = world.sides()[s];
                for (int j = es.firstUnit; j != -1; j = es.nextObject(j)) {
                    const Unit& e = es.units[j];
                    const int beh = world.unitTemplate(e).specialBehavior;
                    if ((e.flags & 0x400140) || beh == 5 || beh == 2 || beh == 8) continue;
                    const int d = std::abs(e.blockFromX - u.blockFromX) + std::abs(e.blockFromY - u.blockFromY);
                    if (d < best) { best = d; bs = s; bi = j; }
                }
            }
            if (bs >= 0) world.orderAttackUnit(as, i, bs, bi);
        }
    }
}

void vfxDemoTick(game::World& world, int cx, int cy) {
    using namespace d2k::game;
    const auto& T = *world.rules().templates;
    auto weapon = [&](const char* name) {
        for (int i = 0; i < formats::kMaxWeapons; ++i)
            if (std::string(T.bin.weaponNames[i], strnlen(T.bin.weaponNames[i], sizeof T.bin.weaponNames[i])) == name) return i;
        return -1;
    };
    auto explosion = [&](const char* name) {
        for (std::size_t i = 0; i < T.explosionNames.size(); ++i)
            if (T.explosionNames[i] == name) return int(i);
        return -1;
    };
    const std::uint32_t t = world.gameTicks();
    // D2K_VFX_DEMO_SITE=x,y: the demo's tile instead of --at (which also places the camera, from
    // the output size in pixels: on a HiDPI Original view the two do not coincide).
    if (const char* site = std::getenv("D2K_VFX_DEMO_SITE")) {
        int sx = 0, sy = 0;
        if (std::sscanf(site, "%d,%d", &sx, &sy) == 2) {
            cx = sx * 32;
            cy = sy * 32;
        }
    }
    // D2K_VFX_DEMO=units: a column of real units (side 0) shooting at the ground 2 tiles east
    // (attack-tile orders: the real FireAt path with its reload delays, muzzle points and
    // instant-hit machine-gun bullets) instead of the synthetic bullet rows.
    // D2K_VFX_DEMO=craters: terrain damage levels 1..15 written straight into the cells (one row
    // of sand cells, one of rock cells near the tile), nothing else - a crater look check.
    // D2K_VFX_DEMO=base: a small base of side 0 on concrete (buildings placed with their slabs, an
    // open apron of concrete next to them), laid at tick 1 - a concrete look check. Needs build-on
    // ground around the tile (H5V1: --at 6 8).
    if (const char* mode = std::getenv("D2K_VFX_DEMO"); mode && std::string(mode) == "base") {
        if (t != 1) return;
        auto building = [&](const char* name) {
            for (int i = 0; i < int(T.bin.buildingCount); ++i)
                if (std::string(T.bin.buildingNames[i]) == name) return i;
            return -1;
        };
        const int ax = cx / 32 - 5, ay = cy / 32 - 4;
        struct B { const char* name; int dx, dy; };
        static const B kBase[] = {{"HARKONNEN CONSTRUCTION YARD", 0, 0}, {"HARKONNEN WIND TRAP", 4, 0},
                                  {"HARKONNEN BARRACKS", 7, 0},          {"HARKONNEN REFINERY", 0, 4},
                                  {"HARKONNEN WIND TRAP", 4, 4}};
        for (const B& b : kBase)
            if (const int type = building(b.name); type >= 0) world.addBuilding(0, type, ax + b.dx, ay + b.dy, true, true);
        // An apron of bare slabs (2x2 concrete pieces).
        if (const int conc = building("HARKONNEN CONCRETE"); conc >= 0)
            for (int y = 0; y < 4; y += 2)
                for (int x = 0; x < 6; x += 2) world.addConcrete(0, conc, ax + 7 + x, ay + 4 + y, 0x33);
        return;
    }
    // D2K_VFX_DEMO=slabs: an 8 x 4 apron of concrete slabs (side 0) laid at tick 1 west of the
    // tile - and with D2K_VFX_DEMO=slabs-built a wind trap built in the middle of it at tick 12
    // (after the slabs were drawn): the slabs must look the same with and without the building.
    if (const char* mode = std::getenv("D2K_VFX_DEMO"); mode && (std::string(mode) == "slabs" || std::string(mode) == "slabs-built")) {
        auto building = [&](const char* name) {
            for (int i = 0; i < int(T.bin.buildingCount); ++i)
                if (std::string(T.bin.buildingNames[i]) == name) return i;
            return -1;
        };
        const int ax = cx / 32 - 4, ay = cy / 32 - 2;
        if (t == 1) {
            if (const int conc = building("HARKONNEN CONCRETE"); conc >= 0)
                for (int y = 0; y < 4; y += 2)
                    for (int x = 0; x < 8; x += 2) world.addConcrete(0, conc, ax + x, ay + y, 0x33);
        } else if (t == 12 && std::string(mode) == "slabs-built") {
            if (const int type = building("HARKONNEN WIND TRAP"); type >= 0) {
                const int idx = world.addBuilding(0, type, ax + 3, ay + 1, true, true);
                SDL_Log("vfx demo: wind trap %d built on the slabs at tick %u", idx, t);
            }
        }
        return;
    }
    if (const char* mode = std::getenv("D2K_VFX_DEMO"); mode && std::string(mode) == "craters") {
        if (t != 1) return;
        for (int pass = 0; pass < 2; ++pass) {
            const std::uint32_t want = pass == 0 ? 0x10000u : 0x8000u;
            int level = 1;
            for (int ty = cy / 32 - 6; ty < cy / 32 + 8 && level <= 15; ty += 2)
                for (int tx = cx / 32 - 8; tx < cx / 32 + 8 && level <= 15; tx += 2) {
                    if (!world.inBounds(tx, ty)) continue;
                    auto& c = world.cell(tx, ty);
                    if (!(c.flags & want) || (pass == 1 && (c.flags & 0x10000)) || c.damage) continue;
                    c.damage = std::uint8_t(level * 16 + 1);
                    ++level;
                }
        }
        return;
    }
    auto unitType = [&](const char* name) {
        for (int i = 0; i < int(T.bin.unitCount); ++i)
            if (std::string(T.bin.unitNames[i]) == name) return i;
        return -1;
    };
    auto buildingType = [&](const char* name) {
        for (int i = 0; i < int(T.bin.buildingCount); ++i)
            if (std::string(T.bin.buildingNames[i]) == name) return i;
        return -1;
    };
    const std::string demo = std::getenv("D2K_VFX_DEMO") ? std::getenv("D2K_VFX_DEMO") : "";
    // D2K_VFX_DEMO=harvest: two harvesters of the player put on the nearest spice cells (they
    // harvest through the real state machine: EX_HARVDUST per load step).
    if (demo == "harvest") {
        // Two harvesters 3 tiles apart on the spice, sent to harvest where they stand (a move
        // order onto spice = MovingToHarvest), a refinery 5 tiles east to unload in.
        static int h[2] = {-1, -1};
        const int me = world.mySide(), bx = cx / 32, by = cy / 32;
        if (t == 1) {
            const int type = unitType("HARVESTER");
            for (int k = 0; k < 2 && type >= 0; ++k) h[k] = world.addUnit(me, type, bx - 2 + 3 * k, by + k, bx - 2 + 3 * k, by + k);
            if (const int ref = buildingType("HARKONNEN REFINERY"); ref >= 0) world.addBuilding(me, ref, bx + 4, by - 2, false, true);
        }
        if (t == 3)
            for (int k = 0; k < 2; ++k)
                if (h[k] >= 0) world.orderMove(me, h[k], bx - 2 + 3 * k, by + k);
        return;
    }
    // D2K_VFX_DEMO=infantry: a squad of every infantry type for the player and one for an enemy
    // side, 8 tiles apart; at tick 2 the armed ones attack their opposite number (walk, fire, lie
    // down and crawl under fire, die, fidget), the unarmed ones walk past - the infantry animation
    // check (3D models: docs/3d-models.md "Animation"). D2K_VFX_DEMO_ROWS=N: N squads (stress).
    if (demo == "infantry") {
        static std::vector<std::array<int, 4>> pairs;  // type, my index, foe index, foe side
        const int me = world.mySide(), bx = cx / 32, by = cy / 32;
        if (t == 1) {
            int foe = -1;
            for (int s = 0; s < kMaxSides && foe < 0; ++s)
                if (s != me && world.diplomacy(me, s) == 1) foe = s;
            if (foe < 0) foe = (me + 1) % kMaxSides;
            static const char* kInf[] = {"LIGHT INF", "TROOPER", "GRENADIER", "FREMEN", "SARDAUKAR", "ENGINEER", "THUMPER", "SABOTEUR"};
            const char* rowsEnv = std::getenv("D2K_VFX_DEMO_ROWS");
            const int rows = std::clamp(rowsEnv ? std::atoi(rowsEnv) : 1, 1, 12);
            for (int r = 0; r < rows; ++r)
                for (int k = 0; k < int(std::size(kInf)); ++k) {
                    const int type = unitType(kInf[k]);
                    if (type < 0) continue;
                    const int y = by - 4 + k, xa = bx - 4 - r, xb = bx + 4 + r;
                    const int a = world.addUnit(me, type, xa, y, xa, y), b = world.addUnit(foe, type, xb, y, xb, y);
                    pairs.push_back({type, a, b, foe});
                }
        }
        if (t == 2)
            for (const auto& p : pairs) {
                const bool armed = T.bin.units[p[0]].primaryWeapon >= 0;
                if (p[1] < 0 || p[2] < 0) continue;
                if (armed) {
                    world.orderAttackUnit(me, p[1], p[3], p[2]);
                } else {
                    const Unit& u = world.sides()[me].units[p[1]];
                    world.orderMove(me, p[1], u.blockFromX + 9, u.blockFromY);
                }
            }
        return;
    }
    // D2K_VFX_DEMO=buildings: the animated buildings of the player in a row (Atreides construction
    // yard, outpost, silo): the yard's crane plays its build animation every 200 ticks (the flag a
    // placed building sets, spawn.cpp), the side's spice steps through the silo fill levels every
    // 100 ticks - the building animation check (3D model poses: docs/3d-models.md "Animation").
    if (demo == "buildings") {
        static int yard = -1;
        const int me = world.mySide(), bx = cx / 32, by = cy / 32;
        if (t == 1) {
            if (const int ty = buildingType("ATREIDES CONSTRUCTION YARD"); ty >= 0) yard = world.addBuilding(me, ty, bx - 5, by - 1, true, true);
            if (const int ty = buildingType("ATREIDES OUTPOST"); ty >= 0) world.addBuilding(me, ty, bx - 1, by - 1, true, true);
            if (const int ty = buildingType("ATREIDES SILO"); ty >= 0) world.addBuilding(me, ty, bx + 3, by, true, true);
        }
        if (yard >= 0 && t % 200 == 20) world.sides()[me].buildings[yard].flags |= 0x20;  // animate once
        if (t % 100 == 0) {
            Side& sd = world.sides()[me];
            sd.spice = sd.maxStorage * int((t / 100) % 4) / 3;
        }
        return;
    }
    // D2K_VFX_DEMO=allbuildings: every building of one house (D2K_VFX_DEMO_HOUSE=ATREIDES|HARKONNEN|
    // ORDOS|..., default ATREIDES; plus the house-less ones: turrets, repair pad, ...) packed side by
    // side with no gap, rows touching, from the tile east / south (silos in the free top corners of
    // the 3 x 4 buildings) - the 3D model footprint check (docs/3d-models.md "Placement"). Every
    // 30 ticks a light infantry / a trike of the player is sent along the walkable front row of the
    // first two building rows (3D decks: they must stay on the ground).
    // D2K_VFX_DEMO=walls: walls of the player's house (D2K_VFX_DEMO_HOUSE) in every link configuration - a single
    // post, straight runs, a ring (corners), a cross, T junctions, ends - the redesigned wall check (tools/models/wall.py).
    // D2K_VFX_DEMO=exits: units leaving their buildings (the 3D models' doors vs the original exit cells): a barracks,
    // light factory, heavy factory, star port and repair pad of the player's house (D2K_VFX_DEMO_HOUSE) in a row,
    // instant build; from tick 20 every 120 ticks an infantry, a trike and a combat tank are ordered, a frigate
    // delivery is bought, a damaged quad is sent to the pad; tick 40 an MCV deploys into a construction yard.
    if (demo == "exits") {
        // D2K_VFX_DEMO_DX=N shifts the row N tiles east (close-ups: 10 puts the barracks under the camera).
        const char* dxe = std::getenv("D2K_VFX_DEMO_DX");
        const int me = world.mySide(), bx = cx / 32 + (dxe ? std::atoi(dxe) : 0), by = cy / 32;
        const char* he = std::getenv("D2K_VFX_DEMO_HOUSE");
        const std::string house = he && *he ? he : "ATREIDES";
        static int pad = -1, port = -1;
        if (t == 1) {
            world.orderCheatInstantBuild(me, true);
            world.orderCheatSetCredits(me, 100000);
            world.orderCheatTechLevel(me, 9);
            const char* kinds[] = {"BARRACKS", "LIGHT FACTORY", "HEAVY FACTORY", "STAR PORT"};
            int x = bx - 11;
            for (const char* k : kinds) {
                int ty = buildingType((house + " " + k).c_str());
                if (ty < 0) continue;
                const int bi = world.addBuilding(me, ty, x, by - 3, false, true, true);
                if (std::string(k) == "STAR PORT") port = bi;
                x += std::max(1, int(T.bin.buildings[ty].artWidth) / 32) + 2;
            }
            if (const int ty = buildingType((house + " REPAIR PAD").c_str()); ty >= 0)
                pad = world.addBuilding(me, ty, x, by - 2, false, true, true);
            for (int k = 0; k < 6; ++k)  // power
                if (const int wt = buildingType((house + " WIND TRAP").c_str()); wt >= 0)
                    world.addBuilding(me, wt, bx - 9 + 2 * k, by + 6, false, true, true);
            if (const int mcv = unitType("MCV"); mcv >= 0) world.addUnit(me, mcv, bx - 1, by + 2, bx - 1, by + 2);
        }
        if (port >= 0 && t % 200 == 100) world.sides()[me].buildings[port].flags |= 0x20;  // landing lights (as a frigate)
        // The bought deliveries come 40 ticks after the purchase instead of starportFrigateDelay (1500): the frigate
        // and the star port's landing lights (render::starportLanding) within a short demo.
        for (auto& d : world.sides()[me].deliveries)
            if (d.active && !d.started && d.type == 2 && d.time > world.gameTicks() + 40) d.time = world.gameTicks() + 40;
        if (t == 40)
            for (int i = world.sides()[me].firstUnit; i >= 0; i = world.sides()[me].nextObject(i))
                if (world.sides()[me].units[i].type == unitType("MCV")) world.orderDeploy(me, i);
        if (t >= 21 && t % 120 == 21) world.orderStarportPurchase(me);  // the pick (queued last tick) is in now
        if (t >= 20 && t % 120 == 20) {
            for (const char* u : {"LIGHT INF", "TRIKE", "ATREIDES COMBAT TANK", "HARKONNEN COMBAT TANK", "ORDOS COMBAT TANK"})
                if (const int ty = unitType(u); ty >= 0) world.orderBuildUnit(me, ty);  // (the side's own tank only)
            {  // a unit the star port has in stock (a quad when it can), purchased next tick (below)
                const Side& sd = world.sides()[me];
                int q = unitType("QUAD");
                if (q < 0 || sd.starportStock[std::size_t(q)] == 0)
                    for (const int ty : sd.starportTypes)
                        if (ty >= 0 && ty < formats::kMaxUnitTypes && sd.starportStock[std::size_t(ty)] > 0) {
                            q = ty;
                            break;
                        }
                if (q >= 0) world.orderStarportPick(me, q);
            }
            if (pad >= 0)
                if (const int q = unitType("QUAD"); q >= 0) {
                    const int u = world.addUnit(me, q, bx + 4, by + 3, bx + 4, by + 3);
                    if (u >= 0) {
                        world.sides()[me].units[u].health = 20;
                        world.orderRepairUnit(me, u, pad);
                    }
                }
        }
        return;
    }
    if (demo == "walls") {
        if (t != 1) return;
        const int me = world.mySide(), bx = cx / 32, by = cy / 32;
        const char* he = std::getenv("D2K_VFX_DEMO_HOUSE");
        const int wall = buildingType(((he && *he ? std::string(he) : std::string("ATREIDES")) + " WALL").c_str());
        if (wall < 0) return;
        bool damaged = false;  // the T and the L: below half health (the damaged frames)
        auto put = [&](int x, int y) {
            const int i = world.addBuilding(me, wall, bx + x, by + y, false, true, true);
            if (damaged && i >= 0) world.sides()[me].buildings[i].health = std::int32_t(T.bin.buildings[wall].hitPoints) / 3;
        };
        put(-7, -4);                                               // single
        for (int x = -5; x <= -2; ++x) put(x, -4);                 // east-west run
        for (int y = -5; y <= -2; ++y) put(1, y);                  // north-south run
        for (int k = 0; k < 4; ++k) put(-7 + k, 0), put(-7 + k, 3), put(-7, k), put(-4, k);  // a ring
        for (int k = -1; k <= 1; ++k) put(2 + k, 1), put(2, 1 + k);  // a cross
        damaged = true;
        for (int k = 0; k < 3; ++k) put(5 + k, -1);                // a T
        put(6, 0), put(6, 1);
        put(5, 3), put(6, 3), put(6, 4);                           // an L
        return;
    }
    if (demo == "allbuildings") {
        // D2K_VFX_DEMO_DX / _DY=N: the layout N tiles east / south of the camera tile (close-ups).
        const char* dxe = std::getenv("D2K_VFX_DEMO_DX");
        const char* dye = std::getenv("D2K_VFX_DEMO_DY");
        const int me = world.mySide(), bx = cx / 32 + (dxe ? std::atoi(dxe) : 0), by = cy / 32 + (dye ? std::atoi(dye) : 0);
        if (t == 1) {
            const char* he = std::getenv("D2K_VFX_DEMO_HOUSE");
            const std::string house = he && *he ? he : "ATREIDES";
            static const char* kHouses[] = {"ATREIDES", "HARKONNEN", "ORDOS", "FREMEN", "SARDAUKAR", "IMPERIAL", "MERCENARY",
                                            "SMUGGLER", "SPECIAL"};
            std::vector<int> types;
            for (int i = 0; i < int(T.bin.buildingCount); ++i) {
                const std::string n = T.bin.buildingNames[i];
                if (n.find("CONCRETE") != std::string::npos || n.find("WALL") != std::string::npos) continue;
                bool other = false, mine = n.rfind(house + " ", 0) == 0;
                for (const char* h : kHouses) other |= n.rfind(std::string(h) + " ", 0) == 0;
                if (mine || !other) types.push_back(i);
            }
            const int silo = buildingType((house + " SILO").c_str());
            const int x0 = bx - 8;
            int x = x0, y = by - 5, rowH = 0;
            const int rowW = 16;
            for (const int ty : types) {
                const auto& bt = T.bin.buildings[ty];
                const int w = std::max(1, int(bt.artWidth) / 32), h = std::max(1, int(bt.artHeight) / 32);
                if (x + w > x0 + rowW) x = x0, y += rowH, rowH = 0;
                const int idx = world.addBuilding(me, ty, x, y, false, true, true);
                SDL_Log("allbuildings: %s at %d,%d (%dx%d) -> %d", T.bin.buildingNames[ty], x, y, w, h, idx);
                if (silo >= 0 && h == 4 && bt.tilesOccupiedAll == 0x7772) {
                    world.addBuilding(me, silo, x, y, false, true, true);
                    world.addBuilding(me, silo, x + 2, y, false, true, true);
                }
                // D2K_VFX_DEMO_PARK=1: a unit parked on every walkable cell of the building (the
                // 3D "Walkable cells" check): combat tank, trike, light infantry in turn.
                if (const char* pk = std::getenv("D2K_VFX_DEMO_PARK"); pk && pk[0] == '1' && idx >= 0) {
                    static int turn = 0;
                    const int kinds[] = {unitType((house + " COMBAT TANK").c_str()), unitType("TRIKE"), unitType("LIGHT INF")};
                    const std::uint32_t walk = bt.tilesOccupiedAll & ~bt.tilesOccupiedSolid;
                    for (int c = 0; c < 16; ++c)
                        if (walk & (1u << c)) {
                            const int k = kinds[turn++ % 3];
                            if (k >= 0) world.addUnit(me, k, x + c % 4, y + c / 4, x + c % 4, y + c / 4);
                        }
                }
                x += w;
                rowH = std::max(rowH, h);
            }
            // D2K_VFX_DEMO_POWER=N: N more wind traps in a row below (full power: the lights' powered look).
            if (const char* pw = std::getenv("D2K_VFX_DEMO_POWER"); pw && std::atoi(pw) > 0) {
                const int wt = buildingType((house + " WIND TRAP").c_str());
                y += rowH;
                for (int k = 0; wt >= 0 && k < std::atoi(pw); ++k) world.addBuilding(me, wt, x0 + 2 * k, y, false, true, true);
            }
        }
        // D2K_VFX_DEMO_WORK=1: every building working as the simulation marks it (the working animations check,
        // docs/3d-models.md "Working lamps"): the factories, barracks and palaces hold the producing flag
        // (kAnimPermanent), the construction yard / star port / repair pad / Harkonnen palace play their once-animation
        // (kAnimOnce: a placement, a frigate, a repair step, a Death Hand launch) every 90 ticks; the silos' fill steps.
        if (const char* wk = std::getenv("D2K_VFX_DEMO_WORK"); wk && wk[0] == '1') {
            Side& sd = world.sides()[me];
            for (int i = sd.firstBuilding; i >= 0; i = sd.nextObject(i)) {
                Building& b = sd.buildings[std::size_t(i)];
                const int sb = T.bin.buildings[b.type].specialBehavior;
                const bool once = sb == 2 || sb == 6 || sb == 13 || sb == 17;  // yard, repair pad, star port, H palace
                if (once) {
                    if (t % 90 == 10) b.flags |= game::building_flags::kAnimOnce;
                } else if (sb >= 7 || sb == 11) {
                    b.flags |= game::building_flags::kAnimPermanent;  // factories, barracks, palaces
                }
            }
            if (t % 100 == 0) sd.spice = sd.maxStorage * int((t / 100) % 4) / 3;
        }
        const char* park = std::getenv("D2K_VFX_DEMO_PARK");
        if (t % 30 == 2 && t < 400 && !(park && park[0] == '1')) {  // along the walkable front rows of the first two building rows
            const int inf = unitType("LIGHT INF"), trike = unitType("TRIKE");
            const int k = int(t / 30);
            const int ty = (k & 1) ? trike : inf, row = (k / 2) % 2 == 0 ? by - 3 : by + 1;
            if (ty >= 0) {
                const int u = world.addUnit(me, ty, bx - 9, row, bx - 9, row);
                if (u >= 0) world.orderMove(me, u, bx + 9, row);
            }
        }
        return;
    }
    // D2K_VFX_DEMO=factories: the player's factories working (the "producing" animation flag
    // kAnimPermanent held on every tick, as production sets it): the five heavy factories in a row
    // east of the tile (D2K_VFX_DEMO_HOUSE=light: the light / high tech factories of the three
    // houses and the Harkonnen research centre instead; D2K_VFX_DEMO_DX=N shifts the row N tiles east, for
    // close-ups) - the working overlay check (welding flashes, docs/3d-models.md "Anchors"). Needs open
    // ground (H5V1: --at 21 16).
    if (demo == "factories") {
        static std::vector<int> made;
        const int me = world.mySide(), bx = cx / 32, by = cy / 32;
        if (t == 1) {
            const char* he = std::getenv("D2K_VFX_DEMO_HOUSE");
            const bool light = he && std::string(he) == "light";
            static const char* kHeavy[] = {"ATREIDES HEAVY FACTORY", "HARKONNEN HEAVY FACTORY", "ORDOS HEAVY FACTORY",
                                           "MERCENARY HEAVY FACTORY", "IMPERIAL HEAVY FACTORY"};
            static const char* kLight[] = {"ATREIDES LIGHT FACTORY",      "HARKONNEN LIGHT FACTORY",
                                           "ORDOS LIGHT FACTORY",         "ATREIDES HIGH TECH FACTORY",
                                           "HARKONNEN HIGH TECH FACTORY", "ORDOS HIGH TECH FACTORY",
                                           "HARKONNEN RESEARCH CENTRE"};
            int x = light ? bx - 10 : bx - 8;
            if (const char* dx = std::getenv("D2K_VFX_DEMO_DX")) x += std::atoi(dx);  // shift the row (close-ups)
            for (const char* name : light ? std::vector<const char*>(std::begin(kLight), std::end(kLight))
                                          : std::vector<const char*>(std::begin(kHeavy), std::end(kHeavy))) {
                const int ty = buildingType(name);
                if (ty < 0) continue;
                const int w = std::max(1, int(T.bin.buildings[ty].artWidth) / 32);
                const int idx = world.addBuilding(me, ty, x, by - 2, false, true, true);
                if (idx >= 0) made.push_back(idx);
                x += w;
            }
            // D2K_VFX_DEMO_POWER=N: N Harkonnen wind traps in a row further south (full power: the glow's
            // powered look, e.g. the Harkonnen research centre's blinking dome lights).
            if (const char* pw = std::getenv("D2K_VFX_DEMO_POWER"); pw && std::atoi(pw) > 0)
                if (const int wt = buildingType("HARKONNEN WIND TRAP"); wt >= 0)
                    for (int k = 0; k < std::atoi(pw); ++k) world.addBuilding(me, wt, bx - 12 + 2 * k, by + 5, false, true, true);
        }
        for (const int i : made) world.sides()[me].buildings[i].flags |= game::building_flags::kAnimPermanent;
        return;
    }
    // D2K_VFX_DEMO=ornithopter: an air strike in view - three ornithopters of the player set up as
    // Ornithopter_LaunchStrike 0x49e770 does (12 bombs, flags 0x10800, Moving), but starting 9 / 11 / 13
    // tiles west of the tile, every 300 ticks: their wing beat (unit art rows 0-1-2,
    // FlyUnit) is the 3D row-pose check (docs/3d-models.md "Animation").
    // D2K_VFX_DEMO=airdefence: the same strike flying into an enemy position under the shroud -
    // at tick 1 an enemy side (the first one at war with the player) gets three rocket turrets
    // (LARGE GUN TURRET, powered by two wind traps) and four troopers around the tile. The
    // shooters' cells are revealed by FireAt (shooter's cell cleared when the target is the
    // player's), the shroud check of re/notes/shroud.md §7 (radar vs battlefield).
    if (demo == "airdefence" && t == 1) {
        const int me = world.mySide(), bx = cx / 32, by = cy / 32;
        int enemy = -1;
        for (int s = 0; s < kMaxSides && enemy < 0; ++s)
            if (s != me && world.diplomacy(me, s) == 1 && world.sides()[s].participates && world.sides()[s].firstBuilding != -1)
                enemy = s;
        SDL_Log("airdefence: enemy side %d", enemy);
        auto building = [&](const char* name) {
            for (int i = 0; i < int(T.bin.buildingCount); ++i)
                if (std::string(T.bin.buildingNames[i]) == name) return i;
            return -1;
        };
        if (enemy >= 0) {
            const int trap = building("HARKONNEN WIND TRAP"), tur = building("HARKONNEN LARGE GUN TURRET");
            if (trap >= 0) {
                world.addBuilding(enemy, trap, bx + 3, by - 4, false, true);
                world.addBuilding(enemy, trap, bx + 5, by - 4, false, true);
            }
            static const int kTur[][2] = {{-2, -1}, {2, 1}, {0, 3}};
            if (tur >= 0)
                for (const auto& p : kTur) world.addBuilding(enemy, tur, bx + p[0], by + p[1], false, true);
            if (const int tr = unitType("TROOPER"); tr >= 0)
                for (int k = 0; k < 4; ++k) world.addUnit(enemy, tr, bx - 3 + 2 * k, by + 5, bx - 3 + 2 * k, by + 5);
        }
        // The radar on: D2K_VFX_DEMO_RADAR=x,y puts a player outpost + two wind traps there.
        if (const char* rp = std::getenv("D2K_VFX_DEMO_RADAR")) {
            int rx = 0, ry = 0;
            if (std::sscanf(rp, "%d,%d", &rx, &ry) == 2) {
                if (const int op = building("HARKONNEN OUTPOST"); op >= 0) world.addBuilding(me, op, rx, ry, false, true);
                if (const int trap = building("HARKONNEN WIND TRAP"); trap >= 0) {
                    world.addBuilding(me, trap, rx + 3, ry, false, true);
                    world.addBuilding(me, trap, rx + 5, ry, false, true);
                }
            }
        }
        for (int i = world.sides()[me].firstBuilding; i != -1; i = world.sides()[me].nextObject(i)) {
            const Building& b = world.sides()[me].buildings[i];
            SDL_Log("airdefence: player building %d at tile %d,%d", int(b.type), (b.posX >> 16) / 32, (b.posY >> 16) / 32);
        }
    }
    if (demo == "airdefence" && std::getenv("D2K_VFX_DEMO_LOG") && t % 10 == 0) {
        // The shroud bytes of the 9 x 9 cells around the tile (".." = 0, else hex).
        const int bx = cx / 32, by = cy / 32;
        std::string rows;
        for (int y = by - 4; y <= by + 5; ++y) {
            rows += "\n  ";
            for (int x = bx - 4; x <= bx + 4; ++x) {
                char b[4];
                const int v = world.inBounds(x, y) ? world.cell(x, y).shroud : 0x11;
                std::snprintf(b, sizeof b, v ? "%02x " : ".. ", v);
                rows += b;
            }
        }
        SDL_Log("airdefence t=%u shroud around %d,%d:%s", t, bx, by, rows.c_str());
    }
    if (demo == "ornithopter" || demo == "airdefence") {
        const int me = world.mySide(), bx = cx / 32, by = cy / 32;
        const int type = unitType("ORNITHOPTER");
        if (type >= 0 && t % 300 == 1)
            for (int k = 0; k < 3; ++k) {
                const int sx = bx - 9 - 2 * k, sy = by - 2 + 2 * k;
                const int i = world.addUnit(me, type, sx, sy, bx, by + k - 1);
                if (i < 0) continue;
                Unit& o = world.sides()[me].units[i];
                o.byte29 = 0xc;
                o.flags |= 0x10800;
                world.unitAdjustState(o, UnitState::Moving);
                o.facing = o.facingTurret = 8;  // east, towards the tile
                o.animDelay = std::uint8_t(k);  // not all in step
            }
        if (std::getenv("D2K_VFX_DEMO_LOG") && t % 10 == 0)
            for (int i = world.sides()[me].firstUnit; i != -1; i = world.sides()[me].nextObject(i)) {
                const Unit& u = world.sides()[me].units[i];
                if (u.type == type)
                    SDL_Log("ornithopter demo t=%u #%d state %d pos %d,%d z %d facing %d row %d flags %x", t, i, int(u.state), u.posX >> 16,
                            u.posY >> 16, u.posZ >> 16, int(u.facing), int(u.animFrame), unsigned(u.flags));
            }
        return;
    }
    // D2K_VFX_DEMO=carryall: a carryall reinforcement of one unit (D2K_VFX_DEMO_UNIT=name, default a
    // harvester) for the player to the tile at tick 1 (the cheat reinforcement: the .MIS Reinforcement
    // mechanism), entering from map edge D2K_VFX_DEMO_EDGE (0 N, 1 E, 2 S, 3 W; default the nearest) -
    // the 3D flight-height check over cliffs (docs/3d-mode.md "Flyers").
    if (demo == "carryall") {
        if (t != 1) return;
        const char* name = std::getenv("D2K_VFX_DEMO_UNIT") ? std::getenv("D2K_VFX_DEMO_UNIT") : "HARVESTER";
        const int edge = std::getenv("D2K_VFX_DEMO_EDGE") ? std::atoi(std::getenv("D2K_VFX_DEMO_EDGE")) : -1;
        if (const int type = unitType(name); type >= 0)
            world.orderCheatReinforce(world.mySide(), {std::uint8_t(type)}, 0, cx / 32, cy / 32, edge);
        else
            SDL_Log("carryall demo: no unit type '%s'", name);
        return;
    }
    // D2K_VFX_DEMO=refinery: a refinery of the player with two loaded harvesters next to it (7 and
    // 3 blobs) told to dock at tick 3 (orderDockAtRefinery: the real states 13-16; one EX_REF per
    // unloaded blob, harvestUnloadDelay apart); the second docks when the first has left.
    // D2K_VFX_DEMO_REFINERY="ATREIDES REFINERY" picks another refinery template (3D model checks).
    if (demo == "refinery") {
        static int h[2] = {-1, -1}, r = -1;
        const int me = world.mySide(), bx = cx / 32, by = cy / 32;
        if (t == 1) {
            const char* refName = std::getenv("D2K_VFX_DEMO_REFINERY");
            if (const int ref = buildingType(refName && *refName ? refName : "HARKONNEN REFINERY"); ref >= 0)
                r = world.addBuilding(me, ref, bx - 1, by - 2, true, true);
            const int type = unitType("HARVESTER");
            for (int k = 0; k < 2 && type >= 0; ++k) {
                h[k] = world.addUnit(me, type, bx - 2 + 4 * k, by + 3, bx - 2 + 4 * k, by + 3);
                if (h[k] >= 0) world.sides()[me].units[h[k]].byte29 = k == 0 ? 7 : 3;
            }
        }
        if (t == 3 && r >= 0)
            for (int k = 0; k < 2; ++k)
                if (h[k] >= 0) world.orderDockAtRefinery(me, h[k], r);
        if (t % 50 == 0 && std::getenv("D2K_VFX_DEMO_LOG"))
            for (int k = 0; k < 2; ++k)
                if (h[k] >= 0) {
                    const Unit& u = world.sides()[me].units[h[k]];
                    const Building& rb = world.sides()[me].buildings[r];
                    const auto& rt = world.buildingTemplate(rb);
                    SDL_Log("refinery demo t=%u harvester %d state %d load %d at %d,%d (refinery tile %d,%d exit %d,%d art %d)", t, k,
                            int(u.state), int(u.byte29), u.blockFromX, u.blockFromY, rb.posX >> 21, (rb.posY >> 16) >> 5,
                            int(rt.exitPoint1X), int(rt.exitPoint1Y), int(rt.artHeight));
                }
        return;
    }
    // D2K_VFX_DEMO=worm: a sandworm (side 7) awake 3 tiles west of the tile and three unarmed vehicles of the player
    // standing on the sand east of it (MCV, harvester, MCV): the worm hunts them through its real
    // state machine (states 27 / 28: sand particles and wormsign explosions while it moves, the swallow
    // explosions deathExplosion / +1 when it eats one), then the next. Needs open sand round the tile
    // (H5V1: --at 40 40). D2K_VFX_DEMO_LOG=1 logs the worm.
    if (demo == "worm") {
        static int wi = -1;
        constexpr int kWormSide = 7;
        const int bx = cx / 32, by = cy / 32;
        if (t == 1) {
            int wt = -1;
            for (int i = 0; i < int(T.bin.unitCount); ++i)
                if (T.bin.units[i].specialBehavior == game::behaviour::kSandworm) wt = i;
            if (wt >= 0) wi = world.addUnit(kWormSide, wt, bx - 3, by, bx - 3, by);
            if (wi >= 0) {
                Unit& w = world.sides()[kWormSide].units[wi];
                w.state = UnitState::Eating;  // awake: picks the nearest vehicle on sand next tick
                w.animDelay = 0;
            }
            const char* foes[3] = {"MCV", "HARVESTER", "MCV"};  // unarmed: nothing shoots the worm
            for (int k = 0; k < 3; ++k)
                if (const int ty = unitType(foes[k]); ty >= 0) world.addUnit(world.mySide(), ty, bx + 1 + 3 * k, by + (k & 1), bx + 1 + 3 * k, by + (k & 1));
        }
        if (wi >= 0 && t % 20 == 0 && std::getenv("D2K_VFX_DEMO_LOG")) {
            const Unit& w = world.sides()[kWormSide].units[wi];
            SDL_Log("worm demo t=%u state %d at %d,%d target %d,%d flags %x meals %d particles %d", t, int(w.state), w.posX >> 16,
                    w.posY >> 16, int(w.targetX), int(w.targetY), unsigned(w.flags), int(w.byte29), world.particleCount());
        }
        return;
    }
    // D2K_VFX_DEMO=mcv: three MCVs of the player - one driving 6 tiles east, one deploying at tick 120 (the
    // construction yard builds up), one standing - the lamp check for the redesigned MCV (models.ini fx_light_*).
    // Needs open buildable ground round the tile (H5V1: --at 6 8).
    if (demo == "mcv") {
        static int m[3] = {-1, -1, -1};
        const int me = world.mySide(), bx = cx / 32, by = cy / 32;
        if (t == 1)
            if (const int ty = unitType("MCV"); ty >= 0)
                for (int k = 0; k < 3; ++k) m[k] = world.addUnit(me, ty, bx - 3 + 3 * k, by + (k == 0 ? 3 : 0), bx - 3 + 3 * k, by + (k == 0 ? 3 : 0));
        if (t == 3 && m[0] >= 0) world.orderMove(me, m[0], bx + 6, by + 3);
        if (t == 120 && m[1] >= 0) SDL_Log("mcv demo: deploy %s", world.orderDeploy(me, m[1]) ? "ordered" : "refused");
        return;
    }
    // D2K_VFX_DEMO=bloom: spice blooms of every stage (7..10) in a row on bare sand near the tile,
    // through World::placeCrate (the original record). The ripe one bursts at tick 60 (SpiceMound:
    // spice shells, the bloom recycled to an invisible stage-7 seed on its cell); one stage-9 bloom grows at tick 30 (EX_SPICEBLOOM); every
    // 150 ticks another ripe bloom is placed a little further east and bursts 60 ticks later.
    if (demo == "bloom") {
        auto bare = [&](int x, int y) {
            return world.inBounds(x, y) && (world.cell(x, y).flags & 0x10000u) && !(world.cell(x, y).flags & 0x7013f8u);
        };
        auto place = [&](int x0, int y0, std::uint32_t timer, int type) {
            for (int k = 0; k < 12; ++k) {
                const int x = x0 + k % 4, y = y0 + k / 4;
                if (bare(x, y)) {
                    world.placeCrate(x, y, timer, type, 4 + (type - 7), 5);  // 5 regrowths, like map blooms
                    return;
                }
            }
        };
        const int bx = cx / 32, by = cy / 32;
        if (t == 1) {
            place(bx - 6, by - 1, 5000, 7);
            place(bx - 4, by - 1, 5000, 8);
            place(bx - 2, by - 1, 30, 9);
            place(bx + 0, by - 1, 60, 10);
        }
        if (t > 100 && t % 150 == 0) {
            const int k = int(t / 150) % 4;
            place(bx - 6 + k * 3, by + 3, 60, 10);
        }
        return;
    }
    // D2K_VFX_DEMO=damage: the player's buildings (wind trap, refinery, barracks, a row of walls,
    // concrete) and three tanks, brought below half health at tick 2 and hit once by a machine-gun
    // bullet (DamageTiles: the original smoke timers start); a second round of hits at tick 700
    // (the fire flares up again). The smoke dies out on its own in between.
    if (demo == "damage") {
        const int me = world.mySide();
        const int ax = cx / 32 - 5, ay = cy / 32 - 3;
        static std::vector<std::pair<int, int>> blds, units;  // (index, health percent)
        if (t == 1) {
            struct B { const char* name; int dx, dy, hp; };
            static const B kB[] = {{"HARKONNEN WIND TRAP", 0, 0, 45}, {"HARKONNEN REFINERY", 3, 0, 30},
                                   {"HARKONNEN BARRACKS", 7, 0, 18}};
            for (const B& b : kB)
                if (const int type = buildingType(b.name); type >= 0) {
                    const int i = world.addBuilding(me, type, ax + b.dx, ay + b.dy, true, true);
                    if (i >= 0) blds.push_back({i, b.hp});
                }
            if (const int wall = buildingType("HARKONNEN WALL"); wall >= 0)
                for (int k = 0; k < 6; ++k) {
                    const int i = world.addBuilding(me, wall, ax + k, ay + 5, false, true);
                    if (i >= 0) blds.push_back({i, 20 + 5 * k});
                }
            static const int kHp[3] = {45, 30, 15};
            if (const int tank = unitType("HARKONNEN COMBAT TANK"); tank >= 0)
                for (int k = 0; k < 3; ++k) {
                    const int i = world.addUnit(me, tank, ax + 7 + k * 2, ay + 5, ax + 7 + k * 2, ay + 5);
                    if (i >= 0) units.push_back({i, kHp[k]});
                }
        }
        const int lmg = weapon("LMG");
        if ((t == 2 || t == 700) && lmg >= 0) {
            Side& S = world.sides()[me];
            for (const auto& [i, hp] : blds) {
                Building& b = S.buildings[i];
                if (!b.alive) continue;
                const auto& bt = world.buildingTemplate(b);
                if (t == 2) b.health = std::int32_t(bt.hitPoints) * hp / 100;
                world.damageTiles(b.tileX * 32 + 16, b.tileY * 32 + 16 + (bt.artHeight > 32 ? 32 : 0), 0, lmg, (me + 1) & 7, -1,
                                  false);
            }
            for (const auto& [i, hp] : units) {
                Unit& u = S.units[i];
                if (!u.alive) continue;
                if (t == 2) u.health = std::int32_t(world.unitTemplate(u).hitPoints) * hp / 100;
                world.damageTiles(u.posX / 65536, u.posY / 65536, 0, lmg, (me + 1) & 7, -1, false);
            }
        }
        return;
    }
    // D2K_VFX_DEMO=fire: burning buildings of several houses (refinery, heavy factory, light factory,
    // barracks of Atreides / Harkonnen / Ordos) in two rows, the north row held at 40% health, the south
    // row at 15%, hit by a machine-gun bullet every 200 ticks (the original's smoke timers keep running:
    // the BUILEXP.BIN smoke / flame events go on) - the building fire look check (render/effects_fire.cpp).
    if (demo == "fire") {
        const int me = world.mySide();
        const int ax = cx / 32 - 7, ay = cy / 32 - 4;
        static std::vector<std::pair<int, int>> blds;  // (index, health percent)
        if (t == 1) {
            struct B { const char* name; int dx; };
            static const B kRow[] = {{"HARKONNEN REFINERY", 0}, {"ATREIDES HEAVY FACTORY", 4}, {"ORDOS LIGHT FACTORY", 8},
                                     {"HARKONNEN BARRACKS", 12}};
            for (int row = 0; row < 2; ++row)
                for (const B& b : kRow)
                    if (const int type = buildingType(b.name); type >= 0) {
                        const int i = world.addBuilding(me, type, ax + b.dx, ay + row * 4, true, true);
                        if (i >= 0) blds.push_back({i, row == 0 ? 40 : 15});
                    }
        }
        const int lmg = weapon("LMG");
        if (lmg >= 0 && (t == 2 || (t > 2 && t % 200 == 0))) {
            Side& S = world.sides()[me];
            for (const auto& [i, hp] : blds) {
                Building& b = S.buildings[i];
                if (!b.alive) continue;
                const auto& bt = world.buildingTemplate(b);
                b.health = std::int32_t(bt.hitPoints) * hp / 100;
                world.damageTiles(b.tileX * 32 + 16, b.tileY * 32 + 16 + (bt.artHeight > 32 ? 32 : 0), 0, lmg, (me + 1) & 7, -1,
                                  false);
                b.health = std::int32_t(bt.hitPoints) * hp / 100;
            }
        }
        return;
    }
    // D2K_VFX_DEMO=capture: a wind trap and a barracks of another side, two engineers of the
    // player next to them; at tick 100 (after the build-up) the engineers are sent in
    // (orderAttackBuilding: the real infiltrate + CaptureBuilding path). At tick 150 a third
    // building of that side is destroyed
    // (for the comparison: destruction vs capture).
    // D2K_VFX_DEMO=tankfight (effects scale check: hits on real tanks, wrecks, a building's death): two armoured
    // groups 7 tiles apart - the player's (combat tanks, a missile tank, a siege tank) and an unused side's
    // (the same, Harkonnen) with its wind trap and barracks behind it - set on each other at tick 20 (attack the
    // nearest); at tick 200 the wind trap is destroyed.
    if (demo == "tankfight") {
        const int me = world.mySide();
        static int other = -1;
        for (int s = 0; s < kMaxSides && other < 0; ++s)
            if (s != me && s != ((me + 1) & 7) && world.sides()[s].firstBuilding < 0 && world.sides()[s].firstUnit < 0) other = s;
        if (other < 0) return;
        static int wt = -1;
        const int bx = cx / 32, by = cy / 32;
        if (t == 1) {
            world.setDiplomacy(me, other, 1);
            world.setDiplomacy(other, me, 1);
            const char* mine[] = {"ATREIDES COMBAT TANK", "ATREIDES COMBAT TANK", "MISSILE TANK", "ATREIDES COMBAT TANK", "SIEGE TANK"};
            const char* theirs[] = {"HARKONNEN COMBAT TANK", "HARKONNEN COMBAT TANK", "SIEGE TANK", "HARKONNEN COMBAT TANK", "MISSILE TANK"};
            for (int k = 0; k < 5; ++k) {
                if (const int ty = unitType(mine[k]); ty >= 0) world.addUnit(me, ty, bx - 4, by - 2 + k, bx - 4, by - 2 + k);
                if (const int ty = unitType(theirs[k]); ty >= 0) world.addUnit(other, ty, bx + 3, by - 2 + k, bx + 3, by - 2 + k);
            }
            if (const int w = buildingType("HARKONNEN WIND TRAP"); w >= 0) wt = world.addBuilding(other, w, bx + 5, by - 4, false, true);
            if (const int b = buildingType("HARKONNEN BARRACKS"); b >= 0) world.addBuilding(other, b, bx + 5, by + 1, false, true);
        }
        if (t == 20) orderBattle(world);
        if (t > 20 && t % 60 == 0) orderBattle(world);  // the survivors pick new targets
        if (t == 200 && wt >= 0)
            if (Building* b = world.building(other, wt)) world.destroyBuilding(*b, false);
        return;
    }
    // D2K_VFX_DEMO=bigfight (the effects intensity / readability check, docs/enhanced-graphics.md "Effects
    // intensity"): the tank fight at scale - 32 tanks per side (mostly combat tanks, some missile and siege
    // tanks) in two 4 x 8 blocks 10 tiles apart, set on each other at tick 20 (attack the nearest, survivors
    // re-target every 60 ticks); a wind trap and a barracks behind the other side are destroyed at ticks 260 / 420.
    if (demo == "bigfight") {
        const int me = world.mySide();
        static int other = -1;
        for (int s = 0; s < kMaxSides && other < 0; ++s)
            if (s != me && s != ((me + 1) & 7) && world.sides()[s].firstBuilding < 0 && world.sides()[s].firstUnit < 0) other = s;
        if (other < 0) return;
        static int wt = -1, br = -1;
        const int bx = cx / 32, by = cy / 32;
        if (t == 1) {
            world.setDiplomacy(me, other, 1);
            world.setDiplomacy(other, me, 1);
            for (int r = 0; r < 8; ++r)
                for (int c = 0; c < 4; ++c) {
                    const int k = r * 4 + c;
                    const char* mine = k % 8 == 3 ? "MISSILE TANK" : k % 8 == 6 ? "SIEGE TANK" : "ATREIDES COMBAT TANK";
                    const char* theirs = k % 8 == 2 ? "SIEGE TANK" : k % 8 == 5 ? "MISSILE TANK" : "HARKONNEN COMBAT TANK";
                    if (const int ty = unitType(mine); ty >= 0) world.addUnit(me, ty, bx - 8 + c, by - 4 + r, bx - 8 + c, by - 4 + r);
                    if (const int ty = unitType(theirs); ty >= 0) world.addUnit(other, ty, bx + 5 + c, by - 4 + r, bx + 5 + c, by - 4 + r);
                }
            if (const int w = buildingType("HARKONNEN WIND TRAP"); w >= 0) wt = world.addBuilding(other, w, bx + 10, by - 6, false, true);
            if (const int b = buildingType("HARKONNEN BARRACKS"); b >= 0) br = world.addBuilding(other, b, bx + 10, by + 2, false, true);
        }
        if (t == 20) orderBattle(world);
        if (t > 20 && t % 60 == 0) orderBattle(world);
        if (t == 260 && wt >= 0)
            if (Building* b = world.building(other, wt)) world.destroyBuilding(*b, false);
        if (t == 420 && br >= 0)
            if (Building* b = world.building(other, br)) world.destroyBuilding(*b, false);
        return;
    }
    // D2K_VFX_DEMO=single (effects intensity: a calm scene must look as before): single events in an empty area,
    // well apart in time - a rocket onto the sand at tick 40, an 80 mm tank shell at tick 160, a howitzer shell at
    // tick 280 and a parked enemy combat tank killed by one rocket at tick 400.
    if (demo == "single") {
        const int me = world.mySide();
        static int other = -1, tank = -1;
        for (int s = 0; s < kMaxSides && other < 0; ++s)
            if (s != me && s != ((me + 1) & 7) && world.sides()[s].firstBuilding < 0 && world.sides()[s].firstUnit < 0) other = s;
        if (other < 0) return;
        const int bx = cx / 32, by = cy / 32;
        if (t == 1) {
            world.setDiplomacy(me, other, 1);
            if (const int ty = unitType("HARKONNEN COMBAT TANK"); ty >= 0) tank = world.addUnit(other, ty, bx + 2, by + 2, bx + 2, by + 2);
        }
        auto shoot = [&](const char* name, int tx, int ty) {
            if (const int w = weapon(name); w >= 0) world.addBullet(me, w, 0, -1, tx - 300, ty, tx, ty);
        };
        if (t == 40) shoot("ROCKET", cx - 64, cy);
        if (t == 160) shoot("80MM GUN", cx - 64, cy);
        if (t == 280) shoot("155MM HOWITZER", cx - 64, cy);
        if (t == 400 && tank >= 0) {
            Unit& u = world.sides()[other].units[tank];
            if (u.alive) {
                u.health = 1;
                shoot("ROCKET", game::fixToPixel(u.posX), game::fixToPixel(u.posY));
            }
        }
        return;
    }
    // D2K_VFX_DEMO=artillery (the artillery hit check): every 90 ticks, left to right, a row of 155 mm howitzer
    // shells, a row of 80 mm tank shells and a row of rockets onto the ground (comparison), two grenadiers throwing
    // at the ground, and a howitzer shell onto a parked enemy combat tank (kept alive: the hit on armour).
    if (demo == "artillery") {
        const int me = world.mySide();
        static int other = -1, tank = -1;
        for (int s = 0; s < kMaxSides && other < 0; ++s)
            if (s != me && s != ((me + 1) & 7) && world.sides()[s].firstBuilding < 0 && world.sides()[s].firstUnit < 0) other = s;
        if (other < 0) return;
        const int bx = cx / 32, by = cy / 32;
        if (t == 1) {
            world.setDiplomacy(me, other, 1);
            if (const int ty = unitType("HARKONNEN COMBAT TANK"); ty >= 0) tank = world.addUnit(other, ty, bx + 3, by + 3, bx + 3, by + 3);
        }
        if (tank >= 0) {
            Unit& u = world.sides()[other].units[tank];
            if (u.alive) u.health = std::max<std::int32_t>(u.health, 200);
        }
        static int gren[2] = {-1, -1};
        if (t == 1)
            for (int k = 0; k < 2; ++k)
                if (const int ty = unitType("GRENADIER"); ty >= 0) gren[k] = world.addUnit(me, ty, bx - 6, by - 1 + k, bx - 6, by - 1 + k);
        if (t % 90 == 30)
            for (int k = 0; k < 2; ++k)
                if (gren[k] >= 0 && world.sides()[me].units[gren[k]].alive) world.orderAttackTile(me, gren[k], bx - 3 + k, by - 1);
        static const char* kRows[] = {"155MM HOWITZER", "", "80MM GUN", "ROCKET"};
        if (t % 90 == 30)
            for (int r = 0; r < int(std::size(kRows)); ++r)
                if (const int w = *kRows[r] ? weapon(kRows[r]) : -1; w >= 0) {
                    const int y = cy - 110 + r * 64;
                    world.addBullet(me, w, 0, -1, cx - 330, y, cx - 40 + (r & 1) * 30, y);
                }
        if (t % 90 == 75 && tank >= 0)
            if (const int w = weapon("155MM HOWITZER"); w >= 0) {
                const Unit& u = world.sides()[other].units[tank];
                const int tx = game::fixToPixel(u.posX), ty = game::fixToPixel(u.posY);
                world.addBullet(me, w, 0, -1, tx - 290, ty, tx, ty);
            }
        return;
    }
    // D2K_VFX_DEMO=wrecks (the vehicle wrecks check, docs/3d-mode.md "Vehicle wrecks"): a row of parked vehicles of
    // an unused side 2 tiles apart, west to east from 6 tiles west of the site - trike, quad, combat tank, siege
    // tank, missile tank, harvester, MCV, Devastator - destroyed (the real DestroyUnit) at tick 40
    // (D2K_VFX_DEMO_KILL=tick), all at once or, with D2K_VFX_DEMO_STAGGER=N, N ticks apart. D2K_VFX_DEMO_UNIT=NAME:
    // that one vehicle on the site instead of the row. D2K_VFX_DEMO_DRIVE=N: N ticks after the (first) death a
    // combat tank of the player 3 tiles west of the first wreck drives east through the wrecks' cells (a hulk is
    // never an obstacle: it must be gone where the tank is).
    if (demo == "wrecks") {
        const int me = world.mySide();
        static int other = -1, driver = -1;
        static std::vector<int> row;
        for (int s = 0; s < kMaxSides && other < 0; ++s)
            if (s != me && s != ((me + 1) & 7) && world.sides()[s].firstBuilding < 0 && world.sides()[s].firstUnit < 0) other = s;
        if (other < 0) return;
        const int bx = cx / 32, by = cy / 32;
        auto envInt = [](const char* name, int def) {
            const char* e = std::getenv(name);
            return e ? std::atoi(e) : def;
        };
        const int kill = envInt("D2K_VFX_DEMO_KILL", 40), stagger = envInt("D2K_VFX_DEMO_STAGGER", 0);
        const int drive = envInt("D2K_VFX_DEMO_DRIVE", -1);
        const char* one = std::getenv("D2K_VFX_DEMO_UNIT");
        const int x0 = one ? bx : bx - 6;
        if (t == 1) {
            world.setDiplomacy(me, other, 1);
            const char* names[] = {"TRIKE", "QUAD", "HARKONNEN COMBAT TANK", "SIEGE TANK", "MISSILE TANK", "HARVESTER", "MCV", "DEVASTATOR"};
            if (one) {
                if (const int ty = unitType(one); ty >= 0) row.push_back(world.addUnit(other, ty, bx, by, bx, by));
            } else {
                for (int k = 0; k < int(std::size(names)); ++k)
                    if (const int ty = unitType(names[k]); ty >= 0) row.push_back(world.addUnit(other, ty, x0 + 2 * k, by, x0 + 2 * k, by));
            }
            if (drive >= 0)
                if (const int ty = unitType("ATREIDES COMBAT TANK"); ty >= 0) driver = world.addUnit(me, ty, x0 - 3, by, x0 - 3, by);
        }
        for (int k = 0; k < int(row.size()); ++k)
            if (int(t) == kill + k * stagger && row[std::size_t(k)] >= 0) {
                Unit& u = world.sides()[other].units[std::size_t(row[std::size_t(k)])];
                if (u.alive && u.state != UnitState::Dead) world.destroyUnit(u);
            }
        if (drive >= 0 && driver >= 0 && int(t) == kill + drive) world.orderMove(me, driver, x0 + 2 * int(row.size()) + 1, by);
        return;
    }
    // D2K_VFX_DEMO=crash (the aircraft crash check, docs/3d-mode.md "Aircraft crashes"): flyers of the player shot
    // down in flight (the real DestroyUnit) at tick 40 (D2K_VFX_DEMO_KILL=tick). D2K_VFX_DEMO_UNIT picks which:
    // ORNITHOPTER (an air strike's ornithopter flying east, from 9 tiles west of the site), CARRYALL (a
    // carryall carrying a harvester - D2K_VFX_DEMO_CARGO=NAME another unit - east from 4 tiles west of the site,
    // as after a pickup: the cargo is a unit and dies with it), DELIVERY (a carryall reinforcement of that unit to the site, destroyed when it is
    // within 5 tiles of it, not before the kill tick), EMPTY (a carryall without cargo); default ORNITHOPTER
    // 3 tiles north of the site's row and CARRYALL 3 tiles south of it. D2K_VFX_DEMO_LOG=1 logs the deaths.
    if (demo == "crash") {
        const int me = world.mySide(), bx = cx / 32, by = cy / 32;
        static int orni = -1, car = -1, cargoIdx = -1;
        static bool delivery = false, done = false;
        auto envInt = [](const char* name, int def) {
            const char* e = std::getenv(name);
            return e ? std::atoi(e) : def;
        };
        const int kill = envInt("D2K_VFX_DEMO_KILL", 40);
        const char* one = std::getenv("D2K_VFX_DEMO_UNIT");
        const std::string which = one ? one : "";
        const char* cargoName = std::getenv("D2K_VFX_DEMO_CARGO") ? std::getenv("D2K_VFX_DEMO_CARGO") : "HARVESTER";
        const bool log = std::getenv("D2K_VFX_DEMO_LOG") != nullptr;
        auto fly = [&](const char* name, int sx, int sy) {
            const int ty = unitType(name);
            const int i = ty >= 0 ? world.addUnit(me, ty, sx, sy, bx + 12, sy) : -1;
            if (i < 0) return -1;
            Unit& o = world.sides()[me].units[std::size_t(i)];
            o.facing = o.facingTurret = 8;  // east
            return i;
        };
        if (t == 1) {
            const int row = which.empty() ? 3 : 0;
            if (which.empty() || which == "ORNITHOPTER") {
                orni = fly("ORNITHOPTER", bx - 9, by - row);
                if (orni >= 0) {  // (as Ornithopter_LaunchStrike sends it)
                    Unit& o = world.sides()[me].units[std::size_t(orni)];
                    o.byte29 = 0xc;
                    o.flags |= 0x10800;
                    world.unitAdjustState(o, UnitState::Moving);
                }
            }
            if (which.empty() || which == "CARRYALL" || which == "EMPTY") {
                car = fly("CARRYALL", bx - 4, by + row);
                const int ct = which == "EMPTY" ? -1 : unitType(cargoName);
                if (car >= 0 && ct >= 0) cargoIdx = world.addUnit(me, ct, bx - 4, by + row, bx - 4, by + row);
                if (car >= 0) {
                    Unit& c = world.sides()[me].units[std::size_t(car)];
                    // (Flown by the demo below: an idle carryall goes about its side's pickups.)
                    c.byte29 = 0xff;
                    world.unitAdjustState(c, UnitState::State33);
                    if (cargoIdx >= 0) {  // carried, as CarryAll pickup leaves the two (economy.cpp updateFlyingDown)
                        Unit& v = world.sides()[me].units[std::size_t(cargoIdx)];
                        std::uint32_t& cf = world.cell(v.blockFromX, v.blockFromY).flags;
                        cf &= ~game::cell_flags::kVehicle;
                        c.flags |= game::unit_flags::kCarried;
                        c.enemyIndex = cargoIdx;
                        v.flags |= game::unit_flags::kCarried;
                        world.unitAdjustState(v, UnitState::Airlifted);
                    }
                }
            }
            if (which == "DELIVERY") {
                delivery = true;
                if (const int ct = unitType(cargoName); ct >= 0) world.orderCheatReinforce(me, {std::uint8_t(ct)}, 0, bx, by, -1);
            }
        }
        if (!done && car >= 0 && int(t) < kill) {  // east at 3 px per tick (dragCargo takes its cargo along)
            Unit& c = world.sides()[me].units[std::size_t(car)];
            const int row = which.empty() ? 3 : 0;
            c.posX = ((bx - 4) * 32 + 16 + 3 * int(t)) * 0x10000;
            c.posY = ((by + row) * 32 + 16) * 0x10000;
            c.posZ = 0x3c0000;
            c.facing = c.facingTurret = 8;
            c.blockFromX = c.blockToX = std::uint8_t(game::fixToPixel(c.posX) / 32);
            c.blockFromY = c.blockToY = std::uint8_t(by + row);
        }
        if (done || int(t) < kill) return;
        if (delivery) {
            for (int i = world.sides()[me].firstUnit; i != -1 && !done; i = world.sides()[me].nextObject(i)) {
                Unit& u = world.sides()[me].units[std::size_t(i)];
                if (!u.alive || u.state == UnitState::Dead || !(u.flags & 0x20000000)) continue;
                const int ux = game::fixToPixel(u.posX) / 32, uy = game::fixToPixel(u.posY) / 32;
                if (std::abs(ux - bx) > 5 || std::abs(uy - by) > 5) continue;
                if (log) SDL_Log("crash demo: t=%u delivery carryall destroyed at %d,%d z %d", t, ux, uy, u.posZ >> 16);
                world.destroyUnit(u);
                done = true;
            }
            return;
        }
        done = true;
        for (const int i : {orni, car}) {
            if (i < 0) continue;
            Unit& u = world.sides()[me].units[std::size_t(i)];
            if (!u.alive || u.state == UnitState::Dead) continue;
            if (log)
                SDL_Log("crash demo: t=%u unit %d (type %d) destroyed at px %d,%d z %d facing %d flags %x", t, i, int(u.type),
                        game::fixToPixel(u.posX), game::fixToPixel(u.posY), u.posZ >> 16, int(u.facing), unsigned(u.flags));
            world.destroyUnit(u);
        }
        return;
    }
    // D2K_VFX_DEMO=tankhit (the smoke-over-a-damaged-tank check): a parked enemy combat tank 2 tiles south-east
    // of the site, held at ~40% health (smoking), hit by a rocket every 60 ticks from tick 20.
    if (demo == "tankhit") {
        const int me = world.mySide();
        static int other = -1, tank = -1;
        for (int s = 0; s < kMaxSides && other < 0; ++s)
            if (s != me && s != ((me + 1) & 7) && world.sides()[s].firstBuilding < 0 && world.sides()[s].firstUnit < 0) other = s;
        if (other < 0) return;
        const int bx = cx / 32, by = cy / 32;
        if (t == 1) {
            world.setDiplomacy(me, other, 1);
            if (const int ty = unitType("HARKONNEN COMBAT TANK"); ty >= 0) tank = world.addUnit(other, ty, bx + 2, by + 2, bx + 2, by + 2);
        }
        if (tank < 0) return;
        Unit& u = world.sides()[other].units[tank];
        if (!u.alive) return;
        const std::int32_t hp40 = std::int32_t(world.unitTemplate(u).hitPoints) * 40 / 100;
        if (t == 2 || u.health < hp40 * 3 / 4) u.health = hp40;
        if (t % 60 == 20)
            if (const int w = weapon("ROCKET"); w >= 0) {
                const int tx = game::fixToPixel(u.posX), ty = game::fixToPixel(u.posY);
                world.addBullet(me, w, 0, -1, tx - 200, ty, tx, ty);
            }
        return;
    }
    // D2K_VFX_DEMO=crush (crushed infantry vs the vehicle on top, docs/3d-models.md "Infantry"): a row of 5 enemy
    // light infantry 3 tiles east of the site; at tick 10 a combat tank of the player 4 tiles west of them drives
    // through the row (a move order 4 tiles past it). D2K_VFX_DEMO_LOG=1 logs the soldiers' deaths.
    if (demo == "crush") {
        const int me = world.mySide();
        static int other = -1, tank = -1;
        static std::vector<int> men;
        for (int s = 0; s < kMaxSides && other < 0; ++s)
            if (s != me && s != ((me + 1) & 7) && world.sides()[s].firstBuilding < 0 && world.sides()[s].firstUnit < 0) other = s;
        if (other < 0) return;
        const int bx = cx / 32, by = cy / 32;
        if (t == 1) {
            world.setDiplomacy(me, other, 1);
            world.setDiplomacy(other, me, 1);
            if (const int ty = unitType("LIGHT INF"); ty >= 0)
                for (int k = 0; k < 5; ++k) men.push_back(world.addUnit(other, ty, bx + 3, by - 2 + k, bx + 3, by - 2 + k));
            if (const int ty = unitType("HARKONNEN COMBAT TANK"); ty >= 0) tank = world.addUnit(me, ty, bx - 1, by, bx - 1, by);
        }
        if (t == 10 && tank >= 0) world.orderMove(me, tank, bx + 7, by);
        if (std::getenv("D2K_VFX_DEMO_LOG"))
            for (int i : men)
                if (i >= 0 && world.sides()[other].units[i].alive && world.sides()[other].units[i].state == UnitState::Dead &&
                    t % 3 == 0)
                    SDL_Log("crush demo: t=%u soldier %d dead, anim %d frame %d", t, i, int(world.sides()[other].units[i].animType),
                            int(world.sides()[other].units[i].animFrame));
        return;
    }
    if (demo == "capture") {
        const int me = world.mySide();
        static int other = -1;  // an unused side, picked once
        for (int s = 0; s < kMaxSides && other < 0; ++s)
            if (s != me && s != ((me + 1) & 7) && world.sides()[s].firstBuilding < 0 && world.sides()[s].firstUnit < 0) other = s;
        if (other < 0) return;
        static int b0 = -1, b1 = -1, b2 = -1, e0 = -1, e1 = -1;
        const int ax = cx / 32 - 4, ay = cy / 32 - 2;
        if (t == 1) {
            world.setDiplomacy(me, other, 1);
            world.setDiplomacy(other, me, 1);
            if (const int wt = buildingType("ORDOS WIND TRAP"); wt >= 0) b0 = world.addBuilding(other, wt, ax, ay, false, true);
            if (const int br = buildingType("ORDOS BARRACKS"); br >= 0) b1 = world.addBuilding(other, br, ax + 4, ay, false, true);
            if (const int wt = buildingType("ORDOS WIND TRAP"); wt >= 0) b2 = world.addBuilding(other, wt, ax + 8, ay, false, true);
            if (const int eng = unitType("ENGINEER"); eng >= 0) {
                e0 = world.addUnit(me, eng, ax + 1, ay + 4, ax + 1, ay + 4);
                e1 = world.addUnit(me, eng, ax + 5, ay + 4, ax + 5, ay + 4);
            }
        }
        if (t == 100) {
            if (e0 >= 0 && b0 >= 0) world.orderAttackBuilding(me, e0, other, b0);
            if (e1 >= 0 && b1 >= 0) world.orderAttackBuilding(me, e1, other, b1);
        }
        if (t == 150 && b2 >= 0) {
            if (Building* b = world.building(other, b2)) world.destroyBuilding(*b, false);
        }
        return;
    }
    // D2K_VFX_DEMO=column: a vehicle column (side 0, one per row) that sets off east, stops, fires
    // at the ground ahead, drives a diagonal, turns about and back west - Vehicle animation / barrel tips check (period 400).
    // D2K_VFX_DEMO=reimagined (docs/3d-models.md "Reimagined models": the model check and the fps scene): the
    // player's Atreides construction yard, refinery and two wind traps west of the tile, and a block of
    // D2K_VFX_DEMO_ROWS x 6 (default 6 x 6) Atreides combat tanks of the player east of them that drive a slow
    // loop (east, south, west, north; every 100 ticks), turrets following - turning, driving and standing models.
    if (demo == "reimagined") {
        static std::vector<int> tanks;
        // D2K_VFX_DEMO_SIDE=n: another side than the player's (e.g. the Atreides side 0 of H5V1, whose house colour
        // is blue, while the mission goes on for the player).
        static const int me = std::getenv("D2K_VFX_DEMO_SIDE") ? std::clamp(std::atoi(std::getenv("D2K_VFX_DEMO_SIDE")), 0, 7)
                                                               : world.mySide();
        // D2K_VFX_DEMO_ANCHOR="X Y": the scene at that tile whatever the camera (--at), for shots from several cameras.
        static int ax0 = -1, ay0 = -1;
        if (ax0 < 0) {
            ax0 = cx / 32, ay0 = cy / 32;
            if (const char* a = std::getenv("D2K_VFX_DEMO_ANCHOR")) std::sscanf(a, "%d %d", &ax0, &ay0);
        }
        const int bx = ax0, by = ay0;
        static int rows = 6;
        if (t == 1) {
            if (const char* r = std::getenv("D2K_VFX_DEMO_ROWS")) rows = std::clamp(std::atoi(r), 1, 16);
            if (const int ty = buildingType("ATREIDES CONSTRUCTION YARD"); ty >= 0) world.addBuilding(me, ty, bx - 9, by - 4, true, true);
            if (const int ty = buildingType("ATREIDES REFINERY"); ty >= 0) world.addBuilding(me, ty, bx - 9, by, true, true);
            // D2K_VFX_DEMO_HOUSE=HARKONNEN|ORDOS: that house's wind traps (the redesign checks, docs/3d-models.md).
            const char* wh = std::getenv("D2K_VFX_DEMO_HOUSE");
            if (const int ty = buildingType(((wh && *wh ? std::string(wh) : std::string("ATREIDES")) + " WIND TRAP").c_str()); ty >= 0) {
                world.addBuilding(me, ty, bx - 5, by - 4, true, true);
                world.addBuilding(me, ty, bx - 5, by, true, true);
            }
            // D2K_VFX_DEMO_BASE=full: every other Atreides building too (rows of 5 south-west of the block, 4 tiles
            // apart) and one of each vehicle in a row south of the tanks - the Reimagined base overview / fps scene.
            if (const char* base = std::getenv("D2K_VFX_DEMO_BASE"); base && std::string(base) == "full") {
                int slot = 0;
                for (int i = 0; i < int(T.bin.buildingCount); ++i) {
                    const std::string n = T.bin.buildingNames[i];
                    if (n.rfind("ATREIDES ", 0) != 0 || n == "ATREIDES WALL" || n == "ATREIDES CONSTRUCTION YARD" ||
                        n == "ATREIDES REFINERY" || n == "ATREIDES WIND TRAP")
                        continue;
                    world.addBuilding(me, i, bx - 18 + (slot % 5) * 4, by + 8 + (slot / 5) * 4, true, true);
                    ++slot;
                }
                const char* kVehicles[] = {"TRIKE", "QUAD", "MCV", "MISSILE TANK", "SIEGE TANK", "SONIC TANK", "HARVESTER"};
                for (int k = 0; k < int(std::size(kVehicles)); ++k)
                    if (const int ty = unitType(kVehicles[k]); ty >= 0) world.addUnit(me, ty, bx - 9 + k * 2, by + 5, bx - 9 + k * 2, by + 5);
            }
            if (const int ty = unitType("ATREIDES COMBAT TANK"); ty >= 0)
                for (int r = 0; r < rows; ++r)
                    for (int c = 0; c < 6; ++c)
                        tanks.push_back(world.addUnit(me, ty, bx - 1 + c * 2, by - 4 + r * 2, bx - 1 + c * 2, by - 4 + r * 2));
        }
        // D2K_VFX_DEMO_WORK=1: the factories, barracks and palaces hold the producing flag (kAnimPermanent), as in
        // allbuildings (the building life check: working stacks smoke, docs/3d-mode.md "Building life").
        if (const char* wk = std::getenv("D2K_VFX_DEMO_WORK"); wk && wk[0] == '1') {
            Side& sd = world.sides()[me];
            for (int i = sd.firstBuilding; i >= 0; i = sd.nextObject(i)) {
                Building& b = sd.buildings[std::size_t(i)];
                const int sb = T.bin.buildings[b.type].specialBehavior;
                if ((sb >= 7 || sb == 11) && sb != 13 && sb != 14 && sb != 15 && sb != 16 && sb != 17)
                    b.flags |= game::building_flags::kAnimPermanent;
            }
        }
        if (t > 1 && t % 100 == 0) {
            static const int kStep[4][2] = {{2, 0}, {0, 2}, {-2, 0}, {0, -2}};
            const int k = int((t / 100 - 1) % 4);
            int dx = 0, dy = 0;
            for (int j = 0; j <= k; ++j) dx += kStep[j][0], dy += kStep[j][1];
            for (std::size_t i = 0; i < tanks.size(); ++i)
                if (tanks[i] >= 0)
                    world.orderMove(me, tanks[i], bx - 1 + int(i % 6) * 2 + dx, by - 4 + int(i / 6) * 2 + dy);
        }
        return;
    }
    if (demo == "column") {
        static const char* kUnits[] = {"ATREIDES COMBAT TANK", "HARKONNEN COMBAT TANK", "ORDOS COMBAT TANK", "SIEGE TANK",
                                       "DEVASTATOR TANK", "MISSILE TANK", "QUAD", "TRIKE"};
        static std::vector<int> ids;
        const int ax = cx / 32 - 5, ay = cy / 32 - 4;
        if (ids.empty()) {
            for (int k = 0; k < int(std::size(kUnits)); ++k) {
                int type = -1;
                for (int i = 0; i < int(T.bin.unitCount); ++i)
                    if (std::string(T.bin.unitNames[i]) == kUnits[k]) type = i;
                ids.push_back(type >= 0 ? world.addUnit(0, type, ax, ay + k, ax, ay + k) : -1);
            }
        }
        const std::uint32_t p = t % 400;
        for (int k = 0; k < int(ids.size()); ++k) {
            if (ids[k] < 0) continue;
            if (p == 10) world.orderMove(0, ids[k], ax + 4, ay + k);
            if (p == 110) world.orderAttackTile(0, ids[k], ax + 7, ay + k);
            if (p == 200) world.orderMove(0, ids[k], ax + 7, ay + k + 3);  // a diagonal: turning while driving
            if (p == 290) world.orderMove(0, ids[k], ax + 1, ay + k);      // turn about, back west
        }
        return;
    }
    // D2K_VFX_DEMO=march (Modern movement check, docs/modern-movement.md): ten combat tanks of the
    // player in a 5 x 2 block west of a wall (side 0) with a 2-tile gap; beyond the gap a second wall
    // they have to drive round (its south end) to reach the goal north-east of it. At tick 20 they are sent to a point past both (one group order), and
    // back again whenever all of them stand still (or after 700 ticks): corners, a choke, spreading
    // out at the goal. The same scene with --movement original shows the original movement.
    if (demo == "march") {
        static std::vector<int> ids;
        static std::uint32_t sent = 0;
        static int leg = 0;
        const int me = world.mySide();
        const int ax = cx / 32 - 10, ay = cy / 32 - 2;
        if (ids.empty()) {
            auto building = [&](const char* name) {
                for (int i = 0; i < int(T.bin.buildingCount); ++i)
                    if (std::string(T.bin.buildingNames[i]) == name) return i;
                return -1;
            };
            if (const int wall = building("ATREIDES WALL"); wall >= 0) {
                for (int y = -7; y <= 9; ++y)
                    if (y != 1 && y != 2) world.addBuilding(0, wall, ax + 9, ay + y, false, true);
                for (int y = -4; y <= 4; ++y) world.addBuilding(0, wall, ax + 13, ay + y, false, true);
            }
            int type = -1;
            const char* house = me == 1 ? "HARKONNEN COMBAT TANK" : me == 2 ? "ORDOS COMBAT TANK" : "ATREIDES COMBAT TANK";
            for (int i = 0; i < int(T.bin.unitCount); ++i)
                if (std::string(T.bin.unitNames[i]) == house) type = i;
            for (int k = 0; k < 10 && type >= 0; ++k) {
                const int x = ax + (k % 5), y = ay - 3 + (k / 5) * 2;
                ids.push_back(world.addUnit(me, type, x, y, x, y));
            }
            SDL_Log("march demo: 10 tanks at %d,%d (side %d), gap %d,%d..%d, goal %d,%d", ax, ay - 3, me, ax + 9, ay + 1, ay + 2,
                    ax + 17, ay - 2);
        }
        bool still = true;
        for (int id : ids)
            if (const Unit* u = world.unit(me, id)) still = still && u->posSteps == 0 && u->state != UnitState::Moving;
        if (const char* log = std::getenv("D2K_MARCH_LOG")) {  // per tick: tank, px, py, heading (65536ths), speed (16.16), state
            static FILE* f = std::fopen(log, "w");
            for (int k = 0; f && k < int(ids.size()); ++k)
                if (const Unit* u = world.unit(me, ids[k]))
                    std::fprintf(f, "%u,%d,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", t, k, u->posX / 65536.0, u->posY / 65536.0,
                                 world.unitHeading(*u), u->mm.speed, int(u->state), u->targetX, u->targetY, u->mm.wpIndex, u->mm.wpCount,
                                 u->mm.stall, u->mm.flags, u->blockFromX, u->blockFromY);
            if (f) std::fflush(f);
        }
        if (t == 20 || (t > sent + 30 && (still || t > sent + 700))) {
            const int tx = leg % 2 == 0 ? ax + 17 : ax + 2, ty = ay - 2;
            for (int id : ids)
                if (world.unit(me, id)) world.orderMove(me, id, tx, ty);
            SDL_Log("march demo: tick %u: leg %d -> %d,%d", t, leg, tx, ty);
            ++leg;
            sent = t;
        }
        return;
    }
    // D2K_VFX_DEMO=harvestcycle (Modern movement check): a refinery of the player ~10 tiles from the
    // nearest spice to the tile and one nearly full harvester on the spice: field -> refinery dock ->
    // unload -> field.
    if (demo == "harvestcycle") {
        static bool placed = false;
        if (placed) return;
        placed = true;
        const int me = world.mySide();
        const int tx0 = cx / 32, ty0 = cy / 32;
        int sx = -1, sy = -1;
        for (int r = 0; r < 40 && sx < 0; ++r)
            for (int dy = -r; dy <= r && sx < 0; ++dy)
                for (int dx = -r; dx <= r && sx < 0; ++dx)
                    if (std::max(std::abs(dx), std::abs(dy)) == r && world.inBounds(tx0 + dx, ty0 + dy) &&
                        (world.cell(tx0 + dx, ty0 + dy).flags & cell_flags::kSpiceMask) >= 0x300000)
                        sx = tx0 + dx, sy = ty0 + dy;
        int ref = -1, harv = -1;
        const int house = world.sides()[me].house;
        for (int i = 0; i < int(T.bin.buildingCount); ++i)
            if (T.bin.buildings[i].specialBehavior == building_behaviour::kRefinery && (ref < 0 || T.bin.buildings[i].ownerHouse == house))
                ref = i;
        for (int i = 0; i < int(T.bin.unitCount); ++i)
            if (T.bin.units[i].specialBehavior == behaviour::kHarvester && harv < 0) harv = i;
        if (sx < 0 || ref < 0 || harv < 0) {
            SDL_Log("harvestcycle demo: no spice / refinery / harvester");
            return;
        }
        // A 3 x 4 block of free drivable non-spice cells 8..12 tiles from the spice.
        auto freeBlock = [&](int x0, int y0) {
            for (int y = y0; y < y0 + 4; ++y)
                for (int x = x0; x < x0 + 3; ++x) {
                    if (!world.inBounds(x, y)) return false;
                    const std::uint32_t f = world.cell(x, y).flags;
                    if (!(f & cell_flags::kDriveOn) || (f & (cell_flags::kOccupied | cell_flags::kSpiceMask | cell_flags::kWall)))
                        return false;
                }
            return true;
        };
        int bx = -1, by = -1;
        for (int r = 8; r <= 14 && bx < 0; ++r)
            for (int dy = -r; dy <= r && bx < 0; ++dy)
                for (int dx = -r; dx <= r && bx < 0; ++dx)
                    if (std::max(std::abs(dx), std::abs(dy)) == r && freeBlock(sx + dx, sy + dy)) bx = sx + dx, by = sy + dy;
        if (bx < 0) {
            SDL_Log("harvestcycle demo: no room for a refinery near the spice at %d,%d", sx, sy);
            return;
        }
        const int b = world.addBuilding(me, ref, bx, by, true, true);
        const int h = world.addUnit(me, harv, sx, sy, sx, sy);
        if (Unit* hu = world.unit(me, h)) hu->byte29 = 6;  // nearly full: one load, then off to the refinery
        SDL_Log("harvestcycle demo: refinery %d at %d,%d, harvester %d on spice %d,%d (side %d)", b, bx, by, h, sx, sy, me);
        return;
    }
    // D2K_VFX_DEMO=route: a wall (side 0) across the middle and three combat tanks + a trike (side 0)
    // shuttling between waypoints on either side of it, each sent on as soon as it stands on the
    // last one - their routes trace round the wall's ends (90 / 45 degree corners inside one route,
    // the original's stop-turn-go) and cross the open sand diagonally (staircases of 45 degree
    // steps). Smooth driving check (render/smooth_drive.hpp).
    if (demo == "route") {
        static const char* kUnits[] = {"ATREIDES COMBAT TANK", "HARKONNEN COMBAT TANK", "ORDOS COMBAT TANK", "TRIKE"};
        static const int kWays[][2][2] = {{{-3, 1}, {9, 4}}, {{-3, 3}, {9, 6}}, {{-3, 5}, {9, 1}}, {{-3, 7}, {9, 3}}};
        static std::vector<int> ids, next;
        static std::vector<std::uint32_t> ordered;
        const int ax = cx / 32 - 3, ay = cy / 32 - 4;
        if (ids.empty()) {
            auto building = [&](const char* name) {
                for (int i = 0; i < int(T.bin.buildingCount); ++i)
                    if (std::string(T.bin.buildingNames[i]) == name) return i;
                return -1;
            };
            if (const int wall = building("ATREIDES WALL"); wall >= 0)
                for (int y = 1; y <= 7; ++y) world.addBuilding(0, wall, ax + 3, ay + y, false, true);
            for (int k = 0; k < int(std::size(kUnits)); ++k) {
                int type = -1;
                for (int i = 0; i < int(T.bin.unitCount); ++i)
                    if (std::string(T.bin.unitNames[i]) == kUnits[k]) type = i;
                const int* p = kWays[k][0];
                ids.push_back(type >= 0 ? world.addUnit(0, type, ax + p[0], ay + p[1], ax + p[0], ay + p[1]) : -1);
                next.push_back(1);
                ordered.push_back(0);
            }
        }
        if (t < 10) return;
        for (int k = 0; k < int(ids.size()); ++k) {
            const Unit* u = ids[k] >= 0 ? world.unit(0, ids[k]) : nullptr;
            if (!u || !u->alive || t < ordered[k] + 4 || u->posSteps != 0 || u->blockFromX != u->targetX ||
                u->blockFromY != u->targetY)
                continue;  // (an order runs at the start of the next tick)
            const int* p = kWays[k][next[k] & 1];
            world.orderMove(0, ids[k], ax + p[0], ay + p[1]);
            ++next[k];
            ordered[k] = t;
        }
        return;
    }
    if (demo == "units") {
        static const char* kUnits[] = {"TRIKE", "RAIDER", "QUAD", "LIGHT INF", "TROOPER", "FREMEN", "SARDAUKAR",
                                       "ATREIDES COMBAT TANK", "MISSILE TANK"};
        static std::vector<int> ids;
        if (ids.empty()) {
            const int ax = cx / 32 - 4, ay = cy / 32 - 4;
            for (int k = 0; k < int(std::size(kUnits)); ++k) {
                int type = -1;
                for (int i = 0; i < int(T.bin.unitCount); ++i)
                    if (std::string(T.bin.unitNames[i]) == kUnits[k]) type = i;
                const int idx = type >= 0 ? world.addUnit(0, type, ax, ay + k, ax, ay + k) : -1;
                ids.push_back(idx);
            }
        }
        if (t % 20 == 1) {
            const int ax = cx / 32 - 4, ay = cy / 32 - 4;
            for (int k = 0; k < int(ids.size()); ++k)
                if (ids[k] >= 0) world.orderAttackTile(0, ids[k], ax + 2, ay + k);
        }
        return;
    }
    // Rows of shots, left to right, each row every 40 ticks (staggered).
    static const char* kRows[] = {"LMG", "HMG", "80MM GUN", "110MM GUN", "ROCKET", "NEW ROCKET", "155MM HOWITZER",
                                  "DEVBULLET", "DEVIATOR", "SOUND", "ORNIBOMB"};
    for (int r = 0; r < int(std::size(kRows)); ++r) {
        const int w = weapon(kRows[r]);
        if (w < 0 || t % 40 != 0) continue;
        const int y = cy - 150 + r * 30, x0 = cx - 210, x1 = cx - 30;
        const bool gun = r < 2;
        for (int k = 0; k < (gun ? 3 : 1); ++k)
            world.addBullet(0, w, k * 2, -1, x0, y, x1, y, -1, -1);
    }
    // Explosions on the right: vehicle deaths (wreck flash + death explosion), a heavy one, and
    // now and then a Death Hand.
    struct Ex { const char* name; int dx, dy; std::uint32_t period, phase; bool wreck; };
    static const Ex kEx[] = {{"EX_BANG11", 60, -110, 70, 0, true},  {"EX_BANG13", 130, -110, 70, 20, true},
                             {"EX_BANG14", 60, -20, 70, 40, true},   {"EX_TANKGUN", 130, -20, 30, 10, false},
                             {"EX_NEW ROCKET", 200, -20, 30, 25, false}, {"EX_DEATHHAND", 150, 110, 200, 60, false},
                             {"EX_DEVASTATOR", 220, -110, 50, 5, false}};
    for (const Ex& e : kEx) {
        if ((t + e.phase) % e.period != 0) continue;
        const int type = explosion(e.name);
        if (type < 0) continue;
        if (e.wreck) world.addExplosion(0, explosion("EX_WHITEN1"), cx + e.dx, cy + e.dy, 0);
        world.addExplosion(0, type, cx + e.dx, cy + e.dy, 0);
    }
}

}  // namespace d2k::platform

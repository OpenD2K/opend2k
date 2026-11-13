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
            motion.apply(b, alpha);
            for (int s = 0; s < kMaxSides; ++s)
                for (int i = b.sides()[s].firstUnit; i != -1; i = b.sides()[s].nextObject(i))
                    moved += b.sides()[s].units[i].posX != a.sides()[s].units[i].posX;
            motion.restore(b);
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
        if (t == 40)
            for (int i = world.sides()[me].firstUnit; i >= 0; i = world.sides()[me].nextObject(i))
                if (world.sides()[me].units[i].type == unitType("MCV")) world.orderDeploy(me, i);
        if (t >= 20 && t % 120 == 20) {
            for (const char* u : {"LIGHT INF", "TRIKE", "ATREIDES COMBAT TANK", "HARKONNEN COMBAT TANK", "ORDOS COMBAT TANK"})
                if (const int ty = unitType(u); ty >= 0) world.orderBuildUnit(me, ty);  // (the side's own tank only)
            if (const int q = unitType("QUAD"); q >= 0 && world.orderStarportPick(me, q)) world.orderStarportPurchase(me);
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
        const int me = world.mySide(), bx = cx / 32, by = cy / 32;
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
        }
        for (const int i : made) world.sides()[me].buildings[i].flags |= game::building_flags::kAnimPermanent;
        return;
    }
    // D2K_VFX_DEMO=ornithopter: an air strike in view - three ornithopters of the player set up as
    // Ornithopter_LaunchStrike 0x49e770 does (12 bombs, flags 0x10800, Moving), but starting 9 / 11 / 13
    // tiles west of the tile, every 300 ticks: their wing beat (unit art rows 0-1-2,
    // FlyUnit) is the 3D row-pose check (docs/3d-models.md "Animation").
    if (demo == "ornithopter") {
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
    // D2K_VFX_DEMO=capture: a wind trap and a barracks of another side, two engineers of the
    // player next to them; at tick 100 (after the build-up) the engineers are sent in
    // (orderAttackBuilding: the real infiltrate + CaptureBuilding path). At tick 150 a third
    // building of that side is destroyed
    // (for the comparison: destruction vs capture).
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

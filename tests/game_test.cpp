// Game-logic unit tests. Synthetic maps/templates; no game data needed. If the original
// data is present (D2K_GAME_DATA_DIR), a mission is also loaded and a unit is moved.

#include "formats/bintables.hpp"
#include "formats/io.hpp"
#include "formats/map.hpp"
#include "formats/mis.hpp"
#include "formats/mission_ini.hpp"
#include "formats/techpos.hpp"
#include "formats/templates.hpp"
#include "formats/tileatr.hpp"
#include "formats/vars.hpp"
#include "game/geometry.hpp"
#include "render/lighting.hpp"
#include "game/pathfind.hpp"
#include "game/random.hpp"
#include "game/save_check.hpp"
#include "game/savegame.hpp"
#include "game/scenario.hpp"
#include "game/sound_system.hpp"
#include "game/tables.hpp"
#include "game/campaign.hpp"
#include "game/world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <memory>

namespace d2k::game {
// Test hooks into World internals (declared as a friend in world.hpp).
struct WorldTestAccess {
    static void loadEvents(World& w, const formats::MisFile& m) { w.loadMissionEvents(m); }
    static void evaluateAllExists(World& w) { w.evaluateAllExists(); }
    static void updateShroud(World& w, int x0, int y0, int x1, int y1) { w.updateShroudInRegion(x0, y0, x1, y1); }
    static void removeSkirt(World& w, int x, int y, int type) { w.removeBuildingSkirt(x, y, type); }
    static int crateTypeAt(const World& w, int x, int y) { int i = 0; return w.crateTypeAt(x, y, i); }
    static void initStarport(World& w, int side) { w.initStarport(w.sides_[side]); }
    static void missionOutcome(World& w, bool win) { w.missionOutcome(win); }
    static void checkPlayAsEnd(World& w) { w.checkPlayAsEnd(); }
    static void setExists(World& w, int side, bool units, bool buildings) {
        w.unitsExist_[side & 7] = units;
        w.buildingsExist_[side & 7] = buildings;
    }
};
}  // namespace d2k::game

namespace {
int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

using namespace d2k;
using namespace d2k::game;

constexpr std::uint32_t kRock = 0x2000 | 0x4000 | (1u << 29);  // drive + walk, terrain type 1
constexpr std::uint32_t kDunes = 0x2000 | 0x4000 | 0x10000 | (2u << 29);
constexpr int kTank = 0, kInfantry = 1, kTrike = 2;

struct Fixture {
    std::unique_ptr<formats::Templates> templates = std::make_unique<formats::Templates>();
    formats::SpeedTable speed{};
    Fixture() {
        std::memset(&templates->bin, 0, sizeof(templates->bin));
        auto& b = templates->bin;
        b.unitCount = 3;
        auto& tank = b.units[kTank];
        tank.hitPoints = 400;
        tank.speed = 0x20000;  // 2 px/tick: exactly 16 ticks per straight tile on factor 1.0
        tank.primaryWeapon = 0;
        tank.secondaryWeapon = -1;
        tank.primaryWeaponUseBarrel = 1;  // has a turret
        tank.speedType = 1;               // tracked
        tank.canCrushInfantry = 1;
        auto& inf = b.units[kInfantry];
        inf.hitPoints = 50;
        inf.speed = 0x10000;
        inf.isInfantry = 1;
        inf.primaryWeapon = 1;
        inf.secondaryWeapon = -1;
        inf.speedType = 0;
        auto& trike = b.units[kTrike];
        trike.hitPoints = 100;
        trike.speed = 0x60000;
        trike.unitRotationSpeed = 1;
        trike.primaryWeapon = 2;
        trike.secondaryWeapon = -1;
        trike.speedType = 2;  // wheeled
        // Vanilla SPEED.BIN values (re/notes/movement.md §1.2).
        const float v[8][4] = {{1, 1, 0.9f, 1},  {1, 1, 1, 1},       {0.8f, 0.7f, 0.5f, 1}, {0.7f, 0.5f, 0.5f, 1},
                               {0.5f, 0.5f, 0.5f, 1}, {0.7f, 0.5f, 0.5f, 1}, {1, 1, 1, 1},  {1, 1, 1, 1}};
        std::memcpy(speed.values, v, sizeof(v));
    }
    Rules rules() const { return {templates.get(), &speed, nullptr}; }
    std::unique_ptr<World> world(int w, int h, std::uint32_t flags = kRock) const {
        auto wd = std::make_unique<World>(w, h, rules(), 0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) wd->cell(x, y).flags = flags;
        return wd;
    }
};

// Run ticks until the unit is idle again (or the limit); returns the tick count or -1.
int runUntilIdle(World& w, int side, int idx, int limit) {
    for (int t = 1; t <= limit; ++t) {
        w.tick();
        const Unit* u = w.unit(side, idx);
        if (u && u->state == UnitState::Idle && u->posSteps == 0) return t;
    }
    return -1;
}

void testRandom() {
    // MSVC rand() after srand(1): 41, 18467, 6334, 26500, 19169.
    Random rng(1);
    CHECK(rng.next() == 41);
    CHECK(rng.next() == 18467);
    CHECK(rng.next() == 6334);
    CHECK(rng.next() == 26500);
    CHECK(rng.next() == 19169);
}

void testGeometry() {
    CHECK(getDir8(5, 5, 6, 5) == 8);    // east
    CHECK(getDir8(5, 5, 5, 4) == 0);    // north
    CHECK(getDir8(5, 5, 5, 6) == 16);   // south
    CHECK(getDir8(5, 5, 4, 5) == 24);   // west
    CHECK(getDir8(5, 5, 6, 4) == 4);    // north-east
    CHECK(getDir8(5, 5, 4, 6) == 20);   // south-west
    CHECK(getFacing(0, 0, 1, -2) == 2);  // ~26.6 deg: rounds to facing 2 ...
    CHECK(getDir8(0, 0, 1, -2) == 0);    // ... and GetDir8 truncates it to north
    CHECK(getBestAngle(0, 0, 3, -1) == 8);  // dominant axis
    CHECK(getBestAngle(0, 0, 2, -2) == 4);  // exact diagonal (north-east)
    CHECK(getBestAngle(0, 0, -2, 2) == 20);
    int x = 3, y = 3;
    stepInDirection(x, y, 28);
    CHECK(x == 2 && y == 2);
}

void testObjectPool() {
    Side s;
    s.initPool();
    const int a = s.allocObject(), b = s.allocObject(), c = s.allocObject();
    CHECK(a == 999 && b == 998 && c == 997);  // CSide::Init: free top = slot 999
    s.linkObject(a, SlotKind::Unit);
    s.linkObject(b, SlotKind::Building);
    s.linkObject(c, SlotKind::Unit);
    CHECK(s.firstUnit == 999 && s.nextObject(999) == 997 && s.nextObject(997) == -1);  // insertion order
    CHECK(s.firstBuilding == 998);
    s.freeObject(a);
    CHECK(s.firstUnit == 997);
    CHECK(s.allocObject() == 999);  // LIFO: the freed slot comes back first
    CHECK(s.allocObject() == 996);

    // Units and buildings share one pool: indices interleave.
    Fixture f;
    auto w = f.world(8, 8);
    CHECK(w->addUnit(0, kTank, 1, 1, 1, 1) == 999);
    CHECK(w->addUnit(0, kTank, 2, 1, 2, 1) == 998);
    CHECK(w->addUnit(1, kTank, 3, 1, 3, 1) == 999);  // per side
}

void testSpawnFields() {
    Fixture f;
    auto w = f.world(8, 8);
    const int i0 = w->addUnit(0, kTank, 2, 3, 2, 3);
    const int i1 = w->addUnit(1, kTank, 4, 3, 4, 3);
    const Unit& u0 = w->sides()[0].units[i0];
    const Unit& u1 = w->sides()[1].units[i1];
    CHECK(u0.posX == tileCentre(2) && u0.posY == tileCentre(3));
    CHECK(u0.state == UnitState::Idle);      // the local player's units stay idle
    CHECK(u1.state == UnitState::Guarding);  // other sides guard in single player
    CHECK(u1.rememberX == 4 && u1.rememberY == 3);
    CHECK((w->cell(2, 3).flags & 0xF) == (0x8 | 0));
    CHECK((w->cell(4, 3).flags & 0xF) == (0x8 | 1));
    CHECK(u0.health == 400 && u0.lastAttacked == 0xFF && u0.enemyIndex == kNoIndex);
}

void testSpeed() {
    Fixture f;
    auto w = f.world(8, 8);
    const int i = w->addUnit(0, kTank, 2, 2, 2, 2);
    Unit& u = w->sides()[0].units[i];
    CHECK(w->getSpeed(u) == 0x20000);  // rock, tracked: 1.0
    w->cell(2, 2).flags = kDunes;
    CHECK(w->getSpeed(u) == static_cast<int>(131072.0 * static_cast<double>(0.7f)));  // dunes, tracked 0.7
    CHECK(w->getSpeed(u) == 91750);
    w->cell(2, 2).flags = kDunes | 0x800;  // concrete overrides terrain: x1.1
    CHECK(w->getSpeed(u) == 144179);
    w->cell(2, 2).flags = kRock;
    u.health = 400;
    CHECK(w->stepSpeed(u) == 0x20000);
    u.health = 150;  // 2*150 < 400 <= 3*150: x0.66
    CHECK(w->stepSpeed(u) == 86507);
    u.health = 100;  // 3*100 < 400: x0.85 (the original's quirk: faster than at 150)
    CHECK(w->stepSpeed(u) == 111411);

    const int j = w->addUnit(0, kTrike, 4, 4, 4, 4);
    Unit& t = w->sides()[0].units[j];
    w->cell(4, 4).flags = 0x2000 | 0x4000;  // terrain 0: wheeled 0.9
    CHECK(w->getSpeed(t) == static_cast<int>(393216.0 * static_cast<double>(0.9f)));

    const int k = w->addUnit(0, kInfantry, 5, 5, 5, 5);
    Unit& inf = w->sides()[0].units[k];
    CHECK(w->stepSpeed(inf) == 0x10000);
    inf.animType = 5;  // crawling: half speed
    CHECK(w->stepSpeed(inf) == 0x8000);
}

void testInfantrySubcells() {
    Fixture f;
    auto w = f.world(8, 8);
    // ModelAddUnit takes sub-cells TL(4), DL(3), DR(2), TR(1), MID(0).
    const int expect[5] = {4, 3, 2, 1, 0};
    const int offX[5] = {16, 23, 23, 7, 7}, offY[5] = {21, 14, 26, 26, 14};
    for (int n = 0; n < 5; ++n) {
        const int i = w->addUnit(0, kInfantry, 3, 3, 3, 3);
        const Unit& u = w->sides()[0].units[i];
        CHECK(u.cellspotFrom == expect[n] && u.cellspotTo == expect[n]);
        CHECK(u.posX == toFix(3 * 32 + offX[expect[n]]) && u.posY == toFix(3 * 32 + offY[expect[n]]));
    }
    CHECK((w->cell(3, 3).flags & 0x3E0) == 0x3E0);
    CHECK(((w->cell(3, 3).flags >> 25) & 7) == 0);
    // A full cell is no longer usable by friendly infantry.
    const int other = w->addUnit(0, kInfantry, 5, 3, 5, 3);
    CHECK(!w->canUnitUseSquare(3, 3, w->sides()[0].units[other], 0, 0));

    // Moving keeps the sub-spot when it is free in the new cell, else the first free from TL.
    auto w2 = f.world(8, 8);
    const int a = w2->addUnit(0, kInfantry, 1, 1, 1, 1);  // spot 4 (TL)
    const int b = w2->addUnit(0, kInfantry, 2, 1, 2, 1);  // spot 4 in the destination cell
    w2->orderMove(0, a, 2, 1);
    const int t = runUntilIdle(*w2, 0, a, 400);
    CHECK(t > 0);
    const Unit& ua = w2->sides()[0].units[a];
    CHECK(ua.blockFromX == 2 && ua.cellspotFrom == 3);  // TL taken -> DL
    // At 1 px/tick an axis stops once x >= (target - Speed/2) >> 16, i.e. one pixel short
    // when approaching from below (MoveUnit 0x494260, faithful).
    CHECK(ua.posX == toFix(2 * 32 + 7 - 1) && ua.posY == toFix(1 * 32 + 26 - 1));
    CHECK((w2->cell(1, 1).flags & 0x3E0) == 0);
    CHECK((w2->cell(2, 1).flags & 0x3E0) == (0x200 | 0x100));
    (void)b;
}

void testFindPath() {
    Fixture f;
    auto w = f.world(16, 16);
    for (int y = 2; y <= 13; ++y) w->cell(8, y).flags = 0;  // vertical wall
    const int i = w->addUnit(0, kTank, 4, 8, 4, 8);
    const Unit& u = w->sides()[0].units[i];
    const Passable pass = [&](int x, int y) { return w->canUnitUseSquare(x, y, u, 0, 0); };
    PathFinder pf;
    PathResult r = pf.findPath(4, 8, 12, 8, pass);
    CHECK(r.status == kPathReached);
    CHECK(r.len > 0 && r.path[r.len - 1] == (PathPoint{12, 8}));
    auto valid = [&](const PathPoint* p, int len) {
        PathPoint prev{4, 8};
        for (int k = 0; k < len; ++k) {
            if (chebyshev(prev.x, prev.y, p[k].x, p[k].y) != 1) return false;
            if (!pass(p[k].x, p[k].y)) return false;
            prev = p[k];
        }
        return true;
    };
    CHECK(valid(r.path, r.len));
    const int rawLen = r.len;
    pf.smoothPath(4, 8, r.path, r.len, pass);
    CHECK(valid(r.path, r.len));
    CHECK(r.len <= rawLen && r.path[r.len - 1] == (PathPoint{12, 8}));
    // Goes round an end of the wall (y < 2 or y > 13).
    bool around = false;
    for (int k = 0; k < r.len; ++k)
        if (r.path[k].x == 8) around = r.path[k].y < 2 || r.path[k].y > 13;
    CHECK(around);

    // An enclosed target: no route; the result is a partial path or [S] without status 1.
    auto w2 = f.world(16, 16);
    for (int k = 9; k <= 13; ++k) {
        w2->cell(k, 9).flags = 0;
        w2->cell(k, 13).flags = 0;
        w2->cell(9, k).flags = 0;
        w2->cell(13, k).flags = 0;
    }
    const int j = w2->addUnit(0, kTank, 2, 2, 2, 2);
    const Unit& u2 = w2->sides()[0].units[j];
    const Passable pass2 = [&](int x, int y) { return w2->canUnitUseSquare(x, y, u2, 0, 0); };
    PathResult r2 = pf.findPath(2, 2, 11, 11, pass2);
    CHECK(r2.status != kPathReached);
}

void testVehicleMove() {
    Fixture f;
    auto w = f.world(16, 16);
    const int i = w->addUnit(0, kTank, 2, 2, 2, 2);
    w->orderMove(0, i, 7, 2);
    const Unit& u = w->sides()[0].units[i];
    bool sawTwoCells = false;
    int t = 0;
    for (t = 1; t <= 300; ++t) {
        w->tick();
        int marked = 0;
        for (int x = 0; x < 16; ++x) marked += (w->cell(x, 2).flags & 0x8) != 0;
        if (marked == 2) sawTwoCells = true;  // reserved BlockTo + BlockFrom until halfway
        CHECK(marked == 1 || marked == 2);
        if (u.state == UnitState::Idle) break;
    }
    // Facing 0 -> 8 turns in place first (one step per tick, rotation speed 0): 8 ticks; the
    // 0x80000000 -> 0x40000000 re-check then drops the route once (1 tick, movement.md §1.5);
    // 5 tiles x 16 ticks at 2 px/tick; +1 tick for the arrival check.
    CHECK(t == 8 + 1 + 5 * 16 + 1);
    CHECK(sawTwoCells);
    CHECK(u.blockFromX == 7 && u.blockFromY == 2 && u.blockToX == 7);
    CHECK(u.posX == tileCentre(7) && u.posY == tileCentre(2));  // exact centre: 32 / 2 px steps
    CHECK(u.facing == 8 && u.facingTurret == 8);
    CHECK((w->cell(7, 2).flags & 0xF) == 0x8);
    CHECK((w->cell(2, 2).flags & 0x8) == 0);
    CHECK((u.flags & unit_flags::kBlockToMarked) == 0);

    // Around a wall to a far tile, arriving exactly at the centre (diagonals included).
    auto w2 = f.world(16, 16);
    for (int y = 0; y <= 10; ++y) w2->cell(6, y).flags = 0;
    const int j = w2->addUnit(0, kTank, 2, 2, 2, 2);
    w2->orderMove(0, j, 10, 3);
    CHECK(runUntilIdle(*w2, 0, j, 2000) > 0);
    const Unit& u2 = w2->sides()[0].units[j];
    CHECK(u2.blockFromX == 10 && u2.blockFromY == 3);
    CHECK(u2.posX == tileCentre(10) && u2.posY == tileCentre(3));
    int vehicleCells = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) vehicleCells += (w2->cell(x, y).flags & 0x8) != 0;
    CHECK(vehicleCells == 1);
}

void testBlockedTarget() {
    // Ordered onto a friendly unit's tile: RouteMode escalates, the blocker is asked to move
    // away, and the mover accepts a tile within 2 of the goal. Nothing hangs.
    Fixture f;
    auto w = f.world(16, 16);
    const int mover = w->addUnit(0, kTank, 2, 8, 2, 8);
    const int blocker = w->addUnit(0, kTank, 10, 8, 10, 8);
    w->orderMove(0, mover, 10, 8);
    for (int t = 0; t < 3000; ++t) w->tick();
    const Unit& m = w->sides()[0].units[mover];
    const Unit& b = w->sides()[0].units[blocker];
    CHECK(m.state == UnitState::Idle && b.state == UnitState::Idle);
    CHECK(chebyshev(m.blockFromX, m.blockFromY, 10, 8) <= 2);
    CHECK(!(m.blockFromX == b.blockFromX && m.blockFromY == b.blockFromY));
    int vehicleCells = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) vehicleCells += (w->cell(x, y).flags & 0x8) != 0;
    CHECK(vehicleCells == 2);
}

// Owner play-test O6V1 (re/notes/movement.md §5.6): a tank on a building's corner exit tile at
// the map edge, boxed in by IDLE friendlies. Ordered 2 tiles away it gives up where it stands
// (RouteMode 0 and 1 find no path, then GetNextSquare 0x4936a0's "RouteMode >= 2 and within 2
// tiles of the goal" rule fires before any mode-2 route exists, so AskUnitsOnTileToMoveAway
// is never reached) - faithful. Ordered 3 tiles away, the mode-2 route makes the blocker
// step aside and the tank arrives.
void testBoxedInNearGoal() {
    Fixture f;
    for (int far = 0; far < 2; ++far) {
        auto w = f.world(16, 16);
        w->cell(14, 4).flags = w->cell(15, 4).flags = w->cell(14, 5).flags = 0;  // solid (building)
        const int mover = w->addUnit(0, kTank, 15, 5, 15, 5);
        const int b1 = w->addUnit(0, kTank, 14, 6, 14, 6);
        const int b2 = w->addUnit(0, kTank, 15, 6, 15, 6);
        const int ty = far ? 8 : 7;
        w->orderMove(0, mover, 14, ty);
        bool asked = false;
        for (int t = 0; t < 2000; ++t) {
            w->tick();
            for (int b : {b1, b2}) asked |= w->sides()[0].units[b].state == UnitState::MovingAway;
        }
        const Unit& m = w->sides()[0].units[mover];
        CHECK(m.state == UnitState::Idle);
        if (!far) {
            CHECK(!asked);
            CHECK(m.blockFromX == 15 && m.blockFromY == 5 && m.targetX == 15 && m.targetY == 5);
        } else {
            CHECK(asked);
            CHECK(m.blockFromX == 14 && m.blockFromY == 8);
        }
    }
}

void testInfantryAnimation() {
    Fixture f;
    auto w = f.world(16, 16);
    const int i = w->addUnit(0, kInfantry, 2, 2, 2, 2);
    w->orderMove(0, i, 8, 2);
    const Unit& u = w->sides()[0].units[i];
    bool walked = false;
    for (int t = 0; t < 60; ++t) {
        w->tick();
        if (u.posSteps != 0 && u.animType == 1 && u.animFrame >= 1 && u.animFrame <= 6) walked = true;
    }
    CHECK(walked);
    CHECK(runUntilIdle(*w, 0, i, 2000) > 0);
    w->tick();
    CHECK(u.animType == 0 || u.animType == 4 || u.animType >= 14);  // standing (or fidgeting)
}

void testRealMission() {
#ifdef D2K_GAME_DATA_DIR
    const std::string data = D2K_GAME_DATA_DIR;
    const std::string mapPath = formats::findFileNoCase(data + "/Missions", "H5V1.MAP");
    const std::string misPath = formats::findFileNoCase(data + "/maps", "_H5V1.MIS");
    if (mapPath.empty() || misPath.empty()) {
        std::printf("(game data not found: skipping mission test)\n");
        return;
    }
    const auto map = formats::loadMap(mapPath);
    const auto mis = formats::loadMis(misPath);
    const auto tileAtr = formats::loadTileAtr(formats::findFileNoCase(data + "/bin", mis.tileAttributes + ".BIN"));
    const auto tileData = formats::loadTileData(formats::findFileNoCase(data + "/bin", "TILEDATA.BIN"));
    auto templates = std::make_unique<formats::Templates>(
        formats::loadTemplates(formats::findFileNoCase(data + "/bin", "Templates.bin")));
    const auto speed = formats::loadSpeed(formats::findFileNoCase(data + "/bin", "SPEED.BIN"));
    World w(map, mis, tileAtr, tileData, {templates.get(), &speed, nullptr}, 0);
    int units = 0, buildings = 0;
    for (const auto& side : w.sides()) {
        for (int i = side.firstUnit; i != -1; i = side.nextObject(i)) ++units;
        for (int i = side.firstBuilding; i != -1; i = side.nextObject(i)) {
            ++buildings;
            const Building& b = side.buildings[i];
            CHECK(w.cell(b.tileX, b.tileY).buildingIndex >= 0 || templates->bin.buildings[b.type].tilesOccupiedSolid == 0 ||
                  !(templates->bin.buildings[b.type].tilesOccupiedSolid & 1));
        }
    }
    CHECK(units > 0 && buildings > 0);
    std::printf("mission test: H5V1 has %d units, %d buildings\n", units, buildings);
    // Move the first ordinary ground vehicle (any side) a few tiles and check it arrives.
    bool moved = false;
    for (int sd = 0; sd < kMaxSides && !moved; ++sd) {
    Side& s0 = w.sides()[sd];
    for (int i = s0.firstUnit; i != -1; i = s0.nextObject(i)) {
        Unit& u = s0.units[i];
        const auto& t = w.unitTemplate(u);
        if (t.isInfantry || (u.flags & 0x40) || t.specialBehavior != 0) continue;
        int tx = -1, ty = -1;
        for (int r = 4; r >= 1 && tx < 0; --r)
            for (int dy = -r; dy <= r && tx < 0; ++dy)
                for (int dx = -r; dx <= r && tx < 0; ++dx)
                    if (w.canUnitUseSquare(u.blockFromX + dx, u.blockFromY + dy, u, sd, 0)) {
                        tx = u.blockFromX + dx;
                        ty = u.blockFromY + dy;
                    }
        if (tx < 0) continue;
        w.orderMove(sd, i, tx, ty);
        int arrived = -1;
        for (int k = 1; k <= 1500 && arrived < 0; ++k) {
            w.tick();
            if (u.state != UnitState::Moving && u.posSteps == 0 && k > 1) arrived = k;
        }
        CHECK(arrived > 0);
        CHECK(u.blockFromX == tx && u.blockFromY == ty);
        std::printf("mission test: side %d unit #%d reached (%d,%d) in %d ticks\n", sd, i, tx, ty, arrived);
        moved = true;
        break;
    }
    }
    CHECK(moved);
#endif
}


// ---------------------------------------------------------------------------------------
// Combat

constexpr int kDummy = 3;  // unarmed target vehicle
constexpr int kTurret = 0; // building type

// Fixture plus weapons, warheads, an explosion and a turret building.
struct CombatFixture : Fixture {
    formats::ArmourTable armour{};
    CombatFixture() {
        auto& b = templates->bin;
        std::memset(b.groupIds, 0xff, sizeof(b.groupIds));  // no special weapons/explosions...
        b.groupIds[49] = 0;                                  // ...no debris
        b.groupIds[58] = 0;                                  // EX_WHITEN1 = explosion 0
        b.unitCount = 4;
        auto& w = b.weapons[0];
        w.projectileSpeed = 0x80000;  // 8 px/tick
        w.damage = 100;
        w.range = 128;                // 4 tiles
        w.warhead = 0;
        w.hitExplosion = 0;
        w.trailExplosion = -1;
        b.weapons[1] = w;
        b.weapons[2] = w;
        b.projectileArtDirections[0] = 32;
        auto& tank = b.units[kTank];
        tank.primaryWeaponLongDelay = 10;
        tank.primaryWeaponShootOffset = 2;  // muzzle 30 px from an adjacent target: no inaccuracy
        tank.barrelArt = 0;  // muzzle offset follows the turret (barrelArt -1: the body)
        tank.muzzleFlashExplosion = -1;
        tank.deathExplosion = -1;
        b.units[kInfantry].barrelArt = -1;
        b.units[kInfantry].muzzleFlashExplosion = -1;
        b.units[kInfantry].deathExplosion = -1;
        b.units[kInfantry].primaryWeaponLongDelay = 10;
        b.units[kTrike].muzzleFlashExplosion = -1;
        b.units[kTrike].deathExplosion = -1;
        b.units[kTrike].barrelArt = -1;
        auto& d = b.units[kDummy];
        d.hitPoints = 400;
        d.speed = 0x20000;
        d.primaryWeapon = d.secondaryWeapon = -1;
        d.speedType = 1;
        d.deathExplosion = 0;
        d.muzzleFlashExplosion = -1;
        d.barrelArt = -1;
        auto& t = b.buildings[kTurret];
        t.hitPoints = 1000;
        t.primaryWeapon = 0;
        t.secondaryWeapon = -1;
        t.actLikeTurret = 1;
        t.primaryWeaponLongDelay = 10;
        t.artHeight = 32;
        t.artWidth = 32;
        t.tilesOccupiedAll = t.tilesOccupiedSolid = 1;
        t.specialBehavior = 16;
        t.deathExplosion = -1;
        t.muzzleFlashExplosion = -1;
        t.flags = 0x400000;  // no concrete
        templates->explosions.assign(1, formats::ExplosionTemplate{0, 0, 0, 0, -1});
        templates->animationArtFrames.assign(1, 4);
        templates->animationArtFlags.assign(1, 0);
        armour.warheads[0].versusArmour.fill(50);  // 50 % against every armour
        armour.warheads[0].radius = 16;
        armour.warheads[0].infantryDeath = 2;
    }
    Rules rules() const { return {templates.get(), &speed, &armour}; }
    std::unique_ptr<World> world(int w, int h) const {
        auto wd = std::make_unique<World>(w, h, rules(), 0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) wd->cell(x, y).flags = kRock;
        return wd;
    }
};

void testSinCosTable() {
    CHECK(sinTable(8, 32) == 0x10000);    // 1 px east
    CHECK(cosTable(0, 32) == 0x10000);    // 1 px north
    CHECK(cosTable(16, 32) == -0x10000);  // south
    CHECK(sinTable(0, 100) == 0);
    // The whole table is built with std::sin / std::cos at start-up: the C library must give the
    // same truncated values on every platform (macOS libm, glibc, the MSVC CRT) or the simulation
    // diverges. FNV-1a over all 2 x 16384 entries, value from macOS / arm64.
    std::uint32_t h = 2166136261u;
    for (int i = 0; i < kSinCosSize; ++i)
        for (const std::int32_t v : {sinTableFlat(i), cosTableFlat(i)})
            for (int b = 0; b < 4; ++b) {
                h ^= (static_cast<std::uint32_t>(v) >> (8 * b)) & 0xffu;
                h *= 16777619u;
            }
    if (h != 0x72e12f77u) std::fprintf(stderr, "sin/cos table hash 0x%08x\n", static_cast<unsigned>(h));
    CHECK(h == 0x72e12f77u);
}

// A tank ordered to attack an adjacent unarmed vehicle: no inaccuracy (muzzle 30 px away),
// the shell lands on the target's pixel, damage = 100 * 50 % = 50, 400 hp -> 8 shots.
void testAttackUnitKills() {
    CombatFixture f;
    auto w = f.world(12, 12);
    const int a = w->addUnit(0, kTank, 5, 5, 5, 5);
    const int t = w->addUnit(1, kDummy, 6, 5, 6, 5);
    Unit& tu = w->sides()[1].units[t];
    CHECK(tu.state == UnitState::Idle);  // unarmed: Guarding refused
    w->orderAttackUnit(0, a, 1, t);
    int shots = 0, killedAt = -1, hits = 0;
    std::int32_t hp = tu.health;
    for (int k = 1; k <= 400 && killedAt < 0; ++k) {
        w->tick();
        const Unit& au = w->sides()[0].units[a];
        if (au.reloadDelay == 10) ++shots;
        if (tu.health != hp) {
            ++hits;
            CHECK(hp - tu.health == 50 || tu.health == 0);
            hp = tu.health;
        }
        if (tu.state == UnitState::Dead) killedAt = k;
    }
    CHECK(killedAt > 0);
    CHECK(shots == 8);
    CHECK(hits == 8);
    CHECK(w->sides()[0].units[a].state == UnitState::AttackingUnit || w->sides()[0].units[a].state == UnitState::Moving ||
          w->sides()[0].units[a].state == UnitState::Idle);
    CHECK(w->sides()[1].unitsLost == 1 && w->sides()[0].unitsKilled == 1);
    CHECK(tu.attackerSide == 0 && tu.attackerIndex == a);
    // Death explosion: EX_WHITEN1 + the template's explosion (both type 0 here).
    int explosions = 0;
    for (int i = w->sides()[1].firstExplosion; i != -1; i = w->sides()[1].nextObject(i)) ++explosions;
    CHECK(explosions == 2);
    // The wreck stays 10 ticks (DeadStateTimer), then the slot is freed and the cell released.
    for (int k = 0; k < 12; ++k) w->tick();
    CHECK(w->unit(1, t) == nullptr);
    CHECK((w->cell(6, 5).flags & cell_flags::kVehicle) == 0);
    CHECK(w->sides()[1].allocObject() == t);  // LIFO: the freed slot comes back first
    // Explosions are gone after 4 frames x 3 ticks.
    int left = 0;
    for (int i = w->sides()[1].firstExplosion; i != -1; i = w->sides()[1].nextObject(i)) ++left;
    CHECK(left == 0);
    // The attacker returned to rest (target gone -> Moving -> Idle).
    for (int k = 0; k < 5; ++k) w->tick();
    CHECK(w->sides()[0].units[a].enemyIndex == kNoIndex || w->sides()[0].units[a].state == UnitState::Idle);
}

// A straight bullet: (dist << 16) / speed steps, then it explodes at the target pixel.
void testBulletFlight() {
    CombatFixture f;
    f.templates->bin.weapons[0].projectileSpeed = 0x40000;  // 4 px/tick
    auto w = f.world(12, 12);
    const int b = w->addBullet(0, 0, 0, kNoIndex, 16, 16, 116, 16);
    CHECK(b == 999);
    const Bullet& bu = w->sides()[0].bullets[b];
    CHECK(bu.moveSteps == 25 && bu.speedX == 0x40000 && bu.speedY == 0);
    CHECK(bu.animFrame == 8);  // facing east, 32 directions
    for (int k = 0; k < 25; ++k) w->tick();
    CHECK(w->sides()[0].links[b].kind == SlotKind::Bullet);
    CHECK(bu.posX == 116 << 16 && bu.posY == 16 << 16);
    w->tick();
    // Freed after the update; the hit explosion was allocated during it (next slot).
    CHECK(w->sides()[0].links[b].kind == SlotKind::Free);
    CHECK(w->sides()[0].firstBullet == -1 && w->sides()[0].firstExplosion == 998);
    const Explosion& e = w->sides()[0].explosions[w->sides()[0].firstExplosion];
    CHECK(e.posX == 116 << 16 && e.posY == 16 << 16 && e.frame == 1);  // updated once in the same tick
}

// A turret building engages an enemy unit two tiles away (rotating its turret first).
void testTurretEngages() {
    CombatFixture f;
    auto w = f.world(12, 12);
    const int bi = w->addBuilding(1, kTurret, 2, 2, false);
    CHECK(bi == 999);
    const int t = w->addUnit(0, kDummy, 4, 2, 4, 2);
    Unit& tu = w->sides()[0].units[t];
    for (int k = 0; k < 60; ++k) w->tick();
    const Building& b = w->sides()[1].buildings[bi];
    CHECK(b.facing == 8);  // turned east
    CHECK(b.enemySide == 0 && b.enemyIndex == t);
    CHECK(tu.health < 400);
    CHECK(tu.attackerSide == 1 && tu.attackerIndex == bi);
    // Terrain under the hits collected damage: 100 * 50 % / 100 / 40 = 1 per hit.
    CHECK(w->cell(4, 2).damage > 0);
}

// An infantry kill picks the warhead's death animation (infantryDeath + 9).
void testInfantryDeath() {
    CombatFixture f;
    auto w = f.world(12, 12);
    const int i = w->addUnit(1, kInfantry, 6, 6, 6, 6);
    Unit& u = w->sides()[1].units[i];
    u.health = 10;
    w->damageTiles(u.posX >> 16, u.posY >> 16, 0, 0, 0, kNoIndex, false);
    CHECK(u.state == UnitState::Dead);
    CHECK(u.animType == 11);
    CHECK(u.deadStateTimer == 50);
}

// DestroyBuilding 0x49ca10 + UpdateBuilding state 17: 2 death explosions per solid tile inside
// the footprint (delay 0/1, hidden frame 0), the building keeps its slot and tiles (and is
// drawn) for 30 ticks, survivors leave at DeadTimer 25 (tick 6), removal on tick 31.
void testBuildingDeathSequence() {
    CombatFixture f;
    constexpr int kBunker = 1;
    auto& bt = f.templates->bin.buildings[kBunker];
    bt = f.templates->bin.buildings[kTurret];
    bt.primaryWeapon = -1;
    bt.actLikeTurret = 0;
    bt.specialBehavior = 0;
    bt.artWidth = 64;
    bt.artHeight = 64;
    bt.tilesOccupiedAll = bt.tilesOccupiedSolid = 0x33;  // 2x2
    bt.deathExplosion = 0;
    bt.costBuild = 300;
    bt.flags = 0x400000 | building_flags::kSelectable;
    f.templates->bin.units[kInfantry].cost = 50;  // 300 / 3 / 50 = 2 survivors
    f.templates->animationArtFrames[0] = 10;      // death explosion: 10 frames x 3 ticks
    auto w = f.world(16, 16);
    const int bi = w->addBuilding(1, kBunker, 6, 6, false);
    Side& s = w->sides()[1];
    Building& b = s.buildings[bi];
    auto countExplosions = [&](bool visibleOnly) {
        int n = 0;
        for (int i = s.firstExplosion; i >= 0; i = s.nextObject(i))
            if (!visibleOnly || !(s.explosions[i].flags & explosion_flags::kHidden)) ++n;
        return n;
    };
    auto countInfantry = [&] {
        int n = 0;
        for (int i = s.firstUnit; i >= 0; i = s.nextObject(i)) n += s.units[i].type == kInfantry;
        return n;
    };
    b.health = 10;
    w->damageTiles(7 * 32 + 16, 7 * 32 + 16, 0, 0, 0, kNoIndex, false);
    CHECK(b.state == 17 && b.deadTimer == 30 && b.health == 10);  // the killing hit does not zero health
    CHECK(countExplosions(false) == 8);
    CHECK(countExplosions(true) == 0);  // flag 0x20: frame 0 is never drawn
    for (int i = s.firstExplosion; i >= 0; i = s.nextObject(i)) {
        const auto& e = s.explosions[i];
        CHECK(e.posX >> 16 >= 6 * 32 && e.posX >> 16 < 8 * 32);
        CHECK(e.posY >> 16 >= 6 * 32 && e.posY >> 16 < 8 * 32);
        CHECK(e.delay <= 1);
    }
    int tick = 0, survivorsAt = -1, removedAt = -1, maxVisible = 0;
    while (tick < 60) {
        w->tick();
        ++tick;
        maxVisible = std::max(maxVisible, countExplosions(true));
        if (survivorsAt < 0 && countInfantry() > 0) survivorsAt = tick;
        if (s.links[bi].kind != SlotKind::Building || !b.alive) {
            removedAt = tick;
            break;
        }
        CHECK(w->cell(6, 6).buildingSide == 1 && w->cell(7, 7).buildingSide == 1);  // tiles kept while dying
    }
    CHECK(maxVisible == 8);
    CHECK(survivorsAt == 6);
    CHECK(countInfantry() == 2);
    CHECK(removedAt == 31);
    CHECK(w->cell(6, 6).buildingSide < 0 && w->cell(7, 7).buildingSide < 0);
    std::printf("building death: 8 explosions, survivors at tick %d, removed at tick %d\n", survivorsAt, removedAt);
}

// Crushing (CrushInfantry 0x49f8f0): the squashed infantry stays as a dead unit playing the
// crushed animation (type 13) for the death timer, then is removed.
void testCrushInfantry() {
    CombatFixture f;
    auto w = f.world(12, 12);
    const int a = w->addUnit(0, kTank, 2, 6, 2, 6);
    const int i = w->addUnit(1, kInfantry, 4, 6, 4, 6);
    w->orderMove(0, a, 6, 6);
    bool crushed = false;
    int deadTicks = 0;
    for (int t = 0; t < 200; ++t) {
        w->tick();
        const Unit& u = w->sides()[1].units[i];
        if (std::getenv("D2K_TRACE_CRUSH"))
            std::printf("t%d alive %d kind %d state %d anim %d/%d dead %d flags %x\n", t, u.alive,
                        int(w->sides()[1].links[i].kind), int(u.state), u.animType, u.animFrame, u.deadStateTimer, u.flags);
        if (w->sides()[1].links[i].kind == SlotKind::Unit && u.state == UnitState::Dead) {
            crushed = crushed || u.animType == 13;
            ++deadTicks;
        }
    }
    CHECK(crushed);
    CHECK(deadTicks > 10);
}

// A survivor walking out (LEAVING_BUILDING, state 8) whose exit cell an enemy vehicle takes after the step began
// waits in state 8 forever (UpdateUnit case 6). Vanilla DamageTiles skips state 8, so it was immune (an original
// bug); like the community patch we only skip it on building tiles (owner's O8V1 save).
void testLeavingBuildingInfantryDamage() {
    CombatFixture f;
    {
        Rules r = f.rules();
        auto w = std::make_unique<World>(12, 12, r, 0);
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 12; ++x) w->cell(x, y).flags = kRock;
        const int i = w->addUnit(1, kInfantry, 5, 6, 6, 6);
        Unit& u = w->sides()[1].units[i];
        CHECK(u.state == UnitState::LeavingBuilding);
        for (int t = 0; t < 10 && u.posSteps == 0; ++t) w->tick();
        CHECK(u.posSteps != 0 && (u.flags & unit_flags::kBlockToMarked));
        w->addUnit(0, kDummy, 6, 6, 6, 6);  // an enemy vehicle drives onto the reserved exit cell
        for (int t = 0; t < 50; ++t) w->tick();
        CHECK(u.state == UnitState::LeavingBuilding);  // stuck (faithful)
        const std::int32_t hp = u.health;
        w->damageTiles(u.posX >> 16, u.posY >> 16, 0, 0, 0, kNoIndex, false);
        CHECK(u.state == UnitState::Dead);
        (void)hp;
        // Still on a building tile: immune either way.
        auto w2 = std::make_unique<World>(12, 12, r, 0);
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 12; ++x) w2->cell(x, y).flags = kRock;
        const int j = w2->addUnit(1, kInfantry, 5, 6, 6, 6);
        Unit& v = w2->sides()[1].units[j];
        w2->cell(5, 6).flags |= cell_flags::kBuilding;
        w2->damageTiles(v.posX >> 16, v.posY >> 16, 0, 0, 0, kNoIndex, false);
        CHECK(v.state == UnitState::LeavingBuilding && v.health == f.templates->bin.units[kInfantry].hitPoints);
    }
}

// Right-click semantics: enemy unit -> attack (target flickers), empty tile -> move.
void testSmartOrder() {
    CombatFixture f;
    auto w = f.world(12, 12);
    const int a = w->addUnit(0, kTank, 2, 2, 2, 2);
    const int t = w->addUnit(1, kDummy, 8, 8, 8, 8);
    w->sides()[0].units[a].selected = true;
    CHECK(w->smartOrder(0, 8 * 32 + 10, 8 * 32 + 10) == SmartOrder::AttackUnit);
    CHECK(w->sides()[1].units[t].flicker == 7);
    w->tick();
    CHECK(w->sides()[0].units[a].state == UnitState::AttackingUnit);
    CHECK(w->sides()[0].units[a].enemyIndex == t);
    CHECK(w->smartOrder(0, 3 * 32, 9 * 32) == SmartOrder::Move);
    w->tick();
    CHECK(w->sides()[0].units[a].state == UnitState::Moving);
    w->decayFlicker();
    CHECK(w->sides()[1].units[t].flicker == 6);
}

// Alt (force move) on enemy infantry, 0x4433a0: with a crusher selected and a drivable cell it is a
// plain GenerateUnitMoveOrder to the cell - the tank drives over the infantry and never fires
// (UpdateUnit state Moving does no targeting). Without a crusher, without Alt or on an undrivable
// cell it stays an attack order. Ctrl on an own unit = attack it.
void testForceMoveCrush() {
    CombatFixture f;
    {
        auto w = f.world(12, 12);
        const int a = w->addUnit(0, kTank, 1, 5, 1, 5);
        const int i = w->addUnit(1, kInfantry, 8, 5, 8, 5);  // out of the tank's 4-tile range
        f.templates->bin.units[kInfantry].primaryWeapon = -1;  // keep it from walking up to the tank
        Unit& tank = w->sides()[0].units[a];
        Unit& inf = w->sides()[1].units[i];
        tank.selected = true;
        const int px = fixToPixel(inf.posX), py = fixToPixel(inf.posY);
        CHECK(w->smartOrder(0, px, py, false, true) == SmartOrder::Move);
        CHECK(inf.flicker == 7);
        w->tick();
        CHECK(tank.state == UnitState::Moving && tank.targetX == 8 && tank.targetY == 5 && tank.enemyIndex == kNoIndex);
        bool fired = false;
        for (int t = 0; t < 200 && inf.state != UnitState::Dead; ++t) {
            w->tick();
            fired = fired || w->sides()[0].firstBullet != -1;  // (reloadDelay starts at LongDelay at spawn)
        }
        CHECK(inf.state == UnitState::Dead);  // crushed
        CHECK(!fired);
        CHECK(tank.blockToX == 8 && tank.blockToY == 5);
        f.templates->bin.units[kInfantry].primaryWeapon = 1;
    }
    {
        auto w = f.world(12, 12);
        const int a = w->addUnit(0, kTrike, 2, 5, 2, 5);  // cannot crush
        const int i = w->addUnit(1, kInfantry, 6, 5, 6, 5);
        w->sides()[0].units[a].selected = true;
        const Unit& inf = w->sides()[1].units[i];
        CHECK(w->smartOrder(0, fixToPixel(inf.posX), fixToPixel(inf.posY), false, true) == SmartOrder::AttackUnit);
        w->sides()[0].units[a].selected = false;
        const int b = w->addUnit(0, kTank, 2, 7, 2, 7);
        w->sides()[0].units[b].selected = true;
        w->sides()[0].units[a].selected = true;  // any crusher among the selection is enough
        CHECK(w->smartOrder(0, fixToPixel(inf.posX), fixToPixel(inf.posY), false, true) == SmartOrder::Move);
        w->cell(6, 5).flags &= ~cell_flags::kDriveOn;  // not drivable: attack instead
        CHECK(w->smartOrder(0, fixToPixel(inf.posX), fixToPixel(inf.posY), false, true) == SmartOrder::AttackUnit);
        // Own unit: no Ctrl -> left to the frontend (select), Ctrl -> attack it.
        const int o = w->addUnit(0, kDummy, 9, 9, 9, 9);
        const Unit& own = w->sides()[0].units[o];
        CHECK(w->smartOrder(0, fixToPixel(own.posX), fixToPixel(own.posY)) == SmartOrder::Friendly);
        CHECK(w->smartOrder(0, fixToPixel(own.posX), fixToPixel(own.posY), true) == SmartOrder::AttackUnit);
        w->tick();
        CHECK(w->sides()[0].units[b].state == UnitState::AttackingUnit && w->sides()[0].units[b].enemyIndex == o);
    }
}

// A selected own turret with no units selected (0x4433a0 -> GenerateBuildingAttackUnitOrder
// 0x459d20 / GenerateBuildingAttackBuildingOrder 0x459de0, ProcessOrder cases 0xb / 0xc):
// enemy objects with a plain click, own ones with Ctrl; only within reach of the mouse tile /
// building (else nothing: the frontend selects the object); no attack-ground order for buildings.
void testTurretForceAttack() {
    CombatFixture f;
    constexpr int kBunker = 1;
    auto& bt = f.templates->bin.buildings[kBunker];
    bt = f.templates->bin.buildings[kTurret];
    bt.primaryWeapon = -1;
    bt.barrelArt = -1;
    bt.actLikeTurret = 0;
    bt.specialBehavior = 0;
    bt.flags = 0x400000 | building_flags::kSelectable;
    auto w = f.world(16, 16);
    const int ti = w->addBuilding(0, kTurret, 2, 2, false);
    Building& turret = w->sides()[0].buildings[ti];
    const int own = w->addUnit(0, kDummy, 4, 2, 4, 2);
    Unit& ou = w->sides()[0].units[own];
    const int far = w->addUnit(1, kDummy, 12, 12, 12, 12);
    const int ob = w->addBuilding(0, kBunker, 2, 5, false);
    const int eb = w->addBuilding(1, kBunker, 5, 3, false);
    turret.selected = true;
    const int opx = fixToPixel(ou.posX), opy = fixToPixel(ou.posY);
    CHECK(w->selectedBarrelBuilding(0) == &turret);
    CHECK(w->smartOrder(0, opx, opy) == SmartOrder::Friendly);
    CHECK(w->smartOrder(0, opx, opy, true) == SmartOrder::TurretAttackUnit);
    CHECK(ou.flicker == 7);
    w->tick();
    CHECK(turret.state == 4 && turret.enemySide == 0 && turret.enemyIndex == own);
    for (int k = 0; k < 80; ++k) w->tick();
    CHECK(ou.health < 400);  // the turret shot its own unit
    // Out of reach: nothing ordered.
    const Unit& fu = w->sides()[1].units[far];
    CHECK(w->smartOrder(0, fixToPixel(fu.posX), fixToPixel(fu.posY), true) == SmartOrder::None);
    // Ground: no building attack-tile order.
    CHECK(w->smartOrder(0, 3 * 32 + 16, 4 * 32 + 16, true) == SmartOrder::None);
    // Enemy building in range: plain click; own building: Ctrl.
    CHECK(w->smartOrder(0, 5 * 32 + 16, 3 * 32 + 16) == SmartOrder::TurretAttackBuilding);
    w->tick();
    CHECK(turret.state == 5 && turret.enemySide == 1 && turret.enemyIndex == eb);
    CHECK(w->smartOrder(0, 2 * 32 + 16, 5 * 32 + 16) == SmartOrder::Friendly);
    CHECK(w->smartOrder(0, 2 * 32 + 16, 5 * 32 + 16, true) == SmartOrder::TurretAttackBuilding);
    w->tick();
    CHECK(turret.state == 5 && turret.enemySide == 0 && turret.enemyIndex == ob);
    // Ctrl on the turret itself: ProcessOrder ignores the self target.
    turret.state = 2;
    CHECK(w->smartOrder(0, 2 * 32 + 16, 2 * 32 + 16, true) == SmartOrder::TurretAttackBuilding);
    w->tick();
    CHECK(turret.state == 2);
    // With units selected the units get the order instead.
    ou.selected = true;
    CHECK(w->smartOrder(0, fixToPixel(fu.posX), fixToPixel(fu.posY)) == SmartOrder::AttackUnit);
}

// Sidebar Guard / Retreat buttons (GenerateUnitGuardOrder 0x459980 / GenerateUnitRetreatOrder
// 0x459ab0): guard needs a selected armed unit; retreat needs a base position (a building).
void testGuardRetreat() {
    CombatFixture f;
    auto w = f.world(12, 12);
    const int a = w->addUnit(0, kTank, 2, 2, 2, 2);
    CHECK(!w->orderGuardSelected(0));  // nothing selected
    w->sides()[0].units[a].selected = true;
    CHECK(w->orderGuardSelected(0));
    w->tick();
    CHECK(w->sides()[0].units[a].state == UnitState::Guarding);
    int x = -1, y = -1;
    CHECK(!w->findBestBasePosition(0, x, y));  // no buildings
    CHECK(x == 2 && y == 2);                    // falls back to the first ground unit
    CHECK(!w->orderRetreatSelected(0));
}

// Data-backed soak: real campaign missions with ARMOUR.BIN / BUILEXP.BIN, 3000 ticks of
// combat after ordering every armed unit to attack; checks the object lists stay consistent.
void testRealCombatSoak() {
#ifdef D2K_GAME_DATA_DIR
    const std::string data = D2K_GAME_DATA_DIR;
    const std::string bin = data + "/bin";
    if (formats::findFileNoCase(bin, "ARMOUR.BIN").empty()) return;
    auto templates = std::make_unique<formats::Templates>(formats::loadTemplates(formats::findFileNoCase(bin, "Templates.bin")));
    const auto speed = formats::loadSpeed(formats::findFileNoCase(bin, "SPEED.BIN"));
    const auto armour = formats::loadArmour(formats::findFileNoCase(bin, "ARMOUR.BIN"));
    const auto builexp = formats::readFile(formats::findFileNoCase(bin, "BUILEXP.BIN"));
    const auto tileData = formats::loadTileData(formats::findFileNoCase(bin, "TILEDATA.BIN"));
    for (const char* name : {"H5V1", "A3V1", "O6V1", "A8V1"}) {
        const std::string mapPath = formats::findFileNoCase(data + "/Missions", std::string(name) + ".MAP");
        const std::string misPath = formats::findFileNoCase(data + "/maps", std::string("_") + name + ".MIS");
        if (mapPath.empty() || misPath.empty()) continue;
        const auto map = formats::loadMap(mapPath);
        const auto mis = formats::loadMis(misPath);
        const auto tileAtr = formats::loadTileAtr(formats::findFileNoCase(bin, mis.tileAttributes + ".BIN"));
        World w(map, mis, tileAtr, tileData, {templates.get(), &speed, &armour, nullptr, &builexp}, 0);
        int maxBullets = 0, unitsBefore = 0, unitsAfter = 0, buildingsBefore = 0, buildingsAfter = 0;
        for (const auto& sd : w.sides()) {
            for (int i = sd.firstUnit; i != -1; i = sd.nextObject(i)) ++unitsBefore;
            for (int i = sd.firstBuilding; i != -1; i = sd.nextObject(i)) ++buildingsBefore;
        }
        // Every armed ground unit attacks the nearest enemy unit (exercises all combat paths).
        for (int sd = 0; sd < kMaxSides; ++sd) {
            Side& S = w.sides()[sd];
            for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
                const Unit& u = S.units[i];
                if (w.unitTemplate(u).primaryWeapon < 0 || (u.flags & 0x40)) continue;
                int best = 1 << 30, bs = -1, bi = -1;
                for (int es = 0; es < kMaxSides; ++es) {
                    if (w.diplomacy(sd, es) != 1) continue;
                    const Side& E = w.sides()[es];
                    for (int j = E.firstUnit; j != -1; j = E.nextObject(j)) {
                        const Unit& e = E.units[j];
                        if (e.flags & 0x400140) continue;
                        const int d = std::abs(e.blockFromX - u.blockFromX) + std::abs(e.blockFromY - u.blockFromY);
                        if (d < best) { best = d; bs = es; bi = j; }
                    }
                }
                if (bs >= 0) w.orderAttackUnit(sd, i, bs, bi);
            }
        }
        for (int k = 0; k < 3000; ++k) {
            w.tick();
            int bullets = 0;
            for (const auto& sd : w.sides()) {
                for (int i = sd.firstBullet; i != -1; i = sd.nextObject(i)) {
                    ++bullets;
                    CHECK(sd.links[i].kind == SlotKind::Bullet && sd.bullets[i].alive);
                }
                for (int i = sd.firstUnit; i != -1; i = sd.nextObject(i)) CHECK(sd.units[i].alive);
            }
            if (bullets > maxBullets) maxBullets = bullets;
        }
        for (const auto& sd : w.sides()) {
            for (int i = sd.firstUnit; i != -1; i = sd.nextObject(i)) ++unitsAfter;
            for (int i = sd.firstBuilding; i != -1; i = sd.nextObject(i)) ++buildingsAfter;
        }
        std::printf("combat soak %s: units %d -> %d, buildings %d -> %d, max bullets in flight %d\n", name, unitsBefore,
                    unitsAfter, buildingsBefore, buildingsAfter, maxBullets);
    }
#endif
}


// ---------------------------------------------------------------------------------------
// Economy and production (re/notes/economy.md)

constexpr std::uint32_t kBuildRock = kRock | 0x8000;  // rock you can build on
constexpr int kHarv = 4, kCarry = 5, kMcv = 6;        // unit types added by EcoFixture
constexpr int kYard = 0, kWind = 1, kRefin = 2, kHeavy = 3, kSlab = 4;  // building types
constexpr int kGrpYard = 0, kGrpWind = 1, kGrpRefin = 2, kGrpHeavy = 3, kGrpSlab = 4;

struct EcoFixture : Fixture {
    EcoFixture() {
        auto& b = templates->bin;
        std::memset(b.groupIds, 0xff, sizeof(b.groupIds));
        b.groupIds[group_ids::kHeavyFactoryQueue] = kGrpHeavy;
        b.groupIds[group_ids::kRefinery] = kGrpRefin;
        b.groupIds[group_ids::kHarvester] = kHarv;
        b.groupIds[group_ids::kCarryall] = kCarry;
        b.unitCount = 7;
        b.units[kTank].unitGroup = 0;
        b.units[kInfantry].unitGroup = 1;
        b.units[kTrike].unitGroup = 2;
        auto& tank = b.units[kTank];
        tank.ownerHouse = 0xff;
        tank.cost = 700;
        tank.buildSpeed = 1000;
        tank.prereq1BuildingGroup = kGrpHeavy;
        tank.prereq1OwnerHouse = 0xff;
        tank.prereq2BuildingGroup = -1;
        for (int t : {kInfantry, kTrike}) b.units[t].prereq1BuildingGroup = b.units[t].prereq2BuildingGroup = -1;
        auto& h = b.units[kHarv];
        h.unitGroup = 4;
        h.ownerHouse = 0xff;
        h.hitPoints = 500;
        h.speed = 0x10000;
        h.speedType = 1;
        h.primaryWeapon = h.secondaryWeapon = -1;
        h.specialBehavior = behaviour::kHarvester;
        h.cost = 1200;
        h.buildSpeed = 18;
        h.prereq1BuildingGroup = kGrpHeavy;
        h.prereq1OwnerHouse = 0xff;
        h.prereq2BuildingGroup = -1;
        auto& c = b.units[kCarry];
        c.unitGroup = 5;
        c.ownerHouse = 0xff;
        c.hitPoints = 300;
        c.speed = 0x40000;
        c.unitRotationSpeed = 1;
        c.speedType = 3;
        c.primaryWeapon = c.secondaryWeapon = -1;
        c.specialBehavior = behaviour::kCarryall;
        c.prereq1BuildingGroup = c.prereq2BuildingGroup = -1;
        auto& m = b.units[kMcv];
        m.unitGroup = 6;
        m.ownerHouse = 0xff;
        m.hitPoints = 800;
        m.speed = 0xc000;
        m.speedType = 1;
        m.primaryWeapon = m.secondaryWeapon = -1;
        m.specialBehavior = behaviour::kMcv;
        m.prereq1BuildingGroup = m.prereq2BuildingGroup = -1;

        b.buildingCount = 5;
        auto bld = [&](int i, int group, int beh, std::uint32_t all, std::uint32_t solid, int power, std::uint32_t cost,
                       std::uint32_t speed, int art) -> formats::BuildingTemplate& {
            auto& t = b.buildings[i];
            t.hitPoints = 1000;
            t.ownerHouse = 0xff;
            t.buildingGroup = static_cast<std::uint8_t>(group);
            t.specialBehavior = static_cast<std::uint8_t>(beh);
            t.tilesOccupiedAll = all;
            t.tilesOccupiedSolid = solid;
            t.powerConsumption = power;
            t.costBuild = cost;
            t.buildSpeedBuild = speed;
            t.artHeight = static_cast<std::uint8_t>(art);
            t.artWidth = 96;
            t.primaryWeapon = t.secondaryWeapon = -1;
            t.prereq1BuildingGroup = kGrpYard;
            t.prereq1OwnerHouse = 0xff;
            t.prereq2BuildingGroup = -1;
            t.flags = building_flags::kSelectable;
            return t;
        };
        bld(kYard, kGrpYard, building_behaviour::kConstructionYard, 0x777, 0x77, -20, 2000, 0, 96).techLevelBuild = 9;
        b.buildings[kYard].prereq1BuildingGroup = -1;
        bld(kWind, kGrpWind, 0, 0x33, 0x33, -200, 300, 2304, 64);
        auto& r = bld(kRefin, kGrpRefin, building_behaviour::kRefinery, 0x777, 0x36, 75, 1500, 2304, 96);
        r.exitPoint1X = 2;
        r.exitPoint1Y = 1;
        auto& f = bld(kHeavy, kGrpHeavy, building_behaviour::kHeavyFactory, 0x777, 0x77, 100, 1000, 2304, 96);
        f.exitPoint1X = 1;
        f.exitPoint1Y = 2;
        f.exitPoint2X = 1;
        f.exitPoint2Y = 3;
        bld(kSlab, kGrpSlab, building_behaviour::kConcrete, 0x1, 0x33, 0, 20, 2304, 32);
    }
};

int countUnits(const Side& s, int type) {
    int n = 0;
    for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) n += s.units[i].type == type;
    return n;
}

// Harvesting takes one spice level per load step and rewrites the cell (map-and-spawn §1.6).
void testHarvestCell() {
    EcoFixture fx;
    auto w = fx.world(16, 16);
    Cell& c = w->cell(5, 5);
    c.flags |= 0x400000;  // thick spice (level 4)
    c.tile = 0x2ed;
    const int hi = w->addUnit(0, kHarv, 5, 5, 5, 5);
    Unit& h = w->sides()[0].units[hi];
    CHECK(h.state == UnitState::Harvesting);
    const int delay = static_cast<int>(w->vars().harvestLoadSpiceDelay);
    w->tick();
    CHECK(c.spice() == 3 && h.byte29 == 1 && h.animDelay == delay);
    for (int k = 0; k < delay + 1; ++k) w->tick();
    CHECK(c.spice() == 2 && h.byte29 == 2);
    for (int k = 0; k < delay + 1; ++k) w->tick();
    CHECK(c.spice() == 1 && h.byte29 == 3);
    for (int k = 0; k < delay + 1; ++k) w->tick();
    CHECK(c.spice() == 0 && h.byte29 == 4);
    const std::uint16_t sand[] = {48, 49, 50, 51, 68, 69, 70, 71};
    CHECK(std::find(std::begin(sand), std::end(sand), c.tile) != std::end(sand));
    for (int k = 0; k < delay + 1; ++k) w->tick();
    // No spice left anywhere and no refinery: loaded harvester goes for a refinery, finds none, idles.
    CHECK(h.byte29 == 4 && (h.state == UnitState::MovingToRefinery || h.state == UnitState::Idle));
    // Levels 6/7 drop straight to 2 (quirk).
    Cell& c2 = w->cell(9, 9);
    c2.flags |= 0x700000;
    const int h2 = w->addUnit(0, kHarv, 9, 9, 9, 9);
    w->tick();
    CHECK(c2.spice() == 2 && w->sides()[0].units[h2].byte29 == 1);
}

// Docking and unloading: each blob adds harvestBlobValue spice to the spice drip, paid out at
// max(10, (|spiceDrip| + |cashDrip|) / 16) per tick; credits = cash + spice.
void testUnloadCredits() {
    EcoFixture fx;
    auto w = fx.world(24, 24);
    Side& s = w->sides()[0];
    s.cash = 1000;
    const int ri = w->addBuilding(0, kRefin, 10, 10, true, true);
    CHECK(ri >= 0);
    const int hi = w->addUnit(0, kHarv, 12, 15, 12, 15);
    Unit& h = s.units[hi];
    CHECK(h.refineryIndex == ri);  // GetRefineryIndex at ModelAddUnit
    h.byte29 = 7;
    w->unitAdjustState(h, UnitState::MovingToRefinery);
    bool sawDock = false, checkedDrip = false;
    int t = 0;
    for (; t < 4000; ++t) {
        w->tick();
        if (h.state == UnitState::UnloadingSpice) {
            sawDock = true;
            CHECK(h.blockFromX == 12 && h.blockFromY == 11 && h.facing == 12);  // refinery (10,10) + exit point (2,1)
        }
        if (!checkedDrip && s.spiceHarvested == 100) {
            checkedDrip = true;
            CHECK(s.spice == 10 && s.spiceDrip == 90);  // first payout step of the first blob
        }
        if (h.state == UnitState::LeavingRefinery || h.state == UnitState::MovingToHarvest) break;
    }
    CHECK(sawDock && checkedDrip);
    CHECK(h.byte29 == 0);
    for (int k = 0; k < 200; ++k) w->tick();
    CHECK(s.spiceHarvested == 700);
    CHECK(s.maxStorage == 2000);
    CHECK(w->credits(0) == 1700 && s.spiceDrip == 0 && s.cash == 1000 && s.spice == 700);
    std::printf("economy: harvester unloaded 7 blobs in %d ticks, credits 1000 -> %d\n", t, w->credits(0));
}

// Storage cap: spice above MaxStorage is lost.
void testStorageCap() {
    EcoFixture fx;
    auto w = fx.world(24, 24);
    Side& s = w->sides()[0];
    w->addBuilding(0, kRefin, 10, 10, true, true);
    w->addSpice(0, 2600);
    for (int k = 0; k < 400; ++k) w->tick();
    CHECK(s.maxStorage == 2000 && s.spice == 2000 && s.spiceDrip == 0);
}

// Spend uses spice first, the change goes through the drips.
void testSpendAndDrip() {
    EcoFixture fx;
    auto w = fx.world(8, 8);
    Side& s = w->sides()[0];
    s.cash = 500;
    s.spice = 300;
    CHECK(!w->spend(0, 801));
    CHECK(w->spend(0, 200) && s.spiceDrip == -200 && s.cashDrip == 0);
    CHECK(w->spend(0, 400) && s.spiceDrip == -300 && s.cashDrip == -300);  // 100 spice left, 300 from cash
    for (int k = 0; k < 100; ++k) w->tick();
    CHECK(s.spice == 0 && s.cash == 200 && s.cashDrip == 0 && s.spiceDrip == 0);
}

// Power: producers scaled by health (+200 base when any), consumers (+200).
void testPowerBalance() {
    EcoFixture fx;
    auto w = fx.world(32, 32, kBuildRock);
    Side& s = w->sides()[0];
    w->addBuilding(0, kYard, 2, 2, true);    // produces 20
    w->addBuilding(0, kHeavy, 10, 2, true);  // consumes 100
    w->tick();
    CHECK(s.powerOutput == 220 && s.powerDemand == 300 && s.powerPercent == 73);
    const int wi = w->addBuilding(0, kWind, 6, 8, true);  // produces 200
    w->tick();
    CHECK(s.powerOutput == 420 && s.powerPercent == 140);
    s.buildings[wi].health = 500;  // half health: half output
    w->tick();
    CHECK(s.powerOutput == 320 && s.powerPercent == 106);
    // Low power halves the build speed (73 %: >> 1).
    s.buildings[wi].health = 0;
    w->tick();
    CHECK(s.powerPercent == 73 && w->buildingBuildSpeedPercent(0) == 50);
    s.buildings[wi].health = 1000;
    w->tick();
    CHECK(w->buildingBuildSpeedPercent(0) == 100);
    w->difficulty = 0;  // easy: the local player builds at 125 % and pays 75 %
    CHECK(w->buildingBuildSpeedPercent(0) == 125 && w->buildingCost(kWind, 0, 0) == 225 && w->buildingCost(kWind, 0, 1) == 300);
}

// A unit is paid for progressively and leaves the factory at exit point 1 towards exit point 2.
void testUnitProduction() {
    EcoFixture fx;
    auto w = fx.world(32, 32, kBuildRock);
    Side& s = w->sides()[0];
    s.cash = 5000;
    w->addBuilding(0, kYard, 2, 2, true);
    w->addBuilding(0, kWind, 6, 2, true);
    const int fi = w->addBuilding(0, kHeavy, 10, 10, true);
    const auto opts = w->buildableUnits(0);
    CHECK(std::any_of(opts.begin(), opts.end(), [](const BuildOption& o) { return o.type == kTank && o.cost == 700; }));
    CHECK(w->orderBuildUnit(0, kTank));
    CHECK(!w->orderBuildUnit(0, kMcv));  // not on the sidebar
    w->tick();
    QueueState q = w->unitQueueFor(0, kTank);
    CHECK(q.active && q.progress == 1000 && q.paid == 30);  // trunc(1000/23040 * 700)
    CHECK(s.buildings[fi].flags & building_flags::kAnimPermanent);
    // A second unit of the same factory group is refused while the first is being built.
    w->orderBuildUnit(0, kHarv);
    w->tick();
    CHECK(!w->unitQueueFor(0, kHarv).active);
    int paidMid = 0, ticks = 2;
    while (countUnits(s, kTank) == 0 && ticks < 100) {
        w->tick();
        ++ticks;
        if (ticks == 12) paidMid = w->unitQueueFor(0, kTank).paid;
    }
    CHECK(paidMid > 300 && paidMid < 400);  // ~ half way
    CHECK(countUnits(s, kTank) == 1 && ticks == 24);  // 0x5a00 / 1000 = 23.04 steps
    for (int i = s.firstUnit; i != -1; i = s.nextObject(i))
        if (s.units[i].type == kTank) {
            const Unit& u = s.units[i];
            CHECK(u.blockFromX == 11 && u.blockFromY == 12);  // factory (10,10) + exit point 1 (1,2)
            CHECK(u.targetX == 11 && u.targetY == 13 && u.state == UnitState::LeavingBuilding);
        }
    CHECK(!(s.buildings[fi].flags & building_flags::kAnimPermanent));
    CHECK(!w->unitQueueFor(0, kTank).active);
    for (int k = 0; k < 100; ++k) w->tick();
    CHECK(w->credits(0) == 5000 - 700);
    // Pause, resume and cancel with refund of the progress share.
    CHECK(w->orderBuildUnit(0, kTank));
    for (int k = 0; k < 6; ++k) w->tick();
    q = w->unitQueueFor(0, kTank);
    const int paid = q.paid, progress = q.progress;
    w->orderCancelUnit(0, kTank);  // first: on hold
    for (int k = 0; k < 5; ++k) w->tick();
    q = w->unitQueueFor(0, kTank);
    CHECK(q.onHold && q.paid == paid && q.progress == progress);
    w->orderCancelUnit(0, kTank);  // second: cancel, refund progress * cost / 0x5a00
    for (int k = 0; k < 100; ++k) w->tick();
    CHECK(!w->unitQueueFor(0, kTank).active);
    CHECK(w->credits(0) == 4300 - paid + progress * 700 / 0x5a00);
}

// Placement rules: free, buildable, within 2 tiles of an own building or own concrete, not row 0.
void testBuildingPlacement() {
    EcoFixture fx;
    auto w = fx.world(32, 32, kBuildRock);
    for (int y = 0; y < 32; ++y)
        for (int x = 10; x < 32; ++x) w->cell(x, y).flags = kDunes;  // sand: not buildable
    Side& s = w->sides()[0];
    s.cash = 5000;
    w->addBuilding(0, kYard, 5, 5, false, true);  // occupies 5..7 x 5..7 (no concrete)
    CHECK(w->canPlaceBuilding(0, kWind, 8, 5));     // adjacent rock
    CHECK(!w->canPlaceBuilding(0, kWind, 2, 15));   // too far from the base
    CHECK(!w->canPlaceBuilding(0, kWind, 9, 5));    // second column on sand
    CHECK(!w->canPlaceBuilding(0, kWind, 6, 6));    // on the yard
    CHECK(w->canPlaceBuilding(0, kWind, 3, 3));     // diagonal, 1 tile gap
    CHECK(!w->canPlaceBuilding(1, kWind, 8, 5));    // another side's base does not count
    std::array<bool, 16> tiles{};
    CHECK(!w->canPlaceBuilding(0, kWind, 9, 5, &tiles) && tiles[0] && !tiles[1] && tiles[4] && !tiles[5]);
    w->addUnit(0, kTank, 8, 6, 8, 6);  // a unit in the footprint blocks it
    CHECK(!w->canPlaceBuilding(0, kWind, 8, 5));
    CHECK(w->canPlaceBuilding(0, kWind, 3, 8));
    // Concrete only needs one placeable tile.
    CHECK(w->canPlaceBuilding(0, kSlab, 8, 3));
    // Produce and place a wind trap.
    CHECK(!w->orderPlaceBuilding(0, kWind, 3, 8));  // nothing ready
    {
        const auto opts = w->buildableBuildings(0);  // one vector: begin/end of two temporaries was UB
        CHECK(std::any_of(opts.begin(), opts.end(), [](const BuildOption& o) { return o.type == kWind; }));
    }
    CHECK(w->orderBuildBuilding(0, kWind));
    int t = 0;
    while (!w->buildingQueue(0).ready && t < 100) {
        w->tick();
        ++t;
    }
    CHECK(w->buildingQueue(0).ready && w->buildingQueue(0).paid == 300);
    CHECK(!w->orderPlaceBuilding(0, kWind, 2, 15));
    CHECK(w->orderPlaceBuilding(0, kWind, 3, 8));
    w->tick();
    int sd = 0, bi = 0;
    const Building* b = w->buildingAt(3, 8, sd, bi);
    CHECK(b && b->type == kWind && sd == 0 && !w->buildingQueue(0).active);
    CHECK(!(w->cell(3, 8).flags & 0x8000) && (w->cell(4, 9).flags & 0x10));
}

// Selling refunds half the cost when the building leaves the map; a refinery's refund
// excludes the free harvester.
void testSellAndRepair() {
    EcoFixture fx;
    auto w = fx.world(32, 32, kBuildRock);
    Side& s = w->sides()[0];
    w->addBuilding(0, kYard, 2, 2, true);
    const int wi = w->addBuilding(0, kWind, 8, 2, true);
    const int ri = w->addBuilding(0, kRefin, 8, 8, true, true);
    CHECK(w->orderSellBuilding(0, wi));
    for (int k = 0; k < 200; ++k) w->tick();
    CHECK(w->building(0, wi) == nullptr);
    CHECK(w->credits(0) == 150);
    w->orderSellBuilding(0, ri);
    for (int k = 0; k < 200; ++k) w->tick();
    CHECK(w->credits(0) == 150 + (1500 - 1200) / 2);
    // Repair: BuildingRepairValue hp per step for ((cost * value) >> 1) / maxHP credits, one
    // side per tick (gGameTicks & 7).
    s.cash = 1000;
    const int yi = s.firstBuilding;
    Building& y = s.buildings[yi];
    y.health = 900;
    CHECK(w->orderRepairBuilding(0, yi));
    for (int k = 0; k < 8 * 5; ++k) w->tick();
    CHECK(y.health == 900 + 5 * 12);
    for (int k = 0; k < 8 * 20; ++k) w->tick();
    CHECK(y.health == 1000 && !(y.flags & building_flags::kRepairing));
    for (int k = 0; k < 100; ++k) w->tick();
    CHECK(w->credits(0) == 1000 - 9 * 12);  // 9 steps of ((2000 * 12) >> 1) / 1000 = 12
}

// MCV deploy turns the MCV into the side's construction yard one tile up-left.
void testMcvDeploy() {
    EcoFixture fx;
    auto w = fx.world(24, 24, kBuildRock);
    Side& s = w->sides()[0];
    const int mi = w->addUnit(0, kMcv, 8, 8, 8, 8);
    CHECK(w->canDeployMcv(0, mi));
    CHECK(w->orderDeploy(0, mi));
    for (int k = 0; k < 100; ++k) w->tick();
    int sd = 0, bi = 0;
    const Building* b = w->buildingAt(7, 7, sd, bi);
    CHECK(b && b->type == kYard && b->tileX == 7 && b->tileY == 7);
    CHECK(countUnits(s, kMcv) == 0);
    CHECK(!w->buildableBuildings(0).empty());  // the yard enables the sidebar
    // Not on sand.
    for (int x = 0; x < 24; ++x) w->cell(x, 18).flags = kDunes;
    const int m2 = w->addUnit(0, kMcv, 4, 18, 4, 18);
    CHECK(!w->canDeployMcv(0, m2) && !w->orderDeploy(0, m2));
}

// A selected Devastator deploys anywhere (self-destruct, state 31); a thumper only on sand; plain units never.
void testDeployRules() {
    EcoFixture fx;
    fx.templates->bin.units[kTrike].specialBehavior = behaviour::kDevastator;
    auto w = fx.world(16, 16, kDunes);
    Side& s = w->sides()[0];
    const int di = w->addUnit(0, kTrike, 5, 5, 5, 5);
    const int ti = w->addUnit(0, kTank, 9, 9, 9, 9);
    CHECK(w->canDeploy(0, di));          // on sand, where an MCV couldn't deploy
    CHECK(!w->canDeploy(0, ti));         // no special behaviour
    CHECK(w->orderDeploy(0, di));
    w->tick();
    CHECK(s.units[di].state == UnitState::Selfdestruct);
}

// A new refinery's free harvester arrives by carryall from the nearest map edge.
void testFreeHarvesterDelivery() {
    EcoFixture fx;
    auto w = fx.world(32, 32, kBuildRock);
    Side& s = w->sides()[0];
    w->addBuilding(0, kRefin, 12, 12, true);
    CHECK(s.deliveries[0].active && s.deliveries[0].x == 14 && s.deliveries[0].y == 13);
    int t = 0, maxCarry = 0;
    while (countUnits(s, kHarv) == 0 && t < 2000) {
        w->tick();
        ++t;
        maxCarry = std::max(maxCarry, countUnits(s, kCarry));
    }
    CHECK(countUnits(s, kHarv) == 1 && maxCarry == 1);
    for (int k = 0; k < 2000 && countUnits(s, kCarry) != 0; ++k) w->tick();
    CHECK(countUnits(s, kCarry) == 0);  // the carryall left the map
    CHECK(!s.deliveries[0].active && s.carryallCount == 0 && s.carryallsAlive == 0);
    std::printf("economy: free harvester delivered after %d ticks\n", t);
}

// ---------------------------------------------------------------------------------------
// Starport and air strikes (re/notes/starport.md)

constexpr int kFrigateU = 7, kOrni = 8;  // unit types added by PortFixture
constexpr int kPort = 5, kGrpPort = 5;   // starport building

struct PortFixture : EcoFixture {
    PortFixture() {
        auto& b = templates->bin;
        b.groupIds[group_ids::kFrigate] = kFrigateU;
        b.groupIds[18] = kOrni;
        b.unitCount = 9;
        b.units[kTank].availableInStarport = 1;
        auto& trike = b.units[kTrike];
        trike.availableInStarport = 1;
        trike.ownerHouse = 0xff;
        trike.cost = 400;
        auto& fr = b.units[kFrigateU];
        fr.unitGroup = 7;
        fr.ownerHouse = 0xff;
        fr.hitPoints = 2000;
        fr.speed = 0x30000;
        fr.unitRotationSpeed = 1;
        fr.speedType = 3;
        fr.primaryWeapon = fr.secondaryWeapon = -1;
        fr.specialBehavior = behaviour::kFrigate;
        fr.prereq1BuildingGroup = fr.prereq2BuildingGroup = -1;
        auto& o = b.units[kOrni];
        o.unitGroup = 8;
        o.ownerHouse = 0xff;
        o.hitPoints = 200;
        o.speed = 0x60000;
        o.unitRotationSpeed = 1;
        o.speedType = 3;
        o.primaryWeapon = 3;
        o.secondaryWeapon = -1;
        o.specialBehavior = behaviour::kOrnithopter;
        o.prereq1BuildingGroup = o.prereq2BuildingGroup = -1;
        auto& wp = b.weapons[3];
        wp.projectileSpeed = 0x80000;
        wp.damage = 50;
        wp.range = 128;
        wp.hitExplosion = -1;
        wp.trailExplosion = -1;
        b.buildingCount = 6;
        auto& p = b.buildings[kPort];
        p.hitPoints = 1000;
        p.ownerHouse = 0xff;
        p.buildingGroup = kGrpPort;
        p.specialBehavior = building_behaviour::kStarport;
        p.tilesOccupiedAll = 0x777;
        p.tilesOccupiedSolid = 0x277;  // vanilla: bottom corners free (the units drive out there)
        p.artHeight = 96;
        p.artWidth = 96;
        p.primaryWeapon = p.secondaryWeapon = -1;
        p.prereq1BuildingGroup = p.prereq2BuildingGroup = -1;
        p.flags = building_flags::kSelectable;
    }
};

// Pick (pays now, max 5 per type / 6 per order, from the stock), unpick (refund), purchase:
// after starportFrigateDelay ticks a frigate flies from the nearest edge to the starport, drops
// the units one by one (every 8 ticks), and leaves; the starport takes orders again.
void testStarportPurchase() {
    PortFixture fx;
    auto w = fx.world(40, 40, kBuildRock);
    Side& s = w->sides()[0];
    s.cash = 5000;
    w->addBuilding(0, kPort, 20, 6, true);
    WorldTestAccess::initStarport(*w, 0);
    CHECK(s.starportTypes[0] == kTank && s.starportTypes[1] == kTrike && s.starportTypes[2] == -1);
    s.starportPrice[kTank] = 600;
    s.starportStock[kTank] = 3;
    s.starportPrice[kTrike] = 300;
    s.starportStock[kTrike] = 2;
    s.starportPriceTimer = s.starportStockTimer = 100000;  // freeze the upkeep for the test
    CHECK(w->starportOpen(0) && !w->starportBusy(0));
    CHECK(!w->orderStarportPurchase(0));  // nothing picked
    CHECK(w->orderStarportPick(0, kTank) && w->orderStarportPick(0, kTank) && w->orderStarportPick(0, kTrike));
    w->tick();
    CHECK(s.starportOrdered[kTank] == 2 && s.starportStock[kTank] == 1 && s.starportOrdered[kTrike] == 1);
    CHECK(s.starportSpent == 1500 && w->starportPickedTotal(0) == 3);
    CHECK(w->orderStarportUnpick(0, kTrike));
    for (int k = 0; k < 3; ++k) w->orderStarportPick(0, kTank);  // only one left in stock
    w->tick();
    CHECK(s.starportOrdered[kTrike] == 0 && s.starportStock[kTrike] == 2);
    CHECK(s.starportOrdered[kTank] == 3 && s.starportStock[kTank] == 0 && s.starportSpent == 1800);
    const auto opts = w->starportUnits(0);
    CHECK(opts.size() == 8 && opts[0].ordered == 3 && !opts[0].available && opts[1].available && opts[1].price == 300);
    for (int k = 0; k < 200; ++k) w->tick();  // cash drip pays out
    CHECK(w->credits(0) == 5000 - 1800);
    CHECK(w->orderStarportPurchase(0));
    CHECK(!w->orderStarportPurchase(0));  // already pending
    w->tick();
    const std::uint32_t bought = w->gameTicks();
    CHECK(w->starportBusy(0) && w->starportPickedTotal(0) == 0);
    CHECK(s.deliveries[0].active && s.deliveries[0].type == 2 && s.deliveries[0].units[0] == kTank &&
          s.deliveries[0].units[2] == kTank && s.deliveries[0].units[3] == 0xff);
    CHECK(!w->orderStarportPick(0, kTrike));  // no picks while the frigate is on its way
    int firstTank = -1, maxFrigates = 0, frigateAt = -1;
    std::uint32_t t = 0;
    for (; t < 6000; ++t) {
        w->tick();
        const int fr = countUnits(s, kFrigateU);
        if (fr && frigateAt < 0) frigateAt = static_cast<int>(w->gameTicks() - bought);
        maxFrigates = std::max(maxFrigates, fr);
        if (firstTank < 0 && countUnits(s, kTank)) firstTank = static_cast<int>(w->gameTicks() - bought);
        if (countUnits(s, kTank) == 3 && fr == 0) break;
    }
    CHECK(countUnits(s, kTank) == 3 && maxFrigates == 1 && countUnits(s, kFrigateU) == 0);
    CHECK(frigateAt >= static_cast<int>(w->vars().starportFrigateDelay) - 8 && firstTank > frigateAt);
    CHECK(!w->starportBusy(0) && s.starportSpent == 0 && !s.deliveries[0].active);
    CHECK(w->orderStarportPick(0, kTrike));  // open again
    w->tick();
    CHECK(s.starportOrdered[kTrike] == 1);
    // Empty list slots (-1) read SpiceDrip (+0x24264, the int before +0x24268) into the picked
    // total: 6 empty slots x drip 1 = 6 blocks the next pick (orders run before the drip pays out).
    s.spiceDrip = 1;
    CHECK(w->orderStarportPick(0, kTrike));
    w->tick();
    CHECK(s.starportOrdered[kTrike] == 1);
    s.spiceDrip = 0;
    CHECK(w->orderStarportPick(0, kTrike));
    w->tick();
    CHECK(s.starportOrdered[kTrike] == 2);
    std::printf("starport: frigate after %d ticks, first unit after %d, done after %u\n", frigateAt, firstTank,
                t);
}

// Starport upkeep: prices cost - var .. cost - 1 (var = cost * CostVariationPercent / 100),
// re-rolled every starportUpdateDelay/8 calls only while nothing is picked; stock grows to 5.
void testStarportPrices() {
    PortFixture fx;
    auto w = fx.world(24, 24, kBuildRock);
    Side& s = w->sides()[1];
    WorldTestAccess::initStarport(*w, 1);
    w->tick();
    for (int k = 0; k < 8; ++k) w->tick();  // every side's slot ran once
    const std::uint32_t tankCost = w->unitCost(kTank, 1), var = tankCost * 25 / 100;
    const int p0 = s.starportPrice[kTank];
    CHECK(p0 >= static_cast<int>(tankCost - var) && p0 < static_cast<int>(tankCost));
    CHECK(s.starportPrice[kTrike] >= 300 && s.starportPrice[kTrike] < 400);
    int changes = 0, prev = p0, maxStock = 0;
    for (int k = 0; k < 40000; ++k) {
        w->tick();
        if (s.starportPrice[kTank] != prev) ++changes;
        prev = s.starportPrice[kTank];
        CHECK(s.starportStock[kTank] <= 5);
        maxStock = std::max(maxStock, s.starportStock[kTank]);
    }
    CHECK(changes > 0 && maxStock > 0);
    // Frozen while something is picked.
    s.starportStock[kTrike] = std::max(1, s.starportStock[kTrike]);
    s.cash = 5000;
    w->orderStarportPick(1, kTrike);
    w->tick();
    const int frozen = s.starportPrice[kTank];
    for (int k = 0; k < 3000; ++k) w->tick();
    CHECK(s.starportPrice[kTank] == frozen);
    w->orderStarportCancel(1);
    w->tick();
    CHECK(s.starportOrdered[kTrike] == 0 && s.starportSpent == 0);
    std::printf("starport: tank price %d (cost %u), %d price changes in 40000 ticks, max stock %d\n", p0, tankCost,
                changes, maxStock);
}

// Losing the starport refunds the booked order and drops the frigate delivery.
void testStarportLost() {
    PortFixture fx;
    auto w = fx.world(32, 32, kBuildRock);
    Side& s = w->sides()[0];
    s.cash = 3000;
    const int pi = w->addBuilding(0, kPort, 10, 10, true);
    WorldTestAccess::initStarport(*w, 0);
    s.starportPrice[kTank] = 500;
    s.starportStock[kTank] = 2;
    s.starportPriceTimer = s.starportStockTimer = 100000;
    w->orderStarportPick(0, kTank);
    w->tick();
    w->orderStarportPurchase(0);
    w->tick();
    CHECK(s.deliveries[0].active && s.starportSpent == 500);
    w->destroyBuilding(s.buildings[pi], true);
    for (int k = 0; k < 400; ++k) w->tick();
    CHECK(!s.deliveries[0].active && !w->starportBusy(0) && s.starportSpent == 0 && !w->starportOpen(0));
    CHECK(w->credits(0) == 3000);
}

// Air strike: the ready queue slot is consumed, three ornithopters enter 4/7/10 tiles outside the
// edge nearest to the side's home view, fly to the target, fire their 12 shots and leave the map.
void testAirStrike() {
    PortFixture fx;
    auto w = fx.world(64, 64, kBuildRock);
    Side& s = w->sides()[1];
    w->addBuilding(1, kYard, 6, 30, true);  // home view: left edge is nearest
    s.unitQueue[3].type = kOrni;
    s.unitQueue[3].progress = kBuildDone;
    CHECK(!w->orderSpecialWeapon(1, behaviour::kDeathHand, 40, 30));
    CHECK(w->orderSpecialWeapon(1, behaviour::kOrnithopter, 40, 30));
    w->tick();
    CHECK(s.unitQueue[3].type == -1);
    std::vector<int> ornis;
    for (int i = s.firstUnit; i != -1; i = s.nextObject(i))
        if (s.units[i].type == kOrni) ornis.push_back(i);
    CHECK(ornis.size() == 3);
    for (int i : ornis) {
        const Unit& o = s.units[i];
        CHECK(o.posZ == 0x3c0000 && (o.flags & 0x10840) == 0x10840 && o.targetX == 40 && o.targetY == 30);
        CHECK(o.posX < 0);  // west of the map
        CHECK(o.state == UnitState::Moving);
    }
    int shots = 0, t = 0, closest = 1 << 30;
    std::array<int, 3> ammo{12, 12, 12};
    for (; t < 4000; ++t) {
        w->tick();
        for (int k = 0; k < 3; ++k)
            if (const Unit* o = w->unit(1, ornis[k]); o && o->type == kOrni && o->byte29 < ammo[k]) {
                shots += ammo[k] - o->byte29;  // FlyUnit fires every 8th tick within 4 tiles
                ammo[k] = o->byte29;
            }
        for (int i = s.firstUnit; i != -1; i = s.nextObject(i))
            if (s.units[i].type == kOrni) {
                const int dx = s.units[i].tileX() - 40, dy = s.units[i].tileY() - 30;
                closest = std::min(closest, dx * dx + dy * dy);
            }
        if (countUnits(s, kOrni) == 0) break;
    }
    CHECK(countUnits(s, kOrni) == 0);  // all left the map
    CHECK(shots == 36 && closest < 0x10);
    std::printf("air strike: ornithopters gone after %d ticks, %d shots\n", t, shots);
}

// AI BuyUnitsFromStarport: every 128 ticks an AI with a starport picks the units it wants (armed,
// price <= cost * factor) and sends the order.
void testAiStarport() {
    PortFixture fx;
    auto w = fx.world(40, 40, kBuildRock);
    Side& s = w->sides()[1];
    s.cash = 20000;
    w->addBuilding(1, kPort, 20, 20, true);
    WorldTestAccess::initStarport(*w, 1);
    AiState& a = w->ai(1);
    a.b(1) = 1;  // enabled
    a.b(4) = 1;  // BuyUnits
    a.set<float>(0xf68, 0.8f);
    a.set<std::uint32_t>(0x1074, 0);
    s.starportPrice[kTank] = 500;  // <= 700 * 0.8 (fewer than 4 unit icons: factor 1.2)
    s.starportStock[kTank] = 2;
    s.starportPrice[kTrike] = 300;
    s.starportStock[kTrike] = 0;
    s.starportPriceTimer = s.starportStockTimer = 100000;
    while ((w->gameTicks() & 0x7f) != 0x7f) w->tick();
    w->tick();  // AI buys (orders queued), ProcessOrder runs them in the same tick
    CHECK(w->starportBusy(1));
    w->tick();
    CHECK(s.deliveries[0].active && s.deliveries[0].type == 2 && s.deliveries[0].units[0] == kTank &&
          s.deliveries[0].units[1] == kTank && s.deliveries[0].units[2] == 0xff);
    for (int k = 0; k < 6000 && w->starportBusy(1); ++k) w->tick();
    CHECK(!w->starportBusy(1) && countUnits(s, kTank) == 2);
}

// Data-backed: on A1V1 the player builds a wind trap and a refinery; the free harvester is
// delivered and spice turns into credits.
void testRealEconomy() {
#ifdef D2K_GAME_DATA_DIR
    const std::string data = D2K_GAME_DATA_DIR;
    const std::string mapPath = formats::findFileNoCase(data + "/Missions", "A1V1.MAP");
    const std::string misPath = formats::findFileNoCase(data + "/maps", "_A1V1.MIS");
    const std::string varsPath = formats::findFileNoCase(data + "/bin", "VARS.BIN");
    if (mapPath.empty() || misPath.empty() || varsPath.empty()) {
        std::printf("(game data not found: skipping economy test)\n");
        return;
    }
    const auto map = formats::loadMap(mapPath);
    const auto mis = formats::loadMis(misPath);
    const auto tileAtr = formats::loadTileAtr(formats::findFileNoCase(data + "/bin", mis.tileAttributes + ".BIN"));
    const auto tileData = formats::loadTileData(formats::findFileNoCase(data + "/bin", "TILEDATA.BIN"));
    auto templates = std::make_unique<formats::Templates>(
        formats::loadTemplates(formats::findFileNoCase(data + "/bin", "Templates.bin")));
    const auto speed = formats::loadSpeed(formats::findFileNoCase(data + "/bin", "SPEED.BIN"));
    const auto armour = formats::loadArmour(formats::findFileNoCase(data + "/bin", "ARMOUR.BIN"));
    const auto vars = formats::loadVars(varsPath);
    CHECK(vars.harvestBlobValue == 100 && vars.harvestUnloadDelay == 60 && vars.harvesterDriveDistance == 64);
    World w(map, mis, tileAtr, tileData, {templates.get(), &speed, &armour, nullptr, nullptr, &vars}, 0);
    Side& me = w.sides()[0];
    CHECK(w.credits(0) == 2300);
    auto buildBeh = [&](int beh) {
        int type = -1;
        for (const auto& o : w.buildableBuildings(0)) {
            const auto& bt = templates->bin.buildings[o.type];
            if (bt.specialBehavior == beh && (beh != 0 || bt.powerConsumption < 0)) type = o.type;
        }
        if (type < 0 || !w.orderBuildBuilding(0, type)) return false;
        for (int k = 0; k < 5000 && !w.buildingQueue(0).ready; ++k) w.tick();
        const Building& yard = me.buildings[me.firstBuilding];
        for (int r = 1; r < 10; ++r)
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                    if (w.orderPlaceBuilding(0, type, yard.tileX + dx, yard.tileY + dy)) {
                        w.tick();
                        return true;
                    }
        return false;
    };
    CHECK(buildBeh(0));
    CHECK(buildBeh(building_behaviour::kRefinery));
    CHECK(w.credits(0) == 2300 - 225 - 1500 || me.cashDrip != 0);
    int low = w.credits(0), trips = 0;
    UnitState prevState = UnitState::Dummy;
    for (int k = 0; k < 6000; ++k) {
        w.tick();
        low = std::min(low, w.credits(0));
        for (int i = me.firstUnit; i != -1; i = me.nextObject(i))
            if (templates->bin.units[me.units[i].type].specialBehavior == behaviour::kHarvester) {
                if (me.units[i].state == UnitState::UnloadingSpice && prevState != UnitState::UnloadingSpice) ++trips;
                prevState = me.units[i].state;
            }
    }
    CHECK(trips >= 2 && me.spiceHarvested >= 1400 && w.credits(0) >= low + 1400);
    std::printf("economy: A1V1 refinery: %d unload trips, %d spice harvested, credits %d -> %d\n", trips,
                me.spiceHarvested, low, w.credits(0));
#endif
}

}  // namespace


// ---------------------------------------------------------------------------------------
// Shroud (re/notes/shroud.md)

void testShroudReveal() {
    Fixture f;
    auto w = f.world(20, 20);
    // Fresh map: everything fully shrouded (0x11 = edge id 1, checker variant 1 as loaded).
    CHECK((w->cell(10, 10).shroud & 0xF) == 1);
    w->revealCircle(10, 10, 3);
    CHECK(w->cell(10, 10).shroud == 0);
    // Radius-3 mask: top row reveals x-1..x+1, the corners stay shrouded.
    CHECK(w->cell(9, 7).shroud == 0 && w->cell(10, 7).shroud == 0 && w->cell(11, 7).shroud == 0);
    CHECK((w->cell(12, 7).shroud & 0xF) != 0 && (w->cell(7, 7).shroud & 0xF) != 0);
    CHECK(w->cell(7, 10).shroud == 0 && w->cell(13, 10).shroud == 0 && (w->cell(6, 10).shroud & 0xF) != 0);
    // (10,6): only S, SW, SE clear -> mask 0x1f -> edge 3; variant ((6&1)*2 + (10&1)) = 0.
    CHECK(w->cell(10, 6).shroud == 0x03);
    // (7,7): only SE clear -> mask 0x7f -> edge 2; variant (7&1)*2 + (7&1) = 3.
    CHECK(w->cell(7, 7).shroud == 0x32);
    // Far away: still fully shrouded, its variant rewritten only inside the update box.
    CHECK(w->cell(0, 0).shroud == 0x11);
    // A shrouded cell whose 8 neighbours are all clear is cleared by the edge table (entry 0)
    // but keeps its checker variant in the high nibble (quirk: the byte is not 0).
    w->revealMap();
    w->cell(5, 15).shroud = 0x11;
    WorldTestAccess::updateShroud(*w, 4, 14, 7, 17);
    CHECK(w->cell(5, 15).shroud == 0x30);
    // Map borders count as shrouded: a shrouded corner next to clear cells.
    w->cell(0, 0).shroud = 0x11;
    WorldTestAccess::updateShroud(*w, 0, 0, 2, 2);
    CHECK(w->cell(0, 0).shroud == 0x0D);  // mask 0x2f (W/NW/SW/N/NE off-map) -> edge 13

#ifdef D2K_GAME_DATA_DIR
    // The built-in masks equal bin/CIRCLES.BIN.
    const std::string circ = formats::findFileNoCase(std::string(D2K_GAME_DATA_DIR) + "/bin", "CIRCLES.BIN");
    if (!circ.empty()) {
        const auto bytes = formats::readFile(circ);
        CHECK(bytes.size() == 680);
        Rules r = f.rules();
        r.circles = &bytes;
        for (int rad = 0; rad <= 7; ++rad) {
            World a(20, 20, f.rules(), 0), b(20, 20, r, 0);
            a.revealCircle(9, 9, rad);
            b.revealCircle(9, 9, rad);
            bool same = true;
            for (int y = 0; y < 20; ++y)
                for (int x = 0; x < 20; ++x) same = same && a.cell(x, y).shroud == b.cell(x, y).shroud;
            CHECK(same);
        }
    }
#endif
}

void testShroudUnitSight() {
    Fixture f;
    f.templates->bin.units[kTank].sightRadius = 2;
    auto w = f.world(24, 24);
    const int me = w->addUnit(0, kTank, 5, 5, 5, 5);
    const int enemy = w->addUnit(1, kTank, 18, 18, 18, 18);
    CHECK(me >= 0 && enemy >= 0);
    for (int t = 0; t < 8; ++t) w->tick();  // FUN_0044cff0 visits every unit once per 8 ticks
    CHECK(w->cell(5, 5).shroud == 0 && w->cell(7, 5).shroud == 0 && w->cell(5, 3).shroud == 0);
    CHECK((w->cell(8, 5).shroud & 0xF) != 0);
    CHECK((w->cell(18, 18).shroud & 0xF) == 1);  // enemy units reveal nothing
    // An allied side (diplomacy towards the player = 0) shares its sight.
    w->setDiplomacy(1, 0, 0);
    for (int t = 0; t < 8; ++t) w->tick();
    CHECK(w->cell(18, 18).shroud == 0);
}

// ---------------------------------------------------------------------------------------
// Mission events (re/notes/mission-events.md)

std::array<std::uint8_t, formats::kMisConditionSize> rawCondition(int type, int side, std::uint32_t val2, int arg2) {
    std::array<std::uint8_t, formats::kMisConditionSize> c{};
    std::memcpy(&c[0], &val2, 4);
    c[24] = static_cast<std::uint8_t>(side);
    c[25] = static_cast<std::uint8_t>(type);
    c[27] = static_cast<std::uint8_t>(arg2);
    return c;
}
std::array<std::uint8_t, formats::kMisEventSize> rawEvent(int type, int side, std::initializer_list<int> conds) {
    std::array<std::uint8_t, formats::kMisEventSize> e{};
    e[13] = static_cast<std::uint8_t>(type);
    e[15] = static_cast<std::uint8_t>(side);
    int n = 0;
    for (int c : conds) e[19 + n++] = static_cast<std::uint8_t>(c);
    e[12] = static_cast<std::uint8_t>(n);
    return e;
}

void testMissionTimerMessage() {
    Fixture f;
    auto w = f.world(16, 16);
    formats::MisFile mis;
    mis.timeLimit = -1;
    mis.conditions.push_back(rawCondition(3, 0, 5, 2));  // Timer: counter == 5
    auto ev = rawEvent(17, 0, {0});                      // Show Message 4711
    const std::uint32_t id = 4711;
    std::memcpy(&ev[47 + 21], &id, 4);
    mis.events.push_back(ev);
    WorldTestAccess::loadEvents(*w, mis);
    for (int t = 0; t < 4; ++t) w->tick();
    CHECK(w->messages().empty());
    w->tick();
    CHECK(w->messages().size() == 1 && w->messages()[0].textId == 4711 && w->messages()[0].tick == 5);
    bool chat = false;
    for (const auto& s : w->soundEvents()) chat = chat || (s.name && std::strcmp(s.name, "S_CHATMSG") == 0);
    CHECK(chat);
    w->tick();
    CHECK(w->messages().size() == 1);
    CHECK(w->gameEndState() == GameEnd::None);
    // Set Timer / countdown.
    formats::MisFile m2;
    m2.timeLimit = 100;
    WorldTestAccess::loadEvents(*w, m2);
    w->tick();
    CHECK(w->missionTimer() == 99);
}

void testMissionWinWhenEnemyDestroyed() {
    Fixture f;
    auto w = f.world(16, 16);
    w->addUnit(0, kTank, 2, 2, 2, 2);
    const int e = w->addUnit(1, kInfantry, 12, 12, 12, 12);
    WorldTestAccess::evaluateAllExists(*w);
    CHECK(w->unitsExist(1) && !w->buildingsExist(1));
    formats::MisFile mis;
    mis.timeLimit = -1;
    mis.conditions.push_back(rawCondition(5, 1, 0, 0));  // Base Destroyed (side 1)
    mis.conditions.push_back(rawCondition(6, 1, 0, 0));  // Units Destroyed (side 1)
    mis.conditions.push_back(rawCondition(6, 0, 0, 0));  // Units Destroyed (side 0)
    mis.events.push_back(rawEvent(10, 0, {0, 1}));       // Mission Win
    mis.events.push_back(rawEvent(11, 0, {2}));          // Mission Fail
    WorldTestAccess::loadEvents(*w, mis);
    for (int t = 0; t < 32; ++t) w->tick();
    CHECK(w->gameEndState() == GameEnd::None);
    w->destroyUnit(w->sides()[1].units[e]);
    int ticks = 0;
    while (w->gameEndState() == GameEnd::None && ticks < 200) {
        w->tick();
        ++ticks;
    }
    CHECK(w->gameEndState() == GameEnd::Won);
    std::printf("mission: won %d ticks after the last enemy died\n", ticks);
    bool won = false;
    for (const auto& s : w->soundEvents()) won = won || (s.house && s.name && std::strcmp(s.name, "MISSIONWON") == 0 && s.queue);
    CHECK(won);
}

void testSkirmishDefaultRules() {
    Fixture f;
    auto w = f.world(16, 16);
    w->skirmish = true;
    w->addUnit(0, kTank, 2, 2, 2, 2);
    const int e = w->addUnit(1, kTank, 12, 12, 12, 12);
    WorldTestAccess::evaluateAllExists(*w);
    for (int t = 0; t < 20; ++t) w->tick();
    CHECK(w->gameEndState() == GameEnd::None);
    w->destroyUnit(w->sides()[1].units[e]);
    for (int t = 0; t < 40 && w->gameEndState() == GameEnd::None; ++t) w->tick();
    CHECK(w->gameEndState() == GameEnd::Won && w->gameOver());
}

int phantomVehicleCells(const World& w);

// Skirmish start (Setup_SpawnMapObjects skirmish pass) on a real 4-player map.
void testSkirmishSpawn() {
#ifdef D2K_GAME_DATA_DIR
    const std::string data = D2K_GAME_DATA_DIR, bin = data + "/bin";
    const std::string mapPath = formats::findFileNoCase(data + "/maps", "4PLAY1.MAP");
    const std::string misPath = formats::findFileNoCase(data + "/maps", "_SPAWN.MIS");
    const std::string tpPath = formats::findFileNoCase(bin, "TECHPOS.BIN");
    if (mapPath.empty() || misPath.empty() || tpPath.empty()) return;
    auto templates = std::make_unique<formats::Templates>(formats::loadTemplates(formats::findFileNoCase(bin, "Templates.bin")));
    const auto speed = formats::loadSpeed(formats::findFileNoCase(bin, "SPEED.BIN"));
    const auto armour = formats::loadArmour(formats::findFileNoCase(bin, "ARMOUR.BIN"));
    const auto tileData = formats::loadTileData(formats::findFileNoCase(bin, "TILEDATA.BIN"));
    const auto techPos = formats::loadTechPos(tpPath);
    CHECK(techPos.entries[1][2].unitGroup[2] == 6 && techPos.entries[1][2].dx == 1 && techPos.entries[1][2].dy == -2);
    const auto map = formats::loadMap(mapPath);
    const auto mis = formats::loadMis(misPath);
    const auto tileAtr = formats::loadTileAtr(formats::findFileNoCase(bin, mis.tileAttributes + ".BIN"));
    SkirmishSetup setup;
    setup.techPos = &techPos;
    setup.aiPlayers = 3;
    setup.techLevel = 3;
    setup.unitCount = 10;
    World w(map, mis, tileAtr, tileData, {templates.get(), &speed, &armour}, setup);
    CHECK(w.spawnLocations().size() == 8);
    CHECK(w.skirmish && !w.ai(0).enabled());
    int mcvs = 0, units = 0;
    for (int s = 0; s < 4; ++s) {
        CHECK(s == 0 || w.ai(s).enabled());
        const Side& S = w.sides()[s];
        for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
            ++units;
            if (w.unitTemplate(S.units[i]).specialBehavior == behaviour::kMcv) ++mcvs;
        }
    }
    CHECK(mcvs == 4);
    CHECK(units == 4 * 11);  // MCV + 10 TECHPOS units each (tech level 3 uses all 10 slots)
    CHECK(w.sides()[0].cash == 5000 && w.sides()[1].cash == 10500);
    std::printf("skirmish: 4PLAY1 with 3 AIs: %d units, houses %d %d %d %d\n", units, w.sides()[0].house,
                w.sides()[1].house, w.sides()[2].house, w.sides()[3].house);
    // Let the AIs play (the human side idles): they deploy their MCVs and build bases.
    for (int k = 0; k < 20000; ++k) w.tick();
    CHECK(phantomVehicleCells(w) == 0);
    for (int s = 1; s < 4; ++s) {
        const Side& S = w.sides()[s];
        int b = 0, u = 0;
        for (int i = S.firstBuilding; i != -1; i = S.nextObject(i)) ++b;
        for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) ++u;
        std::printf("  skirmish AI side %d after 20000 ticks: built %d buildings, %d units; now %d buildings, %d units\n", s,
                    S.buildingsBuilt, S.unitsBuilt, b, u);
    }
    std::printf("  skirmish end state %d, game over %d\n", static_cast<int>(w.gameEndState()), w.gameOver());
#endif
}

// Data-backed soak over the campaign missions: the AIs play 15000 ticks; counts starport
// purchases, frigate flights and ornithopter strikes per mission, and replays the busiest
// mission to check the game stays deterministic.
void testStarportSoak() {
#ifdef D2K_GAME_DATA_DIR
    const std::string data = D2K_GAME_DATA_DIR, bin = data + "/bin";
    if (formats::findFileNoCase(bin, "ARMOUR.BIN").empty()) return;
    auto templates = std::make_unique<formats::Templates>(formats::loadTemplates(formats::findFileNoCase(bin, "Templates.bin")));
    const auto speed = formats::loadSpeed(formats::findFileNoCase(bin, "SPEED.BIN"));
    const auto armour = formats::loadArmour(formats::findFileNoCase(bin, "ARMOUR.BIN"));
    const auto builexp = formats::readFile(formats::findFileNoCase(bin, "BUILEXP.BIN"));
    const auto circles = formats::readFile(formats::findFileNoCase(bin, "CIRCLES.BIN"));
    const auto vars = formats::loadVars(formats::findFileNoCase(bin, "VARS.BIN"));
    const auto tileData = formats::loadTileData(formats::findFileNoCase(bin, "TILEDATA.BIN"));
    const int frigate = static_cast<std::uint8_t>(templates->bin.groupIds[group_ids::kFrigate]);
    const int orni = static_cast<std::uint8_t>(templates->bin.groupIds[18]);
    struct Result { int purchases = 0, frigates = 0, strikes = 0, delivered = 0; std::uint32_t hash = 0; };
    auto run = [&](const std::string& name, int player) {
        Result r;
        const auto map = formats::loadMap(formats::findFileNoCase(data + "/Missions", name + ".MAP"));
        const auto mis = formats::loadMis(formats::findFileNoCase(data + "/maps", "_" + name + ".MIS"));
        const auto tileAtr = formats::loadTileAtr(formats::findFileNoCase(bin, mis.tileAttributes + ".BIN"));
        World w(map, mis, tileAtr, tileData, {templates.get(), &speed, &armour, nullptr, &builexp, &vars, &circles}, player);
        std::array<bool, kMaxSides> busy{};
        std::array<int, kMaxSides> ornis{};
        bool sawFrigate = false;
        for (int k = 0; k < 15000; ++k) {
            w.tick();
            bool frigateNow = false;
            for (int s = 0; s < kMaxSides; ++s) {
                const Side& S = w.sides()[s];
                if (S.starportFrigateBusy && !busy[s]) ++r.purchases;
                busy[s] = S.starportFrigateBusy;
                int o = 0;
                for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
                    const Unit& u = S.units[i];
                    if (u.type == frigate) frigateNow = true;
                    if (u.type == orni && (u.flags & 0x10000)) ++o;
                    if (k % 100 == 0 && (u.flags & 0x40000) && S.deliveries[u.byte2E % 10].type == 2) ++r.delivered;
                }
                if (o > ornis[s]) ++r.strikes;
                ornis[s] = o;
            }
            if (frigateNow && !sawFrigate) ++r.frigates;
            sawFrigate = frigateNow;
        }
        CHECK(phantomVehicleCells(w) == 0);
        r.hash = static_cast<std::uint32_t>(w.rng().next());
        for (const auto& S : w.sides())
            for (int i = S.firstUnit; i != -1; i = S.nextObject(i))
                r.hash = r.hash * 31u + static_cast<std::uint32_t>(S.units[i].posX ^ S.units[i].posY ^ S.units[i].type);
        return r;
    };
    int total[3] = {0, 0, 0}, best = -1;
    std::string bestName;
    int bestPlayer = 0;
    Result bestResult;
    for (const char house : {'A', 'H', 'O'})
        for (int n = 1; n <= 9; ++n) {
            const std::string name = std::string(1, house) + std::to_string(n) + "V1";
            if (formats::findFileNoCase(data + "/Missions", name + ".MAP").empty() ||
                formats::findFileNoCase(data + "/maps", "_" + name + ".MIS").empty())
                continue;
            const int player = house == 'A' ? 0 : house == 'H' ? 1 : 2;
            const Result r = run(name, player);
            total[0] += r.purchases;
            total[1] += r.frigates;
            total[2] += r.strikes;
            if (r.purchases || r.strikes)
                std::printf("starport soak %s: %d purchases, %d frigate flights, %d air strikes\n", name.c_str(),
                            r.purchases, r.frigates, r.strikes);
            if (r.purchases + r.strikes > best) {
                best = r.purchases + r.strikes;
                bestName = name;
                bestPlayer = player;
                bestResult = r;
            }
        }
    if (best > 0) {
        const Result again = run(bestName, bestPlayer);
        CHECK(again.hash == bestResult.hash && again.purchases == bestResult.purchases && again.strikes == bestResult.strikes);
        std::printf("starport soak: replay of %s deterministic %s\n", bestName.c_str(), again.hash == bestResult.hash ? "yes" : "NO");
    }
    std::printf("starport soak: %d purchases, %d frigate flights, %d air strikes over the campaign (15000 ticks each)\n",
                total[0], total[1], total[2]);
#endif
}

// Vehicle occupancy bit (0x8) consistency: every marked cell is BlockFrom/BlockTo of a live
// ground vehicle. Returns the number of phantom cells.
int phantomVehicleCells(const World& w) {
    std::vector<std::uint8_t> claimed(static_cast<std::size_t>(w.width()) * w.height(), 0);
    for (const auto& S : w.sides())
        for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
            const Unit& u = S.units[i];
            const auto& t = w.unitTemplate(u);
            if (t.isInfantry) continue;
            for (const auto& p : {std::array<int, 2>{u.blockFromX, u.blockFromY}, std::array<int, 2>{u.blockToX, u.blockToY}})
                if (w.inBounds(p[0], p[1])) claimed[static_cast<std::size_t>(p[1]) * w.width() + p[0]] = 1;
        }
    int phantom = 0;
    for (int y = 0; y < w.height(); ++y)
        for (int x = 0; x < w.width(); ++x)
            if ((w.cell(x, y).flags & cell_flags::kVehicle) && !claimed[static_cast<std::size_t>(y) * w.width() + x]) ++phantom;
    return phantom;
}

// Data-backed AI soak: campaign missions with computer players, N thousand ticks; logs what
// the AIs build and how often they attack the player; checks object lists and occupancy.
void testAiSoak() {
#ifdef D2K_GAME_DATA_DIR
    const std::string data = D2K_GAME_DATA_DIR, bin = data + "/bin";
    if (formats::findFileNoCase(bin, "ARMOUR.BIN").empty()) return;
    auto templates = std::make_unique<formats::Templates>(formats::loadTemplates(formats::findFileNoCase(bin, "Templates.bin")));
    const auto speed = formats::loadSpeed(formats::findFileNoCase(bin, "SPEED.BIN"));
    const auto armour = formats::loadArmour(formats::findFileNoCase(bin, "ARMOUR.BIN"));
    const auto builexp = formats::readFile(formats::findFileNoCase(bin, "BUILEXP.BIN"));
    const auto circles = formats::readFile(formats::findFileNoCase(bin, "CIRCLES.BIN"));
    const auto vars = formats::loadVars(formats::findFileNoCase(bin, "VARS.BIN"));
    const auto tileData = formats::loadTileData(formats::findFileNoCase(bin, "TILEDATA.BIN"));
    struct Case { const char* name; int player; int ticks; };
    for (const Case c : {Case{"A1V1", 0, 20000}, Case{"A3V1", 0, 25000}, Case{"A4V1", 0, 20000}, Case{"H5V1", 1, 15000}, Case{"O6V1", 2, 15000}}) {
        const std::string mapPath = formats::findFileNoCase(data + "/Missions", std::string(c.name) + ".MAP");
        const std::string misPath = formats::findFileNoCase(data + "/maps", std::string("_") + c.name + ".MIS");
        if (mapPath.empty() || misPath.empty()) continue;
        const auto map = formats::loadMap(mapPath);
        const auto mis = formats::loadMis(misPath);
        const auto tileAtr = formats::loadTileAtr(formats::findFileNoCase(bin, mis.tileAttributes + ".BIN"));
        Rules rules{templates.get(), &speed, &armour, nullptr, &builexp, &vars, &circles};
        World w(map, mis, tileAtr, tileData, rules, c.player);
        std::array<int, kMaxSides> b0{}, u0{};
        for (int s = 0; s < kMaxSides; ++s) {
            b0[s] = w.sides()[s].buildingsBuilt;
            u0[s] = w.sides()[s].unitsBuilt;
        }
        int attackTicks = 0, maxAttackers = 0, maxPhantom = 0, messages = 0;
        bool listsOk = true;
        GameEnd end = GameEnd::None;
        int endTick = -1;
        for (int k = 0; k < c.ticks; ++k) {
            w.tick();
            messages += static_cast<int>(w.messages().size());
            w.clearMessages();
            if (end == GameEnd::None && w.gameEndState() != GameEnd::None) {
                end = w.gameEndState();
                endTick = k + 1;
            }
            if (k % 50 != 0) continue;
            int attackers = 0;
            for (int s = 0; s < kMaxSides; ++s) {
                const Side& S = w.sides()[s];
                for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
                    const Unit& u = S.units[i];
                    listsOk = listsOk && u.alive && S.links[i].kind == SlotKind::Unit;
                    if (w.ai(s).enabled() && w.diplomacy(s, c.player) == 1 && u.enemySide == c.player &&
                        (u.state == UnitState::AttackingUnit || u.state == UnitState::AttackingBuilding))
                        ++attackers;
                }
                for (int i = S.firstBuilding; i != -1; i = S.nextObject(i))
                    listsOk = listsOk && S.buildings[i].alive && S.links[i].kind == SlotKind::Building;
            }
            if (attackers) ++attackTicks;
            maxAttackers = std::max(maxAttackers, attackers);
            maxPhantom = std::max(maxPhantom, phantomVehicleCells(w));
        }
        CHECK(listsOk);
        CHECK(maxPhantom == 0);
        std::printf("ai soak %s (%d ticks, player side %d): end %s at %d, %d messages, attacks seen in %d/%d samples (max %d attackers), phantom vehicle cells %d\n",
                    c.name, c.ticks, c.player, end == GameEnd::Won ? "won" : end == GameEnd::Lost ? "lost" : "none", endTick,
                    messages, attackTicks, c.ticks / 50, maxAttackers, maxPhantom);
        for (int s = 0; s < kMaxSides; ++s) {
            if (!w.ai(s).enabled()) continue;
            const Side& S = w.sides()[s];
            int bNow = 0, uNow = 0;
            for (int i = S.firstBuilding; i != -1; i = S.nextObject(i)) ++bNow;
            for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) ++uNow;
            std::printf("  AI side %d: built %d buildings, %d units; now %d buildings, %d units, credits %d\n", s,
                        S.buildingsBuilt - b0[s], S.unitsBuilt - u0[s], bNow, uNow, w.credits(s));
        }
    }
#endif
}


// Data-backed soak over many campaign missions for the audit-1 subsystems: occupancy (no phantom
// vehicle cells), worm cells, crate bits vs. records, object lists; prints how often worms,
// blooms, stealth and repair/capture states occurred.
void testAuditSoak() {
#ifdef D2K_GAME_DATA_DIR
    const std::string data = D2K_GAME_DATA_DIR, bin = data + "/bin";
    if (formats::findFileNoCase(bin, "ARMOUR.BIN").empty()) return;
    auto templates = std::make_unique<formats::Templates>(formats::loadTemplates(formats::findFileNoCase(bin, "Templates.bin")));
    const auto speed = formats::loadSpeed(formats::findFileNoCase(bin, "SPEED.BIN"));
    const auto armour = formats::loadArmour(formats::findFileNoCase(bin, "ARMOUR.BIN"));
    const auto builexp = formats::readFile(formats::findFileNoCase(bin, "BUILEXP.BIN"));
    const auto circles = formats::readFile(formats::findFileNoCase(bin, "CIRCLES.BIN"));
    const auto vars = formats::loadVars(formats::findFileNoCase(bin, "VARS.BIN"));
    const auto tileData = formats::loadTileData(formats::findFileNoCase(bin, "TILEDATA.BIN"));
    int maps = 0;
    for (const char house : {'A', 'H', 'O'})
        for (int n = 1; n <= 9; ++n) {
            const std::string name = std::string(1, house) + std::to_string(n) + "V1";
            const std::string mapPath = formats::findFileNoCase(data + "/Missions", name + ".MAP");
            const std::string misPath = formats::findFileNoCase(data + "/maps", "_" + name + ".MIS");
            if (mapPath.empty() || misPath.empty()) continue;
            const auto map = formats::loadMap(mapPath);
            const auto mis = formats::loadMis(misPath);
            const auto tileAtr = formats::loadTileAtr(formats::findFileNoCase(bin, mis.tileAttributes + ".BIN"));
            const int player = house == 'A' ? 0 : house == 'H' ? 1 : 2;
            Rules rules{templates.get(), &speed, &armour, nullptr, &builexp, &vars, &circles};
            World w(map, mis, tileAtr, tileData, rules, player);
            ++maps;
            int worms = 0, wormWander = 0, wormEat = 0, cloaked = 0, repair = 0, infiltr = 0, deviated = 0;
            int maxPhantom = 0, crateMismatch = 0, wormMismatch = 0, bloomsStart = 0, bloomsEnd = 0, mounds = 0;
            bool listsOk = true;
            for (const auto& c : w.crates()) bloomsStart += c.active;
            std::array<std::uint8_t, kMaxCrates> lastType{};
            for (int i = 0; i < kMaxCrates; ++i) lastType[i] = w.crates()[i].active ? w.crates()[i].type : 0;
            const int ticks = 12000;
            for (int k = 0; k < ticks; ++k) {
                w.tick();
                w.clearMessages();
                for (int i = 0; i < kMaxCrates; ++i) {
                    const Crate& c = w.crates()[i];
                    const std::uint8_t t = c.active ? c.type : 0;
                    if (lastType[i] >= 8 && t == 7) ++mounds;  // burst + regrow
                    lastType[i] = t;
                }
                if (k % 25 != 0) continue;
                std::vector<std::uint8_t> wormCells(static_cast<std::size_t>(w.width()) * w.height(), 0);
                for (int sd = 0; sd < kMaxSides; ++sd) {
                    const Side& S = w.sides()[sd];
                    for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
                        const Unit& u = S.units[i];
                        listsOk = listsOk && u.alive && S.links[i].kind == SlotKind::Unit;
                        const int beh = w.unitTemplate(u).specialBehavior;
                        if (beh == behaviour::kSandworm) {
                            ++worms;
                            wormWander += u.state == UnitState::Wandering;
                            wormEat += u.state == UnitState::Eating;
                            for (const auto& p : {std::array<int, 2>{u.blockFromX, u.blockFromY}, std::array<int, 2>{u.blockToX, u.blockToY}})
                                if (w.inBounds(p[0], p[1])) wormCells[static_cast<std::size_t>(p[1]) * w.width() + p[0]] = 1;
                        } else if (u.flags & unit_flags::kCloaked) {
                            ++cloaked;
                        }
                        repair += u.state == UnitState::MovingToRepairPad || u.state == UnitState::Repairing ||
                                  u.state == UnitState::LeavingRepairPad;
                        infiltr += u.state == UnitState::Infiltrating;
                        deviated += (u.flags & unit_flags::kDeviated) != 0;
                    }
                    for (int i = S.firstBuilding; i != -1; i = S.nextObject(i))
                        listsOk = listsOk && S.buildings[i].alive && S.links[i].kind == SlotKind::Building;
                }
                for (int y = 0; y < w.height(); ++y)
                    for (int x = 0; x < w.width(); ++x) {
                        const std::uint32_t f = w.cell(x, y).flags;
                        if ((f & cell_flags::kWorm) && !wormCells[static_cast<std::size_t>(y) * w.width() + x]) ++wormMismatch;
                        if (f & cell_flags::kCrate) {
                            bool rec = false;
                            for (const auto& c : w.crates()) rec = rec || (c.active && c.x == x && c.y == y);
                            crateMismatch += !rec;
                        }
                    }
                maxPhantom = std::max(maxPhantom, phantomVehicleCells(w));
            }
            for (const auto& c : w.crates()) bloomsEnd += c.active;
            CHECK(listsOk);
            CHECK(maxPhantom == 0);
            CHECK(wormMismatch == 0);
            std::printf("audit soak %s: blooms %d -> %d (%d burst+regrow), worm samples %d (wandering %d, eating %d), "
                        "cloaked samples %d, repair samples %d, infiltrating %d, deviated %d, crate-bit mismatches %d\n",
                        name.c_str(), bloomsStart, bloomsEnd, mounds, worms, wormWander, wormEat, cloaked, repair, infiltr,
                        deviated, crateMismatch);
        }
    std::printf("audit soak: %d missions x 12000 ticks\n", maps);
#endif
}

// Repair pad (UpdateUnit states 24..26, order 0x21): drive onto the pad, repair for credits,
// drive off again.
void testRepairPad() {
    EcoFixture fx;
    auto& b = fx.templates->bin;
    constexpr int kPad = 5;
    b.buildingCount = 6;
    auto& pad = b.buildings[kPad];
    pad = b.buildings[kWind];
    pad.specialBehavior = building_behaviour::kRepairPad;
    pad.tilesOccupiedAll = pad.tilesOccupiedSolid = 0x777;
    pad.artHeight = 96;
    pad.exitPoint1X = 1;
    pad.exitPoint1Y = 1;
    pad.buildingGroup = 5;
    auto w = fx.world(32, 32, kBuildRock);
    Side& s = w->sides()[0];
    s.cash = 1000;
    const int pi = w->addBuilding(0, kPad, 10, 10, true, true);
    const int ti = w->addUnit(0, kTank, 4, 11, 4, 11);
    Unit& u = s.units[ti];
    u.health = 200;
    CHECK(w->orderRepairUnit(0, ti, pi));
    CHECK(!w->orderRepairUnit(0, w->addUnit(0, kInfantry, 2, 2, 2, 2), pi));  // infantry: rejected
    bool repaired = false, left = false;
    for (int k = 0; k < 600; ++k) {
        w->tick();
        if (u.state == UnitState::Repairing) repaired = true;
        if (repaired && u.health == 400 && u.state != UnitState::Repairing &&
            !(u.blockFromX == 11 && u.blockFromY == 11) && u.posSteps == 0)
            left = true;
        if (left) break;
    }
    CHECK(repaired && u.health == 400 && left);
    // 25 steps of 8 hp, max(1, (700 * 8 / 400) >> 1) = 7 credits each.
    CHECK(w->credits(0) == 1000 - 25 * 7);
}

// Deviator hit: the unit changes to the attacker's side (flag 0x1000, timer 0xf0, oldSide).
void testDeviator() {
    CombatFixture f;
    auto w = f.world(16, 16);
    w->addUnit(0, kTank, 2, 2, 2, 2);  // keeps side 0 alive (deviated units only return then)
    const int ti = w->addUnit(0, kTank, 8, 8, 8, 8);
    f.armour.warheads[0].radius = 16;
    f.armour.warheads[0].versusArmour[0] = 10;
    w->damageTiles(8 * 32 + 16, 8 * 32 + 16, 0, 0, 1, kNoIndex, true);
    const Unit* old = w->unit(0, ti);
    CHECK(old && old->state == UnitState::Dead);
    const Unit* nu = nullptr;
    for (int i = w->sides()[1].firstUnit; i != -1; i = w->sides()[1].nextObject(i)) nu = &w->sides()[1].units[i];
    CHECK(nu && (nu->flags & unit_flags::kDeviated) && nu->byte2E == 0xf0 && nu->oldSide == 0 && nu->blockFromX == 8);
    CHECK((w->cell(8, 8).flags & cell_flags::kOwnerMask) == 1 && (w->cell(8, 8).flags & cell_flags::kVehicle));
}

// ---------------------------------------------------------------------------------------
// Audit-1 fixes: building skirt, sandworms, spice blooms, stealth.

// Map__PlaceBuildingSkirt 0x44f1b0 / Map__RemoveBuildingSkirt 0x44f2f0: the skirt rows get
// skirt tiles on placement; on removal every non-concrete skirt cell draws rand() & 7 into
// kDamagedConcreteTiles (0x4e0af0), concrete cells get 0x28b without a draw.
void testBuildingSkirt() {
    EcoFixture fx;
    auto& wt = fx.templates->bin.buildings[kWind];
    wt.flags |= building_flags::kHasSkirt;
    wt.artWidth = 64;  // 2 columns
    wt.artHeight = 64;
    auto w = fx.world(32, 32, kBuildRock);
    w->cell(9, 3).flags |= cell_flags::kConcrete;  // one concrete cell under the skirt
    const int wi = w->addBuilding(0, kWind, 8, 2, false, true);
    CHECK(wi >= 0);
    // Rows y + (64 >> 5) - 2 = 2 and 3; 2-wide skirt tiles 0x269 (+0x20 on concrete) + col + row * 0x14.
    CHECK(w->cell(8, 2).tile == 0x269 && w->cell(9, 2).tile == 0x26a);
    CHECK(w->cell(8, 3).tile == 0x269 + 0x14 && w->cell(9, 3).tile == 0x289 + 1 + 0x14);
    // Direct removal: 3 rand draws (the concrete cell draws none).
    static constexpr std::uint16_t kDamaged[8] = {0x228, 0x229, 0x22a, 0x22b, 0x22c, 0x23c, 0x23d, 0x23e};
    Random ref = w->rng();
    WorldTestAccess::removeSkirt(*w, 8, 2, kWind);
    const std::uint16_t t0 = kDamaged[ref.next() & 7], t1 = kDamaged[ref.next() & 7], t2 = kDamaged[ref.next() & 7];
    CHECK(w->cell(8, 2).tile == t0 && w->cell(9, 2).tile == t1 && w->cell(8, 3).tile == t2);
    CHECK(w->cell(9, 3).tile == 0x28b);
    CHECK(w->rng().seed() == ref.seed());
    // Selling runs RemoveBuildingFromMap, which removes the skirt (tiles leave the skirt range).
    for (int x = 8; x < 10; ++x)
        for (int y = 2; y < 4; ++y) w->cell(x, y).tile = 0x269;
    CHECK(w->orderSellBuilding(0, wi));
    for (int k = 0; k < 200; ++k) w->tick();
    CHECK(w->building(0, wi) == nullptr);
    CHECK(w->cell(8, 2).tile != 0x269 && w->cell(9, 3).tile == 0x28b);
}

// Sandworm states 29 (sleeping) -> 27 (wandering) -> 28 (eating) and the shot-sleep rule.
void testSandworm() {
    Fixture fx;
    auto& b = fx.templates->bin;
    constexpr int kWormType = 3;
    b.unitCount = 4;
    auto& wt = b.units[kWormType];
    wt.hitPoints = 1000;
    wt.speed = 0x20000;
    wt.speedType = 1;
    wt.primaryWeapon = 0;
    wt.secondaryWeapon = -1;
    wt.specialBehavior = behaviour::kSandworm;
    wt.deathExplosion = -1;
    auto w = fx.world(24, 24, kDunes);
    const int wi = w->addUnit(7, kWormType, 4, 4, 4, 4);
    Unit* worm = w->unit(7, wi);
    CHECK(worm && worm->state == UnitState::Sleeping && worm->animDelay == 50);  // SandWormInitialSleep
    worm->animDelay = 2;
    int ticks = 0;
    Random ref = w->rng();
    while (worm->state == UnitState::Sleeping && ticks < 1000) {
        ref = w->rng();
        w->tick();
        ++ticks;
        if (worm->state == UnitState::Sleeping) CHECK((worm->flags & unit_flags::kCloaked) != 0);
    }
    CHECK(ticks == 2 * 101 + 1);  // +0x25 counts in units of 101 ticks, then one wake-up tick
    CHECK(worm->state == UnitState::Wandering && worm->health == 1000);
    const int ex = static_cast<int>(static_cast<std::uint32_t>(ref.next()) % 24u);
    const int ey = static_cast<int>(static_cast<std::uint32_t>(ref.next()) % 24u);
    CHECK(worm->targetX == ex && worm->targetY == ey);
    CHECK(w->rng().seed() == ref.seed());
    // A tank on sand is swallowed when the worm arrives on its tile.
    const int ti = w->addUnit(0, kTank, 9, 4, 9, 4);
    CHECK(ti >= 0);
    worm->targetX = 9;
    worm->targetY = 4;
    for (int k = 0; k < 400 && worm->state == UnitState::Wandering; ++k) w->tick();
    CHECK(worm->state == UnitState::Eating && worm->animDelay <= 0x32 && worm->byte29 == 1);
    CHECK(w->sides()[0].unitsLost == 1);
    CHECK(w->unit(0, ti) && w->unit(0, ti)->state == UnitState::Dead);
    for (int k = 0; k < 11; ++k) w->tick();  // deadStateTimer 10
    CHECK(w->unit(0, ti) == nullptr);  // swallowed: freed after the dead state
    // Digest, then no target left: a random destination (2 rand), wandering again.
    for (int k = 0; k < 60 && worm->state == UnitState::Eating; ++k) w->tick();
    CHECK(worm->state == UnitState::Wandering);
    // Shot below half health: back to sleep for SandWormShotSleep.
    worm->health = 400;
    w->tick();
    CHECK(worm->state == UnitState::Sleeping && worm->animDelay == 90);
}

// Spice blooms: growth on the timer, a low hit on a grown bloom bursts it into a spice mound
// (spice shells) without damaging the unit on it, and RecycleCrate regrows it.
void testSpiceBloom() {
    Fixture fx;
    auto w = fx.world(32, 32, kDunes);
    w->placeCrate(10, 10, 1, 7, 4, 0);
    CHECK((w->cell(10, 10).flags & cell_flags::kCrate) && WorldTestAccess::crateTypeAt(*w, 10, 10) == 7);
    Random ref = w->rng();
    w->tick();  // timer expires: stage 8, new timer 2000..2999
    CHECK(WorldTestAccess::crateTypeAt(*w, 10, 10) == 8);
    const Crate* c = nullptr;
    for (const auto& k : w->crates())
        if (k.active && k.x == 10 && k.y == 10) c = &k;
    CHECK(c && c->image == 5 && c->timer == static_cast<std::uint32_t>(ref.next()) % 1000 + 2000 - 0);
    // A tank on the bloom is not damaged by a low hit; the bloom bursts (no regrowths left).
    const int ti = w->addUnit(1, kTank, 10, 10, 10, 10);
    CHECK(ti >= 0);
    w->damageTiles(10 * 32 + 16, 10 * 32 + 16, 0, 0, 0, kNoIndex, false);
    CHECK(w->unit(1, ti)->health == 400);
    CHECK(WorldTestAccess::crateTypeAt(*w, 10, 10) == 0xff && !(w->cell(10, 10).flags & cell_flags::kCrate));
    int bullets = 0;
    for (const auto& sd : w->sides())
        for (int i = sd.firstBullet; i != -1; i = sd.nextObject(i)) ++bullets;
    CHECK(bullets > 0);
    // With regrowths left the record restarts as a new bloom (stage 7, image 4).
    w->placeCrate(20, 20, 3000, 9, 6, 2);
    w->damageTiles(20 * 32 + 16, 20 * 32 + 16, 0, 0, 0, kNoIndex, false);
    for (const auto& k : w->crates())
        if (k.active && k.x == 20 && k.y == 20) c = &k;
    CHECK(c && c->x == 20 && c->type == 7 && c->image == 4 && c->respawns == 1 && c->timer >= 2000 && c->timer < 3000);
    // A high shot (z > 0x60000) ignores the bloom.
    w->damageTiles(20 * 32 + 16, 20 * 32 + 16, 0x70000, 0, 0, kNoIndex, false);
    CHECK(WorldTestAccess::crateTypeAt(*w, 20, 20) == 7);
}

// Unit_UpdateStealth 0x49f610: a stealth unit cloaks once it has not fired for a while and
// decloaks (22-tick fade) when enemy infantry comes next to it.
void testStealthCloak() {
    Fixture fx;
    auto w = fx.world(24, 24);
    const int ti = w->addUnit(0, kTrike, 5, 5, 5, 5);
    // A visible unit of the same side, so UncloakRemainingStealthUnit 0x455480 stays out of it.
    const int ni = w->addUnit(0, kTank, 15, 15, 15, 15);
    Unit* u = w->unit(0, ti);
    u->flags |= unit_flags::kStealth;
    w->tick();
    CHECK((u->flags & unit_flags::kCloaked) && u->stealthTimer == 0x16);
    for (int k = 0; k < 0x16; ++k) w->tick();
    CHECK((u->flags & unit_flags::kCloaked) && u->stealthTimer == 0);
    // Enemy infantry adjacent: decloak.
    CHECK(w->addUnit(1, kInfantry, 6, 5, 6, 5) >= 0);
    w->tick();

    CHECK(!(u->flags & unit_flags::kCloaked) && u->stealthTimer == 0x16);
    // A non-stealth unit never cloaks.
    for (int k = 0; k < 10; ++k) w->tick();
    CHECK(!(w->unit(0, ni)->flags & unit_flags::kCloaked));
}

// Save games (game/savegame.*): save after N ticks, load into a freshly constructed world, run
// both M more ticks with the same scripted orders (AIs active on real data) and require
// identical state hashes / save bytes. Also: file header checks and corrupt-file rejection.
void reportRoundTrip(const char* name, const RoundTripReport& r) {
    CHECK(r.ok);
    CHECK(r.freshDiffered);
    std::printf("save round trip %s: %s%s (world %zu bytes, file %zu bytes, %d pending orders saved, hash %016llx -> %016llx)\n",
                name, r.ok ? "identical" : "FAILED: ", r.error.c_str(), r.saveBytes, r.fileBytes, r.pendingOrdersSaved,
                static_cast<unsigned long long>(r.hashAtSave), static_cast<unsigned long long>(r.hashAtEnd));
}

void testSaveRoundTrip() {
    // Synthetic: two sides fighting on an empty map (no game data needed).
    {
        CombatFixture f;
        auto make = [&]() {
            auto w = f.world(32, 32);
            for (int i = 0; i < 6; ++i) {
                w->addUnit(0, i % 2 ? kTank : kInfantry, 4 + i, 6, 4 + i, 6);
                w->addUnit(1, i % 2 ? kTrike : kTank, 20 + i, 20, 20 + i, 20);
            }
            return w;
        };
        // The fresh world must differ from the saved one: give the original a head start.
        auto orders = [](World& w, std::uint32_t step) {
            if (step == 10)
                for (int i = w.sides()[0].firstUnit; i != -1; i = w.sides()[0].nextObject(i)) w.orderMove(0, i, 22, 18);
            if (step == 200)
                for (int i = w.sides()[1].firstUnit; i != -1; i = w.sides()[1].nextObject(i)) w.orderMove(1, i, 6, 8);
            scriptedOrders(w, 0, step);
        };
        reportRoundTrip("synthetic", saveRoundTrip(make, 150, 1500, orders, 50));
    }
    // File format guards.
    {
        Fixture fx;
        auto w = fx.world(8, 8);
        SaveMeta m;
        m.name = "H5V1 - test";
        m.map = "H5V1.MAP";
        m.skirmish = true;
        m.aiPlayers = 3;
        m.cameraX = 123.5f;
        auto file = writeSaveFile(m, *w);
        CHECK(std::memcmp(file.data(), "D2KSAVE", 8) == 0);
        const SaveMeta back = readSaveMeta(file);
        CHECK(back.name == m.name && back.map == m.map && back.skirmish && back.aiPlayers == 3 && back.cameraX == 123.5f);
        std::span<const std::uint8_t> section;
        bool threw = false;
        auto bad = file;
        bad[bad.size() / 2] ^= 0x55;
        try { readSaveFile(bad, section); } catch (const SaveError&) { threw = true; }
        CHECK(threw);
        threw = false;
        bad = file;
        bad[8] = 99;  // version
        try { readSaveMeta(bad); } catch (const SaveError&) { threw = true; }
        CHECK(threw);
        threw = false;
        auto other = fx.world(9, 8);  // map size mismatch
        try {
            readSaveFile(file, section);
            loadWorldState(*other, section);
        } catch (const SaveError&) { threw = true; }
        CHECK(threw);
    }
    // Campaign-map saves (UISaveGame FUN_00474e20 outside a battle): meta only, empty world section.
    {
        SaveMeta m;
        m.name = "Before the sietch";
        m.savedAt = 1700000000;
        m.campaignHouse = 2;
        m.missionNumber = 6;
        m.difficulty = 2;
        const auto file = writeCampaignSaveFile(m);
        std::span<const std::uint8_t> section;
        const SaveMeta back = readSaveFile(file, section);
        CHECK(back.kind == SaveKind::CampaignMap && section.empty());
        CHECK(back.campaignHouse == 2 && back.missionNumber == 6 && back.difficulty == 2);
        CHECK(back.name == m.name && back.savedAt == m.savedAt);
        CHECK(readSaveMeta(file).kind == SaveKind::CampaignMap);
        // Writing it again gives the same bytes (nothing else is stored).
        CHECK(writeCampaignSaveFile(back) == file);
        // A battle save keeps its kind; writeSaveFile always marks Battle.
        Fixture fx;
        auto w = fx.world(8, 8);
        SaveMeta bm = m;  // kind Battle by default even when copied from campaign fields
        const auto battle = writeSaveFile(bm, *w);
        CHECK(readSaveFile(battle, section).kind == SaveKind::Battle && !section.empty());
        // Version 6 files (no kind / missionNumber at the end of the metadata) still load as battles.
        {
            SaveReader r(battle);
            char magic[8];
            r.bytes(magic, 8);
            r.u32();
            const std::uint32_t metaLen = r.u32();
            const auto meta = r.take(metaLen);
            const std::uint32_t worldLen = r.u32();
            const auto world = r.take(worldLen);
            SaveWriter v6;
            v6.bytes(kSaveMagic, 8);
            v6.u32(6);
            v6.u32(metaLen - 5);  // u8 kind + i32 missionNumber
            v6.bytes(meta.data(), metaLen - 5);
            v6.u32(worldLen);
            v6.bytes(world.data(), world.size());
            v6.u64(fnv1a64(v6.data()));
            const SaveMeta old6 = readSaveFile(v6.data(), section);
            CHECK(old6.kind == SaveKind::Battle && old6.missionNumber == 0 && old6.name == m.name);
            CHECK(section.size() == worldLen);
        }
        // Inconsistent kinds are rejected.
        bool threw = false;
        try {
            // A campaign-map kind with a world section: the battle file rebuilt with the kind byte set.
            SaveReader r(battle);
            char magic[8];
            r.bytes(magic, 8);
            r.u32();
            const std::uint32_t metaLen = r.u32();
            const auto meta = r.take(metaLen);
            const std::uint32_t worldLen = r.u32();
            const auto world = r.take(worldLen);
            std::vector<std::uint8_t> metaBytes(meta.begin(), meta.end());
            metaBytes[metaLen - 5] = 1;  // kind = CampaignMap
            SaveWriter f;
            f.bytes(kSaveMagic, 8);
            f.u32(kSaveVersion);
            f.u32(metaLen);
            f.bytes(metaBytes.data(), metaBytes.size());
            f.u32(worldLen);
            f.bytes(world.data(), world.size());
            f.u64(fnv1a64(f.data()));
            readSaveFile(f.data(), section);
        } catch (const SaveError&) { threw = true; }
        CHECK(threw);
    }
#ifdef D2K_GAME_DATA_DIR
    const std::string data = D2K_GAME_DATA_DIR, bin = data + "/bin";
    if (formats::findFileNoCase(bin, "ARMOUR.BIN").empty()) {
        std::printf("(game data not found: save round trip on real missions skipped)\n");
        return;
    }
    auto templates = std::make_unique<formats::Templates>(formats::loadTemplates(formats::findFileNoCase(bin, "Templates.bin")));
    const auto speed = formats::loadSpeed(formats::findFileNoCase(bin, "SPEED.BIN"));
    const auto armour = formats::loadArmour(formats::findFileNoCase(bin, "ARMOUR.BIN"));
    const auto builexp = formats::readFile(formats::findFileNoCase(bin, "BUILEXP.BIN"));
    const auto circles = formats::readFile(formats::findFileNoCase(bin, "CIRCLES.BIN"));
    const auto vars = formats::loadVars(formats::findFileNoCase(bin, "VARS.BIN"));
    const auto tileData = formats::loadTileData(formats::findFileNoCase(bin, "TILEDATA.BIN"));
    const Rules rules{templates.get(), &speed, &armour, nullptr, &builexp, &vars, &circles};
    // Campaign missions (the player's side gets scripted orders, the computer players play).
    struct Mission { const char* name; int player; int pre, post; };
    for (const Mission c : {Mission{"H5V1", 1, 3000, 3000}, Mission{"H8V1", 1, 6000, 4000}}) {
        const std::string mapPath = formats::findFileNoCase(data + "/Missions", std::string(c.name) + ".MAP");
        const std::string misPath = formats::findFileNoCase(data + "/maps", std::string("_") + c.name + ".MIS");
        if (mapPath.empty() || misPath.empty()) continue;
        const auto map = formats::loadMap(mapPath);
        const auto mis = formats::loadMis(misPath);
        const auto tileAtr = formats::loadTileAtr(formats::findFileNoCase(bin, mis.tileAttributes + ".BIN"));
        int frigateTicks = 0, deliveries = 0;
        auto orders = [&, player = c.player](World& w, std::uint32_t step) {
            scriptedOrders(w, player, step);
            if (step % 10 == 0)
                for (const Side& S : w.sides()) {
                    frigateTicks += S.starportFrigateBusy;
                    for (const Delivery& d : S.deliveries) deliveries += d.active;
                }
        };
        auto make = [&]() { return std::make_unique<World>(map, mis, tileAtr, tileData, rules, c.player); };
        reportRoundTrip(c.name, saveRoundTrip(make, c.pre, c.post, orders));
        std::printf("  %s: starport frigate busy in %d samples, active deliveries in %d samples (both worlds)\n", c.name,
                    frigateTicks, deliveries);
    }
    // Skirmish with three computer players (the loaded world starts from another seed).
    {
        const std::string mapPath = formats::findFileNoCase(data + "/maps", "4PLAY1.MAP");
        const std::string misPath = formats::findFileNoCase(data + "/maps", "_SPAWN.MIS");
        const std::string tpPath = formats::findFileNoCase(bin, "TECHPOS.BIN");
        if (!mapPath.empty() && !misPath.empty() && !tpPath.empty()) {
            const auto techPos = formats::loadTechPos(tpPath);
            const auto map = formats::loadMap(mapPath);
            const auto mis = formats::loadMis(misPath);
            const auto tileAtr = formats::loadTileAtr(formats::findFileNoCase(bin, mis.tileAttributes + ".BIN"));
            std::uint32_t seed = 7;
            auto make = [&]() {
                SkirmishSetup setup;
                setup.techPos = &techPos;
                setup.aiPlayers = 3;
                setup.techLevel = 7;
                setup.crates = true;
                setup.seed = seed++;
                return std::make_unique<World>(map, mis, tileAtr, tileData, rules, setup);
            };
            reportRoundTrip("4PLAY1 skirmish (3 AIs)",
                            saveRoundTrip(make, 4000, 4000, [](World& w, std::uint32_t step) { scriptedOrders(w, 0, step); }));
        }
    }
#endif
}

// ---------------------------------------------------------------------------------------
// Sound (re/notes/audio.md)

// Fake AIL channels: a started sample plays until finish(ch).
struct FakeChannels : SoundOutput {
    struct Ch { bool busy = false; int sample = -1, volume = 0, pan = 64; };
    std::array<Ch, SoundSystem::kChannels> ch{};
    std::vector<int> started;  // sample ids in start order
    bool busy(int c) const override { return ch[c].busy; }
    int sample(int c) const override { return ch[c].sample; }
    int volume(int c) const override { return ch[c].volume; }
    void start(int c, int s, int pan, int vol) override {
        ch[c] = {true, s, vol, pan};
        started.push_back(s);
    }
    void finish(int c) { ch[c].busy = false; }
};

// QueueAudioToPlay / FUN_0046faa0: one speech at a time, priority 1 first, flag-0 requests only
// into empty queues, flag-1 requests deduplicated, per-sample repeat guard.
void testSoundQueue() {
    FakeChannels out;
    SoundSystem snd;
    snd.setMoneyTick(99);
    CHECK(snd.queue(out, 10, true, 0, 0, 1));   // UNITREADY
    CHECK(!snd.queue(out, 10, true, 0, 0, 1));  // already waiting
    CHECK(snd.queue(out, 11, true, 0, 1, 1));   // REINFORCE (priority 1)
    CHECK(!snd.queue(out, 12, false, 0, 0, 1)); // unit response: queues not empty -> dropped
    snd.update(out, 100);
    CHECK(out.started.size() == 1 && out.started[0] == 11 && out.ch[0].busy && out.ch[0].volume == 100);
    CHECK(!snd.queue(out, 11, true, 0, 1, 2));  // playing right now -> dropped
    snd.update(out, 100);                       // still talking: nothing new
    CHECK(out.started.size() == 1);
    out.finish(0);
    snd.update(out, 100);                       // channel free: the flag clears this frame...
    CHECK(out.started.size() == 1 && !snd.speaking());
    snd.update(out, 100);                       // ...the next sample starts on the next one
    CHECK(out.started.size() == 2 && out.started[1] == 10 && snd.queuesEmpty());
    // Flag 0 while talking with empty queues: waits behind the current speech...
    CHECK(!snd.queue(out, 99, false, 15, 0, 3));  // ...but a money tick never does
    CHECK(snd.queue(out, 12, false, 0, 0, 3));
    out.finish(0);
    snd.update(out, 100);
    snd.update(out, 100);
    CHECK(out.started.back() == 12);
    out.finish(0);
    snd.update(out, 100);
    CHECK(snd.queue(out, 99, false, 15, 0, 20));  // idle: the money tick plays
    snd.update(out, 100);
    CHECK(out.started.back() == 99 && !snd.queue(out, 12, false, 0, 0, 20));  // nothing waits behind it
    CHECK(!snd.queue(out, 99, false, 15, 0, 30));  // repeat guard (15 ticks)
    out.finish(0);
    snd.update(out, 100);
    // A queue holds 4 waiting samples.
    for (int i = 0; i < 6; ++i) snd.queue(out, 20 + i, true, 0, 0, 4);
    CHECK(snd.queued(0) == 4);
    snd.reset();
    CHECK(snd.queuesEmpty());
    // Repeat guard: UNDERATTACK (300 ticks) requested every tick of a fight.
    FakeChannels out2;
    SoundSystem s2;
    int queued = 0;
    for (std::uint32_t t = 1; t <= 700; ++t) {
        if (s2.queue(out2, 30, true, 300, 0, t)) ++queued;
        s2.update(out2, 100);
        out2.finish(0);
    }
    CHECK(queued == 3);  // ticks 1, 301, 601
}

// PlaySoundAt: 50 on screen (centred), quieter and panned off screen, never below 5; channels
// 1..5, the quietest replaced only by a louder sound.
void testSoundPositional() {
    const SoundView view{0, 0, 480, 352};  // 15 x 11 tiles from (0, 0)
    int pan = 0, vol = 0;
    SoundSystem::positionalMix(7, 5, view, 64, 100, pan, vol);
    CHECK(pan == 64 && vol == 50);
    SoundSystem::positionalMix(7, 5, view, 64, 50, pan, vol);
    CHECK(vol == 25);
    SoundSystem::positionalMix(39, 5, view, 64, 100, pan, vol);  // 32 tiles right of the centre
    CHECK(pan == 128 - (64 - 32 * 64 / 64 < 16 ? 16 : 64 - 32 * 64 / 64) && vol == 35 - 32 * 35 / 64);
    SoundSystem::positionalMix(63, 63, view, 64, 100, pan, vol);
    CHECK(vol == 5 && pan > 64);
    FakeChannels out;
    for (int i = 0; i < 5; ++i) CHECK(SoundSystem::playAt(out, 40 + i, 7, 5, view, 64, 100) == i + 1);
    CHECK(!out.ch[0].busy);                                          // never the speech channel
    CHECK(SoundSystem::playAt(out, 50, 63, 63, view, 64, 100) == -1); // quieter than all: dropped
    out.ch[3].volume = 4;
    CHECK(SoundSystem::playAt(out, 51, 63, 63, view, 64, 100) == 3);  // replaces the quieter one
    CHECK(SoundSystem::playing(out, 51) && !SoundSystem::playing(out, 50));
    CHECK(SoundSystem::playUi(out, 52, 100) == 0);                   // UI: any channel, centre
}

int countMentat(const World& w, const char* name, bool queue, int throttle = 0) {
    int n = 0;
    for (const auto& e : w.soundEvents())
        if (e.house && e.name && std::strcmp(e.name, name) == 0 && e.queue == queue && e.throttle == throttle) ++n;
    return n;
}

// Simulation-side speech: construction complete once, unit lost, base under attack on every
// hit (the queue's 300-tick guard spaces it out), only for the local player.
void testSoundEvents() {
    {
        EcoFixture fx;
        auto w = fx.world(32, 32, kBuildRock);
        w->sides()[0].cash = 5000;
        w->addBuilding(0, kYard, 5, 5, false, true);
        CHECK(w->orderBuildBuilding(0, kWind));
        int ready = 0, building = 0, t = 0;
        for (; t < 120; ++t) {
            w->tick();
            ready += countMentat(*w, "BUILDREADY", true);
            building += countMentat(*w, "BUILDING", false);
        }
        CHECK(w->buildingQueue(0).ready && ready == 1 && building == 1);
    }
    {
        CombatFixture f;
        auto w = f.world(16, 16);
        const int bi = w->addBuilding(0, kTurret, 2, 2, false);
        const int ui = w->addUnit(0, kDummy, 8, 8, 8, 8);
        w->addBuilding(1, kTurret, 12, 12, false);
        w->tick();
        Building& b = w->sides()[0].buildings[bi];
        b.health = 100000;
        FakeChannels out;
        SoundSystem snd;
        int hits = 0, queued = 0;
        for (std::uint32_t t = 0; t < 400; ++t) {
            w->tick();
            w->damageTiles(2 * 32 + 16, 2 * 32 + 16, 0, 0, 1, kNoIndex, false);
            hits += countMentat(*w, "UNDERATTACK", true, 300);
            for (const auto& e : w->soundEvents())
                if (e.house && std::strcmp(e.name, "UNDERATTACK") == 0 && snd.queue(out, 7, e.queue, e.throttle, e.priority, w->gameTicks()))
                    ++queued;
            snd.update(out, 100);
            out.finish(0);
        }
        CHECK(hits == 400 && queued == 2);  // requested on every hit, played at t 1 and 301
        Unit& u = w->sides()[0].units[ui];
        u.health = 10;
        w->damageTiles(u.posX >> 16, u.posY >> 16, 0, 0, 1, kNoIndex, false);
        CHECK(u.state == UnitState::Dead && countMentat(*w, "UNITLOST", true) == 1);
        // The other side's losses are silent for us.
        const int e = w->addUnit(1, kDummy, 10, 4, 10, 4);
        w->tick();
        Unit& eu = w->sides()[1].units[e];
        eu.health = 10;
        w->damageTiles(eu.posX >> 16, eu.posY >> 16, 0, 0, 0, kNoIndex, false);
        CHECK(eu.state == UnitState::Dead && countMentat(*w, "UNITLOST", true) == 0);
    }
}


// Campaign table, territory map and score (game/campaign.*, re/notes/campaign-flow.md).
void testCampaignScore() {
    using namespace d2k::game;
    // Score targets (FUN_0043ec70): units 150 * share of the losses not ours, spice 50 * our share.
    ScoreInput in;
    in.mySide = 0;
    in.sides[0] = {true, 0, 10, 2, 3000};
    in.sides[1] = {true, 1, 30, 6, 1000};
    in.sides[2] = {false, 2, 100, 100, 100};  // not participating: ignored
    const ScoreTargets t = scoreTargets(in);
    CHECK(t.ours[0] == 112 && t.theirs[0] == 38);  // (40 - 10) * 150 / 40
    CHECK(t.ours[1] == 75 && t.theirs[1] == 25);
    CHECK(t.ours[2] == 37 && t.theirs[2] == 13);   // 3000 * 50 / 4000
    ScoreInput none;
    none.sides[0].participates = true;
    const ScoreTargets z = scoreTargets(none);
    CHECK(z.ours[0] == 0 && z.theirs[0] == 0 && z.theirs[2] == 0);

    // Counting (FUN_0043f030): +1 per call, 2 s pauses after units and buildings.
    ScoreCounter c(in, 1.5f);
    std::uint32_t now = 0;
    int ticks = 0;
    for (int i = 0; i < 112; ++i) ticks += c.step(now) ? 1 : 0;
    CHECK(ticks == 112 && c.counter(true, 0) == 112 && c.counter(false, 0) == 38 && c.category() == 1);
    CHECK(!c.step(now + 1999) && c.counter(true, 1) == 0);  // paused
    CHECK(c.step(now + 2000) && c.counter(true, 1) == 1);
    now += 2000;
    while (!c.done()) {
        now += 2001;
        c.step(now);
    }
    CHECK(c.shown(true, 0) == 168 && c.shown(false, 0) == 57);  // ftol(112 * 1.5), ftol(38 * 1.5)
    CHECK(c.ourTotal() == 168 + 112 + 55 && c.theirTotal() == 57 + 37 + 19);
    CHECK(c.barWidth(true, 0) == 74 && c.barWidth(true, 2) == 74);
    // A lost mission: ours halved, our total halved again (quirk), theirs not.
    in.lost = true;
    ScoreCounter l(in, 1.0f);
    for (std::uint32_t k = 0; !l.done() && k < 1000; ++k) l.step(k * 2001);
    CHECK(l.shown(true, 0) == 56 && l.shown(false, 0) == 38 && l.ourTotal() == (56 + 37 + 18) / 2);

    CHECK(scoreRank(0) == 0 && scoreRank(250) == 1 && scoreRank(500) == 4 && scoreRank(1350) == 11);
    std::array<HighScore, kHighScores> hs{};
    CHECK(insertHighScore(hs, 300, 0, 2) == 0 && hs[0].mission == 1 && hs[0].rank == 2);
    CHECK(insertHighScore(hs, 900, 1, 5) == 0 && hs[1].score == 300 && hs[0].mission == 4);
    CHECK(insertHighScore(hs, 100, 2, 1) == 2 && hs[2].mission == 1);  // MissionNumber 1 -> 1

    // Territories.
    CHECK(pointInRegion(13, 320, 140) && !pointInRegion(13, 100, 300));
    CHECK(regionPointerPos(9)[0] == (181 + 313 + 221 + 194) / 4 - 16);

    const std::string data = D2K_GAME_DATA_DIR;
    const std::string path = data + "/UI_DATA/campaign.uib";
    if (!std::filesystem::exists(path)) return;
    const CampaignTable tab = CampaignTable::parse(d2k::formats::readFile(path));
    const CampaignRecord& a1 = tab.at(0, 1);
    CHECK(a1.region[0] == 7 && a1.region[1] == 8 && a1.mission[0] == "a1v1" && a1.mission[1] == "a1v2");
    CHECK(a1.pointerX[0] == 121 && a1.pointerY[0] == 199 && a1.revealSound[0] == "C_A1a");
    CHECK(tab.at(0, 9).scoreMultiplier == 5.0f && tab.at(0, 3).enemyHouse == 2 && tab.at(1, 0).music == "Arakatak");
    // Atreides, MissionNumber 2: record 0 as the old state, record 1 appearing.
    const GlobeState g = globeState(tab, 0, 2);
    CHECK(g.owner[1] == 0 && g.owner[2] == 0 && g.owner[11] == 1 && g.owner[20] == 2);  // regions 2, 3, 12, 21
    CHECK(g.fresh[6] && g.owner[6] == 0 && g.fresh[0] && g.owner[0] == 0);  // 7 (mission), 1 (A list)
    CHECK(g.fresh[12] && g.owner[12] == 1 && !g.fresh[1] && g.ownerBefore[6] == kNoOwner);
    const auto order = revealOrder(tab, g, 0, 2);
    // Record 2 reveals Atreides (0), then Ordos (2), then Harkonnen (1) with C_A2a / b / c.
    CHECK(!order.empty() && order.front().region == 1 && order.front().sound == "C_A2a");
    CHECK(order.size() == 9 && order[3].sound == "C_A2b" && g.owner[order[3].region - 1] == 2);
    int variant = -1;
    CHECK(choosableRegionAt(tab, 0, 2, 120, 100, &variant) == 5 && variant == 0);  // a2v1 territory
}

// ---------------------------------------------------------------------------------------
// Cheats (cheats.cpp, not original): queued orders, saved state.

bool hasOption(const std::vector<BuildOption>& v, int type) {
    return std::any_of(v.begin(), v.end(), [&](const BuildOption& o) { return o.type == type; });
}

void testCheatCredits() {
    EcoFixture fx;
    auto w = fx.world(24, 24, kBuildRock);
    Side& s = w->sides()[0];
    s.cash = 1000;
    s.cashDrip = 300;  // pending: would change the total later
    CHECK(!w->cheatsUsed());
    CHECK(w->orderCheatSetCredits(0, 12345));
    CHECK(w->credits(0) == 1000);  // queued, not executed yet
    w->tick();
    CHECK(w->credits(0) == 12345 && w->cheatsUsed());
    for (int k = 0; k < 100; ++k) w->tick();
    CHECK(w->credits(0) == 12345);
    CHECK(w->orderCheatAddCredits(0, 10000));
    w->tick();
    CHECK(w->credits(0) == 22345);
    CHECK(w->orderCheatAddCredits(0, -50000));
    w->tick();
    CHECK(w->credits(0) == 0);
}

void testCheatTechAndOverrides() {
    EcoFixture fx;
    fx.templates->bin.units[kTank].techLevel = 3;
    auto w = fx.world(32, 32, kBuildRock);
    Side& s = w->sides()[0];
    s.cash = 5000;
    s.techLevel = 1;
    w->addBuilding(0, kYard, 2, 2, true);
    w->addBuilding(0, kWind, 6, 2, true);
    w->addBuilding(0, kHeavy, 10, 10, true);
    CHECK(!hasOption(w->buildableUnits(0), kTank));  // tech 1 < 3
    CHECK(w->orderCheatTechLevel(0, 5));
    w->tick();
    CHECK(s.techLevel == 5 && hasOption(w->buildableUnits(0), kTank));
    // Overrides: block a default option, allow ones the rules never list.
    CHECK(hasOption(w->buildableBuildings(0), kWind) && !hasOption(w->buildableUnits(0), kMcv));
    w->orderCheatUnitOverride(0, kTank, BuildOverride::Blocked);
    w->orderCheatUnitOverride(0, kMcv, BuildOverride::Allowed);
    w->orderCheatBuildingOverride(0, kWind, BuildOverride::Blocked);
    w->tick();
    CHECK(!hasOption(w->buildableUnits(0), kTank) && hasOption(w->buildableUnits(0), kMcv));
    CHECK(!hasOption(w->buildableBuildings(0), kWind) && hasOption(w->buildableBuildings(0), kHeavy));
    CHECK(w->unitOverride(0, kMcv) == BuildOverride::Allowed && w->unitOverride(1, kMcv) == BuildOverride::Default);
    CHECK(!w->orderBuildUnit(0, kTank));
    // An allowed unit without a factory it can leave from is built and delivered by carryall.
    CHECK(w->orderCheatInstantBuild(0, true));
    w->tick();
    CHECK(w->instantBuild(0) && w->orderBuildUnit(0, kMcv));
    int t = 0;
    for (; t < 3000 && countUnits(s, kMcv) == 0; ++t) w->tick();
    CHECK(countUnits(s, kMcv) == 1);
    // Instant build of a normal unit: done within a couple of ticks.
    w->orderCheatResetOverrides(0);
    w->tick();
    CHECK(hasOption(w->buildableUnits(0), kTank) && !hasOption(w->buildableUnits(0), kMcv) &&
          hasOption(w->buildableBuildings(0), kWind));
    CHECK(w->orderBuildUnit(0, kTank));
    for (int k = 0; k < 3; ++k) w->tick();
    CHECK(countUnits(s, kTank) == 1);
    std::printf("cheats: allowed MCV without factory delivered after %d ticks\n", t);
}

void testCheatDelivery() {
    // With a starport: frigate trips of up to 39 units, free.
    {
        PortFixture fx;
        auto w = fx.world(48, 48, kBuildRock);
        Side& s = w->sides()[0];
        s.cash = 777;
        w->addBuilding(0, kPort, 20, 6, true);
        WorldTestAccess::initStarport(*w, 0);
        CHECK(w->orderCheatDeliver(0, kTank, 42) && w->orderCheatDeliver(0, kTrike, 3));
        w->tick();
        CHECK(w->cheatDeliveriesPending(0) == 0);
        int frigates = 0;
        for (int k = 0; k < 20000 && !(countUnits(s, kTank) == 42 && countUnits(s, kTrike) == 3); ++k) {
            w->tick();
            frigates = std::max(frigates, countUnits(s, kFrigateU));
        }
        CHECK(countUnits(s, kTank) == 42 && countUnits(s, kTrike) == 3 && frigates == 1);
        for (int k = 0; k < 300; ++k) w->tick();
        CHECK(w->credits(0) == 777);
        std::printf("cheats: by frigate %d tanks %d trikes, max frigates %d, tick %u\n", countUnits(s, kTank), countUnits(s, kTrike), frigates, w->gameTicks());
    }
    // Without a starport: carryall to the base.
    {
        EcoFixture fx;
        auto w = fx.world(32, 32, kBuildRock);
        Side& s = w->sides()[0];
        s.cash = 100;
        w->addBuilding(0, kYard, 10, 10, true);
        CHECK(w->orderCheatDeliver(0, kTrike, 3));
        for (int k = 0; k < 3000 && countUnits(s, kTrike) < 3; ++k) w->tick();
        CHECK(countUnits(s, kTrike) == 3 && w->credits(0) == 100);
    }
}

void testCheatSpawn() {
    EcoFixture fx;
    auto w = fx.world(40, 40, kBuildRock);
    CHECK(w->orderCheatSpawnUnit(1, kTank, 30, 30, 5));
    CHECK(w->canCheatSpawnBuilding(kHeavy, 20, 5));
    CHECK(!w->canCheatSpawnBuilding(kHeavy, 38, 5));  // off the map
    CHECK(w->orderCheatSpawnBuilding(2, kHeavy, 20, 5, true));
    w->tick();
    CHECK(countUnits(w->sides()[1], kTank) == 5);
    int found = 0;
    for (int i = w->sides()[2].firstBuilding; i != -1; i = w->sides()[2].nextObject(i))
        found += w->sides()[2].buildings[i].type == kHeavy;
    CHECK(found == 1);
    CHECK(!w->canCheatSpawnBuilding(kWind, 21, 6));  // occupied now
    CHECK(!w->orderCheatSpawnBuilding(0, kWind, 21, 6));
    // Every spawned tank on its own tile.
    const Side& s1 = w->sides()[1];
    for (int i = s1.firstUnit; i != -1; i = s1.nextObject(i))
        for (int j = s1.nextObject(i); j != -1; j = s1.nextObject(j))
            CHECK(s1.units[i].blockFromX != s1.units[j].blockFromX || s1.units[i].blockFromY != s1.units[j].blockFromY);
}

void testCheatSaveRoundTrip() {
    PortFixture f;
    auto make = [&]() {
        auto w = f.world(40, 40, kBuildRock);
        w->sides()[0].cash = 3000;
        w->sides()[0].techLevel = 2;
        w->addBuilding(0, kYard, 2, 2, true);
        w->addBuilding(0, kHeavy, 8, 2, true);
        w->addBuilding(0, kPort, 20, 6, true);
        w->addBuilding(1, kYard, 30, 30, true);
        return w;
    };
    auto orders = [](World& w, std::uint32_t step) {
        if (step == 5) {
            w.orderCheatSetCredits(0, 50000);
            w.orderCheatTechLevel(0, 7);
            w.orderCheatUnitOverride(0, kMcv, BuildOverride::Allowed);
            w.orderCheatBuildingOverride(0, kWind, BuildOverride::Blocked);
            w.orderCheatInstantBuild(0, true);
            w.orderCheatDeliver(0, kTank, 50);  // two trips: the second is still queued at the save
            w.orderCheatSpawnUnit(1, kTrike, 32, 25, 4);
        }
        if (step == 60) {  // pending at the save point
            w.orderCheatSpawnBuilding(1, kHeavy, 34, 30, true);
            w.orderCheatDeliver(0, kTrike, 2);
        }
        if (step == 300) w.orderBuildUnit(0, kMcv);
    };
    auto r = saveRoundTrip(make, 60, 3000, orders, 100);
    reportRoundTrip("cheats", r);
    auto w = make();
    for (std::uint32_t k = 0; k < 10; ++k) {
        orders(*w, k);
        w->tick();
    }
    CHECK(w->cheatsUsed());
}

// Cheat reinforcements: scheduled carryall deliveries (the .MIS Reinforcement event path).
void testCheatReinforcements() {
    EcoFixture fx;
    auto w = fx.world(40, 40, kBuildRock);
    Side& s = w->sides()[1];  // a computer side (not the player's)
    w->addBuilding(1, kYard, 24, 24, true);
    const std::vector<std::uint8_t> units = {kTank, kTank, kTrike};
    CHECK(!w->orderCheatReinforce(1, {}, 0));          // nothing to send
    CHECK(!w->orderCheatReinforce(1, units, 0, 99, 5)); // off the map
    CHECK(w->orderCheatReinforce(1, units, 100, -1, -1, 3));  // in 100 ticks, from the west edge
    CHECK(w->orderCheatReinforce(1, {kTrike}, 50));           // cancelled below
    w->tick();  // tick 1: the orders run
    CHECK(w->cheatReinforcements().size() == 2);
    const ScheduledReinforcement r = w->cheatReinforcements()[0];
    CHECK(r.side == 1 && r.due == 101 && r.edge == 3 && r.units.size() == 3);
    CHECK(w->orderCheatCancelReinforcement(w->cheatReinforcements()[1].id));
    auto anyDelivery = [&] {
        for (const auto& d : s.deliveries)
            if (d.active) return true;
        return false;
    };
    int deliveryAt = -1, carryallAt = -1;
    float carryallX = 0;
    while (w->gameTicks() < 2000 && countUnits(s, kTank) + countUnits(s, kTrike) < 3) {
        w->tick();
        if (deliveryAt < 0 && anyDelivery()) {
            deliveryAt = static_cast<int>(w->gameTicks());
            for (const auto& d : s.deliveries)
                if (d.active) CHECK(d.type == 1 && d.entryEdge == 3);
        }
        if (carryallAt < 0 && countUnits(s, kCarry) > 0) {
            carryallAt = static_cast<int>(w->gameTicks());
            for (int i = s.firstUnit; i != -1; i = s.nextObject(i))
                if (s.units[i].type == kCarry) carryallX = static_cast<float>(s.units[i].posX >> 16);
        }
    }
    CHECK(deliveryAt == 101);                                  // exactly when due
    CHECK(carryallAt >= 101 && carryallAt < 101 + 8);          // updateDeliveries: one side per tick
    CHECK(carryallX < 0);                                      // entered from the west edge
    CHECK(countUnits(s, kTank) == 2 && countUnits(s, kTrike) == 1);  // the cancelled trike never came
    CHECK(w->cheatReinforcements().empty() && w->cheatsUsed());
    std::printf("cheats: reinforcement due at 101, delivery %d, carryall %d, landed by %u\n", deliveryAt, carryallAt,
                w->gameTicks());
}

// Rally points (rally.cpp, not original): a unit built at a factory with a rally point drives there.
void testRallyPoint() {
    EcoFixture fx;
    auto w = fx.world(40, 40, kBuildRock);
    Side& s = w->sides()[0];
    s.cash = 5000;
    w->addBuilding(0, kYard, 2, 2, true);
    w->addBuilding(0, kWind, 6, 2, true);
    const int fi = w->addBuilding(0, kHeavy, 10, 10, true);
    const int yi = s.firstBuilding;
    CHECK(w->canHaveRallyPoint(0, fi) && !w->canHaveRallyPoint(0, yi));
    CHECK(!w->canHaveRallyPoint(1, fi));
    CHECK(!w->orderSetRallyPoint(0, yi, 20, 20));  // not a factory
    CHECK(w->orderSetRallyPoint(0, fi, 24, 22));
    CHECK(w->orderBuildUnit(0, kTank));
    w->tick();
    int x = 0, y = 0;
    CHECK(w->rallyPoint(0, fi, x, y) && x == 24 && y == 22);
    int tank = -1;
    for (int t = 0; t < 1500; ++t) {
        w->tick();
        for (int i = s.firstUnit; i != -1 && tank < 0; i = s.nextObject(i))
            if (s.units[i].type == kTank) tank = i;
        if (tank >= 0 && s.units[tank].blockFromX == 24 && s.units[tank].blockFromY == 22 &&
            s.units[tank].state == UnitState::Idle)
            break;
    }
    CHECK(tank >= 0);
    if (tank >= 0) CHECK(s.units[tank].blockFromX == 24 && s.units[tank].blockFromY == 22);
    // Cleared: the next unit stays at the factory exit.
    CHECK(w->orderClearRallyPoint(0, fi));
    w->tick();
    CHECK(!w->rallyPoint(0, fi, x, y));
    std::printf("rally point: tank at (%d,%d) by tick %u\n", tank >= 0 ? s.units[tank].blockFromX : -1,
                tank >= 0 ? s.units[tank].blockFromY : -1, w->gameTicks());
}

// Save/load with a pending reinforcement and a rally point (+ a unit on its way to it).
void testReinforcementSaveRoundTrip() {
    EcoFixture f;
    auto make = [&]() {
        auto w = f.world(40, 40, kBuildRock);
        w->sides()[0].cash = 5000;
        w->addBuilding(0, kYard, 2, 2, true);
        w->addBuilding(0, kWind, 6, 2, true);
        w->addBuilding(0, kHeavy, 10, 10, true);
        w->addBuilding(1, kYard, 30, 30, true);
        return w;
    };
    auto orders = [](World& w, std::uint32_t step) {
        if (step == 5) {
            w.orderCheatReinforce(1, {kTank, kTrike, kTrike}, 150, 20, 30, 2);  // due at tick 156
            w.orderSetRallyPoint(0, w.sides()[0].lastBuilding, 25, 18);
            w.orderBuildUnit(0, kTank);
        }
        if (step == 60) w.orderCheatReinforce(1, {kTank}, 0);  // queued (not executed) at the save
    };
    auto r = saveRoundTrip(make, 60, 600, orders, 50);
    reportRoundTrip("reinforcements", r);
    auto w = make();
    for (std::uint32_t k = 0; k < 60; ++k) {
        orders(*w, k);
        w->tick();
    }
    CHECK(w->cheatReinforcements().size() == 1 && w->cheatReinforcements()[0].due == 156);
    for (std::uint32_t k = 60; k < 1200; ++k) {
        orders(*w, k);
        w->tick();
    }
    CHECK(countUnits(w->sides()[1], kTrike) == 2 && countUnits(w->sides()[1], kTank) == 2);
}

// ---- Community extended event system (game/mission_ext.cpp, docs/extended-events.md) -------
namespace ext {
using Ev = std::array<std::uint8_t, formats::kMisEventSize>;
using Co = std::array<std::uint8_t, formats::kMisConditionSize>;
void put32(std::uint8_t* p, std::int32_t v) { std::memcpy(p, &v, 4); }
Co cond(int type, int side = 0, int arg1 = 0, int arg2 = 0, std::int32_t val1 = 0, std::int32_t val2 = 0,
        std::int32_t val3 = 0) {
    Co c{};
    put32(&c[0], val2);
    put32(&c[4], val1);
    put32(&c[8], val3);
    c[24] = static_cast<std::uint8_t>(side);
    c[25] = static_cast<std::uint8_t>(type);
    c[26] = static_cast<std::uint8_t>(arg1);
    c[27] = static_cast<std::uint8_t>(arg2);
    return c;
}
Ev event(int type, std::initializer_list<int> conds, int side = 0, std::int32_t value = 0, int flags = 0) {
    Ev e{};
    e[13] = static_cast<std::uint8_t>(type);
    e[15] = static_cast<std::uint8_t>(side);
    put32(&e[8], value);
    int n = 0;
    for (int c : conds) {
        const bool neg = c < 0;
        e[19 + n] = static_cast<std::uint8_t>(neg ? -c - 1 : c);
        e[33 + n] = neg ? 1 : 0;
        ++n;
    }
    e[12] = static_cast<std::uint8_t>(n);
    e[46] = static_cast<std::uint8_t>(flags);
    return e;
}
formats::MisFile mission(std::vector<Co> conds, std::vector<Ev> events) {
    formats::MisFile m;
    m.timeLimit = -1;
    m.extended = true;
    m.extendedConditions = static_cast<int>(conds.size());
    m.extendedEvents = static_cast<int>(events.size());
    m.extConditions = std::move(conds);
    m.extEvents = std::move(events);
    return m;
}
}  // namespace ext

void testExtendedEvents() {
    using namespace ext;
    Fixture f;
    // C0 timer ==5, C1 timer ==10, C2 Check Units (side 1, type tank, tag 7, >= 2), C3 flag,
    // C4 Interval (start 2, every 3 ticks, 3 runs), C5 Timer == 20.
    Co checkTanks = cond(14, 1, 0, 2);
    checkTanks[8] = 1;   // criteria 0: type
    checkTanks[16] = kTank;
    checkTanks[9] = 5;   // criteria 1: tag
    checkTanks[17] = 7;  // (and_or 0 = AND)
    std::vector<Co> conds = {cond(3, 0, 0, 2, 0, 5), cond(3, 0, 0, 2, 0, 10), checkTanks, cond(9),
                             cond(2, 0, 0, 0, 2, 3, 3), cond(3, 0, 0, 2, 0, 20)};
    // E0: Unit Spawn 2 tanks for side 1 at (5,5), tag 7, run once.
    Ev spawn = event(18, {0}, 1, 7, 1);
    spawn[0] = 5; spawn[4] = 5; spawn[14] = 2; spawn[17] = 2;  // amount 2, facing 2
    spawn[47] = kTank; spawn[48] = kTank;
    // E1: when the tanks exist: Set Flag C3 = 1 (side byte = condition index).
    Ev setFlag = event(19, {2}, 3, 1, 1);
    // E2: Interval -> Add Unit (trike) for side 2 at (1, 1 + ...): counts the interval runs via tag.
    Ev addTrike = event(23, {4}, 2, 9);  // value = tag 9
    addTrike[0] = 1; addTrike[4] = 1; addTrike[16] = kTrike;
    addTrike[17] = 1;  // movement: random adjacent
    // E3: at tick 10, Set Unit Property tag (offset 14, byte) := 3 on side 1 tanks with tag 7
    // (filter at data+1: criteria type tag == 7), limit 1 (data[0]).
    Ev setTag = event(55, {1}, 1, 3, 1);
    setTag[14] = 0; setTag[16] = 14; setTag[17] = 0;
    setTag[47] = 1;           // limit 1
    setTag[47 + 1 + 8] = 5;   // criteria type: tag
    setTag[47 + 1 + 16] = 7;  // == 7
    // E4: flag set (C3) AND C5 -> Mission Win; E5 blocked lose (never runs); E6 OR test: unblock nothing.
    Ev win = event(10, {3, 5}, 0, 0, 1);
    Ev lose = event(11, {0}, 0, 0, 2);  // blocked from the start
    std::vector<Ev> events = {spawn, setFlag, addTrike, setTag, win, lose};
    auto make = [&]() {
        auto w = f.world(24, 24);
        WorldTestAccess::loadEvents(*w, mission(conds, events));
        return w;
    };
    auto w = make();
    CHECK(w->extendedEvents());
    auto count = [&](int side, int type, int tag) {
        int n = 0;
        const Side& s = w->sides()[side];
        for (int i = s.firstUnit; i != -1; i = s.nextObject(i))
            if (s.units[i].type == type && (tag < 0 || s.units[i].tag == tag)) ++n;
        return n;
    };
    for (int t = 0; t < 4; ++t) w->tick();
    CHECK(count(1, kTank, -1) == 0);
    w->tick();  // tick 5: spawn
    CHECK(count(1, kTank, 7) == 2);
    w->tick();  // tick 6: Check Units sees them -> flag
    for (int t = 0; t < 4; ++t) w->tick();  // tick 10: tag of one tank -> 3
    CHECK(count(1, kTank, 7) == 1 && count(1, kTank, 3) == 1);
    CHECK(w->gameEndState() == GameEnd::None);
    for (int t = 0; t < 10; ++t) w->tick();  // tick 20: win
    CHECK(w->gameEndState() == GameEnd::Won);
    // Interval: start 2 (+1 off-by-one) -> ticks 3, 7, 11 (period 3 + 1), 3 runs.
    CHECK(count(2, kTrike, 9) == 3);
    std::printf("extended events: spawn/check/flag/property/interval/win ok\n");

    // Save round trip with an extended mission running.
    auto orders = [](World&, std::uint32_t) {};
    auto r = saveRoundTrip(make, 7, 40, orders, 5);
    reportRoundTrip("extended events", r);
}

// Data-backed: community missions that need the extended event system (Summers' Solstice
// starts with an empty map; its units come from events). Runs N ticks headlessly and checks
// that events spawn units and that the mission can end.
void testExtendedRealMissions() {
#ifdef D2K_GAME_DATA_DIR
    const std::string data = D2K_GAME_DATA_DIR;
    // choice: buildings the player "builds" at tick 10 (FEYS01: the four buildings the mission
    // asks for, which starts its timers and enemy reinforcements); wipe: side whose forces are
    // destroyed afterwards (-1 = the player) and the expected outcome.
    struct Case { const char* map; const char* mis; int ticks; std::vector<int> choice; int wipe; GameEnd expect; };
    for (const Case& c : {Case{"FEYS01V1.MAP", "_FEYS01V1.MIS", 6000, {11, 17, 35, 73}, -1, GameEnd::Lost},
                          Case{"Coalition of Nobles sequel 02.map", "_COALITION OF NOBLES SEQUEL 02.MIS", 6000, {}, 1,
                               GameEnd::Won}}) {
        const std::string mapPath = formats::findFileNoCase(data + "/Missions", c.map);
        const std::string misPath = formats::findFileNoCase(data + "/Missions", c.mis);
        if (mapPath.empty() || misPath.empty()) {
            std::printf("(community mission %s not found: skipping)\n", c.map);
            continue;
        }
        std::string iniPath = mapPath.substr(0, mapPath.rfind('.')) + ".ini";
        const auto ini = formats::loadMissionIni(iniPath);
        const std::string mods = formats::missionModsDataDir(data, ini);
        auto binFile = [&](const std::string& name) { return formats::findOverlayFile(mods, data, "bin", name); };
        const auto map = formats::loadMap(mapPath);
        const auto mis = formats::loadMis(misPath);
        CHECK(mis.extended && !mis.extEvents.empty());
        auto templates = std::make_unique<formats::Templates>(formats::loadTemplates(binFile("Templates.bin")));
        const auto speed = formats::loadSpeed(binFile("SPEED.BIN"));
        const auto armour = formats::loadArmour(binFile("ARMOUR.BIN"));
        const auto builexp = formats::readFile(binFile("BUILEXP.BIN"));
        const auto circles = formats::readFile(binFile("CIRCLES.BIN"));
        const auto vars = formats::loadVars(binFile("VARS.BIN"));
        const auto tileData = formats::loadTileData(binFile("TILEDATA.BIN"));
        const auto tileAtr = formats::loadTileAtr(binFile(mis.tileAttributes + ".BIN"));
        Rules rules{templates.get(), &speed, &armour, nullptr, &builexp, &vars, &circles};
        const int player = ini.sideId >= 0 ? ini.sideId & 7 : 0;
        World w(map, mis, tileAtr, tileData, rules, player);
        CHECK(w.extendedEvents());
        auto totalUnits = [&] {
            int n = 0;
            for (const auto& s : w.sides())
                for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) ++n;
            return n;
        };
        const int units0 = totalUnits();
        int maxUnits = units0, messages = 0, endTick = -1;
        GameEnd end = GameEnd::None;
        for (int k = 0; k < c.ticks; ++k) {
            if (k == 10)
                for (std::size_t n = 0; n < c.choice.size(); ++n) w.addBuilding(player, c.choice[n], 2 + 4 * int(n), 2, false);
            w.tick();
            messages += static_cast<int>(w.messages().size());
            w.clearMessages();
            maxUnits = std::max(maxUnits, totalUnits());
            if (end == GameEnd::None && w.gameEndState() != GameEnd::None) {
                end = w.gameEndState();
                endTick = k + 1;
            }
        }
        std::printf("community mission %s: units %d at start, max %d, %d messages, player units %s, end %s at tick %d\n",
                    c.map, units0, maxUnits, messages, w.unitsExist(player) ? "yes" : "no",
                    end == GameEnd::Won ? "won" : end == GameEnd::Lost ? "lost" : "none", endTick);
        CHECK(maxUnits > units0);  // events spawned units
        CHECK(messages > 0);
        // Scripted end: the mission's own Mission Win / Mission Fail events must be able to fire.
        // Destroy every unit and building of one side and run until the mission reacts.
        if (end == GameEnd::None) {
            Side& me = w.sides()[c.wipe < 0 ? player : c.wipe];
            for (int i = me.firstBuilding; i != -1;) { const int n = me.nextObject(i); w.destroyBuilding(me.buildings[i], true); i = n; }
            for (int i = me.firstUnit; i != -1;) { const int n = me.nextObject(i); w.destroyUnit(me.units[i]); i = n; }
            for (int k = 0; k < 2000 && w.gameEndState() == GameEnd::None; ++k) w.tick();
            std::printf("  after destroying side %d's forces: %s\n", c.wipe < 0 ? player : c.wipe,
                        w.gameEndState() == GameEnd::Lost ? "lost" : w.gameEndState() == GameEnd::Won ? "won" : "no end");
            CHECK(w.gameEndState() == c.expect);
        }
    }
#endif
}

// D2K_EXT_SCAN=1 (with D2K_TEST_EXT_ONLY): every extended community mission, 3000 ticks
// headless; prints units / messages / early end per mission (a smoke scan, no checks).
void scanExtendedMissions() {
#ifdef D2K_GAME_DATA_DIR
    namespace fs = std::filesystem;
    const std::string data = D2K_GAME_DATA_DIR;
    std::vector<std::string> files;
    for (const auto& e : fs::directory_iterator(data + "/Missions"))
        if (e.path().extension() == ".MIS" && fs::file_size(e.path()) > 68066) files.push_back(e.path().string());
    std::sort(files.begin(), files.end());
    for (const auto& misPath : files) {
        const std::string stem = fs::path(misPath).stem().string().substr(1);
        const std::string mapPath = formats::findFileNoCase(data + "/Missions", stem + ".map");
        if (mapPath.empty()) continue;
        const auto ini = formats::loadMissionIni(mapPath.substr(0, mapPath.rfind('.')) + ".ini");
        const std::string mods = formats::missionModsDataDir(data, ini);
        auto binFile = [&](const std::string& name) { return formats::findOverlayFile(mods, data, "bin", name); };
        try {
            const auto map = formats::loadMap(mapPath);
            const auto mis = formats::loadMis(misPath);
            auto templates = std::make_unique<formats::Templates>(formats::loadTemplates(binFile("Templates.bin")));
            const auto speed = formats::loadSpeed(binFile("SPEED.BIN"));
            const auto armour = formats::loadArmour(binFile("ARMOUR.BIN"));
            const auto builexp = formats::readFile(binFile("BUILEXP.BIN"));
            const auto circles = formats::readFile(binFile("CIRCLES.BIN"));
            const auto vars = formats::loadVars(binFile("VARS.BIN"));
            const auto tileData = formats::loadTileData(binFile("TILEDATA.BIN"));
            const auto tileAtr = formats::loadTileAtr(binFile(mis.tileAttributes + ".BIN"));
            Rules rules{templates.get(), &speed, &armour, nullptr, &builexp, &vars, &circles};
            const int player = ini.sideId >= 0 ? ini.sideId & 7 : 0;
            World w(map, mis, tileAtr, tileData, rules, player);
            auto count = [&] { int n = 0; for (const auto& sd : w.sides()) for (int i = sd.firstUnit; i != -1; i = sd.nextObject(i)) ++n; return n; };
            const int u0 = count();
            int maxU = u0, msgs = 0, endT = -1;
            for (int k = 0; k < 3000; ++k) {
                w.tick();
                msgs += int(w.messages().size());
                w.clearMessages();
                maxU = std::max(maxU, count());
                if (endT < 0 && w.gameEndState() != GameEnd::None) endT = k + 1;
            }
            std::printf("scan %-36s events %4d units %3d -> max %3d, msgs %3d, player %s, end %s@%d\n", stem.c_str(),
                        mis.extendedEvents, u0, maxU, msgs, w.unitsExist(player) || w.buildingsExist(player) ? "alive" : "gone",
                        w.gameEndState() == GameEnd::Won ? "won" : w.gameEndState() == GameEnd::Lost ? "lost" : "-", endT);
        } catch (const std::exception& ex) {
            std::printf("scan %-36s ERROR %s\n", stem.c_str(), ex.what());
        }
    }
#endif
}

// ---- Extended unit queue (not original; re/notes/economy.md §10) ----------------------------
// EcoFixture with the trike moved into the heavy factory group (cost 300, ~12 ticks per unit),
// so the tank (700, 24 ticks) and the trike share one factory queue.
struct AutoQueueFixture : EcoFixture {
    AutoQueueFixture() {
        auto& t = templates->bin.units[kTrike];
        t.ownerHouse = 0xff;
        t.cost = 300;
        t.buildSpeed = 2000;
        t.prereq1BuildingGroup = kGrpHeavy;
        t.prereq1OwnerHouse = 0xff;
        t.prereq2BuildingGroup = -1;
    }
    std::unique_ptr<World> base(int& factory) const {
        auto w = world(40, 40, kBuildRock);
        w->sides()[0].cash = 50000;
        w->addBuilding(0, kYard, 2, 2, true);
        w->addBuilding(0, kWind, 6, 2, true);
        factory = w->addBuilding(0, kHeavy, 10, 10, true);
        w->tick();
        return w;
    }
};

// Ticks and returns the types of the units that left the factory, in order; each new unit is
// sent away so that the exit stays free.
std::vector<int> autoBuilt(World& w, int ticks, std::size_t want = 1000) {
    std::vector<int> out;
    Side& s = w.sides()[0];
    std::vector<bool> seen(kObjectsPerSide, false);
    for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) seen[std::size_t(i)] = true;
    for (int t = 0; t < ticks && out.size() < want; ++t) {
        w.tick();
        for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) {
            if (!seen[std::size_t(i)]) {
                seen[std::size_t(i)] = true;
                out.push_back(s.units[i].type);
            }
            if (s.units[i].state == UnitState::Idle && s.units[i].blockFromY < 18)
                w.orderMove(0, i, 2 + (i % 30), 20 + ((i / 30) % 8) * 2);
        }
    }
    return out;
}

void testAutoQueueCounts() {
    AutoQueueFixture fx;
    int fi = 0;
    auto w = fx.base(fi);
    CHECK(w->orderAutoAdd(0, kTank));
    CHECK(w->orderAutoAdd(0, kTank));
    CHECK(w->orderAutoAdd(0, kTank));
    CHECK(!w->orderAutoAdd(0, kMcv));  // not on the sidebar
    w->tick();
    AutoQueueState st = w->autoQueueFor(0, kTank);
    CHECK(st.count == 3 && !st.infinite && st.position == 0);
    CHECK(w->unitQueueFor(0, kTank).active);  // the first one started
    CHECK(w->orderAutoAdd(0, kTank, 10));     // Shift+click
    w->tick();
    CHECK(w->autoQueueFor(0, kTank).count == 13);
    CHECK(w->orderAutoRemove(0, kTank));      // Ctrl+right: one fewer
    w->tick();
    CHECK(w->autoQueueFor(0, kTank).count == 12);
    CHECK(w->orderAutoAdd(0, kTank, 500));    // capped
    w->tick();
    CHECK(w->autoQueueFor(0, kTank).count == kAutoMaxCount);
    CHECK(w->orderAutoInfinite(0, kTank));    // right click: infinite
    w->tick();
    st = w->autoQueueFor(0, kTank);
    CHECK(st.infinite && st.count == 1);
    CHECK(w->orderAutoAdd(0, kTank));         // no effect on an infinite type
    w->tick();
    CHECK(w->autoQueueFor(0, kTank).infinite);
    // Shift+click = ten single clicks (same state afterwards).
    int f1 = 0, f2 = 0;
    auto a = fx.base(f1), b = fx.base(f2);
    a->orderAutoAdd(0, kTrike, 10);
    for (int i = 0; i < 10; ++i) b->orderAutoAdd(0, kTrike);
    for (int k = 0; k < 50; ++k) { a->tick(); b->tick(); }
    CHECK(a->autoQueueFor(0, kTrike).count == b->autoQueueFor(0, kTrike).count);
    CHECK(a->autoQueueFor(0, kTrike).count > 0 && a->autoQueueFor(0, kTrike).count < 10);
    CHECK(a->credits(0) == b->credits(0) && a->sides()[0].autoQueue.size() == 1);
    std::printf("auto queue: counts ok (tank %d, trike after 50 ticks %d)\n", w->autoQueueFor(0, kTank).count,
                a->autoQueueFor(0, kTrike).count);
}

void testAutoQueueOrder() {
    AutoQueueFixture fx;
    auto seq = [](const std::vector<int>& v) {
        std::string r;
        for (int t : v) r += t == kTank ? 'K' : t == kTrike ? 'T' : '?';
        return r;
    };
    {  // FIFO across types: 2 tanks chosen first, then 2 trikes (then one more tank click)
        int fi = 0;
        auto w = fx.base(fi);
        w->orderAutoAdd(0, kTank);
        w->orderAutoAdd(0, kTrike);
        w->orderAutoAdd(0, kTrike);
        w->orderAutoAdd(0, kTank);  // adds to the tank entry (keeps its place)
        const std::string r = seq(autoBuilt(*w, 2000, 4));
        CHECK(r == "KKTT");
        CHECK(w->sides()[0].autoQueue.empty());
        std::printf("auto queue: FIFO %s\n", r.c_str());
    }
    {  // two infinite types alternate
        int fi = 0;
        auto w = fx.base(fi);
        w->orderAutoInfinite(0, kTank);
        w->orderAutoInfinite(0, kTrike);
        const std::string r = seq(autoBuilt(*w, 3000, 6));
        CHECK(r == "KTKTKT");
        std::printf("auto queue: two infinite %s\n", r.c_str());
    }
    {  // infinite first, then a finite type: the finite one is finished, then the infinite resumes
        int fi = 0;
        auto w = fx.base(fi);
        w->orderAutoInfinite(0, kTank);
        w->orderAutoAdd(0, kTrike, 2);
        const std::string r = seq(autoBuilt(*w, 3000, 6));
        CHECK(r == "KTTKKK");
        std::printf("auto queue: mixed %s\n", r.c_str());
    }
    {  // finite first, then infinite: the finite count is used up first
        int fi = 0;
        auto w = fx.base(fi);
        w->orderAutoAdd(0, kTrike, 3);
        w->orderAutoInfinite(0, kTank);
        const std::string r = seq(autoBuilt(*w, 3000, 5));
        CHECK(r == "TTTKK");
        std::printf("auto queue: finite then infinite %s\n", r.c_str());
    }
}

void testAutoQueueCancel() {
    AutoQueueFixture fx;
    {  // right click on an infinite type cancels it: refund progress * cost / 0x5a00
        int fi = 0;
        auto w = fx.base(fi);
        const int c0 = w->credits(0);
        w->orderAutoInfinite(0, kTank);
        for (int k = 0; k < 9; ++k) w->tick();
        const QueueState q = w->unitQueueFor(0, kTank);
        CHECK(q.active && q.paid > 0);
        w->orderAutoInfinite(0, kTank);
        for (int k = 0; k < 100; ++k) w->tick();
        CHECK(!w->unitQueueFor(0, kTank).active && w->sides()[0].autoQueue.empty());
        CHECK(w->credits(0) == c0 - q.paid + q.progress * 700 / 0x5a00);
    }
    {  // queued-but-unstarted units cost nothing; removing the last one cancels with a refund
        int fi = 0;
        auto w = fx.base(fi);
        const int c0 = w->credits(0);
        w->orderAutoAdd(0, kTank, 5);
        for (int k = 0; k < 6; ++k) w->tick();
        for (int i = 0; i < 4; ++i) w->orderAutoRemove(0, kTank);
        w->tick();
        CHECK(w->autoQueueFor(0, kTank).count == 1 && w->unitQueueFor(0, kTank).active);
        const QueueState q = w->unitQueueFor(0, kTank);
        const Side& s0 = w->sides()[0];  // (payments reach cash/spice through the drips)
        CHECK(s0.cash + s0.spice + s0.cashDrip + s0.spiceDrip == c0 - q.paid);  // only the unit in production is paid
        w->orderAutoRemove(0, kTank);
        for (int k = 0; k < 100; ++k) w->tick();
        CHECK(!w->unitQueueFor(0, kTank).active && w->autoQueueFor(0, kTank).count == 0);
        CHECK(w->credits(0) == c0 - q.paid + q.progress * 700 / 0x5a00);
    }
    {  // Ctrl+right on an infinite type: the unit in production is finished, then it stops
        int fi = 0;
        auto w = fx.base(fi);
        w->orderAutoInfinite(0, kTrike);
        w->tick();
        w->tick();
        w->orderAutoRemove(0, kTrike);
        const auto built = autoBuilt(*w, 400);
        CHECK(built.size() == 1 && w->sides()[0].autoQueue.empty() && !w->unitQueueFor(0, kTrike).active);
    }
    {  // no money: production waits (progressive payment stalls), resumes with credits
        int fi = 0;
        auto w = fx.base(fi);
        Side& s = w->sides()[0];
        s.cash = 0;
        s.spice = 0;
        w->orderAutoAdd(0, kTrike, 2);
        for (int k = 0; k < 50; ++k) w->tick();
        CHECK(w->unitQueueFor(0, kTrike).active && w->unitQueueFor(0, kTrike).progress == 0);
        CHECK(!w->unitQueueFor(0, kTrike).onHold && w->autoQueueFor(0, kTrike).count == 2);
        s.cash = 1000;
        CHECK(autoBuilt(*w, 400).size() == 2);
    }
    {  // factory destroyed: the group's queue is cleared and nothing more is paid
        int fi = 0;
        auto w = fx.base(fi);
        w->orderAutoAdd(0, kTank, 5);
        w->orderAutoInfinite(0, kTrike);
        for (int k = 0; k < 5; ++k) w->tick();
        w->destroyBuilding(w->sides()[0].buildings[fi], true);
        for (int k = 0; k < 400; ++k) w->tick();
        const int c1 = w->credits(0);
        for (int k = 0; k < 200; ++k) w->tick();
        CHECK(w->sides()[0].autoQueue.empty() && w->unitQueues(0).empty() && w->credits(0) == c1);
    }
    std::printf("auto queue: cancel / refunds / no money / factory lost ok\n");
}

void testAutoQueueSaveRoundTrip() {
    AutoQueueFixture f;
    auto make = [&]() {
        int fi = 0;
        return f.base(fi);
    };
    auto orders = [](World& w, std::uint32_t step) {
        if (step == 3) {
            w.orderAutoAdd(0, kTank, 3);
            w.orderAutoInfinite(0, kTrike);
        }
        if (step == 70) w.orderAutoAdd(0, kTank, 10);  // pending at the save point
        if (step == 500) w.orderAutoRemove(0, kTank);
        if (step == 900) w.orderAutoInfinite(0, kTrike);  // cancel
        if (step == 1000) w.orderAutoInfinite(0, kTank);
        // send the finished units away so that the factory exit stays free
        Side& s = w.sides()[0];
        if (step % 20 == 0)
            for (int i = s.firstUnit, k = 0; i != -1; i = s.nextObject(i), ++k)
                if (s.units[i].state == UnitState::Idle) w.orderMove(0, i, 2 + k % 30, 20 + (k / 30) * 2);
    };
    reportRoundTrip("auto queue", saveRoundTrip(make, 70, 1500, orders, 50));
}

// "Play as any side" (not original; docs/play-as.md): Atreides mission 9a played as the
// Emperor's Sardaukar (side 3). The chosen side has no AI, the Atreides (side 0) get one from
// their .MIS block and play; outcome rules; save/load round trip.
void testPlayAs() {
#ifdef D2K_GAME_DATA_DIR
    const std::string data = D2K_GAME_DATA_DIR, bin = data + "/bin";
    const std::string mapPath = formats::findFileNoCase(data + "/Missions", "A9V1.MAP");
    const std::string misPath = formats::findFileNoCase(data + "/maps", "_A9V1.MIS");
    if (mapPath.empty() || misPath.empty() || formats::findFileNoCase(bin, "ARMOUR.BIN").empty()) {
        std::printf("(game data not found: play-as test skipped)\n");
        return;
    }
    auto templates = std::make_unique<formats::Templates>(formats::loadTemplates(formats::findFileNoCase(bin, "Templates.bin")));
    const auto speed = formats::loadSpeed(formats::findFileNoCase(bin, "SPEED.BIN"));
    const auto armour = formats::loadArmour(formats::findFileNoCase(bin, "ARMOUR.BIN"));
    const auto builexp = formats::readFile(formats::findFileNoCase(bin, "BUILEXP.BIN"));
    const auto circles = formats::readFile(formats::findFileNoCase(bin, "CIRCLES.BIN"));
    const auto vars = formats::loadVars(formats::findFileNoCase(bin, "VARS.BIN"));
    const auto tileData = formats::loadTileData(formats::findFileNoCase(bin, "TILEDATA.BIN"));
    const Rules rules{templates.get(), &speed, &armour, nullptr, &builexp, &vars, &circles};
    const auto map = formats::loadMap(mapPath);
    const auto mis = formats::loadMis(misPath);
    const auto tileAtr = formats::loadTileAtr(formats::findFileNoCase(bin, mis.tileAttributes + ".BIN"));
    constexpr int kAtreides = 0, kSardaukar = 3;
    CHECK(mis.houseId[kSardaukar] == 3);
    const auto counts = sideObjectCounts(map, tileData);
    CHECK(counts[kAtreides] > 0 && counts[kSardaukar] > 0);

    // Normal start: unchanged.
    {
        World w(map, mis, tileAtr, tileData, rules, kAtreides);
        CHECK(!w.playAs() && w.scriptSide() == kAtreides);
        CHECK(!w.ai(kAtreides).enabled() && w.ai(kSardaukar).enabled());
        World w2(map, mis, tileAtr, tileData, rules, kAtreides, kAtreides);  // same side = normal
        CHECK(!w2.playAs() && worldStateHash(w) == worldStateHash(w2));
    }
    auto make = [&]() { return std::make_unique<World>(map, mis, tileAtr, tileData, rules, kSardaukar, kAtreides); };
    {
        auto w = make();
        CHECK(w->playAs() && w->mySide() == kSardaukar && w->scriptSide() == kAtreides);
        CHECK(!w->ai(kSardaukar).enabled() && w->ai(kAtreides).enabled());
        // The former player's starting units guard (computer side), the chosen side's stay idle.
        const Side& at0 = w->sides()[kAtreides];
        const int built0 = at0.unitsBuilt + at0.buildingsBuilt;
        int busyMax = 0;
        for (int t = 0; t < 6000 && w->gameEndState() == GameEnd::None; ++t) {
            w->tick();
            if (t % 100 == 0) {
                int busy = 0;
                for (int i = at0.firstUnit; i != -1; i = at0.nextObject(i))
                    busy += at0.units[i].state != UnitState::Idle && at0.units[i].state != UnitState::Guarding;
                busyMax = std::max(busyMax, busy);
            }
        }
        const int built = at0.unitsBuilt + at0.buildingsBuilt - built0;
        std::printf("play as: A9V1 as the Sardaukar, %u ticks: Atreides AI built %d, up to %d units busy, end state %d\n",
                    w->gameTicks(), built, busyMax, int(w->gameEndState()));
        CHECK(built > 0 || busyMax > 0);  // the Atreides AI acts
    }
    // Mission Win / Fail events decide for the Atreides: inverted for their enemy.
    {
        auto w = make();
        WorldTestAccess::missionOutcome(*w, true);  // "Mission Win" (Atreides succeeded)
        CHECK(w->gameEndState() == GameEnd::Lost);
        auto w2 = make();
        WorldTestAccess::missionOutcome(*w2, false);  // "Mission Fail"
        CHECK(w2->gameEndState() == GameEnd::Won);
    }
    // Elimination: nothing left -> lost; no hostile side left -> won.
    {
        auto w = make();
        WorldTestAccess::checkPlayAsEnd(*w);
        CHECK(w->gameEndState() == GameEnd::None);
        WorldTestAccess::setExists(*w, kSardaukar, false, false);
        WorldTestAccess::checkPlayAsEnd(*w);
        CHECK(w->gameEndState() == GameEnd::Lost);
        auto w2 = make();
        WorldTestAccess::checkPlayAsEnd(*w2);  // sees the hostile sides
        for (int s = 0; s < kMaxSides; ++s)
            if (s != kSardaukar && (w2->diplomacy(kSardaukar, s) == 1 || w2->diplomacy(s, kSardaukar) == 1))
                WorldTestAccess::setExists(*w2, s, false, false);
        WorldTestAccess::checkPlayAsEnd(*w2);
        CHECK(w2->gameEndState() == GameEnd::Won);
    }
    reportRoundTrip("A9V1 played as the Sardaukar",
                    saveRoundTrip(make, 2000, 3000, [](World& w, std::uint32_t step) { scriptedOrders(w, 3, step); }));
#endif
}

// Time of day / Modern lighting (render/lighting.hpp): pure, render-only model. Classic Day must be
// the identity (the old look); the Cycle starts and ends on Day and passes through Night.
void testLighting() {
    using namespace d2k::render;
    const SceneLight id = sceneLight(TimeOfDay::Day, LightingStyle::Classic);
    CHECK(id.identity && id.lamps == 0 && id.bloom == 0 && id.sat == 1);
    const float base[3] = {-0.5417f, 0.6428f, -0.5417f};
    float out[3];
    id.sunDirection(base, out);
    CHECK(out[0] == base[0] && out[1] == base[1] && out[2] == base[2]);
    float g[3];
    id.grade(0.62f, 0.55f, 0.64f, g);
    CHECK(g[0] == 1 && g[1] == 1 && g[2] == 1);
    const SceneLight night = sceneLight(TimeOfDay::Night, LightingStyle::Modern);
    CHECK(!night.identity && night.lamps == 1 && night.bloom > 0);
    night.grade(0.62f, 0.55f, 0.8f, g);
    CHECK(g[2] > g[0] && g[0] < 0.5f);  // cool and dark
    const SceneLight dusk = sceneLight(TimeOfDay::Dusk, LightingStyle::Classic);
    CHECK(dusk.bloom == 0);  // Classic: no bloom
    dusk.sunDirection(base, out);
    CHECK(out[1] < base[1] && out[1] >= std::sin(0.349f) - 1e-4f);  // lower sun, never below 20 degrees
    const double len = cycleLength();
    const SceneLight c0 = sceneLight(TimeOfDay::Cycle, LightingStyle::Classic, 0.0);
    CHECK(c0.identity);
    const SceneLight cn = sceneLight(TimeOfDay::Cycle, LightingStyle::Modern, len * 0.7);
    CHECK(cn.lamps == 1);
    const SceneLight lite = sceneLight(TimeOfDay::Night, LightingStyle::Modern, 0, true);
    CHECK(lite.lamps == 0 && lite.bloom == 0);
    CHECK(timeOfDayFrom(timeOfDayName(TimeOfDay::Dawn)) == TimeOfDay::Dawn);
}

int main() {
    if (std::getenv("D2K_TEST_EXT_ONLY")) {  // quick run of the extended-event tests
        if (std::getenv("D2K_EXT_SCAN")) { scanExtendedMissions(); return 0; }
        testCrushInfantry();
        testExtendedEvents();
        testExtendedRealMissions();
        std::printf("%s\n", failures ? "FAILED" : "extended tests passed");
        return failures ? 1 : 0;
    }
    testRandom();
    testLighting();
    testGeometry();
    testObjectPool();
    testSpawnFields();
    testSpeed();
    testInfantrySubcells();
    testFindPath();
    testVehicleMove();
    testBlockedTarget();
    testBoxedInNearGoal();
    testInfantryAnimation();
    testSinCosTable();
    testAttackUnitKills();
    testBulletFlight();
    testTurretEngages();
    testInfantryDeath();
    testBuildingDeathSequence();
    testCrushInfantry();
    testLeavingBuildingInfantryDamage();
    testSmartOrder();
    testForceMoveCrush();
    testTurretForceAttack();
    testGuardRetreat();
    testRealMission();
    testRealCombatSoak();
    testHarvestCell();
    testUnloadCredits();
    testStorageCap();
    testSpendAndDrip();
    testPowerBalance();
    testUnitProduction();
    testBuildingPlacement();
    testSellAndRepair();
    testMcvDeploy();
    testDeployRules();
    testFreeHarvesterDelivery();
    testStarportPurchase();
    testStarportPrices();
    testStarportLost();
    testAirStrike();
    testAiStarport();
    testStarportSoak();
    testRealEconomy();
    testShroudReveal();
    testShroudUnitSight();
    testMissionTimerMessage();
    testMissionWinWhenEnemyDestroyed();
    testSoundQueue();
    testSoundPositional();
    testSoundEvents();
    testSkirmishDefaultRules();
    testSkirmishSpawn();
    testAiSoak();
    testBuildingSkirt();
    testSandworm();
    testSpiceBloom();
    testStealthCloak();
    testRepairPad();
    testDeviator();
    testAuditSoak();
    testSaveRoundTrip();
    testCampaignScore();
    testCheatCredits();
    testCheatTechAndOverrides();
    testCheatDelivery();
    testCheatSpawn();
    testCheatSaveRoundTrip();
    testCheatReinforcements();
    testRallyPoint();
    testReinforcementSaveRoundTrip();
    testAutoQueueCounts();
    testAutoQueueOrder();
    testAutoQueueCancel();
    testAutoQueueSaveRoundTrip();
    testExtendedEvents();
    testExtendedRealMissions();
    testPlayAs();
    std::printf("%s\n", failures ? "FAILED" : "all game tests passed");
    return failures ? 1 : 0;
}

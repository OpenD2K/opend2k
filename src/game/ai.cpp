// The computer player (CAI, "AI.CPP" in the original; re/notes/ai.md).
//
// Entry points (World members): InitAI FUN_0041af50 (+ the .MIS AI segment copy done by
// Mission__LoadObjectives), ActivateAI FUN_0041b250 (Game_Init), DoAI FUN_0041b610 (main loop,
// every tick, before Mission__CheckEvents). Everything else lives in AiController below, one
// method per original function (names from the original's debug-trace strings "Xxx(AI)").
//
// State: the 7608-byte CAI object is kept raw (AiState) and accessed at the original offsets.
// Groups: 32 x 0x2c bytes at +0x2c (see re/notes/ai.md for the field map).
//
// Faithfulness notes:
//  - RNG calls (rand via GetRandomValue 0x448460) happen exactly where the original makes them.
//  - Float math: the original runs the x87 FPU in the MSVC default 53-bit mode, so double
//    arithmetic reproduces it as long as values the original stores to float variables are
//    rounded to float at the same points (done below). __ftol truncates and returns the low
//    32 bits of the 64-bit result (x87Ftol). Comparisons follow the FPU flag tests used by the
//    original (an unordered result counts as "less" for C0 tests and "equal" for C3 tests).
//  - "Generate*Order" calls of the original act on the side's selected units (Unit +0x19,
//    used by the AI as scratch) and are queued like the player's orders (World::orders_);
//    the guard order (0x18), which the model's order list does not have, is executed
//    directly (ProcessOrder case 5 semantics).

#include "game/geometry.hpp"
#include "game/tables.hpp"
#include "game/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace d2k::game {

// ===========================================================================================
// InitAI / ActivateAI / DoAI
// ===========================================================================================

namespace {

constexpr std::size_t kGroups = 0x2c;     // first group
constexpr std::size_t kGroupSize = 0x2c;  // AIGroup size
// AIGroup field offsets.
constexpr int gTargetSide = 0x00, gTargetIndex = 0x02, gTime = 0x08, gStrength = 0x0c, gStrength0 = 0x10,
              gTries = 0x14, gCounter = 0x18, gTask = 0x1c, gSub = 0x1d, gFlags = 0x20, gX = 0x24, gY = 0x25,
              gUsed = 0x26, gMode = 0x27, gArea = 0x28;
// Defend areas: 20 bytes each at +0x1d54 (minX, maxX, minY, maxY, ..., strength +8, interval +0xc).
constexpr std::size_t kAreas = 0x1d54, kAreaSize = 0x14;
// Initial building positions (StructForCAI, 16 bytes) at +0x5ac, count at +0xf0c.
constexpr std::size_t kInitPos = 0x5ac;

// Group tasks (eTaskType).
constexpr int kTaskNewUnits = 1, kTaskBase = 2, kTaskProtect = 3, kTaskAttackBuilding = 5, kTaskAttackUnit = 6,
              kTaskApproacher = 7, kTaskReturn = 10, kTaskRepair = 11, kTaskDelivery = 13, kTaskHunt = 14,
              kTaskDefendArea = 15, kTaskEngineers = 16, kTaskSaboteur = 18;

// Max group size per task before MonitorGroups splits it (0x4be350).
constexpr int kMaxGroupSize[19] = {200, 400, 60, 30, 30, 20, 30, 30, 60, 60, 40, 20, 60, 60, 20, 500, 20, 10, 10};

// kSpiralDx / kSpiralDy / kSpiralLegInc (0x4eb2d8).
constexpr int kSpiralDx[4] = {-1, 0, 1, 0}, kSpiralDy[4] = {0, 1, 0, -1}, kSpiralLegInc[4] = {0, 1, 0, 1};

constexpr float kF001 = 0.01f;  // 0x4be3bc
// UnknownNetVar2 0x4e3ae8: Model__ResetVars -> FUN_0045a7a0 sets it to DAT_004e3b24 (= 2) for
// GameType 0/1 (campaign / skirmish); 40/75 are the network values.
constexpr int kUnknownNetVar2 = 2;

// __ftol 0x4b1840: truncate to int64 and return the low 32 bits.
std::uint32_t x87Ftol(double v) {
    if (!(v > -9.2e18 && v < 9.2e18)) return 0;  // integer indefinite 0x8000000000000000
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(v)));
}
// FPU compare flags of "fcom st, mem": C0 (st < mem or unordered), C3 (equal or unordered).
bool fpuC0(double st, double m) { return std::isnan(st) || std::isnan(m) || st < m; }
bool fpuC3(double st, double m) { return std::isnan(st) || std::isnan(m) || st == m; }

// Tile of a 16.16 unit position as the original computes it (C division toward zero).
int posTile(std::int32_t p) { return static_cast<int>(static_cast<std::int64_t>(p) / 0x200000); }

}  // namespace

struct AiController {
    World& w;
    int s;
    AiState& a;
    Side& S;
    const formats::TemplatesBin& T;

    AiController(World& world, int side)
        : w(world), s(side), a(world.ai_[side & 7]), S(world.sides_[side & 7]), T(world.rules_.templates->bin) {}

    // ---- raw CAI access (writes past the 7608 bytes spill into the next side's CAI object, as
    // in the original's contiguous gAIArray; past side 7 they are dropped) ----
    std::uint8_t* p(std::ptrdiff_t off) {
        static std::uint8_t sink[8];
        int sd = s;
        while (off >= static_cast<std::ptrdiff_t>(kAiSize)) { off -= kAiSize; ++sd; }
        while (off < 0) { off += kAiSize; --sd; }
        if (sd < 0 || sd >= kMaxSides || off + 4 > static_cast<std::ptrdiff_t>(kAiSize)) {
            std::memset(sink, 0, sizeof sink);
            return sink;
        }
        return w.ai_[sd].raw.data() + off;
    }
    std::uint8_t B(std::ptrdiff_t off) { return *p(off); }
    void SB(std::ptrdiff_t off, int v) { *p(off) = static_cast<std::uint8_t>(v); }
    std::uint32_t D(std::ptrdiff_t off) { std::uint32_t v; std::memcpy(&v, p(off), 4); return v; }
    void SD(std::ptrdiff_t off, std::uint32_t v) { std::memcpy(p(off), &v, 4); }
    float F(std::ptrdiff_t off) { float v; std::memcpy(&v, p(off), 4); return v; }
    std::uint16_t W(std::ptrdiff_t off) { std::uint16_t v; std::memcpy(&v, p(off), 2); return v; }
    void SW(std::ptrdiff_t off, int v) { const std::uint16_t x = static_cast<std::uint16_t>(v); std::memcpy(p(off), &x, 2); }
    // Group g (signed: the original indexes with (char) in one place).
    static std::ptrdiff_t go(int g) { return static_cast<std::ptrdiff_t>(kGroups) + g * static_cast<std::ptrdiff_t>(kGroupSize); }
    std::uint8_t gB(int g, int f) { return B(go(g) + f); }
    void gSB(int g, int f, int v) { SB(go(g) + f, v); }
    std::uint32_t gD(int g, int f) { return D(go(g) + f); }
    void gSD(int g, int f, std::uint32_t v) { SD(go(g) + f, v); }
    std::uint16_t gW(int g, int f) { return W(go(g) + f); }
    void gSW(int g, int f, int v) { SW(go(g) + f, v); }

    std::uint8_t side() const { return static_cast<std::uint8_t>(s); }
    std::uint32_t credits() const { return static_cast<std::uint32_t>(S.spice + S.cash); }
    std::uint8_t dipl(int other) const { return w.diplomacy_[s & 7][other & 7]; }
    const formats::UnitTemplate& ut(const Unit& u) const { return T.units[u.type]; }
    const formats::BuildingTemplate& bt(int type) const { return T.buildings[type & 0xff]; }
    std::uint8_t gid(int i) const { return static_cast<std::uint8_t>(T.groupIds[i]); }
    std::uint8_t groupCount(int g) const { return static_cast<std::uint8_t>(S.buildingsExistPerGroup[g & 0xff]); }
    std::int16_t primary(int k) const { return static_cast<std::int16_t>(S.primaryBuilding[k]); }
    std::uint32_t rnd() { return static_cast<std::uint32_t>(w.rng_.next()); }
    template <class Fn> void forUnits(Fn fn) {
        for (int i = S.firstUnit; i != -1;) { const int n = S.nextObject(i); fn(S.units[i]); i = n; }
    }
    template <class Fn> void forBuildings(Fn fn) {
        for (int i = S.firstBuilding; i != -1;) { const int n = S.nextObject(i); fn(S.buildings[i]); i = n; }
    }
    // Random factor 1 + (rp - rand % 2rp) / 100 with the original's unsigned wrap (fildll of
    // a zero-extended dword): a negative offset becomes ~4.29e9.
    double randFactorUnsigned(int rp, int line) {
        (void)line;
        const std::uint32_t r = rnd();
        const std::uint32_t m = static_cast<std::uint32_t>(rp) * 2;
        const std::uint32_t v = static_cast<std::uint32_t>(rp) - (m ? r % m : 0);
        return static_cast<double>(v) * static_cast<double>(kF001) - -1.0;
    }

    // Object kind of a side slot, the original's ObjectType (+0x90): 1 unit, 2 building.
    int kindOf(int sd, int idx) const {
        if (sd < 0 || sd >= kMaxSides || idx < 0 || idx >= kObjectsPerSide) return 0;
        return static_cast<int>(w.sides_[sd].links[idx].kind);
    }

    // ---------------- selection helpers ----------------
    // GetSelected 0x41cc20: remember (and unselect) a unit of ours the player has selected.
    void getSelected() {
        SD(0x1d40, 0xffffffff);
        forUnits([&](Unit& u) {
            if (u.selected && D(0x1d40) == 0xffffffff) {  // (second one: "More than one enemy unit selected")
                SD(0x1d40, static_cast<std::uint32_t>(u.index));
                u.selected = false;
            }
        });
    }
    // ResetSelected 0x41cc90.
    void resetSelected() {
        if (D(0x1d40) != 0xffffffff) {
            Unit& u = S.units[D(0x1d40) % kObjectsPerSide];
            if (u.state != UnitState::Dead) u.selected = true;
            SD(0x1d40, 0xffffffff);
        }
    }
    // SelectedGroup 0x41cce0.
    bool selectGroup(int g, bool includeAttacking) {
        bool any = false;
        forUnits([&](Unit& u) {
            if (u.groupId == static_cast<std::uint8_t>(g) && u.state != UnitState::LeavingBuilding &&
                (includeAttacking || !(u.flags & 0x100000)) && u.state != UnitState::Dead) {
                any = true;
                u.selected = true;
            }
        });
        return any;
    }
    // SelectUnit 0x41cd50.
    bool selectUnit(int idx) {
        if ((idx & 0xffff) == 0xffff) return false;
        Unit& u = S.units[idx % kObjectsPerSide];
        if (u.state != UnitState::Dead) u.selected = true;
        return true;
    }
    // ClearUnits 0x41cdb0.
    void clearUnits() { forUnits([](Unit& u) { u.selected = false; }); }

    // ---------------- orders (Generate*Order) ----------------
    void moveOrder(int x, int y) {  // GenerateUnitMoveOrder 0x459xxx (order 0x13)
        x &= 0xff;
        y &= 0xff;
        if (x >= w.width_ || y >= w.height_) return;  // DebugFatal "Invalid targetX/Y"
        int n = 0;
        forUnits([&](Unit& u) {
            if (n < 64 && u.selected && u.state != UnitState::Dead) {
                ++n;
                w.orders_.push_back({World::Order::Move, s, u.index, x, y});
            }
        });
    }
    void attackUnitOrder(int tside, int tidx) {  // GenerateUnitAttackUnitOrder 0x4596f0 (0x14)
        tside &= 0xff;
        tidx &= 0xffff;
        if (kindOf(tside, tidx) != 1) return;  // DebugFatal "Enemy is not a valid unit"
        if (w.sides_[tside].units[tidx].flags & unit_flags::kExcluded) return;
        int n = 0;
        forUnits([&](Unit& u) {
            if (n < 64 && u.selected && u.state != UnitState::Dead && ut(u).primaryWeapon != -1) {
                ++n;
                w.orders_.push_back({World::Order::AttackUnit, s, u.index, 0, 0, tside, tidx});
            }
        });
    }
    void attackBuildingOrder(int tside, int tidx) {  // GenerateUnitAttackBuildingOrder 0x459800 (0x15)
        tside &= 0xff;
        tidx &= 0xffff;
        int n = 0;
        forUnits([&](Unit& u) {
            if (n >= 64 || !u.selected || u.state == UnitState::Dead) return;
            const int beh = ut(u).specialBehavior;
            if (beh == behaviour::kHarvester || beh == behaviour::kMcv || beh == behaviour::kThumper) return;
            if (s == tside && (beh == behaviour::kEngineer || beh == behaviour::kSaboteur)) return;
            ++n;
            w.orders_.push_back({World::Order::AttackBuilding, s, u.index, 0, 0, tside, tidx});
        });
    }
    // GenerateUnitGuardOrder (0x18), executed immediately: ProcessOrder case 5.
    void guardOrder() {
        int n = 0;
        forUnits([&](Unit& u) {
            if (n >= 64 || !u.selected || u.state == UnitState::Dead || ut(u).primaryWeapon == -1) return;
            ++n;
            // Order 0x18, executed next tick by ProcessOrder case 5 (World::Order::Guard).
            w.orders_.push_back({World::Order::Guard, s, u.index, 0, 0});
        });
    }
    void deployOrder(int idx) { w.orders_.push_back({World::Order::Deploy, s, idx & 0xffff, 0, 0}); }
    void unitPickOrder(int type) { w.orders_.push_back({World::Order::UnitPick, s, kNoIndex, type & 0xff, 0}); }
    void buildingPickOrder(int type) { w.orders_.push_back({World::Order::BuildingPick, s, kNoIndex, type & 0xff, 0}); }
    void buildingCancelOrder(int type) {
        // (the type is passed as a byte: an empty queue (-1) becomes 255 and matches nothing)
        w.orders_.push_back({World::Order::BuildingCancel, s, kNoIndex, type & 0xff, 0});
    }
    void placeOrder(int type, int x, int y) {
        if (S.buildingQueue.type != (type & 0xff)) return;  // DebugFatal "Invalid buildingType"
        w.orders_.push_back({World::Order::PlaceBuilding, s, kNoIndex, x & 0xff, y & 0xff, 0, type & 0xff});
    }
    void upgradeOrder(int group) { w.orders_.push_back({World::Order::UpgradePick, s, kNoIndex, group & 0xff, 0}); }
    void sellOrder(int idx) { w.orders_.push_back({World::Order::SellBuilding, s, idx, 0, 0}); }

    // ---------------- strength / groups ----------------
    // GetUnitStrength 0x41cb70: HP * HPWeight[task] + (speed >> 16) * SpeedWeight[task].
    std::uint32_t unitStrength(const Unit& u, int task) {
        if (u.state == UnitState::Dead) return 0;
        const auto& t = ut(u);
        const std::uint32_t a1 = x87Ftol(static_cast<double>(t.hitPoints) * F(0xfbc + (task & 0xff) * 4));
        const std::uint32_t a2 = x87Ftol(static_cast<double>(t.speed >> 16) * F(0xf70 + (task & 0xff) * 4));
        return a2 + a1;
    }
    // GetGroupStrength 0x41c340.
    std::uint32_t groupStrength(int g, int task) {
        std::uint32_t sum = 0;
        forUnits([&](Unit& u) { if (u.groupId == static_cast<std::uint8_t>(g)) sum += unitStrength(u, task); });
        return sum;
    }
    // FindFirstFreeGroup 0x41c3a0.
    int firstFreeGroup() {
        for (int g = 1; g < 32; ++g)
            if (gB(g, gUsed) == 0) return g;
        return 0;
    }
    // CreateSplinterGroup 0x41ca60: move units of `from` (not leaving a building, not
    // harvesters/carryalls) into `to` until `want` (randomised by GroupSizeRandomPercentage)
    // strength is reached.
    bool createSplinterGroup(int from, int to, std::uint32_t want) {
        const double f = randFactorUnsigned(B(0xf6f), 0x645);
        const std::uint32_t target = x87Ftol(f * static_cast<double>(static_cast<std::int32_t>(want)));
        std::uint32_t got = 0;
        for (int i = S.firstUnit; i != -1 && got < target; i = S.nextObject(i)) {
            Unit& u = S.units[i];
            const int beh = ut(u).specialBehavior;
            if (u.groupId == static_cast<std::uint8_t>(from) && u.state != UnitState::LeavingBuilding &&
                beh != behaviour::kCarryall && beh != behaviour::kHarvester) {
                u.groupId = static_cast<std::uint8_t>(to);
                got += unitStrength(u, gB(to, gTask));
            }
        }
        gSD(to, gStrength, got);
        gSD(to, gStrength0, got);
        gSD(to, gCounter, 0);
        gSB(to, gUsed, 1);
        gSB(to, gSub, 0);
        return got != 0;
    }
    // GetPercentage 0x41e5d0.
    std::uint32_t percentage(int g, int pct) {
        if (B(0x1d20) != 0) return static_cast<std::uint32_t>(static_cast<std::int32_t>(D(0xf4c)) * (pct & 0xff) / 100);
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(gD(g, gStrength)) * (pct & 0xff) / 100);
    }
    // NumberOfGroupsDoingTask 0x4206c0.
    int groupsDoing(int task) {
        int n = 0;
        for (int g = 0; g < 32; ++g)
            if (gB(g, gUsed) && gB(g, gTask) == task) ++n;
        return n & 0xff;
    }
    // GroupExists 0x41fc30.
    bool groupExists(int g) {
        bool any = false;
        forUnits([&](Unit& u) { if (u.groupId == static_cast<std::uint8_t>(g)) any = true; });
        return any;
    }
    // GetAveragePositionOfGroup 0x420a70.
    void groupAverage(int g, int& x, int& y) {
        std::uint32_t sx = 0, sy = 0, n = 0;
        forUnits([&](Unit& u) {
            if (u.groupId == static_cast<std::uint8_t>(g)) {
                sx += static_cast<std::uint32_t>(posTile(u.posX));
                sy += static_cast<std::uint32_t>(posTile(u.posY));
                ++n;
            }
        });
        if (n) { sx /= n; sy /= n; }
        x = static_cast<std::uint8_t>(sx);
        y = static_cast<std::uint8_t>(sy);
    }
    // MergeGroups 0x421710.
    void mergeGroups(int from, int to) {
        forUnits([&](Unit& u) { if (u.groupId == static_cast<std::uint8_t>(from)) u.groupId = static_cast<std::uint8_t>(to); });
        gSB(from, gUsed, 0);
    }
    // GroupStable 0x41e220: no unit of the group moved within the last 10 x 8 ticks.
    bool groupStable(int g) {
        bool r = true;
        forUnits([&](Unit& u) { if (u.groupId == static_cast<std::uint8_t>(g) && u.lastMoved < 10) r = false; });
        return r;
    }
    // UnitRecentlyInAction 0x41e2c0.
    bool unitRecentlyInAction(const Unit& u) {
        if (u.state == UnitState::Dead) return false;
        if (u.lastAttacked > 0xb && static_cast<std::uint8_t>((ut(u).primaryWeaponLongDelay >> 4) * 3) <= u.lastFired)
            return false;
        return true;
    }
    // RecentlyInAction 0x41e270.
    bool recentlyInAction(int g) {
        bool r = false;
        forUnits([&](Unit& u) { if (u.groupId == static_cast<std::uint8_t>(g) && unitRecentlyInAction(u)) r = true; });
        return r;
    }
    // UnitRecentlyHit 0x41e310.
    bool unitRecentlyHit(const Unit& u) { return u.state != UnitState::Dead && u.lastAttacked < 0xc; }
    // GroupNearTarget 0x41e4e0.
    bool groupNearTarget(int g, int r) {
        if (groupStable(g)) return true;
        bool near = true;
        forUnits([&](Unit& u) {
            if (u.groupId != static_cast<std::uint8_t>(g)) return;
            const int dx = ((u.posX >> 16) / 32) - gB(g, gX), dy = ((u.posY >> 16) / 32) - gB(g, gY);
            if (static_cast<std::uint32_t>(r * r) < static_cast<std::uint32_t>(dx * dx + dy * dy)) near = false;
        });
        return near;
    }
    // GroupAtFullHealth 0x41e860.
    bool groupAtFullHealth(int g) {
        bool r = true;
        forUnits([&](Unit& u) {
            if (u.groupId == static_cast<std::uint8_t>(g) && u.health != static_cast<std::int32_t>(ut(u).hitPoints)) r = false;
        });
        return r;
    }
    // MoraleCheck 0x41e420: losses within the task's morale (+ MoraleModifier).
    bool moraleCheck(int g) {
        const std::int32_t s0 = static_cast<std::int32_t>(gD(g, gStrength0));
        const std::int32_t sv = static_cast<std::int32_t>(gD(g, gStrength));
        return s0 - sv <= ((B(0x1078 + gB(g, gTask)) + B(0x1d1c)) * s0) / 100;
    }
    // CanSpendCash 0x41e8c0: credits minus CashStash[job] (clamped at 0) > amount.
    bool canSpendCash(int job, std::uint32_t amount) {
        std::uint32_t v = static_cast<std::uint32_t>(S.cash + (S.spice - static_cast<std::int32_t>(D(0x1054 + (job & 0xff) * 4))));
        if (static_cast<std::int32_t>(v) < 1) v = 0;
        return amount < v;
    }

    // ---------------- positions ----------------
    // CSide__FindBestBasePosition 0x46e3f0.
    void findBestBasePosition(int& ox, int& oy) {
        const int W = w.width_, H = w.height_;
        if (S.firstBuilding == -1) {
            for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
                const Unit& u = S.units[i];
                if (u.flags & unit_flags::kFlying) continue;
                ox = static_cast<std::uint8_t>((u.posX >> 16) / 32);
                if (ox > W - 1) ox = static_cast<std::uint8_t>(W - 1);
                oy = static_cast<std::uint8_t>((u.posY >> 16) / 32);
                if (oy > H - 1) oy = static_cast<std::uint8_t>(H - 1);
                return;
            }
            ox = static_cast<std::uint8_t>((S.baseMaxX - S.baseMinX) / 2 + S.baseMinX);
            oy = static_cast<std::uint8_t>((S.baseMaxY - S.baseMinY) / 2 + S.baseMinY);
            return;
        }
        int ci = S.primaryBuilding[0];
        if ((ci & 0xffff) == 0xffff) ci = S.firstBuilding;
        const Building& c = S.buildings[ci % kObjectsPerSide];
        const int cx = ((c.posX >> 16) / 32) & 0xff, cy = ((c.posY >> 16) / 32) & 0xff;
        int sx = 0, sy = 0, n = 0;
        forBuildings([&](Building& b) {
            const int bx = ((b.posX >> 16) / 32) & 0xff, by = ((b.posY >> 16) / 32) & 0xff;
            if ((cx - bx) * (cx - bx) + (cy - by) * (cy - by) < 100) { sx += bx; sy += by; ++n; }
        });
        if (n == 0) { ox = static_cast<std::uint8_t>(cx); oy = static_cast<std::uint8_t>(cy); }
        else { ox = static_cast<std::uint8_t>(sx / n); oy = static_cast<std::uint8_t>(sy / n); }
        if (ox > W - 1) ox = static_cast<std::uint8_t>(W - 1);
        if (oy > H - 1) oy = static_cast<std::uint8_t>(H - 1);
    }
    // GetBestBasePosition 0x41e180 (remembers the first non-zero result at +0x24/+0x28).
    void bestBasePosition(int& x, int& y) {
        findBestBasePosition(x, y);
        if (x == 0 && y == 0) {
            if (D(0x24) != 0xffffffff) { x = B(0x24); y = B(0x28); }
            else { SD(0x24, static_cast<std::uint32_t>(x)); SD(0x28, static_cast<std::uint32_t>(y)); }
        } else if (D(0x24) == 0xffffffff) {
            SD(0x24, static_cast<std::uint32_t>(x));
            SD(0x28, static_cast<std::uint32_t>(y));
        }
        if (x >= w.width_ - 1) x = static_cast<std::uint8_t>(w.width_ - 1);
        if (y >= w.height_ - 1) y = static_cast<std::uint8_t>(w.height_ - 1);
    }

    // ---------------- group operations ----------------
    void validateTarget(int g);  // 0x4237a0
    void commence(int g);        // CommenceOperation 0x41fc80
    void sendGroupToPoint(int x, int y, int g) {  // 0x41e3c0
        clearUnits();
        selectGroup(g, true);
        gSB(g, gX, x);
        gSB(g, gY, y);
        moveOrder(x, y);
    }
    void returnToBase(int g) {  // 0x41e340
        int x, y;
        bestBasePosition(x, y);
        gSB(g, gX, x);
        gSB(g, gY, y);
        gSB(g, gTask, kTaskReturn);
        gSB(g, gSub, 1);
        gSW(g, gTargetIndex, 0xffff);
        clearUnits();
        selectGroup(g, false);
        commence(g);
    }
    void setUnitToGuard(Unit& u) {  // 0x41fea0
        clearUnits();
        if (u.state != UnitState::Dead) {
            u.selected = true;
            guardOrder();
        }
    }
    void setGroupToGuard(int g) {  // 0x41e490
        forUnits([&](Unit& u) { if (u.groupId == static_cast<std::uint8_t>(g)) setUnitToGuard(u); });
    }
    void huntMode(int g, bool used) {  // the "task 14 / time 10000 / sub 4" reset used everywhere
        gSB(g, gTask, kTaskHunt);
        gSD(g, gTime, 10000);  // FUN_00423820
        gSB(g, gSub, 4);
        if (used) gSB(g, gUsed, 1);
        gSD(g, gCounter, 0);
        gSW(g, gTargetIndex, 0xffff);
    }
    const Building* bestBuilding(int tside, int x, int y, float weight);  // GetBestBuilding 0x41bbb0
    const Unit* bestUnit(int tside, int x, int y);                         // GetBestUnit 0x41bdb0
    bool attackBestBuilding(int g, int tside, int x, int y);              // 0x41fee0
    bool attackBestUnit(int g, int tside, int x, int y);                  // 0x420970
    int bestSideToAttack();                                               // 0x41cdf0
    int recalculateSideToAttack();                                        // 0x41ce50
    bool nearestHostileThing(int& x, int& y, int& idx, int& tside);       // 0x420700
    bool unitInBase(int tside, Unit*& out);                               // 0x4205f0
    void attackObject(Unit& u, int tside, int tidx);                      // 0x420010
    void findMostAppropriateTarget(const Unit& u, int& idx, int& tside);  // 0x420140
    bool doStopStartStuff(int g, int mod, bool retarget);                 // 0x41f670
    bool sendNearestUnit(int g, int x, int y);                            // 0x423360
    bool attackBuildingWithPart(int g, int idx, int tside, int pct);      // 0x423440
    void enemyBaseWaypoint(int& x, int& y, int tx, int ty);               // 0x420b20
    void monitorGroups(int phase);                                        // 0x41d070
    void groupNewUnits(int g, bool force);                                // 0x41c3e0
    int findGroupForDelivery(int delivery);                               // 0x420480
    void doFremen(bool force);                                            // 0x422a80
    void doSaboteur(bool force);                                          // 0x422900

    // ---------------- production / base ----------------
    bool canBuildUnit(int type);           // 0x41b850
    int findBestUnitToBuild();             // 0x41b910
    bool shouldIBuildNow();                // 0x41bea0
    bool inIconList(int type);             // 0x422e60
    bool shouldIBuildUnit(int type);       // 0x420350
    void doRepairs();                      // 0x41bf00
    void doBuildUnits();                   // 0x41bfe0
    void doBuildCarryalls();               // 0x4203d0
    void doOverrides();                    // 0x422c20
    void doMcvs();                         // 0x422140
    void dealWithMcv(Unit& u);             // 0x422ec0
    bool canIDeployMcvHere(int x, int y);  // 0x423020
    bool kickUnitsOutOfArea(int x0, int y0, int x1, int y1, const Unit* except);  // 0x423100
    bool placeToMoveTo(int x0, int y0, int x1, int y1, int r, int& x, int& y);     // 0x423220
    void monitorHarvesters();              // 0x421fa0
    void monitorBuildings();               // 0x41eab0
    void rebuildBuildings();               // 0x41ebc0
    bool buildStuck();                     // 0x41f5c0
    void pickNextPiece();                  // 0x41f560
    void placeBuiltBuilding();             // CAI_PlaceBuiltBuilding 0x41ef30
    int bestBuildingToBuild();             // 0x420d50
    bool needBuilding(int group);          // 0x4218e0
    bool findNewPosition(int& x, int& y, int type);       // 0x421950
    bool findRandomPosition(int& x, int& y, int type, int tries);  // 0x420e90
    std::uint32_t canAIPlaceBuilding(int type, int x, int y);      // 0x421000
    bool canAIPlaceConcrete(int type, int x, int y);      // 0x4212a0
    bool concreteAlready(int x, int y);                   // 0x421a90
    bool findAlternativePlace(int& x, int& y);            // 0x421b60
    bool noFriendlyUnits(int type, int x, int y);         // 0x423500
    void ownership(int x, int y, std::uint8_t& fl);       // GetOwnershipStatusOfCell 0x428c30
    void getInitialBuildingPositions();                   // 0x41e920
    void upgradeBuildings();                              // 0x421c40
    void buyUnitsFromStarport();                          // 0x421cf0
    bool shouldIBuyUnit(int type);                        // 0x421e50
    void doUnitFacings();                                 // 0x420c80

    // ---------------- groups (DoAI) ----------------
    void doAttackBuildings();   // 0x41c920
    void doProtection();        // 0x41c070
    void doDefence();           // 0x41c270
    void doDefendChecks();      // 0x4214a0
    int defendGroupId(int area);  // 0x422230
    void doAttackApproachers(); // 0x420500
    void doSpecialWeapons();    // 0x422270
    void doRepairUnits(int g1, int g2);  // 0x41e650
    // GenerateRepairSelectedUnitsOrder 0x4595e0 (order 0x21): selected, alive vehicles, max 64.
    void repairOrder(int padIndex) {
        int n = 0;
        forUnits([&](Unit& u) {
            if (n >= 64 || !u.selected || u.state == UnitState::Dead || ut(u).isInfantry) return;
            ++n;
            w.orders_.push_back({World::Order::RepairUnit, s, u.index, 0, 0, s, padIndex & 0xffff});
        });
    }
    bool itemReady(int beh, int& slot);  // 0x422730
    const void* bestTarget(int tside, int weapon, int& kind, int& px, int& py);  // 0x422800
    void autoBerserkCheck();    // 0x423850

    void activate();  // ActivateAI
    void update();    // DoAI
};

// -------------------------------------------------------------------------------------------
// Targets and group operations

void AiController::validateTarget(int g) {  // FUN_004237a0: drop a dead/excluded target unit
    const int idx = gW(g, gTargetIndex);
    if (idx == 0xffff) return;
    const int ts = gB(g, gTargetSide);
    if (kindOf(ts, idx) != 1) return;
    const Unit& t = w.sides_[ts & 7].units[idx % kObjectsPerSide];
    if ((t.flags & unit_flags::kExcluded) || T.units[t.type].specialBehavior == behaviour::kSandworm ||
        t.state == UnitState::Dead)
        gSW(g, gTargetIndex, 0xffff);
}

void AiController::commence(int g) {
    switch (gB(g, gTask)) {
    case kTaskAttackBuilding: case kTaskEngineers: case kTaskSaboteur:
        if (gB(g, gSub) > 6 && gB(g, gSub) < 9) {
            moveOrder(gB(g, gX), gB(g, gY));
            return;
        }
        if (gW(g, gTargetIndex) != 0xffff) attackBuildingOrder(gB(g, gTargetSide), gW(g, gTargetIndex));
        return;
    case kTaskAttackUnit:
        validateTarget(g);
        if (gW(g, gTargetIndex) != 0xffff) attackUnitOrder(gB(g, gTargetSide), gW(g, gTargetIndex));
        return;
    case kTaskReturn: {
        int x, y;
        bestBasePosition(x, y);
        gSB(g, gX, x);
        gSB(g, gY, y);
        sendGroupToPoint(gB(g, gX), y, g);
        return;
    }
    case kTaskHunt: {
        validateTarget(g);
        if (gW(g, gTargetIndex) == 0xffff) {
            int x, y;
            groupAverage(g, x, y);
            gSB(g, gX, x);
            gSB(g, gY, y);
            int idx, ts;
            x = gB(g, gX);
            y = gB(g, gY);
            idx = gW(g, gTargetIndex);
            ts = gB(g, gTargetSide);
            if (nearestHostileThing(x, y, idx, ts)) { gSB(g, gX, x); gSB(g, gY, y); }
            gSW(g, gTargetIndex, idx);
            gSB(g, gTargetSide, ts);
            if (gW(g, gTargetIndex) == 0xffff) return;
        }
        const int idx = gW(g, gTargetIndex), ts = gB(g, gTargetSide);
        const int k = kindOf(ts, idx);
        if (k == 1) attackUnitOrder(ts, idx);
        else if (k == 2) attackBuildingOrder(ts, idx);
        else gSW(g, gTargetIndex, 0xffff);
        return;
    }
    default: return;
    }
}

const Building* AiController::bestBuilding(int tside, int x, int y, float weight) {
    Side& ts = w.sides_[tside & 7];
    struct Cand { float pri; std::uint32_t dist; int idx; };
    std::vector<Cand> c;
    std::uint32_t minD = 0xffffffff, maxD = 0;
    for (int i = ts.firstBuilding; i != -1; i = ts.nextObject(i)) {
        const Building& b = ts.buildings[i];
        const double f = randFactorUnsigned(B(0xf6c), 0x2d7) * F(0x121c + b.type * 4);
        const float pri = static_cast<float>(f);
        if (fpuC3(f, 0.0) || c.size() >= 199) continue;
        const int dx = (x & 0xff) - posTile(b.posX), dy = (y & 0xff) - posTile(b.posY);
        const std::uint32_t d = x87Ftol(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
        if (d < minD) minD = d;
        if (maxD < d) maxD = d;
        c.push_back({pri, d, b.index});
    }
    float best = 0.0f;
    const Building* r = nullptr;
    for (const Cand& k : c) {
        const double v = static_cast<double>(maxD - k.dist) * weight * F(0xf64) + k.pri;
        if (fpuC0(v, best) || fpuC3(v, best)) continue;
        best = static_cast<float>(v);
        if (k.idx != 0xffff && k.idx <= 1000) r = &ts.buildings[k.idx];
    }
    return r;
}

const Unit* AiController::bestUnit(int tside, int x, int y) {
    Side& ts = w.sides_[tside & 7];
    double best = 0.0;
    const Unit* r = nullptr;
    for (int i = ts.firstUnit; i != -1; i = ts.nextObject(i)) {
        const Unit& u = ts.units[i];
        const float pri = F(0x1bd8 + u.type * 4);
        if (!fpuC0(best, pri)) continue;
        const int dx = (x & 0xff) - posTile(u.posX), dy = (y & 0xff) - posTile(u.posY);
        const std::uint32_t d = x87Ftol(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
        best = static_cast<double>(d) * F(0xf60) + pri;
        r = &u;
    }
    return r;
}

bool AiController::attackBestBuilding(int g, int tside, int x, int y) {
    if (static_cast<std::int8_t>(tside) == -1) {
        returnToBase(g);
        return false;
    }
    const Building* b = bestBuilding(tside, x, y, 1.0f);
    gSB(g, gTargetSide, tside);
    if (b) {
        gSW(g, gTargetIndex, b->index);
        gSB(g, gTargetSide, tside);
        gSB(g, gX, (b->posX >> 16) / 32);
        gSB(g, gY, (b->posY >> 16) / 32);
        gSB(g, gSub, 2);
        gSD(g, gTries, 0);
        gSB(g, gTask, kTaskAttackBuilding);
        clearUnits();
        selectGroup(g, true);
        commence(g);
        return true;
    }
    huntMode(g, true);
    gSD(g, gStrength, groupStrength(g, gB(g, gTask)));
    commence(g);
    return true;
}

bool AiController::attackBestUnit(int g, int tside, int x, int y) {
    if (static_cast<std::int8_t>(tside) == -1) {
        returnToBase(g);
        return false;
    }
    const Unit* u = bestUnit(tside, x, y);
    if (u) {
        gSW(g, gTargetIndex, u->index);
        gSB(g, gTargetSide, tside);
        gSB(g, gX, (u->posX >> 16) / 32);
        gSB(g, gY, (u->posY >> 16) / 32);
        gSB(g, gSub, 2);
        gSD(g, gTries, 0);
        gSB(g, gTask, kTaskAttackUnit);
        clearUnits();
        selectGroup(g, true);
        commence(g);
        return true;
    }
    returnToBase(g);
    return false;
}

int AiController::bestSideToAttack() {
    const int cur = static_cast<std::int8_t>(B(2));
    bool notEnemy = false;
    if (B(2) != 0xff && dipl(cur) != 1) notEnemy = true;
    if (D(0x18) != 0) SD(0x18, D(0x18) - 1);
    if (D(0x18) == 0 || B(2) == 0xff || notEnemy) {
        SB(2, recalculateSideToAttack());
        SD(0x18, 0x19);
    }
    return B(2);
}

int AiController::recalculateSideToAttack() {
    int bestSide = 0xff;
    std::uint32_t best = 0;
    for (int o = 0; o < kMaxSides; ++o) {
        Side& os = w.sides_[o];
        std::int32_t count = 0;
        if (o != s && os.participates && dipl(o) == 1) {
            for (int i = os.firstBuilding; i != -1; i = os.nextObject(i)) ++count;  // (+8 flags & 0x44: never set)
            for (int i = os.firstUnit; i != -1; i = os.nextObject(i))
                if (!(os.units[i].byte29 & 0x40)) count += 3;
            if (count != 0) {
                const int ex = (S.baseMaxX - S.baseMinX) / 2 + S.baseMinX - ((os.baseMaxX - os.baseMinX) / 2 + os.baseMinX);
                const int ey = (S.baseMaxY - S.baseMinY) / 2 + S.baseMinY - ((os.baseMaxY - os.baseMinY) / 2 + os.baseMinY);
                const std::uint32_t d = x87Ftol(std::sqrt(static_cast<double>(static_cast<std::int64_t>(ex * ex + ey * ey))));
                if (d != 0)
                    count = static_cast<std::int32_t>(x87Ftol(static_cast<double>(count) /
                                                               (static_cast<double>(d) * static_cast<double>(0.0078125f))));
            }
        }
        if (count < 2) count = 1;
        std::uint32_t v = ((rnd() % 40 + 80) * static_cast<std::uint32_t>(count)) / 100;
        if (!w.ai_[o].enabled()) v <<= 2;
        if (best < v) { bestSide = o; best = v; }
    }
    return bestSide;
}

bool AiController::nearestHostileThing(int& x, int& y, int& idx, int& tside) {
    std::uint32_t best = 0xffffff;
    bool found = false;
    int fx = 0, fy = 0;
    for (int o = 0; o < kMaxSides; ++o) {
        if (dipl(o) != 1) continue;
        Side& os = w.sides_[o];
        for (int i = os.firstBuilding; i != -1; i = os.nextObject(i)) {
            const Building& b = os.buildings[i];
            const auto& t = T.buildings[b.type];
            if (b.state == 17 || t.specialBehavior == building_behaviour::kWall || t.buildingGroup == gid(9)) continue;
            const int dx = (x & 0xff) - (b.posX >> 16) / 32, dy = (y & 0xff) - (b.posY >> 16) / 32;
            const std::uint32_t d = static_cast<std::uint32_t>(dx * dx + dy * dy);
            if (d < best) {
                idx = b.index;
                tside = o;  // (the original stores the loop counter here; equal to o)
                fx = ((b.posX >> 16) / 32) & 0xff;
                fy = ((b.posY >> 16) / 32) & 0xff;
                best = d;
            }
        }
        for (int i = os.firstUnit; i != -1; i = os.nextObject(i)) {
            const Unit& u = os.units[i];
            if (u.state == UnitState::Dead || T.units[u.type].specialBehavior == behaviour::kSandworm ||
                (u.flags & 0x400144))
                continue;
            const int ux = posTile(u.posX), uy = posTile(u.posY);
            const int dx = (x & 0xff) - ux, dy = (y & 0xff) - uy;
            const std::uint32_t d = static_cast<std::uint32_t>(dx * dx + dy * dy);
            if (d < best) {
                found = true;
                idx = u.index;
                fx = ux & 0xff;
                tside = o;
                fy = uy & 0xff;
                best = d;
            }
        }
    }
    if (!found) return false;  // (a building alone does not count, as in the original)
    x = fx;
    y = fy;
    return true;
}

bool AiController::unitInBase(int tside, Unit*& out) {
    if (dipl(tside) != 1) return false;
    Side& ts = w.sides_[tside & 7];
    for (int i = ts.firstUnit; i != -1; i = ts.nextObject(i)) {
        Unit& u = ts.units[i];
        const int dx = posTile(u.posX) - B(0xf57), dy = posTile(u.posY) - B(0xf58);
        if (static_cast<std::uint32_t>(dx * dx + dy * dy) < D(0xf50)) {
            out = &u;
            return true;
        }
    }
    return false;
}

void AiController::attackObject(Unit& u, int tside, int tidx) {
    u.enemySide = tside & 0xff;
    u.enemyIndex = tidx & 0xffff;
    if ((tside & 0xff) >= 8 || (tidx & 0xffff) >= 1000) return;
    const int k = kindOf(tside, tidx);
    if (k == 1) {
        Unit& t = w.sides_[tside].units[tidx];
        if (t.flags & unit_flags::kFlying) {
            u.attackerIndex = kNoIndex;
            return;
        }
        if (B(0x1c) != 0) {  // SquishInfantry
            const int dx = posTile(u.posX) - posTile(t.posX), dy = posTile(u.posY) - posTile(t.posY);
            if (static_cast<std::uint32_t>(dx * dx + dy * dy) < D(0x14) && ut(u).canCrushInfantry) {
                moveOrder(posTile(t.posX), posTile(t.posY));
                return;
            }
        }
        attackUnitOrder(tside, tidx);
    } else if (k == 2) {
        attackBuildingOrder(tside, tidx);
        u.flags |= 0x100000;
    }
}

void AiController::findMostAppropriateTarget(const Unit& u, int& idx, int& tside) {
    idx = 0xffff;
    tside = 0xff;
    const std::uint8_t g = u.groupId;
    auto isWorm = [&](int sd, int i) {
        return kindOf(sd, i) == 1 && T.units[w.sides_[sd & 7].units[i].type].specialBehavior == behaviour::kSandworm;
    };
    auto excluded = [&](int sd, int i) {
        return kindOf(sd, i) == 1 && (w.sides_[sd & 7].units[i].flags & unit_flags::kExcluded);
    };
    forUnits([&](Unit& o) {
        if (&o == &u || o.groupId != g || !unitRecentlyInAction(o)) return;
        const int dx = posTile(u.posX) - posTile(o.posX), dy = posTile(u.posY) - posTile(o.posY);
        const std::uint32_t d = static_cast<std::uint32_t>(dx * dx + dy * dy);
        bool ok = true;
        if (o.enemyIndex != kNoIndex && kindOf(o.enemySide, o.enemyIndex) == 1 &&
            (w.sides_[o.enemySide & 7].units[o.enemyIndex].flags & unit_flags::kCloaked))
            ok = false;
        if (!ok || d >= 0xfff) return;
        const int as = o.attackerSide, ai = o.attackerIndex;
        if (ai == kNoIndex || dipl(as) == 0 || isWorm(as, ai) || excluded(as, ai)) {
            if (o.enemyIndex != kNoIndex && dipl(o.attackerSide) != 0 && !isWorm(o.enemySide, o.enemyIndex) &&
                !excluded(o.enemySide, o.enemyIndex)) {
                idx = o.enemyIndex;
                tside = o.enemySide & 0xff;
            }
        } else {
            idx = ai;
            tside = as;
        }
    });
}

bool AiController::doStopStartStuff(int g, int mod, bool retarget) {
    bool did = false;
    int stopped = 0;
    clearUnits();
    for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
        Unit& u = S.units[i];
        if (u.groupId != static_cast<std::uint8_t>(g)) continue;
        bool checkHit = true;
        if (u.flags & 0x100000) {
            // Attacking: give up a target that has run out of weapon range + IgnoreDistance.
            if (u.enemyIndex == kNoIndex) {
                u.flags &= ~0x100000u;
            } else if (kindOf(u.enemySide, u.enemyIndex) == 1) {
                const Unit& t = w.sides_[u.enemySide & 7].units[u.enemyIndex];
                const int dx = posTile(u.posX) - posTile(t.posX), dy = posTile(u.posY) - posTile(t.posY);
                const std::uint32_t d = static_cast<std::uint32_t>(dx * dx + dy * dy);
                const auto& tt = ut(u);
                std::uint32_t r = tt.primaryWeapon >= 0 ? T.weapons[tt.primaryWeapon].range >> 5 : 0;
                if (tt.secondaryWeapon != -1 && r < (T.weapons[tt.secondaryWeapon].range >> 5))
                    r = T.weapons[tt.secondaryWeapon].range >> 5;
                if (r * r < d && w.vars().ignoreDistance < d - r) {
                    u.attackerIndex = kNoIndex;
                    u.attackerSide = 0xff;
                    u.flags &= ~0x100000u;
                    if (u.state != UnitState::Dead) {
                        u.selected = true;
                        commence(g);
                        u.selected = false;
                    }
                }
            }
            checkHit = !(u.flags & 0x100000);
        }
        if (checkHit && unitRecentlyHit(u)) {
            if (dipl(u.attackerSide) != 0 && u.state != UnitState::Guarding && u.attackerIndex != kNoIndex &&
                !(u.flags & 0x100000)) {
                if (ut(u).primaryWeapon == -1) {
                    clearUnits();
                    selectUnit(u.index);
                    if (u.state == UnitState::Idle) {
                        int x = posTile(u.posX) & 0xff, y = posTile(u.posY) & 0xff;
                        w.findDriveOnTileNear(s, x, y);
                        moveOrder(x, y);
                        did = true;
                        continue;
                    }
                } else {
                    const bool gone = kindOf(u.attackerSide, u.attackerIndex) == 1 &&
                                      (w.sides_[u.attackerSide & 7].units[u.attackerIndex].flags & unit_flags::kExcluded);
                    if (!gone) {
                        clearUnits();
                        selectUnit(u.index);
                        attackObject(u, u.attackerSide, u.attackerIndex);
                        u.flags |= 0x100000;
                    }
                }
                did = true;
            }
            continue;
        }
        // LAB_0041f933: idle-ish units look for something to do.
        if (u.lastMoved > 10 && u.lastFired > 10) {
            bool handled = false;
            int tidx = u.enemyIndex, tside = 0;
            if (!(u.flags & 0x100000) || unitRecentlyInAction(u) || u.lastMoved < 0xb) {
                if (retarget && rnd() % (mod & 0xff) != (u.index & 0xffff) % (mod & 0xff)) {
                    findMostAppropriateTarget(u, tidx, tside);
                    if ((tidx & 0xffff) != 0xffff) {
                        const bool gone = kindOf(tside, tidx) == 1 &&
                                          (w.sides_[tside & 7].units[tidx].flags & unit_flags::kExcluded);
                        if (!gone) {
                            clearUnits();
                            selectUnit(u.index);
                            attackObject(u, tside, tidx);
                            u.flags |= 0x100000;
                            handled = true;
                        }
                    }
                }
            } else {
                ++stopped;
                u.flags &= ~0x100000u;
                clearUnits();
                selectUnit(u.index);
                handled = true;
            }
            if ((tidx & 0xffff) == 0xffff && ((w.gameTicks_ >> 5) & 7) == (u.index & 7)) {
                // Look for crates around (5 tiles) and drive to them.
                const int x0 = std::max(u.blockFromX - 5, 0), x1 = std::min(u.blockFromX + 6, w.width_ - 1);
                const int y0 = std::max(u.blockFromY - 5, 0), y1 = std::min(u.blockFromY + 6, w.height_ - 1);
                for (int yy = y0; yy < y1; ++yy)
                    for (int xx = x0; xx < x1; ++xx)
                        if (w.cell(xx, yy).flags & cell_flags::kCrate) {
                            // GetCrateFromMap has side effects (faithful): looking at an
                            // ordinary crate removes it, a ripe bloom is recycled (rand).
                            const int ct = w.getCrateFromMap(xx, yy);
                            if (!(ct < 7 || ct > 0xb)) continue;
                            clearUnits();
                            selectUnit(u.index);
                            moveOrder(xx, yy);
                        }
            }
            if (!handled) {
                selectUnit(u.index);
                ++stopped;
            }
        }
    }
    if (stopped != 0) commence(g);
    return did;
}

bool AiController::sendNearestUnit(int g, int x, int y) {
    Unit* best = nullptr;
    std::uint32_t bd = 0xfffff;
    std::uint8_t inf = 0;
    forUnits([&](Unit& u) {
        if (u.groupId != static_cast<std::uint8_t>(g)) return;
        const std::uint8_t ci = ut(u).isInfantry;
        const int dx = posTile(u.posX) - (x & 0xff), dy = posTile(u.posY) - (y & 0xff);
        const std::uint32_t d = static_cast<std::uint32_t>(dx * dx + dy * dy);
        if ((d < bd && u.state == UnitState::Idle && ci == inf) || (ci != 0 && inf == 0)) {
            best = &u;
            bd = d;
            inf = ci;
        }
    });
    if (best) {
        clearUnits();
        selectUnit(best->index);
        moveOrder(x, y);
    }
    return best != nullptr;
}

bool AiController::attackBuildingWithPart(int g, int idx, int tside, int pct) {
    int n = 0;
    clearUnits();
    forUnits([&](Unit& u) {
        if (u.groupId == static_cast<std::uint8_t>(g) && rnd() % 100 <= static_cast<std::uint32_t>(pct & 0xff)) {
            ++n;
            selectUnit(u.index);
        }
    });
    if (n) {
        attackBuildingOrder(tside, idx);
        return true;
    }
    return false;
}

void AiController::enemyBaseWaypoint(int& x, int& y, int tx, int ty) {
    int ang = atan4096((x & 0xff) - (tx & 0xff), (ty & 0xff) - (y & 0xff)) + 0x40;
    ang %= 0x1000;  // signed remainder
    rnd();          // (result unused)
    x = tx & 0xff;
    y = ty & 0xff;
    const int dir = (static_cast<std::uint8_t>(static_cast<std::int8_t>(ang / 128) - 2)) & 0x1f;
    const std::int32_t sx = sinTable(dir, 352), cy = cosTable(dir, 352);
    // One step (NoHostileUnitsWithinRange 0x420c60 always returns 1).
    x = static_cast<std::uint8_t>(x - static_cast<std::int8_t>(sx / 0x10000));
    y = static_cast<std::uint8_t>(y - static_cast<std::int8_t>(cy / 0x10000));
    if (x < 0xc1) { if (x > w.width_ - 1) x = static_cast<std::uint8_t>(w.width_ - 1); }
    else x = 0;
    if (y < 0xc1) { if (y > w.height_ - 1) y = static_cast<std::uint8_t>(w.height_ - 1); }
    else y = 0;
}

void AiController::monitorGroups(int phase) {
    const int gsel = (phase + static_cast<int>(w.gameTicks_)) & 0x1f;
    bool enemySeen = false;
    if (gsel == 0) {
        std::array<int, 32> cnt{};
        forUnits([&](Unit& u) { if (u.groupId != 0xff) ++cnt[u.groupId & 0x1f]; });
        for (int g = 0; g < 32; ++g)
            if (cnt[g] == 0 && gB(g, gUsed) && g != B(0x1d3c) && g != B(0x1d07)) gSB(g, gUsed, 0);
        for (int g = 0; g < 32; ++g) {
            if (kMaxGroupSize[std::min<int>(gB(g, gTask), 18)] < cnt[g]) {
                const int ng = firstFreeGroup();
                if (ng) {
                    createSplinterGroup(g, ng, percentage(g, 0x32));
                    std::uint8_t buf[kGroupSize];
                    std::memcpy(buf, p(go(g)), kGroupSize);
                    for (std::size_t k = 0; k < kGroupSize; ++k) SB(go(ng) + static_cast<std::ptrdiff_t>(k), buf[k]);
                    commence(ng);
                }
            }
        }
    }
    for (int g = 0; g < 32; ++g) {
        if (!gB(g, gUsed)) continue;
        validateTarget(gsel);  // (the original validates the monitored group here, not g)
        if (gW(g, gTargetIndex) == 0xffff) continue;
        const int ts = gB(g, gTargetSide), ti = gW(g, gTargetIndex);
        bool keep = false, sub4 = false;
        if (dipl(ts) == 1) {
            if (ts == s) keep = true;
            else {
                const int k = kindOf(ts, ti);
                if (k == 1) { if (w.sides_[ts & 7].units[ti].state != UnitState::Dead) keep = true; else sub4 = true; }
                else if (k == 2) { if (w.sides_[ts & 7].buildings[ti].state != 17) keep = true; else sub4 = true; }
            }
        } else {
            sub4 = true;
        }
        if (keep) continue;
        if (sub4) gSB(g, gSub, 4);
        gSW(g, gTargetIndex, 0xffff);
    }
    if (gsel == 0) return;
    const int g = gsel;
    if (B(0x1e) == 1 && g == B(0x1d3c)) {
        huntMode(g, false);
        int x, y;
        groupAverage(g, x, y);
        gSB(g, gX, x);
        gSB(g, gY, y);
        gSB(g, gUsed, 1);
    }
    if (gD(g, gStrength) == 0 && g != B(0x1d3c) && gB(g, gUsed) && g != B(0x1d07)) {
        gSB(g, gUsed, 0);
        return;
    }
    gSD(g, gCounter, gD(g, gCounter) + 1);
    gSD(g, gStrength, groupStrength(g, gB(g, gTask)));
    const int task = gB(g, gTask);
    auto toHuntAtAverage = [&]() {
        gSB(g, gSub, 4);
        gSD(g, gCounter, 0);
        gSB(g, gTask, kTaskHunt);
        gSD(g, gTime, 10000);
        gSW(g, gTargetIndex, 0xffff);
        int x, y;
        groupAverage(g, x, y);
        gSB(g, gX, x);
        gSB(g, gY, y);
    };
    switch (task) {
    case 3: case 4:
        if (!moraleCheck(g)) returnToBase(g);
        doStopStartStuff(g, 8, true);
        if (gB(g, gSub) != 5) {
            if (gB(g, gSub) == 0) {
                if (!groupStable(g)) { gSB(g, gSub, 1); return; }
            } else if (groupNearTarget(g, 0x10)) {
                toHuntAtAverage();
                return;
            }
        }
        break;
    case kTaskAttackUnit:
        if (gD(g, gCounter) > 10) doStopStartStuff(g, 8, true);
        if (!moraleCheck(g)) returnToBase(g);
        if (gB(g, gSub) == 4) {
            bool calm = true;
            if (recentlyInAction(g)) { calm = false; if (gB(g, gSub) == 2) gSB(g, gSub, 3); }
            if (!calm || !groupExists(g)) { gSD(g, gTries, 0); return; }
            gSD(g, gTries, gD(g, gTries) + 1);
            if (gD(g, gTries) > 2) {
                int x, y;
                groupAverage(g, x, y);
                attackBestUnit(g, gB(g, gTargetSide), x, y);
                return;
            }
        }
        break;
    case kTaskApproacher: {
        clearUnits();
        if (gD(g, gCounter) > 10) doStopStartStuff(g, 8, true);
        if (!moraleCheck(g)) returnToBase(g);
        if (gB(g, gSub) != 4) return;
        bool calm = true;
        if (recentlyInAction(g)) { calm = false; if (gB(g, gSub) == 2) gSB(g, gSub, 3); }
        if (calm && groupExists(g)) {
            gSD(g, gTries, gD(g, gTries) + 1);
            if (gD(g, gTries) < 3) return;
            Unit* t = nullptr;
            if (!unitInBase(gB(g, gTargetSide), t)) { returnToBase(g); return; }
            gSW(g, gTargetIndex, t->index);
            selectGroup(g, true);
            // (quirk: the original passes the low byte of the unit index as the target side)
            attackUnitOrder(gB(g, gTargetIndex), t->index);
            return;
        }
        gSD(g, gTries, 0);
        return;
    }
    default:
        doStopStartStuff(g, 8, false);
        break;
    case kTaskReturn:
        if (gD(g, gCounter) > 10) doStopStartStuff(g, 8, true);
        forUnits([&](Unit& u) {
            if (u.groupId == static_cast<std::uint8_t>(g) && S.baseMinX <= u.blockFromX && u.blockFromX <= S.baseMaxX &&
                S.baseMinY <= u.blockFromY && u.blockFromY <= S.baseMaxY)
                u.groupId = 0xff;
        });
        return;
    case kTaskRepair:
        if (!groupAtFullHealth(g)) return;
        returnToBase(g);
        return;
    case kTaskHunt:
        if (gD(g, gTime) == 0) returnToBase(g);
        if (gD(g, gCounter) > 10) doStopStartStuff(g, 8, true);
        if (gB(g, gSub) == 4) {
            int x = gB(g, gX), y = gB(g, gY), idx = gW(g, gTargetIndex), ts = gB(g, gTargetSide);
            const bool f = nearestHostileThing(x, y, idx, ts);
            if (f) { gSB(g, gX, x); gSB(g, gY, y); }
            gSW(g, gTargetIndex, idx);
            gSB(g, gTargetSide, ts);
            if (f) {
                clearUnits();
                selectGroup(g, false);
                if (static_cast<std::int32_t>(gD(g, gTime)) > 0) gSD(g, gTime, gD(g, gTime) - 1);
                const int k = kindOf(ts, idx);
                if (k == 1) { gSB(g, gSub, 2); attackUnitOrder(ts, idx); return; }
                if (k == 2) { gSB(g, gSub, 2); attackBuildingOrder(ts, idx); return; }
                gSW(g, gTargetIndex, 0xffff);
                return;
            }
        }
        break;
    case kTaskDefendArea: {
        if (B(0x19ec) == 0) break;
        doStopStartStuff(g, 8, true);
        const std::ptrdiff_t ar = static_cast<std::ptrdiff_t>(kAreas) + gB(g, gArea) * static_cast<std::ptrdiff_t>(kAreaSize);
        const bool busy = recentlyInAction(g);
        bool wander = false;
        if (!busy) {
            if ((rnd() & 1) == 0 && B(ar + 3) != B(ar + 2)) wander = true;
        }
        if (wander) {
            for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
                Unit& u = S.units[i];
                const int bx = u.blockFromX, by = u.blockFromY;
                const bool outside = S.baseMaxX < bx || bx < S.baseMinX || S.baseMaxY < by || by < S.baseMinY ||
                                     (w.cell(bx, by).flags & cell_flags::kSand);
                if (u.groupId != static_cast<std::uint8_t>(g)) continue;
                if (!(u.state == UnitState::Idle || u.state == UnitState::Guarding)) continue;
                if (!((rnd() & 0x7f) == 0 || u.lastMoved > 200 || outside)) continue;
                const int ax0 = B(ar), ax1 = B(ar + 1), ay0 = B(ar + 2), ay1 = B(ar + 3);
                const std::uint32_t r1 = rnd();
                const int x = static_cast<int>(r1 % static_cast<std::uint32_t>(ax1 - ax0 ? ax1 - ax0 : 1)) + ax0;
                const std::uint32_t r2 = rnd();
                const int y = static_cast<int>(r2 % static_cast<std::uint32_t>(ay1 - ay0 ? ay1 - ay0 : 1)) + ay0;
                if (!w.inBounds(x, y)) continue;
                const std::uint32_t f = w.cell(x, y).flags;
                const auto i2 = [&](std::ptrdiff_t o) { return static_cast<std::int32_t>(D(o)); };
                if ((f & 0x103f8) == 0 && (f & cell_flags::kDriveOn) &&
                    (B(0xb) == 0 || ((x < i2(0xf2c) || i2(0xf34) < x || y < i2(0xf30)) && i2(0xf38) < y))) {
                    clearUnits();
                    if (u.state != UnitState::Dead) u.selected = true;
                    moveOrder(x, y);
                }
            }
        } else {
            const int ax0 = std::max(B(ar) - 4, 0), ax1 = std::min(B(ar + 1) + 4, w.width_ - 1);
            const int ay0 = std::max(B(ar + 2) - 4, 0), ay1 = std::min(B(ar + 3) + 4, w.height_ - 1);
            for (int pass = 0; pass < 4; ++pass)
                for (int y = ay0; y < ay1; ++y)
                    for (int x = ax0; x < ax1; ++x) {
                        const std::uint32_t f = w.cell(x, y).flags;
                        if (dipl(static_cast<int>(f & 7)) != 1) continue;
                        if ((f & 1000) == 0) {
                            int bs = 0, bi = 0;
                            if ((f & cell_flags::kBuilding) && w.buildingAt(x, y, bs, bi)) {
                                const Building& b = w.sides_[bs & 7].buildings[bi];
                                if (!(b.flags & 0x4000) && T.buildings[b.type].specialBehavior != building_behaviour::kWall) {
                                    attackBuildingWithPart(g, bi, bs, 0x28);
                                    return;
                                }
                            }
                        } else {
                            int us = 0, uidx = 0;
                            const Unit* t = w.getUnitOnTile(x * 32, y * 32, us, uidx, false);
                            if (t && !(t->flags & unit_flags::kCloaked)) {
                                enemySeen = true;
                                const int beh = T.units[t->type].specialBehavior;
                                const int n = beh == behaviour::kSaboteur ? 6 : beh == behaviour::kEngineer ? 4
                                                                                  : (beh == 16 ? 6 : 2);
                                for (int k = 0; k < n; ++k)
                                    if (sendNearestUnit(g, x, y) && B(0x1d) != 0 &&
                                        gB(g, gArea) == static_cast<int>(static_cast<std::int8_t>(B(0x1d52))) &&
                                        (w.gameTicks_ & 0xf) == 0)
                                        SD(0xf18, D(0xf18) + 1);
                            }
                        }
                    }
        }
        if (B(0x1d) != 0) {
            const bool baseArea = gB(g, gArea) == static_cast<int>(static_cast<std::int8_t>(B(0x1d52)));
            if (baseArea) SB(0x1d1d, enemySeen ? 1 : 0);
            if (baseArea && (w.gameTicks_ & 0xe0) == 0) {
                if (B(0x1d07) == 0xff) SB(0x1d07, g);
                SB(ar + 2, S.baseMinY);
                SB(ar + 3, S.baseMaxY);
                SB(ar + 0, S.baseMinX);
                SB(ar + 1, S.baseMaxX);
                if (S.firstBuilding != -1) {
                    const int area = (B(ar + 3) - B(ar + 2)) * (B(ar + 1) - B(ar + 0));
                    SD(ar + 8, x87Ftol(std::sqrt(static_cast<double>(area)) * static_cast<double>(D(0xf5c))));
                    return;
                }
                SD(ar + 8, 0);
                return;
            }
        }
        break;
    }
    case kTaskSaboteur: {
        Unit* first = nullptr;
        for (int i = S.firstUnit; i != -1 && !first; i = S.nextObject(i))
            if (S.units[i].groupId == static_cast<std::uint8_t>(g)) first = &S.units[i];
        if (first) {
            int ox, oy, osd, oi;
            const bool f = w.findNearestEnemy(first->posX >> 16, first->posY >> 16, s, 0xc0, ox, oy, osd, oi, 9, -1,
                                              false, false, true);
            const int dx = posTile(first->posX) - gB(g, gX), dy = posTile(first->posY) - gB(g, gY);
            if ((first->lastAttacked < 10 || static_cast<std::uint32_t>(dx * dx + dy * dy) < 0x40 || f) &&
                first->byte29 == 0xa0 && !(first->flags & unit_flags::kStealth))
                first->flags |= unit_flags::kStealth;
        }
        [[fallthrough]];
    }
    case kTaskAttackBuilding: case kTaskEngineers: {
        if (gD(g, gCounter) > 10 && doStopStartStuff(g, 8, true) && (rnd() & 0x3f) != 0 && task == kTaskAttackBuilding)
            toHuntAtAverage();
        if (!moraleCheck(g)) returnToBase(g);
        const int sub = gB(g, gSub);
        auto attackOn = [&]() {
            int x, y;
            groupAverage(g, x, y);
            attackBestBuilding(g, gB(g, gTargetSide), x, y);
        };
        if (sub == 7) {
            if (groupStable(g) && gD(g, gCounter) > 10) {
                if (static_cast<std::uint32_t>(B(0xf47)) < rnd() % 100) {
                    int x, y;
                    groupAverage(g, x, y);
                    const int ts = bestSideToAttack();
                    attackBestBuilding(g, ts, x, y);
                    gSD(g, gCounter, 0);
                    gSB(g, gSub, 8);
                    enemyBaseWaypoint(x, y, gB(g, gX), gB(g, gY));
                    sendGroupToPoint(x, y, g);
                    return;
                }
                attackOn();
                return;
            }
        } else if (sub == 8) {
            if (groupStable(g) && gD(g, gCounter) > 10) { attackOn(); return; }
        } else if (sub == 4) {
            bool calm = true;
            if (recentlyInAction(g)) { calm = false; if (gB(g, gSub) == 2) gSB(g, gSub, 3); }
            if (!calm || !groupExists(g)) { gSD(g, gTries, 0); return; }
            if (gD(g, gFlags) & 8) {
                gSD(g, gFlags, gD(g, gFlags) & ~8u);
                if (static_cast<std::int8_t>(defendGroupId(gB(g, gArea))) != -1) { returnToBase(g); return; }
                gSB(g, gTask, kTaskDefendArea);
            }
            gSD(g, gTries, gD(g, gTries) + 1);
            if (gD(g, gTries) > 2) { attackOn(); return; }
        }
        break;
    }
    }
}

// CAI__ShouldIBuyUnit 0x421e50: the price must not exceed cost * factor (CAI +0xf68, 0.60..0.89;
// 1.2 with fewer than 4 unit icons; 5.0 when the silos are nearly full). Buys armed units,
// harvesters when wanted (+0xf54), and (rand & 7) == 5 an MCV when the base has none.
bool AiController::shouldIBuyUnit(int type) {
    type &= 0xff;
    const std::uint32_t cost = w.unitCost(type, s);
    float f = F(0xf68);
    if (S.unitIcons.size() < 4) f = 1.2f;
    if (static_cast<std::uint32_t>(S.maxStorage) - 200u <= static_cast<std::uint32_t>(S.spice)) f = 5.0f;
    // x87: (int64 cost) * f compared with (int64 price), both exact in double.
    const double limit = static_cast<double>(cost) * static_cast<double>(f);
    const double price = static_cast<double>(static_cast<std::uint32_t>(S.starportPrice[type]));
    if (!(limit >= price)) return false;
    if (T.units[type].primaryWeapon != -1) return true;
    if (T.units[type].specialBehavior == behaviour::kHarvester && B(0xf54) != 0) return true;
    const int mcv = gid(group_ids::kMcv);
    if (type == mcv && primary(0) == -1 && S.unitsExistPerType[mcv] == 0 && S.starportOrdered[mcv] == 0 && B(3) != 0)
        return (rnd() & 7) == 5;
    return false;
}

// CAI__BuyUnitsFromStarport 0x421cf0 (every 128 ticks, with a primary starport and no frigate on
// its way): for each starport type it wants (at most 6 types), pick the whole stock (MCVs: one)
// as long as credits - price stay above the reserve (+0x1074; credits do not change while the
// picks are only queued), then send the purchase order.
void AiController::buyUnitsFromStarport() {
    if ((w.gameTicks_ & 0x7f) != 0 || primary(7) == -1 || S.starportFrigateBusy) return;
    bool bought = false;
    int types = 0;
    const int mcv = gid(group_ids::kMcv);
    for (int t : S.starportTypes) {
        if (t < 0 || t >= formats::kMaxUnitTypes) continue;  // (vanilla always lists 8 types)
        const int type = t & 0xff;  // (byte read of the int list)
        if (!shouldIBuyUnit(type) || types >= 6) continue;
        ++types;
        auto cnt = static_cast<std::uint8_t>(S.starportStock[type]);
        // Quirk: the "one MCV" rule compares the template's SpecialBehavior with the MCV *type
        // index* (groupIds[15]); in vanilla data no starport unit matches.
        if (T.units[type].specialBehavior == mcv && cnt != 0) cnt = 1;
        while (D(0x1074) < credits() - static_cast<std::uint32_t>(S.starportPrice[type]) && cnt != 0) {
            if (!S.starportPurchasePending)  // GenerateStarportPickOrder 0x45a200
                w.orders_.push_back({World::Order::StarportPick, s, kNoIndex, type, 0});
            --cnt;
            bought = true;
        }
    }
    if (!bought) return;
    S.starportFrigateBusy = true;
    if (!S.starportPurchasePending) {  // GenerateStarportPurchaseOrder 0x45a2b0
        S.starportPurchasePending = true;
        w.orders_.push_back({World::Order::StarportPurchase, s, kNoIndex, 0, 0});
    }
}

int AiController::findGroupForDelivery(int delivery) {
    int g = 0xff;
    for (int i = 1; i < 32; ++i) {
        if (gB(i, gUsed) == 0 && g == 0xff) g = i;
        if (gB(i, gUsed) == 1 && gB(i, gTask) == kTaskDelivery && gB(i, gSub) == (delivery & 0xff)) g = i;
    }
    const int gi = static_cast<std::int8_t>(g);  // (char) index in the original
    if (gB(gi, gUsed) == 0) {
        gSB(gi, gUsed, 1);
        gSB(gi, gSub, delivery);
        gSB(gi, gTask, kTaskDelivery);
        gSD(gi, gStrength, 100);
        gSW(gi, gTargetIndex, 0xffff);
    }
    return g;
}

void AiController::groupNewUnits(int baseGroup, bool force) {
    int unsorted = 0, fremen = 0, saboteurs = 0, guards = 0;
    bool reorder = false;
    clearUnits();
    Unit* last = nullptr;
    int g = baseGroup & 0xff;
    if ((w.gameTicks_ & 0x1f) == static_cast<std::uint32_t>(s) || force) {
        int freeG = firstFreeGroup();
        if (freeG == 0) return;
        const int berserk = B(0x1e);
        for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
            Unit& u = S.units[i];
            last = &u;
            if (berserk == 0) {
                if (u.groupId != 0xff && gB(u.groupId, gUsed) == 0) u.groupId = 0xff;
                if (u.groupId != 0xff || (u.flags & 0x840)) continue;
                const int beh = ut(u).specialBehavior;
                if (beh == behaviour::kHarvester || beh == behaviour::kCarryall || beh == behaviour::kMcv) continue;
                if (beh == behaviour::kSaboteur) { ++saboteurs; continue; }
                if (beh == behaviour::kFremen) { ++fremen; continue; }
                const UnitState st = u.state;
                if (st == UnitState::LeavingBuilding || st == UnitState::Invalid || st == UnitState::MovingAway ||
                    u.posSteps != 0) {
                    ++unsorted;
                    continue;
                }
                if (!(u.flags & 0x80) && !(u.flags & 0x1000)) {
                    if (!(u.flags & 0x40000)) {
                        u.groupId = static_cast<std::uint8_t>(g);
                        if (st != UnitState::Dead) {
                            u.selected = true;
                            ++guards;
                        }
                    } else {
                        u.flags &= ~0x40000u;
                        if (S.deliveries[u.byte2E % 10].type == 2) {
                            u.groupId = B(0x1d3c);
                        } else {
                            const int dg = findGroupForDelivery(u.byte2E);
                            u.groupId = static_cast<std::uint8_t>(dg);
                            gSD(dg, gFlags, gD(dg, gFlags) | 4);
                        }
                    }
                } else {
                    const int ng = firstFreeGroup();
                    freeG = ng;
                    if (ng) {
                        u.groupId = static_cast<std::uint8_t>(ng);
                        gSB(ng, gTask, kTaskHunt);
                        gSD(ng, gTime, 2);
                        gSD(ng, gStrength, groupStrength(ng, kTaskHunt));
                        gSB(ng, gUsed, 1);
                        gSB(ng, gSub, 4);
                        gSD(ng, gCounter, 0);
                        gSW(ng, gTargetIndex, 0xffff);
                        u.flags &= ~0x80u;
                        if (beh == 7 && (rnd() & 1)) {
                            u.selected = true;
                            deployOrder(u.index);
                            u.selected = false;
                        }
                    }
                }
            } else if (berserk < 3) {
                if (u.groupId != 0xff && gB(u.groupId, gUsed) == 0) u.groupId = 0xff;
                if (!(u.flags & 0x840) && u.groupId == 0xff && u.posSteps == 0) {
                    u.groupId = static_cast<std::uint8_t>(freeG);
                    if (u.state == UnitState::Guarding) {
                        w.unitAdjustState(u, UnitState::Idle);
                        selectUnit(u.index);
                    }
                    ++unsorted;
                }
            }
        }
        const int pg = freeG;  // param_1 after the loop
        gSD(g, gStrength, groupStrength(g, gB(g, gTask)));
        int rg = pg;
        if (unsorted == 0) {
            for (int k = 1; k < 32; ++k)
                if (gD(k, gFlags) & 1) {
                    gSD(k, gFlags, gD(k, gFlags) & ~1u);
                    rg = k;
                    reorder = true;
                    break;
                }
        }
        if (reorder) {
            const int mode = gB(rg, gMode);
            if (mode == 0) {
                setGroupToGuard(rg);
            } else if (mode == 1) {
                if (last) {
                    gSB(rg, gX, posTile(last->posX));
                    gSB(rg, gY, posTile(last->posY));
                    gSW(rg, gTargetIndex, 0xffff);
                    gSB(rg, gTargetSide, 0);
                    gSD(rg, gTime, 10000);
                    gSB(rg, gTask, kTaskHunt);
                    gSB(rg, gSub, 4);
                    gSD(rg, gCounter, 0);
                }
            } else if (mode == 2) {
                returnToBase(rg);
            }
            selectGroup(rg, false);
            commence(rg);
        }
        if (B(0x1e) == 1 && unsorted != 0) {
            huntMode(rg, true);
            gSD(rg, gStrength, groupStrength(rg, gB(rg, gTask)));
            commence(rg);
        }
    }
    if (fremen > 4 && B(0x1e) == 0) doFremen(force);
    if (saboteurs != 0 && B(0x1e) == 0) doSaboteur(force);
    if (guards != 0) guardOrder();
}

void AiController::doFremen(bool force) {
    if (force) return;
    const int g = firstFreeGroup();
    if (!g) return;
    forUnits([&](Unit& u) {
        if (u.groupId == 0xff && ut(u).specialBehavior == behaviour::kFremen) u.groupId = static_cast<std::uint8_t>(g);
    });
    const int ts = bestSideToAttack();
    int x, y;
    groupAverage(g, x, y);
    const Building* b = bestBuilding(ts, x, y, 0.25f);
    if (!b) { mergeGroups(g, B(0x1d3c)); return; }
    gSB(g, gTask, kTaskAttackBuilding);
    gSW(g, gTargetIndex, b->index);
    gSB(g, gTargetSide, ts);
    gSB(g, gX, (b->posX >> 16) / 32);
    gSB(g, gY, (b->posY >> 16) / 32);
    gSB(g, gSub, 2);
    gSD(g, gTries, 0);
    gSD(g, gStrength, groupStrength(g, gB(g, gTask)));
    gSB(g, gUsed, 1);
    clearUnits();
    selectGroup(g, true);
    commence(g);
}

void AiController::doSaboteur(bool force) {
    if (force) return;
    const int g = firstFreeGroup();
    if (!g) return;
    bool found = false;
    for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
        Unit& u = S.units[i];
        if (u.groupId == 0xff && ut(u).specialBehavior == behaviour::kSaboteur && u.byte29 > 0x9f) {
            u.groupId = static_cast<std::uint8_t>(g);
            found = true;
            break;
        }
    }
    if (!found) return;
    const int ts = bestSideToAttack();
    int x, y;
    groupAverage(g, x, y);
    const Building* b = bestBuilding(ts, x, y, 0.25f);
    if (!b) { mergeGroups(g, B(0x1d3c)); return; }
    gSB(g, gTask, kTaskSaboteur);
    gSW(g, gTargetIndex, b->index);
    gSB(g, gTargetSide, ts);
    gSB(g, gX, (b->posX >> 16) / 32);
    gSB(g, gY, (b->posY >> 16) / 32);
    gSB(g, gSub, 2);
    gSD(g, gTries, 0);
    gSD(g, gStrength, 100);
    gSB(g, gUsed, 1);
    clearUnits();
    selectGroup(g, true);
    commence(g);
    clearUnits();
}

// -------------------------------------------------------------------------------------------
// Production and base building

bool AiController::canBuildUnit(int type) {
    type &= 0xff;
    if (!w.canUnitBeBuilt(s, type, true)) return false;
    if (credits() < w.unitCost(type, s)) return false;
    for (int t : S.unitIcons)
        if (t == type) return true;
    return false;
}

bool AiController::inIconList(int type) {
    for (int t : S.unitIcons)
        if (t == (type & 0xff)) return true;
    return false;
}

bool AiController::shouldIBuildUnit(int type) {
    if (credits() <= D(0x106c) + w.unitCost(type & 0xff, s))
        if (B(8) == 0 && B(0x1d1d) == 0) return false;
    return true;
}

int AiController::findBestUnitToBuild() {
    const int refineryG = gid(group_ids::kRefinery), mcv = gid(group_ids::kMcv), harv = gid(group_ids::kHarvester);
    SB(0xf54, static_cast<std::uint32_t>(static_cast<std::uint8_t>(S.harvesterCount)) <
                      static_cast<std::uint32_t>(groupCount(refineryG)) * B(0x1d04)
                  ? 1 : 0);
    if (B(0xf54) && canBuildUnit(harv)) return harv;  // (ShouldIBuildHarvester always says yes)
    if (B(3) && primary(0) == -1 && S.unitsExistPerType[mcv] == 0 && (rnd() & 7) == 1) return mcv;
    std::array<bool, 60> listed{};
    float totalPri = 0.0f;
    std::uint32_t totalExist = 0;
    for (int i = 0; i < 60; ++i)
        if (inIconList(i)) {
            totalPri = static_cast<float>(static_cast<double>(totalPri) + F(0x19f8 + i * 4));
            totalExist += static_cast<std::uint32_t>(S.unitsExistPerType[i]);
            listed[i] = true;
        }
    float best = 100000.0f;
    int pick = 0xff;
    for (int i = 0; i < 60; ++i) {
        if (!listed[i]) continue;
        const double a = static_cast<double>(F(0x19f8 + i * 4)) / totalPri;
        const double e = static_cast<double>(static_cast<std::uint32_t>(S.unitsExistPerType[i])) / static_cast<double>(totalExist);
        const float af = static_cast<float>(a), ef = static_cast<float>(e);
        double v;
        if (fpuC3(a, 0.0)) {
            v = 100000.0;
        } else {
            const std::uint32_t r = rnd() % static_cast<std::uint32_t>(B(10) ? B(10) : 1);
            v = static_cast<double>(ef) / af * static_cast<double>(static_cast<std::int32_t>((r & 0xff) + 100)) * kF001;
        }
        if (fpuC0(v, best)) { best = static_cast<float>(v); pick = i; }
    }
    if (pick != 0xff && shouldIBuildUnit(pick)) {
        if (B(0xf54) == 0) return pick;
        if (canBuildUnit(pick)) return pick;
    }
    return -1;
}

bool AiController::shouldIBuildNow() {
    const std::uint32_t rate = B(0x1d1d) == 0 ? D(0x1d2c) : D(0xf28);
    const std::uint32_t c = D(0x1d28) + 1;
    SD(0x1d28, c);
    if (rate <= c) {
        SD(0x1d28, 0);
        return true;
    }
    return false;
}

void AiController::doRepairs() {
    const std::uint32_t every = D(0x1d0c);
    if (every == 0) return;  // (division by zero in the original)
    forBuildings([&](Building& b) {
        if (static_cast<std::uint32_t>(b.index) % every != w.gameTicks_ % every) return;
        const auto& t = T.buildings[b.type];
        if (!(t.flags & building_flags::kSelectable)) return;
        if (b.health < static_cast<std::int32_t>(x87Ftol(static_cast<double>(t.hitPoints) * F(0x13ac + b.type * 4))) &&
            10 < credits())
            b.flags |= building_flags::kRepairing;
    });
}

void AiController::doBuildUnits() {
    if (!shouldIBuildNow()) return;
    const int t = findBestUnitToBuild();
    if (t == -1 || !canBuildUnit(t)) return;
    int a1, a2, a3, a4;
    if (w.findUnitExitBuilding(t, s, a1, a2, a3, a4, true)) unitPickOrder(t);
}

void AiController::doBuildCarryalls() {
    if (D(0x1064) <= credits() &&
        static_cast<std::uint8_t>(S.carryallCount) < static_cast<std::uint32_t>(groupCount(gid(group_ids::kRefinery))) * B(0x1d05)) {
        const int t = w.sideVersionOfUnit(S, gid(0), false);
        if (canBuildUnit(t) && static_cast<std::uint8_t>(S.harvesterCount) != 0)
            unitPickOrder(w.sideVersionOfUnit(S, gid(0), false));
    }
}

void AiController::doOverrides() {
    if (B(7) != 0 || (w.gameTicks_ & 7) != static_cast<std::uint32_t>(s)) return;
    SB(8, 0);
    SB(9, 0);
    if (S.spice == S.maxStorage) { SB(9, 1); SB(8, 1); }
    if (S.buildingIcons.empty()) return;
    const std::uint8_t refs = groupCount(gid(group_ids::kRefinery));
    const std::uint8_t harvs = static_cast<std::uint8_t>(S.harvesterCount);
    if (S.spice + S.cash == 0) {
        SB(6, 1);
        if (refs != 0 || harvs == 0) return;
    }
    SB(6, 0);
    if (S.powerDemand <= S.powerOutput) return;
    bool repairing = false;
    forBuildings([&](Building& b) {
        const auto& t = T.buildings[b.type];
        if (t.powerConsumption < 0 && b.health < static_cast<std::int32_t>(t.hitPoints)) {
            repairing = true;
            b.flags |= building_flags::kRepairing;
        }
    });
    SB(9, 1);
    if (repairing || credits() >= 400) return;
    Building* pick = nullptr;
    for (int i = S.firstBuilding; i != -1 && !pick; i = S.nextObject(i))
        if (T.buildings[S.buildings[i].type].requireEnoughPower) pick = &S.buildings[i];
    if (!pick)
        for (int i = S.firstBuilding; i != -1 && !pick; i = S.nextObject(i)) {
            Building& b = S.buildings[i];
            const double lim = static_cast<double>(static_cast<float>(T.buildings[b.type].hitPoints)) * static_cast<double>(0.1f);
            const double hp = static_cast<float>(b.health);
            if (!(lim < hp || lim == hp) && b.lastAttacked <= 9) pick = &b;
        }
    if (!pick) {
        std::uint8_t best = 0;
        forBuildings([&](Building& b) {
            if (best < T.buildings[b.type].sellPriority) { pick = &b; best = T.buildings[b.type].sellPriority; }
        });
        if (!pick) return;
    }
    if ((pick->flags & 0x400000e) == 0) {
        w.sellBuilding(s, pick->index);  // ModelBuildingSell (direct call)
        pick->flags |= 0x4000000;
    }
}

bool AiController::canIDeployMcvHere(int x, int y) {
    if (x < 2 || y < 2 || w.width_ - 2 < x || w.height_ - 2 < y) return false;
    for (int yy = y - 1; yy < y + 2; ++yy)
        for (int xx = x - 1; xx < x + 2; ++xx) {
            const std::uint32_t f = w.cell(xx, yy).flags;
            if ((f & 0xa000) != 0xa000) return false;
            if ((f & 0x3f8) && static_cast<int>(f & 7) != s) return false;
        }
    return true;
}

bool AiController::placeToMoveTo(int x0, int y0, int x1, int y1, int r, int& ox, int& oy) {
    int x = ox, y = oy, dir = 0, leg = 1, left = 1;
    const int lim = (r & 0xff) * 2 + 2;
    do {
        x += kSpiralDx[dir];
        y += kSpiralDy[dir];
        if (x >= 0 && y >= 0 && x < w.width_ && y < w.height_ &&
            (x < (x0 & 0xff) || y < (y0 & 0xff) || (x1 & 0xff) < x || (y1 & 0xff) < y) &&
            (w.cell(x, y).flags & 0x63f8) == 0x6000) {
            ox = x;
            oy = y;
            return true;
        }
        if (--left == 0) {
            leg = (leg + kSpiralLegInc[dir]) & 0xff;
            dir = (dir + 1) & 3;
            left = leg;
        }
    } while (leg < lim);
    return false;
}

bool AiController::kickUnitsOutOfArea(int x0, int y0, int x1, int y1, const Unit* except) {
    x0 &= 0xff; y0 &= 0xff; x1 &= 0xff; y1 &= 0xff;
    for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
        Unit& u = S.units[i];
        const int ux = posTile(u.posX) & 0xff, uy = posTile(u.posY) & 0xff;
        if (x0 <= ux && ux <= x1 && y0 <= uy && uy <= y1 && &u != except && w.unitCanTakeOrders(&u, false) &&
            (u.state == UnitState::Idle || u.state == UnitState::Guarding)) {
            clearUnits();
            u.selected = true;
            int x = ux, y = uy;
            placeToMoveTo(x0 - 1, y0 - 1, x1 + 1, y1 + 1, 8, x, y);
            moveOrder(x, y);
            return true;
        }
    }
    return false;
}

void AiController::dealWithMcv(Unit& u) {
    int x = posTile(u.posX), y = posTile(u.posY);
    if (!canIDeployMcvHere(x, y)) {
        int dir = 0, leg = 1, left = 1;
        for (;;) {
            x += kSpiralDx[dir];
            y += kSpiralDy[dir];
            if (canIDeployMcvHere(x, y)) break;
            if (--left == 0) {
                leg += kSpiralLegInc[dir];
                dir = (dir + 1) & 3;
                left = leg;
            }
            if (leg > 0xd) return;
        }
        clearUnits();
        if (u.state == UnitState::Dead) return;
        u.selected = true;
        SD(0xf2c, static_cast<std::uint32_t>(x - 1));
        SD(0xf34, static_cast<std::uint32_t>(x + 1));
        SD(0xf30, static_cast<std::uint32_t>(y - 1));
        SD(0xf38, static_cast<std::uint32_t>(y + 1));
        moveOrder(x, y);
        SB(0x1d49, 1);
    }
    kickUnitsOutOfArea(x - 1, y - 1, x + 1, y + 1, &u);
}

void AiController::doMcvs() {
    const bool noIcons = S.buildingIcons.empty();
    if (!noIcons) SB(0x19ec, 1);
    if ((w.gameTicks_ & 0x3f) != 0) return;
    forUnits([&](Unit& u) {
        if (ut(u).specialBehavior != behaviour::kMcv || u.state != UnitState::Idle || !w.unitCanTakeOrders(&u, false)) return;
        if (!w.canDeployMcv(s, u.index)) dealWithMcv(u);  // CheckIfMCVCanBeDeployedOn 0x443250
        else if (u.byte2E != 0 || noIcons) deployOrder(u.index);
    });
}

void AiController::monitorHarvesters() {
    if (((w.gameTicks_ + static_cast<std::uint32_t>(s)) & 0x1f) != 0) return;
    forUnits([&](Unit& u) {
        const auto& t = ut(u);
        if (t.primaryWeapon == -1 && t.canCrushInfantry && u.lastAttacked < 10 && u.attackerIndex != kNoIndex &&
            kindOf(u.attackerSide, u.attackerIndex) == 1) {
            const Unit& at = w.sides_[u.attackerSide & 7].units[u.attackerIndex];
            if (T.units[at.type].isInfantry && w.unitCanTakeOrders(&u, false) && u.state != UnitState::Dead) {
                clearUnits();
                u.selected = true;
                moveOrder(posTile(at.posX) & 0xff, posTile(at.posY) & 0xff);
            }
        }
        if (t.specialBehavior == behaviour::kHarvester && !(u.flags & unit_flags::kCarried)) {
            if (u.state == UnitState::Idle) w.unitAdjustState(u, UnitState::MovingToHarvest);
            if (u.lastAttacked < 10 && u.health < static_cast<std::int32_t>(t.hitPoints >> 2) &&
                u.state != UnitState::MovingToRefinery)
                w.unitAdjustState(u, UnitState::MovingToRefinery);
        }
    });
}

void AiController::monitorBuildings() {
    if ((w.gameTicks_ & 7) != static_cast<std::uint32_t>(s)) return;
    forBuildings([&](Building& b) {
        if ((b.flags & 0x2000000) && !(b.flags & 0x4000000)) {
            b.flags &= ~0x2000000u;
            w.sellBuilding(s, b.index);
            b.flags |= 0x4000000;
        }
    });
    for (int i = 0; i < B(0xf0c); ++i) {
        const std::ptrdiff_t e = static_cast<std::ptrdiff_t>(kInitPos) + i * 16;
        const int idx = W(e + 2) % kObjectsPerSide;
        // (the original reads the slot's state field whatever it now holds; a freed slot keeps
        // the dead building's data)
        const bool dead = S.links[idx].kind == SlotKind::Unit ? false : S.buildings[idx].state == 17;
        if (dead && B(e + 0xd) != 0) {
            SB(e + 0xd, 0);
            SD(e + 4, 0);
            SD(e + 8, D(0x153c + B(e + 0xc) * 4));
        }
    }
}

void AiController::ownership(int x, int y, std::uint8_t& fl) {
    if (x < 0 || x >= w.width_ || y <= 0 || y >= w.height_) return;
    const std::uint32_t f = w.cell(x, y).flags;
    if ((f & cell_flags::kBuilding) && static_cast<int>(f & 7) == s) fl |= 1;
    if ((f & cell_flags::kConcrete) && static_cast<int>((f >> 17) & 7) == s) fl |= 2;
}

std::uint32_t AiController::canAIPlaceBuilding(int type, int x, int y) {
    const std::uint32_t mask = bt(type).tilesOccupiedAll;
    std::uint8_t fl = 0;
    bool outside = false, ok = true;
    std::uint32_t freeCount = 0, bit = 1;
    x &= 0xff;
    y &= 0xff;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            if (!(mask & bit)) continue;
            const int tx = x + c, ty = y + r;
            if (tx < w.width_ && tx > 0 && ty < w.height_ && ty > 0) {
                const int y0 = (ty - 2 < 1) ? 0 : ty - 2, y1 = std::min(ty + 3, w.height_);
                const int x0 = (tx - 2 < 1) ? 0 : tx - 2, x1 = std::min(tx + 3, w.width_);
                for (int yy = y0; yy < y1; ++yy)
                    for (int xx = x0; xx < x1; ++xx) {
                        ownership(xx, yy, fl);
                        if (!(w.cell(xx, yy).flags & cell_flags::kBuilding)) ++freeCount;
                    }
            } else {
                outside = true;
            }
        }
    if (!(fl & 1) || outside) return 0;
    bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            const int tx = c + x, ty = y + r;
            if (!(mask & bit) || tx >= w.width_ || ty >= w.height_) continue;
            const bool near = (fl & 1) || (fl & 2);
            const std::uint32_t f = w.cell(tx, ty).flags;
            if ((f & 0x13f8) || !(f & cell_flags::kBuildOn) || ty < 1 || !near) ok = false;
        }
    return ok ? freeCount + 1 : 0;
}

bool AiController::canAIPlaceConcrete(int type, int x, int y) {
    const std::uint32_t mask = bt(type).tilesOccupiedSolid;
    std::uint8_t fl = 0;
    std::uint32_t bit = 1;
    x &= 0xff;
    y &= 0xff;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            const int tx = x + c, ty = y + r;
            if (!(mask & bit) || tx >= w.width_ || ty >= w.height_) continue;
            const int y0 = (ty - 2 < 1) ? 0 : ty - 2, y1 = std::min(ty + 3, w.height_);
            const int x0 = (tx - 2 < 1) ? 0 : tx - 2, x1 = std::min(tx + 3, w.width_);
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) ownership(xx, yy, fl);
        }
    const bool near = fl != 0;
    bool r1 = false;
    bit = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            const int tx = c + x, ty = y + r;
            if (!(mask & bit) || tx >= w.width_ || ty >= w.height_) continue;
            const std::uint32_t f = w.cell(tx, ty).flags;
            if (!(f & cell_flags::kConcrete) && (f & cell_flags::kBuildOn) && near && ty > 0) r1 = true;
        }
    return r1;
}

bool AiController::concreteAlready(int x, int y) {
    x &= 0xff;
    y &= 0xff;
    if (x >= w.width_ - 2 || y >= w.height_ - 2) return false;
    bool all = true;
    for (int yy = y; yy < y + 2; ++yy)
        for (int xx = x; xx < x + 2; ++xx)
            if (!(w.cell(xx, yy).flags & cell_flags::kConcrete)) all = false;
    return all;
}

bool AiController::noFriendlyUnits(int type, int x, int y) {
    const std::uint32_t mask = bt(type).tilesOccupiedSolid;
    std::uint32_t bit = 1;
    bool r = true;
    for (int rr = 0; rr < 4; ++rr)
        for (int c = 0; c < 4; ++c, bit <<= 1) {
            const int tx = c + (x & 0xff), ty = (y & 0xff) + rr;
            if (!(mask & bit) || tx >= w.width_ || ty >= w.height_) continue;
            const std::uint32_t f = w.cell(tx, ty).flags;
            if ((f & cell_flags::kBuildOn) && (f & 0x3f8) && static_cast<int>(f & 7) == s) r = false;
        }
    return r;
}

bool AiController::findAlternativePlace(int& x, int& y) {
    bool r = false;
    const int y0 = (y - 1 < 1) ? 0 : y - 1, y1 = std::min(y + 2, w.height_ - 1);
    const int x0 = (x - 1 < 1) ? 0 : x - 1, x1 = std::min(x + 2, w.width_ - 1);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
            const std::uint32_t f = w.cell(xx, yy).flags;
            if ((f & 0x3f8) == 0 && (f & cell_flags::kDriveOn)) { x = xx; y = yy; r = true; }
        }
    return r;
}

bool AiController::needBuilding(int group) {
    for (int i = 0; i < B(0xf0c); ++i) {
        const std::ptrdiff_t e = static_cast<std::ptrdiff_t>(kInitPos) + i * 16;
        if (B(e + 0xd) == 0 && bt(B(e + 0xc)).buildingGroup == (group & 0xff)) return true;
    }
    return false;
}

bool AiController::findNewPosition(int& ox, int& oy, int type) {
    if (S.firstBuilding == -1) return false;
    bool found = false;
    int lx = 0, ly = 0;
    for (int i = 0; i < B(0xf0c); ++i) {
        const std::ptrdiff_t e = static_cast<std::ptrdiff_t>(kInitPos) + i * 16;
        if (B(e + 0xd) == 0 && B(e + 0xc) == (type & 0xff)) {
            lx = B(e);
            ly = B(e + 1);
            if (canAIPlaceBuilding(type, lx, ly)) { found = true; ox = lx; oy = ly; }
        }
    }
    if (found) return true;
    bool r = false;
    for (int i = 0; i < B(0xf0c); ++i) {
        const std::ptrdiff_t e = static_cast<std::ptrdiff_t>(kInitPos) + i * 16;
        if (B(e + 0xd) == 0 && B(e + 0xc) == (type & 0xff)) {
            findAlternativePlace(lx, ly);
            if (canAIPlaceBuilding(type, lx, ly)) { r = true; ox = lx & 0xff; oy = ly & 0xff; }
        }
    }
    return r;
}

bool AiController::findRandomPosition(int& ox, int& oy, int type, int tries) {
    if (S.firstBuilding == -1) return false;
    const int x0 = (S.baseMinX - 4 < 1) ? 0 : (S.baseMinX - 4) & 0xff;
    const int y0 = (S.baseMinY - 4 < 1) ? 0 : (S.baseMinY - 4) & 0xff;
    const int x1 = std::min(S.baseMaxX + 2, w.width_ - 1) & 0xff;
    const int y1 = std::min(S.baseMaxY + 2, w.height_ - 1) & 0xff;
    std::uint32_t best = 0;
    for (int k = tries & 0xff; k != 0; --k) {
        const std::uint32_t dx = static_cast<std::uint32_t>(x1 - x0), dy = static_cast<std::uint32_t>(y1 - y0);
        const std::uint32_t r1 = rnd();
        const int x = static_cast<std::uint8_t>((dx ? r1 % dx : 0) + x0);
        const std::uint32_t r2 = rnd();
        const int y = static_cast<std::uint8_t>((dy ? r2 % dy : 0) + y0);
        const std::uint32_t v = canAIPlaceBuilding(type, x, y);
        if (best < v) { ox = x; oy = y; best = v; }
    }
    return best != 0;
}

int AiController::bestBuildingToBuild() {
    int pick = 0xff;
    float best = 100000.0f;
    for (int t : S.buildingIcons) {
        const int g = T.buildings[t].buildingGroup;
        const std::uint32_t cnt = groupCount(g);
        const float f = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(cnt + 1)) / F(0x16cc + g * 4));
        if ((D(0x1068) < credits() || B(9) != 0) && fpuC0(f, best) && cnt < D(0x185c + g * 4) &&
            T.buildings[w.sideVersionOfBuilding(S, g, false) & 0xff].specialBehavior != building_behaviour::kConcrete &&
            (B(3) != 0 || needBuilding(g))) {
            pick = g;
            best = f;
        }
    }
    return static_cast<std::int8_t>(pick);
}

bool AiController::buildStuck() {
    if (S.buildingQueue.type == -1) {
        const std::uint32_t c = D(0xc) + 1;
        SD(0xc, c);
        if (static_cast<std::uint32_t>(std::max(kUnknownNetVar2, 1)) * 10 < c) {
            SB(0x1d49, 0);
            buildingCancelOrder(S.buildingQueue.type);
            SB(0x1d50, 0);
            SD(0xc, 0);
            return true;
        }
    } else {
        SD(0xc, 0);
    }
    return false;
}

void AiController::pickNextPiece() {
    if (B(0x1d50) != 0) buildingPickOrder(S.buildingIcons.empty() ? 0xff : S.buildingIcons[0]);
    else buildingPickOrder(B(0x1d4a));
    SB(0x1f, 0);
}

void AiController::placeBuiltBuilding() {
    const int conc = S.buildingIcons.empty() ? 0xff : (S.buildingIcons[0] & 0xff);
    const int bx = B(0x1d47), by = B(0x1d48);
    if (B(0x1d50) == 0) {
        if (canAIPlaceBuilding(B(0x1d4a), bx, by)) {
            if (S.buildingQueue.type == B(0x1d4a) && !S.buildingQueue.placing) {
                placeOrder(B(0x1d4a), bx, by);
                S.buildingQueue.placing = true;
            }
            SB(0x1d49, 0);
            buildingCancelOrder(S.buildingQueue.type);
            SB(0x1d50, 0);
            SB(0x1f, 0);
            SB(0xb, 0);
            return;
        }
        const std::uint32_t c = D(0x1d4c) + 1;
        SD(0x1d4c, c);
        if (c < 0x33) return;
        int nx, ny;
        if (findRandomPosition(nx, ny, B(0x1d4a), 8)) {
            SB(0x1d47, nx);
            SB(0x1d48, ny);
        }
        return;
    }
    // Concrete slabs first: TL (1), TR (2), BL (4), BR (8).
    const int ox[4] = {0, 2, 0, 2}, oy[4] = {0, 0, 2, 2};
    for (int k = 0; k < 4; ++k) {
        const int bit = 1 << k;
        if (!(B(0x1d50) & bit)) continue;
        const int x = bx + ox[k], y = by + oy[k];
        if ((canAIPlaceConcrete(conc, x, y) || concreteAlready(x, y)) && noFriendlyUnits(conc, x, y)) {
            if (!concreteAlready(x, y) && !S.buildingQueue.placing) {
                placeOrder(conc, x, y);
                S.buildingQueue.placing = true;
            }
            SB(0x1d50, B(0x1d50) & ~bit);
            SB(0x1f, 1);
            break;
        }
    }
    if (S.buildingQueue.placing) return;
    for (int k = 0; k < 4; ++k) {
        const int bit = 1 << k;
        if (!(B(0x1d50) & bit)) continue;
        if (canAIPlaceConcrete(conc, bx + ox[k], by + oy[k])) continue;
        SB(0x1d50, B(0x1d50) & ~bit);
        if (B(0x1d50) == 0) buildingCancelOrder(conc);
        return;
    }
}

void AiController::rebuildBuildings() {
    if (((w.gameTicks_ + static_cast<std::uint32_t>(s)) & 0xf) != 0) return;
    if (S.buildingIcons.empty()) return;
    const int conc = S.buildingIcons[0] & 0xff;
    if (D(0x1068) < credits()) SB(9, 0);
    if (B(0x1d49) == 0) {
        SB(0x1f, 0);
        const int g = bestBuildingToBuild();
        if (g == -1) return;
        const int type = w.sideVersionOfBuilding(S, g, false) & 0xff;
        int x = 0, y = 0;
        const bool ok = B(3) == 0 ? findNewPosition(x, y, type) : findRandomPosition(x, y, type, 8);
        if (!ok || S.buildingQueue.type != -1) return;
        SB(0x1d47, x);
        SB(0x1d48, y);
        SB(0x1d4a, type);
        SB(0x1d49, 1);
        SD(0x1d4c, 0);
        buildingPickOrder(conc);
        SB(0x1d50, 1);
        SD(0xf2c, static_cast<std::uint32_t>(x & 0xff));
        SD(0xf34, static_cast<std::uint32_t>((x & 0xff) + 2));
        SD(0xf30, static_cast<std::uint32_t>(y & 0xff));
        SD(0xf38, static_cast<std::uint32_t>((y & 0xff) + 2));
        const auto& t = bt(type);
        if (t.artWidth > 0x40) {
            SB(0x1d50, B(0x1d50) | 2);
            SD(0xf34, D(0xf34) + 2);
        }
        if (t.artHeight > 0x40) {
            SB(0x1d50, B(0x1d50) | 4);
            SD(0xf38, D(0xf38) + 2);
            if (t.artWidth > 0x40) SB(0x1d50, B(0x1d50) | 8);
        }
        return;
    }
    kickUnitsOutOfArea(B(0xf2c), B(0xf30), B(0xf34), B(0xf38), nullptr);
    if (buildStuck()) return;
    if (S.buildingQueue.type != -1 && S.buildingQueue.progress == kBuildDone && !S.buildingQueue.placing)
        placeBuiltBuilding();
    if (B(0x1f) != 0 && S.buildingQueue.type == -1) pickNextPiece();
}

void AiController::getInitialBuildingPositions() {
    if (B(3) != 0) return;
    for (int g = 0; g < 100; ++g) SD(0x185c + g * 4, 0);
    SB(0xf0c, 0);
    forBuildings([&](Building& b) {
        const auto& t = T.buildings[b.type];
        if (t.specialBehavior == building_behaviour::kWall) return;
        const std::ptrdiff_t e = static_cast<std::ptrdiff_t>(kInitPos) + B(0xf0c) * 16;
        SB(e + 0xc, b.type);
        SB(e + 0, (b.posX >> 16) / 32);
        SB(e + 1, static_cast<std::uint8_t>((b.posY >> 16) / 32) - (t.artHeight >> 5));
        SW(e + 2, b.index);
        SB(e + 0xd, 1);
        SD(0x185c + t.buildingGroup * 4, D(0x185c + t.buildingGroup * 4) + 1);
        SB(0xf0c, B(0xf0c) + 1);
        // (DebugFatal "Too many initial buildings" above 149)
    });
}

void AiController::upgradeBuildings() {
    if ((w.gameTicks_ & 0x3f) != 0 || S.upgradeQueue.type != -1) return;
    // UNCERTAIN: the original walks CSide::__UpgradeIcons[8] (+0x262c8), which the model does not
    // keep; upgradableBuildings() lists the same groups in template order.
    const auto list = w.upgradableBuildings(s);
    for (std::size_t i = 0; i < list.size() && i < 8; ++i)
        if (w.canSideUpgradeGroup(s, list[i].group)) {
            upgradeOrder(list[i].group);
            return;
        }
}

void AiController::doUnitFacings() {
    int x, y;
    findBestBasePosition(x, y);
    if (S.primaryBuilding[0] == kNoIndex) return;
    forUnits([&](Unit& u) {
        const int dy = u.posY - (y & 0xff) * 0x200000;
        const int dx = (x & 0xff) * 0x200000 - u.posX;
        const int a = atan4096(dx / 0x10000, dy / 0x10000);
        const int f = static_cast<std::uint8_t>(a / 128);
        u.facing = f;
        u.facingTurret = f;
        u.facingTurretTarget = f;
    });
}

// -------------------------------------------------------------------------------------------
// Group-making functions of DoAI

void AiController::doAttackBuildings() {
    const std::int32_t c = static_cast<std::int32_t>(D(0xf18));
    SD(0xf18, static_cast<std::uint32_t>(c - 1));
    if (c >= 1) return;
    const double f = randFactorUnsigned(B(0xf6e), 0x5fc);
    SD(0xf18, x87Ftol(f * static_cast<double>(static_cast<std::int32_t>(D(0xf14)))));
    const int base = B(0x1d3c);
    const int ng = firstFreeGroup();
    if (!ng) return;
    createSplinterGroup(base, ng, percentage(base, B(0xf3d)));
    int x, y;
    groupAverage(ng, x, y);
    gSB(ng, gTask, kTaskAttackBuilding);
    const int ts = bestSideToAttack();
    attackBestBuilding(ng, ts, x, y);
    groupAverage(ng, x, y);
    attackBestBuilding(ng, gB(ng, gTargetSide), x, y);
}

void AiController::doProtection() {
    const std::uint32_t every = D(0x1d34);
    if (every == 0) return;
    for (int i = S.firstUnit; i != -1; i = S.nextObject(i)) {
        Unit& u = S.units[i];
        if (static_cast<std::uint32_t>(u.index) % every != w.gameTicks_ % every || B(0x1cc8 + u.type) == 0) continue;
        if (groupsDoing(kTaskProtect) >= B(0xf43)) return;
        if (!(u.lastAttacked < D(0x1d34) && !(u.flags & 8) && u.state != UnitState::AwaitingAirlift)) continue;
        bool lifted = false;
        u.flags |= 8;
        if (rnd() % 100 <= B(0x1d1f)) {
            const int pos = w.pickupQueuePos(S, u);
            if (pos != -1) {
                u.targetX = w.width_ / 2;
                u.targetY = w.height_ / 2;
                bestBasePosition(u.targetX, u.targetY);
                if (w.addToPickupQueue(S, u, pos, 1)) lifted = true;
            }
        }
        if (lifted && B(0x1d44) == 0) continue;
        const int base = B(0x1d3c);
        const std::uint32_t want = percentage(base, B(0xf3e));
        const int pg = firstFreeGroup();
        if (!pg) continue;
        createSplinterGroup(base, pg, want);
        gSB(pg, gTask, kTaskHunt);
        gSD(pg, gTime, 10000);
        gSB(pg, gSub, 4);
        gSW(pg, gTargetIndex, 0xffff);
        gSB(pg, gX, posTile(u.posX));
        gSB(pg, gY, posTile(u.posY));
    }
}

void AiController::doDefence() {
    const std::uint32_t every = D(0x1d38);
    if (every == 0) return;
    forBuildings([&](Building& b) {
        if (static_cast<std::uint32_t>(b.index) % every != w.gameTicks_ % every || b.lastAttacked >= every) return;
        SB(8, 1);
        if (B(0x1d) == 0 || b.attackerIndex == kNoIndex) return;
        if (kindOf(b.attackerSide, b.attackerIndex) != 1) return;
        const Unit& at = w.sides_[b.attackerSide & 7].units[b.attackerIndex];
        if (at.flags & unit_flags::kFlying) return;
        sendNearestUnit(B(0x1d07), at.blockFromX, at.blockFromY);
    });
}

int AiController::defendGroupId(int area) {
    int r = 0xff;
    for (int g = 0; g < 32; ++g)
        if (gB(g, gUsed) && gB(g, gTask) == kTaskDefendArea && gB(g, gArea) == (area & 0xff)) r = g;
    return r;
}

void AiController::doDefendChecks() {
    if (B(0x1e) != 0 || B(0x1d51) == 0) return;
    for (int i = 0; i < B(0x1d51); ++i) {
        const std::ptrdiff_t ar = static_cast<std::ptrdiff_t>(kAreas) + i * static_cast<std::ptrdiff_t>(kAreaSize);
        const std::uint32_t every = D(ar + 0xc);
        if (every == 0 || w.gameTicks_ % every != 0) continue;
        const int cx = (B(ar + 1) - B(ar)) / 2 + B(ar), cy = (B(ar + 3) - B(ar + 2)) / 2 + B(ar + 2);
        const int dg = static_cast<std::int8_t>(defendGroupId(i));
        const std::int32_t want = static_cast<std::int32_t>(D(ar + 8));
        if (dg == -1) {
            const int base = B(0x1d3c);
            const int ng = firstFreeGroup();
            if (!ng) continue;
            createSplinterGroup(base, ng, static_cast<std::uint32_t>(want * 0x14 / 100));
            if (gD(ng, gStrength) == 0) { gSB(ng, gUsed, 0); continue; }
            clearUnits();
            selectGroup(ng, false);
            sendGroupToPoint(cx & 0xff, cy & 0xff, ng);
            gSB(ng, gTask, kTaskDefendArea);
            gSB(ng, gArea, i);
            gSW(ng, gTargetIndex, 0xffff);
        } else {
            const std::int32_t have = static_cast<std::int32_t>(gD(dg, gStrength));
            if (have >= want) continue;
            std::int32_t need = want - have;
            const std::int32_t cap = want * 0x14 / 100;
            if (cap <= need) need = cap;
            const int base = B(0x1d3c);
            const int ng = firstFreeGroup();
            if (!ng) continue;
            createSplinterGroup(base, ng, static_cast<std::uint32_t>(need));
            if (gD(ng, gStrength) != 0) {
                selectGroup(ng, false);
                sendGroupToPoint(cx & 0xff, cy & 0xff, ng);
                mergeGroups(ng, dg);
            }
            gSB(ng, gUsed, 0);
        }
    }
}

void AiController::doAttackApproachers() {
    if (groupsDoing(kTaskApproacher) >= B(0xf45)) return;
    const int ts = static_cast<int>(w.gameTicks_ & 7);
    Unit* t = nullptr;
    if (!unitInBase(ts, t)) return;
    const int base = B(0x1d3c);
    const std::uint32_t want = percentage(base, B(0xf40));
    const int ng = firstFreeGroup();
    if (!ng) return;
    createSplinterGroup(base, ng, want);
    gSB(ng, gTask, kTaskApproacher);
    gSB(ng, gSub, 0);
    gSW(ng, gTargetIndex, t->index);
    gSB(ng, gTargetSide, ts);
    selectGroup(ng, true);
    attackUnitOrder(ts, t->index);
}

bool AiController::itemReady(int beh, int& slot) {
    bool listed = false;
    for (int t : S.unitIcons)
        if (t != -1 && T.units[t].specialBehavior == beh) listed = true;
    if (!listed) return false;
    int found = -1;
    for (int q = 0; q < 10; ++q)
        if (S.unitQueue[q].type != -1 && T.units[S.unitQueue[q].type].specialBehavior == beh) found = q;
    if (found == -1) return false;
    slot = found;
    return S.unitQueue[found].progress > 0x59ff;
}

const void* AiController::bestTarget(int tside, int weapon, int& kind, int& px, int& py) {
    if (static_cast<std::int8_t>(tside) == -1) return nullptr;
    int bldChance = 5, unitChance = 5;
    if (weapon == 9) { bldChance = 10; unitChance = 5; }
    else if (weapon == 10) { bldChance = 15; unitChance = 1; }
    Side& ts = w.sides_[tside & 7];
    for (int i = ts.firstBuilding; i != -1; i = ts.nextObject(i)) {
        const Building& b = ts.buildings[i];
        if (T.buildings[b.type].specialBehavior != building_behaviour::kWall && static_cast<int>(rnd() & 0x7f) < bldChance) {
            kind = 2;
            px = b.posX >> 16;
            py = b.posY >> 16;
            return &b;
        }
    }
    for (int i = ts.firstUnit; i != -1; i = ts.nextObject(i)) {
        const Unit& u = ts.units[i];
        if (unitChance <= static_cast<int>(rnd() & 0x7f) || (u.flags & unit_flags::kExcluded) ||
            T.units[u.type].specialBehavior == behaviour::kSandworm)
            continue;
        kind = 1;
        px = u.posX >> 16;
        py = u.posY >> 16;
        return &u;
    }
    return nullptr;
}

void AiController::doSpecialWeapons() {
    if (B(0x1e) != 0 || ((w.gameTicks_ + static_cast<std::uint32_t>(s)) & 0x1f) != 0 || w.gameTicks_ < D(0x1d18)) return;
    const int target = bestSideToAttack();
    int slot = 0;
    switch (((static_cast<std::uint32_t>(s) + w.gameTicks_) >> 5) & 7) {
    case 0: {  // engineers of the base group capture the best building
        Unit* eng = nullptr;
        for (int i = S.firstUnit; i != -1 && !eng; i = S.nextObject(i))
            if (ut(S.units[i]).specialBehavior == behaviour::kEngineer && S.units[i].groupId == B(0x1d3c)) eng = &S.units[i];
        if (!eng) return;
        const int g = firstFreeGroup();
        if (!g) return;
        eng->groupId = static_cast<std::uint8_t>(g);
        int n = static_cast<int>(rnd() & 7);
        for (int i = S.firstUnit; i != -1 && n != 0; i = S.nextObject(i)) {
            Unit& u = S.units[i];
            const auto& t = ut(u);
            if (t.isInfantry && u.groupId == B(0x1d3c) && t.primaryWeapon != -1) {
                --n;
                u.groupId = static_cast<std::uint8_t>(g);
            }
        }
        const std::uint32_t st = groupStrength(g, kTaskEngineers);
        gSD(g, gStrength, st);
        gSD(g, gStrength0, st);
        gSB(g, gUsed, 1);
        int x, y;
        groupAverage(g, x, y);
        if (static_cast<std::int8_t>(target) == -1) return;
        const Building* b = bestBuilding(target, x, y, 2.0f);
        if (!b) { mergeGroups(g, B(0x1d3c)); return; }
        gSW(g, gTargetIndex, b->index);
        gSB(g, gTargetSide, target);
        gSB(g, gX, (b->posX >> 16) / 32);
        gSB(g, gY, (b->posY >> 16) / 32);
        gSB(g, gSub, 2);
        gSD(g, gTries, 0);
        gSB(g, gTask, kTaskEngineers);
        gSD(g, gStrength, groupStrength(g, kTaskEngineers));
        clearUnits();
        selectGroup(g, true);
        commence(g);
        return;
    }
    case 1: case 2: {  // saboteur / fremen palace weapons
        const int beh = (((static_cast<std::uint32_t>(s) + w.gameTicks_) >> 5) & 7) == 1 ? 4 : 12;
        if (!itemReady(beh, slot)) return;
        const int type = static_cast<std::uint8_t>(T.groupIds[beh == 4 ? group_ids::kSaboteur : group_ids::kFremen]);
        int a1, a2, a3, a4;
        if (!w.findUnitExitBuilding(type, s, a1, a2, a3, a4, true) || S.unitQueue[slot].placing) return;
        w.orders_.push_back({World::Order::SpecialWeapon, s, kNoIndex, 0, 0, s, beh});  // GenerateSpecialWeaponOrder
        S.unitQueue[slot].placing = true;
        return;
    }
    case 3: case 4: {  // ornithopter strike / death hand
        const int beh = (((static_cast<std::uint32_t>(s) + w.gameTicks_) >> 5) & 7) == 3 ? 9 : 10;
        if (!itemReady(beh, slot) || S.unitQueue[slot].placing) return;
        int kind = 0, px = 0, py = 0;
        if (!bestTarget(bestSideToAttack(), beh, kind, px, py)) return;
        if (kind != 1 && kind != 2) {
            if (beh == 10) return;  // (death hand: no order without a unit/building target)
            px = py = 0;
        }
        w.orders_.push_back({World::Order::SpecialWeapon, s, kNoIndex, (px / 32) & 0xff, (py / 32) & 0xff, s, beh});
        S.unitQueue[slot].placing = true;
        return;
    }
    default: return;
    }
}

void AiController::autoBerserkCheck() {  // FUN_00423850
    if ((w.gameTicks_ & 7) != static_cast<std::uint32_t>(s) || B(0x1e) != 0 || B(3) == 0) return;
    const bool noRefinery = groupCount(gid(group_ids::kRefinery)) == 0;
    const std::uint8_t harv = static_cast<std::uint8_t>(S.harvesterCount);
    const bool noConyard = primary(0) == -1 && S.unitsExistPerType[gid(group_ids::kMcv)] == 0;
    bool noFactory = true;
    for (int k : {7, 1, 2, 5, 9, 10, 8})
        if (primary(k) != -1) noFactory = false;
    const bool broke = credits() < 400;
    if (noRefinery && noConyard && broke) SB(0x1e, 1);
    if (noFactory && noConyard) SB(0x1e, 1);
    if (broke && (harv == 0 || noRefinery)) SB(0x1e, 1);
}

// -------------------------------------------------------------------------------------------
// ActivateAI / DoAI

void AiController::activate() {
    // (GameType != 1: DebugFatal when NetPlayersTechLevel / StartingCredits are 0)
    const std::uint32_t r0 = rnd();
    SD(0xf18, D(0xf10));
    const float f68 = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(r0 % 30 + 60)) * kF001);
    std::memcpy(p(0xf68), &f68, 4);
    // FUN_004a6070(6, side): group of the first repair-pad template owned by bit (1 << side).
    int repairGroup = 0xff;
    for (int t = 0; t < 100; ++t)
        if (T.buildings[t].specialBehavior == building_behaviour::kRepairPad && (T.buildings[t].ownerHouse & (1u << (s & 0x1f)))) {
            repairGroup = T.buildings[t].buildingGroup;
            break;
        }
    SB(0x1d46, repairGroup);
    {
        const int rp = B(0xf6e);
        const std::uint32_t r = rnd();
        const std::int32_t v = rp - static_cast<std::int32_t>(rp * 2 ? r % static_cast<std::uint32_t>(rp * 2) : 0) + 100;
        SD(0xf18, x87Ftol(static_cast<double>(v) * kF001 * static_cast<double>(static_cast<std::int32_t>(D(0xf10)))));
    }
    {
        const int rp = B(0xf6e);
        const std::uint32_t r = rnd();
        const std::int32_t v = rp - static_cast<std::int32_t>(rp * 2 ? r % static_cast<std::uint32_t>(rp * 2) : 0) + 100;
        SD(0xf24, x87Ftol(static_cast<double>(D(0xf1c)) * (static_cast<double>(v) * kF001)));
    }
    if (w.difficulty == 2) SD(0x1d2c, x87Ftol(static_cast<double>(D(0x1d2c)) * 1.5));
    getInitialBuildingPositions();
    for (int g = 0; g < 32; ++g) {
        const std::ptrdiff_t o = go(g);
        SB(o + gUsed, 0);
        SB(o + gTask, 0);
        SD(o + gStrength, 0);
        SD(o + gTries, 0);
        SB(o + gSub, 0);
        SD(o + gFlags, 0);
        SD(o + gCounter, 0);
        SW(o + gTargetIndex, 0xffff);
    }
    gSB(0, gUsed, 1);
    const int baseG = firstFreeGroup();
    SB(0x1d3d, baseG);
    gSB(baseG, gTask, kTaskBase);
    if (!w.skirmish) groupNewUnits(baseG, true);
    doUnitFacings();
    const std::uint32_t st = groupStrength(baseG, 1);
    gSD(baseG, gStrength, st);
    gSD(baseG, gStrength0, st);
    gSB(baseG, gUsed, 1);
    const int newG = firstFreeGroup();
    gSB(newG, gTask, kTaskNewUnits);
    SB(0x1d3c, newG);
    gSD(newG, gStrength, 0);
    gSD(newG, gStrength0, 0);
    gSB(newG, gUsed, 1);
    if (B(3) != 0) SB(0x19ec, 0);
    int bx = 0, by = 0;
    findBestBasePosition(bx, by);
    SB(0xf57, bx);
    SB(0xf58, by);
    SB(0xf56, 0);
    if (D(0xf5c) == 0) SB(0x1d, 0);
    if (B(0x1d) == 0) {
        SB(0x1d52, 0xff);
    } else {
        const std::ptrdiff_t ar = static_cast<std::ptrdiff_t>(kAreas) + B(0x1d51) * static_cast<std::ptrdiff_t>(kAreaSize);
        SB(ar + 2, S.baseMinY);
        SB(ar + 3, S.baseMaxY);
        SB(ar + 0, S.baseMinX);
        SB(ar + 1, S.baseMaxX);
        SD(ar + 8, 1000);
        SD(ar + 0xc, 100);
        SD(ar + 0x10, 0xffffffff);
        SB(0x1d52, B(0x1d51));
        SB(0x1d51, B(0x1d51) + 1);
    }
    const std::int32_t free = static_cast<std::int32_t>(gD(B(0x1d3d), gStrength) * B(0xf0d)) / 100;
    if (free != 0) createSplinterGroup(B(0x1d3d), 0xff, static_cast<std::uint32_t>(free));
    clearUnits();
}

// CAI__DoRepairUnits 0x41e650: every D(0x1d10) calls, unless a repair group exists, idle
// (or guarding, B(0x1d15)) damaged vehicles of groups g1/g2 below HP * F(0x1ae8 + type * 4)
// that were neither attacked nor fired for 12 checks join a new repair group (at most B(0x1d08),
// if CanSpendCash(2, cost / HP >> 1)), which is sent to the nearest repair pad.
void AiController::doRepairUnits(int g1, int g2) {
    SD(0x10, D(0x10) + 1);
    if (D(0x10) < D(0x1d10)) return;
    SD(0x10, 0);
    for (int g = 0; g < 32; ++g)
        if (gB(g, gUsed) != 0 && gB(g, gTask) == kTaskRepair) return;
    int count = 0, grp = 0;
    forUnits([&](Unit& u) {
        if ((u.groupId != static_cast<std::uint8_t>(g1) && u.groupId != static_cast<std::uint8_t>(g2)) ||
            u.lastAttacked <= 0xc || u.lastFired <= 0xc)
            return;
        const auto& t = ut(u);
        const std::int32_t thr = static_cast<std::int32_t>(
            x87Ftol(static_cast<double>(t.hitPoints) * static_cast<double>(F(0x1ae8 + u.type * 4))));
        if (!(u.health < thr) ||
            !(u.state == UnitState::Idle || (B(0x1d15) != 0 && u.state == UnitState::Guarding)) ||
            count >= B(0x1d08) || t.isInfantry)
            return;
        grp = firstFreeGroup();
        if (grp == 0) return;
        const std::uint32_t cost = w.unitCost(u.type, s) / (t.hitPoints ? t.hitPoints : 1) >> 1;
        if (canSpendCash(2, cost)) {
            ++count;
            u.groupId = static_cast<std::uint8_t>(grp);
        }
    });
    if (count == 0 || grp == 0) return;
    gSB(grp, gUsed, 1);
    gSB(grp, gTask, kTaskRepair);
    gSD(grp, gStrength, groupStrength(grp, kTaskRepair));
    gSW(grp, gTargetIndex, 0xffff);
    clearUnits();
    selectGroup(grp, true);
    repairOrder(0xffff);
}

void AiController::update() {
    if (!a.enabled()) return;
    getSelected();
    doMcvs();
    groupNewUnits(B(0x1d3c), false);
    doRepairs();
    monitorHarvesters();
    doBuildUnits();
    // DoBuildBuildings 0x41c060 is empty in the original.
    doOverrides();
    if (B(0xf56) != 0) doAttackApproachers();
    if (B(0x1d14) != 0 && B(0x1d46) != 0xff && groupCount(B(0x1d46)) != 0) doRepairUnits(B(0x1d3c), B(0x1d3d));
    doAttackBuildings();
    doProtection();
    doSpecialWeapons();
    doDefence();
    monitorGroups(0);
    monitorGroups(0x10);
    monitorBuildings();
    doDefendChecks();
    doBuildCarryalls();
    if (B(0x1d45) != 0) rebuildBuildings();
    if (B(5) != 0) upgradeBuildings();
    if (B(4) != 0) buyUnitsFromStarport();
    autoBerserkCheck();
    if (B(0x1e) == 2 || B(0x1e) == 1) {
        forBuildings([&](Building& b) {
            if ((b.flags & 0x4000006) == 0 && (rnd() & 0x1f) == 0 &&
                T.buildings[b.type].specialBehavior != building_behaviour::kWall) {
                sellOrder(b.index);  // GenerateBuildingSellOrder
                b.flags |= 0x4000000;
            }
        });
        const int g = static_cast<int>(w.gameTicks_ & 0x1f);
        if (gB(g, gTask) != kTaskHunt && gB(g, gUsed) == 1) {
            gSB(g, gTask, kTaskHunt);
            gSD(g, gTime, 10000);
            gSB(g, gSub, 4);
            gSW(g, gTargetIndex, 0xffff);
            gSB(g, gTargetSide, 0);  // (the original writes group[g+1] byte 0 = this group's target side)
            int x, y;
            groupAverage(g, x, y);
            gSB(g, gX, x);
            gSB(g, gY, y);
        }
    }
    clearUnits();
    resetSelected();
}

// -------------------------------------------------------------------------------------------
// World entry points

// InitAI FUN_0041af50 for every side, then Mission__LoadObjectives copies the .MIS AI segments
// over the CAI objects (campaign); without a mission the InitAI defaults stay.
void World::aiInit(const formats::MisFile* mission) {
    for (int s = 0; s < kMaxSides; ++s) {
        aiDefaults(s);
        if (mission && s < static_cast<int>(mission->aiSegments.size())) ai_[s].raw = mission->aiSegments[s];
    }
}

void World::aiDefaults(int s) {
    {
        AiState& a = ai_[s & 7];
        a.raw.fill(0);
        auto b = [&](std::size_t o, int v) { a.raw[o] = static_cast<std::uint8_t>(v); };
        auto d = [&](std::size_t o, std::uint32_t v) { a.set<std::uint32_t>(o, v); };
        auto f = [&](std::size_t o, float v) { a.set<float>(o, v); };
        b(0, s);
        b(1, 0);
        for (int i = 0; i < 19; ++i) {  // SpeedWeight, HPWeight, FirePowerWeight = 1.0
            f(0xf70 + i * 4, 1.0f);
            f(0xfbc + i * 4, 1.0f);
            f(0x1008 + i * 4, 1.0f);
        }
        b(0x1d22, 0xff);
        b(0x1d23, 0xff);
        for (int i = 0; i < 100; ++i) {
            f(0x108c + i * 4, 1.0f);   // DefendBuildingPriority
            f(0x121c + i * 4, 1.0f);   // AttackBuildingPriority
            f(0x13ac + i * 4, 0.66f);  // BuildingRepairValue
            d(0x153c + i * 4, 850);    // TimeBeforeRebuilding
            f(0x16cc + i * 4, 1.0f);   // BuildRatio
            d(0x185c + i * 4, 1);      // MaxBuildings
        }
        for (int i = 0; i < 60; ++i) {
            f(0x19f8 + i * 4, 1.0f);   // UnitBuildPriority
            f(0x1ae8 + i * 4, 0.5f);   // UnitRepairValue
            f(0x1bd8 + i * 4, 0.0f);
            b(0x1cc8 + i, 0);          // ProtectUnit
        }
        for (int i = 0; i < 9; ++i) d(0x1054 + i * 4, 0x708);  // CashStash = 1800
        d(0x1d2c, 1000);
        d(0x1d28, 0);
        b(0x1d1f, 0x19);
        d(0xf10, 6000);
        d(0xf14, 5000);
        d(0xf1c, 6000);
        d(0xf20, 5000);
        d(0x1d0c, 8);
        d(0x1d10, 32);
        d(0x10, 0);
        d(0x1d34, 4);
        d(0x1d38, 25);
        d(0xf50, 64);
        f(0xf60, 1.0f);
        f(0xf64, 1.0f);
        b(0x1d45, 0);
        b(0x1d44, 0);
        b(0x1d14, 0);
        b(0x1d15, 0);
        b(0x1d08, 5);
        for (int i = 0; i < 19; ++i) b(0x1078 + i, 100);  // Morale
        for (int i = 0xf3c; i <= 0xf40; ++i) b(i, 0xf);
        for (int i = 0xf41; i <= 0xf45; ++i) b(i, 2);
        d(0x1d18, 0);
        b(0x1d1c, 0);
        b(0x1d1e, 0x19);
        b(0xf56, 0);
        b(0xf54, 0);
        b(0xf55, 0);
        b(0x1d04, 2);
        b(0x1d05, 1);
        b(0xf6c, 0x32);
        b(0xf6d, 0x32);
        b(0xf6e, 0x19);
        b(0xf46, 10);
        b(0xf47, 10);
        b(0xf6f, 0x14);
        b(0x1d51, 0);
        b(0x1d49, 0);
        b(2, 0xff);
        d(0x18, 1);
        b(0x1e, 0);
        b(3, 0);
        b(4, 0);
        b(5, 1);
        b(0x1f, 0);
        b(0x1d, 1);
        d(0x28, 0xffffffff);
        d(0x24, 0xffffffff);
        b(0x1c, 0);
        d(0x14, 9);
        d(0xf5c, 1000);
        d(0xc, 0);
        b(6, 0);
        b(10, 0x14);
        b(0xb, 0);
        b(7, 0);
        b(0xf0d, 0);
        b(0x19ec, 1);
        b(0x1d07, 0xff);
        b(0x1d3d, 0xff);
        d(0xf28, 1);
        b(0x1d1d, 0);
    }
}

void World::aiActivate(int side) { AiController(*this, side).activate(); }

// UpdateUnit case 0x13 after the last unit of a delivery is dropped (AI sides): the delivery's
// group is flagged for re-ordering (flag 1) with the delivery's deploy action as its mode.
void World::aiDeliveryDone(int side, int delivery) {
    AiController c(*this, side);
    const int g = c.findGroupForDelivery(delivery & 0xff);
    c.gSD(g, gFlags, c.gD(g, gFlags) | 1);
    const int g2 = c.findGroupForDelivery(delivery & 0xff);
    c.gSB(g2, gMode, sides_[side & 7].deliveries[(delivery & 0xff) % 10].deployAction);
}

void World::aiUpdate(int side) { AiController(*this, side).update(); }

}  // namespace d2k::game

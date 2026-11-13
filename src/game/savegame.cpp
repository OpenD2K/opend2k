// Save games (see savegame.hpp for the file layout and the design).
//
// Every struct with mutable state has ONE io(ar, obj) visitor below that names each member in
// declaration order. The same visitor drives three archives:
//   OutAr   - writes the members (little-endian, fixed widths: int = 4 bytes, size_t = 8),
//   InAr    - reads them back,
//   CountAr - counts the visited members at compile time.
// Guards against forgetting a member when a struct grows:
//   - aggregates: static_assert(aggregateFields<T>() == visitedFields<T>()) - the real member
//     count (probed with brace initialisation) must equal the members io() visits;
//   - every saved type: static_assert(sizeof(T) == ...) on libc++ / 64-bit (the dev setup),
//     which also covers World and PathFinder (not aggregates). When one fires: add the new
//     member to io(), bump kSaveVersion if the format changed, then update the size.

#include "game/savegame.hpp"

#include "game/world.hpp"

#include <bit>
#include <cstring>
#include <type_traits>

namespace d2k::game {

void SaveWriter::f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
void SaveWriter::bytes(const void* p, std::size_t n) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    buf_.insert(buf_.end(), b, b + n);
}
void SaveWriter::str(const std::string& s) {
    u32(static_cast<std::uint32_t>(s.size()));
    bytes(s.data(), s.size());
}

float SaveReader::f32() { return std::bit_cast<float>(u32()); }
void SaveReader::bytes(void* p, std::size_t n) {
    need(n);
    if (n) std::memcpy(p, d_.data() + pos_, n);
    pos_ += n;
}
std::string SaveReader::str() {
    const std::uint32_t n = u32();
    need(n);
    std::string s(reinterpret_cast<const char*>(d_.data() + pos_), n);
    pos_ += n;
    return s;
}
std::span<const std::uint8_t> SaveReader::take(std::size_t n) {
    need(n);
    auto s = d_.subspan(pos_, n);
    pos_ += n;
    return s;
}

std::uint64_t fnv1a64(std::span<const std::uint8_t> data) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::uint8_t b : data) {
        h ^= b;
        h *= 0x100000001b3ull;
    }
    return h;
}

namespace {

template <class T> struct IsStdArray : std::false_type {};
template <class T, std::size_t N> struct IsStdArray<std::array<T, N>> : std::true_type {};
template <class T> struct IsStdVector : std::false_type {};
template <class T, class A> struct IsStdVector<std::vector<T, A>> : std::true_type {};

// Upper bound for element counts read from a file (cells of a 128x128 map are 16384).
constexpr std::uint32_t kMaxElements = 1u << 20;

struct CountAr {
    std::size_t n = 0;
    template <class... T> constexpr void operator()(T&...) { n += sizeof...(T); }
    template <class T, class F> constexpr void slots(std::vector<T>&, F) { ++n; }
};

struct OutAr {
    SaveWriter& w;
    template <class... T> void operator()(T&... v) { (one(v), ...); }
    template <class T> void one(T& v);
    // Pool slot arrays: most slots hold their freed/default contents; one byte marks those.
    template <class T, class F> void slots(std::vector<T>& v, F makeDefault);
};

struct InAr {
    SaveReader& r;
    template <class... T> void operator()(T&... v) { (one(v), ...); }
    template <class T> void one(T& v);
    template <class T, class F> void slots(std::vector<T>& v, F makeDefault);
};

}  // namespace

// Friend of World and PathFinder: the per-struct field lists.
struct SaveAccess {
    using MisEvent = World::MisEvent;
    using MisCondition = World::MisCondition;
    using Order = World::Order;

    template <class Ar> static constexpr void io(Ar& ar, Unit& u) {
        ar(u.alive, u.index, u.side, u.type, u.speed, u.health, u.flags, u.enemyIndex, u.state, u.oldState, u.selected,
           u.turnDelay, u.turretTurnDelay, u.reloadDelay, u.flicker, u.animFrame, u.animType, u.lying, u.animDelay,
           u.enemySide, u.smokeTimer, u.groupId, u.byte29, u.lastAttacked, u.lastFired, u.lastMoved, u.deadStateTimer,
           u.attackerSide, u.stealthTimer, u.attackerIndex, u.posX, u.posY, u.posZ, u.stepTargetX, u.stepTargetY,
           u.targetX, u.targetY, u.blockFromX, u.blockFromY, u.blockToX, u.blockToY, u.posStepsMax, u.posSteps,
           u.rememberX, u.rememberY, u.cellspotTo, u.cellspotFrom, u.facing, u.facingTurret, u.facingTurretTarget,
           u.refineryIndex, u.repairPadIndex, u.routeX, u.routeY, u.routeSteps, u.routeMode, u.routeTotalSteps,
           u.routeRetryDelay, u.routeBlocked, u.byte2E, u.oldSide, u.tag, u.prevSelected);
    }
    template <class Ar> static constexpr void io(Ar& ar, Building& b) {
        ar(b.alive, b.index, b.side, b.type, b.posX, b.posY, b.attackTileX, b.attackTileY, b.tileX, b.tileY, b.maxX,
           b.maxY, b.wallFrame, b.facing, b.health, b.flags, b.enemyIndex, b.state, b.selected, b.turretTurnDelay,
           b.reloadDelay, b.flicker, b.deadTimer, b.enemySide, b.smokeTimer, b.buildupCounter, b.animCounter,
           b.lastAttacked, b.buildupTick, b.timeAdded, b.smokeBits, b.attackerSide, b.attackerIndex, b.tag,
           b.prevSelected, b.specialPurpose);
    }
    template <class Ar> static constexpr void io(Ar& ar, Bullet& b) {
        ar(b.alive, b.index, b.side, b.moveSteps, b.flags, b.speedX, b.speedY, b.speed, b.firerIndex, b.homingIndex,
           b.type, b.homingSide, b.byte1B, b.targetX, b.targetY, b.delay, b.posX, b.posY, b.posZ, b.fallSpeed,
           b.animFrame, b.dir, b.prevDir, b.tag);
    }
    template <class Ar> static constexpr void io(Ar& ar, Explosion& e) {
        ar(e.alive, e.index, e.side, e.type, e.frame, e.delay, e.flags, e.unitIndex, e.posX, e.posY, e.posZ, e.driftX,
           e.driftY, e.tag);
    }
    template <class Ar> static constexpr void io(Ar& ar, ProductionSlot& q) {
        ar(q.type, q.progress, q.prevProgress, q.costAccum, q.paid, q.onHold, q.placing);
    }
    template <class Ar> static constexpr void io(Ar& ar, AutoBuildEntry& e) { ar(e.type, e.remaining); }
    template <class Ar> static constexpr void io(Ar& ar, PickupEntry& p) {
        ar(p.x, p.y, p.destX, p.destY, p.priority, p.state, p.unitIndex, p.carryallIndex, p.waitTicks, p.origState);
    }
    template <class Ar> static constexpr void io(Ar& ar, Delivery& d) {
        ar(d.x, d.y, d.started, d.active, d.time, d.units, d.type, d.deployAction, d.tag, d.entryEdge);  // entryEdge: v5
    }
    template <class Ar> static constexpr void io(Ar& ar, ObjectLink& l) { ar(l.next, l.prev, l.kind); }
    template <class Ar> static constexpr void io(Ar& ar, Crate& c) {
        ar(c.x, c.y, c.type, c.active, c.image, c.respawns, c.timer);
    }
    template <class Ar> static constexpr void io(Ar& ar, Particle& p) {
        ar(p.x, p.y, p.vx, p.vy, p.life, p.tile, p.tilePx, p.tilePy, p.active);
    }
    template <class Ar> static constexpr void io(Ar& ar, Cell& c) {
        ar(c.tile, c.flags, c.shroud, c.damage, c.buildingSide, c.buildingIndex);
    }
    template <class Ar> static constexpr void io(Ar& ar, MissionMessage& m) { ar(m.textId, m.tick); }
    template <class Ar> static constexpr void io(Ar& ar, AiState& a) { ar(a.raw); }
    template <class Ar> static constexpr void io(Ar& ar, ExtVariable& v) { ar(v.value, v.oldValue, v.ticks); }
    template <class Ar> static constexpr void io(Ar& ar, ExtMission& x) {
        ar(x.active, x.events, x.conditions, x.next, x.elseIndex, x.vars, x.tickRandom, x.backupTile, x.tileAtr,
           x.exitCount, x.breakCount, x.continueCount);
    }
    template <class Ar> static constexpr void io(Ar& ar, MisEvent& e) {
        ar(e.x, e.y, e.value, e.numConditions, e.type, e.amount, e.side, e.arg2, e.arg3, e.arg4, e.conditionIndex,
           e.conditionNot, e.data);
    }
    template <class Ar> static constexpr void io(Ar& ar, MisCondition& c) {
        ar(c.val2, c.val1, c.value, c.coordX, c.coordY, c.floatVal, c.side, c.type, c.arg1, c.arg2);
    }
    template <class Ar> static constexpr void io(Ar& ar, Order& o) {
        ar(o.kind, o.side, o.unitIndex, o.x, o.y, o.targetSide, o.targetIndex, o.payload);  // payload: v5
    }
    template <class Ar> static constexpr void io(Ar& ar, PathPoint& p) { ar(p.x, p.y); }
    template <class Ar> static constexpr void io(Ar& ar, CheatState& c) {
        ar(c.buildingOverride, c.unitOverride, c.deliveryBacklog, c.instantBuild, c.used, c.reinforcements,
           c.nextReinforcementId);  // reinforcements: v5
    }
    template <class Ar> static constexpr void io(Ar& ar, ScheduledReinforcement& r) {
        ar(r.id, r.side, r.due, r.ordered, r.x, r.y, r.edge, r.units);
    }
    template <class Ar> static constexpr void io(Ar& ar, RallyPoint& p) { ar(p.side, p.building, p.timeAdded, p.x, p.y); }
    template <class Ar> static constexpr void io(Ar& ar, RallyPending& p) { ar(p.side, p.unit, p.since, p.x, p.y); }
    template <class Ar> static constexpr void io(Ar& ar, Random& r) {
        std::uint32_t seed = r.seed();
        ar(seed);
        r.reseed(seed);
    }
    template <class Ar> static void io(Ar& ar, PathFinder& p) {
        // Scratch buffers of FindPath, kept so a loaded game is bit-identical even where the
        // original reads entries left over from an earlier search.
        ar(p.a_.p, p.a_.cnt, p.a_.st, p.b_.p, p.b_.cnt, p.b_.st, p.exits_, p.nExits_);
    }

    template <class Ar> static constexpr void io(Ar& ar, Side& s) {
        ar(s.id, s.house, s.cash, s.spice, s.maxStorage, s.cashDrip, s.spiceDrip, s.techLevel, s.startingCredits,
           s.participates);
        ar.slots(s.units, [](int i) { Unit u; u.index = i; return u; });
        ar.slots(s.buildings, [](int i) { Building b; b.index = i; return b; });
        ar.slots(s.bullets, [](int i) { Bullet b; b.index = i; return b; });
        ar.slots(s.explosions, [](int i) { Explosion e; e.index = i; return e; });
        ar(s.links, s.firstUnit, s.lastUnit, s.firstBuilding, s.lastBuilding, s.firstBullet, s.lastBullet,
           s.firstExplosion, s.lastExplosion, s.freeList);
        ar(s.unitsBuilt, s.buildingsBuilt, s.unitsLost, s.buildingsLost, s.unitsKilled, s.buildingsKilled,
           s.harvesterCount, s.carryallCount, s.unitsBuiltPerType, s.unitsExistPerType, s.unitsLostPerType,
           s.buildingsBuiltPerType, s.buildingsExistPerGroup, s.primaryBuilding);
        ar(s.powerOutput, s.powerDemand, s.powerPercent, s.powerPercentPrev, s.spiceHarvested);
        ar(s.buildable, s.upgrades, s.buildingIcons, s.unitIcons, s.prevBuildingIcons, s.prevUnitIcons, s.baseMinX,
           s.baseMinY, s.baseMaxX, s.baseMaxY);
        ar(s.buildingQueue, s.unitQueue, s.upgradeQueue, s.pickup, s.pickupCount, s.carryallsAlive, s.deliveries,
           s.autoQueue);
        ar(s.starportTypes, s.starportPrice, s.starportStock, s.starportOrdered, s.starportPriceTimer,
           s.starportStockTimer, s.starportSpent, s.starportPurchasePending, s.starportFrigateBusy, s.frigateSpeech,
           s.starportInitialised);
    }

    // World: everything except the static Rules pointers, defaultVars_ (constant) and the
    // per-tick sound / shot outputs (sounds_ / uiSounds_ / shots_: cleared on load, they never feed the model).
    template <class Ar> static void io(Ar& ar, World& w) {
        ar(w.width_, w.height_, w.cells_, w.sides_, w.diplomacy_, w.gameTicks_, w.rng_, w.mySide_, w.orders_,
           w.pathFinder_);
        ar(w.screenShakes_, w.unitResponseType_, w.unitResponseKind_, w.difficulty, w.lowMoneyWarned_, w.radarOnline_,
           w.lowPowerWarned_, w.silosWarned_);
        ar(w.revealCounter_, w.screenShakeOffset_);
        ar(w.events_, w.conditions_, w.messages_, w.timerValue_, w.win_, w.lose_, w.gameOver_, w.endHandled_,
           w.buildingsExist_, w.unitsExist_, w.persideFlags_, w.defeatOrder_, w.defeatCount_, w.skirmish);
        ar(w.ai_, w.spawnLocations_, w.spawnUsed_, w.crates_);
        ar.slots(w.particles_, [](int) { return Particle{}; });
        ar(w.particleCount_, w.numCratesAllowed_, w.playerColors_);
        ar(w.cheats_);
        ar(w.ext_);  // v4
        ar(w.rallyPointsEnabled, w.rallyPoints_, w.rallyPending_);  // v5
        ar(w.scriptSide_, w.playAsHostiles_);                        // v6
    }

    static void save(const World& cw, SaveWriter& out) {
        World& w = const_cast<World&>(cw);  // OutAr only reads
        OutAr ar{out};
        io(ar, w);
    }
    static void load(World& w, SaveReader& in) {
        const int width = w.width_, height = w.height_;
        InAr ar{in};
        io(ar, w);
        if (w.width_ != width || w.height_ != height || w.cells_.size() != std::size_t(width) * height)
            throw SaveError("save game map size does not match the loaded map");
        w.sounds_.clear();
        w.uiSounds_.clear();
        w.shots_.clear();
    }
};

namespace {

template <class T> void OutAr::one(T& v) {
    if constexpr (std::is_same_v<T, bool>) {
        w.u8(v ? 1 : 0);
    } else if constexpr (std::is_enum_v<T>) {
        auto u = static_cast<std::underlying_type_t<T>>(v);
        one(u);
    } else if constexpr (std::is_same_v<T, float>) {
        w.f32(v);
    } else if constexpr (std::is_integral_v<T>) {
        using U = std::make_unsigned_t<T>;
        const auto u = static_cast<std::uint64_t>(static_cast<U>(v));
        for (std::size_t i = 0; i < sizeof(T); ++i) w.u8(static_cast<std::uint8_t>(u >> (8 * i)));
    } else if constexpr (IsStdArray<T>::value) {
        if constexpr (std::is_same_v<typename T::value_type, std::uint8_t>) w.bytes(v.data(), v.size());
        else for (auto& e : v) one(e);
    } else if constexpr (IsStdVector<T>::value) {
        w.u32(static_cast<std::uint32_t>(v.size()));
        for (auto& e : v) one(e);
    } else {
        SaveAccess::io(*this, v);
    }
}

template <class T, class F> void OutAr::slots(std::vector<T>& v, F makeDefault) {
    w.u32(static_cast<std::uint32_t>(v.size()));
    for (std::size_t i = 0; i < v.size(); ++i) {
        SaveWriter a, b;
        OutAr oa{a}, ob{b};
        oa.one(v[i]);
        T d = makeDefault(static_cast<int>(i));
        ob.one(d);
        if (a.data() == b.data()) {
            w.u8(0);
        } else {
            w.u8(1);
            w.bytes(a.data().data(), a.data().size());
        }
    }
}

template <class T> void InAr::one(T& v) {
    if constexpr (std::is_same_v<T, bool>) {
        const std::uint8_t b = r.u8();
        if (b > 1) throw SaveError("save game: bad boolean");
        v = b != 0;
    } else if constexpr (std::is_enum_v<T>) {
        std::underlying_type_t<T> u{};
        one(u);
        v = static_cast<T>(u);
    } else if constexpr (std::is_same_v<T, float>) {
        v = r.f32();
    } else if constexpr (std::is_integral_v<T>) {
        std::uint64_t u = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) u |= std::uint64_t(r.u8()) << (8 * i);
        v = static_cast<T>(static_cast<std::make_unsigned_t<T>>(u));
    } else if constexpr (IsStdArray<T>::value) {
        if constexpr (std::is_same_v<typename T::value_type, std::uint8_t>) r.bytes(v.data(), v.size());
        else for (auto& e : v) one(e);
    } else if constexpr (IsStdVector<T>::value) {
        const std::uint32_t n = r.u32();
        if (n > kMaxElements) throw SaveError("save game: bad element count");
        v.assign(n, typename T::value_type{});
        for (auto& e : v) one(e);
    } else {
        SaveAccess::io(*this, v);
    }
}

template <class T, class F> void InAr::slots(std::vector<T>& v, F makeDefault) {
    const std::uint32_t n = r.u32();
    if (n > kMaxElements) throw SaveError("save game: bad slot count");
    v.resize(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint8_t present = r.u8();
        if (present > 1) throw SaveError("save game: bad slot marker");
        if (present) one(v[i]);
        else v[i] = makeDefault(static_cast<int>(i));
    }
}

// --- Compile-time completeness checks ---

// Converts to any member type: T{AnyField{} x N} compiles iff T has at least N members.
struct AnyField {
    template <class T> constexpr operator T() const noexcept;
};
template <class T, class... A> consteval std::size_t aggregateFields() {
    if constexpr (requires { T{A{}..., AnyField{}}; }) return aggregateFields<T, A..., AnyField>();
    else return sizeof...(A);
}
template <class T> consteval std::size_t visitedFields() {
    T v{};
    CountAr c;
    SaveAccess::io(c, v);
    return c.n;
}

// Checked with clang and GCC (macOS / Linux / clang-cl builds). Skipped for MSVC's cl: its
// handling of this brace-initialisation probe is untested, and the guard is for development only.
#if defined(_MSC_VER) && !defined(__clang__)
#define D2K_SAVE_FIELDS(T) static_assert(true)
#else
#define D2K_SAVE_FIELDS(T)                                      \
    static_assert(aggregateFields<T>() == visitedFields<T>(), \
                  "savegame.cpp: io(" #T ") must list every member of " #T " (then bump kSaveVersion)")
#endif
D2K_SAVE_FIELDS(Unit);
D2K_SAVE_FIELDS(Building);
D2K_SAVE_FIELDS(Bullet);
D2K_SAVE_FIELDS(Explosion);
D2K_SAVE_FIELDS(ProductionSlot);
D2K_SAVE_FIELDS(AutoBuildEntry);
D2K_SAVE_FIELDS(PickupEntry);
D2K_SAVE_FIELDS(Delivery);
D2K_SAVE_FIELDS(ObjectLink);
D2K_SAVE_FIELDS(Crate);
D2K_SAVE_FIELDS(Particle);
D2K_SAVE_FIELDS(Cell);
D2K_SAVE_FIELDS(MissionMessage);
D2K_SAVE_FIELDS(AiState);
D2K_SAVE_FIELDS(SaveAccess::MisEvent);
D2K_SAVE_FIELDS(SaveAccess::MisCondition);
D2K_SAVE_FIELDS(SaveAccess::Order);
D2K_SAVE_FIELDS(PathPoint);
D2K_SAVE_FIELDS(Side);
D2K_SAVE_FIELDS(CheatState);
D2K_SAVE_FIELDS(ScheduledReinforcement);
D2K_SAVE_FIELDS(RallyPoint);
D2K_SAVE_FIELDS(RallyPending);
D2K_SAVE_FIELDS(ExtVariable);
D2K_SAVE_FIELDS(ExtMission);
#undef D2K_SAVE_FIELDS

// Sizes on the development toolchain (libc++, 64-bit). A member that fits into padding keeps
// the size, but then the field count above (aggregates) still catches it; World and PathFinder
// are only covered here.
#if defined(_LIBCPP_VERSION) && INTPTR_MAX == INT64_MAX
#define D2K_SAVE_SIZE(T, n) \
    static_assert(sizeof(T) == (n), "savegame.cpp: " #T " changed - update its io() field list, then this size")
D2K_SAVE_SIZE(Unit, 212);
D2K_SAVE_SIZE(Building, 112);
D2K_SAVE_SIZE(Bullet, 92);
D2K_SAVE_SIZE(Explosion, 56);
D2K_SAVE_SIZE(Side, 17096);
D2K_SAVE_SIZE(PathFinder, 24068);
D2K_SAVE_SIZE(World, 227576);  // (+8: Rules::ext, not saved - set by the loader)
#undef D2K_SAVE_SIZE
#endif

void writeMeta(SaveWriter& w, const SaveMeta& m) {
    w.str(m.name);
    w.i64(m.savedAt);
    w.str(m.map);
    w.str(m.tileset);
    w.u8(m.skirmish ? 1 : 0);
    w.i32(m.playerHouse);
    w.i32(m.aiPlayers);
    w.i32(m.techLevel);
    w.u32(m.startingCredits);
    w.i32(m.unitCount);
    w.i32(m.worms);
    w.i32(m.difficulty);
    w.i32(m.campaignHouse);
    w.i32(m.missionIndex);
    w.i32(m.playerSide);
    w.i32(m.playAsSide);
    w.f32(m.cameraX);
    w.f32(m.cameraY);
    w.f32(m.cameraZoom);
    w.u32(m.gameTicks);
    w.u32(static_cast<std::uint32_t>(m.messages.size()));
    for (const auto& msg : m.messages) {
        w.str(msg.text);
        w.u32(msg.until);
    }
    // Version 7.
    w.u8(static_cast<std::uint8_t>(m.kind));
    w.i32(m.missionNumber);
}

SaveMeta readMeta(SaveReader& r, std::uint32_t version) {
    SaveMeta m;
    m.name = r.str();
    m.savedAt = r.i64();
    m.map = r.str();
    m.tileset = r.str();
    m.skirmish = r.u8() != 0;
    m.playerHouse = r.i32();
    m.aiPlayers = r.i32();
    m.techLevel = r.i32();
    m.startingCredits = r.u32();
    m.unitCount = r.i32();
    m.worms = r.i32();
    m.difficulty = r.i32();
    m.campaignHouse = r.i32();
    m.missionIndex = r.i32();
    m.playerSide = r.i32();
    m.playAsSide = r.i32();
    m.cameraX = r.f32();
    m.cameraY = r.f32();
    m.cameraZoom = r.f32();
    m.gameTicks = r.u32();
    const std::uint32_t n = r.u32();
    if (n > 1000) throw SaveError("save game: bad message count");
    m.messages.resize(n);
    for (auto& msg : m.messages) {
        msg.text = r.str();
        msg.until = r.u32();
    }
    if (version >= 7) {
        const std::uint8_t kind = r.u8();
        if (kind > static_cast<std::uint8_t>(SaveKind::CampaignMap)) throw SaveError("save game: unknown kind");
        m.kind = static_cast<SaveKind>(kind);
        m.missionNumber = r.i32();
    }
    return m;
}

// Magic + version; returns the version, the reader positioned at the metadata length.
// Version 6 differs only in the metadata (no kind / missionNumber), so it is still read.
std::uint32_t readHeader(SaveReader& r) {
    char magic[8];
    r.bytes(magic, sizeof magic);
    if (std::memcmp(magic, kSaveMagic, sizeof magic) != 0) throw SaveError("not a D2K save game");
    const std::uint32_t version = r.u32();
    if (version < kOldestSaveVersion || version > kSaveVersion)
        throw SaveError("unsupported save game version " + std::to_string(version) + " (expected " +
                        std::to_string(kSaveVersion) + ")");
    return version;
}

std::vector<std::uint8_t> writeFile(const SaveMeta& meta, std::span<const std::uint8_t> world) {
    SaveWriter out;
    out.bytes(kSaveMagic, sizeof kSaveMagic);
    out.u32(kSaveVersion);
    SaveWriter m;
    writeMeta(m, meta);
    out.u32(static_cast<std::uint32_t>(m.data().size()));
    out.bytes(m.data().data(), m.data().size());
    out.u32(static_cast<std::uint32_t>(world.size()));
    out.bytes(world.data(), world.size());
    out.u64(fnv1a64(out.data()));
    return std::move(out.data());
}

}  // namespace

std::vector<std::uint8_t> saveWorldState(const World& w) {
    SaveWriter out;
    SaveAccess::save(w, out);
    return std::move(out.data());
}

void loadWorldState(World& w, std::span<const std::uint8_t> data) {
    SaveReader in(data);
    SaveAccess::load(w, in);
    if (!in.atEnd()) throw SaveError("save game: trailing data in the world section");
}

std::uint64_t worldStateHash(const World& w) { return fnv1a64(saveWorldState(w)); }

std::vector<std::uint8_t> writeSaveFile(const SaveMeta& meta, const World& w) {
    SaveMeta m = meta;
    m.kind = SaveKind::Battle;
    return writeFile(m, saveWorldState(w));
}

std::vector<std::uint8_t> writeCampaignSaveFile(const SaveMeta& meta) {
    SaveMeta m = meta;
    m.kind = SaveKind::CampaignMap;
    return writeFile(m, {});
}

SaveMeta readSaveMeta(std::span<const std::uint8_t> file) {
    SaveReader r(file);
    const std::uint32_t version = readHeader(r);
    const std::uint32_t n = r.u32();
    SaveReader mr(r.take(n));
    return readMeta(mr, version);
}

SaveMeta readSaveFile(std::span<const std::uint8_t> file, std::span<const std::uint8_t>& worldSection) {
    if (file.size() < 8) throw SaveError("save file truncated");
    SaveReader r(file);
    const std::uint32_t version = readHeader(r);
    const std::uint32_t n = r.u32();
    SaveReader mr(r.take(n));
    SaveMeta m = readMeta(mr, version);
    const std::uint32_t wn = r.u32();
    worldSection = r.take(wn);
    const std::size_t sumAt = r.pos();
    const std::uint64_t sum = r.u64();
    if (!r.atEnd()) throw SaveError("save file: trailing data");
    if (fnv1a64(file.first(sumAt)) != sum) throw SaveError("save file is corrupt (checksum mismatch)");
    if (m.kind == SaveKind::CampaignMap && !worldSection.empty())
        throw SaveError("save file: campaign-map save with a world section");
    if (m.kind == SaveKind::Battle && worldSection.empty()) throw SaveError("save file: battle save without a world");
    return m;
}

}  // namespace d2k::game

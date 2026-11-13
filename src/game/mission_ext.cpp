// Community extended event system (Dune 2000 patch 1.06p+, "extended maps"): the replacement
// of Mission__CheckEvents for extended .MIS files (143014 bytes: up to 1024 events / 256
// conditions, new event and condition types, object filters, variables, run-once / blocked
// event flags, OR conditions, blocks). Spec: docs/extended-events.md.
//
// Behaviour follows the community patch sources (ref/dune2000-sp-patches,
// src/event-system/event-core.c, event-conditions.c, event-actions.c, event-filters.c,
// event-utils.c, src/extended-maps/setupmapstuff.c, delivery-tag.c) and the D2kEditor
// event/condition definitions (ref/D2kEditor/config/*.ini, MIT, github.com/jkoncick/D2kEditor).
// Those are used as a factual reference only; this is our own implementation on top of the
// World model. Struct offsets used by the "property" events (Set Unit Property etc.) are the
// original Unit/Building/Tile layouts (D2kEditor config/game_structs.ini) mapped onto our fields.
//
// Deterministic: rand() is World::rng_ (the patch calls rand() exactly where noted). Only runs
// for extended missions; vanilla .MIS files keep mission.cpp's original event code.

#include "game/geometry.hpp"
#include "game/tables.hpp"
#include "game/world.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>

namespace d2k::game {

using namespace cell_flags;
using namespace unit_flags;

namespace {

// Event type ids (event-core.h EventTypes).
enum : int {
    ET_REINFORCEMENT = 0, ET_STARPORT_DELIVERY, ET_ALLEGIANCE, ET_LEAVE, ET_BESERK, ET_PLAYSOUND, ET_SETBUILDRATE,
    ET_SETATTACKBUILDINGRATE, ET_SETCASH, ET_SETTECH, ET_WIN, ET_LOSE, ET_SWITCH_MY_SIDE, ET_HIDE_MAP, ET_REVEAL,
    ET_SETTIMER, ET_HIDETIMER, ET_SHOWMESSAGE, ET_UNIT_SPAWN, ET_SET_FLAG, ET_UN_BLOCK_EVENT, ET_PLAY_MUSIC,
    ET_DAMAGE_TILES, ET_ADD_UNIT, ET_ADD_BUILDING, ET_ADD_BULLET, ET_ADD_EXPLOSION, ET_ADD_CRATE, ET_ADD_CONCRETE,
    ET_SPICE_BLOOM, ET_SHAKE_SCREEN, ET_CHANGE_VIEWPORT, ET_CHANGE_MAP_BLOCK, ET_TRANSFORM_TILES,
    ET_ADD_BUILDING_DESTRUCT, ET_ADD_HOMING_BULLET, ET_ACTIVATE_TIMER, ET_REMOVE_MESSAGE, ET_SET_MESSAGE_COLOR,
    ET_SET_TOOLTIP,
    ET_TRANSFER_CREDITS = 40, ET_SET_BUILDING_UPGRADES, ET_SET_STARPORT_STOCK, ET_SET_STARPORT_COST,
    ET_CHANGE_STARPORT_UNIT, ET_SHOW_SIDE_DATA, ET_SET_AI_PROPERTY, ET_SHOW_AI_DATA, ET_SET_MEMORY_DATA,
    ET_SHOW_MEMORY_DATA,
    ET_DESTROY_UNIT = 50, ET_DAMAGE_HEAL_UNIT, ET_CHANGE_UNIT_OWNER, ET_CHANGE_UNIT_TYPE, ET_SET_UNIT_FLAG,
    ET_SET_UNIT_PROPERTY, ET_SELECT_UNIT, ET_AIRLIFT_UNIT, ET_58, ET_SHOW_UNIT_DATA,
    ET_DESTROY_BUILDING = 60, ET_DAMAGE_HEAL_BUILDING, ET_CHANGE_BUILDING_OWNER, ET_CHANGE_BUILDING_TYPE,
    ET_SET_BUILDING_FLAG, ET_SET_BUILDING_PROPERTY, ET_SELECT_BUILDING, ET_SHOW_BUILDING_DATA,
    ET_SET_BULLET_PROPERTY = 68, ET_SET_EXPLOSION_PROPERTY = 69,
    ET_REMOVE_CRATE = 70, ET_PICKUP_CRATE, ET_SET_CRATE_PROPERTY, ET_SHOW_CRATE_DATA,
    ET_CHANGE_TILE = 74, ET_SET_TILE_ATTRIBUTE, ET_SET_TILE_PROPERTY, ET_REVEAL_TILE, ET_HIDE_TILE, ET_SHOW_TILE_DATA,
    ET_ORDER_UNIT_MOVE = 80, ET_ORDER_DOCK_WITH_REFINERY, ET_ORDER_REPAIR_SELECTED_UNITS, ET_ORDER_REPAIR_SINGLE_UNIT,
    ET_ORDER_UNIT_ATTACK_UNIT, ET_ORDER_UNIT_ATTACK_BUILDING, ET_ORDER_UNIT_ATTACK_TILE, ET_ORDER_UNIT_GUARD,
    ET_ORDER_UNIT_SCATTER, ET_ORDER_UNIT_RETREAT, ET_ORDER_UNIT_DEPLOY, ET_ORDER_BUILDING_ATTACK_UNIT,
    ET_ORDER_BUILDING_ATTACK_BUILDING, ET_ORDER_BUILDING_SET_PRIMARY, ET_ORDER_BUILDING_REPAIR,
    ET_ORDER_BUILDING_SELL, ET_ORDER_STOP, ET_ORDER_BUILD_BUILDING_PICK, ET_ORDER_BUILD_BUILDING_CANCEL,
    ET_ORDER_BUILD_PLACE_BUILDING, ET_ORDER_BUILD_UNIT_PICK, ET_ORDER_BUILD_UNIT_CANCEL, ET_ORDER_STARPORT_PICK,
    ET_ORDER_STARPORT_UNPICK, ET_ORDER_STARPORT_PURCHASE, ET_ORDER_STARPORT_CANCEL, ET_ORDER_UPGRADE_PICK,
    ET_ORDER_UPGRADE_CANCEL, ET_ORDER_SPECIAL_WEAPON,
    ET_SET_VARIABLE = 120, ET_GET_VARIABLE, ET_SET_FLOAT_VARIABLE,
    ET_GET_RANDOM_VALUE = 125,
    ET_GET_UNIT_COUNT = 130, ET_GET_BUILDING_COUNT, ET_GET_BULLET_COUNT, ET_GET_EXPLOSION_COUNT, ET_GET_CRATE_COUNT,
    ET_GET_TILE_COUNT, ET_GET_SIDE_COUNT, ET_GET_SPICE_COUNT, ET_GET_DAMAGE_COUNT,
    ET_GET_GAME_TICKS = 160, ET_GET_MY_SIDE_ID, ET_GET_DIFFICULTY,
    ET_CALLABLE_BLOCK_START = 233, ET_HOOK_BLOCK_START, ET_EXECUTE_BLOCK, ET_EXIT_FROM_BLOCK, ET_IF, ET_ELSE_IF,
    ET_ELSE, ET_LOOP_WHILE, ET_LOOP_VALUES_FROM_RANGE, ET_LOOP_COORDS_FROM_AREA, ET_LOOP_VALUES_FROM_LIST,
    ET_LOOP_COORDS_FROM_LIST, ET_LOOP_AREAS_FROM_LIST, ET_LOOP_UNITS, ET_LOOP_BUILDINGS, ET_LOOP_BULLETS,
    ET_LOOP_EXPLOSIONS, ET_LOOP_CRATES, ET_LOOP_TILES, ET_LOOP_SIDES, ET_BREAK_LOOP, ET_CONTINUE_LOOP, ET_END,
};

// Condition type ids (event-core.h ConditionTypes).
enum : int {
    CT_BUILDINGEXISTS = 0, CT_UNITEXISTS, CT_INTERVAL, CT_TIMER, CT_CASUALTIES, CT_BASEDESTROYED, CT_UNITSDESTROYED,
    CT_REVEALED, CT_CREDITS, CT_FLAG, CT_RANDOMCHANCE, CT_RANDOMINTERVAL, CT_DIPLOMACY, CT_DIFFICULTY,
    CT_CHECKUNITS, CT_CHECKBUILDINGS, CT_CHECKCRATES, CT_CHECKTILES, CT_SPICE_IN_AREA, CT_DAMAGE_IN_AREA,
    CT_POWER, CT_BUILDING_UPGRADES, CT_STARPORT_STOCK, CT_STARPORT_COST, CT_STARPORT_PICK, CT_STARPORT_DELIVERY,
    CT_26, CT_BUILDING_ICON, CT_UNIT_ICON, CT_UPGRADE_ICON, CT_SPICE_HARVESTED, CT_UNITS_BUILT,
    CT_BUILDINGS_BUILT, CT_UNITS_LOST, CT_BUILDINGS_LOST, CT_UNITS_KILLED, CT_BUILDINGS_KILLED, CT_SIDE_PROPERTY,
    CT_AI_PROPERTY, CT_MEMORY_VALUE, CT_VARIABLE_VALUE, CT_VARIABLE_CHANGED,
};

enum EventFlags : std::uint8_t {
    EF_AUTO_BLOCK = 1, EF_BLOCKED = 2, EF_CONDITIONS_OR = 4, EF_OBJECT_INDEX = 8, EF_SKIP_VAR = 16, EF_LIMIT_VAR = 32,
};

enum class BlockType { Global, Block, Condition, Loop };

// Raw record field offsets.
namespace ev {
constexpr int kCoordX = 0, kCoordY = 4, kValue = 8, kNumConditions = 12, kType = 13, kArgs = 14, kCondIndex = 19,
              kCoordVarFlags = 31, kArgVarFlags = 32, kCondNeg = 33, kFilterSkip = 45, kFlags = 46, kData = 47;
}
namespace cd {
constexpr int kVal2 = 0, kVal1 = 4, kVal3 = 8, kCoordX = 12, kCoordVarFlags = 14, kArgVarFlags = 15, kCoordY = 16,
              kVal4 = 20, kSide = 24, kType = 25, kArg1 = 26, kArg2 = 27;
}

std::int32_t rd32(const std::uint8_t* p) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
                                     static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24);
}
void wr32(std::uint8_t* p, std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    p[0] = static_cast<std::uint8_t>(u);
    p[1] = static_cast<std::uint8_t>(u >> 8);
    p[2] = static_cast<std::uint8_t>(u >> 16);
    p[3] = static_cast<std::uint8_t>(u >> 24);
}
std::uint16_t rd16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }

int hlimit(int v, int hi) { return v > hi ? hi : v; }
int llimit(int v, int lo) { return v < lo ? lo : v; }

// CompareValue (event-utils.c): comparison flag = ">=", otherwise "==".
bool compareValue(int val, int comp, bool comparison) { return comparison ? val >= comp : val == comp; }

bool compareDistance(int x1, int y1, int x2, int y2, int comp, bool comparison) {
    const int dx = std::abs(x1 - x2), dy = std::abs(y1 - y2);
    return compareValue(dx * dx + dy * dy, comp * comp, comparison);
}

// FlagOperation.
std::uint32_t flagOperation(std::uint32_t value, int flag, int op) {
    const std::uint32_t bit = 1u << (flag & 31);
    switch (op) {
    case 0: return value | bit;
    case 1: return value & ~bit;
    case 2: return value ^ bit;
    default: return value;
    }
}

// The ObjectFilterStruct (24 bytes) at event data+1 / at the start of a Check* condition.
struct Filter {
    const std::uint8_t* p;
    int posMinX() const { return p[0]; }
    int posMinY() const { return p[1]; }
    int posMaxX() const { return p[2]; }
    int posMaxY() const { return p[3]; }
    int posFlags() const { return p[4]; }
    int criteriaVarFlags() const { return p[5]; }
    int andOr() const { return rd16(p + 6); }
    int criteriaType(int i) const { return p[8 + i]; }
    int criteriaValue(int i) const { return p[16 + i]; }
};

}  // namespace

// ===========================================================================================
// The runner: one object per Mod__HandleConditionsAndEvents call (friend of World).
// ===========================================================================================
struct ExtEventRunner {
    World& w;
    ExtMission& x;
    std::array<std::uint8_t, 256> condResult{};

    explicit ExtEventRunner(World& world) : w(world), x(world.ext_) {}

    const formats::TemplatesBin& T() const { return w.rules_.templates->bin; }
    int rnd() { return w.rng_.next(); }
    // "The player" of the mission's conditions/filters: the side it was written for (= MySideID,
    // or the original side when played as another side, World::scriptSide).
    int mySide() const { return w.scriptSide(); }

    // ---------------------------------------------------------------- variables
    int var(int i) const { return x.vars[static_cast<std::size_t>(i & 255)].value; }
    void setVar(int i, int v) {  // SetVariableValue
        ExtVariable& e = x.vars[static_cast<std::size_t>(i & 255)];
        if (e.ticks != w.gameTicks_) {
            e.oldValue = e.value;
            e.ticks = w.gameTicks_;
        }
        e.value = v;
    }
    int varOrConst(int flags, int idx, int v) const { return (flags & (1 << idx)) ? var(v) : v; }

    // ValueOperation (event-utils.c). rand() where the operation asks for it.
    int valueOperation(int a, int b, int op) {
        auto mod = [](int v, int m) { return m ? v % m : 0; };  // (DebugFatal in the original)
        switch (op) {
        case 0: return b;
        case 1: return a + b;
        case 2: return a - b;
        case 3: return a * b;
        case 4: return (a * b) / 100;
        case 5: return b ? a / b : a;
        case 6: return mod(a, b);
        case 7: return hlimit(a, b);
        case 8: return llimit(a, b);
        case 9: return hlimit(a + b, 255);
        case 10: return llimit(a - b, 0);
        case 11: return a & b;
        case 12: return a | b;
        case 13: return a ^ b;
        case 14: return static_cast<int>(static_cast<std::uint32_t>(a) << (b & 31));
        case 15: return a >> (b & 31);
        case 16: return a | (1 << (b & 31));
        case 17: return a & ~(1 << (b & 31));
        case 18: return a ^ (1 << (b & 31));
        case 19: return mod(rnd(), b);
        case 20: return a + mod(rnd(), b);
        case 21: return a - mod(rnd(), b);
        case 22: return a * mod(rnd(), b);
        case 23: return a + mod(rnd(), (a * b) / 100);
        case 24: return a - mod(rnd(), (a * b) / 100);
        default: return a;
        }
    }

    // ---------------------------------------------------------------- object field access
    // Original struct offsets (D2kEditor game_structs.ini) mapped onto our fields. Returns false
    // for offsets that have no counterpart in our model (the write is then ignored).
    template <class F> static bool unitField(Unit& u, int type, int off, F&& f) {
        auto u8 = [&](auto& field) {
            int v = static_cast<std::int8_t>(static_cast<std::uint8_t>(field));
            f(v, 8);
            field = static_cast<std::remove_reference_t<decltype(field)>>(static_cast<std::uint8_t>(v));
            return true;
        };
        auto u16 = [&](int& field) {
            int v = static_cast<std::int16_t>(static_cast<std::uint16_t>(field));
            f(v, 16);
            field = static_cast<std::uint16_t>(v);
            return true;
        };
        auto i32 = [&](auto& field) {
            int v = static_cast<std::int32_t>(field);
            f(v, 32);
            field = static_cast<std::remove_reference_t<decltype(field)>>(v);
            return true;
        };
        auto st = [&](UnitState& s, int bits) {
            int v = bits == 8 ? static_cast<std::int8_t>(static_cast<std::uint8_t>(s)) : static_cast<int>(s);
            f(v, bits);
            s = static_cast<UnitState>(bits == 8 ? static_cast<std::uint8_t>(v) : v);
            return true;
        };
        if (type == 0) {  // byte
            switch (off) {
            case 14: return u8(u.tag);
            case 15: { int v = u.prevSelected; f(v, 8); u.prevSelected = (v & 0xff) != 0; return true; }
            case 16: return st(u.state, 8);
            case 20: return st(u.oldState, 8);
            case 24: return u8(u.type);
            case 25: { int v = u.selected; f(v, 8); u.selected = (v & 0xff) != 0; return true; }
            case 26: return u8(u.turnDelay);
            case 27: return u8(u.turretTurnDelay);
            case 28: return u8(u.reloadDelay);
            case 29: return u8(u.flicker);
            case 30: return u8(u.animFrame);
            case 36: return u8(u.lying);
            case 37: return u8(u.animDelay);
            case 38: return u8(u.enemySide);
            case 39: return u8(u.smokeTimer);
            case 40: return u8(u.groupId);
            case 41: return u8(u.byte29);
            case 42: return u8(u.lastAttacked);
            case 43: return u8(u.lastFired);
            case 44: return u8(u.lastMoved);
            case 45: return u8(u.oldSide);
            case 46: return u8(u.byte2E);
            case 47: return u8(u.deadStateTimer);
            case 48: return u8(u.attackerSide);
            case 49: return u8(u.stealthTimer);
            case 72: return u8(u.targetX);
            case 73: return u8(u.targetY);
            case 74: return u8(u.blockFromX);
            case 75: return u8(u.blockFromY);
            case 76: return u8(u.blockToX);
            case 77: return u8(u.blockToY);
            case 78: return u8(u.posStepsMax);
            case 79: return u8(u.posSteps);
            case 80: return u8(u.rememberX);
            case 81: return u8(u.rememberY);
            case 82: return u8(u.cellspotTo);
            case 83: return u8(u.cellspotFrom);
            case 84: return u8(u.facing);
            case 85: return u8(u.facingTurret);
            case 86: return u8(u.facingTurretTarget);
            case 116: return u8(u.routeSteps);
            case 117: return u8(u.routeMode);
            default: return false;
            }
        }
        if (type == 1) {  // word
            switch (off) {
            case 12: return u16(u.enemyIndex);
            case 50: return u16(u.attackerIndex);
            case 88: return u16(u.refineryIndex);
            case 90: return u16(u.repairPadIndex);
            case 118: return u16(u.routeTotalSteps);
            case 120: return u16(u.routeRetryDelay);
            default: return false;
            }
        }
        switch (off) {  // dword (float writes are not meaningful for these fields)
        case 0: return i32(u.speed);
        case 4: return i32(u.health);
        case 8: return i32(u.flags);
        case 16: return st(u.state, 32);
        case 20: return st(u.oldState, 32);
        case 32: return i32(u.animType);
        case 52: return i32(u.posX);
        case 56: return i32(u.posY);
        case 60: return i32(u.posZ);
        case 64: return i32(u.stepTargetX);
        case 68: return i32(u.stepTargetY);
        default: return false;
        }
    }

    template <class F> static bool buildingField(Building& b, int type, int off, F&& f) {
        auto u8 = [&](auto& field) {
            int v = static_cast<std::int8_t>(static_cast<std::uint8_t>(field));
            f(v, 8);
            field = static_cast<std::remove_reference_t<decltype(field)>>(static_cast<std::uint8_t>(v));
            return true;
        };
        auto byteOfWord = [&](int& field, int shift) {  // a byte write into a 16-bit field
            std::uint16_t wv = static_cast<std::uint16_t>(field);
            int v = static_cast<std::int8_t>(static_cast<std::uint8_t>(wv >> shift));
            f(v, 8);
            wv = static_cast<std::uint16_t>((wv & ~(0xff << shift)) | (static_cast<std::uint8_t>(v) << shift));
            field = wv;
            return true;
        };
        auto i32 = [&](auto& field) {
            int v = static_cast<std::int32_t>(field);
            f(v, 32);
            field = static_cast<std::remove_reference_t<decltype(field)>>(v);
            return true;
        };
        if (type == 0) {
            switch (off) {
            case 14: return u8(b.tag);
            case 15: { int v = b.prevSelected; f(v, 8); b.prevSelected = (v & 0xff) != 0; return true; }
            case 20: return u8(b.attackTileX);
            case 21: return u8(b.attackTileY);
            case 32: return u8(b.wallFrame);
            case 33: return u8(b.facing);
            case 44: return byteOfWord(b.enemyIndex, 0);
            case 45: return byteOfWord(b.enemyIndex, 8);
            case 48: return u8(b.state);
            case 53: { int v = b.selected; f(v, 8); b.selected = (v & 0xff) != 0; return true; }
            case 54: return u8(b.turretTurnDelay);
            case 55: return u8(b.reloadDelay);
            case 56: return u8(b.flicker);
            case 58: return u8(b.specialPurpose);
            case 59: return u8(b.deadTimer);
            case 60: return u8(b.enemySide);
            case 61: return u8(b.smokeTimer);
            case 62: return u8(b.buildupCounter);
            case 63: return u8(b.animCounter);
            case 65: return u8(b.lastAttacked);
            case 80: return u8(b.attackerSide);
            default: return false;
            }
        }
        if (type == 1) {
            switch (off) {
            case 44: { int v = static_cast<std::int16_t>(static_cast<std::uint16_t>(b.enemyIndex)); f(v, 16); b.enemyIndex = static_cast<std::uint16_t>(v); return true; }
            case 82: { int v = static_cast<std::int16_t>(static_cast<std::uint16_t>(b.attackerIndex)); f(v, 16); b.attackerIndex = static_cast<std::uint16_t>(v); return true; }
            default: return false;
            }
        }
        switch (off) {
        case 0: return i32(b.posX);
        case 4: return i32(b.posY);
        case 36: return i32(b.health);
        case 40: return i32(b.flags);
        case 48: return i32(b.state);
        default: return false;
        }
    }

    // SetDataValue on a mapped field: bits = width of the original field.
    auto setter(int op, int value) {
        return [this, op, value](int& v, int bits) {
            int r = valueOperation(v, value, op);
            if (bits == 8) r = static_cast<std::int8_t>(static_cast<std::uint8_t>(r));
            else if (bits == 16) r = static_cast<std::int16_t>(static_cast<std::uint16_t>(r));
            v = r;
        };
    }

    // ---------------------------------------------------------------- filters (event-filters.c)
    bool evalFilterPosition(const Filter& f, int ppx, int ppy) const {
        if (!(f.posFlags() & 1)) return true;
        const int px = ppx >> 5, py = ppy >> 5;
        const int posType = (f.posFlags() >> 2) & 3;
        const int minX = varOrConst(f.posFlags(), 4, f.posMinX()), minY = varOrConst(f.posFlags(), 5, f.posMinY());
        const int maxX = varOrConst(f.posFlags(), 6, f.posMaxX()), maxY = varOrConst(f.posFlags(), 7, f.posMaxY());
        const int cx = minX, cy = minY, cpx = cx * 32 + 16, cpy = cy * 32 + 16, r = maxX;
        bool res = false;
        switch (posType) {
        case 0: res = px >= minX && py >= minY && px <= maxX && py <= maxY; break;
        case 1: res = std::abs(px - cx) <= r && std::abs(py - cy) <= r; break;
        case 2: res = (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r; break;
        case 3: res = (ppx - cpx) * (ppx - cpx) + (ppy - cpy) * (ppy - cpy) <= r * r; break;
        }
        return res != ((f.posFlags() & 2) != 0);
    }

    // EvaluateFilterExpression: and/or chain, OR (odd codes) binding looser than AND? The
    // patch reduces operation codes 3, 2, 1, 0 in that order (each pass left to right).
    static bool evalExpression(const Filter& f, const std::array<bool, 8>& in) {
        std::array<int, 8> op{};
        std::array<bool, 8> r = in;
        for (int i = 0; i < 8; ++i) op[i] = (f.andOr() >> (i * 2)) & 3;
        int total = 0;
        for (int operation = 3; operation >= 0; --operation) {
            int shifts = 0;
            for (int i = 0; i < 7 - total; ++i) {
                const int k = i - shifts;
                if (op[k] == operation) {
                    r[k] = (operation & 1) ? (r[k] || r[k + 1]) : (r[k] && r[k + 1]);
                    for (int j = k + 1; j < 7 - total; ++j) r[j] = r[j + 1];
                    for (int j = k; j < 7 - total; ++j) op[j] = op[j + 1];
                    ++shifts;
                }
            }
            total += shifts;
        }
        return r[0];
    }

    template <class C> bool matchCriteria(const Filter& f, C&& crit) const {
        std::array<bool, 8> res{};
        for (int i = 0; i < 8; ++i) {
            const int value = varOrConst(f.criteriaVarFlags(), i, f.criteriaValue(i));
            const int ct = f.criteriaType(i);
            res[i] = crit(ct & 63, (ct & 64) != 0, (ct & 128) != 0, value);
        }
        return evalExpression(f, res);
    }

    static bool isSpecialUnit(int beh) {
        return beh == behaviour::kSandworm || beh == behaviour::kFrigate || beh == behaviour::kOrnithopter ||
               beh == behaviour::kCarryall || beh == behaviour::kDeathHand;
    }

    // Kind of the object in a pool slot: 1 unit, 2 building, else 0.
    int slotKind(int side, int index) const {
        if (side < 0 || side >= kMaxSides || index < 0 || index >= kObjectsPerSide) return 0;
        const SlotKind k = w.sides_[side].links[index].kind;
        return k == SlotKind::Unit ? 1 : k == SlotKind::Building ? 2 : 0;
    }

    bool tileCheck(int x, int y, int check, bool cmp, int value) {
        if (!w.inBounds(x, y)) return false;
        const std::uint32_t a = w.cell(x, y).flags;
        switch (check) {
        case 0: return compareValue(x, value, cmp);
        case 1: return compareValue(y, value, cmp);
        case 2: return (a & (1u << (value & 31))) != 0;
        case 3: return compareValue(static_cast<int>((a >> 29) & 7), value, cmp);
        case 4: return compareValue(static_cast<int>((a >> 20) & 7), value, cmp);
        case 5: return compareValue(static_cast<int>((a >> 17) & 7), value, cmp);
        case 6: return compareValue(static_cast<int>(a & 7), value, cmp);
        case 7: return compareValue(static_cast<int>((a >> 25) & 7), value, cmp);
        case 8: case 9: case 10: case 11: case 12: {
            int side = 0, index = 0;
            if (!(a & kAnyUnit) || !w.getUnitOnTile(32 * x, 32 * y, side, index, false)) return false;
            index = -1;
            while (Unit* u = w.getNextUnitOnTile(x, y, side, index)) {
                switch (check) {
                case 8: if (compareValue(u->type, value, cmp)) return true; break;
                case 9: if (compareValue(T().units[u->type].unitGroup, value, cmp)) return true; break;
                case 10: if (compareValue(u->tag, value, cmp)) return true; break;
                case 11: if (u->flags & (1u << (value & 31))) return true; break;
                case 12: if (compareValue(static_cast<int>(u->state), value, cmp)) return true; break;
                }
            }
            return false;
        }
        case 13: case 14: case 15: case 16: case 17: case 18: {
            int side = 0, index = 0;
            if (!(a & kBuilding)) return false;
            const Building* b = w.buildingAt(x, y, side, index);
            if (!b) return false;
            const auto& bt = T().buildings[b->type];
            switch (check) {
            case 13: return compareValue(b->type, value, cmp);
            case 14: return compareValue(bt.buildingGroup, value, cmp);
            case 15: return compareValue(b->tag, value, cmp);
            case 16: return (b->flags & (1u << (value & 31))) != 0;
            case 17: return compareValue(b->state, value, cmp);
            case 18:
                return compareValue((x - (b->posX >> 21)) + (y - (((b->posY >> 16) - bt.artHeight) >> 5)) * 4, value, cmp);
            }
            return false;
        }
        default: return false;
        }
    }

    bool unitCategory(const Unit& u, int side, int value) {
        const auto& t = T().units[u.type];
        const bool special = isSpecialUnit(t.specialBehavior);
        const int px = u.posX >> 21, py = u.posY >> 21;
        const bool tileValid = w.inBounds(px, py);
        const bool enemyValid = u.enemyIndex != kNoIndex, attackerValid = u.attackerIndex != kNoIndex;
        const int es = u.enemySide & 0xff, as = u.attackerSide & 0xff, os = u.oldSide;
        auto dip = [&](int other, int v) { return other < 8 && w.diplomacy_[side][other] == v && side != other; };
        switch (value) {
        case 0: return t.isInfantry;
        case 1: return !t.isInfantry && !special;
        case 2: return !t.isInfantry && !t.canCrushInfantry && !special;
        case 3: return !t.isInfantry && t.canCrushInfantry && !special;
        case 4: return special;
        case 5: return t.primaryWeapon != -1;
        case 6: return t.secondaryWeapon != -1;
        case 7: return t.barrelArt != -1;
        case 8: return false;  // UnitUpgradeAllowed (extended templates) not loaded
        case 9: return side == mySide();
        case 10: return w.diplomacy_[side][mySide()] == 0 && side != mySide();
        case 11: return w.diplomacy_[side][mySide()] == 1 && side != mySide();
        case 12: return w.diplomacy_[side][mySide()] == 2 && side != mySide();
        case 13: return u.selected;
        case 14: return false;  // under the mouse cursor: no cursor in the model
        case 15: return u.posSteps == 0;
        case 16: return tileValid && (w.cell(px, py).shroud & 15) == 0;
        case 17: return tileValid && (w.cell(px, py).shroud & 15) != 1;
        case 18: return u.lying != 0;
        case 19: return enemyValid;
        case 20: return enemyValid && !special && slotKind(es, u.enemyIndex) == 1;
        case 21: return enemyValid && !special && slotKind(es, u.enemyIndex) == 2;
        case 22: return enemyValid && es == mySide();
        case 23: return enemyValid && dip(es, 0);
        case 24: return enemyValid && dip(es, 1);
        case 25: return enemyValid && dip(es, 2);
        case 26: return attackerValid;
        case 27: return attackerValid && slotKind(as, u.attackerIndex) == 1;
        case 28: return attackerValid && slotKind(as, u.attackerIndex) == 2;
        case 29: return attackerValid && as == mySide();
        case 30: return attackerValid && dip(as, 0);
        case 31: return attackerValid && dip(as, 1);
        case 32: return attackerValid && dip(as, 2);
        case 33: return (u.flags & kDeviated) && os == mySide();
        case 34: return (u.flags & kDeviated) && dip(os, 0);
        case 35: return (u.flags & kDeviated) && dip(os, 1);
        case 36: return (u.flags & kDeviated) && dip(os, 2);
        case 37: case 38: case 39: case 40: return u.routeMode == value - 37;
        default: return true;
        }
    }

    bool buildingCategory(const Building& b, int side, int value) {
        const auto& t = T().buildings[b.type];
        const bool enemyValid = b.enemyIndex != kNoIndex, attackerValid = b.attackerIndex != kNoIndex;
        const int es = b.enemySide & 0xff, as = b.attackerSide & 0xff;
        auto dip = [&](int other, int v) { return other < 8 && w.diplomacy_[side][other] == v && side != other; };
        switch (value) {
        case 0: return t.powerConsumption > 0;
        case 1: return t.powerConsumption < 0;
        case 2: return t.requireEnoughPower != 0;
        case 3: return t.screenShake > 0;
        case 5: return t.primaryWeapon != -1;
        case 6: return t.secondaryWeapon != -1;
        case 7: return t.barrelArt != -1;
        case 8: return w.canSideUpgradeGroup(side, t.buildingGroup);
        case 9: return side == mySide();
        case 10: return w.diplomacy_[side][mySide()] == 0 && side != mySide();
        case 11: return w.diplomacy_[side][mySide()] == 1 && side != mySide();
        case 12: return w.diplomacy_[side][mySide()] == 2 && side != mySide();
        case 13: return b.selected;
        case 14: return false;  // under the cursor
        case 19: return enemyValid;
        case 20: return enemyValid && slotKind(es, b.enemyIndex) == 1;
        case 21: return enemyValid && slotKind(es, b.enemyIndex) == 2;
        case 22: return enemyValid && es == mySide();
        case 23: return enemyValid && dip(es, 0);
        case 24: return enemyValid && dip(es, 1);
        case 25: return enemyValid && dip(es, 2);
        case 26: return attackerValid;
        case 27: return attackerValid && slotKind(as, b.attackerIndex) == 1;
        case 28: return attackerValid && slotKind(as, b.attackerIndex) == 2;
        case 29: return attackerValid && as == mySide();
        case 30: return attackerValid && dip(as, 0);
        case 31: return attackerValid && dip(as, 1);
        case 32: return attackerValid && dip(as, 2);
        default: return true;
        }
    }

    // ObjectCheck: a property of the unit/building in slot (side, index).
    bool objectCheck(int side, int index, int check, int px, int py, bool cmp, int value) {
        const int kind = slotKind(side, index);
        if (kind == 1) {
            Unit& u = w.sides_[side].units[index];
            const auto& ut = T().units[u.type];
            switch (check) {
            case 0: return compareValue(u.type, value, cmp);
            case 1: return compareValue(ut.unitGroup, value, cmp);
            case 2: return unitCategory(u, side, value);
            case 3: return compareValue(u.tag, value, cmp);
            case 4: return compareValue(ut.armorType, value, cmp);
            case 5: return (ut.primaryWeapon != -1 && compareValue(ut.primaryWeapon, value, cmp)) ||
                           (ut.secondaryWeapon != -1 && compareValue(ut.secondaryWeapon, value, cmp));
            case 6: return compareDistance(u.posX >> 16, u.posY >> 16, px, py, value, cmp);
            }
        } else if (kind == 2) {
            Building& b = w.sides_[side].buildings[index];
            const auto& bt = T().buildings[b.type];
            switch (check) {
            case 0: return compareValue(b.type, value, cmp);
            case 1: return compareValue(bt.buildingGroup, value, cmp);
            case 2: return buildingCategory(b, side, value);
            case 3: return compareValue(b.tag, value, cmp);
            case 4: return compareValue(bt.armorType, value, cmp);
            case 5: return (bt.primaryWeapon != -1 && compareValue(bt.primaryWeapon, value, cmp)) ||
                           (bt.secondaryWeapon != -1 && compareValue(bt.secondaryWeapon, value, cmp));
            case 6: return compareDistance((b.posX >> 16) + 16, (b.posY >> 16) - bt.artHeight + 16, px, py, value, cmp);
            }
        }
        return false;
    }

    int weaponRange(int weapon) const { return weapon != -1 ? static_cast<int>(T().weapons[weapon].range) : 0; }

    bool unitCriteria(Unit& u, int side, int ct, bool neg, bool cmp, int value) {
        const auto& t = T().units[u.type];
        const bool special = isSpecialUnit(t.specialBehavior);
        const int px = u.posX >> 21, py = u.posY >> 21;
        const int nx = u.blockToX, ny = u.blockToY, tx = u.targetX, ty = u.targetY;
        const bool tileValid = w.inBounds(px, py), nextValid = w.inBounds(nx, ny), targValid = w.inBounds(tx, ty);
        const bool enemyBad = u.enemyIndex == kNoIndex || special, attackerBad = u.attackerIndex == kNoIndex;
        const int es = u.enemySide & 0xff, as = u.attackerSide & 0xff;
        const int ei = u.enemyIndex, ai = u.attackerIndex;
        const bool atTarget = px == tx && py == ty;
        bool r = true;
        switch (ct) {
        case 0: r = true; break;
        case 1: r = compareValue(u.type, value, cmp); break;
        case 2: r = compareValue(t.unitGroup, value, cmp); break;
        case 3: r = compareValue(t.specialBehavior, value, cmp); break;
        case 4: r = unitCategory(u, side, value); break;
        case 5: r = compareValue(u.tag, value, cmp); break;
        case 6: r = (t.ownerHouse & (1 << (value & 7))) != 0; break;
        case 7: r = compareValue(t.armorType, value, cmp); break;
        case 8: r = compareValue(t.speedType, value, cmp); break;
        case 9: r = compareValue(t.primaryWeapon, value, cmp); break;
        case 10: r = compareValue(t.secondaryWeapon, value, cmp); break;
        case 11: r = compareValue(t.sightRadius, value, cmp); break;
        case 12: r = compareValue(weaponRange(t.primaryWeapon), value, cmp); break;
        case 13: r = compareValue(static_cast<int>(t.speed >> 12), value, cmp); break;
        case 14: r = compareValue(t.primaryWeaponLongDelay, value, cmp); break;
        case 15: r = compareValue(static_cast<int>(t.hitPoints / 100), value, cmp); break;
        case 16: r = compareValue(u.health / 100, value, cmp); break;
        case 17: r = compareValue(t.hitPoints ? static_cast<int>((static_cast<std::int64_t>(u.health) * 100) / t.hitPoints) : 0, value, cmp); break;
        case 18: r = (u.flags & (1u << (value & 31))) != 0; break;
        case 19: r = compareValue(static_cast<int>(u.state), value, cmp); break;
        case 20: r = compareValue(u.groupId, value, cmp); break;
        case 21: if (!tileValid) return false; r = tileCheck(px, py, 2, cmp, value); break;
        case 22: if (!tileValid) return false; r = tileCheck(px, py, 3, cmp, value); break;
        case 23: if (!tileValid) return false; r = tileCheck(px, py, 13, cmp, value); break;
        case 24: if (!tileValid) return false; r = tileCheck(px, py, 14, cmp, value); break;
        case 25: if (!tileValid) return false; r = tileCheck(px, py, 15, cmp, value); break;
        case 26: if (!tileValid) return false; r = tileCheck(px, py, 16, cmp, value); break;
        case 27: if (!tileValid) return false; r = tileCheck(px, py, 18, cmp, value); break;
        case 28: r = compareValue(nx, value, cmp); break;
        case 29: r = compareValue(ny, value, cmp); break;
        case 30: if (!nextValid) return false; r = tileCheck(nx, ny, 2, cmp, value); break;
        case 31: if (!nextValid) return false; r = tileCheck(nx, ny, 3, cmp, value); break;
        case 32: if (atTarget) return false; r = compareValue(tx, value, cmp); break;
        case 33: if (atTarget) return false; r = compareValue(ty, value, cmp); break;
        case 34: if (atTarget) return false; r = targValid && tileCheck(tx, ty, 2, cmp, value); break;
        case 35: if (atTarget) return false; r = targValid && tileCheck(tx, ty, 3, cmp, value); break;
        case 36: if (atTarget) return false; r = compareDistance(u.posX >> 16, u.posY >> 16, tx * 32 + 16, ty * 32 + 16, value, cmp); break;
        case 37: if (enemyBad) return false; r = compareValue(es, value, cmp); break;
        case 38: if (enemyBad) return false; r = objectCheck(es, ei, 0, 0, 0, cmp, value); break;
        case 39: if (enemyBad) return false; r = objectCheck(es, ei, 1, 0, 0, cmp, value); break;
        case 40: if (enemyBad) return false; r = objectCheck(es, ei, 2, 0, 0, cmp, value); break;
        case 41: if (enemyBad) return false; r = objectCheck(es, ei, 3, 0, 0, cmp, value); break;
        case 42: if (enemyBad) return false; r = objectCheck(es, ei, 4, 0, 0, cmp, value); break;
        case 43: if (enemyBad) return false; r = objectCheck(es, ei, 6, u.posX >> 16, u.posY >> 16, cmp, value); break;
        case 44: r = static_cast<std::int16_t>(u.enemyIndex) == value; break;
        case 45: if (attackerBad) return false; r = compareValue(as, value, cmp); break;
        case 46: if (attackerBad) return false; r = objectCheck(as, ai, 0, 0, 0, cmp, value); break;
        case 47: if (attackerBad) return false; r = objectCheck(as, ai, 1, 0, 0, cmp, value); break;
        case 48: if (attackerBad) return false; r = objectCheck(as, ai, 2, 0, 0, cmp, value); break;
        case 49: if (attackerBad) return false; r = objectCheck(as, ai, 3, 0, 0, cmp, value); break;
        case 50: if (attackerBad) return false; r = objectCheck(as, ai, 5, 0, 0, cmp, value); break;
        case 51: if (attackerBad) return false; r = objectCheck(as, ai, 6, u.posX >> 16, u.posY >> 16, cmp, value); break;
        case 52: r = static_cast<std::int16_t>(u.attackerIndex) == value; break;
        case 53: r = compareValue(u.facing, value, cmp); break;
        case 54: r = compareValue(u.facingTurret, value, cmp); break;
        case 55: r = compareValue(u.animFrame, value, cmp); break;
        case 56: r = compareValue(u.byte29, value, cmp); break;
        case 57: r = compareValue(u.lastAttacked, value, cmp); break;
        case 58: r = compareValue(u.lastFired, value, cmp); break;
        case 59: r = compareValue(u.lastMoved, value, cmp); break;
        case 60: r = compareValue(u.oldSide, value, cmp); break;
        case 61: r = compareValue(u.byte2E, value, cmp); break;
        case 62: r = compareValue(u.deadStateTimer, value, cmp); break;
        case 63: r = compareValue(u.reloadDelay, value, cmp); break;
        default: break;
        }
        return r != neg;
    }

    bool buildingCriteria(Building& b, int side, int ct, bool neg, bool cmp, int value) {
        const auto& t = T().buildings[b.type];
        const Side& s = w.sides_[side];
        const bool enemyBad = b.enemyIndex == kNoIndex, attackerBad = b.attackerIndex == kNoIndex;
        const int es = b.enemySide & 0xff, as = b.attackerSide & 0xff;
        const int cx = (b.posX >> 16) + 16, cy = (b.posY >> 16) - t.artHeight + 16;
        auto concrete = [&](int& total, int& onConcrete) {
            w.buildingConcreteCount(b.type, b.posX >> 21, ((b.posY >> 16) - t.artHeight) >> 5, total, onConcrete);
        };
        bool r = true;
        switch (ct) {
        case 0: r = true; break;
        case 1: r = compareValue(b.type, value, cmp); break;
        case 2: r = compareValue(t.buildingGroup, value, cmp); break;
        case 3: r = compareValue(t.specialBehavior, value, cmp); break;
        case 4: r = buildingCategory(b, side, value); break;
        case 5: r = compareValue(b.tag, value, cmp); break;
        case 6: r = (t.ownerHouse & (1 << (value & 7))) != 0; break;
        case 7: r = compareValue(t.armorType, value, cmp); break;
        case 8: r = compareValue(t.primaryWeapon, value, cmp); break;
        case 9: r = compareValue(t.secondaryWeapon, value, cmp); break;
        case 10: r = compareValue(t.sightRadius, value, cmp); break;
        case 11: r = compareValue(t.powerConsumption > 0 ? t.powerConsumption : 0, value, cmp); break;
        case 12: r = compareValue(t.powerConsumption < 0 ? -t.powerConsumption : 0, value, cmp); break;
        case 13: r = compareValue(weaponRange(t.primaryWeapon), value, cmp); break;
        case 14: r = compareValue(t.primaryWeaponLongDelay, value, cmp); break;
        case 15: r = compareValue(static_cast<int>(t.hitPoints / 100), value, cmp); break;
        case 16: r = compareValue(b.health / 100, value, cmp); break;
        case 17: r = compareValue(t.hitPoints ? static_cast<int>((static_cast<std::int64_t>(b.health) * 100) / t.hitPoints) : 0, value, cmp); break;
        case 18: r = (b.flags & (1u << (value & 31))) != 0; break;
        case 19: r = compareValue(b.state, value, cmp); break;
        case 20: r = compareValue(s.upgrades[t.buildingGroup], value, cmp); break;
        case 21: { int tot = 0, on = 0; concrete(tot, on); r = compareValue(tot, value, cmp); break; }
        case 22: { int tot = 0, on = 0; concrete(tot, on); r = compareValue(on, value, cmp); break; }
        case 23: { int tot = 0, on = 0; concrete(tot, on); r = compareValue(tot ? (on * 100) / tot : 0, value, cmp); break; }
        case 24: case 25: case 26: case 27: case 50: return false;
        case 28: if (enemyBad) return false; r = compareValue(es, value, cmp); break;
        case 29: if (enemyBad) return false; r = objectCheck(es, b.enemyIndex, 0, 0, 0, cmp, value); break;
        case 30: if (enemyBad) return false; r = objectCheck(es, b.enemyIndex, 1, 0, 0, cmp, value); break;
        case 31: if (enemyBad) return false; r = objectCheck(es, b.enemyIndex, 2, 0, 0, cmp, value); break;
        case 32: if (enemyBad) return false; r = objectCheck(es, b.enemyIndex, 3, 0, 0, cmp, value); break;
        case 33: if (enemyBad) return false; r = objectCheck(es, b.enemyIndex, 4, 0, 0, cmp, value); break;
        case 34: if (enemyBad) return false; r = objectCheck(es, b.enemyIndex, 6, cx, cy, cmp, value); break;
        case 35: r = static_cast<std::int16_t>(b.enemyIndex) == value; break;
        case 36: if (attackerBad) return false; r = compareValue(as, value, cmp); break;
        case 37: if (attackerBad) return false; r = objectCheck(as, b.attackerIndex, 0, 0, 0, cmp, value); break;
        // (the patch swaps group / category for the attacker checks; kept)
        case 38: if (attackerBad) return false; r = objectCheck(as, b.attackerIndex, 2, 0, 0, cmp, value); break;
        case 39: if (attackerBad) return false; r = objectCheck(as, b.attackerIndex, 1, 0, 0, cmp, value); break;
        case 40: if (attackerBad) return false; r = objectCheck(as, b.attackerIndex, 3, 0, 0, cmp, value); break;
        case 41: if (attackerBad) return false; r = objectCheck(as, b.attackerIndex, 5, 0, 0, cmp, value); break;
        case 42: if (attackerBad) return false; r = objectCheck(as, b.attackerIndex, 6, cx, cy, cmp, value); break;
        case 43: r = static_cast<std::int16_t>(b.attackerIndex) == value; break;
        case 44: r = compareValue(b.facing, value, cmp); break;
        case 45: r = compareValue(0, value, cmp); break;  // DirectionFrame (+0x39) not kept
        case 46: r = compareValue(b.buildupCounter, value, cmp); break;
        case 47: r = compareValue(b.animCounter, value, cmp); break;
        case 48: r = compareValue(b.specialPurpose, value, cmp); break;
        case 49: r = compareValue(b.lastAttacked, value, cmp); break;
        case 51: r = compareValue(b.deadTimer, value, cmp); break;
        case 52: r = compareValue(b.reloadDelay, value, cmp); break;
        case 53: r = compareValue(b.turretTurnDelay, value, cmp); break;
        default: r = compareValue(0, value, cmp); break;  // custom bytes (not kept: 0)
        }
        return r != neg;
    }

    bool tileCriteria(int x, int y, int ct, bool neg, bool cmp, int value) {
        const Cell& c = w.cell(x, y);
        bool r = false;
        switch (ct) {
        case 0: r = true; break;
        case 1: r = compareValue(c.tile, value, cmp); break;
        case 2: r = compareValue(c.tile % 20, value, cmp); break;
        case 3: r = compareValue(c.tile / 20, value, cmp); break;
        case 4:
            switch (value) {
            case 0: r = false; break;  // under the cursor
            case 1: r = (c.shroud & 15) == 1; break;
            case 2: r = (c.shroud & 15) != 0; break;
            case 3: r = (c.shroud & 15) != 1; break;
            case 4: r = (c.shroud & 15) == 0; break;
            case 5: r = c.tile != backupTile(x, y); break;
            case 6: r = (c.flags & kInfantryMask) != 0; break;
            case 7: r = (c.flags & kAnyUnit) != 0; break;
            case 8: r = (c.flags & kOccupied) != 0; break;
            }
            break;
        case 5: r = compareValue(c.damage, value, cmp); break;
        case 6: r = compareValue(c.shroud, value, cmp); break;
        default: r = tileCheck(x, y, ct - 7, cmp, value); break;
        }
        return r != neg;
    }

    bool unitMatches(const Filter& f, Unit& u, int side) {
        if (!evalFilterPosition(f, u.posX >> 16, u.posY >> 16)) return false;
        return matchCriteria(f, [&](int ct, bool neg, bool cmp, int v) { return unitCriteria(u, side, ct, neg, cmp, v); });
    }
    bool buildingMatches(const Filter& f, Building& b, int side) {
        const int px = (b.posX >> 16) + 16, py = (b.posY >> 16) - T().buildings[b.type].artHeight + 16;
        if (!evalFilterPosition(f, px, py)) return false;
        return matchCriteria(f, [&](int ct, bool neg, bool cmp, int v) { return buildingCriteria(b, side, ct, neg, cmp, v); });
    }
    bool bulletMatches(const Filter& f, Bullet& b) {
        if (!evalFilterPosition(f, b.posX >> 16, b.posY >> 16)) return false;
        return matchCriteria(f, [&](int ct, bool neg, bool cmp, int v) {
            const auto& wt = T().weapons[b.type];
            bool r = true;
            switch (ct) {
            case 1: r = compareValue(b.type, v, cmp); break;
            case 2: r = v == 0 ? wt.antiAircraft != 0 : true; break;
            case 3: r = compareValue(b.tag, v, cmp); break;
            case 4: r = compareValue(static_cast<int>(wt.damage), v, cmp); break;
            case 5: r = compareValue(static_cast<int>(wt.range), v, cmp); break;
            case 6: r = compareValue(static_cast<int>(wt.projectileSpeed), v, cmp); break;
            case 7: r = compareValue(wt.warhead, v, cmp); break;
            case 8: r = compareValue(wt.hitExplosion, v, cmp); break;
            case 9: r = compareValue(wt.trailExplosion, v, cmp); break;
            case 10: r = (b.flags & (1u << (v & 31))) != 0; break;
            case 11: r = compareValue(b.moveSteps, v, cmp); break;
            case 12: r = compareValue(b.speedX, v, cmp); break;
            case 13: r = compareValue(b.speedY, v, cmp); break;
            case 14: r = compareValue(static_cast<std::int16_t>(b.firerIndex), v, cmp); break;
            case 15: r = compareValue(static_cast<std::int16_t>(b.homingIndex), v, cmp); break;
            case 16: r = compareValue(b.homingSide, v, cmp); break;
            case 17: r = compareValue(b.targetX, v, cmp); break;
            case 18: r = compareValue(b.targetY, v, cmp); break;
            case 19: r = compareValue(b.posZ, v, cmp); break;
            case 20: r = compareValue(b.animFrame, v, cmp); break;
            default: if (ct >= 21) r = compareValue(0, v, cmp); break;
            }
            return r != neg;
        });
    }
    bool explosionMatches(const Filter& f, Explosion& e) {
        if (!evalFilterPosition(f, e.posX >> 16, e.posY >> 16)) return false;
        return matchCriteria(f, [&](int ct, bool neg, bool cmp, int v) {
            bool r = true;
            switch (ct) {
            case 1: r = compareValue(e.type, v, cmp); break;
            case 2: r = compareValue(e.tag, v, cmp); break;
            case 3: r = (e.flags & (1u << (v & 31))) != 0; break;
            case 4: r = compareValue(e.frame, v, cmp); break;
            case 5: r = compareValue(e.delay, v, cmp); break;
            case 6: r = compareValue(e.posZ, v, cmp); break;
            default: if (ct >= 7) r = compareValue(0, v, cmp); break;
            }
            return r != neg;
        });
    }
    bool crateMatches(const Filter& f, const Crate& c) {
        if (!c.active) return false;
        if (!evalFilterPosition(f, c.x * 32 + 16, c.y * 32 + 16)) return false;
        return matchCriteria(f, [&](int ct, bool neg, bool cmp, int v) {
            bool r = true;
            switch (ct) {
            case 0: r = true; break;
            case 1: r = compareValue(c.type, v, cmp); break;
            case 2: r = compareValue(c.image, v, cmp); break;
            case 3:
                switch (v) {
                case 1: r = c.type >= 7 && c.type <= 10; break;
                case 2: r = c.type >= 7 && c.type <= 10; break;
                default: r = false; break;
                }
                break;
            case 4: case 5: r = compareValue(0, v, cmp) && ct == 4; break;  // ext data not kept (0)
            case 6: r = compareValue(c.respawns, v, cmp); break;
            case 7: r = compareValue(static_cast<int>(c.timer), v, cmp); break;
            case 8: r = compareValue(static_cast<int>(c.timer / 25), v, cmp); break;
            case 9: r = compareValue(0, v, cmp); break;
            default: r = tileCheck(c.x, c.y, ct - 10, cmp, v); break;
            }
            return r != neg;
        });
    }
    bool tileMatches(const Filter& f, int x, int y) {
        if (!evalFilterPosition(f, x * 32 + 16, y * 32 + 16)) return false;
        return matchCriteria(f, [&](int ct, bool neg, bool cmp, int v) { return tileCriteria(x, y, ct, neg, cmp, v); });
    }
    bool sideMatches(const Filter& f, int side) {
        return matchCriteria(f, [&](int ct, bool neg, bool cmp, int v) {
            const Side& s = w.sides_[side];
            bool r = false;
            switch (ct) {
            case 0: r = true; break;
            case 1: r = compareValue(side, v, cmp); break;
            case 2: r = compareValue(s.house, v, cmp); break;
            case 3: r = compareValue(s.techLevel, v, cmp); break;
            case 4:
                switch (v) {
                case 0: r = side == mySide(); break;
                case 1: r = w.diplomacy_[side][mySide()] == 0; break;
                case 2: r = w.diplomacy_[side][mySide()] == 1; break;
                case 3: r = w.diplomacy_[side][mySide()] == 2; break;
                case 4: r = w.ai_[side].enabled(); break;
                case 5: r = s.participates; break;
                case 6: r = w.unitsExist_[side] != 0; break;
                case 7: r = w.buildingsExist_[side] != 0; break;
                }
                break;
            case 5: r = w.diplomacy_[side][v & 7] == 0; break;
            case 6: r = w.diplomacy_[side][v & 7] == 1; break;
            case 7: r = w.diplomacy_[side][v & 7] == 2; break;
            case 8: r = compareValue(s.cash + s.spice, v, cmp); break;
            case 9: r = compareValue(s.spice, v, cmp); break;
            case 10: r = compareValue(s.cash, v, cmp); break;
            case 11: r = compareValue(s.maxStorage, v, cmp); break;
            case 12: r = compareValue(static_cast<int>(s.powerPercent), v, cmp); break;
            case 13: r = compareValue(static_cast<int>(s.powerOutput), v, cmp); break;
            case 14: r = compareValue(static_cast<int>(s.powerDemand), v, cmp); break;
            case 15: r = s.upgrades[v & 0xff] >= 1; break;
            case 16: r = s.upgrades[v & 0xff] >= 2; break;
            case 17: r = s.upgrades[v & 0xff] >= 3; break;
            case 18: r = compareValue(s.spiceHarvested, v, cmp); break;
            case 19: r = compareValue(s.unitsBuilt, v, cmp); break;
            case 20: r = compareValue(s.buildingsBuilt, v, cmp); break;
            case 21: r = compareValue(s.unitsLost, v, cmp); break;
            case 22: r = compareValue(s.buildingsLost, v, cmp); break;
            case 23: r = compareValue(s.unitsKilled, v, cmp); break;
            case 24: r = compareValue(s.buildingsKilled, v, cmp); break;
            }
            return r != neg;
        });
    }

    void boundsForPosFilter(const Filter& f, int& x0, int& y0, int& x1, int& y1) const {
        x0 = 0;
        y0 = 0;
        x1 = w.width_ - 1;
        y1 = w.height_ - 1;
        if ((f.posFlags() & 1) && !(f.posFlags() & 2)) {
            const int minX = varOrConst(f.posFlags(), 4, f.posMinX()), minY = varOrConst(f.posFlags(), 5, f.posMinY());
            const int maxX = varOrConst(f.posFlags(), 6, f.posMaxX()), maxY = varOrConst(f.posFlags(), 7, f.posMaxY());
            switch ((f.posFlags() >> 2) & 3) {
            case 0: x0 = minX; y0 = minY; x1 = maxX; y1 = maxY; break;
            case 1:
            case 2:
                x0 = llimit(minX - maxX, x0); y0 = llimit(minY - maxX, y0);
                x1 = hlimit(minX + maxX, x1); y1 = hlimit(minY + maxX, y1);
                break;
            case 3:
                x0 = llimit(minX - maxX / 32, x0); y0 = llimit(minY - maxX / 32, y0);
                x1 = hlimit(minX + maxX / 32, x1); y1 = hlimit(minY + maxX / 32, y1);
                break;
            }
        }
        // (the original indexes the map unchecked; keep the scan on the map)
        x0 = std::max(x0, 0);
        y0 = std::max(y0, 0);
        x1 = std::min(x1, w.width_ - 1);
        y1 = std::min(y1, w.height_ - 1);
    }

    std::uint16_t backupTile(int tx, int ty) const {
        const std::size_t i = static_cast<std::size_t>(ty) * w.width_ + tx;
        return i < x.backupTile.size() ? x.backupTile[i] : w.cell(tx, ty).tile;
    }
    std::uint32_t tileAtrFlags(int tile) const {
        tile &= 0x0fff;
        return tile < static_cast<int>(x.tileAtr.size()) ? x.tileAtr[static_cast<std::size_t>(tile)] : 0u;
    }

    // ---------------------------------------------------------------- conditions
    bool evaluateCondition(int ci);

    // ---------------------------------------------------------------- events
    bool isStartBlock(int i) const {
        const int t = x.events[static_cast<std::size_t>(i)][ev::kType];
        return t == ET_CALLABLE_BLOCK_START || t == ET_HOOK_BLOCK_START || t == ET_IF ||
               (t >= ET_LOOP_WHILE && t <= ET_LOOP_SIDES);
    }
    int findEndMarker(int i) {
        const int n = static_cast<int>(x.events.size());
        const int t0 = x.events[static_cast<std::size_t>(i)][ev::kType];
        for (int k = i + 1; k < n; ++k) {
            const int t = x.events[static_cast<std::size_t>(k)][ev::kType];
            if (x.elseIndex[static_cast<std::size_t>(i)] == -1 && (t0 == ET_IF || t0 == ET_ELSE_IF) &&
                (t == ET_ELSE_IF || t == ET_ELSE))
                x.elseIndex[static_cast<std::size_t>(i)] = k;
            if (t == ET_END) return k;
            if (isStartBlock(k)) k = findEndMarker(k);
        }
        return n - 1;  // DebugFatal "Missing END event": run to the end
    }

    void executeRange(int first, int last, BlockType bt);
    void executeEvent(int i);
    void executeAction(int i, int type, std::array<int, 4>& cx, std::array<int, 4>& cy, std::array<int, 6>& a,
                       std::uint8_t* data, int obj);

    void run() {
        x.tickRandom = rnd();  // tick_random_value = rand()
        const int nc = static_cast<int>(x.conditions.size());
        for (int i = 0; i < nc; ++i) condResult[static_cast<std::size_t>(i)] = evaluateCondition(i) ? 1 : 0;
        x.exitCount = x.breakCount = x.continueCount = 0;
        executeRange(0, static_cast<int>(x.events.size()), BlockType::Global);
        x.exitCount = x.breakCount = x.continueCount = 0;
    }

    // ---- actions (EvAct_*)
    void addDelivery(int ex, int ey, int side, int amount, int tag, int deploy, int delay, int type, const std::uint8_t* list);
    void setCash(int side, int op, int value);
    void unitSpawn(int ex, int ey, int side, int amount, int facing, int tag, const std::uint8_t* list);
    void randomSpread(int& px, int& py, int sx, int sy, bool circle);
    void addUnit(int ex, int ey, int side, int props, int type, int movement, int facing, int tag, int targetVar);
    void addBuilding(int ex, int ey, int side, int props, int type, int method, int facing, int tag, int targetVar);
    void spiceBloom(int ex, int ey, int range, int mode, bool randomizer);
    void changeMapTile(int ex, int ey, int tile, int mode);
    void transferCredits(int side, int op, int value);
    void setBuildingUpgrades(int side, int group, int op, int value);
    void airliftUnit(int side, int tx, int ty, bool unitsTarget, int index);
    void hideTile(int ex, int ey);
    void hideMap();
    void switchMySide(int side, int aiSwitch, bool revealBase);
    void selectionBackup(int side, bool units, bool buildings);
    void restoreUnitSelection(int side, bool restore);
    void restoreBuildingSelection(int side, bool restore);
    template <class Filt, class Issue> void forSelectedUnits(int side, Filt&& filt, Issue&& issue) {
        if (side < 0 || side >= kMaxSides) return;
        Side& s = w.sides_[side];
        int n = 0;
        for (int i = s.firstUnit; i != -1 && n < 64; i = s.nextObject(i)) {
            const Unit& u = s.units[i];
            if (!u.selected || u.state == UnitState::Dead || !filt(u)) continue;
            issue(i);
            ++n;
        }
    }
};

// ===========================================================================================
// Conditions (EvaluateCondition, event-core.c / event-conditions.c)
// ===========================================================================================
bool ExtEventRunner::evaluateCondition(int ci) {
    std::uint8_t* c = x.conditions[static_cast<std::size_t>(ci)].data();
    const int type = c[cd::kType];
    std::array<int, 2> cx{}, cy{};
    for (int i = 0; i < 2; ++i) {
        cx[i] = varOrConst(c[cd::kCoordVarFlags], i * 2, c[cd::kCoordX + i]);
        cy[i] = varOrConst(c[cd::kCoordVarFlags], i * 2 + 1, c[cd::kCoordY + i]);
    }
    std::array<int, 7> a{};
    if (type < CT_CHECKUNITS || type > CT_CHECKTILES) {
        const int f = c[cd::kArgVarFlags];
        a[0] = varOrConst(f, 0, c[cd::kSide]);
        a[1] = varOrConst(f, 1, c[cd::kArg1]);
        a[2] = varOrConst(f, 2, c[cd::kArg2]);
        a[3] = varOrConst(f, 3, rd32(c + cd::kVal1));
        a[4] = varOrConst(f, 4, rd32(c + cd::kVal2));
        a[5] = varOrConst(f, 5, rd32(c + cd::kVal3));
        a[6] = varOrConst(f, 6, rd32(c + cd::kVal4));
    }
    const int side = a[0] & 7;
    Side& s = w.sides_[side];
    const auto ticks = static_cast<std::int64_t>(w.gameTicks_);
    float fval = 0;
    std::memcpy(&fval, c + cd::kVal4, 4);
    // Check Units / Buildings / Crates / Tiles: the filter is the condition record itself;
    // side byte = side (8 = any), arg1 bit 0 = exact count, arg2 = amount (at least 1).
    auto countCheck = [&](auto&& forEach) {
        const bool strict = c[cd::kArg1] & 1;
        const int amount = llimit(c[cd::kArg2], 1);
        int matched = 0;
        int result = -1;  // -1 = undecided
        forEach([&](bool m) {
            if (m) ++matched;
            if (!strict && matched == amount) { result = 1; return false; }
            if (strict && matched > amount) { result = 0; return false; }
            return true;
        });
        if (result >= 0) return result == 1;
        return strict && matched == amount;
    };
    const Filter f{c};
    switch (type) {
    case CT_BUILDINGEXISTS:
        for (int i = s.firstBuilding; i != -1; i = s.nextObject(i))
            if (s.buildings[i].type == a[1]) return true;
        return false;
    case CT_UNITEXISTS:
        for (int i = s.firstUnit; i != -1; i = s.nextObject(i))
            if (s.units[i].type == a[2]) return true;
        return false;
    case CT_INTERVAL: {
        if (a[1]) return false;  // inactive
        const int off = w.rulesExt().intervalsAreOffByOneTick ? 1 : 0;
        const std::int64_t t = ticks - rd32(c + cd::kVal4) - (a[3] + off);
        if (t < 0) return false;
        const std::int64_t period = a[4] + off;
        if (period == 0) return false;
        return t % period == 0 && (a[5] == 0 || t / period < a[5]);
    }
    case CT_TIMER: {
        if (a[1]) return false;
        const std::int64_t t = ticks - rd32(c + cd::kVal4);
        if (t < 0) return false;
        switch (a[2]) {
        case 0: return t > a[4];
        case 1: return t < a[4];
        case 2: return t == a[4];
        case 3: return a[4] != 0 && (t + a[5]) % a[4] == 0;
        }
        return false;
    }
    case CT_CASUALTIES: {
        const int lost = s.unitsLost;
        if (lost > a[5]) {
            const auto killed = static_cast<std::uint32_t>(s.unitsKilled);
            const double v = killed ? static_cast<double>(static_cast<std::uint32_t>(100 * lost) / killed) * 0.0099999998
                                    : static_cast<double>(lost);
            return v > fval;
        }
        return false;
    }
    case CT_BASEDESTROYED: return !w.buildingsExist_[side];
    case CT_UNITSDESTROYED: return !w.unitsExist_[side];
    case CT_REVEALED:
        if (a[5] && w.inBounds(cx[0], cy[0]) && w.cell(cx[0], cy[0]).shroud == 0) {
            wr32(c + cd::kVal3, a[5] - 1);
            return true;
        }
        return false;
    case CT_CREDITS: {
        const Side& cs = w.sides_[(a[2] ? a[0] : mySide()) & 7];
        switch (a[1]) {
        case 0: return cs.cash + cs.spice >= a[5];
        case 1: return cs.spice >= a[5];
        case 2: return cs.cash >= a[5];
        case 3: return cs.maxStorage >= a[5];
        }
        return false;
    }
    case CT_FLAG: return a[5] != 0;
    case CT_RANDOMCHANCE: {
        const auto fixed = static_cast<std::uint32_t>(a[6]);
        if (fixed && w.gameTicks_ < fixed) return false;
        if (fixed && w.gameTicks_ > fixed) return c[cd::kArg1] != 0;
        const int value = a[3] ? x.tickRandom % a[3] + 1 : 1;
        const bool r = value >= a[4] && value <= a[5];
        c[cd::kArg1] = r ? 1 : 0;
        return r;
    }
    case CT_RANDOMINTERVAL: {
        const int status = a[1], runCount = a[2];
        if (status == 1) return false;
        const int span = a[5] - a[4] + 1;
        if (status == 0) {
            c[cd::kSide] = static_cast<std::uint8_t>(runCount);
            wr32(c + cd::kVal4, a[3] + (span > 0 ? rnd() % span : 0) + a[4]);
            c[cd::kArg1] = 2;
        }
        if (rd32(c + cd::kVal4) == 0) {
            --c[cd::kSide];
            if (c[cd::kSide] == 0 && runCount) c[cd::kArg1] = 1;
            else wr32(c + cd::kVal4, (span > 0 ? rnd() % span : 0) + a[4]);
            return true;
        }
        wr32(c + cd::kVal4, rd32(c + cd::kVal4) - 1);
        return false;
    }
    case CT_DIPLOMACY: return w.diplomacy_[side][a[1] & 7] == a[2];
    case CT_DIFFICULTY: return w.difficulty == a[5];
    case CT_CHECKUNITS:
        return countCheck([&](auto&& visit) {
            for (int sd = 0; sd < kMaxSides; ++sd) {
                if (c[cd::kSide] != 8 && c[cd::kSide] != sd) continue;
                Side& ss = w.sides_[sd];
                for (int i = ss.firstUnit; i != -1; i = ss.nextObject(i))
                    if (!visit(unitMatches(f, ss.units[i], sd))) return;
            }
        });
    case CT_CHECKBUILDINGS:
        return countCheck([&](auto&& visit) {
            for (int sd = 0; sd < kMaxSides; ++sd) {
                if (c[cd::kSide] != 8 && c[cd::kSide] != sd) continue;
                Side& ss = w.sides_[sd];
                for (int i = ss.firstBuilding; i != -1; i = ss.nextObject(i))
                    if (!visit(buildingMatches(f, ss.buildings[i], sd))) return;
            }
        });
    case CT_CHECKCRATES:
        return countCheck([&](auto&& visit) {
            for (const Crate& cr : w.crates_) {
                if (!cr.active) continue;
                if (!visit(crateMatches(f, cr))) return;
            }
        });
    case CT_CHECKTILES:
        return countCheck([&](auto&& visit) {
            int x0, y0, x1, y1;
            boundsForPosFilter(f, x0, y0, x1, y1);
            for (int yy = y0; yy <= y1; ++yy)
                for (int xx = x0; xx <= x1; ++xx)
                    if (!visit(tileMatches(f, xx, yy))) return;
        });
    case CT_SPICE_IN_AREA: {
        int total = 0;
        for (int yy = cy[0]; yy < cy[0] + cy[1]; ++yy)
            for (int xx = cx[0]; xx < cx[0] + cx[1]; ++xx)
                if (w.inBounds(xx, yy)) total += w.cell(xx, yy).spice();
        return total >= a[5];
    }
    case CT_DAMAGE_IN_AREA: {
        int total = 0;
        for (int yy = cy[0]; yy < cy[0] + cy[1]; ++yy)
            for (int xx = cx[0]; xx < cx[0] + cx[1]; ++xx)
                if (w.inBounds(xx, yy) && (!a[1] || w.cell(xx, yy).terrain() == a[2])) total += w.cell(xx, yy).damage;
        return total >= a[5];
    }
    case CT_POWER: {
        const bool cmp = !a[3];
        const int out = static_cast<int>(s.powerOutput), drain = static_cast<int>(s.powerDemand);
        switch (a[1]) {
        case 0: return compareValue(static_cast<int>(s.powerPercent), a[5], cmp);
        case 1: return compareValue(out, a[5], cmp);
        case 2: return compareValue(drain, a[5], cmp);
        case 3: return compareValue(out - drain, a[5], cmp);
        case 4: return compareValue(drain - out, a[5], cmp);
        }
        return false;
    }
    case CT_BUILDING_UPGRADES: return compareValue(s.upgrades[a[1] & 0xff], a[5], !a[3]);
    case CT_STARPORT_STOCK: {
        int amount = 0;
        if (a[2]) {
            for (int t : s.starportTypes)
                if (t >= 0 && t < formats::kMaxUnitTypes) amount += s.starportStock[t];
        } else if (a[1] < formats::kMaxUnitTypes) {
            amount = s.starportStock[a[1]];
        }
        return compareValue(amount, a[5], !a[3]);
    }
    case CT_STARPORT_COST: return a[1] < formats::kMaxUnitTypes && compareValue(s.starportPrice[a[1]], a[5], !a[3]);
    case CT_STARPORT_PICK: {
        int amount = 0;
        if (a[2]) {
            for (int t : s.starportTypes)
                if (t >= 0 && t < formats::kMaxUnitTypes) amount += s.starportOrdered[t];
        } else if (a[1] < formats::kMaxUnitTypes) {
            amount = s.starportOrdered[a[1]];
        }
        return compareValue(amount, a[5], !a[3]);
    }
    case CT_STARPORT_DELIVERY: return s.starportFrigateBusy;
    case CT_BUILDING_ICON: {
        const int built = s.buildingQueue.type >= 0 ? T().buildings[s.buildingQueue.type].buildingGroup : -1;
        const bool groupMatch = built == a[4] || (a[2] && built != -1);
        const bool statusMatch = s.buildingQueue.progress == kBuildDone || !a[1];
        return groupMatch && statusMatch;
    }
    case CT_UNIT_ICON:
        for (const auto& q : s.unitQueue) {
            if (q.type < 0) continue;
            const auto& ut = T().units[q.type];
            const bool statusMatch = q.progress == kBuildDone || !a[1];
            if (!a[2]) {
                if (ut.unitGroup == a[4]) return statusMatch;
            } else if (!a[3]) {
                return statusMatch;
            } else {
                const int qi = a[3] <= 6 ? a[3] : a[3] + 3;
                if (ut.prereq1BuildingGroup == static_cast<std::uint8_t>(T().groupIds[qi])) return statusMatch;
            }
        }
        return false;
    case CT_UPGRADE_ICON: {
        const int built = s.upgradeQueue.type >= 0 ? T().buildings[s.upgradeQueue.type].buildingGroup : -1;
        return built == a[4] || (a[2] && built != -1);
    }
    case CT_SPICE_HARVESTED: return compareValue(s.spiceHarvested, a[5], !a[3]);
    case CT_UNITS_BUILT:
        return compareValue(a[2] ? s.unitsBuilt : (a[1] < formats::kMaxUnitTypes ? s.unitsBuiltPerType[a[1]] : 0), a[5], !a[3]);
    case CT_BUILDINGS_BUILT:
        return compareValue(a[2] ? s.buildingsBuilt : (a[1] < formats::kMaxBuildingTypes ? s.buildingsBuiltPerType[a[1]] : 0), a[5], !a[3]);
    case CT_UNITS_LOST:
        return compareValue(a[2] ? s.unitsLost : (a[1] < formats::kMaxUnitTypes ? s.unitsLostPerType[a[1]] : 0), a[5], !a[3]);
    case CT_BUILDINGS_LOST: return compareValue(s.buildingsLost, a[5], !a[3]);
    case CT_UNITS_KILLED:  // per type/enemy kill counts are not kept: totals only
        return a[3] ? compareValue(s.unitsKilled, a[5], !a[4]) : compareValue(0, a[5], !a[4]);
    case CT_BUILDINGS_KILLED:
        return a[3] ? compareValue(s.buildingsKilled, a[5], !a[4]) : compareValue(0, a[5], !a[4]);
    case CT_AI_PROPERTY: {
        const int off = a[3];
        const auto& ai = w.ai_[side];
        int v = 0;
        switch (a[1]) {
        case 0: v = off >= 0 && off < static_cast<int>(kAiSize) ? static_cast<std::int8_t>(ai.b(off)) : 0; break;
        case 1: v = off >= 0 && off + 2 <= static_cast<int>(kAiSize) ? ai.get<std::int16_t>(off) : 0; break;
        default: v = off >= 0 && off + 4 <= static_cast<int>(kAiSize) ? ai.get<std::int32_t>(off) : 0; break;
        }
        if (a[1] == 3) {
            float fv = 0, fc = 0;
            std::memcpy(&fv, &v, 4);
            std::memcpy(&fc, &a[5], 4);
            return !a[4] ? fv >= fc : fv == fc;
        }
        return compareValue(v, a[5], !a[4]);
    }
    case CT_VARIABLE_VALUE: return compareValue(var(a[1]), a[5], !a[4]);
    case CT_VARIABLE_CHANGED: {
        ExtVariable& v = x.vars[static_cast<std::size_t>(a[1] & 255)];
        const bool r = v.oldValue != v.value;
        v.oldValue = v.value;
        return r;
    }
    default:  // CT_SIDE_PROPERTY / CT_MEMORY_VALUE (raw memory) and unknown types: not modelled
        return false;
    }
}

// ===========================================================================================
// Event execution (ExecuteEventsInRange / ExecuteEvent, event-core.c)
// ===========================================================================================
void ExtEventRunner::executeRange(int first, int last, BlockType bt) {
    int i = first;
    while (i < last && i >= 0 && i < static_cast<int>(x.events.size())) {
        const std::uint8_t* e = x.events[static_cast<std::size_t>(i)].data();
        const int n = std::min<int>(e[ev::kNumConditions], 12);
        bool run;
        if (e[ev::kFlags] & EF_CONDITIONS_OR) {
            run = false;
            for (int k = 0; k < n; ++k)
                if (condResult[e[ev::kCondIndex + k]] != e[ev::kCondNeg + k]) { run = true; break; }
        } else {
            run = true;
            for (int k = 0; k < n; ++k)
                if (condResult[e[ev::kCondIndex + k]] == e[ev::kCondNeg + k]) { run = false; break; }
        }
        if (run) {
            executeEvent(i);
            if (x.exitCount) {
                if (bt != BlockType::Global) {
                    if (bt == BlockType::Block) --x.exitCount;
                    break;
                }
                x.exitCount = 0;  // (DebugFatal: Exit outside a block)
            }
            if (x.breakCount) {
                if (bt == BlockType::Global || bt == BlockType::Block) x.breakCount = 0;
                else break;
            }
            if (x.continueCount) {
                if (bt == BlockType::Global || bt == BlockType::Block) {
                    x.continueCount = 0;
                } else {
                    if (bt == BlockType::Loop) --x.continueCount;
                    break;
                }
            }
        }
        i = x.next[static_cast<std::size_t>(i)];
    }
}

void ExtEventRunner::executeEvent(int i) {
    std::uint8_t* e = x.events[static_cast<std::size_t>(i)].data();
    const int et = e[ev::kType];
    if (e[ev::kFlags] & EF_BLOCKED) return;
    if (e[ev::kFlags] & EF_AUTO_BLOCK) e[ev::kFlags] |= EF_BLOCKED;
    std::array<int, 4> cx{}, cy{};
    for (int k = 0; k < 4; ++k) {
        cx[k] = varOrConst(e[ev::kCoordVarFlags], k * 2, e[ev::kCoordX + k]);
        cy[k] = varOrConst(e[ev::kCoordVarFlags], k * 2 + 1, e[ev::kCoordY + k]);
    }
    // args[0] = amount (var flag 1), args[1] = side (var flag 0), args[2..4] = item/enum/bool,
    // args[5] = value.
    std::array<int, 6> a{};
    for (int k = 0; k < 5; ++k) {
        const int flag = k == 0 ? 1 : k == 1 ? 0 : k;
        a[k] = varOrConst(e[ev::kArgVarFlags], flag, e[ev::kArgs + k]);
    }
    a[5] = varOrConst(e[ev::kArgVarFlags], 5, rd32(e + ev::kValue));
    std::uint8_t* data = e + ev::kData;
    int skip = varOrConst(e[ev::kFlags], 4, e[ev::kFilterSkip]);
    int limit = varOrConst(e[ev::kFlags], 5, data[0]);
    const Filter f{data + 1};

    if (e[ev::kFlags] & EF_OBJECT_INDEX) {  // object index in a variable instead of the filter
        if (et >= ET_CHANGE_TILE && et <= ET_SHOW_TILE_DATA) {
            cx[0] = var(e[ev::kFilterSkip]);
            cy[0] = var(e[ev::kFilterSkip] + 1);
            executeAction(i, et, cx, cy, a, data, 0);
        } else {
            if (et == ET_SELECT_UNIT) selectionBackup(a[1], true, false);
            if (et == ET_SELECT_BUILDING) selectionBackup(a[1], false, true);
            executeAction(i, et, cx, cy, a, data, var(e[ev::kFilterSkip]));
        }
        return;
    }
    auto loopSides = [&](auto&& perSide) {
        int affected = 0;
        int argSide = a[1];
        for (int sd = 0; sd < kMaxSides; ++sd) {
            if (argSide != 8 && argSide != sd) continue;
            a[1] = sd;
            if (!perSide(sd, affected, argSide)) return;
        }
    };
    const bool unitEvent = (et >= ET_DESTROY_UNIT && et <= ET_SHOW_UNIT_DATA) || et == ET_ORDER_REPAIR_SINGLE_UNIT ||
                           et == ET_ORDER_UNIT_ATTACK_UNIT || et == ET_ORDER_UNIT_DEPLOY ||
                           et == ET_ORDER_BUILDING_ATTACK_UNIT || et == ET_GET_UNIT_COUNT || et == ET_LOOP_UNITS;
    if (unitEvent) {
        if (et == ET_ORDER_UNIT_ATTACK_UNIT || et == ET_ORDER_BUILDING_ATTACK_UNIT) limit = 1;
        if (et == ET_GET_UNIT_COUNT) setVar(a[0], 0);
        loopSides([&](int sd, int& affected, int& argSide) {
            Side& s = w.sides_[sd];
            if (et == ET_SELECT_UNIT) selectionBackup(sd, true, false);
            for (int u = s.firstUnit; u != -1;) {
                const int nextU = s.nextObject(u);
                if (unitMatches(f, s.units[u], sd)) {
                    if (skip) {
                        --skip;
                    } else {
                        executeAction(i, et, cx, cy, a, data, u);
                        if (x.breakCount) { --x.breakCount; argSide = -1; return false; }
                        ++affected;
                    }
                }
                if (limit && affected == limit) return false;
                u = nextU;
            }
            return true;
        });
        return;
    }
    const bool buildingEvent = (et >= ET_DESTROY_BUILDING && et <= ET_SHOW_BUILDING_DATA) ||
                               et == ET_ORDER_DOCK_WITH_REFINERY || et == ET_ORDER_REPAIR_SELECTED_UNITS ||
                               et == ET_ORDER_UNIT_ATTACK_BUILDING || et == ET_ORDER_BUILDING_ATTACK_BUILDING ||
                               et == ET_ORDER_BUILDING_REPAIR || et == ET_ORDER_BUILDING_SELL ||
                               et == ET_GET_BUILDING_COUNT || et == ET_LOOP_BUILDINGS;
    if (buildingEvent) {
        if (et == ET_ORDER_DOCK_WITH_REFINERY || et == ET_ORDER_REPAIR_SELECTED_UNITS ||
            et == ET_ORDER_UNIT_ATTACK_BUILDING || et == ET_ORDER_BUILDING_ATTACK_BUILDING)
            limit = 1;
        if (et == ET_GET_BUILDING_COUNT) setVar(a[0], 0);
        loopSides([&](int sd, int& affected, int& argSide) {
            Side& s = w.sides_[sd];
            if (et == ET_SELECT_BUILDING) selectionBackup(sd, false, true);
            for (int b = s.firstBuilding; b != -1;) {
                const int nextB = s.nextObject(b);
                if (buildingMatches(f, s.buildings[b], sd)) {
                    if (skip) {
                        --skip;
                    } else {
                        executeAction(i, et, cx, cy, a, data, b);
                        if (x.breakCount) { --x.breakCount; argSide = -1; return false; }
                        ++affected;
                    }
                }
                if (limit && affected == limit) return false;
                b = nextB;
            }
            return true;
        });
        return;
    }
    if (et == ET_SET_BULLET_PROPERTY || et == ET_GET_BULLET_COUNT || et == ET_LOOP_BULLETS ||
        et == ET_SET_EXPLOSION_PROPERTY || et == ET_GET_EXPLOSION_COUNT || et == ET_LOOP_EXPLOSIONS) {
        const bool bullets = et == ET_SET_BULLET_PROPERTY || et == ET_GET_BULLET_COUNT || et == ET_LOOP_BULLETS;
        if (et == ET_GET_BULLET_COUNT || et == ET_GET_EXPLOSION_COUNT) setVar(a[0], 0);
        loopSides([&](int sd, int& affected, int& argSide) {
            Side& s = w.sides_[sd];
            for (int o = bullets ? s.firstBullet : s.firstExplosion; o != -1;) {
                const int nextO = s.nextObject(o);
                if (bullets ? bulletMatches(f, s.bullets[o]) : explosionMatches(f, s.explosions[o])) {
                    if (skip) {
                        --skip;
                    } else {
                        executeAction(i, et, cx, cy, a, data, o);
                        if (x.breakCount) { --x.breakCount; argSide = -1; return false; }
                        ++affected;
                    }
                }
                if (limit && affected == limit) return false;
                o = nextO;
            }
            return true;
        });
        return;
    }
    if ((et >= ET_REMOVE_CRATE && et <= ET_SHOW_CRATE_DATA) || et == ET_GET_CRATE_COUNT || et == ET_LOOP_CRATES) {
        if (et == ET_GET_CRATE_COUNT) setVar(a[0], 0);
        int affected = 0;
        for (int k = 0; k < kMaxCrates; ++k) {
            if (crateMatches(f, w.crates_[static_cast<std::size_t>(k)])) {
                if (skip) {
                    --skip;
                } else {
                    executeAction(i, et, cx, cy, a, data, k);
                    if (x.breakCount) { --x.breakCount; break; }
                    ++affected;
                }
            }
            if (limit && affected == limit) return;
        }
        return;
    }
    if ((et >= ET_CHANGE_TILE && et <= ET_SHOW_TILE_DATA) || et == ET_GET_TILE_COUNT || et == ET_GET_SPICE_COUNT ||
        et == ET_GET_DAMAGE_COUNT || et == ET_LOOP_TILES) {
        if (et == ET_GET_TILE_COUNT || et == ET_GET_SPICE_COUNT || et == ET_GET_DAMAGE_COUNT) setVar(a[0], 0);
        int affected = 0;
        int x0, y0, x1, y1;
        boundsForPosFilter(f, x0, y0, x1, y1);
        for (int yy = y0; yy <= y1; ++yy)
            for (int xx = x0; xx <= x1; ++xx) {
                if (tileMatches(f, xx, yy)) {
                    if (skip) {
                        --skip;
                    } else {
                        cx[0] = xx;
                        cy[0] = yy;
                        executeAction(i, et, cx, cy, a, data, 0);
                        if (x.breakCount) { --x.breakCount; break; }
                        ++affected;
                    }
                }
                if (limit && affected == limit) return;
            }
        return;
    }
    if (et == ET_GET_SIDE_COUNT || et == ET_LOOP_SIDES) {
        if (et == ET_GET_SIDE_COUNT) setVar(a[0], 0);
        int affected = 0;
        for (int sd = 0; sd < kMaxSides; ++sd) {
            if (sideMatches(f, sd)) {
                if (skip) {
                    --skip;
                } else {
                    executeAction(i, et, cx, cy, a, data, sd);
                    if (x.breakCount) { --x.breakCount; break; }
                    ++affected;
                }
            }
            if (limit && affected == limit) return;
        }
        return;
    }
    executeAction(i, et, cx, cy, a, data, 0);
}

// ===========================================================================================
// Actions (ExecuteEventAction / EvAct_*, event-actions.c)
// ===========================================================================================
void ExtEventRunner::selectionBackup(int side, bool units, bool buildings) {
    if (side < 0 || side >= kMaxSides) return;
    Side& s = w.sides_[side];
    if (units)
        for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) {
            s.units[i].prevSelected = s.units[i].selected;
            s.units[i].selected = false;
        }
    if (buildings)
        for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
            s.buildings[i].prevSelected = s.buildings[i].selected;
            s.buildings[i].selected = false;
        }
}

void ExtEventRunner::restoreUnitSelection(int side, bool restore) {
    if (!restore || side < 0 || side >= kMaxSides) return;
    Side& s = w.sides_[side];
    for (int i = s.firstUnit; i != -1; i = s.nextObject(i)) {
        s.units[i].selected = s.units[i].prevSelected;
        s.units[i].prevSelected = false;
    }
}

void ExtEventRunner::restoreBuildingSelection(int side, bool restore) {
    if (!restore || side < 0 || side >= kMaxSides) return;
    Side& s = w.sides_[side];
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
        s.buildings[i].selected = s.buildings[i].prevSelected;
        s.buildings[i].prevSelected = false;
    }
}

void ExtEventRunner::addDelivery(int ex, int ey, int side, int amount, int tag, int deploy, int delay, int type,
                                 const std::uint8_t* list) {
    Side& s = w.sides_[side & 7];
    if (type == 2 && s.primaryBuilding[7] == kNoIndex) return;
    int k = 0;
    while (k < 10 && s.deliveries[k].active) ++k;
    if (k >= 10) return;  // DebugFatal "Too many deliveries"
    Delivery& d = s.deliveries[k];
    d.active = true;
    d.started = false;
    d.x = ex & 0xff;
    d.y = ey & 0xff;
    d.time = w.gameTicks_ + static_cast<std::uint32_t>(delay);
    d.deployAction = deploy;
    d.type = type;
    d.tag = static_cast<std::uint8_t>(tag);
    d.entryEdge = -1;  // (not original field)
    amount = std::clamp(amount, 0, 39);
    for (int n = 0; n < amount; ++n) d.units[static_cast<std::size_t>(n)] = list[n];
    d.units[static_cast<std::size_t>(amount)] = 0xff;
    if (type == 2) s.starportFrigateBusy = true;
}

void ExtEventRunner::setCash(int side, int op, int value) {
    Side& s = w.sides_[side & 7];
    const int actual = s.spice + s.spiceDrip + s.cash + s.cashDrip;
    const int target = valueOperation(actual, value, op);
    int cost = hlimit(actual - target, actual);
    if (cost <= 0) {
        s.cashDrip -= cost;
    } else if (s.spiceDrip + s.spice <= cost) {
        s.cashDrip -= cost - (s.spice + s.spiceDrip);
        s.spiceDrip = -s.spice;
    } else {
        s.spiceDrip -= cost;
    }
}

void ExtEventRunner::unitSpawn(int ex, int ey, int side, int amount, int facing, int tag, const std::uint8_t* list) {
    for (int n = 0; n < amount && n < 25; ++n) {
        int px = ex & 0xff, py = ey & 0xff;
        if (T().units[list[n]].specialBehavior != behaviour::kSandworm) w.nearestFreeTile(px, py, 12);
        const int idx = w.addUnit(side, list[n], px, py, px, py, 0, 0);
        Unit* u = w.unit(side, idx);
        if (!u) continue;
        u->facing = u->facingTurret = u->facingTurretTarget = (facing << 2) & 0xff;
        u->tag = static_cast<std::uint8_t>(tag);
    }
}

void ExtEventRunner::randomSpread(int& px, int& py, int sx, int sy, bool circle) {
    if (circle) {
        const int angle = rnd() % 32;
        if (sx) px += sinTable(angle, rnd() % sx) / 2048;
        if (sy) py += cosTable(angle, rnd() % sy) / 2048;
    } else {
        if (sx) px += rnd() % (sx * 2 + 1) - sx;
        if (sy) py += rnd() % (sy * 2 + 1) - sy;
    }
}

void ExtEventRunner::addUnit(int ex, int ey, int side, int props, int type, int movement, int facing, int tag,
                             int targetVar) {
    if (!w.inBounds(ex, ey) || type < 0 || type >= formats::kMaxUnitTypes) {
        setVar(targetVar, -1);
        return;
    }
    Cell& c = w.cell(ex, ey);
    const auto& t = T().units[type];
    if (t.specialBehavior != behaviour::kSandworm && (c.flags & kVehicle)) {
        setVar(targetVar, -1);
        return;
    }
    if (t.isInfantry && (c.flags & kInfantryMask) == kInfantryMask) {
        setVar(targetVar, -1);
        return;
    }
    const bool occBuilding = (c.flags & kBuilding) != 0;
    const int origOwner = static_cast<int>(c.flags & kOwnerMask);
    if (occBuilding && origOwner != side) c.flags = (c.flags & ~kOwnerMask) | static_cast<std::uint32_t>(side & 7);
    int tx = ex, ty = ey;
    switch (movement) {
    case 1: w.getRandomAdjacentTileExt(tx, ty); break;
    case 2: --tx; --ty; break;
    case 3: --ty; break;
    case 4: ++tx; --ty; break;
    case 5: ++tx; break;
    case 6: ++tx; ++ty; break;
    case 7: ++ty; break;
    case 8: --tx; ++ty; break;
    case 9: --tx; break;
    }
    const int idx = w.addUnit(side, type, ex, ey, tx & 0xff, ty & 0xff, 0, 0);
    if (Unit* u = w.unit(side, idx)) {
        if (!movement) u->facing = u->facingTurret = u->facingTurretTarget = (facing << 2) & 0xff;
        if (props & 1) u->flags |= kStealth;
        u->tag = static_cast<std::uint8_t>(tag);
    }
    if (occBuilding && origOwner != side)
        w.cell(ex, ey).flags = (w.cell(ex, ey).flags & ~kOwnerMask) | static_cast<std::uint32_t>(origOwner);
    setVar(targetVar, idx);
}

void ExtEventRunner::addBuilding(int ex, int ey, int side, int props, int type, int method, int facing, int tag,
                                 int targetVar) {
    if (type < 0 || type >= formats::kMaxBuildingTypes) {
        setVar(targetVar, -1);
        return;
    }
    bool initial = false, captured = false, concrete = false;
    switch (method) {
    case 1: concrete = true; break;
    case 2: initial = true; break;
    case 3: captured = true; break;
    case 4: captured = concrete = true; break;
    case 5: initial = captured = true; break;
    }
    if (concrete) {
        const int ct = w.sideVersionOfBuilding(w.sides_[side & 7], static_cast<std::uint8_t>(T().groupIds[13]), false);
        if (ct >= 0) w.addConcrete(side, ct, ex, ey, T().buildings[type].tilesOccupiedAll);
    }
    // ModelAddBuilding(side, type, x, y, initialSetup, noFreeHarvester, noConyardAnim).
    const int idx = w.addBuilding(side, type, ex, ey, initial, captured, captured);
    if (Building* b = w.building(side, idx)) {
        if (T().buildings[type].specialBehavior == building_behaviour::kTurret) b->facing = (facing << 2) & 0xff;
        if (props & 1) w.setBuildingAsPrimary(side, idx);
        b->tag = static_cast<std::uint8_t>(tag);
    }
    setVar(targetVar, idx);
}

void ExtEventRunner::spiceBloom(int ex, int ey, int range, int mode, bool randomizer) {
    if (mode == 0) {  // classic: SpiceMound
        w.spiceMound(ex, ey, range);
        return;
    }
    const bool circle = mode == 2 || mode == 3;
    if (circle) range = hlimit(range, 7);
    range = std::max(range, 0);
    const int n = range * 2 + 1;
    auto inside = [&](int col, int row) { return !circle || w.circleMask(range, row, col) == 0; };
    for (int yy0 = 0; yy0 < n; ++yy0) {
        const int yy = ey + yy0 - range;
        if (yy < 0 || yy >= w.height_) continue;
        for (int xx0 = 0; xx0 < n; ++xx0) {
            const int xx = ex + xx0 - range;
            if (xx < 0 || xx >= w.width_) continue;
            if (!inside(xx0, yy0)) continue;
            Cell& c = w.cell(xx, yy);
            if ((c.flags & (kSand | kBuilding | kConcrete)) != kSand) continue;
            if (randomizer && !(xx == ex && yy == ey) && (rnd() % 100) < 40) continue;
            int amount = c.spice();
            const int lim = mode == 3 ? 4 : 2;
            amount = amount < lim ? hlimit(amount + 2, lim) : amount;
            c.flags = (c.flags & ~kSpiceMask) | (static_cast<std::uint32_t>(amount) << kSpiceShift);
        }
    }
    if (mode == 3) {
        auto noSpice = [&](int xx, int yy) {
            xx = std::clamp(xx, 0, w.width_ - 1);
            yy = std::clamp(yy, 0, w.height_ - 1);
            return (w.cell(xx, yy).flags & kSpiceMask) == 0;
        };
        for (int yy0 = 0; yy0 < n; ++yy0) {
            const int yy = ey + yy0 - range;
            if (yy < 0 || yy >= w.height_) continue;
            for (int xx0 = 0; xx0 < n; ++xx0) {
                const int xx = ex + xx0 - range;
                if (xx < 0 || xx >= w.width_ || !inside(xx0, yy0)) continue;
                Cell& c = w.cell(xx, yy);
                if ((c.flags & (kSand | kBuilding | kConcrete)) != kSand) continue;
                if (c.spice() > 2 && (noSpice(xx - 1, yy - 1) || noSpice(xx, yy - 1) || noSpice(xx + 1, yy - 1) ||
                                      noSpice(xx - 1, yy) || noSpice(xx + 1, yy) || noSpice(xx - 1, yy + 1) ||
                                      noSpice(xx, yy + 1) || noSpice(xx + 1, yy + 1)))
                    c.flags = (c.flags & ~kSpiceMask) | 0x200000u;
            }
        }
        w.screenShakes_ = 10;
        w.playNamed("S_SPICEMOUND", 0, ex, ey);
    }
    w.updateSpiceInRegion(std::max(ex - range - 1, 0), std::max(ey - range - 1, 0),
                          std::min(ex + range + 2, w.width_), std::min(ey + range + 2, w.height_));
}

// ChangeMapTile (event-actions.c).
void ExtEventRunner::changeMapTile(int ex, int ey, int tile, int mode) {
    if (tile == 65535 || !w.inBounds(ex, ey)) return;
    Cell& c = w.cell(ex, ey);
    std::uint16_t& backup = x.backupTile[static_cast<std::size_t>(ey) * w.width_ + ex];
    if (mode == 1) { c.tile = static_cast<std::uint16_t>(tile); return; }
    if (mode == 2) { c.tile = backup; return; }
    const std::uint16_t oldTile = backup;
    const std::uint32_t nf = tileAtrFlags(tile);
    const bool noDrive = !(nf & kDriveOn), noWalk = !(nf & kWalkOn), noBuild = !(nf & kBuildOn);
    const bool occUnit = (nf & kVehicle) != 0, occBuilding = (nf & kBuilding) != 0, occInf = (nf & kInfantryMask) != 0;
    const bool destroyBuildings = noBuild || occBuilding || occUnit || occInf;
    const bool killInf = noWalk || occBuilding || occUnit || occInf;
    const bool killLight = noDrive || occBuilding || occUnit || occInf;
    const bool killHeavy = noDrive || occBuilding || occUnit;
    int side = 0, index = 0;
    bool realBuilding = false, realUnit = false;
    if (c.flags & kBuilding) {
        if (const Building* cb = w.buildingAt(ex, ey, side, index)) {
            Building& b = w.sides_[side].buildings[index];
            (void)cb;
            if (destroyBuildings && b.state != 17 && !(T().buildings[b.type].flags & building_flags::kNoConcrete))
                w.destroyBuilding(b, false);
            else
                realBuilding = true;
        }
    }
    if ((c.flags & kAnyUnit) && w.getUnitOnTile(32 * ex, 32 * ey, side, index, false)) {
        index = -1;
        while (Unit* u = w.getNextUnitOnTile(ex, ey, side, index)) {
            const auto& ut = T().units[u->type];
            const bool inf = ut.isInfantry != 0, heavy = ut.canCrushInfantry != 0;
            if ((inf && killInf) || (!inf && !heavy && killLight) || (!inf && heavy && killHeavy))
                w.destroyUnit(*u);
            else
                realUnit = true;
        }
    }
    if (noBuild && (c.flags & kConcrete)) {
        c.tile = backup;
        c.flags &= ~kConcrete;
    }
    if (!(nf & kSand) && (c.flags & kSpiceMask)) {
        c.tile = backup;
        c.flags &= ~kSpiceMask;
    }
    if (c.tile == backup) c.tile = static_cast<std::uint16_t>(tile);
    backup = static_cast<std::uint16_t>(tile);
    c.flags &= ~tileAtrFlags(oldTile);
    c.flags |= nf;
    if (realBuilding || realUnit) c.flags = (c.flags & ~kOwnerMask) | static_cast<std::uint32_t>(side & 7);
    c.damage = 0;
}

void ExtEventRunner::transferCredits(int side, int op, int value) {
    Side& s = w.sides_[side & 7];
    int rem = 0;
    switch (op) {
    case 0: rem = s.spiceDrip + s.spice; break;
    case 1: rem = s.maxStorage - (s.spiceDrip + s.spice); break;
    case 2: rem = s.cashDrip + s.cash; break;
    case 3: rem = value; break;
    case 4: rem = hlimit(s.maxStorage - (s.spiceDrip + s.spice), value); break;
    case 5: rem = value; break;
    }
    if (op == 0 || op == 3) {
        const int dripPart = hlimit(s.spiceDrip, rem);
        rem -= dripPart;
        s.cashDrip += dripPart;
        s.spiceDrip -= dripPart;
        const int realPart = hlimit(s.spice, rem);
        s.cash += realPart;
        s.spice -= realPart;
    } else if (op >= 1 && op <= 5) {
        const int dripPart = std::clamp(s.cashDrip, 0, std::max(rem, 0));
        rem -= dripPart;
        s.spiceDrip += dripPart;
        s.cashDrip -= dripPart;
        const int realPart = hlimit(s.cash, rem);
        s.spice += realPart;
        s.spice = hlimit(s.spice, s.maxStorage);
        s.cash -= realPart;
    }
}

void ExtEventRunner::setBuildingUpgrades(int side, int group, int op, int value) {
    Side& s = w.sides_[side & 7];
    group &= 0xff;
    if (group >= formats::kMaxBuildingTypes) return;
    const int oldUp = s.upgrades[static_cast<std::size_t>(group)];
    const int newUp = llimit(valueOperation(oldUp, value, op), 0);
    s.upgrades[static_cast<std::size_t>(group)] = static_cast<std::uint8_t>(newUp);
    w.updateIcons(side & 7);
    if (oldUp < newUp && s.upgradeQueue.type >= 0 &&
        s.upgradeQueue.type == w.sideVersionOfBuilding(s, group, false)) {
        s.upgradeQueue.onHold = true;
        w.orders_.push_back({World::Order::UpgradeCancel, side & 7, kNoIndex, s.upgradeQueue.type, 0});
    }
}

void ExtEventRunner::airliftUnit(int side, int tx, int ty, bool unitsTarget, int index) {
    Side& s = w.sides_[side & 7];
    Unit* u = w.unit(side, index);
    if (!u || u->state == UnitState::AwaitingAirlift) return;
    if (unitsTarget && u->targetX == u->blockFromX && u->targetY == u->blockFromY) return;
    if (!unitsTarget && tx == u->blockFromX && ty == u->blockFromY) return;
    const int pos = w.pickupQueuePos(s, *u);
    if (pos == -1) return;
    if (!unitsTarget) {
        u->targetX = tx;
        u->targetY = ty;
    }
    if (u->flags & kBlockToMarked) {
        if (w.inBounds(u->blockToX, u->blockToY)) {
            std::uint32_t& cf = w.cell(u->blockToX, u->blockToY).flags;
            cf &= ~kVehicle;
            if (cf & kInfantryMask) cf = (cf & ~kOwnerMask) | ((cf >> kInfantryOwnerShift) & 7);
        }
        u->flags &= ~kBlockToMarked;
        u->blockToX = u->blockFromX;
        u->blockToY = u->blockFromY;
        u->posX = (u->blockFromX << 21) + (1 << 20);
        u->posY = (u->blockFromY << 21) + (1 << 20);
        u->posStepsMax = 0;
        u->posSteps = 0;
    }
    if (w.addToPickupQueue(s, *u, pos, 100)) w.unitAdjustState(*u, UnitState::AwaitingAirlift);
}

void ExtEventRunner::hideTile(int ex, int ey) {
    for (int yy = std::max(ey - 1, 0); yy <= std::min(ey + 1, w.height_ - 1); ++yy)
        for (int xx = std::max(ex - 1, 0); xx <= std::min(ex + 1, w.width_ - 1); ++xx) w.cell(xx, yy).shroud = 1;
    w.updateShroudInRegion(ex - 2, ey - 2, ex + 3, ey + 3);
}

void ExtEventRunner::hideMap() {
    for (auto& c : w.cells_) c.shroud = 1;
    w.revealTilesSeenByBuildingsAndUnits(w.mySide_);
}

void ExtEventRunner::switchMySide(int side, int aiSwitch, bool revealBase) {
    if (w.playAs()) return;  // play as another side: the human keeps the chosen side (docs/play-as.md)
    if (aiSwitch & 2) w.ai_[w.mySide_].b(1) = 1;
    w.mySide_ = side & 7;
    if (revealBase) w.revealTilesSeenByBuildingsAndUnits(w.mySide_);
    if (aiSwitch & 1) w.ai_[w.mySide_].b(1) = 0;
}

void ExtEventRunner::executeAction(int ei, int type, std::array<int, 4>& cx, std::array<int, 4>& cy,
                                   std::array<int, 6>& a, std::uint8_t* data, int obj) {
    const int side = a[1] & 7;  // (side 8 = any is resolved by the object loops)
    Side& s = w.sides_[side];
    const int amount = a[0], item = a[2], en = a[3], bo = a[4], val1 = a[5];
    const int val2 = rd32(data + 1);
    const auto& TT = T();
    switch (type) {
    // ---- vanilla / general
    case ET_REINFORCEMENT: addDelivery(cx[0], cy[0], side, amount, item, bo, val1, 1, data); break;
    case ET_STARPORT_DELIVERY: addDelivery(cx[0], cy[0], side, amount, item, bo, val1, 2, data); break;
    case ET_ALLEGIANCE:
        w.diplomacy_[side][item & 7] = static_cast<std::uint8_t>(en);
        if (en == 0 || en == 2) w.resetEnemyForSide(side, item);
        if (bo) {
            w.diplomacy_[item & 7][side] = static_cast<std::uint8_t>(en);
            if (en == 0 || en == 2) w.resetEnemyForSide(item, side);
        }
        break;
    case ET_LEAVE: {  // CSide__BlowupAll_surrender 0x46e6b0
        for (int i = s.firstBuilding; i != -1;) {
            const int nx = s.nextObject(i);
            if (s.buildings[i].state != 17) w.destroyBuilding(s.buildings[i], true);
            i = nx;
        }
        for (int i = s.firstUnit; i != -1;) {
            const int nx = s.nextObject(i);
            Unit& u = s.units[i];
            if (u.state != UnitState::Dead && TT.units[u.type].specialBehavior != behaviour::kSandworm) w.destroyUnit(u);
            i = nx;
        }
        s.participates = false;
        break;
    }
    case ET_BESERK: w.ai_[side].b(0x1E) = 1; break;
    case ET_PLAYSOUND:
        if (bo) {
            w.playSound(val1, cx[0], cy[0]);
        } else {
            SoundEvent e{val1};
            e.queue = true;
            e.priority = en ? 1 : 0;
            w.sounds_.push_back(e);
        }
        break;
    case ET_SETBUILDRATE: w.ai_[side].set<std::int32_t>(0x1D2C, valueOperation(w.ai_[side].get<std::int32_t>(0x1D2C), val1, en)); break;
    case ET_SETATTACKBUILDINGRATE: w.ai_[side].set<std::int32_t>(0xF14, valueOperation(w.ai_[side].get<std::int32_t>(0xF14), val1, en)); break;
    case ET_SETCASH: setCash(side, en, val1); break;
    case ET_SETTECH:
        s.techLevel = valueOperation(s.techLevel, val1, en);
        if (bo) w.updateIcons(side);
        break;
    case ET_WIN: w.missionOutcome(true); break;    // (if !gLose: gWin = 1; remapped when played as another side)
    case ET_LOSE: w.missionOutcome(false); break;
    case ET_SWITCH_MY_SIDE: switchMySide(side, en, bo != 0); break;
    case ET_HIDE_MAP: hideMap(); break;
    case ET_REVEAL:
        if (amount == 0) w.revealMap();
        else w.revealCircle(cx[0], cy[0], amount);
        break;
    case ET_SETTIMER: w.timerValue_ = valueOperation(w.timerValue_, val1, en); break;
    case ET_HIDETIMER: w.timerValue_ = -1; break;
    case ET_SHOWMESSAGE: {
        // sound mode: 0 S_CHATMSG, 1 none, 2 custom sample, 3 custom sample (forced)
        const int sample = rd32(data + 1);
        if (en == 0) w.speech("S_CHATMSG", true);
        else if (en == 2 || en == 3) {
            SoundEvent e{sample};
            e.queue = true;
            e.priority = en == 3 ? 1 : 0;
            w.sounds_.push_back(e);
        }
        w.messages_.push_back({rd32(data + 21), w.gameTicks_});
        break;
    }
    case ET_UNIT_SPAWN: unitSpawn(cx[0], cy[0], side, amount, en, val1, data); break;
    case ET_SET_FLAG:
        if (side < static_cast<int>(x.conditions.size()) || a[1] < static_cast<int>(x.conditions.size()))
            wr32(x.conditions[static_cast<std::size_t>(a[1] & 0xff) % x.conditions.size()].data() + cd::kVal3, val1);
        break;
    case ET_UN_BLOCK_EVENT:
        if (val1 >= 0 && val1 < static_cast<int>(x.events.size())) {
            std::uint8_t& fl = x.events[static_cast<std::size_t>(val1)][ev::kFlags];
            if (bo) fl |= EF_BLOCKED;
            else fl &= static_cast<std::uint8_t>(~EF_BLOCKED);
        }
        break;
    case ET_PLAY_MUSIC: {
        char name[26] = {};
        std::memcpy(name, data, 25);
        w.extUi_.music = name;
        break;
    }
    case ET_DAMAGE_TILES: {
        int px = cx[0] * 32 + cx[2], py = cy[0] * 32 + cy[2];
        randomSpread(px, py, cx[3], cy[3], en != 0);
        if (px < 0 || px >= w.width_ * 32 || py < 0 || py >= w.height_ * 32 || item < 0 || item >= formats::kMaxWeapons) break;
        w.damageTiles(px, py, 0, item, side, kNoIndex, false);
        if (bo && TT.weapons[item].hitExplosion != -1) {
            const int he = TT.weapons[item].hitExplosion;
            w.addExplosion(side, he, px, py, 0);
            w.playSound(w.explosionTemplate(he & 0xff).sound, cx[0], cy[0]);
        }
        break;
    }
    case ET_ADD_UNIT: addUnit(cx[0], cy[0], side, amount, item, en, bo, val1, val2); break;
    case ET_ADD_BUILDING: addBuilding(cx[0], cy[0], side, amount, item, en, bo, val1, val2); break;
    case ET_ADD_BULLET: {
        if (item < 0 || item >= formats::kMaxWeapons) break;
        int px = cx[1] * 32 + cx[2], py = cy[1] * 32 + cy[2];
        randomSpread(px, py, cx[3], cy[3], en != 0);
        const int idx = w.addBullet(side, item, 0, kNoIndex, cx[0] * 32 + 16, cy[0] * 32 + 16, px, py, kNoIndex, -1);
        setVar(val2, idx);
        if (idx >= 0) s.bullets[static_cast<std::size_t>(idx)].tag = static_cast<std::uint8_t>(val1);
        if (bo) w.playSound(TT.weapons[item].firingSound, cx[0], cy[0]);
        break;
    }
    case ET_ADD_EXPLOSION: {
        int px = cx[0] * 32 + cx[2], py = cy[0] * 32 + cy[2];
        randomSpread(px, py, cx[3], cy[3], en != 0);
        const int idx = w.addExplosion(side, item, px, py, 0);
        setVar(val2, idx);
        if (idx >= 0) s.explosions[static_cast<std::size_t>(idx)].tag = static_cast<std::uint8_t>(val1);
        if (bo && item >= 0 && static_cast<std::size_t>(item) < w.explosionCount())
            w.playSound(w.explosionTemplate(item).sound, cx[0], cy[0]);
        break;
    }
    case ET_ADD_CRATE: {
        // EvAct_AddCrate(x, y, type = side byte, image = amount, ext data = item, respawns = enum,
        // expiration = value). The extended crate types / ext data are not modelled; the
        // record is written as the patch does.
        if (!w.inBounds(cx[0], cy[0]) || (w.cell(cx[0], cy[0]).flags & kCrate)) break;
        const int k = w.freeCrateIndex();
        if (k < 0) break;
        Crate& cr = w.crates_[static_cast<std::size_t>(k)];
        cr.active = true;
        cr.x = static_cast<std::uint8_t>(cx[0]);
        cr.y = static_cast<std::uint8_t>(cy[0]);
        cr.type = static_cast<std::uint8_t>(a[1]);
        cr.image = static_cast<std::uint8_t>(amount);
        cr.timer = val1 ? static_cast<std::uint32_t>(val1) : static_cast<std::uint32_t>(INT32_MAX);
        cr.respawns = static_cast<std::uint8_t>(en);
        w.cell(cx[0], cy[0]).flags |= kCrate;
        break;
    }
    case ET_ADD_CONCRETE: {
        const int ct = w.sideVersionOfBuilding(s, static_cast<std::uint8_t>(TT.groupIds[13]), false);
        if (val1) {
            if (ct >= 0) w.addConcrete(side, ct, cx[0], cy[0], static_cast<std::uint32_t>(val1));
        } else {
            for (int yy = cy[0]; yy <= cy[1]; ++yy)
                for (int xx = cx[0]; xx <= cx[1]; ++xx) {
                    if (!w.inBounds(xx, yy)) continue;
                    Cell& c = w.cell(xx, yy);
                    if ((c.flags & kBuildOn) && !(c.flags & kConcrete)) {
                        c.flags &= ~kConcreteOwnerMask;
                        c.flags |= kConcrete | (static_cast<std::uint32_t>(side) << kConcreteOwnerShift);
                        c.tile = static_cast<std::uint16_t>(651 + 20 * (rnd() % 3));
                    }
                }
        }
        break;
    }
    case ET_SPICE_BLOOM: spiceBloom(cx[0], cy[0], amount, en, bo != 0); break;
    case ET_SHAKE_SCREEN: w.screenShakes_ = val1; break;
    case ET_CHANGE_VIEWPORT: {
        int px = cx[0], py = cy[0];
        if (!bo) {
            px = px * 32 + (en ? 0 : 16);
            py = py * 32 + (en ? 0 : 16);
        }
        w.extUi_.viewport = true;
        w.extUi_.viewportX = px;
        w.extUi_.viewportY = py;
        w.extUi_.viewportCentred = en == 0;
        break;
    }
    case ET_CHANGE_MAP_BLOCK: {
        const int width = cx[1], height = cy[1];
        if (width * height > 12) break;  // DebugFatal
        for (int yy = 0; yy < height; ++yy)
            for (int xx = 0; xx < width; ++xx) changeMapTile(cx[0] + xx, cy[0] + yy, rd16(data + 1 + 2 * (yy * width + xx)), en);
        w.updateSpiceInRegion(std::max(cx[0] - 1, 0), std::max(cy[0] - 1, 0), std::min(cx[0] + width + 1, w.width_),
                              std::min(cy[0] + height + 1, w.height_));
        break;
    }
    case ET_TRANSFORM_TILES: {
        if (amount > 6) break;
        for (int yy = 0; yy < w.height_; ++yy)
            for (int xx = 0; xx < w.width_; ++xx)
                for (int k = 0; k < amount; ++k)
                    if (w.cell(xx, yy).tile == rd16(data + 1 + 4 * k)) {
                        changeMapTile(xx, yy, rd16(data + 3 + 4 * k), en);
                        break;
                    }
        break;
    }
    case ET_ADD_BUILDING_DESTRUCT: {
        const int oldShakes = w.screenShakes_;
        const int idx = w.addBuilding(side, item, cx[0], cy[0], false, true, true);
        Building* b = w.building(side, idx);
        if (!b) break;
        w.destroyBuilding(*b, false);
        --s.buildingsBuilt;
        if (item >= 0 && item < formats::kMaxBuildingTypes) --s.buildingsBuiltPerType[static_cast<std::size_t>(item)];
        b->deadTimer = 1;
        w.screenShakes_ = oldShakes;
        break;
    }
    case ET_ADD_HOMING_BULLET: {
        const int weapon = amount, enemySide = item & 7;
        int enemyIndex = var(en);
        const int px = cx[0] * 32 + cx[1], py = cy[0] * 32 + cy[1];
        int tx = 0, ty = 0, hs = enemySide;
        const int kind = slotKind(enemySide, enemyIndex);
        if (kind == 1) {
            const Unit& eu = w.sides_[enemySide].units[static_cast<std::size_t>(enemyIndex)];
            tx = eu.posX >> 16;
            ty = eu.posY >> 16;
        } else if (kind == 2) {
            const Building& eb = w.sides_[enemySide].buildings[static_cast<std::size_t>(enemyIndex)];
            w.closestBuildingTile(px / 32, py / 32, eb, tx, ty);
            tx = tx * 32 + 16;
            ty = ty * 32 + 16;
            enemyIndex = kNoIndex;
            hs = -1;
        }
        if (weapon < 0 || weapon >= formats::kMaxWeapons) break;
        const int idx = w.addBullet(side, weapon, 0, kNoIndex, px, py, tx, ty, enemyIndex & 0xffff, hs);
        setVar(val2, idx);
        if (idx >= 0) s.bullets[static_cast<std::size_t>(idx)].tag = static_cast<std::uint8_t>(val1);
        if (bo) w.playSound(TT.weapons[weapon].firingSound, px / 32, py / 32);
        break;
    }
    case ET_ACTIVATE_TIMER:
        if (val1 >= 0 && val1 < static_cast<int>(x.conditions.size())) {
            std::uint8_t* c = x.conditions[static_cast<std::size_t>(val1)].data();
            const int ct = c[cd::kType];
            if (ct == CT_INTERVAL || ct == CT_TIMER || ct == CT_RANDOMINTERVAL) {
                c[cd::kArg1] = 0;
                wr32(c + cd::kVal4, static_cast<std::int32_t>(w.gameTicks_));
            }
        }
        break;
    case ET_REMOVE_MESSAGE: case ET_SET_MESSAGE_COLOR: case ET_SET_TOOLTIP: break;  // UI only (not modelled)
    // ---- side
    case ET_TRANSFER_CREDITS: transferCredits(side, en, val1); break;
    case ET_SET_BUILDING_UPGRADES: setBuildingUpgrades(side, item, en, val1); break;
    case ET_SET_STARPORT_STOCK:
        if (item >= 0 && item < formats::kMaxUnitTypes) s.starportStock[static_cast<std::size_t>(item)] = valueOperation(s.starportStock[static_cast<std::size_t>(item)], val1, en);
        break;
    case ET_SET_STARPORT_COST:
        if (item < 0 || item >= formats::kMaxUnitTypes) break;
        if (bo) s.starportPrice[static_cast<std::size_t>(item)] = static_cast<int>(TT.units[item].cost);
        else s.starportPrice[static_cast<std::size_t>(item)] = llimit(valueOperation(s.starportPrice[static_cast<std::size_t>(item)], val1, en), 0);
        break;
    case ET_CHANGE_STARPORT_UNIT:
        if (amount >= 0 && amount < 8) s.starportTypes[static_cast<std::size_t>(amount)] = static_cast<std::int8_t>(item);
        break;
    case ET_SET_AI_PROPERTY: {
        AiState& ai = w.ai_[side];
        const int off = val2;
        if (off < 0) break;
        if (amount == 0 && off < static_cast<int>(kAiSize)) {
            ai.b(off) = static_cast<std::uint8_t>(valueOperation(static_cast<std::int8_t>(ai.b(off)), val1, en));
        } else if (amount == 1 && off + 2 <= static_cast<int>(kAiSize)) {
            ai.set<std::int16_t>(off, static_cast<std::int16_t>(valueOperation(ai.get<std::int16_t>(off), val1, en)));
        } else if (amount == 2 && off + 4 <= static_cast<int>(kAiSize)) {
            ai.set<std::int32_t>(off, valueOperation(ai.get<std::int32_t>(off), val1, en));
        } else if (amount == 3 && off + 4 <= static_cast<int>(kAiSize)) {
            float cur = ai.get<float>(off), v = 0;
            std::memcpy(&v, &val1, 4);
            switch (en) {
            case 0: cur = v; break;
            case 1: cur += v; break;
            case 2: cur -= v; break;
            case 3: cur *= v; break;
            case 4: cur = cur * v / 100; break;
            case 5: if (v != 0) cur /= v; break;
            case 7: cur = std::min(cur, v); break;
            case 8: cur = std::max(cur, v); break;
            }
            ai.set<float>(off, cur);
        }
        break;
    }
    case ET_SHOW_SIDE_DATA: case ET_SHOW_AI_DATA: case ET_SET_MEMORY_DATA: case ET_SHOW_MEMORY_DATA: break;  // debug / raw memory
    // ---- units
    case ET_DESTROY_UNIT: {
        Unit* u = w.unit(side, obj);
        if (!u || u->state == UnitState::Dead) break;
        if (bo) {
            u->state = UnitState::Dead;
            u->deadStateTimer = 1;
            u->attackerIndex = kNoIndex;
        } else {
            w.destroyUnit(*u);
        }
        break;
    }
    case ET_DAMAGE_HEAL_UNIT: {
        Unit* u = w.unit(side, obj);
        if (!u || u->state == UnitState::Dead) break;
        const int hpMax = static_cast<int>(TT.units[u->type].hitPoints);
        const int hp = (bo ? (hpMax * val1) / 100 : val1) * (en ? 1 : -1);
        u->health = std::clamp(u->health + hp, 0, hpMax);
        if (!u->health) w.destroyUnit(*u);
        break;
    }
    case ET_CHANGE_UNIT_OWNER: w.changeUnitOwner(side, item, obj, false); break;
    case ET_CHANGE_UNIT_TYPE: {
        Unit* u = w.unit(side, obj);
        if (!u || bo || item < 0 || item >= formats::kMaxUnitTypes) break;  // (defined type: extended templates)
        const auto& from = TT.units[u->type];
        const auto& to = TT.units[item];
        u->health = llimit(u->health + static_cast<int>(to.hitPoints) - static_cast<int>(from.hitPoints), 1);
        u->flags = (u->flags & ~from.flags) | to.flags;
        u->speed = to.speed;
        u->type = item;
        break;
    }
    case ET_SET_UNIT_FLAG:
        if (Unit* u = w.unit(side, obj)) u->flags = flagOperation(u->flags, val1, en);
        break;
    case ET_SET_UNIT_PROPERTY:
        if (Unit* u = w.unit(side, obj)) unitField(*u, amount, item, setter(en, val1));
        break;
    case ET_SELECT_UNIT:
        if (Unit* u = w.unit(side, obj)) {
            u->selected = true;
            if (bo) u->prevSelected = false;
        }
        break;
    case ET_AIRLIFT_UNIT: airliftUnit(side, cx[0], cy[0], bo != 0, obj); break;
    case ET_SHOW_UNIT_DATA: break;
    // ---- buildings
    case ET_DESTROY_BUILDING: {
        Building* b = w.building(side, obj);
        if (!b || b->state == 17) break;
        if (bo) {
            b->state = 17;
            b->deadTimer = 1;
            b->attackerIndex = kNoIndex;
        } else {
            w.destroyBuilding(*b, false);
        }
        break;
    }
    case ET_DAMAGE_HEAL_BUILDING: {
        Building* b = w.building(side, obj);
        if (!b || b->state == 17) break;
        const int hpMax = static_cast<int>(TT.buildings[b->type].hitPoints);
        const int hp = (bo ? (hpMax * val1) / 100 : val1) * (en ? 1 : -1);
        b->health = std::clamp(b->health + hp, 0, hpMax);
        if (!b->health) w.destroyBuilding(*b, false);
        break;
    }
    case ET_CHANGE_BUILDING_OWNER:
        if (Building* b = w.building(side, obj)) {
            b->flags |= 0x1000000;  // BFLAGS_1000000_INFILTRATED
            w.captureBuilding(side, item, obj);
        }
        break;
    case ET_CHANGE_BUILDING_TYPE: {
        Building* b = w.building(side, obj);
        if (!b || item < 0 || item >= formats::kMaxBuildingTypes) break;
        const auto& from = TT.buildings[b->type];
        const auto& to = TT.buildings[item];
        b->health = llimit(b->health + static_cast<int>(to.hitPoints) - static_cast<int>(from.hitPoints), 1);
        b->flags = (b->flags & ~from.flags) | to.flags;
        b->type = item;
        break;
    }
    case ET_SET_BUILDING_FLAG:
        if (Building* b = w.building(side, obj)) b->flags = flagOperation(b->flags, val1, en);
        break;
    case ET_SET_BUILDING_PROPERTY:
        if (Building* b = w.building(side, obj)) buildingField(*b, amount, item, setter(en, val1));
        break;
    case ET_SELECT_BUILDING:
        if (Building* b = w.building(side, obj)) {
            b->selected = true;
            if (bo) b->prevSelected = false;
        }
        break;
    case ET_SHOW_BUILDING_DATA: case ET_SET_BULLET_PROPERTY: case ET_SET_EXPLOSION_PROPERTY: break;  // (no data used them)
    // ---- crates
    case ET_REMOVE_CRATE: {
        Crate& cr = w.crates_[static_cast<std::size_t>(obj) % kMaxCrates];
        cr.active = false;
        if (w.inBounds(cr.x, cr.y)) w.cell(cr.x, cr.y).flags &= ~kCrate;
        break;
    }
    case ET_PICKUP_CRATE: case ET_SET_CRATE_PROPERTY: case ET_SHOW_CRATE_DATA: break;  // not modelled
    // ---- tiles
    case ET_CHANGE_TILE: changeMapTile(cx[0], cy[0], val1, en); break;
    case ET_SET_TILE_ATTRIBUTE: {
        if (!w.inBounds(cx[0], cy[0])) break;
        Cell& c = w.cell(cx[0], cy[0]);
        const std::uint32_t oldF = c.flags;
        c.flags = flagOperation(c.flags, val1, en);
        if ((oldF & kSpiceMask) && !(c.flags & kSpiceMask)) c.tile = backupTile(cx[0], cy[0]);
        if ((oldF & kConcrete) && !(c.flags & kConcrete)) c.tile = backupTile(cx[0], cy[0]);
        if (val1 >= 20 && val1 <= 22)
            w.updateSpiceInRegion(std::max(cx[0] - 1, 0), std::max(cy[0] - 1, 0), std::min(cx[0] + 2, w.width_),
                                  std::min(cy[0] + 2, w.height_));
        break;
    }
    case ET_SET_TILE_PROPERTY: {
        if (!w.inBounds(cx[0], cy[0])) break;
        Cell& c = w.cell(cx[0], cy[0]);
        auto set = setter(en, val1);
        auto doField = [&](auto& field, int bits) {
            int v = bits == 8 ? static_cast<std::int8_t>(static_cast<std::uint8_t>(field))
                  : bits == 16 ? static_cast<std::int16_t>(static_cast<std::uint16_t>(field))
                               : static_cast<std::int32_t>(field);
            set(v, bits);
            field = static_cast<std::remove_reference_t<decltype(field)>>(v);
        };
        if (amount == 0) {  // byte
            if (item == 0) { std::uint8_t lo = static_cast<std::uint8_t>(c.tile); doField(lo, 8); c.tile = static_cast<std::uint16_t>((c.tile & 0xff00) | lo); }
            else if (item == 1) { std::uint8_t hi = static_cast<std::uint8_t>(c.tile >> 8); doField(hi, 8); c.tile = static_cast<std::uint16_t>((c.tile & 0xff) | hi << 8); }
            else if (item == 8) doField(c.shroud, 8);
            else if (item == 9) doField(c.damage, 8);
        } else if (amount == 1) {
            if (item == 0) doField(c.tile, 16);
            else if (item == 2 && !x.backupTile.empty()) doField(x.backupTile[static_cast<std::size_t>(cy[0]) * w.width_ + cx[0]], 16);
        } else if (amount == 2 && item == 4) {
            doField(c.flags, 32);
        }
        break;
    }
    case ET_REVEAL_TILE: w.revealCircle(cx[0], cy[0], amount); break;
    case ET_HIDE_TILE: hideTile(cx[0], cy[0]); break;
    case ET_SHOW_TILE_DATA: break;
    // ---- orders (Generate*Order: the selected units/buildings of the side, max 64)
    case ET_ORDER_UNIT_MOVE:
        if (w.inBounds(cx[0], cy[0]))
            forSelectedUnits(side, [](const Unit&) { return true; }, [&](int i) { w.orderMove(side, i, cx[0], cy[0]); });
        restoreUnitSelection(side, bo);
        break;
    case ET_ORDER_DOCK_WITH_REFINERY:
        forSelectedUnits(side, [](const Unit&) { return true; },
                         [&](int i) { w.orders_.push_back({World::Order::DockRefinery, side, i, 0, 0, side, obj}); });
        restoreUnitSelection(side, bo);
        break;
    case ET_ORDER_REPAIR_SELECTED_UNITS:
        forSelectedUnits(side, [&](const Unit& u) { return !TT.units[u.type].isInfantry; },
                         [&](int i) { w.orders_.push_back({World::Order::RepairUnit, side, i, 0, 0, side, obj}); });
        restoreUnitSelection(side, bo);
        break;
    case ET_ORDER_REPAIR_SINGLE_UNIT: {
        const Unit* u = w.unit(side, obj);
        if (u && u->state != UnitState::Dead && !TT.units[u->type].isInfantry)
            w.orders_.push_back({World::Order::RepairUnit, side, obj, 0, 0, side, kNoIndex});
        break;
    }
    case ET_ORDER_UNIT_ATTACK_UNIT: {
        // (A_ITEM = the side whose selected units attack; side/obj = the target)
        const int attacker = item & 7;
        forSelectedUnits(attacker, [&](const Unit& u) { return TT.units[u.type].primaryWeapon != -1; },
                         [&](int i) { w.orderAttackUnit(attacker, i, side, obj); });
        restoreUnitSelection(attacker, bo);
        break;
    }
    case ET_ORDER_UNIT_ATTACK_BUILDING: {
        const int attacker = item & 7;
        forSelectedUnits(attacker, [&](const Unit& u) {
            const int beh = TT.units[u.type].specialBehavior;
            return beh != behaviour::kHarvester && beh != behaviour::kMcv && beh != behaviour::kThumper;
        }, [&](int i) { w.orderAttackBuilding(attacker, i, side, obj); });
        restoreUnitSelection(attacker, bo);
        break;
    }
    case ET_ORDER_UNIT_ATTACK_TILE:
        if (w.inBounds(cx[0], cy[0]))
            forSelectedUnits(side, [&](const Unit& u) { return TT.units[u.type].primaryWeapon != -1; },
                             [&](int i) { w.orderAttackTile(side, i, cx[0], cy[0]); });
        restoreUnitSelection(side, bo);
        break;
    case ET_ORDER_UNIT_GUARD:
        forSelectedUnits(side, [&](const Unit& u) { return TT.units[u.type].primaryWeapon != -1; },
                         [&](int i) { w.orders_.push_back({World::Order::Guard, side, i, 0, 0}); });
        restoreUnitSelection(side, bo);
        break;
    case ET_ORDER_UNIT_SCATTER:
        forSelectedUnits(side, [](const Unit&) { return true; },
                         [&](int i) { w.orders_.push_back({World::Order::Scatter, side, i, 0, 0}); });
        restoreUnitSelection(side, bo);
        break;
    case ET_ORDER_UNIT_RETREAT: {
        int bx = 0, by = 0;
        if (w.findBestBasePosition(side, bx, by))
            forSelectedUnits(side, [](const Unit&) { return true; },
                             [&](int i) { w.orders_.push_back({World::Order::Retreat, side, i, bx, by}); });
        restoreUnitSelection(side, bo);
        break;
    }
    case ET_ORDER_UNIT_DEPLOY: w.orders_.push_back({World::Order::Deploy, side, obj, 0, 0}); break;
    case ET_ORDER_BUILDING_ATTACK_UNIT: case ET_ORDER_BUILDING_ATTACK_BUILDING:
        restoreBuildingSelection(side, bo);  // building attack orders are not modelled
        break;
    case ET_ORDER_BUILDING_SET_PRIMARY: {
        int n = 0;
        for (int i = s.firstBuilding; i != -1 && n < 64; i = s.nextObject(i))
            if (s.buildings[i].selected && s.buildings[i].state != 17) {
                w.orders_.push_back({World::Order::SetPrimary, side, i, 0, 0});
                ++n;
            }
        restoreBuildingSelection(side, bo);
        break;
    }
    case ET_ORDER_BUILDING_REPAIR: w.orders_.push_back({World::Order::RepairBuilding, side, obj, 0, 0}); break;
    case ET_ORDER_BUILDING_SELL: w.orders_.push_back({World::Order::SellBuilding, side, obj, 0, 0}); break;
    case ET_ORDER_STOP:
        forSelectedUnits(side, [](const Unit&) { return true; }, [&](int i) { w.orderStop(side, i); });
        restoreUnitSelection(side, bo);
        restoreBuildingSelection(side, bo);
        break;
    case ET_ORDER_BUILD_BUILDING_PICK: w.orders_.push_back({World::Order::BuildingPick, side, kNoIndex, item, 0}); break;
    case ET_ORDER_BUILD_BUILDING_CANCEL:
        if (bo) s.buildingQueue.onHold = true;
        w.orders_.push_back({World::Order::BuildingCancel, side, kNoIndex, s.buildingQueue.type, 0});
        break;
    case ET_ORDER_BUILD_PLACE_BUILDING:
        w.orders_.push_back({World::Order::PlaceBuilding, side, kNoIndex, cx[0], cy[0], side, s.buildingQueue.type});
        break;
    case ET_ORDER_BUILD_UNIT_PICK: w.orders_.push_back({World::Order::UnitPick, side, kNoIndex, item, 0}); break;
    case ET_ORDER_BUILD_UNIT_CANCEL:
        for (auto& q : s.unitQueue) {
            const int built = q.type;
            const int pre = built >= 0 ? TT.units[built].prereq1BuildingGroup : -1;
            const int qi = en <= 6 ? en : en + 3;
            if ((!amount && built == item) ||
                (amount && (en == 0 || (en > 0 && pre == static_cast<std::uint8_t>(TT.groupIds[qi]))))) {
                if (bo) q.onHold = true;
                w.orders_.push_back({World::Order::UnitCancel, side, kNoIndex, built, 0});
            }
        }
        break;
    case ET_ORDER_STARPORT_PICK:
        if (!s.starportFrigateBusy) w.orders_.push_back({World::Order::StarportPick, side, kNoIndex, item, 0});
        break;
    case ET_ORDER_STARPORT_UNPICK: w.orders_.push_back({World::Order::StarportUnpick, side, kNoIndex, item, 0}); break;
    case ET_ORDER_STARPORT_PURCHASE: w.orders_.push_back({World::Order::StarportPurchase, side, kNoIndex, 0, 0}); break;
    case ET_ORDER_STARPORT_CANCEL: w.orders_.push_back({World::Order::StarportCancel, side, kNoIndex, 0, 0}); break;
    case ET_ORDER_UPGRADE_PICK: w.orders_.push_back({World::Order::UpgradePick, side, kNoIndex, item, 0}); break;
    case ET_ORDER_UPGRADE_CANCEL:
        if (bo) s.upgradeQueue.onHold = true;
        w.orders_.push_back({World::Order::UpgradeCancel, side, kNoIndex, s.upgradeQueue.type, 0});
        break;
    case ET_ORDER_SPECIAL_WEAPON:
        w.orders_.push_back({World::Order::SpecialWeapon, side, kNoIndex, cx[0] & 0xffff, cy[0] & 0xffff, side, item});
        break;
    // ---- variables (a subset; none of the shipped missions use variables)
    case ET_SET_VARIABLE: {
        const int target = (bo ? var(item) : 0) + amount;  // use_offset: + value of offset_var
        setVar(target, valueOperation(var(target), val1, en));
        break;
    }
    case ET_GET_VARIABLE: setVar(amount, var(item + (a[1] ? var(a[1]) : 0))); break;
    case ET_GET_RANDOM_VALUE: {
        const int span = val1 - item + 1;
        setVar(amount, span > 0 ? item + rnd() % span : item);
        break;
    }
    case ET_GET_UNIT_COUNT: case ET_GET_BUILDING_COUNT: case ET_GET_BULLET_COUNT: case ET_GET_EXPLOSION_COUNT:
    case ET_GET_CRATE_COUNT: case ET_GET_TILE_COUNT: case ET_GET_SIDE_COUNT:
        setVar(amount, var(amount) + 1);
        break;
    case ET_GET_SPICE_COUNT:
        if (w.inBounds(cx[0], cy[0])) setVar(amount, var(amount) + w.cell(cx[0], cy[0]).spice());
        break;
    case ET_GET_DAMAGE_COUNT:
        if (w.inBounds(cx[0], cy[0])) setVar(amount, var(amount) + w.cell(cx[0], cy[0]).damage);
        break;
    case ET_GET_GAME_TICKS: setVar(amount, static_cast<std::int32_t>(w.gameTicks_)); break;
    case ET_GET_MY_SIDE_ID: setVar(amount, mySide()); break;
    case ET_GET_DIFFICULTY: setVar(amount, w.difficulty); break;
    // ---- blocks
    case ET_CALLABLE_BLOCK_START: case ET_HOOK_BLOCK_START: break;  // skipped in the global list
    case ET_EXECUTE_BLOCK:
        if (val1 >= 0 && val1 < static_cast<int>(x.events.size()) &&
            x.events[static_cast<std::size_t>(val1)][ev::kType] == ET_CALLABLE_BLOCK_START)
            executeRange(val1 + 1, x.next[static_cast<std::size_t>(val1)] - 1, BlockType::Block);
        break;
    case ET_EXIT_FROM_BLOCK: ++x.exitCount; break;
    case ET_BREAK_LOOP: ++x.breakCount; break;
    case ET_CONTINUE_LOOP: ++x.continueCount; break;
    case ET_END: break;
    default:
        // Remaining extended types (expressions/loops, Get* queries, template/memory edits, UI
        // queries): not modelled - docs/extended-events.md lists them.
        (void)ei;
        break;
    }
}

// ===========================================================================================
// World entry points
// ===========================================================================================

// Mission__LoadObjectives with extend-event-limits.asm, then setupmapstuff.c's event setup:
// Timer/Interval base times and Flag values cleared, variables reset, block structure cached.
void World::extLoad(const formats::MisFile& mission) {
    ext_ = ExtMission{};
    ext_.active = true;
    ext_.events = mission.extEvents;
    ext_.conditions = mission.extConditions;
    if (ext_.conditions.empty()) ext_.conditions.resize(1);  // (condition indexes always point somewhere)
    for (auto& c : ext_.conditions) {
        const int t = c[cd::kType];
        if (t == CT_INTERVAL || t == CT_TIMER) wr32(c.data() + cd::kVal4, 0);
        if (t == CT_FLAG) wr32(c.data() + cd::kVal3, 0);
    }
    const int n = static_cast<int>(ext_.events.size());
    ext_.next.assign(static_cast<std::size_t>(n), 0);
    ext_.elseIndex.assign(static_cast<std::size_t>(n), -1);
    ExtEventRunner r(*this);
    for (int i = 0; i < n; ++i) {
        ext_.next[static_cast<std::size_t>(i)] = i + 1;
        const int t = ext_.events[static_cast<std::size_t>(i)][ev::kType];
        if (r.isStartBlock(i) || t == ET_ELSE_IF || t == ET_ELSE)
            ext_.next[static_cast<std::size_t>(i)] = r.findEndMarker(i) + 1;
    }
    // Condition indexes beyond the table read result 0 (condResult is 256 entries).
}

// Backup tiles (GameMapTileStruct back_up_tile_index = the map tile & 0x0FFF) and the tileset's
// TILEATR flags for ChangeMapTile.
void World::extInitMap(const formats::MapFile& map, const formats::TileAtr& tileAtr) {
    ext_.backupTile.assign(cells_.size(), 0);
    for (std::size_t i = 0; i < cells_.size() && i < map.cells.size(); ++i)
        ext_.backupTile[i] = static_cast<std::uint16_t>(map.cells[i].tile & 0x0FFF);
    ext_.tileAtr.assign(tileAtr.flags.begin(), tileAtr.flags.end());
}

void World::extCheckEvents() { ExtEventRunner(*this).run(); }

}  // namespace d2k::game

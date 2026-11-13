#include "game/save_check.hpp"

#include "game/savegame.hpp"

#include <algorithm>
#include <cstdlib>

namespace d2k::game {

RoundTripReport saveRoundTrip(const WorldFactory& make, int preTicks, int postTicks, const OrderScript& orders,
                              int compareEvery) {
    RoundTripReport rep;
    std::unique_ptr<World> a = make();
    std::uint32_t step = 0;
    for (; step < static_cast<std::uint32_t>(preTicks); ++step) {
        orders(*a, step);
        a->tick();
    }
    orders(*a, step);  // queued now, executed by the next tick of both worlds
    rep.pendingOrdersSaved = static_cast<int>(a->pendingOrders());
    const std::vector<std::uint8_t> state = saveWorldState(*a);
    rep.saveBytes = state.size();
    rep.hashAtSave = fnv1a64(state);

    // Through the full file format as the frontend does it.
    SaveMeta meta;
    meta.name = "round trip";
    meta.map = "TEST.MAP";
    meta.gameTicks = a->gameTicks();
    const std::vector<std::uint8_t> file = writeSaveFile(meta, *a);
    rep.fileBytes = file.size();
    std::unique_ptr<World> b = make();
    rep.freshDiffered = worldStateHash(*b) != rep.hashAtSave;
    try {
        std::span<const std::uint8_t> section;
        const SaveMeta back = readSaveFile(file, section);
        if (back.name != meta.name || back.gameTicks != meta.gameTicks) {
            rep.error = "metadata did not round-trip";
            return rep;
        }
        if (!std::equal(section.begin(), section.end(), state.begin(), state.end())) {
            rep.error = "world section in the file differs from saveWorldState";
            return rep;
        }
        loadWorldState(*b, section);
    } catch (const SaveError& e) {
        rep.error = std::string("load failed: ") + e.what();
        return rep;
    }
    if (saveWorldState(*b) != state) {
        rep.error = "loaded world saves differently (a field is not restored)";
        return rep;
    }
    ++step;
    for (int k = 0; k < postTicks; ++k, ++step) {
        if (k > 0) {
            orders(*a, step - 1);
            orders(*b, step - 1);
        }
        a->tick();
        b->tick();
        if ((k + 1) % compareEvery == 0 || k + 1 == postTicks) {
            if (worldStateHash(*a) != worldStateHash(*b)) {
                rep.divergedAt = preTicks + 1 + k;
                rep.error = "state diverged after loading, at step " + std::to_string(rep.divergedAt);
                return rep;
            }
        }
    }
    const std::vector<std::uint8_t> ea = saveWorldState(*a), eb = saveWorldState(*b);
    rep.hashAtEnd = fnv1a64(ea);
    if (ea != eb) {
        rep.error = "final save bytes differ";
        return rep;
    }
    rep.ok = true;
    return rep;
}

void scriptedOrders(World& w, int side, std::uint32_t step) {
    if (step % 50 != 0) return;
    Side& me = w.sides()[side];
    const auto& tpl = w.rules().templates->bin;
    const std::uint32_t phase = step / 50;

    // Moves: every 3rd ground unit wanders a few tiles (every 150 ticks).
    if (phase % 3 == 0) {
        int n = 0;
        for (int i = me.firstUnit; i != -1; i = me.nextObject(i)) {
            const Unit& u = me.units[i];
            const auto& t = tpl.units[u.type];
            if (u.flags & unit_flags::kExcluded) continue;
            if (t.specialBehavior != behaviour::kNone) continue;
            if (n++ % 3 != 0) continue;
            const int dx = static_cast<int>((phase + i) % 9) - 4, dy = static_cast<int>((phase * 7 + i) % 9) - 4;
            const int tx = std::clamp(u.blockFromX + dx, 0, w.width() - 1);
            const int ty = std::clamp(u.blockFromY + dy, 0, w.height() - 1);
            w.orderMove(side, i, tx, ty);
        }
    }
    // Attack: the first armed ground unit goes for the nearest enemy unit (every 300 ticks).
    if (phase % 6 == 1) {
        for (int i = me.firstUnit; i != -1; i = me.nextObject(i)) {
            const Unit& u = me.units[i];
            const auto& t = tpl.units[u.type];
            if (t.primaryWeapon < 0 || (u.flags & unit_flags::kExcluded) || t.specialBehavior != 0) continue;
            int best = 1 << 30, bs = -1, bi = -1;
            for (int s = 0; s < kMaxSides; ++s) {
                if (w.diplomacy(side, s) != 1) continue;
                const Side& es = w.sides()[s];
                for (int j = es.firstUnit; j != -1; j = es.nextObject(j)) {
                    const Unit& e = es.units[j];
                    if (e.flags & unit_flags::kExcluded) continue;
                    const int beh = tpl.units[e.type].specialBehavior;
                    if (beh == behaviour::kSandworm || beh == behaviour::kCarryall || beh == behaviour::kFrigate) continue;
                    const int d = std::abs(e.blockFromX - u.blockFromX) + std::abs(e.blockFromY - u.blockFromY);
                    if (d < best) { best = d; bs = s; bi = j; }
                }
            }
            if (bs >= 0) w.orderAttackUnit(side, i, bs, bi);
            break;
        }
    }
    // Production: keep a unit queued; build buildings and place them near the base.
    if (phase % 4 == 2) {
        const auto units = w.buildableUnits(side);
        if (!units.empty()) {
            const auto& o = units[phase % units.size()];
            if (!w.unitQueueFor(side, o.type).active) w.orderBuildUnit(side, o.type);
        }
        const QueueState bq = w.buildingQueue(side);
        if (bq.active && bq.ready) {
            bool placed = false;
            for (int r = 0; r < 10 && !placed; ++r)
                for (int bi = me.firstBuilding; bi != -1 && !placed; bi = me.nextObject(bi)) {
                    const Building& c = me.buildings[bi];
                    for (int dy = -r; dy <= r && !placed; ++dy)
                        for (int dx = -r; dx <= r && !placed; ++dx) {
                            if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                            if (!w.canPlaceBuilding(side, bq.type, c.tileX + dx, c.tileY + dy)) continue;
                            placed = w.orderPlaceBuilding(side, bq.type, c.tileX + dx, c.tileY + dy);
                        }
                }
            if (!placed) w.orderCancelBuilding(side, bq.type);
        } else if (!bq.active) {
            const auto blds = w.buildableBuildings(side);
            if (!blds.empty()) w.orderBuildBuilding(side, blds[phase % blds.size()].type);
        }
    }
    // Starport: pick what is available and send the frigate.
    if (phase % 8 == 5 && w.starportOpen(side) && !w.starportBusy(side)) {
        int picked = 0;
        for (const StarportOption& o : w.starportUnits(side))
            if (o.type >= 0 && o.available && picked < 3 && w.orderStarportPick(side, o.type)) ++picked;
        if (picked) w.orderStarportPurchase(side);
    }
}

}  // namespace d2k::game

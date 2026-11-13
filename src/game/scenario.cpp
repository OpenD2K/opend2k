// Scenario construction. Spice tile selection, special-value decoding and the
// house-version lookup follow D2kEditor (_renderer.pas, _structures.pas; MIT,
// github.com/jkoncick/D2kEditor). To be cross-checked against DUNE2000.DAT.

#include "game/scenario.hpp"

#include "game/spice_tables.hpp"

#include <algorithm>
#include <array>

namespace d2k::game {

namespace {

using formats::SpecialType;

constexpr std::array<std::uint16_t, 3> kConcreteTiles = {651, 671, 691};

constexpr std::uint16_t kTileConcreteFlag = 0x1000;
constexpr std::uint16_t kTileIndexMask = 0x0FFF;

// Spice from the special layer (1/2) or encoded in the tile's high bits.
std::uint8_t spiceOf(const formats::MapCell& c) {
    if (c.special == 1 || c.special == 2) return static_cast<std::uint8_t>(c.special);
    if ((c.tile & kTileConcreteFlag) == 0) {
        const int amount = (c.tile >> 13) & 7;
        if (amount == 1 || amount == 2) return 1;
    }
    return 0;
}

ObjectKind miscKind(std::uint16_t special) {
    switch (special) {
    case 20: return ObjectKind::WormSpawner;
    case 23: return ObjectKind::PlayerStart;
    case 41: case 42: case 43: case 44: case 45: case 46: return ObjectKind::SpiceBloom;
    case 6: case 7: case 9: case 10: case 12: case 13: case 16: return ObjectKind::Crate;
    default: return ObjectKind::Other;
    }
}

int houseOfSide(const formats::MisFile& mis, int side) {
    if (side < 0 || side >= formats::kMaxSides) return -1;
    return mis.houseId[side];
}

}  // namespace

Scenario buildScenario(const formats::MapFile& map, const formats::MisFile& mis,
                       const formats::Templates& templates, const formats::TileData& tileData) {
    Scenario s;
    s.width = map.width;
    s.height = map.height;
    const std::size_t n = static_cast<std::size_t>(map.width) * map.height;
    s.tiles.resize(n);
    s.spice.resize(n);

    for (int y = 0; y < map.height; ++y)
        for (int x = 0; x < map.width; ++x) s.spice[y * map.width + x] = spiceOf(map.at(x, y));

    auto spiceAt = [&](int x, int y) {
        x = std::clamp(x, 0, map.width - 1);
        y = std::clamp(y, 0, map.height - 1);
        return s.spice[static_cast<std::size_t>(y) * map.width + x];
    };

    std::vector<MapObject> buildings, units, misc;
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const auto& c = map.at(x, y);
            std::uint16_t tile = c.tile;
            const auto sp = spiceAt(x, y);
            if (sp == 1) {
                int mask = 0;
                const int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
                for (int i = 0; i < 8; ++i)
                    if (spiceAt(x + dx[i], y + dy[i]) > 0) mask |= 1 << i;
                tile = kThinSpiceTiles[mask];
            } else if (sp == 2) {
                tile = kThickSpiceTiles[kThickSpicePattern[y & 7][x & 7] - 1];
            } else if (tile & kTileConcreteFlag) {
                tile = kConcreteTiles[(x + y) % kConcreteTiles.size()];
            }
            s.tiles[static_cast<std::size_t>(y) * map.width + x] = tile & kTileIndexMask;

            // Objects in the special layer (values <= 2 are spice).
            const std::uint16_t special = c.special;
            if (special <= 2 || special == 0xFFFF) continue;
            MapObject o;
            o.x = x;
            o.y = y;
            o.special = special;

            if (special & 0x8000) {  // extended encoding: misc object
                o.kind = miscKind(special & 0x7FFF);
                misc.push_back(o);
                continue;
            }
            if (special & 0x4000) {  // extended encoding: explicit unit
                o.kind = ObjectKind::Unit;
                o.side = (special >> 6) & 7;
                o.house = houseOfSide(mis, o.side);
                o.templateIndex = (special & 63) < templates.bin.unitCount ? (special & 63) : -1;
                units.push_back(o);
                continue;
            }
            if (special & 0x2000) {  // extended encoding: explicit building
                o.kind = ObjectKind::Building;
                o.side = (special >> 7) & 7;
                o.house = houseOfSide(mis, o.side);
                o.templateIndex = (special & 127) < templates.bin.buildingCount ? (special & 127) : -1;
                buildings.push_back(o);
                continue;
            }
            if (special & 0x1000) continue;  // explosion marker, not rendered statically
            if (special >= formats::kTileDataEntries) continue;

            const auto& td = tileData[special];
            o.side = td.side;
            o.house = houseOfSide(mis, o.side);
            switch (td.type) {
            case SpecialType::Unit:
                o.kind = ObjectKind::Unit;
                if (o.house >= 0 && td.index < formats::kMaxUnitTypes)
                    o.templateIndex = templates.unitHouseVersion[td.index][o.house];
                units.push_back(o);
                break;
            case SpecialType::Building:
                o.kind = ObjectKind::Building;
                if (o.house >= 0 && td.index < formats::kMaxBuildingTypes)
                    o.templateIndex = templates.buildingHouseVersion[td.index][o.house];
                buildings.push_back(o);
                break;
            case SpecialType::MiscObject:
                o.kind = miscKind(special);
                o.side = -1;
                misc.push_back(o);
                break;
            default: break;
            }
        }
    }

    // Draw order: buildings (top to bottom), then units, then markers.
    auto byRow = [](const MapObject& a, const MapObject& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; };
    std::stable_sort(buildings.begin(), buildings.end(), byRow);
    std::stable_sort(units.begin(), units.end(), byRow);
    s.objects = std::move(buildings);
    s.objects.insert(s.objects.end(), units.begin(), units.end());
    s.objects.insert(s.objects.end(), misc.begin(), misc.end());
    return s;
}

std::array<int, formats::kMaxSides> sideObjectCounts(const formats::MapFile& map, const formats::TileData& tileData) {
    std::array<int, formats::kMaxSides> n{};
    for (int y = 0; y < map.height; ++y)
        for (int x = 0; x < map.width; ++x) {
            const std::uint16_t special = map.at(x, y).special;
            if (special <= 2 || special == 0xFFFF || (special & 0x8000)) continue;
            if (special & 0x4000) {  // explicit unit
                ++n[(special >> 6) & 7];
            } else if (special & 0x2000) {  // explicit building
                ++n[(special >> 7) & 7];
            } else if (!(special & 0x1000) && special < formats::kTileDataEntries) {
                const auto& td = tileData[special];
                if ((td.type == SpecialType::Unit || td.type == SpecialType::Building) && td.side < formats::kMaxSides)
                    ++n[td.side];
            }
        }
    return n;
}

}  // namespace d2k::game

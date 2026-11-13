#pragma once
// Initial game state built from a .MAP + its .MIS: terrain tiles (with spice and
// concrete resolved to tileset tiles), spice amounts, and the starting objects
// (buildings, units, misc objects) encoded in the map's special layer.
// Pure logic: no SDL / OS calls.

#include "formats/bintables.hpp"
#include "formats/map.hpp"
#include "formats/mis.hpp"
#include "formats/templates.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace d2k::game {

enum class ObjectKind { Building, Unit, WormSpawner, PlayerStart, SpiceBloom, Crate, Other };

struct MapObject {
    ObjectKind kind = ObjectKind::Other;
    int x = 0, y = 0;        // tile position (top-left for buildings)
    int side = -1;           // 0..7, -1 for neutral misc objects
    int house = -1;          // house id of that side (from the mission)
    int templateIndex = -1;  // index into Templates units[] / buildings[]
    int direction = 0;       // art direction frame (0 = default)
    std::uint16_t special = 0;
};

struct Scenario {
    int width = 0, height = 0;
    std::vector<std::uint16_t> tiles;  // tileset tile per cell after spice/concrete substitution
    std::vector<std::uint8_t> spice;   // 0 none, 1 thin, 2 thick
    std::vector<MapObject> objects;    // buildings first, then units, then misc (draw order)

    std::uint16_t tileAt(int x, int y) const { return tiles[static_cast<std::size_t>(y) * width + x]; }
};

Scenario buildScenario(const formats::MapFile& map, const formats::MisFile& mis,
                       const formats::Templates& templates, const formats::TileData& tileData);

// Units + buildings each side starts with in the map's special layer (same decoding as
// buildScenario; misc objects, spice and markers are not counted). Used by the "Play as" choice.
std::array<int, formats::kMaxSides> sideObjectCounts(const formats::MapFile& map, const formats::TileData& tileData);

}  // namespace d2k::game

#pragma once
// Dune 2000 .MAP files: u16 width, u16 height, then width*height cells of
// (u16 tile index into the BLOX tileset, u16 special value: spice, spawns, ...).

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::formats {

struct MapCell {
    std::uint16_t tile = 0;
    std::uint16_t special = 0;
};

struct MapFile {
    int width = 0, height = 0;
    std::vector<MapCell> cells;  // row-major
    const MapCell& at(int x, int y) const { return cells[static_cast<std::size_t>(y) * width + x]; }
};

MapFile parseMap(std::span<const std::uint8_t> data);
MapFile loadMap(const std::string& path);

}  // namespace d2k::formats

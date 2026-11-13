#pragma once
// bin/TILEATRn.BIN (6400 bytes): u32 TileBitflags[800] then u32 TileTooltips[800]
// (LoadMapData 0x44c6d0, shipping path; see re/notes/map-and-spawn.md §2.1).
// TileBitflags are the static cell flags copied into every map cell at mission setup:
// 0x2000 drive-on, 0x4000 walk-on, 0x8000 build-on, 0x10000 sand, terrain type << 29.

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace d2k::formats {

constexpr int kTileAtrEntries = 800;

struct TileAtr {
    std::array<std::uint32_t, kTileAtrEntries> flags{};
    std::array<std::int32_t, kTileAtrEntries> tooltips{};  // text id, -1 = none (file value 0)

    // Vanilla indexes TileBitflags with the raw .MAP tile word. Community maps may set
    // bits 12..15 (editor concrete/spice convention), so lookups mask to 0x0FFF; anything
    // still outside the table has no attributes.
    std::uint32_t flagsFor(std::uint16_t tile) const {
        const unsigned t = tile & 0x0FFFu;
        return t < kTileAtrEntries ? flags[t] : 0u;
    }
};

TileAtr parseTileAtr(std::span<const std::uint8_t> data);
TileAtr loadTileAtr(const std::string& path);

}  // namespace d2k::formats

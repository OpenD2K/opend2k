#pragma once
// Small fixed-size tables from data/bin/.
//   TILEDATA.BIN: 1000 x {u16 index, u8 side, u8 type} — meaning of map "special" values < 1000.
//   COLOURS.BIN : 8 houses x 16 shades of RGB555 — house colour remap for palette indexes 240..255.
// Layouts per D2kEditor (_structures.pas, _colours.pas; MIT, github.com/jkoncick/D2kEditor).

#include "formats/r16.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace d2k::formats {

enum class SpecialType : std::uint8_t {
    Unit = 0,
    MiscObject = 1,
    Explosion = 2,
    Building = 128,
    Nothing = 255,
};

struct TileDataEntry {
    std::uint16_t index = 0;  // unit group / building group / misc object
    std::uint8_t side = 0;
    SpecialType type = SpecialType::Nothing;
};

constexpr int kTileDataEntries = 1000;
using TileData = std::array<TileDataEntry, kTileDataEntries>;

TileData parseTileData(std::span<const std::uint8_t> data);
TileData loadTileData(const std::string& path);

constexpr int kHouseCount = 8;  // Atreides, Harkonnen, Ordos, Emperor, Fremen, Smugglers, Mercenaries, Sandworm
using HouseColours = std::array<std::array<Rgba, 16>, kHouseCount>;

HouseColours parseColours(std::span<const std::uint8_t> data);
HouseColours loadColours(const std::string& path);

// ARMOUR.BIN (original layout, 2702 bytes): damage % of each warhead vs each armour type.
constexpr int kOrigWarheads = 30;
constexpr int kOrigArmourTypes = 12;
struct Warhead {
    std::array<std::uint8_t, kOrigArmourTypes> versusArmour{};  // percent
    std::uint32_t radius = 0;
    std::uint32_t infantryDeath = 0;  // infantry death animation type
    std::string name;
};
struct ArmourTable {
    std::array<Warhead, kOrigWarheads> warheads{};
    std::array<std::string, kOrigArmourTypes> armourNames{};
    int warheadCount = 0, armourTypeCount = 0;
};
ArmourTable parseArmour(std::span<const std::uint8_t> data);
ArmourTable loadArmour(const std::string& path);

// SPEED.BIN (384 bytes): speed multipliers [terrain row 0..7][speed type 0..3] + speed type names.
struct SpeedTable {
    float values[8][4]{};
    std::array<std::string, 4> speedTypeNames{};
};
SpeedTable parseSpeed(std::span<const std::uint8_t> data);
SpeedTable loadSpeed(const std::string& path);

}  // namespace d2k::formats

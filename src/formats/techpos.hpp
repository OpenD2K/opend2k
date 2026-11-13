#pragma once
// bin/TECHPOS.BIN (500 bytes): skirmish/multiplayer starting units, 10 tech levels x 10 entries
// of {i8 unitGroup[3] (Atreides, Harkonnen, Ordos), i8 dx, i8 dy}; unitGroup[0] == -1 = unused.
// Read into _TechPosdata 0x6b8868; used by Setup_SpawnMapObjects 0x469790 (skirmish pass).
// Layout per D2kEditor (_structures.pas TTechposUnitEntry; MIT).

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace d2k::formats {

struct TechPosEntry {
    std::array<std::int8_t, 3> unitGroup{-1, -1, -1};
    std::int8_t dx = 0, dy = 0;
};
struct TechPos {
    std::array<std::array<TechPosEntry, 10>, 10> entries{};  // [techLevel][slot]
};

TechPos parseTechPos(std::span<const std::uint8_t> data);
TechPos loadTechPos(const std::string& path);

}  // namespace d2k::formats

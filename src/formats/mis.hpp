#pragma once
// Dune 2000 .MIS mission files. Original layout is 68066 bytes; the 1.06p+ extended format
// (D2kEditor / FunkyFr3sh event system) appends u16 event count, u16 condition count, conditions
// 48..255 and events 64..1023 (docs/extended-events.md).
// Layout cross-checked with D2kEditor (_mission.pas, MIT, github.com/jkoncick/D2kEditor).
// See docs/formats.md.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::formats {

constexpr int kMaxSides = 8;
constexpr int kMisOrigEvents = 64;
constexpr int kMisOrigConditions = 48;
constexpr std::size_t kMisAiSegmentSize = 7608;
constexpr std::size_t kMisEventSize = 72;
constexpr std::size_t kMisConditionSize = 28;
constexpr int kMisExtMaxEvents = 1024;      // MAX_EVENTS (event-core.h)
constexpr int kMisExtMaxConditions = 256;   // MAX_CONDITIONS

struct MisFile {
    std::array<std::uint8_t, kMaxSides> techLevel{};
    std::array<std::uint32_t, kMaxSides> startingMoney{};
    std::array<std::uint8_t, kMaxSides> houseId{};  // which house each side plays
    std::array<std::array<std::uint8_t, kMaxSides>, kMaxSides> allegiance{};  // 0 ally, 1 enemy, 2 neutral
    std::vector<std::array<std::uint8_t, kMisAiSegmentSize>> aiSegments;      // raw, decoded later
    std::vector<std::array<std::uint8_t, kMisEventSize>> events;              // raw, num_events used
    std::vector<std::array<std::uint8_t, kMisConditionSize>> conditions;      // raw, num_conditions used
    std::string tileset;         // e.g. "BLOXBGBS" (case as stored)
    std::string tileAttributes;  // e.g. "TILEATR3"
    std::int32_t timeLimit = 0;
    // Extended format (file longer than 68066 bytes): the real counts. The events/conditions
    // above stay the first 64 / 48 (the original slots, vanilla path); extEvents /
    // extConditions hold all `extendedEvents` / `extendedConditions` raw records (slots 0..63 /
    // 0..47 from the original area, the rest from the appended area) for the community event
    // system (game/mission_ext.cpp, docs/extended-events.md).
    bool extended = false;
    int extendedEvents = 0, extendedConditions = 0;
    std::vector<std::array<std::uint8_t, kMisEventSize>> extEvents;
    std::vector<std::array<std::uint8_t, kMisConditionSize>> extConditions;
};

MisFile parseMis(std::span<const std::uint8_t> data);
MisFile loadMis(const std::string& path);

}  // namespace d2k::formats

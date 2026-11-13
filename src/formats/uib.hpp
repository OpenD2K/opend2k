#pragma once
// .UIB string tables (data/UI_DATA/*.uib): u32 count, then count x {u16 keyLen, key bytes
// (NUL-terminated, length includes NUL), u16 valueLen, value bytes (same)}.
// Holds UI text, unit/building names, mission messages and briefings.

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2k::formats {

struct StringTable {
    std::vector<std::pair<std::string, std::string>> entries;  // file order
    std::unordered_map<std::string, std::size_t> index;        // key -> entries index

    const std::string* find(const std::string& key) const {
        auto it = index.find(key);
        return it == index.end() ? nullptr : &entries[it->second].second;
    }
    // GetTextID 0x48dc10 semantics: case-insensitive (_strcmpi), the first matching entry.
    // Templates group names ("COMBAT_TANK") are looked up this way ("Combat_Tank").
    std::unordered_map<std::string, std::size_t> lowerIndex;  // lower-case key -> entries index
    const std::string* findNoCase(const std::string& key) const;
};

StringTable parseUib(std::span<const std::uint8_t> data);
StringTable loadUib(const std::string& path);

}  // namespace d2k::formats

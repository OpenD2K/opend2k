#include "formats/uib.hpp"

#include "formats/io.hpp"

#include <cctype>

namespace d2k::formats {

namespace {
std::string lowerKey(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string readString(ByteReader& rd) {
    const std::uint16_t len = rd.u16();
    auto b = rd.bytes(len);
    std::string s(reinterpret_cast<const char*>(b.data()), b.size());
    if (const auto nul = s.find('\0'); nul != std::string::npos) s.resize(nul);
    return s;
}
}  // namespace

StringTable parseUib(std::span<const std::uint8_t> data) {
    ByteReader rd(data);
    StringTable t;
    const std::uint32_t count = rd.u32();
    if (count > 100000) throw std::runtime_error("UIB: implausible entry count");
    t.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string key = readString(rd);
        std::string value = readString(rd);
        t.index.emplace(key, t.entries.size());  // first occurrence wins
        t.lowerIndex.emplace(lowerKey(key), t.entries.size());
        t.entries.emplace_back(std::move(key), std::move(value));
    }
    return t;
}

const std::string* StringTable::findNoCase(const std::string& key) const {
    auto it = lowerIndex.find(lowerKey(key));
    return it == lowerIndex.end() ? nullptr : &entries[it->second].second;
}

StringTable loadUib(const std::string& path) { return parseUib(readFile(path)); }

}  // namespace d2k::formats

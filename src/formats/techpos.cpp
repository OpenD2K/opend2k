#include "formats/techpos.hpp"

#include "formats/io.hpp"

#include <stdexcept>

namespace d2k::formats {

TechPos parseTechPos(std::span<const std::uint8_t> data) {
    if (data.size() < 500) throw std::runtime_error("TECHPOS.BIN: file too small");
    TechPos t;
    std::size_t o = 0;
    for (auto& level : t.entries)
        for (auto& e : level) {
            for (auto& g : e.unitGroup) g = static_cast<std::int8_t>(data[o++]);
            e.dx = static_cast<std::int8_t>(data[o++]);
            e.dy = static_cast<std::int8_t>(data[o++]);
        }
    return t;
}

TechPos loadTechPos(const std::string& path) { return parseTechPos(readFile(path)); }

}  // namespace d2k::formats

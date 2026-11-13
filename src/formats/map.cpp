#include "formats/map.hpp"

#include "formats/io.hpp"

namespace d2k::formats {

MapFile parseMap(std::span<const std::uint8_t> data) {
    ByteReader rd(data);
    MapFile m;
    m.width = rd.u16();
    m.height = rd.u16();
    if (m.width <= 0 || m.height <= 0 || m.width > 512 || m.height > 512)
        throw std::runtime_error("MAP: invalid dimensions");
    m.cells.resize(static_cast<std::size_t>(m.width) * m.height);
    for (auto& c : m.cells) {
        c.tile = rd.u16();
        c.special = rd.u16();
    }
    // Some community maps carry trailing bytes after the cell grid; ignored.
    return m;
}

MapFile loadMap(const std::string& path) { return parseMap(readFile(path)); }

}  // namespace d2k::formats

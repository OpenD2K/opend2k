#include "formats/tileatr.hpp"

#include "formats/io.hpp"

namespace d2k::formats {

TileAtr parseTileAtr(std::span<const std::uint8_t> data) {
    if (data.size() < kTileAtrEntries * 8) throw std::runtime_error("TILEATR: unexpected size");
    ByteReader rd(data);
    TileAtr t;
    for (auto& f : t.flags) f = rd.u32();
    for (auto& tip : t.tooltips) {
        const std::int32_t v = rd.i32();
        tip = v == 0 ? -1 : v;  // LoadMapData stores 0 as -1
    }
    return t;
}

TileAtr loadTileAtr(const std::string& path) { return parseTileAtr(readFile(path)); }

}  // namespace d2k::formats

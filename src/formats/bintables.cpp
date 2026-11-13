#include "formats/bintables.hpp"

#include "formats/io.hpp"

#include <cstring>

namespace d2k::formats {

TileData parseTileData(std::span<const std::uint8_t> data) {
    if (data.size() != kTileDataEntries * 4) throw std::runtime_error("TILEDATA.BIN: unexpected size");
    ByteReader rd(data);
    TileData t{};
    for (auto& e : t) {
        e.index = rd.u16();
        e.side = rd.u8();
        e.type = static_cast<SpecialType>(rd.u8());
    }
    return t;
}

TileData loadTileData(const std::string& path) { return parseTileData(readFile(path)); }

HouseColours parseColours(std::span<const std::uint8_t> data) {
    if (data.size() != kHouseCount * 16 * 2) throw std::runtime_error("COLOURS.BIN: unexpected size");
    ByteReader rd(data);
    HouseColours c{};
    for (auto& house : c)
        for (auto& shade : house) shade = rgb555ToRgba(rd.u16());
    return c;
}

HouseColours loadColours(const std::string& path) { return parseColours(readFile(path)); }

namespace {
std::string fixedName(ByteReader& rd, std::size_t n) {
    auto b = rd.bytes(n);
    std::string s;
    for (auto c : b) {
        if (!c) break;
        s.push_back(static_cast<char>(c));
    }
    return s;
}
}  // namespace

ArmourTable parseArmour(std::span<const std::uint8_t> data) {
    // The game reads the first 2702 bytes; some community files carry extra bytes after them
    // (War of the Landsraad: 2750; D2kEditor's extended layout appends more warheads).
    if (data.size() < 2702) throw std::runtime_error("ARMOUR.BIN: unexpected size");
    ByteReader rd(data);
    ArmourTable t;
    for (auto& w : t.warheads) {
        for (auto& v : w.versusArmour) v = rd.u8();
        w.radius = rd.u32();
        w.infantryDeath = rd.u32();
    }
    for (auto& w : t.warheads) w.name = fixedName(rd, 50);
    for (auto& n : t.armourNames) n = fixedName(rd, 50);
    t.warheadCount = rd.u8();
    t.armourTypeCount = rd.u8();
    return t;
}

ArmourTable loadArmour(const std::string& path) { return parseArmour(readFile(path)); }

SpeedTable parseSpeed(std::span<const std::uint8_t> data) {
    if (data.size() != 384) throw std::runtime_error("SPEED.BIN: unexpected size");
    ByteReader rd(data);
    SpeedTable t;
    for (auto& row : t.values)
        for (auto& v : row) {
            const std::uint32_t bits = rd.u32();
            std::memcpy(&v, &bits, sizeof v);
        }
    for (auto& n : t.speedTypeNames) n = fixedName(rd, 32);
    return t;
}

SpeedTable loadSpeed(const std::string& path) { return parseSpeed(readFile(path)); }

}  // namespace d2k::formats

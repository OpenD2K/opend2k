#include "formats/vars.hpp"

#include "formats/io.hpp"

#include <stdexcept>

namespace d2k::formats {

namespace {
std::uint32_t u32(std::span<const std::uint8_t> d, std::size_t o) {
    return static_cast<std::uint32_t>(d[o]) | static_cast<std::uint32_t>(d[o + 1]) << 8 |
           static_cast<std::uint32_t>(d[o + 2]) << 16 | static_cast<std::uint32_t>(d[o + 3]) << 24;
}
}  // namespace

Vars parseVars(std::span<const std::uint8_t> d) {
    if (d.size() < 0x4e) throw std::runtime_error("VARS.BIN too short");
    Vars v;
    v.harvestUnloadDelay = u32(d, 0x00);
    v.harvestBlobValue = u32(d, 0x04);
    v.harvestLoadSpiceDelay = u32(d, 0x08);
    v.starportUpdateDelay = u32(d, 0x0c);
    v.starportStockIncreaseDelay = u32(d, 0x10);
    v.starportStockIncreaseProb = u32(d, 0x14);
    v.starportCostVariationPercent = u32(d, 0x18);
    v.starportFrigateDelay = u32(d, 0x1c);
    v.refineryExplosionOffsetX = static_cast<std::int32_t>(u32(d, 0x20));
    v.refineryExplosionOffsetY = static_cast<std::int32_t>(u32(d, 0x24));
    v.harvesterDriveDistance = u32(d, 0x28);
    v.repairDriveDistance = u32(d, 0x2c);
    v.buildingRepairValue = u32(d, 0x30);
    v.unitRepairValue = u32(d, 0x34);
    v.singlePlayerDelay = u32(d, 0x38);
    v.numberOfFremen = d[0x3c];
    v.sandWormAppetite = d[0x3d];
    v.sandWormInitialSleep = d[0x3e];
    v.sandWormFedSleep = d[0x3f];
    v.sandWormShotSleep = d[0x40];
    v.numberOfCrates = d[0x41];
    v.cratesPerPlayer = d[0x42];
    v.devastatorExplodeDelay = d[0x43];
    v.ignoreDistance = u32(d, 0x44);
    v.crateCash = u32(d, 0x48);
    v.showWarnings = d[0x4c];
    v.deathHandAccuracy = d[0x4d];
    return v;
}

Vars loadVars(const std::string& path) {
    const auto data = readFile(path);
    return parseVars(data);
}

}  // namespace d2k::formats

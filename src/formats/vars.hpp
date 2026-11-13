#pragma once
// bin/VARS.BIN (80 bytes): the game-rule variables block read by Mission__LoadVarsFile 0x4544e0
// straight into the globals 0x6b8818..0x6b8867 (harvestUnloadDelay first). The defaults below are
// the values of the shipped VARS.BIN (the debug "vars.dat" path has different built-in defaults).

#include <cstdint>
#include <span>
#include <string>

namespace d2k::formats {

struct Vars {
    std::uint32_t harvestUnloadDelay = 60;            // 0x00 @0x6b8818 ticks per spice blob unloaded
    std::uint32_t harvestBlobValue = 100;             // 0x04 @0x6b881c spice per blob
    std::uint32_t harvestLoadSpiceDelay = 60;         // 0x08 @0x6b8820 ticks per blob loaded
    std::uint32_t starportUpdateDelay = 1500;         // 0x0c
    std::uint32_t starportStockIncreaseDelay = 1500;  // 0x10
    std::uint32_t starportStockIncreaseProb = 10;     // 0x14
    std::uint32_t starportCostVariationPercent = 25;  // 0x18
    std::uint32_t starportFrigateDelay = 1500;        // 0x1c
    std::int32_t refineryExplosionOffsetX = 62;       // 0x20
    std::int32_t refineryExplosionOffsetY = -79;      // 0x24
    std::uint32_t harvesterDriveDistance = 64;        // 0x28 @0x6b8840 (tiles squared)
    std::uint32_t repairDriveDistance = 64;           // 0x2c
    std::uint32_t buildingRepairValue = 12;           // 0x30 hit points per repair step
    std::uint32_t unitRepairValue = 8;                // 0x34
    std::uint32_t singlePlayerDelay = 2;              // 0x38
    std::uint8_t numberOfFremen = 2;                  // 0x3c
    std::uint8_t sandWormAppetite = 3;                // 0x3d
    std::uint8_t sandWormInitialSleep = 50;           // 0x3e
    std::uint8_t sandWormFedSleep = 40;               // 0x3f
    std::uint8_t sandWormShotSleep = 90;              // 0x40
    std::uint8_t numberOfCrates = 1;                  // 0x41
    std::uint8_t cratesPerPlayer = 0;                 // 0x42
    std::uint8_t devastatorExplodeDelay = 100;        // 0x43
    std::uint32_t ignoreDistance = 2;                 // 0x44
    std::uint32_t crateCash = 1000;                   // 0x48
    std::uint8_t showWarnings = 1;                    // 0x4c
    std::uint8_t deathHandAccuracy = 3;               // 0x4d
};

constexpr std::size_t kVarsFileSize = 0x50;

Vars parseVars(std::span<const std::uint8_t> data);  // throws if shorter than 0x4e bytes
Vars loadVars(const std::string& path);

}  // namespace d2k::formats

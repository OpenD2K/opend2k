#include "formats/templates.hpp"

#include "formats/io.hpp"

#include <bit>
#include <cstring>

namespace d2k::formats {

static_assert(std::endian::native == std::endian::little,
              "Templates.bin is loaded by memcpy and requires a little-endian host");

namespace {

// Mirrors TStructures.compute_image_indexes (D2kEditor): DATA.R16 is laid out as
// technical entries, then unit art, building art, projectiles, animations, icons,
// build-up art and building animations, each sized from the counts in Templates.bin.
void computeImageIndexes(Templates& t) {
    const auto& b = t.bin;
    int index = kNumTechnicalGraphicalEntries;
    t.unitArtFirstFrame.assign(kMaxUnitArt, index);
    for (int i = 0; i < b.unitArtCount && i < kMaxUnitArt; ++i) {
        t.unitArtFirstFrame[i] = index;
        index += b.unitArtAnimationFrames[i] * b.unitArtDirectionFrames[i];
    }
    t.buildingArtFirstFrame.assign(kMaxBuildingArt, index);
    for (int i = 0; i < b.buildingArtCount && i < kMaxBuildingArt; ++i) {
        t.buildingArtFirstFrame[i] = index;
        index += b.buildingArtDirections[i] * 2 + 1;
    }
    t.projectileArtFirstFrame.assign(kMaxWeapons, index);
    for (int i = 0; i < b.projectileArtCount && i < kMaxWeapons; ++i) {
        t.projectileArtFirstFrame[i] = index;
        index += b.projectileArtDirections[i];
    }
    t.animationArtFirstFrame.assign(kMaxExplosions, index);
    for (int i = 0; i < b.animationArtCount && i < static_cast<int>(t.animationArtFrames.size()); ++i) {
        t.animationArtFirstFrame[i] = index;
        index += t.animationArtFrames[i];
    }
    t.firstUnitIconFrame = index;
    index += b.unitCount + kNumEmptyUnitSidebarIcons;
    t.firstBuildingIconFrame = index;
    index += b.buildingCount + 1;
    t.buildupArtFirstFrame.assign(kMaxBuildingTypes, 0);
    for (int i = 0; i < b.buildingCount; ++i) {
        t.buildupArtFirstFrame[i] = index;
        index += b.buildupArtFrames[i];
    }
    t.buildingAnimationFirstFrame.assign(kMaxBuildingTypes, 0);
    for (int i = 0; i < b.buildingCount; ++i) {
        t.buildingAnimationFirstFrame[i] = index;
        index += b.buildingAnimationFrames[i];
    }
}

// Mirrors compute_building_and_unit_house_versions: for each group and house pick the
// template whose OwnerHouse bit matches, falling back to the first of that group.
void computeHouseVersions(Templates& t) {
    const auto& b = t.bin;
    for (int g = 0; g < kMaxUnitTypes; ++g)
        for (int h = 0; h < 8; ++h) {
            t.unitHouseVersion[g][h] = -1;
            for (int i = 0; i < b.unitCount; ++i) {
                if (b.units[i].unitGroup != g) continue;
                if (t.unitHouseVersion[g][h] == -1) t.unitHouseVersion[g][h] = i;
                if (b.units[i].ownerHouse & (1 << h)) t.unitHouseVersion[g][h] = i;
            }
        }
    for (int g = 0; g < kMaxBuildingTypes; ++g)
        for (int h = 0; h < 8; ++h) {
            t.buildingHouseVersion[g][h] = -1;
            for (int i = 0; i < b.buildingCount; ++i) {
                if (b.buildings[i].buildingGroup != g) continue;
                if (t.buildingHouseVersion[g][h] == -1) t.buildingHouseVersion[g][h] = i;
                if (b.buildings[i].ownerHouse & (1 << h)) t.buildingHouseVersion[g][h] = i;
            }
        }
}

}  // namespace

const UnitTemplate* Templates::unitFor(int group, int house) const {
    if (group < 0 || group >= kMaxUnitTypes || house < 0 || house >= 8) return nullptr;
    const int i = unitHouseVersion[group][house];
    return i < 0 ? nullptr : &bin.units[i];
}

const BuildingTemplate* Templates::buildingFor(int group, int house) const {
    if (group < 0 || group >= kMaxBuildingTypes || house < 0 || house >= 8) return nullptr;
    const int i = buildingHouseVersion[group][house];
    return i < 0 ? nullptr : &bin.buildings[i];
}

Templates parseTemplates(std::span<const std::uint8_t> data) {
    constexpr std::size_t kExtSize =
        (kMaxExplosions - kMaxOrigExplosions) * (sizeof(ExplosionTemplate) + 50 + 4 + 4);
    const bool extended = data.size() == sizeof(TemplatesBin) + kExtSize;
    if (data.size() != sizeof(TemplatesBin) && !extended)
        throw std::runtime_error("Templates.bin: unexpected size " + std::to_string(data.size()));
    Templates t;
    std::memcpy(&t.bin, data.data(), sizeof(TemplatesBin));

    const auto& b = t.bin;
    for (int i = 0; i < kMaxOrigExplosions; ++i) {
        t.explosions.push_back(b.explosions[i]);
        t.explosionNames.emplace_back(b.explosionNames[i], strnlen(b.explosionNames[i], 50));
        t.animationArtFrames.push_back(b.animationArtFrames[i]);
        t.animationArtFlags.push_back(b.animationArtFlags[i]);
    }
    if (extended) {
        // Extension block order (D2kEditor load_templates_bin): definitions, names, frames, flags.
        ByteReader rd(data.subspan(sizeof(TemplatesBin)));
        constexpr int n = kMaxExplosions - kMaxOrigExplosions;
        for (int i = 0; i < n; ++i) {
            ExplosionTemplate e;
            std::memcpy(&e, rd.bytes(sizeof e).data(), sizeof e);
            t.explosions.push_back(e);
        }
        for (int i = 0; i < n; ++i) {
            auto s = rd.bytes(50);
            const char* c = reinterpret_cast<const char*>(s.data());
            t.explosionNames.emplace_back(c, strnlen(c, 50));
        }
        for (int i = 0; i < n; ++i) t.animationArtFrames.push_back(rd.i32());
        for (int i = 0; i < n; ++i) t.animationArtFlags.push_back(rd.u32());
    }
    computeImageIndexes(t);
    computeHouseVersions(t);
    return t;
}

Templates loadTemplates(const std::string& path) { return parseTemplates(readFile(path)); }

}  // namespace d2k::formats

#pragma once
// bin/Templates.bin — unit, building, weapon and explosion definitions plus art tables.
// Layout mirrors D2kEditor's TTemplatesBinFile (_structures.pas, MIT,
// github.com/jkoncick/D2kEditor); the "mod" fields are Gruntmods extensions that
// are zero in the original data. All structs are packed little-endian and are
// loaded by memcpy (static_asserts pin the sizes to the file layout).

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2k::formats {

constexpr int kMaxUnitTypes = 60;
constexpr int kMaxBuildingTypes = 100;
constexpr int kMaxWeapons = 64;
constexpr int kMaxOrigExplosions = 64;   // stored in the fixed file block
constexpr int kMaxExplosions = 128;      // with the optional extension block appended
constexpr int kMaxUnitArt = 90;
constexpr int kMaxBuildingArt = 120;

// First DATA.R16 frame used by unit/building art (earlier frames are UI/technical).
constexpr int kNumTechnicalGraphicalEntries = 206;
constexpr int kNumEmptyUnitSidebarIcons = 10;

#pragma pack(push, 1)
struct UnitTemplate {
    std::uint8_t ownerHouse;  // bitmask of houses that own this version
    std::uint8_t unitGroup;
    std::uint8_t armorType;
    std::uint8_t unitRotationSpeed;
    std::uint32_t hitPoints;
    std::uint32_t speed;
    std::int8_t primaryWeapon;
    std::int8_t secondaryWeapon;
    std::uint8_t primaryWeaponLongDelay;
    std::uint8_t barrelRotationSpeed;
    std::uint8_t sightRadius;
    std::uint8_t isInfantry;
    std::uint8_t useName;
    std::uint8_t _pad19;
    std::int32_t unitArt;
    std::int32_t barrelArt;
    std::uint32_t cost;
    std::uint32_t buildSpeed;
    std::int8_t techLevel;
    std::uint8_t availableInStarport;
    std::uint8_t primaryWeaponUseBarrel;
    std::uint8_t prereq1UpgradesNeeded;
    std::int32_t prereq1BuildingGroup;
    std::uint8_t prereq1OwnerHouse;
    std::uint8_t specialBehavior;
    std::uint8_t _unused46;
    std::int8_t deathExplosion;
    std::int32_t prereq2BuildingGroup;
    std::uint8_t _unused52;
    std::uint8_t canCrushInfantry;
    std::uint8_t healthBarSize;
    std::int8_t primaryWeaponShootOffset;
    std::uint32_t flags;
    std::uint8_t directionFrames[32];
    std::uint32_t voices[18];
    std::uint32_t voicePriority;
    std::int8_t muzzleFlashExplosion;
    std::uint8_t speedType;
    std::uint8_t multiplayerOnly;
    std::uint8_t modExtra[17];  // Gruntmods extension fields (zero in original data)
    std::uint32_t customDeathSound;
    std::uint8_t notEdible;
    std::uint8_t storageCapacity;
    std::int8_t primaryWeaponShootYOffset;
    std::int8_t secondaryWeaponShootYOffset;
    std::uint8_t secondaryWeaponUseBarrel;
    std::uint8_t weaponPriority;
    std::uint8_t _zero[58];
};

struct BuildingTemplate {
    std::uint32_t hitPoints;
    std::uint8_t ownerHouse;
    std::uint8_t armorType;
    std::uint8_t barrelRotationSpeed;
    std::uint8_t primaryWeaponLongDelay;
    std::uint32_t screenShake;
    std::int8_t primaryWeapon;
    std::int8_t secondaryWeapon;
    std::uint8_t sightRadius;
    std::uint8_t actLikeTurret;
    std::int8_t techLevelBuild;
    std::int8_t techLevelUpgrade[3];
    std::int32_t buildingArt;
    std::int32_t barrelArt;
    std::uint32_t costBuild;
    std::uint32_t costUpgrade[3];
    std::uint32_t buildSpeedBuild;
    std::uint32_t buildSpeedUpgrade[3];
    std::int32_t powerConsumption;
    std::int32_t prereq1BuildingGroup;
    std::uint8_t prereq1OwnerHouse;
    std::uint8_t prereq1UpgradesNeeded;
    std::uint8_t useName;
    std::uint8_t _pad71;
    std::int32_t prereq2BuildingGroup;
    std::uint8_t prereq2OwnerHouse;
    std::uint8_t prereq2UpgradesNeeded;
    std::uint8_t requireEnoughPower;
    std::int8_t deathExplosion;
    std::uint32_t tilesOccupiedAll;    // 4x4 bitmask, bit (y*4+x)
    std::uint32_t tilesOccupiedSolid;
    std::uint32_t flags;
    std::uint8_t specialBehavior;
    std::uint8_t sellPriority;
    std::uint8_t _unused94;
    std::uint8_t healthBarSize;
    std::int8_t exitPoint1X, exitPoint1Y, exitPoint2X, exitPoint2Y;
    std::uint8_t directionFrames[32];
    std::uint8_t _unused132;
    std::uint8_t animationSpeed;
    std::uint8_t artHeight;
    std::uint8_t artWidth;
    std::uint8_t buildingGroup;
    std::uint8_t buildupFramesToShow;
    std::uint8_t buildupArt;
    std::uint8_t buildingAnimation;
    std::int8_t muzzleFlashExplosion;
    std::uint8_t modExtra[15];  // Gruntmods extension fields incl. padding
    std::uint32_t storageCapacity;
    std::uint8_t _zero[108];
};

struct WeaponTemplate {
    std::uint32_t projectileSpeed;
    std::uint32_t damage;
    std::uint32_t flags;
    std::int32_t firingSound;
    std::uint8_t projectileArt;
    std::int8_t hitExplosion;
    std::int8_t trailExplosion;
    std::uint8_t _unknown19;
    std::uint8_t antiAircraft;
    std::uint8_t warhead;
    std::uint8_t inaccuracy;
    std::uint8_t behavior;
    std::uint32_t range;
};

struct ExplosionTemplate {
    std::uint8_t myIndex;
    std::uint8_t muzzleFlashPattern;
    std::uint8_t repeatCount;
    std::uint8_t animationDelay;
    std::int32_t sound;
};

struct TemplatesBin {
    UnitTemplate units[kMaxUnitTypes];
    BuildingTemplate buildings[kMaxBuildingTypes];
    WeaponTemplate weapons[kMaxWeapons];
    ExplosionTemplate explosions[kMaxOrigExplosions];
    std::int32_t unitArtAnimationFrames[kMaxUnitArt];
    std::int32_t unitArtDirectionFrames[kMaxUnitArt];
    std::int32_t unitArtCount;
    std::uint8_t unitCount;
    std::int32_t animationArtCount;
    std::int32_t animationArtFrames[kMaxOrigExplosions];
    std::uint8_t explosionCount;
    std::int32_t weaponCount;
    std::int32_t projectileArtDirections[kMaxWeapons];
    std::uint8_t projectileArtCount;
    std::int32_t buildingArtCount;
    std::int32_t buildingArtDirections[kMaxBuildingArt];
    std::uint8_t buildingCount;
    char buildingNames[kMaxBuildingTypes][450];
    char weaponNames[kMaxWeapons][50];
    char explosionNames[kMaxOrigExplosions][50];
    char unitNames[kMaxUnitTypes][450];
    char unitGroupNames[kMaxUnitTypes][50];
    char buildingGroupNames[kMaxBuildingTypes][50];
    std::int8_t groupIds[86];
    std::uint32_t animationArtFlags[kMaxOrigExplosions];
    std::uint8_t buildingAnimationFrames[kMaxBuildingTypes];
    std::uint8_t buildupArtFrames[kMaxBuildingTypes];
    std::uint8_t buildingGroupCount;
    std::uint8_t unitGroupCount;
};
#pragma pack(pop)

static_assert(sizeof(UnitTemplate) == 256, "UnitTemplate layout");
static_assert(sizeof(WeaponTemplate) == 28, "WeaponTemplate layout");
static_assert(sizeof(ExplosionTemplate) == 8, "ExplosionTemplate layout");
static_assert(sizeof(BuildingTemplate) == 268, "BuildingTemplate layout");
static_assert(sizeof(TemplatesBin) == 133140, "Templates.bin original layout");

// Templates.bin plus derived lookup tables (DATA.R16 frame indexes, house versions).
struct Templates {
    TemplatesBin bin{};
    // Explosion/animation tables merged with the optional extension block (up to 128).
    std::vector<ExplosionTemplate> explosions;
    std::vector<std::string> explosionNames;
    std::vector<std::int32_t> animationArtFrames;
    std::vector<std::uint32_t> animationArtFlags;
    std::vector<int> unitArtFirstFrame;       // [unitArt] -> DATA.R16 frame
    std::vector<int> buildingArtFirstFrame;   // [buildingArt] -> DATA.R16 frame
    std::vector<int> projectileArtFirstFrame;
    std::vector<int> animationArtFirstFrame;
    int firstUnitIconFrame = 0, firstBuildingIconFrame = 0;
    // Indexed by BuildingTemplate::buildupArt / ::buildingAnimation (shared art tables), not by building.
    std::vector<int> buildupArtFirstFrame;
    std::vector<int> buildingAnimationFirstFrame;
    int unitHouseVersion[kMaxUnitTypes][8];         // [unitGroup][house] -> unit index, -1 none
    int buildingHouseVersion[kMaxBuildingTypes][8]; // [buildingGroup][house] -> building index

    const UnitTemplate* unitFor(int group, int house) const;
    const BuildingTemplate* buildingFor(int group, int house) const;
    std::string unitName(int index) const { return bin.unitNames[index]; }
    std::string buildingName(int index) const { return bin.buildingNames[index]; }
};

Templates parseTemplates(std::span<const std::uint8_t> data);
Templates loadTemplates(const std::string& path);

}  // namespace d2k::formats

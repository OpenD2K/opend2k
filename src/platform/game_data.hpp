#pragma once
// Loading all original data one game session needs (map, mission, tileset, sprites, rules
// tables, font, strings), for a campaign mission or a skirmish map.

#include "formats/bintables.hpp"
#include "formats/font.hpp"
#include "formats/map.hpp"
#include "formats/mission_ini.hpp"
#include "formats/mis.hpp"
#include "formats/r16.hpp"
#include "formats/techpos.hpp"
#include "formats/templates.hpp"
#include "formats/tileatr.hpp"
#include "formats/uib.hpp"
#include "formats/vars.hpp"
#include "game/scenario.hpp"
#include "game/world.hpp"
#include "render/mod_art.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace d2k::platform {

// Lobby values for a skirmish start (mapped onto game::SkirmishSetup).
struct SkirmishOptions {
    int playerHouse = 0;  // 0 Atreides, 1 Harkonnen, 2 Ordos
    int aiPlayers = 1;
    int techLevel = 7;
    std::uint32_t startingCredits = 5000;
    int unitCount = 10;
    int worms = 1;
    int difficulty = 1;   // 0 easy, 1 normal, 2 hard (AI starting credits 7000 / 10500 / 14000;
                          // campaign games: gDifficultyLevel for the world too)
    bool crates = false;  // gNetCrates
    int playerColor = 0;  // gNetPlayerColour 0..7
};

// What to start: a map plus how (campaign mission rules or a skirmish lobby).
struct GameStart {
    std::string map;      // e.g. "A1V1.MAP" or "4PLAY1.MAP" (searched in maps/ then Missions/)
    std::string tileset;  // --tileset override (empty = from the mission / MP map table)
    bool skirmish = false;
    SkirmishOptions sk;
    std::string title;    // shown in the pause menu
    // Where the menu returns after the game (campaign house 0..2 and mission index, or -1).
    int campaignHouse = -1, missionIndex = -1;
    std::string briefing;  // custom missions: the briefing text (in-game briefing page)
    std::string communityCampaign;  // community campaign id (campaignHouse kCommunityHouse)
    // "Play as any side" (not original; docs/play-as.md): the side the human plays in a mission
    // (campaign / custom / community); -1 = the mission's own side (SideId). Chosen per start.
    int playAsSide = -1;
};

// A side the player can take in a mission ("Play as"): sides that start with units or
// buildings on the map (the sandworm house excluded), the mission's own side first.
struct PlayAsOption {
    int side = 0;
    int house = 0;       // .MIS house of the side (0..7)
    int objects = 0;     // units + buildings on the map
    bool original = false;  // the mission's own side (SideId)
};
// Reads the mission's .ini, .MAP, .MIS and bin/TILEDATA.BIN; empty when something is missing or
// for skirmish maps. `map` as in GameStart::map.
std::vector<PlayAsOption> playAsOptions(const std::string& dataDir, const std::string& map);
// "Atreides", "Harkonnen", ..., "Sandworm" (bin/COLOURS order); "Side N" past the table.
const char* houseName(int house);

// All original data needed to show a scenario.
struct GameData {
    std::string dataDir;
    formats::R16File tiles;
    formats::R16File sprites;
    formats::Templates templates;
    formats::TileData tileData;
    formats::HouseColours colours;
    formats::SpeedTable speed;
    formats::ArmourTable armour;
    formats::MisFile mission;
    formats::Font font;
    formats::StringTable text;   // UI_DATA/Text.UIB (mission messages by index)
    formats::R16File ui;         // UI_ENG.R16 (banners, sidebar buttons, power bar, radar logos)
    // Original in-game UI (Graphics: Original; re/notes/ui.md). Empty when the files are missing.
    std::vector<std::uint8_t> uibb;    // UIBB.R16: 640x400 RGB555 backdrop of the in-game frame
    formats::Font fontCol;             // FONTCOL.FNT (font 1: timer, messages, Ready/Hold)
    formats::FontPalette fontColPalette{};  // FONTCOL.FPL
    bool haveFontCol = false;
    formats::MapFile map;
    formats::TileAtr tileAtr;
    formats::TechPos techPos;    // bin/TECHPOS.BIN (skirmish starting units)
    bool haveTechPos = false;
    game::Scenario scenario;
    std::string tilesetName;
    game::BuildingArtSizes buildingArt;  // frame sizes the model reads (see game/world.hpp)
    std::vector<std::uint8_t> builExp;   // bin/BUILEXP.BIN: building damage smoke points
    formats::Vars vars;                  // bin/VARS.BIN: game-rule variables
    std::vector<std::uint8_t> circles;   // bin/CIRCLES.BIN: shroud reveal masks
    int playerSide = 0;                  // Missions/<map>.ini SideId (Gruntmods launcher data); 0 in skirmish;
                                         // the chosen side with GameStart::playAsSide
    int missionSide = 0;                 // the mission's own side (SideId), = playerSide unless playing as another
    bool playAs() const { return !start.skirmish && missionSide != playerSide; }
    // Mission music (re/notes/audio.md): the first track (Music/<track>.AUD) and whether the
    // campaign rule applies (GameType 0: a random house track after it ends; else it loops).
    std::string musicTrack;
    bool campaignMusic = false;
    GameStart start;
    // Community missions (Missions/<stem>.ini; docs/community-campaigns.md).
    std::string modsDataDir;                 // CustomCampaignData/<folder>/<mods>/data, or ""
    game::RulesExt rulesExt;                 // [Vars] patch rules the model implements
    std::map<int, std::string> missionText;  // [Text]: message strings replacing the table's
    std::array<std::string, 8> sideNames{};  // [Sides] / [Data] PlayersFile= names ("" = default)
    // What the mods folder's DATA.R16 / Templates.bin / tileset change in the art (render/mod_art.hpp):
    // those frames / types / tiles never take art-pack frames or 3D models. Inactive for vanilla data.
    render::ModArt modArt;
};

// Throws std::runtime_error with a readable message when something required is missing.
GameData loadGameData(const std::string& dataDir, const GameStart& start);

game::Rules rulesOf(const GameData& g);
// Applies a [Vars] section (rules.ini or a mission .ini) to the VARS.BIN variables and the
// patch rules; `missionFile` = the mission's own .ini (InfiniteSpice is only read from it).
void applyMissionVars(const formats::IniSection& vars, formats::Vars& v, game::RulesExt& ext, bool missionFile);
game::SkirmishSetup skirmishSetupOf(const GameData& g);

// The simulation rate depends on the GameSpeed setting (Settings::gameTicksPerSecond,
// re/notes/timing.md). Game-time displays use the original's 25 ticks per "second"
// (mission timer: ticks / 1500 minutes, ticks / 25 % 60 seconds in FUN_004255e0).
constexpr int kTicksPerGameSecond = 25;

}  // namespace d2k::platform

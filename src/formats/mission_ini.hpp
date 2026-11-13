#pragma once
// Mission .ini files next to a map in data/Missions (community-patch / Gruntmods launcher
// extension, docs/community-campaigns.md): [Basic] name, author, music, player side, briefing,
// text table; [Data] the CustomCampaignData links (campaign folder, mods folder, colours file,
// players file, intel id); [Vars] rule overrides; [Text] message strings by index; [Sides]
// side names. Key names are case-insensitive (GetPrivateProfileString).
// Field list cross-checked with D2kEditor _missionini.pas (MIT) and the launcher's strings.

#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace d2k::formats {

// Minimal INI reader: lower-case section name -> (key, value) list in file order. Lines are
// trimmed; ';' / '#' start comments; bytes are kept as they are (Latin-1 in these files).
using IniSection = std::vector<std::pair<std::string, std::string>>;
using IniFile = std::map<std::string, IniSection>;
IniFile parseIni(const std::string& text);
IniFile readIni(const std::string& path);  // empty when the file is missing
std::string iniGet(const IniFile& ini, const std::string& section, const std::string& key, const std::string& def = {});
const IniSection* iniSection(const IniFile& ini, const std::string& section);

struct MissionIni {
    bool present = false;  // the file existed
    // [Basic]
    std::string name, author, music;
    int sideId = -1;        // -1 = not set
    int missionNumber = 0;  // >0: the game uses IG<side><n>MIS.UIL for the in-game briefing
    std::string textUib;              // replacement for UI_DATA/Text.UIB (mods folder UI_DATA first)
    std::string textUibBriefingKey;   // briefing entry in that table
    std::string briefing;             // raw Briefing= ('_' = line break)
    // [Data] (MissionLauncher 1.2+: CustomCampaignData/<campaignFolder>/...)
    std::string campaignFolder, modsFolder, coloursFile, playersFile, intelId;
    IniSection vars;                  // [Vars] as written
    std::map<int, std::string> text;  // [Text] message index -> text
    std::array<std::string, 8> sides; // [Sides] side names (editor data; "" = default)
    // Code page of the text values above (bytes are kept as in the file): CP1252 unless the file
    // is UTF-8 (lang::kUtf8) or Russian (1251) — formats/language.hpp detectCodepage.
    int codepage = 1252;
};

MissionIni parseMissionIni(const IniFile& ini);
MissionIni loadMissionIni(const std::string& path);

// "Briefing=" text: '_' is a line break (the launcher / in-game-briefing.c replace it with the
// font's 0xAC line break). Returns '\n'-separated text.
std::string missionBriefingText(const std::string& raw);

// Paths in the game folder (the parent of the data folder). Empty when missing.
//   CustomCampaignData/<campaignFolder>
std::string customCampaignDir(const std::string& dataDir, const std::string& campaignFolder);
//   CustomCampaignData/<campaignFolder>/<modsFolder>/data: files that replace data/... for the
//   mission (the launcher copies them over data/ before starting the game).
std::string missionModsDataDir(const std::string& dataDir, const MissionIni& ini);
// Overlay lookup: <modsData>/<sub>/<name> if it exists, else <dataDir>/<sub>/<name>
// (case-insensitive; sub may be "" or e.g. "bin", "UI_DATA").
std::string findOverlayFile(const std::string& modsData, const std::string& dataDir, const std::string& sub,
                            const std::string& name);

// Players/<file>.ini: [Player0..7] name= / short= (side display names), as UTF-8.
std::array<std::string, 8> loadPlayersFile(const std::string& path);

}  // namespace d2k::formats

#pragma once
// What the menus can offer, read from the data folder: the original campaign missions
// (Missions/[AHO]<n>V<v>.ini), other (custom/Gruntmods) missions, the community campaigns
// built from them (formats/community.hpp) and the skirmish maps (maps/MultiplayerMaps.ini
// [Maps]). Briefings come from UI_DATA/Text.UIB or the mission's own TextUib / Briefing=.
// Names, authors, briefings and intel texts are UTF-8 (converted from each file's code page:
// formats/language.hpp "per-source code pages").

#include "formats/community.hpp"
#include "formats/uib.hpp"

#include <string>
#include <vector>

namespace d2k::platform {

struct MissionInfo {
    std::string map;        // "A1V1.MAP" (file name as found)
    std::string name;       // [Basic] Name=, e.g. "Original Campaign - Atreides Mission 1a"
    std::string shortName;  // "Mission 1a"
    std::string author;
    std::string briefing;   // plain text, '\n' separated paragraphs
    int side = 0;           // SideId
    int house = 0;          // 0 Atreides, 1 Harkonnen, 2 Ordos (campaign); SideId-derived otherwise
    int number = 0;         // MissionNumber
    // Community missions (docs/community-campaigns.md).
    std::string stem;            // map stem
    std::string campaignFolder;  // [Data] CustomCampaignData folder
    std::string modsFolder;      // [Data] replacement data files (game_data overlay)
    std::string intelId;         // [Data] Intel/Entries/<id>_<n>.txt
    std::vector<std::string> intel;  // those intel texts (up to 4)
    bool extendedMis = false;    // 1.06p+ extended .MIS (event system not supported yet)
    int extendedEvents = 0;      // its event count
};

struct SkirmishMap {
    std::string stem;  // "4PLAY1"
    std::string name;  // "The Minor Erg (MED 2-4 player)"
    std::string file;  // file name in maps/
    int maxPlayers = 8;
};

struct Catalog {
    std::vector<MissionInfo> campaign[3];  // per house, in mission order (1a, 1b, 2a, ...)
    std::vector<MissionInfo> custom;       // everything else with a .MAP + .MIS, sorted by name
    std::vector<SkirmishMap> skirmish;     // MultiplayerMaps.ini order (existing maps only)
    // Community campaigns; their step mission indices point into `custom`.
    std::vector<formats::CommunityCampaign> community;
    formats::StringTable text;             // Text.UIB

    static Catalog load(const std::string& dataDir);
};

extern const char* const kHouseNames[3];

}  // namespace d2k::platform

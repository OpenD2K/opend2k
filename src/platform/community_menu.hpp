#pragma once
// Shared front-end logic for the community campaigns (formats/community.hpp) used by both menu
// sets (Enhanced: menu_community.cpp, Original: original_community.cpp): the rows of a
// campaign's mission list with their state, the progress file, and recording a win.
// `translated`: the Enhanced pages pass true to get our texts in the UI language (render::tr,
// UTF-8); the Original pages keep the English (their bitmap font cannot draw UTF-8).

#include "formats/community.hpp"
#include "platform/catalog.hpp"

#include <string>
#include <vector>

namespace d2k::platform {

// GameStart::campaignHouse of a game started from a community campaign (custom missions use 3).
constexpr int kCommunityHouse = 4;

struct CommunityRow {
    int step = -1;      // index into CommunityCampaign::steps
    int mission = -1;   // index into Catalog::custom
    std::string text;   // "Mission 3 (Option 1)" / "S03V1: Double Bind"
    std::string status; // "Won", "Locked", "Bonus", "Bonus - won", "" (open)
    bool locked = false, won = false, bonus = false;
};

std::vector<CommunityRow> communityRows(const Catalog& cat, const formats::CommunityCampaign& c,
                                        const formats::CommunityProgress& p, bool translated = false);
// Index into rows of the mission to preselect (the next one to play).
int communitySuggestedRow(const std::vector<CommunityRow>& rows, const formats::CommunityCampaign& c,
                          const formats::CommunityProgress& p);
// "3 / 15 missions won" (bonus missions not counted).
std::string communityProgressText(const formats::CommunityCampaign& c, const formats::CommunityProgress& p,
                                  bool translated = false);
// Description lines for a campaign (author, size, overrides, warnings).
std::vector<std::string> communityCampaignInfo(const Catalog& cat, const formats::CommunityCampaign& c,
                                               bool translated = false);
// Briefing plus the mission's intel entries and a note on unsupported features.
std::string communityBriefingText(const MissionInfo& m, bool translated = false);

// A string for the modern pages' TrueType font: kept when it is valid UTF-8, else (a game string
// in the active code page, e.g. "Coup de Gr\xe2ce") converted, so it can be joined with our UTF-8
// tr() texts (a mixed string would be read as code-page bytes and garble the UTF-8 part).
std::string uiUtf8(const std::string& s);

// <settings folder>/community_progress.ini ("" when there is no settings folder).
std::string communityProgressPath();
// Marks `stem` won in every campaign that contains it.
void recordCommunityWin(const Catalog& cat, formats::CommunityProgress& p, const std::string& stem);

}  // namespace d2k::platform

#pragma once
// Community (Gruntmods) campaigns: which missions in data/Missions form a campaign, in which
// order, with which alternatives — and the player's progress through them. Not original game
// data handling: the Gruntmods MissionLauncher 1.3.0 has most campaign lists hard-coded per
// campaign window; the one data-driven format is CustomCampaignData/<folder>/SplashArtData
// (config.ini + arc<N>.txt, Summers' Solstice). docs/community-campaigns.md has the details.
//
// Campaigns come from
//  1. SplashArtData arcs: each arc file lists the map stems; config.ini [<stem>] Index= is the
//     step (missions sharing an Index are alternative versions), "...B<n>" stems are bonus
//     missions;
//  2. mission names "<Series> - [<House>] Mission <n>[a-z] [(Option k)|(part k)]" or
//     "<Series> - [Mission] Bonus" (all community campaigns use this scheme; the launcher's
//     hard-coded lists follow the same order). A house word splits a series into one campaign
//     per house ("War of the Landsraad - Atreides"). Letters / options are alternatives of one
//     step; parts are consecutive steps.
// Progress: a step is won when one of its missions was won. The first step is open; a later
// step opens when the previous non-bonus step is won; a bonus step opens with the step after
// the non-bonus step before it and is never required.

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2k::formats {

struct CommunityMissionRef {
    std::string stem;            // map file stem, e.g. "WOL_A1"
    std::string name;            // [Basic] Name=
    std::string author;          // [Basic] Author=
    int side = -1;               // SideId
    std::string campaignFolder;  // [Data] CampaignFolder=
};

struct MissionNameParts {
    bool valid = false;
    std::string series;    // "War of the Landsraad"
    std::string subtitle;  // middle part of "A - B - Mission 1"
    std::string house;     // "Atreides" (house word before "Mission"), else ""
    int number = 0;        // mission number
    int part = 0;          // "(part k)": k (consecutive steps)
    int option = 0;        // "(Option k)" or the letter after the number (a = 1): alternatives
    bool bonus = false;
};
MissionNameParts parseCommunityMissionName(const std::string& name);

struct CommunityStep {
    std::vector<int> missions;       // indices into the mission refs (alternatives, in order)
    std::vector<std::string> stems;  // their stems
    bool bonus = false;
    int chapter = 0;                 // SplashArtData arc (1-based), 0 = none
    std::string label;               // "Mission 3", "Bonus", "S03V1" ...
};

struct CommunityCampaign {
    std::string id;      // stable key for the progress file
    std::string name;    // "War of the Landsraad - Atreides"
    std::string series;  // "War of the Landsraad"
    std::string house;   // "Atreides" or ""
    std::string author;  // most common Author= of its missions
    std::string folder;  // CustomCampaignData folder, if any
    int side = -1;       // the player's SideId (most missions)
    int chapters = 0;    // SplashArtData arcs
    std::vector<CommunityStep> steps;
    int missionCount() const;
};

// Builds the campaigns from the mission list. `dataDir` locates CustomCampaignData (for the
// SplashArtData arcs); "" skips them. Missions that belong to no campaign are left out.
std::vector<CommunityCampaign> buildCommunityCampaigns(const std::vector<CommunityMissionRef>& missions,
                                                       const std::string& dataDir);

// Per campaign id, the (lower-case) stems of the won missions. Stored as an INI file:
// [<id>] won=STEM,STEM,...
struct CommunityProgress {
    std::map<std::string, std::set<std::string>> won;

    static CommunityProgress load(const std::string& path);  // empty when missing
    bool save(const std::string& path) const;
    bool isWon(const std::string& campaign, const std::string& stem) const;
    void markWon(const std::string& campaign, const std::string& stem);
};

bool communityStepWon(const CommunityCampaign& c, int step, const CommunityProgress& p);
bool communityStepUnlocked(const CommunityCampaign& c, int step, const CommunityProgress& p);
int communityWonSteps(const CommunityCampaign& c, const CommunityProgress& p);  // non-bonus steps won
int communityRequiredSteps(const CommunityCampaign& c);                       // non-bonus steps
// The step to suggest: the first open, not yet won, non-bonus step (else the last step).
int communityNextStep(const CommunityCampaign& c, const CommunityProgress& p);

}  // namespace d2k::formats

#pragma once
// The original front end outside the campaign screens, drawn from the game's page scripts
// (Graphics: Original; re/notes/campaign-flow.md §6 "Other front-end pages"):
//   MAINMENU (MainMenu.uil) -> DIFFICULTY (skill.uil) -> [MVE_INTRO2] -> HOUSESELECTION (the
//   campaign screens), LOADMISSION (loadmis.uil, "Select Mission"), LOAD (Load.uil),
//   MULTIPLAYERTYPE (MPType.uil) -> SKIRMISH (Skirmish.uil), SETTINGS (setting.uil) -> OPTIONS
//   (options.uil) -> SOUNDOPTIONS (sound.uil), CREDITS (credits.uil + CreateCredits 0x43d2a0),
//   and the skirmish result SCORE (score.uil + FUN_0043dcb0).
// Our additions on the original pages (with the original widget art) are listed in the notes.

#include "game/campaign.hpp"
#include "platform/catalog.hpp"
#include "platform/game_data.hpp"
#include "platform/saves.hpp"
#include "platform/uil_pages.hpp"

#include <array>
#include <string>
#include <vector>

namespace d2k::platform {

struct OriginalMenuAction {
    enum Kind {
        None,
        Quit,
        Campaign,        // HOUSESELECTION: the campaign screens' house selection
        CampaignMission, // LoadMission <name> from the mission select page
        Start,           // skirmish StartGame
        Load,            // UILoadGame: save slot
        DataDir,         // our data-folder page (addition)
        Enhanced,        // Graphics switched to Enhanced on the settings page
        ArtPack,         // our Art Pack button on the settings page: Options -> Art pack (addition)
        CustomMission,   // our custom missions page: slot = Catalog::custom index
        CommunityMission,  // our community campaign page: slot = Catalog::custom, campaign index
    } kind = None;
    GameStart start;
    int slot = -1;
    std::string mission;  // CampaignMission: "a1v1"
    int campaign = -1;    // CommunityMission: Catalog::community index
    int playAsSide = -1;  // Custom/CommunityMission: GameStart::playAsSide (the page's "As:" button)
};

// One row of the skirmish result table (FUN_0043dcb0).
struct SkirmishScoreRow {
    std::string name;
    int house = 0, colour = 0;
    int unitsKilled = 0, unitsLost = 0, buildingsLost = 0, buildingsKilled = 0, spice = 0;
    int score = 0;
};

class OriginalMenus : public UilPageStack {
public:
    OriginalMenus(SDL_Renderer* r, Ui& ui, Settings& settings);
    void setData(const std::string& dataDir, const Catalog* catalog);

    // Entry points ("MAINMENU", {"MAINMENU","MULTIPLAYERTYPE","SKIRMISH"}, ...).
    void open(const std::vector<std::string>& stack) { setStack(stack); }
    // After a skirmish (won, lost after "No" on REPLAY_MISSION, or aborted): FUN_004738b0
    // "SCORE" with GameType 1 = MAINMENU, MULTIPLAYERTYPE, SCORE; Continue -> the lobby.
    void openScore(const game::ScoreInput& in);
    // Screenshot pages with sample data.
    bool openByName(const std::string& name);
    // Back from a custom mission: the custom missions page with that mission selected.
    void openCustomMissions(int sel);
    // Community pages (original_community.cpp): D2K_COMMUNITY with tab 0 campaigns / 1 maps &
    // missions and the selected row; D2K_COMMUNITYCAMPAIGN for one campaign (row -1 = next).
    void setCommunityProgress(const formats::CommunityProgress* p) { progress_ = p; }
    void openCommunity(int tab, int sel);
    void openCommunityCampaign(int campaign, int row);

    OriginalMenuAction frame();
    std::string musicOverride() const { return music_; }  // sound page: "" = page default, "-" = stopped
    bool scorePage() const { return current() == "SCORE"; }

protected:
    std::string pageFile(const std::string& menu) const override;
    const formats::UilPage* page(const std::string& menu) const override;
    bool special(const std::string& menu) override;
    void enter(const std::string& menu, UilState& s) override;
    void overrides(const std::string& menu, UilOverrides& ov) override;
    void command(const std::string& menu, const std::string& op, const std::string& arg) override;
    void widgetEvent(const std::string& menu, const UilEvents& ev, UilState& s) override;
    void drawExtra(const std::string& menu, const UiStage& st, bool top) override;
    std::string escapeButton(const std::string& menu) const override;
    void stackEmpty() override;

private:
    void enterSkirmish(UilState& s);
    void enterSettings(UilState& s);
    void fillCustomBriefing(UilState& s);
    void fillLanguageInfo(UilState& s);
    void startSkirmish();
    void drawCredits(const UiStage& st, bool top);
    void drawScore(const UiStage& st);
    const SkirmishMap* selectedMap() const;
    std::string resolution() const;
    void setResolution(const std::string& r);

    const Catalog* catalog_ = nullptr;
    std::string dataDir_;
    OriginalMenuAction action_;
    UiStage st_;
    std::vector<SaveSlot> slots_;      // LOAD page rows (used slots)
    std::vector<std::string> tracks_;  // SOUNDOPTIONS rows (data/MUSIC/*.AUD stems)
    std::string music_;
    // Credits (FUN_0043d2a0 / FUN_0043d520).
    std::vector<std::string> credits_;
    double creditsPos_ = 0;   // pixels scrolled
    Uint64 creditsLast_ = 0, creditsEndAt_ = 0;
    // Skirmish result.
    std::vector<SkirmishScoreRow> score_;
    // setting.uil plus our rows (graphics mode, fullscreen, smooth motion, mouse controls, game
    // settings, data folder) built from its own widgets; rebuilt when the data changes.
    mutable formats::UilPage settingsPage_;
    mutable bool settingsBuilt_ = false;
    // Not original: loadmis.uil with a "Custom Missions" button in the unused Survival slot, and
    // the custom (community) missions page D2K_CUSTOMMISSIONS built from loadmis.uil / Load.uil
    // widgets (mission list, briefing, Start / Cancel). re/notes/campaign-flow.md §6.4.
    mutable formats::UilPage loadMisPage_, customPage_;
    mutable bool loadMisBuilt_ = false, customBuilt_ = false;
    // Not original: the language page D2K_LANGUAGE (SETTINGS' Language button; platform/language.hpp).
    mutable formats::UilPage languagePage_;
    mutable bool languageBuilt_ = false;
    int languageInfoFor_ = -1;
    int customSel_ = 0;
    int briefFor_ = -1;  // mission whose briefing fills the briefing list
    // Community pages (original_community.cpp).
    const formats::UilPage* communityPage(const std::string& menu) const;
    void enterCommunity(const std::string& menu, UilState& s);
    void communityOverrides(const std::string& menu, UilOverrides& ov);
    bool communityCommand(const std::string& menu, const std::string& op, const std::string& arg);
    void communityEvent(const std::string& menu, const UilEvents& ev, UilState& s);
    void fillCommunityInfo(const std::string& menu, UilState& s, bool force);
    const formats::CommunityProgress* progress_ = nullptr;
    mutable formats::UilPage communityPage_, communityCampaignPage_;
    mutable bool communityBuilt_ = false, communityCampaignBuilt_ = false;
    int communityTab_ = 0, communitySel_ = 0, communityMapSel_ = 0, communityCampaign_ = 0, communityRow_ = -1;
    int communityInfoFor_ = -2;  // row whose text fills the info list (-2 = none yet)
    // "Play as" button (not original; docs/play-as.md) between Cancel and Start of the custom /
    // community mission pages: cycles through the selected mission's sides.
    static bool playAsPage(const std::string& menu);
    const MissionInfo* playAsMission(const std::string& menu) const;
    void playAsOverrides(const std::string& menu, UilOverrides& ov);
    std::vector<PlayAsOption> playAs_;
    std::string playAsMap_;
    int playAsSel_ = 0;
};

}  // namespace d2k::platform

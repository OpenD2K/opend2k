#pragma once
// The original campaign flow outside the battles (re/notes/campaign-flow.md), drawn from the
// game's own page scripts (UI_DATA/*.uil) through UilView:
//   HOUSESELECTION (HouseSel.uil) -> [MVE_<HOUSE> movie] -> PLAY_<HOUSE> (playAT/HK/OD.uil:
//   "Do you want to play House X?") -> [MVE_UNFOLD movie] -> GLOBE (Globe.uil + the territory
//   map, CreateGlobe 0x42bc60) -> LoadMission -> [MVE_<HOUSE><n> briefing movie] -> PRECAP =
//   L<h><n>MIS.uil (the "picture book" briefing with the unit pages) -> Start -> battle ->
//   SCORE_SINGLE (score_1.uil, FUN_0043d820 / FUN_0043f030 / FUN_0043fc90) -> GLOBE (won: next
//   MissionNumber; lost: the same one) ... after mission 9: score -> CREDITS (here: main menu).
// A page that only holds a MOVIE plays it through Movies (platform/movies.hpp) and runs the
// movie's end command when it is over or skipped (at once when cutscenes are off or the file is
// missing — the installed game's VQA files are 0 bytes).
// The same pages are used in both graphics modes (Enhanced = the art smoothly upscaled).

#include "game/campaign.hpp"
#include "game/savegame.hpp"
#include "platform/catalog.hpp"
#include "platform/game_data.hpp"
#include "platform/uil_view.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace d2k::platform {

class Ui;
class Movies;
class InGamePages;
struct Settings;

struct MenuSound {
    std::string name;     // samples.uib name
    bool queued = false;  // QueueAudioToPlay (speech channel) instead of a UI click channel
};

struct CampaignAction {
    // LoadPage: the modern load page (Enhanced; Back returns to the territory map).
    // LoadSlot: Load.uil over the territory map picked `slot` (a battle or a campaign-map save).
    enum Kind { None, MainMenu, LoadPage, LoadSlot, CustomMissions, Start, Credits } kind = None;
    GameStart start;
    int slot = -1;
};

class CampaignScreens {
public:
    CampaignScreens(SDL_Renderer* r, Ui& ui, Settings& settings);
    ~CampaignScreens();
    void setData(const std::string& dataDir, const Catalog* catalog);
    void setMovies(Movies* m) { movies_ = m; }

    // Entry points (each replaces the menu stack like the original's jumps).
    void openHouseSelect();                        // main menu -> Campaign
    void openGlobe(int house, int missionNumber);  // e.g. back from a quit mission
    // A campaign-map save (game::SaveKind::CampaignMap): gDifficultyLevel from the save, then the
    // territory map of its house and MissionNumber (LOADMISSION -> MAINMENU, GLOBE: FUN_00475740).
    void openSaved(const game::SaveMeta& m);
    // The territory map is the current page (no dialog over it): Save is possible.
    bool onGlobe() const;
    int house() const { return house_; }
    int missionNumber() const { return mission_; }
    // UISaveGame FUN_00474e20 outside a battle: writes a campaign-map save of the current
    // territory map to `slot` (platform/saves.hpp). False + `error` on failure.
    bool saveToSlot(int slot, const std::string& name, std::string& error);
    // After a campaign battle: the score page, then the globe (missionNumber = MissionNumber
    // after the battle: +1 when won) or, after mission 9, the main menu.
    void openScore(const game::ScoreInput& in);
    // The mission select page's "LoadMission a3v1": the briefing movie, then PRECAP.
    void openMission(const std::string& name);
    // Screenshot pages ("campaign", "house-confirm", "campaign-map[-a|-h|-o]",
    // "briefing-original", "score", "score-lost", "score-counting", "quit-campaign",
    // "campaign-map-save", "campaign-map-load") with sample data and finished animations.
    bool openByName(const std::string& name);

    CampaignAction frame();
    int cursor() const { return cursor_; }    // MOUSE.R16 cursor id (2 over a territory)
    bool scorePage() const;                   // music: score.aud
    std::vector<MenuSound> takeSounds() { std::vector<MenuSound> s; s.swap(sounds_); return s; }
    void setSpeechBusy(bool b) { speechBusy_ = b; }
    bool persistHighScores = true;  // false in one-frame runs

    // Original map name ("A3V1.MAP") -> house 0..2 and mission number; false otherwise.
    static bool parseCampaignMap(const std::string& map, int& house, int& mission);

private:
    struct GlobeAnim {
        game::GlobeState state;
        std::vector<game::RevealStep> reveal;
        std::array<int, game::kRegionCount> shown{};  // owner currently drawn (kNoOwner = none)
        int phase = 0;      // 0 map fade-in, 1 territories appear, 2 waiting for speech, 3 choose
        int counter = 0;    // 0..32 fade (phase 0 +4, phase 1 +2 per step)
        int step = -1;      // index into reveal (phase 1), -1 = pick the next
        double acc = 0;     // seconds not yet turned into steps
        bool dirty = true;  // map composite needs a rebuild
    };

    // CUIManager-like page stack of menu names.
    std::string current() const { return stack_.empty() ? std::string() : stack_.back(); }
    void enter();  // the current page became visible: movies resolve, globe/score set up
    CampaignAction jump(const std::string& menu);
    CampaignAction back();
    CampaignAction quitTo(const std::string& menu);
    CampaignAction run(const std::vector<std::string>& cmds);
    CampaignAction special(const std::string& menu, bool& handled);
    std::string pageFile(const std::string& menu) const;
    CampaignAction startPending();

    CampaignAction drawGlobe(const formats::UilPage& page);
    void drawGlobeBelow();          // the finished map without input (under a dialog)
    void ensureDialog();
    void openDialog(const std::string& menu);  // Save.uil / Load.uil over the territory map
    CampaignAction drawDialog();
    void stepGlobe();
    void rebuildMap();
    CampaignAction drawScore(const formats::UilPage& page);
    void drawHighScores();
    void loadHighScores();
    void saveHighScores();
    void finishGlobe();
    void finishScore();

    SDL_Renderer* r_;
    Ui& ui_;
    Settings& settings_;
    OriginalUi res_;
    UilView view_;
    UiStage st_;
    const Catalog* catalog_ = nullptr;
    game::CampaignTable table_;
    std::string dataDir_;

    std::vector<std::string> stack_;
    Movies* movies_ = nullptr;
    // The end commands of the MOVIE page now playing (run when the movie is over).
    std::vector<std::string> movieCommands_;
    bool moviePending_ = false;
    int house_ = 0;
    int mission_ = 1;           // MissionNumber
    std::string pendingMap_;    // LoadMission target ("a2v1")
    bool gameStatePending_ = false;
    int cursor_ = 0;
    bool speechBusy_ = false;
    std::vector<MenuSound> sounds_;

    GlobeAnim globe_;
    // The territory map's SAVE / LOAD pages (Save.uil, Load.uil and their prompts), drawn over it.
    std::unique_ptr<InGamePages> dialog_;
    bool dialogOpen_ = false;
    SDL_Texture* mapTex_ = nullptr;  // DUNEMAP + territory pictures (640x400 target)
    bool mapSmooth_ = false;
    Uint64 lastNs_ = 0;

    game::ScoreInput score_;
    game::ScoreCounter counter_;
    int enemyHouse_ = 1;
    bool scoreDone_ = false;      // counting finished (FUN_0043fc90 ran)
    Uint64 scoreStepNs_ = 0;
    std::array<game::HighScore, game::kHighScores> highScores_{};
    int newEntry_ = -1;           // high-score row being named
    bool editing_ = false;
};

}  // namespace d2k::platform

#pragma once
// The original in-game Esc menu (Graphics: Original), drawn from the game's page scripts over
// the battlefield (re/notes/campaign-flow.md §6): INGAME (InGame.uil, campaign) /
// INGAME_SKIRMISH (InGameSK.uil) -> LOAD, SAVE (Save.uil + SAVE_DESC / CONFIRM_OVERWRITE),
// DELETEGAME (delete.uil + CONFIRM_DELETE), OPTIONS (options.uil), CONFIRMABORT (confirmA.uil
// -> Abort / REALLY_RESTART), RECAP (the in-game briefing IG<h><n>MIS.uil), and REPLAY_MISSION
// (Replay.uil) after a lost skirmish. HandleBattlefieldClick 0x4433a0 / Game__MainLoopFrame
// 0x448550 open them (CUIManager__ReplaceWithOne 0x470e60 over PointerToMainImage).

#include "platform/saves.hpp"
#include "platform/uil_pages.hpp"

#include <functional>
#include <string>
#include <vector>

namespace d2k::platform {

struct InGameAction {
    enum Kind { None, Resume, Load, Abort, Restart, Cheats } kind = None;  // Cheats: not original
    int slot = -1;
};

class InGamePages : public UilPageStack {
public:
    InGamePages(SDL_Renderer* r, Ui& ui, Settings& settings);
    // `text` = Text.UIB; `recapFile` = the briefing page ("IGA3MIS", "" = none); `briefing` =
    // the text for the custom-mission page (IG?0MIS).
    void setup(const std::string& dataDir, const formats::StringTable* text, bool skirmish, const std::string& recapFile,
               const std::string& briefing);
    // Writes the running game to a slot with a description (Session::saveToSlot).
    std::function<bool(int slot, const std::string& name, std::string& error)> saveGame;
    bool canSave = true;  // false after the game ended / without a saves folder
    bool gameRunning = true;  // false after the game ended (Cheats button disabled)

    void openMenu();          // Esc / the sidebar's Options button
    void openReplayPrompt();  // a lost skirmish
    bool openByName(const std::string& name);  // screenshot pages ("pause", "pause-save", ...)
    // Only `menu` ("SAVE" / "LOAD") on the stack: the territory map's Save / Load buttons
    // (CampaignScreens draws the map below; closing the page = Resume).
    void openDialog(const std::string& menu);
    // Draws over the current frame (`enhanced`: the fractional, smooth fit of the campaign pages).
    InGameAction frame(bool enhanced = false);

protected:
    std::string pageFile(const std::string& menu) const override;
    const formats::UilPage* page(const std::string& menu) const override;
    bool special(const std::string& menu) override;
    void enter(const std::string& menu, UilState& s) override;
    void overrides(const std::string& menu, UilOverrides& ov) override;
    void command(const std::string& menu, const std::string& op, const std::string& arg) override;
    void widgetEvent(const std::string& menu, const UilEvents& ev, UilState& s) override;
    void stackEmpty() override { action_.kind = InGameAction::Resume; }

private:
    void listSaves(UilList& l, bool all);
    void doSave();
    std::vector<std::string> wrap(const std::string& s, float w) const;

    bool skirmish_ = false;
    std::string recapFile_, briefing_;
    InGameAction action_;
    std::vector<SaveSlot> slots_;  // rows of the current list page
    int pendingDelete_ = -1, pendingSave_ = -1;
    std::string pendingName_;
    // INGAME / INGAME_SKIRMISH with our Cheats button (Settings::cheats), built on demand.
    mutable formats::UilPage menuPage_;
    mutable std::string menuPageFor_;  // menu name the cached page was built for ("" = none)
};

}  // namespace d2k::platform

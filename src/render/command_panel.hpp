#pragma once
// Enhanced in-game UI: the top of the right-hand column and the readouts along the top edge,
// drawn with the modern procedural skin (UiSkin):
//  - the column background (brushed graphite, house-colour accent on the battlefield edge),
//  - the minimap frame (radarRect(); the session draws the minimap or the radar-off screen),
//  - the command buttons with vector icons: repair, sell (click modes), guard, retreat, and
//    UPGRADE / STARPORT (enabled under the original's conditions: something upgradable / a
//    starport; they open the production panel's Special sections),
//  - top: OPTIONS (pause menu), the power readout (lamp, output / demand), the credits readout and
//    the mission timer when one runs.
// Drawing only reads the World; clicks become CommandActions for the session.

#include "game/world.hpp"
#include "render/ui_skin.hpp"

#include <string>
#include <vector>

namespace d2k::render {

struct CommandAction {
    enum Kind { None, Options, RepairMode, SellMode, Guard, Retreat, Upgrade, Starport } kind = None;
};

class CommandPanel {
public:
    explicit CommandPanel(UiSkin& skin) : skin_(skin) {}

    // panelX = left edge of the right-hand column (= battlefield width).
    void layout(float panelX, int outW, int outH);
    static constexpr float kBlockH = 212;  // original pixels: minimap + command rows
    float blockBottom() const { return skin_.px(kBlockH); }
    SDL_FRect radarRect() const;
    float topBarH() const { return skin_.px(22); }

    // cursorMode: the session's click mode (2 repair, 3 sell) shows its button active.
    // upgradeLit / starportLit: the production panel shows that section.
    // mouseX / mouseY: hover.
    void drawBlock(const game::World& w, int side, int cursorMode, bool upgradeLit, bool starportLit, float mouseX = -1,
                   float mouseY = -1);
    void drawTopBar(const game::World& w, int side, float mouseX = -1, float mouseY = -1);

    CommandAction mouseDown(float x, float y, int button, const game::World& w, int side);
    void mouseUp() { pressed_ = -1; }
    bool contains(float x, float y) const;
    std::vector<UiSkin::TipLine> tooltipAt(float x, float y, const game::World& w, int side) const;

private:
    enum Btn { kRepair, kSell, kGuard, kRetreat, kUpgrade, kStarport, kOptions, kCount };
    SDL_FRect buttonRect(int b) const;
    bool enabled(int b, const game::World& w, int side) const;
    int hit(float x, float y) const;
    SDL_FRect creditsRect() const;
    SDL_FRect powerRect() const;
    SDL_FRect timerRect() const;

    UiSkin& skin_;
    float panelX_ = 0;
    int outW_ = 0, outH_ = 0;
    int pressed_ = -1;
    bool timerShown_ = false;
};

}  // namespace d2k::render

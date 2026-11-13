#pragma once
// In-game Cheats page (not original): money, tech level, build availability overrides, free
// deliveries, scheduled carryall reinforcements, spawning units/buildings for any side, and a few toggles. Every change is a
// World::orderCheat* order (executed at the next tick, deterministic and saved). Drawn with the
// modern ui.* widgets in both graphics modes. Opened from the pause menu ("Cheats") or with
// Ctrl+Shift+C; the Spawn section hands over to a placement mode on the battlefield.

#include "formats/bintables.hpp"
#include "formats/templates.hpp"
#include "game/world.hpp"

#include <SDL3/SDL.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace d2k::render {
class SpriteCache;
}

namespace d2k::platform {

class Ui;

class CheatMenu {
public:
    enum class Result { None, Close, Place };
    struct Spawn {
        int side = 0;
        bool building = false;
        int type = -1;
        int count = 1;          // units per click
        bool concrete = true;   // buildings: lay concrete under the footprint (like mission setup)
    };

    explicit CheatMenu(const formats::Templates& templates, const formats::HouseColours* colours = nullptr)
        : t_(templates), colours_(colours) {}

    // Called when the page opens (keeps the last tab; selects the player's side the first time).
    void open(const game::World& w, int playerSide);
    // Draws the page over the whole output (outW x outH) and handles its input.
    Result frame(Ui& ui, game::World& w, int playerSide, render::SpriteCache* icons, int outW, int outH);
    // Opens a section by name for screenshots ("money", "tech", "build", "deliver", "reinforce",
    // "spawn", "spawn-buildings", "toggles").
    void setTab(const std::string& name);
    // Game ticks per second at the current game speed: reinforcement times are entered in
    // seconds / minutes of game time and ordered in ticks.
    void setTicksPerSecond(int tps) { tps_ = tps > 0 ? tps : 20; }

    // Placement mode (after Result::Place): spawning, or the drop point of a reinforcement ----
    bool placing() const { return placing_; }
    void cancelPlacing() { placing_ = false; }
    const Spawn& spawn() const { return spawn_; }
    // Top-left footprint tile for the cursor at world pixel (wx, wy) (buildings centred like
    // the normal placement cursor; units: the tile under the cursor).
    void placementTile(float wx, float wy, int& tx, int& ty) const;
    // Left click on the battlefield at world pixel (wx, wy). `keep` (Shift): stay in the mode.
    // Returns true when an order was queued.
    bool click(game::World& w, float wx, float wy, bool keep);
    // Footprint / target preview; camX/camY/zoom map world pixels to the current render target.
    // `markers` (Original graphics): DATA.R16 placement markers (frames 0/1) instead of fills.
    // The hint above the cursor is drawn with `ui` in the Play font (translated, render/ui_text).
    void drawPlacement(SDL_Renderer* r, Ui& ui, const game::World& w, float wx, float wy, float camX, float camY, float zoom,
                       render::SpriteCache* markers) const;
    // The same preview through any projection (3D mode): cell(tileX, tileY, fill, outline) draws
    // one footprint cell on the ground; project(world x, y, out screen x, y) places the hint.
    void drawPlacement(Ui& ui, const game::World& w, float wx, float wy,
                       const std::function<void(int, int, SDL_FColor, SDL_FColor)>& cell,
                       const std::function<bool(float, float, float&, float&)>& project) const;
    std::string placementHint() const;  // UTF-8, translated: "Spawn 5 x Trike (side 1) - Shift: keep, right click/Esc: stop"

    static const char* houseName(int house);
    std::string sideLabel(const game::World& w, int side, int playerSide) const;

private:
    void drawHint(Ui& ui, float x, float y) const;  // the placement hint, its bottom-left at (x, y)
    enum Tab { kMoney, kTech, kBuild, kDeliver, kReinforce, kSpawn, kToggles, kTabCount };
    struct Cell {
        int type = -1;
        bool building = false;
    };
    // Icon grid of `cells` in `area`; returns the index clicked (-1 none). `badge(i)` gives the
    // overlay text, `tint(i)` the frame colour (alpha 0 = none). `hovered` gets the cell under the mouse.
    template <class Badge, class Tint>
    int grid(Ui& ui, const SDL_FRect& area, const std::vector<Cell>& cells, float& scroll, render::SpriteCache* icons,
             int house, Badge badge, Tint tint, int& hovered);
    std::string name(const Cell& c) const;
    std::vector<Cell> unitCells(bool deliverable) const;
    std::vector<Cell> buildingCells() const;
    int colourOf(const game::World& w, int side) const;

    float drawMoney(Ui& ui, game::World& w, const SDL_FRect& area, float s);
    float drawTech(Ui& ui, game::World& w, const SDL_FRect& area, float s);
    void drawBuild(Ui& ui, game::World& w, const SDL_FRect& area, float s, render::SpriteCache* icons);
    void drawDeliver(Ui& ui, game::World& w, const SDL_FRect& area, float s, render::SpriteCache* icons);
    bool drawSpawn(Ui& ui, game::World& w, const SDL_FRect& area, float s, render::SpriteCache* icons);
    // Sections without a grid return their content height (they scroll when it does not fit).
    float drawToggles(Ui& ui, game::World& w, const SDL_FRect& area, float s);
    bool drawReinforce(Ui& ui, game::World& w, const SDL_FRect& area, float s, render::SpriteCache* icons);
    std::vector<std::uint8_t> reinforceUnits() const;  // the picked units, by type
    std::uint32_t reinforceDelayTicks() const;
    std::string timeText(std::uint32_t ticks) const;  // "1:05" of game time

    const formats::Templates& t_;
    const formats::HouseColours* colours_;
    int tab_ = kMoney;
    int side_ = -1;              // side the page acts on
    bool resetFocus_ = true;
    float scroll_[4] = {0, 0, 0, 0};  // build, deliver, spawn, reinforce grids
    float sectionScroll_ = 0, sectionContentH_ = 0;  // money / tech / toggles when they do not fit
    int tps_ = 20;
    // Reinforcements section.
    std::vector<int> reinfCount_;
    int reinfSel_ = -1;           // unit type of the -/+ buttons
    int reinfWhen_ = 0;           // index into the delay presets
    int reinfEdge_ = 0;           // 0 nearest, 1 north, 2 east, 3 south, 4 west
    int reinfListSel_ = -1;
    float reinfListScroll_ = 0;
    bool placeReinforce_ = false;  // placement mode picks a reinforcement drop point
    int creditsSlider_ = 100;    // x 100 credits
    std::vector<int> deliverCount_;
    int deliverSel_ = -1;
    bool spawnBuildings_ = false;
    bool buildUnits_ = false;    // Build list section shows units (else buildings)
    Spawn spawn_;
    bool placing_ = false;
    std::string status_;
    bool statusOrdered_ = false;  // status_ is a reinforcement order (gets "(when the game runs)" while paused)
    // Changes ordered while the game is paused (the orders run at the next tick): what the
    // page shows until the world reflects them. Key: kind * 1000 + side * 100 + type.
    std::map<int, int> pending_;
    int shown(int key, int worldValue);
};

}  // namespace d2k::platform

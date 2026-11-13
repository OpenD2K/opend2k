#pragma once
// Enhanced production panel (the lower part of the right-hand column, under the command block),
// in the modern procedural skin (UiSkin): a power meter on the left (the original's percent / 200
// rule and colours), a segmented control of four tabs with vector icons (Buildings / Infantry /
// Vehicles / Special) with ready badges and a progress line, and a 2-column grid of the large unit /
// building icons (HQ / art-pack pictures) in bezels inside a recessed tray, empty bezels below.
// Production runs as the original clock wedge (FUN_004263b0) plus a progress line, READY / HOLD
// in the original pulsing green. Special holds the palace weapons / air strike, the building
// upgrades (the original's UPGRADE page 0x4278a0) and the starport (0x427690) with PURCHASE. Two
// scroll buttons close the panel. Drawing only reads the World; clicks are translated into
// actions the frontend executes.

#include "formats/uib.hpp"
#include "game/world.hpp"
#include "render/sprites.hpp"
#include "render/ui_skin.hpp"

#include <string>
#include <vector>

namespace d2k::render {

struct SidebarAction {
    enum Kind {
        None, BuildBuilding, CancelBuilding, BeginPlacement, BuildUnit, CancelUnit,
        SpecialWeapon,   // type = unit behaviour (saboteur / fremen): launch now
        TargetWeapon,    // type = unit behaviour (ornithopter / death hand): pick a target on the map
        StarportPick, StarportUnpick,     // type = unit template
        StarportPurchase, StarportCancel,
        Upgrade,         // type = building group (GenerateUpgradePickOrder)
        CancelUpgrade,   // type = building template being upgraded (hold, then cancel)
    } kind = None;
    int type = -1;
};

// Display names as the original's tooltips show them (FUN_0044b5c0): the Text.UIB entry of the
// template's group name, else the template name in title case.
std::string unitDisplayName(const game::World& w, const formats::StringTable* strings, int type);
std::string buildingDisplayName(const game::World& w, const formats::StringTable* strings, int type);

class Sidebar {
public:
    Sidebar(UiSkin& skin, SpriteCache& sprites) : skin_(skin), sprites_(sprites) {}

    enum class Tab { Buildings, Infantry, Vehicles, Special };

    // Lays out and draws into `area` (the panel below the radar block down to the window's
    // bottom); remembers the hit rectangles for click(). mouse = for the hover highlight.
    void draw(const SDL_FRect& area, const game::World& w, int side, float mouseX, float mouseY);
    // Mouse press at (x, y) with SDL button index; returns what to do. Leaving the Special tab
    // with starport picks cancels them (as the original's MAIN button does).
    SidebarAction click(float x, float y, int button, const game::World& w, int side);
    void mouseUp() { pressedArrow_ = -1; pressed_ = false; }
    void scroll(float x, float y, float wheel);
    bool contains(float x, float y) const;
    // Open the Special tab at the starport / upgrade section (STARPORT / UPGRADE buttons).
    void showStarport(bool on) { if (on) jump(Section::Starport); }
    void showUpgrades(bool on) { if (on) jump(Section::Upgrades); }
    bool starportShown() const { return tab_ == Tab::Special && section_ == Section::Starport; }
    bool upgradesShown() const { return tab_ == Tab::Special && section_ == Section::Upgrades; }
    // Text.UIB for the unit / building names (Templates group names, case-insensitive).
    void setStrings(const formats::StringTable* t) { strings_ = t; }
    // Hover tooltip for the mouse at (x, y) (call after draw(); nothing outside the panel).
    void drawTooltip(float x, float y, const game::World& w, int side, float maxX, float maxY);
    std::string unitName(const game::World& w, int type) const;
    std::string buildingName(const game::World& w, int type) const;
    // Not original: Extended production queue mode (Settings::productionQueue) - busy factories'
    // unit icons are not greyed; queued types show their count / an infinity mark.
    bool extendedQueue = false;

private:
    enum class Section { None, Palace, Upgrades, Starport };
    struct Item {
        enum Kind { Building, Unit, Upgrade, Port, Header, Purchase, Cancel } kind = Building;
        int type = -1, group = -1;
        int iconFrame = -1, cost = 0, level = 0;
        int stock = 0, ordered = 0;  // starport
        bool available = true;       // starport: can be picked now
        Section section = Section::None;  // headers
        SDL_FRect rc{};
    };
    void jump(Section s) { tab_ = Tab::Special; section_ = s; jumpPending_ = true; }
    std::vector<Item> items(Tab t, const game::World& w, int side) const;
    std::vector<game::BuildOption> upgradeList(const game::World& w, int side) const;
    void drawTabs(const game::World& w, int side, float mouseX, float mouseY);
    void drawItem(const Item& it, const game::World& w, int side, bool hover);
    void drawSlotIcon(const SDL_FRect& rc, int frame, bool greyed, const game::QueueState& q, int house, UiSkin::Bezel st);
    void drawReadyText(const SDL_FRect& rc, const char* text);
    void drawQueueCount(const SDL_FRect& rc, const game::AutoQueueState& st);
    void drawPower(const game::World& w, int side);
    void drawFooter(float mouseX, float mouseY);
    int arrowAt(float x, float y) const;
    SDL_FRect arrowRect(int i) const;
    void scrollBy(float px);

    UiSkin& skin_;
    SpriteCache& sprites_;
    SDL_FRect area_{}, tray_{}, power_{}, footer_{};
    SDL_FRect tabRc_[4]{};
    std::vector<Item> laid_;   // this frame's items with rectangles (current tab)
    Tab tab_ = Tab::Buildings;
    Section section_ = Section::None;
    bool jumpPending_ = false;
    float scroll_[4] = {0, 0, 0, 0};
    float contentH_ = 0, rowPitch_ = 0;
    int pressedArrow_ = -1;
    bool pressed_ = false;  // left button held on an item (pressed bezel while over it)
    const formats::StringTable* strings_ = nullptr;
};

}  // namespace d2k::render

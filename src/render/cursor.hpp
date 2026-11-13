#pragma once
// The original mouse cursors (MOUSE.R16; re/notes/ui.md "Cursors").
//
// MOUSE.R16 holds 33 cursors x 8 animation frames (frame = cursor * 8 + step) plus one 48x48
// save-under buffer (frame 264). Every frame was cut from a 48x48 cell whose centre (24, 24) is
// the hotspot: a frame is drawn at mouse - 24 - (x offset, y offset) (FUN_00423a20). The step
// advances every 3rd drawn frame (1/20 s at 60 fps) and wraps at 8.
//
// pickCursor is the one place that decides the cursor from what is under the mouse; it mirrors
// the cursor half of HandleBattlefieldClick 0x4433a0 and only reads the World.

#include "formats/bintables.hpp"
#include "formats/r16.hpp"
#include "game/world.hpp"

#include <memory>
#include <string>

struct SDL_Renderer;

namespace d2k::render {

class SpriteCache;

// CurrentCursorId 0x798540 values (SetMouseCursor 0x44c420).
enum class Cursor : int {
    Arrow = 0,         // normal pointer (also every menu: CUIManager pages force 0)
    Move = 1,          // green ring
    Attack = 2,        // red ring (also: harvesters over spice, campaign map regions)
    NoMove = 3,        // green ring crossed out
    Enter = 4,         // green arrows down: dock at refinery / repair pad, engineer capture
    Select = 5,        // shrinking white brackets
    RadarMove = 6,     // small green ring: radar click moves the selected units
    NoSell = 7,        // sell mode, nothing to sell under the cursor
    NoRepair = 8,      // repair mode, nothing to repair
    AirStrike = 9,     // blue ring: ornithopter strike target (gMouseMode 4)
    Sell = 10,         // spinning $
    Repair = 11,       // wrench
    Deploy = 12,       // green four-way arrows: deploy / self-destruct / make primary
    NoDeploy = 13,     // the same crossed out
    ScrollN = 14,      // 14..21 scroll arrows N, NE, E, SE, S, SW, W, NW
    NoScrollN = 22,    // 22..29 the same crossed out (view already at that edge)
    DeathHand = 30,    // spinning skull: death hand target (gMouseMode 5)
    Sabotage = 31,     // dynamite: saboteur on a building it can blow up
    InfantryMove = 32, // grey ring: only infantry can go there
};
constexpr int kCursorCount = 33, kCursorFrames = 8;
constexpr int kCursorHotspot = 24;  // centre of the 48x48 source cell
const char* cursorName(Cursor c);

// Inputs of the decision besides the World.
struct CursorQuery {
    int side = 0;                 // MySideID
    int mode = 0;                 // gMouseMode 0x516438: 0 normal, 1 placing, 2 repair, 3 sell, 4/5 weapon target
    bool modernControls = false;  // Settings::Controls::Modern (left selects, right orders)
    bool overBattlefield = false;
    int worldX = 0, worldY = 0;   // world pixels under the mouse (overBattlefield)
    bool overRadar = false;       // inside the radar map while it is on and moves units on click
    bool shroud = true;           // the shroud is drawn (a fully shrouded cell hides its contents)
    bool ctrl = false, shift = false, alt = false;
};
Cursor pickCursor(const game::World& w, const CursorQuery& q);

// The scroll arrows that replace the cursor while the view scrolls (tail of 0x4433a0; only in
// mode 0 and not while the left button is held). dx/dy = -1/0/1, blocked = the view is clamped
// at that edge. The vertical arrow is chosen first, the horizontal one combines with it.
Cursor scrollCursor(Cursor current, int dx, int dy, bool blockedX, bool blockedY);

// Draws MOUSE.R16 cursors as sprites (on top of everything; the system cursor is hidden).
class CursorRenderer {
public:
    CursorRenderer(SDL_Renderer* r, const std::string& mouseR16Path);  // throws on a bad file
    ~CursorRenderer();
    CursorRenderer(const CursorRenderer&) = delete;
    CursorRenderer& operator=(const CursorRenderer&) = delete;

    // (x, y) = mouse in output pixels. Original look: integer `scale` with the hotspot snapped
    // to whole game pixels; hq = Scale2x-upscaled frames, smooth scaling (Enhanced mode).
    void draw(Cursor c, float x, float y, float scale, bool hq);

private:
    SDL_Renderer* r_;
    formats::R16File file_;
    formats::HouseColours colours_{};
    std::unique_ptr<SpriteCache> cache_;
    unsigned long long start_ = 0;  // SDL ticks (ms) when created: animation phase
};

}  // namespace d2k::render

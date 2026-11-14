#pragma once
// Order feedback on the battlefield (render only; never touches the simulation).
//
// Graphics: Original - the original's "cursor puffs" (AddCursorPuffAnimationToQueue 0x456d50,
// Display__DrawCursorPuffs 0x428220, re/notes/ui.md "Order feedback"): a 10-entry queue of
// EX_PUFF animations (Templates.bin groupIds[57], DATA.R16, subtractive blit) at the clicked
// point of a move / attack-ground order, one animation frame per 3 drawn frames at 60 fps.
// MapRenderer draws them (after the projectiles, before the shroud).
//
// Enhanced / 3D - a modern equivalent: an animated ground ring at the move target (green) or the
// attacked ground (red), and a red ring pulsing around an attacked unit / building (following it).
// The target's own grey blink is the simulation's flicker counter (Unit+0x1d / Building+0x38).
#include <SDL3/SDL.h>

#include <array>
#include <functional>
#include <vector>

namespace d2k::game {
class World;
}

namespace d2k::render {

class OrderFeedback {
public:
    // One entry of the original's queue 0x516490 (stride 16: x, y, frame, delay).
    struct Puff {
        int x = 0, y = 0;     // world pixels of the click
        int frame = 1 << 30;  // animation frame; >= frames = free
        int delay = 0;        // drawn frames shown at this frame (0..2)
    };
    static constexpr int kPuffSlots = 10;

    // EX_PUFF art: first DATA.R16 frame and frame count (animationArtFrames[groupIds[57]]).
    void setPuffArt(int firstFrame, int frames) { puffFirst_ = firstFrame, puffFrames_ = frames; }
    int puffFirstFrame() const { return puffFirst_; }
    int puffFrames() const { return puffFrames_; }
    const std::array<Puff, kPuffSlots>& puffs() const { return puffs_; }

    // AddCursorPuffAnimationToQueue 0x456d50.
    void addPuff(int wx, int wy);
    // Modern markers.
    void addMove(float wx, float wy, bool attackGround);
    void addAttackUnit(int side, int index);
    void addAttackBuilding(int side, int index);

    // Advance by dt seconds (the original: per drawn frame, i.e. 60 steps per second).
    void update(double dt);
    void clear();

    // Modern markers through a world -> screen projection (2D view or the 3D terrain at z = 0).
    void drawModern(SDL_Renderer* r, const game::World& w,
                    const std::function<bool(float, float, float&, float&)>& project) const;

private:
    void stepPuffs();  // the advance part of Display__DrawCursorPuffs 0x428220

    struct Marker {
        enum Kind { Move, AttackGround, AttackUnit, AttackBuilding } kind = Move;
        float x = 0, y = 0;
        int side = -1, index = -1;
        float age = 0;
    };
    std::array<Puff, kPuffSlots> puffs_{};
    int puffFirst_ = -1, puffFrames_ = 0;
    double puffClock_ = 0;
    std::vector<Marker> markers_;
};

}  // namespace d2k::render

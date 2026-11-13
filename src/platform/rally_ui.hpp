#pragma once
// Factory rally points on the battlefield (not original; game/rally.cpp): which click sets
// them and the marker drawn for the selected factory. Controls (Settings::rallyPoints on):
//   Original mouse scheme: with an own factory selected (and no units), Alt+left click on the
//     ground sets its rally point (plain left / right clicks keep their original meaning).
//   Modern scheme: with an own factory selected (and no units), right click on the ground.
//   Either click on the factory itself clears the point.

#include "game/world.hpp"

#include <SDL3/SDL.h>

#include <functional>

namespace d2k::platform {

// The single selected own building that can have a rally point, or -1 (also -1 when units are
// selected: then the click is an ordinary unit order).
int selectedRallyFactory(const game::World& w, int side);
// Rally click at world pixel (wx, wy): orders the point (or clears it on the factory itself).
// Returns false when no rally factory is selected (the caller handles the click as usual).
bool rallyClick(game::World& w, int side, int wx, int wy);
// Flag at the rally point and a line from the factory, for the selected factory. camX/camY/zoom
// map world pixels to the current render target.
void drawRallyPoint(SDL_Renderer* r, const game::World& w, int side, float camX, float camY, float zoom);
// The same through any projection (3D mode): project(world x, world y, out screen x, y) -> false if
// not visible; scale = screen pixels per world pixel at the rally point (flag size).
void drawRallyPoint(SDL_Renderer* r, const game::World& w, int side,
                    const std::function<bool(float, float, float&, float&)>& project, float scale);

}  // namespace d2k::platform

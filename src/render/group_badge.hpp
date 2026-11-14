#pragma once
// Enhanced / 3D (not original): an own unit's control-group number (Unit::groupId 1..10, key 0 =
// group 10 shown as "0") as a small dark badge with a light digit in the UI font, drawn left of the
// unit's health bar (Settings::groupNumbers; MapRenderer::drawHealthBars, Renderer3D::drawOverlays).

#include "render/ttf_font.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace d2k::render {

// right / midY: the badge's right edge and vertical centre in screen pixels; z: the overlay scale
// (1, 1.5 or 2 - the health bars' sqrt(zoom) step).
inline void drawGroupBadge(SDL_Renderer* r, TtfFont* font, int group, float right, float midY, float z) {
    if (!font || group < 1 || group > 10) return;
    const char digit[2] = {char('0' + group % 10), 0};
    const float px = std::round(11 * z);
    const float tw = font->width(digit, px);
    const float h = std::round(px + 1), w = std::round(std::max(h * 0.8f, tw + 4 * z));
    const SDL_FRect box{std::round(right - w), std::round(midY - h / 2), w, h};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 175);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 90);
    SDL_RenderRect(r, &box);
    font->draw(digit, std::round(box.x + (w - tw) / 2), box.y + std::round((h - font->lineHeight(px)) / 2), px,
               Colour{255, 236, 170, 255});
}

}  // namespace d2k::render

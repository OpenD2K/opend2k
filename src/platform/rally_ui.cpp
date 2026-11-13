#include "platform/rally_ui.hpp"

#include <cmath>

namespace d2k::platform {

namespace {

SDL_FPoint factoryCentre(const game::World& w, const game::Building& b) {
    const auto& t = w.rules().templates->bin.buildings[b.type];
    return {float(game::fixToPixel(b.posX)) + t.artWidth / 2.0f, float(game::fixToPixel(b.posY)) - t.artHeight / 2.0f};
}

bool onFactory(const game::World& w, const game::Building& b, int wx, int wy) {
    const auto& t = w.rules().templates->bin.buildings[b.type];
    const int x0 = game::fixToPixel(b.posX), y0 = game::fixToPixel(b.posY) - t.artHeight;
    return wx >= x0 && wy >= y0 && wx < x0 + t.artWidth && wy < y0 + t.artHeight;
}

}  // namespace

int selectedRallyFactory(const game::World& w, int side) {
    if (side < 0 || side >= game::kMaxSides) return -1;
    const game::Side& s = w.sides()[static_cast<std::size_t>(side)];
    for (int i = s.firstUnit; i != -1; i = s.nextObject(i))
        if (s.units[i].selected) return -1;
    int found = -1;
    for (int i = s.firstBuilding; i != -1; i = s.nextObject(i)) {
        if (!s.buildings[i].selected) continue;
        if (found >= 0 || !w.canHaveRallyPoint(side, i)) return -1;
        found = i;
    }
    return found;
}

bool rallyClick(game::World& w, int side, int wx, int wy) {
    const int bi = selectedRallyFactory(w, side);
    if (bi < 0) return false;
    const game::Building& b = w.sides()[static_cast<std::size_t>(side)].buildings[bi];
    if (onFactory(w, b, wx, wy)) w.orderClearRallyPoint(side, bi);
    else if (w.inBounds(wx >> 5, wy >> 5)) w.orderSetRallyPoint(side, bi, wx >> 5, wy >> 5);
    return true;
}

void drawRallyPoint(SDL_Renderer* r, const game::World& w, int side, float camX, float camY, float zoom) {
    drawRallyPoint(
        r, w, side,
        [&](float x, float y, float& sx, float& sy) {
            sx = (x - camX) * zoom;
            sy = (y - camY) * zoom;
            return true;
        },
        zoom);
}

void drawRallyPoint(SDL_Renderer* r, const game::World& w, int side,
                    const std::function<bool(float, float, float&, float&)>& project, float zoom) {
    const int bi = selectedRallyFactory(w, side);
    int tx = 0, ty = 0;
    if (bi < 0 || !w.rallyPoint(side, bi, tx, ty)) return;
    const game::Building& b = w.sides()[static_cast<std::size_t>(side)].buildings[bi];
    const SDL_FPoint c = factoryCentre(w, b);
    SDL_FPoint a{}, p{};
    if (!project(c.x, c.y, a.x, a.y) || !project(tx * 32.0f + 16, ty * 32.0f + 24, p.x, p.y)) return;
    // Dashed line factory -> point (dark outline under a light dash).
    const float dx = p.x - a.x, dy = p.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    const float dash = 6 * std::max(1.0f, zoom);
    for (float d = 0; d < len; d += 2 * dash) {
        const float e = std::min(len, d + dash);
        const SDL_FPoint s0{a.x + dx * d / len, a.y + dy * d / len}, s1{a.x + dx * e / len, a.y + dy * e / len};
        SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
        SDL_RenderLine(r, s0.x + 1, s0.y + 1, s1.x + 1, s1.y + 1);
        SDL_SetRenderDrawColor(r, 255, 226, 150, 220);
        SDL_RenderLine(r, s0.x, s0.y, s1.x, s1.y);
    }
    // Flag: pole with a pennant, on a small ground ring.
    const float k = std::max(1.0f, zoom);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
    SDL_FRect pole{p.x - 1 * k + 1, p.y - 20 * k + 1, 2 * k, 20 * k};
    SDL_RenderFillRect(r, &pole);
    SDL_SetRenderDrawColor(r, 230, 220, 200, 255);
    pole = {p.x - 1 * k, p.y - 20 * k, 2 * k, 20 * k};
    SDL_RenderFillRect(r, &pole);
    const SDL_FColor fc{0.94f, 0.66f, 0.2f, 1};
    const SDL_Vertex v[3] = {{{p.x + 1 * k, p.y - 20 * k}, fc, {0, 0}},
                             {{p.x + 13 * k, p.y - 15 * k}, fc, {0, 0}},
                             {{p.x + 1 * k, p.y - 10 * k}, fc, {0, 0}}};
    SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
    SDL_SetRenderDrawColor(r, 255, 226, 150, 200);
    const SDL_FRect ring{p.x - 6 * k, p.y - 2 * k, 12 * k, 4 * k};
    SDL_RenderRect(r, &ring);
}

}  // namespace d2k::platform

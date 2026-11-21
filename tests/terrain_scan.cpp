// terrain_scan: the 3D faithful terrain's relief where ground units move (docs/3d-mode.md "Reimagined",
// "Passable ground"). For every campaign + skirmish map: builds the layout guide and the heightfield the
// way the 3D renderer does (plain faithful and Reimagined) and measures, on a 4 px grid over the cells
// vehicles can use (the drive-on bit):
//   slope  the steepest rise between two neighbouring samples (4 px apart) that both lie on such cells
//          (px per px; a wall > 1);
//   pit    how deep a sample lies below its surroundings: in each of 8 directions the highest sample
//          (any cell) within 32 px, the lowest of those 8 minus the sample (a slope has a way down: 0);
//   cell   a drive-on cell's centre below the lowest centre of its drive-on 4-neighbours (>= 2 of them).
// Render-only diagnostics; needs the original game data. Usage:
//   terrain_scan [--data DIR] [--maps A9V2.MAP,...] [--foundations] [--top N]
// Building foundations (docs/3d-mode.md "Building foundations"): --refinery places the player's refinery
// just south of its construction yard (the owner's report), --place-all also every other building group
// of its house near the yard (where each fits); both imply --foundations and print, around every
// foundation (3 cells, the cells units drive over: drive-on, no building), the steepest slope, the sample
// pairs the foundations made steeper than kReimRamp / 0.5, the highest mound / deepest dip against the
// natural ground (the heightfield without foundations), the hollow depth they added, the build time and
// (--refinery) the cost of one more building in play. --write-save DIR: slot 1 there (the world with those
// buildings), for `d2k --load 1 --saves-dir DIR`. --measure-foundations: the same numbers for the map's own
// buildings. Env SCAN_LIST=1: every building's level; SCAN_DIFF=x,y: (height - natural) / natural around x,y;
// SCAN_FLAGS=1: the cell flags around the yard.
#include "game/savegame.hpp"
#include "platform/catalog.hpp"
#include "platform/saves.hpp"
#include "platform/game_data.hpp"
#include "render/tileset.hpp"
#include "render3d/terrain.hpp"
#include "render3d/terrain_guide.hpp"
#include "render3d/terrain_v2.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace d2k;

namespace {

struct Worst {
    float v = 0;
    int x = 0, y = 0;  // world px
};
struct Stats {
    Worst slope, pit, cell;
    int walls = 0;     // samples pairs with slope > 1
    int pits = 0;      // samples deeper than 4 px
    int samples = 0;
    int cells = 0;     // cells deeper than 8 px
};

Stats measure(const game::World& w, const render3d::Terrain& t) {
    namespace cf = game::cell_flags;
    Stats s;
    const int W = w.width() * 8 + 1, H = w.height() * 8 + 1;  // 4 px grid
    std::vector<float> h(std::size_t(W) * H);
    std::vector<std::uint8_t> pass(std::size_t(W) * H);
    auto drive = [&](float wx, float wy) {
        const int cx = std::clamp(int(wx / 32), 0, w.width() - 1), cy = std::clamp(int(wy / 32), 0, w.height() - 1);
        return (w.cell(cx, cy).flags & cf::kDriveOn) != 0;
    };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            // Samples on cell borders belong to both cells: passable only if every touching cell is.
            const float wx = float(x) * 4, wy = float(y) * 4;
            bool p = true;
            for (float oy : {-0.5f, 0.5f})
                for (float ox : {-0.5f, 0.5f}) {
                    const float sx = std::clamp(wx + ox, 0.0f, float(w.width() * 32) - 0.01f);
                    const float sy = std::clamp(wy + oy, 0.0f, float(w.height() * 32) - 0.01f);
                    p = p && drive(sx, sy);
                }
            pass[std::size_t(y) * W + x] = p;
            h[std::size_t(y) * W + x] = t.height(wx, wy);
        }
    auto H_ = [&](int x, int y) { return h[std::size_t(y) * W + x]; };
    auto P_ = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H && pass[std::size_t(y) * W + x] != 0; };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!P_(x, y)) continue;
            ++s.samples;
            for (int k = 0; k < 2; ++k) {
                const int nx = x + (k == 0), ny = y + (k == 1);
                if (!P_(nx, ny)) continue;
                const float sl = std::fabs(H_(nx, ny) - H_(x, y)) / 4.0f;
                if (sl > 1.0f) ++s.walls;
                if (sl > s.slope.v) s.slope = {sl, x * 4, y * 4};
            }
            static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
            float depth = 1e9f;
            for (int d = 0; d < 8; ++d) {
                float best = -1e9f;
                for (int r = 1; r <= 8; ++r) {
                    const int qx = x + dx[d] * r, qy = y + dy[d] * r;
                    if (qx < 0 || qy < 0 || qx >= W || qy >= H) break;
                    best = std::max(best, H_(qx, qy) - H_(x, y));
                }
                if (best > -1e8f) depth = std::min(depth, std::max(best, 0.0f));
            }
            if (depth > 1e8f) depth = 0;
            if (depth > 8.0f) ++s.pits;
            if (depth > s.pit.v) s.pit = {depth, x * 4, y * 4};
        }
    // Per cell: a drive-on cell's centre below the lowest centre of its drive-on 4-neighbours (at least two).
    for (int cy = 0; cy < w.height(); ++cy)
        for (int cx = 0; cx < w.width(); ++cx) {
            if (!(w.cell(cx, cy).flags & cf::kDriveOn)) continue;
            float nmin = 1e9f;
            int nn = 0;
            static const int ddx[4] = {1, -1, 0, 0}, ddy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                const int nx = cx + ddx[k], ny = cy + ddy[k];
                if (nx < 0 || ny < 0 || nx >= w.width() || ny >= w.height() || !(w.cell(nx, ny).flags & cf::kDriveOn)) continue;
                nmin = std::min(nmin, t.height(float(nx * 32 + 16), float(ny * 32 + 16)));
                ++nn;
            }
            if (nn < 2) continue;
            const int lx = cx * 32 + 16, ly = cy * 32 + 16;
            const float d = nmin - t.height(float(lx), float(ly));
            if (d > 8.0f) ++s.cells;
            if (d > s.cell.v) s.cell = {d, lx, ly};
        }
    return s;
}

// Around the buildings' footprints (3 cells): slopes on the cells units drive over (drive-on, no building) and the change against the natural ground.
struct FoundStats {
    Worst slope, mound, dip, pit;  // pit: how much deeper than the natural ground a sample lies in a hollow (as measure())
    int steep = 0;    // drive-on sample pairs the foundations made steeper than kReimRamp
    int steep50 = 0;  // ... steeper than 0.5 px per px
    int samples = 0;
};
FoundStats measureFoundations(const game::World& w, const render3d::Terrain& t, const render3d::Terrain& nat) {
    namespace cf = game::cell_flags;
    FoundStats s;
    const int cw = w.width(), ch = w.height();
    std::vector<std::uint8_t> nearB(std::size_t(cw) * ch, 0);
    for (const auto& side : w.sides())
        for (int bi = side.firstBuilding; bi >= 0; bi = side.nextObject(bi)) {
            const game::Building& b = side.buildings[std::size_t(bi)];
            if (!b.alive) continue;
            const std::uint32_t mask = w.buildingTemplate(b).tilesOccupiedAll & 0xFFFFu;
            for (int k = 0; k < 16; ++k)
                if (mask & (1u << k))
                    for (int y = b.tileY + k / 4 - 3; y <= b.tileY + k / 4 + 3; ++y)
                        for (int x = b.tileX + k % 4 - 3; x <= b.tileX + k % 4 + 3; ++x)
                            if (x >= 0 && y >= 0 && x < cw && y < ch) nearB[std::size_t(y) * cw + x] = 1;
        }
    auto ok = [&](float wx, float wy) {
        if (wx < 0 || wy < 0 || wx > float(cw * 32) || wy > float(ch * 32)) return false;
        for (float oy : {-0.5f, 0.5f})
            for (float ox : {-0.5f, 0.5f}) {
                const int cx = std::clamp(int((wx + ox) / 32), 0, cw - 1), cy = std::clamp(int((wy + oy) / 32), 0, ch - 1);
                if (!(w.cell(cx, cy).flags & cf::kDriveOn) || (w.cell(cx, cy).flags & cf::kBuilding) || !nearB[std::size_t(cy) * cw + cx])
                    return false;  // (a building's solid cells keep the drive-on bit)
            }
        return true;
    };
    static const float ox[4] = {4, 0, 4, -4}, oy[4] = {0, 4, 4, 4};
    for (int y = 0; y <= ch * 8; ++y)
        for (int x = 0; x <= cw * 8; ++x) {
            const float wx = float(x) * 4, wy = float(y) * 4;
            if (!ok(wx, wy)) continue;
            ++s.samples;
            const float h = t.height(wx, wy), d = h - nat.height(wx, wy);
            if (d > s.mound.v) s.mound = {d, x * 4, y * 4};
            if (-d > s.dip.v) s.dip = {-d, x * 4, y * 4};
            for (int k = 0; k < 4; ++k) {
                const float nx = wx + ox[k], ny = wy + oy[k];
                if (!ok(nx, ny)) continue;
                const float len = k >= 2 ? 5.657f : 4.0f;
                const float sl = std::fabs(t.height(nx, ny) - h) / len;
                // Only what the foundations add (the natural ground's own slope is the guide's business).
                const float slN = std::fabs(nat.height(nx, ny) - nat.height(wx, wy)) / len;
                if (sl > render3d::TerrainGuide::kReimRamp + 0.01f && sl > slN + 0.01f) ++s.steep;
                if (sl > 0.5f && sl > slN + 0.01f) ++s.steep50;
                if (sl > s.slope.v) s.slope = {sl, x * 4, y * 4};
            }
            // Hollow depth (measure()): in 8 directions the highest sample within 32 px above this one,
            // the lowest of those; the foundations' share = with minus without.
            auto depth = [&](const render3d::Terrain& tt) {
                static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
                const float h0 = tt.height(wx, wy);
                float dmin = 1e9f;
                for (int dd = 0; dd < 8; ++dd) {
                    float best = 0;
                    for (int r = 1; r <= 8; ++r) best = std::max(best, tt.height(wx + float(dx[dd] * r * 4), wy + float(dy[dd] * r * 4)) - h0);
                    dmin = std::min(dmin, best);
                }
                return dmin;
            };
            const float pd = depth(t) - depth(nat);
            if (pd > s.pit.v) s.pit = {pd, x * 4, y * 4};
        }
    return s;
}

// The player's construction yard (top-left cell), (-1, -1) if none.
std::pair<int, int> conyardOf(const game::World& w) {
    const auto& side = w.sides()[std::size_t(w.mySide())];
    for (int bi = side.firstBuilding; bi >= 0; bi = side.nextObject(bi)) {
        const game::Building& b = side.buildings[std::size_t(bi)];
        if (b.alive && w.buildingTemplate(b).specialBehavior == game::building_behaviour::kConstructionYard) return {b.tileX, b.tileY};
    }
    return {-1, -1};
}

// A building of `group` (the player's house version) at the first spot that fits, searched outwards
// from (px, py) up to `radius` cells. Returns the cell or (-1, -1).
std::pair<int, int> placeNear(game::World& w, int group, int px, int py, int radius, bool southFirst = false) {
    const auto& T = w.rules().templates->bin;
    const auto& side = w.sides()[std::size_t(w.mySide())];
    const auto bit = std::uint8_t(1u << (side.house & 0x1f));
    int type = -1;
    for (int i = 0; i < T.buildingCount; ++i)
        if (T.buildings[i].buildingGroup == group && ((T.buildings[i].ownerHouse & bit) || type == -1)) type = i;
    if (type < 0) return {-1, -1};
    for (int pass = southFirst ? 0 : 1; pass < 2; ++pass)  // pass 0: only at or below py
    for (int r = 0; r <= radius; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r || (pass == 0 && dy < 0)) continue;
                const int x = px + dx, y = py + dy;
                if (!w.canPlaceBuilding(w.mySide(), type, x, y)) continue;
                if (w.addBuilding(w.mySide(), type, x, y, false, true, true) < 0) continue;
                return {x, y};
            }
    return {-1, -1};
}

// --probe x,y: the cells around a world point (kind: S sand / R rock / C cliff (Terrain), d drive-on,
// c the guide's cliff cell) and the guide's height g (x100) / the mesh height (px) every 8 px.
void probe(const game::World& w, const render3d::Terrain& t, const render3d::TerrainGuide& guide, int px, int py) {
    const int cx = px / 32, cy = py / 32;
    std::printf("  probe %d,%d (cell %d,%d) h=%.1f\n", px, py, cx, cy, t.height(float(px), float(py)));
    for (int y = cy - 2; y <= cy + 2; ++y) {
        std::printf("  cells %3d:", y);
        for (int x = cx - 3; x <= cx + 3; ++x) {
            if (x < 0 || y < 0 || x >= w.width() || y >= w.height()) { std::printf("    "); continue; }
            const std::size_t i = std::size_t(y) * w.width() + x;
            std::printf(" %c%c%c", "SRC"[t.cellKinds()[i]], (w.cell(x, y).flags & game::cell_flags::kDriveOn) ? 'd' : '-',
                        guide.cliffCells()[i] ? 'c' : '.');
        }
        std::printf("\n");
    }
    for (int y = py - 40; y <= py + 40; y += 8) {
        std::printf("  g y%5d:", y);
        for (int x = px - 48; x <= px + 48; x += 8) std::printf(" %3d", int(guide.heightAt(float(x), float(y)) * 100 + 0.5f));
        std::printf("\n  h y%5d:", y);
        for (int x = px - 48; x <= px + 48; x += 8) std::printf(" %3d", int(std::lround(t.height(float(x), float(y)))));
        std::printf("\n");
    }
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    for (std::string item; std::getline(ss, item, ',');)
        if (!item.empty()) out.push_back(item);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::string data = "game/Dune 2000/data";
    std::vector<std::string> only;
    bool foundations = false, before = false, refinery = false, placeAll = false, measureF = false;
    std::string saveDir;
    int probeX = -1, probeY = -1;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!std::strcmp(argv[i], "--maps") && i + 1 < argc) only = split(argv[++i]);
        else if (!std::strcmp(argv[i], "--foundations")) foundations = true;
        else if (!std::strcmp(argv[i], "--before")) before = true;  // the guide without Params::reimagined
        else if (!std::strcmp(argv[i], "--refinery")) refinery = foundations = true;
        else if (!std::strcmp(argv[i], "--place-all")) refinery = placeAll = foundations = true;
        else if (!std::strcmp(argv[i], "--measure-foundations")) measureF = foundations = true;  // the map's own buildings
        else if (!std::strcmp(argv[i], "--write-save") && i + 1 < argc) saveDir = argv[++i];
        else if (!std::strcmp(argv[i], "--probe") && i + 1 < argc) std::sscanf(argv[++i], "%d,%d", &probeX, &probeY);
    }
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_WARN);
    SDL_Surface* surf = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    if (!ren) {
        std::fprintf(stderr, "no software renderer: %s\n", SDL_GetError());
        return 1;
    }
    struct Job {
        std::string map;
        bool skirmish;
    };
    std::vector<Job> jobs;
    const platform::Catalog cat = platform::Catalog::load(data);
    for (const auto& house : cat.campaign)
        for (const auto& m : house) jobs.push_back({m.map, false});
    for (const auto& m : cat.skirmish) jobs.push_back({m.file, true});
    if (!only.empty()) {
        std::vector<Job> sel;
        for (const auto& o : only) {
            bool found = false;
            for (const auto& j : jobs)
                if (SDL_strcasecmp(j.map.c_str(), o.c_str()) == 0) sel.push_back(j), found = true;
            if (!found) sel.push_back({o, false});
        }
        jobs = sel;
    }
    std::printf("%-14s %5s | %-34s | %-34s\n", "map", "cells", "plain faithful: slope(n>1) pit(n>8px) cellpit(n>8px)",
                "reimagined: slope(n>1) pit(n>8px) cellpit(n>8px)");
    for (const Job& j : jobs) {
        platform::GameStart gs;
        gs.map = j.map;
        gs.skirmish = j.skirmish;
        platform::GameData g;
        try {
            g = platform::loadGameData(data, gs);
        } catch (const std::exception& e) {
            std::printf("%-14s load failed: %s\n", j.map.c_str(), e.what());
            continue;
        }
        std::unique_ptr<game::World> w;
        if (j.skirmish) w = std::make_unique<game::World>(g.map, g.mission, g.tileAtr, g.tileData, platform::rulesOf(g), platform::skirmishSetupOf(g));
        else w = std::make_unique<game::World>(g.map, g.mission, g.tileAtr, g.tileData, platform::rulesOf(g), g.playerSide);
        if (refinery) {
            if (conyardOf(*w).first < 0) {  // a skirmish starts with an MCV: deploy it
                const auto& side = w->sides()[std::size_t(w->mySide())];
                for (int ui = side.firstUnit; ui >= 0; ui = side.nextObject(ui))
                    if (w->unitTemplate(side.units[std::size_t(ui)]).specialBehavior == game::behaviour::kMcv) {
                        w->orderDeploy(w->mySide(), ui);
                        break;
                    }
                for (int k = 0; k < 200 && conyardOf(*w).first < 0; ++k) w->tick();
            }
            const auto [yx, yy] = conyardOf(*w);
            if (yx < 0) {
                std::printf("%-14s no construction yard\n", j.map.c_str());
                continue;
            }
            {  // the starting units stand south of the yard: move them out of the way (north-west of it)
                const auto& side = w->sides()[std::size_t(w->mySide())];
                for (int ui = side.firstUnit; ui >= 0; ui = side.nextObject(ui)) w->orderMove(w->mySide(), ui, yx - 4, yy - 2);
                for (int k = 0; k < 300; ++k) w->tick();
            }
            if (std::getenv("SCAN_FLAGS"))
                for (int y = yy - 1; y <= yy + 7; ++y) {
                    for (int x = yx - 5; x <= yx + 4; ++x) {
                        const std::uint32_t f = (x >= 0 && x < w->width()) ? w->cell(x, y).flags : 0;
                        std::printf(" %c%c%c", (f & game::cell_flags::kBuildOn) ? 'B' : '-', (f & game::cell_flags::kOccupied) ? 'o' : '.',
                                    (f & game::cell_flags::kBuilding) ? '#' : '.');
                    }
                    std::printf("  y%d\n", y);
                }
            const auto [rx, ry] = placeNear(*w, game::group_ids::kRefinery, yx - 1, yy + 2, 6, true);
            std::printf("%-14s yard %d,%d refinery %d,%d\n", j.map.c_str(), yx, yy, rx, ry);
            if (placeAll) {
                const auto& T = w->rules().templates->bin;
                std::vector<int> groups;
                for (int i = 0; i < T.buildingCount; ++i) {
                    const int gr = T.buildings[i].buildingGroup, sb = T.buildings[i].specialBehavior;
                    if (gr == game::group_ids::kRefinery || sb == game::building_behaviour::kWall ||
                        sb == game::building_behaviour::kConcrete || sb == game::building_behaviour::kConstructionYard)
                        continue;
                    if (std::find(groups.begin(), groups.end(), gr) == groups.end()) groups.push_back(gr);
                }
                for (int gr : groups) placeNear(*w, gr, yx, yy, 9);
            }
            if (!saveDir.empty()) {
                platform::setSavesDirOverride(saveDir);
                game::SaveMeta m = platform::saveMetaOf(gs, w->mySide());
                m.cameraX = float(yx * 32 + 48) - 1280.0f / 4.0f;
                m.cameraY = float(yy * 32 + 96) - 800.0f / 4.0f;
                m.cameraZoom = 2.0f;
                std::string err;
                if (!platform::writeSaveSlot(1, game::writeSaveFile(m, *w), err)) std::printf("save failed: %s\n", err.c_str());
            }
        }
        render::TileAtlas atlas(ren, g.tiles, false);
        const std::vector<float> rgb = render3d::DesignedTerrain::tileMeans(&atlas);
        Stats st[2];
        for (int r = 0; r < 2; ++r) {
            render3d::TerrainGuide guide;
            render3d::TerrainGuide::Params p;  // the 3D mode's (DesignedTerrain::prepareGuide)
            p.faceSouth = 12.0f;
            p.faceNorth = 8.0f;
            p.faceEastWest = 9.0f;
            p.reimagined = r == 1 && !before;
            if (!guide.build(*w, &atlas, p)) {
                std::printf("%-14s no guide\n", j.map.c_str());
                break;
            }
            render3d::Terrain t;
            t.enableFoundations(foundations);
            t.setReimagined(r == 1);
            const Uint64 t0 = SDL_GetTicksNS();
            t.build(*w, rgb, true, &guide);
            const double msFound = double(SDL_GetTicksNS() - t0) / 1e6;
            st[r] = measure(*w, t);
            if (refinery || measureF) {
                render3d::Terrain nat;
                nat.enableFoundations(false);
                nat.setReimagined(r == 1);
                const Uint64 t1 = SDL_GetTicksNS();
                nat.build(*w, rgb, true, &guide);
                const double msNat = double(SDL_GetTicksNS() - t1) / 1e6;
                const FoundStats fs = measureFoundations(*w, t, nat);
                if (refinery && r == 1) {  // the cost of one more building in play (Terrain::updateFoundations)
                    const auto [yx, yy] = conyardOf(*w);
                    const auto [px, py] = placeNear(*w, game::group_ids::kRefinery, yx, yy, 12);
                    const Uint64 t2 = SDL_GetTicksNS();
                    const bool moved = t.updateFoundations(*w);
                    std::printf("  one more refinery at %d,%d: update %.1f ms (%s)\n", px, py, double(SDL_GetTicksNS() - t2) / 1e6,
                                moved ? "changed" : "unchanged");
                }
                if (std::getenv("SCAN_LIST") && r == 1)  // every building: cell, footprint, level, natural min / max under it
                    for (const auto& side : w->sides())
                        for (int bi = side.firstBuilding; bi >= 0; bi = side.nextObject(bi)) {
                            const game::Building& b = side.buildings[std::size_t(bi)];
                            if (!b.alive) continue;
                            const std::uint32_t mask = w->buildingTemplate(b).tilesOccupiedAll & 0xFFFFu;
                            float lo = 1e9f, hi = -1e9f, F = -1;
                            for (int k = 0; k < 16; ++k)
                                if (mask & (1u << k)) {
                                    const int cx = b.tileX + k % 4, cy = b.tileY + k / 4;
                                    F = t.foundationAt(cx, cy, -1);
                                    for (int sy = 0; sy <= 8; ++sy)
                                        for (int sx = 0; sx <= 8; ++sx) {
                                            const float h = nat.height(float(cx * 32 + sx * 4), float(cy * 32 + sy * 4));
                                            lo = std::min(lo, h), hi = std::max(hi, h);
                                        }
                                }
                            std::printf("  building type %3d at %d,%d mask %04x solid %04x drive %d: level %.1f natural %.1f..%.1f\n", b.type,
                                        b.tileX, b.tileY, mask, unsigned(w->buildingTemplate(b).tilesOccupiedSolid & 0xFFFFu),
                                        int((w->cell(b.tileX, b.tileY).flags & game::cell_flags::kDriveOn) != 0), F, lo, hi);
                        }
                if (const char* e = std::getenv("SCAN_DIFF"); e && r == 1) {  // "x,y": (h - natural) / natural every 16 px, +-160 px
                    int px = 0, py = 0;
                    std::sscanf(e, "%d,%d", &px, &py);
                    for (int y = py - 160; y <= py + 160; y += 16) {
                        std::printf("  y%5d:", y);
                        for (int x = px - 160; x <= px + 160; x += 16)
                            std::printf(" %3d/%-3d", int(std::lround(t.height(float(x), float(y)) - nat.height(float(x), float(y)))),
                                        int(std::lround(nat.height(float(x), float(y)))));
                        std::printf("\n");
                    }
                }
                std::printf("  %s foundations: slope %.2f @%d,%d  added steep >%.2f: %d, >0.5: %d of %d  mound %.1f @%d,%d  dip %.1f @%d,%d  pit +%.1f @%d,%d"
                            "  build %.0f ms (no foundations %.0f)\n",
                            r ? "reimagined" : "plain     ", fs.slope.v, fs.slope.x, fs.slope.y, render3d::TerrainGuide::kReimRamp,
                            fs.steep, fs.steep50, fs.samples, fs.mound.v, fs.mound.x, fs.mound.y, fs.dip.v, fs.dip.x, fs.dip.y,
                            fs.pit.v, fs.pit.x, fs.pit.y, msFound, msNat);
            }
            if (probeX >= 0) {
                std::printf("%s %s\n", j.map.c_str(), r ? "reimagined" : "plain faithful");
                probe(*w, t, guide, probeX, probeY);
            }
        }
        auto f = [](const Stats& s) {
            char b[160];
            std::snprintf(b, sizeof b, "%5.2f(%4d) @%5d,%-5d %5.1f(%4d) @%5d,%-5d %5.1f(%3d) @%5d,%-5d", s.slope.v, s.walls,
                          s.slope.x, s.slope.y, s.pit.v, s.pits, s.pit.x, s.pit.y, s.cell.v, s.cells, s.cell.x, s.cell.y);
            return std::string(b);
        };
        std::printf("%-14s %5d | %s | %s\n", j.map.c_str(), w->width() * w->height(), f(st[0]).c_str(), f(st[1]).c_str());
        std::fflush(stdout);
    }
    SDL_DestroyRenderer(ren);
    SDL_DestroySurface(surf);
    return 0;
}

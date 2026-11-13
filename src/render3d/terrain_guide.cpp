#include "render3d/terrain_guide.hpp"

#include "game/world.hpp"
#include "render/tileset.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace d2k::render3d {

namespace {

constexpr int P = TerrainGuide::kPx;
constexpr int PP = P * P;

// Colour-lock fallback when a map has no plain (buildable) rock cell: the stock tilesets' rock.
constexpr float kStockRock[3] = {204, 132, 68};

float lumOf(float r, float g, float b) { return 0.299f * r + 0.587f * g + 0.114f * b; }
float sat(float v) { return std::clamp(v, 0.0f, 1.0f); }

// What a cell is before its pixels are looked at.
enum CellClass : std::uint8_t {
    kPureSand,   // sand flag: all sand
    kPlainRock,  // build-on / concrete / building / wall: all plain rock
    kMixed       // shore, rough rock, cliffs: per pixel
};

// One tile's pixels, classified (32 x 32).
struct TileInfo {
    std::array<std::uint8_t, PP> sand;  // 1 = looks like sand
    std::array<std::uint8_t, PP> lum;   // 3x3-blurred luminance
    std::array<std::uint8_t, PP> rug;   // local contrast, 0 plain .. 255 broken
    std::array<std::int8_t, PP> tone;   // (lum - plain rock lum) / plain rock lum, -127 .. 127
    float sandFrac = 0;
};

// The map's sand / rock colour model.
struct Model {
    float sand[3], rock[3];
    float cs[2], cr[2];  // chromaticity (r, b) of the two
    float axis[2];       // (cs - cr) / |cs - cr|^2
    float lumSand, lumRock;
    float sdRock;        // typical 5x5 luminance std of plain rock
};

struct TileReader {
    const render::TileAtlas* atlas;
    void rgb(int tile, float (*out)[3]) const {  // out[PP][3]
        for (int y = 0; y < P; ++y)
            for (int x = 0; x < P; ++x) {
                const formats::Rgba c = atlas->pixel(tile, x, y);
                float* o = out[y * P + x];
                o[0] = float(c & 0xFF);
                o[1] = float((c >> 8) & 0xFF);
                o[2] = float((c >> 16) & 0xFF);
            }
    }
};

// 3x3 box blur inside one tile (edges clamped).
void box3(const float* in, float* out) {
    for (int y = 0; y < P; ++y)
        for (int x = 0; x < P; ++x) {
            float s = 0;
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i)
                    s += in[std::clamp(y + j, 0, P - 1) * P + std::clamp(x + i, 0, P - 1)];
            out[y * P + x] = s / 9.0f;
        }
}

// Luminance std in the 5x5 window around every pixel of a tile (edges clamped).
void localStd(const float* lum, float* out) {
    for (int y = 0; y < P; ++y)
        for (int x = 0; x < P; ++x) {
            float s = 0, s2 = 0;
            for (int j = -2; j <= 2; ++j)
                for (int i = -2; i <= 2; ++i) {
                    const float v = lum[std::clamp(y + j, 0, P - 1) * P + std::clamp(x + i, 0, P - 1)];
                    s += v;
                    s2 += v * v;
                }
            const float m = s / 25.0f;
            out[y * P + x] = std::sqrt(std::max(0.0f, s2 / 25.0f - m * m));
        }
}

void classifyTile(const TileReader& rd, int tile, const Model& m, TileInfo& t) {
    static thread_local float rgb[PP][3], ch[3][PP], blur[3][PP], lumRaw[PP], sd[PP], rug0[PP], rug1[PP];
    rd.rgb(tile, rgb);
    for (int i = 0; i < PP; ++i) {
        for (int k = 0; k < 3; ++k) ch[k][i] = rgb[i][k];
        lumRaw[i] = lumOf(rgb[i][0], rgb[i][1], rgb[i][2]);
    }
    for (int k = 0; k < 3; ++k) box3(ch[k], blur[k]);
    localStd(lumRaw, sd);
    const float sdLo = m.sdRock * 1.25f + 1.0f, sdRange = std::max(6.0f, m.sdRock * 1.05f);
    int sandCount = 0;
    for (int i = 0; i < PP; ++i) {
        const float r = blur[0][i], g = blur[1][i], b = blur[2][i];
        const float sum = std::max(1.0f, r + g + b);
        const float tt = (r / sum - m.cr[0]) * m.axis[0] + (b / sum - m.cr[1]) * m.axis[1];  // 0 rock .. 1 sand
        const float L = lumOf(r, g, b);
        const bool sand = tt >= 0.5f && L >= 0.53f * m.lumRock;
        t.sand[std::size_t(i)] = sand ? 1 : 0;
        sandCount += sand ? 1 : 0;
        t.lum[std::size_t(i)] = std::uint8_t(std::clamp(L, 0.0f, 255.0f));
        // Tone: the pixel's own luminance, thin features (1 px pale rims, crevices) a little
        // emphasised so they survive the enlargement.
        const float Lt = lumRaw[i] + 0.3f * (lumRaw[i] - L);
        t.tone[std::size_t(i)] = std::int8_t(std::clamp((Lt - m.lumRock) / m.lumRock, -1.0f, 1.0f) * 127.0f);
        rug0[i] = std::max(sat((sd[i] - sdLo) / sdRange), sat((0.8f * m.lumRock - L) / (0.25f * m.lumRock)));
    }
    box3(rug0, rug1);
    for (int i = 0; i < PP; ++i) t.rug[std::size_t(i)] = std::uint8_t(sat(rug1[i]) * 255.0f);
    t.sandFrac = float(sandCount) / float(PP);
}

}  // namespace

void TerrainGuide::clear() {
    valid_ = false;
    texels_.clear();
    caster_.clear();
    cliffCells_.clear();
    stones_.clear();
    gw_ = gh_ = cw_ = ch_ = 0;
}

bool TerrainGuide::build(const game::World& w, const render::TileAtlas* atlas, const Params& prm) {
    namespace cf = game::cell_flags;
    clear();
    if (!atlas || atlas->tileCount() <= 0 || w.width() <= 0 || w.height() <= 0) return false;
    if (std::getenv("D2K_GUIDE_OFF")) {  // debugging: as on a tileset that cannot be classified
        SDL_Log("Terrain (faithful): D2K_GUIDE_OFF - using the cell-based layout");
        return false;
    }
    const Uint64 t0 = SDL_GetTicksNS();
    cw_ = w.width();
    ch_ = w.height();
    const int nCells = cw_ * ch_;
    const int tileCount = atlas->tileCount();
    const TileReader rd{atlas};

    // ---- cell classes + the map's sand / rock colours -------------------------------------
    std::vector<std::uint8_t> cls(static_cast<std::size_t>(nCells), kMixed);
    std::vector<int> tileOf(static_cast<std::size_t>(nCells), 0);
    std::vector<int> sandUse(static_cast<std::size_t>(tileCount), 0), rockUse(static_cast<std::size_t>(tileCount), 0);
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            const game::Cell& c = w.cell(x, y);
            const std::size_t i = std::size_t(y) * cw_ + x;
            int tile = c.tile & 0x0FFF;
            if (tile >= tileCount) tile = 0;
            tileOf[i] = tile;
            const bool built = (c.flags & (cf::kBuilding | cf::kWall | cf::kConcrete)) != 0;
            if (c.flags & cf::kSand) {
                cls[i] = kPureSand;
                if (c.spice() == 0 && !built && (c.flags & (cf::kDriveOn | cf::kWalkOn))) ++sandUse[std::size_t(tile)];
            } else if (built || (c.flags & cf::kBuildOn)) {
                cls[i] = kPlainRock;
                if (!built) ++rockUse[std::size_t(tile)];
            }
        }
    Model m{};
    {
        static thread_local float rgb[PP][3], lum[PP], sd[PP];
        double s[2][3] = {}, n[2] = {}, sdSum = 0, sdN = 0;
        int rockTilesMeasured = 0;
        for (int t = 0; t < tileCount; ++t) {
            const int use[2] = {sandUse[std::size_t(t)], rockUse[std::size_t(t)]};
            if (use[0] == 0 && use[1] == 0) continue;
            rd.rgb(t, rgb);
            double ts[3] = {};
            for (int i = 0; i < PP; ++i)
                for (int k = 0; k < 3; ++k) ts[k] += rgb[i][k];
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 3; ++k) s[c][k] += ts[k] / PP * use[c];
            n[0] += use[0];
            n[1] += use[1];
            if (use[1] > 0 && rockTilesMeasured < 12) {  // plain rock's own grain
                for (int i = 0; i < PP; ++i) lum[i] = lumOf(rgb[i][0], rgb[i][1], rgb[i][2]);
                localStd(lum, sd);
                for (int i = 0; i < PP; ++i) sdSum += sd[i];
                sdN += PP;
                ++rockTilesMeasured;
            }
        }
        if (n[0] < 8) {
            SDL_Log("Terrain (faithful): no plain sand cells on this map - using the cell-based layout");
            return false;
        }
        for (int k = 0; k < 3; ++k) {
            m.sand[k] = float(s[0][k] / n[0]);
            m.rock[k] = n[1] >= 8 ? float(s[1][k] / n[1]) : kStockRock[k];
        }
        m.sdRock = sdN > 0 ? float(sdSum / sdN) : 13.0f;
        const float ss = m.sand[0] + m.sand[1] + m.sand[2], rs = m.rock[0] + m.rock[1] + m.rock[2];
        m.cs[0] = m.sand[0] / ss;
        m.cs[1] = m.sand[2] / ss;
        m.cr[0] = m.rock[0] / rs;
        m.cr[1] = m.rock[2] / rs;
        const float dx = m.cs[0] - m.cr[0], dy = m.cs[1] - m.cr[1];
        const float d2 = dx * dx + dy * dy;
        if (d2 < 0.04f * 0.04f) {
            SDL_Log("Terrain (faithful): sand and rock are too close in colour on this tileset - using the cell-based layout");
            return false;
        }
        m.axis[0] = dx / d2;
        m.axis[1] = dy / d2;
        m.lumSand = lumOf(m.sand[0], m.sand[1], m.sand[2]);
        m.lumRock = lumOf(m.rock[0], m.rock[1], m.rock[2]);
    }
    std::memcpy(sand_, m.sand, sizeof sand_);
    std::memcpy(rock_, m.rock, sizeof rock_);

    // ---- per-pixel fields at the original's resolution ------------------------------------
    const int W = cw_ * P, H = ch_ * P;
    const std::size_t N = std::size_t(W) * H;
    // sand: 0 rock, 1 sand (final), 2 sand candidate (a mixed tile's sand-looking pixel)
    std::vector<std::uint8_t> sand(N, 0), rug(N, 0), lum(N, std::uint8_t(std::clamp(m.lumRock, 0.0f, 255.0f)));
    std::vector<std::int8_t> tone(N, 0);
    std::vector<std::unique_ptr<TileInfo>> tiles(static_cast<std::size_t>(tileCount));
    for (int cy = 0; cy < ch_; ++cy)
        for (int cx = 0; cx < cw_; ++cx) {
            const std::size_t ci = std::size_t(cy) * cw_ + cx;
            const std::size_t base = std::size_t(cy) * P * W + std::size_t(cx) * P;
            if (cls[ci] == kPureSand) {
                for (int y = 0; y < P; ++y) std::memset(&sand[base + std::size_t(y) * W], 1, P);
                continue;
            }
            if (cls[ci] == kPlainRock) continue;
            auto& ti = tiles[std::size_t(tileOf[ci])];
            if (!ti) {
                ti = std::make_unique<TileInfo>();
                classifyTile(rd, tileOf[ci], m, *ti);
            }
            // A mixed cell that is all but sand (crater floors, sand with a few specks) is open sand.
            const std::uint8_t mark = ti->sandFrac >= 0.85f ? 1 : 2;
            for (int y = 0; y < P; ++y) {
                const std::size_t row = base + std::size_t(y) * W;
                for (int x = 0; x < P; ++x) {
                    const std::size_t k = std::size_t(y) * P + x;
                    sand[row + x] = ti->sand[k] ? mark : 0;
                }
                std::memcpy(&rug[row], &ti->rug[std::size_t(y) * P], P);
                std::memcpy(&lum[row], &ti->lum[std::size_t(y) * P], P);
                std::memcpy(&tone[row], &ti->tone[std::size_t(y) * P], P);
            }
        }
    // Sand must be connected to open sand: flood from the final sand through the candidates.
    {
        std::vector<std::uint32_t> q;
        q.reserve(N / 16);
        auto tryPush = [&](std::size_t i) {
            if (sand[i] == 2) {
                sand[i] = 1;
                q.push_back(std::uint32_t(i));
            }
        };
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const std::size_t i = std::size_t(y) * W + x;
                if (sand[i] != 1) continue;
                if (x > 0) tryPush(i - 1);
                if (x + 1 < W) tryPush(i + 1);
                if (y > 0) tryPush(i - std::size_t(W));
                if (y + 1 < H) tryPush(i + std::size_t(W));
            }
        while (!q.empty()) {
            const std::size_t i = q.back();
            q.pop_back();
            const int x = int(i % std::size_t(W)), y = int(i / std::size_t(W));
            if (x > 0) tryPush(i - 1);
            if (x + 1 < W) tryPush(i + 1);
            if (y > 0) tryPush(i - std::size_t(W));
            if (y + 1 < H) tryPush(i + std::size_t(W));
        }
        for (std::size_t i = 0; i < N; ++i)
            if (sand[i] == 2) sand[i] = 0;  // pale facets inside the rock
    }

    // ---- cells: what the simulation lets vehicles use ---------------------------------------
    std::vector<float> rockFrac(static_cast<std::size_t>(nCells), 0.0f);
    for (int cy = 0; cy < ch_; ++cy)
        for (int cx = 0; cx < cw_; ++cx) {
            int n = 0;
            const std::size_t base = std::size_t(cy) * P * W + std::size_t(cx) * P;
            for (int y = 0; y < P; ++y)
                for (int x = 0; x < P; ++x) n += sand[base + std::size_t(y) * W + x] ? 0 : 1;
            rockFrac[std::size_t(cy) * cw_ + cx] = float(n) / float(PP);
        }
    // A cell's part in the relief:
    //   kCSand   open sand (or a mixed cell that is all but sand): level 0;
    //   kCCliff  rock no VEHICLE can use - the drive-on bit is clear, the terrain test of
    //            CanUnitUseSquare 0x45d990 for wheeled and tracked units (infantry-only cells
    //            included). The only cells that may carry the raised cliff relief; how tall it
    //            stands there follows the original's own dark faces (below);
    //   kCShore  vehicle-passable rock next to sand: level ground;
    //   kCFree   other vehicle-passable rock (the plateau): level, gently raised behind cliffs.
    enum : std::uint8_t { kCSand, kCCliff, kCShore, kCFree };
    std::vector<std::uint8_t> ck(static_cast<std::size_t>(nCells), kCFree);
    std::vector<std::uint8_t> walkOnly(static_cast<std::size_t>(nCells), 0);
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            const std::size_t i = std::size_t(y) * cw_ + x;
            const game::Cell& c = w.cell(x, y);
            const bool drive = (c.flags & cf::kDriveOn) != 0;
            if (cls[i] == kPureSand || rockFrac[i] < 0.04f) ck[i] = kCSand;
            else if (cls[i] == kMixed && !drive) ck[i] = rockFrac[i] > 0.2f ? kCCliff : kCSand;
            walkOnly[i] = cls[i] == kMixed && !drive && (c.flags & cf::kWalkOn);
        }
    auto ckAt = [&](int x, int y) { return ck[std::size_t(std::clamp(y, 0, ch_ - 1)) * cw_ + std::size_t(std::clamp(x, 0, cw_ - 1))]; };
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            std::uint8_t& k = ck[std::size_t(y) * cw_ + x];
            if (k != kCFree) continue;
            if (ckAt(x - 1, y) == kCSand || ckAt(x + 1, y) == kCSand || ckAt(x, y - 1) == kCSand || ckAt(x, y + 1) == kCSand)
                k = kCShore;
        }
    // Every cell no vehicle can use, bar the ones flagged as sand (the shader's mask, the
    // boulders) - also the mostly-sand ones (loose rocks on the sand) and the ones whose art is
    // no rock at all (wrecks, bones), which take no part in the relief: a blocked cell must
    // never look like open ground.
    cliffCells_.assign(static_cast<std::size_t>(nCells), 0);
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            const std::size_t i = std::size_t(y) * cw_ + x;
            cliffCells_[i] = cls[i] == kMixed && !(w.cell(x, y).flags & cf::kDriveOn) ? 1 : 0;
        }
    // Cliff cells in groups (4-connected): a group that touches no sand cell lies on the plateau
    // (an outcrop: rough rock and boulders among the passable rock). It has no outline of its
    // own to rise from - a face along its cell borders would be a straight line -, so it lies at
    // the plateau's level and reads through its dark rock and its boulders.
    std::vector<std::uint8_t> interior(static_cast<std::size_t>(nCells), 0);
    {
        std::vector<std::uint8_t> seen(static_cast<std::size_t>(nCells), 0);
        std::vector<int> comp;
        for (int start = 0; start < nCells; ++start) {
            if (ck[std::size_t(start)] != kCCliff || seen[std::size_t(start)]) continue;
            comp.clear();
            comp.push_back(start);
            seen[std::size_t(start)] = 1;
            bool touchesSand = false;
            for (std::size_t qi = 0; qi < comp.size(); ++qi) {
                const int x = comp[qi] % cw_, y = comp[qi] / cw_;
                for (int j = -1; j <= 1; ++j)
                    for (int k = -1; k <= 1; ++k) {
                        const int nx = x + k, ny = y + j;
                        if (nx < 0 || ny < 0 || nx >= cw_ || ny >= ch_ || (!j && !k)) continue;
                        const std::size_t n = std::size_t(ny) * cw_ + nx;
                        if (ck[n] == kCSand) touchesSand = true;
                        if ((j == 0 || k == 0) && ck[n] == kCCliff && !seen[n]) {
                            seen[n] = 1;
                            comp.push_back(int(n));
                        }
                    }
            }
            if (!touchesSand)
                for (int c : comp) interior[std::size_t(c)] = 1;
        }
    }

    // ---- which cliff cells stand tall: the original's faces ---------------------------------
    // The original shows a cliff as a dark FACE (shaded rock, crevices: clearly darker than
    // plain rock on average) below a light top, and low crumbling rock as light all over - also
    // on cells no unit can use. A FACE pixel: rock whose mean tone over 13 x 13 px is darker
    // than plain rock by 10 % (0) .. 24 % (1). (Stock tilesets: the faces of cliffs, ridges and
    // mesas come out as closed bands, crumbling cliff ends and infantry-only rock stay clear.)
    std::vector<std::uint8_t> face8(N, 0);
    {
        constexpr int kR = 6;
        auto boxSum = [&](auto value) {  // sums over (2 kR + 1)^2 pixels, edges clamped to the map
            std::vector<std::int32_t> tmp(N), out(N);
            for (int y = 0; y < H; ++y) {
                const std::size_t row = std::size_t(y) * W;
                int run = 0;
                for (int x = -kR; x <= kR; ++x) run += value(row + std::size_t(std::clamp(x, 0, W - 1)));
                for (int x = 0; x < W; ++x) {
                    tmp[row + x] = run;
                    run += value(row + std::size_t(std::min(x + kR + 1, W - 1))) - value(row + std::size_t(std::max(x - kR, 0)));
                }
            }
            for (int x = 0; x < W; ++x) {
                int run = 0;
                for (int y = -kR; y <= kR; ++y) run += tmp[std::size_t(std::clamp(y, 0, H - 1)) * W + x];
                for (int y = 0; y < H; ++y) {
                    out[std::size_t(y) * W + x] = run;
                    run += tmp[std::size_t(std::min(y + kR + 1, H - 1)) * W + x] - tmp[std::size_t(std::max(y - kR, 0)) * W + x];
                }
            }
            return out;
        };
        const std::vector<std::int32_t> ts = boxSum([&](std::size_t i) { return sand[i] ? 0 : int(tone[i]); });
        const std::vector<std::int32_t> rs = boxSum([&](std::size_t i) { return sand[i] ? 0 : 1; });
        for (std::size_t i = 0; i < N; ++i) {
            if (sand[i] || rs[i] <= 0) continue;
            const float mean = float(ts[i]) / (float(rs[i]) * 127.0f);
            const float f = sat((-mean - 0.10f) / 0.14f);
            face8[i] = std::uint8_t(f * f * (3 - 2 * f) * 255.0f + 0.5f);
        }
    }
    // Per cliff cell: how much of its rock is face -> how sure it is a tall cliff (0 .. 1).
    std::vector<float> strength(static_cast<std::size_t>(nCells), 0.0f);
    std::vector<std::uint8_t> crumbling(static_cast<std::size_t>(nCells), 0);
    for (int cy = 0; cy < ch_; ++cy)
        for (int cx = 0; cx < cw_; ++cx) {
            const std::size_t ci = std::size_t(cy) * cw_ + cx;
            if (ck[ci] != kCCliff) continue;
            int sum = 0, n = 0;
            const std::size_t base = std::size_t(cy) * P * W + std::size_t(cx) * P;
            for (int y = 0; y < P; ++y)
                for (int x = 0; x < P; ++x) {
                    const std::size_t i = base + std::size_t(y) * W + x;
                    if (sand[i]) continue;
                    sum += face8[i];
                    ++n;
                }
            const float frac = n ? float(sum) / (255.0f * float(n)) : 0.0f;
            strength[ci] = sat((frac - 0.12f) / 0.22f);
            // Rock infantry walks over is never a cliff, nor is an outcrop on the plateau.
            if (walkOnly[ci] || interior[ci]) strength[ci] = 0.0f;
            // Crumbling rock at the edge: the rock's outline runs through the cell (it is part
            // sand) and the original paints (almost) no face there - it lies on the ground.
            crumbling[ci] = strength[ci] < 0.35f && rockFrac[ci] < 0.85f;
            if (crumbling[ci]) strength[ci] = 0.0f;
        }

    // ---- the level of every rock cell ------------------------------------------------------
    // 0 = the ground, 1 = a cliff top. Sand, vehicle-passable shores and crumbling edges: 0. A
    // cliff cell with faces: 1. Everything else is relaxed between them (Laplace): the passable
    // plateau behind tall cliffs is level with them and slopes away gently; cliff cells WITHOUT
    // faces inside the rock take their neighbours' level - the light top of a mesa stands as
    // tall as its rim, a cliff tapers off towards a crumbling end.
    std::vector<float> level(static_cast<std::size_t>(nCells), 0.0f);
    for (int i = 0; i < nCells; ++i)
        if (ck[std::size_t(i)] == kCCliff) level[std::size_t(i)] = strength[std::size_t(i)];
        else if (ck[std::size_t(i)] == kCFree) level[std::size_t(i)] = 0.5f;
    auto levelAt = [&](int x, int y) { return level[std::size_t(std::clamp(y, 0, ch_ - 1)) * cw_ + std::size_t(std::clamp(x, 0, cw_ - 1))]; };
    for (int it = 0; it < 200; ++it)
        for (int y = 0; y < ch_; ++y)
            for (int x = 0; x < cw_; ++x) {
                const std::size_t i = std::size_t(y) * cw_ + x;
                if ((ck[i] != kCFree && ck[i] != kCCliff) || crumbling[i]) continue;
                const float avg = 0.25f * (levelAt(x - 1, y) + levelAt(x + 1, y) + levelAt(x, y - 1) + levelAt(x, y + 1));
                level[i] = ck[i] == kCCliff ? strength[i] + (1.0f - strength[i]) * avg : avg;
            }
    // The level at a pixel: bilinear between the cell centres. For a passable cell only the
    // passable cells count (it lies level: no relief from a cliff beside it reaches in); for a
    // cliff cell every rock cell counts (it tapers towards lower neighbours), sand cells never
    // (a cliff keeps its height up to its own outline).
    auto levelField = [&](int px, int py, bool passableOnly) {
        const float u = (float(px) + 0.5f) / P - 0.5f, v = (float(py) + 0.5f) / P - 0.5f;
        const int x0 = int(std::floor(u)), y0 = int(std::floor(v));
        const float fx = u - float(x0), fy = v - float(y0);
        float sum = 0, wsum = 0;
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                const int cx = std::clamp(x0 + i, 0, cw_ - 1), cy = std::clamp(y0 + j, 0, ch_ - 1);
                const std::size_t ci = std::size_t(cy) * cw_ + cx;
                if (ck[ci] == kCSand || (passableOnly && ck[ci] == kCCliff && !interior[ci])) continue;
                const float wgt = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
                sum += level[ci] * wgt;
                wsum += wgt;
            }
        return wsum > 1e-4f ? sum / wsum : 0.0f;
    };
    auto plateauAt = [&](int px, int py) { return levelField(px, py, true); };

    // ---- the faces' profile: an anisotropic distance into the rock (Dial's algorithm) ------
    // 1.0 = a full face width from the outline: a step that travels north (enters from the south
    // side) costs 1 / faceSouth per px, south 1 / faceNorth, east / west 1 / faceEastWest - a
    // face is wide on the south side, narrow on the north side (the original's oblique view).
    constexpr int kScale = 2048;
    constexpr int kCap = int(1.06f * kScale);
    constexpr std::uint16_t kInf = 0xFFFF;
    std::vector<std::uint16_t> dist(N, kInf);
    {
        int stepCost[8], sdx[8], sdy[8], ns = 0, maxCost = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const float len = std::sqrt(float(dx * dx + dy * dy));
                const float vy = float(dy) / len;
                const float wdt = prm.faceEastWest + (prm.faceSouth - prm.faceEastWest) * std::max(0.0f, -vy) +
                                  (prm.faceNorth - prm.faceEastWest) * std::max(0.0f, vy);
                sdx[ns] = dx;
                sdy[ns] = dy;
                stepCost[ns] = std::max(1, int(std::lround(len / std::max(1.0f, wdt) * kScale)));
                maxCost = std::max(maxCost, stepCost[ns]);
                ++ns;
            }
        std::vector<std::vector<std::uint32_t>> buckets(static_cast<std::size_t>(kCap + maxCost + 2));
        // Sources: sand pixels that touch rock.
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const std::size_t i = std::size_t(y) * W + x;
                if (!sand[i]) continue;
                dist[i] = 0;
                bool edge = false;
                for (int k = 0; k < 8 && !edge; ++k) {
                    const int nx = x + sdx[k], ny = y + sdy[k];
                    edge = nx >= 0 && ny >= 0 && nx < W && ny < H && !sand[std::size_t(ny) * W + nx];
                }
                if (edge) buckets[0].push_back(std::uint32_t(i));
            }
        for (int b = 0; b <= kCap; ++b) {
            auto& bucket = buckets[std::size_t(b)];
            for (std::size_t qi = 0; qi < bucket.size(); ++qi) {
                const std::size_t i = bucket[qi];
                if (dist[i] != b) continue;  // settled earlier with a smaller distance
                const int x = int(i % std::size_t(W)), y = int(i / std::size_t(W));
                for (int k = 0; k < 8; ++k) {
                    const int nx = x + sdx[k], ny = y + sdy[k];
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    const std::size_t j = std::size_t(ny) * W + nx;
                    if (sand[j]) continue;
                    const int nd = b + stepCost[k];
                    if (nd < dist[j] && nd <= kCap + maxCost) {
                        dist[j] = std::uint16_t(nd);
                        buckets[std::size_t(nd)].push_back(std::uint32_t(j));
                    }
                }
            }
            std::vector<std::uint32_t>().swap(bucket);
        }
    }
    // The profile across a face, 0 at the outline .. 1 at a full face width (and beyond).
    auto profile = [&](std::size_t i) { return dist[i] == kInf ? 1.0f : std::min(1.0f, float(dist[i]) / float(kScale)); };

    // Distance to the sand outline (px * 16; two-pass chamfer, 16 / 23).
    constexpr int kUnit = 16;
    std::vector<std::uint16_t> dOut(N, 0);
    {
        constexpr int kFar = 60000;
        for (std::size_t i = 0; i < N; ++i) dOut[i] = sand[i] ? 0 : std::uint16_t(kFar);
        auto relax = [&](std::size_t i, std::size_t j, int cost) {
            const int v = int(dOut[j]) + cost;
            if (v < int(dOut[i])) dOut[i] = std::uint16_t(v);
        };
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const std::size_t i = std::size_t(y) * W + x;
                if (!dOut[i]) continue;
                if (x > 0) relax(i, i - 1, 16);
                if (y > 0) {
                    relax(i, i - std::size_t(W), 16);
                    if (x > 0) relax(i, i - std::size_t(W) - 1, 23);
                    if (x + 1 < W) relax(i, i - std::size_t(W) + 1, 23);
                }
            }
        for (int y = H - 1; y >= 0; --y)
            for (int x = W - 1; x >= 0; --x) {
                const std::size_t i = std::size_t(y) * W + x;
                if (!dOut[i]) continue;
                if (x + 1 < W) relax(i, i + 1, 16);
                if (y + 1 < H) {
                    relax(i, i + std::size_t(W), 16);
                    if (x + 1 < W) relax(i, i + std::size_t(W) + 1, 23);
                    if (x > 0) relax(i, i + std::size_t(W) - 1, 23);
                }
            }
    }

    // ---- height + the colours of the broken rock's tone levels ------------------------------
    // Height (1 = a full cliff): all rock stands a little above the sand (a low step along its
    // outline). Vehicle-passable rock lies at its cells' level - no faces. A cliff cell rises
    // across its face (the profile) to its level; next to passable rock it meets that rock's
    // level over most of its own cell (no step against the plateau behind it, a sloping end
    // towards a shore), so no relief reaches a passable cell.
    std::vector<std::uint8_t> hm(N, 0), wall(prm.topDown ? N : 0, 0);
    {
        constexpr float kPlateau = 4.0f / 30.0f;  // of the cliff height
        constexpr float kStepPx = 8.0f;           // the step's width along the outline
        constexpr float kWall = 0.5f;             // topDown: the shadow casters stand half as tall
        constexpr float kEndPx = 24.0f;
        double sum[kLevels][3] = {}, cnt[kLevels] = {};
        for (int y = 0; y < H; ++y) {
            const int cy = y / P;
            for (int x = 0; x < W; ++x) {
                const std::size_t i = std::size_t(y) * W + x;
                if (sand[i]) continue;
                const int cx = x / P;
                const std::size_t ci = std::size_t(cy) * cw_ + cx;
                const float out = float(dOut[i]) / kUnit;  // px
                const float step = sat(out / kStepPx);
                const float base = kPlateau * step * step * (3 - 2 * step);
                float f, cast = 0;  // the relief 0..1 above the base; what casts a shadow
                if (ck[ci] != kCCliff || interior[ci]) {
                    f = plateauAt(x, y);
                } else {
                    f = cast = levelField(x, y, false) * profile(i);
                    // The passable rock cells around (within kEndPx): meet their level - the
                    // nearer a cell, the more it counts.
                    float dmin = 1e9f, lsum = 0, wsum = 0;
                    for (int j = -1; j <= 1; ++j)
                        for (int k = -1; k <= 1; ++k) {
                            const int nx = cx + k, ny = cy + j;
                            if ((!j && !k) || nx < 0 || ny < 0 || nx >= cw_ || ny >= ch_) continue;
                            const std::size_t n = std::size_t(ny) * cw_ + nx;
                            if (ck[n] == kCSand || (ck[n] == kCCliff && !interior[n])) continue;
                            const float ddx = float(std::max({nx * P - x, x - (nx * P + P - 1), 0}));
                            const float ddy = float(std::max({ny * P - y, y - (ny * P + P - 1), 0}));
                            const float dn = std::sqrt(ddx * ddx + ddy * ddy);
                            if (dn >= kEndPx) continue;
                            const float wgt = (kEndPx - dn) * (kEndPx - dn);
                            lsum += level[n] * wgt;
                            wsum += wgt;
                            dmin = std::min(dmin, dn);
                        }
                    if (wsum > 0) {
                        const float t = sat(dmin / kEndPx), sm = t * t * (3 - 2 * t);
                        f = (lsum / wsum) * (1 - sm) + f * sm;
                        cast *= sm;
                    }
                }
                hm[i] = std::uint8_t(sat(base + (1.0f - base) * f) * 255.0f + 0.5f);
                if (prm.topDown) wall[i] = std::uint8_t(sat(base + (kWall - base) * cast) * 255.0f + 0.5f);
                if (cls[ci] != kMixed || rug[i] < 96) continue;
                const float tn = float(tone[i]) / 127.0f;
                int lv = 0;
                while (lv < kLevels - 1 && tn >= kLevelEdge[lv]) ++lv;
                const formats::Rgba c = atlas->pixel(tileOf[ci], x % P, y % P);
                sum[lv][0] += double(c & 0xFF);
                sum[lv][1] += double((c >> 8) & 0xFF);
                sum[lv][2] += double((c >> 16) & 0xFF);
                cnt[lv] += 1;
            }
        }
        // Fallback for a level the map hardly has: the plain rock, darker / towards the sand.
        static constexpr float kMul[kLevels] = {0.27f, 0.57f, 0.82f, 1.0f, 1.0f};
        for (int lv = 0; lv < kLevels; ++lv)
            for (int k = 0; k < 3; ++k) {
                const float fb = lv == kLevels - 1 ? 0.35f * m.rock[k] + 0.65f * m.sand[k] : m.rock[k] * kMul[lv];
                level_[lv][k] = cnt[lv] >= 64 ? float(sum[lv][k] / cnt[lv]) : fb;
            }
    }

    // ---- boulders ---------------------------------------------------------------------------
    // Rock no vehicle can use that does not stand as a tall cliff must still read as blocked:
    // a boulder field on it (the rock meshes, with their own shadows) - on the original's dark
    // lumps first, then spread over the rock. Fewer and smaller ones where infantry can walk.
    {
        auto hashCell = [](int x, int y, int k) {
            std::uint32_t h = std::uint32_t(x) * 0x8da6b343u ^ std::uint32_t(y) * 0xd8163841u ^ std::uint32_t(k) * 0xcb1ab31fu;
            h ^= h >> 13;
            h *= 0x5bd1e995u;
            h ^= h >> 15;
            return float(h & 0xFFFFFF) / float(0xFFFFFF);
        };
        for (int cy = 0; cy < ch_; ++cy)
            for (int cx = 0; cx < cw_; ++cx) {
                const std::size_t sci = std::size_t(cy) * cw_ + cx;
                if (!cliffCells_[sci]) continue;
                // (A tall cell is a cliff: no boulders. Outcrops and infantry-only rock lie level.)
                const bool inf = walkOnly[sci] != 0;
                const float lvl = ck[sci] == kCCliff && !interior[sci] && !inf ? level[sci] : 0.0f;
                if (lvl >= 0.45f) continue;
                const float lowness = sat(1.0f - lvl * 2.2f);
                const int want = std::clamp(int(std::lround(float(inf ? 4 : 7) * rockFrac[sci] * (0.4f + 0.6f * lowness))), inf ? 2 : 3, 8);
                const float sizeLo = inf ? 6.0f : 9.0f, sizeHi = inf ? 9.0f : 15.0f;
                struct Cand {
                    int x, y, l;
                };
                Cand cand[64];
                int nc = 0;
                for (int y = 1; y < P - 1; ++y)
                    for (int x = 1; x < P - 1; ++x) {
                        const int px = cx * P + x, py = cy * P + y;
                        const std::size_t i = std::size_t(py) * W + px;
                        if (sand[i] || tone[i] > -38) continue;  // the original's dark lumps
                        const int l = lum[i];
                        bool minimum = true;
                        for (int j = -1; j <= 1 && minimum; ++j)
                            for (int k = -1; k <= 1 && minimum; ++k)
                                minimum = l <= lum[i + std::size_t(std::ptrdiff_t(j) * W + k)];
                        if (minimum && nc < 64) cand[nc++] = {px, py, l};
                    }
                std::sort(cand, cand + nc, [](const Cand& a, const Cand& b) { return a.l < b.l; });
                // Then hashed spots on the cell's rock, a little inside its outline; and, for a
                // cell with (almost) no rock - a wreck, bones, a few crumbs -, around its middle.
                for (int k = 0; k < 16 && nc < 64; ++k) {
                    const int px = cx * P + 2 + int(hashCell(cx, cy, 200 + k) * float(P - 4));
                    const int py = cy * P + 2 + int(hashCell(cx, cy, 300 + k) * float(P - 4));
                    const std::size_t i = std::size_t(py) * W + px;
                    if (!sand[i] && dOut[i] >= 3 * kUnit) cand[nc++] = {px, py, 255};
                }
                for (int k = 0; k < 8 && nc < 64; ++k) {
                    const int px = cx * P + 7 + int(hashCell(cx, cy, 400 + k) * float(P - 14));
                    const int py = cy * P + 7 + int(hashCell(cx, cy, 500 + k) * float(P - 14));
                    cand[nc++] = {px, py, 256};
                }
                auto blockedCell = [&](int x, int y) {
                    return x >= 0 && y >= 0 && x < cw_ && y < ch_ && cliffCells_[std::size_t(y) * cw_ + x] != 0;
                };
                const bool openW = !blockedCell(cx - 1, cy), openE = !blockedCell(cx + 1, cy);
                const bool openN = !blockedCell(cx, cy - 1), openS = !blockedCell(cx, cy + 1);
                Cand kept[8];
                int nk = 0;
                for (int i = 0; i < nc && nk < want; ++i) {
                    // (The first boulder of a cell is a big one: the cell reads as blocked.)
                    const float h7 = hashCell(cand[i].x, cand[i].y, 7);
                    const float sz = sizeLo + (sizeHi - sizeLo) * (nk == 0 ? 0.75f + 0.25f * h7 : h7);
                    // A boulder stays on blocked ground: it does not hang over into a cell that
                    // vehicles can use.
                    const int r = int(sz * 0.5f);
                    Cand c = cand[i];
                    if (openW) c.x = std::max(c.x, cx * P + r);
                    if (openE) c.x = std::min(c.x, cx * P + P - 1 - r);
                    if (openN) c.y = std::max(c.y, cy * P + r);
                    if (openS) c.y = std::min(c.y, cy * P + P - 1 - r);
                    bool far = true;
                    for (int k = 0; k < nk && far; ++k) {
                        const int dx = c.x - kept[k].x, dy = c.y - kept[k].y;
                        far = dx * dx + dy * dy >= 81;
                    }
                    if (!far) continue;
                    kept[nk++] = c;
                    stones_.push_back({float(c.x), float(c.y) + 0.5f, sz});
                }
            }
    }

    // ---- pack: 3x3 box blur of rock + height, then down to `res` texels per cell ---------
    res_ = prm.res == 16 || prm.res == 32 ? prm.res : (std::max(cw_, ch_) <= 96 ? 32 : 16);
    if (const char* e = std::getenv("D2K_GUIDE_RES")) {
        const int r = std::atoi(e);
        if (r == 16 || r == 32) res_ = r;
    }
    const int f = P / res_;  // 1 or 2 original pixels per guide texel
    gw_ = cw_ * res_;
    gh_ = ch_ * res_;
    texels_.assign(std::size_t(gw_) * gh_, 0);
    auto blur3 = [&](const std::vector<std::uint8_t>& src, bool rockMask) {
        // Horizontal then vertical 3-tap box; rockMask: the source is !sand (0 / 255).
        std::vector<std::uint8_t> tmp(N), out(N);
        auto val = [&](std::size_t i) -> int { return rockMask ? (sand[i] ? 0 : 255) : src[i]; };
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const std::size_t i = std::size_t(y) * W + x;
                const int a = val(x > 0 ? i - 1 : i), b = val(i), c = val(x + 1 < W ? i + 1 : i);
                tmp[i] = std::uint8_t((a + b + c + 1) / 3);
            }
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const std::size_t i = std::size_t(y) * W + x;
                const int a = tmp[y > 0 ? i - std::size_t(W) : i], b = tmp[i], c = tmp[y + 1 < H ? i + std::size_t(W) : i];
                out[i] = std::uint8_t((a + b + c + 1) / 3);
            }
        return out;
    };
    const std::vector<std::uint8_t> rockB = blur3(sand, true);
    const std::vector<std::uint8_t> hmB = blur3(hm, false);
    if (prm.topDown) {
        caster_.assign(std::size_t(gw_) * gh_, 0);
        for (int gy = 0; gy < gh_; ++gy)
            for (int gx = 0; gx < gw_; ++gx) {
                int h = 0;
                for (int j = 0; j < f; ++j)
                    for (int i = 0; i < f; ++i) h += wall[std::size_t(gy * f + j) * W + std::size_t(gx * f + i)];
                caster_[std::size_t(gy) * gw_ + gx] = std::uint8_t(h / (f * f));
            }
    }
    for (int gy = 0; gy < gh_; ++gy)
        for (int gx = 0; gx < gw_; ++gx) {
            int r = 0, h = 0, t = 0, g = 0, n = 0, nr = 0;
            for (int j = 0; j < f; ++j)
                for (int i = 0; i < f; ++i) {
                    const std::size_t k = std::size_t(gy * f + j) * W + std::size_t(gx * f + i);
                    r += rockB[k];
                    h += hmB[k];
                    ++n;
                    if (!sand[k]) {
                        t += tone[k];
                        g += rug[k];
                        ++nr;
                    }
                }
            const std::uint32_t R = std::uint32_t(r / n), G = std::uint32_t(h / n);
            const std::uint32_t B = std::uint32_t(std::clamp(128 + (nr ? t / nr : 0), 0, 255));
            const std::uint32_t A = std::uint32_t(nr ? g / nr : 0);
            texels_[std::size_t(gy) * gw_ + gx] = R | (G << 8) | (B << 16) | (A << 24);
        }
    valid_ = true;
    SDL_Log("Terrain (faithful): guide %dx%d (%d per cell) from the map's tiles in %.0f ms - sand %.0f,%.0f,%.0f rock "
            "%.0f,%.0f,%.0f, broken rock %.0f,%.0f,%.0f / %.0f,%.0f,%.0f / %.0f,%.0f,%.0f / %.0f,%.0f,%.0f / %.0f,%.0f,%.0f, "
            "%zu stones",
            gw_, gh_, res_, double(SDL_GetTicksNS() - t0) / 1e6, m.sand[0], m.sand[1], m.sand[2], m.rock[0], m.rock[1],
            m.rock[2], level_[0][0], level_[0][1], level_[0][2], level_[1][0], level_[1][1], level_[1][2], level_[2][0],
            level_[2][1], level_[2][2], level_[3][0], level_[3][1], level_[3][2], level_[4][0], level_[4][1], level_[4][2],
            stones_.size());
    if (const char* dir = std::getenv("D2K_GUIDE_DUMP")) {
        dump(dir);
        // The cells as the relief sees them (one pixel per cell), for the passability audit:
        // R 255 = rock no vehicle can use; G 255 infantry-only, 128 crumbling edge (on the
        // ground), 64 outcrop on the plateau; B = the cell's level (255 = a full cliff).
        if (SDL_Surface* s = SDL_CreateSurface(cw_, ch_, SDL_PIXELFORMAT_RGB24)) {
            for (int y = 0; y < ch_; ++y) {
                Uint8* row = static_cast<Uint8*>(s->pixels) + std::size_t(y) * std::size_t(s->pitch);
                for (int x = 0; x < cw_; ++x) {
                    const std::size_t i = std::size_t(y) * cw_ + x;
                    row[x * 3] = cliffCells_[i] ? 255 : 0;
                    row[x * 3 + 1] = walkOnly[i] ? 255 : crumbling[i] ? 128 : interior[i] ? 64 : 0;
                    row[x * 3 + 2] = Uint8(sat(level[i]) * 255.0f + 0.5f);
                }
            }
            const std::string path = std::string(dir) + "/guide_cells.bmp";
            if (!SDL_SaveBMP(s, path.c_str())) SDL_Log("Terrain (faithful): could not write %s: %s", path.c_str(), SDL_GetError());
            SDL_DestroySurface(s);
        }
        // And what they were read from: R = the share of rock in the cell's art, G = 0 flagged
        // sand / 128 buildable or built on / 255 other, B 255 = vehicles can use the cell (the
        // drive-on bit CanUnitUseSquare tests), 128 = infantry only.
        if (SDL_Surface* s = SDL_CreateSurface(cw_, ch_, SDL_PIXELFORMAT_RGB24)) {
            for (int y = 0; y < ch_; ++y) {
                Uint8* row = static_cast<Uint8*>(s->pixels) + std::size_t(y) * std::size_t(s->pitch);
                for (int x = 0; x < cw_; ++x) {
                    const std::size_t i = std::size_t(y) * cw_ + x;
                    const std::uint32_t fl = w.cell(x, y).flags;
                    row[x * 3] = Uint8(sat(rockFrac[i]) * 255.0f + 0.5f);
                    row[x * 3 + 1] = cls[i] == kPureSand ? 0 : cls[i] == kPlainRock ? 128 : 255;
                    row[x * 3 + 2] = (fl & cf::kDriveOn) ? 255 : (fl & cf::kWalkOn) ? 128 : 0;
                }
            }
            const std::string path = std::string(dir) + "/guide_flags.bmp";
            if (!SDL_SaveBMP(s, path.c_str())) SDL_Log("Terrain (faithful): could not write %s: %s", path.c_str(), SDL_GetError());
            SDL_DestroySurface(s);
        }
    }
    return true;
}

float TerrainGuide::sample(int channel, float wx, float wy) const {
    if (!valid_ || gw_ < 1 || gh_ < 1 || texels_.empty()) return 0;
    const float s = float(res_) / float(P);
    const float u = wx * s - 0.5f, v = wy * s - 0.5f;
    const int x0 = int(std::floor(u)), y0 = int(std::floor(v));
    const float fx = u - float(x0), fy = v - float(y0);
    auto at = [&](int x, int y) {
        x = std::clamp(x, 0, gw_ - 1);
        y = std::clamp(y, 0, gh_ - 1);
        return float((texels_[std::size_t(y) * gw_ + x] >> (8 * channel)) & 0xFF) / 255.0f;
    };
    return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}

float TerrainGuide::heightAt(float wx, float wy) const {
    if (caster_.empty()) return sample(1, wx, wy);
    const float s = float(res_) / float(P);
    const float u = wx * s - 0.5f, v = wy * s - 0.5f;
    const int x0 = int(std::floor(u)), y0 = int(std::floor(v));
    const float fx = u - float(x0), fy = v - float(y0);
    auto at = [&](int x, int y) {
        return float(caster_[std::size_t(std::clamp(y, 0, gh_ - 1)) * gw_ + std::size_t(std::clamp(x, 0, gw_ - 1))]) / 255.0f;
    };
    return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}
float TerrainGuide::rockAt(float wx, float wy) const { return sample(0, wx, wy); }

void TerrainGuide::dump(const char* dir) const {
    if (!valid_ || texels_.empty()) return;
    static const char* kNames[4] = {"rock", "height", "tone", "rugged"};
    for (int c = 0; c < 4; ++c) {
        SDL_Surface* s = SDL_CreateSurface(gw_, gh_, SDL_PIXELFORMAT_RGB24);
        if (!s) return;
        for (int y = 0; y < gh_; ++y) {
            Uint8* row = static_cast<Uint8*>(s->pixels) + std::size_t(y) * std::size_t(s->pitch);
            for (int x = 0; x < gw_; ++x) {
                const Uint8 v = Uint8((texels_[std::size_t(y) * gw_ + x] >> (8 * c)) & 0xFF);
                row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = v;
            }
        }
        const std::string path = std::string(dir) + "/guide_" + kNames[c] + ".bmp";
        if (!SDL_SaveBMP(s, path.c_str())) SDL_Log("Terrain (faithful): could not write %s: %s", path.c_str(), SDL_GetError());
        SDL_DestroySurface(s);
    }
}

}  // namespace d2k::render3d

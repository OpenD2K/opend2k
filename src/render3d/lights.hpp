#pragma once
// Dynamic lights of the 3D mode (docs/enhanced-graphics.md "Light list", docs/3d-mode.md "Dynamic
// lights"): clustered forward lighting on a ground grid.
//
// Every frame Renderer3D::buildPointLights picks the lights (the Modern effects' light list and the
// lamps of the time of day, render::SceneLights) and LightGrid bins them into a grid of square cells
// laid on the ground (world x / z) over the lights' reach. The result is ONE storage buffer of uint4:
//   [0, 3L)                 the lights, 3 x uint4 each (float bits): position (x, height, z) + radius,
//                           colour * intensity, spot direction + cos of the half angle (w < -1: point light)
//   [cellBase, ...)         one uint per cell, 4 per uint4: (first index << 8) | count
//   [indexBase, ...)        the cells' light indices, 4 per uint4
// bound as fragment storage buffer 0 of every lit shader (terrain_fs, terrain2_fs, terrain3_fs, rock_fs,
// model_fs, sprite_fs; MSL [[buffer(<uniform count>)]], HLSL t<sampler count> space2). A fragment finds
// its cell from its world position and loops over that cell's lights only, so the per-pixel cost depends on
// how many lights overlap that spot, not on how many are on screen (the old block looped over all 48 for
// every pixel, which is why the lamps were capped at 10 - 20 and the ones away from the view centre went
// dark; owner 2026-11-14). The uniform block (fragment uniform buffer 1, PointLightsU) keeps the frame's
// sun / sky colours and the grid's placement.
//
// Each light adds colour * (1 - d^2 / r^2)^2 * (wrapped N.L) on top of the sun-lit albedo, before fog and
// shroud (so a light under the shroud stays hidden). Header-only and SDL-free (tests/game_test.cpp).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace d2k::render3d {

// Budgets. Desktop: up to 768 lights a frame (effects + lamps), at most 40 per cell; phones / tablets less.
#if defined(__ANDROID__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
inline constexpr int kMaxGridLights = 192;
inline constexpr int kMaxLightsPerCell = 20;
#else
inline constexpr int kMaxGridLights = 768;
inline constexpr int kMaxLightsPerCell = 40;
#endif
inline constexpr int kMaxGridCells = 128 * 128;       // grid size cap (the cell grows on huge views)
inline constexpr int kMaxGridIndices = 96 * 1024;      // cell -> light entries
inline constexpr float kMinGridCell = 32;              // world px (one map cell)
// Storage buffer size in uint4 (fixed; the used part is uploaded every frame).
inline constexpr int kLightBufferVec4 = kMaxGridLights * 3 + kMaxGridCells / 4 + kMaxGridIndices / 4;

// Fragment uniform block 1 of every lit shader, std140-compatible. The head carries the frame's global light
// (time of day / Modern lighting, render/lighting.hpp): every lit shader multiplies its ambient term by ambColor
// and its sun term by sunColor (1 = the old light), billboards take spriteTint; info.y scales the glTF
// materials' sun highlight. grid / grid2 place the light grid (LightGrid).
struct PointLightsU {
    float info[4];        // x = light count (0 = none: the storage buffer is not read), y = specular multiplier,
                          // z = saturation (1 = as is), w unused
    float sunColor[4];    // rgb: the sun's (diffuse) colour multiplier
    float ambColor[4];    // rgb: the ambient (sky) colour multiplier
    float spriteTint[4];  // rgb: billboards' light (flat sunlit ground vs the old light)
    float grid[4];        // x, y = the grid's origin (world x, z), z = 1 / cell size, w = cells across
    float grid2[4];       // x = cells down, y = cell table start (uint4), z = index list start (uint4), w unused
    PointLightsU() { clear(); }
    // No lights, the old light (colours 1).
    void clear() {
        std::memset(this, 0, sizeof *this);
        info[1] = 1;
        info[2] = 1;
        for (int i = 0; i < 3; ++i) sunColor[i] = ambColor[i] = spriteTint[i] = 1;
    }
    void setScene(const float sun[3], const float amb[3], const float tint[3], float specular, float saturation = 1) {
        info[2] = saturation;
        for (int i = 0; i < 3; ++i) {
            sunColor[i] = sun[i];
            ambColor[i] = amb[i];
            spriteTint[i] = tint[i];
        }
        info[1] = specular;
    }
};
static_assert(sizeof(PointLightsU) == 96, "PointLightsU layout");

// One light for the grid: what the shaders read + its footprint on the ground (a circle that holds every
// point the light can reach, in world x / z).
struct GridLight {
    float posRadius[4] = {};  // x, height, z, radius
    float color[4] = {};      // rgb * intensity
    float spot[4] = {0, 0, 0, -2};
    float bx = 0, bz = 0, br = 0;  // ground footprint (circle)

    // A point light: its sphere.
    void boundSphere() {
        bx = posRadius[0];
        bz = posRadius[2];
        br = posRadius[3];
    }
    // A spot light: the sphere cut by the cone. For a beam that is mostly level (headlights) the cone's
    // ground footprint lies in a circle around the beam's middle (the apex and the rim at full reach inside);
    // a steep / wide beam keeps the whole sphere.
    void boundSpot() {
        boundSphere();
        const float hx = spot[0], hz = spot[2], hl = std::sqrt(hx * hx + hz * hz);
        const float cosA = spot[3];
        if (hl < 0.8f || cosA < 0.5f) return;
        const float r = posRadius[3];
        // Widened by the horizontal tilt of the beam (its projection on the ground is a little wider).
        const float a = std::acos(std::clamp(cosA, -1.0f, 1.0f)) + std::acos(std::clamp(hl, 0.0f, 1.0f)) + 0.05f;
        if (a >= 1.0f) return;
        const float c = std::cos(a), s = std::sin(a);
        const float ux = hx / hl, uz = hz / hl;
        const float m = r * 0.5f;  // centre half way along the beam
        const float rimA = std::hypot(r * c - m, r * s), rimB = std::hypot(r * c - m, -r * s);
        const float rad = std::max({m, rimA, rimB, std::abs(r - m)}) + 1.0f;
        if (rad >= r) return;
        bx = posRadius[0] + ux * m;
        bz = posRadius[2] + uz * m;
        br = rad;
    }
};

// Bins the lights (in priority order: the first ones keep their place when a cell is full) into the grid and
// packs the storage buffer (words = uint32, 4 per uint4).
class LightGrid {
public:
    std::vector<std::uint32_t> words;  // the storage buffer's used part, a multiple of 4
    float originX = 0, originZ = 0, cell = kMinGridCell;
    int gw = 0, gh = 0, lights = 0;
    std::uint32_t cellBase = 0, indexBase = 0;  // in uint4
    int maxInCell = 0, droppedEntries = 0;      // statistics (D2K_LIGHT_LOG)

    void build(const std::vector<GridLight>& in, int maxPerCell = kMaxLightsPerCell, float minCell = kMinGridCell) {
        words.clear();
        gw = gh = lights = 0;
        maxInCell = droppedEntries = 0;
        const int n = std::min<int>(int(in.size()), kMaxGridLights);
        if (n <= 0) return;
        float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
        for (int i = 0; i < n; ++i) {
            const GridLight& l = in[std::size_t(i)];
            x0 = std::min(x0, l.bx - l.br);
            z0 = std::min(z0, l.bz - l.br);
            x1 = std::max(x1, l.bx + l.br);
            z1 = std::max(z1, l.bz + l.br);
        }
        cell = minCell;
        const float ext = std::max(x1 - x0, z1 - z0);
        cell = std::max(cell, ext / 128.0f);
        for (;;) {
            gw = std::max(1, int(std::ceil((x1 - x0) / cell)));
            gh = std::max(1, int(std::ceil((z1 - z0) / cell)));
            if (gw * gh <= kMaxGridCells) break;
            cell *= 1.25f;
        }
        originX = x0;
        originZ = z0;
        lights = n;
        const int cells = gw * gh;
        count_.assign(std::size_t(cells), 0);
        // Pass 1: the cells each light overlaps (its circle against the cell's square), capped per cell.
        auto forCells = [&](const GridLight& l, auto&& fn) {
            const float inv = 1.0f / cell;
            const int cx0 = std::max(0, int(std::floor((l.bx - l.br - x0) * inv)));
            const int cz0 = std::max(0, int(std::floor((l.bz - l.br - z0) * inv)));
            const int cx1 = std::min(gw - 1, int(std::floor((l.bx + l.br - x0) * inv)));
            const int cz1 = std::min(gh - 1, int(std::floor((l.bz + l.br - z0) * inv)));
            const float r2 = l.br * l.br;
            for (int cz = cz0; cz <= cz1; ++cz) {
                const float zlo = z0 + float(cz) * cell, zhi = zlo + cell;
                const float dz = l.bz < zlo ? zlo - l.bz : (l.bz > zhi ? l.bz - zhi : 0.0f);
                for (int cx = cx0; cx <= cx1; ++cx) {
                    const float xlo = x0 + float(cx) * cell, xhi = xlo + cell;
                    const float dx = l.bx < xlo ? xlo - l.bx : (l.bx > xhi ? l.bx - xhi : 0.0f);
                    if (dx * dx + dz * dz > r2) continue;
                    fn(cz * gw + cx);
                }
            }
        };
        const int cap = std::clamp(maxPerCell, 1, 255);
        int total = 0;
        for (int i = 0; i < n; ++i)
            forCells(in[std::size_t(i)], [&](int c) {
                if (count_[std::size_t(c)] >= cap || total >= kMaxGridIndices) {
                    ++droppedEntries;
                    return;
                }
                ++count_[std::size_t(c)];
                ++total;
            });
        // Layout.
        cellBase = std::uint32_t(n * 3);
        indexBase = cellBase + std::uint32_t((cells + 3) / 4);
        const std::size_t vec4 = std::size_t(indexBase) + std::size_t((total + 3) / 4);
        words.assign(vec4 * 4, 0);
        for (int i = 0; i < n; ++i) {
            const GridLight& l = in[std::size_t(i)];
            std::memcpy(&words[std::size_t(i) * 12 + 0], l.posRadius, 16);
            std::memcpy(&words[std::size_t(i) * 12 + 4], l.color, 16);
            std::memcpy(&words[std::size_t(i) * 12 + 8], l.spot, 16);
        }
        // Offsets (prefix sums), then the entries in light order.
        fill_.assign(std::size_t(cells), 0);
        std::uint32_t off = 0;
        for (int c = 0; c < cells; ++c) {
            fill_[std::size_t(c)] = off;
            const int k = count_[std::size_t(c)];
            maxInCell = std::max(maxInCell, k);
            words[std::size_t(cellBase) * 4 + std::size_t(c)] = (off << 8) | std::uint32_t(k);
            off += std::uint32_t(k);
        }
        std::fill(count_.begin(), count_.end(), 0);
        int placed = 0;
        for (int i = 0; i < n; ++i)
            forCells(in[std::size_t(i)], [&](int c) {
                const std::uint32_t k = words[std::size_t(cellBase) * 4 + std::size_t(c)] & 0xFFu;
                if (std::uint32_t(count_[std::size_t(c)]) >= k || placed >= total) return;
                words[std::size_t(indexBase) * 4 + fill_[std::size_t(c)] + std::uint32_t(count_[std::size_t(c)])] = std::uint32_t(i);
                ++count_[std::size_t(c)];
                ++placed;
            });
    }
    // The uniform block's grid fields.
    void place(PointLightsU& u) const {
        u.info[0] = float(lights);
        u.grid[0] = originX;
        u.grid[1] = originZ;
        u.grid[2] = 1.0f / cell;
        u.grid[3] = float(gw);
        u.grid2[0] = float(gh);
        u.grid2[1] = float(cellBase);
        u.grid2[2] = float(indexBase);
        u.grid2[3] = 0;
    }
    // CPU twin of the shaders' lookup (tests): the lights listed for world point (x, z).
    template <class Fn>
    void forPoint(float x, float z, Fn&& fn) const {
        if (lights <= 0) return;
        const float gx = (x - originX) / cell, gz = (z - originZ) / cell;
        if (gx < 0 || gz < 0 || gx >= float(gw) || gz >= float(gh)) return;
        const std::uint32_t c = std::uint32_t(gz) * std::uint32_t(gw) + std::uint32_t(gx);
        const std::uint32_t e = words[std::size_t(cellBase) * 4 + c];
        const std::uint32_t first = std::uint32_t(indexBase) * 4 + (e >> 8);
        for (std::uint32_t k = 0; k < (e & 0xFFu); ++k) fn(int(words[first + k]));
    }

private:
    std::vector<int> count_;
    std::vector<std::uint32_t> fill_;
};

}  // namespace d2k::render3d

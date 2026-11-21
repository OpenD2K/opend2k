#pragma once
// Heightfield of the 3D mode (docs/3d-mode.md "Terrain"), derived from the map's cell flags
// (TILEATR bits copied into every cell at mission setup, re/notes/map-and-spawn.md §2.1):
//
//   sand / spice        level 0 (spice a little raised, by its amount)
//   dunes (terrain 2)   level 0 + gently rolling low-frequency waves
//   rock (passable, not sand: terrain 1/3/5, build-on, concrete)   level 1 = a raised plateau
//   impassable, not sand (cliff faces, rock outcrops)   solved: the level relaxes between the
//                       fixed sand / rock cells (Laplace), so a cliff band becomes a slope from
//                       the plateau down to the sand; impassable cells deep inside rock get an
//                       extra bump (outcrops / mountains).
//
// The vertex grid has sub() vertices per cell edge (cell centres are vertices, so a cell's own
// level is exact there and cliffs stay as steep as their band is narrow). Per vertex: position,
// normal, and the sun visibility from a soft heightfield shadow march (baked, cheap; no shadow map).
// Render-only: reads the World, never writes it.
//
// Two styles (Settings "3D terrain"): Original tiles = 2 vertices per cell, the levels above only;
// Designed (docs/3d-mode.md "Terrain v2", drawn by render3d/terrain_v2.*) = 4 vertices per cell and
// the same levels plus noise: uneven plateau tops, rugged cliff bands, curved asymmetric dune
// ridges, a gentle swell of the open sand. Gameplay never reads any of it.
//
// Designed (faithful) (docs/3d-mode.md "Terrain v3"): the designed grid and sand relief, but the
// rock's relief comes from the layout guide read from the original tiles (render3d/terrain_guide.*):
// level ground everywhere, cliffs rising kCliffHeight on the original's own outline.

#include "render3d/math3d.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace d2k::game {
class World;
}

namespace d2k::render3d {

class TerrainGuide;

struct TerrainVertex {
    float x, y, z;     // world: east, height, south
    float nx, ny, nz;  // normal
    float sun;         // 0 (shadowed) .. 1 (sunlit)
};

class Terrain {
public:
    static constexpr float kRockHeight = 22.0f;          // plateau height (world px; a tile is 32)
    static constexpr float kDuneHeight = 7.0f;
    static constexpr float kSpiceHeight = 0.6f;          // per spice amount step (0..7)
    static constexpr float kCliffHeight = 30.0f;         // faithful: a cliff top above the ground
    static Vec3 sunDirection();                          // unit vector towards the sun (north-west, 40 degrees up)
    // The sun the baked per-vertex visibility (TerrainVertex::sun) is marched towards; set it
    // before build(). Default: sunDirection(). (The faithful terrain is lit from the south-west.)
    void setSun(const Vec3& s) { sun_ = s; }
    // "Reimagined" (docs/3d-mode.md "Reimagined"; faithful only): taller relief - plateaus 1.6x, cliff tops up to
    // 2.4x kCliffHeight (near-vertical faces), see reliefScale. Set before build().
    void setReimagined(bool on) { reimagined_ = on; }
    bool reimagined() const { return reimagined_; }
    // The faithful relief's height (world px) for the guide's height g (0 sand .. 1 a cliff top). The shader
    // (terrain3_fs macroH) mirrors it. Reimagined: the guide was built for this curve (TerrainGuide::Params::
    // reimagined: the full scale on cliffs, gentle ramps on the cells ground units use).
    static float reliefHeight(float g, bool reimagined) {
        if (!reimagined) return g * kCliffHeight;
        const float t = std::clamp((g - 0.1333f) / 0.8667f, 0.0f, 1.0f);
        return g * kCliffHeight * (1.6f + 0.8f * t * t * (3.0f - 2.0f * t));
    }
    const Vec3& sun() const { return sun_; }

    // Full build from the world (classification, levels, heights, normals, shadows). tileRgb:
    // mean colour of every tileset tile (3 floats each; may be empty): impassable cells whose tile
    // looks like this map's sand rather than its rock (crater floors, sand-coloured walls of
    // dunes) count as sand, not as outcrops.
    // designed: the Terrain v2 heightfield (finer grid + noise), else the original-tiles one.
    // guide (designed only; used during the call, not kept): the faithful layout - the rock's
    // relief is the guide's instead of the per-cell levels.
    void build(const game::World& w, const std::vector<float>& tileRgb = {}, bool designed = false,
               const TerrainGuide* guide = nullptr);
    // Spice amounts changed since the last build/refresh: recompute the heights (cheap, levels
    // are kept). Returns true if the vertices changed. Reimagined (4 px grid): the spice relief
    // (spiceRelief) of the changed cells only, block by block; dirtyRows() then names the vertex
    // rows that changed (a partial upload).
    bool refreshSpice(const game::World& w);
    // Reimagined spice relief (docs/3d-mode.md "Spice fields in 3D"): the height (world px) the spice adds at
    // (wx, wy) - soft mounds and wind-shaped drifts, higher on thick spice, 0 off the field. In the mesh, so
    // units, track marks and the spice clumps sit on what is drawn. 0 on every other terrain style.
    float spiceRelief(float wx, float wy) const;
    // The vertex rows [first, last] the last refreshSpice changed and nothing else (-1, -1: everything / none
    // known - upload the whole buffer). Cleared by the next full build.
    std::pair<int, int> dirtyRows() const { return dirtyRows_; }
    // Bumped whenever the index buffer is rebuilt (a bloom's new spice relief on coarse quads): upload it again.
    int indexGen() const { return indexGen_; }

    enum CellKind : std::uint8_t { kSandCell = 0, kRockCell = 1, kCliffCell = 2 };
    bool designed() const { return designed_; }
    bool faithful() const { return faithful_; }  // built with a layout guide
    int sub() const { return sub_; }        // vertices per cell edge
    float step() const { return step_; }    // vertex spacing in world pixels
    // Per cell (after build): the height class (sand-looking impassable cells count as sand) and
    // the level (0 sand .. 1 plateau, more on outcrops).
    const std::vector<std::uint8_t>& cellKinds() const { return kind_; }
    const std::vector<float>& cellLevels() const { return level_; }
    int cellsW() const { return cw_; }
    int cellsH() const { return ch_; }
    int gridW() const { return gw_; }
    int gridH() const { return gh_; }
    const std::vector<TerrainVertex>& vertices() const { return verts_; }
    const std::vector<std::uint32_t>& indices() const { return idx_; }
    // The part of indices() to draw (first, count). Reimagined's 4 px grid holds two ranges: near = the
    // adaptive mesh (fine cliff faces), far = plain 8 px quads (zoomed out the 4 px faces are a few screen px:
    // all cost, no detail). Otherwise the whole buffer.
    std::pair<std::uint32_t, std::uint32_t> drawRange(bool far) const {
        if (sub_ != 8 || nearCount_ == 0) return {0u, std::uint32_t(idx_.size())};
        return far ? std::pair<std::uint32_t, std::uint32_t>{nearCount_, std::uint32_t(idx_.size()) - nearCount_}
                   : std::pair<std::uint32_t, std::uint32_t>{0u, nearCount_};
    }

    // Reimagined (4 px grid): is the 8 px quad (qx, qy) drawn with all its 4 px vertices in the near range
    // (buildAdaptiveIndices)? The world beyond the map (render3d/surround.*) stitches to the map's edge with it.
    bool quadFine(int qx, int qy) const {
        const int qw = (gw_ - 1) / 2, qh = (gh_ - 1) / 2;
        return sub_ == 8 && qx >= 0 && qy >= 0 && qx < qw && qy < qh && std::size_t(qy) * qw + qx < fineQuad_.size() &&
               fineQuad_[std::size_t(qy) * qw + qx] != 0;
    }
    // Height at world pixel (wx, wy): bilinear over the vertex grid; 0 outside the map.
    float height(float wx, float wy) const;
    // Ground marks (scorch, craters, rings: docs/3d-mode.md "Ground marks on slopes"): how much of a mark lies at
    // (wx, wy), 0..1 - none on near-vertical faces (the up component of the normal 0.55 .. 0.85) and none where the
    // ground is more than 4 .. 8 px above refY (the mark's centre / impact height), so marks never climb walls.
    float markFade(float wx, float wy, float refY) const;
    // Highest terrain over a cell rectangle (inclusive), for flat building decals.
    float maxHeight(int cx0, int cy0, int cx1, int cy1) const;

    // Building foundations (docs/3d-mode.md "Building foundations"): the ground under every
    // building's occupied cells (tilesOccupiedAll) is levelled to one height - a low quantile
    // (kFoundationLevel) of the natural heights there, neighbours kept close (setFoundations) - so
    // models and sprites stand on level ground on any terrain style. Around them the ground meets
    // that height on slope-limited ramps (applyFoundations): the natural relief is clamped between
    // two geodesic cones from the footprints that rise / fall at max(kFoundationRamp, the natural
    // slope + kFoundationRampOver) per px - no mound, pit or step steeper than that, and a cliff
    // beside a building keeps its face. The change ends within kFoundationReach px. build() applies
    // them; call updateFoundations every frame (a cheap comparison when nothing was built or
    // removed): true when vertices changed (re-upload the mesh).
    static constexpr float kFoundationLevel = 0.3f;       // the level: this quantile of the natural heights under it
    static constexpr float kFoundationTouchStep = 3.0f;   // px: the most two touching footprints differ
    static constexpr float kFoundationRamp = 0.3f;       // px rise per px (TerrainGuide::kReimRamp 0.35: the passable limit)
    static constexpr float kFoundationRampOver = 0.12f;   // on natural slopes: this much steeper (meets the ground)
    static constexpr float kFoundationReach = 160.0f;     // world px (path length) from the footprint
    bool updateFoundations(const game::World& w);
    // Off: no foundations (the 2D terrain bake keeps the natural relief). Set before build().
    void enableFoundations(bool on) { foundationsOn_ = on; }
    // The foundation height of the building standing on cell (cx, cy), or `fallback` if none.
    float foundationAt(int cx, int cy, float fallback) const;
    // First intersection of a ray with the terrain (or the y = 0 plane past the map).
    bool raycast(const Vec3& origin, const Vec3& dir, Vec3& hit) const;

private:
    // Recompute the vertices of the grid rectangle [gx0, gx1] x [gy0, gy1] (inclusive; heights,
    // then normals and sun visibility, which reach a little further).
    void computeHeights(int gx0, int gy0, int gx1, int gy1);
    void computeNormalsAndSun(int gx0, int gy0, int gx1, int gy1);
    // Reimagined: the faithful relief (world px) sculpted into rock (terrain.cpp).
    float reimaginedRelief(float wx, float wy) const;
    float reimaginedSculpt(float wx, float wy, float g0) const;
    // Reimagined: the index buffer of the 4 px grid, 8 px quads where they describe the relief (terrain.cpp).
    void buildAdaptiveIndices();
    // The soft heightfield shadow's march: spacing (world px) and steps (Reimagined: coarser, the 4 px grid has 4x the vertices).
    bool fineGrid() const { return designed_ && faithful_ && reimagined_; }
    float marchStep() const { return fineGrid() ? 12.0f : step_ * 0.75f; }
    int marchSteps() const { return fineGrid() ? 20 : 40; }
    bool setFoundations(const game::World& w, int& x0, int& y0, int& x1, int& y1);
    // h_ / vertex heights of a grid rectangle from hNat_ and the foundations. changed (optional, 4 ints):
    // the grid box of the vertices whose height changed; returns false when none did.
    bool applyFoundations(int gx0, int gy0, int gx1, int gy1, int* changed = nullptr);
    struct Foundation {
        int x = 0, y = 0;            // building top-left cell
        std::uint16_t mask = 0;      // tilesOccupiedAll (4x4, bit y*4+x)
        float h = 0;                 // levelled height
        std::uint16_t solid = 0;     // tilesOccupiedSolid (the rest of mask: aprons units drive over)
        bool operator==(const Foundation& o) const { return x == o.x && y == o.y && mask == o.mask; }
    };
    bool foundationsOn_ = true;
    bool reimagined_ = false;
    std::vector<Foundation> found_;
    std::vector<int> foundCell_;      // per cell: index into found_, -1 = none
    std::vector<std::uint8_t> foundNear_;  // per cell: 1 = a footprint within kFoundationReach (+ a cell)
    std::vector<float> hNat_;         // per vertex: the natural height (before the foundations)
    Vec3 sun_ = sunDirection();
    bool designed_ = false;
    bool faithful_ = false;
    const TerrainGuide* guide_ = nullptr;  // only during build()
    int sub_ = 2;
    float step_ = 16.0f;
    std::vector<std::uint8_t> kind_;  // per cell, CellKind
    int cw_ = 0, ch_ = 0, gw_ = 0, gh_ = 0;
    std::vector<float> level_;        // per cell, 0 sand .. 1 plateau (+ outcrop bumps)
    std::vector<float> dune_;         // per cell, dune weight 0..1 (smoothed)
    std::vector<float> rough_;        // per cell, 1 = rough rock (terrain type 3 / 5; designed relief)
    std::vector<std::uint8_t> spice_; // per cell, spice amount 0..7
    std::vector<float> hBase_;        // Reimagined: per vertex, the natural height without the spice relief
    std::pair<int, int> dirtyRows_{-1, -1};
    std::vector<int> spiceChanged_;   // Reimagined: cells whose spice changed (refreshSpice -> refreshSpiceRelief)
    std::vector<int> spiceBox_;       // per 8 x 8 cell block: the changed cells' box (scratch)
    bool refreshSpiceRelief(const game::World& w);
    bool quadWantsFine(int qx, int qy) const;  // Reimagined: an 8 px quad needs its 4 px vertices (buildAdaptiveIndices)
    int indexGen_ = 0;
    std::vector<float> h_;            // per vertex height
    std::vector<TerrainVertex> verts_;
    std::vector<std::uint32_t> idx_;
    std::uint32_t nearCount_ = 0;     // Reimagined: indices of the near range (drawRange)
    std::vector<std::uint8_t> fineQuad_;  // Reimagined: per 8 px quad, 1 = drawn fine in the near range (quadFine)
};

}  // namespace d2k::render3d

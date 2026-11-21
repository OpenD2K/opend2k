#pragma once
// "World beyond the map" (docs/3d-mode.md; Graphics: 3D, Reimagined only): a decorative, non-playable
// landscape around the battlefield, so the map sits inside a larger desert instead of ending in black.
// Render-only and deterministic for a map (its own hash of the heightfield's cell classes - never the
// game's RNG); the World is not even read.
//
// A ring of kMarginCells cells on an 8 px grid around the map's rectangle, then a flat apron far out:
//   - in the first 1-2 cells the ground is the map's own, MIRRORED across the edge (heights here; the shader
//     reads the map's guide / splat textures through mirror samplers), so type, height and texture
//     continue without a seam: the ring's first row are the map mesh's own edge vertices (position,
//     normal, baked sun; the 4 px vertices too where the map's edge quad is drawn fine);
//   - further out it blends (weight w, the far texture's blue channel; all of it from ~3-4 cells) into a
//     procedural desert: the edge's rock carried on outwards as plateaus with a warped, eroded outline that end
//     in a cliff, free-standing mesas and
//     outcrops, dunes across the wind; everything flattens out before the ring's outer edge, where the
//     apron (flat sand out to kApron px, enough for any zoom / tilt) takes over;
//   - nothing out there may hide the battlefield: the ground never rises faster than kOcclusionSlope
//     (under the steepest view) from the map edge's own height.
// The far texture (RGBA8, one texel per ring vertex: rock coverage, relief height in the guide's units,
// the blend weight, the cliff mask) tells terrain3_fs what the procedural part is made of.

#include "render3d/math3d.hpp"
#include "render3d/terrain.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace d2k::render3d {

class Surround {
public:
    static constexpr int kMarginCells = 20;         // the ring's width
    static constexpr float kStep = 8.0f;            // its vertex spacing (world px)
    static constexpr float kApron = 60000.0f;       // the flat apron's reach beyond the ring (world px)
    static constexpr float kOcclusionSlope = 0.42f; // px rise per px from the edge (cot of the steepest tilt, 66 deg: 0.445)
    static constexpr float margin() { return float(kMarginCells) * 32.0f; }

    // From a built Reimagined terrain (4 px grid; anything else clears it). ~50-150 ms.
    void build(const Terrain& t);
    // The map's heights changed (foundations, spice): the mirrored zone follows. True when vertices changed.
    bool refresh(const Terrain& t);
    void clear();
    bool valid() const { return !verts_.empty(); }

    const std::vector<TerrainVertex>& vertices() const { return verts_; }
    const std::vector<std::uint32_t>& indices() const { return idx_; }  // (empty after dropIndices)
    std::uint32_t indexCount() const { return indexCount_; }
    void dropIndices() {  // the list is on the GPU: free the CPU copy (~3-4.5 MB)
        idx_.clear();
        idx_.shrink_to_fit();
    }
    // The part of indices() to draw (first, count), matching Terrain::drawRange(far): near = the first row
    // stitched to the map's fine edge quads, far = to its plain 8 px edge.
    std::pair<std::uint32_t, std::uint32_t> drawRange(bool far) const {
        return far ? std::pair<std::uint32_t, std::uint32_t>{nearEdge_, indexCount_ - nearEdge_}
                   : std::pair<std::uint32_t, std::uint32_t>{0u, farEdge_};
    }
    // The far texture: texel (i, j) = the ring's grid point (i, j); uv = (world + margin + kStep / 2) / (map
    // size + 2 margin + kStep). R rock coverage, G relief height (guide units), B blend weight, A cliff mask.
    int texW() const { return ew_; }
    int texH() const { return eh_; }
    const std::vector<std::uint32_t>& texels() const { return tex_; }

    // How much of the ground at a point outside the map is rock (0 sand .. 1): the mirrored map's cell class
    // near the edge, the procedural desert's rock further out (the ground clutter out there asks).
    float rockAt(const Terrain& t, float wx, float wz) const;
    // The ring's ground height at a world point outside the map (0 inside it, and beyond the ring).
    float height(float wx, float wz) const;
    // First intersection of a ray with the ground outside the map (the apron's plane past the ring).
    bool raycast(const Vec3& origin, const Vec3& dir, Vec3& hit) const;
    float top() const { return top_; }  // the highest ring vertex

private:
    struct Far {
        float h = 0, rock = 0, hRock = 0, w = 1, cliff = 0;
    };
    Far farAt(float x, float z) const;
    void seedSample(float x, float z, float& a, float& b) const;
    int vid(int i, int j) const;  // vertex of grid point (i, j), -1 strictly inside the map
    float gridH(const Terrain& t, int i, int j) const;
    float blended(const Terrain& t, int i, int j) const;
    void normals(const Terrain& t, bool sun);
    void buildIndices(const Terrain& t);

    int cw_ = 0, ch_ = 0;      // map cells
    int ew_ = 0, eh_ = 0;      // ring grid points per axis
    int m_ = 0;                // grid offset of the map's origin
    int w4_ = 0, h4_ = 0;      // the map's size in grid steps
    float mapW_ = 0, mapH_ = 0;
    float seedX_ = 0, seedZ_ = 0;
    Vec3 sun_{};
    float top_ = 0;
    std::vector<float> seedA_, seedB_;   // per map cell: rock (blurred), rock x height (blurred)
    std::vector<float> hFar_;            // per ring vertex: the procedural height
    std::vector<std::uint32_t> tex_;     // the far texture (ew_ x eh_)
    std::vector<TerrainVertex> verts_;   // ring grid vertices, the edge's 4 px vertices, the apron
    std::uint32_t gridVerts_ = 0, midFirst_ = 0;
    std::vector<std::uint32_t> idx_;     // [near edge row][the rest][far edge row]
    std::uint32_t nearEdge_ = 0, farEdge_ = 0, indexCount_ = 0;
};

}  // namespace d2k::render3d

#pragma once
// "Designed (faithful)" (Terrain v3; docs/3d-mode.md "Terrain v3", docs/enhanced-graphics.md
// "Designed (faithful)"): the layout guide of the faithful designed terrain.
//
// The original tiles are pictures of a 3D scene: where exactly the rock ends and the sand begins,
// where a cliff stands and how wide its face is, where rock is plain and where it is broken, is
// drawn in their pixels, not stored in the cell flags. The guide reads those pixels AT RUN TIME
// from the player's own tileset (like the colour lock reads the tiles' mean colours) and turns
// them into four smooth per-pixel fields the designed terrain is laid out with:
//
//   rock    sand / rock coverage: a tile pixel is sand or rock by its chromaticity between the
//           map's mean sand and mean plateau-rock colours (3x3 blur); sand counts only when it
//           is connected to open sand, so the pale facets inside a cliff stay rock;
//   height  the relief, 0 (sand) .. 1 (a full cliff). All rock stands a little above the sand
//           (a low step along its outline). Rock VEHICLES can use (the drive-on bit, as
//           CanUnitUseSquare tests it) lies level - never a face. Rock they cannot use stands as
//           tall as the original paints it: a cell whose rock shows the original's dark faces
//           (rock clearly darker than plain rock over 13 x 13 px) is a cliff, crumbling edge
//           cells without faces stay on the ground, every other rock cell takes its level from
//           its neighbours (the plateau behind tall cliffs is level with them, a mesa's plain
//           top stands as tall as its rim). Inside a cliff cell the height rises from the sand
//           outline across a face - an anisotropic distance: a face is wide on the south side,
//           narrow on the north side (the original's oblique view) - and comes down to its
//           passable neighbours' level within the cell;
//   tone    how much lighter / darker than plain plateau rock the original draws a rock pixel
//           (pale crust, dark lumps and pockets);
//   rugged  local contrast: 0 = plain (buildable-looking) rock, 1 = broken rock.
//
// Nothing derived from the game data is stored or shipped: the guide lives in memory for the
// map being played. Render-only (reads the World, never changes it). CPU only (no SDL_gpu).

#include <cstdint>
#include <vector>

namespace d2k::game {
class World;
}
namespace d2k::render {
class TileAtlas;
}

namespace d2k::render3d {

class TerrainGuide {
public:
    static constexpr int kPx = 32;  // working resolution: the original's pixels per cell

    struct Params {
        // World px over which a face rises to the full cliff height, by the side the face looks
        // to (the original's faces: tall on the south side, hidden on the north side).
        float faceSouth = 20.0f, faceNorth = 6.0f, faceEastWest = 10.0f;
        int res = 0;  // guide texels per cell: 32 or 16 (0 = 32 up to 96 x 96 cells, else 16)
        // The 2D bake (seen from straight above, the relief is PAINTED from the height like the
        // original tiles): heightAt() - the mesh, which only casts the shadows there - is the
        // same relief at half the cliff height, so the shadows fall on the sand beyond the foot.
        bool topDown = false;
    };
    // A boulder (on rock no vehicle can use that does not stand as a tall cliff: crumbling
    // cliff ends, rubble, outcrops, infantry-only rock): world px + size.
    struct Stone {
        float x, y, size;
    };

    // Reads the map's tiles. False (and valid() false) when they cannot be classified: no atlas,
    // no plain sand or plain rock cells on the map, or sand and rock too close in colour.
    bool build(const game::World& w, const render::TileAtlas* atlas, const Params& p);
    void clear();
    bool valid() const { return valid_; }

    int res() const { return res_; }  // texels per cell
    int width() const { return gw_; }
    int height() const { return gh_; }
    // RGBA8 texels (R rock, G height, B tone: 128 = plain rock, A rugged), row-major, width() x height().
    const std::vector<std::uint32_t>& texels() const { return texels_; }
    // The mesh's relief 0..1 at a world pixel (bilinear; 0 outside the map): the height field of
    // the texture, or with Params::topDown the shadow casters' relief (half as tall).
    float heightAt(float wx, float wy) const;
    // Rock coverage 0..1 at a world pixel.
    float rockAt(float wx, float wy) const;
    const std::vector<Stone>& stones() const { return stones_; }
    // Per cell (cells wide x cells high): 1 = a cell no vehicle can use (the drive-on bit is
    // clear: CanUnitUseSquare's terrain test for every wheeled / tracked unit) that is not
    // flagged as sand - the only cells that may show cliff faces, dark rock and boulders.
    // Kept after releaseTexels().
    const std::vector<std::uint8_t>& cliffCells() const { return cliffCells_; }

    // Colours measured on the map's own tiles (0..255): plain sand, plain (buildable) rock.
    const float* sandColour() const { return sand_; }
    const float* rockColour() const { return rock_; }
    // The original's broken rock comes in a few tones - crevices, shaded rock, mid rock, lit
    // facets, pale rims / crust (its 3D render's light and dark). Tone = (luminance - plain
    // rock's) / plain rock's; level i lies between kLevelEdge[i - 1] and kLevelEdge[i]. The
    // colour of each level: the mean colour of this map's broken-rock pixels of that tone.
    static constexpr int kLevels = 5;
    static constexpr float kLevelEdge[kLevels - 1] = {-0.58f, -0.30f, -0.07f, 0.20f};
    const float* levelColour(int i) const { return level_[i]; }

    // Frees the texels once they are on the GPU (heightAt / rockAt then return 0; valid() stays).
    void releaseTexels() {
        std::vector<std::uint32_t>().swap(texels_);
        std::vector<std::uint8_t>().swap(caster_);
    }

    // Debugging (D2K_GUIDE_DUMP=<dir>): the four fields as BMPs.
    void dump(const char* dir) const;

private:
    float sample(int channel, float wx, float wy) const;

    bool valid_ = false;
    int cw_ = 0, ch_ = 0, res_ = 16, gw_ = 0, gh_ = 0;
    std::vector<std::uint32_t> texels_;
    std::vector<std::uint8_t> caster_;  // Params::topDown: the mesh's height (0..255), width() x height()
    std::vector<std::uint8_t> cliffCells_;
    std::vector<Stone> stones_;
    float sand_[3] = {}, rock_[3] = {};
    float level_[kLevels][3] = {};
};

}  // namespace d2k::render3d

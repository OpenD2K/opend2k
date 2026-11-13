#pragma once
// Art packs (Enhanced graphics only): replacement images at any resolution for DATA.R16 frames
// and terrain tiles, loaded lazily from a folder; anything the pack lacks falls back to the
// original frame. Layout (docs/enhanced-graphics.md):
//
//   <pack>/pack.ini                    name=..., scale=N (texels per original pixel), optional
//                                      derive_shadows=0|1, derive_house=0|1 (default 1)
//   <pack>/DATA/<frame>.png            RGBA base colour; house-colour pixels as a neutral grey ramp
//   <pack>/DATA/<frame>.house.png      optional 8-bit house-colour intensity (tinted with the
//                                      house's COLOURS.BIN ramp by luminance)
//   <pack>/DATA/<frame>.shadow.png     optional 8-bit shadow mask (255 = halve the destination,
//                                      like the original's shadow pixels)
//   <pack>/DATA/<frame>.glow.png       optional RGB(A) emission of the frame's lit surfaces (lamps, cyan
//                                      louvres, windows; same size / hotspot as <frame>.png), added at night
//                                      by the time of day's lamps (docs/enhanced-graphics.md "Emission")
//   <pack>/DATA/hotspots.txt           optional: lines "<frame> <x> <y>" = the image texel corner
//                                      that lands on the frame's anchor (replaces the original
//                                      R16 hotspot x*scale, y*scale; rendered-from-3D packs whose
//                                      images are larger than / offset from the original frame)
//   <pack>/<TILESET>/<tile>.png        terrain tile (e.g. BLOXBGBS/123.png), 32*scale square
//   <pack>/UI_ENG/<frame>.png          Enhanced in-game UI: a UI_ENG.R16 frame (RGBA, any scale)
//   <pack>/UIBB/0.png                  Enhanced in-game UI: UIBB.R16, the 640x400 frame (any scale)
//
// <frame> is the R16 entry index. The image's top-left is the original frame's top-left (same
// hotspot / offset semantics) unless hotspots.txt lists the frame, its size in world pixels is
// (width / scale, height / scale).
// Packs are searched in <pref path>/artpacks/ (userDir(): where the in-app download installs,
// platform/artpack_download.*) and artpacks/ next to the executable (or up to four folders above
// it, e.g. the repository root for build/d2k) and in the working directory.

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace d2k::render {

struct ModArt;

// Decoded RGBA8 image (formats::Rgba packing: R in the low byte, A in the high byte).
struct ArtImage {
    int w = 0, h = 0;
    std::vector<std::uint32_t> px;
    bool empty() const { return w <= 0 || h <= 0; }
};

// PNG via SDL_LoadPNG, converted to RGBA8. False if missing or unreadable.
bool loadPng(const std::string& path, ArtImage& out);

class ArtPack {
public:
    // Folders searched for packs, most specific first (existing ones only).
    static std::vector<std::string> searchDirs();
    // The writable per-user pack folder: <pref path>/artpacks (may not exist yet; "" without a
    // pref path), or the override (--artpack-dir, tests). Searched first.
    static std::string userDir();
    static void setUserDirOverride(const std::string& dir);
    // Only this folder is searched (no pref / exe / cwd folders): --render-previews renders with
    // exactly the pack it was given ("" = the normal search).
    static void setExclusiveDir(const std::string& dir);
    // Packs installed / removed while running: forget the cached pack lists (isRedesign) and bump
    // generation() so cached UI lists refresh.
    static void invalidate();
    static int generation();
    // Folder names of the installed packs (those with a pack.ini), sorted, without duplicates.
    static std::vector<std::string> available();
    // Whether the installed pack `name` is a redesign (rendered from 3D models; pack.ini
    // kind=rendered|redesign, or written by tools/render_sprites/render_pack.py) rather than an
    // upscale of the original art. Reads only pack.ini; cached per name. The Enhanced graphics
    // preset keeps such a pack and otherwise uses the original sprites (Settings::applyPreset).
    static bool isRedesign(const std::string& name);
    // Opens the pack folder `name` from the search dirs; nullptr if none has it.
    static std::unique_ptr<ArtPack> open(const std::string& name);
    // Graphics: 3D (docs/3d-mode.md "Objects"): the installed OpenD2K 3D art pack to use - a pack folder
    // with both models/ (the glTF models every object is drawn with) and DATA/ (the redesigned frames a
    // type without a model falls back to as a billboard; never the original sprites). `preferred`
    // (Settings::modelPack) wins when it is such a pack, else the first by name; "" = none installed,
    // and Graphics: 3D is not offered. Rescans the folders at most every 2 s.
    static std::string threeDPack(const std::string& preferred = {});

    const std::string& folder() const { return folder_; }
    const std::string& title() const { return title_; }
    const std::string& root() const { return root_; }
    int scale() const { return scale_; }
    bool deriveShadows() const { return deriveShadows_; }
    bool deriveHouse() const { return deriveHouse_; }

    // A community mission's own art (render/mod_art.hpp; not owned, nullptr = none): every frame /
    // tile query below goes through it - a frame the campaign changed has no pack frame, a base frame
    // the campaign's DATA.R16 moved is found at its new index; tiles of a campaign tileset only where
    // the base tileset of that name has the same pixels. Set right after open().
    void setModArt(const ModArt* m) { mod_ = m; }
    const ModArt* modArt() const { return mod_; }

    bool hasFrame(int frame) const;
    // The frame's own hotspot from hotspots.txt in texels (x right, y down from the image's
    // top-left); false = use the original frame's hotspot.
    bool hotspot(int frame, int& x, int& y) const;
    int frameCount() const { return static_cast<int>(frames_.size()); }
    // Base image plus the optional masks (left empty when the pack has none).
    bool loadFrame(int frame, ArtImage& base, ArtImage& house, ArtImage& shadow) const;
    // The frame's optional emission layer (<frame>.glow.png); false = none.
    bool loadGlow(int frame, ArtImage& glow) const;
    // Terrain tile of tileset `tileset` (file stem, e.g. "BLOXBGBS"); false if not in the pack.
    bool loadTile(const std::string& tileset, int tile, ArtImage& out) const;
    int tileCount(const std::string& tileset) const;
    // Any other section's image <pack>/<section>/<index>.png (UI_ENG, UIBB); false if missing.
    bool loadImage(const std::string& section, int index, ArtImage& out) const;
    bool hasSection(const std::string& section) const;

private:
    std::string folder_, title_, root_;
    const ModArt* mod_ = nullptr;
    int packFrame(int frame) const;
    int scale_ = 1;
    bool deriveShadows_ = true, deriveHouse_ = true;
    std::set<int> frames_, houseMasks_, shadowMasks_, glowMasks_;
    std::unordered_map<int, std::pair<int, int>> hotspots_;
    struct TileDir { std::string path; std::set<int> tiles; };
    mutable std::unordered_map<std::string, TileDir> tileDirs_;
    const TileDir& tileDir(const std::string& tileset) const;
};

// Premultiplied-alpha 2x box downsample (mip level); ow/oh = ceil(w/2), ceil(h/2).
std::vector<std::uint32_t> downsample2(const std::vector<std::uint32_t>& src, int w, int h, int& ow, int& oh);
// Bilinear resample to an exact size (premultiplied), for tiles whose size is off.
std::vector<std::uint32_t> resample(const std::vector<std::uint32_t>& src, int w, int h, int ow, int oh);

}  // namespace d2k::render

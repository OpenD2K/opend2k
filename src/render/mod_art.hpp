#pragma once
// Community campaigns' own art (docs/community-campaigns.md §8): a mission's mods folder can
// replace DATA.R16 (new / changed unit and building sprites), Templates.bin (types repurposed, new
// art indexes) and the tileset. Art packs and 3D models are keyed by the BASE install's frame
// numbers / type names, so while such a mission runs:
//   - a DATA.R16 frame whose content differs from every base frame is the campaign's art: never
//     replaced by an art pack (the frame is drawn from the mod's own sprite, like the original art);
//     a frame equal to a base frame (also when the mod's added frames moved it to another index)
//     keeps the pack's art of that base frame;
//   - a unit / building type whose render-relevant template fields or frames differ from the base
//     type of the same index draws no 3D model (in Graphics: 3D a billboard of its sprite: the pack's
//     redesign where the frame is unchanged, else the mod's own sprite);
//   - tiles of a tileset from the mods folder keep an art pack's tile only where the base tileset of
//     that name has the same pixels.
// Computed once per mission load (game_data.cpp); the base install's per-frame hashes are cached.
// Render-only: the simulation never sees it.

#include "formats/r16.hpp"
#include "formats/templates.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace d2k::render {

struct ModArt {
    bool active = false;  // the mission replaced DATA.R16, Templates.bin or the tileset
    // Per DATA.R16 frame: the base frame with the same content (the art pack's key), -1 = the
    // campaign's own art. Empty = DATA.R16 is the base one (identity).
    std::vector<int> frameSource;
    // Per unit / building type of the mission's Templates.bin: the base type it is - same name and
    // art, or (renamed / moved) the first base type with the same art - whose name and group the 3D
    // models are looked up by (key*); -1 = the campaign's own art: no model. Empty = identity.
    std::vector<int> unitBase, buildingBase;
    std::vector<std::string> unitKeyName, unitKeyGroup, buildingKeyName, buildingKeyGroup;
    std::string tileset;          // stem of a tileset read from the mods folder ("" = the base one)
    std::vector<int> tileSource;  // per tile of `tileset`: the base tile with the same pixels, -1 = campaign's
    bool tileAtrFromMod = false;
    int changedFrames = 0, movedFrames = 0, ownUnits = 0, ownBuildings = 0, changedTiles = 0;
    std::string summary;  // the one-line log ("Mod art: ...")
    std::vector<std::string> ownTypes;  // "unit 3 NAME: art" per such type (logged with D2K_MOD_ART=1)

    // The art pack frame for DATA.R16 frame `frame` (-1 = campaign art: no pack frame).
    int packFrame(int frame) const {
        if (frameSource.empty()) return frame;
        return frame >= 0 && frame < int(frameSource.size()) ? frameSource[std::size_t(frame)] : -1;
    }
    bool ownFrame(int frame) const { return !frameSource.empty() && packFrame(frame) < 0; }
    bool ownUnit(int type) const { return !unitBase.empty() && baseOf(unitBase, type) < 0; }
    bool ownBuilding(int type) const { return !buildingBase.empty() && baseOf(buildingBase, type) < 0; }
    // Types the campaign changed (own art, or at another index than the base type): without a model and
    // a redesigned frame they are billboards of the campaign's sprite in Graphics: 3D.
    bool touchedUnit(int type) const { return !unitBase.empty() && baseOf(unitBase, type) != type; }
    bool touchedBuilding(int type) const { return !buildingBase.empty() && baseOf(buildingBase, type) != type; }
    static int baseOf(const std::vector<int>& v, int type) {
        return type >= 0 && type < int(v.size()) ? v[std::size_t(type)] : -1;
    }
    // The art pack tile for tile `tile` of tileset `stem` (case-insensitive), -1 = campaign's.
    int packTile(const std::string& stem, int tile) const;
};

struct ModArtInput {
    const formats::R16File* sprites = nullptr;  // the mission's DATA.R16 (loaded)
    std::string spritesPath, baseSpritesPath;   // equal = not replaced
    const formats::Templates* templates = nullptr;
    std::string templatesPath, baseTemplatesPath;
    const formats::R16File* tiles = nullptr;    // the mission's tileset (loaded)
    std::string tilesetStem, tilesPath, baseTilesPath;  // baseTilesPath "" = no base tileset of that name
    bool tilesConverted = false;                // BLOXXMAS: converted from .R8 (no per-tile compare)
    bool tileAtrFromMod = false;
};

ModArt analyseModArt(const ModArtInput& in);

// FNV-1a 64 of one R16 frame: header fields, pixels and (8 bpp) its palette's colours.
std::uint64_t frameHash(const formats::R16File& f, std::size_t frame);

}  // namespace d2k::render

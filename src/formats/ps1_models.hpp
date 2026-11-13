#pragma once
// The PS1 Dune 2000's real-time 3D models (vehicles and buildings), read from the player's own disc
// image at runtime (DUNE.EXE). docs/3d-models.md has the full layout; summary:
//
// - DUNE.EXE (PS-X EXE, data at file 0x800 = the load address) has a model table of 20-byte records
//   {u32 hmd_address, u32 tim_address - 8, char name[12]} ("trike", "mcv", ..., "ix", "wall").
// - Each model is a Sony libgs HMD: u32 0x50, u32 map flag, u32 primitive-header section (word
//   index), u32 block count, block pointers; the first primitive header lists the polygon data,
//   vertex (SVECTOR), normal (SVECTOR, 4096 = 1) and coordinate sections. Coordinates are
//   GsCOORDINATE2-like (80 bytes: flag, MATRIX with translation, work matrix, ..., parent pointer).
//   Block i uses coordinate i - 1. Polygon types (low byte): bit0 textured, bit1 coloured
//   (pre-lit), bit2 gouraud, bit3 triangle, bit4 quad; 0x00200000 = no light calculation.
// - Layout used by every 10/11-block model: block 1 = body, blocks 2..7 = one house marker each
//   (Atreides blue, Harkonnen red, Ordos green, purple, brown, yellow: the PS1 colours houses by
//   drawing one of six marker polygons, not by palette swaps), block 8 = flat black shadow
//   polygons, block 9 (only some) = the turret / second part on coordinate 8. 4-block models
//   (ornithopter, frigate, Death Hand missile): block 1 = body, block 2 = shadow.
// - One 64x64 (trike 32x32) 4bpp TIM with one 16-colour CLUT per model; tpage/CLUT are set at
//   runtime (the records' CBA/TSB are 0), uv are texels of that one texture.
// - Axes: PS1 x = east, y = down, z = NORTH (right-handed; the fixed PS1 camera looks from the
//   south), 256 units per map tile (= 32 px, scale 1/8); ours = (x, -y, -z) / 8 (a rotation, no
//   mirror). Vehicles are centred on the origin, front towards PS1 +z (= north = facing 0).
//   Buildings: the root coordinate's translation is a constant centring offset; without it the
//   model starts at the bottom-left (south-west) of its box, most 3x3 ones one tile further north;
//   we put the front ground row on the PC art box's bottom edge (Anchor::ArtBottomLeft).
// - Only the faces the PS1 camera sees exist: building backs are open. Their boundary edges get
//   one-sided "curtains" down to the ground (docs/3d-models.md); shadows are mirrored to fall
//   south-east like ours.
//
// No SDL here (pure parsing). The disc is read with formats/ps1_disc.

#include "formats/model3d.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace d2k::formats::ps1 {

class Disc;

struct ModelInfo {
    std::string name;
    int blocks = 0, coords = 0, polygons = 0, vertices = 0;
    bool building = false;
};

// Parses all models of a DUNE.EXE image. False + err when the table is not found.
bool parseModels(std::span<const std::uint8_t> exe, std::vector<Model3D>& out, std::string& err,
                 std::vector<ModelInfo>* info = nullptr);
// Reads DUNE.EXE from an opened disc and parses it.
bool loadModels(Disc& disc, std::vector<Model3D>& out, std::string& err, std::vector<ModelInfo>* info = nullptr);

// PS1 model name for one of our templates ("" = none). Unit: its unit group name (Templates.bin
// unitGroupNames, e.g. "COMBAT_TANK"). Building: its template name ("ATREIDES BARRACKS") and group
// name ("BARRACKS"); per-house barracks / palaces are picked by name.
std::string modelForUnit(std::string_view groupName);
std::string modelForBuilding(std::string_view templateName, std::string_view groupName);

}  // namespace d2k::formats::ps1

#pragma once
// Assemble build-up (render/building_assemble.hpp, docs/3d-mode.md "Assemble build-up"): how a
// building model comes apart into parts and in which order / how they arrive. Built once per model
// at load (CPU), stored per vertex (model_vs: render3d/models_msl.hpp "assemble").
//
// The packs' building models are one mesh each (glTF nodes base / main / glow), but every mesh is
// made of many separate pieces (boxes, tanks, pipes, rails: 100-300 islands of triangles that share
// no corner). The pieces are found by welding equal corner positions (union-find), tiny ones are
// folded into the piece that holds them, and the rest are merged by nearness until 6..22 parts
// remain. A model that is one piece (or one tiny cloud) falls back to height layers that extrude in
// turn (a rising cut with a glowing edge). Deterministic: the same model gives the same parts.

#include "formats/model3d.hpp"
#include "render/building_assemble.hpp"

#include <cstdint>
#include <vector>

namespace d2k::render3d {

struct AssemblyPart {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};  // model px box
    float pivot[3] = {};  // the foot (x, z = box centre, y = box bottom)
    float param = 0;      // Extrude / Telescope: height; Lower: drop height; Slide: distance; Hinge: -
    float dirAngle = 0;   // Slide / Hinge: outward direction in the ground plane (radians, atan2(z, x))
    float t0 = 0, dur = 0;
    render::AssembleMotion motion = render::AssembleMotion::Extrude;
};

struct AssemblyPlan {
    std::vector<AssemblyPart> parts;      // in assembly order
    std::vector<std::uint16_t> bodyTris;  // per body triangle: its part
    bool empty() const { return parts.empty(); }
};

// The plan of a building model (body triangles; turret / house / pose / lod meshes are mapped onto
// its parts by assemblyPartAt).
AssemblyPlan planAssembly(const formats::Model3D& m);

// The part of a triangle of another mesh of the model (by its centroid: the smallest part box that
// holds it, else the nearest).
int assemblyPartAt(const AssemblyPlan& p, const float centroid[3]);

// The per-vertex encoding (model vertex attributes 8 and 9, render3d/models.cpp): a = pivot xyz +
// param; b = UBYTE4_NORM bytes (t0, dur, motion * 32, 1 + direction 0..254; b[3] = 0: no part).
void assemblyVertexAttrs(const AssemblyPart& part, float a[4], std::uint8_t b[4]);

}  // namespace d2k::render3d

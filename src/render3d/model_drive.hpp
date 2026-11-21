#pragma once
// Running gear (docs/3d-models.md "Wheels and tracks"): which parts of a vehicle model turn / run
// while it drives (Smooth driving's odometer, render/smooth_drive.hpp). Render only.
//
// The packs' vehicles are mostly one body mesh: wheels, lugs and track links are baked into it as
// separate pieces (islands of triangles that share no corner). Found once per model at load (CPU),
// stored per vertex (model_vs: render3d/models_msl.hpp "driveGear"):
//  - wheels: round pieces about the lateral (x) axis that stand on the ground (a tyre), plus every
//    piece inside their cylinder (lugs, hub, caps); they turn about the axle by -distance / radius;
//  - track links: many equal small pieces (same lateral extent and size) round a loop in the side
//    plane (tread pads, grousers, guide horns); each moves towards the next link of the loop by
//    fract(distance / pitch) - a rigid motion (rotation about a pivot, or a shift on straight runs),
//    so the belt runs continuously; at a gap in the loop (an open run) the link slides on.
// Pieces bound to a skeleton wheel node (wheel* / sprocket* / idler*: ModelRenderer::wheelPalette)
// are left to the node. Deterministic: the same model gives the same parts.

#include "formats/model3d.hpp"

#include <cstdint>
#include <vector>

namespace d2k::render3d {

// Per corner of a part (MeshPart::tris order): the vertex attributes 8 and 9 (render3d/models.cpp).
//  kind 1 wheel:      a = axle y, axle z, radius, -
//  kind 2 link (arc): a = pivot y, pivot z, turn to the next link (radians), pitch
//  kind 3 link (run): a = shift y, shift z (to the next link), -, pitch
// b = kind, lateral position of the wheel / loop (x + 128 px in 1/256 px, 16 bits), 0 (b[3] = 0:
// not an Assemble part).
struct DriveAttr {
    float a[4] = {};
    std::uint8_t b[4] = {};
};
// Where the running gear meets the ground (the track marks, render3d/track_marks.hpp): a ground wheel's
// tyre or a track loop's bottom run. Model px: lateral x0..x1, along z0..z1 (forward = -z).
struct DriveContact {
    float x0 = 0, x1 = 0, z0 = 0, z1 = 0;
    bool track = false;  // a track loop (else a tyre)
};
struct DriveGear {
    std::vector<std::vector<DriveAttr>> parts;  // per input part, per corner (empty = nothing moves)
    std::vector<DriveContact> contacts;         // ground wheels and track loops
    int wheels = 0, loops = 0, links = 0;
    bool empty() const { return wheels == 0 && loops == 0; }
};

// The running gear of a vehicle's parts (body + house, or their simplified versions; model px).
// skipJoint[j] != 0: corners bound to joint j (a skeleton wheel node) are left alone.
DriveGear planDriveGear(const std::vector<const formats::MeshPart*>& parts, const std::vector<std::uint8_t>& skipJoint);

}  // namespace d2k::render3d

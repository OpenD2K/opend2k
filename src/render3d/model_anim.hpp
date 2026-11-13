#pragma once
// Pose evaluation of animated glTF models (docs/3d-models.md "Animation"): the glTF animation
// channels (translation / rotation / scale, LINEAR or STEP) of a formats::Model3D::Skeleton at a
// time -> the joint palette the model shaders skin with (render3d/models.*). Pure CPU maths, no GPU.

#include "formats/model3d.hpp"

#include <vector>

namespace d2k::render3d {

// One model-space matrix per skeleton node (column-major, 16 floats each, appended to `out`): it
// takes a vertex stored in the rest pose (model space, px) to its place in the pose `anim` at
// `time` seconds (anim < 0 or out of range: the rest pose = identities). Times outside the
// animation clamp to its first / last key, as glTF players do.
void posePalette(const formats::Model3D::Skeleton& sk, int anim, float time, std::vector<float>& out);

// World matrices (glTF space) of every node in the rest pose (column-major, 16 floats per node).
void restWorld(const formats::Model3D::Skeleton& sk, std::vector<float>& out);

// Column-major 4x4 helpers shared with the loader.
void mat4Mul(const float a[16], const float b[16], float out[16]);
bool mat4Inverse(const float m[16], float out[16]);
void mat4FromTrs(const float t[3], const float r[4], const float s[3], float out[16]);

}  // namespace d2k::render3d

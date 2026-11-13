#include "render3d/camera3d.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::render3d {

void Camera3D::update(const CameraPose& want, float groundY, float dt, bool snap) {
    if (!init_ || snap) {
        cur_ = want;
        groundY_ = groundY;
        init_ = true;
    } else {
        // Exponential easing: frame-rate independent.
        const float kPos = 1.0f - std::exp(-dt / 0.06f), kRot = 1.0f - std::exp(-dt / 0.09f);
        cur_.cx += (want.cx - cur_.cx) * kPos;
        cur_.cy += (want.cy - cur_.cy) * kPos;
        cur_.zoom *= std::pow(want.zoom / cur_.zoom, kRot);
        float dy = std::remainder(want.yaw - cur_.yaw, 2 * kPi);
        cur_.yaw += dy * kRot;
        cur_.tilt += (want.tilt - cur_.tilt) * kRot;
        groundY_ += (groundY - groundY_) * (1.0f - std::exp(-dt / 0.25f));
        // Close enough: settle exactly (keeps screenshots / picking stable).
        if (std::abs(want.cx - cur_.cx) < 0.01f && std::abs(want.cy - cur_.cy) < 0.01f) { cur_.cx = want.cx; cur_.cy = want.cy; }
        if (std::abs(want.zoom / cur_.zoom - 1) < 1e-4f) cur_.zoom = want.zoom;
        if (std::abs(dy) < 1e-4f) cur_.yaw = want.yaw;
        if (std::abs(want.tilt - cur_.tilt) < 1e-4f) cur_.tilt = want.tilt;
    }
    rebuild();
}

void Camera3D::rebuild() {
    const float aspect = float(vw_) / float(std::max(1, vh_));
    const float halfTan = std::tan(kFovY * 0.5f);
    // The 2D zoom shows viewH / zoom world pixels vertically; the same at the look-at point.
    dist_ = (float(vh_) / cur_.zoom * 0.5f) / halfTan;
    projScale_ = (float(vh_) * 0.5f) / halfTan;
    const float tilt = std::clamp(cur_.tilt, kMinTilt, kMaxTilt);
    const Vec3 target{cur_.cx + shakeX_, groundY_, cur_.cy + shakeY_};
    // Screen-up direction on the ground: north (-Z) turned clockwise by yaw.
    const Vec3 ground{std::sin(cur_.yaw), 0, -std::cos(cur_.yaw)};
    eye_ = target + Vec3{0, std::cos(tilt), 0} * dist_ - ground * (std::sin(tilt) * dist_);
    view_ = lookAt(eye_, target, ground);
    // Near / far planes: generous for the low tilts, tight enough for a 24-bit-ish precision.
    proj_ = perspective(kFovY, aspect, dist_ * 0.05f, farDepth());
    viewProj_ = proj_ * view_;
    fwd_ = normalize(target - eye_);
    right_ = normalize(cross(fwd_, ground));
    up_ = cross(right_, fwd_);
}

bool Camera3D::project(const Vec3& p, float& sx, float& sy) const {
    const Vec4 c = viewProj_ * Vec4{p.x, p.y, p.z, 1};
    if (c.w <= 1e-4f) return false;
    sx = (c.x / c.w * 0.5f + 0.5f) * float(vw_);
    sy = (0.5f - c.y / c.w * 0.5f) * float(vh_);
    return true;
}

void Camera3D::ray(float sx, float sy, Vec3& origin, Vec3& dir) const {
    const float halfTan = std::tan(kFovY * 0.5f);
    const float aspect = float(vw_) / float(std::max(1, vh_));
    const float nx = (sx / float(vw_)) * 2 - 1, ny = 1 - (sy / float(vh_)) * 2;
    origin = eye_;
    dir = normalize(fwd_ + right_ * (nx * halfTan * aspect) + up_ * (ny * halfTan));
}

void Camera3D::groundPoint(float sx, float sy, float& wx, float& wz) const {
    Vec3 o, d;
    ray(sx, sy, o, d);
    const float planeY = eye_.y + fwd_.y * dist_;  // the look-at point's height
    const float along = std::max(1e-4f, dot(d, fwd_));
    float t = farDepth() / along;
    if (d.y < -1e-6f) t = std::min(t, (planeY - o.y) / d.y);
    wx = o.x + d.x * t;
    wz = o.z + d.z * t;
}

}  // namespace d2k::render3d

#pragma once
// RTS camera of the 3D mode (docs/3d-mode.md "Camera"): orbits a ground point (the centre of the
// session's 2D view rectangle) at a distance derived from the 2D zoom, with a tilt (0 = straight
// down) and a yaw (0 = north up, clockwise). The session owns the wanted state (CameraPose);
// the camera eases towards it every frame, and picking / projection use the eased state, i.e.
// exactly what is on screen.

#include "render3d/math3d.hpp"

namespace d2k::render3d {

struct CameraPose {
    float cx = 0, cy = 0;  // looked-at ground point, world pixels
    float zoom = 1;        // the 2D zoom: screen pixels per world pixel at the look-at point
    float yaw = 0;         // radians, clockwise from north
    float tilt = 0;        // radians from vertical
};

class Camera3D {
public:
    static constexpr float kFovY = 0.5236f;  // 30 degrees: little perspective distortion
    static constexpr float kDefaultTilt = 0.61f;  // ~35 degrees from vertical (55 from the horizon)
    static constexpr float kMinTilt = 0.0f, kMaxTilt = 1.15f;  // 0 .. ~66 degrees (3D models)
    // Sprite objects (pre-rendered, one view): beyond ~50 degrees the upright billboards read as
    // cards standing on the map; below ~20 the 3D adds nothing over the 2D view.
    static constexpr float kSpriteMinTilt = 0.35f, kSpriteMaxTilt = 0.87f;  // 20 .. 50 degrees

    void setViewport(int w, int h) { vw_ = w; vh_ = h; }
    int viewW() const { return vw_; }
    int viewH() const { return vh_; }
    // Moves the shown pose towards `want` (time constant ~0.1 s; snap: jump there) and rebuilds
    // the matrices. groundY: terrain height under the look-at point (eased too).
    void update(const CameraPose& want, float groundY, float dt, bool snap);
    // Screen shake (world px added to the look-at point after the easing; Modern effects).
    void setShake(float x, float y) { shakeX_ = x; shakeY_ = y; }

    const CameraPose& pose() const { return cur_; }
    const Mat4& viewProj() const { return viewProj_; }
    const Vec3& eye() const { return eye_; }
    const Vec3& forward() const { return fwd_; }   // view direction
    const Vec3& right() const { return right_; }   // screen right in the world
    const Vec3& up() const { return up_; }         // screen up in the world
    float distance() const { return dist_; }
    // Screen pixels per world pixel at view depth `depth` (distance along forward()).
    float pixelsPerUnit(float depth) const { return depth > 1e-3f ? projScale_ / depth : projScale_; }
    float depthOf(const Vec3& p) const { return dot(p - eye_, fwd_); }

    // World point -> view pixels (origin top-left of the 3D view). False behind the camera.
    bool project(const Vec3& p, float& sx, float& sy) const;
    // View pixel -> world ray (origin at the eye, unit direction).
    void ray(float sx, float sy, Vec3& origin, Vec3& dir) const;
    // View pixel -> world point (x, z) on the flat ground plane through the look-at point (the
    // eased terrain height there), for the minimap footprint. A ray that misses the ground or
    // meets it beyond the far plane stops at the far plane's depth (then dropped onto the plane),
    // so a view near the horizon gives a large but finite polygon. Translation invariant: moving
    // the look-at moves every point by the same amount.
    void groundPoint(float sx, float sy, float& wx, float& wz) const;
    float farDepth() const { return dist_ * 12.0f; }

private:
    void rebuild();
    int vw_ = 1, vh_ = 1;
    CameraPose cur_;
    float groundY_ = 0;
    float shakeX_ = 0, shakeY_ = 0;
    bool init_ = false;
    Mat4 view_, proj_, viewProj_;
    Vec3 eye_, fwd_, right_, up_;
    float dist_ = 1000, projScale_ = 1;
};

}  // namespace d2k::render3d

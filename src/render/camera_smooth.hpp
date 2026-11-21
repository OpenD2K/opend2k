#pragma once
// "Smooth camera" (Settings::smoothCamera; not original - docs/STATUS.md "Smooth camera"): the
// camera's motion model. Pure math (no SDL, no World), owned by the session, which feeds it the
// input and applies the results to its 2D camera (render::Camera) / the 3D camera's wanted pose.
//  - Keyboard / edge scrolling: the speed follows the held keys with an exponential ease (speeds
//    up / slows down in ~0.18 s) instead of starting and stopping at full speed.
//  - Right / middle drag pans 1:1; on release the view keeps most of the drag's last speed and coasts to
//    a stop (flick), unless the pointer rested before the release.
//  - Wheel zoom: the wanted zoom moves at once, the shown zoom eases to it (~0.14 s); the session
//    re-anchors every step at the cursor point, so that point stays put through the whole glide.
//  - Glides (minimap jumps when near): an exponential approach of the view position (~0.2 s).
//  - 3D turn / tilt keys: the rate eases in / out like the scrolling.
// Every step is the exact solution of the exponential for its dt, so the motion is the same at
// 60 / 120 / 144 Hz (and with uneven frames). Nothing here allocates.

namespace d2k::render {

// One axis of a velocity that eases towards a wanted velocity: v(t) = want + (v0 - want) e^(-t/tau).
struct EasedRate {
    float v = 0;
    // Advances by dt seconds; returns the distance covered (the integral of v over dt).
    float step(float want, float dt, float tau);
};

class CameraSmoother {
public:
    static constexpr float kScrollTau = 0.06f;  // keys / edges: ~95 % of the speed in 0.18 s, stop in ~0.18 s
    static constexpr float kFlickTau = 0.15f;   // drag-release momentum (coasts ~kFlickKeep * v * 0.15 s)
    static constexpr float kFlickKeep = 0.7f;   // share of the drag speed kept on release
    static constexpr float kZoomTau = 0.045f;   // wheel zoom: ~95 % in 0.135 s
    static constexpr float kGlideTau = 0.07f;   // minimap jumps: ~95 % in 0.21 s
    static constexpr float kTurnTau = 0.06f;    // 3D turn / tilt keys

    void reset();

    // --- Scrolling (velocities in view-aligned world px per second, as Session::panView takes them).
    // Keyboard / edge input this frame (0, 0 = nothing held) -> this frame's displacement.
    void scroll(float wantX, float wantY, float dt, float& dx, float& dy);
    bool moving() const { return x_.v != 0 || y_.v != 0; }
    // The view hit a map edge on that axis: drop the speed there (no push / overshoot past it).
    void stopX() { x_.v = 0; }
    void stopY() { y_.v = 0; }
    void stop() { x_.v = y_.v = 0; flick_ = false; }

    // --- Drag (pans 1:1 elsewhere; this only measures the speed for the release).
    void dragBegin();
    void dragSample(float dx, float dy, double seconds);  // the displacement applied, its time
    // Starts the momentum from the drag's speed over its last ~0.1 s (none when the pointer
    // rested > 0.06 s before the release); maxSpeed caps it (world px / s).
    void dragRelease(double seconds, float maxSpeed);

    // --- Zoom. The wanted zoom (clamped to minZ..maxZ) and the view point to keep fixed.
    void zoomBy(float factor, float currentZoom, float anchorX, float anchorY, float minZ, float maxZ);
    bool zooming() const { return zooming_; }
    // The zoom to show after dt seconds from `zoom` (= the target once close enough; then idle).
    float zoomStep(float zoom, float dt);
    float zoomTarget() const { return zoomTarget_; }
    float anchorX() const { return anchorX_; }
    float anchorY() const { return anchorY_; }
    void cancelZoom() { zooming_ = false; }

    // --- Glide of the view's top-left corner (2D camera position) to (x, y).
    void glideTo(float x, float y);
    bool gliding() const { return gliding_; }
    void cancelGlide() { gliding_ = false; }
    // Moves (x, y) towards the glide target; false = no glide (left alone).
    bool glideStep(float& x, float& y, float dt);

    // --- 3D turn / tilt keys: rates in radians per second.
    float turn(float want, float dt) { return yaw_.step(want, dt, kTurnTau); }
    float tilt(float want, float dt) { return tilt_.step(want, dt, kTurnTau); }

private:
    EasedRate x_, y_, yaw_, tilt_;
    bool flick_ = false;
    struct Sample { float dx = 0, dy = 0; double t = -1; };
    static constexpr int kSamples = 16;
    Sample samples_[kSamples];
    int sampleHead_ = 0;
    bool zooming_ = false;
    float zoomTarget_ = 1, anchorX_ = 0, anchorY_ = 0;
    bool gliding_ = false;
    float glideX_ = 0, glideY_ = 0;
};

}  // namespace d2k::render

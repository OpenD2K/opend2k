#include "render/camera_smooth.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::render {

float EasedRate::step(float want, float dt, float tau) {
    if (!(dt > 0)) return 0;
    const float e = std::exp(-dt / tau);
    const float d = want * dt + (v - want) * tau * (1 - e);
    v = want + (v - want) * e;
    if (want == 0 && std::abs(v) < 0.5f) v = 0;  // settled (well under a pixel per second)
    return d;
}

void CameraSmoother::reset() { *this = CameraSmoother{}; }

void CameraSmoother::scroll(float wantX, float wantY, float dt, float& dx, float& dy) {
    const bool held = wantX != 0 || wantY != 0;
    if (held) flick_ = false;
    const float tau = flick_ ? kFlickTau : kScrollTau;
    dx = x_.step(wantX, dt, tau);
    dy = y_.step(wantY, dt, tau);
    if (!moving()) flick_ = false;
}

void CameraSmoother::dragBegin() {
    stop();
    for (Sample& s : samples_) s = Sample{};
    sampleHead_ = 0;
}

void CameraSmoother::dragSample(float dx, float dy, double seconds) {
    samples_[sampleHead_] = Sample{dx, dy, seconds};
    sampleHead_ = (sampleHead_ + 1) % kSamples;
}

void CameraSmoother::dragRelease(double seconds, float maxSpeed) {
    constexpr double kWindow = 0.1, kRest = 0.06;
    double newest = -1, oldest = seconds;
    float sx = 0, sy = 0;
    for (const Sample& s : samples_) {
        if (s.t < 0 || s.t < seconds - kWindow || s.t > seconds) continue;
        newest = std::max(newest, s.t);
        oldest = std::min(oldest, s.t);
        sx += s.dx;
        sy += s.dy;
    }
    stop();
    if (newest < 0 || seconds - newest > kRest) return;  // the pointer rested: no momentum
    // The motion events sum the movement since the previous one, so the oldest sample covers the
    // time before it too: measure over the window that ends at the newest sample.
    const double span = std::max(1.0 / 120.0, std::min(kWindow, seconds - oldest + 1.0 / 120.0));
    float vx = float(sx / span), vy = float(sy / span);
    const float sp = std::hypot(vx, vy);
    if (sp > maxSpeed && sp > 0) vx *= maxSpeed / sp, vy *= maxSpeed / sp;
    x_.v = vx * kFlickKeep;
    y_.v = vy * kFlickKeep;
    flick_ = moving();
}

void CameraSmoother::zoomBy(float factor, float currentZoom, float anchorX, float anchorY, float minZ, float maxZ) {
    const float from = zooming_ ? zoomTarget_ : currentZoom;
    zoomTarget_ = std::clamp(from * factor, minZ, maxZ);
    anchorX_ = anchorX;
    anchorY_ = anchorY;
    zooming_ = std::abs(zoomTarget_ / currentZoom - 1) > 1e-5f;
}

float CameraSmoother::zoomStep(float zoom, float dt) {
    if (!zooming_) return zoom;
    if (!(dt > 0)) return zoom;
    // Exponential in log space: the same relative speed zooming in and out.
    const float k = 1 - std::exp(-dt / kZoomTau);
    float z = zoom * std::pow(zoomTarget_ / zoom, k);
    if (std::abs(z / zoomTarget_ - 1) < 2e-3f) {
        z = zoomTarget_;
        zooming_ = false;
    }
    return z;
}

void CameraSmoother::glideTo(float x, float y) {
    glideX_ = x;
    glideY_ = y;
    gliding_ = true;
    stop();
}

bool CameraSmoother::glideStep(float& x, float& y, float dt) {
    if (!gliding_) return false;
    const float k = dt > 0 ? 1 - std::exp(-dt / kGlideTau) : 0.0f;
    x += (glideX_ - x) * k;
    y += (glideY_ - y) * k;
    if (std::abs(glideX_ - x) < 0.25f && std::abs(glideY_ - y) < 0.25f) {
        x = glideX_;
        y = glideY_;
        gliding_ = false;
    }
    return true;
}

}  // namespace d2k::render

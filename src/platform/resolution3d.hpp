#pragma once
// "3D resolution: Auto" (Settings::resolution3d = 0; docs/3d-mode.md "Performance"): the share of
// the view's pixels per axis the 3D battlefield is rendered at (Renderer3D::Options::renderScale).
//
//   - cap: 100 % while the view has at most budgetMp() megapixels (MSAA 4x; more with 2x / off),
//     less on very large outputs: a 5120 x 1924 window (9.9 MP) starts at 0.75 (5.5 MP);
//   - dynamic: while the frames are GPU-bound (the frame interval clearly longer than the CPU's
//     own work) and slower than the target rate (the display's refresh rate or the frame limit),
//     it steps down (to kMin); after a few steady seconds at the target it probes a step up again,
//     waiting longer each time a probe had to be taken back (no ping-pong).
// Fixed-step runs (screenshots, recordings) use the cap only: reproducible pictures.
// Pure logic (no SDL): game_test checks it.

#include <algorithm>
#include <cmath>

namespace d2k::platform {

class Resolution3D {
public:
    static constexpr float kStep = 0.05f;
    static constexpr float kMin = 0.6f;
    // Megapixels of the 3D pass at full rate before Auto scales down (per MSAA level). Measured on
    // an M4 Pro with the Faithful terrain, High quality, a busy base: ~6 MP at 4x keeps a 100 Hz
    // display fed (docs/3d-mode.md "Performance").
    static float budgetMp(int msaa) { return msaa >= 4 ? 6.0f : msaa >= 2 ? 7.5f : 9.0f; }

    // The highest scale Auto uses for a view of w x h pixels.
    static float cap(int w, int h, int msaa) {
        const float mp = float(w) * float(h) / 1.0e6f;
        if (mp <= 0) return 1.0f;
        const float c = std::sqrt(budgetMp(msaa) / mp);
        return c >= 1.0f ? 1.0f : std::max(kMin, std::floor(c / kStep) * kStep);
    }

    // One frame. fixedPercent: Settings::resolution3d (0 = Auto). frameMs: the interval since the
    // last frame; cpuMs: this frame's own CPU work (update + render); targetMs: the wanted frame
    // interval (1000 / refresh rate, or the frame limit); measure = false: fixed-step runs, a
    // pause of the measuring (loading) - the scale stays at the cap / as it is.
    float update(int fixedPercent, int w, int h, int msaa, double frameMs, double cpuMs, double targetMs, bool measure) {
        if (fixedPercent > 0) {
            scale_ = std::clamp(float(fixedPercent) / 100.0f, 0.25f, 1.0f);
            return scale_;
        }
        const float c = cap(w, h, msaa);
        if (w != w_ || h != h_ || msaa != msaa_ || scale_ <= 0) {  // a new view: start at the cap
            w_ = w;
            h_ = h;
            msaa_ = msaa;
            scale_ = c;
            reset();
        }
        scale_ = std::min(scale_, c);
        if (!measure || frameMs <= 0 || targetMs <= 0 || frameMs > 250) return scale_;
        ema_ = ema_ <= 0 ? frameMs : ema_ * 0.9 + frameMs * 0.1;
        cpuEma_ = cpuEma_ <= 0 ? cpuMs : cpuEma_ * 0.9 + cpuMs * 0.1;
        sinceChange_ += frameMs / 1000.0;
        const bool gpuBound = ema_ > cpuEma_ * 1.3 + 1.0;
        if (ema_ > targetMs * 1.06 && gpuBound) {
            slowFor_ += frameMs / 1000.0;
            fastFor_ = 0;
        } else if (ema_ < targetMs * 1.03) {
            fastFor_ += frameMs / 1000.0;
            slowFor_ = 0;
        } else {
            slowFor_ = fastFor_ = 0;
        }
        if (slowFor_ > 0.5 && sinceChange_ > 0.5 && scale_ > kMin + 1e-3f) {
            scale_ = std::max(kMin, scale_ - kStep);
            if (probing_) probeWait_ = std::min(60.0, probeWait_ * 2);  // the probe did not hold
            probing_ = false;
            changed();
        } else if (fastFor_ > probeWait_ && sinceChange_ > probeWait_ && scale_ < c - 1e-3f) {
            scale_ = std::min(c, scale_ + kStep);
            probing_ = true;
            changed();
        } else if (probing_ && sinceChange_ > 3) {
            probing_ = false;  // the step up held
            probeWait_ = std::max(3.0, probeWait_ * 0.75);
        }
        return scale_;
    }
    float scale() const { return scale_; }

private:
    void reset() {
        ema_ = cpuEma_ = 0;
        slowFor_ = fastFor_ = sinceChange_ = 0;
        probing_ = false;
        probeWait_ = 3;
    }
    void changed() {
        sinceChange_ = slowFor_ = fastFor_ = 0;
        ema_ = 0;  // the new size's own times
    }
    int w_ = 0, h_ = 0, msaa_ = 0;
    float scale_ = 0;
    double ema_ = 0, cpuEma_ = 0, slowFor_ = 0, fastFor_ = 0, sinceChange_ = 0, probeWait_ = 3;
    bool probing_ = false;
};

}  // namespace d2k::platform

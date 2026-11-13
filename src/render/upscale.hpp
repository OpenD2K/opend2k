#pragma once
// Pixel-art upscaling for the optional HQ mode: Scale2x (AdvMAME2x / EPX), an edge-aware
// 2x scaler that never invents colours; applied twice for 4x. Works on exact colour
// equality, so palette pixels, transparency and house colours stay crisp.

#include <cstdint>
#include <vector>

namespace d2k::render {

inline std::vector<std::uint32_t> scale2x(const std::vector<std::uint32_t>& src, int w, int h) {
    std::vector<std::uint32_t> dst(static_cast<std::size_t>(w) * h * 4);
    auto at = [&](int x, int y) {
        x = x < 0 ? 0 : (x >= w ? w - 1 : x);
        y = y < 0 ? 0 : (y >= h ? h - 1 : y);
        return src[static_cast<std::size_t>(y) * w + x];
    };
    const int dw = w * 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::uint32_t b = at(x, y - 1), d = at(x - 1, y), e = at(x, y), f = at(x + 1, y), hh = at(x, y + 1);
            std::uint32_t e0 = e, e1 = e, e2 = e, e3 = e;
            if (b != hh && d != f) {
                e0 = d == b ? d : e;
                e1 = b == f ? f : e;
                e2 = d == hh ? d : e;
                e3 = hh == f ? f : e;
            }
            const std::size_t o = static_cast<std::size_t>(y * 2) * dw + x * 2;
            dst[o] = e0;
            dst[o + 1] = e1;
            dst[o + dw] = e2;
            dst[o + dw + 1] = e3;
        }
    return dst;
}

// 4x = Scale2x applied twice.
inline std::vector<std::uint32_t> scale4x(const std::vector<std::uint32_t>& src, int w, int h) {
    return scale2x(scale2x(src, w, h), w * 2, h * 2);
}

}  // namespace d2k::render

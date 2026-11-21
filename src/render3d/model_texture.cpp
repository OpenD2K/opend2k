#include "render3d/model_texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace d2k::render3d {

namespace {

int blockBytes(TexFormat f) { return f == TexFormat::BC1 ? 8 : 16; }

// One 2x2 box-filtered level below (w x h, R lowest byte); odd sizes clamp the last row / column.
std::vector<std::uint32_t> halve(const std::vector<std::uint32_t>& src, int w, int h, int& ow, int& oh) {
    ow = std::max(1, w / 2);
    oh = std::max(1, h / 2);
    std::vector<std::uint32_t> dst(std::size_t(ow) * std::size_t(oh));
    for (int y = 0; y < oh; ++y) {
        const int y0 = std::min(h - 1, y * 2), y1 = std::min(h - 1, y * 2 + 1);
        for (int x = 0; x < ow; ++x) {
            const int x0 = std::min(w - 1, x * 2), x1 = std::min(w - 1, x * 2 + 1);
            const std::uint32_t a = src[std::size_t(y0) * std::size_t(w) + std::size_t(x0)];
            const std::uint32_t b = src[std::size_t(y0) * std::size_t(w) + std::size_t(x1)];
            const std::uint32_t c = src[std::size_t(y1) * std::size_t(w) + std::size_t(x0)];
            const std::uint32_t d = src[std::size_t(y1) * std::size_t(w) + std::size_t(x1)];
            std::uint32_t o = 0;
            for (int k = 0; k < 32; k += 8) {
                const std::uint32_t s = ((a >> k) & 0xFF) + ((b >> k) & 0xFF) + ((c >> k) & 0xFF) + ((d >> k) & 0xFF);
                o |= ((s + 2) >> 2) << k;
            }
            dst[std::size_t(y) * std::size_t(ow) + std::size_t(x)] = o;
        }
    }
    return dst;
}

inline int to5(float v) { return std::clamp(int(std::lround(v * 31.0f / 255.0f)), 0, 31); }
inline int to6(float v) { return std::clamp(int(std::lround(v * 63.0f / 255.0f)), 0, 63); }
inline std::uint16_t pack565(const float c[3]) { return std::uint16_t((to5(c[0]) << 11) | (to6(c[1]) << 5) | to5(c[2])); }
inline void unpack565(std::uint16_t v, int c[3]) {
    const int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    c[0] = (r << 3) | (r >> 2);
    c[1] = (g << 2) | (g >> 4);
    c[2] = (b << 3) | (b >> 2);
}

// The 4-colour palette of endpoints (a, b) and the best index per texel; returns the squared error.
int fitIndices(const int px[16][3], std::uint16_t a, std::uint16_t b, std::uint8_t idx[16]) {
    int p[4][3];
    unpack565(a, p[0]);
    unpack565(b, p[1]);
    for (int k = 0; k < 3; ++k) {
        p[2][k] = (2 * p[0][k] + p[1][k] + 1) / 3;
        p[3][k] = (p[0][k] + 2 * p[1][k] + 1) / 3;
    }
    int err = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 1 << 30, bi = 0;
        for (int j = 0; j < 4; ++j) {
            const int dr = px[i][0] - p[j][0], dg = px[i][1] - p[j][1], db = px[i][2] - p[j][2];
            const int e = dr * dr + dg * dg + db * db;
            if (e < best) best = e, bi = j;
        }
        idx[i] = std::uint8_t(bi);
        err += best;
    }
    return err;
}

}  // namespace

void encodeBc1(const std::uint32_t in[16], std::uint8_t out[8]) {
    int px[16][3];
    float mean[3] = {0, 0, 0}, lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i)
        for (int k = 0; k < 3; ++k) {
            px[i][k] = int((in[i] >> (8 * k)) & 0xFF);
            mean[k] += float(px[i][k]);
            lo[k] = std::min(lo[k], float(px[i][k]));
            hi[k] = std::max(hi[k], float(px[i][k]));
        }
    for (float& m : mean) m /= 16.0f;
    // Principal axis (covariance, power iteration from the bounding box diagonal).
    float cov[6] = {};
    for (int i = 0; i < 16; ++i) {
        const float r = float(px[i][0]) - mean[0], g = float(px[i][1]) - mean[1], b = float(px[i][2]) - mean[2];
        cov[0] += r * r, cov[1] += r * g, cov[2] += r * b, cov[3] += g * g, cov[4] += g * b, cov[5] += b * b;
    }
    float ax[3] = {hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};
    for (int it = 0; it < 6; ++it) {
        const float x = cov[0] * ax[0] + cov[1] * ax[1] + cov[2] * ax[2];
        const float y = cov[1] * ax[0] + cov[3] * ax[1] + cov[4] * ax[2];
        const float z = cov[2] * ax[0] + cov[4] * ax[1] + cov[5] * ax[2];
        const float n = std::max({std::abs(x), std::abs(y), std::abs(z)});
        if (n < 1e-6f) break;
        ax[0] = x / n, ax[1] = y / n, ax[2] = z / n;
    }
    const float an = ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2];
    float e0[3], e1[3];
    if (an < 1e-8f) {
        std::memcpy(e0, mean, sizeof e0);
        std::memcpy(e1, mean, sizeof e1);
    } else {
        float tmin = 1e30f, tmax = -1e30f;
        for (int i = 0; i < 16; ++i) {
            const float t = ((float(px[i][0]) - mean[0]) * ax[0] + (float(px[i][1]) - mean[1]) * ax[1] +
                             (float(px[i][2]) - mean[2]) * ax[2]) / an;
            tmin = std::min(tmin, t), tmax = std::max(tmax, t);
        }
        for (int k = 0; k < 3; ++k) {
            e0[k] = std::clamp(mean[k] + ax[k] * tmax, 0.0f, 255.0f);
            e1[k] = std::clamp(mean[k] + ax[k] * tmin, 0.0f, 255.0f);
        }
    }
    std::uint16_t a = pack565(e0), b = pack565(e1);
    std::uint8_t idx[16], tryIdx[16];
    int err = fitIndices(px, a, b, idx);
    // Least-squares refinement of the endpoints for the chosen indices (twice).
    static constexpr float kW[4] = {1.0f, 0.0f, 2.0f / 3.0f, 1.0f / 3.0f};  // weight of endpoint a per index
    for (int it = 0; it < 2 && err > 0; ++it) {
        float aa = 0, ab = 0, bb = 0, ax3[3] = {}, bx3[3] = {};
        for (int i = 0; i < 16; ++i) {
            const float wa = kW[idx[i]], wb = 1.0f - wa;
            aa += wa * wa, ab += wa * wb, bb += wb * wb;
            for (int k = 0; k < 3; ++k) ax3[k] += wa * float(px[i][k]), bx3[k] += wb * float(px[i][k]);
        }
        const float det = aa * bb - ab * ab;
        if (std::abs(det) < 1e-6f) break;
        float na[3], nb[3];
        for (int k = 0; k < 3; ++k) {
            na[k] = std::clamp((ax3[k] * bb - bx3[k] * ab) / det, 0.0f, 255.0f);
            nb[k] = std::clamp((bx3[k] * aa - ax3[k] * ab) / det, 0.0f, 255.0f);
        }
        const std::uint16_t a2 = pack565(na), b2 = pack565(nb);
        const int e2 = fitIndices(px, a2, b2, tryIdx);
        if (e2 >= err) break;
        a = a2, b = b2, err = e2;
        std::memcpy(idx, tryIdx, sizeof idx);
    }
    // 4-colour mode needs a > b (BC3's colour block is always read that way; BC1 picks the mode by it).
    if (a < b) {
        std::swap(a, b);
        for (auto& i : idx) i = std::uint8_t(i == 0 ? 1 : i == 1 ? 0 : i == 2 ? 3 : 2);
    } else if (a == b) {
        std::memset(idx, 0, sizeof idx);
    }
    out[0] = std::uint8_t(a & 0xFF), out[1] = std::uint8_t(a >> 8);
    out[2] = std::uint8_t(b & 0xFF), out[3] = std::uint8_t(b >> 8);
    for (int r = 0; r < 4; ++r)
        out[4 + r] = std::uint8_t(idx[r * 4] | (idx[r * 4 + 1] << 2) | (idx[r * 4 + 2] << 4) | (idx[r * 4 + 3] << 6));
}

void decodeBc1(const std::uint8_t in[8], std::uint32_t px[16]) {
    const std::uint16_t a = std::uint16_t(in[0] | (in[1] << 8)), b = std::uint16_t(in[2] | (in[3] << 8));
    int p[4][3];
    unpack565(a, p[0]);
    unpack565(b, p[1]);
    for (int k = 0; k < 3; ++k) {
        p[2][k] = (2 * p[0][k] + p[1][k] + 1) / 3;
        p[3][k] = (p[0][k] + 2 * p[1][k] + 1) / 3;
    }
    for (int i = 0; i < 16; ++i) {
        const int j = (in[4 + i / 4] >> ((i % 4) * 2)) & 3;
        px[i] = std::uint32_t(p[j][0]) | (std::uint32_t(p[j][1]) << 8) | (std::uint32_t(p[j][2]) << 16) | 0xFF000000u;
    }
}

namespace {
// BC4 palette of (a0, a1): 8 values when a0 > a1, else 6 + exact 0 and 255.
void bc4Palette(int a0, int a1, int p[8]) {
    p[0] = a0, p[1] = a1;
    if (a0 > a1) {
        for (int k = 1; k < 7; ++k) p[k + 1] = ((7 - k) * a0 + k * a1 + 3) / 7;
    } else {
        for (int k = 1; k < 5; ++k) p[k + 1] = ((5 - k) * a0 + k * a1 + 2) / 5;
        p[6] = 0, p[7] = 255;
    }
}
int bc4Fit(const std::uint8_t v[16], int a0, int a1, std::uint8_t idx[16]) {
    int p[8];
    bc4Palette(a0, a1, p);
    int err = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 1 << 30, bi = 0;
        for (int j = 0; j < 8; ++j) {
            const int d = int(v[i]) - p[j];
            if (d * d < best) best = d * d, bi = j;
        }
        idx[i] = std::uint8_t(bi);
        err += best;
    }
    return err;
}
}  // namespace

void encodeBc4(const std::uint8_t v[16], std::uint8_t out[8]) {
    int lo = 255, hi = 0, lo6 = 255, hi6 = 0;
    for (int i = 0; i < 16; ++i) {
        lo = std::min(lo, int(v[i])), hi = std::max(hi, int(v[i]));
        if (v[i] != 0 && v[i] != 255) lo6 = std::min(lo6, int(v[i])), hi6 = std::max(hi6, int(v[i]));
    }
    std::uint8_t idx[16], idx6[16];
    int a0 = hi, a1 = lo;
    int err = 0;
    if (hi == lo) {
        std::memset(idx, 0, sizeof idx);
    } else {
        err = bc4Fit(v, hi, lo, idx);
        // 6-value mode: exact 0 / 255 (cut-out and opaque alpha) plus 6 steps over the values between.
        if (lo6 > hi6) lo6 = hi6 = lo;
        const int e6 = bc4Fit(v, lo6, hi6, idx6);
        if (e6 < err) {
            a0 = lo6, a1 = hi6;
            std::memcpy(idx, idx6, sizeof idx);
        }
    }
    out[0] = std::uint8_t(a0), out[1] = std::uint8_t(a1);
    std::uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) bits |= std::uint64_t(idx[i]) << (3 * i);
    for (int k = 0; k < 6; ++k) out[2 + k] = std::uint8_t(bits >> (8 * k));
}

void decodeBc4(const std::uint8_t in[8], std::uint8_t v[16]) {
    int p[8];
    bc4Palette(in[0], in[1], p);
    std::uint64_t bits = 0;
    for (int k = 0; k < 6; ++k) bits |= std::uint64_t(in[2 + k]) << (8 * k);
    for (int i = 0; i < 16; ++i) v[i] = std::uint8_t(p[(bits >> (3 * i)) & 7]);
}

std::size_t PreparedTexture::levelBytes(int level) const {
    const int lw = levelW(level), lh = levelH(level);
    if (format == TexFormat::RGBA8) return std::size_t(lw) * std::size_t(lh) * 4;
    return std::size_t((lw + 3) / 4) * std::size_t((lh + 3) / 4) * std::size_t(blockBytes(format));
}

std::size_t PreparedTexture::levelOffset(int level) const {
    std::size_t o = 0;
    for (int l = 0; l < level; ++l) o += levelBytes(l);
    return o;
}

PreparedTexture prepareTexture(const std::vector<std::uint32_t>& rgba, int w, int h, TexKind kind, float texelsPerPx,
                               const TexCaps& caps) {
    PreparedTexture t;
    if (w <= 0 || h <= 0 || rgba.size() < std::size_t(w) * std::size_t(h)) return t;
    // Levels the closest zoom never reaches.
    int drop = 0;
    for (float d = texelsPerPx; d > 0 && d * 0.5f >= kMaxTexelsPerWorldPx && (w >> (drop + 1)) >= 256 && (h >> (drop + 1)) >= 256;
         d *= 0.5f)
        ++drop;
    bool opaque = true;
    if (kind == TexKind::Color)
        for (const std::uint32_t p : rgba)
            if ((p >> 24) != 0xFF) {
                opaque = false;
                break;
            }
    const int bw = w >> drop, bh = h >> drop;
    const bool bc = caps.bc && bw % 4 == 0 && bh % 4 == 0;
    t.format = !bc ? TexFormat::RGBA8 : kind == TexKind::Normal ? TexFormat::BC5 : kind == TexKind::Color && opaque ? TexFormat::BC1
                                                                                                                      : TexFormat::BC3;
    t.w = bw, t.h = bh;
    // Level count: down to 1x1 (RGBA8), down to the last level of whole 4x4 blocks (BC).
    int levels = 1;
    for (int lw = bw, lh = bh; (lw > 1 || lh > 1) && (!bc || (lw / 2 >= 4 && lh / 2 >= 4)); lw = std::max(1, lw / 2), lh = std::max(1, lh / 2))
        ++levels;
    t.levels = levels;
    std::size_t total = 0;
    for (int l = 0; l < levels; ++l) total += t.levelBytes(l);
    t.data.resize(total);
    std::vector<std::uint32_t> cur;
    const std::vector<std::uint32_t>* lv = &rgba;
    int cw = w, ch = h;
    for (int l = -drop; l < levels; ++l) {
        if (l > -drop) {
            int nw, nh;
            std::vector<std::uint32_t> next = halve(*lv, cw, ch, nw, nh);
            cur.swap(next);
            lv = &cur;
            cw = nw, ch = nh;
        }
        if (l < 0) continue;
        std::uint8_t* dst = t.data.data() + t.levelOffset(l);
        if (t.format == TexFormat::RGBA8) {
            std::memcpy(dst, lv->data(), std::size_t(cw) * std::size_t(ch) * 4);
            continue;
        }
        const int bxN = (cw + 3) / 4, byN = (ch + 3) / 4, bb = blockBytes(t.format);
        for (int by = 0; by < byN; ++by)
            for (int bx = 0; bx < bxN; ++bx) {
                std::uint32_t blk[16];
                for (int i = 0; i < 16; ++i) {
                    const int x = std::min(cw - 1, bx * 4 + i % 4), y = std::min(ch - 1, by * 4 + i / 4);
                    blk[i] = (*lv)[std::size_t(y) * std::size_t(cw) + std::size_t(x)];
                }
                std::uint8_t* o = dst + (std::size_t(by) * std::size_t(bxN) + std::size_t(bx)) * std::size_t(bb);
                std::uint8_t ch8[16];
                switch (t.format) {
                case TexFormat::BC1: encodeBc1(blk, o); break;
                case TexFormat::BC3:
                    for (int i = 0; i < 16; ++i) ch8[i] = std::uint8_t(blk[i] >> 24);
                    encodeBc4(ch8, o);
                    encodeBc1(blk, o + 8);
                    break;
                case TexFormat::BC5:
                    for (int c = 0; c < 2; ++c) {
                        for (int i = 0; i < 16; ++i) ch8[i] = std::uint8_t(blk[i] >> (8 * c));
                        encodeBc4(ch8, o + 8 * c);
                    }
                    break;
                case TexFormat::RGBA8: break;
                }
            }
    }
    return t;
}

}  // namespace d2k::render3d

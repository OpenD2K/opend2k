#include "render3d/surround.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace d2k::render3d {

namespace {

float ss(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// Value noise (the same kind as the designed terrain's relief; deterministic, render-only).
float hash2(int x, int y) {
    std::uint32_t h = std::uint32_t(x) * 0x8da6b343u ^ std::uint32_t(y) * 0xd8163841u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return float(h & 0xFFFFFF) / float(0xFFFFFF);
}
float valueNoise(float x, float y) {
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = int(fx), iy = int(fy);
    const float tx = x - fx, ty = y - fy;
    const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    const float a = hash2(ix, iy), b = hash2(ix + 1, iy), c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    return (a * (1 - sx) + b * sx) * (1 - sy) + (c * (1 - sx) + d * sx) * sy;
}
// 0..1, `oct` octaves, the first with period `period` world px.
float fbm(float x, float y, float period, int oct) {
    float sum = 0, amp = 0.5f, norm = 0, f = 1.0f / period;
    for (int i = 0; i < oct; ++i) {
        sum += amp * valueNoise(x * f + 17.3f * float(i), y * f - 9.1f * float(i));
        norm += amp;
        amp *= 0.5f;
        f *= 2.03f;
    }
    return sum / norm;
}

// The guide height (0..1) whose Reimagined relief is h world px (the inverse of Terrain::reliefHeight).
float guideOf(float h) {
    if (h <= 0) return 0;
    float lo = 0, hi = 1;
    if (Terrain::reliefHeight(1.0f, true) <= h) return 1;
    for (int i = 0; i < 14; ++i) {
        const float mid = 0.5f * (lo + hi);
        if (Terrain::reliefHeight(mid, true) < h) lo = mid;
        else hi = mid;
    }
    return 0.5f * (lo + hi);
}

std::uint32_t pack(float r, float g, float b, float a) {
    auto q = [](float v) { return std::uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
}

// A coordinate mirrored across the map's edge (0 or len), at most `depth` px into the map.
float mirrorIn(float v, float len, float depth) {
    if (v < 0) return std::min(std::min(-v, depth), len);
    if (v > len) return std::max(len - std::min(v - len, depth), 0.0f);
    return v;
}

}  // namespace

void Surround::clear() {
    verts_.clear();
    idx_.clear();
    tex_.clear();
    hFar_.clear();
    seedA_.clear();
    seedB_.clear();
    cw_ = ch_ = ew_ = eh_ = 0;
    nearEdge_ = farEdge_ = indexCount_ = 0;
    top_ = 0;
}

int Surround::vid(int i, int j) const {
    if (i < 0 || j < 0 || i >= ew_ || j >= eh_) return -1;
    const int side = m_ + 1;  // grid columns left (and right) of the map's inside
    if (j <= m_) return j * ew_ + i;
    const int base = side * ew_;
    if (j < m_ + h4_) {
        const int row = base + (j - m_ - 1) * 2 * side;
        if (i <= m_) return row + i;
        if (i >= m_ + w4_) return row + side + (i - m_ - w4_);
        return -1;
    }
    return base + (h4_ - 1) * 2 * side + (j - m_ - h4_) * ew_ + i;
}

void Surround::seedSample(float x, float z, float& a, float& b) const {
    const float u = std::clamp(x / 32.0f - 0.5f, 0.0f, float(cw_ - 1)), v = std::clamp(z / 32.0f - 0.5f, 0.0f, float(ch_ - 1));
    const int x0 = std::min(int(u), cw_ - 2), y0 = std::min(int(v), ch_ - 2);
    const float fx = u - float(x0), fy = v - float(y0);
    auto at = [&](const std::vector<float>& f) {
        const float* r0 = &f[std::size_t(y0) * cw_ + x0];
        const float* r1 = r0 + cw_;
        return (r0[0] * (1 - fx) + r0[1] * fx) * (1 - fy) + (r1[0] * (1 - fx) + r1[1] * fx) * fy;
    };
    a = at(seedA_);
    b = at(seedB_);
}

// The procedural desert at a point outside the map (see the header).
Surround::Far Surround::farAt(float x, float z) const {
    Far f;
    const float M = margin();
    const float dx = std::max({-x, x - mapW_, 0.0f}), dz = std::max({-z, z - mapH_, 0.0f});
    const float d = std::hypot(dx, dz), dc = std::max(dx, dz);
    const float sx = seedX_, sz = seedZ_;
    // Flat towards the ring's outer edge (the apron's level): from a distance that wanders (so nothing ends
    // along a line parallel to the map), always by M - 130.
    const float nz = fbm(x + sx * 2.0f, z - sz * 2.0f, 330.0f, 2);
    const float T = 1.0f - ss((dc + nz * 110.0f - (M - 230.0f)) / 100.0f);
    // The blend from the mirrored map: none in the first 1-2 cells (the edge's own ground goes on exactly),
    // all of it from ~3-4: a mirror kept longer reads as a copied block (a ledge inside the map coming back
    // as a ledge parallel to the edge).
    f.w = ss((d - 34.0f - 30.0f * fbm(x + sx, z + sz, 150.0f, 2)) / 64.0f);
    // The edge zone's rock carried on outwards: the map's rock (per cell, blurred) read at the mirrored
    // point - under a cell in, i.e. the edge's own rock and level, running straight out. So that it does not read as a block
    // pushed out of the map: the read point is warped at two scales (the sides bend and wander, more with
    // distance), the outline is eaten into by a noise threshold (bays, spurs, rounded corners), the rock
    // narrows outwards and ends at a distance that changes quickly along the edge - in a cliff (or a low
    // shore) -, and its top steps down / swells instead of staying one level.
    const float far01 = std::min(d / 220.0f, 1.0f);
    const float wa = far01 * 115.0f, wb = std::min(d / 90.0f, 1.0f) * 30.0f;
    // (fbm stays near 0.5: stretched to about -1 .. 1.)
    auto wide = [](float v) { return std::clamp((v - 0.5f) * 3.4f, -1.0f, 1.0f); };
    const float xw = x + wide(fbm(x + 11.0f + sx, z - 7.0f + sz, 300.0f, 2)) * wa +
                     (valueNoise((x + sx) / 85.0f, (z - sz) / 85.0f) - 0.5f) * 2.0f * wb;
    const float zw = z + wide(fbm(x - 31.0f + sx, z + 53.0f + sz, 300.0f, 2)) * wa +
                     (valueNoise((x - sx) / 85.0f + 9.0f, (z + sz) / 85.0f + 4.0f) - 0.5f) * 2.0f * wb;
    float a = 0, b = 0;
    seedSample(mirrorIn(xw, mapW_, 24.0f), mirrorIn(zw, mapH_, 24.0f), a, b);
    const float level = b / std::max(a, 1e-3f);
    const float reach = 280.0f + 190.0f * wide(fbm(x + sx * 3.0f, z + sz * 3.0f, 260.0f, 3));
    const float erode = (wide(fbm(x + sx * 4.0f + 70.0f, z - sz * 4.0f, 120.0f, 3)) * 0.34f + 0.06f) * std::min(d / 110.0f, 1.0f);
    const float m1 = (a * (1.0f - 0.3f * far01) - erode) * (1.0f - ss((d - (reach - 70.0f)) / 70.0f));
    const float c1 = ss((m1 - 0.40f) / 0.2f);
    // (Height: a second, lower terrace along the eroded rim of tall rock; a slow swell of the top.)
    const float terrace = 0.62f + 0.38f * ss((m1 - 0.52f) / 0.1f);
    const float topVar = 1.0f + wide(fbm(x - sx + 300.0f, z + sz - 200.0f, 240.0f, 2)) * 0.22f * far01;
    const float h1 = level * c1 * (level > 14.0f ? terrace * topVar : 1.0f);
    // Free-standing mesas and small outcrops, from ~5 cells out.
    // (Held back near the map by a slow ramp, far gentler than the noise's own slopes: a mesa's outline is
    // the noise's, never a front parallel to the map's edge.)
    const float gate = 0.24f * (1.0f - ss((d + (nz - 0.5f) * 130.0f - 100.0f) / 280.0f));
    const float f2 = fbm(x + sx * 5.0f + 900.0f, z + sz * 5.0f - 400.0f, 270.0f, 3) - gate;
    const float c2 = ss((f2 - 0.6f) / 0.05f);
    const float h2 = c2 * (30.0f + 55.0f * valueNoise((x + sx) / 640.0f, (z + sz) / 640.0f) + std::max(0.0f, f2 - 0.625f) * 90.0f);
    const float f3 = fbm(x - 300.0f + sx * 7.0f, z + 700.0f + sz * 7.0f, 115.0f, 2) - gate;
    const float c3 = ss((f3 - 0.68f) / 0.035f);
    const float h3 = c3 * (8.0f + 16.0f * valueNoise((x - sx) / 210.0f, (z - sz) / 210.0f));
    f.rock = std::max({c1, c2, c3}) * T;
    f.hRock = std::max({h1, h2, h3}) * T;
    f.cliff = ss((f.hRock - 9.0f) / 10.0f);
    // Dunes across the wind (CinematicFx::kWind: east-north-east): a gentle windward side, a short lee
    // side, a crest line; a broad swell under them. Lower near the map (its own sand is level) and on rock.
    constexpr float kWx = 0.908f, kWz = -0.419f;
    // (Crest spacing ~230 px, bent and broken up by two noises; whole patches without dunes.)
    const float u = (x * kWx + z * kWz) / 230.0f + 2.3f * valueNoise((x + sx) / 430.0f, (z + sz) / 430.0f) +
                    0.55f * valueNoise((x - sx) / 140.0f, (z + sx) / 140.0f);
    const float r = u - std::floor(u);
    const float prof = r < 0.68f ? ss(r / 0.68f) : ss((1.0f - r) / 0.32f);
    const float amp = ss((fbm(x + sx + 50.0f, z + sz + 90.0f, 360.0f, 2) - 0.3f) / 0.4f);
    const float dune = ss((d - 40.0f) / 200.0f) * 20.0f * prof * amp;
    const float swell = (fbm(x + sx, z - sz, 700.0f, 2) - 0.35f) * 12.0f * ss(d / 250.0f);
    f.h = std::max(0.0f, dune + swell) * (1.0f - f.rock) * T + f.hRock;
    return f;
}

float Surround::gridH(const Terrain& t, int i, int j) const {
    const int v = vid(i, j);
    if (v >= 0) return verts_[std::size_t(v)].y;
    return t.height(float(i - m_) * kStep, float(j - m_) * kStep);
}

// The height of ring grid point (i, j): the mirrored map blended into the procedural desert, kept under
// the occlusion limit. Points on the map's edge are the map's own.
float Surround::blended(const Terrain& t, int i, int j) const {
    const float x = float(i - m_) * kStep, z = float(j - m_) * kStep;
    const float cx = std::clamp(x, 0.0f, mapW_), cz = std::clamp(z, 0.0f, mapH_);
    if (cx == x && cz == z) return t.vertices()[std::size_t((j - m_) * 2) * std::size_t(t.gridW()) + std::size_t((i - m_) * 2)].y;
    const float w = float((tex_[std::size_t(j) * ew_ + i] >> 16) & 0xFF) / 255.0f;
    float h = hFar_[std::size_t(vid(i, j))];
    if (w < 1.0f) {
        const float mx = x < 0 ? std::min(-x, mapW_) : x > mapW_ ? std::max(2 * mapW_ - x, 0.0f) : x;
        const float mz = z < 0 ? std::min(-z, mapH_) : z > mapH_ ? std::max(2 * mapH_ - z, 0.0f) : z;
        h = t.height(mx, mz) * (1.0f - w) + h * w;
    }
    return std::min(h, t.height(cx, cz) + kOcclusionSlope * std::hypot(x - cx, z - cz));
}

void Surround::build(const Terrain& t) {
    clear();
    if (t.sub() != 8 || !t.reimagined() || t.cellsW() < 8 || t.cellsH() < 8) return;
    cw_ = t.cellsW();
    ch_ = t.cellsH();
    mapW_ = float(cw_) * 32.0f;
    mapH_ = float(ch_) * 32.0f;
    m_ = kMarginCells * 4;
    w4_ = cw_ * 4;
    h4_ = ch_ * 4;
    ew_ = w4_ + 2 * m_ + 1;
    eh_ = h4_ + 2 * m_ + 1;
    sun_ = t.sun();

    // The map's hash (cell classes + levels): where the noise is read. The edge zone's rock, per cell.
    const auto& kinds = t.cellKinds();
    const auto& levels = t.cellLevels();
    std::uint32_t hsh = 2166136261u ^ std::uint32_t(cw_ * 131 + ch_);
    const std::size_t n = std::size_t(cw_) * ch_;
    seedA_.assign(n, 0.0f);
    seedB_.assign(n, 0.0f);
    for (std::size_t c = 0; c < n; ++c) {
        const int lv = c < levels.size() ? int(std::lround(levels[c] * 16.0f)) : 0;
        hsh = (hsh ^ std::uint32_t(kinds[c] + 4 * lv)) * 16777619u;
        if (kinds[c] != Terrain::kSandCell) {
            seedA_[c] = 1.0f;
            seedB_[c] = t.height(float(c % std::size_t(cw_)) * 32.0f + 16.0f, float(c / std::size_t(cw_)) * 32.0f + 16.0f);
        }
    }
    seedX_ = float(hsh & 0x3FF) * 37.0f;
    seedZ_ = float((hsh >> 10) & 0x3FF) * 41.0f;
    {  // two 3 x 3 binomial passes
        std::vector<float> tmp(n);
        for (std::vector<float>* f : {&seedA_, &seedB_})
            for (int pass = 0; pass < 2; ++pass) {
                for (int y = 0; y < ch_; ++y)
                    for (int x = 0; x < cw_; ++x) {
                        float s = 0;
                        for (int j = -1; j <= 1; ++j)
                            for (int i = -1; i <= 1; ++i) {
                                const int xx = std::clamp(x + i, 0, cw_ - 1), yy = std::clamp(y + j, 0, ch_ - 1);
                                s += (*f)[std::size_t(yy) * cw_ + xx] * float((2 - std::abs(i)) * (2 - std::abs(j)));
                            }
                        tmp[std::size_t(y) * cw_ + x] = s / 16.0f;
                    }
                f->swap(tmp);
            }
    }

    // Grid vertices: the procedural field and the far texture, then the heights.
    gridVerts_ = std::uint32_t(vid(ew_ - 1, eh_ - 1) + 1);
    midFirst_ = gridVerts_;
    const std::uint32_t mids = std::uint32_t(2 * (w4_ + h4_));
    verts_.assign(std::size_t(gridVerts_) + mids + 8, TerrainVertex{});
    hFar_.assign(gridVerts_, 0.0f);
    tex_.assign(std::size_t(ew_) * eh_, 0u);
    for (int j = 0; j < eh_; ++j)
        for (int i = 0; i < ew_; ++i) {
            const int v = vid(i, j);
            if (v < 0) continue;
            const float x = float(i - m_) * kStep, z = float(j - m_) * kStep;
            TerrainVertex& tv = verts_[std::size_t(v)];
            tv.x = x;
            tv.z = z;
            tv.ny = 1;
            tv.sun = 1;
            const bool onEdge = x >= 0 && z >= 0 && x <= mapW_ && z <= mapH_;
            if (onEdge) continue;  // (texel 0: no far part)
            Far f = farAt(x, z);
            if (i == 0 || j == 0 || i == ew_ - 1 || j == eh_ - 1) f = Far{};  // the apron reads these texels
            hFar_[std::size_t(v)] = f.h;
            tex_[std::size_t(j) * ew_ + i] = pack(f.rock, guideOf(f.hRock), f.w, f.cliff);
        }
    // The apron: a flat ring far out, tucked half a px under the ring's flat outer rows.
    {
        const float in = margin() - 2 * kStep, out = margin() + kApron;
        const float xs[8] = {-in, mapW_ + in, mapW_ + in, -in, -out, mapW_ + out, mapW_ + out, -out};
        const float zs[8] = {-in, -in, mapH_ + in, mapH_ + in, -out, -out, mapH_ + out, mapH_ + out};
        for (int k = 0; k < 8; ++k) verts_[std::size_t(gridVerts_) + mids + std::size_t(k)] = TerrainVertex{xs[k], -0.5f, zs[k], 0, 1, 0, 1};
    }
    refresh(t);
    normals(t, true);
    buildIndices(t);
}

bool Surround::refresh(const Terrain& t) {
    if (!valid() || t.cellsW() != cw_ || t.cellsH() != ch_ || t.sub() != 8) return false;
    bool changed = false;
    float top = 0;
    for (int j = 0; j < eh_; ++j)
        for (int i = 0; i < ew_; ++i) {
            const int v = vid(i, j);
            if (v < 0) {  // (strictly inside the map: skip the row's middle)
                i = m_ + w4_ - 1;
                continue;
            }
            TerrainVertex& tv = verts_[std::size_t(v)];
            const float h = blended(t, i, j);
            if (h != tv.y) {
                tv.y = h;
                changed = true;
            }
            top = std::max(top, h);
        }
    // The edge's 4 px vertices: the map mesh's own.
    const auto& mv = t.vertices();
    const int gw = t.gridW(), gh = t.gridH();
    auto mid = [&](std::uint32_t id, int gx, int gy) {
        const TerrainVertex& s = mv[std::size_t(gy) * gw + gx];
        TerrainVertex& tv = verts_[std::size_t(midFirst_) + id];
        if (tv.y != s.y || tv.x != s.x || tv.z != s.z) changed = true;
        tv = s;
    };
    for (int k = 0; k < w4_; ++k) {
        mid(std::uint32_t(k), 2 * k + 1, 0);
        mid(std::uint32_t(w4_ + k), 2 * k + 1, gh - 1);
    }
    for (int k = 0; k < h4_; ++k) {
        mid(std::uint32_t(2 * w4_ + k), 0, 2 * k + 1);
        mid(std::uint32_t(2 * w4_ + h4_ + k), gw - 1, 2 * k + 1);
    }
    top_ = top;
    if (changed) normals(t, false);
    return changed;
}

// Normals of the ring (central differences; the map's own on its edge), and with `sun` the baked sun
// visibility (the soft heightfield march of Terrain::computeNormalsAndSun over the ring and the map).
void Surround::normals(const Terrain& t, bool sun) {
    const auto& mv = t.vertices();
    const float horiz = std::max(1e-4f, std::sqrt(sun_.x * sun_.x + sun_.z * sun_.z));
    const float sdx = sun_.x / horiz, sdz = sun_.z / horiz, slope = sun_.y / horiz;
    auto ground = [&](float x, float z) {
        return x >= 0 && z >= 0 && x <= mapW_ && z <= mapH_ ? t.height(x, z) : height(x, z);
    };
    for (int j = 0; j < eh_; ++j)
        for (int i = 0; i < ew_; ++i) {
            const int v = vid(i, j);
            if (v < 0) {
                i = m_ + w4_ - 1;
                continue;
            }
            TerrainVertex& tv = verts_[std::size_t(v)];
            const bool onEdge = tv.x >= 0 && tv.z >= 0 && tv.x <= mapW_ && tv.z <= mapH_;
            if (onEdge) {
                const TerrainVertex& s = mv[std::size_t((j - m_) * 2) * std::size_t(t.gridW()) + std::size_t((i - m_) * 2)];
                tv.nx = s.nx;
                tv.ny = s.ny;
                tv.nz = s.nz;
                tv.sun = s.sun;
                continue;
            }
            const int i0 = std::max(i - 1, 0), i1 = std::min(i + 1, ew_ - 1), j0 = std::max(j - 1, 0), j1 = std::min(j + 1, eh_ - 1);
            const float dhx = (gridH(t, i1, j) - gridH(t, i0, j)) / (float(i1 - i0) * kStep);
            const float dhz = (gridH(t, i, j1) - gridH(t, i, j0)) / (float(j1 - j0) * kStep);
            const Vec3 nrm = normalize(Vec3{-dhx, 1, -dhz});
            tv.nx = nrm.x;
            tv.ny = nrm.y;
            tv.nz = nrm.z;
            if (!sun) continue;
            float vis = 1;
            for (int s = 1; s <= 20 && vis > 0; ++s) {
                const float tt = float(s) * 12.0f;
                const float th = ground(tv.x + sdx * tt, tv.z + sdz * tt);
                const float ray = tv.y + 0.5f + tt * slope;
                const float soft = 1.5f + 0.06f * tt;
                vis = std::min(vis, std::clamp(0.5f + (ray - th) / (2 * soft), 0.0f, 1.0f));
            }
            tv.sun = vis;
        }
}

void Surround::buildIndices(const Terrain& t) {
    idx_.clear();
    auto V = [&](int i, int j) { return std::uint32_t(vid(i, j)); };
    auto quad = [&](std::vector<std::uint32_t>& out, int i, int j) {
        const std::uint32_t a = V(i, j), b = V(i + 1, j), c = V(i, j + 1), d = V(i + 1, j + 1);
        if ((i + j) & 1) out.insert(out.end(), {a, c, b, b, c, d});
        else out.insert(out.end(), {a, c, d, a, d, b});
    };
    // The row of quads along the map's edge: (grid quad, the map quad behind its inner side, that side's
    // two vertices p / q, the outer two o1 (beyond p) / o2 (beyond q), the side's 4 px vertex).
    struct EdgeQuad {
        int i, j;
        bool fine;
        std::uint32_t p, q, o1, o2, mid;
    };
    std::vector<EdgeQuad> edge;
    edge.reserve(std::size_t(2 * (w4_ + h4_)));
    for (int k = 0; k < w4_; ++k) {
        const int i = m_ + k;
        edge.push_back({i, m_ - 1, t.quadFine(k, 0), V(i, m_), V(i + 1, m_), V(i, m_ - 1), V(i + 1, m_ - 1), midFirst_ + std::uint32_t(k)});
        edge.push_back({i, m_ + h4_, t.quadFine(k, h4_ - 1), V(i, m_ + h4_), V(i + 1, m_ + h4_), V(i, m_ + h4_ + 1),
                        V(i + 1, m_ + h4_ + 1), midFirst_ + std::uint32_t(w4_ + k)});
    }
    for (int k = 0; k < h4_; ++k) {
        const int j = m_ + k;
        edge.push_back({m_ - 1, j, t.quadFine(0, k), V(m_, j), V(m_, j + 1), V(m_ - 1, j), V(m_ - 1, j + 1),
                        midFirst_ + std::uint32_t(2 * w4_ + k)});
        edge.push_back({m_ + w4_, j, t.quadFine(w4_ - 1, k), V(m_ + w4_, j), V(m_ + w4_, j + 1), V(m_ + w4_ + 1, j),
                        V(m_ + w4_ + 1, j + 1), midFirst_ + std::uint32_t(2 * w4_ + h4_ + k)});
    }
    auto isEdgeQuad = [&](int i, int j) {
        const bool inX = i >= m_ && i < m_ + w4_, inZ = j >= m_ && j < m_ + h4_;
        return (inX && (j == m_ - 1 || j == m_ + h4_)) || (inZ && (i == m_ - 1 || i == m_ + w4_));
    };
    // Near: the inner side split at its 4 px vertex where the map draws that quad fine.
    for (const EdgeQuad& e : edge) {
        if (e.fine) idx_.insert(idx_.end(), {e.p, e.mid, e.o1, e.mid, e.o2, e.o1, e.mid, e.q, e.o2});
        else quad(idx_, e.i, e.j);
    }
    nearEdge_ = std::uint32_t(idx_.size());
    for (int j = 0; j + 1 < eh_; ++j)
        for (int i = 0; i + 1 < ew_; ++i) {
            if (i >= m_ && i < m_ + w4_ && j >= m_ && j < m_ + h4_) {  // the map itself
                i = m_ + w4_ - 1;
                continue;
            }
            if (!isEdgeQuad(i, j)) quad(idx_, i, j);
        }
    {  // the apron: four trapezoids (inner corners 0..3, outer 4..7, clockwise from the north-west)
        const std::uint32_t a = gridVerts_ + std::uint32_t(2 * (w4_ + h4_));
        for (std::uint32_t k = 0; k < 4; ++k) {
            const std::uint32_t k1 = (k + 1) & 3;
            idx_.insert(idx_.end(), {a + k, a + 4 + k, a + k1, a + k1, a + 4 + k, a + 4 + k1});
        }
    }
    farEdge_ = std::uint32_t(idx_.size());
    for (const EdgeQuad& e : edge) quad(idx_, e.i, e.j);
    indexCount_ = std::uint32_t(idx_.size());
}

float Surround::rockAt(const Terrain& t, float wx, float wz) const {
    if (!valid()) return 0;
    const int i = std::clamp(int(std::lround(wx / kStep)) + m_, 0, ew_ - 1), j = std::clamp(int(std::lround(wz / kStep)) + m_, 0, eh_ - 1);
    const std::uint32_t px = tex_[std::size_t(j) * ew_ + i];
    const float far = float(px & 0xFF) / 255.0f, w = float((px >> 16) & 0xFF) / 255.0f;
    if (w >= 1.0f) return far;
    const float mx = wx < 0 ? std::min(-wx, mapW_) : wx > mapW_ ? std::max(2 * mapW_ - wx, 0.0f) : wx;
    const float mz = wz < 0 ? std::min(-wz, mapH_) : wz > mapH_ ? std::max(2 * mapH_ - wz, 0.0f) : wz;
    const int cx = std::clamp(int(mx / 32.0f), 0, cw_ - 1), cy = std::clamp(int(mz / 32.0f), 0, ch_ - 1);
    const auto& kinds = t.cellKinds();
    const std::size_t c = std::size_t(cy) * cw_ + cx;
    const float near = c < kinds.size() && kinds[c] != Terrain::kSandCell ? 1.0f : 0.0f;
    return near * (1.0f - w) + far * w;
}

float Surround::height(float wx, float wz) const {
    if (!valid()) return 0;
    const float u = wx / kStep + float(m_), v = wz / kStep + float(m_);
    if (u < 0 || v < 0 || u > float(ew_ - 1) || v > float(eh_ - 1)) return 0;
    const int i = std::min(int(u), ew_ - 2), j = std::min(int(v), eh_ - 2);
    const int a = vid(i, j), b = vid(i + 1, j), c = vid(i, j + 1), d = vid(i + 1, j + 1);
    if (a < 0 || b < 0 || c < 0 || d < 0) return 0;  // inside the map
    const float fx = u - float(i), fy = v - float(j);
    return (verts_[std::size_t(a)].y * (1 - fx) + verts_[std::size_t(b)].y * fx) * (1 - fy) +
           (verts_[std::size_t(c)].y * (1 - fx) + verts_[std::size_t(d)].y * fx) * fy;
}

bool Surround::raycast(const Vec3& o, const Vec3& d, Vec3& hit) const {
    if (!valid() || d.y >= -1e-5f) return false;
    float t = o.y > top_ ? (o.y - top_) / -d.y : 0.0f;
    const float tEnd = o.y / -d.y;
    auto above = [&](float tt) {
        const Vec3 p = o + d * tt;
        return p.y > height(p.x, p.z);
    };
    float prev = t;
    for (; t <= tEnd; t += 4.0f) {
        if (!above(t)) {
            float lo = prev, hi = t;
            for (int i = 0; i < 10; ++i) {
                const float mid = 0.5f * (lo + hi);
                if (above(mid)) lo = mid;
                else hi = mid;
            }
            hit = o + d * hi;
            return true;
        }
        prev = t;
    }
    hit = o + d * tEnd;
    return true;
}

}  // namespace d2k::render3d

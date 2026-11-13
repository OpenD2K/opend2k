#include "render3d/terrain.hpp"

#include "game/world.hpp"
#include "render3d/terrain_guide.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <iterator>

namespace d2k::render3d {

namespace {

float smoothstep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// Value noise for the designed terrain's relief (deterministic, render-only).
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
// 0..1, `oct` octaves, first one with period `period` world px.
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

}  // namespace

Vec3 Terrain::sunDirection() {
    // Light from the north-west, 40 degrees above the horizon (the original art is lit from the
    // upper left).
    const float e = radians(40.0f);
    return normalize(Vec3{-std::cos(e) * 0.7071f, std::sin(e), -std::cos(e) * 0.7071f});
}

void Terrain::build(const game::World& w, const std::vector<float>& tileRgb, bool designed, const TerrainGuide* guide) {
    namespace cf = game::cell_flags;
    designed_ = designed;
    faithful_ = designed && guide && guide->valid();
    guide_ = faithful_ ? guide : nullptr;
    sub_ = designed ? 4 : 2;
    step_ = 32.0f / float(sub_);
    cw_ = w.width();
    ch_ = w.height();
    const int n = cw_ * ch_;
    kind_.assign(std::size_t(n), kSandCell);
    auto& kind = kind_;
    level_.assign(std::size_t(n), 0.0f);
    dune_.assign(std::size_t(n), 0.0f);
    rough_.assign(std::size_t(n), 0.0f);
    spice_.assign(std::size_t(n), 0);
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            const game::Cell& c = w.cell(x, y);
            const std::size_t i = std::size_t(y) * cw_ + x;
            const bool passable = (c.flags & (cf::kDriveOn | cf::kWalkOn)) != 0;
            const bool sand = (c.flags & cf::kSand) != 0;
            // Buildings sit on rock (their cells lose the pass bits): rock, not cliff.
            const bool built = (c.flags & (cf::kBuilding | cf::kConcrete | cf::kWall)) != 0;
            if (sand) kind[i] = kSandCell;
            else if (passable || built || (c.flags & cf::kBuildOn)) kind[i] = kRockCell;
            else kind[i] = kCliffCell;
            if (c.terrain() == 2) dune_[i] = 1.0f;
            if (c.terrain() == 3 || c.terrain() == 5) rough_[i] = 1.0f;
            spice_[i] = std::uint8_t(c.spice());
        }
    // Sand-looking impassable cells: nearer to the mean sand colour than to the mean rock colour.
    if (!tileRgb.empty()) {
        double sum[2][3] = {}, cnt[2] = {};
        auto rgbOf = [&](int x, int y) -> const float* {
            const std::size_t t = std::size_t(w.cell(x, y).tile & 0x0FFF) * 3;
            return t + 2 < tileRgb.size() ? &tileRgb[t] : nullptr;
        };
        for (int y = 0; y < ch_; ++y)
            for (int x = 0; x < cw_; ++x) {
                const std::uint8_t k = kind[std::size_t(y) * cw_ + x];
                const float* c = rgbOf(x, y);
                if (!c || k == kCliffCell || w.cell(x, y).spice() > 0) continue;
                for (int i = 0; i < 3; ++i) sum[k][i] += c[i];
                cnt[k] += 1;
            }
        if (cnt[0] > 0 && cnt[1] > 0)
            for (int y = 0; y < ch_; ++y)
                for (int x = 0; x < cw_; ++x) {
                    std::uint8_t& k = kind[std::size_t(y) * cw_ + x];
                    const float* c = rgbOf(x, y);
                    if (k != kCliffCell || !c) continue;
                    double d[2] = {};
                    for (int s = 0; s < 2; ++s)
                        for (int i = 0; i < 3; ++i) {
                            const double e = c[i] - sum[s][i] / cnt[s];
                            d[s] += e * e;
                        }
                    if (d[0] * 2.5 < d[1]) k = kSandCell;
                }
    }
    auto at = [&](int x, int y) { return std::size_t(std::clamp(y, 0, ch_ - 1)) * cw_ + std::size_t(std::clamp(x, 0, cw_ - 1)); };
    const int dx4[4] = {1, -1, 0, 0}, dy4[4] = {0, 0, 1, -1};

    // Rock: a ramp from passable sand borders (3 cells), else a full plateau.
    {
        constexpr int kRamp = 3;
        std::vector<int> dist(std::size_t(n), 1 << 20);
        std::deque<int> q;
        for (int y = 0; y < ch_; ++y)
            for (int x = 0; x < cw_; ++x) {
                const std::size_t i = std::size_t(y) * cw_ + x;
                if (kind[i] != kRockCell) continue;
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx4[k], ny = y + dy4[k];
                    if (nx < 0 || ny < 0 || nx >= cw_ || ny >= ch_) continue;
                    if (kind[std::size_t(ny) * cw_ + nx] == kSandCell) {
                        dist[i] = 1;
                        q.push_back(int(i));
                        break;
                    }
                }
            }
        while (!q.empty()) {
            const int i = q.front();
            q.pop_front();
            const int x = i % cw_, y = i / cw_;
            for (int k = 0; k < 4; ++k) {
                const int nx = x + dx4[k], ny = y + dy4[k];
                if (nx < 0 || ny < 0 || nx >= cw_ || ny >= ch_) continue;
                const std::size_t j = std::size_t(ny) * cw_ + nx;
                if (kind[j] != kRockCell || dist[j] <= dist[i] + 1) continue;
                dist[j] = dist[i] + 1;
                if (dist[j] <= kRamp + 1) q.push_back(int(j));
            }
        }
        for (int i = 0; i < n; ++i)
            if (kind[std::size_t(i)] == kRockCell)
                level_[std::size_t(i)] = 0.2f + 0.8f * smoothstep01((float(dist[std::size_t(i)]) - 0.5f) / kRamp);
    }
    // Cliffs: relax between the fixed neighbours (Gauss-Seidel), so a band becomes a slope.
    {
        for (int i = 0; i < n; ++i)
            if (kind[std::size_t(i)] == kCliffCell) level_[std::size_t(i)] = 0.5f;
        for (int it = 0; it < 80; ++it)
            for (int y = 0; y < ch_; ++y)
                for (int x = 0; x < cw_; ++x) {
                    const std::size_t i = std::size_t(y) * cw_ + x;
                    if (kind[i] != kCliffCell) continue;
                    level_[i] = 0.25f * (level_[at(x - 1, y)] + level_[at(x + 1, y)] + level_[at(x, y - 1)] + level_[at(x, y + 1)]);
                }
        // Outcrops: impassable cells far from any passable cell rise above the plateau.
        std::vector<int> dist(std::size_t(n), 1 << 20);
        std::deque<int> q;
        for (int i = 0; i < n; ++i)
            if (kind[std::size_t(i)] != kCliffCell) { dist[std::size_t(i)] = 0; q.push_back(i); }
        while (!q.empty()) {
            const int i = q.front();
            q.pop_front();
            const int x = i % cw_, y = i / cw_;
            for (int k = 0; k < 4; ++k) {
                const int nx = x + dx4[k], ny = y + dy4[k];
                if (nx < 0 || ny < 0 || nx >= cw_ || ny >= ch_) continue;
                const std::size_t j = std::size_t(ny) * cw_ + nx;
                if (dist[j] <= dist[std::size_t(i)] + 1) continue;
                dist[j] = dist[std::size_t(i)] + 1;
                q.push_back(int(j));
            }
        }
        for (int i = 0; i < n; ++i)
            if (kind[std::size_t(i)] == kCliffCell) {
                // Designed: every impassable rock band is a raised ridge / plateau rim (its faces
                // get modelled rock, render3d/terrain_v2.cpp), not a slope down to the sand.
                if (designed) level_[std::size_t(i)] = std::max(level_[std::size_t(i)], 0.95f);
                if (dist[std::size_t(i)] >= 2) level_[std::size_t(i)] += 0.35f * std::min(dist[std::size_t(i)] - 1, 3);
            }
    }
    // Dunes: a soft weight (two 3x3 box passes) so the waves fade out at their edges.
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<float> t(static_cast<std::size_t>(n));
        for (int y = 0; y < ch_; ++y)
            for (int x = 0; x < cw_; ++x) {
                float s = 0;
                for (int j = -1; j <= 1; ++j)
                    for (int i = -1; i <= 1; ++i) s += dune_[at(x + i, y + j)];
                t[std::size_t(y) * cw_ + x] = s / 9.0f;
            }
        dune_.swap(t);
    }

    gw_ = cw_ * sub_ + 1;
    gh_ = ch_ * sub_ + 1;
    h_.assign(std::size_t(gw_) * gh_, 0.0f);
    verts_.assign(std::size_t(gw_) * gh_, TerrainVertex{});
    idx_.clear();
    idx_.reserve(std::size_t(gw_ - 1) * (gh_ - 1) * 6);
    for (int y = 0; y + 1 < gh_; ++y)
        for (int x = 0; x + 1 < gw_; ++x) {
            const std::uint32_t a = std::uint32_t(y * gw_ + x), b = a + 1, c = a + std::uint32_t(gw_), d = c + 1;
            // Alternate the diagonal so slopes do not get a directional sawtooth.
            if ((x + y) & 1) {
                idx_.insert(idx_.end(), {a, c, b, b, c, d});
            } else {
                idx_.insert(idx_.end(), {a, c, d, a, d, b});
            }
        }
    for (int gy = 0; gy < gh_; ++gy)
        for (int gx = 0; gx < gw_; ++gx) {
            TerrainVertex& v = verts_[std::size_t(gy) * gw_ + gx];
            v.x = float(gx) * step_;
            v.z = float(gy) * step_;
        }
    hNat_.assign(std::size_t(gw_) * gh_, 0.0f);
    found_.clear();
    foundCell_.assign(std::size_t(n), -1);
    computeHeights(0, 0, gw_ - 1, gh_ - 1);
    int fx0, fy0, fx1, fy1;
    if (setFoundations(w, fx0, fy0, fx1, fy1)) applyFoundations(0, 0, gw_ - 1, gh_ - 1);
    computeNormalsAndSun(0, 0, gw_ - 1, gh_ - 1);
    guide_ = nullptr;
}

// The buildings' footprints now; true (with the cell rectangle that changed) when they differ
// from the current foundations, which are replaced (levelled heights from the natural ones).
bool Terrain::setFoundations(const game::World& w, int& x0, int& y0, int& x1, int& y1) {
    std::vector<Foundation> want;
    for (const auto& side : w.sides())
        for (int bi = side.firstBuilding; bi >= 0 && foundationsOn_; bi = side.nextObject(bi)) {
            const game::Building& b = side.buildings[std::size_t(bi)];
            if (!b.alive) continue;
            const auto mask = std::uint16_t(w.buildingTemplate(b).tilesOccupiedAll & 0xFFFFu);
            if (mask) want.push_back(Foundation{b.tileX, b.tileY, mask, 0});
        }
    auto less = [](const Foundation& a, const Foundation& b) {
        return a.y != b.y ? a.y < b.y : a.x != b.x ? a.x < b.x : a.mask < b.mask;
    };
    std::sort(want.begin(), want.end(), less);
    if (want == found_) return false;
    // The rectangle of every footprint that appeared or went away.
    std::vector<Foundation> diff;
    std::set_symmetric_difference(found_.begin(), found_.end(), want.begin(), want.end(), std::back_inserter(diff), less);
    x0 = cw_, y0 = ch_, x1 = -1, y1 = -1;
    for (const Foundation& f : diff)
        for (int k = 0; k < 16; ++k)
            if (f.mask & (1u << k)) {
                x0 = std::min(x0, f.x + k % 4);
                y0 = std::min(y0, f.y + k / 4);
                x1 = std::max(x1, f.x + k % 4);
                y1 = std::max(y1, f.y + k / 4);
            }
    // Levelled height: the mean natural height over the footprint's vertices.
    auto H = [&](int gx, int gy) {
        return hNat_[std::size_t(std::clamp(gy, 0, gh_ - 1)) * gw_ + std::size_t(std::clamp(gx, 0, gw_ - 1))];
    };
    std::fill(foundCell_.begin(), foundCell_.end(), -1);
    for (std::size_t i = 0; i < want.size(); ++i) {
        Foundation& f = want[i];
        double sum = 0;
        int cnt = 0;
        for (int k = 0; k < 16; ++k) {
            if (!(f.mask & (1u << k))) continue;
            const int cx = f.x + k % 4, cy = f.y + k / 4;
            if (cx < 0 || cy < 0 || cx >= cw_ || cy >= ch_) continue;
            foundCell_[std::size_t(cy) * cw_ + cx] = int(i);
            for (int gy = cy * sub_; gy <= (cy + 1) * sub_; ++gy)
                for (int gx = cx * sub_; gx <= (cx + 1) * sub_; ++gx) sum += H(gx, gy), ++cnt;
        }
        f.h = cnt ? float(sum / cnt) : 0.0f;
    }
    found_ = std::move(want);
    return x1 >= 0;
}

bool Terrain::updateFoundations(const game::World& w) {
    if (w.width() != cw_ || w.height() != ch_ || hNat_.empty()) return false;
    int x0, y0, x1, y1;
    if (!setFoundations(w, x0, y0, x1, y1)) return false;
    const int s = int(std::ceil(kFoundationSkirt / step_)) + 1;
    const int hx0 = std::max(0, x0 * sub_ - s), hy0 = std::max(0, y0 * sub_ - s);
    const int hx1 = std::min(gw_ - 1, (x1 + 1) * sub_ + s), hy1 = std::min(gh_ - 1, (y1 + 1) * sub_ + s);
    applyFoundations(hx0, hy0, hx1, hy1);
    // Normals one vertex further; the sun visibility down-sun as far as the shadow march reaches.
    const int reach = int(std::ceil(40 * 0.75f)) + 2;
    computeNormalsAndSun(std::max(0, hx0 - reach), std::max(0, hy0 - reach), std::min(gw_ - 1, hx1 + reach),
                         std::min(gh_ - 1, hy1 + reach));
    return true;
}

float Terrain::foundationAt(int cx, int cy, float fallback) const {
    if (cx < 0 || cy < 0 || cx >= cw_ || cy >= ch_ || foundCell_.empty()) return fallback;
    const int i = foundCell_[std::size_t(cy) * cw_ + cx];
    return i >= 0 ? found_[std::size_t(i)].h : fallback;
}

void Terrain::applyFoundations(int gx0, int gy0, int gx1, int gy1) {
    const float S = kFoundationSkirt;
    for (int gy = gy0; gy <= gy1; ++gy)
        for (int gx = gx0; gx <= gx1; ++gx) {
            const std::size_t vi = std::size_t(gy) * gw_ + gx;
            float h = hNat_[vi];
            if (!found_.empty()) {
                const float wx = float(gx) * step_, wy = float(gy) * step_;
                // Nearest footprint cells within the skirt: per foundation its strongest weight.
                int ids[9];
                float ws[9];
                int n = 0;
                const int cxa = int(std::floor((wx - S) / 32)), cxb = int(std::floor((wx + S) / 32));
                const int cya = int(std::floor((wy - S) / 32)), cyb = int(std::floor((wy + S) / 32));
                for (int cy = std::max(0, cya); cy <= std::min(ch_ - 1, cyb); ++cy)
                    for (int cx = std::max(0, cxa); cx <= std::min(cw_ - 1, cxb); ++cx) {
                        const int fi = foundCell_[std::size_t(cy) * cw_ + cx];
                        if (fi < 0) continue;
                        const float dx = std::max({float(cx * 32) - wx, 0.0f, wx - float(cx * 32 + 32)});
                        const float dy = std::max({float(cy * 32) - wy, 0.0f, wy - float(cy * 32 + 32)});
                        const float d = std::sqrt(dx * dx + dy * dy);
                        if (d >= S) continue;
                        const float wgt = 1.0f - smoothstep01(d / S);
                        int k = 0;
                        while (k < n && ids[k] != fi) ++k;
                        if (k == n && n < 9) ids[n] = fi, ws[n++] = wgt;
                        else if (k < n) ws[k] = std::max(ws[k], wgt);
                    }
                if (n > 0) {
                    float wsum = 0, hsum = 0, wmax = 0;
                    for (int k = 0; k < n; ++k) {
                        wsum += ws[k];
                        hsum += ws[k] * found_[std::size_t(ids[k])].h;
                        wmax = std::max(wmax, ws[k]);
                    }
                    if (wsum > 0) h += (hsum / wsum - h) * wmax;
                }
            }
            h_[vi] = h;
            verts_[vi].y = h;
        }
}

bool Terrain::refreshSpice(const game::World& w) {
    if (w.width() != cw_ || w.height() != ch_) return false;
    int x0 = cw_, y0 = ch_, x1 = -1, y1 = -1;
    for (int y = 0; y < ch_; ++y)
        for (int x = 0; x < cw_; ++x) {
            const std::uint8_t s = std::uint8_t(w.cell(x, y).spice());
            std::uint8_t& old = spice_[std::size_t(y) * cw_ + x];
            if (s != old) {
                old = s;
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
        }
    if (x1 < 0) return false;
    // Designed: spice is a material (render3d/terrain_v2.*), the ground does not move.
    if (designed_) return false;
    // Heights follow the bilinear cell field (one cell around a change); the normals one vertex
    // further; the sun visibility down-sun as far as the shadow march reaches (it looks towards
    // the sun, the north-west, so a change shades vertices to its south-east).
    const int hx0 = std::max(0, (x0 - 1) * sub_), hy0 = std::max(0, (y0 - 1) * sub_);
    const int hx1 = std::min(gw_ - 1, (x1 + 2) * sub_), hy1 = std::min(gh_ - 1, (y1 + 2) * sub_);
    computeHeights(hx0, hy0, hx1, hy1);
    const int reach = int(std::ceil(40 * 0.75f)) + 2;  // march steps (computeNormalsAndSun) in vertices
    computeNormalsAndSun(std::max(0, hx0 - 1), std::max(0, hy0 - 1), std::min(gw_ - 1, hx1 + reach),
                         std::min(gh_ - 1, hy1 + reach));
    return true;
}

void Terrain::computeHeights(int gx0, int gy0, int gx1, int gy1) {
    // Cell-centred fields sampled bilinearly at each vertex.
    auto field = [&](auto get, float wx, float wy) {
        const float u = wx / 32.0f - 0.5f, v = wy / 32.0f - 0.5f;
        const int x0 = int(std::floor(u)), y0 = int(std::floor(v));
        const float fx = u - float(x0), fy = v - float(y0);
        auto g = [&](int x, int y) { return get(std::size_t(std::clamp(y, 0, ch_ - 1)) * cw_ + std::size_t(std::clamp(x, 0, cw_ - 1))); };
        return (g(x0, y0) * (1 - fx) + g(x0 + 1, y0) * fx) * (1 - fy) + (g(x0, y0 + 1) * (1 - fx) + g(x0 + 1, y0 + 1) * fx) * fy;
    };
    for (int gy = gy0; gy <= gy1; ++gy)
        for (int gx = gx0; gx <= gx1; ++gx) {
            const float wx = float(gx) * step_, wy = float(gy) * step_;
            const float lvl = field([&](std::size_t i) { return level_[i]; }, wx, wy);
            const float dune = field([&](std::size_t i) { return dune_[i]; }, wx, wy);
            float h;
            if (!designed_) {
                const float spice = field([&](std::size_t i) { return float(spice_[i]); }, wx, wy);
                // Rolling dunes: two crossing low-frequency waves (crest ~5 tiles apart).
                const float wave = 0.5f + 0.5f * std::sin((wx * 0.8f + wy * 0.6f) * (2 * kPi / 170.0f) +
                                                          1.3f * std::sin((wx * -0.5f + wy * 0.85f) * (2 * kPi / 260.0f)));
                h = lvl * kRockHeight + dune * wave * kDuneHeight + spice * kSpiceHeight;
            } else {
                const float rock = field([&](std::size_t i) { return kind_[i] == kRockCell ? 1.0f : 0.0f; }, wx, wy);
                const float cliff = field([&](std::size_t i) { return kind_[i] == kCliffCell ? 1.0f : 0.0f; }, wx, wy);
                const float sand = std::max(0.0f, 1.0f - rock - cliff);
                // Dunes: curved, asymmetric ridges (long windward rise, short steep lee side),
                // crests ~4.5 tiles apart, their line bent by low-frequency noise.
                const float warp = (fbm(wx, wy, 300.0f, 2) - 0.5f) * 2.2f;
                const float phase = (wx * 0.8f + wy * 0.6f) / 145.0f + warp;
                const float t = phase - std::floor(phase);
                const float ridge = t < 0.72f ? smoothstep01(t / 0.72f) : 1.0f - smoothstep01((t - 0.72f) / 0.28f);
                const float duneH = dune * (0.35f + 0.65f * ridge) * (0.7f + 0.6f * fbm(wx, wy, 180.0f, 2)) * 9.0f;
                // Open sand: a gentle swell; plateau tops: uneven; cliff bands: rugged.
                const float swell = (fbm(wx, wy, 230.0f, 3) - 0.5f) * 3.0f * sand * (1.0f - dune);
                const float top = rock * smoothstep01((lvl - 0.8f) / 0.2f);
                const float uneven = (fbm(wx + 500.0f, wy, 110.0f, 3) - 0.5f) * 3.5f * top;
                const float rugged = (fbm(wx, wy + 700.0f, 36.0f, 3) - 0.5f) * 9.0f * cliff;
                const float rw = field([&](std::size_t i) { return kind_[i] == kRockCell ? rough_[i] : 0.0f; }, wx, wy);
                const float craggy = (fbm(wx - 300.0f, wy, 26.0f, 3) - 0.3f) * 8.0f * rw;
                h = lvl * kRockHeight + duneH + swell + uneven + rugged + craggy;
                // Faithful: level ground (the designed sand relief stays), the cliffs' macro
                // relief from the guide; the fine relief is the shader's (terrain3_fs).
                if (guide_) h = guide_->heightAt(wx, wy) * kCliffHeight + duneH + swell;
            }
            hNat_[std::size_t(gy) * gw_ + gx] = h;
        }
    applyFoundations(gx0, gy0, gx1, gy1);
}

void Terrain::computeNormalsAndSun(int gx0, int gy0, int gx1, int gy1) {
    const Vec3 sun = sun_;
    const float horiz = std::sqrt(sun.x * sun.x + sun.z * sun.z);
    const float sdx = sun.x / horiz, sdz = sun.z / horiz, slope = sun.y / horiz;  // rise per px towards the sun
    auto H = [&](int x, int y) { return h_[std::size_t(std::clamp(y, 0, gh_ - 1)) * gw_ + std::size_t(std::clamp(x, 0, gw_ - 1))]; };
    for (int gy = gy0; gy <= gy1; ++gy)
        for (int gx = gx0; gx <= gx1; ++gx) {
            TerrainVertex& v = verts_[std::size_t(gy) * gw_ + gx];
            const float h = H(gx, gy);
            const float dhx = (H(gx + 1, gy) - H(gx - 1, gy)) / (2 * step_), dhz = (H(gx, gy + 1) - H(gx, gy - 1)) / (2 * step_);
            const Vec3 nrm = normalize(Vec3{-dhx, 1, -dhz});
            v.nx = nrm.x;
            v.ny = nrm.y;
            v.nz = nrm.z;
            // Soft heightfield shadow: march towards the sun; the penumbra widens with distance.
            float vis = 1;
            for (int s = 1; s <= 40 && vis > 0; ++s) {
                const float t = float(s) * step_ * 0.75f;
                const float th = height(v.x + sdx * t, v.z + sdz * t);
                const float ray = h + 0.5f + t * slope;
                const float soft = 1.5f + 0.06f * t;
                vis = std::min(vis, std::clamp(0.5f + (ray - th) / (2 * soft), 0.0f, 1.0f));
            }
            v.sun = vis;
        }
}

float Terrain::height(float wx, float wy) const {
    if (gw_ < 2 || gh_ < 2) return 0;
    const float u = wx / step_, v = wy / step_;
    if (u < 0 || v < 0 || u > float(gw_ - 1) || v > float(gh_ - 1)) return 0;
    const int x0 = std::min(int(u), gw_ - 2), y0 = std::min(int(v), gh_ - 2);
    const float fx = u - float(x0), fy = v - float(y0);
    const float* r0 = &h_[std::size_t(y0) * gw_ + x0];
    const float* r1 = r0 + gw_;
    return (r0[0] * (1 - fx) + r0[1] * fx) * (1 - fy) + (r1[0] * (1 - fx) + r1[1] * fx) * fy;
}

float Terrain::maxHeight(int cx0, int cy0, int cx1, int cy1) const {
    float m = 0;
    for (int gy = std::max(0, cy0 * sub_); gy <= std::min(gh_ - 1, (cy1 + 1) * sub_); ++gy)
        for (int gx = std::max(0, cx0 * sub_); gx <= std::min(gw_ - 1, (cx1 + 1) * sub_); ++gx)
            m = std::max(m, h_[std::size_t(gy) * gw_ + gx]);
    return m;
}

bool Terrain::raycast(const Vec3& o, const Vec3& d, Vec3& hit) const {
    if (d.y >= -1e-5f) return false;
    // Start where the ray comes down to the highest possible terrain.
    float top = 0;
    for (float h : h_) top = std::max(top, h);
    float t = o.y > top ? (o.y - top) / -d.y : 0.0f;
    const float tEnd = (o.y + 1.0f) / -d.y;  // below the sand level
    const float step = 3.0f;
    auto above = [&](float tt) {
        const Vec3 p = o + d * tt;
        return p.y > height(p.x, p.z);
    };
    float prev = t;
    for (; t <= tEnd; t += step) {
        if (!above(t)) {
            float lo = prev, hi = t;
            for (int i = 0; i < 12; ++i) {
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

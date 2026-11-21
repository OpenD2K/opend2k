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
    // Reimagined (faithful): a 4 px grid, fine enough for the sculpted cliffs (reimaginedRelief).
    sub_ = !designed ? 2 : faithful_ && reimagined_ ? 8 : 4;
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
    nearCount_ = 0;
    fineQuad_.clear();
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
    // Reimagined: the spice relief is added to the natural height (spiceRelief); the base is kept so a
    // harvested / grown cell can be recomputed without the guide (refreshSpice).
    hBase_.clear();
    if (fineGrid()) hBase_.assign(std::size_t(gw_) * gh_, 0.0f);
    dirtyRows_ = {-1, -1};
    spiceChanged_.clear();
    found_.clear();
    foundCell_.assign(std::size_t(n), -1);
    foundNear_.assign(std::size_t(n), 0);
    computeHeights(0, 0, gw_ - 1, gh_ - 1);
    int fx0, fy0, fx1, fy1;
    if (setFoundations(w, fx0, fy0, fx1, fy1)) applyFoundations(0, 0, gw_ - 1, gh_ - 1);
    computeNormalsAndSun(0, 0, gw_ - 1, gh_ - 1);
    if (sub_ == 8) buildAdaptiveIndices();
    guide_ = nullptr;
}

// Reimagined (4 px grid): only where the relief needs it every vertex is used. An 8 px quad the
// coarse grid describes well enough (no vertex above kTall - dunes, plateau shores - or every 4 px
// vertex within kTol of its bilinear surface: plateau tops, the sand at a cliff's foot) is drawn as
// 2 triangles; one that borders a fine quad is a fan around its centre vertex that takes the fine
// side's middle vertex too (no T-junction, no crack). The fine quads are the cliff faces. Same
// vertices; the heights picking reads (height(), the 4 px grid) differ from the drawn surface by a
// fraction of a px on the coarse quads.
bool Terrain::quadWantsFine(int qx, int qy) const {
    constexpr float kTol = 0.15f, kTall = 12.0f;
    auto H = [&](int gx, int gy) { return h_[std::size_t(gy) * gw_ + gx]; };
    const int x0 = qx * 2, y0 = qy * 2;
    float top = 0, spice = 0;
    for (int j = 0; j <= 2; ++j)
        for (int i = 0; i <= 2; ++i) {
            top = std::max(top, H(x0 + i, y0 + j));
            // (The spice relief - docs/3d-mode.md "Spice fields in 3D" - is low but needs the 4 px grid for its
            // drifts; not on the map's outer two quads, where the world beyond stitches to the coarse edge.)
            if (!hBase_.empty()) {
                const std::size_t vi = std::size_t(y0 + j) * gw_ + std::size_t(x0 + i);
                spice = std::max(spice, hNat_[vi] - hBase_[vi]);
            }
        }
    const int qw = (gw_ - 1) / 2, qh = (gh_ - 1) / 2;
    const bool spiceQuad = spice > 0.05f && qx >= 2 && qy >= 2 && qx < qw - 2 && qy < qh - 2;
    if (top <= kTall && !spiceQuad) return false;
    const float a = H(x0, y0), b = H(x0 + 2, y0), c = H(x0, y0 + 2), d = H(x0 + 2, y0 + 2);
    for (int j = 0; j <= 2; ++j)
        for (int i = 0; i <= 2; ++i) {
            const float u = float(i) * 0.5f, v = float(j) * 0.5f;
            const float bil = (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
            if (std::fabs(H(x0 + i, y0 + j) - bil) > kTol) return true;
        }
    return false;
}

void Terrain::buildAdaptiveIndices() {
    const int qw = (gw_ - 1) / 2, qh = (gh_ - 1) / 2;  // 8 px quads
    std::vector<std::uint8_t>& fine = fineQuad_;
    fine.assign(std::size_t(qw) * qh, 0);
    for (int qy = 0; qy < qh; ++qy)
        for (int qx = 0; qx < qw; ++qx) fine[std::size_t(qy) * qw + qx] = quadWantsFine(qx, qy) ? 1 : 0;
    ++indexGen_;
    auto isFine = [&](int qx, int qy) {
        return qx >= 0 && qy >= 0 && qx < qw && qy < qh && fine[std::size_t(qy) * qw + qx] != 0;
    };
    auto V = [&](int gx, int gy) { return std::uint32_t(gy * gw_ + gx); };
    idx_.clear();
    for (int qy = 0; qy < qh; ++qy)
        for (int qx = 0; qx < qw; ++qx) {
            const int x0 = qx * 2, y0 = qy * 2;
            if (isFine(qx, qy)) {
                for (int y = y0; y < y0 + 2; ++y)
                    for (int x = x0; x < x0 + 2; ++x) {
                        const std::uint32_t a = V(x, y), b = a + 1, c = a + std::uint32_t(gw_), d = c + 1;
                        if ((x + y) & 1) idx_.insert(idx_.end(), {a, c, b, b, c, d});
                        else idx_.insert(idx_.end(), {a, c, d, a, d, b});
                    }
                continue;
            }
            // Sides that touch a fine quad: north, east, south, west.
            const bool sN = isFine(qx, qy - 1), sS = isFine(qx, qy + 1), sW = isFine(qx - 1, qy), sE = isFine(qx + 1, qy);
            const std::uint32_t nw = V(x0, y0), ne = V(x0 + 2, y0), sw = V(x0, y0 + 2), se = V(x0 + 2, y0 + 2);
            if (!sN && !sS && !sW && !sE) {
                if ((qx + qy) & 1) idx_.insert(idx_.end(), {nw, sw, ne, ne, sw, se});
                else idx_.insert(idx_.end(), {nw, sw, se, nw, se, ne});
                continue;
            }
            const std::uint32_t ctr = V(x0 + 1, y0 + 1);
            auto side = [&](std::uint32_t p, std::uint32_t q, bool split, std::uint32_t mid) {
                if (split) idx_.insert(idx_.end(), {ctr, p, mid, ctr, mid, q});
                else idx_.insert(idx_.end(), {ctr, p, q});
            };
            side(nw, ne, sN, V(x0 + 1, y0));
            side(ne, se, sE, V(x0 + 2, y0 + 1));
            side(se, sw, sS, V(x0 + 1, y0 + 2));
            side(sw, nw, sW, V(x0, y0 + 1));
        }
    // The far range (drawRange): every 8 px quad as 2 triangles, appended after the near one.
    nearCount_ = std::uint32_t(idx_.size());
    for (int qy = 0; qy < qh; ++qy)
        for (int qx = 0; qx < qw; ++qx) {
            const std::uint32_t nw = V(qx * 2, qy * 2), ne = V(qx * 2 + 2, qy * 2), sw = V(qx * 2, qy * 2 + 2), se = V(qx * 2 + 2, qy * 2 + 2);
            if ((qx + qy) & 1) idx_.insert(idx_.end(), {nw, sw, ne, ne, sw, se});
            else idx_.insert(idx_.end(), {nw, sw, se, nw, se, ne});
        }
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
            const auto solid = std::uint16_t(w.buildingTemplate(b).tilesOccupiedSolid & mask);
            if (mask) want.push_back(Foundation{b.tileX, b.tileY, mask, 0, solid});
        }
    auto less = [](const Foundation& a, const Foundation& b) {
        return a.y != b.y ? a.y < b.y : a.x != b.x ? a.x < b.x : a.mask < b.mask;
    };
    std::sort(want.begin(), want.end(), less);
    if (want == found_) return false;
    // Levelled height: a low quantile (kFoundationLevel) of the natural heights of the footprint's
    // vertices - on sloping ground the building is set into the slope (a cut on the uphill side) rather
    // than raised onto a fill that would stand out over the ground below as a mound.
    auto H = [&](int gx, int gy) {
        return hNat_[std::size_t(std::clamp(gy, 0, gh_ - 1)) * gw_ + std::size_t(std::clamp(gx, 0, gw_ - 1))];
    };
    std::fill(foundCell_.begin(), foundCell_.end(), -1);
    const std::size_t nf = want.size();
    std::vector<float> cells(nf, 0.0f), hs;
    for (std::size_t i = 0; i < nf; ++i) {
        Foundation& f = want[i];
        hs.clear();
        for (int k = 0; k < 16; ++k) {
            if (!(f.mask & (1u << k))) continue;
            const int cx = f.x + k % 4, cy = f.y + k / 4;
            if (cx < 0 || cy < 0 || cx >= cw_ || cy >= ch_) continue;
            foundCell_[std::size_t(cy) * cw_ + cx] = int(i);
            cells[i] += 1.0f;
            for (int gy = cy * sub_; gy <= (cy + 1) * sub_; ++gy)
                for (int gx = cx * sub_; gx <= (cx + 1) * sub_; ++gx) hs.push_back(H(gx, gy));
        }
        if (hs.empty()) continue;
        const auto nth = hs.begin() + std::ptrdiff_t(std::lround(kFoundationLevel * float(hs.size() - 1)));
        std::nth_element(hs.begin(), nth, hs.end());
        f.h = *nth;
    }
    // Neighbouring buildings: two footprints a gap apart may differ by at most the ramps' rate x the
    // gap, so their ramps agree. Touching ones where a cell units drive over meets the other (an
    // apron: the owner's refinery right below the construction yard's apron row) by
    // kFoundationTouchStep - a small lip on the shared border instead of a wall on that apron, while
    // a base on sloping ground still follows the slope in small steps (one shared height would sink
    // or raise its far end by the whole relief). Solid parts touching solid parts are free (a turret
    // against a wall stands on its own level; nothing drives over that border). Every pair over its
    // limit is pulled together, the smaller footprint moving more.
    constexpr int kPairCells = 4;  // footprints further apart than this are left alone
    struct Pair {
        int a, b;
        float gap;
        bool apron;  // touching, with a non-solid cell at the contact
    };
    auto solidAt = [&](int fi, int cx, int cy) {
        const Foundation& f = want[std::size_t(fi)];
        return (f.solid >> ((cy - f.y) * 4 + (cx - f.x))) & 1u;
    };
    std::vector<Pair> pairs;
    for (int cy = 0; cy < ch_; ++cy)
        for (int cx = 0; cx < cw_; ++cx) {
            const int fi = foundCell_[std::size_t(cy) * cw_ + cx];
            if (fi < 0) continue;
            for (int y = std::max(0, cy - kPairCells); y <= std::min(ch_ - 1, cy + kPairCells); ++y)
                for (int x = std::max(0, cx - kPairCells); x <= std::min(cw_ - 1, cx + kPairCells); ++x) {
                    const int fj = foundCell_[std::size_t(y) * cw_ + x];
                    if (fj <= fi) continue;
                    const float gx = float(std::max(0, std::abs(x - cx) - 1)), gy = float(std::max(0, std::abs(y - cy) - 1));
                    const float gap = 32.0f * std::sqrt(gx * gx + gy * gy);
                    const bool apron = gap == 0.0f && !(solidAt(fi, cx, cy) && solidAt(fj, x, y));
                    auto it = std::find_if(pairs.begin(), pairs.end(), [&](const Pair& p) { return p.a == fi && p.b == fj; });
                    if (it == pairs.end()) pairs.push_back({fi, fj, gap, apron});
                    else it->gap = std::min(it->gap, gap), it->apron = it->apron || apron;
                }
        }
    for (int it = 0; it < 64 && !pairs.empty(); ++it) {
        bool moved = false;
        for (const Pair& p : pairs) {
            if (p.gap == 0.0f && !p.apron) continue;
            float& ha = want[std::size_t(p.a)].h;
            float& hb = want[std::size_t(p.b)].h;
            const float allowed = p.apron ? kFoundationTouchStep : (kFoundationRamp + kFoundationRampOver) * p.gap;
            const float excess = std::fabs(ha - hb) - allowed;
            if (excess <= 0.01f) continue;
            const float wa = cells[std::size_t(p.b)] / (cells[std::size_t(p.a)] + cells[std::size_t(p.b)]);
            const float s = ha > hb ? -1.0f : 1.0f;
            ha += s * excess * wa;
            hb -= s * excess * (1.0f - wa);
            moved = true;
        }
        if (!moved) break;
    }
    // The rectangle of every footprint that appeared, went away or got another height.
    x0 = cw_, y0 = ch_, x1 = -1, y1 = -1;
    auto grow = [&](const Foundation& f) {
        for (int k = 0; k < 16; ++k)
            if (f.mask & (1u << k)) {
                x0 = std::min(x0, f.x + k % 4);
                y0 = std::min(y0, f.y + k / 4);
                x1 = std::max(x1, f.x + k % 4);
                y1 = std::max(y1, f.y + k / 4);
            }
    };
    for (const Foundation& f : want) {
        auto it = std::lower_bound(found_.begin(), found_.end(), f, less);
        if (it == found_.end() || !(*it == f) || std::fabs(it->h - f.h) > 1e-4f) grow(f);
    }
    for (const Foundation& f : found_)
        if (!std::binary_search(want.begin(), want.end(), f, less)) grow(f);
    // The cells a foundation's ramps can reach (applyFoundations skips the rest).
    std::fill(foundNear_.begin(), foundNear_.end(), std::uint8_t(0));
    const int rc = int(std::ceil(kFoundationReach / 32.0f)) + 1;
    for (int cy = 0; cy < ch_; ++cy)
        for (int cx = 0; cx < cw_; ++cx) {
            if (foundCell_[std::size_t(cy) * cw_ + cx] < 0) continue;
            for (int y = std::max(0, cy - rc); y <= std::min(ch_ - 1, cy + rc); ++y)
                for (int x = std::max(0, cx - rc); x <= std::min(cw_ - 1, cx + rc); ++x) foundNear_[std::size_t(y) * cw_ + x] = 1;
        }
    found_ = std::move(want);
    return x1 >= 0;
}

bool Terrain::updateFoundations(const game::World& w) {
    if (w.width() != cw_ || w.height() != ch_ || hNat_.empty()) return false;
    int x0, y0, x1, y1;
    if (!setFoundations(w, x0, y0, x1, y1)) return false;
    const int s = int(std::ceil(kFoundationReach / step_)) + 1;
    const int hx0 = std::max(0, x0 * sub_ - s), hy0 = std::max(0, y0 * sub_ - s);
    const int hx1 = std::min(gw_ - 1, (x1 + 1) * sub_ + s), hy1 = std::min(gh_ - 1, (y1 + 1) * sub_ + s);
    int c[4];
    if (!applyFoundations(hx0, hy0, hx1, hy1, c)) return true;
    // Normals one vertex around the vertices that moved; the sun visibility down-sun as far as the shadow march reaches.
    const int reach = int(std::ceil(float(marchSteps()) * marchStep() / step_)) + 2;
    computeNormalsAndSun(std::max(0, c[0] - reach), std::max(0, c[1] - reach), std::min(gw_ - 1, c[2] + reach),
                         std::min(gh_ - 1, c[3] + reach));
    return true;
}

float Terrain::foundationAt(int cx, int cy, float fallback) const {
    if (cx < 0 || cy < 0 || cx >= cw_ || cy >= ch_ || foundCell_.empty()) return fallback;
    const int i = foundCell_[std::size_t(cy) * cw_ + cx];
    return i >= 0 ? found_[std::size_t(i)].h : fallback;
}

// The foundations' ramps (docs/3d-mode.md "Building foundations"). Two bounds per vertex, each a
// geodesic cone from the footprints: U = min over footprints (F + the cheapest path cost to it),
// L = max (F - that cost), where a step of the path costs its length x the ramp rate of the ground
// it crosses: max(kFoundationRamp, the natural slope there + kFoundationRampOver). The natural
// height is clamped into [L, U]: kept where it already lies inside (gentle ground near a building
// stays as it is), else the bound - a ramp from the footprint's edge down (fill) or up (cut) that
// meets the natural ground. U and L change at most by the local rate per px, so the result is never
// steeper than max(kFoundationRamp, natural slope + kFoundationRampOver): no mound, pit or step on
// passable ground; across a cliff face the rate follows the face, so the cone climbs (or drops)
// with it and a cliff beside a building keeps its face and its top. Footprint vertices are exactly
// F (where two footprints touch: the middle of their heights; likewise where two cones disagree).
// Paths end at kFoundationReach px; the last 32 px blend back to the natural height (only reached
// where the natural ground keeps rising faster than the cone's rate).
bool Terrain::applyFoundations(int gx0, int gy0, int gx1, int gy1, int* changed) {
    if (changed) changed[0] = gw_, changed[1] = gh_, changed[2] = -1, changed[3] = -1;
    auto put = [&](int gx, int gy, float h) {
        const std::size_t vi = std::size_t(gy) * gw_ + gx;
        if (changed && h != h_[vi]) {
            changed[0] = std::min(changed[0], gx), changed[1] = std::min(changed[1], gy);
            changed[2] = std::max(changed[2], gx), changed[3] = std::max(changed[3], gy);
        }
        h_[vi] = h;
        verts_[vi].y = h;
    };
    auto natural = [&] {
        for (int gy = gy0; gy <= gy1; ++gy)
            for (int gx = gx0; gx <= gx1; ++gx) put(gx, gy, hNat_[std::size_t(gy) * gw_ + gx]);
        return !changed || changed[2] >= 0;
    };
    if (found_.empty()) return natural();
    const float R = kFoundationReach, fade = 32.0f;
    const int rv = int(std::ceil(R / step_)) + 1;
    const int ex0 = std::max(0, gx0 - rv), ey0 = std::max(0, gy0 - rv);
    const int ex1 = std::min(gw_ - 1, gx1 + rv), ey1 = std::min(gh_ - 1, gy1 + rv);
    const int ew = ex1 - ex0 + 1, eh = ey1 - ey0 + 1;
    const std::size_t en = std::size_t(ew) * eh;
    // Is anything near at all (a footprint within the reach of the rectangle)?
    bool any = false;
    for (int cy = std::max(0, gy0 / sub_ - 1); cy <= std::min(ch_ - 1, gy1 / sub_) && !any; ++cy)
        for (int cx = std::max(0, gx0 / sub_ - 1); cx <= std::min(cw_ - 1, gx1 / sub_) && !any; ++cx)
            any = foundNear_[std::size_t(cy) * cw_ + cx] != 0;
    if (!any) return natural();

    constexpr float kInf = 1e30f;
    std::vector<float> rate(en, -1.0f);  // -1: no footprint within reach (not visited)
    std::vector<float> U(en, kInf), L(en, kInf), lenU(en, kInf), lenL(en, kInf);  // L holds -L
    std::vector<std::uint8_t> fixed(en, 0);
    const int ds = std::max(1, int(std::lround(8.0f / step_)));
    auto nat = [&](int gx, int gy) {
        return hNat_[std::size_t(std::clamp(gy, 0, gh_ - 1)) * gw_ + std::size_t(std::clamp(gx, 0, gw_ - 1))];
    };
    using Item = std::pair<float, std::uint32_t>;
    std::vector<Item> heapU, heapL;
    for (int gy = ey0; gy <= ey1; ++gy)
        for (int gx = ex0; gx <= ex1; ++gx) {
            const std::size_t li = std::size_t(gy - ey0) * ew + std::size_t(gx - ex0);
            // A vertex belongs to every cell it touches: a footprint vertex if any of them is one.
            bool near = false;
            for (int oy = -1; oy <= 0; ++oy)
                for (int ox = -1; ox <= 0; ++ox) {
                    const int cx = gx / sub_ + ((gx % sub_ == 0) ? ox : 0), cy = gy / sub_ + ((gy % sub_ == 0) ? oy : 0);
                    if (cx < 0 || cy < 0 || cx >= cw_ || cy >= ch_) continue;
                    const std::size_t ci = std::size_t(cy) * cw_ + cx;
                    near = near || foundNear_[ci];
                    const int fi = foundCell_[ci];
                    if (fi < 0) continue;
                    const float F = found_[std::size_t(fi)].h;
                    U[li] = std::min(U[li], F);
                    L[li] = std::min(L[li], -F);
                    fixed[li] = 1;
                }
            if (!near) continue;
            const float sx = (nat(gx + ds, gy) - nat(gx - ds, gy)) / (2.0f * float(ds) * step_);
            const float sy = (nat(gx, gy + ds) - nat(gx, gy - ds)) / (2.0f * float(ds) * step_);
            rate[li] = std::max(kFoundationRamp, std::sqrt(sx * sx + sy * sy) + kFoundationRampOver);
            if (fixed[li]) {
                lenU[li] = lenL[li] = 0.0f;
                heapU.push_back({U[li], std::uint32_t(li)});
                heapL.push_back({L[li], std::uint32_t(li)});
            }
        }
    // Multi-source Dijkstra (min-plus) over the 8-neighbourhood.
    auto run = [&](std::vector<float>& val, std::vector<float>& len, std::vector<Item>& heap) {
        auto cmp = [](const Item& a, const Item& b) { return a.first > b.first; };
        std::make_heap(heap.begin(), heap.end(), cmp);
        static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        while (!heap.empty()) {
            std::pop_heap(heap.begin(), heap.end(), cmp);
            const auto [v, i] = heap.back();
            heap.pop_back();
            if (v > val[i]) continue;
            const int x = int(i % std::uint32_t(ew)), y = int(i / std::uint32_t(ew));
            for (int k = 0; k < 8; ++k) {
                const int nx = x + dx[k], ny = y + dy[k];
                if (nx < 0 || ny < 0 || nx >= ew || ny >= eh) continue;
                const std::size_t j = std::size_t(ny) * ew + nx;
                if (rate[j] < 0 || fixed[j]) continue;
                const float step = (k < 4 ? 1.0f : 1.41421356f) * step_;
                const float l = len[i] + step;
                if (l > R) continue;
                const float c = v + 0.5f * (rate[i] + rate[j]) * step;
                if (c < val[j]) {
                    val[j] = c;
                    len[j] = l;
                    heap.push_back({c, std::uint32_t(j)});
                    std::push_heap(heap.begin(), heap.end(), cmp);
                }
            }
        }
    };
    run(U, lenU, heapU);
    run(L, lenL, heapL);
    for (int gy = gy0; gy <= gy1; ++gy)
        for (int gx = gx0; gx <= gx1; ++gx) {
            const std::size_t li = std::size_t(gy - ey0) * ew + std::size_t(gx - ex0);
            const std::size_t vi = std::size_t(gy) * gw_ + gx;
            const float hn = hNat_[vi], u = U[li], l = L[li] < kInf ? -L[li] : -kInf;
            float h = hn;
            if (fixed[li]) {
                h = 0.5f * (u + l);
            } else if (u < kInf || l > -kInf) {
                float t = hn, len = R;
                if (l > u) t = 0.5f * (l + u), len = std::min(lenU[li], lenL[li]);
                else if (hn > u) t = u, len = lenU[li];
                else if (hn < l) t = l, len = lenL[li];
                h = hn + (t - hn) * smoothstep01((R - len) / fade);
            }
            put(gx, gy, h);
        }
    return !changed || changed[2] >= 0;
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
                if (!hBase_.empty()) spiceChanged_.push_back(int(std::size_t(y) * cw_ + x));
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
        }
    if (x1 < 0) return false;
    if (!hBase_.empty()) return refreshSpiceRelief(w);
    // Designed: spice is a material (render3d/terrain_v2.*), the ground does not move.
    if (designed_) return false;
    // Heights follow the bilinear cell field (one cell around a change); the normals one vertex
    // further; the sun visibility down-sun as far as the shadow march reaches (it looks towards
    // the sun, the north-west, so a change shades vertices to its south-east).
    const int hx0 = std::max(0, (x0 - 1) * sub_), hy0 = std::max(0, (y0 - 1) * sub_);
    const int hx1 = std::min(gw_ - 1, (x1 + 2) * sub_), hy1 = std::min(gh_ - 1, (y1 + 2) * sub_);
    computeHeights(hx0, hy0, hx1, hy1);
    const int reach = int(std::ceil(float(marchSteps()) * marchStep() / step_)) + 2;  // march steps (computeNormalsAndSun) in vertices
    computeNormalsAndSun(std::max(0, hx0 - 1), std::max(0, hy0 - 1), std::min(gw_ - 1, hx1 + reach),
                         std::min(gh_ - 1, hy1 + reach));
    return true;
}

// Reimagined spice relief (docs/3d-mode.md "Spice fields in 3D"). Per cell the presence (the amount up to thin
// spice, 2 of 7) and the richness (what lies above thin: thick spice 1, blooms more), sampled bilinearly between
// the cell centres. The relief stays inside the field (its envelope is 0 where the presence falls to half, i.e.
// on the border of a spice cell and a bare one: the cells beside a field never move, the clutter there keeps its
// height) and fades out within ~24 px of the map's edge (the world beyond mirrors the edge and draws no spice).
// Inside: a low base layer, soft mounds (~46 px) and wind-shaped drifts across the wind of the sand ripples
// (terrain3_fs, reim[1].xy = 0.8, 0.6): a long windward rise, a short lee slope, crests ~52 px apart. Thin spice
// ~1.5 px at most, thick ~4.5 px, blooms ~6 px. Smooth at 8 px (the Reimagined mesh draws the sand as 8 px quads,
// buildAdaptiveIndices): the drawn surface stays within a fraction of a px of height(). The fine relief
// (granules, ripples, their self-shadow) is the shader's.
float Terrain::spiceRelief(float wx, float wy) const {
    if (hBase_.empty() || spice_.empty()) return 0.0f;
    const float u = wx / 32.0f - 0.5f, v = wy / 32.0f - 0.5f;
    const int x0 = int(std::floor(u)), y0 = int(std::floor(v));
    const float fx = u - float(x0), fy = v - float(y0);
    float P = 0, R = 0;
    for (int k = 0; k < 4; ++k) {
        const int x = std::clamp(x0 + (k & 1), 0, cw_ - 1), y = std::clamp(y0 + (k >> 1), 0, ch_ - 1);
        const float wgt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
        const float a = float(spice_[std::size_t(y) * cw_ + x]);
        P += wgt * std::min(a, 2.0f) * 0.5f;
        R += wgt * std::clamp((a - 2.0f) * 0.5f, 0.0f, 1.25f);
    }
    if (P <= 0.5f) return 0.0f;
    const float edge = std::min({wx, wy, float(cw_) * 32.0f - wx, float(ch_) * 32.0f - wy});
    const float env = smoothstep01((P - 0.5f) / 0.45f) * smoothstep01((edge - 4.0f) / 20.0f);
    if (env <= 0.0f) return 0.0f;
    const float mound = smoothstep01((fbm(wx + 211.0f, wy - 87.0f, 34.0f, 2) - 0.34f) / 0.38f);
    const float warp = (fbm(wx - 53.0f, wy + 140.0f, 110.0f, 2) - 0.5f) * 1.6f;
    const float phase = (wx * 0.8f + wy * 0.6f) / 30.0f + warp;
    const float t = phase - std::floor(phase);
    const float ridge = (t < 0.8f ? smoothstep01(t / 0.8f) : 1.0f - smoothstep01((t - 0.8f) / 0.2f)) *
                        (0.4f + 1.0f * fbm(wx, wy + 77.0f, 60.0f, 1));
    return env * (0.6f + 1.0f * R + (0.8f + 2.6f * R) * mound + (0.5f + 1.4f * R) * ridge);
}

// Reimagined: the spice relief of the cells that changed (spiceChanged_), grouped in 8 x 8 cell blocks (two
// harvesters far apart recompute two small boxes, not the rectangle between them). Heights one cell round a
// change (the bilinear field) + the foundations, the normals one vertex further and the sun visibility down-sun
// as far as the shadow march reaches. dirtyRows_: the vertex rows touched.
bool Terrain::refreshSpiceRelief(const game::World& w) {
    (void)w;
    constexpr int kB = 8;
    const int bw = (cw_ + kB - 1) / kB, bh = (ch_ + kB - 1) / kB;
    spiceBox_.assign(std::size_t(bw) * bh * 4, -1);
    for (const int i : spiceChanged_) {
        const int x = i % cw_, y = i / cw_;
        int* b = &spiceBox_[(std::size_t(y / kB) * bw + std::size_t(x / kB)) * 4];
        if (b[2] < 0) {
            b[0] = b[2] = x;
            b[1] = b[3] = y;
        } else {
            b[0] = std::min(b[0], x), b[1] = std::min(b[1], y);
            b[2] = std::max(b[2], x), b[3] = std::max(b[3], y);
        }
    }
    spiceChanged_.clear();
    const int reach = int(std::ceil(float(marchSteps()) * marchStep() / step_)) + 2;
    const float horiz = std::sqrt(sun_.x * sun_.x + sun_.z * sun_.z);
    // A vertex looks towards the sun: a change shades the vertices on its far side from the sun.
    const bool sunW = horiz > 0 && sun_.x < 0, sunN = horiz > 0 && sun_.z < 0;
    int r0 = gh_, r1 = -1;
    bool needIdx = false;
    for (std::size_t b = 0; b < spiceBox_.size(); b += 4) {
        if (spiceBox_[b + 2] < 0) continue;
        const int hx0 = std::max(0, (spiceBox_[b] - 1) * sub_), hy0 = std::max(0, (spiceBox_[b + 1] - 1) * sub_);
        const int hx1 = std::min(gw_ - 1, (spiceBox_[b + 2] + 2) * sub_), hy1 = std::min(gh_ - 1, (spiceBox_[b + 3] + 2) * sub_);
        for (int gy = hy0; gy <= hy1; ++gy)
            for (int gx = hx0; gx <= hx1; ++gx) {
                const std::size_t vi = std::size_t(gy) * gw_ + gx;
                hNat_[vi] = hBase_[vi] + spiceRelief(float(gx) * step_, float(gy) * step_);
            }
        applyFoundations(hx0, hy0, hx1, hy1);
        // (New relief on coarse 8 px quads - a bloom's spice: the index buffer again.)
        if (sub_ == 8 && !needIdx) {
            const int qw = (gw_ - 1) / 2, qh = (gh_ - 1) / 2;
            for (int qy = std::max(0, hy0 / 2); qy <= std::min(qh - 1, hy1 / 2) && !needIdx; ++qy)
                for (int qx = std::max(0, hx0 / 2); qx <= std::min(qw - 1, hx1 / 2) && !needIdx; ++qx)
                    needIdx = fineQuad_[std::size_t(qy) * qw + qx] == 0 && quadWantsFine(qx, qy);
        }
        const int nx0 = std::max(0, hx0 - 1 - (sunW ? 0 : reach)), nx1 = std::min(gw_ - 1, hx1 + 1 + (sunW ? reach : 0));
        const int ny0 = std::max(0, hy0 - 1 - (sunN ? 0 : reach)), ny1 = std::min(gh_ - 1, hy1 + 1 + (sunN ? reach : 0));
        computeNormalsAndSun(nx0, ny0, nx1, ny1);
        r0 = std::min(r0, ny0);
        r1 = std::max(r1, ny1);
    }
    dirtyRows_ = r1 >= 0 ? std::pair<int, int>{r0, r1} : std::pair<int, int>{-1, -1};
    if (needIdx) {
        buildAdaptiveIndices();
        dirtyRows_ = {-1, -1};
    }
    return r1 >= 0;
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
                if (guide_) h = (reimagined_ ? reimaginedRelief(wx, wy) : reliefHeight(guide_->heightAt(wx, wy), false)) + duneH + swell;
                if (!hBase_.empty()) {
                    hBase_[std::size_t(gy) * gw_ + gx] = h;
                    h += spiceRelief(wx, wy);
                }
            }
            hNat_[std::size_t(gy) * gw_ + gx] = h;
        }
    applyFoundations(gx0, gy0, gx1, gy1);
}

// Reimagined (docs/3d-mode.md "Reimagined", round 2): the faithful relief sculpted into rock -
// gullies and buttresses cut into the faces (the guide's height read again a few px away, kept
// only where that is LOWER: a face recedes by 0..6 px in a pattern that runs down it, the rim breaks
// up, nothing ever rises - cells vehicles use get no new relief) and sandstone ledges (the height
// stepped into strata 11-16 px thick where the face is steep: each step a narrow shelf, the face
// above it set back; plateau tops and gentle slopes untouched). Picking and every height lookup use
// the same mesh.
float Terrain::reimaginedRelief(float wx, float wy) const {
    const float g0 = guide_->heightAt(wx, wy);
    if (g0 <= 0.0f) return 0.0f;
    // Ground units use: the sculpting may only cut into impassable cliff cells. Near a sand / rock
    // cell (where units drive or walk) it fades out within 2..10 px, so a narrow sand gap between
    // two cliffs is never carved into a pit. (The ground of those cells is continuous with gentle
    // ramps in the guide itself: TerrainGuide::Params::reimagined, docs/3d-mode.md "Passable ground".)
    float dPass = 1e9f;
    {
        const int cx = int(std::floor(wx / 32.0f)), cy = int(std::floor(wy / 32.0f));
        for (int y = cy - 1; y <= cy + 1; ++y)
            for (int x = cx - 1; x <= cx + 1; ++x) {
                if (x < 0 || y < 0 || x >= cw_ || y >= ch_) continue;
                if (kind_[std::size_t(y) * cw_ + x] == kCliffCell) continue;
                const float ddx = std::max({float(x) * 32.0f - wx, 0.0f, wx - float(x + 1) * 32.0f});
                const float ddy = std::max({float(y) * 32.0f - wy, 0.0f, wy - float(y + 1) * 32.0f});
                dPass = std::min(dPass, std::sqrt(ddx * ddx + ddy * ddy));
            }
    }
    const float carve = smoothstep01((dPass - 2.0f) / 8.0f);
    if (carve <= 0.0f) return reliefHeight(g0, true);
    const float plain = reliefHeight(g0, true);
    return plain + (reimaginedSculpt(wx, wy, g0) - plain) * carve;
}

float Terrain::reimaginedSculpt(float wx, float wy, float g0) const {
    // Only the tall cliffs are sculpted: level rock (no slope within the sculpting's reach) and the low
    // steps of plateau shores (nothing over a quarter of the cliff height near) stay as they are.
    float tall;
    {
        const float r = 8.0f;
        const float a = guide_->heightAt(wx + r, wy), b = guide_->heightAt(wx - r, wy), c = guide_->heightAt(wx, wy + r),
                    d = guide_->heightAt(wx, wy - r);
        const float m = std::max({std::fabs(a - g0), std::fabs(b - g0), std::fabs(c - g0), std::fabs(d - g0)});
        tall = smoothstep01((std::max({g0, a, b, c, d}) - 0.25f) / 0.2f);
        if (m < 1e-4f || tall <= 0.0f) return reliefHeight(g0, true);
    }
    const float e = 2.0f;
    const float dx = guide_->heightAt(wx + e, wy) - guide_->heightAt(wx - e, wy);
    const float dy = guide_->heightAt(wx, wy + e) - guide_->heightAt(wx, wy - e);
    const float gl = std::sqrt(dx * dx + dy * dy);
    const float slope = gl / (2.0f * e) * kCliffHeight * 2.0f;  // rise per px of the reimagined height
    // Columns: across the face (u = along its outline) rounded columns 7-17 px wide with sharp
    // V-shaped gullies between them; the face recedes into the rock by up to 7 px at a gully (the
    // height read that far downhill). Plus a coarse irregular warp, so no wall stays straight.
    float g = g0;
    if (gl > 1e-4f) {
        const float nx = -dx / gl, ny = -dy / gl;  // downhill (out of the rock)
        const float u = (-ny * wx + nx * wy) + (fbm(wx, wy, 23.0f, 2) - 0.5f) * 10.0f;
        const float t = u / 11.0f, fi = std::floor(t);
        const int i = int(fi);
        auto edge = [&](int k) { return float(k) + (hash2(k, 913) - 0.5f) * 0.6f; };  // jittered column borders
        int k = i;
        if (t < edge(i)) k = i - 1;
        else if (t >= edge(i + 1)) k = i + 1;
        const float x = (t - edge(k)) / std::max(edge(k + 1) - edge(k), 1e-3f);
        const float c = std::fabs(2.0f * x - 1.0f);
        const float r = 7.0f * c * c * c * smoothstep01((gl - 0.002f) / 0.01f) * tall;
        if (r > 0.05f) g = std::min(g, guide_->heightAt(wx + nx * r, wy + ny * r));
    }
    const float A = 4.0f * tall;
    const float ox = (fbm(wx, wy, 14.0f, 2) - 0.5f) * 2.0f, oy = (fbm(wx + 51.7f, wy - 23.1f, 14.0f, 2) - 0.5f) * 2.0f;
    g = std::min(g, guide_->heightAt(wx + ox * A, wy + oy * A));
    float h = reliefHeight(g, true);
    // Ledges, where the original relief is steep.
    const float wTer = smoothstep01((slope - 0.8f) / 1.6f) * tall;
    if (wTer > 0.0f && h > 4.0f) {
        const float T = 11.0f + 5.0f * fbm(wx, wy, 90.0f, 1);
        const float t = (h - 4.0f) / T, fl = std::floor(t), fr = t - fl;
        const float ter = 4.0f + (fl + std::max(0.0f, (fr - 0.3f) / 0.7f)) * T;  // <= h: only ever lower
        h += (ter - h) * wTer * 0.85f;
    }
    // Talus: the foot keeps the unsculpted profile up to 6-14 px (a skirt of fallen rock the columns
    // stand on, no spikes; never above the original relief).
    const float talus = 6.0f + 8.0f * fbm(wx + 7.0f, wy - 3.0f, 17.0f, 2);
    return std::max(h, std::min(reliefHeight(g0, true), talus));
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
            for (int s = 1; s <= marchSteps() && vis > 0; ++s) {
                const float t = float(s) * marchStep();
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

float Terrain::markFade(float wx, float wy, float refY) const {
    auto smooth = [](float e0, float e1, float x) {
        const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    };
    const float d = std::max(2.0f, step_);
    const float gx = (height(wx + d, wy) - height(wx - d, wy)) / (2 * d);
    const float gy = (height(wx, wy + d) - height(wx, wy - d)) / (2 * d);
    const float up = 1.0f / std::sqrt(1 + gx * gx + gy * gy);
    return smooth(0.55f, 0.85f, up) * (1 - smooth(4.0f, 8.0f, height(wx, wy) - refY));
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

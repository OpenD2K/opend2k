// Running gear of vehicle models: wheels and track links (render3d/model_drive.hpp,
// docs/3d-models.md "Wheels and tracks").
#include "render3d/model_drive.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace d2k::render3d {

namespace {

constexpr float kPi = 3.14159265358979f;

int root(std::vector<int>& par, int x) {
    while (par[std::size_t(x)] != x) {
        par[std::size_t(x)] = par[std::size_t(par[std::size_t(x)])];
        x = par[std::size_t(x)];
    }
    return x;
}

struct Island {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    float c[3] = {};  // centroid of the corners
    float rad = 0;    // farthest corner from the centroid in the side (y, z) plane
    int first = 0, count = 0;  // corners (Gear::order)
    bool skip = false;         // bound to a skeleton wheel node (or a mixed piece)
    int owner = -1;            // wheel / loop it belongs to
};

float wrapPi(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a <= -kPi) a += 2 * kPi;
    return a;
}

void setX(DriveAttr& d, std::uint8_t kind, float x) {
    const long q = std::clamp(std::lround((double(x) + 128.0) * 256.0), 0L, 65535L);
    d.b[0] = kind;
    d.b[1] = std::uint8_t(q >> 8);
    d.b[2] = std::uint8_t(q & 0xFF);
    d.b[3] = 0;
}

}  // namespace

DriveGear planDriveGear(const std::vector<const formats::MeshPart*>& parts, const std::vector<std::uint8_t>& skipJoint) {
    DriveGear g;
    g.parts.resize(parts.size());
    // All corners of all parts, one index space.
    std::vector<const formats::MeshVertex*> vx;
    std::vector<std::pair<int, int>> where;  // part, corner
    for (std::size_t p = 0; p < parts.size(); ++p) {
        if (!parts[p]) continue;
        for (std::size_t i = 0; i < parts[p]->tris.size(); ++i) {
            vx.push_back(&parts[p]->tris[i]);
            where.emplace_back(int(p), int(i));
        }
    }
    const std::size_t n = vx.size();
    if (n < 3 || n > 4'000'000) return g;

    // 1. Islands: corners of a triangle and corners at the same position (1/64 px) belong together.
    std::vector<int> par(n);
    std::iota(par.begin(), par.end(), 0);
    std::unordered_map<std::uint64_t, int> weld;
    weld.reserve(n);
    auto q = [](float x) { return std::uint64_t(std::int64_t(std::lround(double(x) * 64.0)) & 0x1FFFFF); };
    for (std::size_t i = 0; i < n; ++i) {
        const float* p = vx[i]->pos;
        const auto [it, added] = weld.try_emplace(q(p[0]) | q(p[1]) << 21 | q(p[2]) << 42, int(i));
        if (!added) par[std::size_t(root(par, int(i)))] = root(par, it->second);
    }
    weld.clear();
    for (std::size_t p = 0, base = 0; p < parts.size(); ++p) {
        const std::size_t cnt = parts[p] ? parts[p]->tris.size() : 0;
        for (std::size_t t = 0; t + 2 < cnt; t += 3) {
            const int a = root(par, int(base + t));
            par[std::size_t(root(par, int(base + t + 1)))] = a;
            par[std::size_t(root(par, int(base + t + 2)))] = a;
        }
        base += cnt;
    }
    std::vector<int> isl(n), rootIsl(n, -1);
    std::vector<Island> is;
    for (std::size_t i = 0; i < n; ++i) {
        const int r = root(par, int(i));
        if (rootIsl[std::size_t(r)] < 0) rootIsl[std::size_t(r)] = int(is.size()), is.emplace_back();
        isl[i] = rootIsl[std::size_t(r)];
    }
    std::vector<int> order(n);
    {
        std::vector<int> cnt(is.size() + 1, 0);
        for (std::size_t i = 0; i < n; ++i) ++cnt[std::size_t(isl[i]) + 1];
        for (std::size_t k = 1; k < cnt.size(); ++k) cnt[k] += cnt[k - 1];
        for (std::size_t k = 0; k < is.size(); ++k) is[k].first = cnt[k], is[k].count = cnt[k + 1] - cnt[k];
        for (std::size_t i = 0; i < n; ++i) order[std::size_t(cnt[std::size_t(isl[i])]++)] = int(i);
    }
    float ground = 1e30f, top = -1e30f;
    for (Island& s : is) {
        int joint = -1;
        for (int k = 0; k < s.count; ++k) {
            const formats::MeshVertex& v = *vx[std::size_t(order[std::size_t(s.first + k)])];
            for (int a = 0; a < 3; ++a) s.lo[a] = std::min(s.lo[a], v.pos[a]), s.hi[a] = std::max(s.hi[a], v.pos[a]), s.c[a] += v.pos[a];
            const int j = v.weight[0] ? int(v.joint[0]) : -1;
            if (j >= 0 && std::size_t(j) < skipJoint.size() && skipJoint[std::size_t(j)]) s.skip = true;
            if (k == 0) joint = j;
            else if (j != joint) s.skip = true;  // a skinned / mixed piece: not ours to move
        }
        for (float& c : s.c) c /= float(std::max(1, s.count));
        for (int k = 0; k < s.count; ++k) {
            const float* p = vx[std::size_t(order[std::size_t(s.first + k)])]->pos;
            s.rad = std::max(s.rad, std::hypot(p[1] - s.c[1], p[2] - s.c[2]));
        }
        ground = std::min(ground, s.lo[1]);
        top = std::max(top, s.hi[1]);
    }
    if (!(top > ground)) return g;
    auto attrOf = [&](int corner) -> DriveAttr& {
        const auto [p, i] = where[std::size_t(corner)];
        auto& v = g.parts[std::size_t(p)];
        if (v.empty()) v.resize(parts[std::size_t(p)]->tris.size());
        return v[std::size_t(i)];
    };

    // 2. Wheels: round pieces about the x axis standing on the ground; then what is inside them.
    struct Wheel {
        float y, z, r, x0, x1;
        int island;
    };
    std::vector<Wheel> wheels;
    for (std::size_t k = 0; k < is.size(); ++k) {
        const Island& s = is[k];
        if (s.skip) continue;
        const float ey = s.hi[1] - s.lo[1], ez = s.hi[2] - s.lo[2], ex = s.hi[0] - s.lo[0];
        const float r = (ey + ez) / 4;
        if (r < 0.8f || std::abs(ey - ez) > 0.1f * std::max(ey, ez) || ex > 2.5f * r) continue;
        if (s.lo[1] - ground > 0.25f * r + 0.5f) continue;
        const float cy = 0.5f * (s.lo[1] + s.hi[1]), cz = 0.5f * (s.lo[2] + s.hi[2]);
        float far = 0;
        for (int i = 0; i < s.count; ++i) {
            const float* p = vx[std::size_t(order[std::size_t(s.first + i)])]->pos;
            far = std::max(far, std::hypot(p[1] - cy, p[2] - cz));
        }
        if (far > 1.1f * r) continue;  // a box, not a disc
        wheels.push_back({cy, cz, r, s.lo[0], s.hi[0], int(k)});
    }
    std::sort(wheels.begin(), wheels.end(), [](const Wheel& a, const Wheel& b) { return a.r > b.r; });
    for (std::size_t w = 0; w < wheels.size(); ++w) is[std::size_t(wheels[w].island)].owner = int(w);
    for (std::size_t k = 0; k < is.size(); ++k) {
        Island& s = is[k];
        if (s.skip || s.owner >= 0) continue;
        const float xc = 0.5f * (s.lo[0] + s.hi[0]);
        for (std::size_t w = 0; w < wheels.size(); ++w) {
            const Wheel& e = wheels[w];
            if (xc < e.x0 || xc > e.x1 || s.lo[0] < e.x0 - 0.3f * e.r || s.hi[0] > e.x1 + 0.3f * e.r) continue;
            if (s.lo[1] < e.y - e.r * 1.04f || s.hi[1] > e.y + e.r * 1.04f || s.lo[2] < e.z - e.r * 1.04f || s.hi[2] > e.z + e.r * 1.04f)
                continue;
            bool inside = true;
            for (int i = 0; i < s.count && inside; ++i) {
                const float* p = vx[std::size_t(order[std::size_t(s.first + i)])]->pos;
                inside = std::hypot(p[1] - e.y, p[2] - e.z) <= e.r * 1.04f;
            }
            if (inside) {
                s.owner = int(w);
                break;
            }
        }
    }
    for (std::size_t k = 0; k < is.size(); ++k) {
        const Island& s = is[k];
        if (s.owner < 0) continue;
        const Wheel& e = wheels[std::size_t(s.owner)];
        for (int i = 0; i < s.count; ++i) {
            DriveAttr& d = attrOf(order[std::size_t(s.first + i)]);
            d.a[0] = e.y, d.a[1] = e.z, d.a[2] = e.r, d.a[3] = 0;
            setX(d, 1, 0.5f * (e.x0 + e.x1));
        }
    }
    g.wheels = int(wheels.size());
    for (const Wheel& e : wheels) g.contacts.push_back({e.x0, e.x1, e.z - e.r, e.z + e.r, false});

    // 3. Track links: equal small pieces (lateral extent, size) in loops round the side plane.
    std::vector<int> cand;
    const float height = top - ground;
    for (std::size_t k = 0; k < is.size(); ++k) {
        const Island& s = is[k];
        if (!s.skip && s.owner < 0 && s.count >= 6 && s.rad > 0.05f && s.rad < 0.12f * height) cand.push_back(int(k));
    }
    // Groups: the same lateral extent (lo.x, hi.x within 0.08 px) and size (rad within 15 %).
    std::stable_sort(cand.begin(), cand.end(), [&](int a, int b) { return is[std::size_t(a)].lo[0] < is[std::size_t(b)].lo[0]; });
    std::vector<std::vector<int>> groups;
    for (const int k : cand) {
        const Island& s = is[std::size_t(k)];
        bool put = false;
        for (auto it = groups.rbegin(); it != groups.rend(); ++it) {
            const Island& f = is[std::size_t(it->front())];
            if (f.lo[0] < s.lo[0] - 0.5f) break;  // (groups start in lo.x order)
            if (std::abs(f.lo[0] - s.lo[0]) <= 0.08f && std::abs(f.hi[0] - s.hi[0]) <= 0.08f &&
                std::abs(f.rad - s.rad) <= 0.15f * std::max(f.rad, s.rad)) {
                it->push_back(k);
                put = true;
                break;
            }
        }
        if (!put) groups.push_back({k});
    }
    for (const auto& grp : groups) {
        if (grp.size() < 8) continue;
        // Loops: single-linkage clusters in the side plane (4 x the typical spacing).
        const std::size_t m = grp.size();
        std::vector<float> nn(m, 1e30f);
        for (std::size_t a = 0; a < m; ++a)
            for (std::size_t b = 0; b < m; ++b)
                if (a != b) {
                    const Island &p = is[std::size_t(grp[a])], &r = is[std::size_t(grp[b])];
                    nn[a] = std::min(nn[a], std::hypot(p.c[1] - r.c[1], p.c[2] - r.c[2]));
                }
        std::vector<float> sorted = nn;
        std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(m / 2), sorted.end());
        const float link = 4.0f * sorted[m / 2];
        std::vector<int> lab(m, -1);
        for (std::size_t a = 0; a < m; ++a) {
            if (lab[a] >= 0) continue;
            std::vector<std::size_t> st{a};
            lab[a] = int(a);
            while (!st.empty()) {
                const std::size_t x = st.back();
                st.pop_back();
                for (std::size_t b = 0; b < m; ++b) {
                    if (lab[b] >= 0) continue;
                    const Island &p = is[std::size_t(grp[x])], &r = is[std::size_t(grp[b])];
                    if (std::hypot(p.c[1] - r.c[1], p.c[2] - r.c[2]) < link) lab[b] = int(a), st.push_back(b);
                }
            }
        }
        for (std::size_t L = 0; L < m; ++L) {
            std::vector<int> loop;
            for (std::size_t a = 0; a < m; ++a)
                if (lab[a] == int(L)) loop.push_back(grp[a]);
            const std::size_t c = loop.size();
            if (c < 8) continue;
            float cy = 0, cz = 0, ylo = 1e30f, yhi = -1e30f, zlo = 1e30f, zhi = -1e30f, xs = 0;
            for (const int k : loop) {
                const Island& s = is[std::size_t(k)];
                cy += s.c[1], cz += s.c[2], xs += s.c[0];
                ylo = std::min(ylo, s.c[1]), yhi = std::max(yhi, s.c[1]), zlo = std::min(zlo, s.c[2]), zhi = std::max(zhi, s.c[2]);
            }
            cy /= float(c), cz /= float(c), xs /= float(c);
            const Island& first = is[std::size_t(loop.front())];
            const DriveContact contact{first.lo[0], first.hi[0], zlo, zhi, true};
            // Round the loop in the direction of travel (the top runs forward, -z): angle about +x falling.
            std::vector<std::pair<float, int>> ang;
            for (const int k : loop) ang.emplace_back(std::atan2(is[std::size_t(k)].c[2] - cz, is[std::size_t(k)].c[1] - cy), k);
            std::sort(ang.begin(), ang.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            std::vector<float> py(c), pz(c), dist(c);
            for (std::size_t i = 0; i < c; ++i) py[i] = is[std::size_t(ang[i].second)].c[1], pz[i] = is[std::size_t(ang[i].second)].c[2];
            for (std::size_t i = 0; i < c; ++i) dist[i] = std::hypot(py[(i + 1) % c] - py[i], pz[(i + 1) % c] - pz[i]);
            std::vector<float> ds = dist;
            std::nth_element(ds.begin(), ds.begin() + std::ptrdiff_t(c / 2), ds.end());
            const float pitch = ds[c / 2];
            int gaps = 0, regular = 0;
            for (const float d : dist) gaps += d > 1.8f * pitch, regular += d >= 0.6f * pitch && d <= 1.5f * pitch;
            // A loop: mostly regular spacing, a few gaps (open runs), round something, down to the ground.
            if (pitch < 0.05f || gaps > 4 || float(regular) < 0.75f * float(c) || yhi - ylo < 3 * pitch || zhi - zlo < 3 * pitch ||
                ylo - ground > 0.35f * (yhi - ylo) + 1.0f)
                continue;
            auto gapAfter = [&](std::size_t i) { return dist[i] > 1.8f * pitch; };
            auto tangent = [&](std::size_t i, float& ty, float& tz) {
                const std::size_t s = (i + 1) % c, p = (i + c - 1) % c;
                const bool gn = gapAfter(i), gp = gapAfter(p);
                if (gn && gp) return false;
                ty = (gn ? py[i] : py[s]) - (gp ? py[i] : py[p]);
                tz = (gn ? pz[i] : pz[s]) - (gp ? pz[i] : pz[p]);
                return std::hypot(ty, tz) > 1e-6f;
            };
            for (std::size_t i = 0; i < c; ++i) {
                const std::size_t s = (i + 1) % c;
                float ty = 0, tz = 0;
                if (!tangent(i, ty, tz)) continue;  // a lone link: stays
                DriveAttr d;
                d.a[3] = pitch;
                if (gapAfter(i)) {  // the end of an open run: slides on along it
                    const float l = std::hypot(ty, tz);
                    d.a[0] = ty / l * pitch, d.a[1] = tz / l * pitch;
                    setX(d, 3, xs);
                } else {
                    float sy = 0, sz = 0;
                    if (!tangent(s, sy, sz)) sy = ty, sz = tz;
                    const float dt = wrapPi(std::atan2(sz, sy) - std::atan2(tz, ty));
                    if (std::abs(dt) < 0.02f) {
                        d.a[0] = py[s] - py[i], d.a[1] = pz[s] - pz[i];
                        setX(d, 3, xs);
                    } else {
                        // The rigid motion link i -> link s: rotation by dt about P, (I - R) P = c_s - R c_i.
                        const float co = std::cos(dt), si = std::sin(dt);
                        const float ry = py[s] - (co * py[i] - si * pz[i]), rz = pz[s] - (si * py[i] + co * pz[i]);
                        const float det = 2 - 2 * co;
                        d.a[0] = ((1 - co) * ry - si * rz) / det;
                        d.a[1] = (si * ry + (1 - co) * rz) / det;
                        d.a[2] = dt;
                        setX(d, 2, xs);
                    }
                }
                const Island& isl2 = is[std::size_t(ang[i].second)];
                for (int k = 0; k < isl2.count; ++k) attrOf(order[std::size_t(isl2.first + k)]) = d;
                ++g.links;
            }
            ++g.loops;
            g.contacts.push_back(contact);
        }
    }
    return g;
}

}  // namespace d2k::render3d

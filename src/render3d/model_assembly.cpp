// Assemble build-up: a building model's parts and their order (render3d/model_assembly.hpp,
// docs/3d-mode.md "Assemble build-up").
#include "render3d/model_assembly.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <unordered_map>

namespace d2k::render3d {

namespace {

using render::AssembleMotion;

struct Box {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    void add(const float* p) {
        for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], p[k]), hi[k] = std::max(hi[k], p[k]);
    }
    void add(const Box& b) {
        for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], b.lo[k]), hi[k] = std::max(hi[k], b.hi[k]);
    }
    float size(int k) const { return std::max(hi[k] - lo[k], 0.0f); }
    float diag() const { return std::sqrt(size(0) * size(0) + size(1) * size(1) + size(2) * size(2)); }
    float centre(int k) const { return 0.5f * (lo[k] + hi[k]); }
    bool holds(const float* p, float m) const {
        for (int k = 0; k < 3; ++k)
            if (p[k] < lo[k] - m || p[k] > hi[k] + m) return false;
        return true;
    }
    // Distance between two boxes (0 when they touch / overlap).
    float gap(const Box& b) const {
        float s = 0;
        for (int k = 0; k < 3; ++k) {
            const float d = std::max({lo[k] - b.hi[k], b.lo[k] - hi[k], 0.0f});
            s += d * d;
        }
        return std::sqrt(s);
    }
    float gapTo(const float* p) const {
        float s = 0;
        for (int k = 0; k < 3; ++k) {
            const float d = std::max({lo[k] - p[k], p[k] - hi[k], 0.0f});
            s += d * d;
        }
        return std::sqrt(s);
    }
};

int findRoot(std::vector<int>& par, int x) {
    while (par[std::size_t(x)] != x) {
        par[std::size_t(x)] = par[std::size_t(par[std::size_t(x)])];
        x = par[std::size_t(x)];
    }
    return x;
}

void centroid(const formats::MeshVertex* v, float c[3]) {
    for (int k = 0; k < 3; ++k) c[k] = (v[0].pos[k] + v[1].pos[k] + v[2].pos[k]) / 3.0f;
}

}  // namespace

AssemblyPlan planAssembly(const formats::Model3D& m) {
    AssemblyPlan plan;
    const auto& tris = m.body.tris;
    const std::size_t nt = tris.size() / 3;
    if (nt == 0 || nt > 65535u * 8u) return plan;

    // 1. Islands: triangles that share a corner position (welded to 1/64 px) belong together.
    std::unordered_map<std::uint64_t, int> weld;
    weld.reserve(nt * 2);
    std::vector<int> corner(nt * 3);
    auto q = [](float x) { return std::uint64_t(std::int64_t(std::lround(double(x) * 64.0)) & 0x1FFFFF); };
    for (std::size_t i = 0; i < nt * 3; ++i) {
        const float* p = tris[i].pos;
        const std::uint64_t key = q(p[0]) | q(p[1]) << 21 | q(p[2]) << 42;
        corner[i] = weld.try_emplace(key, int(weld.size())).first->second;
    }
    std::vector<int> par(weld.size());
    std::iota(par.begin(), par.end(), 0);
    for (std::size_t t = 0; t < nt; ++t) {
        const int a = findRoot(par, corner[t * 3]);
        const int b = findRoot(par, corner[t * 3 + 1]);
        const int c = findRoot(par, corner[t * 3 + 2]);
        par[std::size_t(b)] = a;
        par[std::size_t(findRoot(par, c))] = a;
    }
    std::unordered_map<int, int> islandOf;
    std::vector<int> triIsland(nt);
    std::vector<Box> box;
    std::vector<int> count;
    Box all;
    for (std::size_t t = 0; t < nt; ++t) {
        const int r = findRoot(par, corner[t * 3]);
        const auto [it, added] = islandOf.try_emplace(r, int(box.size()));
        if (added) box.emplace_back(), count.push_back(0);
        const int id = it->second;
        triIsland[t] = id;
        for (int k = 0; k < 3; ++k) box[std::size_t(id)].add(tris[t * 3 + std::size_t(k)].pos);
        ++count[std::size_t(id)];
    }
    for (const Box& b : box) all.add(b);
    const float modelDiag = std::max(all.diag(), 1.0f);
    const float top = std::max(all.hi[1], 1.0f);

    // 2. Groups: tiny islands go into the island that holds them (bolts, panels, rails on a block),
    //    then the smallest groups merge into their nearest neighbour until 6..22 remain.
    const int n0 = int(box.size());
    std::vector<int> group(static_cast<std::size_t>(n0), -1);
    std::vector<int> order(static_cast<std::size_t>(n0));
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return box[std::size_t(a)].diag() > box[std::size_t(b)].diag(); });
    const float tinyDiag = std::max(3.0f, 0.07f * modelDiag);
    std::vector<int> anchors;
    for (int i : order)
        if (box[std::size_t(i)].diag() >= tinyDiag && count[std::size_t(i)] >= 8) anchors.push_back(i);
    if (anchors.empty()) anchors.push_back(order.front());
    struct Group { Box b; int tris = 0; bool alive = true; };
    std::vector<Group> groups;
    for (int a : anchors) {
        group[std::size_t(a)] = int(groups.size());
        groups.push_back({box[std::size_t(a)], count[std::size_t(a)], true});
    }
    for (int i : order) {
        if (group[std::size_t(i)] >= 0) continue;
        float c[3] = {box[std::size_t(i)].centre(0), box[std::size_t(i)].centre(1), box[std::size_t(i)].centre(2)};
        int best = -1;
        float bestSize = 1e30f, bestGap = 1e30f;
        for (std::size_t g = 0; g < groups.size(); ++g) {  // the smallest group box that holds it ...
            const Box& b = box[std::size_t(anchors[g])];
            if (b.holds(c, 1.0f) && b.diag() < bestSize) best = int(g), bestSize = b.diag();
        }
        if (best < 0)  // ... else the nearest
            for (std::size_t g = 0; g < groups.size(); ++g) {
                const float d = box[std::size_t(anchors[g])].gap(box[std::size_t(i)]);
                if (d < bestGap) best = int(g), bestGap = d;
            }
        group[std::size_t(i)] = best;
        groups[std::size_t(best)].b.add(box[std::size_t(i)]);
        groups[std::size_t(best)].tris += count[std::size_t(i)];
    }
    const int target = std::clamp(int(std::lround(std::sqrt(double(nt)) / 7.0)), 6, 22);
    int alive = int(groups.size());
    std::vector<int> remap(groups.size());
    std::iota(remap.begin(), remap.end(), 0);
    while (alive > target) {
        int s = -1;
        for (std::size_t g = 0; g < groups.size(); ++g)
            if (groups[g].alive && (s < 0 || groups[g].b.diag() < groups[std::size_t(s)].b.diag())) s = int(g);
        int best = -1;
        float bestCost = 1e30f;
        const Box& sb = groups[std::size_t(s)].b;
        for (std::size_t g = 0; g < groups.size(); ++g) {
            if (!groups[g].alive || int(g) == s) continue;
            const Box& b = groups[g].b;
            const float cost = sb.gap(b) + 0.5f * std::fabs(sb.lo[1] - b.lo[1]) + 0.05f * b.diag();
            if (cost < bestCost) best = int(g), bestCost = cost;
        }
        if (best < 0) break;
        groups[std::size_t(best)].b.add(sb);
        groups[std::size_t(best)].tris += groups[std::size_t(s)].tris;
        groups[std::size_t(s)].alive = false;
        for (int& r : remap)
            if (r == s) r = best;
        --alive;
    }
    std::vector<int> triPart(nt);
    std::vector<int> liveIndex(groups.size(), -1);
    std::vector<Box> parts;
    for (std::size_t g = 0; g < groups.size(); ++g)
        if (groups[g].alive) liveIndex[g] = int(parts.size()), parts.push_back(groups[g].b);
    for (std::size_t t = 0; t < nt; ++t) triPart[t] = liveIndex[std::size_t(remap[std::size_t(group[std::size_t(triIsland[t])])])];

    // 3. One piece: height layers that extrude in turn (a rising cut with a glowing edge).
    bool layered = false;
    if (parts.size() < 2) {
        const int layers = std::clamp(int(top / 7.0f), 3, 6);
        parts.assign(std::size_t(layers), Box{});
        for (std::size_t t = 0; t < nt; ++t) {
            float c[3];
            centroid(&tris[t * 3], c);
            const int l = std::clamp(int(c[1] / top * float(layers)), 0, layers - 1);
            triPart[t] = l;
            for (int k = 0; k < 3; ++k) parts[std::size_t(l)].add(tris[t * 3 + std::size_t(k)].pos);
        }
        std::vector<int> keep(parts.size(), -1);
        std::vector<Box> nonEmpty;
        for (std::size_t l = 0; l < parts.size(); ++l)
            if (parts[l].hi[1] >= parts[l].lo[1]) keep[l] = int(nonEmpty.size()), nonEmpty.push_back(parts[l]);
        for (int& p : triPart) p = keep[std::size_t(p)];
        parts = nonEmpty;
        layered = true;
    }

    // 4. How each part arrives.
    const float cx = all.centre(0), cz = all.centre(2);
    const float ex = std::max(all.size(0), 1.0f), ez = std::max(all.size(2), 1.0f);
    const float halfPlan = 0.5f * std::sqrt(ex * ex + ez * ez);
    const float groundY = all.lo[1] + 2.0f + 0.04f * top;
    std::vector<AssemblyPart> out(parts.size());
    std::vector<float> key(parts.size());
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const Box& b = parts[i];
        AssemblyPart& p = out[i];
        std::memcpy(p.lo, b.lo, sizeof p.lo);
        std::memcpy(p.hi, b.hi, sizeof p.hi);
        p.pivot[0] = b.centre(0);
        p.pivot[1] = b.lo[1];
        p.pivot[2] = b.centre(2);
        const float dx = b.size(0), dy = b.size(1), dz = b.size(2), wide = std::max(dx, dz);
        const float ox = b.centre(0) - cx, oz = b.centre(2) - cz;
        const float rn = std::sqrt(ox * ox + oz * oz) / std::max(halfPlan, 1.0f);
        p.dirAngle = rn > 0.05f ? std::atan2(oz, ox) : 1.5707963f;  // the centre: towards the front (+z)
        const bool ground = b.lo[1] <= groundY;
        const bool thin = dy > 2.0f * wide && dy > 10.0f;
        float late = 0;  // later in the order than its height alone says
        if (layered) {
            p.motion = AssembleMotion::Extrude;
        } else if (ground) {
            if (dx * dz >= 0.12f * ex * ez || rn < 0.35f) {
                p.motion = AssembleMotion::Extrude;
            } else if (thin) {
                p.motion = AssembleMotion::Hinge;
                late = 0.3f;
            } else {
                p.motion = AssembleMotion::Slide;
                late = 0.35f;
            }
        } else if (thin) {
            p.motion = rn > 0.5f ? AssembleMotion::Hinge : AssembleMotion::Telescope;
        } else if (wide >= 0.4f * std::max(ex, ez) || dy < 0.7f * wide) {
            p.motion = AssembleMotion::Lower;
        } else {
            p.motion = AssembleMotion::Telescope;
        }
        switch (p.motion) {
        case AssembleMotion::Extrude:
        case AssembleMotion::Telescope: p.param = std::max(dy, 0.5f); break;
        case AssembleMotion::Lower: p.param = std::clamp(1.2f * dy + 10.0f, 12.0f, 40.0f); break;
        case AssembleMotion::Slide: p.param = std::clamp(0.8f * wide + 4.0f, 6.0f, 0.5f * std::max(ex, ez)); break;
        case AssembleMotion::Hinge: p.param = 0; break;
        }
        key[i] = b.lo[1] + 0.25f * dy + late * top + 0.05f * rn * top;
    }
    std::vector<int> ord(out.size());
    std::iota(ord.begin(), ord.end(), 0);
    std::stable_sort(ord.begin(), ord.end(), [&](int a, int b) { return key[std::size_t(a)] < key[std::size_t(b)]; });
    std::vector<int> rank(out.size());
    for (std::size_t i = 0; i < ord.size(); ++i) rank[std::size_t(ord[i])] = int(i);
    plan.parts.resize(out.size());
    for (std::size_t i = 0; i < out.size(); ++i) plan.parts[std::size_t(rank[i])] = out[i];

    // 5. Timing: staggered, overlapping windows; a part never starts before what it stands on has
    //    nearly arrived; then stretched / squeezed onto [kAssembleParts0, kAssembleParts1].
    const std::size_t n = plan.parts.size();
    const float span = render::kAssembleParts1 - render::kAssembleParts0;
    const float D = std::clamp(2.6f * span / float(n), 0.07f, 0.30f);
    float end = 0;
    for (std::size_t i = 0; i < n; ++i) {
        AssemblyPart& p = plan.parts[i];
        p.t0 = n > 1 ? (span - D) * float(i) / float(n - 1) : 0.0f;
        p.dur = D * (p.motion == AssembleMotion::Extrude && (p.hi[1] - p.lo[1]) > 0.5f * top ? 1.3f : 1.0f);
        for (std::size_t j = 0; j < i; ++j) {
            const AssemblyPart& s = plan.parts[j];
            const bool under = s.hi[1] <= p.lo[1] + 2.0f && s.lo[0] < p.hi[0] && s.hi[0] > p.lo[0] && s.lo[2] < p.hi[2] &&
                               s.hi[2] > p.lo[2];
            if (under) p.t0 = std::max(p.t0, s.t0 + 0.85f * s.dur);
        }
        end = std::max(end, p.t0 + p.dur);
    }
    const float k = end > 0 ? span / end : 1.0f;
    for (auto& p : plan.parts) {
        p.t0 = render::kAssembleParts0 + p.t0 * k;
        p.dur = std::max(p.dur * k, 0.02f);
    }
    plan.bodyTris.resize(nt);
    for (std::size_t t = 0; t < nt; ++t) plan.bodyTris[t] = std::uint16_t(rank[std::size_t(triPart[t])]);
    return plan;
}

int assemblyPartAt(const AssemblyPlan& p, const float c[3]) {
    int best = -1;
    float bestSize = 1e30f;
    for (std::size_t i = 0; i < p.parts.size(); ++i) {
        Box b;
        std::memcpy(b.lo, p.parts[i].lo, sizeof b.lo);
        std::memcpy(b.hi, p.parts[i].hi, sizeof b.hi);
        if (b.holds(c, 0.5f) && b.diag() < bestSize) best = int(i), bestSize = b.diag();
    }
    if (best >= 0) return best;
    float bestGap = 1e30f;
    for (std::size_t i = 0; i < p.parts.size(); ++i) {
        Box b;
        std::memcpy(b.lo, p.parts[i].lo, sizeof b.lo);
        std::memcpy(b.hi, p.parts[i].hi, sizeof b.hi);
        if (const float g = b.gapTo(c); g < bestGap) best = int(i), bestGap = g;
    }
    return best;
}

void assemblyVertexAttrs(const AssemblyPart& part, float a[4], std::uint8_t b[4]) {
    a[0] = part.pivot[0];
    a[1] = part.pivot[1];
    a[2] = part.pivot[2];
    a[3] = part.param;
    auto byte = [](float x) { return std::uint8_t(std::clamp(int(std::lround(x * 255.0f)), 0, 255)); };
    b[0] = byte(part.t0);
    b[1] = byte(part.dur);
    b[2] = std::uint8_t(int(part.motion) * 32);
    float u = part.dirAngle / 6.2831853f;
    u -= std::floor(u);
    b[3] = std::uint8_t(1 + std::clamp(int(std::lround(u * 254.0f)), 0, 254));
}

}  // namespace d2k::render3d

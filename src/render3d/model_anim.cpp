#include "render3d/model_anim.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace d2k::render3d {

using Skeleton = formats::Model3D::Skeleton;

void mat4Mul(const float a[16], const float b[16], float out[16]) {
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = s;
        }
    std::memcpy(out, r, sizeof r);
}

bool mat4Inverse(const float m[16], float out[16]) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] -
             m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] +
             m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] -
             m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] +
              m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] +
             m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] +
             m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] -
              m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] -
             m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] +
             m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] -
              m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] +
              m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] +
             m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] -
             m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] +
              m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::abs(det) < 1e-30f) return false;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] / det;
    return true;
}

void mat4FromTrs(const float t[3], const float r[4], const float s[3], float out[16]) {
    const float x = r[0], y = r[1], z = r[2], w = r[3];
    const float xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
    out[0] = (1 - 2 * (yy + zz)) * s[0];
    out[1] = (2 * (xy + wz)) * s[0];
    out[2] = (2 * (xz - wy)) * s[0];
    out[3] = 0;
    out[4] = (2 * (xy - wz)) * s[1];
    out[5] = (1 - 2 * (xx + zz)) * s[1];
    out[6] = (2 * (yz + wx)) * s[1];
    out[7] = 0;
    out[8] = (2 * (xz + wy)) * s[2];
    out[9] = (2 * (yz - wx)) * s[2];
    out[10] = (1 - 2 * (xx + yy)) * s[2];
    out[11] = 0;
    out[12] = t[0];
    out[13] = t[1];
    out[14] = t[2];
    out[15] = 1;
}

namespace {

struct Trs {
    float t[3], r[4], s[3];
};

// The channel's value at `time` (n = 3 or 4 components) into v.
void sample(const Skeleton::Channel& c, float time, float* v) {
    const int n = c.path == 1 ? 4 : 3;
    const std::size_t keys = c.times.size();
    if (!keys || c.values.size() < keys * std::size_t(n)) return;
    if (time <= c.times.front() || keys == 1) {
        std::memcpy(v, &c.values[0], sizeof(float) * std::size_t(n));
        return;
    }
    if (time >= c.times.back()) {
        std::memcpy(v, &c.values[(keys - 1) * std::size_t(n)], sizeof(float) * std::size_t(n));
        return;
    }
    const std::size_t k1 = std::size_t(std::upper_bound(c.times.begin(), c.times.end(), time) - c.times.begin());
    const std::size_t k0 = k1 - 1;
    const float* a = &c.values[k0 * std::size_t(n)];
    const float* b = &c.values[k1 * std::size_t(n)];
    const float span = c.times[k1] - c.times[k0];
    const float u = c.step || span <= 0 ? 0.0f : (time - c.times[k0]) / span;
    if (u <= 0) {
        std::memcpy(v, a, sizeof(float) * std::size_t(n));
        return;
    }
    if (n == 4) {  // normalised lerp on the shorter arc (keys are close: one game frame apart)
        const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
        const float sb = d < 0 ? -1.0f : 1.0f;
        float l = 0;
        for (int i = 0; i < 4; ++i) {
            v[i] = a[i] * (1 - u) + b[i] * sb * u;
            l += v[i] * v[i];
        }
        l = std::sqrt(l);
        if (l > 1e-12f)
            for (int i = 0; i < 4; ++i) v[i] /= l;
        return;
    }
    for (int i = 0; i < 3; ++i) v[i] = a[i] * (1 - u) + b[i] * u;
}

void worldOf(const Skeleton& sk, const std::vector<Trs>& local, std::vector<float>& world) {
    const std::size_t n = sk.nodes.size();
    world.assign(n * 16, 0.0f);
    std::vector<char> done(n, 0);
    // Parents first (any order in the file): resolve each chain once.
    std::vector<int> chain;
    for (std::size_t i = 0; i < n; ++i) {
        chain.clear();
        for (int k = int(i); k >= 0 && !done[std::size_t(k)]; k = sk.nodes[std::size_t(k)].parent) {
            chain.push_back(k);
            if (chain.size() > n) break;  // a cycle (invalid file)
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const std::size_t k = std::size_t(*it);
            float m[16];
            mat4FromTrs(local[k].t, local[k].r, local[k].s, m);
            const int p = sk.nodes[k].parent;
            if (p >= 0 && done[std::size_t(p)]) mat4Mul(&world[std::size_t(p) * 16], m, &world[k * 16]);
            else std::memcpy(&world[k * 16], m, sizeof m);
            done[k] = 1;
        }
    }
}

std::vector<Trs> restLocal(const Skeleton& sk) {
    std::vector<Trs> local(sk.nodes.size());
    for (std::size_t i = 0; i < sk.nodes.size(); ++i) {
        std::memcpy(local[i].t, sk.nodes[i].t, sizeof local[i].t);
        std::memcpy(local[i].r, sk.nodes[i].r, sizeof local[i].r);
        std::memcpy(local[i].s, sk.nodes[i].s, sizeof local[i].s);
    }
    return local;
}

}  // namespace

void restWorld(const Skeleton& sk, std::vector<float>& out) { worldOf(sk, restLocal(sk), out); }

void posePalette(const Skeleton& sk, int anim, float time, std::vector<float>& out) {
    const std::size_t n = sk.nodes.size();
    const std::size_t base = out.size();
    out.resize(base + n * 16, 0.0f);
    static const float kId[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (anim < 0 || anim >= int(sk.animations.size())) {
        for (std::size_t i = 0; i < n; ++i) std::memcpy(&out[base + i * 16], kId, sizeof kId);
        return;
    }
    std::vector<Trs> local = restLocal(sk);
    std::vector<float> rest, posed;
    worldOf(sk, local, rest);
    for (const auto& c : sk.animations[std::size_t(anim)].channels) {
        if (c.node < 0 || std::size_t(c.node) >= n) continue;
        Trs& l = local[std::size_t(c.node)];
        sample(c, time, c.path == 0 ? l.t : c.path == 1 ? l.r : l.s);
    }
    worldOf(sk, local, posed);
    for (std::size_t i = 0; i < n; ++i) {
        // place * posed * inverse(place * rest)
        float pr[16], inv[16], pp[16], k[16];
        mat4Mul(sk.place, &rest[i * 16], pr);
        if (!mat4Inverse(pr, inv)) {
            // A node collapsed to zero size at rest (a part shown only in some frames, modelled at
            // 1/1000 and scaled up by its bone - docs/art-pipeline.md "Animated models"): its
            // vertices are skinned by their bone, so this entry is never used alone.
            std::memcpy(&out[base + i * 16], kId, sizeof kId);
            continue;
        }
        mat4Mul(sk.place, &posed[i * 16], pp);
        mat4Mul(pp, inv, k);
        std::memcpy(&out[base + i * 16], k, sizeof k);
    }
}

}  // namespace d2k::render3d

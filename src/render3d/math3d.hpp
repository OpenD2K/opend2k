#pragma once
// Minimal vector / matrix maths for the 3D mode (docs/3d-mode.md).
//
// World frame: right-handed, Y up. X = east (world pixels), Z = south (world pixels), Y = height
// (world pixels). A map point (wx, wy) at height h is Vec3{wx, h, wy}. Matrices are column-major
// (m[col * 4 + row]) like MSL float4x4 / GLSL mat4, multiplied as M * v. Clip space depth is 0..1
// (Metal / D3D / Vulkan convention of SDL_gpu).

#include <cmath>

namespace d2k::render3d {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) {
    const float l = length(a);
    return l > 1e-12f ? a * (1.0f / l) : Vec3{0, 1, 0};
}

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
};

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += at(rr, k) * o.at(k, c);
                r.at(rr, c) = s;
            }
        return r;
    }
    Vec4 operator*(const Vec4& v) const {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z + at(0, 3) * v.w,
                at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z + at(1, 3) * v.w,
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z + at(2, 3) * v.w,
                at(3, 0) * v.x + at(3, 1) * v.y + at(3, 2) * v.z + at(3, 3) * v.w};
    }
};

// Right-handed look-at (camera looks down -Z in view space).
inline Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& upHint) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, upHint));
    const Vec3 u = cross(s, f);
    Mat4 r;
    r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z; r.at(0, 3) = -dot(s, eye);
    r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z; r.at(1, 3) = -dot(u, eye);
    r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z; r.at(2, 3) = dot(f, eye);
    return r;
}

// Right-handed perspective with clip depth 0 (near) .. 1 (far).
inline Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
    const float t = 1.0f / std::tan(fovY * 0.5f);
    Mat4 r;
    r.m[0] = t / aspect;
    r.m[5] = t;
    r.at(2, 2) = zFar / (zNear - zFar);
    r.at(2, 3) = zFar * zNear / (zNear - zFar);
    r.at(3, 2) = -1;
    r.at(3, 3) = 0;
    return r;
}

// Orthographic projection, right-handed view space, clip depth 0 (near) .. 1 (far).
inline Mat4 orthoRH(float l, float r, float b, float t, float n, float f) {
    Mat4 m;
    m.at(0, 0) = 2 / (r - l);
    m.at(1, 1) = 2 / (t - b);
    m.at(2, 2) = -1 / (f - n);
    m.at(0, 3) = -(r + l) / (r - l);
    m.at(1, 3) = -(t + b) / (t - b);
    m.at(2, 3) = -n / (f - n);
    return m;
}

constexpr float kPi = 3.14159265358979323846f;
inline float radians(float deg) { return deg * kPi / 180.0f; }
inline float degrees(float rad) { return rad * 180.0f / kPi; }

}  // namespace d2k::render3d

// Small vector / matrix library used across the game.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

constexpr float PI = 3.14159265358979f;

struct vec2 {
    float x = 0, y = 0;
    vec2() = default;
    vec2(float a, float b) : x(a), y(b) {}
    vec2 operator+(vec2 o) const { return {x + o.x, y + o.y}; }
    vec2 operator-(vec2 o) const { return {x - o.x, y - o.y}; }
    vec2 operator*(float s) const { return {x * s, y * s}; }
    vec2& operator+=(vec2 o) { x += o.x; y += o.y; return *this; }
};
inline float dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(vec2 a) { return std::sqrt(dot(a, a)); }

struct vec3 {
    float x = 0, y = 0, z = 0;
    vec3() = default;
    vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    vec3 operator+(vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    vec3 operator*(vec3 o) const { return {x * o.x, y * o.y, z * o.z}; }
    vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    vec3 operator-() const { return {-x, -y, -z}; }
    vec3& operator+=(vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    vec3& operator-=(vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};
inline vec3 operator*(float s, vec3 v) { return v * s; }
inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(vec3 a) { return std::sqrt(dot(a, a)); }
inline vec3 normalize(vec3 a) { float l = length(a); return l > 1e-8f ? a / l : vec3(0, 1, 0); }
inline vec3 lerp(vec3 a, vec3 b, float t) { return a + (b - a) * t; }

struct vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    vec4() = default;
    vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    vec4(vec3 v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
};

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
inline float signf(float v) { return v < 0 ? -1.f : 1.f; }
inline float wrapAngle(float a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}
// exponential smoothing factor independent of framerate
inline float damp(float rate, float dt) { return 1.f - std::exp(-rate * dt); }

// Column-major 4x4 matrix (OpenGL convention).
struct mat4 {
    float m[16];
    mat4() { for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.f : 0.f; }
    float& operator()(int r, int c) { return m[c * 4 + r]; }
    float operator()(int r, int c) const { return m[c * 4 + r]; }
    mat4 operator*(const mat4& o) const {
        mat4 r;
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += (*this)(rr, k) * o(k, c);
                r(rr, c) = s;
            }
        return r;
    }
    vec4 operator*(vec4 v) const {
        return {m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
                m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
                m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
                m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w};
    }
    vec3 transformPoint(vec3 p) const { vec4 r = (*this) * vec4(p, 1.f); return {r.x, r.y, r.z}; }
    vec3 transformDir(vec3 p) const { vec4 r = (*this) * vec4(p, 0.f); return {r.x, r.y, r.z}; }
};

inline mat4 translate(vec3 t) { mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
inline mat4 scale(vec3 s) { mat4 r; r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r; }
inline mat4 rotateX(float a) { mat4 r; float c = std::cos(a), s = std::sin(a); r(1,1)=c; r(1,2)=-s; r(2,1)=s; r(2,2)=c; return r; }
inline mat4 rotateY(float a) { mat4 r; float c = std::cos(a), s = std::sin(a); r(0,0)=c; r(0,2)=s; r(2,0)=-s; r(2,2)=c; return r; }
inline mat4 rotateZ(float a) { mat4 r; float c = std::cos(a), s = std::sin(a); r(0,0)=c; r(0,1)=-s; r(1,0)=s; r(1,1)=c; return r; }
// Basis matrix from right/up/forward axes (columns) + origin.
inline mat4 basis(vec3 right, vec3 up, vec3 fwd, vec3 origin) {
    mat4 r;
    r.m[0] = right.x; r.m[1] = right.y; r.m[2] = right.z;
    r.m[4] = up.x; r.m[5] = up.y; r.m[6] = up.z;
    r.m[8] = fwd.x; r.m[9] = fwd.y; r.m[10] = fwd.z;
    r.m[12] = origin.x; r.m[13] = origin.y; r.m[14] = origin.z;
    return r;
}
inline mat4 perspective(float fovy, float aspect, float n, float f) {
    mat4 r;
    float t = 1.f / std::tan(fovy * 0.5f);
    for (float& v : r.m) v = 0;
    r(0,0) = t / aspect; r(1,1) = t; r(2,2) = (f + n) / (n - f); r(2,3) = 2 * f * n / (n - f); r(3,2) = -1;
    return r;
}
inline mat4 ortho(float l, float rr, float b, float t, float n, float f) {
    mat4 r;
    r(0,0) = 2 / (rr - l); r(1,1) = 2 / (t - b); r(2,2) = -2 / (f - n);
    r(0,3) = -(rr + l) / (rr - l); r(1,3) = -(t + b) / (t - b); r(2,3) = -(f + n) / (f - n);
    return r;
}
inline mat4 lookAt(vec3 eye, vec3 at, vec3 up) {
    vec3 f = normalize(at - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);
    mat4 r;
    r(0,0) = s.x; r(0,1) = s.y; r(0,2) = s.z;
    r(1,0) = u.x; r(1,1) = u.y; r(1,2) = u.z;
    r(2,0) = -f.x; r(2,1) = -f.y; r(2,2) = -f.z;
    r(0,3) = -dot(s, eye); r(1,3) = -dot(u, eye); r(2,3) = dot(f, eye);
    return r;
}
inline mat4 inverse(const mat4& mm) {
    const float* m = mm.m;
    float inv[16];
    inv[0] = m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    mat4 r;
    if (std::fabs(det) < 1e-12f) return r;
    det = 1.f / det;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det;
    return r;
}

// Deterministic RNG (xorshift) so the world is the same every launch.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1234567u) : s(seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() & 0xFFFFFF) / float(0x1000000); }
    float range(float a, float b) { return a + (b - a) * f(); }
    int irange(int a, int b) { return a + int(next() % uint32_t(b - a + 1)); }
};

// Value noise + fBm.
inline float hash2(int x, int y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / float(0xFFFFFF);
}
inline float valueNoise(float x, float y, uint32_t seed) {
    int xi = int(std::floor(x)), yi = int(std::floor(y));
    float fx = x - xi, fy = y - yi;
    float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed);
    float c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return lerpf(lerpf(a, b, u), lerpf(c, d, u), v) * 2.f - 1.f;
}
inline float fbm(float x, float y, int oct, uint32_t seed) {
    float sum = 0, amp = 0.5f, freq = 1, norm = 0;
    for (int i = 0; i < oct; i++) {
        sum += valueNoise(x * freq, y * freq, seed + i * 17) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return sum / norm;
}

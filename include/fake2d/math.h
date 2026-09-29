#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace fake2d {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2 &o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2 &o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }

    Vec2 &operator+=(const Vec2 &o) { x += o.x; y += o.y; return *this; }
    Vec2 &operator-=(const Vec2 &o) { x -= o.x; y -= o.y; return *this; }
    Vec2 &operator*=(float s) { x *= s; y *= s; return *this; }
    Vec2 &operator/=(float s) { x /= s; y /= s; return *this; }

    [[nodiscard]] float LengthSq() const { return x * x + y * y; }
    [[nodiscard]] float Length() const { return std::sqrt(LengthSq()); }

    [[nodiscard]] Vec2 Normalized() const {
        const float len = Length();
        return len > 1e-6f ? (*this / len) : Vec2{0.0f, 0.0f};
    }
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float x_, float y_, float w_, float h_)
        : x(x_), y(y_), width(w_), height(h_) {}

    [[nodiscard]] constexpr float Left() const { return x; }
    [[nodiscard]] constexpr float Top() const { return y; }
    [[nodiscard]] constexpr float Right() const { return x + width; }
    [[nodiscard]] constexpr float Bottom() const { return y + height; }

    [[nodiscard]] constexpr bool Contains(const Vec2 &p) const {
        return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height;
    }
};

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}

    static constexpr Color White()   { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    static constexpr Color Black()   { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    static constexpr Color Red()     { return {1.0f, 0.0f, 0.0f, 1.0f}; }
    static constexpr Color Green()   { return {0.0f, 1.0f, 0.0f, 1.0f}; }
    static constexpr Color Blue()    { return {0.0f, 0.0f, 1.0f, 1.0f}; }
    static constexpr Color Yellow()  { return {1.0f, 1.0f, 0.0f, 1.0f}; }
    static constexpr Color Cyan()    { return {0.0f, 1.0f, 1.0f, 1.0f}; }
    static constexpr Color Magenta() { return {1.0f, 0.0f, 1.0f, 1.0f}; }
    static constexpr Color Clear()   { return {0.0f, 0.0f, 0.0f, 0.0f}; }

    static constexpr Color FromRGBA8(uint8_t r8, uint8_t g8, uint8_t b8, uint8_t a8 = 255) {
        return {r8 / 255.0f, g8 / 255.0f, b8 / 255.0f, a8 / 255.0f};
    }

    [[nodiscard]] uint32_t ToRGBA8() const {
        const auto r8 = static_cast<uint32_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
        const auto g8 = static_cast<uint32_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
        const auto b8 = static_cast<uint32_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
        const auto a8 = static_cast<uint32_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
        return (a8 << 24) | (b8 << 16) | (g8 << 8) | r8;
    }
};

/// 4x4 matrix, stored column-major for direct submission to OpenGL uniforms.
struct Mat4 {
    float m[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    static Mat4 Identity() {
        return Mat4{};
    }

    static Mat4 Orthographic(float left, float right, float bottom, float top, float z_near = -1.0f, float z_far = 1.0f) {
        Mat4 res;
        const float rl = right - left;
        const float tb = top - bottom;
        const float fn = z_far - z_near;

        res.m[0]  =  2.0f / rl;
        res.m[5]  =  2.0f / tb;
        res.m[10] = -2.0f / fn;

        res.m[12] = -(right + left) / rl;
        res.m[13] = -(top + bottom) / tb;
        res.m[14] = -(z_far + z_near) / fn;
        res.m[15] = 1.0f;
        return res;
    }

    static Mat4 Translation(float tx, float ty, float tz = 0.0f) {
        Mat4 res = Identity();
        res.m[12] = tx;
        res.m[13] = ty;
        res.m[14] = tz;
        return res;
    }

    static Mat4 Scale(float sx, float sy, float sz = 1.0f) {
        Mat4 res = Identity();
        res.m[0]  = sx;
        res.m[5]  = sy;
        res.m[10] = sz;
        return res;
    }

    static Mat4 RotationZ(float radians) {
        Mat4 res = Identity();
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        res.m[0] =  c;
        res.m[1] =  s;
        res.m[4] = -s;
        res.m[5] =  c;
        return res;
    }

    Mat4 operator*(const Mat4 &o) const {
        Mat4 r;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                r.m[col * 4 + row] =
                    m[0 * 4 + row] * o.m[col * 4 + 0] +
                    m[1 * 4 + row] * o.m[col * 4 + 1] +
                    m[2 * 4 + row] * o.m[col * 4 + 2] +
                    m[3 * 4 + row] * o.m[col * 4 + 3];
            }
        }
        return r;
    }

    [[nodiscard]] Vec2 TransformPoint(const Vec2 &p) const {
        return {
            m[0] * p.x + m[4] * p.y + m[12],
            m[1] * p.x + m[5] * p.y + m[13]
        };
    }
};

} // namespace fake2d

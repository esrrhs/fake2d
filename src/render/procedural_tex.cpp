#include "fake2d/procedural_tex.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace fake2d::procedural_tex {

namespace {

float Hash2D(int x, int y) {
    unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
}

float TileableValueNoise(float x, float y, int period) {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(xi);
    const float fy = y - static_cast<float>(yi);
    auto at = [period](int a, int b) {
        return Hash2D(((a % period) + period) % period, ((b % period) + period) % period);
    };
    const float u = fx * fx * (3.0f - 2.0f * fx);
    const float v = fy * fy * (3.0f - 2.0f * fy);
    const float a = at(xi, yi);
    const float b2 = at(xi + 1, yi);
    const float c = at(xi, yi + 1);
    const float d = at(xi + 1, yi + 1);
    return a + (b2 - a) * u + (c - a) * v + (a - b2 - c + d) * u * v;
}

} // namespace

Texture2D CreateShadowNineSlice(int size, float inner_radius, float outer_radius,
                                std::uint8_t r, std::uint8_t g, std::uint8_t b, float exponent) {
    Texture2D tex;
    if (size <= 0) {
        return tex;
    }
    std::vector<std::uint8_t> px(static_cast<std::size_t>(size) * size * 4u, 0);
    const float center = (static_cast<float>(size) - 1.0f) * 0.5f;
    const float max_blur = std::max(1e-4f, outer_radius - inner_radius);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = std::max(0.0f, std::abs(static_cast<float>(x) - center) - inner_radius);
            const float dy = std::max(0.0f, std::abs(static_cast<float>(y) - center) - inner_radius);
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float t = std::clamp(dist / max_blur, 0.0f, 1.0f);
            float alpha = 1.0f - (t * t * (3.0f - 2.0f * t));
            alpha = std::pow(std::max(0.0f, alpha), exponent);
            const std::size_t idx = (static_cast<std::size_t>(y) * size + x) * 4u;
            px[idx + 0] = r;
            px[idx + 1] = g;
            px[idx + 2] = b;
            px[idx + 3] = static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
        }
    }
    tex.Create(size, size, px.data(), 4, TextureFilter::Linear, TextureWrap::Clamp);
    return tex;
}

Texture2D CreateSolidCircle(int size, std::uint8_t r, std::uint8_t g, std::uint8_t b, float feather) {
    Texture2D tex;
    if (size <= 0) {
        return tex;
    }
    std::vector<std::uint8_t> px(static_cast<std::size_t>(size) * size * 4u, 0);
    const float c = (static_cast<float>(size) - 1.0f) * 0.5f;
    const float radius = static_cast<float>(size) * 0.5f - 1.0f;
    const float denom = std::max(1e-4f, feather);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x) - c;
            const float dy = static_cast<float>(y) - c;
            const float cov = std::clamp((radius - std::sqrt(dx * dx + dy * dy)) / denom + 0.5f, 0.0f, 1.0f);
            const std::size_t idx = (static_cast<std::size_t>(y) * size + x) * 4u;
            px[idx + 0] = r;
            px[idx + 1] = g;
            px[idx + 2] = b;
            px[idx + 3] = static_cast<std::uint8_t>(cov * 255.0f + 0.5f);
        }
    }
    tex.Create(size, size, px.data(), 4, TextureFilter::Linear, TextureWrap::Clamp);
    return tex;
}

Texture2D CreatePaperGrain(int size, float tint_r, float tint_g, float tint_b, TextureWrap wrap) {
    Texture2D tex;
    if (size <= 0) {
        return tex;
    }
    std::vector<std::uint8_t> px(static_cast<std::size_t>(size) * size * 4u, 0);
    const float nf = static_cast<float>(size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float n = 0.0f;
            float amp = 0.5f;
            float freq = 4.0f;
            for (int oct = 0; oct < 5; ++oct) {
                n += amp * TileableValueNoise(x * freq / nf, y * freq / nf, static_cast<int>(freq));
                amp *= 0.5f;
                freq *= 2.0f;
            }
            const float fib = TileableValueNoise(x * 64.0f / nf, y * 8.0f / nf, 64) * 0.5f +
                              TileableValueNoise(x * 128.0f / nf, y * 16.0f / nf, 128) * 0.5f;
            const float stain = TileableValueNoise(x * 3.0f / nf + 7.0f, y * 3.0f / nf + 3.0f, 3);
            float l = 0.80f + 0.22f * (n - 0.5f) + 0.06f * (fib - 0.5f) - 0.10f * std::max(0.0f, stain - 0.62f) * 3.0f;
            l = std::clamp(l, 0.0f, 1.0f);
            const std::size_t idx = (static_cast<std::size_t>(y) * size + x) * 4u;
            px[idx + 0] = static_cast<std::uint8_t>(255.0f * std::min(1.0f, l * tint_r));
            px[idx + 1] = static_cast<std::uint8_t>(255.0f * std::min(1.0f, l * tint_g));
            px[idx + 2] = static_cast<std::uint8_t>(255.0f * std::min(1.0f, l * tint_b));
            px[idx + 3] = 255;
        }
    }
    tex.Create(size, size, px.data(), 4, TextureFilter::Linear, wrap);
    return tex;
}

Texture2D CreateBeveledDisc(int size, float rim_start, float spec_inner) {
    Texture2D tex;
    if (size <= 0) {
        return tex;
    }
    std::vector<std::uint8_t> px(static_cast<std::size_t>(size) * size * 4u, 0);
    const float nf = static_cast<float>(size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) / nf * 2.0f - 1.0f;
            const float dy = (static_cast<float>(y) + 0.5f) / nf * 2.0f - 1.0f;
            const float r = std::sqrt(dx * dx + dy * dy);
            const float a = std::clamp((1.0f - r) * nf * 0.5f, 0.0f, 1.0f);
            float l = r > rim_start ? 0.45f : (0.92f + 0.12f * (-dy) - 0.05f * r);
            if (r < rim_start && r > spec_inner) {
                l = 1.05f;
            }
            l = std::clamp(l, 0.0f, 1.0f);
            const std::size_t idx = (static_cast<std::size_t>(y) * size + x) * 4u;
            const auto gray = static_cast<std::uint8_t>(255.0f * l);
            px[idx + 0] = gray;
            px[idx + 1] = gray;
            px[idx + 2] = gray;
            px[idx + 3] = static_cast<std::uint8_t>(255.0f * a);
        }
    }
    tex.Create(size, size, px.data(), 4, TextureFilter::Linear, TextureWrap::Clamp);
    return tex;
}

} // namespace fake2d::procedural_tex

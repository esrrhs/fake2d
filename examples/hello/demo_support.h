#pragma once
// Private unchanged demo/fixture helpers shared by separated entrypoints.
// The demos are pure FakeLua scripts (examples/hello/scripts/*_demo.lua + game.lua).
// This binary only provides: engine bootstrap, procedural asset generation
// (PNG/Tiled JSON/WAV, since scripts cannot encode binary files), and the
// micro-benchmarks that measure engine internals.
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include "fake2d/version.h"
#include "fake2d/app_paths.h"
#include "fake2d/demos.h"
#include "fake2d/ease.h"
#include "fake2d/engine.h"
#include "fake2d/camera.h"
#include "fake2d/entity_store.h"
#include "fake2d/physics.h"
#include "fake2d/rng.h"
#include "fake2d/world_hash.h"
#include "fake2d/input_recording.h"
#include "fake2d/i18n.h"
#include "fake2d/log.h"
#include "fake2d/save_store.h"
#include "fake2d/steam.h"
#include "fake2d/ui_test_runner.h"
#include "render/gl.h" // benchmark reports the actual current GL driver/context

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <string_view>
#include <vector>

// stb_image_write implementation lives in the fake2d library
// (src/render/image_write.cpp); here we only need the declarations.
#include <stb_image.h>
#include <stb_image_write.h>

namespace {

// ---------------------------------------------------------------------------
// Procedural asset generation for the breakout sample: three small white
// textures tinted from Lua. Generated on demand so the repo stays asset-free.
// ---------------------------------------------------------------------------

// --- breakout shapes (white on transparent; tinted per brick row from Lua) ---

void PaintRoundedRect(std::uint8_t *pixels, int w, int h, int radius) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t *px = pixels + (static_cast<size_t>(y) * w + x) * 4;
            const float cx = static_cast<float>(std::clamp(x, radius, w - 1 - radius));
            const float cy = static_cast<float>(std::clamp(y, radius, h - 1 - radius));
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float a = std::clamp(static_cast<float>(radius) - d + 0.5f, 0.0f, 1.0f);
            px[0] = px[1] = px[2] = 255;
            px[3] = static_cast<std::uint8_t>(a * 255.0f);
        }
    }
}

/// 64x16 coin sheet: four 16x16 frames of a spinning coin; the lighter inner
/// ellipse shrinks/widens to fake horizontal rotation.
void PaintCoinSheet(std::uint8_t *pixels) {
    constexpr int kFrame = 16;
    constexpr int kFrames = 4;
    const float inner_rx[kFrames] = {6.0f, 3.5f, 1.0f, 3.5f};
    for (int f = 0; f < kFrames; ++f) {
        const float ox = static_cast<float>(f * kFrame);
        for (int y = 0; y < kFrame; ++y) {
            for (int x = 0; x < kFrame; ++x) {
                const float dx = static_cast<float>(x) - (kFrame * 0.5f - 0.5f);
                const float dy = static_cast<float>(y) - (kFrame * 0.5f - 0.5f);
                const float d_outer = std::sqrt(dx * dx + dy * dy);
                float a = std::clamp(7.0f - d_outer + 0.5f, 0.0f, 1.0f);
                float rr = 240.0f, gg = 200.0f, bb = 80.0f;
                // Inner highlight ellipse.
                const float inner = (dx * dx) / (inner_rx[f] * inner_rx[f] + 0.01f) +
                                    (dy * dy) / (6.2f * 6.2f);
                if (inner <= 1.0f && a > 0.0f) {
                    rr = 255.0f; gg = 238.0f; bb = 165.0f;
                }
                std::uint8_t *px = pixels + (static_cast<size_t>(y) * (kFrame * kFrames) +
                                             static_cast<int>(ox) + x) * 4;
                px[0] = static_cast<std::uint8_t>(rr);
                px[1] = static_cast<std::uint8_t>(gg);
                px[2] = static_cast<std::uint8_t>(bb);
                px[3] = static_cast<std::uint8_t>(a * 255.0f);
            }
        }
    }
}

void PaintCircle(std::uint8_t *pixels, int w, int h) {
    const float r = w * 0.5f - 0.5f;
    const float c = w * 0.5f - 0.5f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t *px = pixels + (static_cast<size_t>(y) * w + x) * 4;
            const float dx = static_cast<float>(x) - c;
            const float dy = static_cast<float>(y) - c;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float a = std::clamp(r - d + 0.5f, 0.0f, 1.0f);
            px[0] = px[1] = px[2] = 255;
            px[3] = static_cast<std::uint8_t>(a * 255.0f);
        }
    }
}

/// 64x64 shaded sphere used (tinted) for every snooker ball: warm light from
/// the upper left, Lambert falloff towards the rim and a tight specular dot.
/// The tint comes from Lua, so one white sprite draws the whole set.
void PaintSnookerBall(std::uint8_t *pixels, int w, int h) {
    const float cx = static_cast<float>(w) * 0.5f - 0.5f;
    const float cy = static_cast<float>(h) * 0.5f - 0.5f;
    const float radius = static_cast<float>(w) * 0.5f - 0.5f;
    const float lx = -0.48f;
    const float ly = -0.56f;
    const float lz = 0.68f;
    const float hlen = std::sqrt(lx * lx + ly * ly + (lz + 1.0f) * (lz + 1.0f));
    const float hx = lx / hlen;
    const float hy = ly / hlen;
    const float hz = (lz + 1.0f) / hlen;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t *px = pixels + (static_cast<size_t>(y) * w + x) * 4;
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float a = std::clamp(radius - d + 0.5f, 0.0f, 1.0f);
            if (a <= 0.0f) {
                px[0] = px[1] = px[2] = px[3] = 0;
                continue;
            }
            const float nx = dx / radius;
            const float ny = dy / radius;
            const float nz2 = 1.0f - nx * nx - ny * ny;
            const float nz = nz2 > 0.0f ? std::sqrt(nz2) : 0.0f;
            const float lambert = std::max(0.0f, nx * lx + ny * ly + nz * lz);
            const float spec = std::pow(std::max(0.0f, nx * hx + ny * hy + nz * hz), 22.0f);
            const float shade = std::clamp(0.28f + 0.64f * lambert + 0.55f * spec, 0.0f, 1.0f);
            const std::uint8_t v = static_cast<std::uint8_t>(shade * 255.0f);
            px[0] = v;
            px[1] = v;
            px[2] = v;
            px[3] = static_cast<std::uint8_t>(a * 255.0f);
        }
    }
}

/// 80x16 five-frame blast strip: a hot core grows and fades frame by frame.
void PaintExplosionSheet(std::uint8_t *pixels) {
    constexpr int kFrame = 16;
    constexpr int kFrames = 5;
    for (int f = 0; f < kFrames; ++f) {
        const float r = 3.0f + static_cast<float>(f) * 1.45f;
        const float fade = 1.0f - static_cast<float>(f) * 0.11f;
        for (int y = 0; y < kFrame; ++y) {
            for (int x = 0; x < kFrame; ++x) {
                const float dx = static_cast<float>(x) - 7.5f;
                const float dy = static_cast<float>(y) - 7.5f;
                const float d = std::sqrt(dx * dx + dy * dy);
                float a = std::clamp((r - d + 0.6f) * 0.8f, 0.0f, 1.0f) * fade;
                std::uint8_t *px = pixels +
                    (static_cast<size_t>(y) * (kFrame * kFrames) +
                     f * kFrame + x) * 4;
                if (a <= 0.0f) {
                    px[0] = px[1] = px[2] = px[3] = 0;
                    continue;
                }
                if (d < r * 0.45f) {
                    px[0] = 255;
                    px[1] = 242;
                    px[2] = 190;
                } else {
                    px[0] = 255;
                    px[1] = 140;
                    px[2] = 46;
                }
                px[3] = static_cast<std::uint8_t>(a * 255.0f);
            }
        }
    }
}

/// Soft radial sprite: a moon disc (opaque core) or an additive glow (alpha
/// falls off to the rim). Tinted from Lua for muzzle flashes and pickups.
void PaintSoftDisc(std::uint8_t *pixels, int w, int h, bool moon) {
    const float cx = static_cast<float>(w) * 0.5f - 0.5f;
    const float cy = static_cast<float>(h) * 0.5f - 0.5f;
    const float radius = static_cast<float>(w) * 0.5f - 0.5f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t *px = pixels + (static_cast<size_t>(y) * w + x) * 4;
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float t = std::clamp(1.0f - d / radius, 0.0f, 1.0f);
            float a = 0.0f;
            if (moon) {
                a = std::clamp((radius - d + 0.8f) * 0.9f, 0.0f, 1.0f);
            } else {
                a = t * t * t * 0.85f;
            }
            // A few darker craters on the moon face.
            float shade = 1.0f;
            if (moon) {
                const float c1 = (dx + 7.0f) * (dx + 7.0f) + (dy - 5.0f) * (dy - 5.0f);
                const float c2 = (dx - 9.0f) * (dx - 9.0f) + (dy + 8.0f) * (dy + 8.0f);
                const float c3 = (dx + 2.0f) * (dx + 2.0f) + (dy + 14.0f) * (dy + 14.0f);
                if (c1 < 34.0f || c2 < 44.0f || c3 < 22.0f) shade = 0.80f;
            }
            px[0] = static_cast<std::uint8_t>(std::clamp(0.88f * shade, 0.0f, 1.0f) * 255.0f);
            px[1] = static_cast<std::uint8_t>(std::clamp(0.92f * shade, 0.0f, 1.0f) * 255.0f);
            px[2] = static_cast<std::uint8_t>(std::clamp(1.00f * shade, 0.0f, 1.0f) * 255.0f);
            px[3] = static_cast<std::uint8_t>(a * 255.0f);
        }
    }
}

/// Write the sample's PNG assets unless they already exist next to the binary.
void EnsureGameAssets() {
    const std::filesystem::path dir = "assets";
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    const auto write = [&dir](const char *name, void (*paint)(std::uint8_t *, int, int), int w, int h) {
        const std::filesystem::path path = dir / name;
        if (std::filesystem::exists(path)) {
            return;
        }
        std::vector<std::uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        paint(pixels.data(), w, h);
        if (!stbi_write_png(path.string().c_str(), w, h, 4, pixels.data(), w * 4)) {
            std::fprintf(stderr, "fake2d_hello: failed to write %s\n", path.string().c_str());
        }
    };

    write("brick.png", [](std::uint8_t *p, int w, int h) { PaintRoundedRect(p, w, h, 7); }, 86, 26);
    write("paddle.png", [](std::uint8_t *p, int w, int h) { PaintRoundedRect(p, w, h, 8); }, 120, 18);
    write("ball.png", PaintCircle, 18, 18);

    // Spinning coin sprite sheet: four 16x16 frames in a 64x16 strip.
    {
        const std::filesystem::path path = dir / "coin.png";
        if (!std::filesystem::exists(path)) {
            std::vector<std::uint8_t> pixels(64 * 16 * 4);
            PaintCoinSheet(pixels.data());
            stbi_write_png(path.string().c_str(), 64, 16, 4, pixels.data(), 64 * 4);
        }
    }
}

/// The brawler demo paints its fighters from rotated quads, so all it needs is
/// one fully white texel to tint. Written next to the other sample assets.
void EnsureBeatEmUpAssets() {
    const std::filesystem::path dir = "assets";
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);
    const std::filesystem::path path = dir / "white.png";
    if (std::filesystem::exists(path)) {
        return;
    }
    const std::uint8_t white[4] = {255, 255, 255, 255};
    stbi_write_png(path.string().c_str(), 1, 1, 4, white, 4);
}

/// Snooker needs one shaded ball sprite (tinted per colour from Lua) and the
/// same one-texel white used by the other demos for sticks and aim guides.
void EnsureSnookerAssets() {
    const std::filesystem::path dir = "assets";
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    const std::filesystem::path white_path = dir / "white.png";
    if (!std::filesystem::exists(white_path)) {
        const std::uint8_t white[4] = {255, 255, 255, 255};
        stbi_write_png(white_path.string().c_str(), 1, 1, 4, white, 4);
    }

    const std::filesystem::path dir_ball = dir / "snooker";
    std::filesystem::create_directories(dir_ball, ec);
    const std::filesystem::path ball_path = dir_ball / "ball.png";
    if (std::filesystem::exists(ball_path)) {
        return;
    }
    std::vector<std::uint8_t> pixels(64 * 64 * 4);
    PaintSnookerBall(pixels.data(), 64, 64);
    if (!stbi_write_png(ball_path.string().c_str(), 64, 64, 4, pixels.data(), 64 * 4)) {
        std::fprintf(stderr, "fake2d_hello: failed to write %s\n",
                     ball_path.string().c_str());
    }
}

/// Run-and-gun demo: a moon disc for the jungle sky, an additive glow for
/// muzzle flashes / pickups, and the shared white texel for rotated quads.
void EnsureContraAssets() {
    const std::filesystem::path dir = "assets";
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    const std::filesystem::path white_path = dir / "white.png";
    if (!std::filesystem::exists(white_path)) {
        const std::uint8_t white[4] = {255, 255, 255, 255};
        stbi_write_png(white_path.string().c_str(), 1, 1, 4, white, 4);
    }

    const std::filesystem::path cdir = dir / "contra";
    std::filesystem::create_directories(cdir, ec);
    const auto write = [&cdir](const char *name, bool moon) {
        const std::filesystem::path path = cdir / name;
        if (std::filesystem::exists(path)) {
            return;
        }
        std::vector<std::uint8_t> pixels(64 * 64 * 4);
        PaintSoftDisc(pixels.data(), 64, 64, moon);
        if (!stbi_write_png(path.string().c_str(), 64, 64, 4, pixels.data(), 64 * 4)) {
            std::fprintf(stderr, "fake2d_hello: failed to write %s\n",
                         path.string().c_str());
        }
    };
    write("moon.png", true);
    write("glow.png", false);

    const std::filesystem::path expl_path = cdir / "explosion.png";
    if (!std::filesystem::exists(expl_path)) {
        std::vector<std::uint8_t> pixels(80 * 16 * 4);
        PaintExplosionSheet(pixels.data());
        if (!stbi_write_png(expl_path.string().c_str(), 80, 16, 4,
                            pixels.data(), 80 * 4)) {
            std::fprintf(stderr, "fake2d_hello: failed to write %s\n",
                         expl_path.string().c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// City-simulation assets (Phase 40): a terrain tileset, a building atlas and a
// generated base map (grass, lake, forests and an entrance road grid). The Lua
// city demo mutates the map in place with map_set_tile, so only the base
// terrain ships here.
// ---------------------------------------------------------------------------

constexpr int kCityTile = 32;
constexpr int kCitySheetCols = 6;
constexpr int kCityMapCols = 56;
constexpr int kCityMapRows = 42;
constexpr int kCityBuildFrameW = 32;
constexpr int kCityBuildFrameH = 64;
constexpr int kCityBuildLevels = 4;
constexpr int kCityBuildRows = 6;

std::uint32_t CityHash(int x, int y, int salt) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u +
                      static_cast<std::uint32_t>(y) * 668265263u +
                      static_cast<std::uint32_t>(salt) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return h;
}

void PaintCityTileset(std::uint8_t *img) {
    const int sheet_w = kCitySheetCols * kCityTile;
    auto put = [&](int id, int x, int y, std::uint8_t r, std::uint8_t g,
                   std::uint8_t b, std::uint8_t a = 255) {
        if (x < 0 || y < 0 || x >= kCityTile || y >= kCityTile) {
            return;
        }
        const int ox = (id % kCitySheetCols) * kCityTile;
        const int oy = (id / kCitySheetCols) * kCityTile;
        std::uint8_t *px = img + (static_cast<size_t>(oy + y) * sheet_w + ox + x) * 4;
        px[0] = r;
        px[1] = g;
        px[2] = b;
        px[3] = a;
    };
    auto fill = [&](int id, int r, int g, int b, int vary, int salt) {
        for (int y = 0; y < kCityTile; ++y) {
            for (int x = 0; x < kCityTile; ++x) {
                const int n = static_cast<int>(CityHash(x, y, salt) % (vary * 2 + 1)) - vary;
                put(id, x, y,
                    static_cast<std::uint8_t>(std::clamp(r + n, 0, 255)),
                    static_cast<std::uint8_t>(std::clamp(g + n, 0, 255)),
                    static_cast<std::uint8_t>(std::clamp(b + n, 0, 255)));
            }
        }
    };
    auto box = [&](int id, int x0, int y0, int x1, int y1, std::uint8_t r,
                   std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                put(id, x, y, r, g, b, a);
            }
        }
    };
    auto tree = [&](int id, int cx, int cy, int radius) {
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                const int d2 = x * x + y * y;
                if (d2 <= radius * radius) {
                    const bool lit = d2 <= (radius - 2) * (radius - 2) && x + y < 0;
                    put(id, cx + x, cy + y, lit ? 92 : 42, lit ? 148 : 92,
                        lit ? 62 : 46);
                }
            }
        }
        box(id, cx - 1, cy + radius - 1, cx + 1, cy + radius + 2, 92, 66, 42);
    };

    // 0/1 grass and flowering grass.
    fill(0, 88, 150, 74, 10, 7);
    fill(1, 88, 150, 74, 10, 8);
    for (int i = 0; i < 22; ++i) {
        const int x = static_cast<int>(CityHash(i, 0, 21) % kCityTile);
        const int y = static_cast<int>(CityHash(i, 1, 21) % kCityTile);
        put(0, x, y, 66, 124, 56);
        put(0, x + 1, y - 2, 104, 168, 82);
    }
    for (int i = 0; i < 12; ++i) {
        const int x = static_cast<int>(CityHash(i, 2, 22) % kCityTile);
        const int y = static_cast<int>(CityHash(i, 3, 22) % kCityTile);
        const bool white = (i % 3) == 0;
        put(1, x, y, white ? 244 : 246, white ? 240 : 214, white ? 214 : 92);
        put(1, x + 1, y, white ? 232 : 232, white ? 232 : 196, white ? 210 : 84);
    }

    // 2 water with light wave crests.
    fill(2, 46, 112, 178, 6, 3);
    for (int y = 5; y < kCityTile; y += 9) {
        for (int x = 2; x < kCityTile - 2; ++x) {
            const int wobble = static_cast<int>(CityHash(x, y, 31) % 5) - 2;
            if ((x + y) % 7 < 4) {
                put(2, x, y + wobble, 92, 164, 214);
                put(2, x, y + wobble + 1, 70, 140, 196);
            }
        }
    }

    // 3 sand with pebbles.
    fill(3, 202, 184, 132, 8, 5);
    for (int i = 0; i < 26; ++i) {
        const int x = static_cast<int>(CityHash(i, 4, 33) % kCityTile);
        const int y = static_cast<int>(CityHash(i, 5, 33) % kCityTile);
        put(3, x, y, 172, 152, 104);
    }

    // 4 asphalt with curbs and gravel.
    fill(4, 72, 76, 84, 5, 9);
    box(4, 0, 0, kCityTile - 1, 1, 54, 58, 66);
    box(4, 0, kCityTile - 2, kCityTile - 1, kCityTile - 1, 54, 58, 66);
    box(4, 0, 0, 1, kCityTile - 1, 54, 58, 66);
    box(4, kCityTile - 2, 0, kCityTile - 1, kCityTile - 1, 54, 58, 66);
    for (int i = 0; i < 30; ++i) {
        const int x = static_cast<int>(CityHash(i, 6, 34) % kCityTile);
        const int y = static_cast<int>(CityHash(i, 7, 34) % kCityTile);
        put(4, x, y, 98, 102, 110);
    }

    // 5/6/7 zoned plots: grass tinted by zone with a coloured survey border.
    fill(5, 108, 166, 90, 9, 12);
    box(5, 3, 3, kCityTile - 4, 3, 214, 198, 152);
    box(5, 3, kCityTile - 4, kCityTile - 4, kCityTile - 4, 214, 198, 152);
    box(5, 3, 3, 3, kCityTile - 4, 214, 198, 152);
    box(5, kCityTile - 4, 3, kCityTile - 4, kCityTile - 4, 214, 198, 152);
    fill(6, 76, 148, 150, 9, 13);
    box(6, 3, 3, kCityTile - 4, 3, 136, 184, 232);
    box(6, 3, kCityTile - 4, kCityTile - 4, kCityTile - 4, 136, 184, 232);
    box(6, 3, 3, 3, kCityTile - 4, 136, 184, 232);
    box(6, kCityTile - 4, 3, kCityTile - 4, kCityTile - 4, 136, 184, 232);
    fill(7, 142, 150, 78, 9, 14);
    box(7, 3, 3, kCityTile - 4, 3, 222, 178, 96);
    box(7, 3, kCityTile - 4, kCityTile - 4, kCityTile - 4, 222, 178, 96);
    box(7, 3, 3, 3, kCityTile - 4, 222, 178, 96);
    box(7, kCityTile - 4, 3, kCityTile - 4, kCityTile - 4, 222, 178, 96);

    // 8 park plot: mown grass with a white fence line.
    fill(8, 74, 140, 70, 8, 15);
    box(8, 0, 0, kCityTile - 1, 1, 226, 232, 214);
    box(8, 0, kCityTile - 2, kCityTile - 1, kCityTile - 1, 226, 232, 214);
    for (int i = 0; i < 14; ++i) {
        const int x = static_cast<int>(CityHash(i, 8, 35) % kCityTile);
        const int y = static_cast<int>(CityHash(i, 9, 35) % kCityTile);
        put(8, x, y, 244, 232, 176);
    }

    // 9 rubble.
    fill(9, 116, 96, 74, 9, 16);
    for (int i = 0; i < 24; ++i) {
        const int x = static_cast<int>(CityHash(i, 10, 36) % kCityTile);
        const int y = static_cast<int>(CityHash(i, 11, 36) % kCityTile);
        put(9, x, y, 78, 68, 58);
        put(9, x + 1, y, 142, 124, 96);
    }

    // 10 forest.
    fill(10, 76, 132, 64, 8, 17);
    tree(10, 9, 17, 6);
    tree(10, 22, 19, 5);
    for (int i = 0; i < 10; ++i) {
        const int x = static_cast<int>(CityHash(i, 12, 37) % kCityTile);
        const int y = static_cast<int>(CityHash(i, 13, 37) % kCityTile);
        put(10, x, y, 58, 112, 52);
    }

    // 11 power-plant pad: concrete grid with a hatching corner.
    fill(11, 132, 132, 128, 5, 18);
    for (int i = 0; i < kCityTile; i += 8) {
        box(11, i, 0, i, kCityTile - 1, 116, 116, 112);
        box(11, 0, i, kCityTile - 1, i, 116, 116, 112);
    }
    for (int i = 0; i < 7; ++i) {
        put(11, i, 0, 226, 196, 72);
        put(11, 0, i, 226, 196, 72);
        put(11, kCityTile - 1 - i, kCityTile - 1, 226, 196, 72);
    }
}

void PaintCityBuilding(std::uint8_t *img, int type, int level) {
    const int sheet_w = kCityBuildLevels * kCityBuildFrameW;
    const int ox = level * kCityBuildFrameW;
    const int oy = type * kCityBuildFrameH;
    auto put = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                   std::uint8_t a = 255) {
        if (x < 0 || y < 0 || x >= kCityBuildFrameW || y >= kCityBuildFrameH) {
            return;
        }
        std::uint8_t *px = img + (static_cast<size_t>(oy + y) * sheet_w + ox + x) * 4;
        px[0] = r;
        px[1] = g;
        px[2] = b;
        px[3] = a;
    };
    auto rect = [&](int x0, int y0, int x1, int y1, std::uint8_t r,
                    std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                put(x, y, r, g, b, a);
            }
        }
    };
    auto circle = [&](int cx, int cy, int radius, std::uint8_t r,
                      std::uint8_t g, std::uint8_t b) {
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x * x + y * y <= radius * radius) {
                    put(cx + x, cy + y, r, g, b);
                }
            }
        }
    };
    auto windows = [&](int x0, int top, int cols, int rows, int step_x,
                       int step_y, std::uint8_t lr, std::uint8_t lg,
                       std::uint8_t lb) {
        int k = 0;
        for (int ry = 0; ry < rows; ++ry) {
            for (int cx = 0; cx < cols; ++cx) {
                const bool lit = (CityHash(k++, type, level) % 5) < 3;
                const std::uint8_t r = lit ? lr : 52;
                const std::uint8_t g = lit ? lg : 58;
                const std::uint8_t b = lit ? lb : 72;
                rect(x0 + cx * step_x, top + ry * step_y,
                     x0 + cx * step_x + 3, top + ry * step_y + 3, r, g, b);
            }
        }
    };

    // Contact shadow under every footprint.
    for (int y = 58; y < 64; ++y) {
        for (int x = 3; x < 29; ++x) {
            put(x, y, 24, 30, 26, 70);
        }
    }

    if (type == 3) { // park
        rect(2, 30, 29, 62, 82, 142, 70);
        rect(2, 30, 29, 32, 108, 168, 88);
        const int count = 1 + level;
        for (int i = 0; i < count; ++i) {
            const int cx = 7 + (i * 9) % 20;
            const int cy = 40 + (i * 7) % 16;
            rect(cx - 1, cy, cx + 1, cy + 6, 96, 70, 44);
            circle(cx, cy - 3, 5, 46, 118, 54);
            circle(cx - 1, cy - 4, 3, 74, 158, 76);
        }
        return;
    }
    if (type == 4) { // power plant
        rect(4, 40, 28, 62, 118, 120, 118);
        rect(4, 40, 28, 42, 156, 158, 154);
        rect(13, 20, 18, 42, 148, 146, 140);
        rect(22, 8, 27, 42, 148, 146, 140);
        rect(22, 8, 27, 11, 196, 90, 64);
        rect(13, 20, 18, 22, 196, 90, 64);
        // Lightning badge.
        rect(7, 46, 10, 49, 246, 208, 72);
        rect(8, 49, 12, 52, 246, 208, 72);
        rect(7, 52, 10, 55, 246, 208, 72);
        return;
    }

    int h = 0;
    std::uint8_t wr = 0, wg = 0, wb = 0;
    std::uint8_t rr = 0, rg = 0, rb = 0;
    int cols = 0;
    int top = 0;
    if (type == 0) {
        h = 22 + level * 9;
        wr = static_cast<std::uint8_t>(210 + level * 4);
        wg = static_cast<std::uint8_t>(196 - level * 10);
        wb = static_cast<std::uint8_t>(168 - level * 12);
        rr = 150;
        rg = 96;
        rb = 78;
        cols = 4;
    } else if (type == 1) {
        h = 26 + level * 13;
        wr = static_cast<std::uint8_t>(88 + level * 8);
        wg = static_cast<std::uint8_t>(126 + level * 8);
        wb = static_cast<std::uint8_t>(154 + level * 10);
        rr = 62;
        rg = 88;
        rb = 118;
        cols = 5;
    } else {
        h = 18 + level * 7;
        wr = 150;
        wg = 138;
        wb = 118;
        rr = 92;
        rg = 84;
        rb = 74;
        cols = 3;
    }
    top = 60 - h;
    rect(4, top, 27, 61, wr, wg, wb);
    rect(25, top, 27, 61, static_cast<std::uint8_t>(wr * 0.72f),
         static_cast<std::uint8_t>(wg * 0.72f), static_cast<std::uint8_t>(wb * 0.72f));
    rect(2, top - 3, 29, top + 1, rr, rg, rb);
    rect(2, top - 3, 29, top - 2, static_cast<std::uint8_t>(rr + 26),
         static_cast<std::uint8_t>(rg + 26), static_cast<std::uint8_t>(rb + 26));

    if (type == 0) {
        windows(7, top + 5, cols, 3, 6, 6, 252, 224, 122);
        rect(14, 56, 17, 61, 92, 66, 44);
        rect(4, 61, 27, 62, 116, 112, 104);
    } else if (type == 1) {
        windows(5, top + 5, cols, 4, 5, 7, 170, 220, 244);
        rect(5, 60, 26, 62, 68, 76, 92);
        rect(5, top + h - 6, 24, top + h - 4, 236, 196, 72);
    } else {
        windows(7, top + 4, cols, 2, 6, 6, 226, 190, 120);
        // Saw-tooth roof and a smokestack.
        for (int i = 0; i < 3; ++i) {
            rect(5 + i * 7, top - 6, 11 + i * 7, top - 1, 78, 74, 68);
        }
        rect(21, top - 14, 25, 61, 110, 108, 102);
        rect(21, top - 14, 25, top - 11, 178, 74, 56);
    }

    if (type == 0 && level >= 2) {
        rect(6, top - 8, 10, top - 1, 130, 122, 110);
        rect(6, top - 8, 10, top - 7, 190, 184, 172);
    }
}

void PaintCitySpecial(std::uint8_t *img, int col) {
    const int sheet_w = kCityBuildLevels * kCityBuildFrameW;
    const int ox = col * kCityBuildFrameW;
    const int oy = 5 * kCityBuildFrameH;
    auto put = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                   std::uint8_t a = 255) {
        if (x < 0 || y < 0 || x >= kCityBuildFrameW || y >= kCityBuildFrameH) {
            return;
        }
        std::uint8_t *px = img + (static_cast<size_t>(oy + y) * sheet_w + ox + x) * 4;
        px[0] = r;
        px[1] = g;
        px[2] = b;
        px[3] = a;
    };
    auto rect = [&](int x0, int y0, int x1, int y1, std::uint8_t r,
                    std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                put(x, y, r, g, b, a);
            }
        }
    };
    if (col == 0) { // construction site
        rect(2, 50, 29, 62, 132, 110, 82);
        rect(2, 50, 29, 52, 156, 134, 100);
        for (int i = 0; i < 4; ++i) {
            rect(5 + i * 6, 34, 6 + i * 6, 56, 178, 142, 82);
            rect(5 + i * 6, 34, 6 + i * 6, 35, 214, 186, 120);
        }
        rect(4, 40, 27, 41, 178, 142, 82);
        rect(21, 6, 24, 48, 206, 176, 96);
        rect(24, 6, 27, 8, 206, 176, 96);
        rect(26, 8, 27, 22, 206, 176, 96);
        rect(7, 55, 12, 62, 110, 88, 64);
        return;
    }
    // Abandoned shell: dark walls, broken windows and a caved-in roof.
    rect(4, 30, 27, 62, 104, 96, 88);
    rect(22, 30, 27, 62, 80, 74, 68);
    rect(2, 26, 29, 31, 72, 68, 64);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 3; ++j) {
            const bool broken = (CityHash(i, j, 41) % 3) != 0;
            rect(6 + i * 5, 34 + j * 7, 8 + i * 5, 36 + j * 7,
                 broken ? 28 : 88, broken ? 32 : 96, broken ? 34 : 106);
        }
    }
    rect(12, 54, 18, 62, 54, 48, 44);
    rect(4, 30, 8, 46, 118, 108, 98);
}

/// Buildings atlas: rows are types (residential, commercial, industrial, park,
/// power, special) and columns are growth levels 1..4. 32x64 frames keep the
/// footprint one tile wide while the height gives the city its skyline.
void EnsureCityAssets(bool regen) {
    const std::filesystem::path dir = "assets/city";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::filesystem::path tiles_path = dir / "city_tiles.png";
    if (regen || !std::filesystem::exists(tiles_path)) {
        std::vector<std::uint8_t> pixels(
            static_cast<size_t>(kCitySheetCols * kCityTile) * (2 * kCityTile) * 4, 0);
        PaintCityTileset(pixels.data());
        stbi_write_png(tiles_path.string().c_str(), kCitySheetCols * kCityTile,
                       2 * kCityTile, 4, pixels.data(), kCitySheetCols * kCityTile * 4);
    }

    const std::filesystem::path build_path = dir / "buildings.png";
    if (regen || !std::filesystem::exists(build_path)) {
        std::vector<std::uint8_t> pixels(
            static_cast<size_t>(kCityBuildLevels * kCityBuildFrameW) *
                (kCityBuildRows * kCityBuildFrameH) * 4,
            0);
        for (int type = 0; type < 5; ++type) {
            for (int level = 0; level < kCityBuildLevels; ++level) {
                PaintCityBuilding(pixels.data(), type, level);
            }
        }
        PaintCitySpecial(pixels.data(), 0);
        PaintCitySpecial(pixels.data(), 1);
        stbi_write_png(build_path.string().c_str(),
                       kCityBuildLevels * kCityBuildFrameW,
                       kCityBuildRows * kCityBuildFrameH, 4, pixels.data(),
                       kCityBuildLevels * kCityBuildFrameW * 4);
    }

    const std::filesystem::path map_path = dir / "city_map.json";
    if (!regen && std::filesystem::exists(map_path)) {
        return;
    }
    // gid order matches the tileset above: 1 grass, 2 flowers, 3 water,
    // 4 sand, 5 road, 6 residential, 7 commercial, 8 industrial, 9 park,
    // 10 rubble, 11 forest, 12 power pad.
    std::vector<std::uint32_t> gids(static_cast<size_t>(kCityMapCols) * kCityMapRows, 1);
    auto at = [&](int c, int r) -> std::uint32_t & {
        return gids[static_cast<size_t>(r) * kCityMapCols + c];
    };
    for (int r = 0; r < kCityMapRows; ++r) {
        for (int c = 0; c < kCityMapCols; ++c) {
            const std::uint32_t h = CityHash(c, r, 11);
            at(c, r) = (h % 6 == 0) ? 2 : 1;
            if (h % 1000 > 962) {
                at(c, r) = 11;
            }
        }
    }
    // Lake in the north-east and a pond in the south-west, both ringed by sand.
    const auto lake = [&](int cx, int cy, double rx, double ry) {
        for (int r = 0; r < kCityMapRows; ++r) {
            for (int c = 0; c < kCityMapCols; ++c) {
                const double dx = (c - cx) / rx;
                const double dy = (r - cy) / ry;
                const double d = dx * dx + dy * dy;
                if (d < 1.0) {
                    at(c, r) = 3;
                } else if (d < 1.35) {
                    at(c, r) = 4;
                }
            }
        }
    };
    lake(43, 13, 10.0, 8.0);
    lake(9, 36, 6.5, 4.2);

    // Baked entrance network: a road from the west edge into a starter grid.
    const auto road = [&](int c, int r) {
        if (c >= 0 && c < kCityMapCols && r >= 0 && r < kCityMapRows) {
            at(c, r) = 5;
        }
    };
    for (int c = 0; c <= 24; ++c) {
        road(c, 21);
    }
    for (int r = 12; r <= 33; ++r) {
        road(8, r);
    }
    for (int c = 8; c <= 18; ++c) {
        road(c, 14);
    }
    for (int r = 14; r <= 21; ++r) {
        road(18, r);
    }
    for (int r = 21; r <= 31; ++r) {
        road(24, r);
    }

    std::ostringstream json;
    json << "{\n"
         << "  \"orientation\": \"orthogonal\",\n"
         << "  \"width\": " << kCityMapCols << ", \"height\": " << kCityMapRows << ",\n"
         << "  \"tilewidth\": " << kCityTile << ", \"tileheight\": " << kCityTile << ",\n"
         << "  \"tilesets\": [{\n"
         << "    \"firstgid\": 1,\n"
         << "    \"name\": \"city_tiles\",\n"
         << "    \"tilewidth\": " << kCityTile << ", \"tileheight\": " << kCityTile << ",\n"
         << "    \"columns\": " << kCitySheetCols << ", \"tilecount\": "
         << kCitySheetCols * 2 << ",\n"
         << "    \"image\": \"city_tiles.png\",\n"
         << "    \"imagewidth\": " << kCitySheetCols * kCityTile
         << ", \"imageheight\": " << 2 * kCityTile << "\n"
         << "  }],\n"
         << "  \"layers\": [{\n"
         << "    \"name\": \"ground\", \"type\": \"tilelayer\", \"visible\": true,\n"
         << "    \"opacity\": 1, \"data\": [";
    for (std::size_t i = 0; i < gids.size(); ++i) {
        if (i % kCityMapCols == 0) {
            json << "\n      ";
        }
        json << gids[i] << (i + 1 == gids.size() ? "\n" : ", ");
    }
    json << "  ]}]\n}\n";

    std::ofstream out(map_path, std::ios::binary);
    out << json.str();
}

// ---------------------------------------------------------------------------
// RTS assets (Phase 41): a terrain tileset, per-unit and per-building sprites
// (white/grey art tinted by owner team colour) and a 64x48 base map with ore
// fields, lakes, rock/tree obstacles and mirrored starter bases.
// ---------------------------------------------------------------------------

constexpr int kRtsTile = 32;
constexpr int kRtsTileCols = 6;
constexpr int kRtsMapCols = 64;
constexpr int kRtsMapRows = 48;

void PaintRtsTileset(std::uint8_t *img) {
    const int sheet_w = kRtsTileCols * kRtsTile;
    auto put = [&](int id, int x, int y, std::uint8_t r, std::uint8_t g,
                   std::uint8_t b, std::uint8_t a = 255) {
        if (x < 0 || y < 0 || x >= kRtsTile || y >= kRtsTile) {
            return;
        }
        const int ox = (id % kRtsTileCols) * kRtsTile;
        const int oy = (id / kRtsTileCols) * kRtsTile;
        std::uint8_t *px = img + (static_cast<size_t>(oy + y) * sheet_w + ox + x) * 4;
        px[0] = r; px[1] = g; px[2] = b; px[3] = a;
    };
    auto fill = [&](int id, int r, int g, int b, int vary, int salt) {
        for (int y = 0; y < kRtsTile; ++y) {
            for (int x = 0; x < kRtsTile; ++x) {
                const int n = static_cast<int>(CityHash(x, y, salt) % (vary * 2 + 1)) - vary;
                put(id, x, y,
                    static_cast<std::uint8_t>(std::clamp(r + n, 0, 255)),
                    static_cast<std::uint8_t>(std::clamp(g + n, 0, 255)),
                    static_cast<std::uint8_t>(std::clamp(b + n, 0, 255)));
            }
        }
    };
    auto box = [&](int id, int x0, int y0, int x1, int y1, std::uint8_t r,
                   std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                put(id, x, y, r, g, b, a);
            }
        }
    };
    auto rock = [&](int id, int cx, int cy, int radius) {
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x * x + y * y <= radius * radius) {
                    const bool lit = x + y < 0 && x * x + y * y < radius * radius - 1;
                    put(id, cx + x, cy + y, lit ? 158 : 112, lit ? 156 : 110,
                        lit ? 148 : 104);
                }
            }
        }
    };

    // 0/1 grass and flowering grass.
    fill(0, 90, 148, 72, 10, 27);
    fill(1, 90, 148, 72, 10, 28);
    for (int i = 0; i < 18; ++i) {
        const int x = static_cast<int>(CityHash(i, 0, 51) % kRtsTile);
        const int y = static_cast<int>(CityHash(i, 1, 51) % kRtsTile);
        put(1, x, y, 236, 228, 166);
        put(1, x + 1, y, 220, 206, 144);
    }

    // 2 dirt.
    fill(2, 148, 122, 86, 9, 29);

    // 3 rock (blocked).
    fill(3, 132, 116, 92, 7, 30);
    rock(3, 10, 12, 6);
    rock(3, 22, 20, 5);
    rock(3, 23, 8, 4);

    // 4 water (blocked).
    fill(4, 42, 92, 152, 6, 31);
    for (int y = 5; y < kRtsTile; y += 9) {
        for (int x = 2; x < kRtsTile - 2; ++x) {
            if ((x + y) % 7 < 4) {
                put(4, x, y, 92, 148, 194);
                put(4, x, y + 1, 70, 126, 178);
            }
        }
    }

    // 5/6 ore (rich and partly mined).
    fill(5, 148, 122, 86, 8, 32);
    fill(6, 148, 122, 86, 8, 33);
    for (int i = 0; i < 22; ++i) {
        const int x = static_cast<int>(CityHash(i, 2, 52) % kRtsTile);
        const int y = static_cast<int>(CityHash(i, 3, 52) % kRtsTile);
        put(5, x, y, 242, 202, 74);
        put(5, x + 1, y, 206, 158, 48);
        put(5, x, y + 1, 224, 176, 58);
    }
    for (int i = 0; i < 8; ++i) {
        const int x = static_cast<int>(CityHash(i, 4, 53) % kRtsTile);
        const int y = static_cast<int>(CityHash(i, 5, 53) % kRtsTile);
        put(6, x, y, 232, 190, 70);
    }

    // 7 concrete base slab.
    fill(7, 126, 128, 126, 4, 34);
    for (int i = 0; i < kRtsTile; i += 8) {
        box(7, i, 0, i, kRtsTile - 1, 108, 110, 108);
        box(7, 0, i, kRtsTile - 1, i, 108, 110, 108);
    }

    // 8 tree (blocked).
    fill(8, 82, 138, 66, 8, 35);
    box(8, 14, 16, 17, 28, 92, 66, 42);
    for (int y = -8; y <= 8; ++y) {
        for (int x = -8; x <= 8; ++x) {
            if (x * x + y * y <= 64) {
                const bool lit = x + y < 0;
                put(8, 15 + x, 12 + y, lit ? 96 : 44, lit ? 152 : 88, lit ? 62 : 44);
            }
        }
    }

    // 9 sand, 10 crater, 11 rubble.
    fill(9, 198, 180, 130, 8, 36);
    fill(10, 92, 82, 70, 7, 37);
    for (int i = 0; i < 16; ++i) {
        const int x = static_cast<int>(CityHash(i, 6, 54) % kRtsTile);
        const int y = static_cast<int>(CityHash(i, 7, 54) % kRtsTile);
        put(10, x, y, 52, 46, 40);
    }
    fill(11, 120, 104, 82, 8, 38);
    for (int i = 0; i < 20; ++i) {
        const int x = static_cast<int>(CityHash(i, 8, 55) % kRtsTile);
        const int y = static_cast<int>(CityHash(i, 9, 55) % kRtsTile);
        put(11, x, y, 76, 68, 56);
        put(11, x + 1, y, 150, 136, 108);
    }
}

void PaintRtsUnit(std::uint8_t *img, int kind) {
    const int w = 32;
    auto put = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                   std::uint8_t a = 255) {
        if (x < 0 || y < 0 || x >= w || y >= 32) {
            return;
        }
        std::uint8_t *px = img + (static_cast<size_t>(y) * w + x) * 4;
        px[0] = r; px[1] = g; px[2] = b; px[3] = a;
    };
    auto rect = [&](int x0, int y0, int x1, int y1, std::uint8_t r,
                    std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                put(x, y, r, g, b, a);
            }
        }
    };
    auto disc = [&](int cx, int cy, int radius, std::uint8_t r, std::uint8_t g,
                    std::uint8_t b) {
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x * x + y * y <= radius * radius) {
                    put(cx + x, cy + y, r, g, b);
                }
            }
        }
    };
    if (kind == 0) { // infantry, viewed from above, rifle pointing right
        rect(12, 18, 19, 24, 188, 188, 182);
        disc(15, 15, 6, 168, 168, 160);
        disc(15, 15, 4, 196, 196, 188);
        rect(7, 20, 11, 23, 140, 140, 134);
        rect(20, 21, 23, 22, 140, 140, 134);
        rect(18, 15, 29, 16, 96, 96, 92);
        rect(19, 14, 21, 14, 120, 120, 112);
    } else if (kind == 1) { // rocket soldier
        rect(10, 16, 21, 25, 180, 180, 174);
        disc(15, 14, 6, 166, 166, 158);
        rect(20, 12, 27, 20, 96, 96, 92);
        rect(22, 12, 26, 13, 120, 120, 114);
        rect(20, 19, 27, 20, 70, 70, 68);
        rect(8, 18, 13, 24, 132, 132, 126);
    } else if (kind == 2) { // tank
        rect(7, 9, 24, 25, 176, 176, 168);
        rect(7, 9, 24, 11, 92, 92, 88);
        rect(7, 23, 24, 25, 92, 92, 88);
        rect(10, 12, 21, 22, 198, 198, 190);
        disc(16, 17, 7, 170, 170, 162);
        disc(16, 17, 5, 204, 204, 196);
        rect(15, 2, 17, 12, 120, 120, 114);
        rect(14, 10, 18, 12, 150, 150, 142);
        rect(10, 13, 12, 15, 130, 130, 124);
    } else { // harvester
        rect(6, 8, 25, 26, 178, 174, 160);
        rect(8, 10, 23, 18, 196, 192, 176);
        rect(11, 20, 20, 25, 120, 116, 104);
        for (int i = 0; i < 10; ++i) {
            put(9 + (i % 5) * 3, 21 + (i / 5) * 3, 236, 198, 76);
        }
        rect(6, 8, 25, 9, 92, 90, 82);
        rect(6, 25, 25, 26, 92, 90, 82);
        rect(12, 2, 19, 7, 166, 162, 148);
        disc(16, 6, 4, 184, 180, 164);
    }
}

void PaintRtsBuilding(std::uint8_t *img, int kind) {
    const int w = 64;
    auto put = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                   std::uint8_t a = 255) {
        if (x < 0 || y < 0 || x >= w || y >= 64) {
            return;
        }
        std::uint8_t *px = img + (static_cast<size_t>(y) * w + x) * 4;
        px[0] = r; px[1] = g; px[2] = b; px[3] = a;
    };
    auto rect = [&](int x0, int y0, int x1, int y1, std::uint8_t r,
                    std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                put(x, y, r, g, b, a);
            }
        }
    };
    auto disc = [&](int cx, int cy, int radius, std::uint8_t r, std::uint8_t g,
                    std::uint8_t b) {
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x * x + y * y <= radius * radius) {
                    put(cx + x, cy + y, r, g, b);
                }
            }
        }
    };
    // Every structure sits on a concrete pad.
    rect(5, 43, 58, 60, 132, 134, 130);
    rect(5, 43, 58, 45, 158, 160, 154);
    if (kind == 0) { // construction yard
        rect(13, 18, 51, 50, 182, 182, 174);
        rect(13, 18, 51, 20, 218, 218, 208);
        rect(18, 24, 46, 44, 160, 160, 152);
        rect(24, 30, 40, 50, 108, 108, 102);
        rect(30, 8, 33, 32, 214, 196, 92);
        rect(30, 8, 44, 11, 214, 196, 92);
        rect(41, 11, 44, 22, 214, 196, 92);
        rect(48, 40, 54, 52, 120, 120, 114);
    } else if (kind == 1) { // power plant
        rect(12, 26, 52, 52, 170, 172, 168);
        disc(22, 34, 9, 186, 188, 182);
        disc(22, 34, 6, 120, 122, 118);
        disc(42, 34, 9, 186, 188, 182);
        disc(42, 34, 6, 120, 122, 118);
        rect(26, 20, 38, 26, 150, 150, 146);
        // Lightning badge.
        rect(28, 44, 32, 48, 238, 210, 96);
        rect(30, 48, 36, 52, 238, 210, 96);
        rect(28, 52, 32, 56, 238, 210, 96);
    } else if (kind == 2) { // barracks
        rect(8, 24, 56, 52, 178, 180, 172);
        rect(8, 24, 56, 27, 206, 208, 198);
        for (int x = 12; x < 56; x += 8) {
            rect(x, 28, x + 2, 44, 142, 144, 136);
        }
        rect(24, 40, 40, 54, 104, 104, 98);
        rect(46, 12, 48, 26, 128, 128, 122);
        rect(46, 12, 54, 16, 206, 206, 196);
    } else if (kind == 3) { // war factory
        rect(8, 20, 56, 52, 172, 174, 168);
        rect(8, 20, 56, 22, 202, 204, 196);
        rect(14, 30, 50, 54, 108, 110, 106);
        for (int x = 16; x < 50; x += 6) {
            rect(x, 31, x, 53, 88, 90, 86);
        }
        rect(22, 14, 42, 19, 196, 196, 188);
        rect(28, 10, 36, 14, 220, 220, 210);
    } else if (kind == 4) { // refinery
        rect(8, 24, 56, 52, 174, 176, 170);
        disc(20, 34, 11, 196, 198, 192);
        disc(20, 34, 7, 132, 134, 130);
        disc(44, 34, 11, 196, 198, 192);
        disc(44, 34, 7, 132, 134, 130);
        rect(32, 34, 44, 36, 120, 122, 118);
        rect(32, 44, 44, 46, 120, 122, 118);
        rect(16, 16, 48, 20, 150, 152, 146);
        rect(18, 48, 22, 54, 92, 94, 90);
    } else { // turret
        rect(10, 40, 54, 58, 132, 134, 130);
        disc(32, 40, 13, 172, 174, 166);
        disc(32, 40, 9, 206, 206, 198);
        disc(32, 40, 5, 150, 152, 146);
        rect(30, 14, 34, 38, 110, 110, 106);
        rect(28, 34, 36, 40, 160, 160, 152);
        rect(33, 14, 35, 20, 190, 190, 182);
    }
}

void EnsureRtsAssets(bool regen) {
    const std::filesystem::path dir = "assets/rts";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::filesystem::path tiles_path = dir / "tiles.png";
    if (regen || !std::filesystem::exists(tiles_path)) {
        std::vector<std::uint8_t> px(
            static_cast<size_t>(kRtsTileCols * kRtsTile) * (2 * kRtsTile) * 4, 0);
        PaintRtsTileset(px.data());
        stbi_write_png(tiles_path.string().c_str(), kRtsTileCols * kRtsTile,
                       2 * kRtsTile, 4, px.data(), kRtsTileCols * kRtsTile * 4);
    }

    static const char *kUnitNames[] = {"infantry", "rocket", "tank", "harvester"};
    const std::filesystem::path udir = dir / "units";
    std::filesystem::create_directories(udir, ec);
    for (int i = 0; i < 4; ++i) {
        const std::filesystem::path path = udir / (std::string(kUnitNames[i]) + ".png");
        if (regen || !std::filesystem::exists(path)) {
            std::vector<std::uint8_t> px(32 * 32 * 4, 0);
            PaintRtsUnit(px.data(), i);
            stbi_write_png(path.string().c_str(), 32, 32, 4, px.data(), 32 * 4);
        }
    }

    static const char *kBuildNames[] = {"cy", "power", "barracks", "factory",
                                        "refinery", "turret"};
    const std::filesystem::path bdir = dir / "buildings";
    std::filesystem::create_directories(bdir, ec);
    for (int i = 0; i < 6; ++i) {
        const std::filesystem::path path = bdir / (std::string(kBuildNames[i]) + ".png");
        if (regen || !std::filesystem::exists(path)) {
            std::vector<std::uint8_t> px(64 * 64 * 4, 0);
            PaintRtsBuilding(px.data(), i);
            stbi_write_png(path.string().c_str(), 64, 64, 4, px.data(), 64 * 4);
        }
    }

    const std::filesystem::path map_path = dir / "map.json";
    if (!regen && std::filesystem::exists(map_path)) {
        return;
    }
    // gid order: 1 grass, 2 flowers, 3 dirt, 4 rock, 5 water, 6 ore,
    // 7 poor ore, 8 concrete, 9 tree, 10 sand, 11 crater, 12 rubble.
    std::vector<std::uint32_t> gids(static_cast<size_t>(kRtsMapCols) * kRtsMapRows, 1);
    auto at = [&](int c, int r) -> std::uint32_t & {
        return gids[static_cast<size_t>(r) * kRtsMapCols + c];
    };
    for (int r = 0; r < kRtsMapRows; ++r) {
        for (int c = 0; c < kRtsMapCols; ++c) {
            const std::uint32_t h = CityHash(c, r, 61);
            at(c, r) = (h % 5 == 0) ? 2 : 1;
            if (h % 1000 > 958) {
                at(c, r) = 9;
            }
        }
    }
    const auto blob = [&](int cx, int cy, double rx, double ry, std::uint32_t gid) {
        for (int r = 0; r < kRtsMapRows; ++r) {
            for (int c = 0; c < kRtsMapCols; ++c) {
                const double dx = (c - cx) / rx;
                const double dy = (r - cy) / ry;
                if (dx * dx + dy * dy < 1.0) {
                    at(c, r) = gid;
                }
            }
        }
    };
    blob(6, 6, 5.5, 4.0, 5);      // north-west lake
    blob(59, 44, 4.5, 3.0, 5);     // south-east pond
    blob(34, 8, 4.0, 3.0, 4);      // northern rocks
    blob(54, 33, 3.5, 3.0, 4);     // eastern rocks
    blob(17, 20, 3.0, 2.0, 4);     // central rocks
    // Ore fields near both bases and in the centre.
    blob(24, 32, 4.6, 3.4, 7);
    blob(24, 32, 3.0, 2.2, 6);
    blob(39, 16, 4.4, 3.2, 7);
    blob(39, 16, 2.8, 2.0, 6);
    blob(32, 25, 5.4, 3.6, 7);
    blob(32, 25, 3.2, 2.1, 6);
    // Starter bases: clear the pads and lay concrete under the six structures.
    const auto clear_area = [&](int c0, int r0, int c1, int r1) {
        for (int r = r0; r <= r1; ++r) {
            for (int c = c0; c <= c1; ++c) {
                if (c >= 0 && c < kRtsMapCols && r >= 0 && r < kRtsMapRows) {
                    at(c, r) = (CityHash(c, r, 62) % 4 == 0) ? 3 : 1;
                }
            }
        }
    };
    clear_area(6, 30, 19, 43);
    clear_area(42, 5, 58, 19);
    const auto pad = [&](int c, int r) {
        for (int y = r; y < r + 2; ++y) {
            for (int x = c; x < c + 2; ++x) {
                if (x >= 0 && x < kRtsMapCols && y >= 0 && y < kRtsMapRows) {
                    at(x, y) = 8;
                }
            }
        }
    };
    // Player base.
    pad(9, 33);
    pad(13, 33);
    pad(9, 37);
    pad(13, 37);
    pad(17, 33);
    pad(17, 37);
    // Enemy base (the mirror layout, with its command yard facing the centre).
    pad(46, 10);
    pad(50, 10);
    pad(54, 10);
    pad(46, 14);
    pad(50, 14);
    pad(54, 14);

    std::ostringstream json;
    json << "{\n"
         << "  \"orientation\": \"orthogonal\",\n"
         << "  \"width\": " << kRtsMapCols << ", \"height\": " << kRtsMapRows << ",\n"
         << "  \"tilewidth\": " << kRtsTile << ", \"tileheight\": " << kRtsTile << ",\n"
         << "  \"tilesets\": [{\n"
         << "    \"firstgid\": 1,\n"
         << "    \"name\": \"rts_tiles\",\n"
         << "    \"tilewidth\": " << kRtsTile << ", \"tileheight\": " << kRtsTile << ",\n"
         << "    \"columns\": " << kRtsTileCols << ", \"tilecount\": "
         << kRtsTileCols * 2 << ",\n"
         << "    \"image\": \"tiles.png\",\n"
         << "    \"imagewidth\": " << kRtsTileCols * kRtsTile
         << ", \"imageheight\": " << 2 * kRtsTile << ",\n"
         << "    \"tiles\": [\n"
         << "      {\"id\": 3, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 4, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 8, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]}\n"
         << "    ]\n"
         << "  }],\n"
         << "  \"layers\": [{\n"
         << "    \"name\": \"ground\", \"type\": \"tilelayer\", \"visible\": true,\n"
         << "    \"opacity\": 1, \"data\": [";
    for (std::size_t i = 0; i < gids.size(); ++i) {
        if (i % kRtsMapCols == 0) {
            json << "\n      ";
        }
        json << gids[i] << (i + 1 == gids.size() ? "\n" : ", ");
    }
    json << "  ]}]\n}\n";
    std::ofstream out(map_path, std::ios::binary);
    out << json.str();
}

// ---------------------------------------------------------------------------
// Visual-novel assets (Phase 42): painted backgrounds and anime-style bust
// portraits with five expressions for four heroines.
// ---------------------------------------------------------------------------

constexpr int kVnBgW = 960;
constexpr int kVnBgH = 540;
constexpr int kVnPortraitW = 320;
constexpr int kVnPortraitH = 480;

void VnBlend(std::uint8_t *img, int w, int h, int x, int y, int r, int g, int b,
             float a) {
    if (x < 0 || y < 0 || x >= w || y >= h || a <= 0.0f) {
        return;
    }
    std::uint8_t *px = img + (static_cast<size_t>(y) * w + x) * 4;
    const float sa = std::min(a, 1.0f);
    const float da = px[3] / 255.0f;
    const float oa = sa + da * (1.0f - sa);
    if (oa <= 0.0f) {
        return;
    }
    px[0] = static_cast<std::uint8_t>((r * sa + px[0] * da * (1.0f - sa)) / oa);
    px[1] = static_cast<std::uint8_t>((g * sa + px[1] * da * (1.0f - sa)) / oa);
    px[2] = static_cast<std::uint8_t>((b * sa + px[2] * da * (1.0f - sa)) / oa);
    px[3] = static_cast<std::uint8_t>(oa * 255.0f);
}

void VnRect(std::uint8_t *img, int w, int h, int x0, int y0, int x1, int y1,
            int r, int g, int b, float a = 1.0f) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            VnBlend(img, w, h, x, y, r, g, b, a);
        }
    }
}

void VnEllipse(std::uint8_t *img, int w, int h, float cx, float cy, float rx,
               float ry, int r, int g, int b, float a = 1.0f) {
    const int x0 = static_cast<int>(cx - rx - 1.0f);
    const int x1 = static_cast<int>(cx + rx + 1.0f);
    const int y0 = static_cast<int>(cy - ry - 1.0f);
    const int y1 = static_cast<int>(cy + ry + 1.0f);
    const float edge_scale = std::min(rx, ry);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = (static_cast<float>(x) - cx) / rx;
            const float dy = (static_cast<float>(y) - cy) / ry;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d < 1.0f) {
                const float cov = std::clamp((1.0f - d) * edge_scale + 0.5f, 0.0f, 1.0f);
                VnBlend(img, w, h, x, y, r, g, b, a * cov);
            }
        }
    }
}

void VnLine(std::uint8_t *img, int w, int h, float x1, float y1, float x2,
            float y2, float thickness, int r, int g, int b, float a = 1.0f) {
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float len2 = dx * dx + dy * dy;
    const int bx0 = static_cast<int>(std::min(x1, x2) - thickness - 1.0f);
    const int bx1 = static_cast<int>(std::max(x1, x2) + thickness + 1.0f);
    const int by0 = static_cast<int>(std::min(y1, y2) - thickness - 1.0f);
    const int by1 = static_cast<int>(std::max(y1, y2) + thickness + 1.0f);
    for (int y = by0; y <= by1; ++y) {
        for (int x = bx0; x <= bx1; ++x) {
            const float px = static_cast<float>(x);
            const float py = static_cast<float>(y);
            float t = 0.0f;
            if (len2 > 0.0f) {
                t = ((px - x1) * dx + (py - y1) * dy) / len2;
                t = std::clamp(t, 0.0f, 1.0f);
            }
            const float qx = x1 + dx * t;
            const float qy = y1 + dy * t;
            const float d = std::sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy));
            if (d <= thickness * 0.5f + 1.0f) {
                const float cov = std::clamp(thickness * 0.5f - d + 0.5f, 0.0f, 1.0f);
                VnBlend(img, w, h, x, y, r, g, b, a * cov);
            }
        }
    }
}

void VnTri(std::uint8_t *img, int w, int h, float ax, float ay, float bx,
           float by, float cx, float cy, int r, int g, int b, float a = 1.0f) {
    const int x0 = static_cast<int>(std::min({ax, bx, cx}) - 1.0f);
    const int x1 = static_cast<int>(std::max({ax, bx, cx}) + 1.0f);
    const int y0 = static_cast<int>(std::min({ay, by, cy}) - 1.0f);
    const int y1 = static_cast<int>(std::max({ay, by, cy}) + 1.0f);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
            const float d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
            const float d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
            const bool neg = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
            const bool pos = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;
            if (!(neg && pos)) {
                VnBlend(img, w, h, x, y, r, g, b, a);
            }
        }
    }
}

void VnGradient(std::uint8_t *img, int w, int h, int x0, int x1, int y0, int y1,
                int r0, int g0, int b0, int r1, int g1, int b1) {
    for (int y = y0; y <= y1; ++y) {
        const float t = (y1 > y0) ? static_cast<float>(y - y0) / (y1 - y0) : 0.0f;
        const int r = static_cast<int>(r0 + (r1 - r0) * t);
        const int g = static_cast<int>(g0 + (g1 - g0) * t);
        const int b = static_cast<int>(b0 + (b1 - b0) * t);
        VnRect(img, w, h, x0, y, x1, y, r, g, b, 1.0f);
    }
}

void PaintVnSchoolGate(std::uint8_t *img) {
    VnGradient(img, kVnBgW, kVnBgH, 0, kVnBgW - 1, 0, 330, 132, 186, 236, 246, 231, 211);
    VnGradient(img, kVnBgW, kVnBgH, 0, kVnBgW - 1, 331, kVnBgH - 1, 154, 196, 146,
               92, 150, 104);
    VnEllipse(img, kVnBgW, kVnBgH, 780, 120, 110, 90, 255, 244, 206, 0.35f);
    VnEllipse(img, kVnBgW, kVnBgH, 780, 120, 60, 50, 255, 250, 228, 0.55f);
    // Clouds.
    VnEllipse(img, kVnBgW, kVnBgH, 170, 96, 80, 26, 250, 250, 250, 0.75f);
    VnEllipse(img, kVnBgW, kVnBgH, 240, 88, 60, 24, 255, 255, 255, 0.8f);
    VnEllipse(img, kVnBgW, kVnBgH, 520, 130, 96, 24, 250, 250, 252, 0.6f);
    VnEllipse(img, kVnBgW, kVnBgH, 600, 122, 55, 20, 255, 255, 255, 0.7f);
    // Distant school block.
    VnRect(img, kVnBgW, kVnBgH, 300, 232, 660, 380, 226, 214, 188, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 288, 216, 672, 240, 152, 138, 118, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 322, 252, 494, 286, 176, 204, 218, 1.0f);
    for (int i = 0; i < 4; ++i) {
        VnRect(img, kVnBgW, kVnBgH, 340 + i * 52, 300, 372 + i * 52, 340,
               176, 204, 218, 1.0f);
        VnRect(img, kVnBgW, kVnBgH, 340 + i * 52, 352, 372 + i * 52, 374,
               176, 204, 218, 1.0f);
    }
    // Gate and path.
    VnRect(img, kVnBgW, kVnBgH, 430, 300, 470, 430, 238, 232, 218, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 490, 300, 530, 430, 238, 232, 218, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 430, 300, 530, 314, 210, 202, 190, 1.0f);
    VnTri(img, kVnBgW, kVnBgH, 420, 540, 540, 540, 480, 430, 212, 198, 176, 1.0f);
    VnTri(img, kVnBgW, kVnBgH, 420, 540, 480, 430, 390, 540, 196, 182, 162, 1.0f);
    // Cherry trees.
    const auto tree = [&](int cx, int cy, int scale) {
        VnRect(img, kVnBgW, kVnBgH, cx - 7, cy, cx + 7, cy + 150, 110, 78, 56, 1.0f);
        for (int i = 0; i < 9; ++i) {
            const int ox = static_cast<int>(CityHash(i, 0, 71) % (scale * 2)) - scale;
            const int oy = static_cast<int>(CityHash(i, 1, 71) % (scale * 2)) - scale;
            VnEllipse(img, kVnBgW, kVnBgH, static_cast<float>(cx + ox),
                      static_cast<float>(cy + oy), 52.0f, 40.0f, 244, 196, 210, 0.9f);
        }
        for (int i = 0; i < 6; ++i) {
            const int ox = static_cast<int>(CityHash(i, 2, 72) % (scale * 2)) - scale;
            const int oy = static_cast<int>(CityHash(i, 3, 72) % (scale * 2)) - scale;
            VnEllipse(img, kVnBgW, kVnBgH, static_cast<float>(cx + ox),
                      static_cast<float>(cy + oy), 38.0f, 30.0f, 255, 222, 230, 0.85f);
        }
    };
    tree(110, 300, 70);
    tree(850, 290, 76);
    // Fallen petals.
    for (int i = 0; i < 90; ++i) {
        const int x = static_cast<int>(CityHash(i, 4, 73) % kVnBgW);
        const int y = static_cast<int>(CityHash(i, 5, 73) % 470);
        VnEllipse(img, kVnBgW, kVnBgH, static_cast<float>(x), static_cast<float>(y),
                  3.0f, 2.0f, 255, 206, 216, 0.9f);
    }
}

void PaintVnClassroom(std::uint8_t *img) {
    VnGradient(img, kVnBgW, kVnBgH, 0, kVnBgW - 1, 0, 300, 238, 222, 198, 222, 204, 182);
    VnRect(img, kVnBgW, kVnBgH, 0, 300, kVnBgW, 540, 160, 124, 90, 1.0f);
    // Window.
    VnRect(img, kVnBgW, kVnBgH, 52, 52, 430, 282, 90, 78, 62, 1.0f);
    VnGradient(img, kVnBgW, kVnBgH, 64, 424, 64, 270, 150, 196, 238, 208, 226, 232);
    VnEllipse(img, kVnBgW, kVnBgH, 120, 110, 54, 22, 255, 255, 255, 0.8f);
    VnEllipse(img, kVnBgW, kVnBgH, 190, 90, 40, 18, 255, 255, 255, 0.7f);
    VnRect(img, kVnBgW, kVnBgH, 64, 264, 424, 276, 120, 106, 86, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 246, 64, 254, 276, 120, 106, 86, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 64, 168, 424, 174, 120, 106, 86, 1.0f);
    // Blackboard.
    VnRect(img, kVnBgW, kVnBgH, 500, 70, 890, 270, 92, 72, 56, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 514, 84, 876, 256, 52, 78, 66, 1.0f);
    VnLine(img, kVnBgW, kVnBgH, 548, 118, 780, 118, 3.0f, 210, 220, 206, 0.6f);
    VnLine(img, kVnBgW, kVnBgH, 548, 148, 740, 148, 3.0f, 210, 220, 206, 0.5f);
    VnLine(img, kVnBgW, kVnBgH, 548, 178, 810, 178, 3.0f, 210, 220, 206, 0.5f);
    VnRect(img, kVnBgW, kVnBgH, 500, 270, 890, 282, 126, 104, 84, 1.0f);
    // Desks.
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 4; ++col) {
            const int x = 70 + col * 220;
            const int y = 330 + row * 100;
            VnRect(img, kVnBgW, kVnBgH, x, y, x + 150, y + 14, 216, 184, 132, 1.0f);
            VnRect(img, kVnBgW, kVnBgH, x + 10, y + 14, x + 20, y + 70, 150, 118, 82, 1.0f);
            VnRect(img, kVnBgW, kVnBgH, x + 130, y + 14, x + 140, y + 70, 150, 118, 82, 1.0f);
            VnRect(img, kVnBgW, kVnBgH, x + 8, y + 74, x + 142, y + 84, 120, 92, 64, 1.0f);
            VnRect(img, kVnBgW, kVnBgH, x + 40, y + 40, x + 70, y + 44, 190, 160, 112, 1.0f);
        }
    }
    VnEllipse(img, kVnBgW, kVnBgH, 300, 0, 220, 130, 255, 244, 206, 0.22f);
}

void PaintVnLibrary(std::uint8_t *img) {
    VnGradient(img, kVnBgW, kVnBgH, 0, kVnBgW - 1, 0, kVnBgH - 1, 106, 78, 60, 140, 106, 76);
    VnRect(img, kVnBgW, kVnBgH, 0, 360, kVnBgW, 540, 92, 66, 48, 1.0f);
    const auto shelf = [&](int x0, int y0, int x1, int y1) {
        VnRect(img, kVnBgW, kVnBgH, x0, y0, x1, y1, 92, 64, 44, 1.0f);
        VnRect(img, kVnBgW, kVnBgH, x0 + 8, y0 + 8, x1 - 8, y0 + 14, 126, 94, 62, 1.0f);
        for (int i = 0; i < 12; ++i) {
            const int bx = x0 + 12 + i * ((x1 - x0 - 24) / 12);
            const int bh = 26 + static_cast<int>(CityHash(i, 0, 74) % 22);
            const int rr = 120 + static_cast<int>(CityHash(i, 1, 74) % 90);
            const int gg = 80 + static_cast<int>(CityHash(i, 2, 74) % 70);
            const int bb = 70 + static_cast<int>(CityHash(i, 3, 74) % 80);
            VnRect(img, kVnBgW, kVnBgH, bx, y1 - bh, bx + 7, y1 - 2, rr, gg, bb, 1.0f);
        }
    };
    shelf(20, 70, 300, 160);
    shelf(20, 180, 300, 270);
    shelf(20, 290, 300, 380);
    shelf(620, 70, 930, 160);
    shelf(620, 180, 930, 270);
    shelf(620, 290, 930, 380);
    // Window and warm light.
    VnRect(img, kVnBgW, kVnBgH, 380, 60, 560, 270, 74, 56, 42, 1.0f);
    VnGradient(img, kVnBgW, kVnBgH, 392, 548, 72, 258, 244, 216, 160, 210, 164, 112);
    VnRect(img, kVnBgW, kVnBgH, 468, 72, 476, 258, 74, 56, 42, 1.0f);
    VnEllipse(img, kVnBgW, kVnBgH, 470, 150, 190, 160, 255, 224, 160, 0.18f);
    // Reading table.
    VnRect(img, kVnBgW, kVnBgH, 330, 370, 640, 398, 152, 110, 74, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 344, 398, 360, 470, 118, 84, 58, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 610, 398, 626, 470, 118, 84, 58, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 462, 356, 512, 372, 232, 226, 210, 1.0f);
    VnLine(img, kVnBgW, kVnBgH, 486, 360, 486, 370, 2.0f, 160, 120, 90, 1.0f);
}

void PaintVnGym(std::uint8_t *img) {
    VnGradient(img, kVnBgW, kVnBgH, 0, kVnBgW - 1, 0, 230, 206, 220, 236, 186, 200, 216);
    VnRect(img, kVnBgW, kVnBgH, 0, 230, kVnBgW, 540, 190, 136, 84, 1.0f);
    for (int i = 0; i < 12; ++i) {
        VnLine(img, kVnBgW, kVnBgH, 0, static_cast<float>(250 + i * 26),
               static_cast<float>(kVnBgW), static_cast<float>(250 + i * 26), 1.0f,
               170, 118, 70, 0.8f);
    }
    VnRect(img, kVnBgW, kVnBgH, 0, 0, kVnBgW, 40, 128, 92, 58, 1.0f);
    VnEllipse(img, kVnBgW, kVnBgH, 480, 40, 240, 26, 148, 110, 70, 1.0f);
    // Bleachers.
    for (int i = 0; i < 4; ++i) {
        VnRect(img, kVnBgW, kVnBgH, 40, 130 + i * 24, 280, 150 + i * 24,
               126, 96, 66, 1.0f);
    }
    // Hoop and court lines.
    VnRect(img, kVnBgW, kVnBgH, 830, 30, 840, 170, 110, 110, 108, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 800, 150, 880, 158, 200, 200, 198, 1.0f);
    VnEllipse(img, kVnBgW, kVnBgH, 840, 172, 26, 6, 220, 90, 60, 1.0f);
    VnLine(img, kVnBgW, kVnBgH, 480, 230, 480, 540, 4.0f, 232, 226, 214, 0.7f);
    VnEllipse(img, kVnBgW, kVnBgH, 480, 360, 120, 60, 232, 226, 214, 0.25f);
    VnEllipse(img, kVnBgW, kVnBgH, 480, 360, 30, 16, 226, 224, 214, 0.5f);
    // Banner.
    VnRect(img, kVnBgW, kVnBgH, 300, 60, 660, 96, 190, 64, 66, 1.0f);
    for (int i = 0; i < 5; ++i) {
        VnEllipse(img, kVnBgW, kVnBgH, 340 + i * 64, 78, 12, 12, 240, 220, 160, 0.9f);
    }
}

void PaintVnPark(std::uint8_t *img) {
    VnGradient(img, kVnBgW, kVnBgH, 0, kVnBgW - 1, 0, 300, 240, 160, 126, 250, 206, 150);
    VnRect(img, kVnBgW, kVnBgH, 0, 301, kVnBgW, 389, 96, 132, 82, 1.0f);
    VnEllipse(img, kVnBgW, kVnBgH, 700, 250, 90, 80, 255, 214, 130, 0.55f);
    VnEllipse(img, kVnBgW, kVnBgH, 700, 250, 44, 40, 255, 236, 182, 0.8f);
    VnEllipse(img, kVnBgW, kVnBgH, 170, 120, 100, 22, 230, 180, 160, 0.35f);
    VnEllipse(img, kVnBgW, kVnBgH, 440, 100, 130, 20, 240, 190, 160, 0.3f);
    // Distant buildings.
    for (int i = 0; i < 9; ++i) {
        const int x = 260 + i * 70;
        const int hh = 40 + static_cast<int>(CityHash(i, 0, 75) % 70);
        VnRect(img, kVnBgW, kVnBgH, x, 300 - hh, x + 52, 300, 150, 110, 100, 0.7f);
    }
    // Water and bank.
    VnRect(img, kVnBgW, kVnBgH, 0, 390, kVnBgW, 540, 112, 148, 150, 1.0f);
    VnEllipse(img, kVnBgW, kVnBgH, 300, 370, 430, 26, 120, 160, 120, 1.0f);
    for (int i = 0; i < 22; ++i) {
        const int y = 400 + i * 6;
        VnLine(img, kVnBgW, kVnBgH, 60.0f + i * 7.0f, static_cast<float>(y),
               560.0f - i * 5.0f, static_cast<float>(y), 2.0f, 178, 198, 190, 0.35f);
    }
    // Trees and bench.
    const auto tree = [&](int cx, int scale) {
        VnRect(img, kVnBgW, kVnBgH, cx - 6, 300, cx + 6, 420, 92, 66, 46, 1.0f);
        for (int i = 0; i < 7; ++i) {
            const int ox = static_cast<int>(CityHash(i, 1, 76) % (scale * 2)) - scale;
            VnEllipse(img, kVnBgW, kVnBgH, static_cast<float>(cx + ox),
                      310.0f + static_cast<float>(i % 3) * 18.0f, 46.0f, 38.0f,
                      70, 108, 70, 0.95f);
        }
    };
    tree(110, 60);
    tree(850, 66);
    VnRect(img, kVnBgW, kVnBgH, 680, 380, 820, 396, 128, 94, 66, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 690, 396, 702, 440, 100, 74, 52, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 800, 396, 812, 440, 100, 74, 52, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 680, 372, 820, 382, 148, 110, 78, 1.0f);
}

void PaintVnCafe(std::uint8_t *img) {
    VnGradient(img, kVnBgW, kVnBgH, 0, kVnBgW - 1, 0, 306, 120, 84, 64, 168, 122, 88);
    VnRect(img, kVnBgW, kVnBgH, 0, 306, kVnBgW, 540, 112, 78, 58, 1.0f);
    // Window with dusk street.
    VnRect(img, kVnBgW, kVnBgH, 80, 50, 500, 300, 78, 58, 44, 1.0f);
    VnGradient(img, kVnBgW, kVnBgH, 92, 488, 62, 288, 110, 130, 188, 238, 164, 120);
    VnEllipse(img, kVnBgW, kVnBgH, 150, 110, 48, 30, 250, 190, 130, 0.5f);
    for (int i = 0; i < 6; ++i) {
        VnRect(img, kVnBgW, kVnBgH, 110 + i * 60, 210 + (i % 2) * 30, 156 + i * 60,
               288, 64, 52, 52, 0.85f);
    }
    VnRect(img, kVnBgW, kVnBgH, 284, 62, 292, 288, 78, 58, 44, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 92, 176, 488, 184, 78, 58, 44, 1.0f);
    // Counter and lamps.
    VnRect(img, kVnBgW, kVnBgH, 0, 330, 300, 430, 128, 90, 62, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 0, 318, 300, 332, 156, 112, 78, 1.0f);
    for (int i = 0; i < 3; ++i) {
        const int x = 330 + i * 190;
        VnLine(img, kVnBgW, kVnBgH, static_cast<float>(x), 0.0f,
               static_cast<float>(x), 70.0f, 2.0f, 70, 52, 44, 1.0f);
        VnEllipse(img, kVnBgW, kVnBgH, static_cast<float>(x), 78, 22, 16,
                  255, 218, 142, 1.0f);
        VnEllipse(img, kVnBgW, kVnBgH, static_cast<float>(x), 78, 80, 60,
                  255, 220, 150, 0.16f);
    }
    // Tables.
    VnEllipse(img, kVnBgW, kVnBgH, 380, 430, 90, 28, 176, 132, 92, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 382, 430, 396, 520, 112, 80, 56, 1.0f);
    VnEllipse(img, kVnBgW, kVnBgH, 690, 400, 100, 30, 176, 132, 92, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 692, 400, 706, 500, 112, 80, 56, 1.0f);
    VnEllipse(img, kVnBgW, kVnBgH, 660, 392, 22, 14, 240, 232, 214, 1.0f);
    VnEllipse(img, kVnBgW, kVnBgH, 760, 396, 20, 12, 240, 232, 214, 1.0f);
}

void PaintVnRooftop(std::uint8_t *img) {
    VnGradient(img, kVnBgW, kVnBgH, 0, kVnBgW - 1, 0, 300, 62, 72, 126, 250, 172, 130);
    VnEllipse(img, kVnBgW, kVnBgH, 700, 290, 70, 60, 255, 214, 150, 0.65f);
    VnEllipse(img, kVnBgW, kVnBgH, 700, 290, 34, 30, 255, 236, 190, 0.85f);
    VnEllipse(img, kVnBgW, kVnBgH, 200, 140, 80, 18, 230, 170, 170, 0.25f);
    VnEllipse(img, kVnBgW, kVnBgH, 520, 110, 120, 20, 240, 180, 160, 0.2f);
    // City skyline.
    for (int i = 0; i < 14; ++i) {
        const int x = i * 74;
        const int hh = 60 + static_cast<int>(CityHash(i, 0, 77) % 100);
        VnRect(img, kVnBgW, kVnBgH, x, 330 - hh, x + 60, 330, 66, 62, 84, 0.85f);
    }
    VnRect(img, kVnBgW, kVnBgH, 0, 330, kVnBgW, 540, 96, 90, 104, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 0, 330, kVnBgW, 338, 136, 126, 140, 1.0f);
    // Railing.
    for (int x = 20; x < kVnBgW; x += 90) {
        VnRect(img, kVnBgW, kVnBgH, x, 340, x + 8, 500, 72, 74, 84, 1.0f);
    }
    VnRect(img, kVnBgW, kVnBgH, 0, 340, kVnBgW, 352, 78, 80, 92, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 0, 500, kVnBgW, 540, 80, 74, 82, 1.0f);
    VnEllipse(img, kVnBgW, kVnBgH, 160, 510, 90, 24, 112, 100, 112, 0.8f);
    VnEllipse(img, kVnBgW, kVnBgH, 760, 510, 100, 26, 112, 100, 112, 0.8f);
}

void PaintVnFestival(std::uint8_t *img) {
    VnGradient(img, kVnBgW, kVnBgH, 0, kVnBgW - 1, 0, 330, 18, 22, 48, 84, 52, 96);
    const auto star = [&](int x, int y) {
        VnEllipse(img, kVnBgW, kVnBgH, static_cast<float>(x), static_cast<float>(y),
                  2.0f, 2.0f, 255, 250, 220, 0.8f);
    };
    for (int i = 0; i < 80; ++i) {
        star(static_cast<int>(CityHash(i, 0, 78) % kVnBgW),
             static_cast<int>(CityHash(i, 1, 78) % 220));
    }
    // School silhouette.
    VnRect(img, kVnBgW, kVnBgH, 260, 220, 700, 360, 42, 38, 62, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 240, 210, 720, 226, 34, 32, 52, 1.0f);
    for (int i = 0; i < 5; ++i) {
        VnRect(img, kVnBgW, kVnBgH, 300 + i * 80, 250, 336 + i * 80, 286,
               196, 156, 86, 0.5f);
    }
    // Lantern strings.
    for (int s = 0; s < 3; ++s) {
        const int y0 = 60 + s * 40;
        for (int i = 0; i < 14; ++i) {
            const int x = 30 + i * 70;
            const int y = y0 + (i % 2) * 14;
            VnEllipse(img, kVnBgW, kVnBgH, static_cast<float>(x), static_cast<float>(y),
                      8.0f, 12.0f, 246, 88, 70, 1.0f);
            VnEllipse(img, kVnBgW, kVnBgH, static_cast<float>(x), static_cast<float>(y),
                      34.0f, 30.0f, 255, 160, 90, 0.12f);
            VnRect(img, kVnBgW, kVnBgH, x - 1, y - 16, x + 1, y - 10, 240, 200, 120, 1.0f);
        }
    }
    // Ground and stalls.
    VnRect(img, kVnBgW, kVnBgH, 0, 331, kVnBgW, 540, 48, 40, 58, 1.0f);
    VnRect(img, kVnBgW, kVnBgH, 0, 360, kVnBgW, 540, 42, 35, 52, 1.0f);
    for (int i = 0; i < 6; ++i) {
        const int x = 40 + i * 160;
        VnRect(img, kVnBgW, kVnBgH, x, 370, x + 110, 430, 140, 70, 64, 1.0f);
        for (int k = 0; k < 5; ++k) {
            VnRect(img, kVnBgW, kVnBgH, x + k * 22, 366, x + k * 22 + 12, 378,
                   i % 2 == 0 ? 236 : 220, 200, 120, 1.0f);
        }
    }
    // Reflections.
    VnEllipse(img, kVnBgW, kVnBgH, 240, 470, 160, 20, 196, 120, 90, 0.18f);
    VnEllipse(img, kVnBgW, kVnBgH, 720, 480, 180, 18, 196, 120, 90, 0.18f);
}

void PaintVnBackground(std::uint8_t *img, int id) {
    if (id == 0) PaintVnSchoolGate(img);
    else if (id == 1) PaintVnClassroom(img);
    else if (id == 2) PaintVnLibrary(img);
    else if (id == 3) PaintVnGym(img);
    else if (id == 4) PaintVnPark(img);
    else if (id == 5) PaintVnCafe(img);
    else if (id == 6) PaintVnRooftop(img);
    else PaintVnFestival(img);
}

struct VnGirlStyle {
    int style;
    int hair_r, hair_g, hair_b;
    int eye_r, eye_g, eye_b;
    int blazer_r, blazer_g, blazer_b;
    int ribbon_r, ribbon_g, ribbon_b;
};

void PaintVnPortrait(std::uint8_t *img, int girl, int expr) {
    const VnGirlStyle styles[4] = {
        {0, 196, 116, 62, 198, 124, 58, 42, 50, 84, 204, 62, 74},
        {1, 54, 48, 66, 86, 114, 154, 44, 52, 88, 72, 112, 184},
        {2, 176, 150, 214, 142, 112, 190, 50, 48, 88, 222, 180, 240},
        {3, 240, 202, 118, 190, 150, 54, 84, 42, 62, 196, 58, 64},
    };
    const VnGirlStyle &st = styles[girl];
    const int hr = st.hair_r, hg = st.hair_g, hb = st.hair_b;
    const int hdr = hr * 2 / 3, hdg = hg * 2 / 3, hdb = hb * 2 / 3;
    const int hl_r = std::min(255, hr + 38), hl_g = std::min(255, hg + 38),
              hl_b = std::min(255, hb + 38);
    const int skin_r = 255, skin_g = 226, skin_b = 210;
    const int shd_r = 238, shd_g = 190, shd_b = 178;
    const int ink_r = 58, ink_g = 46, ink_b = 58;

    // --- hair silhouette behind the head/body -----------------------------
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 160, 160, 110, 118, hdr, hdg, hdb);
    if (st.style == 0) {
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 160, 218, 86, 62, hdr, hdg, hdb);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 246, 235, 28, 58, hdr, hdg, hdb);
    } else if (st.style == 1) {
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 100, 290, 34, 120, hdr, hdg, hdb);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 220, 290, 34, 120, hdr, hdg, hdb);
    } else if (st.style == 2) {
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 52, 260, 38, 96, hdr, hdg, hdb);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 268, 260, 38, 96, hdr, hdg, hdb);
    } else {
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 92, 300, 40, 118, hdr, hdg, hdb);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 228, 300, 40, 118, hdr, hdg, hdb);
    }

    // --- body / uniform -----------------------------------------------------
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 160, 410, 162, 150, st.blazer_r,
              st.blazer_g, st.blazer_b);
    VnRect(img, kVnPortraitW, kVnPortraitH, 20, 410, 300, 479, st.blazer_r,
           st.blazer_g, st.blazer_b);
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 160, 258, 22, 40, shd_r, shd_g, shd_b);
    // White shirt wedge and lapels.
    VnTri(img, kVnPortraitW, kVnPortraitH, 160, 268, 126, 340, 194, 340, 244, 240, 232);
    VnTri(img, kVnPortraitW, kVnPortraitH, 160, 268, 92, 330, 138, 352,
          st.blazer_r * 3 / 4, st.blazer_g * 3 / 4, st.blazer_b * 3 / 4);
    VnTri(img, kVnPortraitW, kVnPortraitH, 160, 268, 228, 330, 182, 352,
          st.blazer_r * 3 / 4, st.blazer_g * 3 / 4, st.blazer_b * 3 / 4);
    // Ribbon.
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 143, 326, 16, 11, st.ribbon_r,
              st.ribbon_g, st.ribbon_b);
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 177, 326, 16, 11, st.ribbon_r,
              st.ribbon_g, st.ribbon_b);
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 160, 326, 8, 8, st.ribbon_r * 4 / 5,
              st.ribbon_g * 4 / 5, st.ribbon_b * 4 / 5);

    // --- head ---------------------------------------------------------------
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 160, 178, 64, 82, skin_r, skin_g, skin_b);
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 96, 190, 12, 20, skin_r, skin_g, skin_b);
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 224, 190, 12, 20, skin_r, skin_g, skin_b);
    // Cheek/jaw shading and bang shadow.
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 160, 238, 44, 18, shd_r, shd_g, shd_b, 0.5f);
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 160, 152, 54, 16, shd_r, shd_g, shd_b, 0.45f);

    // --- front hair ---------------------------------------------------------
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 160, 106, 72, 46, hr, hg, hb);
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 124, 130, 30, 40, hr, hg, hb);
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 196, 130, 30, 40, hr, hg, hb);
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 98, 178, 18, 72, hdr, hdg, hdb);
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 222, 178, 18, 72, hdr, hdg, hdb);
    if (st.style == 1) {
        // Straight fringe built from overlapping rounded strands.
        for (int i = 0; i < 5; ++i) {
            VnEllipse(img, kVnPortraitW, kVnPortraitH, 108.0f + i * 26.0f,
                      106.0f, 16.0f, 44.0f, hr, hg, hb);
        }
    } else if (st.style == 2) {
        // Centre-parted fringe.
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 130, 112, 34, 42, hr, hg, hb);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 190, 112, 34, 42, hr, hg, hb);
    } else if (st.style == 3) {
        // Swept fringe.
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 120, 112, 44, 42, hr, hg, hb);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 190, 100, 38, 36, hr, hg, hb);
    }
    // Hair highlight.
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 128, 96, 34, 8, hl_r, hl_g, hl_b, 0.75f);
    VnEllipse(img, kVnPortraitW, kVnPortraitH, 186, 100, 22, 6, hl_r, hl_g, hl_b, 0.6f);

    // --- eyes / expression --------------------------------------------------
    const bool smile = expr == 1;
    const bool blush = expr == 2;
    const bool sad = expr == 3;
    const float eye_ry = smile ? 8.0f : (blush ? 12.0f : 10.5f);
    const auto eye = [&](float ex) {
        const int dark_r = ink_r, dark_g = ink_g, dark_b = ink_b;
        VnEllipse(img, kVnPortraitW, kVnPortraitH, ex, 190, 18.0f,
                  eye_ry + 3.0f, dark_r, dark_g, dark_b);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, ex, 191, 16.0f, eye_ry,
                  250, 250, 252);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, ex, 192, 9.0f, 9.0f,
                  st.eye_r, st.eye_g, st.eye_b);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, ex, 192, 4.0f, 4.0f,
                  40, 32, 46);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, ex - 3.5f, 188, 3.0f, 3.0f,
                  255, 255, 255);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, ex + 3.0f, 196, 2.0f, 2.0f,
                  255, 255, 255, 0.7f);
    };
    eye(128.0f);
    eye(192.0f);
    // Brows.
    if (sad) {
        VnLine(img, kVnPortraitW, kVnPortraitH, 111, 168, 145, 160, 3.0f, ink_r, ink_g, ink_b);
        VnLine(img, kVnPortraitW, kVnPortraitH, 175, 160, 209, 168, 3.0f, ink_r, ink_g, ink_b);
    } else {
        VnLine(img, kVnPortraitW, kVnPortraitH, 110, 165, 145, 160, 3.0f, hdr, hdg, hdb);
        VnLine(img, kVnPortraitW, kVnPortraitH, 175, 160, 210, 165, 3.0f, hdr, hdg, hdb);
    }
    // Nose and mouth.
    VnLine(img, kVnPortraitW, kVnPortraitH, 159, 209, 163, 213, 2.0f, shd_r, shd_g, shd_b, 0.7f);
    if (blush) {
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 160, 228, 7.0f, 9.0f, 120, 60, 72);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 160, 232, 4.0f, 4.0f, 200, 100, 110);
    } else if (smile) {
        VnLine(img, kVnPortraitW, kVnPortraitH, 146, 226, 160, 232, 2.5f, 150, 76, 88);
        VnLine(img, kVnPortraitW, kVnPortraitH, 160, 232, 174, 226, 2.5f, 150, 76, 88);
    } else if (sad) {
        VnLine(img, kVnPortraitW, kVnPortraitH, 146, 232, 160, 226, 2.5f, 150, 76, 88);
        VnLine(img, kVnPortraitW, kVnPortraitH, 160, 226, 174, 232, 2.5f, 150, 76, 88);
    } else {
        VnLine(img, kVnPortraitW, kVnPortraitH, 151, 227, 169, 227, 2.5f, 150, 76, 88);
    }
    if (blush || smile) {
        const float ba = blush ? 0.55f : 0.25f;
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 112, 212, 16.0f, 9.0f, 244, 152, 148, ba);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 208, 212, 16.0f, 9.0f, 244, 152, 148, ba);
    }

    // --- accessories and front hair tails -----------------------------------
    if (st.style == 0) {
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 247, 205, 9.0f, 7.0f, 210, 70, 76);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 258, 250, 20.0f, 42.0f, hr, hg, hb);
    } else if (st.style == 1) {
        // Rectangular lens frames (no line through the iris) and long side hair.
        VnLine(img, kVnPortraitW, kVnPortraitH, 106, 176, 151, 176, 2.0f,
               ink_r, ink_g, ink_b, 0.55f);
        VnLine(img, kVnPortraitW, kVnPortraitH, 106, 204, 151, 204, 2.0f,
               ink_r, ink_g, ink_b, 0.55f);
        VnLine(img, kVnPortraitW, kVnPortraitH, 106, 176, 106, 204, 2.0f,
               ink_r, ink_g, ink_b, 0.55f);
        VnLine(img, kVnPortraitW, kVnPortraitH, 151, 176, 151, 204, 2.0f,
               ink_r, ink_g, ink_b, 0.55f);
        VnLine(img, kVnPortraitW, kVnPortraitH, 169, 176, 214, 176, 2.0f,
               ink_r, ink_g, ink_b, 0.55f);
        VnLine(img, kVnPortraitW, kVnPortraitH, 169, 204, 214, 204, 2.0f,
               ink_r, ink_g, ink_b, 0.55f);
        VnLine(img, kVnPortraitW, kVnPortraitH, 169, 176, 169, 204, 2.0f,
               ink_r, ink_g, ink_b, 0.55f);
        VnLine(img, kVnPortraitW, kVnPortraitH, 214, 176, 214, 204, 2.0f,
               ink_r, ink_g, ink_b, 0.55f);
        VnLine(img, kVnPortraitW, kVnPortraitH, 151, 190, 169, 190, 2.0f,
               ink_r, ink_g, ink_b, 0.55f);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 72, 330, 24.0f, 105.0f, hr, hg, hb);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 248, 330, 24.0f, 105.0f, hr, hg, hb);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 80, 280, 14.0f, 20.0f, hr, hg, hb, 0.8f);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 240, 280, 14.0f, 20.0f, hr, hg, hb, 0.8f);
    } else if (st.style == 2) {
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 44, 300, 30.0f, 100.0f, hr, hg, hb, 0.95f);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 276, 300, 30.0f, 100.0f, hr, hg, hb, 0.95f);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 54, 295, 10.0f, 8.0f, st.ribbon_r,
                  st.ribbon_g, st.ribbon_b);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 266, 295, 10.0f, 8.0f, st.ribbon_r,
                  st.ribbon_g, st.ribbon_b);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 212, 96, 8.0f, 8.0f, 250, 220, 120, 1.0f);
    } else {
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 58, 340, 26.0f, 110.0f, hr, hg, hb, 0.95f);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 262, 340, 26.0f, 110.0f, hr, hg, hb, 0.95f);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 226, 112, 12.0f, 8.0f, st.ribbon_r,
                  st.ribbon_g, st.ribbon_b);
        VnEllipse(img, kVnPortraitW, kVnPortraitH, 246, 120, 8.0f, 6.0f, st.ribbon_r,
                  st.ribbon_g, st.ribbon_b);
    }
}

void EnsureVnAssets(bool regen) {
    const std::filesystem::path dir = "assets/vn";
    std::error_code ec;
    std::filesystem::create_directories(dir / "bg", ec);
    std::filesystem::create_directories(dir / "portraits", ec);

    static const char *kBgNames[] = {"gate", "classroom", "library", "gym",
                                     "park", "cafe", "rooftop", "festival"};
    for (int i = 0; i < 8; ++i) {
        const std::filesystem::path path = dir / "bg" / (std::string(kBgNames[i]) + ".png");
        if (regen || !std::filesystem::exists(path)) {
            std::vector<std::uint8_t> px(kVnBgW * kVnBgH * 4, 0);
            PaintVnBackground(px.data(), i);
            stbi_write_png(path.string().c_str(), kVnBgW, kVnBgH, 4, px.data(),
                           kVnBgW * 4);
        }
    }
    static const char *kGirlNames[] = {"linxia", "suqing", "guyu", "tangxin"};
    static const char *kExprNames[] = {"neutral", "smile", "blush", "sad"};
    for (int g = 0; g < 4; ++g) {
        for (int e = 0; e < 4; ++e) {
            const std::filesystem::path path =
                dir / "portraits" /
                (std::string(kGirlNames[g]) + "_" + kExprNames[e] + ".png");
            if (regen || !std::filesystem::exists(path)) {
                std::vector<std::uint8_t> px(kVnPortraitW * kVnPortraitH * 4, 0);
                PaintVnPortrait(px.data(), g, e);
                stbi_write_png(path.string().c_str(), kVnPortraitW, kVnPortraitH, 4,
                               px.data(), kVnPortraitW * 4);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Procedural one-shot sound effects: mono float PCM at 44.1 kHz, synthesized
// from sine/square/saw chirps so the sample ships without binary assets.
// ---------------------------------------------------------------------------

constexpr int kAudioRate = 44100;
constexpr double kTwoPi = 6.283185307179586;

/// Frequency glides from f0 to f1; wave: 0 sine, 1 square, 2 saw.
std::vector<float> MakeTone(float f0, float f1, float seconds, int wave,
                            float volume, float decay = 4.0f) {
    const auto n = static_cast<size_t>(seconds * kAudioRate);
    std::vector<float> pcm(n);
    double phase = 0.0;
    const size_t attack = static_cast<size_t>(0.002f * kAudioRate);
    for (size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        const float freq = f0 + (f1 - f0) * t;
        phase = std::fmod(phase + kTwoPi * freq / kAudioRate, kTwoPi);

        float s = 0.0f;
        if (wave == 0) {
            s = static_cast<float>(std::sin(phase));
        } else if (wave == 1) {
            s = std::sin(phase) >= 0.0 ? 1.0f : -1.0f;
        } else {
            s = 2.0f * static_cast<float>(phase / kTwoPi) - 1.0f;
        }
        const float attack_gain = attack > 0
            ? std::min(1.0f, static_cast<float>(i) / static_cast<float>(attack))
            : 1.0f;
        pcm[i] = std::clamp(s * volume * attack_gain * std::exp(-decay * t), -1.0f, 1.0f);
    }
    return pcm;
}

std::vector<float> MakeArpeggio(const std::vector<float> &freqs, float note_seconds, float volume) {
    std::vector<float> out;
    for (const float f : freqs) {
        const auto tone = MakeTone(f, f, note_seconds, 1, volume, 5.0f);
        out.insert(out.end(), tone.begin(), tone.end());
    }
    return out;
}

/// UTF-8 language packs for the text/platform demo (generated once).
void EnsureI18nAssets() {
    const std::filesystem::path dir = "assets/lang";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto write = [&dir](const char *name, const char *text) {
        const std::filesystem::path path = dir / name;
        if (std::filesystem::exists(path)) {
            return;
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    };
    write("zh.tsv", R"(# name: 中文
FAKE2D TEXT & PLATFORM	FAKE2D 文本与平台
Text	文本
Language	语言
Switch language	切换语言
Font	字体
CJK font loaded	已加载中文字体
Fallback ASCII only	仅 ASCII 回退字体
User data	用户目录
Saves	存档目录
Config path	配置文件
Save slot	存档槽
Save	保存
Saved bytes	已保存字节
Version	版本
Steam	Steam
Steam unavailable	Steam 不可用
Tooltip	提示
Hover this panel for a tooltip	悬停此面板查看提示
CJK wraps per character; Latin words wrap at spaces.	中文按字换行，Latin words 按空格换行。
Rotated text	旋转文字
Shrink to fit	自动缩字
Press L or the button to switch	按 L 或点击按钮切换
Slot list	槽位列表
)");
    write("en.tsv", R"(# name: English
FAKE2D TEXT & PLATFORM	FAKE2D TEXT & PLATFORM
Text	Text
Language	Language
Switch language	Switch language
Font	Font
CJK font loaded	CJK font loaded
Fallback ASCII only	Fallback ASCII only
User data	User data
Saves	Saves
Config path	Config path
Save slot	Save slot
Save	Save
Saved bytes	Saved bytes
Version	Version
Steam	Steam
Steam unavailable	Steam unavailable
Tooltip	Tooltip
Hover this panel for a tooltip	Hover this panel for a tooltip
CJK wraps per character; Latin words wrap at spaces.	CJK wraps per character; Latin words wrap at spaces.
Rotated text	Rotated text
Shrink to fit	Shrink to fit
Press L or the button to switch	Press L or the button to switch
Slot list	Slot list
)");
}

/// Register the samples' named clips (no-ops when audio is disabled).
void EnsureGameAudio(fake2d::AudioEngine &audio) {
    // No IsEnabled() gate: clips are memory and the mixer runs headless too,
    // so registering them unconditionally keeps every demo's script identical
    // on hosts with and without a device.
    if (audio.ClipCount() > 0) {
        return;
    }
    audio.AddClip("wall", MakeTone(720.0f, 900.0f, 0.05f, 1, 0.18f, 26.0f));
    audio.AddClip("paddle", MakeTone(320.0f, 520.0f, 0.07f, 0, 0.5f, 12.0f));
    audio.AddClip("brick", MakeTone(600.0f, 240.0f, 0.08f, 1, 0.45f, 14.0f));
    audio.AddClip("launch", MakeTone(300.0f, 760.0f, 0.09f, 2, 0.35f, 10.0f));
    audio.AddClip("lose", MakeTone(240.0f, 70.0f, 0.45f, 2, 0.4f, 4.0f));
    audio.AddClip("win", MakeArpeggio({523.25f, 659.25f, 783.99f, 1046.5f}, 0.09f, 0.35f));
    audio.AddClip("coin", MakeArpeggio({880.0f, 1174.7f}, 0.06f, 0.3f));
    audio.AddClip("jump", MakeTone(280.0f, 620.0f, 0.09f, 0, 0.28f, 10.0f));
}

/// Looping theme for the brawler demo (a short arpeggio, restarted by the
/// script with `audio_loop`).
void EnsureBeatEmUpAudio(fake2d::AudioEngine &audio) {
    audio.AddClip("brawler_theme",
                  MakeArpeggio({196.0f, 246.94f, 293.66f, 392.0f,
                                329.63f, 293.66f, 246.94f, 329.63f},
                               0.20f, 0.18f));
}

/// Shmup SFX and the looping stage theme (synthesized, no binary audio).
void EnsureShmupAudio(fake2d::AudioEngine &audio) {
    audio.AddClip("shoot", MakeTone(1150.0f, 620.0f, 0.035f, 0, 0.16f, 30.0f));
    audio.AddClip("laser", MakeTone(1500.0f, 900.0f, 0.05f, 0, 0.14f, 24.0f));
    audio.AddClip("missile", MakeTone(300.0f, 900.0f, 0.08f, 2, 0.12f, 14.0f));
    audio.AddClip("enemy_shot", MakeTone(420.0f, 250.0f, 0.07f, 1, 0.18f, 16.0f));
    audio.AddClip("hit", MakeTone(980.0f, 620.0f, 0.03f, 1, 0.20f, 32.0f));
    audio.AddClip("boom", MakeTone(180.0f, 45.0f, 0.32f, 2, 0.45f, 6.0f));
    audio.AddClip("bomb", MakeTone(260.0f, 50.0f, 0.50f, 2, 0.50f, 3.0f));
    audio.AddClip("power", MakeArpeggio({660.0f, 990.0f, 1320.0f}, 0.06f, 0.28f));
    audio.AddClip("boss", MakeArpeggio({110.0f, 82.0f, 110.0f, 73.0f}, 0.18f, 0.35f));
    audio.AddClip("shmup_theme",
                  MakeArpeggio({196.0f, 246.94f, 293.66f, 392.0f,
                                329.63f, 293.66f, 246.94f, 329.63f},
                               0.16f, 0.16f));
}

/// Snooker table SFX: a soft cue thock, ball clicks, cushion thud, pocket
/// drop plus pot / foul / frame stingers.
void EnsureSnookerAudio(fake2d::AudioEngine &audio) {
    audio.AddClip("cue", MakeTone(520.0f, 170.0f, 0.055f, 2, 0.38f, 16.0f));
    audio.AddClip("click", MakeTone(1500.0f, 900.0f, 0.03f, 0, 0.15f, 30.0f));
    audio.AddClip("cushion", MakeTone(280.0f, 150.0f, 0.06f, 0, 0.20f, 18.0f));
    audio.AddClip("pocket", MakeTone(190.0f, 55.0f, 0.28f, 2, 0.32f, 5.0f));
    audio.AddClip("pot", MakeArpeggio({523.25f, 659.25f, 880.0f}, 0.06f, 0.22f));
    audio.AddClip("foul", MakeArpeggio({330.0f, 262.0f, 196.0f}, 0.11f, 0.20f));
    audio.AddClip("frame", MakeArpeggio({523.25f, 659.25f, 783.99f, 1046.5f}, 0.10f, 0.30f));
}

/// Run-and-gun SFX: four weapon reports, hits, booms, death, pickup, boss and
/// the stage-clear fanfare, plus a looping march-like theme.
void EnsureContraAudio(fake2d::AudioEngine &audio) {
    audio.AddClip("rifle", MakeTone(1000.0f, 420.0f, 0.045f, 1, 0.22f, 26.0f));
    audio.AddClip("mg", MakeTone(900.0f, 500.0f, 0.032f, 1, 0.17f, 30.0f));
    audio.AddClip("spread", MakeTone(620.0f, 300.0f, 0.060f, 1, 0.20f, 20.0f));
    audio.AddClip("laser", MakeTone(1600.0f, 700.0f, 0.090f, 0, 0.18f, 18.0f));
    audio.AddClip("ehit", MakeTone(500.0f, 1100.0f, 0.040f, 0, 0.16f, 20.0f));
    audio.AddClip("jump2", MakeTone(280.0f, 620.0f, 0.090f, 0, 0.25f, 12.0f));
    audio.AddClip("enemy", MakeTone(420.0f, 210.0f, 0.070f, 1, 0.15f, 18.0f));
    audio.AddClip("boom", MakeTone(220.0f, 45.0f, 0.350f, 2, 0.50f, 5.0f));
    audio.AddClip("death", MakeArpeggio({330.0f, 262.0f, 196.0f, 131.0f}, 0.12f, 0.30f));
    audio.AddClip("pickup", MakeArpeggio({660.0f, 880.0f, 1320.0f}, 0.06f, 0.30f));
    audio.AddClip("boss", MakeArpeggio({110.0f, 82.0f, 110.0f, 73.0f}, 0.20f, 0.35f));
    audio.AddClip("clear", MakeArpeggio({523.25f, 659.25f, 783.99f, 1046.5f}, 0.12f, 0.35f));
    audio.AddClip("contra_theme",
                  MakeArpeggio({262.0f, 262.0f, 330.0f, 262.0f,
                                392.0f, 330.0f, 262.0f, 196.0f},
                               0.15f, 0.20f));
}

/// City-builder SFX: tool placement, zoning, demolition, an invalid click and
/// a monthly budget chime, plus a slow looping city theme.
void EnsureCityAudio(fake2d::AudioEngine &audio) {
    audio.AddClip("city_build", MakeTone(760.0f, 460.0f, 0.055f, 1, 0.24f, 20.0f));
    audio.AddClip("city_zone", MakeTone(480.0f, 720.0f, 0.070f, 0, 0.22f, 16.0f));
    audio.AddClip("city_bulldoze", MakeTone(300.0f, 95.0f, 0.140f, 2, 0.30f, 8.0f));
    audio.AddClip("city_error", MakeTone(240.0f, 170.0f, 0.120f, 1, 0.22f, 12.0f));
    audio.AddClip("city_month", MakeArpeggio({659.25f, 783.99f, 987.77f}, 0.055f, 0.18f));
    audio.AddClip("city_power", MakeArpeggio({220.0f, 329.63f, 440.0f}, 0.080f, 0.24f));
    audio.AddClip("city_theme",
                  MakeArpeggio({196.0f, 246.94f, 293.66f, 349.23f,
                                293.66f, 246.94f, 220.0f, 196.0f},
                               0.24f, 0.14f));
}

/// RTS SFX: placement / completion / unit-ready chimes, harvesting cash,
/// rifle, cannon, rocket, explosion, base alarm and a looping march.
void EnsureRtsAudio(fake2d::AudioEngine &audio) {
    audio.AddClip("rts_build", MakeTone(520.0f, 300.0f, 0.09f, 2, 0.30f, 10.0f));
    audio.AddClip("rts_done", MakeArpeggio({523.25f, 783.99f}, 0.06f, 0.26f));
    audio.AddClip("rts_ready", MakeArpeggio({392.0f, 523.25f, 659.25f}, 0.05f, 0.30f));
    audio.AddClip("rts_cash", MakeArpeggio({880.0f, 1174.7f, 1568.0f}, 0.04f, 0.24f));
    audio.AddClip("rts_rifle", MakeTone(1150.0f, 500.0f, 0.035f, 1, 0.18f, 28.0f));
    audio.AddClip("rts_cannon", MakeTone(220.0f, 70.0f, 0.18f, 2, 0.40f, 8.0f));
    audio.AddClip("rts_rocket", MakeTone(700.0f, 1100.0f, 0.08f, 0, 0.20f, 16.0f));
    audio.AddClip("rts_boom", MakeTone(160.0f, 42.0f, 0.42f, 2, 0.55f, 4.0f));
    audio.AddClip("rts_alarm", MakeArpeggio({440.0f, 330.0f, 440.0f, 330.0f}, 0.09f, 0.28f));
    audio.AddClip("rts_theme",
                  MakeArpeggio({261.63f, 293.66f, 329.63f, 392.0f,
                                329.63f, 293.66f, 261.63f, 220.0f},
                               0.18f, 0.14f));
}

/// Visual-novel SFX: dialogue blips, choice clicks, affection chimes and a
/// gentle looping piano-like theme.
void EnsureVnAudio(fake2d::AudioEngine &audio) {
    audio.AddClip("vn_blip", MakeTone(640.0f, 760.0f, 0.022f, 0, 0.10f, 30.0f));
    audio.AddClip("vn_click", MakeTone(520.0f, 860.0f, 0.055f, 0, 0.22f, 18.0f));
    audio.AddClip("vn_choice", MakeTone(420.0f, 620.0f, 0.050f, 1, 0.20f, 18.0f));
    audio.AddClip("vn_aff", MakeArpeggio({659.25f, 783.99f, 987.77f}, 0.055f, 0.24f));
    audio.AddClip("vn_heart", MakeArpeggio({523.25f, 659.25f, 783.99f, 1046.5f}, 0.07f, 0.28f));
    audio.AddClip("vn_sad", MakeArpeggio({392.0f, 329.63f, 261.63f, 196.0f}, 0.11f, 0.22f));
    audio.AddClip("vn_theme",
                  MakeArpeggio({261.63f, 329.63f, 392.0f, 523.25f,
                                392.0f, 329.63f, 293.66f, 261.63f},
                               0.20f, 0.12f));
}

/// Two looping themes for --audio-demo, so the cross-fade has something to
/// cross-fade between. Same rhythm, different key: with the fade slider at 0
/// the switch is meant to be obvious.
void EnsureAudioDemoMusic(fake2d::AudioEngine &audio) {
    audio.AddClip("theme_a", MakeArpeggio({220.0f, 261.63f, 329.63f, 261.63f,
                                           220.0f, 261.63f, 329.63f, 392.0f},
                                          0.25f, 0.30f));
    audio.AddClip("theme_b", MakeArpeggio({392.0f, 493.88f, 587.33f, 493.88f,
                                           392.0f, 493.88f, 587.33f, 659.25f},
                                          0.25f, 0.30f));
}

/// Write mono float PCM as a 16-bit PCM WAV (used to test AddClipWav).
void WriteWav(const std::filesystem::path &path, const std::vector<float> &pcm,
              int sample_rate) {
    std::ofstream out(path, std::ios::binary);
    const std::uint32_t data_size = static_cast<std::uint32_t>(pcm.size() * 2);
    const std::uint32_t riff_size = 36 + data_size;
    auto put16 = [&](std::uint16_t v) {
        char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
        out.write(b, 2);
    };
    auto put32 = [&](std::uint32_t v) {
        char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                     static_cast<char>((v >> 16) & 0xFF), static_cast<char>(v >> 24)};
        out.write(b, 4);
    };
    out.write("RIFF", 4);
    put32(riff_size);
    out.write("WAVEfmt ", 8);
    put32(16);
    put16(1);             // PCM
    put16(1);             // mono
    put32(sample_rate);
    put32(sample_rate * 2); // byte rate
    put16(2);             // block align
    put16(16);            // bits per sample
    out.write("data", 4);
    put32(data_size);
    for (float s : pcm) {
        const int v = static_cast<int>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
        put16(static_cast<std::uint16_t>(v));
    }
}

// ---------------------------------------------------------------------------
// Phase 5 benchmark: thousands of sprites across several textures, comparing
// immediate submission-order flushing with multi-key sorted batching.
// ---------------------------------------------------------------------------



// Physics broadphase benchmark: a tiled static floor plus many dynamic
// circles, compared brute force vs spatial hash grid. Each frame also runs
// one ray cast and one circle overlap over the full body set for reference
// query costs (pure CPU; informational only).


// Physics query self-test: ray casts, shape overlaps and the contact
// lifecycle (began/stayed/ended) against a hand-built scene with exact
// expected values. Prints [phys-query] lines; exits non-zero on failure.
// Headless-safe: never touches the window or renderer.


// Collision-filtering self-test: category/mask bitmasks gate pair
// collisions, contact events and the query surface (ray casts / overlaps).
// Headless CPU-only. Prints [phys-filter] lines; non-zero on fail.


// Rigid-body self-test (Phase 31): mass/inertia from shape, off-centre
// impulses, oriented-box ray casts, friction, restitution, containment
// push-out and a five-box stack. Headless CPU-only. Prints [phys-rigid]
// lines; non-zero on fail.


// Extended rigid-body self-test (Phase 32): kinematic platforms, damping,
// sleeping/waking, joints (distance/rope/revolute/weld, motor and limits),
// joint collision filtering, contact-point payload, fixed rotation and
// substepped anti-tunneling. Headless CPU-only. Prints [phys-extended] lines;
// non-zero on fail.


// UTF-8 / CJK text self-test (Phase 33): dynamic glyph rasterization, CJK
// advances, per-character wrapping and rotated text. A CJK font is optional:
// when none exists on the host the CJK checks are skipped (CI containers).


// Platform self-test (Phase 33): i18n packs, app paths, binary save
// containers, the Steam stub and the shrink-to-fit UI text helpers. Uses a
// throwaway directory under the system temp dir and cleans it up.


// Gameplay self-test (Phase 35): deterministic RNG, easing, entity snapshot,
// input actions, window controls and the text shadow/outline helpers.








// Combat / 2.5D self-test (Phase 36): the global time scale + hit-stop
// plumbing, the widened entity slots and the larger animation clip pool that
// a brawler needs.


// Shmup asset self-test (Phase 37): every sprite sheet, background layer and
// the nearest-filter loader the vertical shooter depends on.


// Shipped-art self-test (Phase 43). Every game demo now renders its scene from
// image assets that live in the repository instead of primitives painted at
// first run. That only holds while the art is actually tracked: if a folder
// falls out of .gitignore again, a fresh clone silently loses it and the demo
// drops back to the painters without failing anything. So walk the shipped art
// tree and require every PNG to load, rather than trusting a green demo run.


// Snooker sprite self-test (Phase 38): the generated shaded ball sprite must
// exist at 64x64, be lit in the centre, transparent in the corners and load
// through both filters; white.png and missing-file handling are checked too.


// Run-and-gun art self-test (Phase 39): both soldier strips, the explosion
// sheet and the generated moon/glow discs (with alpha probes).


// City-builder asset self-test (Phase 40): the generated terrain tileset, the
// building atlas (alpha/shape probes) and the generated Tiled base map.


// RTS asset self-test (Phase 41): terrain tileset, four unit sprites, six
// building sprites, alpha/shape probes and the generated 64x48 battle map.


// Visual-novel art self-test (Phase 42): eight painted backgrounds, sixteen
// heroine portraits and the alpha/shape probes the dialogue view relies on.


// Fixed-step / camera-tooling self-test (Phase 16). Headless CPU-only:
// the zero-dt contact pass used by the accumulator, render interpolation,
// and the camera follow / deadzone / bounds helpers. Prints [timestep] lines;
// non-zero on fail.


// ---------------------------------------------------------------------------
// path demo assets
//
// A deterministic maze (fixed-seed recursive backtracker, then a few walls
// knocked out to create loops so A* has real choices) exported as Tiled JSON.
// Only the wall tile carries `solid`, so the A* grid matches what the player
// collides with. Spawns live in an object layer: player_start, three chasers
// and the goal marker.
// ---------------------------------------------------------------------------

constexpr int kPathCols = 40;
constexpr int kPathRows = 24;
constexpr int kPathTile = 24;
constexpr int kPathSheetCols = 3;
// tile ids (gid = id + 1)
constexpr std::uint32_t kPFloor = 0;
constexpr std::uint32_t kPWall = 1;
constexpr std::uint32_t kPGoal = 2;

void PaintPathSheet(std::uint8_t *sheet) {
    const int sw = kPathSheetCols * kPathTile;
    const auto fill = [&](int id, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        const int ox = id * kPathTile;
        for (int y = 0; y < kPathTile; ++y) {
            for (int x = 0; x < kPathTile; ++x) {
                std::uint8_t *px = sheet + (static_cast<std::size_t>(y) * sw + ox + x) * 4;
                px[0] = r;
                px[1] = g;
                px[2] = b;
                px[3] = 255;
            }
        }
    };
    const auto edge = [&](int id, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        const int ox = id * kPathTile;
        for (int i = 0; i < kPathTile; ++i) {
            const std::size_t row_bytes = static_cast<std::size_t>(sw) * 4;
            std::uint8_t *top = sheet + (static_cast<std::size_t>(ox + i)) * 4;
            std::uint8_t *bot = sheet + static_cast<std::size_t>((kPathTile - 1) * sw + ox + i) * 4;
            std::uint8_t *left = sheet + static_cast<std::size_t>(i * sw + ox) * 4;
            std::uint8_t *right = sheet + static_cast<std::size_t>(i * sw + ox + kPathTile - 1) * 4;
            (void)row_bytes;
            top[0] = r; top[1] = g; top[2] = b;
            bot[0] = r; bot[1] = g; bot[2] = b;
            left[0] = r; left[1] = g; left[2] = b;
            right[0] = r; right[1] = g; right[2] = b;
        }
    };
    fill(kPFloor, 26, 32, 48);
    fill(kPWall, 74, 88, 122);
    edge(kPWall, 40, 50, 74);
    fill(kPGoal, 46, 190, 96);
    edge(kPGoal, 24, 120, 62);
}

void EnsurePathAssets(bool regen = false) {
    const std::filesystem::path dir = "assets";
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    const auto sheet_path = dir / "pf_tiles.png";
    if (regen || !std::filesystem::exists(sheet_path)) {
        std::vector<std::uint8_t> sheet(
            static_cast<size_t>(kPathSheetCols * kPathTile) * kPathTile * 4, 0);
        PaintPathSheet(sheet.data());
        stbi_write_png(sheet_path.string().c_str(), kPathSheetCols * kPathTile,
                       kPathTile, 4, sheet.data(), kPathSheetCols * kPathTile * 4);
    }

    // ---- Maze -------------------------------------------------------------
    std::vector<std::uint32_t> gids(
        static_cast<size_t>(kPathCols) * kPathRows, kPWall + 1);
    auto at = [&](int c, int r) -> std::uint32_t & {
        return gids[static_cast<size_t>(r) * kPathCols + c];
    };

    // Fixed-seed LCG so the level (and every headless screenshot) is stable.
    std::uint32_t seed = 0x9E3779B9u;
    const auto next_rand = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 16) & 0x7FFFu;
    };

    const int cw = (kPathCols - 1) / 2; // 19
    const int ch = (kPathRows - 1) / 2; // 11
    std::vector<char> visited(static_cast<size_t>(cw) * ch, 0);
    std::vector<std::pair<int, int>> stack;
    auto carve = [&](int c, int r) { at(c, r) = kPFloor + 1; };

    auto visit = [&](int cx, int cy) {
        visited[static_cast<size_t>(cy) * cw + cx] = 1;
        carve(cx * 2 + 1, cy * 2 + 1);
        stack.emplace_back(cx, cy);
    };
    visit(0, 0);
    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!stack.empty()) {
        const auto [cx, cy] = stack.back();
        const int start_dir = static_cast<int>(next_rand() % 4);
        bool moved = false;
        for (int k = 0; k < 4 && !moved; ++k) {
            const int d = (start_dir + k) % 4;
            const int nx = cx + dirs[d][0];
            const int ny = cy + dirs[d][1];
            if (nx < 0 || ny < 0 || nx >= cw || ny >= ch) {
                continue;
            }
            if (visited[static_cast<size_t>(ny) * cw + nx]) {
                continue;
            }
            carve(cx * 2 + 1 + dirs[d][0], cy * 2 + 1 + dirs[d][1]);
            visit(nx, ny);
            moved = true;
        }
        if (!moved) {
            stack.pop_back();
        }
    }

    // Knock out ~30% of the remaining interior walls that sit between two
    // corridors: loops instead of a perfect maze, so A* has alternatives and
    // the drawn path visibly cuts corners.
    for (int r = 1; r + 1 < kPathRows; ++r) {
        for (int c = 1; c + 1 < kPathCols; ++c) {
            if (at(c, r) != kPWall + 1) {
                continue;
            }
            const bool h_open = at(c - 1, r) != kPWall + 1 && at(c + 1, r) != kPWall + 1;
            const bool v_open = at(c, r - 1) != kPWall + 1 && at(c, r + 1) != kPWall + 1;
            if ((h_open || v_open) && (next_rand() % 100) < 30) {
                at(c, r) = kPFloor + 1;
            }
        }
    }

    // Goal: the walkable cell closest to the bottom-right corner.
    int goal_c = 1;
    int goal_r = 1;
    int best = -1;
    for (int r = 1; r + 1 < kPathRows; ++r) {
        for (int c = 1; c + 1 < kPathCols; ++c) {
            if (at(c, r) == kPWall + 1) {
                continue;
            }
            const int score = c * 2 + r;
            if (score > best) {
                best = score;
                goal_c = c;
                goal_r = r;
            }
        }
    }
    at(goal_c, goal_r) = kPGoal + 1;

    // Chaser spawns: three walkable cells far from the player start (1,1).
    int spawn_c[3] = {1, 1, 1};
    int spawn_r[3] = {1, 1, 1};
    int spawn_best[3] = {-1, -1, -1};
    for (int r = 1; r + 1 < kPathRows; ++r) {
        for (int c = 1; c + 1 < kPathCols; ++c) {
            if (at(c, r) == kPWall + 1) {
                continue;
            }
            const int dist = c * c + r * r;
            // Keep three distinct "far" cells: best overall, plus the two
            // best cells that are far from the already picked ones.
            for (int i = 0; i < 3; ++i) {
                if (dist > spawn_best[i]) {
                    bool clash = false;
                    for (int j = 0; j < i; ++j) {
                        const int dc = c - spawn_c[j];
                        const int dr = r - spawn_r[j];
                        if (dc * dc + dr * dr < 100) {
                            clash = true;
                        }
                    }
                    if (!clash) {
                        for (int k = 2; k > i; --k) {
                            spawn_best[k] = spawn_best[k - 1];
                            spawn_c[k] = spawn_c[k - 1];
                            spawn_r[k] = spawn_r[k - 1];
                        }
                        spawn_best[i] = dist;
                        spawn_c[i] = c;
                        spawn_r[i] = r;
                        break;
                    }
                }
            }
        }
    }

    std::ostringstream json;
    json << "{\n"
         << "  \"orientation\": \"orthogonal\",\n"
         << "  \"width\": " << kPathCols << ", \"height\": " << kPathRows << ",\n"
         << "  \"tilewidth\": " << kPathTile << ", \"tileheight\": " << kPathTile << ",\n"
         << "  \"tilesets\": [{\n"
         << "    \"firstgid\": 1, \"name\": \"pf_tiles\",\n"
         << "    \"tilewidth\": " << kPathTile << ", \"tileheight\": " << kPathTile << ",\n"
         << "    \"columns\": " << kPathSheetCols << ", \"tilecount\": " << kPathSheetCols << ",\n"
         << "    \"image\": \"pf_tiles.png\",\n"
         << "    \"imagewidth\": " << kPathSheetCols * kPathTile
         << ", \"imageheight\": " << kPathTile << ",\n"
         << "    \"tiles\": [\n"
         << "      {\"id\": 1, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]}\n"
         << "    ]\n"
         << "  }],\n"
         << "  \"layers\": [{\n"
         << "    \"name\": \"world\", \"type\": \"tilelayer\", \"visible\": true,\n"
         << "    \"opacity\": 1, \"data\": [";
    for (std::size_t i = 0; i < gids.size(); ++i) {
        if (i % kPathCols == 0) json << "\n      ";
        json << gids[i] << (i + 1 == gids.size() ? "\n" : ", ");
    }
    const auto point = [](int id, const char *name, int c, int r) {
        std::ostringstream o;
        o << "      {\"id\": " << id << ", \"name\": \"" << name
          << "\", \"type\": \"spawn\", \"x\": " << c * kPathTile + kPathTile / 2
          << ", \"y\": " << r * kPathTile + kPathTile / 2
          << ", \"width\": 0, \"height\": 0, \"rotation\": 0,"
             " \"visible\": true, \"point\": true}";
        return o.str();
    };
    json << "  ]}, {\n"
         << "    \"name\": \"spawns\", \"type\": \"objectgroup\",\n"
         << "    \"draworder\": \"topdown\", \"opacity\": 1, \"visible\": true,\n"
         << "    \"objects\": [\n"
         << point(1, "player_start", 1, 1) << ",\n"
         << point(2, "goal", goal_c, goal_r) << ",\n"
         << point(3, "chaser", spawn_c[0], spawn_r[0]) << ",\n"
         << point(4, "chaser", spawn_c[1], spawn_r[1]) << ",\n"
         << point(5, "chaser", spawn_c[2], spawn_r[2]) << "\n"
         << "    ]\n"
         << "  }]\n}\n";

    const auto level_path = dir / "path.json";
    if (regen || !std::filesystem::exists(level_path)) {
        std::ofstream out(level_path, std::ios::binary);
        out << json.str();
    }
}

// Grid A* self-test (Phase 18). Headless CPU-only: three inline maps cover
// straight runs (string pulling), a forced detour through a gap, unreachable
// goals, corner-cut prevention and the endpoint guards. Prints [path] lines;
// non-zero on fail.


// A UI is driven by a mouse and a keyboard, neither of which exists in a
// headless run — so this test covers everything that is pure computation
// (clamping, ranges, text handling, focus bookkeeping) and asserts that every
// widget produces draw commands without corrupting the batch. Interaction
// itself is exercised interactively through --ui-demo.




// Post-processing and the minimap bake. Everything here is a pixel probe:
// the scene goes into a target, the post pass blits it into a second one
// (or onto the screen), and the result is read back — so no assertion needs
// a mouse, a keyboard, or a visible window.


// Multi-pass chains: one ping-pong pass (rt_copy) plus a resizable target are
// all a bloom needs — scene -> blur H -> blur V -> composite. Everything here
// is a pixel probe, and the blur shader's "u_dir = 0 is a straight copy"
// contract is what makes the passes measurable step by step.


// Masks are the reason an accumulation target is worth keeping around, and
// until now there was no way to *apply* one: alpha covers pixels over and
// additive only brightens, so neither can dim a scene by a per-pixel coverage
// value. Multiply (`result = source * destination`) is that missing piece, and
// because it is an ordinary batch blend it composes **inside** a bound target
// — no Begin/End pair, so unlike rt_copy it is not blocked by the no-nesting
// rule. Everything below is a pixel probe on a target nobody has to see.


// Audio is the one subsystem that used to be untestable without hardware:
// every call was gated on "a device exists", so headless CI exercised none of
// it. The mixer is now device-independent, and this test drives it directly —
// a standalone AudioEngine that was never Init()ed (no device thread is
// advancing cursors behind the assertions).
// Mix `frames` frames out of the engine and return the LAST left sample —
// "what the mixer is putting out after this much time has passed", which is
// what every envelope assertion below is about. The buffer is sized from the
// request, so a long fade cannot overrun it.
float MixLast(fake2d::AudioEngine &audio, int frames) {
    std::vector<float> out(static_cast<std::size_t>(frames) * 2 + 2);
    audio.Mix(out.data(), frames);
    return out[static_cast<std::size_t>(frames - 1) * 2];
}

/// Both channels of the last mixed frame — how pan and distance are checked.
struct Stereo {
    float left = 0.0f;
    float right = 0.0f;
};

Stereo MixStereo(fake2d::AudioEngine &audio, int frames) {
    std::vector<float> out(static_cast<std::size_t>(frames) * 2 + 2);
    audio.Mix(out.data(), frames);
    return {out[static_cast<std::size_t>(frames - 1) * 2],
            out[static_cast<std::size_t>(frames - 1) * 2 + 1]};
}

/// Skeletal animation self-test.
///
/// The rig maths is the part worth gating: a bone hierarchy that composes
/// wrongly, an interpolation that snaps, or an IK solver that mis-reaches all
/// look fine on screen for a frame and wrong for the rest of the session. Most
/// of this builds a rig in code so the expected numbers are known exactly, then
/// loads the shipped soldier to prove the JSON path reaches the same shapes.


/// Lighting self-test. The shadow pass is geometry, so the properties that
/// matter are measured on the light buffer itself instead of trusted from the
/// counters: a pool brightens exactly as far as its radius, a shadow darkens
/// behind its caster, a shadow never escapes the circle of the light that cast
/// it, and a hundred extra casters cost no extra draw calls.


/// Lighting performance gate. What is being claimed is that the pass is
/// *bounded*: pools and shadow quads co-batch, so the number of draw calls does
/// not grow with the number of lights, and the shadow geometry has a ceiling a
/// game can set. The exit code is the gate, so a regression that turns the pass
/// into one draw call per light fails CI on any machine -- which is exactly what
/// it caught while it was being written.






// Defined later in this translation unit (marble asset scaffolding); the
// object-layer test regenerates mario.json to check the production pipeline.
void EnsureMarioAssets(bool regen);

// Tiled object-layer self-test: parsing of objectgroups (points, rects,
// ellipses, polylines, polygons), custom properties and layer/object queries
// against an inline JSON document, plus an end-to-end check of the generated
// assets/mario.json spawns layer. Prints [map-obj] lines; non-zero on fail.
// Headless-safe: parses JSON only, never touches the window or renderer.


// ---------------------------------------------------------------------------
// Phase 74 self-test: flow fields (integrated cost-to-go steering).
//
// A 12x12 map, 16px cells. Column 5 is walled rows 0-10 with a gap at row 11,
// so the right side must detour down, across and back up to reach the goal at
// cell (2,6). gid 1 is solid (tileset tile property), 0 is empty.
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Phase 30 self-test: Tiled tile animation.
//
// The clock is per tile *id*, not per cell (Tiled semantics), so the whole
// feature is one accumulator plus a frame lookup — and the thing that can go
// wrong is subtle: a frame index that matches by tile id breaks on a 0,1,0
// animation, and collision must never follow the animation.
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Asset scaffolding for examples/hello/scripts/map_demo.lua: tilesheet + Tiled JSON.
// Generated on demand so the repo stays binary-asset-free.
// ---------------------------------------------------------------------------

constexpr int kMapTile = 32;
constexpr int kMapCols = 30;
constexpr int kMapRows = 17;
constexpr int kMapSheetCols = 8;

void PutPixel(std::uint8_t *sheet, int tiles_x, int tile_id, int x, int y,
              std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    const int sheet_w = tiles_x * kMapTile;
    const int ox = (tile_id % tiles_x) * kMapTile;
    const int oy = (tile_id / tiles_x) * kMapTile;
    std::uint8_t *px = sheet + (static_cast<size_t>(oy + y) * sheet_w + ox + x) * 4;
    px[0] = r; px[1] = g; px[2] = b; px[3] = a;
}

void PaintTileset(std::uint8_t *sheet) {
    auto fill = [&](int id, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        for (int y = 0; y < kMapTile; ++y) {
            for (int x = 0; x < kMapTile; ++x) {
                PutPixel(sheet, kMapSheetCols, id, x, y, r, g, b);
            }
        }
    };
    auto border = [&](int id, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        for (int i = 0; i < kMapTile; ++i) {
            PutPixel(sheet, kMapSheetCols, id, i, 0, r, g, b);
            PutPixel(sheet, kMapSheetCols, id, i, kMapTile - 1, r, g, b);
            PutPixel(sheet, kMapSheetCols, id, 0, i, r, g, b);
            PutPixel(sheet, kMapSheetCols, id, kMapTile - 1, i, r, g, b);
        }
    };

    // 0 grass top: dirt body with a green cap.
    fill(0, 122, 82, 50);
    for (int x = 0; x < kMapTile; ++x) {
        for (int y = 0; y < 10; ++y) {
            PutPixel(sheet, kMapSheetCols, 0, x, y, 92, 178, 92);
        }
    }
    for (int y = 10; y < kMapTile; ++y) {
        PutPixel(sheet, kMapSheetCols, 0, 4, y, 96, 62, 38);
        PutPixel(sheet, kMapSheetCols, 0, 20, y, 140, 96, 60);
        PutPixel(sheet, kMapSheetCols, 0, 27, y, 96, 62, 38);
    }
    // 1 dirt.
    fill(1, 122, 82, 50);
    for (int y = 4; y < kMapTile; y += 7) {
        for (int x = 3; x < kMapTile; x += 11) {
            PutPixel(sheet, kMapSheetCols, 1, x, y, 96, 62, 38);
            PutPixel(sheet, kMapSheetCols, 1, x + 5, y + 3, 140, 96, 60);
        }
    }
    // 2 stone platform with a darker bevel.
    fill(2, 132, 138, 148);
    border(2, 92, 98, 108);
    for (int i = 4; i < kMapTile - 4; i += 8) {
        PutPixel(sheet, kMapSheetCols, 2, i, 8, 108, 114, 124);
        PutPixel(sheet, kMapSheetCols, 2, i + 3, 20, 108, 114, 124);
    }
    // 3 coin (transparent background).
    for (int y = 0; y < kMapTile; ++y) {
        for (int x = 0; x < kMapTile; ++x) {
            const float dx = static_cast<float>(x) - 15.5f;
            const float dy = static_cast<float>(y) - 15.5f;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float a = std::clamp(11.0f - d + 0.5f, 0.0f, 1.0f);
            if (a > 0.0f) {
                const bool edge = d > 8.5f;
                PutPixel(sheet, kMapSheetCols, 3, x, y,
                        edge ? 196 : 248, edge ? 150 : 216, edge ? 52 : 96,
                        static_cast<std::uint8_t>(a * 255.0f));
            }
        }
    }
    // 4 dark backdrop panel.
    fill(4, 22, 27, 40);
    border(4, 33, 40, 58);
}

void EnsureMapAssets() {
    const std::filesystem::path dir = "assets";
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    const std::filesystem::path sheet_path = dir / "tiles.png";
    if (!std::filesystem::exists(sheet_path)) {
        std::vector<std::uint8_t> sheet(static_cast<size_t>(kMapSheetCols * 2) * kMapTile * kMapTile * 4, 0);
        PaintTileset(sheet.data());
        stbi_write_png(sheet_path.string().c_str(), kMapSheetCols * kMapTile, 2 * kMapTile, 4,
                       sheet.data(), kMapSheetCols * kMapTile * 4);
    }

    // Build the level: walls, ground, platforms, coins, backdrop everywhere.
    std::vector<std::uint32_t> gids(static_cast<size_t>(kMapCols) * kMapRows, 5);
    auto at = [&](int c, int r) -> std::uint32_t & {
        return gids[static_cast<size_t>(r) * kMapCols + c];
    };
    for (int r = 0; r < kMapRows; ++r) {
        at(0, r) = 3;
        at(kMapCols - 1, r) = 3;
    }
    for (int c = 1; c < kMapCols - 1; ++c) {
        at(c, kMapRows - 2) = 1;
        at(c, kMapRows - 1) = 2;
    }
    const auto platform = [&](int r, int c0, int c1) {
        for (int c = c0; c <= c1; ++c) {
            at(c, r) = 3;
        }
    };
    platform(11, 4, 9);
    platform(9, 13, 19);
    platform(12, 22, 26);
    platform(6, 3, 6);
    platform(4, 20, 24);
    const int coins[][2] = {{6, 9}, {16, 7}, {24, 10}, {4, 4}, {22, 2}, {14, 12}};
    for (const auto &coin : coins) {
        at(coin[0], coin[1]) = 4;
    }

    std::ostringstream json;
    json << "{\n"
         << "  \"orientation\": \"orthogonal\",\n"
         << "  \"width\": " << kMapCols << ", \"height\": " << kMapRows << ",\n"
         << "  \"tilewidth\": " << kMapTile << ", \"tileheight\": " << kMapTile << ",\n"
         << "  \"tilesets\": [{\n"
         << "    \"firstgid\": 1,\n"
         << "    \"name\": \"demo_tiles\",\n"
         << "    \"tilewidth\": " << kMapTile << ", \"tileheight\": " << kMapTile << ",\n"
         << "    \"columns\": " << kMapSheetCols << ", \"tilecount\": " << kMapSheetCols * 2 << ",\n"
         << "    \"image\": \"tiles.png\",\n"
         << "    \"imagewidth\": " << kMapSheetCols * kMapTile
         << ", \"imageheight\": " << 2 * kMapTile << ",\n"
         << "    \"tiles\": [\n"
         << "      {\"id\": 0, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 1, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 2, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]}\n"
         << "    ]\n"
         << "  }],\n"
         << "  \"layers\": [{\n"
         << "    \"name\": \"world\", \"type\": \"tilelayer\", \"visible\": true,\n"
         << "    \"opacity\": 1, \"data\": [";
    for (std::size_t i = 0; i < gids.size(); ++i) {
        if (i % kMapCols == 0) {
            json << "\n      ";
        }
        json << gids[i] << (i + 1 == gids.size() ? "\n" : ", ");
    }
    json << "  ]}]\n}\n";

    const std::filesystem::path level_path = dir / "level.json";
    if (!std::filesystem::exists(level_path)) {
        std::ofstream out(level_path, std::ios::binary);
        out << json.str();
    }
}


// ---------------------------------------------------------------------------
// Asset scaffolding for examples/hello/scripts/platformer_demo.lua: level tilesheet,
// player sprite, Tiled JSON and a 22050 Hz looping music WAV.
// ---------------------------------------------------------------------------

constexpr int kPTile = 32;
constexpr int kPCols = 42;
constexpr int kPRows = 17;
constexpr int kPSheetCols = 8;

// Tile gids (firstgid = 1).
constexpr std::uint32_t kGGrass = 1;
constexpr std::uint32_t kGDirt = 2;
constexpr std::uint32_t kGPlank = 3;
constexpr std::uint32_t kGCoin = 4;
constexpr std::uint32_t kGGoal = 5;
constexpr std::uint32_t kGBack = 6;
constexpr std::uint32_t kGSlopeUp = 7;
constexpr std::uint32_t kGSlopeDown = 8;

void PaintPlayer(std::uint8_t *px, int w, int h) {
    // A ball that fills the 20x30 frame (radius 9.5 of a 15px half-height),
    // so the sprite reads at the same scale as the 20x30 physics box. A
    // vertical shade plus two eyes keep it a character rather than a dot.
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float a = std::clamp(9.0f - std::sqrt(
                (x - cx) * (x - cx) + (y - cy) * (y - cy)) + 0.5f, 0.0f, 1.0f);
            const float t = std::clamp((y - (cy - 9.0f)) / 18.0f, 0.0f, 1.0f);
            std::uint8_t *p = px + (static_cast<size_t>(y) * w + x) * 4;
            p[0] = static_cast<std::uint8_t>(110 - 55 * t);
            p[1] = static_cast<std::uint8_t>(215 - 60 * t);
            p[2] = static_cast<std::uint8_t>(245 - 55 * t);
            p[3] = static_cast<std::uint8_t>(a * 255);
        }
    }
    const int eye_y = static_cast<int>(cy) - 3;
    for (int e = 0; e < 2; ++e) {
        const int eye_x = static_cast<int>(cx) + (e == 0 ? -3 : 1);
        for (int yy = eye_y; yy < eye_y + 2; ++yy) {
            for (int xx = eye_x; xx < eye_x + 2; ++xx) {
                if (xx < 0 || xx >= w || yy < 0 || yy >= h) continue;
                std::uint8_t *p = px + (static_cast<size_t>(yy) * w + xx) * 4;
                p[0] = 24; p[1] = 52; p[2] = 86; p[3] = 255;
            }
        }
    }
}

/// Blob walk cycle (80x30, 4 x 20): idle / squash / stretch / airborne
/// stretch. The circle is nearest-neighbour resampled around a vertical
/// center, so the feet stay planted during the squash (cartoon-style).
void PaintPlayerSheet(std::uint8_t *px, int sheet_w, int h) {
    const int W = sheet_w / 4;
    std::vector<std::uint8_t> base(static_cast<size_t>(W) * h * 4, 0);
    PaintPlayer(base.data(), W, h);

    // horizontal scale, vertical scale, vertical center of the transform
    const float kFrames[4][3] = {
        {1.0f, 1.0f, 15.0f},
        {1.16f, 0.82f, 18.0f}, // squash: planted contact
        {0.90f, 1.10f, 14.0f}, // stretch: push-off
        {0.84f, 1.18f, 13.0f}  // airborne
    };

    std::fill(px, px + static_cast<size_t>(sheet_w) * h * 4, 0);
    for (int f = 0; f < 4; ++f) {
        const float sx = kFrames[f][0];
        const float sy = kFrames[f][1];
        const float cyc = kFrames[f][2];
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < W; ++x) {
                int src_x = static_cast<int>(std::lround(W * 0.5f + (x - W * 0.5f) / sx));
                int src_y = static_cast<int>(std::lround(cyc + (y - cyc) / sy));
                src_x = std::clamp(src_x, 0, W - 1);
                src_y = std::clamp(src_y, 0, h - 1);
                const std::uint8_t *s =
                    base.data() + (static_cast<size_t>(src_y) * W + src_x) * 4;
                std::uint8_t *d =
                    px + (static_cast<size_t>(y) * sheet_w + f * W + x) * 4;
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
            }
        }
    }
}

void PaintPlatformerSheet(std::uint8_t *sheet) {
    auto fill = [&](int id, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        for (int y = 0; y < kPTile; ++y) {
            for (int x = 0; x < kPTile; ++x) {
                PutPixel(sheet, kPSheetCols, id, x, y, r, g, b);
            }
        }
    };
    auto specks = [&](int id, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        for (int y = 4; y < kPTile; y += 7) {
            for (int x = 3; x < kPTile; x += 11) {
                PutPixel(sheet, kPSheetCols, id, x, y, r, g, b);
            }
        }
    };
    // 0 grass
    fill(0, 122, 82, 50);
    for (int x = 0; x < kPTile; ++x) {
        for (int y = 0; y < 10; ++y) {
            PutPixel(sheet, kPSheetCols, 0, x, y, 92, 178, 92);
        }
    }
    specks(0, 96, 62, 38);
    // 1 dirt
    fill(1, 122, 82, 50);
    specks(1, 96, 62, 38);
    // 2 one-way plank (transparent, beam across the top)
    for (int x = 0; x < kPTile; ++x) {
        for (int y = 4; y < 12; ++y) {
            PutPixel(sheet, kPSheetCols, 2, x, y, 168, 120, 66);
        }
        PutPixel(sheet, kPSheetCols, 2, x, 4, 214, 172, 104);
    }
    // 3 coin
    for (int y = 0; y < kPTile; ++y) {
        for (int x = 0; x < kPTile; ++x) {
            const float dx = static_cast<float>(x) - 15.5f;
            const float dy = static_cast<float>(y) - 15.5f;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float a = std::clamp(10.0f - d + 0.5f, 0.0f, 1.0f);
            if (a <= 0.0f) continue;
            const bool edge = d > 7.5f;
            PutPixel(sheet, kPSheetCols, 3, x, y,
                     edge ? 196 : 248, edge ? 150 : 224, edge ? 52 : 110,
                     static_cast<std::uint8_t>(a * 255.0f));
        }
    }
    // 4 goal flag
    for (int y = 2; y < 30; ++y) {
        PutPixel(sheet, kPSheetCols, 4, 15, y, 235, 235, 245);
        PutPixel(sheet, kPSheetCols, 4, 16, y, 235, 235, 245);
    }
    for (int y = 4; y < 14; ++y) {
        for (int x = 17; x < 30 - (y - 4) / 2; ++x) {
            PutPixel(sheet, kPSheetCols, 4, x, y, 110, 226, 120);
        }
    }
    // 5 backdrop
    fill(5, 20, 25, 38);
    for (int i = 0; i < kPTile; i += 8) {
        PutPixel(sheet, kPSheetCols, 5, i, i, 28, 35, 52);
        PutPixel(sheet, kPSheetCols, 5, i + 3, i + 5, 28, 35, 52);
    }
    // 6 up slope (triangle rising toward +x, grass cap over dirt)
    // 7 down slope (triangle lowering toward +x)
    for (int id = 6; id <= 7; ++id) {
        for (int y = 0; y < kPTile; ++y) {
            for (int x = 0; x < kPTile; ++x) {
                const int surf = id == 6 ? (kPTile - 1 - x) : x;
                if (y < surf) {
                    continue;
                }
                const bool grass_cap = (y - surf) < 8;
                PutPixel(sheet, kPSheetCols, id, x, y,
                         grass_cap ? 92 : 122,
                         grass_cap ? 178 : 82,
                         grass_cap ? 92 : 50);
            }
        }
    }
}

void EnsurePlatformerAssets() {
    const std::filesystem::path dir = "assets";
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    const std::filesystem::path sheet_path = dir / "p_tiles.png";
    if (!std::filesystem::exists(sheet_path)) {
        std::vector<std::uint8_t> sheet(
            static_cast<size_t>(kPSheetCols * 2) * kPTile * kPTile * 4, 0);
        PaintPlatformerSheet(sheet.data());
        stbi_write_png(sheet_path.string().c_str(), kPSheetCols * kPTile, 2 * kPTile,
                       4, sheet.data(), kPSheetCols * kPTile * 4);
    }
    const std::filesystem::path player_path = dir / "player.png";
    if (!std::filesystem::exists(player_path)) {
        std::vector<std::uint8_t> player(80 * 30 * 4, 0);
        PaintPlayerSheet(player.data(), 80, 30);
        stbi_write_png(player_path.string().c_str(), 80, 30, 4,
                       player.data(), 80 * 4);
    }
    const std::filesystem::path music_path = dir / "music.wav";
    if (!std::filesystem::exists(music_path)) {
        // A 22050 Hz 16-bit WAV exercises decode + resampling + looping.
        std::vector<float> loop;
        const std::vector<float> notes = {523.25f, 659.25f, 783.99f, 659.25f,
                                          587.33f, 698.46f, 880.0f, 698.46f};
        for (int rep = 0; rep < 2; ++rep) {
            for (float f : notes) {
                auto bar = MakeTone(f, f, 0.11f, 1, 0.16f, 16.0f);
                loop.insert(loop.end(), bar.begin(), bar.end());
            }
        }
        WriteWav(music_path, loop, 22050);
    }

    // Level layout.
    std::vector<std::uint32_t> gids(
        static_cast<size_t>(kPCols) * kPRows, kGBack);
    auto at = [&](int c, int r) -> std::uint32_t & {
        return gids[static_cast<size_t>(r) * kPCols + c];
    };
    for (int r = 0; r < kPRows; ++r) {
        at(0, r) = kGGrass;
        at(kPCols - 1, r) = kGGrass;
    }
    for (int c = 1; c < kPCols - 1; ++c) {
        at(c, kPRows - 1) = kGDirt;
        bool gap = (c >= 8 && c <= 9) || (c >= 19 && c <= 20) || (c >= 29 && c <= 30);
        at(c, kPRows - 2) = gap ? kGBack : kGGrass;
    }
    const auto plank = [&](int r, int c0, int c1) {
        for (int c = c0; c <= c1; ++c) at(c, r) = kGPlank;
    };
    plank(12, 5, 7);
    plank(10, 11, 13);
    plank(12, 15, 17);
    plank(9, 21, 23);
    plank(11, 25, 27);
    plank(8, 32, 34);
    plank(13, 37, 38);
    const int coin_cols[] = {6, 12, 16, 22, 26, 33, 10, 24};
    const int coin_rows[] = {10, 8, 10, 7, 9, 6, 13, 12};
    for (std::size_t i = 0; i < std::size(coin_cols); ++i) {
        at(coin_cols[i], coin_rows[i]) = kGCoin;
    }
    at(38, kPRows - 3) = kGGoal;
    // Two 45-degree pyramid bumps (up + down tiles on the row above ground).
    at(24, kPRows - 3) = kGSlopeUp;
    at(25, kPRows - 3) = kGSlopeDown;
    at(33, kPRows - 3) = kGSlopeUp;
    at(34, kPRows - 3) = kGSlopeDown;

    std::ostringstream json;
    json << "{\n"
         << "  \"orientation\": \"orthogonal\",\n"
         << "  \"width\": " << kPCols << ", \"height\": " << kPRows << ",\n"
         << "  \"tilewidth\": " << kPTile << ", \"tileheight\": " << kPTile << ",\n"
         << "  \"tilesets\": [{\n"
         << "    \"firstgid\": 1, \"name\": \"p_tiles\",\n"
         << "    \"tilewidth\": " << kPTile << ", \"tileheight\": " << kPTile << ",\n"
         << "    \"columns\": " << kPSheetCols << ", \"tilecount\": " << kPSheetCols * 2 << ",\n"
         << "    \"image\": \"p_tiles.png\",\n"
         << "    \"imagewidth\": " << kPSheetCols * kPTile
         << ", \"imageheight\": " << 2 * kPTile << ",\n"
         << "    \"tiles\": [\n"
         << "      {\"id\": 0, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 1, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 2, \"properties\": [{\"name\": \"oneway\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 6, \"properties\": [{\"name\": \"slope\", \"type\": \"string\", \"value\": \"up\"}]},\n"
         << "      {\"id\": 7, \"properties\": [{\"name\": \"slope\", \"type\": \"string\", \"value\": \"down\"}]}\n"
         << "    ]\n"
         << "  }],\n"
         << "  \"layers\": [{\n"
         << "    \"name\": \"world\", \"type\": \"tilelayer\", \"visible\": true,\n"
         << "    \"opacity\": 1, \"data\": [";
    for (std::size_t i = 0; i < gids.size(); ++i) {
        if (i % kPCols == 0) json << "\n      ";
        json << gids[i] << (i + 1 == gids.size() ? "\n" : ", ");
    }
    json << "  ]}]\n}\n";

    const std::filesystem::path level_path = dir / "platformer.json";
    if (!std::filesystem::exists(level_path)) {
        std::ofstream out(level_path, std::ios::binary);
        out << json.str();
    }
}

// ---------------------------------------------------------------------------
// Mario demo assets: a SMB 1-1 inspired tile sheet, sprites and level JSON.
// Tile gids (firstgid = 1):
//   1 ground top, 2 ground fill, 3 brick, 4 question, 5 used,
//   6 pipe TL, 7 pipe TR, 8 pipe BL, 9 pipe BR, 10 coin,
//   11 pole top (ball), 12 pole shaft, 13 flag cloth, 14 stone block,
//   15 goomba spawn marker
// ---------------------------------------------------------------------------

constexpr int kMTile = 32;
constexpr int kMCols = 112;
constexpr int kMRows = 15;
constexpr int kMSheetCols = 8;
constexpr int kMSheetRows = 3;

constexpr std::uint32_t kMGroundTop = 1;
constexpr std::uint32_t kMGroundFill = 2;
constexpr std::uint32_t kMBrick = 3;
constexpr std::uint32_t kMQuestion = 4;
constexpr std::uint32_t kMUsed = 5;
constexpr std::uint32_t kMPipeTL = 6;
constexpr std::uint32_t kMPipeTR = 7;
constexpr std::uint32_t kMPipeBL = 8;
constexpr std::uint32_t kMPipeBR = 9;
constexpr std::uint32_t kMCoin = 10;
constexpr std::uint32_t kMPoleTop = 11;
constexpr std::uint32_t kMPole = 12;
constexpr std::uint32_t kMFlag = 13;
constexpr std::uint32_t kMStone = 14;
constexpr std::uint32_t kMGoomba = 15;

void MPut(std::uint8_t *sheet, int id, int x, int y,
          std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    const int sheet_w = kMSheetCols * kMTile;
    const int ox = (id % kMSheetCols) * kMTile;
    const int oy = (id / kMSheetCols) * kMTile;
    std::uint8_t *px = sheet + (static_cast<size_t>(oy + y) * sheet_w + ox + x) * 4;
    px[0] = r; px[1] = g; px[2] = b; px[3] = a;
}

// ---------------------------------------------------------------------------
// Hand-authored pixel art: string rows + char palette. ' ' and '.' stay
// transparent (or leave an already-painted base untouched).
// ---------------------------------------------------------------------------
struct Pix {
    char ch;
    std::uint8_t r, g, b;
};

void BlitArt(std::uint8_t *dst, int stride, int W, int H,
             const std::vector<std::string> &art, const std::vector<Pix> &pal) {
    for (int y = 0; y < H && y < static_cast<int>(art.size()); ++y) {
        for (int x = 0; x < W && x < static_cast<int>(art[y].size()); ++x) {
            const char c = art[y][x];
            if (c == ' ' || c == '.') continue;
            for (const auto &p : pal) {
                if (p.ch == c) {
                    std::uint8_t *q = dst + (static_cast<size_t>(y) * stride + x) * 4;
                    q[0] = p.r; q[1] = p.g; q[2] = p.b; q[3] = 255;
                    break;
                }
            }
        }
    }
}

void SheetArt(std::uint8_t *sheet, int id,
              const std::vector<std::string> &art,
              const std::vector<Pix> &pal) {
    const int sheet_w = kMSheetCols * kMTile;
    std::uint8_t *tile = sheet + (static_cast<size_t>(id / kMSheetCols) * kMTile *
                                      sheet_w +
                                  static_cast<size_t>(id % kMSheetCols) * kMTile) *
                                 4;
    // stride is the full sheet width, not the tile width: tiles share rows,
    // so advancing by 32 would bleed the art across neighbouring tiles.
    BlitArt(tile, sheet_w, kMTile, kMTile, art, pal);
}

void SheetFill(std::uint8_t *sheet, int id,
               std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    for (int y = 0; y < kMTile; ++y)
        for (int x = 0; x < kMTile; ++x)
            MPut(sheet, id, x, y, r, g, b);
}

void PaintMarioSheet(std::uint8_t *sheet) {
    // 1-2 ground: orange earth with brick-course mortar; the top tile gets a
    // sunlit cap so the surface edge reads against the sky.
    const auto ground = [&](int id, bool cap) {
        SheetFill(sheet, id, 214, 128, 48);
        for (int y = 0; y < kMTile; ++y) {
            for (int x = 0; x < kMTile; ++x) {
                const bool mortar = y == 6 || y == 17 || y == 28 ||
                                    (y >= 7 && y <= 16 && x == 15) ||
                                    (y >= 18 && y <= 27 && (x == 5 || x == 25));
                if (mortar) MPut(sheet, id, x, y, 146, 72, 22);
                if ((x + y * 3) % 11 == 0 && !mortar)
                    MPut(sheet, id, x, y, 188, 104, 32);
            }
        }
        if (cap) {
            for (int x = 0; x < kMTile; ++x) {
                MPut(sheet, id, x, 0, 255, 216, 134);
                MPut(sheet, id, x, 1, 250, 192, 96);
                for (int y = 2; y <= 4; ++y)
                    MPut(sheet, id, x, y, 240, 172, 80);
                MPut(sheet, id, x, 5, 226, 146, 60);
            }
            MPut(sheet, id, 3, 2, 255, 226, 158);
            MPut(sheet, id, 20, 1, 255, 226, 158);
            MPut(sheet, id, 27, 3, 255, 226, 158);
        }
    };
    ground(0, true);
    ground(1, false);

    // 3 brick: classic red block, two half-courses per cell
    SheetFill(sheet, 2, 214, 82, 50);
    for (int y = 0; y < kMTile; ++y) {
        for (int x = 0; x < kMTile; ++x) {
            const bool edge = x == 0 || x == kMTile - 1 || y == 0 || y == kMTile - 1;
            const bool mortar = y == 15 ||
                                (y >= 1 && y <= 14 && x == 15) ||
                                (y >= 16 && y <= 30 && (x == 7 || x == 23));
            if (edge || mortar) {
                MPut(sheet, 2, x, y, 96, 32, 20);
            } else if (y == 1 || y == 2 || y == 16 || y == 17 ||
                       (x == 1 && y < 14)) {
                MPut(sheet, 2, x, y, 246, 138, 92);
            } else if (y == 13 || y == 14 || y == 29) {
                MPut(sheet, 2, x, y, 178, 60, 36);
            }
        }
    }

    // 4 question / 5 used: riveted panels with frame and bevel
    const auto panel = [&](int id, std::uint8_t br, std::uint8_t bg,
                           std::uint8_t bb, std::uint8_t dr, std::uint8_t dg,
                           std::uint8_t db, std::uint8_t lr, std::uint8_t lg,
                           std::uint8_t lb) {
        SheetFill(sheet, id, br, bg, bb);
        for (int y = 0; y < kMTile; ++y) {
            for (int x = 0; x < kMTile; ++x) {
                if (x < 2 || x > 29 || y < 2 || y > 29)
                    MPut(sheet, id, x, y, dr, dg, db);
                else if (x == 2 || y == 2)
                    MPut(sheet, id, x, y, lr, lg, lb);
                else if (x == 29 || y == 29)
                    MPut(sheet, id, x, y, static_cast<std::uint8_t>((dr + br) / 2),
                         static_cast<std::uint8_t>((dg + bg) / 2),
                         static_cast<std::uint8_t>((db + bb) / 2));
            }
        }
        const int riv[4][2] = {{4, 4}, {27, 4}, {4, 27}, {27, 27}};
        for (auto &p : riv) {
            MPut(sheet, id, p[0], p[1], dr, dg, db);
            MPut(sheet, id, p[0] - 1, p[1] - 1, lr, lg, lb);
        }
    };
    panel(3, 244, 176, 36, 122, 66, 12, 255, 222, 96);
    panel(4, 170, 104, 38, 104, 60, 16, 214, 150, 82);
    static const char *const kQ[] = {
        "..####..",
        ".##..##.",
        "##....##",
        ".....##.",
        "....##..",
        "...##...",
        "..##....",
        "..#.....",
        "..#.....",
        "...##...",
        "....#...",
        "........",
        "..####..",
    };
    std::vector<std::string> qart;
    for (const char *r : kQ) {
        std::string row(r);
        row = std::string((32 - static_cast<int>(row.size())) / 2, '.') + row;
        while (static_cast<int>(row.size()) < 32) row.push_back('.');
        qart.push_back(row);
    }
    std::vector<std::string> qfinal(32, std::string(32, '.'));
    for (int y = 0; y < static_cast<int>(qart.size()); ++y)
        qfinal[y + 9] = qart[y];
    SheetArt(sheet, 3, qfinal, {{'#', 112, 60, 12}});
    for (int i = 0; i < 5; ++i) MPut(sheet, 3, 6 + i, 5 - i, 255, 230, 140);

    // 6-9 pipes: dark outer edges, lit left stripe, body stays continuous
    // across the two-tile seam.
    const auto pipe_body = [&](int id, bool left) {
        SheetFill(sheet, id, 40, 180, 76);
        for (int y = 0; y < kMTile; ++y) {
            if (left) {
                MPut(sheet, id, 0, y, 14, 84, 34);
                MPut(sheet, id, 1, y, 14, 84, 34);
                MPut(sheet, id, 2, y, 132, 236, 144);
                MPut(sheet, id, 3, y, 176, 248, 180);
                MPut(sheet, id, 4, y, 120, 228, 136);
            } else {
                MPut(sheet, id, 30, y, 14, 84, 34);
                MPut(sheet, id, 31, y, 14, 84, 34);
                MPut(sheet, id, 27, y, 84, 212, 108);
                MPut(sheet, id, 28, y, 104, 222, 120);
                MPut(sheet, id, 29, y, 84, 212, 108);
            }
        }
    };
    pipe_body(5, true);
    pipe_body(6, false);
    pipe_body(7, true);
    pipe_body(8, false);
    // lip caps on the top pair (band spans the seam)
    for (int y = 0; y <= 9; ++y) {
        for (int x = 0; x < kMTile; ++x) {
            if (y <= 1) {
                MPut(sheet, 5, x, y, 14, 84, 34);
                MPut(sheet, 6, x, y, 14, 84, 34);
            } else if (y >= 8) {
                MPut(sheet, 5, x, y, 16, 96, 40);
                MPut(sheet, 6, x, y, 16, 96, 40);
            } else if (x >= 2 && x <= 7) {
                MPut(sheet, 5, x, y, 150, 240, 158);
            } else if (x >= 24 && x <= 29) {
                MPut(sheet, 6, x, y, 116, 230, 132);
            }
        }
    }
    MPut(sheet, 5, 2, 2, 186, 250, 190);
    MPut(sheet, 6, 29, 2, 186, 250, 190);

    // 10 coin (centered oval, transparent corners)
    static const char *const kCoin[] = {
        "....kkkkkk....",
        "..kkllllllkk..",
        ".kllllllllllk.",
        "kllllllllllllk",
        "kloolllllllllk",
        "kloooolllllolk",
        "klooooollllolk",
        "kloooooolllolk",
        "kloooooolllolk",
        "klooooollllolk",
        "kloooolllllolk",
        "kloolllllllllk",
        "kllllllllllllk",
        ".kllllllllllk.",
        "..kkllllllkk..",
        "....kkkkkk....",
    };
    std::vector<std::string> cart;
    for (const char *r : kCoin) {
        std::string row(r);
        const int pad = (32 - static_cast<int>(row.size())) / 2;
        row = std::string(pad, '.') + row;
        while (static_cast<int>(row.size()) < 32) row.push_back('.');
        cart.push_back(row);
    }
    std::vector<std::string> coin_final(32, std::string(32, '.'));
    for (int y = 0; y < static_cast<int>(cart.size()); ++y)
        coin_final[y + 8] = cart[y];
    SheetArt(sheet, 9, coin_final,
             {{'k', 126, 74, 0},
              {'l', 252, 202, 44},
              {'o', 202, 134, 18}});
    for (int y = 3; y <= 12; ++y) {
        MPut(sheet, 9, 11, y + 8, 255, 240, 160);
        MPut(sheet, 9, 12, y + 8, 255, 232, 138);
    }

    // 11 pole top: shaft runs through, ball sits at the tile bottom
    for (int y = 0; y < kMTile; ++y) {
        MPut(sheet, 10, 13, y, 16, 96, 40);
        MPut(sheet, 10, 14, y, 150, 230, 160);
        MPut(sheet, 10, 15, y, 96, 214, 120);
        MPut(sheet, 10, 16, y, 64, 196, 96);
        MPut(sheet, 10, 17, y, 40, 172, 76);
        MPut(sheet, 10, 18, y, 16, 96, 40);
    }
    for (int y = 16; y < kMTile; ++y) {
        for (int x = 0; x < kMTile; ++x) {
            const int dx = x - 16, dy = y - 24;
            const int d2 = dx * dx + dy * dy;
            if (d2 <= 42) MPut(sheet, 10, x, y, 40, 172, 76);
            if (d2 <= 24 && dx <= 1 && dy <= 0)
                MPut(sheet, 10, x, y, 150, 232, 160);
            if (d2 > 42 && d2 <= 56) MPut(sheet, 10, x, y, 14, 80, 34);
        }
    }
    // 12 pole shaft
    for (int y = 0; y < kMTile; ++y) {
        MPut(sheet, 11, 13, y, 16, 96, 40);
        MPut(sheet, 11, 14, y, 150, 230, 160);
        MPut(sheet, 11, 15, y, 96, 214, 120);
        MPut(sheet, 11, 16, y, 64, 196, 96);
        MPut(sheet, 11, 17, y, 40, 172, 76);
        MPut(sheet, 11, 18, y, 16, 96, 40);
    }
    // 13 flag pennant (apex against the pole on the right)
    static const char *const kFlag[] = {
        "gggggggggggggggggggk",
        "ggggggggggggggggggok",
        "kggggggggggggggggok.",
        ".kkkkkkwkkkkkkggok..",
        "..kgggggggggggok....",
        "...kggggggwgggok....",
        "....kggggggggok.....",
        ".....kggggggok......",
        "......kggggok.......",
        ".......kggok........",
        "........kok.........",
    };
    std::vector<std::string> fart;
    for (const char *r : kFlag) fart.emplace_back(r);
    SheetArt(sheet, 12, fart,
             {{'k', 22, 96, 40}, {'g', 58, 200, 96}, {'w', 238, 246, 238}});

    // 14 stone stair block: beveled gray slab
    SheetFill(sheet, 13, 172, 172, 184);
    for (int y = 0; y < kMTile; ++y) {
        for (int x = 0; x < kMTile; ++x) {
            if (x <= 1 || y <= 1) MPut(sheet, 13, x, y, 224, 224, 236);
            if (x >= 30 || y >= 30) MPut(sheet, 13, x, y, 104, 104, 118);
        }
    }
    for (int i = 0; i < 4; ++i) {
        MPut(sheet, 13, 6 + i * 7, 8, 146, 146, 160);
        MPut(sheet, 13, 9 + i * 7, 22, 146, 146, 160);
        MPut(sheet, 13, 6 + i * 7, 7, 196, 196, 208);
    }

    // 15 goomba spawn marker: never rendered; debug-only magenta X
    for (int i = 4; i < kMTile - 4; ++i) {
        MPut(sheet, 14, i, i, 220, 60, 200, 200);
        MPut(sheet, 14, i, kMTile - 1 - i, 220, 60, 200, 200);
    }

}

// ---------------------------------------------------------------------------
// character sprites (native resolution, transparent background)
// ---------------------------------------------------------------------------
void PaintArt(std::uint8_t *px, int w, int h,
              const std::vector<std::string> &art, const std::vector<Pix> &pal) {
    std::fill(px, px + static_cast<size_t>(w) * h * 4, 0);
    BlitArt(px, w, w, h, art, pal);
}

void PaintMarioGoomba(std::uint8_t *px, int w, int h) {
    // k outline, b body, d dark foot, h dome highlight, s skin, w eye white
    static const char *const kG[] = {
        "............................",
        "............................",
        ".........kkkkkk............",
        ".......kkbbbbbbbk..........",
        "......kbbbbhbbbbbk.........",
        ".....kbbbbbbbbbbbk.........",
        "....kbbbhhhhbbbbbbk........",
        "...kbbbbbbbbbbbbbbbk.......",
        "..kbbbbbbbbbbbbbbbbk.......",
        "..kbbbbkkkbbbbkkkbbk.......",
        ".kbbbbkwwwkbkwwwkbbbk......",
        ".kbbbkkwwwkbbkwwwkkbbk.....",
        ".kbbkwwbkkbbkkbwwkbbk......",
        ".kbbwwbbwkbbkbkwbbwwbk.....",
        ".kbwssswwkbbkwwsssswk......",
        ".kbssssssssssssssssk.......",
        ".kssssssssssssssssssk......",
        "..kssssssssssssssssk.......",
        "..kksssssssssssssskk.......",
        "...kkkkkkkkkkkkkkkk........",
        "............................",
        "....kkkkkk...kkkkkk........",
        "...kddddddk.kddddddk.......",
        "..kdddddddd kddddddddk.....",
        "..kddddddddd kddddddddk....",
        ".kddddddddddkddddddddddk...",
        ".kdddddddddddddddddddddk...",
        "..kkkkkkkkkkkkkkkkkkkk.....",
    };
    std::vector<std::string> art;
    for (const char *r : kG) art.emplace_back(r);
    PaintArt(px, w, h, art,
             {{'k', 64, 36, 16},
              {'b', 158, 90, 34},
              {'d', 78, 44, 18},
              {'h', 200, 130, 66},
              {'s', 236, 200, 152},
              {'w', 248, 244, 232}});
}

void PaintMushroom(std::uint8_t *px, int w, int h) {
    // k outline, R red, r dark red, w white spot, s stem, t eye slit
    static const char *const kM[] = {
        "........................",
        ".........kkkk...........",
        ".......kkRRRRkk.........",
        "......kRRRRRRRRk........",
        ".....kRwwwwRRRRRk.......",
        "....kRwwwwwRRwwwwRk.....",
        "...kRwwwwwRRwwwwwwRk....",
        "...kRRRRRRRRRRRRRRk.....",
        "..kRRRRRkRRRRkRRRRRk....",
        "..kRRRRRkkwwkkRRRRRk....",
        "..kRRRRRRkwwkRRRRRRk....",
        "..kRRRRRkRRRRkRRRRRk....",
        "..krrrrrrrrrrrrrrrrk....",
        "...kkkkkkkkkkkkkkkk.....",
        "....ksssssssssssk.......",
        "....ksstkssskttsk.......",
        "....ksttkksskkttsk......",
        "....ksstkssskttsk.......",
        "....ksssssssssssk.......",
        "....ksssssssssssk.......",
        ".....ksssssssssk........",
        "......kkkkkkkkkk........",
        "........................",
        "........................",
    };
    std::vector<std::string> art;
    for (const char *r : kM) art.emplace_back(r);
    PaintArt(px, w, h, art,
             {{'k', 168, 28, 24},
              {'R', 226, 44, 32},
              {'r', 178, 28, 24},
              {'w', 250, 248, 240},
              {'s', 248, 224, 176},
              {'t', 34, 28, 20}});
}

void PaintMarioPlayer(std::uint8_t *px, int w, int h) {
    // k outline, R red, S skin, H hair, B blue, b dark blue, Y button, W shoe
    const std::vector<Pix> pal = {
        {'k', 34, 26, 18}, {'R', 214, 44, 32},
        {'S', 250, 196, 146}, {'H', 116, 64, 26},
        {'B', 40, 88, 208}, {'b', 24, 52, 140},
        {'Y', 244, 196, 40}, {'W', 122, 68, 28},
    };
    if (w == 20 && h == 28) {
        static const char *const kS[] = {
            "....................",
            ".....RRRRRR.........",
            "....RRRRRRRRR.......",
            "...RRRRRRRRRRRR.....",
            "..kRRRRRRRRRRRRRR...",
            "..kHHHRRRRRRRRRRR...",
            "..kHSHHSSSSSSSSS....",
            "..kHSSSSkSSSSSSSS...",
            "..kHSSSSSSSSSSSSSS..",
            "..kHSSSSSSSSSSSSS...",
            "..kHSSSSSSSSSSSSSS..",
            "..kkkSSkkkkkkkkk....",
            "...kkkkkkkkkkkkk....",
            "....RRRRRRRRRR......",
            "...SRRRRBRBRRRRS....",
            "...SRRRRBYBRRRRS....",
            "....BBBBBBBBBB......",
            "....BBBBBBBBBB......",
            "....BBBBBBBBBB......",
            "....BBBBBBBBB.......",
            ".....BBBBBBBB.......",
            ".....BBbbBBB........",
            ".....BB...BB........",
            "....BBB...BBB.......",
            "...WWWW...WWWW......",
            "..WWWWWW.WWWWWW.....",
            ".kWWWWWWkWWWWWWk....",
            "..kkkkkk.kkkkkk.....",
        };
        std::vector<std::string> art;
        for (const char *r : kS) art.emplace_back(r);
        PaintArt(px, w, h, art, pal);
        return;
    }

    // big: 24 x 44
    static const char *const kB[] = {
        "........................",
        "........................",
        ".......RRRRRRRR.........",
        "......RRRRRRRRRRR.......",
        ".....RRRRRRRRRRRRRR.....",
        "....kRRRRRRRRRRRRRRR....",
        "...kRRRRRRRRRRRRRRRRR...",
        "...kHHHRRRRRRRRRRRRRR...",
        "...kHSHHSSSSSSSSSS......",
        "...kHSSSSkSSSSSSSSSS....",
        "...kHSSSSSSSSSSSSSSSS...",
        "...kHSSSSSSSSSSSSSSS....",
        "...kHSSSSSSSSSSSSSSS....",
        "...kHSSSSSSSSSSSSSS.....",
        "...kkkSSkkkkkkkkkkk.....",
        "....kkkkkkkkkkkkkkk.....",
        ".....RRRRRRRRRRRR.......",
        "....SRRRRRRRRRRRRS......",
        "....SRRRRRRRRRRRRS......",
        "...SRRRRBBBBRRRRRS......",
        "...SRRRBBYBBBRRRS.......",
        "....BBBBBBBBBBBB........",
        "....BBBBBBBBBBBB........",
        "....BBBBBBBBBBBB........",
        "....BBBBBBBBBBBB........",
        "....BBBBBBBBBBBB........",
        "....BBBBBBBBBBBB........",
        ".....BBBBBBBBBB.........",
        ".....BBBBBBBBBB.........",
        ".....BBBBBBBBBB.........",
        ".....BBBBBBBBBB.........",
        ".....BBBBBBBBBB.........",
        ".....BBb....bBBB........",
        ".....BB......BBB........",
        ".....BB......BBB........",
        "....BBB......BBBB.......",
        "....BBB......BBBB.......",
        "...WWWW......WWWWW......",
        "..WWWWWW....WWWWWW......",
        ".kWWWWWWk..kWWWWWWk.....",
        ".kWWWWWWk..kWWWWWWk.....",
        "..kkkkkkk..kkkkkkk......",
        "........................",
        "........................",
    };
    std::vector<std::string> art;
    for (const char *r : kB) art.emplace_back(r);
    PaintArt(px, w, h, art, pal);
}

// --- walk-cycle sprite sheets ----------------------------------------------
// Only the standing pose is hand-painted (PaintMarioPlayer /
// PaintMarioGoomba). The moving frames are derived by shifting the opaque
// pixels of the two leg/foot halves, so the art stays a single source of
// truth and the frames always line up pixel-perfectly.

/// Erase every opaque pixel in columns [x0,x1) from row y0 down.
void EraseLegRegion(std::vector<std::uint8_t> &frame,
                    int W, int H, int x0, int x1, int y0) {
    for (int y = y0; y < H; ++y) {
        for (int x = x0; x < x1; ++x) {
            frame[(static_cast<size_t>(y) * W + x) * 4 + 3] = 0;
        }
    }
}

/// Copy opaque pixels of base's columns [x0,x1) (from row y0) offset by
/// (dx,dy) into frame. Pixels leaving the frame are clipped.
void StampShiftedRegion(const std::vector<std::uint8_t> &base,
                        std::vector<std::uint8_t> &frame,
                        int W, int H, int x0, int x1, int y0, int dx, int dy) {
    for (int y = y0; y < H; ++y) {
        for (int x = x0; x < x1; ++x) {
            const size_t si = (static_cast<size_t>(y) * W + x) * 4;
            if (base[si + 3] == 0) continue;
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            const size_t di = (static_cast<size_t>(ny) * W + nx) * 4;
            frame[di + 0] = base[si + 0];
            frame[di + 1] = base[si + 1];
            frame[di + 2] = base[si + 2];
            frame[di + 3] = 255;
        }
    }
}

/// Horizontal 4-frame strip: 0 idle, 1/2 alternating strides, 3 airborne.
/// small form is 80x28 (4 x 20), big form 96x44 (4 x 24).
void PaintMarioPlayerSheet(std::uint8_t *px, int sheet_w, int h) {
    const int W = sheet_w / 4;
    std::vector<std::uint8_t> base(static_cast<size_t>(W) * h * 4, 0);
    PaintMarioPlayer(base.data(), W, h);

    // Split the legs down the middle gap; shifts below are in pixels.
    const int split = (W == 24) ? 11 : 9;
    const int y0 = (W == 24) ? 32 : 20;
    struct Shift {
        int lx, ly, rx, ry; // left half, right half
    };
    // front (right half, art faces right) swings forward +2; back lifts -1.
    const Shift kFrames[4] = {
        {0, 0, 0, 0},   // idle
        {-2, -1, 2, 0}, // stride A: back foot lifts, front foot plants
        {2, 0, -2, -1}, // stride B
        {-1, -2, 2, -1} // airborne: back leg tucked, front leg extended
    };

    std::fill(px, px + static_cast<size_t>(sheet_w) * h * 4, 0);
    for (int f = 0; f < 4; ++f) {
        std::vector<std::uint8_t> frame = base;
        if (f != 0) {
            // Erase both halves before stamping either, so a foot swinging
            // across the middle is not wiped by the other half's erase.
            EraseLegRegion(frame, W, h, 0, split, y0);
            EraseLegRegion(frame, W, h, split, W, y0);
            // Back half first: a crossing front foot wins overlap pixels.
            StampShiftedRegion(base, frame, W, h, split, W, y0,
                               kFrames[f].rx, kFrames[f].ry);
            StampShiftedRegion(base, frame, W, h, 0, split, y0,
                               kFrames[f].lx, kFrames[f].ly);
        }
        for (int y = 0; y < h; ++y) {
            std::memcpy(px + (static_cast<size_t>(y) * sheet_w + f * W) * 4,
                        frame.data() + static_cast<size_t>(y) * W * 4,
                        static_cast<size_t>(W) * 4);
        }
    }
}

/// Horizontal 2-frame goomba strip (56x28): standing/waddle. Frame 1 lifts
/// the rear foot one pixel — enough for a readable side-to-side waddle.
void PaintMarioGoombaSheet(std::uint8_t *px, int sheet_w, int h) {
    const int W = sheet_w / 2;
    std::vector<std::uint8_t> base(static_cast<size_t>(W) * h * 4, 0);
    PaintMarioGoomba(base.data(), W, h);

    constexpr int kSplit = 12;
    constexpr int kFeetTop = 22;

    std::fill(px, px + static_cast<size_t>(sheet_w) * h * 4, 0);
    for (int f = 0; f < 2; ++f) {
        std::vector<std::uint8_t> frame = base;
        if (f == 1) {
            EraseLegRegion(frame, W, h, 0, kSplit, kFeetTop);
            EraseLegRegion(frame, W, h, kSplit, W, kFeetTop);
            StampShiftedRegion(base, frame, W, h, kSplit, W, kFeetTop, 1, 0);
            StampShiftedRegion(base, frame, W, h, 0, kSplit, kFeetTop, -1, -1);
        }
        for (int y = 0; y < h; ++y) {
            std::memcpy(px + (static_cast<size_t>(y) * sheet_w + f * W) * 4,
                        frame.data() + static_cast<size_t>(y) * W * 4,
                        static_cast<size_t>(W) * 4);
        }
    }
}

// `regen` forces the generated assets to be rewritten even when they already
// exist on disk. They are written once and then reused, which keeps start-up
// fast — but it also means an edit to the painters or the level layout below
// has no effect until the stale file is deleted. Pass --regen-assets after
// changing either.
void EnsureMarioAssets(bool regen = false) {
    const std::filesystem::path dir = "assets";
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    const std::filesystem::path sheet_path = dir / "m_tiles.png";
    if (regen || !std::filesystem::exists(sheet_path)) {
        std::vector<std::uint8_t> sheet(
            static_cast<size_t>(kMSheetCols * kMSheetRows) * kMTile * kMTile * 4, 0);
        PaintMarioSheet(sheet.data());
        stbi_write_png(sheet_path.string().c_str(), kMSheetCols * kMTile,
                       kMSheetRows * kMTile, 4, sheet.data(),
                       kMSheetCols * kMTile * 4);
    }
    const auto sprite = [&](const char *name, int w, int h,
                           void (*paint)(std::uint8_t *, int, int)) {
        const auto p = dir / name;
        if (!regen && std::filesystem::exists(p)) return;
        std::vector<std::uint8_t> buf(static_cast<size_t>(w) * h * 4, 0);
        paint(buf.data(), w, h);
        stbi_write_png(p.string().c_str(), w, h, 4, buf.data(), w * 4);
    };
    sprite("m_goomba.png", 56, 28, PaintMarioGoombaSheet);
    sprite("m_mushroom.png", 24, 24, PaintMushroom);
    sprite("m_player_s.png", 80, 28, PaintMarioPlayerSheet);
    sprite("m_player_b.png", 96, 44, PaintMarioPlayerSheet);

    // ---- Level layout (112 x 15) -----------------------------------------
    std::vector<std::uint32_t> gids(
        static_cast<size_t>(kMCols) * kMRows, 0);
    auto at = [&](int c, int r) -> std::uint32_t & {
        return gids[static_cast<size_t>(r) * kMCols + c];
    };
    const auto is_gap = [&](int c) {
        return (c >= 16 && c <= 17) || (c >= 74 && c <= 76);
    };
    for (int c = 0; c < kMCols; ++c) {
        if (!is_gap(c)) {
            at(c, 13) = kMGroundTop;
            at(c, 14) = kMGroundFill;
        }
    }
    // boundaries are open (no side walls in SMB)

    // First ? block (coin), classic lone block
    at(14, 9) = kMQuestion;
    // brick/question cluster: brick ?(mushroom) brick ? brick
    at(20, 9) = kMBrick;
    at(21, 9) = kMQuestion; // mushroom block
    at(22, 9) = kMBrick;
    at(23, 9) = kMQuestion; // coin block
    at(24, 9) = kMBrick;
    // high brick row with coins on top row later
    at(21, 5) = kMBrick;
    at(22, 5) = kMCoin;
    at(23, 5) = kMBrick;
    // post-pipe question blocks
    at(44, 9) = kMQuestion;
    at(45, 9) = kMBrick;
    at(62, 5) = kMBrick;
    at(63, 5) = kMQuestion;
    at(64, 5) = kMBrick;
    at(63, 9) = kMQuestion;

    // pipes: two-cell-wide bodies of increasing height
    auto pipe = [&](int c, int top_row) {
        at(c, top_row) = kMPipeTL;
        at(c + 1, top_row) = kMPipeTR;
        for (int r = top_row + 1; r <= 12; ++r) {
            at(c, r) = kMPipeBL;
            at(c + 1, r) = kMPipeBR;
        }
    };
    pipe(28, 11); // height 2
    pipe(38, 10); // height 3
    pipe(48, 9);  // height 4

    // Loose coins arc over the second gap approach
    if (at(55, 8) == 0) at(55, 8) = kMCoin;
    if (at(56, 7) == 0) at(56, 7) = kMCoin;
    if (at(57, 8) == 0) at(57, 8) = kMCoin;

    // Goomba spawn points live in the "spawns" object layer below (Tiled
    // point objects), not in the tile grid: the script sweeps them into
    // EntityStore slots on every level reset.
    static constexpr int kGoombaCols[] = {22, 40, 51, 53, 66, 84, 86};
    constexpr int kGoombaRow = 12;

    // staircase up (91..94) then down (96..99); col 95 stays flat
    for (int i = 0; i < 4; ++i) {
        for (int r = 12 - i; r <= 12; ++r)
            at(91 + i, r) = kMStone;
    }
    for (int i = 0; i < 4; ++i) {
        for (int r = 12 - (3 - i); r <= 12; ++r)
            at(96 + i, r) = kMStone;
    }

    // flagpole at col 102
    at(102, 3) = kMPoleTop;
    for (int r = 4; r <= 12; ++r)
        at(102, r) = kMPole;
    at(101, 4) = kMFlag;

    // simple castle silhouette of stone beyond the pole
    for (int c = 106; c <= 110; ++c)
        at(c, 12) = kMStone;
    for (int r = 9; r <= 12; ++r)
        at(106, r) = kMStone;
    for (int r = 9; r <= 12; ++r)
        at(110, r) = kMStone;
    for (int c = 107; c <= 109; ++c)
        at(c, 11) = kMStone;
    at(108, 9) = kMStone;
    at(108, 10) = kMStone;

    std::ostringstream json;
    json << "{\n"
         << "  \"orientation\": \"orthogonal\",\n"
         << "  \"width\": " << kMCols << ", \"height\": " << kMRows << ",\n"
         << "  \"tilewidth\": " << kMTile << ", \"tileheight\": " << kMTile << ",\n"
         << "  \"tilesets\": [{\n"
         << "    \"firstgid\": 1, \"name\": \"m_tiles\",\n"
         << "    \"tilewidth\": " << kMTile << ", \"tileheight\": " << kMTile << ",\n"
         << "    \"columns\": " << kMSheetCols
         << ", \"tilecount\": " << kMSheetCols * kMSheetRows << ",\n"
         << "    \"image\": \"m_tiles.png\",\n"
         << "    \"imagewidth\": " << kMSheetCols * kMTile
         << ", \"imageheight\": " << kMSheetRows * kMTile << ",\n"
         << "    \"tiles\": [\n"
         << "      {\"id\": 0, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 1, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 2, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 3, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 4, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 5, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 6, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 7, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 8, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 13, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]}\n"
         << "    ]\n"
         << "  }],\n"
         << "  \"layers\": [{\n"
         << "    \"name\": \"world\", \"type\": \"tilelayer\", \"visible\": true,\n"
         << "    \"opacity\": 1, \"data\": [";
    for (std::size_t i = 0; i < gids.size(); ++i) {
        if (i % kMCols == 0) json << "\n      ";
        json << gids[i] << (i + 1 == gids.size() ? "\n" : ", ");
    }
    json << "  ]}, {\n";
    // Data-only object layer: point objects the Lua game sweeps into
    // entities. Coordinates are world pixels; a point's x/y is the point.
    json << "    \"name\": \"spawns\", \"type\": \"objectgroup\",\n"
         << "    \"draworder\": \"topdown\", \"opacity\": 1, \"visible\": true,\n"
         << "    \"objects\": [\n";
    int obj_id = 1;
    for (int col : kGoombaCols) {
        const int x = col * kMTile + 2;
        const int y = kGoombaRow * kMTile + 4;
        json << "      {\"id\": " << obj_id++
             << ", \"name\": \"goomba\", \"type\": \"enemy\","
                " \"x\": " << x << ", \"y\": " << y
             << ", \"width\": 0, \"height\": 0, \"rotation\": 0,"
                " \"visible\": true, \"point\": true},\n";
    }
    // Player spawn: feet position (2.5 tiles in, standing on the ground).
    json << "      {\"id\": " << obj_id
         << ", \"name\": \"player_start\", \"type\": \"spawn\","
            " \"x\": " << 2.5 * kMTile << ", \"y\": " << 13 * kMTile
         << ", \"width\": 0, \"height\": 0, \"rotation\": 0,"
            " \"visible\": true, \"point\": true}\n";
    json << "    ]\n"
         << "  }]\n}\n";

    const std::filesystem::path level_path = dir / "mario.json";
    if (regen || !std::filesystem::exists(level_path)) {
        std::ofstream out(level_path, std::ios::binary);
        out << json.str();
    }
}

// ---------------------------------------------------------------------------
// slope demo assets
//
// A small "slope lab" instead of a game: three hills that exercise the three
// things worth showing about Tiled slope tiles, side by side on flat ground.
//
//   A  rise 32 (default)  45-degree hill, one row up per column
//   B  rise 64           steep hill, two rows up per column
//   C  rise 32           long hill, to show the surface stays smooth
//
// The body of every hill is packed with DECORATIVE dirt that carries no
// `solid`. That is the whole trick to making a ramp walkable: a solid cell
// under the ramp blocks the player's horizontal sweep (box_hits_solid sees the
// body), so the player gets stuck at the first column boundary instead of
// climbing. Support on a ramp comes from map_ground_y, not from solid tiles.
// ---------------------------------------------------------------------------

constexpr int kSlopeCols = 48;
constexpr int kSlopeRows = 15;
constexpr int kSlopeSheetCols = 8;
constexpr int kSlopeSheetRows = 1;
// tile ids (gid = id + 1)
constexpr std::uint32_t kSGrass = 0;
constexpr std::uint32_t kSDirt = 1;
constexpr std::uint32_t kSSlopeUp32 = 2;
constexpr std::uint32_t kSSlopeDown32 = 3;
constexpr std::uint32_t kSSlopeUp64 = 4;
constexpr std::uint32_t kSSlopeDown64 = 5;
constexpr std::uint32_t kSFill = 6;
constexpr std::uint32_t kSFillDeep = 7;

void PaintSlopeSheet(std::uint8_t *sheet) {
    const int sw = kSlopeSheetCols * kMTile;
    const auto put = [&](int id, int x, int y, std::uint8_t r, std::uint8_t g,
                         std::uint8_t b, std::uint8_t a) {
        const int ox = (id % kSlopeSheetCols) * kMTile;
        const int oy = (id / kSlopeSheetCols) * kMTile;
        std::uint8_t *px = sheet + (static_cast<std::size_t>(oy + y) * sw + ox + x) * 4;
        px[0] = r;
        px[1] = g;
        px[2] = b;
        px[3] = a;
    };

    for (int x = 0; x < kMTile; ++x) {
        for (int y = 0; y < kMTile; ++y) {
            // grass cap over dirt
            const bool cap = y < 6;
            const int d = (x + (cap ? 0 : 6)) % 16 < 2 ? 18 : 0;
            put(kSGrass, x, y, static_cast<std::uint8_t>(96 - d + (cap ? 70 : 0)),
                static_cast<std::uint8_t>(168 - d), static_cast<std::uint8_t>(72 - d), 255);
            put(kSDirt, x, y, static_cast<std::uint8_t>(150 - d),
                static_cast<std::uint8_t>(98 - d), static_cast<std::uint8_t>(52 - d), 255);
        }
    }

    // Ramp art. A 45-degree face from one corner to the other with a lit lip
    // on the hypotenuse, SOLID BELOW the line and open above it. `line`
    // matches GroundYAt's `anchor - t * rise`: an `up` ramp's surface leaves
    // the cell's bottom edge at t=0 and reaches its top edge at t=1. Tile-local
    // y grows downward like world y, so the material is at `y - line`; filling
    // the sky side instead leaves a notch under the walked surface and the
    // hill reads as floating steps. A `rise=64` tile reuses this same cell —
    // Tilemap::Draw stretches the sprite over the rise so art and query agree.
    const auto wedge = [&](int id, bool rising) {
        for (int x = 0; x < kMTile; ++x) {
            const int line = rising
                                 ? ((kMTile - 1) - (x * (kMTile - 1)) / (kMTile - 1))
                                 : ((x * (kMTile - 1)) / (kMTile - 1));
            for (int y = 0; y < kMTile; ++y) {
                const int below = y - line;
                if (below >= 0 && below <= 1) {
                    put(id, x, y, 255, 242, 192, 255);  // walkable lip
                } else if (below > 1) {
                    const int d = (below - 2) * 22 / kMTile;
                    put(id, x, y, static_cast<std::uint8_t>(150 - d),
                        static_cast<std::uint8_t>(104 - d),
                        static_cast<std::uint8_t>(58 - d), 255);
                } else {
                    put(id, x, y, 0, 0, 0, 0);  // open above the face
                }
            }
        }
    };
    wedge(kSSlopeUp32, true);
    wedge(kSSlopeDown32, false);
    wedge(kSSlopeUp64, true);
    wedge(kSSlopeDown64, false);

    // Decorative body dirt: no mortar lines. A brick course right under the lit
    // edge draws a square shoulder beside the diagonal and the hill reads as a
    // staircase again.
    for (int i = 0; i < 2; ++i) {
        const int id = kSFill + i;
        for (int x = 0; x < kMTile; ++x) {
            for (int y = 0; y < kMTile; ++y) {
                const int grit = ((x * 7 + y * 13 + i * 5) % 11) == 0 ? 14 : 0;
                const int base = i == 0 ? 150 : 132;
                put(id, x, y, static_cast<std::uint8_t>(base - grit),
                    static_cast<std::uint8_t>((base - grit) * 70 / 100),
                    static_cast<std::uint8_t>((base - grit) * 33 / 100), 255);
            }
        }
    }
}

void EnsureSlopeAssets(bool regen) {
    const std::filesystem::path dir = "assets";
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    const std::filesystem::path sheet_path = dir / "s_tiles.png";
    if (regen || !std::filesystem::exists(sheet_path)) {
        std::vector<std::uint8_t> sheet(
            static_cast<std::size_t>(kSlopeSheetCols * kSlopeSheetRows) * kMTile * kMTile *
                4,
            0);
        PaintSlopeSheet(sheet.data());
        stbi_write_png(sheet_path.string().c_str(), kSlopeSheetCols * kMTile,
                       kSlopeSheetRows * kMTile, 4, sheet.data(),
                       kSlopeSheetCols * kMTile * 4);
    }
    {
        const std::filesystem::path p = dir / "s_player.png";
        if (regen || !std::filesystem::exists(p)) {
            // Same 4-frame walk strip as the small Mario form.
            std::vector<std::uint8_t> buf(80 * 28 * 4, 0);
            PaintMarioPlayerSheet(buf.data(), 80, 28);
            stbi_write_png(p.string().c_str(), 80, 28, 4, buf.data(), 80 * 4);
        }
    }

    std::vector<std::uint32_t> gids(
        static_cast<std::size_t>(kSlopeCols) * kSlopeRows, 0);
    auto at = [&](int c, int r) -> std::uint32_t & {
        return gids[static_cast<std::size_t>(r) * kSlopeCols + c];
    };
    for (int c = 0; c < kSlopeCols; ++c) {
        at(c, 13) = kSGrass + 1;
        at(c, 14) = kSDirt + 1;
    }

    // A: 45 degrees. Chaining rule: each column starts where the previous one
    // ended, so with the default rise that means stepping up one row per column.
    // col3 (row 12) runs 416 -> 384, col4 (row 11) runs 384 -> 352, and so on.
    // No solid crest cell: an `up` ramp's upper end and the `down` ramp's lower
    // end meet exactly (col5 ends at 320, col6 starts at 320), so the surface is
    // continuous across the column boundary. Putting a solid grass cell there
    // instead is what stalls a walker — the box is 28px tall and the slope only
    // lifts it 1px per px, so the body still covers the crest's row when the
    // feet are at ramp height, and the horizontal sweep is blocked by the very
    // cell it is standing on.
    at(3, 12) = kSSlopeUp32 + 1;
    at(4, 11) = kSSlopeUp32 + 1;
    at(5, 10) = kSSlopeUp32 + 1;
    at(6, 10) = kSSlopeDown32 + 1;
    at(7, 11) = kSSlopeDown32 + 1;
    at(8, 12) = kSSlopeDown32 + 1;

    // B: rise 64. The chaining rule scales with the rise, so each column steps
    // up rise/tile_height = 2 rows. col14 (row 12) runs 448 -> 384 with its
    // low end buried in the ground, col15 (row 10) runs 384 -> 320.
    // col14 runs 448 -> 384, col15 runs 384 -> 320, col16 back down 320 -> 384.
    // A solid cap at col16 was tried here and removed: the step from the
    // steep ramp up to a 320 crest is 20px, more than the 16px step-up
    // assist, so the player stalled on the ramp. The ramps already meet
    // exactly at 320 across the boundary.
    at(14, 12) = kSSlopeUp64 + 1;
    at(15, 10) = kSSlopeUp64 + 1;
    at(16, 10) = kSSlopeDown64 + 1;
    at(17, 12) = kSSlopeDown64 + 1;

    // C: a long 45-degree hill, to show the interpolated surface stays smooth
    // over many cells instead of drifting.
    for (int i = 0; i < 5; ++i) {
        at(22 + i, 12 - i) = kSSlopeUp32 + 1;
        at(27 + i, 8 + i) = kSSlopeDown32 + 1;
    }


    // Pack the body of every hill with DECORATIVE dirt (no `solid` — see the
    // note above), starting below the whole span the ramp covers. A rise-64
    // ramp is drawn over two rows, so filling from `top + 1` would overdraw its
    // lower half and cut a horizontal seam across the hill — that is what made
    // the steep section look like a tower with a spike on it instead of a
    // clean 63-degree ramp.
    const auto ramp_span = [](std::uint32_t gid) -> int {
        if (gid == kSSlopeUp32 + 1 || gid == kSSlopeDown32 + 1) return 1;
        if (gid == kSSlopeUp64 + 1 || gid == kSSlopeDown64 + 1) return 2;
        return 1;
    };
    for (int c = 0; c < kSlopeCols; ++c) {
        int top = -1;
        for (int r = 0; r < 13; ++r) {
            if (at(c, r) != 0) {
                top = r;
                break;
            }
        }
        if (top < 0) continue;
        for (int r = top + ramp_span(at(c, top)); r <= 12; ++r) {
            if (at(c, r) == 0) {
                // darker with depth, so a tall fill still reads as a solid body
                at(c, r) = (r >= 11 ? kSFillDeep : kSFill) + 1;
            }
        }
    }

    std::ostringstream json;
    json << "{\n"
         << "  \"orientation\": \"orthogonal\",\n"
         << "  \"width\": " << kSlopeCols << ", \"height\": " << kSlopeRows << ",\n"
         << "  \"tilewidth\": " << kMTile << ", \"tileheight\": " << kMTile << ",\n"
         << "  \"tilesets\": [{\n"
         << "    \"firstgid\": 1, \"name\": \"s_tiles\",\n"
         << "    \"tilewidth\": " << kMTile << ", \"tileheight\": " << kMTile << ",\n"
         << "    \"columns\": " << kSlopeSheetCols
         << ", \"tilecount\": " << kSlopeSheetCols * kSlopeSheetRows << ",\n"
         << "    \"image\": \"s_tiles.png\",\n"
         << "    \"imagewidth\": " << kSlopeSheetCols * kMTile
         << ", \"imageheight\": " << kSlopeSheetRows * kMTile << ",\n"
         << "    \"tiles\": [\n"
         << "      {\"id\": 0, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 1, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\", \"value\": true}]},\n"
         << "      {\"id\": 2, \"properties\": [{\"name\": \"slope\", \"type\": \"string\", \"value\": \"up\"}]},\n"
         << "      {\"id\": 3, \"properties\": [{\"name\": \"slope\", \"type\": \"string\", \"value\": \"down\"}]},\n"
         << "      {\"id\": 4, \"properties\": [{\"name\": \"slope\", \"type\": \"string\", \"value\": \"up\"},"
            "{\"name\": \"slope_rise\", \"type\": \"int\", \"value\": 64}]},\n"
         << "      {\"id\": 5, \"properties\": [{\"name\": \"slope\", \"type\": \"string\", \"value\": \"down\"},"
            "{\"name\": \"slope_rise\", \"type\": \"int\", \"value\": 64}]}\n"
         << "    ]\n"
         << "  }],\n"
         << "  \"layers\": [{\n"
         << "    \"name\": \"world\", \"type\": \"tilelayer\", \"visible\": true,\n"
         << "    \"opacity\": 1, \"data\": [";
    for (std::size_t i = 0; i < gids.size(); ++i) {
        if (i % kSlopeCols == 0) json << "\n      ";
        json << gids[i] << (i + 1 == gids.size() ? "\n" : ", ");
    }
    json << "  ]}]\n}\n";

    const std::filesystem::path level_path = dir / "slope.json";
    if (regen || !std::filesystem::exists(level_path)) {
        std::ofstream out(level_path, std::ios::binary);
        out << json.str();
    }
}

// ---------------------------------------------------------------------------
// Asset scaffolding for examples/hello/scripts/tile_demo.lua: an 8-tile sheet where two of
// the tiles carry Tiled animations (a 3-frame water cycle, a 2-frame torch),
// plus the map that uses them. Generated on demand.
// ---------------------------------------------------------------------------

constexpr int kTaTile = 32;
constexpr int kTaCols = 30;
constexpr int kTaRows = 12;
constexpr int kTaSheetTiles = 8;

/// 256x32 sheet: water a/b/c, torch a/b, sand, grass, stone (in that order).
void PaintTileAnimSheet(std::uint8_t *p, int w, int h) {
    auto put = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        if (x < 0 || y < 0 || x >= w || y >= h) {
            return;
        }
        const std::size_t o = (static_cast<std::size_t>(y) * w + x) * 4;
        p[o + 0] = r;
        p[o + 1] = g;
        p[o + 2] = b;
        p[o + 3] = 255;
    };
    for (int t = 0; t < kTaSheetTiles; ++t) {
        const int ox = t * kTaTile;
        if (t < 3) {
            // Water: the crest slides down the tile, so the three frames read
            // as one travelling wave instead of three unrelated blues.
            const int crest = 6 + t * 7;
            for (int y = 0; y < kTaTile; ++y) {
                for (int x = 0; x < kTaTile; ++x) {
                    const int wave = static_cast<int>(3.0f * std::sin((x + t * 6) * 0.35f));
                    const int d = std::abs(y - (crest + wave));
                    std::uint8_t r = 26, g = 96, b = 168;
                    if (d < 3) {
                        r = 120; g = 200; b = 244;
                    } else if (d < 6) {
                        r = 58; g = 142; b = 206;
                    }
                    put(ox + x, y, r, g, b);
                }
            }
        } else if (t < 5) {
            // Torch: a fixed post, a flame that leans and swells.
            const bool big = (t == 3);
            for (int y = 0; y < kTaTile; ++y) {
                for (int x = 0; x < kTaTile; ++x) {
                    put(ox + x, y, 0, 0, 0);
                }
            }
            for (int y = 16; y < kTaTile; ++y) {
                for (int x = 13; x < 19; ++x) {
                    put(ox + x, y, 96, 68, 44);
                }
            }
            const int cy = big ? 11 : 13;
            const int rx = big ? 8 : 6;
            const int lean = big ? 0 : 2;
            for (int y = 0; y < 20; ++y) {
                for (int x = 0; x < kTaTile; ++x) {
                    const float dx = (x - (16 + lean)) / static_cast<float>(rx);
                    const float dy = (y - cy) / (big ? 9.0f : 7.0f);
                    const float d = dx * dx + dy * dy;
                    if (d <= 1.0f) {
                        const bool core = d < 0.35f;
                        put(ox + x, y, core ? 252 : 244, core ? 240 : 148, core ? 140 : 44);
                    }
                }
            }
        } else if (t == 5) {
            for (int y = 0; y < kTaTile; ++y) {
                for (int x = 0; x < kTaTile; ++x) {
                    const bool speck = ((x * 7 + y * 13) % 29) == 0;
                    put(ox + x, y, speck ? 196 : 222, speck ? 172 : 198, speck ? 118 : 140);
                }
            }
        } else if (t == 6) {
            for (int y = 0; y < kTaTile; ++y) {
                for (int x = 0; x < kTaTile; ++x) {
                    const bool top = y < 7;
                    put(ox + x, y, top ? 92 : 64, top ? 168 : 132, top ? 76 : 58);
                }
            }
        } else {
            for (int y = 0; y < kTaTile; ++y) {
                for (int x = 0; x < kTaTile; ++x) {
                    const bool edge = x < 2 || y < 2 || x >= kTaTile - 2 || y >= kTaTile - 2;
                    put(ox + x, y, edge ? 96 : 126, edge ? 96 : 126, edge ? 104 : 134);
                }
            }
        }
    }
}

void EnsureTileAnimAssets() {
    const std::filesystem::path dir = "assets";
    std::error_code ec;
    std::filesystem::create_directory(dir, ec);

    const std::filesystem::path sheet_path = dir / "ta_sheet.png";
    {
        std::vector<std::uint8_t> sheet(
            static_cast<std::size_t>(kTaSheetTiles * kTaTile) * kTaTile * 4, 0);
        PaintTileAnimSheet(sheet.data(), kTaSheetTiles * kTaTile, kTaTile);
        stbi_write_png(sheet_path.string().c_str(), kTaSheetTiles * kTaTile, kTaTile, 4,
                       sheet.data(), kTaSheetTiles * kTaTile * 4);
    }

    // gids: 1 water (animated), 4 torch (animated), 6 sand, 7 grass, 8 stone.
    std::vector<int> gids(static_cast<std::size_t>(kTaCols) * kTaRows, 0);
    auto at = [&](int col, int row) -> int & {
        return gids[static_cast<std::size_t>(row) * kTaCols + col];
    };
    for (int c = 0; c < kTaCols; ++c) {
        at(c, 7) = 7; // grass line
        for (int r = 8; r < kTaRows; ++r) {
            at(c, r) = 8; // stone body
        }
    }
    for (int c = 4; c <= 9; ++c) {
        at(c, 8) = 1; // pond
        at(c, 9) = 1;
    }
    for (int c = 20; c <= 25; ++c) {
        at(c, 8) = 6; // sandbar
    }
    at(5, 6) = 4;
    at(12, 6) = 4;
    at(19, 6) = 4;

    std::ostringstream json;
    json << "{\n  \"orientation\": \"orthogonal\", \"width\": " << kTaCols
         << ", \"height\": " << kTaRows << ",\n  \"tilewidth\": " << kTaTile
         << ", \"tileheight\": " << kTaTile << ",\n"
         << "  \"tilesets\": [{\n    \"firstgid\": 1, \"name\": \"ta\",\n"
         << "    \"tilewidth\": " << kTaTile << ", \"tileheight\": " << kTaTile << ",\n"
         << "    \"columns\": " << kTaSheetTiles << ", \"tilecount\": " << kTaSheetTiles
         << ",\n    \"image\": \"ta_sheet.png\",\n    \"imagewidth\": "
         << kTaSheetTiles * kTaTile << ", \"imageheight\": " << kTaTile << ",\n"
         << "    \"tiles\": [\n"
         << "      {\"id\": 0, \"animation\": [{\"duration\": 180, \"tileid\": 0},\n"
         << "                                {\"duration\": 180, \"tileid\": 1},\n"
         << "                                {\"duration\": 180, \"tileid\": 2}]},\n"
         << "      {\"id\": 3, \"animation\": [{\"duration\": 250, \"tileid\": 3},\n"
         << "                                {\"duration\": 250, \"tileid\": 4}]},\n"
         << "      {\"id\": 6, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\","
            " \"value\": true}]},\n"
         << "      {\"id\": 7, \"properties\": [{\"name\": \"solid\", \"type\": \"bool\","
            " \"value\": true}]}\n"
         << "    ]\n  }],\n"
         << "  \"layers\": [{\n    \"name\": \"world\", \"type\": \"tilelayer\",\n"
         << "    \"visible\": true, \"opacity\": 1, \"data\": [";
    for (std::size_t i = 0; i < gids.size(); ++i) {
        if (i % kTaCols == 0) {
            json << "\n      ";
        }
        json << gids[i] << (i + 1 == gids.size() ? "\n" : ", ");
    }
    json << "  ]}]\n}\n";

    const std::filesystem::path level_path = dir / "ta_level.json";
    std::ofstream out(level_path, std::ios::binary);
    out << json.str();
}

} // namespace

// ---------------------------------------------------------------------------
// Demo launcher
// ---------------------------------------------------------------------------

/// Run `self` with these arguments and block until it exits. Returns false only
/// when the child could not be started at all.
bool SpawnWaiting(const char *self, const std::vector<std::string> &args) {
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(self));
    for (const auto &arg : args) {
        argv.push_back(const_cast<char *>(arg.data()));
    }
    argv.push_back(nullptr);
#if defined(_WIN32)
    return _spawnv(_P_WAIT, self, argv.data()) >= 0;
#else
    // 127 is the shell's own "could not execute" status, reused here so the
    // parent can tell a demo that never started from one that ran and closed.
    constexpr int kExecFailed = 127;
    const pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        execvp(self, argv.data());
        _exit(kExecFailed);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return !(WIFEXITED(status) && WEXITSTATUS(status) == kExecFailed);
#endif
}

/// The arguments a demo child needs: its own flag, the frame budget so a
/// scripted run cannot spawn a session that never ends, the backend choice
/// when the caller pinned one, and `--ai` so a picker opened under the
/// attract driver hands the same controller to the demo it starts.
std::vector<std::string> DemoArgs(const std::string &flag, int max_frames,
                                  int jit_choice, bool ai = false) {
    std::vector<std::string> args;
    args.push_back("--" + flag + "-demo");
    if (ai) {
        args.push_back("--ai");
    }
    if (max_frames > 0) {
        args.push_back("--frames");
        args.push_back(std::to_string(max_frames));
    }
    if (jit_choice > 0) {
        args.push_back("--jit");
    } else if (jit_choice == 0) {
        args.push_back("--interp");
    }
    return args;
}

/// Start the demo the launcher picked in a child process and wait for it to
/// close. Handing the demo a fresh process beats unwinding a live engine — a
/// different entry script means different host-side asset and audio setup, and
/// Run() already tears the window down on the way out — but the *launcher* has
/// to survive it, because a picker that can only pick once is a boot screen.
bool RunDemoAsChild(const char *self, const std::string &flag, int max_frames,
                    int jit_choice, bool ai) {
    return SpawnWaiting(self, DemoArgs(flag, max_frames, jit_choice, ai));
}

/// Replace this process with a fresh picker once the demo closes.
bool RelaunchAsLauncher(const char *self, int max_frames, int jit_choice,
                        bool ai) {
    std::vector<std::string> args;
    args.push_back("--launcher");
    if (ai) {
        args.push_back("--ai");
    }
    if (max_frames > 0) {
        args.push_back("--frames");
        args.push_back(std::to_string(max_frames));
    }
    if (jit_choice > 0) {
        args.push_back("--jit");
    } else if (jit_choice == 0) {
        args.push_back("--interp");
    }
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(self));
    for (const auto &arg : args) {
        argv.push_back(const_cast<char *>(arg.data()));
    }
    argv.push_back(nullptr);
#if defined(_WIN32)
    return _execv(self, argv.data()) == 0;
#else
    return execvp(self, argv.data()) == 0;
#endif
}

/// Recognize the `--<flag>-demo` form RelaunchWithDemo emits and resolve it
/// through the catalogue. The argument parser and the launcher test both go
/// through here, so the string the menu re-execs with and the grammar the host
/// accepts cannot drift apart.
enum class DemoArg { kNotADemoFlag, kUnknownDemo, kResolved };
DemoArg ParseDemoArg(std::string_view arg, const fake2d::DemoInfo **out) {
    if (!arg.starts_with("--") || arg.size() <= 7 || !arg.ends_with("-demo")) {
        return DemoArg::kNotADemoFlag;
    }
    const fake2d::DemoInfo *info =
        fake2d::FindDemo(arg.substr(2, arg.size() - 7));
    if (info == nullptr) {
        return DemoArg::kUnknownDemo;
    }
    *out = info;
    return DemoArg::kResolved;
}

/// Self-test of the catalogue the launcher menu and the command line share: a
/// demo can only be offered if its flag is unique, its category is one the menu
/// can group by, and its script is really in the checkout.


/// The generic `--ai` rhythm, for the demos that have no attract policy of
/// their own. Directions are held a beat at a time (a tapped arrow is a
/// twitch, a held one is a run) and the action keys tap on a faster clock.
/// The pointer walks a coarse raster across the window, clicking as it goes:
/// UIs are laid out in rows and columns, so a sweep that visits every cell
/// lands on a widget somewhere, while an arc or a Lissajous curve spends its
/// life in the margins. It is not trying to play well -- it is trying to look
/// like somebody is home.
void AiRhythm(fake2d::Input &input, std::uint64_t frame, float w, float h) {
    constexpr std::string_view kDirs[] = {"right", "left", "up", "down"};
    constexpr std::string_view kActions[] = {"space", "j", "space", "x"};
    constexpr std::uint64_t kBeat = 45;
    const int slot = static_cast<int>((frame / kBeat) % 4);
    for (int i = 0; i < 4; ++i) {
        input.AiHoldKey(kDirs[i], i == slot);
    }
    for (std::string_view key : kActions) {
        input.AiHoldKey(key, false);
    }
    if (frame % 18 < 2) {
        input.AiHoldKey(kActions[slot], true);
    }

    constexpr int kCols = 6;
    constexpr int kRows = 5;
    constexpr std::uint64_t kDwell = 26;
    const int cell = static_cast<int>((frame / kDwell) % (kCols * kRows));
    const float fx = (static_cast<float>(cell % kCols) + 0.5f) / kCols;
    const float fy = (static_cast<float>(cell / kCols) + 0.5f) / kRows;
    input.AiHoldMouse({w * (0.10f + 0.80f * fx), h * (0.15f + 0.70f * fy)}, true);
    input.AiHoldButton(0, frame % kDwell < 3);
}

/// Demos whose script already plays itself. Their in-engine AI reads the
/// level and the physics to decide what to do, which beats any rhythm the
/// host could invent, so `--ai` hands them the controller through ai_mode()
/// and injects nothing.
bool AiScriptDriven(std::string_view flag) {
    constexpr std::string_view kScriptDriven[] = {
        "mario", "platformer", "contra", "beat-em-up", "shmup", "snooker",
        "city", "rts", "vn",
    };
    for (std::string_view name : kScriptDriven) {
        if (name == flag) {
            return true;
        }
    }
    return false;
}

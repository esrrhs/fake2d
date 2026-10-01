#include "fake2d/atlas.h"
#include "fake2d/character.h"
#include "fake2d/engine.h"
#include "fake2d/node.h"
#include "fake2d/particle.h"
#include "fake2d/scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// stb_image_write implementation lives in the fake2d library
// (src/render/image_write.cpp); here we only need the declarations.
#include <stb_image_write.h>

namespace {

// ---------------------------------------------------------------------------
// Procedural asset generation for the breakout sample: three small white
// textures tinted from Lua. Generated on demand so the repo stays asset-free.
// ---------------------------------------------------------------------------

constexpr int kSheetSize = 128;
constexpr int kTile = 64;

/// Paint a 128x128 RGBA sheet: four 64x64 shape tiles on transparent background.
void PaintDemoSheet(std::uint8_t *pixels) {
    auto coverage = [](int tile, float dx, float dy) -> std::uint8_t {
        switch (tile) {
            case 0:  // red circle
                return (dx * dx + dy * dy) <= 26.0f * 26.0f ? 255 : 0;
            case 1:  // green box
                return (std::fabs(dx) <= 22.0f && std::fabs(dy) <= 22.0f) ? 255 : 0;
            case 2: {  // blue triangle, wide bottom, apex top
                const float t = (dy + 26.0f) / 52.0f;
                if (t < 0.0f || t > 1.0f) return 0;
                return std::fabs(dx) <= 28.0f * (1.0f - t) ? 255 : 0;
            }
            default:  // yellow diamond
                return (std::fabs(dx) + std::fabs(dy) <= 28.0f) ? 255 : 0;
        }
    };

    const fake2d::Color tile_colors[4] = {
        fake2d::Color::FromRGBA8(225, 65, 65),
        fake2d::Color::FromRGBA8(75, 200, 95),
        fake2d::Color::FromRGBA8(75, 125, 230),
        fake2d::Color::FromRGBA8(240, 208, 66),
    };

    for (int y = 0; y < kSheetSize; ++y) {
        for (int x = 0; x < kSheetSize; ++x) {
            std::uint8_t *px = pixels + (static_cast<size_t>(y) * kSheetSize + x) * 4;
            const int tile = (x >= kTile ? 1 : 0) + (y >= kTile ? 2 : 0);
            const float dx = static_cast<float>(x % kTile) - (kTile * 0.5f - 0.5f);
            const float dy = static_cast<float>(y % kTile) - (kTile * 0.5f - 0.5f);
            const std::uint8_t a = coverage(tile, dx, dy);
            px[0] = static_cast<std::uint8_t>(tile_colors[tile].r * 255.0f);
            px[1] = static_cast<std::uint8_t>(tile_colors[tile].g * 255.0f);
            px[2] = static_cast<std::uint8_t>(tile_colors[tile].b * 255.0f);
            px[3] = a;
        }
    }
}

/// TexturePacker hash-format atlas describing PaintDemoSheet's 2x2 tiles.
constexpr const char *kAtlasJson = R"({
  "frames": {
    "red_circle": {
      "frame": {"x": 0, "y": 0, "w": 64, "h": 64},
      "rotated": false, "trimmed": false,
      "spriteSourceSize": {"x": 0, "y": 0, "w": 64, "h": 64},
      "sourceSize": {"w": 64, "h": 64}, "pivot": {"x": 0.5, "y": 0.5}
    },
    "green_box": {
      "frame": {"x": 64, "y": 0, "w": 64, "h": 64},
      "rotated": false, "trimmed": false,
      "spriteSourceSize": {"x": 0, "y": 0, "w": 64, "h": 64},
      "sourceSize": {"w": 64, "h": 64}, "pivot": {"x": 0.5, "y": 0.5}
    },
    "blue_triangle": {
      "frame": {"x": 0, "y": 64, "w": 64, "h": 64},
      "rotated": false, "trimmed": false,
      "spriteSourceSize": {"x": 0, "y": 0, "w": 64, "h": 64},
      "sourceSize": {"w": 64, "h": 64}, "pivot": {"x": 0.5, "y": 0.5}
    },
    "yellow_diamond": {
      "frame": {"x": 64, "y": 64, "w": 64, "h": 64},
      "rotated": false, "trimmed": false,
      "spriteSourceSize": {"x": 0, "y": 0, "w": 64, "h": 64},
      "sourceSize": {"w": 64, "h": 64}, "pivot": {"x": 0.5, "y": 0.5}
    }
  },
  "meta": { "image": "demo_sheet.png", "size": {"w": 128, "h": 128}, "scale": "1" }
})";

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

/// Register the samples' named clips (no-ops when audio is disabled).
void EnsureGameAudio(fake2d::AudioEngine &audio) {
    if (!audio.IsEnabled() || audio.ClipCount() > 0) {
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

int RunBenchmark(fake2d::Engine &engine, int max_frames) {
    constexpr int kTextureCount = 8;
    constexpr int kTextureSize = 48;
    constexpr int kSprites = 768;
    constexpr int kPhaseFrames = 120;
    constexpr int kWarmup = 30;

    struct Stats {
        double flush_ms_sum = 0.0;
        std::uint64_t draw_calls_sum = 0;
        std::uint64_t draw_calls_min = 0;
        std::uint64_t draw_calls_max = 0;
        int samples = 0;
    };

    std::vector<fake2d::TextureHandle> textures;
    textures.reserve(kTextureCount);
    const fake2d::Color palette[kTextureCount] = {
        fake2d::Color::FromRGBA8(230, 80, 80),
        fake2d::Color::FromRGBA8(235, 140, 60),
        fake2d::Color::FromRGBA8(235, 210, 70),
        fake2d::Color::FromRGBA8(90, 200, 110),
        fake2d::Color::FromRGBA8(80, 180, 220),
        fake2d::Color::FromRGBA8(90, 120, 230),
        fake2d::Color::FromRGBA8(180, 110, 225),
        fake2d::Color::FromRGBA8(235, 235, 235),
    };
    for (int t = 0; t < kTextureCount; ++t) {
        std::vector<std::uint8_t> px(static_cast<size_t>(kTextureSize) * kTextureSize * 4, 0);
        const float c = kTextureSize * 0.5f;
        for (int y = 0; y < kTextureSize; ++y) {
            for (int x = 0; x < kTextureSize; ++x) {
                const float dx = static_cast<float>(x) - c;
                const float dy = static_cast<float>(y) - c;
                const float d = std::sqrt(dx * dx + dy * dy);
                const float a = std::clamp(c - d + 0.5f, 0.0f, 1.0f);
                std::uint8_t *p = px.data() + (static_cast<size_t>(y) * kTextureSize + x) * 4;
                p[0] = static_cast<std::uint8_t>(palette[t].r * 255.0f);
                p[1] = static_cast<std::uint8_t>(palette[t].g * 255.0f);
                p[2] = static_cast<std::uint8_t>(palette[t].b * 255.0f);
                p[3] = static_cast<std::uint8_t>(a * 255.0f);
            }
        }
        const fake2d::TextureHandle h = engine.GetResources().CreateTexture(
            "bench_" + std::to_string(t), kTextureSize, kTextureSize, px.data());
        if (h != fake2d::kInvalidTextureHandle) {
            textures.push_back(h);
        }
    }
    if (textures.empty()) {
        std::fprintf(stderr, "fake2d_hello: benchmark texture creation failed\n");
        return 1;
    }

    Stats immediate;
    Stats sorted;
    std::uint64_t rng = 0x123456789abcdef0ULL;

    {
        // Sorted first: it is the cheap path, so short CI timeouts still
        // capture the headline draw-call reduction.
        engine.GetRenderer().GetSpriteBatch().SetSorted(true);
    }

    engine.SetFrameCallback([&](fake2d::Engine &e) {
        const std::uint64_t frame = e.FrameIndex();
        const bool sorted_phase = frame < static_cast<std::uint64_t>(kPhaseFrames);
        const int phase_frame = static_cast<int>(frame % kPhaseFrames);

        auto &batch = e.GetRenderer().GetSpriteBatch();
        if (frame == static_cast<std::uint64_t>(kPhaseFrames)) {
            batch.SetSorted(false);
        }
        Stats &stats = sorted_phase ? sorted : immediate;

        // Deterministic per-frame layout for both phases.
        rng = 0x123456789abcdef0ULL ^ (frame * 0x9e3779b97f4a7c15ULL);
        for (int i = 0; i < kSprites; ++i) {
            rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
            const float x = static_cast<float>((rng >> 33) % 900);
            rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
            const float y = static_cast<float>((rng >> 33) % 480);
            rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
            const std::size_t ti = static_cast<std::size_t>((rng >> 33) % textures.size());
            const fake2d::Texture2D *tex = e.GetResources().GetTexture(textures[ti]);
            if (tex) {
                e.GetRenderer().DrawSprite(*tex, x, y,
                                           static_cast<float>(kTextureSize),
                                           static_cast<float>(kTextureSize));
            }
        }

        if (phase_frame >= kWarmup) {
            const auto t0 = std::chrono::steady_clock::now();
            batch.End();
            const auto t1 = std::chrono::steady_clock::now();
            stats.flush_ms_sum += std::chrono::duration<double, std::milli>(t1 - t0).count();
            const std::uint64_t calls = e.GetRenderer().DrawCallCount();
            stats.draw_calls_sum += calls;
            stats.draw_calls_min = stats.samples == 0 ? calls : std::min(stats.draw_calls_min, calls);
            stats.draw_calls_max = std::max(stats.draw_calls_max, calls);
            ++stats.samples;
            batch.Begin(e.GetRenderer().GetCamera().ViewProjectionMatrix());
        } else {
            batch.End();
            batch.Begin(e.GetRenderer().GetCamera().ViewProjectionMatrix());
        }
    });

    const int rc = engine.Run(max_frames > 0 ? max_frames : kPhaseFrames * 2);

    const auto report = [](const char *name, const Stats &s) {
        if (s.samples == 0) {
            std::printf("%-10s no samples\n", name);
            return;
        }
        std::printf("%-10s avg flush %7.3f ms | draw calls/frame %8.1f (min %llu, max %llu)\n",
                    name, s.flush_ms_sum / s.samples,
                    static_cast<double>(s.draw_calls_sum) / s.samples,
                    static_cast<unsigned long long>(s.draw_calls_min),
                    static_cast<unsigned long long>(s.draw_calls_max));
    };

    std::printf("== fake2d sprite batch benchmark ==\n");
    std::printf("sprites/frame: %d, textures: %zu, samples/phase: %d (%d warmup)\n",
                kSprites, textures.size(), kPhaseFrames - kWarmup, kWarmup);
    report("immediate", immediate);
    report("sorted", sorted);
    std::printf("batch storage: %zu KiB vertex streams (2 x %zu quads)\n",
                (4096 * 4 * sizeof(fake2d::Vertex2D)) * 2 / 1024, static_cast<size_t>(4096));
    return rc;
}

// Physics broadphase benchmark: a tiled static floor plus many dynamic
// circles, compared brute force vs spatial hash grid.
int RunPhysicsBenchmark(fake2d::Engine &engine) {
    constexpr int kFrames = 300;
    constexpr int kWarmup = 60;
    constexpr int kDynamic = 600;

    auto run_phase = [&](bool grid) {
        fake2d::PhysicsWorld world;
        world.SetGravity({0.0f, 900.0f});
        world.SetUseSpatialGrid(grid);

        fake2d::BodyConfig floor_cfg;
        floor_cfg.type = fake2d::BodyType::Static;
        floor_cfg.half_extents = {480.0f, 20.0f};
        floor_cfg.position = {480.0f, 520.0f};
        world.CreateBody(floor_cfg);
        for (int i = 0; i < 8; ++i) {
            fake2d::BodyConfig pillar = floor_cfg;
            pillar.half_extents = {20.0f, 220.0f};
            pillar.position = {60.0f + i * 120.0f, 300.0f};
            world.CreateBody(pillar);
        }

        std::uint32_t rng = 0xabcdef01u;
        auto randf = [&]() {
            rng = rng * 1664525u + 1013904223u;
            return static_cast<float>(rng & 0xFFFFu) / 65535.0f;
        };
        for (int i = 0; i < kDynamic; ++i) {
            fake2d::BodyConfig cfg;
            cfg.radius = 7.0f;
            cfg.half_extents = {7.0f, 7.0f};
            cfg.position = {40.0f + randf() * 880.0f, 20.0f + randf() * 380.0f};
            cfg.restitution = 0.55f;
            world.CreateBody(cfg);
        }

        double step_ms = 0.0;
        int samples = 0;
        for (int frame = 0; frame < kFrames; ++frame) {
            const auto t0 = std::chrono::steady_clock::now();
            world.Step(1.0f / 60.0f);
            const auto t1 = std::chrono::steady_clock::now();
            if (frame >= kWarmup) {
                step_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
                ++samples;
            }
        }
        return step_ms / samples;
    };

    const double brute_ms = run_phase(false);
    const double grid_ms = run_phase(true);
    std::printf("== fake2d physics broadphase benchmark ==\n");
    std::printf("bodies: %d (dynamics) + 9 statics, samples: %d\n",
                kDynamic, kFrames - kWarmup);
    std::printf("brute-force O(n^2)  avg step %7.3f ms\n", brute_ms);
    std::printf("spatial hash grid   avg step %7.3f ms  (%.1fx)\n",
                grid_ms, brute_ms / grid_ms);
    return 0;
}

// ---------------------------------------------------------------------------
// Phase 6 demo: Tiled tilemap + built-in physics + UI button. All assets
// (tilesheet and Tiled JSON) are generated on demand so the repo stays
// binary-asset-free.
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

int RunMapDemo(fake2d::Engine &engine, int max_frames, const std::string &screenshot) {
    EnsureMapAssets();

    const int map_id = engine.GetTilemaps().Load("assets/level.json", engine.GetResources());
    fake2d::Tilemap *map = engine.GetTilemaps().Get(map_id);
    if (map == nullptr) {
        std::fprintf(stderr, "fake2d_hello: failed to load tilemap\n");
        return 1;
    }
    const auto colliders = map->CreateStaticColliders(engine.GetPhysics());
    (void)colliders; // pinned for the engine lifetime; static world geometry

    const fake2d::TextureHandle ball_tex = engine.GetResources().LoadTexture("assets/ball.png");
    const fake2d::Texture2D *ball_texture = engine.GetResources().GetTexture(ball_tex);

    struct Ball {
        fake2d::BodyId id = fake2d::kInvalidBody;
        fake2d::Color tint;
    };
    constexpr int kBallCount = 24;
    std::vector<Ball> balls(kBallCount);

    const fake2d::Color palette[] = {
        fake2d::Color::FromRGBA8(232, 86, 86),
        fake2d::Color::FromRGBA8(240, 170, 70),
        fake2d::Color::FromRGBA8(238, 220, 90),
        fake2d::Color::FromRGBA8(110, 210, 120),
        fake2d::Color::FromRGBA8(96, 190, 226),
        fake2d::Color::FromRGBA8(120, 140, 236),
        fake2d::Color::FromRGBA8(190, 120, 228),
    };

    std::uint32_t rng = 0x7a17c0deu;
    auto next_rand = [&rng]() {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<float>(rng & 0xFFFFu) / 65535.0f;
    };

    const auto spawn = [&]() {
        for (int i = 0; i < kBallCount; ++i) {
            fake2d::BodyConfig cfg;
            cfg.position = {96.0f + next_rand() * 768.0f, 32.0f + next_rand() * 120.0f};
            cfg.radius = 9.0f;
            cfg.half_extents = {9.0f, 9.0f};
            cfg.velocity = {(next_rand() - 0.5f) * 260.0f, (next_rand() - 0.5f) * 120.0f};
            cfg.restitution = 0.62f;
            balls[i].id = engine.GetPhysics().CreateBody(cfg);
            balls[i].tint = palette[i % 7];
        }
    };
    const auto reset = [&]() {
        for (Ball &ball : balls) {
            engine.GetPhysics().DestroyBody(ball.id);
        }
        spawn();
    };
    spawn();

    engine.SetFrameCallback([&](fake2d::Engine &e) {
        if (e.GetInput().KeyPressed("r")) {
            reset();
        }

        map->Draw(e.GetRenderer());

        for (const Ball &ball : balls) {
            const fake2d::BodyConfig *cfg = e.GetPhysics().Get(ball.id);
            if (cfg != nullptr && ball_texture != nullptr) {
                e.GetRenderer().DrawSprite(
                    *ball_texture,
                    cfg->position.x - cfg->radius, cfg->position.y - cfg->radius,
                    cfg->radius * 2.0f, cfg->radius * 2.0f, ball.tint);
            }
        }

        e.GetUI().Label(fake2d::UIAnchor::TopLeft, 16.0f, 12.0f,
                        "TILEMAP + PHYSICS DEMO", 0.55f,
                        fake2d::Color::FromRGBA8(235, 240, 255));
        if (e.GetUI().Button("reset", fake2d::UIAnchor::TopRight,
                             16.0f, 10.0f, 104.0f, 32.0f, "RESET (R)", 0.5f)) {
            reset();
        }
        e.GetUI().Label(fake2d::UIAnchor::BottomLeft, 16.0f, 12.0f,
                        "built-in AABB/circle physics, Tiled JSON, anchored UI",
                        0.45f, fake2d::Color::FromRGBA8(170, 180, 205));

        if (!screenshot.empty() && e.FrameIndex() == 90) {
            e.GetRenderer().GetSpriteBatch().Flush();
            e.GetRenderer().SaveScreenshot(screenshot);
        }
    });

    return engine.Run(max_frames);
}

// ---------------------------------------------------------------------------
// Phase 7 demo: complete platformer level. Kinematic character controller
// with coyote time / jump buffering / one-way platforms, coin pickups that
// rewrite the tilemap, a goal flag, a trauma-shake follow camera, looping
// WAV background music and debug line overlays.
// ---------------------------------------------------------------------------

namespace {

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

void PaintPlayer(std::uint8_t *px, int w, int h) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float a = std::clamp(9.0f - std::sqrt(
                (x - w * 0.5f) * (x - w * 0.5f) +
                (y - h * 0.5f) * (y - h * 0.5f)) + 0.5f, 0.0f, 1.0f);
            std::uint8_t *p = px + (static_cast<size_t>(y) * w + x) * 4;
            p[0] = 90; p[1] = 200; p[2] = 235; p[3] = static_cast<std::uint8_t>(a * 255);
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
        std::vector<std::uint8_t> player(20 * 30 * 4, 0);
        PaintPlayer(player.data(), 20, 30);
        stbi_write_png(player_path.string().c_str(), 20, 30, 4,
                       player.data(), 20 * 4);
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
         << "      {\"id\": 2, \"properties\": [{\"name\": \"oneway\", \"type\": \"bool\", \"value\": true}]}\n"
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

} // namespace

int RunPlatformerDemo(fake2d::Engine &engine, int max_frames,
                      const std::string &screenshot) {
    EnsurePlatformerAssets();

    const int map_id = engine.GetTilemaps().Load("assets/platformer.json",
                                                 engine.GetResources());
    fake2d::Tilemap *map = engine.GetTilemaps().Get(map_id);
    if (map == nullptr) {
        std::fprintf(stderr, "fake2d_hello: failed to load platformer level\n");
        return 1;
    }
    const std::vector<std::uint32_t> initial_tiles = map->Layers()[0].gids;
    const int total_coins = static_cast<int>(
        std::count(initial_tiles.begin(), initial_tiles.end(), kGCoin));

    const fake2d::TextureHandle player_tex =
        engine.GetResources().LoadTexture("assets/player.png");
    const fake2d::Texture2D *player_texture =
        engine.GetResources().GetTexture(player_tex);

    // Looping background music decoded from the generated 22050 Hz WAV.
    engine.GetAudio().AddClipWav("music", "assets/music.wav");
    engine.GetAudio().PlayMusic("music", 0.22f);

    fake2d::CharacterController player;
    player.SetMap(map);
    fake2d::CharacterTuning tuning;
    player.SetTuning(tuning);

    const fake2d::Vec2 kSpawn{2.0f * kPTile, 13.0f * kPTile};
    int coins = 0;
    int win_timer = -1;
    int jump_hold = 0;
    bool prev_on_ground = false;

    auto reset = [&]() {
        player.Spawn(kSpawn);
        coins = 0;
        win_timer = -1;
        for (std::size_t i = 0; i < initial_tiles.size(); ++i) {
            map->SetTile(static_cast<int>(i % kPCols),
                         static_cast<int>(i / kPCols), initial_tiles[i]);
        }
    };
    reset();

    engine.SetFrameCallback([&](fake2d::Engine &e) {
        const std::uint64_t frame = e.FrameIndex();

        // --- intent: keyboard in interactive mode, deterministic AI headless ---
        const auto &input = e.GetInput();
        fake2d::CharacterIntent intent;
        intent.move_x = 1.0f; // attract mode runs right; keyboard overrides:
        if (input.KeyDown("left") || input.KeyDown("a")) intent.move_x = -1.0f;
        if (input.KeyDown("right") || input.KeyDown("d")) intent.move_x = 1.0f;

        const fake2d::Vec2 center = player.Center();
        const int feet_row = static_cast<int>(
            (center.y + tuning.half_extents.y + 4.0f) / kPTile);
        const int ahead_col = static_cast<int>(
            (center.x + tuning.half_extents.x + 20.0f) / kPTile);
        const bool wall_ahead = map->SolidAt(ahead_col, feet_row) ||
                                map->SolidAt(ahead_col, feet_row - 1);
        const bool gap_ahead = !map->SolidAt(ahead_col, feet_row + 1) &&
                               !map->OneWayAt(ahead_col, feet_row + 1);
        bool coin_ahead = false;
        for (int dc = 0; dc <= 3; ++dc) {
            for (int dr = -4; dr <= 0; ++dr) {
                if (map->TileAt(ahead_col + dc, feet_row + dr) == kGCoin) {
                    coin_ahead = true;
                }
            }
        }
        const bool ai_jump = wall_ahead || (player.OnGround() && gap_ahead) ||
                             coin_ahead || (frame % 80 == 0);
        const bool key_jump = input.KeyPressed("space") || input.KeyPressed("w") ||
                              input.KeyPressed("up");
        intent.jump_pressed = key_jump || (ai_jump && jump_hold == 0);
        if (intent.jump_pressed) {
            jump_hold = 12;
            e.GetAudio().Play("jump", 0.15);
        }
        intent.jump_held = jump_hold > 0;
        if (jump_hold > 0) {
            --jump_hold;
        }

        // --- physics / movement ---
        const fake2d::CharacterFrame pf =
            player.Update(static_cast<float>(e.DeltaTime()), intent);
        if (pf.jumped) {
            e.GetAudio().Play("jump", 0.12);
        }
        if (pf.landed && !prev_on_ground) {
            e.GetRenderer().GetCamera().AddTrauma(
                std::min(0.5f, pf.impact_speed / 1200.0f));
        }
        prev_on_ground = pf.on_ground;

        // Fell out of the world: respawn.
        if (player.Center().y > kPRows * kPTile + 60.0f) {
            player.Spawn(kSpawn);
        }

        // --- coin pickup by tile rewrite ---
        const fake2d::Vec2 c = player.Center();
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                const int col = static_cast<int>(c.x / kPTile) + dc;
                const int row = static_cast<int>(c.y / kPTile) + dr;
                if (map->TileAt(col, row) == kGCoin) {
                    map->SetTile(col, row, kGBack);
                    ++coins;
                    e.GetAudio().Play("coin", 0.35);
                }
            }
        }

        // --- goal ---
        if (map->TileAt(static_cast<int>(c.x / kPTile),
                        static_cast<int>(c.y / kPTile)) == kGGoal &&
            win_timer < 0) {
            win_timer = 0;
            e.GetRenderer().GetCamera().AddTrauma(0.7f);
            e.GetAudio().Play("win", 0.4);
        }
        if (win_timer >= 0) {
            ++win_timer;
            if (win_timer > 180) {
                reset();
            }
        }

        // --- follow camera (trauma shake applied inside the camera) ---
        auto &cam = e.GetRenderer().GetCamera();
        const float vw = cam.ViewportWidth();
        const float vh = cam.ViewportHeight();
        const float cam_x = std::clamp(c.x - vw * 0.5f, 0.0f,
                                       static_cast<float>(map->WidthPx() - vw));
        const float cam_y = std::clamp(c.y - vh * 0.55f, -20.0f,
                                       static_cast<float>(map->HeightPx() - vh));
        cam.SetPosition(cam_x, cam_y);

        // --- render ---
        map->Draw(e.GetRenderer());
        if (player_texture != nullptr) {
            e.GetRenderer().DrawSprite(*player_texture,
                                       player.Position().x - 2.0f,
                                       player.Position().y - 2.0f,
                                       24.0f, 34.0f);
        }

        // Debug line overlays: player box, coin rings, one-way tops, goal.
        const fake2d::Color cyan{0.3f, 0.9f, 1.0f, 0.9f};
        const fake2d::Color yellow{1.0f, 0.9f, 0.4f, 0.8f};
        const fake2d::Color green{0.5f, 1.0f, 0.6f, 0.8f};
        e.GetRenderer().DrawRectOutline(player.Bounds(), cyan);
        const int c0c = std::max(0, static_cast<int>(cam_x / kPTile));
        const int c1c = std::min(kPCols - 1, static_cast<int>((cam_x + vw) / kPTile));
        const int r0c = std::max(0, static_cast<int>(cam_y / kPTile));
        const int r1c = std::min(kPRows - 1, static_cast<int>((cam_y + vh) / kPTile));
        for (int row = r0c; row <= r1c; ++row) {
            for (int col = c0c; col <= c1c; ++col) {
                const float tx = static_cast<float>(col * kPTile);
                const float ty = static_cast<float>(row * kPTile);
                if (map->OneWayAt(col, row)) {
                    e.GetRenderer().DrawLine(tx, ty + 5.0f, tx + kPTile, ty + 5.0f, green);
                }
                if (map->TileAt(col, row) == kGCoin) {
                    e.GetRenderer().DrawCircleOutline(tx + 16.0f, ty + 16.0f, 13.0f, 16, yellow);
                }
                if (map->TileAt(col, row) == kGGoal) {
                    e.GetRenderer().DrawRectOutline({tx + 2, ty + 2, 28, 28}, green);
                }
            }
        }

        // --- HUD (anchored to the camera frustum in world space so it stays
        // on screen while the follow camera scrolls) ---
        e.GetUI().SetWorldOrigin({cam_x, cam_y});
        e.GetUI().Label(fake2d::UIAnchor::TopLeft, cam_x + 16.0f, cam_y + 12.0f,
                        "COINS " + std::to_string(coins) + "/" +
                            std::to_string(total_coins),
                        0.6f, fake2d::Color::FromRGBA8(245, 230, 130));
        e.GetUI().Label(fake2d::UIAnchor::TopLeft, cam_x + 16.0f, cam_y + 38.0f,
                        "A/D MOVE  SPACE JUMP", 0.4f,
                        fake2d::Color::FromRGBA8(170, 180, 205));
        if (e.GetUI().Button("restart", fake2d::UIAnchor::TopLeft,
                             cam_x + vw - 126.0f, cam_y + 10.0f, 110.0f, 32.0f,
                             "RESTART", 0.5f)) {
            reset();
        }
        if (win_timer >= 0) {
            const std::string banner = "LEVEL COMPLETE!";
            const float w = e.GetRenderer().MeasureText(banner, 1.4f);
            e.GetRenderer().DrawText(banner, cam_x + vw * 0.5f - w * 0.5f,
                                     cam_y + vh * 0.5f - 40.0f,
                                     1.4f, fake2d::Color::FromRGBA8(120, 235, 140));
        }

        if (!screenshot.empty() && frame == 140) {
            e.GetRenderer().GetSpriteBatch().Flush();
            e.GetRenderer().SaveScreenshot(screenshot);
        }
    });

    return engine.Run(max_frames);
}

// ---------------------------------------------------------------------------
// Phase 2/4 demo: C++ scene graph with a procedural atlas, bitmap text and
// particles (kept behind --scene-demo; also exercises Phase 4 headlessly).
// ---------------------------------------------------------------------------

int RunSceneDemo(fake2d::Engine &engine, const fake2d::TextureHandle sheet, int max_frames,
                 const std::string &screenshot) {
    fake2d::Atlas demo_atlas;
    if (!demo_atlas.LoadFromString(kAtlasJson, *engine.GetResources().GetTexture(sheet))) {
        std::fprintf(stderr, "fake2d_hello: failed to parse demo atlas\n");
        return 1;
    }

    fake2d::Scene scene;
    scene.Root().GetTransform().SetPosition(480.0f, 270.0f);

    auto orbit_group = std::make_unique<fake2d::Node>("orbit_group");
    fake2d::Node *orbit = orbit_group.get();
    scene.Root().AddChild(std::move(orbit_group));

    const auto add_sprite = [&](const char *name, const char *region,
                                float ox, float oy, int layer, float z, float size) {
        auto node = std::make_unique<fake2d::SpriteNode>(name);
        node->SetAtlasRegion(&demo_atlas, region);
        node->SetSize(size, size);
        node->SetPivot({0.5f, 0.5f});
        node->GetTransform().SetPosition(ox, oy);
        node->SetLayer(layer);
        node->SetZ(z);
        orbit->AddChild(std::move(node));
    };

    // Layer 0: yellow diamond sits behind everything on the orbit group.
    add_sprite("yellow", "yellow_diamond", 0.0f, -130.0f, 0, 0.0f, 72.0f);
    // Layer 1: the three shapes share a layer; z decides who wins overlaps.
    add_sprite("red", "red_circle", -160.0f, 0.0f, 1, 0.0f, 88.0f);
    add_sprite("green", "green_box", 0.0f, 0.0f, 1, 1.0f, 88.0f);
    add_sprite("blue", "blue_triangle", 160.0f, 0.0f, 1, 2.0f, 88.0f);

    // Layer 2: a solid panel above the sprites — layering beats tree insertion
    // order, and it shares position with the green box (z=1, layer=1) below it.
    auto panel = std::make_unique<fake2d::RectNode>("center_panel");
    panel->SetSize(64.0f, 64.0f);
    panel->SetPivot({0.5f, 0.5f});
    panel->SetTint(fake2d::Color::FromRGBA8(255, 214, 102, 235));
    panel->SetLayer(2);
    panel->GetTransform().SetPosition(0.0f, 0.0f);
    scene.Root().AddChild(std::move(panel));

    // Phase 4: a gold spark emitter — gravity + drag + shrinking quads,
    // all batched into the same SpriteBatch as the scene sprites.
    fake2d::ParticleSystem &particles = engine.GetParticles();
    const int sparks = particles.CreateEmitter();
    if (fake2d::ParticleConfig *cfg = particles.Config(sparks)) {
        cfg->lifetime_min = 0.4f;
        cfg->lifetime_max = 0.9f;
        cfg->speed_min = 30.0f;
        cfg->speed_max = 95.0f;
        cfg->angle = -1.5708f;
        cfg->spread = 3.14159f;
        cfg->start_size = 6.0f;
        cfg->end_size = 0.0f;
        cfg->gravity = {0.0f, 70.0f};
        cfg->drag = 1.2f;
        cfg->spin = 6.0f;
        cfg->color = fake2d::Color::FromRGBA8(255, 214, 102);
    }

    const std::string title = "fake2d - scene + text + particles";
    double elapsed = 0.0;
    engine.SetFrameCallback([&](fake2d::Engine &e) {
        elapsed += e.DeltaTime();
        orbit->GetTransform().SetRotation(static_cast<float>(elapsed) * 0.8f);
        const float s = 1.0f + 0.08f * std::sin(elapsed * 2.0f);
        orbit->GetTransform().SetScale(s);

        const float ex = 480.0f + 150.0f * std::cos(elapsed * 1.3);
        const float ey = 270.0f + 120.0f * std::sin(elapsed * 1.3);
        particles.Emit(sparks, 3, {ex, ey});

        scene.Draw(e.GetRenderer());
        particles.Draw(e.GetRenderer().GetSpriteBatch());
        const float title_w = e.GetRenderer().MeasureText(title, 0.8f);
        e.GetRenderer().DrawText(title, 480.0f - title_w * 0.5f, 24.0f, 0.8f,
                                 fake2d::Color::FromRGBA8(235, 240, 255));

        if (!screenshot.empty() && e.FrameIndex() == 20) {
            e.GetRenderer().GetSpriteBatch().Flush();
            e.GetRenderer().SaveScreenshot(screenshot);
        }
    });

    return engine.Run(max_frames);
}

} // namespace

int main(int argc, char **argv) {
    fake2d::EngineConfig cfg;
    cfg.title = "fake2d";
    cfg.width = 960;
    cfg.height = 540;
    cfg.script_entry = "scripts/game.lua";

    bool scene_demo = false;
    bool map_demo = false;
    bool platformer_demo = false;
    bool bench = false;
    bool phys_bench = false;
    int max_frames = 0;
    std::string screenshot;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--headless") {
            cfg.headless = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        } else if (arg == "--hot-reload") {
            cfg.hot_reload = true;
        } else if (arg == "--scene-demo") {
            scene_demo = true;
            cfg.script_entry = "scripts/main.lua";
        } else if (arg == "--map-demo") {
            map_demo = true;
            cfg.script_entry.clear();
        } else if (arg == "--platformer-demo") {
            platformer_demo = true;
            cfg.script_entry.clear();
        } else if (arg == "--screenshot" && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (arg == "--bench") {
            bench = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--phys-bench") {
            phys_bench = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        }
    }

    fake2d::Engine engine;
    if (!engine.Init(cfg)) {
        std::fprintf(stderr, "fake2d_hello: engine init failed\n");
        return 1;
    }

    EnsureGameAudio(engine.GetAudio());

    if (bench) {
        return RunBenchmark(engine, max_frames);
    }

    if (phys_bench) {
        return RunPhysicsBenchmark(engine);
    }

    EnsureGameAssets();

    if (map_demo) {
        return RunMapDemo(engine, max_frames, screenshot);
    }

    if (platformer_demo) {
        return RunPlatformerDemo(engine, max_frames, screenshot);
    }

    if (scene_demo) {
        std::vector<std::uint8_t> pixels(static_cast<size_t>(kSheetSize) * kSheetSize * 4);
        PaintDemoSheet(pixels.data());
        const fake2d::TextureHandle sheet =
            engine.GetResources().CreateTexture("demo_sheet", kSheetSize, kSheetSize, pixels.data());
        if (sheet == fake2d::kInvalidTextureHandle) {
            std::fprintf(stderr, "fake2d_hello: failed to create demo sheet\n");
            return 1;
        }
        return RunSceneDemo(engine, sheet, max_frames, screenshot);
    }

    // Frame 420: the attract-mode ball is in play, so HUD text, sprites and
    // any debris are all on screen.
    if (!screenshot.empty()) {
        engine.SetFrameCallback([&](fake2d::Engine &e) {
            if (e.FrameIndex() == 420) {
                e.GetRenderer().GetSpriteBatch().Flush();
                e.GetRenderer().SaveScreenshot(screenshot);
            }
        });
    }

    return engine.Run(max_frames);
}

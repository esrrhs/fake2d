// The demos are pure FakeLua scripts (scripts/*_demo.lua + game.lua).
// This binary only provides: engine bootstrap, procedural asset generation
// (PNG/Tiled JSON/WAV, since scripts cannot encode binary files), and the
// micro-benchmarks that measure engine internals.
#include "fake2d/engine.h"

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
// Asset scaffolding for scripts/map_demo.lua: tilesheet + Tiled JSON.
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
// Asset scaffolding for scripts/platformer_demo.lua: level tilesheet,
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
    sprite("m_goomba.png", 28, 28, PaintMarioGoomba);
    sprite("m_mushroom.png", 24, 24, PaintMushroom);
    sprite("m_player_s.png", 20, 28, PaintMarioPlayer);
    sprite("m_player_b.png", 24, 44, PaintMarioPlayer);

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

    // loose coins arc over the second gap approach
    if (at(55, 8) == 0) at(55, 8) = kMCoin;
    if (at(56, 7) == 0) at(56, 7) = kMCoin;
    if (at(57, 8) == 0) at(57, 8) = kMCoin;

    // goomba spawn markers (swept into entities on the first frame)
    at(22, 12) = kMGoomba;
    at(40, 12) = kMGoomba;
    at(51, 12) = kMGoomba;
    at(53, 12) = kMGoomba;
    at(66, 12) = kMGoomba;
    at(84, 12) = kMGoomba;
    at(86, 12) = kMGoomba;

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
    json << "  ]}]\n}\n";

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
            std::vector<std::uint8_t> buf(20 * 28 * 4, 0);
            PaintMarioPlayer(buf.data(), 20, 28);
            stbi_write_png(p.string().c_str(), 20, 28, 4, buf.data(), 20 * 4);
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

} // namespace

int main(int argc, char **argv) {
    fake2d::EngineConfig cfg;
    cfg.title = "fake2d";
    cfg.width = 960;
    cfg.height = 540;
    cfg.script_entry = "scripts/game.lua";

    bool map_demo = false;
    bool platformer_demo = false;
    bool mario_demo = false;
    bool slope_demo = false;
    bool bench = false;
    bool phys_bench = false;
    int max_frames = 0;
    int shot_frame = 30;
    std::string screenshot;
    bool regen_assets = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--headless") {
            cfg.headless = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        } else if (arg == "--hot-reload") {
            cfg.hot_reload = true;
        } else if (arg == "--scene-demo") {
            cfg.script_entry = "scripts/scene_demo.lua";
        } else if (arg == "--map-demo") {
            map_demo = true;
            cfg.script_entry = "scripts/map_demo.lua";
        } else if (arg == "--platformer-demo") {
            platformer_demo = true;
            cfg.script_entry = "scripts/platformer_demo.lua";
        } else if (arg == "--mario-demo") {
            mario_demo = true;
            cfg.script_entry = "scripts/mario.lua";
        } else if (arg == "--slope-demo") {
            slope_demo = true;
            cfg.script_entry = "scripts/slope_demo.lua";
        } else if (arg == "--screenshot" && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (arg == "--shot-frame" && i + 1 < argc) {
            shot_frame = std::atoi(argv[++i]);
        } else if (arg == "--regen-assets") {
            // Rewrite the generated sheets/levels even if they exist, so
            // painter or layout edits actually reach the screen.
            regen_assets = true;
        } else if (arg == "--bench") {
            bench = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--phys-bench") {
            phys_bench = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--entry" && i + 1 < argc) {
            // Dev/test hook: run an arbitrary script instead of a demo.
            cfg.script_entry = argv[++i];
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

    // All gameplay is pure Lua below; C++ only guarantees the binary asset
    // scaffolding exists before the script's first frame.
    EnsureGameAssets();
    if (mario_demo) {
        EnsureMarioAssets(regen_assets);
    }
    if (slope_demo) {
        EnsureSlopeAssets(regen_assets);
    }
    if (map_demo) {
        EnsureMapAssets();
    }
    if (platformer_demo) {
        EnsurePlatformerAssets();
        engine.GetAudio().AddClipWav("music", "assets/music.wav");
    }
    // Generic host-level screenshot hook (visual regression; not game logic).
    if (!screenshot.empty()) {
        engine.SetFrameCallback([&](fake2d::Engine &e) {
            if (e.FrameIndex() == static_cast<std::uint64_t>(shot_frame)) {
                e.GetRenderer().GetSpriteBatch().Flush();
                e.GetRenderer().SaveScreenshot(screenshot);
            }
        });
    }

    return engine.Run(max_frames);
}

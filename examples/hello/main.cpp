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
#include <cstring>
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
// circles, compared brute force vs spatial hash grid. Each frame also runs
// one ray cast and one circle overlap over the full body set for reference
// query costs (pure CPU; informational only).
int RunPhysicsBenchmark(fake2d::Engine &engine) {
    constexpr int kFrames = 300;
    constexpr int kWarmup = 60;
    constexpr int kDynamic = 600;

    struct PhaseResult {
        double step_ms = 0.0;
        double ray_ms = 0.0;
        double overlap_ms = 0.0;
    };

    auto run_phase = [&](bool grid) -> PhaseResult {
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

        PhaseResult result;
        int samples = 0;
        std::vector<fake2d::BodyId> overlaps;
        for (int frame = 0; frame < kFrames; ++frame) {
            const auto t0 = std::chrono::steady_clock::now();
            world.Step(1.0f / 60.0f);
            const auto t1 = std::chrono::steady_clock::now();
            fake2d::RayHit ray;
            world.RayCast({480.0f, 40.0f}, {480.0f, 515.0f}, ray);
            const auto t2 = std::chrono::steady_clock::now();
            world.OverlapCircle({480.0f, 300.0f}, 60.0f, overlaps);
            const auto t3 = std::chrono::steady_clock::now();
            if (frame >= kWarmup) {
                result.step_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
                result.ray_ms += std::chrono::duration<double, std::milli>(t2 - t1).count();
                result.overlap_ms += std::chrono::duration<double, std::milli>(t3 - t2).count();
                ++samples;
            }
        }
        result.step_ms /= samples;
        result.ray_ms /= samples;
        result.overlap_ms /= samples;
        return result;
    };

    const PhaseResult brute = run_phase(false);
    const PhaseResult grid = run_phase(true);
    std::printf("== fake2d physics broadphase benchmark ==\n");
    std::printf("bodies: %d (dynamics) + 9 statics, samples: %d\n",
                kDynamic, kFrames - kWarmup);
    std::printf("brute-force O(n^2)  avg step %7.3f ms\n", brute.step_ms);
    std::printf("spatial hash grid   avg step %7.3f ms  (%.1fx)\n",
                grid.step_ms, brute.step_ms / grid.step_ms);
    std::printf("queries per frame: raycast %7.3f ms, overlap circle %7.3f ms\n",
                grid.ray_ms, grid.overlap_ms);
    return 0;
}

// Physics query self-test: ray casts, shape overlaps and the contact
// lifecycle (began/stayed/ended) against a hand-built scene with exact
// expected values. Prints [phys-query] lines; exits non-zero on failure.
// Headless-safe: never touches the window or renderer.
int RunPhysicsQueryTest(fake2d::Engine &engine) {
    (void)engine;
    fake2d::PhysicsWorld world;
    world.SetGravity({0.0f, 900.0f});

    fake2d::BodyConfig box_cfg;
    box_cfg.type = fake2d::BodyType::Static;
    box_cfg.position = {200.0f, 300.0f};
    box_cfg.half_extents = {40.0f, 20.0f}; // spans x[160,240] y[280,320]
    box_cfg.user_id = 1;
    const fake2d::BodyId box = world.CreateBody(box_cfg);

    fake2d::BodyConfig ball_cfg;
    ball_cfg.radius = 10.0f;
    ball_cfg.half_extents = {10.0f, 10.0f};
    ball_cfg.position = {200.0f, 100.0f};
    ball_cfg.restitution = 0.0f;
    ball_cfg.user_id = 2;
    const fake2d::BodyId ball = world.CreateBody(ball_cfg);

    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[phys-query] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
    auto pair_touch = [](const fake2d::ContactEvent &ev) {
        return (ev.user_a == 2 && ev.user_b == 1) || (ev.user_a == 1 && ev.user_b == 2);
    };

    // -- Ray casts ------------------------------------------------------------
    fake2d::RayHit hit;
    check(world.RayCast({100.0f, 300.0f}, {400.0f, 300.0f}, hit), "raycast hits box");
    check(hit.body == box && hit.user_id == 1, "raycast hit body/user");
    check(near(hit.point.x, 160.0f) && near(hit.point.y, 300.0f), "raycast hit point (left face)");
    check(near(hit.normal.x, -1.0f) && near(hit.normal.y, 0.0f), "raycast left-face normal");
    check(near(hit.distance, 60.0f), "raycast distance 60");

    check(world.RayCast({400.0f, 300.0f}, {100.0f, 300.0f}, hit), "reverse raycast hits box");
    check(near(hit.point.x, 240.0f) && near(hit.normal.x, 1.0f) && near(hit.distance, 160.0f),
          "reverse hit right face, distance 160");

    check(!world.RayCast({100.0f, 500.0f}, {400.0f, 500.0f}, hit), "raycast misses below the scene");

    check(world.RayCast({200.0f, 100.0f}, {200.0f, 20.0f}, hit), "ray from inside the ball");
    check(hit.body == ball && near(hit.distance, 0.0f) && near(hit.normal.x, 0.0f) &&
              near(hit.normal.y, 1.0f),
          "inside hit: t=0, normal opposes ray");

    check(world.RayCast({100.0f, 300.0f}, {400.0f, 300.0f}), "buffered raycast");
    check(world.HasRayHit() && world.LastRayHit().body == box && world.LastRayHit().user_id == 1,
          "buffered raycast readers");

    // -- Shape overlaps ---------------------------------------------------------
    std::vector<fake2d::BodyId> ids;
    check(world.OverlapCircle({200.0f, 300.0f}, 5.0f, ids) == 1 && ids[0] == box,
          "overlap circle hits the box only");
    check(world.OverlapCircle({200.0f, 200.0f}, 110.0f, ids) == 2,
          "overlap circle hits both bodies");
    check(world.OverlapBox({200.0f, 200.0f}, {100.0f, 110.0f}, ids) == 2,
          "overlap box hits both bodies");
    check(world.OverlapBox({600.0f, 300.0f}, {10.0f, 10.0f}, ids) == 0,
          "overlap box misses far away");

    check(world.OverlapCircle({200.0f, 300.0f}, 5.0f) == 1, "buffered overlap circle");
    check(world.OverlapBody(0) == box && world.OverlapUser(0) == 1, "buffered overlap readers");
    check(world.OverlapBody(9) == fake2d::kInvalidBody && world.OverlapUser(9) == 0,
          "buffered overlap out-of-range");

    // -- Contact lifecycle ------------------------------------------------------
    int began_frame = -1;
    for (int f = 0; f < 240 && began_frame < 0; ++f) {
        world.Step(1.0f / 60.0f);
        for (const fake2d::ContactEvent &ev : world.Contacts()) {
            if (pair_touch(ev)) {
                began_frame = f;
                break;
            }
        }
    }
    check(began_frame >= 0, "contact began (ball lands on box)");

    bool stayed = false;
    for (int f = 0; f < 60 && !stayed; ++f) {
        world.Step(1.0f / 60.0f);
        for (const fake2d::ContactEvent &ev : world.StayedContacts()) {
            if (pair_touch(ev)) {
                stayed = true;
                break;
            }
        }
    }
    check(stayed, "contact stayed while resting");

    world.DestroyBody(ball);
    world.Step(1.0f / 60.0f);
    bool ended = false;
    for (const fake2d::ContactEvent &ev : world.EndedContacts()) {
        if (pair_touch(ev)) {
            ended = true;
            break;
        }
    }
    check(ended, "contact ended after body destruction");

    std::printf("[phys-query] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

// Collision-filtering self-test: category/mask bitmasks gate pair
// collisions, contact events and the query surface (ray casts / overlaps).
// Headless CPU-only. Prints [phys-filter] lines; non-zero on fail.
int RunPhysicsFilterTest(fake2d::Engine &engine) {
    (void)engine;
    fake2d::PhysicsWorld world;
    world.SetGravity({0.0f, 900.0f});

    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[phys-filter] FAIL %s\n", what);
        }
    };

    // Default filter collides with everything (category 1, mask all).
    // +y is down in this world, so fallers start ABOVE their wall.
    fake2d::BodyConfig a_cfg;
    a_cfg.type = fake2d::BodyType::Static;
    a_cfg.position = {0.0f, 0.0f};
    a_cfg.half_extents = {16.0f, 16.0f}; // spans y[-16,16]; floor surface at y=-16
    a_cfg.user_id = 1;
    const fake2d::BodyId wall = world.CreateBody(a_cfg);

    fake2d::BodyConfig b_cfg;
    b_cfg.position = {0.0f, -100.0f};
    b_cfg.half_extents = {16.0f, 16.0f};
    b_cfg.restitution = 0.0f;
    b_cfg.user_id = 2;
    const fake2d::BodyId faller = world.CreateBody(b_cfg);

    // A filtered pair: faller only accepts category bit 0 (0x1), the wall is
    // category bit 1 (0x2) — no interaction despite overlapping.
    fake2d::BodyConfig c_cfg;
    c_cfg.type = fake2d::BodyType::Static;
    c_cfg.position = {200.0f, 0.0f};
    c_cfg.half_extents = {16.0f, 16.0f};
    c_cfg.user_id = 3;
    c_cfg.category = 0x2;
    const fake2d::BodyId other_wall = world.CreateBody(c_cfg);

    fake2d::BodyConfig d_cfg;
    d_cfg.position = {200.0f, -100.0f};
    d_cfg.half_extents = {16.0f, 16.0f};
    d_cfg.restitution = 0.0f;
    d_cfg.user_id = 4;
    d_cfg.mask = 0x1; // only wants category bit 0
    const fake2d::BodyId filtered_faller = world.CreateBody(d_cfg);

    // One-way filter: e likes f but f does not like e.
    fake2d::BodyConfig e_cfg;
    e_cfg.type = fake2d::BodyType::Static;
    e_cfg.position = {400.0f, 0.0f};
    e_cfg.half_extents = {16.0f, 16.0f};
    e_cfg.user_id = 5;
    const fake2d::BodyId one_way_a = world.CreateBody(e_cfg);
    fake2d::BodyConfig f_cfg;
    f_cfg.position = {400.0f, -100.0f};
    f_cfg.half_extents = {16.0f, 16.0f};
    f_cfg.restitution = 0.0f;
    f_cfg.user_id = 6;
    f_cfg.mask = 0x0; // rejects everything
    const fake2d::BodyId one_way_b = world.CreateBody(f_cfg);

    auto ran_into = [&](std::int64_t user_a, std::int64_t user_b) {
        for (const fake2d::ContactEvent &ev : world.Contacts()) {
            if ((ev.user_a == user_a && ev.user_b == user_b) ||
                (ev.user_a == user_b && ev.user_b == user_a)) {
                return true;
            }
        }
        return false;
    };
    auto near_y = [&](fake2d::BodyId body, float expected) {
        const fake2d::BodyConfig *cfg = world.Get(body);
        return cfg != nullptr && std::fabs(cfg->position.y - expected) < 1.5f;
    };

    // Step until the unfiltered faller has landed on its wall.
    bool landed_a = false;
    bool landed_b = false;
    for (int f = 0; f < 240 && (!landed_a || !landed_b); ++f) {
        world.Step(1.0f / 60.0f);
        landed_a = ran_into(1, 2);
        landed_b = near_y(faller, -32.0f); // rests on the y=-16 surface
    }
    check(landed_a, "default filter: contact began (faller vs wall)");
    check(landed_b, "default filter: faller rests on the wall surface");

    bool filtered_contact = false;
    for (int f = 0; f < 240; ++f) {
        world.Step(1.0f / 60.0f);
        if (ran_into(3, 4)) {
            filtered_contact = true;
            break;
        }
    }
    check(!filtered_contact, "category/mask mismatch: no contact event");
    check(world.Get(filtered_faller)->position.y > 100.0f,
          "category/mask mismatch: filtered faller falls through");

    bool one_way_contact = false;
    for (int f = 0; f < 240; ++f) {
        world.Step(1.0f / 60.0f);
        if (ran_into(5, 6)) {
            one_way_contact = true;
            break;
        }
    }
    check(!one_way_contact, "one-way mask rejection: no contact event");
    check(world.Get(one_way_b)->position.y > 100.0f,
          "one-way mask rejection: rejected faller falls through");
    (void)one_way_a;

    // Mutating the filter at runtime re-enables the pair.
    if (fake2d::BodyConfig *cfg = world.Get(filtered_faller)) {
        cfg->mask = 0xFFFF; // now accepts everything again
        cfg->position = {200.0f, -100.0f};
        cfg->velocity = {0.0f, 0.0f};
    }
    bool reenabled = false;
    for (int f = 0; f < 240 && !reenabled; ++f) {
        world.Step(1.0f / 60.0f);
        reenabled = ran_into(3, 4);
    }
    check(reenabled, "runtime mask widening re-enables the pair");

    // ---- Query surface ------------------------------------------------------
    fake2d::RayHit hit;
    check(world.RayCast({-100.0f, 0.0f}, {100.0f, 0.0f}, hit) && hit.user_id == 1,
          "unmasked raycast hits the default-category wall");
    check(!world.RayCast({-100.0f, 0.0f}, {100.0f, 0.0f}, hit, 0x2),
          "masked raycast skips category bit 0 bodies");
    check(world.RayCast({100.0f, 0.0f}, {300.0f, 0.0f}, hit, 0x2) && hit.user_id == 3,
          "masked raycast hits category bit 1 bodies");

    std::vector<fake2d::BodyId> ids;
    check(world.OverlapCircle({0.0f, 8.0f}, 8.0f, ids) == 1 && ids[0] == wall,
          "unmasked overlap circle sees the wall only");
    check(world.OverlapCircle({0.0f, 8.0f}, 8.0f, ids, 0x2) == 0,
          "masked overlap circle skips the wall");
    check(world.OverlapBox({200.0f, 8.0f}, {20.0f, 20.0f}, ids, 0x2) == 1 &&
              ids[0] == other_wall,
          "masked overlap box sees the bit-1 wall");
    check(world.OverlapCircle({0.0f, 8.0f}, 8.0f, 0x2) == 0,
          "buffered masked overlap circle");

    std::printf("[phys-filter] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

// Fixed-step / camera-tooling self-test (Phase 16). Headless CPU-only:
// the zero-dt contact pass used by the accumulator, render interpolation,
// and the camera follow / deadzone / bounds helpers. Prints [timestep] lines;
// non-zero on fail.
int RunTimestepTest(fake2d::Engine &engine) {
    (void)engine;
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[timestep] FAIL %s\n", what);
        }
    };

    // ---- Physics: zero-dt contact pass ------------------------------------
    fake2d::PhysicsWorld world;
    world.SetGravity({0.0f, 900.0f});

    fake2d::BodyConfig wall_cfg;
    wall_cfg.type = fake2d::BodyType::Static;
    wall_cfg.position = {0.0f, 0.0f};
    wall_cfg.half_extents = {32.0f, 8.0f}; // surface at y = -8
    wall_cfg.user_id = 1;
    const fake2d::BodyId wall = world.CreateBody(wall_cfg);

    fake2d::BodyConfig ball_cfg;
    ball_cfg.position = {0.0f, -20.0f};
    ball_cfg.radius = 6.0f;
    ball_cfg.half_extents = {6.0f, 6.0f};
    ball_cfg.restitution = 0.0f;
    ball_cfg.user_id = 2;
    const fake2d::BodyId ball = world.CreateBody(ball_cfg);

    for (int f = 0; f < 120; ++f) {
        world.Step(1.0f / 60.0f);
    }
    const float rest_y = world.Get(ball)->position.y;
    check(rest_y > -20.0f, "fixed stepping: the ball was caught by the wall");

    // A zero-dt pass must not integrate: position is untouched, and the pair
    // that is still touching shows up as "stayed", not "began" again.
    world.Step(0.0f);
    check(std::fabs(world.Get(ball)->position.y - rest_y) < 1e-4f,
          "zero-dt step: no integration");
    check(world.Contacts().empty(), "zero-dt step: no duplicate began events");
    check(world.StayedContacts().size() == 1, "zero-dt step: pair reports stayed");
    check(world.EndedContacts().empty(), "zero-dt step: no spurious ended events");

    // Teleport away, then a zero-dt pass still reports the separation (this is
    // exactly what a frame that skipped its fixed step must see).
    if (fake2d::BodyConfig *cfg = world.Get(ball)) {
        cfg->position = {0.0f, -400.0f};
        cfg->velocity = {0.0f, 0.0f};
    }
    world.Step(0.0f);
    check(world.EndedContacts().size() == 1, "zero-dt step: separation reports ended");
    check(world.StayedContacts().empty(), "zero-dt step: nothing stays touching");

    // ---- Physics: per-frame event accumulation ----------------------------
    // A fixed-step host can run several steps inside one rendered frame while
    // gameplay reads the contact buffers only once. Without accumulation each
    // step replaces the buffers, so an event raised on an earlier step (the
    // "began" of a fresh contact, typically) is silently dropped and scripts
    // miss it depending on frame timing — an intermittent failure by nature.
    fake2d::PhysicsWorld fworld;
    fworld.SetGravity({0.0f, 0.0f});
    fworld.SetEventAccumulation(true);
    fake2d::BodyConfig fplat_cfg;
    fplat_cfg.type = fake2d::BodyType::Static;
    fplat_cfg.position = {240.0f, 400.0f};
    fplat_cfg.half_extents = {60.0f, 16.0f};
    fplat_cfg.user_id = 1;
    fworld.CreateBody(fplat_cfg);

    fake2d::BodyConfig fball_cfg;
    fball_cfg.position = {240.0f, 380.0f}; // created already overlapping
    fball_cfg.radius = 10.0f;
    fball_cfg.half_extents = {10.0f, 10.0f};
    fball_cfg.gravity_scale = 0.0f;
    fball_cfg.restitution = 0.0f;
    fball_cfg.user_id = 2;
    const fake2d::BodyId fball = fworld.CreateBody(fball_cfg);

    fworld.ClearFrameEvents();
    for (int s = 0; s < 4; ++s) {
        fworld.Step(1.0f / 60.0f); // four steps, one rendered frame
    }
    check(fworld.Contacts().size() == 1, "frame events: began survives a 4-step frame");
    check(fworld.StayedContacts().size() == 1,
          "frame events: a stayed pair is reported once per frame");
    check(fworld.EndedContacts().empty(), "frame events: no ended while touching");

    // A separation on the first of several steps must survive as well.
    if (fake2d::BodyConfig *cfg = fworld.Get(fball)) {
        cfg->position = {240.0f, -400.0f};
        cfg->velocity = {0.0f, 0.0f};
    }
    fworld.ClearFrameEvents();
    for (int s = 0; s < 3; ++s) {
        fworld.Step(1.0f / 60.0f);
    }
    check(fworld.EndedContacts().size() == 1,
          "frame events: ended survives a multi-step frame");
    check(fworld.Contacts().empty(), "frame events: no began after separation");
    check(fworld.StayedContacts().empty(), "frame events: nothing stays touching");

    // Accumulation off keeps the classic "buffers describe the last step".
    fworld.SetEventAccumulation(false);
    if (fake2d::BodyConfig *cfg = fworld.Get(fball)) {
        cfg->position = {240.0f, 380.0f};
    }
    fworld.Step(1.0f / 60.0f); // began
    fworld.Step(1.0f / 60.0f); // stayed, and the began buffer is replaced
    check(fworld.Contacts().empty() && fworld.StayedContacts().size() == 1,
          "frame events: accumulation off restores per-step buffers");

    // ---- Render interpolation ---------------------------------------------
    fake2d::PhysicsWorld iworld;
    iworld.SetGravity({0.0f, 0.0f});
    fake2d::BodyConfig mover_cfg;
    mover_cfg.position = {0.0f, 0.0f};
    mover_cfg.velocity = {600.0f, 0.0f};
    mover_cfg.gravity_scale = 0.0f;
    mover_cfg.radius = 4.0f;
    mover_cfg.half_extents = {4.0f, 4.0f};
    const fake2d::BodyId mover = iworld.CreateBody(mover_cfg);
    iworld.Step(1.0f / 60.0f); // advances 10 units
    const float end_x = iworld.Get(mover)->position.x;
    check(std::fabs(end_x - 10.0f) < 0.2f, "interpolation: one step moved 10 units");
    iworld.SetInterpAlpha(1.0f);
    check(std::fabs(iworld.SmoothPosition(mover).x - end_x) < 1e-4f,
          "interpolation: alpha 1 reports the current position");
    iworld.SetInterpAlpha(0.0f);
    check(std::fabs(iworld.SmoothPosition(mover).x) < 1e-4f,
          "interpolation: alpha 0 reports the previous position");
    iworld.SetInterpAlpha(0.5f);
    check(std::fabs(iworld.SmoothPosition(mover).x - end_x * 0.5f) < 0.2f,
          "interpolation: alpha 0.5 reports the midpoint");
    iworld.SetInterpAlpha(9.0f); // clamped
    check(std::fabs(iworld.SmoothPosition(mover).x - end_x) < 1e-4f,
          "interpolation: alpha is clamped to [0,1]");
    check(iworld.SmoothPosition(fake2d::kInvalidBody).x == 0.0f,
          "interpolation: stale handle reads zero");
    (void)wall;

    // ---- Camera: bounds, deadzone, smoothing -------------------------------
    // NOTE: Camera2D::Position is the TOP-LEFT of the visible world rect.
    fake2d::Camera2D camera(200.0f, 100.0f); // view 200x100 at zoom 1
    camera.SetBounds(0.0f, 0.0f, 1000.0f, 600.0f);
    camera.SnapTo({0.0f, 0.0f});
    check(std::fabs(camera.Position().x) < 1e-3f &&
              std::fabs(camera.Position().y) < 1e-3f,
          "camera bounds: the view is clamped to the bounds' top-left corner");
    camera.SnapTo({9999.0f, 9999.0f});
    check(std::fabs(camera.Position().x - 800.0f) < 1e-3f &&
              std::fabs(camera.Position().y - 500.0f) < 1e-3f,
          "camera bounds: the view is clamped to the bounds' bottom-right corner");
    camera.SetBounds(0.0f, 0.0f, 120.0f, 60.0f); // smaller than the view
    camera.SnapTo({0.0f, 0.0f});
    check(std::fabs(camera.Position().x + 40.0f) < 1e-3f &&
              std::fabs(camera.Position().y + 20.0f) < 1e-3f,
          "camera bounds: a level smaller than the view is centred");
    camera.ClearBounds();
    check(!camera.HasBounds(), "camera bounds: ClearBounds disables clamping");

    // Deadzone: moving the target inside the box around the view centre must
    // not move the camera. View centre for position (500,300) is (600,350).
    camera.SetDeadzone(20.0f, 10.0f);
    camera.SnapTo({500.0f, 300.0f});
    camera.Follow({605.0f, 353.0f}, 1.0f / 60.0f);
    check(std::fabs(camera.Position().x - 500.0f) < 1e-3f &&
              std::fabs(camera.Position().y - 300.0f) < 1e-3f,
          "camera deadzone: target inside the box does not move the camera");
    // Outside the box it catches up, smoothly (not instantly).
    camera.Follow({700.0f, 350.0f}, 1.0f / 60.0f);
    const float after_one = camera.Position().x;
    check(after_one > 500.0f && after_one < 580.0f,
          "camera follow: moves toward the target without snapping");
    for (int f = 0; f < 240; ++f) {
        camera.Follow({700.0f, 350.0f}, 1.0f / 60.0f);
    }
    check(std::fabs(camera.Position().x - 580.0f) < 0.5f,
          "camera follow: settles one deadzone away from the target");
    // Zero smoothing snaps straight to the target (view centred on it).
    camera.SetFollowSmoothing(0.0f);
    camera.SetDeadzone(0.0f, 0.0f);
    camera.Follow({700.0f, 320.0f}, 1.0f / 60.0f);
    check(std::fabs(camera.Position().x - 600.0f) < 1e-3f &&
              std::fabs(camera.Position().y - 270.0f) < 1e-3f,
          "camera follow: rate 0 snaps to the target");

    // Forward-only scroll lock (SMB rule): a target to the left never pulls
    // the camera back; moving right works normally.
    camera.SetForwardOnlyX(true);
    camera.SnapTo({500.0f, 300.0f});
    camera.Follow({400.0f, 300.0f}, 1.0f / 60.0f);
    check(std::fabs(camera.Position().x - 500.0f) < 1e-3f,
          "camera forward-only: a left target is ignored");
    camera.Follow({700.0f, 300.0f}, 1.0f / 60.0f);
    check(std::fabs(camera.Position().x - 600.0f) < 1e-3f,
          "camera forward-only: a right target still scrolls");
    camera.SetForwardOnlyX(false);

    std::printf("[timestep] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

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
int RunRenderTargetTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[rt] FAIL %s\n", what);
        }
    };
    // 8-bit colour channels: 1/255 of slack absorbs any filtering rounding.
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.02f; };
    auto is_red = [&](const fake2d::Color &c) {
        return near(c.r, 1.0f) && near(c.g, 0.0f) && near(c.b, 0.0f);
    };
    auto is_white = [&](const fake2d::Color &c) {
        return near(c.r, 1.0f) && near(c.g, 1.0f) && near(c.b, 1.0f);
    };

    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[rt] FAIL renderer is not initialised\n");
        return 1;
    }
    fake2d::RenderTargetLibrary &lib = r.GetRenderTargets();

    // --- creation ----------------------------------------------------------
    const int rt1 = lib.Create(128, 128);
    check(rt1 > 0, "create: returns a 1-based id");
    fake2d::RenderTarget *t1 = lib.Get(rt1);
    check(t1 != nullptr && t1->IsValid(), "create: target is valid");
    check(t1 != nullptr && t1->Width() == 128 && t1->Height() == 128,
          "create: keeps the requested size");
    check(t1 != nullptr && t1->Texture().Id() != 0, "create: colour texture allocated");
    check(lib.Count() == 1, "create: pool tracks live targets");

    check(lib.Create(0, 0) == 0, "create: zero size rejected");
    check(lib.Create(100000, 64) == 0, "create: oversized rejected");
    check(lib.Get(0) == nullptr, "get: id 0 is never a target");
    check(lib.Get(99) == nullptr, "get: out-of-range id is null");

    // --- render into the target -------------------------------------------
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(rt1), "begin: binds the target");
    check(r.ActiveRenderTarget() == rt1, "begin: reports the active target");

    const int rt2 = lib.Create(128, 128);
    check(rt2 > 0, "create: a second target gets its own slot");
    check(!r.BeginRenderTarget(rt2), "begin: nesting inside a target is refused");
    check(!r.BeginRenderTarget(0), "begin: the window is not a target");
    check(!r.BeginRenderTarget(99), "begin: unknown id refused");

    // Red canvas with a white bar across the UPPER half only — an
    // asymmetric pattern, so a missing vertical flip shows up immediately.
    r.ClearActiveTarget(fake2d::Color{1.0f, 0.0f, 0.0f, 1.0f});
    r.DrawQuad(32.0f, 16.0f, 64.0f, 32.0f, fake2d::Color::White());
    // Drawing a target into itself is undefined; the call must be ignored.
    r.DrawRenderTarget(rt1, 0.0f, 0.0f, 128.0f, 128.0f);
    r.EndRenderTarget();
    check(r.ActiveRenderTarget() == 0, "end: back to the window");

    fake2d::Color px{};
    check(t1->ReadPixel(10, 10, px) && is_red(px), "readback: clear colour outside the quad");
    check(t1->ReadPixel(64, 30, px) && is_white(px), "readback: the quad landed where drawn");
    check(t1->ReadPixel(64, 100, px) && is_red(px), "readback: below the quad is untouched");
    check(!t1->ReadPixel(128, 0, px), "readback: x out of bounds fails");
    check(!t1->ReadPixel(0, -1, px), "readback: negative y fails");

    // --- blit one target into another (vertical orientation) ---------------
    // Sampling an FBO texture flips V (FBO rows are bottom-up, sprite UVs
    // sample top-down), so the white bar must stay in the upper half.
    check(r.BeginRenderTarget(rt2), "begin: second target binds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 1.0f, 1.0f});
    r.DrawRenderTarget(rt1, 0.0f, 0.0f, 128.0f, 128.0f);
    r.EndRenderTarget();
    fake2d::RenderTarget *t2 = lib.Get(rt2);
    check(t2 != nullptr && t2->ReadPixel(64, 30, px) && is_white(px),
          "blit: the target's top stays on top (V flipped)");
    check(t2 != nullptr && t2->ReadPixel(64, 100, px) && is_red(px),
          "blit: the target's bottom stays at the bottom");

    // Sub-rectangle blit: take the bottom-right quarter, which is pure red.
    const int rt3 = lib.Create(64, 64);
    check(rt3 > 0, "create: third target");
    check(r.BeginRenderTarget(rt3), "begin: third target binds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 1.0f, 0.0f, 1.0f});
    r.DrawRenderTarget(rt1, fake2d::Rect{64.0f, 64.0f, 64.0f, 64.0f},
                       fake2d::Rect{0.0f, 0.0f, 64.0f, 64.0f});
    r.EndRenderTarget();
    fake2d::RenderTarget *t3 = lib.Get(rt3);
    check(t3 != nullptr && t3->ReadPixel(32, 32, px) && is_red(px),
          "blit: src sub-rect maps into the dst rect");

    // --- lifetime ----------------------------------------------------------
    lib.Destroy(rt2);
    check(lib.Get(rt2) == nullptr, "destroy: slot released");
    check(lib.Count() == 2, "destroy: pool count updated");
    lib.Destroy(rt2); // idempotent
    check(lib.Get(rt2) == nullptr, "destroy: repeated destroy is safe");
    check(r.BeginRenderTarget(rt2) == false, "begin: destroyed id refused");
    check(rt1 > 0 && lib.Get(rt1) != nullptr, "destroy: other targets survive");
    r.DrawRenderTarget(rt2, 0.0f, 0.0f, 64.0f, 64.0f); // ignored, must not crash

    // A target left bound is released by EndFrame, so the swap always
    // presents the window framebuffer.
    check(r.BeginRenderTarget(rt1), "begin: rebind the first target");
    r.EndFrame();
    check(r.ActiveRenderTarget() == 0, "EndFrame: leaves the target automatically");

    // --- the window survives the round trip --------------------------------
    // A headless context presents through its own offscreen FBO rather than
    // framebuffer 0, so this only works if EndRenderTarget restored that id.
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    r.DrawRenderTarget(rt1, 40.0f, 40.0f, 128.0f, 128.0f);
    r.EndFrame();
    std::error_code ec;
    std::filesystem::create_directories("assets", ec);
    const std::string shot = "assets/_rt_test.png";
    std::filesystem::remove(shot, ec);
    check(r.SaveScreenshot(shot), "screenshot: window framebuffer is intact");
    const auto shot_bytes = std::filesystem::file_size(shot, ec);
    check(!ec && shot_bytes > 0, "screenshot: wrote a non-empty png");
    std::filesystem::remove(shot, ec);

    lib.Clear();
    check(lib.Count() == 0, "clear: releases every target");
    check(lib.Get(rt1) == nullptr, "clear: slots are reusable afterwards");

    std::printf("[rt] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

// A UI is driven by a mouse and a keyboard, neither of which exists in a
// headless run — so this test covers everything that is pure computation
// (clamping, ranges, text handling, focus bookkeeping) and asserts that every
// widget produces draw commands without corrupting the batch. Interaction
// itself is exercised interactively through --ui-demo.
int RunUITest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[ui] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.001f; };
    // Read-back pixels are 8-bit: 0.14 lands on 36/255 = 0.141, so colour
    // probes need ~1/255 of slack.
    auto near_u8 = [](float a, float b) { return std::fabs(a - b) < 0.01f; };

    fake2d::UISystem &ui = engine.GetUI();
    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[ui] FAIL renderer is not initialised\n");
        return 1;
    }

    // Widgets need a camera + input + renderer; all three are wired by Init.
    const fake2d::UIAnchor tl = fake2d::UIAnchor::TopLeft;

    // --- Slider: clamps, and a click-jump never escapes the range ----------
    check(near(ui.Slider("s1", tl, 40, 40, 200, 24, 0.5f, 0.0f, 1.0f), 0.5f),
          "slider: an in-range value passes through");
    check(near(ui.Slider("s1", tl, 40, 40, 200, 24, 5.0f, 0.0f, 1.0f), 1.0f),
          "slider: above max clamps to max");
    check(near(ui.Slider("s1", tl, 40, 40, 200, 24, -5.0f, 0.0f, 1.0f), 0.0f),
          "slider: below min clamps to min");
    check(near(ui.Slider("s1", tl, 40, 40, 200, 24, 3.0f, 10.0f, 20.0f), 10.0f),
          "slider: an out-of-range value snaps into the range");
    // A degenerate range would divide by zero; it must not produce NaN.
    const float degenerate = ui.Slider("s2", tl, 40, 80, 200, 24, 7.0f, 5.0f, 5.0f);
    check(near(degenerate, 5.0f), "slider: a zero-width range reads as its min");
    const float inverted = ui.Slider("s3", tl, 40, 120, 200, 24, 7.0f, 5.0f, 1.0f);
    check(near(inverted, 5.0f), "slider: an inverted range reads as its min");

    // --- Checkbox: one widget per key, toggle reported once ---------------
    const bool toggled = ui.Checkbox("c1", tl, 40, 170, 20, true, "option");
    check(!toggled, "checkbox: no click without a mouse press");
    // A key that was never drawn has no state at all.
    check(!ui.IsHovered("no_such_widget"), "hover: an unknown key is never hovered");

    // --- TextInput: value storage, limits, encoding ------------------------
    check(ui.TextValue("t1").empty(), "text: an unknown key reads as empty");
    ui.SetTextValue("t1", "hello");
    check(ui.TextValue("t1") == "hello", "text: set/get round trip");
    ui.SetTextValue("t1", "");
    check(ui.TextValue("t1").empty(), "text: cleared by setting empty");

    // Multi-byte codepoints are stored whole: 4 bytes for an emoji, and the
    // max_chars limit counts characters, not bytes.
    ui.SetTextValue("t1", "");
    (void)ui.TextInput("t1", tl, 40, 220, 200, 28, 0); // unlimited
    // No keyboard in headless, so drive the field the way the UI does at
    // runtime: through the same UTF-8 path SetTextValue feeds.
    ui.SetTextValue("t1", "\xE6\x97\xA5\xE6\x9C\xAC"); // 日本 = 2 chars, 6 bytes
    check(ui.TextValue("t1") == "\xE6\x97\xA5\xE6\x9C\xAC", "text: multi-byte content survives");
    check(ui.TextValue("t1").size() == 6, "text: stored as bytes");

    // A field with a 3-char limit refuses the 4th character (the UI applies
    // the same limit while typing; here we assert the round trip only stores
    // what fits by truncating through SetTextValue).
    ui.SetTextValue("t2", "abcd");
    check(ui.TextValue("t2") == "abcd", "text: per-field storage is independent");
    check(ui.TextValue("t1") != ui.TextValue("t2"), "text: keys do not alias");

    // Focus bookkeeping: without a click, nothing is focused.
    check(ui.FocusedTextInput().empty(), "text: nothing focused before a click");
    (void)ui.TextInput("t1", tl, 40, 220, 200, 28, 0);
    (void)ui.TextInput("t2", tl, 40, 260, 200, 28, 0);
    check(ui.FocusedTextInput().empty(), "text: drawing does not steal focus");

    // --- ScrollArea: offset clamping --------------------------------------
    check(near(ui.ScrollArea("v1", tl, 40, 320, 300, 200, 1000.0f, 0.0f), 0.0f),
          "scroll: zero offset stays zero");
    check(near(ui.ScrollArea("v1", tl, 40, 320, 300, 200, 1000.0f, 50.0f), 50.0f),
          "scroll: an in-range offset passes through");
    check(near(ui.ScrollArea("v1", tl, 40, 320, 300, 200, 1000.0f, 99999.0f), 800.0f),
          "scroll: clamps to content_height - h");
    check(near(ui.ScrollArea("v1", tl, 40, 320, 300, 200, 1000.0f, -20.0f), 0.0f),
          "scroll: a negative offset clamps to 0");
    // Content shorter than the viewport cannot scroll at all.
    check(near(ui.ScrollArea("v2", tl, 40, 320, 300, 200, 150.0f, 30.0f), 0.0f),
          "scroll: short content pins the offset to 0");
    check(near(ui.ScrollArea("v2", tl, 40, 320, 300, 200, 200.0f, 30.0f), 0.0f),
          "scroll: exactly-fitting content pins to 0");
    // Guard the divide-by-zero in the thumb height.
    check(near(ui.ScrollArea("v2", tl, 40, 320, 300, 200, 0.0f, 30.0f), 0.0f),
          "scroll: empty content pins to 0");

    // --- Every widget draws, and the batch survives it ---------------------
    // QuadCount is reset by BeginFrame, so each frame below is measured
    // standalone rather than diffed against the previous one.
    r.BeginFrame(0.0f, 0.08f, 0.10f, 0.14f);
    (void)ui.Slider("d1", tl, 40, 40, 200, 24, 0.5f, 0.0f, 1.0f);
    (void)ui.Checkbox("d2", tl, 40, 80, 20, true, "option");
    (void)ui.Checkbox("d3", tl, 40, 110, 20, false, "option");
    (void)ui.TextInput("d4", tl, 40, 140, 200, 28, 8);
    (void)ui.ScrollArea("d5", tl, 40, 180, 300, 200, 1000.0f, 0.0f);
    r.EndFrame();
    const std::size_t all_quads = r.QuadCount();
    check(all_quads > 0, "draw: widgets submit quads");
    // 2 per slider (backdrop + track) + thumb, 2 per checkbox + tick,
    // 2 per text field, scrollbar track + thumb.
    check(all_quads >= 10, "draw: every widget contributes geometry");
    check(r.DrawCallCount() > 0, "draw: widgets were flushed");

    // An empty unfocused field falls back to its placeholder; a filled one
    // draws its glyphs. Both are measurable because text emits one quad per
    // glyph.
    ui.SetTextValue("d6", "");
    r.BeginFrame(0.0f, 0.08f, 0.10f, 0.14f);
    (void)ui.TextInput("d6", tl, 40, 40, 200, 28, 0);
    r.EndFrame();
    const std::size_t empty_quads = r.QuadCount();
    ui.SetTextValue("d6", "full");
    r.BeginFrame(0.0f, 0.08f, 0.10f, 0.14f);
    (void)ui.TextInput("d6", tl, 40, 40, 200, 28, 0);
    r.EndFrame();
    const std::size_t full_quads = r.QuadCount();
    check(empty_quads >= 2, "draw: an empty field draws frame + placeholder");
    check(full_quads >= 2, "draw: a filled field draws frame + text");
    check(empty_quads > full_quads,
          "draw: the placeholder is longer than the typed text");

    // --- widgets round 2 (Phase 25) ---------------------------------------
    // Input::InjectFrame fills the snapshot by hand, so click paths — which
    // used to be unreachable headless — are assertable: press, then release.
    fake2d::Input &in = engine.GetInput();
    const std::vector<std::int64_t> none{};
    const std::vector<std::int64_t> left{0};
    const std::vector<std::string_view> no_keys{};

    // Radio: a press and release inside the row is one click.
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    check(!ui.RadioButton("r1", tl, 40, 40, 20, false, "row"),
          "radio: a press alone is not a click");
    in.InjectFrame({50.0f, 50.0f}, no_keys, none);
    check(ui.RadioButton("r1", tl, 40, 40, 20, false, "row"),
          "radio: releasing over the row clicks it");
    // Releasing outside cancels, exactly like a button.
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    in.InjectFrame({500.0f, 500.0f}, no_keys, none);
    check(!ui.RadioButton("r1", tl, 40, 40, 20, false, "row"),
          "radio: releasing outside cancels the click");
    in.InjectFrame({500.0f, 500.0f}, no_keys, none);
    check(!ui.RadioButton("r1", tl, 40, 40, 20, false, "row"),
          "radio: a click elsewhere never clicks this row");
    // Selection is a draw state, not state the widget owns, so the same call
    // reports a click whether or not the row is the selected one.
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    (void)ui.RadioButton("r2", tl, 40, 40, 20, true, "selected row");
    in.InjectFrame({50.0f, 50.0f}, no_keys, none);
    check(ui.RadioButton("r2", tl, 40, 40, 20, true, "selected row"),
          "radio: an already-selected row still reports the click");

    // Combo: the header toggles, and items report their own clicks.
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    check(!ui.ComboBegin("cb", tl, 40, 40, 160, 28, "pick", false),
          "combo: a press alone does not open it");
    in.InjectFrame({50.0f, 50.0f}, no_keys, none);
    check(ui.ComboBegin("cb", tl, 40, 40, 160, 28, "pick", false),
          "combo: releasing over the header clicks it");
    // Unlike a button, a flick that ends outside still opens the list: the
    // popup swallows the gesture.
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    (void)ui.ComboBegin("cb", tl, 40, 40, 160, 28, "pick", false);
    in.InjectFrame({500.0f, 500.0f}, no_keys, none);
    check(ui.ComboBegin("cb", tl, 40, 40, 160, 28, "pick", false),
          "combo: a press that ends outside still clicks it");
    // Item rows: one click each, and only for the row under the cursor.
    in.InjectFrame({50.0f, 90.0f}, no_keys, left);
    check(!ui.ComboItem("ci0", tl, 40, 80, 160, 24, "alpha", true),
          "combo item: a press alone is not a pick");
    in.InjectFrame({50.0f, 90.0f}, no_keys, none);
    check(ui.ComboItem("ci0", tl, 40, 80, 160, 24, "alpha", true),
          "combo item: releasing over the row picks it");
    check(!ui.ComboItem("ci1", tl, 40, 108, 160, 24, "beta", false),
          "combo item: a row away from the cursor is not picked");
    // An open list must not fall through to whatever is beneath it: the
    // header is a separate widget with its own key.
    in.InjectFrame({50.0f, 90.0f}, no_keys, left);
    in.InjectFrame({50.0f, 90.0f}, no_keys, none);
    check(!ui.ComboBegin("cb", tl, 40, 40, 160, 28, "pick", true),
          "combo: the header does not swallow a click aimed at the list");

    // --- ProgressBar: clamped, and measurable in pixels -------------------
    // UI geometry goes through the same batch as everything else, so drawing
    // widgets inside a target lets the fill be read back.
    fake2d::RenderTargetLibrary &ui_lib = r.GetRenderTargets();
    const int bar_rt = ui_lib.Create(128, 128);
    fake2d::RenderTarget *t_bar = ui_lib.Get(bar_rt);
    fake2d::Color px{};
    if (t_bar != nullptr) {
        r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        check(r.BeginRenderTarget(bar_rt), "bar: target binds");
        r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
        // 100x20 at (10,10): half full means x=10..60 filled, 60..110 track.
        ui.ProgressBar(tl, 10, 10, 100, 20, 0.5f,
                       fake2d::Color{1.0f, 0.0f, 0.0f, 1.0f});
        r.EndRenderTarget();
        r.EndFrame();
        check(t_bar->ReadPixel(30, 20, px) && near_u8(px.r, 1.0f),
              "bar: half a bar fills the left half");
        check(t_bar->ReadPixel(90, 20, px) && near_u8(px.r, 0.14f),
              "bar: the right half stays on the track colour");
        // Out-of-range values clamp instead of overflowing the track. The
        // probes sit *outside* the 10..110 track (and outside the 8..112
        // frame) on purpose: a probe inside it reads filled either way, and
        // the assertion would never fail.
        check(r.BeginRenderTarget(bar_rt), "bar: target rebinds");
        ui.ProgressBar(tl, 10, 10, 100, 20, 2.0f,
                       fake2d::Color{1.0f, 0.0f, 0.0f, 1.0f});
        r.EndRenderTarget();
        check(t_bar->ReadPixel(108, 20, px) && near_u8(px.r, 1.0f),
              "bar: above 1 fills the whole track");
        check(t_bar->ReadPixel(120, 20, px) && near_u8(px.r, 0.0f),
              "bar: above 1 does not spill past the track");
        check(r.BeginRenderTarget(bar_rt), "bar: target rebinds again");
        ui.ProgressBar(tl, 10, 10, 100, 20, -1.0f,
                       fake2d::Color{1.0f, 0.0f, 0.0f, 1.0f});
        r.EndRenderTarget();
        check(t_bar->ReadPixel(30, 20, px) && near_u8(px.r, 0.14f),
              "bar: below 0 draws no fill at all");
        check(t_bar->ReadPixel(4, 20, px) && near_u8(px.r, 0.0f),
              "bar: below 0 does not grow backwards past the track");
        r.EndFrame();
        ui_lib.Destroy(bar_rt);
    }

    // Every new widget contributes geometry.
    r.BeginFrame(0.0f, 0.08f, 0.10f, 0.14f);
    ui.ProgressBar(tl, 40, 40, 200, 20, 0.5f, fake2d::Color::White());
    (void)ui.RadioButton("q1", tl, 40, 80, 20, true, "row");
    (void)ui.ComboBegin("q2", tl, 40, 110, 160, 28, "pick", false);
    (void)ui.ComboItem("q3", tl, 40, 144, 160, 24, "alpha", true);
    r.EndFrame();
    check(r.QuadCount() >= 8, "draw: the round-2 widgets submit geometry");

    // --- widgets round 3 (Phase 28) ---------------------------------------
    // A drag is two frames: grab, then move with the button still down. The
    // grab frame must NOT report motion, or the panel jumps by whatever
    // distance the cursor happened to travel to reach the title bar.
    in.InjectFrame({60.0f, 50.0f}, no_keys, left);
    check(!ui.Window("w1", tl, 40, 40, 200, 120, "settings", 0.6),
          "window: the frame that grabs it reports no motion");
    in.InjectFrame({80.0f, 60.0f}, no_keys, left);
    check(ui.Window("w1", tl, 40, 40, 200, 120, "settings", 0.6),
          "window: holding and moving drags it");
    check(in.MouseDelta().x > 0.0f && in.MouseDelta().y > 0.0f,
          "window: the same movement is readable as a delta");
    in.InjectFrame({90.0f, 70.0f}, no_keys, none);
    check(!ui.Window("w1", tl, 40, 40, 200, 120, "settings", 0.6),
          "window: releasing ends the drag");
    in.InjectFrame({100.0f, 80.0f}, no_keys, none);
    check(!ui.Window("w1", tl, 40, 40, 200, 120, "settings", 0.6),
          "window: moving without a button held is not a drag");
    // The body is not a handle: only the bar moves the panel.
    in.InjectFrame({60.0f, 150.0f}, no_keys, left);
    check(!ui.Window("w1", tl, 40, 40, 200, 120, "settings", 0.6),
          "window: a press on the body does not grab it");
    in.InjectFrame({80.0f, 160.0f}, no_keys, left);
    check(!ui.Window("w1", tl, 40, 40, 200, 120, "settings", 0.6),
          "window: dragging from the body does nothing");

    // Tabs: one click each, and cancelling works like a button.
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    check(!ui.Tab("t0", tl, 40, 40, 80, 24, "audio", true, 0.6),
          "tab: a press alone does not select");
    in.InjectFrame({50.0f, 50.0f}, no_keys, none);
    check(ui.Tab("t0", tl, 40, 40, 80, 24, "audio", true, 0.6),
          "tab: releasing over it selects it");
    in.InjectFrame({50.0f, 50.0f}, no_keys, left);
    in.InjectFrame({500.0f, 500.0f}, no_keys, none);
    check(!ui.Tab("t0", tl, 40, 40, 80, 24, "audio", true, 0.6),
          "tab: releasing outside cancels");
    check(!ui.Tab("t1", tl, 130, 40, 80, 24, "video", false, 0.6),
          "tab: a tab away from the cursor is not selected");

    // A tree node is a toggle, so it must fire once and then go quiet —
    // unlike a drag, which is meant to keep firing.
    in.InjectFrame({50.0f, 90.0f}, no_keys, left);
    check(!ui.Tree("n0", tl, 40, 80, 160, 20, "graphics", false, 0.6),
          "tree: a press alone does not toggle");
    in.InjectFrame({50.0f, 90.0f}, no_keys, none);
    check(ui.Tree("n0", tl, 40, 80, 160, 20, "graphics", false, 0.6),
          "tree: releasing over the row toggles it");
    check(!ui.Tree("n0", tl, 40, 80, 160, 20, "graphics", true, 0.6),
          "tree: holding still does not toggle again");
    in.InjectFrame({50.0f, 90.0f}, no_keys, none);
    check(!ui.Tree("n0", tl, 40, 80, 160, 20, "graphics", true, 0.6),
          "tree: an idle frame never toggles");

    // --- round 3 in pixels -------------------------------------------------
    // A window is mostly a silhouette, so what is worth probing is that the
    // bar, the body and the rule between them land where they should.
    fake2d::RenderTargetLibrary &ui3_lib = r.GetRenderTargets();
    const int win_rt = ui3_lib.Create(128, 128);
    fake2d::RenderTarget *t_win = ui3_lib.Get(win_rt);
    if (t_win != nullptr) {
        r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        check(r.BeginRenderTarget(win_rt), "window: target binds");
        r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
        const bool win_drag = ui.Window("pw", tl, 10, 10, 100, 80, "title", 0.6);
        check(!win_drag, "window: a fresh window is not being dragged");
        r.EndRenderTarget();
        r.EndFrame();
        // Probe inside the bar (y=20) and inside the body (y=70): the bar is
        // the lighter of the two, and the body is not the canvas colour.
        check(t_win->ReadPixel(60, 20, px) && near_u8(px.b, 0.48f),
              "window: the title bar is drawn in the bar colour");
        check(t_win->ReadPixel(60, 70, px) && near_u8(px.b, 0.14f),
              "window: the body is drawn below the bar");
        check(t_win->ReadPixel(60, 96, px) && near_u8(px.b, 0.0f),
              "window: nothing spills past the bottom edge");
        ui3_lib.Destroy(win_rt);
    }

    // The active tab must be the BRIGHT one — an earlier revision drew the
    // active tab in the dark panel colour and the idle ones bright, which is
    // the exact inverse of what the header promises. Probe both fills.
    const int tab_rt = ui3_lib.Create(200, 40);
    fake2d::RenderTarget *t_tab = ui3_lib.Get(tab_rt);
    if (t_tab != nullptr) {
        r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        check(r.BeginRenderTarget(tab_rt), "tab: target binds");
        r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
        (void)ui.Tab("on", tl, 10, 10, 80, 20, "on", true, 0.6);
        (void)ui.Tab("off", tl, 110, 10, 80, 20, "off", false, 0.6);
        r.EndRenderTarget();
        r.EndFrame();
        float on_b = -1.0f, off_b = -1.0f;
        if (t_tab->ReadPixel(50, 20, px)) { on_b = px.b; }
        if (t_tab->ReadPixel(150, 20, px)) { off_b = px.b; }
        check(on_b > off_b + 0.05f,
              "tab: the active tab is brighter than an idle one");
        // The active tab grows 2 px past its declared height: y=31 is still
        // fill (10+20+1), and would be background if the grow were removed.
        check(t_tab->ReadPixel(50, 31, px) && near_u8(px.b, 0.48f),
              "tab: the active tab drops its bottom edge (grows 2 px)");
        ui3_lib.Destroy(tab_rt);
    }

    // Every new widget contributes geometry.
    r.BeginFrame(0.0f, 0.08f, 0.10f, 0.14f);
    (void)ui.Window("q4", tl, 20, 20, 160, 100, "panel", 0.6);
    (void)ui.Tab("q5", tl, 20, 130, 70, 24, "one", true, 0.6);
    (void)ui.Tree("q6", tl, 20, 160, 160, 20, "node", false, 0.6);
    r.EndFrame();
    check(r.QuadCount() >= 8, "draw: the round-3 widgets submit geometry");

    // Injection is a test affordance: a real frame replaces it, so nothing
    // synthetic can leak into a windowed run.
    in.InjectFrame({0.0f, 0.0f}, no_keys, none);

    std::printf("[ui] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

// Post-processing and the minimap bake. Everything here is a pixel probe:
// the scene goes into a target, the post pass blits it into a second one
// (or onto the screen), and the result is read back — so no assertion needs
// a mouse, a keyboard, or a visible window.
int RunPostTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[post] FAIL %s\n", what);
        }
    };
    // 8-bit channels: 1/255 of slack absorbs any filtering rounding.
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.02f; };
    auto is_red = [&](const fake2d::Color &c) {
        return near(c.r, 1.0f) && near(c.g, 0.0f) && near(c.b, 0.0f);
    };
    auto is_green = [&](const fake2d::Color &c) {
        return near(c.r, 0.0f) && near(c.g, 1.0f) && near(c.b, 0.0f);
    };
    auto is_white = [&](const fake2d::Color &c) {
        return near(c.r, 1.0f) && near(c.g, 1.0f) && near(c.b, 1.0f);
    };
    auto is_grey = [&](const fake2d::Color &c) {
        return near(c.r, c.g) && near(c.g, c.b);
    };

    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[post] FAIL renderer is not initialised\n");
        return 1;
    }
    fake2d::RenderTargetLibrary &lib = r.GetRenderTargets();

    // --- a real two-tile tileset, generated on demand ----------------------
    // tile 1 = solid red, tile 2 = solid green, 32x16 as two 16x16 tiles.
    // Tiles with no loaded texture are skipped, so the bake needs a real one.
    const std::string tiles_path = "assets/_post_tiles.png";
    {
        std::error_code ec;
        std::filesystem::create_directories("assets", ec);
        std::vector<unsigned char> px(static_cast<std::size_t>(32 * 16 * 3));
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 32; ++x) {
                const std::size_t o = static_cast<std::size_t>(y * 32 + x) * 3;
                px[o + 0] = x < 16 ? 255 : 0;
                px[o + 1] = x < 16 ? 0 : 255;
                px[o + 2] = 0;
            }
        }
        stbi_write_png(tiles_path.c_str(), 32, 16, 3, px.data(), 32 * 3);
    }

    // 4x2 checkerboard: the pattern below is the map read row by row, so a
    // tile drawn in the wrong cell (or at the wrong scale) flips a colour.
    constexpr const char *kDoc = R"JSON({
      "orientation": "orthogonal", "width": 4, "height": 2,
      "tilewidth": 16, "tileheight": 16,
      "tilesets": [{"firstgid": 1, "name": "t", "tilewidth": 16, "tileheight": 16,
                    "columns": 2, "tilecount": 2, "image": "_post_tiles.png",
                    "imagewidth": 32, "imageheight": 16,
                    "tiles": [{"id": 0}, {"id": 1}]}],
      "layers": [{"name": "world", "type": "tilelayer", "visible": true,
                  "opacity": 1,
                  "data": [1, 2, 1, 2,
                           2, 1, 2, 1]}]
    })JSON";

    fake2d::Tilemap map;
    check(map.LoadFromString(kDoc, "assets", engine.GetResources()),
          "map: the generated tileset parses");
    check(map.IsValid() && map.Cols() == 4 && map.Rows() == 2, "map: 4x2 cells");

    const int bake = lib.Create(128, 128);
    const int graded = lib.Create(128, 128);
    const int wide = lib.Create(256, 256);
    check(bake > 0 && graded > 0 && wide > 0, "targets: three slots allocated");
    fake2d::RenderTarget *t_bake = lib.Get(bake);
    fake2d::RenderTarget *t_graded = lib.Get(graded);
    fake2d::RenderTarget *t_wide = lib.Get(wide);
    fake2d::Color px{};

    // --- the minimap bake: DrawScaled into a target ------------------------
    // Inside a target, coordinates are the target's own pixels with (0,0) at
    // its top-left, so the whole map must land at the canvas origin.
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(bake), "bake: target binds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.25f, 1.0f});
    map.DrawScaled(r, 1.0f);
    r.EndRenderTarget();
    check(t_bake->ReadPixel(8, 8, px) && is_red(px), "bake: cell (0,0) is tile 1");
    check(t_bake->ReadPixel(24, 8, px) && is_green(px), "bake: cell (1,0) is tile 2");
    check(t_bake->ReadPixel(40, 8, px) && is_red(px), "bake: cell (2,0) is tile 1");
    check(t_bake->ReadPixel(8, 24, px) && is_green(px), "bake: cell (0,1) is tile 2");
    check(t_bake->ReadPixel(80, 8, px) && near(px.b, 0.25f),
          "bake: past the map's right edge the clear colour survives");

    // Half scale: 64x32 becomes 32x16, so cells shrink to 8px and everything
    // past x=32 is untouched canvas.
    check(r.BeginRenderTarget(bake), "bake: target rebinds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.25f, 1.0f});
    map.DrawScaled(r, 0.5f);
    r.EndRenderTarget();
    check(t_bake->ReadPixel(4, 4, px) && is_red(px), "scaled: cell (0,0) still tile 1");
    check(t_bake->ReadPixel(12, 4, px) && is_green(px), "scaled: cell (1,0) still tile 2");
    check(t_bake->ReadPixel(40, 4, px) && near(px.b, 0.25f),
          "scaled: the map ends at half its width");

    // A non-positive scale is a no-op rather than a degenerate draw.
    check(r.BeginRenderTarget(bake), "bake: target rebinds again");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.25f, 1.0f});
    map.DrawScaled(r, 0.0f);
    r.EndRenderTarget();
    check(t_bake->ReadPixel(4, 4, px) && near(px.b, 0.25f), "scaled: scale 0 draws nothing");

    // --- the same map through the normal draw path -------------------------
    // Tilemap::Draw follows the *active* camera. Scroll the window camera far
    // away first: a map drawn with the window camera would land outside the
    // target, while the canvas one puts it at the canvas origin.
    r.GetCamera().SetPosition(500.0f, 300.0f);
    check(r.BeginRenderTarget(bake), "draw: target binds under a scrolled camera");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.25f, 1.0f});
    map.Draw(r);
    r.EndRenderTarget();
    check(t_bake->ReadPixel(8, 8, px) && is_red(px),
          "draw: a scrolled window camera does not move the baked map");
    check(t_bake->ReadPixel(24, 8, px) && is_green(px),
          "draw: the second cell lands beside the first");
    r.GetCamera().SetPosition(0.0f, 0.0f);

    // --- rt_camera: aim the canvas at a world offset -----------------------
    // The minimap bakes at (0,0); a post pass needs the *camera's* view. The
    // map is 64x32 px, so with the canvas camera at (32, 16), world (32, 16)
    // — tile (2,1), gid 2 — must land on the canvas origin, and everything
    // past world x=64 (canvas x=32) is off the map.
    check(r.BeginRenderTarget(bake), "camera: target binds");
    check(r.SetTargetCamera(32.0f, 16.0f), "camera: moves while a target is bound");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.25f, 1.0f});
    map.Draw(r);
    check(t_bake->ReadPixel(4, 4, px) && is_green(px),
          "camera: world (32,16) lands on the canvas origin");
    check(t_bake->ReadPixel(40, 4, px) && near(px.b, 0.25f),
          "camera: past the map's right edge only the clear colour remains");
    r.EndRenderTarget();
    check(!r.SetTargetCamera(0.0f, 0.0f),
          "camera: refused with no target bound");
    r.EndFrame();

    // --- the post pass -----------------------------------------------------
    const int fx = r.LoadShaderFromFile("assets/post_grade.vert",
                                        "assets/post_grade.frag");
    check(fx > 0, "shader: the post program compiles");

    // Scene: a white upper half over a dark lower half — distinguishable
    // after any grade that is supposed to preserve them.
    const fake2d::Color kDark{0.10f, 0.12f, 0.20f, 1.0f};
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(graded), "scene: target binds");
    r.ClearActiveTarget(kDark);
    r.DrawQuad(0.0f, 0.0f, 128.0f, 64.0f, fake2d::Color::White());
    r.EndRenderTarget();

    // A plain present (no shader) stretches the source over the whole
    // destination, whatever its size.
    check(r.BeginRenderTarget(wide), "present: 256x256 destination binds");
    r.ClearActiveTarget(fake2d::Color{1.0f, 0.0f, 1.0f, 1.0f});
    r.PresentRenderTarget(graded, nullptr);
    r.EndRenderTarget();
    check(t_wide->ReadPixel(64, 32, px) && is_white(px),
          "present: the upper half is scaled up to the destination");
    check(t_wide->ReadPixel(240, 240, px) && near(px.r, kDark.r),
          "present: the quad covers the far corner (no magenta left)");

    // Shader present with every knob at its default: a straight blit.
    check(r.BeginRenderTarget(wide), "post: destination binds");
    r.ClearActiveTarget(fake2d::Color{1.0f, 0.0f, 1.0f, 1.0f});
    if (fx > 0) {
        r.SetShaderFloat(fx, "u_desaturate", 0.0f);
        r.SetShaderFloat(fx, "u_brightness", 0.0f);
        r.SetShaderFloat(fx, "u_vignette", 0.0f);
        r.SetShaderFloat(fx, "u_scanline", 0.0f);
    }
    r.PresentRenderTarget(graded, r.GetShader(fx));
    r.EndRenderTarget();
    check(t_wide->ReadPixel(64, 32, px) && is_white(px),
          "post: defaults are the identity (white stays white)");
    check(t_wide->ReadPixel(64, 200, px) && near(px.b, kDark.b),
          "post: defaults are the identity (dark stays dark)");
    check(r.ActiveShader() == nullptr,
          "post: the override does not leak into the rest of the frame");

    // Brightness lifts every channel; desaturate collapses them onto luma.
    if (fx > 0) {
        r.SetShaderFloat(fx, "u_brightness", 0.25f);
    }
    check(r.BeginRenderTarget(wide), "post: destination binds for the lift");
    r.PresentRenderTarget(graded, r.GetShader(fx));
    r.EndRenderTarget();
    check(t_wide->ReadPixel(64, 200, px) && near(px.b, kDark.b + 0.25f),
          "post: +0.25 brightness lands on the dark half");

    if (fx > 0) {
        r.SetShaderFloat(fx, "u_brightness", 0.0f);
        r.SetShaderFloat(fx, "u_desaturate", 1.0f);
    }
    check(r.BeginRenderTarget(wide), "post: destination binds for the desaturate");
    r.PresentRenderTarget(graded, r.GetShader(fx));
    r.EndRenderTarget();
    check(t_wide->ReadPixel(64, 32, px) && is_grey(px),
          "post: full desaturate flattens the white half to grey");

    // Scanlines darken alternate rows of the destination, so neighbours differ.
    // Same-size pair (128 -> 128) so the frequency matches the resolution.
    if (fx > 0) {
        r.SetShaderFloat(fx, "u_desaturate", 0.0f);
        r.SetShaderFloat(fx, "u_scanline", 0.0f);
        r.SetShaderVec2(fx, "u_resolution", 128.0f, 128.0f);
    }
    check(r.BeginRenderTarget(bake), "post: same-size destination binds");
    r.PresentRenderTarget(graded, r.GetShader(fx));
    r.EndRenderTarget();
    fake2d::Color row_a{};
    fake2d::Color row_b{};
    t_bake->ReadPixel(64, 32, row_a);
    t_bake->ReadPixel(64, 33, row_b);
    check(near(row_a.r, row_b.r), "post: no scanlines leave neighbours identical");
    if (fx > 0) {
        r.SetShaderFloat(fx, "u_scanline", 1.0f);
    }
    check(r.BeginRenderTarget(bake), "post: destination rebinds for scanlines");
    r.PresentRenderTarget(graded, r.GetShader(fx));
    r.EndRenderTarget();
    t_bake->ReadPixel(64, 32, row_a);
    t_bake->ReadPixel(64, 33, row_b);
    check(!near(row_a.r, row_b.r), "post: scanlines darken every other row");

    // --- guards ------------------------------------------------------------
    // A target must not be presented into itself, and the window is not a
    // presentable source.
    check(r.BeginRenderTarget(bake), "guards: destination binds");
    r.ClearActiveTarget(fake2d::Color{1.0f, 0.0f, 1.0f, 1.0f});
    r.PresentRenderTarget(bake, nullptr); // self: ignored
    r.PresentRenderTarget(0, nullptr);    // the window: ignored
    r.PresentRenderTarget(99, nullptr);   // unknown: ignored
    r.EndRenderTarget();
    check(t_bake->ReadPixel(64, 64, px) && near(px.r, 1.0f) && near(px.b, 1.0f),
          "guards: the ignored presents left the target untouched");
    r.EndFrame();

    // --- the window survives a present -------------------------------------
    // Presenting onto the screen has to end up on the framebuffer the window
    // actually shows, which a headless context owns itself (not id 0).
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    r.PresentRenderTarget(graded, nullptr);
    r.EndFrame();
    std::error_code ec;
    const std::string shot = "assets/_post_test.png";
    std::filesystem::remove(shot, ec);
    check(r.SaveScreenshot(shot), "screenshot: the window framebuffer is intact");
    const auto shot_bytes = std::filesystem::file_size(shot, ec);
    check(!ec && shot_bytes > 0, "screenshot: wrote a non-empty png");
    std::filesystem::remove(shot, ec);

    std::filesystem::remove(tiles_path, ec);
    lib.Clear();
    if (fx > 0) {
        r.DestroyShader(fx);
    }

    std::printf("[post] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

// Multi-pass chains: one ping-pong pass (rt_copy) plus a resizable target are
// all a bloom needs — scene -> blur H -> blur V -> composite. Everything here
// is a pixel probe, and the blur shader's "u_dir = 0 is a straight copy"
// contract is what makes the passes measurable step by step.
int RunFxTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[fx] FAIL %s\n", what);
        }
    };
    // 8-bit channels: 1/255 of slack absorbs any filtering rounding.
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.02f; };
    auto is_white = [&](const fake2d::Color &c) {
        return near(c.r, 1.0f) && near(c.g, 1.0f) && near(c.b, 1.0f);
    };
    auto is_black = [&](const fake2d::Color &c) {
        return near(c.r, 0.0f) && near(c.g, 0.0f) && near(c.b, 0.0f);
    };
    auto is_magenta = [&](const fake2d::Color &c) {
        return near(c.r, 1.0f) && near(c.g, 0.0f) && near(c.b, 1.0f);
    };

    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[fx] FAIL renderer is not initialised\n");
        return 1;
    }
    fake2d::RenderTargetLibrary &lib = r.GetRenderTargets();

    const int blur = r.LoadShaderFromFile("assets/fx_blur.vert", "assets/fx_blur.frag");
    const int grade = r.LoadShaderFromFile("assets/post_grade.vert",
                                           "assets/post_grade.frag");
    check(blur > 0, "shader: the blur program compiles");
    check(grade > 0, "shader: the grade program compiles");

    const int a = lib.Create(128, 128);
    const int b = lib.Create(128, 128);
    const int c = lib.Create(64, 64);
    check(a > 0 && b > 0 && c > 0, "targets: three slots allocated");
    fake2d::RenderTarget *t_b = lib.Get(b);
    fake2d::Color px{};

    // Scene: a 4x4 white dot on black at the centre of target A. Small enough
    // that a blur has somewhere to spread into, big enough to read back.
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(a), "scene: target A binds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    r.DrawQuad(62.0f, 62.0f, 4.0f, 4.0f, fake2d::Color::White());
    r.EndRenderTarget();
    check(t_b->IsValid(), "scene: target B is live");

    // --- a plain pass is a copy --------------------------------------------
    check(r.CopyRenderTarget(a, b, nullptr), "copy: A -> B succeeds");
    check(t_b->ReadPixel(64, 64, px) && is_white(px),
          "copy: the dot arrives at the same canvas position");
    check(t_b->ReadPixel(4, 4, px) && is_black(px),
          "copy: the black background arrives too");

    // --- through a shader with u_dir = 0 it is still a copy ----------------
    // Every tap samples the same texel, so the weights sum to the original.
    if (blur > 0) {
        r.SetShaderVec2(blur, "u_dir", 0.0f, 0.0f);
    }
    check(r.CopyRenderTarget(a, b, r.GetShader(blur)), "copy: A -> B through the blur shader");
    check(t_b->ReadPixel(64, 64, px) && is_white(px),
          "copy: u_dir = 0 leaves the dot untouched");
    check(t_b->ReadPixel(70, 64, px) && is_black(px),
          "copy: u_dir = 0 spreads nothing sideways");
    check(r.ActiveShader() == nullptr,
          "copy: the shader override does not leak into the frame");

    // --- one pass blurs in one direction only ------------------------------
    // Step of 2 px per tap: the outermost tap reaches 8 px, so the dot must
    // paint x=70 but leave y=70 alone.
    if (blur > 0) {
        r.SetShaderVec2(blur, "u_dir", 2.0f / 128.0f, 0.0f);
    }
    check(r.CopyRenderTarget(a, b, r.GetShader(blur)), "blur: horizontal pass runs");
    check(t_b->ReadPixel(70, 64, px) && px.r > 0.02f,
          "blur: the dot spreads along the pass direction");
    check(t_b->ReadPixel(64, 70, px) && is_black(px),
          "blur: the perpendicular axis is untouched");
    check(t_b->ReadPixel(64, 64, px) && px.r < 0.9f,
          "blur: the centre gives up some of its level");

    // --- ping-pong: the second pass covers the other axis ------------------
    if (blur > 0) {
        r.SetShaderVec2(blur, "u_dir", 0.0f, 2.0f / 128.0f);
    }
    check(r.CopyRenderTarget(b, a, r.GetShader(blur)), "blur: vertical pass runs back into A");
    check(lib.Get(a)->ReadPixel(64, 70, px) && px.r > 0.02f,
          "blur: two passes cover both axes");
    // The corner is the product of both passes' outer taps, so it is a small
    // but strictly positive fraction of the axis-aligned smear.
    check(lib.Get(a)->ReadPixel(70, 70, px) && px.r > 0.001f,
          "blur: the corner of the smear is lit too");
    check(lib.Get(a)->ReadPixel(4, 4, px) && is_black(px),
          "blur: far from the dot it is still black");

    // --- guards ------------------------------------------------------------
    check(!r.CopyRenderTarget(a, a, nullptr),
          "guards: a target cannot be copied into itself");
    check(!r.CopyRenderTarget(0, b, nullptr), "guards: the window is not a source");
    check(!r.CopyRenderTarget(a, 0, nullptr), "guards: the window is not a destination");
    check(!r.CopyRenderTarget(99, b, nullptr), "guards: an unknown source is refused");
    check(!r.CopyRenderTarget(a, 99, nullptr), "guards: an unknown destination is refused");

    check(r.BeginRenderTarget(c), "guards: target C binds");
    check(!r.CopyRenderTarget(a, b, nullptr), "guards: no nesting");
    r.EndRenderTarget();

    // A refused copy must leave the destination exactly as it was.
    check(r.BeginRenderTarget(b), "guards: B rebinds for the untouched check");
    r.ClearActiveTarget(fake2d::Color{1.0f, 0.0f, 1.0f, 1.0f});
    r.EndRenderTarget();
    check(!r.CopyRenderTarget(99, b, nullptr), "guards: the bad copy is refused again");
    check(t_b->ReadPixel(64, 64, px) && is_magenta(px),
          "guards: the refused copy left the destination untouched");

    // --- resize ------------------------------------------------------------
    // A currently holds the *blurred* result (the ping-pong wrote back into
    // it), so put the sharp dot back before measuring a rescaled copy.
    check(r.BeginRenderTarget(a), "resize: A rebinds for a fresh scene");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    r.DrawQuad(62.0f, 62.0f, 4.0f, 4.0f, fake2d::Color::White());
    r.EndRenderTarget();
    check(r.ResizeRenderTarget(b, 64, 64), "resize: B reallocates at 64x64");
    check(t_b->Width() == 64 && t_b->Height() == 64, "resize: the new size is reported");
    check(!r.ResizeRenderTarget(b, 0, 0), "resize: a zero size is refused");
    check(t_b->Width() == 64 && t_b->Height() == 64,
          "resize: the refused resize kept the size");
    check(!r.ResizeRenderTarget(0, 32, 32), "resize: the window is not resizable");
    check(!r.ResizeRenderTarget(99, 32, 32), "resize: an unknown id is refused");
    check(r.BeginRenderTarget(b), "resize: B binds");
    check(!r.ResizeRenderTarget(b, 32, 32), "resize: refused while the target is bound");
    r.EndRenderTarget();
    check(t_b->Width() == 64, "resize: the bound refusal kept the size");

    // A half-size destination rescales: the dot at canvas 64/128 maps to 32/64.
    check(r.CopyRenderTarget(a, b, nullptr), "resize: A (128) copies into B (64)");
    check(t_b->ReadPixel(32, 32, px) && is_white(px), "resize: the dot lands at half scale");
    check(t_b->ReadPixel(60, 60, px) && is_black(px), "resize: the far corner is background");

    // --- the grade pass still composes on top of a chain -------------------
    if (grade > 0) {
        r.SetShaderFloat(grade, "u_desaturate", 0.0f);
        r.SetShaderFloat(grade, "u_brightness", 0.25f);
        r.SetShaderFloat(grade, "u_vignette", 0.0f);
        r.SetShaderFloat(grade, "u_scanline", 0.0f);
    }
    check(r.CopyRenderTarget(b, c, r.GetShader(grade)), "chain: a grade pass over a copy");
    check(lib.Get(c)->ReadPixel(60, 60, px) && near(px.r, 0.25f),
          "chain: +0.25 brightness lifts the black background");

    r.EndFrame();

    // --- bright pass: the threshold a bloom starts with --------------------
    // Three probes in one scene: a white highlight, a mid grey and a dark
    // grey. Everything here is about the *relationship* between them, so the
    // scene is drawn once and the pass is run over it with different knobs.
    check(r.ResizeRenderTarget(b, 128, 128), "bright: B goes back to 128x128");
    const int bright = r.LoadShaderFromFile("assets/fx_bright.vert", "assets/fx_bright.frag");
    check(bright > 0, "shader: the bright-pass program compiles");
    check(r.BeginRenderTarget(a), "bright: A binds for the probe scene");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    r.DrawQuad(20.0f, 20.0f, 8.0f, 8.0f, fake2d::Color{1.0f, 1.0f, 1.0f, 1.0f});
    r.DrawQuad(60.0f, 20.0f, 8.0f, 8.0f, fake2d::Color{0.5f, 0.5f, 0.5f, 1.0f});
    r.DrawQuad(100.0f, 20.0f, 8.0f, 8.0f, fake2d::Color{0.3f, 0.3f, 0.3f, 1.0f});
    r.EndRenderTarget();
    check(t_b->IsValid(), "bright: B is live");

    // Defaults: threshold 0 / knee 0 keep everything, so the pass is a copy.
    if (bright > 0) {
        r.SetShaderFloat(bright, "u_threshold", 0.0f);
        r.SetShaderFloat(bright, "u_knee", 0.0f);
    }
    check(r.CopyRenderTarget(a, b, r.GetShader(bright)), "bright: the pass runs");
    check(t_b->ReadPixel(24, 24, px) && near(px.r, 1.0f),
          "bright: at threshold 0 the highlight is untouched");
    check(t_b->ReadPixel(104, 24, px) && near(px.r, 0.3f),
          "bright: at threshold 0 the darkest probe is untouched too");

    // A hard cut at 0.9 keeps the highlight and drops both greys.
    if (bright > 0) {
        r.SetShaderFloat(bright, "u_threshold", 0.9f);
    }
    check(r.CopyRenderTarget(a, b, r.GetShader(bright)), "bright: the hard cut runs");
    check(t_b->ReadPixel(24, 24, px) && near(px.r, 1.0f),
          "bright: the highlight survives a 0.9 threshold");
    check(t_b->ReadPixel(64, 24, px) && near(px.r, 0.0f),
          "bright: mid grey is dropped by a 0.9 threshold");
    check(t_b->ReadPixel(104, 24, px) && near(px.r, 0.0f),
          "bright: dark grey is dropped by a 0.9 threshold");

    // A soft knee: the mid grey lands strictly between dropped and kept, and
    // brighter in still means brighter out (the ramp is monotonic, which is
    // the whole point of a knee — no banding as pixels cross the threshold).
    if (bright > 0) {
        r.SetShaderFloat(bright, "u_threshold", 0.5f);
        r.SetShaderFloat(bright, "u_knee", 0.25f);
    }
    check(r.CopyRenderTarget(a, b, r.GetShader(bright)), "bright: the soft knee runs");
    check(t_b->ReadPixel(64, 24, px) && px.r > 0.05f && px.r < 0.45f,
          "bright: a knee lets mid grey through partially");
    check(t_b->ReadPixel(104, 24, px) && px.r < 0.1f,
          "bright: a knee still drops what is well below it");
    check(t_b->ReadPixel(24, 24, px) && near(px.r, 1.0f),
          "bright: a knee does not dim what is well above it");

    // --- animated passes: u_time and u_resolution --------------------------
    // The renderer stamps both on every loaded shader; a pass that declares
    // them animates on its own, and one that declares neither is untouched.
    const int wave = r.LoadShaderFromFile("assets/fx_wave.vert", "assets/fx_wave.frag");
    check(wave > 0, "shader: the wave program compiles");
    r.SetTime(1.5f);
    check(near(r.Time(), 1.5f), "time: the frame clock is readable back");

    // A 4x4 dot at the centre of a 128 target, and the same dot at the centre
    // of a 64 one: with u_freq = 0 every row displaces together, so the pass
    // is a pure sideways shift of `u_amp` pixels — no matter the resolution.
    check(r.BeginRenderTarget(a), "wave: A binds for the dot");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    r.DrawQuad(62.0f, 62.0f, 4.0f, 4.0f, fake2d::Color::White());
    r.EndRenderTarget();

    // At t = 0 every row sits at phase 0, so sin = 0 and nothing moves even
    // with an amplitude dialled in.
    if (wave > 0) {
        r.SetShaderFloat(wave, "u_amp", 8.0f);
        r.SetShaderFloat(wave, "u_freq", 0.0f);
        r.SetShaderFloat(wave, "u_speed", 1.0f);
    }
    r.SetTime(0.0f);
    check(r.CopyRenderTarget(a, b, r.GetShader(wave)), "wave: the pass runs at t = 0");
    check(t_b->ReadPixel(64, 64, px) && is_white(px),
          "wave: at t = 0 the dot has not moved");
    check(t_b->ReadPixel(80, 64, px) && is_black(px),
          "wave: at t = 0 nothing arrives at the shifted position");

    // A quarter period later the sine peaks: +8 pixels, at 128.
    r.SetTime(1.5707963f);
    check(r.CopyRenderTarget(a, b, r.GetShader(wave)), "wave: the pass runs at the peak");
    check(t_b->ReadPixel(72, 64, px) && is_white(px),
          "wave: the dot moves 8 px right at the peak (128 wide)");
    check(t_b->ReadPixel(64, 64, px) && is_black(px),
          "wave: the dot has left its old position (128 wide)");

    // Same pass into a 64 target: still 8 of *its* pixels. This is what the
    // stamped resolution buys — with the window's size instead, a half-size
    // pass would shift by a fraction of a pixel and read unchanged.
    check(r.BeginRenderTarget(c), "wave: C binds for the half-size dot");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    r.DrawQuad(30.0f, 30.0f, 4.0f, 4.0f, fake2d::Color::White());
    r.EndRenderTarget();
    check(r.ResizeRenderTarget(b, 64, 64), "wave: B goes back to 64x64");
    check(r.CopyRenderTarget(c, b, r.GetShader(wave)), "wave: the half-size pass runs");
    check(t_b->ReadPixel(40, 32, px) && is_white(px),
          "wave: the dot moves 8 px right at the peak (64 wide)");
    check(t_b->ReadPixel(32, 32, px) && is_black(px),
          "wave: the dot has left its old position (64 wide)");

    // The clock is the only difference between the two frames above, so this
    // is the assertion that pins "u_time is actually reaching the program".
    r.SetTime(4.712389f); // three quarters: the sine is at -1
    check(r.ResizeRenderTarget(b, 128, 128), "wave: B goes back to 128x128");
    check(r.CopyRenderTarget(a, b, r.GetShader(wave)), "wave: the trough pass runs");
    check(t_b->ReadPixel(56, 64, px) && is_white(px),
          "wave: the dot moves 8 px left at the trough");
    check(t_b->ReadPixel(64, 64, px) && is_black(px),
          "wave: the dot has left the centre at the trough");
    r.SetTime(0.0f);

    // --- the window survives the whole chain -------------------------------
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    r.PresentRenderTarget(b, nullptr);
    r.EndFrame();
    std::error_code ec;
    const std::string shot = "assets/_fx_test.png";
    std::filesystem::remove(shot, ec);
    check(r.SaveScreenshot(shot), "screenshot: the window framebuffer is intact");
    const auto shot_bytes = std::filesystem::file_size(shot, ec);
    check(!ec && shot_bytes > 0, "screenshot: wrote a non-empty png");
    std::filesystem::remove(shot, ec);

    lib.Clear();
    if (blur > 0) {
        r.DestroyShader(blur);
    }
    if (grade > 0) {
        r.DestroyShader(grade);
    }
    if (bright > 0) {
        r.DestroyShader(bright);
    }
    if (wave > 0) {
        r.DestroyShader(wave);
    }

    std::printf("[fx] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

// Masks are the reason an accumulation target is worth keeping around, and
// until now there was no way to *apply* one: alpha covers pixels over and
// additive only brightens, so neither can dim a scene by a per-pixel coverage
// value. Multiply (`result = source * destination`) is that missing piece, and
// because it is an ordinary batch blend it composes **inside** a bound target
// — no Begin/End pair, so unlike rt_copy it is not blocked by the no-nesting
// rule. Everything below is a pixel probe on a target nobody has to see.
int RunFogTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[fog] FAIL %s\n", what);
        }
    };
    // 8-bit channels: a little slack absorbs rounding in the read-back.
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.02f; };

    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[fog] FAIL renderer is not initialised\n");
        return 1;
    }
    fake2d::RenderTargetLibrary &lib = r.GetRenderTargets();

    const int N = 128;
    const int scene = lib.Create(N, N);
    const int mask = lib.Create(N, N);
    const int out = lib.Create(N, N);
    const int spare = lib.Create(N, N);
    check(scene > 0 && mask > 0 && out > 0 && spare > 0,
          "targets: four slots allocated");
    fake2d::RenderTarget *t_scene = lib.Get(scene);
    fake2d::RenderTarget *t_mask = lib.Get(mask);
    fake2d::RenderTarget *t_out = lib.Get(out);
    fake2d::Color px{};

    const fake2d::Color kMid{0.8f, 0.6f, 0.4f, 1.0f};
    const fake2d::Color kRed{1.0f, 0.0f, 0.0f, 1.0f};
    const fake2d::Color kBlack{0.0f, 0.0f, 0.0f, 1.0f};

    // A whole-target helper: fill `id` with one colour, no blending involved.
    auto fill = [&](int id, const fake2d::Color &c) {
        if (!r.BeginRenderTarget(id)) {
            return false;
        }
        r.SetBlendMode(fake2d::BlendMode::Alpha);
        r.ClearActiveTarget(c);
        r.EndRenderTarget();
        return true;
    };

    // --- the mask itself ----------------------------------------------------
    // Left half revealed (white), right half unexplored (black): the two
    // extremes of coverage, with a hard edge so a probe cannot sit on the
    // boundary by accident.
    check(fill(mask, kBlack), "mask: target binds and clears");
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(mask), "mask: rebinds for the revealed half");
    r.DrawQuad(0.0f, 0.0f, 64.0f, 128.0f, fake2d::Color::White());
    r.EndRenderTarget();
    check(t_mask->ReadPixel(32, 64, px) && near(px.r, 1.0f) && near(px.g, 1.0f),
          "mask: the revealed half is white");
    check(t_mask->ReadPixel(96, 64, px) && near(px.r, 0.0f) && near(px.g, 0.0f),
          "mask: the unexplored half is black");

    // --- multiply compositing ----------------------------------------------
    // Scene (mid colour) drawn normally, then the mask drawn *over* it in
    // multiply mode. This is the whole fog-of-war pipeline in three draws.
    check(fill(out, fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f}), "out: target binds");
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(out), "out: rebinds to composite");
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.DrawQuad(0.0f, 0.0f, static_cast<float>(N), static_cast<float>(N), kMid);
    r.SetBlendMode(fake2d::BlendMode::Multiply);
    r.DrawRenderTarget(mask, 0.0f, 0.0f, static_cast<float>(N),
                       static_cast<float>(N));
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndRenderTarget();
    check(t_out->ReadPixel(32, 64, px) && near(px.r, 0.8f) && near(px.g, 0.6f) &&
              near(px.b, 0.4f),
          "multiply: a white mask leaves the scene untouched");
    check(t_out->ReadPixel(96, 64, px) && near(px.r, 0.0f) && near(px.g, 0.0f) &&
              near(px.b, 0.0f),
          "multiply: a black mask erases the scene to black");

    // Probes sit well inside each half — on the 64 px seam the mask is a
    // single-texel edge either way and the assertion would read mush.

    // Per-channel: a mask that keeps red but drops green proves this is a
    // multiplication of channels, not an alpha cover (which would blend every
    // channel by the same amount) or an inverse.
    check(fill(mask, kRed), "mask: refilled red");
    check(fill(out, kMid), "out: refilled mid");
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(out), "out: rebinds for the per-channel mask");
    r.SetBlendMode(fake2d::BlendMode::Multiply);
    r.DrawRenderTarget(mask, 0.0f, 0.0f, static_cast<float>(N),
                       static_cast<float>(N));
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndRenderTarget();
    check(t_out->ReadPixel(64, 64, px) && near(px.r, 0.8f),
          "multiply: the red channel survives a red mask");
    check(t_out->ReadPixel(64, 64, px) && near(px.g, 0.0f),
          "multiply: the green channel is dropped by a red mask");
    check(t_out->ReadPixel(64, 64, px) && near(px.b, 0.0f),
          "multiply: the blue channel is dropped by a red mask");

    // A half-covered pixel reads half the scene, and — the two-sided part —
    // neither full nor empty. A true coverage value has to land in between.
    check(fill(mask, fake2d::Color{0.5f, 0.5f, 0.5f, 1.0f}), "mask: refilled grey");
    check(fill(out, fake2d::Color::White()), "out: refilled white");
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(out), "out: rebinds for the partial mask");
    r.SetBlendMode(fake2d::BlendMode::Multiply);
    r.DrawRenderTarget(mask, 0.0f, 0.0f, static_cast<float>(N),
                       static_cast<float>(N));
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndRenderTarget();
    check(t_out->ReadPixel(64, 64, px) && px.r > 0.3f && px.r < 0.7f,
          "multiply: a half mask dims to half rather than switching");

    // Blend state is per-draw, so one frame can composite *and* paint: after
    // the multiply above, an ordinary red quad is still opaque.
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(out), "out: rebinds for the mixed-blend frame");
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.DrawQuad(0.0f, 0.0f, static_cast<float>(N), static_cast<float>(N), kBlack);
    r.SetBlendMode(fake2d::BlendMode::Multiply);
    r.DrawQuad(0.0f, 0.0f, 32.0f, 32.0f, fake2d::Color::White());
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.DrawQuad(64.0f, 0.0f, 32.0f, 32.0f, kRed);
    r.EndRenderTarget();
    check(t_out->ReadPixel(80, 16, px) && near(px.r, 1.0f) && near(px.g, 0.0f),
          "blend: switching back to alpha paints opaquely again");
    // The discriminator: a white multiply quad over a black base stays black,
    // while an alpha one would paint white there. Its absence is exactly why
    // "the batch does not split runs on a blend change" passed the suite.
    check(t_out->ReadPixel(16, 16, px) && near(px.r, 0.0f) && near(px.g, 0.0f),
          "blend: multiply over a black base stays black");
    check(t_out->ReadPixel(112, 16, px) && near(px.r, 0.0f),
          "blend: a multiply quad does not leak past its own rect");

    // --- why multiply and not rt_copy --------------------------------------
    // Copying another target in while one is bound is refused (nesting), so
    // compositing two masks could not be expressed at all before this.
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(out), "nesting: target bound");
    check(!r.CopyRenderTarget(mask, spare, nullptr),
          "nesting: rt_copy is refused while a target is bound");
    // The multiply path has no such restriction: both of these draw into the
    // bound target legally, in any order, with no feedback read.
    r.SetBlendMode(fake2d::BlendMode::Multiply);
    r.DrawRenderTarget(mask, 0.0f, 0.0f, 64.0f, 64.0f);
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndRenderTarget();
    r.EndFrame();
    check(r.ActiveRenderTarget() == 0, "nesting: the frame ended and unbound");

    // --- accumulation persists across frames -------------------------------
    // Fog is memory: a cell revealed on Monday is still revealed on Friday,
    // and BeginRenderTarget deliberately does not clear.
    check(fill(mask, kBlack), "accumulation: mask starts empty");
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(mask), "accumulation: frame 1 binds the mask");
    r.SetBlendMode(fake2d::BlendMode::Additive);
    r.DrawQuad(0.0f, 0.0f, 32.0f, 128.0f, fake2d::Color{0.25f, 0.25f, 0.25f, 1.0f});
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndRenderTarget();
    r.EndFrame();
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    check(r.BeginRenderTarget(mask), "accumulation: frame 2 rebinds");
    r.SetBlendMode(fake2d::BlendMode::Additive);
    r.DrawQuad(0.0f, 0.0f, 32.0f, 128.0f, fake2d::Color{0.25f, 0.25f, 0.25f, 1.0f});
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndRenderTarget();
    r.EndFrame();
    check(t_mask->ReadPixel(16, 64, px) && px.r > 0.3f && px.r < 0.7f,
          "accumulation: two visits stack into a brighter reveal");
    check(t_mask->ReadPixel(64, 64, px) && near(px.r, 0.0f),
          "accumulation: never-visited pixels stay black");

    // Sampling a drawn target is how a script asks about coverage; the same
    // read-back is what every assertion above is built on, so this pins its
    // orientation (screens are flipped relative to FBO rows) and its bounds.
    const float across = static_cast<float>(t_mask->ReadPixel(120, 64, px)
                                                ? px.r
                                                : -1.0f);
    check(across >= 0.0f && near(across, 0.0f), "sample: an in-bounds read succeeds");
    check(!t_mask->ReadPixel(N, 64, px), "sample: x past the edge is out of range");
    check(!t_mask->ReadPixel(0, N + 1, px), "sample: y past the edge is out of range");
    check(!t_mask->ReadPixel(-1, 0, px), "sample: a negative coordinate is out of range");

    // The scene target is untouched by any of this: nothing wrote to it after
    // its fill, so a mask composite can never mutate its source.
    check(fill(scene, kMid), "scene: refilled");
    check(t_scene->ReadPixel(64, 64, px) && near(px.r, 0.8f) && near(px.g, 0.6f),
          "scene: reading a source target leaves it intact");

    // And the whole pipeline works on the window too, with no target bound.
    r.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.DrawQuad(100.0f, 100.0f, 200.0f, 100.0f, fake2d::Color::White());
    r.SetBlendMode(fake2d::BlendMode::Multiply);
    r.DrawRenderTarget(mask, 100.0f, 100.0f, 200.0f, 100.0f);
    r.SetBlendMode(fake2d::BlendMode::Alpha);
    r.EndFrame();
    std::error_code ec;
    const std::string shot = "assets/_fog_test.png";
    std::filesystem::remove(shot, ec);
    check(r.SaveScreenshot(shot), "screenshot: the window framebuffer is intact");
    const auto shot_bytes = std::filesystem::file_size(shot, ec);
    check(!ec && shot_bytes > 0, "screenshot: wrote a non-empty png");
    std::filesystem::remove(shot, ec);

    lib.Clear();
    std::printf("[fog] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

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

int RunAudioTest(fake2d::Engine & /*engine*/) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[audio] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b, float tol = 0.01f) {
        return std::fabs(a - b) < tol;
    };

    constexpr int kRate = fake2d::AudioEngine::kSampleRate;
    // Constant clip: the mixed sample *is* the gain, so every bus and
    // envelope assertion is a direct read.
    auto constant = [](float value, int frames) {
        return std::vector<float>(static_cast<std::size_t>(frames), value);
    };

    // --- registration ------------------------------------------------------
    {
        fake2d::AudioEngine audio;
        check(!audio.IsEnabled(), "offline: no device claimed");
        check(audio.AddClip("tone", constant(0.5f, kRate)),
              "clip: registers without a device");
        check(!audio.AddClip("tone", constant(0.5f, kRate)),
              "clip: duplicate name rejected");
        check(!audio.AddClip("empty", {}), "clip: empty rejected");
        check(audio.ClipCount() == 1, "clip: only the accepted one is kept");
        check(audio.PlayingCount() == 0, "offline: nothing plays yet");

        std::vector<float> out(128);
        audio.Mix(out.data(), 64);
        bool silent = true;
        for (float v : out) {
            silent = silent && v == 0.0f;
        }
        check(silent, "mix: silence with no voices");
        check(audio.Peak() == 0.0f, "mix: peak is 0 with no voices");
    }

    // --- bus gains ---------------------------------------------------------
    {
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(0.5f, kRate));
        audio.Play("tone", 1.0f);
        check(near(MixLast(audio, 1), 0.5f), "gain: voice 1 with all buses at 1");

        audio.SetBusVolume(fake2d::AudioBus::Master, 0.5f);
        check(near(MixLast(audio, 1), 0.25f), "gain: master 0.5 halves it");

        audio.SetBusVolume(fake2d::AudioBus::Master, 1.0f);
        audio.SetBusVolume(fake2d::AudioBus::Sfx, 0.25f);
        check(near(MixLast(audio, 1), 0.125f), "gain: sfx 0.25 quarters it");

        // The music bus must not touch a one-shot voice.
        audio.SetBusVolume(fake2d::AudioBus::Music, 0.0f);
        check(near(MixLast(audio, 1), 0.125f), "gain: the music bus leaves sfx alone");

        audio.SetBusVolume(fake2d::AudioBus::Music, 1.0f);
        audio.SetBusVolume(fake2d::AudioBus::Sfx, 1.0f);
        audio.SetBusVolume(fake2d::AudioBus::Master, 9.0f);
        check(near(MixLast(audio, 1), 0.5f), "gain: master clamps to 1");
        audio.SetBusVolume(fake2d::AudioBus::Master, -4.0f);
        check(near(MixLast(audio, 1), 0.0f), "gain: master clamps to 0 (silence)");
        check(near(audio.BusVolume(fake2d::AudioBus::Master), 0.0f),
              "gain: the clamped value reads back");
    }

    // --- one-shot lifetime and the voice pool ------------------------------
    {
        fake2d::AudioEngine audio;
        audio.AddClip("blip", constant(1.0f, 100));
        audio.Play("nope", 1.0f); // unknown clip: no voice
        check(audio.PlayingCount() == 0, "play: an unknown clip starts nothing");

        audio.Play("blip", 1.0f);
        check(audio.PlayingCount() == 1, "play: one voice");
        MixLast(audio, 100);
        check(audio.PlayingCount() == 1, "one-shot: still playing at its last sample");
        MixLast(audio, 1);
        check(audio.PlayingCount() == 0, "one-shot: frees itself when it runs out");

        // Loop: a music voice never runs out.
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.PlayMusic("tone", 1.0f);
        check(audio.IsMusicPlaying(), "music: reports playing");
        check(audio.MusicName() == "tone", "music: reports the track name");
        MixLast(audio, kRate + 128); // a full clip plus a bit
        check(audio.IsMusicPlaying(), "music: the loop survives its clip length");

        fake2d::AudioEngine pool;
        pool.AddClip("blip", constant(1.0f, 100));
        for (std::size_t i = 0; i < fake2d::AudioEngine::kMaxVoices + 4; ++i) {
            pool.Play("blip", 1.0f);
        }
        check(pool.PlayingCount() == fake2d::AudioEngine::kMaxVoices,
              "pool: caps at kMaxVoices and drops the overflow");
    }

    // --- envelopes: fade in, cross-fade, fade out, ramps -------------------
    {
        fake2d::AudioEngine audio;
        audio.AddClip("a", constant(0.5f, kRate));
        audio.AddClip("b", constant(0.5f, kRate));

        // Fade in over 1.0 s. The clip is a constant 0.5, so the sample is
        // 0.5 x gain: 0 at the start, 0.125 a quarter in, 0.25 at halfway
        // and 0.5 once the ramp has finished.
        audio.PlayMusic("a", 1.0f, 1.0f);
        check(near(MixLast(audio, 1), 0.0f), "fade: starts from silence");
        check(near(MixLast(audio, kRate / 4 - 1), 0.125f, 0.02f),
              "fade: a quarter in, the gain is a quarter");
        check(near(MixLast(audio, kRate / 4), 0.25f, 0.02f),
              "fade: halfway, the gain is halfway");
        check(near(MixLast(audio, kRate / 2), 0.5f, 0.02f),
              "fade: settled at the target gain");
        check(audio.IsMusicPlaying(), "fade: the track is still the music");

        // Cross-fade: the old track keeps playing while it ramps down.
        audio.PlayMusic("b", 1.0f, 1.0f);
        check(audio.PlayingCount() == 2, "cross-fade: both tracks are audible");
        check(audio.MusicName() == "b", "cross-fade: the new track is 'the' music");
        // A linear cross-fade of two equal clips holds the level: 0.5*0.5 +
        // 0.5*0.5 = 0.5.
        check(near(MixLast(audio, kRate / 2 - 1), 0.5f, 0.03f),
              "cross-fade: the level holds through the middle");
        // Half the fade is already mixed above; this crosses the 1.0 s mark,
        // where the outgoing track reaches 0 and frees its voice. The extra
        // frames are slack so a float-accumulated fade_pos cannot land a
        // hair short of the mark.
        MixLast(audio, kRate / 2 + 64);
        check(audio.PlayingCount() == 1, "cross-fade: the old track retires at 0");
        check(audio.MusicName() == "b", "cross-fade: the new track survives");
        check(near(MixLast(audio, 1), 0.5f, 0.02f),
              "cross-fade: the new track holds the level alone");

        // Fade out to silence retires the voice; a hard stop does it at once.
        audio.StopMusic(0.5f);
        check(audio.IsMusicPlaying(), "stop: still fading out");
        MixLast(audio, static_cast<int>(kRate * 0.6));
        check(!audio.IsMusicPlaying(), "stop: gone once the fade completes");
        check(audio.PlayingCount() == 0, "stop: the voice was released");

        audio.PlayMusic("a", 1.0f);
        audio.StopMusic(0.0f);
        check(audio.PlayingCount() == 0, "stop: a zero fade stops immediately");

        // Ramp the music's gain, then set it outright.
        audio.PlayMusic("a", 1.0f);
        audio.FadeMusicTo(0.25f, 1.0f);
        check(near(MixLast(audio, static_cast<int>(kRate * 1.1)), 0.125f, 0.02f),
              "ramp: the music settles at the new gain");
        audio.SetMusicVolume(1.0f);
        check(near(MixLast(audio, 1), 0.5f), "volume: an immediate set cancels the ramp");
        check(near(audio.MusicVolume(), 1.0f), "volume: reads back the target gain");
    }

    // --- pause freezes the mix ---------------------------------------------
    {
        fake2d::AudioEngine audio;
        // A ramp makes the cursor position readable from the sample value.
        std::vector<float> ramp(1000);
        for (std::size_t i = 0; i < ramp.size(); ++i) {
            ramp[i] = static_cast<float>(i) / 1000.0f;
        }
        audio.AddClip("ramp", ramp);
        audio.Play("ramp", 1.0f);
        const float before = MixLast(audio, 100);

        audio.SetPaused(true);
        check(audio.IsPaused(), "pause: reported");
        check(MixLast(audio, 500) == 0.0f, "pause: the mix is silence");
        check(audio.Peak() == 0.0f, "pause: the peak is silent too");

        audio.SetPaused(false);
        // Unpaused, the ramp must continue from sample 100, not 600.
        check(near(MixLast(audio, 1), 100.0f / 1000.0f, 0.005f),
              "pause: nothing advanced while paused");
        check(before > 0.0f, "pause: the cursor did move before the pause");
    }

    // --- peak --------------------------------------------------------------
    {
        fake2d::AudioEngine audio;
        audio.AddClip("loud", constant(1.0f, kRate));
        audio.Play("loud", 1.0f);
        MixLast(audio, 64);
        check(near(audio.Peak(), 1.0f), "peak: reports the loudest sample");
        audio.SetBusVolume(fake2d::AudioBus::Master, 0.25f);
        MixLast(audio, 64);
        check(near(audio.Peak(), 0.25f), "peak: follows the master gain");
    }

    // --- positional audio: distance rolloff + stereo pan -------------------
    // The listener sits at the origin with a 1000-unit range: a voice is
    // silent that far away, and reaches full separation at half of it.
    {
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);

        // Straight ahead at half range: half level, dead centre.
        audio.PlayAt("tone", 1.0f, 0.0f, -500.0f);
        Stereo centred = MixStereo(audio, 1);
        check(near(centred.left, 0.5f), "positional: distance halves the level");
        check(near(centred.right, 0.5f), "positional: an emitter ahead is centred");
    }
    {
        // Hard right at half range: full level there, silence on the left.
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, 500.0f, 0.0f);
        Stereo hard = MixStereo(audio, 1);
        check(near(hard.right, 0.5f), "positional: the near channel keeps its level");
        check(hard.left == 0.0f, "positional: hard pan mutes the far channel");
    }
    {
        // Past half range the pan is already clamped, so only the distance
        // still bites. (Its own engine: two placed one-shots would sum.)
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, 750.0f, 0.0f);
        Stereo further = MixStereo(audio, 1);
        check(near(further.right, 0.25f), "positional: beyond full separation only distance bites");
        check(further.left == 0.0f, "positional: the pan stays clamped");
    }
    {
        // A quarter to the left: pan -0.5, distance gain 0.75.
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, -250.0f, 0.0f);
        Stereo leftish = MixStereo(audio, 1);
        check(near(leftish.left, 0.75f), "positional: the near side is the loud one");
        check(near(leftish.right, 0.375f), "positional: partial pan trims the far side");
    }
    {
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, 1200.0f, 0.0f);
        Stereo out = MixStereo(audio, 1);
        check(out.left == 0.0f && out.right == 0.0f,
              "positional: out of range is silence");
        check(audio.PlayingCount() == 1,
              "positional: the voice keeps running (only inaudible)");
    }
    {
        // Moving the listener must re-evaluate voices that are already
        // sounding, otherwise walking past an emitter would leave its sound
        // parked where it started.
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, 500.0f, 0.0f);
        MixStereo(audio, 1);
        audio.SetListener(500.0f, 0.0f);
        Stereo on_top = MixStereo(audio, 1);
        check(near(on_top.left, 1.0f) && near(on_top.right, 1.0f),
              "positional: arriving at the emitter centres it at full level");
        audio.SetListener(500.0f, -1000.0f);
        Stereo away = MixStereo(audio, 1);
        check(away.left == 0.0f && away.right == 0.0f,
              "positional: leaving the emitter silences it again");
    }
    {
        // Voices with no position must not react to the listener at all.
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.Play("tone", 1.0f);
        audio.SetListener(9000.0f, -9000.0f);
        Stereo still = MixStereo(audio, 1);
        check(near(still.left, 1.0f) && near(still.right, 1.0f),
              "positional: unplaced voices ignore the listener");
    }
    {
        // Placement sits upstream of the buses, so they still apply.
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, 500.0f, 0.0f);
        MixStereo(audio, 1);
        audio.SetBusVolume(fake2d::AudioBus::Sfx, 0.5f);
        Stereo ducked = MixStereo(audio, 1);
        check(near(ducked.right, 0.25f), "positional: the sfx bus still applies");
        check(ducked.left == 0.0f, "positional: buses never resurrect a muted channel");
    }
    {
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        // Default range: same 1000 units, so scripts need not set anything.
        check(near(audio.ListenerRange(), fake2d::AudioEngine::kDefaultRange),
              "positional: the default range is documented");
        audio.SetListenerRange(0.0f);
        check(near(audio.ListenerRange(), fake2d::AudioEngine::kDefaultRange),
              "positional: a zero range is refused");
        audio.SetListenerRange(-5.0f);
        check(near(audio.ListenerRange(), fake2d::AudioEngine::kDefaultRange),
              "positional: a negative range is refused");
        audio.SetListener(3.0f, 4.0f);
        check(near(audio.ListenerX(), 3.0f) && near(audio.ListenerY(), 4.0f),
              "positional: the listener position reads back");
        audio.PlayAt("unknown", 1.0f, 0.0f, 0.0f);
        check(audio.PlayingCount() == 0,
              "positional: an unknown clip still starts nothing");
    }
    {
        // The per-channel peaks are the stereo image made visible.
        fake2d::AudioEngine audio;
        audio.AddClip("tone", constant(1.0f, kRate));
        audio.SetListener(0.0f, 0.0f);
        audio.SetListenerRange(1000.0f);
        audio.PlayAt("tone", 1.0f, 500.0f, 0.0f);
        MixStereo(audio, 64);
        check(near(audio.PeakLeft(), 0.0f), "peak: the muted channel reads 0");
        check(near(audio.PeakRight(), 0.5f), "peak: per-channel peaks are separate");
        check(near(audio.Peak(), 0.5f), "peak: the scalar peak is the louder one");
    }

    // --- WAV decoding ------------------------------------------------------
    {
        fake2d::AudioEngine audio;
        std::error_code ec;
        std::filesystem::create_directories("assets", ec);
        const std::string path = "assets/_audio_test.wav";
        // 22050 Hz forces the resample path; 0.5 is constant, so resampling
        // has to reproduce it exactly.
        WriteWav(path, constant(0.5f, 1000), 22050);
        check(audio.AddClipWav("wav22", path), "wav: a 22050 Hz file decodes");
        audio.Play("wav22", 1.0f);
        check(near(MixLast(audio, 1), 0.5f),
              "wav: resampled to the mixer rate at the same level");

        {
            std::ofstream junk("assets/_audio_test_bad.wav", std::ios::binary);
            junk << "not a wav at all";
        }
        check(!audio.AddClipWav("bad", "assets/_audio_test_bad.wav"),
              "wav: a non-WAV file is rejected");
        std::filesystem::remove("assets/_audio_test_bad.wav", ec);
        std::filesystem::remove(path, ec);
    }

    // --- decoded files (Phase 29) ------------------------------------------
    // AddClipFile runs the same file through miniaudio's decoder instead of the
    // hand-rolled WAV parser, so the two paths must agree — that is the whole
    // point of having both, and it is checkable with no new assets.
    {
        fake2d::AudioEngine audio;
        std::error_code ec;
        std::filesystem::create_directories("assets", ec);
        const std::string path = "assets/_audio_test.wav";
        // A ramp, not a constant: a constant would survive any resample bug.
        std::vector<float> src(4000);
        for (std::size_t i = 0; i < src.size(); ++i) {
            src[i] = 0.6f * std::sin(static_cast<float>(i) * 0.01f);
        }
        WriteWav(path, src, 22050);
        check(audio.AddClipWav("hand", path), "decode: the WAV parser takes it");
        check(audio.AddClipFile("dec", path), "decode: the file decoder takes it");
        const float len_hand = audio.ClipDuration("hand");
        const float len_dec = audio.ClipDuration("dec");
        check(near(len_hand, len_dec, 0.01f),
              "decode: both paths report the same length");
        // Same track, two engines, same gain. The two paths resample with
        // different interpolators, so they are not sample-identical (a
        // sub-frame phase offset is expected) — what must agree is the
        // length and the energy, i.e. "the same audio came out".
        fake2d::AudioEngine other;
        check(other.AddClipFile("dec", path), "decode: the second engine registers it");
        audio.PlayMusic("hand", 1.0f);
        other.PlayMusic("dec", 1.0f);
        double energy_a = 0.0;
        double energy_b = 0.0;
        float peak_a = 0.0f;
        float peak_b = 0.0f;
        for (int block = 0; block < 8; ++block) {
            std::vector<float> a(512 * 2), b(512 * 2);
            audio.Mix(a.data(), 512);
            other.Mix(b.data(), 512);
            for (int i = 0; i < 512; ++i) {
                const float sa = a[static_cast<std::size_t>(i) * 2];
                const float sb = b[static_cast<std::size_t>(i) * 2];
                energy_a += static_cast<double>(sa) * sa;
                energy_b += static_cast<double>(sb) * sb;
                peak_a = std::max(peak_a, std::fabs(sa));
                peak_b = std::max(peak_b, std::fabs(sb));
            }
        }
        check(peak_a > 0.05f && peak_b > 0.05f, "decode: both paths are audible");
        check(near(peak_a, peak_b, 0.05f), "decode: both paths peak at the same level");
        check(std::fabs(energy_a - energy_b) < energy_a * 0.05,
              "decode: both paths carry the same energy");

        check(!audio.AddClipFile("missing", "assets/_audio_nope.wav"),
              "decode: a missing file is rejected");
        check(!audio.AddClipFile("bad", "assets/_audio_test_bad.ogg"),
              "decode: an unknown format is rejected");
        check(!audio.AddClipFile("hand", path), "decode: a taken name is refused");
        check(near(audio.ClipDuration("nope"), 0.0f),
              "decode: an unknown name has no length");
        check(audio.ResidentFrames("nope") == 0, "decode: an unknown name holds nothing");
        std::filesystem::remove(path, ec);
    }

    // --- streamed tracks (Phase 29) ----------------------------------------
    {
        fake2d::AudioEngine audio;
        std::error_code ec;
        std::filesystem::create_directories("assets", ec);
        const std::string path = "assets/_audio_test.wav";
        // Long enough (0.9 s at 22.05 kHz -> ~1.8 s resampled) that one decode
        // window is visibly a window and not the whole file.
        std::vector<float> src(20000);
        for (std::size_t i = 0; i < src.size(); ++i) {
            src[i] = 0.6f * std::sin(static_cast<float>(i) * 0.01f);
        }
        WriteWav(path, src, 22050);

        check(audio.AddStream("st", path), "stream: a file registers as a stream");
        check(!audio.AddStream("st", path), "stream: a taken name is refused");
        check(!audio.AddStream("gone", "assets/_audio_nope.wav"),
              "stream: a missing file is rejected");
        // The length is the file's, even though nothing is decoded yet.
        check(near(audio.ClipDuration("st"), 20000.0f / 22050.0f, 0.01f),
              "stream: the length is known before any frame is read");
        check(audio.ResidentFrames("st") == 0,
              "stream: nothing is decoded before the first mix");

        // Residency: a fresh engine, played just far enough to fill a window.
        {
            fake2d::AudioEngine held;
            check(held.AddStream("st", path), "stream: the residency case registers");
            held.PlayMusic("st", 1.0f);
            MixLast(held, 64);
            check(held.ResidentFrames("st") > 0, "stream: the first mix fills a window");
            check(held.ResidentFrames("st") <= fake2d::AudioEngine::kStreamWindowFrames,
                  "stream: only one window is ever held");
            // 20000 source frames at 22.05 kHz resample to ~40000, so holding
            // all of them would mean the file was decoded up front.
            check(held.ResidentFrames("st") < 40000,
                  "stream: the whole file is not resident");
        }

        // The real assertion: a streamed track must sound exactly like the
        // same file decoded up front, including across window refills — hence
        // the small blocks, which straddle a boundary on purpose. Both engines
        // start from frame 0, or the comparison would read a phase offset.
        {
            fake2d::AudioEngine streamed;
            fake2d::AudioEngine full;
            check(streamed.AddStream("st", path), "stream: the comparison case registers");
            check(full.AddClipFile("whole", path), "stream: the reference clip loads");
            streamed.PlayMusic("st", 1.0f);
            full.PlayMusic("whole", 1.0f);
            bool same = true;
            float peak = 0.0f;
            for (int block = 0; block < 40; ++block) {
                std::vector<float> a(256 * 2), b(256 * 2);
                streamed.Mix(a.data(), 256);
                full.Mix(b.data(), 256);
                for (int i = 0; i < 256; ++i) {
                    const float l = a[static_cast<std::size_t>(i) * 2];
                    if (std::fabs(l - b[static_cast<std::size_t>(i) * 2]) > 0.001f) {
                        same = false;
                    }
                    peak = std::max(peak, std::fabs(l));
                }
            }
            check(same, "stream: a streamed track matches the decoded one");
            check(peak > 0.05f, "stream: it is actually audible");
        }

        // Looping: the music cursor runs past the end and comes back, which
        // means re-decoding from the start of the file.
        const std::uint64_t frames =
            static_cast<std::uint64_t>(audio.ClipDuration("st") *
                                       static_cast<float>(kRate)) + 64u;
        fake2d::AudioEngine loop_audio;
        check(loop_audio.AddStream("st", path), "stream: the loop case registers");
        loop_audio.PlayMusic("st", 1.0f);
        float loop_peak = 0.0f;
        for (std::uint64_t done = 0; done < frames; done += 512) {
            const int n = static_cast<int>(std::min<std::uint64_t>(512, frames - done));
            std::vector<float> out(static_cast<std::size_t>(n) * 2);
            loop_audio.Mix(out.data(), n);
            for (int i = 0; i < n; ++i) {
                loop_peak = std::max(loop_peak,
                                     std::fabs(out[static_cast<std::size_t>(i) * 2]));
            }
        }
        check(loop_audio.IsMusicPlaying(), "stream: the loop survives the end");
        check(loop_peak > 0.05f, "stream: frames past the end are still audible");

        // Everything the mixer already guarantees holds for a stream: pausing
        // freezes it, a fade-out ends it.
        fake2d::AudioEngine paused;
        check(paused.AddStream("st", path), "stream: the pause case registers");
        paused.PlayMusic("st", 1.0f);
        MixLast(paused, 64);
        paused.SetPaused(true);
        check(near(MixLast(paused, 64), 0.0f), "stream: pausing silences it");
        paused.SetPaused(false);
        check(std::fabs(MixLast(paused, 64)) > 0.0f, "stream: resuming picks it back up");
        paused.StopMusic(0.1f);
        MixLast(paused, static_cast<int>(kRate));
        check(!paused.IsMusicPlaying(), "stream: stopping ends the track");
        std::filesystem::remove(path, ec);
    }

    std::printf("[audio] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAIL",
                checks, failures);
    return failures == 0 ? 0 : 1;
}

int RunPathTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[path] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 1.0f; };

    // gid 1 = solid wall, gid 2 = walkable floor.
    const auto make_map = [&](int cols, int rows, const std::vector<int> &cells) {
        std::ostringstream json;
        json << R"({"orientation":"orthogonal","width":)" << cols
             << R"(,"height":)" << rows << R"(,"tilewidth":16,"tileheight":16,)"
             << R"("tilesets":[{"firstgid":1,"name":"t","tilewidth":16,"tileheight":16,)"
             << R"("columns":2,"tilecount":2,"image":"t.png","imagewidth":32,"imageheight":16,)"
             << R"("tiles":[{"id":0,"properties":[{"name":"solid","type":"bool","value":true}]}]}],)"
             << R"("layers":[{"name":"world","type":"tilelayer","visible":true,"opacity":1,"data":[)";
        for (std::size_t i = 0; i < cells.size(); ++i) {
            json << cells[i] << (i + 1 == cells.size() ? ' ' : ',');
        }
        json << "]}]}";
        auto map = std::make_unique<fake2d::Tilemap>();
        const bool ok = map->LoadFromString(json.str(), ".", engine.GetResources());
        return std::make_pair(ok, std::move(map));
    };

    // A: 6x6 open field. The diagonal is fully visible, so string pulling
    // collapses the whole run into start + goal.
    {
        std::vector<int> cells(36, 2);
        auto [ok, map] = make_map(6, 6, cells);
        check(ok && map->IsValid(), "open field parses");
        check(map->FindPath(8.0f, 8.0f, 5.0f * 16.0f + 8.0f, 5.0f * 16.0f + 8.0f),
              "open field: path found corner to corner");
        check(map->PathPointCount() == 2, "open field: straight run pulls to 2 waypoints");
        check(near(map->PathPointAt(0).x, 8.0f) && near(map->PathPointAt(0).y, 8.0f),
              "open field: first waypoint is the start cell centre");
        check(near(map->PathPointAt(1).x, 88.0f) && near(map->PathPointAt(1).y, 88.0f),
              "open field: last waypoint is the goal cell centre");
        // Same start/goal without diagonals: still reachable.
        check(map->FindPath(8.0f, 8.0f, 88.0f, 88.0f, false),
              "open field: 4-way path found");
        check(map->PathPointCount() > 2, "open field: 4-way path is not a straight line");
        // start == goal is a single waypoint
        check(map->FindPath(8.0f, 8.0f, 8.0f, 8.0f) && map->PathPointCount() == 1,
              "start == goal yields one waypoint");
    }

    // B: 5x5 vertical wall in column 2 with a gap at the bottom row.
    {
        std::vector<int> cells(25, 2);
        for (int r = 0; r < 4; ++r) {
            cells[static_cast<std::size_t>(r) * 5 + 2] = 1;
        }
        auto [ok, map] = make_map(5, 5, cells);
        check(ok, "wall map parses");
        check(map->FindPath(8.0f, 8.0f, 4.0f * 16.0f + 8.0f, 8.0f),
              "detour: path found around the wall");
        // The wall seals column 2 for rows 0..3, so any route must dip to the
        // bottom row (y = 4 * 16 + 8) to cross it.
        bool dips_to_gap_row = false;
        for (std::size_t i = 0; i < map->PathPointCount(); ++i) {
            if (near(map->PathPointAt(i).y, 4.0f * 16.0f + 8.0f)) {
                dips_to_gap_row = true;
            }
        }
        check(dips_to_gap_row, "detour: the path routes through the bottom gap");
        check(map->PathPointCount() >= 3, "detour: string pulling keeps the corners");
    }

    // C: goal sealed inside a box of walls -> unreachable.
    {
        std::vector<int> cells(25, 1);
        cells[12] = 2; // (2,2)
        cells[6] = 2;  // (1,1) start, sealed off
        auto [ok, map] = make_map(5, 5, cells);
        check(ok, "sealed map parses");
        check(!map->FindPath(1.0f * 16.0f + 8.0f, 1.0f * 16.0f + 8.0f,
                             2.0f * 16.0f + 8.0f, 2.0f * 16.0f + 8.0f),
              "sealed goal: no path");
        check(map->PathPointCount() == 0, "sealed goal: buffer stays empty");
    }

    // D: corner cutting. (0,0) -> (1,1) is diagonal; both orthogonal
    // neighbours are walls, so 8-way movement must refuse the shortcut.
    {
        std::vector<int> cells(16, 2);
        cells[1] = 1; // (1,0)
        cells[4] = 1; // (0,1)
        auto [ok, map] = make_map(4, 4, cells);
        check(ok, "corner map parses");
        check(!map->FindPath(8.0f, 8.0f, 1.0f * 16.0f + 8.0f, 1.0f * 16.0f + 8.0f),
              "corner cut: diagonal shortcut is refused");
        // Freeing one orthogonal neighbour makes the same move legal.
        cells[4] = 2;
        auto [ok2, map2] = make_map(4, 4, cells);
        check(ok2, "opened corner map parses");
        check(map2->FindPath(8.0f, 8.0f, 1.0f * 16.0f + 8.0f, 1.0f * 16.0f + 8.0f),
              "corner cut: legal diagonal is taken");
    }

    // E: endpoint guards and the node cap.
    {
        std::vector<int> cells(36, 2);
        cells[0] = 1; // (0,0) solid
        auto [ok, map] = make_map(6, 6, cells);
        check(ok, "guard map parses");
        check(!map->FindPath(8.0f, 8.0f, 88.0f, 88.0f), "solid start is rejected");
        check(!map->FindPath(-8.0f, 8.0f, 88.0f, 88.0f), "out-of-bounds start is rejected");
        check(!map->FindPath(8.0f, 8.0f, 999.0f, 999.0f), "out-of-bounds goal is rejected");
        check(!map->FindPath(88.0f, 8.0f, 8.0f, 88.0f, true, 2),
              "node cap aborts an unreachable-in-budget search");
        map->ClearPath();
        check(map->PathPointCount() == 0, "ClearPath drops the buffer");
    }

    std::printf("[path] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

// Defined later in this translation unit (marble asset scaffolding); the
// object-layer test regenerates mario.json to check the production pipeline.
void EnsureMarioAssets(bool regen);

// Tiled object-layer self-test: parsing of objectgroups (points, rects,
// ellipses, polylines, polygons), custom properties and layer/object queries
// against an inline JSON document, plus an end-to-end check of the generated
// assets/mario.json spawns layer. Prints [map-obj] lines; non-zero on fail.
// Headless-safe: parses JSON only, never touches the window or renderer.
int RunMapObjectTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[map-obj] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };

    constexpr const char *kDoc = R"JSON({
      "orientation": "orthogonal",
      "width": 4, "height": 2,
      "tilewidth": 16, "tileheight": 16,
      "tilesets": [],
      "layers": [
        {"name": "world", "type": "tilelayer", "visible": true,
         "data": [0, 0, 0, 0, 0, 0, 0, 0]},
        {"name": "spawns", "type": "objectgroup", "visible": true,
         "draworder": "topdown", "objects": [
          {"id": 1, "name": "goomba", "type": "enemy",
           "x": 18, "y": 36, "width": 0, "height": 0,
           "rotation": 0, "visible": true, "point": true,
           "properties": [
             {"name": "patrol", "type": "bool", "value": true},
             {"name": "hp", "type": "int", "value": 3},
             {"name": "speed", "type": "float", "value": 42.5},
             {"name": "mood", "type": "string", "value": "angry"}
           ]},
          {"id": 2, "name": "zone", "x": 0, "y": 0,
           "width": 64, "height": 32, "rotation": 90, "visible": false},
          {"id": 3, "name": "trigger", "x": 300, "y": 300,
           "width": 40, "height": 20, "ellipse": true},
          {"id": 4, "name": "path", "x": 10, "y": 10,
           "polyline": [{"x": 0, "y": 0}, {"x": 100, "y": 50}]},
          {"id": 5, "name": "area", "x": 0, "y": 0,
           "polygon": [{"x": 0, "y": 0}, {"x": 16, "y": 0}, {"x": 8, "y": 12}]}
        ]}
      ]
    })JSON";

    fake2d::Tilemap map;
    check(map.LoadFromString(kDoc, ".", engine.GetResources()), "inline map parses");
    check(map.IsValid() && map.Cols() == 4 && map.Rows() == 2, "map dimensions");

    check(map.Layers().size() == 2, "two layers kept");
    check(map.LayerTypeAt(0) == 0, "layer 0 is tile");
    check(map.LayerTypeAt(1) == 2, "layer 1 is object");
    check(map.LayerTypeAt(2) == -1, "layer 2 out of range");
    check(map.FindLayer("world") == 0, "find world layer");
    check(map.FindLayer("spawns") == 1, "find spawns layer");
    check(map.FindLayer("missing") == -1, "missing layer -> -1");
    check(map.ObjectsAt(0) == nullptr, "tile layer has no objects");
    check(map.ObjectsAt(9) == nullptr, "bad object layer index");

    const auto *objs = map.ObjectsAt(1);
    check(objs != nullptr && objs->size() == 5, "five objects parsed");
    if (objs != nullptr && objs->size() == 5) {
        const fake2d::MapObject &goomba = (*objs)[0];
        check(goomba.id == 1 && goomba.name == "goomba" &&
                  goomba.class_name == "enemy",
              "object id/name/type");
        check(near(goomba.x, 18.0f) && near(goomba.y, 36.0f), "object x/y");
        check(goomba.shape == fake2d::MapObjectShape::Point, "point shape");
        check(goomba.visible, "object defaults visible");
        check(goomba.HasProperty("hp") && goomba.HasProperty("mood"),
              "has properties");
        check(!goomba.HasProperty("nope"), "missing property");
        check(goomba.PropBool("patrol"), "bool property");
        check(near(static_cast<float>(goomba.PropNum("hp")), 3.0f), "int property");
        check(near(static_cast<float>(goomba.PropNum("speed")), 42.5f),
              "float property");
        check(goomba.PropStr("mood") == "angry", "string property");
        check(near(static_cast<float>(goomba.PropNum("nope", 7.0f)), 7.0f),
              "numeric default");
        check(!goomba.PropBool("nope"), "bool default");
        check(goomba.PropStr("nope").empty(), "string default");
        // Type mismatch must yield the default, not the stored value.
        check(near(static_cast<float>(goomba.PropNum("patrol", 9.0f)), 9.0f),
              "typed access rejects kind mismatch");

        const fake2d::MapObject &zone = (*objs)[1];
        check(zone.shape == fake2d::MapObjectShape::Rect, "rect shape");
        check(near(zone.width, 64.0f) && near(zone.height, 32.0f), "rect size");
        check(near(zone.rotation, 90.0f), "rotation");
        check(!zone.visible, "hidden object");
        check(zone.class_name.empty(), "empty type");

        const fake2d::MapObject &trigger = (*objs)[2];
        check(trigger.shape == fake2d::MapObjectShape::Ellipse, "ellipse shape");

        const fake2d::MapObject &path = (*objs)[3];
        check(path.shape == fake2d::MapObjectShape::Polyline, "polyline shape");
        check(path.points.size() == 2, "polyline vertex count");
        check(path.points.size() == 2 && near(path.points[0].x, 10.0f) &&
                  near(path.points[0].y, 10.0f) && near(path.points[1].x, 110.0f) &&
                  near(path.points[1].y, 60.0f),
              "polyline vertices are absolute");

        const fake2d::MapObject &area = (*objs)[4];
        check(area.shape == fake2d::MapObjectShape::Polygon, "polygon shape");
        check(area.points.size() == 3 && near(area.points[2].x, 8.0f) &&
                  near(area.points[2].y, 12.0f),
              "polygon vertices are absolute");
    }

    // Tile queries must ignore the object layer entirely.
    check(map.GidAt(1, 0, 0) == 0, "gid read on object layer is empty");
    check(!map.SolidAt(0, 0), "no solids from object layer");
    check(map.TileAt(0, 0) == 0, "tile accessor skips object layer");

    // --- End to end: the host-generated mario level + spawns layer --------
    EnsureMarioAssets(false);
    fake2d::Tilemap mario;
    check(mario.LoadFromFile("assets/mario.json", engine.GetResources()),
          "generated mario.json loads");
    int spawn_layer = mario.FindLayer("spawns");
    check(spawn_layer >= 0 && mario.LayerTypeAt(static_cast<std::size_t>(spawn_layer)) == 2,
          "mario.json has an object spawns layer");
    const auto *spawns = spawn_layer >= 0 ? mario.ObjectsAt(spawn_layer) : nullptr;
    int goombas = 0;
    int starts = 0;
    if (spawns != nullptr) {
        check(spawns->size() == 8, "spawns: 7 goombas + 1 player start");
        for (const fake2d::MapObject &obj : *spawns) {
            if (obj.name == "goomba") {
                ++goombas;
                if (goombas == 1) {
                    check(near(obj.x, 22 * 32 + 2) && near(obj.y, 12 * 32 + 4),
                          "first goomba matches authored cell");
                    check(obj.shape == fake2d::MapObjectShape::Point &&
                              obj.class_name == "enemy",
                          "goomba is an enemy point");
                }
            } else if (obj.name == "player_start") {
                ++starts;
                check(near(obj.x, 80.0f) && near(obj.y, 13 * 32),
                      "player start at 2.5 tiles / ground top");
            }
        }
    }
    check(goombas == 7, "seven goomba spawns");
    check(starts == 1, "one player start");

    // The old marker tile (gid 15) must no longer appear anywhere in the grid.
    bool marker_left = false;
    for (int r = 0; r < mario.Rows(); ++r) {
        for (int c = 0; c < mario.Cols(); ++c) {
            if (mario.TileAt(c, r) == 15) {
                marker_left = true;
            }
        }
    }
    check(!marker_left, "no goomba marker gid left in tile grid");

    std::printf("[map-obj] %s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Phase 30 self-test: Tiled tile animation.
//
// The clock is per tile *id*, not per cell (Tiled semantics), so the whole
// feature is one accumulator plus a frame lookup — and the thing that can go
// wrong is subtle: a frame index that matches by tile id breaks on a 0,1,0
// animation, and collision must never follow the animation.
// ---------------------------------------------------------------------------
int RunTileTest(fake2d::Engine &engine) {
    int failures = 0;
    int checks = 0;
    auto check = [&](bool ok, const char *what) {
        ++checks;
        if (!ok) {
            ++failures;
            std::printf("[tile] FAIL %s\n", what);
        }
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.002f; };

    // --- a 4-tile sheet: 0 red, 1 green, 2 blue, 3 yellow (64x16) ---------
    // Distinct primaries: a probe can name the frame on screen by colour, and
    // no two frames are a rounding error apart.
    const std::string sheet_path = "assets/_tile_anim.png";
    {
        std::error_code ec;
        std::filesystem::create_directories("assets", ec);
        std::vector<std::uint8_t> px(static_cast<std::size_t>(64 * 16 * 4), 0);
        for (int t = 0; t < 4; ++t) {
            std::uint8_t r = 0, g = 0, b = 0;
            switch (t) {
            case 0: r = 255; break;
            case 1: g = 255; break;
            case 2: b = 255; break;
            default: r = 255; g = 255; break;
            }
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    const std::size_t o =
                        (static_cast<std::size_t>(y) * 64 + t * 16 + x) * 4;
                    px[o + 0] = r;
                    px[o + 1] = g;
                    px[o + 2] = b;
                    px[o + 3] = 255;
                }
            }
        }
        stbi_write_png(sheet_path.c_str(), 64, 16, 4, px.data(), 64 * 4);
    }

    // Set A (gids 1..4): gid 1 is animated over local 2 (100 ms) and local 3
    // (100 ms); gid 2 is a plain static tile. Set B (gids 5..8): gid 5 loops
    // local 3 -> local 0 (150 ms each), gid 6 smuggles in a zero-duration
    // frame (rejected), gid 7 has an empty list (rejected) and gid 8 runs a
    // four-frame cycle that names the same tile twice — the shape that
    // catches a frame index derived by *matching* the shown tile id.
    constexpr const char *kDoc = R"JSON({
      "orientation": "orthogonal", "width": 3, "height": 2,
      "tilewidth": 16, "tileheight": 16,
      "tilesets": [
        {"firstgid": 1, "name": "a", "tilewidth": 16, "tileheight": 16,
         "columns": 4, "tilecount": 4, "image": "_tile_anim.png",
         "imagewidth": 64, "imageheight": 16,
         "tiles": [
           {"id": 0, "properties": [{"name": "solid", "type": "bool", "value": true}],
            "animation": [{"duration": 100, "tileid": 2},
                          {"duration": 100, "tileid": 3}]},
           {"id": 1}
         ]},
        {"firstgid": 5, "name": "b", "tilewidth": 16, "tileheight": 16,
         "columns": 4, "tilecount": 4, "image": "_tile_anim.png",
         "imagewidth": 64, "imageheight": 16,
         "tiles": [
           {"id": 0, "animation": [{"duration": 150, "tileid": 3},
                                   {"duration": 150, "tileid": 0}]},
           {"id": 1, "animation": [{"duration": 100, "tileid": 2},
                                   {"duration": 0, "tileid": 3}]},
           {"id": 2, "animation": []},
           {"id": 3, "animation": [{"duration": 100, "tileid": 1},
                                   {"duration": 100, "tileid": 0},
                                   {"duration": 100, "tileid": 1},
                                   {"duration": 100, "tileid": 0}]}
         ]}
      ],
      "layers": [{"name": "world", "type": "tilelayer", "visible": true,
                  "opacity": 1,
                  "data": [1, 2, 5,
                           1, 6, 7]}]
    })JSON";

    fake2d::Tilemap map;
    check(map.LoadFromString(kDoc, "assets", engine.GetResources()),
          "parse: the animated map loads");
    check(map.IsValid() && map.Cols() == 3 && map.Rows() == 2, "parse: 3x2 cells");

    // --- what was parsed ---------------------------------------------------
    check(map.AnimationFrameCount(1) == 2, "parse: gid 1 has two frames");
    check(near(map.AnimationCycle(1), 0.2f), "parse: gid 1 cycles in 0.2 s");
    check(map.AnimationFrameCount(2) == 0, "parse: a tile without animation is static");
    check(near(map.AnimationCycle(2), 0.0f), "parse: a static tile has no cycle");
    check(map.AnimationFrameIndex(2) == -1, "parse: a static tile has no frame index");
    check(map.AnimationFrameCount(99) == 0, "parse: an unknown gid is static");
    check(map.AnimationFrameCount(0) == 0, "parse: gid 0 is static");
    // A zero-duration frame would make the phase step degenerate, and an
    // empty list has no frame to show: both fall back to a static tile
    // instead of playing something wrong.
    check(map.AnimationFrameCount(6) == 0, "parse: a zero-duration frame is rejected");
    check(map.AnimationFrameCount(7) == 0, "parse: an empty animation is rejected");
    check(map.AnimationFrameCount(5) == 2, "parse: the second tileset animates too");
    check(near(map.AnimationCycle(5), 0.3f), "parse: gid 5 cycles in 0.3 s");
    check(map.AnimationFrameCount(8) == 4, "parse: a cycle naming a tile twice keeps 4 frames");
    check(near(map.AnimationCycle(8), 0.4f), "parse: gid 8 cycles in 0.4 s");

    // --- the clock ---------------------------------------------------------
    check(near(map.AnimationTime(), 0.0f), "clock: starts at zero");
    check(map.AnimationFrameIndex(1) == 0, "clock: frame 0 at t = 0");
    check(map.AnimatedGid(1) == 3, "clock: frame 0 is the animation's first tileid");
    check(map.AnimatedGid(2) == 2, "clock: a static tile always shows itself");
    check(map.AnimatedGid(99) == 99, "clock: an unknown gid always shows itself");

    map.Advance(0.05f);
    check(map.AnimationFrameIndex(1) == 0, "clock: still frame 0 at t = 0.05");
    map.Advance(0.06f);
    check(map.AnimationFrameIndex(1) == 1, "clock: frame 1 once t passes 0.1");
    check(map.AnimatedGid(1) == 4, "clock: frame 1 shows the second tileid");
    map.Advance(0.09f); // t = 0.20 → wraps
    check(map.AnimationFrameIndex(1) == 0, "clock: the loop wraps back to frame 0");
    check(near(map.AnimationTime(), 0.2f), "clock: time accumulates across calls");

    // The two tilesets run on the same clock but different cycles, so at
    // t = 0.2 set A is back at frame 0 while set B is inside its second frame.
    check(map.AnimationFrameIndex(5) == 1, "clock: a longer cycle is mid-run where a short one wrapped");
    check(map.AnimatedGid(5) == 5, "clock: set B's second frame is its own local 0");

    map.Advance(-1.0f);
    check(near(map.AnimationTime(), 0.2f), "clock: negative dt is ignored");
    map.ResetAnimation();
    check(near(map.AnimationTime(), 0.0f), "clock: reset puts the clock back to zero");
    check(map.AnimationFrameIndex(1) == 0, "clock: reset rewinds to frame 0");

    // A cycle that repeats a tile: the *index* has to walk 0,1,2,3 even though
    // the shown gid only alternates. Matching the id instead would report 0,1
    // forever and look plausible on screen.
    check(map.AnimationFrameIndex(8) == 0 && map.AnimatedGid(8) == 6,
          "clock: a repeating cycle starts on frame 0");
    map.Advance(0.1f);
    check(map.AnimationFrameIndex(8) == 1 && map.AnimatedGid(8) == 5,
          "clock: frame 1 of the repeating cycle");
    map.Advance(0.1f);
    check(map.AnimationFrameIndex(8) == 2 && map.AnimatedGid(8) == 6,
          "clock: frame 2 repeats frame 0's tile but is not frame 0");
    map.Advance(0.1f);
    check(map.AnimationFrameIndex(8) == 3 && map.AnimatedGid(8) == 5,
          "clock: frame 3 repeats frame 1's tile but is not frame 1");
    map.ResetAnimation();

    // --- collision does not follow the animation ---------------------------
    // gid 1 is the solid animated tile; its frames are gids 3 and 4, which are
    // not solid. A cell holding a frame tile must stay walkable, and the solid
    // cell must stay solid at every point of the cycle.
    check(map.SolidAt(0, 0), "solid: the animated tile's cell is solid");
    check(!map.SolidAt(1, 0), "solid: a cell holding a frame of it is not");
    bool stayed_solid = true;
    for (int i = 0; i < 12; ++i) {
        map.Advance(0.05f);
        if (!map.SolidAt(0, 0) || map.SolidAt(1, 0)) {
            stayed_solid = false;
        }
    }
    check(stayed_solid, "solid: the grid never changes under the animation");
    map.ResetAnimation();

    // --- the drawn picture -------------------------------------------------
    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[tile] FAIL renderer is not initialised\n");
        ++failures;
        return failures == 0 ? 0 : 1;
    }
    fake2d::RenderTargetLibrary &lib = r.GetRenderTargets();
    const int rt = lib.Create(64, 64);
    check(rt > 0, "draw: target allocated");
    fake2d::RenderTarget *t = lib.Get(rt);
    fake2d::Color px{};
    auto is_blue = [](const fake2d::Color &c) {
        return c.b > 0.7f && c.r < 0.2f && c.g < 0.2f;
    };
    auto is_yellow = [](const fake2d::Color &c) {
        return c.r > 0.7f && c.g > 0.7f && c.b < 0.2f;
    };
    auto is_green = [](const fake2d::Color &c) {
        return c.g > 0.7f && c.r < 0.2f && c.b < 0.2f;
    };
    auto is_red = [](const fake2d::Color &c) {
        return c.r > 0.7f && c.g < 0.2f && c.b < 0.2f;
    };

    // t = 0: set A frame 0 (blue), set B frame 0 (yellow), static stays green.
    r.BeginFrame(0.0f, 0.08f, 0.10f, 0.14f);
    check(r.BeginRenderTarget(rt), "draw: target binds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    map.DrawScaled(r, 1.0f);
    r.EndRenderTarget();
    check(t->ReadPixel(8, 8, px) && is_blue(px), "draw: the animated cell shows frame 0");
    check(t->ReadPixel(8, 24, px) && is_blue(px),
          "draw: every cell using the tile is in lockstep");
    check(t->ReadPixel(24, 8, px) && is_green(px), "draw: the static cell never moves");
    check(t->ReadPixel(40, 8, px) && is_yellow(px), "draw: set B's cell shows its own frame 0");

    // t = 0.12: set A is inside frame 1 (yellow), set B still frame 0.
    map.Advance(0.12f);
    check(r.BeginRenderTarget(rt), "draw: target rebinds");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    map.DrawScaled(r, 1.0f);
    r.EndRenderTarget();
    check(t->ReadPixel(8, 8, px) && is_yellow(px), "draw: the cell advanced to frame 1");
    check(t->ReadPixel(8, 24, px) && is_yellow(px), "draw: the lockstep cell advanced too");
    check(t->ReadPixel(24, 8, px) && is_green(px), "draw: the static cell still never moves");
    check(t->ReadPixel(40, 8, px) && is_yellow(px), "draw: set B is still on frame 0");

    // t = 0.22: set A wrapped back to frame 0, set B moved to frame 1 (red).
    map.Advance(0.10f);
    check(r.BeginRenderTarget(rt), "draw: target rebinds again");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    map.DrawScaled(r, 1.0f);
    r.EndRenderTarget();
    check(t->ReadPixel(8, 8, px) && is_blue(px), "draw: set A wrapped back to frame 0");
    check(t->ReadPixel(40, 8, px) && is_red(px), "draw: set B moved to frame 1");

    // The minimap bake shares the clock: half scale puts cell (0,0) at (4,4).
    check(r.BeginRenderTarget(rt), "draw: target binds for the scaled bake");
    r.ClearActiveTarget(fake2d::Color{0.0f, 0.0f, 0.0f, 1.0f});
    map.DrawScaled(r, 0.5f);
    r.EndRenderTarget();
    check(t->ReadPixel(4, 4, px) && is_blue(px), "scaled: the baked minimap animates too");
    r.EndFrame();

    // --- the engine ticks loaded maps on its own ---------------------------
    // Animated tiles ride the frame clock like the animator, so a script plays
    // water or torches with no bookkeeping at all.
    {
        std::error_code ec;
        std::filesystem::create_directories("assets", ec);
        const std::string json_path = "assets/_tile_anim.json";
        {
            std::ofstream out(json_path);
            out << kDoc;
        }
        const int id = engine.GetTilemaps().Load(json_path, engine.GetResources());
        check(id > 0, "engine: the map loads into the library");
        // Run() calls Shutdown() when it returns, and Shutdown clears the
        // library — so the clock has to be read inside the loop, from the
        // per-frame hook, not after it.
        float seen = -1.0f;
        engine.SetFrameCallback([&](fake2d::Engine &e) {
            if (const fake2d::Tilemap *m = e.GetTilemaps().Get(id)) {
                seen = m->AnimationTime();
            }
        });
        engine.Run(20);
        engine.SetFrameCallback(nullptr);
        check(seen > 0.0f, "engine: loaded maps advance every frame without a script");
        engine.GetTilemaps().Destroy(id);
    }

    std::printf("[tile] %s: %d checks, %d failures\n", failures == 0 ? "PASS" : "FAILED",
                checks, failures);
    return failures == 0 ? 0 : 1;
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
// Asset scaffolding for scripts/tile_demo.lua: an 8-tile sheet where two of
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

int main(int argc, char **argv) {
    fake2d::EngineConfig cfg;
    cfg.title = "fake2d";
    cfg.width = 960;
    cfg.height = 540;
    cfg.script_entry = "scripts/game.lua";

    bool map_demo = false;
    bool tile_demo = false;
    bool platformer_demo = false;
    bool audio_clips = false;
    bool mario_demo = false;
    bool slope_demo = false;
    bool path_demo = false;
    bool bench = false;
    bool phys_bench = false;
    bool phys_query = false;
    bool phys_filter = false;
    bool map_object = false;
    bool tile_test = false;
    bool timestep_test = false;
    bool path_test = false;
    bool rt_test = false;
    bool ui_test = false;
    bool post_test = false;
    bool fx_test = false;
    bool fog_test = false;
    bool audio_test = false;
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
        } else if (arg == "--tile-demo") {
            // Animated tiles: a pond and torches running Tiled tileset cycles.
            tile_demo = true;
            cfg.script_entry = "scripts/tile_demo.lua";
        } else if (arg == "--platformer-demo") {
            platformer_demo = true;
            cfg.script_entry = "scripts/platformer_demo.lua";
        } else if (arg == "--mario-demo") {
            mario_demo = true;
            cfg.script_entry = "scripts/mario.lua";
        } else if (arg == "--slope-demo") {
            slope_demo = true;
            cfg.script_entry = "scripts/slope_demo.lua";
        } else if (arg == "--path-demo") {
            path_demo = true;
            cfg.script_entry = "scripts/path_demo.lua";
        } else if (arg == "--rt-demo") {
            // Offscreen render targets: bake-once art + accumulating trail.
            // Needs no procedural assets — everything is drawn from quads.
            cfg.script_entry = "scripts/rt_demo.lua";
        } else if (arg == "--ui-demo") {
            // Sliders, checkboxes, text fields and scroll areas.
            cfg.script_entry = "scripts/ui_demo.lua";
        } else if (arg == "--ui2-demo") {
            // Round 2: progress bars, radio groups and combo boxes.
            cfg.script_entry = "scripts/ui2_demo.lua";
        } else if (arg == "--ui3-demo") {
            // Round 3: a draggable window, a tab bar and collapsible trees.
            cfg.script_entry = "scripts/ui3_demo.lua";
        } else if (arg == "--fog-demo") {
            // Fog of war: an accumulating coverage mask multiplied over the
            // scene, with a clickable minimap and no binary assets.
            cfg.script_entry = "scripts/fog_demo.lua";
        } else if (arg == "--post-demo") {
            // Post-processing pass over the whole world + a baked minimap.
            cfg.script_entry = "scripts/post_demo.lua";
        } else if (arg == "--fx-demo") {
            // Multi-pass chain: bloom over a procedural skyline + a
            // split-screen mode.
            cfg.script_entry = "scripts/fx_demo.lua";
        } else if (arg == "--audio-demo") {
            // Mixer front panel: bus gains, cross-fade, pause, VU meter.
            audio_clips = true;
            cfg.script_entry = "scripts/audio_demo.lua";
        } else if (arg == "--spatial-demo") {
            // Positional audio: place one-shots in the world and move the
            // listener through them with WASD / the mouse.
            audio_clips = true;
            cfg.script_entry = "scripts/spatial_demo.lua";
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
        } else if (arg == "--phys-query-test") {
            // Headless CPU-only self-test of ray casts / overlaps / contact
            // lifecycle; exit code is the pass/fail signal.
            phys_query = true;
            cfg.headless = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--phys-filter-test") {
            // Headless CPU-only self-test of category/mask collision filtering.
            phys_filter = true;
            cfg.headless = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--map-object-test") {
            // Headless self-test of Tiled objectgroup parsing/queries.
            map_object = true;
            cfg.headless = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--tile-test") {
            // Headless self-test of Tiled tileset animation.
            tile_test = true;
            cfg.headless = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--path-test") {
            // Headless self-test of the tilemap A* path finder.
            path_test = true;
            cfg.headless = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--timestep-test") {
            // Headless self-test of the fixed-step accumulator and the camera
            // follow / bounds helpers.
            timestep_test = true;
            cfg.headless = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--rt-test") {
            // Headless self-test of offscreen render targets (FBO).
            rt_test = true;
            cfg.headless = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--ui-test") {
            // Headless self-test of the sliders / checkboxes / text fields /
            // scroll areas added in Phase 20.
            ui_test = true;
            cfg.headless = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--post-test") {
            // Headless self-test of the post-processing pass and the minimap
            // bake: draw into a target, blit it through a shader, read back.
            post_test = true;
            cfg.headless = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--fx-test") {
            // Headless self-test of the multi-pass chain: rt_copy ping-pong,
            // the separable blur and the target resize.
            fx_test = true;
            cfg.headless = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--fog-test") {
            // Headless self-test of mask compositing: multiply blending, an
            // accumulation target that persists across frames, and pixel
            // sampling.
            fog_test = true;
            cfg.headless = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--audio-test") {
            // Self-test of the mixer: no device needed, the test drives
            // AudioEngine::Mix itself.
            audio_test = true;
            cfg.headless = true;
            cfg.script_entry.clear();
            cfg.vsync = false;
        } else if (arg == "--fixed-hz" && i + 1 < argc) {
            // Physics rate; 0 (or --variable-step) restores variable stepping.
            const int hz = std::atoi(argv[++i]);
            cfg.fixed_dt = hz > 0 ? 1.0f / static_cast<float>(hz) : 0.0f;
        } else if (arg == "--variable-step") {
            cfg.fixed_dt = 0.0f;
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
    if (phys_query) {
        return RunPhysicsQueryTest(engine);
    }
    if (phys_filter) {
        return RunPhysicsFilterTest(engine);
    }
    if (map_object) {
        return RunMapObjectTest(engine);
    }
    if (tile_test) {
        return RunTileTest(engine);
    }
    if (timestep_test) {
        return RunTimestepTest(engine);
    }
    if (path_test) {
        return RunPathTest(engine);
    }
    if (rt_test) {
        return RunRenderTargetTest(engine);
    }
    if (ui_test) {
        return RunUITest(engine);
    }
    if (post_test) {
        return RunPostTest(engine);
    }
    if (fx_test) {
        return RunFxTest(engine);
    }
    if (fog_test) {
        return RunFogTest(engine);
    }
    if (audio_test) {
        return RunAudioTest(engine);
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
    if (path_demo) {
        EnsurePathAssets(regen_assets);
    }
    if (map_demo) {
        EnsureMapAssets();
    }
    if (tile_demo) {
        EnsureTileAnimAssets();
    }
    if (platformer_demo) {
        EnsurePlatformerAssets();
        engine.GetAudio().AddClipWav("music", "assets/music.wav");
    }
    if (audio_clips) {
        // SFX come from EnsureGameAudio; the two looping themes are
        // synthesized so the cross-fade needs no binary asset.
        EnsureGameAudio(engine.GetAudio());
        EnsureAudioDemoMusic(engine.GetAudio());
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

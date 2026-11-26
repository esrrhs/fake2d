#include "../examples/hello/demo_support.h"
#include <benchmark/benchmark.h>
#include <exception>
#include <cstdlib>
#include <string_view>
namespace {
int benchmark_failures=0;
void BenchmarkFailure(benchmark::State &state,const char *message) {
    ++benchmark_failures; state.SkipWithError(message);
}
std::unique_ptr<fake2d::Engine> BenchmarkEngine(benchmark::State &state) {
    fake2d::EngineConfig cfg; cfg.title="fake2d";cfg.width=960;cfg.height=540;
    cfg.headless=true;cfg.vsync=false;
    auto engine=std::make_unique<fake2d::Engine>();
    if(!engine->Init(cfg)) { BenchmarkFailure(state,"engine init failed"); return {}; }
    EnsureGameAudio(engine->GetAudio()); return engine;
}
void SpriteBatchTrace(benchmark::State &state) {
    try {
    auto owner=BenchmarkEngine(state); if(!owner) return;
    auto &engine=*owner; constexpr int max_frames=0;
    for(auto _ : state) {
        (void)_; double benchmark_seconds=0;

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
        BenchmarkFailure(state,"benchmark texture creation failed"); return;
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
    state.SetIterationTime((immediate.flush_ms_sum+sorted.flush_ms_sum)/1000.0);
    state.counters["sorted_samples"]=sorted.samples;
    state.counters["immediate_samples"]=immediate.samples;
    state.counters["sorted_draw_calls_mean"]=double(sorted.draw_calls_sum)/sorted.samples;
    state.counters["immediate_draw_calls_mean"]=double(immediate.draw_calls_sum)/immediate.samples;
    benchmark::DoNotOptimize(sorted.draw_calls_sum); benchmark::DoNotOptimize(immediate.draw_calls_sum);
    if(rc) BenchmarkFailure(state,"original engine.Run returned nonzero");
    }

    } catch (const std::exception &e) { BenchmarkFailure(state, e.what()); }
    catch (...) { BenchmarkFailure(state, "unknown benchmark exception"); }
}
BENCHMARK(SpriteBatchTrace)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);

void PhysicsTrace(benchmark::State &state) {
    try {
    auto owner=BenchmarkEngine(state); if(!owner) return;
    auto &engine=*owner; constexpr int max_frames=0;
    for(auto _ : state) {
        (void)_; double benchmark_seconds=0;

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
                benchmark_seconds += std::chrono::duration<double>(t3-t0).count();
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
    state.SetIterationTime(benchmark_seconds);
    state.counters["frames_per_phase"]=kFrames; state.counters["warmup_per_phase"]=kWarmup;
    state.counters["dynamic_bodies"]=kDynamic;
    auto observed_brute=brute, observed_grid=grid;
    benchmark::DoNotOptimize(observed_brute); benchmark::DoNotOptimize(observed_grid);
    }

    } catch (const std::exception &e) { BenchmarkFailure(state, e.what()); }
    catch (...) { BenchmarkFailure(state, "unknown benchmark exception"); }
}
BENCHMARK(PhysicsTrace)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);

void LightingTrace(benchmark::State &state) {
    try {
    auto owner=BenchmarkEngine(state); if(!owner) return;
    auto &engine=*owner; constexpr int max_frames=0;
    for(auto _ : state) {
        (void)_; double benchmark_seconds=0;

    /// A full pool of casters (the slot cap) and a realistic load of moving
    /// lights, kept small enough that the vertex buffer still holds the shadow
    /// segment in one batch -- which is exactly what the draw-call gate measures.
    constexpr int kLights = 64;
    constexpr int kCasters = static_cast<int>(fake2d::LightSystem::kMaxOccluders);
    constexpr int kFrames = 120;
    constexpr int kWarmup = 20;

    fake2d::Renderer &r = engine.GetRenderer();
    if (!r.IsReady()) {
        std::printf("[light-bench] FAIL renderer is not initialised\n");
        BenchmarkFailure(state,"renderer not initialized"); return;
    }
    fake2d::LightSystem &L = r.Lights();
    L.SetEnabled(true);
    L.SetAmbient(0.12f, 0.13f, 0.18f, 1.0f);
    L.SetShadowDarkness(0.0f);
    L.SetCore(0.18f, 0.5f);

    const fake2d::Vec2 origin = r.GetCamera().Position();
    const float vw = r.GetCamera().ViewportWidth();
    const float vh = r.GetCamera().ViewportHeight();

    struct Result {
        double avg_ms = 0.0;
        double max_ms = 0.0;
        int draw_calls = 0;
        int shadow_quads = 0;
        int visible_lights = 0;
    };

    auto measure = [&](int light_count, int caster_count) {
        for (int i = 1; i <= static_cast<int>(fake2d::LightSystem::kMaxLights); ++i) {
            L.RemoveLight(i);
        }
        for (int i = 1; i <= static_cast<int>(fake2d::LightSystem::kMaxOccluders); ++i) {
            L.RemoveOccluder(i);
        }
        std::vector<fake2d::Vec2> base;
        base.reserve(static_cast<size_t>(light_count));
        for (int i = 0; i < light_count; ++i) {
            const int id = L.AddLight();
            if (id == 0) {
                break;
            }
            const float angle = static_cast<float>(i) * 0.7f;
            const float bx = origin.x + vw * 0.5f +
                             std::cos(angle) * (vw * 0.42f) *
                                 static_cast<float>((i % 5) + 1) / 5.0f;
            const float by = origin.y + vh * 0.5f +
                             std::sin(angle) * (vh * 0.42f) *
                                 static_cast<float>((i % 7) + 1) / 7.0f;
            base.push_back({bx, by});
            L.SetLightRadius(id, 90.0f + static_cast<float>(i % 5) * 40.0f);
            L.SetLightColor(id, 1.0f, 0.8f - static_cast<float>(i % 4) * 0.1f,
                            0.5f + static_cast<float>(i % 3) * 0.2f, 0.9f);
        }
        // Casters are hung around the lights rather than spread over the view:
        // a benchmark whose casters all sit out of reach measures nothing, and
        // the 1-light case below has to cast a real shadow to be a baseline.
        for (int i = 0; i < caster_count; ++i) {
            const fake2d::Vec2 anchor =
                base.empty() ? fake2d::Vec2{origin.x + vw * 0.5f, origin.y + vh * 0.5f}
                             : base[static_cast<size_t>(i) % base.size()];
            const float ang = static_cast<float>(i) * 2.39996f;
            const float rad = 26.0f + static_cast<float>(i % 7) * 15.0f;
            const float cx = anchor.x + std::cos(ang) * rad;
            const float cy = anchor.y + std::sin(ang) * rad;
            const fake2d::Vec2 quad[4] = {{cx, cy}, {cx + 26.0f, cy},
                                          {cx + 26.0f, cy + 26.0f}, {cx, cy + 26.0f}};
            if (L.AddOccluder(quad, 4) == 0) {
                break;
            }
        }

        auto step = [&](float t) {
            for (size_t i = 0; i < base.size(); ++i) {
                const float phase = t + static_cast<float>(i) * 0.35f;
                L.SetLightPosition(static_cast<int>(i) + 1,
                                   base[i].x + std::cos(phase) * 70.0f,
                                   base[i].y + std::sin(phase * 1.3f) * 70.0f);
            }
            r.BeginFrame(0.02f, 0.02f, 0.04f, 1.0f);
            L.Render();
            r.EndFrame();
        };
        for (int f = 0; f < kWarmup; ++f) {
            step(static_cast<float>(f) * 0.05f);
        }
        Result out;
        for (int f = 0; f < kFrames; ++f) {
            const auto t0 = std::chrono::steady_clock::now();
            step(static_cast<float>(kWarmup + f) * 0.05f);
            const auto t1 = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            benchmark_seconds += ms/1000.0;
            out.avg_ms += ms;
            out.max_ms = std::max(out.max_ms, ms);
            const fake2d::LightSystem::Stats s = L.GetStats();
            out.draw_calls = s.draw_calls;
            out.shadow_quads += s.shadow_quads;
            out.visible_lights = s.visible_lights;
        }
        out.avg_ms /= kFrames;
        out.shadow_quads /= kFrames;
        return out;
    };

    const Result one = measure(1, 1);
    const Result full = measure(kLights, kCasters);
    // The same stage with the budget pulled in: the worst case is a knob, not
    // a cliff, and that is the property that keeps a crowded scene on frame.
    constexpr int kCappedQuads = 1200;
    L.SetQuadBudget(kCappedQuads);
    const Result capped = measure(kLights, kCasters);
    L.SetQuadBudget(fake2d::LightSystem::kDefaultQuadBudget);

    std::printf("== fake2d 2D lighting benchmark ==\n");
    std::printf("frames/side: %d (%d warmup), view %.0fx%.0f, %d lights x %d casters\n",
                kFrames, kWarmup, vw, vh, kLights, kCasters);
    const auto report = [](const char *name, const Result &res) {
        std::printf(
            "%-10s avg %6.3f ms | max %6.3f ms | draw calls %2d | shadow quads/frame %5d "
            "| lights in view %d\n",
            name, res.avg_ms, res.max_ms, res.draw_calls, res.shadow_quads,
            res.visible_lights);
    };
    report("1 light", one);
    report("full", full);
    report("capped", capped);

    int failures = 0;
    int gates = 0;
    auto expect = [&](bool ok, const char *what) {
        ++gates;
        if (!ok) {
            ++failures;
            std::printf("[light-bench] FAIL %s\n", what);
        }
    };
    // A benchmark that measured nothing is not a gate.
    expect(one.shadow_quads > 0 && full.shadow_quads > 0,
           "the scene actually casts shadows");
    expect(full.visible_lights >= kLights - 4,
           "the full stage really is a full pool of lights in view");
    // The batch's own capacity is the only reason for the allowance: past 4096
    // quads a segment spills into a second flush, which is a geometry cost and
    // not a per-light one. A pass that flushed once per light would land in the
    // hundreds here -- which is exactly the regression this gate caught.
    expect(full.draw_calls <= one.draw_calls + 2,
           "64x the lights and 256x the casters add no per-light draw calls");
    expect(full.draw_calls <= 8, "the whole stage stays within single-digit draw calls");
    expect(capped.shadow_quads <= kCappedQuads,
           "the quad budget holds the worst case down under load");
    expect(capped.avg_ms <= full.avg_ms * 1.25 + 2.0,
           "capping the shadow geometry does not make the frame slower");
    // No absolute millisecond bound here on purpose. This pass is fill-rate
    // bound, and the same frame is an order of magnitude apart on a GPU versus
    // the software GL a CI runner uses; a ceiling loose enough for llvmpipe
    // would wave a real regression through. What is gated is the work submitted
    // -- draw calls and geometry -- which holds on any machine, and the clock
    // is printed so the trend is visible in the log.
    std::printf("clock (reported, not gated): full stage %.3f ms/frame = %.0f fps\n",
                full.avg_ms, 1000.0 / (full.avg_ms + 1e-6));

    std::printf("[light-bench] %s: %d checks, %d failures (full stage %.3f ms/frame, "
                "%d draw calls, %d quads)\n",
                failures == 0 ? "PASS" : "FAILED", gates, failures, full.avg_ms,
                full.draw_calls, full.shadow_quads);
    state.SetIterationTime(benchmark_seconds);
    state.counters["original_gates"]=gates; state.counters["full_draw_calls"]=full.draw_calls;
    state.counters["full_shadow_quads"]=full.shadow_quads; state.counters["capped_shadow_quads"]=capped.shadow_quads;
    state.counters["full_visible_lights"]=full.visible_lights;
    auto observed_full=full, observed_capped=capped;
    benchmark::DoNotOptimize(observed_full); benchmark::DoNotOptimize(observed_capped);
    if(failures) BenchmarkFailure(state,"original lighting gates failed");
    }

    } catch (const std::exception &e) { BenchmarkFailure(state, e.what()); }
    catch (...) { BenchmarkFailure(state, "unknown benchmark exception"); }
}
BENCHMARK(LightingTrace)->Iterations(1)->UseManualTime()->Unit(benchmark::kMillisecond);

} // namespace
int main(int argc,char **argv) {
    benchmark::Initialize(&argc,argv);
    if(benchmark::ReportUnrecognizedArguments(argc,argv)) return 1;
    const auto selected=benchmark::RunSpecifiedBenchmarks(); benchmark::Shutdown();
    return benchmark_failures || selected==0 ? 1 : 0;
}

// Pure migration of the existing four UI benchmark workloads; no new cases.
#include <benchmark/benchmark.h>
#include <exception>
#include <cstdlib>
#include <string_view>
#include "fake2d/engine.h"
#include "fake2d/input.h"
#include "fake2d/camera.h"
#include "fake2d/ui.h"
#include "fake2d/version.h"
#include "render/gl.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

constexpr int kAudioRate = 44100;
constexpr double kTwoPi = 6.283185307179586;

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

int SetupBenchmark(fake2d::Engine &engine) {
    auto &renderer = engine.GetRenderer();
    renderer.AddFallbackFont("assets/fonts/ui_layout_test.otf", 28);
    if (!renderer.IsReady())
        return 1;
    const auto gl_string = [](unsigned int name) {
        const auto *value = glGetString(name);
        return value ? reinterpret_cast<const char *>(value) : "unavailable";
    };
#ifdef NDEBUG
    const char *build_mode = "NDEBUG";
#else
    const char *build_mode = "Debug/assertions";
#endif
#if defined(__VERSION__)
    const char *compiler = __VERSION__;
#elif defined(_MSC_VER)
    const char *compiler = "MSVC";
#else
    const char *compiler = "unavailable";
#endif
    std::printf("UI_BENCH_CONTEXT renderer=\"%s\" vendor=\"%s\" gl_version=\"%s\" compiler=\"%s\" "
                "build=\"%s\"\n",
                gl_string(GL_RENDERER), gl_string(GL_VENDOR), gl_string(GL_VERSION), compiler,
                build_mode);
    constexpr int warmup = 8, samples = 30;
    std::printf("UI_BENCH_CONFIG version=%s target=640x480 warmup=%d samples=%d "
                "timer=steady_clock_cpu gpu_time=not_measured allocations=not_instrumented\n",
                FAKE2D_VERSION_STRING, warmup, samples);
    return 0;
}

namespace {
fake2d::Engine *benchmark_engine = nullptr;
int counter_failures = 0;
void UI(benchmark::State &state, const std::string &scenario) {
    try {
    auto &renderer = benchmark_engine->GetRenderer();
    constexpr int warmup = 8;
    int failures = 0;
        fake2d::Input input;
        fake2d::Camera2D camera;
        camera.SetViewport(640, 480);
        fake2d::UISystem ui;
        ui.Bind(&input, &camera, &renderer);
        ui.SetAudit(true);
        const int target = renderer.GetRenderTargets().Create(640, 480);
        std::vector<std::string> ids;
        for (int i = 0; i < 1200; ++i)
            ids.push_back("bench/" + std::to_string(i));
        ui.ConfigureVariableList("rows", ids, 32, "bench");
        std::vector<double> layout, interaction, total, draw_calls, quads;
        std::size_t max_nodes = 0, max_visible = 0, max_commands = 0, max_steps = 0;
        std::size_t max_rich_batches = 0, max_rich_glyphs = 0;
        int min_rows = 100000, max_rows = 0;
        auto draw_frame = [&](int frame) {
            const auto start = std::chrono::steady_clock::now();
            input.InjectFrame({630, 470}, {}, {});
            ui.NewFrame();
            renderer.BeginFrame(0, 0, 0, 1);
            renderer.BeginRenderTarget(target);
            ui.BeginFrameUI();
            ui.RootBegin(10, 10, fake2d::UISize::Fixed(620), fake2d::UISize::Fixed(460));
            int rows = 0;
            if (scenario == "fixed_1000" || scenario == "variable_1200") {
                ui.ListView("rows", fake2d::UISize::Fixed(360));
            } else if (scenario == "rich_64_links") {
                ui.ScrollBoxBegin("rich", fake2d::UISize::Fixed(360));
                for (int i = 0; i < 64; ++i) {
                    ui.RichText("[color=#ffbb55][b]Rich 田亩[/b][/color] [link=link" +
                                    std::to_string(i) + "]Read[/link] price " +
                                    std::to_string(i * 12345),
                                .6f);
                    ++rows;
                }
                ui.ScrollBoxEnd();
            } else {
                ui.ScrollBoxBegin("outer", fake2d::UISize::Fixed(360));
                ui.SetScrollOffset("inner", static_cast<float>((frame + warmup) * 7));
                ui.VStackBegin(0, 4);
                ui.ThemedPanel();
                ui.Text("Measured nested auto panel", .6f);
                ui.ScrollBoxBegin("inner", fake2d::UISize::Fixed(200));
                for (int i = 0; i < 64; ++i) {
                    ui.Text("田亩 English population " + std::to_string(i) +
                                " LongUnbreakableLatinPopulation12345678901234567890",
                            .6f);
                    ++rows;
                }
                ui.ScrollBoxEnd();
                ui.StackEnd();
                ui.ScrollBoxEnd();
            }
            ui.RootEnd();
            ui.EndLayout();
            if (ui.ListViewBegin("rows")) {
                if (scenario == "fixed_1000") {
                    ui.SetScrollOffset("rows", static_cast<float>((frame + warmup) * 17));
                    auto range = ui.ListBegin("rows", 0, 1000, 24);
                    for (int i = 0; i < range.count; ++i) {
                        const int row = range.first + i;
                        ui.NextHeight(fake2d::UISize::Fixed(24));
                        ui.Button(ids[row], "田亩 population " + std::to_string(row));
                        ++rows;
                    }
                    ui.ListEnd();
                } else {
                    ui.SetScrollOffset("rows", static_cast<float>((frame + warmup) * 31));
                    ui.VariableListBegin("rows", 0);
                    for (int row = ui.VariableListNext(); row >= 0; row = ui.VariableListNext()) {
                        ui.Text(
                            row % 3 == 0
                                ? "能吏抚恤百姓修复水利田亩营制长句自动换行国朝纪事能吏抚恤百姓修复"
                                  "水利田亩营制长句自动换行"
                                : "LongUnbreakableLatinPopulation123456789012345678901234567890",
                            .55f);
                        ui.NextHeight(fake2d::UISize::Fixed(20));
                        ui.Button(ids[row], "row");
                        ui.VariableListRowEnd();
                        ++rows;
                    }
                    ui.VariableListEnd();
                }
                ui.ListViewEnd();
            }
            const bool balanced = ui.EndFrameUI();
            renderer.EndRenderTarget();
            renderer.EndFrame();
            const auto &stats = ui.Stats();
            const bool workload =
                balanced && ui.OverflowCount() == 0 &&
                (scenario == "fixed_1000" ? rows >= 15 && rows <= 16
                 : scenario == "variable_1200"
                     ? rows > 0 && rows < 20 &&
                           stats.measured_rows == static_cast<std::size_t>(rows) &&
                           stats.tree_steps < 2000
                     : scenario == "rich_64_links"
                         ? rows == 64 && stats.rich_batches <= 320 &&
                               renderer.DrawCallCount() <= 400 && stats.nodes == 66
                         : rows == 64);
            if (!workload) {
                ++failures;
                std::printf(
                    "UI_BENCH_FAILURE scenario=%s frame=%d rows=%d nodes=%zu steps=%zu audit=%zu\n",
                    scenario.c_str(), frame, rows, stats.nodes, stats.tree_steps,
                    ui.OverflowCount());
            }
            if (frame < 0)
                return;
            layout.push_back(stats.layout_ms);
            interaction.push_back(stats.interaction_ms);
            total.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count());
            draw_calls.push_back(static_cast<double>(renderer.DrawCallCount()));
            quads.push_back(static_cast<double>(renderer.QuadCount()));
            min_rows = std::min(min_rows, rows);
            max_rows = std::max(max_rows, rows);
            max_nodes = std::max(max_nodes, stats.nodes);
            max_visible = std::max(max_visible, stats.visible);
            max_commands = std::max(max_commands, stats.draw_commands);
            max_steps = std::max(max_steps, stats.tree_steps);
            max_rich_batches = std::max(max_rich_batches, stats.rich_batches);
            max_rich_glyphs = std::max(max_rich_glyphs, stats.rich_glyphs);
        };
        for (int frame = -warmup; frame < 0; ++frame)
            draw_frame(frame);
        int frame = 0;
        for (auto _ : state) {
            (void)_;
            draw_frame(frame++);
            benchmark::DoNotOptimize(max_nodes);
            benchmark::ClobberMemory();
            // Original steady-clock submission timer, not GPU execution time.
            state.SetIterationTime(total.back() / 1000.0);
        }
        auto mean = [](const std::vector<double> &values) {
            double sum = 0;
            for (double value : values)
                sum += value;
            return sum / values.size();
        };
        auto percentile = [](std::vector<double> values, double q) {
            std::sort(values.begin(), values.end());
            return values[static_cast<std::size_t>(q * (values.size() - 1))];
        };
        std::printf(
            "UI_BENCH scenario=%s layout_ms_mean=%.6f layout_ms_p50=%.6f layout_ms_p95=%.6f "
            "interaction_ms_mean=%.6f total_cpu_ms_mean=%.6f submitted_rows_min=%d "
            "submitted_rows_max=%d nodes_max=%zu visible_max=%zu draw_commands_max=%zu "
            "draw_calls_mean=%.1f quads_mean=%.1f tree_steps_max=%zu rich_batches_max=%zu rich_glyphs_max=%zu\n",
            scenario.c_str(), mean(layout), percentile(layout, .5), percentile(layout, .95),
            mean(interaction), mean(total), min_rows, max_rows, max_nodes, max_visible,
            max_commands, mean(draw_calls), mean(quads), max_steps, max_rich_batches, max_rich_glyphs);
        renderer.GetRenderTargets().Destroy(target);
    state.counters["submitted_rows_min"] = min_rows;
    state.counters["submitted_rows_max"] = max_rows;
    state.counters["nodes_max"] = static_cast<double>(max_nodes);
    state.counters["tree_steps_max"] = static_cast<double>(max_steps);
    state.counters["rich_batches_max"] = static_cast<double>(max_rich_batches);
    state.counters["draw_calls_mean"] = mean(draw_calls);
    state.counters["quads_mean"] = mean(quads);
    counter_failures += failures;
    if (failures)
        state.SkipWithError("original UI workload/counter gate failed (see scenario/frame log)");

    } catch (const std::exception &e) { ++counter_failures; state.SkipWithError(e.what()); }
    catch (...) { ++counter_failures; state.SkipWithError("unknown benchmark exception"); }
}
BENCHMARK_CAPTURE(UI, fixed_1000, "fixed_1000")->Iterations(30)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(UI, variable_1200, "variable_1200")->Iterations(30)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(UI, rich_64_links, "rich_64_links")->Iterations(30)->UseManualTime()->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(UI, nested_scroll_64, "nested_scroll_64")->Iterations(30)->UseManualTime()->Unit(benchmark::kMillisecond);
} // namespace

int main(int argc, char **argv) {
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    fake2d::EngineConfig cfg;
    // Exactly the old `fake2d_hello --headless --ui-bench` configuration.
    cfg.title="fake2d"; cfg.width=960; cfg.height=540; cfg.headless=true;
    const char *strict=std::getenv("FAKE2D_TEST_STRICT_ASSETS");
    cfg.strict_assets=strict && std::string_view(strict)=="1";
    fake2d::Engine engine;
    if (!engine.Init(cfg)) return 1;
    EnsureGameAudio(engine.GetAudio());
    if (SetupBenchmark(engine)) return 1;
    benchmark_engine = &engine;
    const auto selected = benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    std::printf("[ui-bench] %s: %zu workloads, %d counter failures\n",
                counter_failures ? "FAIL" : "PASS", selected, counter_failures);
    return counter_failures || selected == 0 ? 1 : 0;
}

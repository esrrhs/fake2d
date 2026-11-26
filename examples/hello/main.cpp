// Dedicated demo bootstrap, sharing unchanged private asset/launcher helpers.
#include "demo_support.h"

int main(int argc, char **argv) {
    fake2d::EngineConfig cfg;
    cfg.title = "fake2d";
    cfg.width = 960;
    cfg.height = 540;
    // Left empty on purpose: which script runs is decided after the parse
    // loop, so a bare windowed launch can open the demo launcher instead.
    cfg.script_entry.clear();

    bool map_demo = false;
    bool tile_demo = false;
    bool platformer_demo = false;
    bool audio_clips = false;
    bool mario_demo = false;
    bool slope_demo = false;
    bool path_demo = false;
    bool angry_demo = false;
    bool text_demo = false;
    bool beat_demo = false;
    bool shmup_demo = false;
    bool snooker_demo = false;
    bool contra_demo = false;
    bool city_demo = false;
    bool rts_demo = false;
    bool vn_demo = false;
    int max_frames = 0;
    int shot_frame = 30;
    int shot_min_colors = 0;
    std::string screenshot;
    bool regen_assets = false;
    bool generate_assets = false;
    bool launcher_mode = false;
    // Script backend: -1 lets the run decide (see the note where
    // cfg.script_jit is set), 0 forces the interpreter, 1 forces the GCC JIT.
    int jit_choice = -1;
    const fake2d::DemoInfo *demo_info = nullptr;
    // Host-side extras a demo needs before its script runs: the procedural
    // sprite sheets and synthesized sound sets that only the host can build.
    // Which script to load is *not* decided here — that stays in the catalogue.
    auto apply_demo_setup = [&](std::string_view flag) {
        if (flag == "angry-birds" || flag == "limbo") {
            angry_demo = true;  // limbo reuses the slingshot SFX set
        } else if (flag == "audio" || flag == "spatial") {
            audio_clips = true;
        } else if (flag == "map") {
            map_demo = true;
        } else if (flag == "tile") {
            tile_demo = true;
        } else if (flag == "platformer") {
            platformer_demo = true;
        } else if (flag == "mario") {
            mario_demo = true;
        } else if (flag == "slope") {
            slope_demo = true;
        } else if (flag == "path") {
            path_demo = true;
        } else if (flag == "text") {
            text_demo = true;
        } else if (flag == "beat-em-up") {
            beat_demo = true;
        } else if (flag == "shmup") {
            shmup_demo = true;
        } else if (flag == "snooker") {
            snooker_demo = true;
        } else if (flag == "contra") {
            contra_demo = true;
        } else if (flag == "city") {
            city_demo = true;
        } else if (flag == "rts") {
            rts_demo = true;
        } else if (flag == "vn") {
            vn_demo = true;
        }
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::printf(
                "Fake2D " FAKE2D_VERSION_STRING
                "\nUsage: fake2d_hello [--headless] [--frames N] [--<name>-demo]\n"
                "  --ui-demo     Box text, fixed/flex columns, auto-height scroll (1.47 breaking "
                "UI API)\n"
                "  --ui2-demo    Box-scoped progress bars, radio groups and combos\n"
                "  --ui3-demo    Box-scoped windows, tabs and trees\n"
                "  --ui-variable-demo  Keyed mixed-height rows, anchors, UTF-8 history/password\n"
                "  --ui-layout-demo  Dynamic text/number/language/resize acceptance scenarios\n"
                "  --ui-features-demo  Containers, skin, rich input, focus/menu/modal, virtual "
                "list\n"
                "  --entry PATH  Run a Lua script; UI migration reference: docs/SCRIPTING.md#ui\n"
                "Demos:\n");
            for (const auto &demo : fake2d::kDemos) std::printf("  --%s-demo\n", demo.flag);
            return 0;
        }

        if (arg.ends_with("-test") || arg=="--bench" || arg=="--phys-bench" ||
            arg=="--light-bench" || arg=="--ui-bench" || arg=="--ui-stress" ||
            arg=="--ui-stress-seed") {
            std::fprintf(stderr,"Test/performance modes moved to fake2d_engine_tests / fake2d_benchmarks / fake2d_ui_benchmark.\n");
            return 2;
        }
        if (arg == "--headless") {
            cfg.headless = true;
        } else if (arg == "--ai") {
            cfg.ai_input = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            max_frames = std::atoi(argv[++i]);
        } else if (arg == "--hot-reload") {
            cfg.hot_reload = true;
        } else if (arg == "--launcher") {
            // Interactive demo picker.
            launcher_mode = true;
        } else if (arg == "--interp") {
            // Skip the JIT compile and call the script through the VM.
            jit_choice = 0;
        } else if (arg == "--jit") {
            jit_choice = 1;
        }  else if (const DemoArg kind = ParseDemoArg(arg, &demo_info);
                   kind != DemoArg::kNotADemoFlag) {
            // `--<flag>-demo`: the catalogue owns the entry script, so the
            // command line and the launcher menu can never disagree about what
            // is startable, and an unlisted flag is an error instead of an
            // argument silently ignored.
            if (kind == DemoArg::kUnknownDemo) {
                std::fprintf(stderr, "fake2d_hello: unknown demo flag '%.*s'\n",
                             static_cast<int>(arg.size()), arg.data());
                return 1;
            }
            cfg.script_entry = demo_info->script;
            apply_demo_setup(demo_info->flag);
        } else if (arg == "--screenshot" && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (arg == "--shot-frame" && i + 1 < argc) {
            shot_frame = std::atoi(argv[++i]);
        } else if (arg == "--shot-min-colors" && i + 1 < argc) {
            // CI guard: a screenshot with fewer sampled colours than this is
            // treated as blank (exit 2) — catches "saved but drew nothing".
            shot_min_colors = std::atoi(argv[++i]);
        } else if (arg == "--regen-assets") {
            // Rewrite the generated sheets/levels even if they exist, so
            // painter or layout edits actually reach the screen.
            regen_assets = true;
        } else if (arg == "--strict-assets") {
            // Packaged-build gate: missing textures are errors, and the host
            // must not paint or synthesize replacements at first run.
            cfg.strict_assets = true;
        } else if (arg == "--generate-assets") {
            // One pass that materializes the whole first-run asset tree.
            // Packaging needs every file up front instead of relying on the
            // demos to fill gaps in when they first run.
            generate_assets = true;
        }                                   else if (arg == "--record" && i + 1 < argc) {
            // Capture the Input snapshot stream to an F2IR fixture.
            cfg.record_path = argv[++i];
        } else if (arg == "--replay" && i + 1 < argc) {
            // Drive Input from an F2IR fixture instead of the device.
            cfg.replay_path = argv[++i];
        }   else if (arg == "--rng-seed" && i + 1 < argc) {
            // Pin Engine::GetRandom() at Init for reproducible runs / replays.
            cfg.pin_rng = true;
            cfg.rng_seed = static_cast<std::uint64_t>(
                std::strtoull(argv[++i], nullptr, 0));
        } else if (arg == "--fixed-hz" && i + 1 < argc) {
            // Physics rate; 0 (or --variable-step) restores variable stepping.
            const int hz = std::atoi(argv[++i]);
            cfg.fixed_dt = hz > 0 ? 1.0f / static_cast<float>(hz) : 0.0f;
        } else if (arg == "--variable-step") {
            cfg.fixed_dt = 0.0f;
        } else if (arg == "--fixed-sim-dt") {
            // Headless: advance one fixed_dt of gameplay per frame, so a
            // `--frames N` budget is a host-independent time budget.
            cfg.fixed_sim_dt = true;
        } else if (arg == "--entry" && i + 1 < argc) {
            // Dev/test hook: run an arbitrary script instead of a demo.
            cfg.script_entry = argv[++i];
        }
    }

    if (generate_assets) {
        // Materialize every generated sheet, map, language pack and clip the
        // demos look for before they start. No engine or GL context needed:
        // these painters only write files, which keeps the packaging job
        // cheap and lets every platform ship the same tree.
        EnsureGameAssets();
        EnsureMarioAssets(false);
        EnsureSlopeAssets(false);
        EnsurePathAssets(false);
        EnsureMapAssets();
        EnsureTileAnimAssets();
        EnsurePlatformerAssets();
        EnsureBeatEmUpAssets();
        EnsureSnookerAssets();
        EnsureContraAssets();
        EnsureCityAssets(false);
        EnsureRtsAssets(false);
        EnsureVnAssets(false);
        EnsureI18nAssets();
        std::printf("fake2d_hello: generated assets under assets/\n");
        return 0;
    }

    if (launcher_mode) {
        cfg.script_entry = "scripts/launcher.lua";
    } else if (cfg.script_entry.empty()) {
        // A bare windowed run opens the picker; a bare headless run keeps the
        // historical Breakout default so existing smoke tests are unaffected.
        cfg.script_entry =
            cfg.headless ? "scripts/game.lua" : "scripts/launcher.lua";
    }

    // A demo is looked at, not profiled. The GCC JIT spends seconds turning a
    // script into native code before the first frame, so a windowed demo or
    // picker run starts on the interpreter instead — same program, called
    // through the VM. A headless run keeps the JIT, because that is the backend
    // CI has always exercised, and `--jit` / `--interp` override either way.
    cfg.script_jit = jit_choice != 0 &&
                     (jit_choice > 0 || cfg.headless ||
                      (!launcher_mode && demo_info == nullptr));

    // The engine auto-loads <resource root>/assets/lang at Init, so the demo
    // language packs must exist before the engine starts.
    if (text_demo && !cfg.strict_assets) {
        EnsureI18nAssets();
    }

    fake2d::Engine engine;
    if (!engine.Init(cfg)) {
        std::fprintf(stderr, "fake2d_hello: engine init failed\n");
        return 1;
    }

    EnsureGameAudio(engine.GetAudio());


    // All gameplay is pure Lua below; C++ only guarantees the binary asset
    // scaffolding exists before the script's first frame. A packaged run
    // passes --strict-assets: the package must ship every file, so generating
    // a missing one here would mask exactly the defect the gate catches.
    if (!cfg.strict_assets) {
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
        }
        if (beat_demo) {
            EnsureBeatEmUpAssets();
        }
        if (snooker_demo) {
            EnsureSnookerAssets();
        }
        if (contra_demo) {
            EnsureContraAssets();
        }
        if (city_demo) {
            EnsureCityAssets(regen_assets);
        }
        if (rts_demo) {
            EnsureRtsAssets(regen_assets);
        }
        if (vn_demo) {
            // The AI-illustrated VN art ships under assets/vn; only fill in a
            // missing file with the procedural fallback (never overwrite it).
            EnsureVnAssets(false);
        }
    }
    if (platformer_demo) {
        engine.GetAudio().AddClipWav("music", "assets/music.wav");
    }
    if (beat_demo) {
        EnsureBeatEmUpAudio(engine.GetAudio());
    }
    if (shmup_demo) {
        EnsureShmupAudio(engine.GetAudio());
    }
    if (snooker_demo) {
        EnsureSnookerAudio(engine.GetAudio());
    }
    if (contra_demo) {
        EnsureContraAudio(engine.GetAudio());
    }
    if (city_demo) {
        EnsureCityAudio(engine.GetAudio());
    }
    if (rts_demo) {
        EnsureRtsAudio(engine.GetAudio());
    }
    if (vn_demo) {
        EnsureVnAudio(engine.GetAudio());
    }
    if (angry_demo) {
        // Launch / impact / pop / win / lose clips come from the same
        // synthesized set the breakout sample uses.
        EnsureGameAudio(engine.GetAudio());
    }
    if (audio_clips) {
        // SFX come from EnsureGameAudio; the two looping themes are
        // synthesized so the cross-fade needs no binary asset.
        EnsureGameAudio(engine.GetAudio());
        EnsureAudioDemoMusic(engine.GetAudio());
    }
    // Generic host-level frame hook: the `--ai` key rhythm and screenshots
    // (visual regression; neither is game logic).
    bool shot_blank = false;
    const bool ai_injects_keys =
        cfg.ai_input &&
        (demo_info == nullptr || !AiScriptDriven(demo_info->flag));
    if (ai_injects_keys || !screenshot.empty()) {
        engine.SetFrameCallback([&](fake2d::Engine &e) {
            // Runs after this frame's script update, so what the rhythm sets
            // here is what the script sees on the next one -- one frame of
            // latency, which is what a human's finger costs too.
            if (ai_injects_keys) {
                AiRhythm(e.GetInput(), e.FrameIndex(),
                         static_cast<float>(e.WindowWidth()),
                         static_cast<float>(e.WindowHeight()));
            }
            if (!screenshot.empty() &&
                e.FrameIndex() == static_cast<std::uint64_t>(shot_frame)) {
                e.GetRenderer().GetSpriteBatch().Flush();
                const bool saved = e.GetRenderer().SaveScreenshot(screenshot);
                if (saved && shot_min_colors > 0) {
                    const int colors = e.GetRenderer().DistinctFramebufferColors();
                    std::printf("fake2d_screenshot_colors=%d\n", colors);
                }
            }
        });
    }

    int rc = engine.Run(max_frames);

    // Strict package gate: a run that could not load a shipped texture is a
    // failure even if the demo script painted a substitute and exited cleanly.
    if (cfg.strict_assets) {
        const std::size_t failed = engine.FailedAssetLoads();
        if (failed > 0) {
            std::fprintf(stderr,
                         "fake2d_hello: strict assets: %zu texture load(s) "
                         "failed\n",
                         failed);
            if (rc == 0) {
                rc = 3;
            }
        }
    }

    // The launcher picked a demo: run it in a child, wait for it to close, then
    // come back to a fresh picker. A headless run only reports the request, so
    // a CI smoke test cannot spawn an unbounded second session.
    if (const std::string chosen = engine.PendingRelaunch(); !chosen.empty()) {
        if (cfg.headless) {
            std::printf("fake2d_hello: would restart with --%s-demo\n",
                        chosen.c_str());
            return shot_blank ? 2 : rc;
        }
        if (!RunDemoAsChild(argv[0], chosen, max_frames, jit_choice,
                            cfg.ai_input)) {
            std::fprintf(stderr,
                         "fake2d_hello: could not start '%s'; run: %s --%s-demo\n",
                         chosen.c_str(), argv[0], chosen.c_str());
            return 1;
        }
        if (RelaunchAsLauncher(argv[0], max_frames, jit_choice, cfg.ai_input)) {
            return rc;  // only reached if the exec failed to replace the image
        }
        std::fprintf(stderr, "fake2d_hello: could not return to the launcher; run: %s\n",
                     argv[0]);
        return rc;
    }
    return shot_blank ? 2 : rc;
}

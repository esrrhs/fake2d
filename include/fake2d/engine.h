#pragma once

#include "fake2d/animation.h"
#include "fake2d/achievements.h"
#include "fake2d/audio.h"
#include "fake2d/entity_store.h"
#include "fake2d/i18n.h"
#include "fake2d/input.h"
#include "fake2d/mod.h"
#include "fake2d/particle.h"
#include "fake2d/physics.h"
#include "fake2d/renderer.h"
#include "fake2d/resource_manager.h"
#include "fake2d/rng.h"
#include "fake2d/save_store.h"
#include "fake2d/tilemap.h"
#include "fake2d/ui.h"
#include "fake2d/version.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace fake2d {

class Engine;

struct EngineConfig {
    std::string title = "fake2d";
    int width = 1280;
    int height = 720;
    bool vsync = true;
    bool headless = false;
    /// Start resizable; games can toggle it later with SetWindowResizable().
    bool resizable = true;
    /// Minimum user-resizable size (0 = no constraint).
    int min_width = 0;
    int min_height = 0;
    /// Relative path to the FakeLua entry script (e.g. scripts/game.lua).
    std::string script_entry = "scripts/game.lua";
    /// Watch the entry script's mtime and hot-reload it on change.
    bool hot_reload = false;
    /// Directory holding save slots. Empty (the default) resolves to
    /// `AppPaths::SavesDir()` — the per-user save directory — so games write
    /// where the platform expects. Set an explicit path for portable builds.
    std::string save_dir;
    /// Directory with `<code>.tsv` language packs. Empty (the default) tries
    /// `<resource root>/assets/lang` when that directory exists.
    std::string localization_dir;
    /// Language activated after the packs load (empty = the first pack).
    std::string language = "en";
    /// Fixed physics timestep in seconds. Physics advances in whole steps of
    /// this size (accumulator + max_physics_steps guard), so simulation
    /// results never depend on the frame rate. `<= 0` restores the legacy
    /// single variable-dt step per frame.
    float fixed_dt = 1.0f / 60.0f;
    /// Upper bound of fixed steps simulated in one frame (spiral-of-death
    /// guard). Leftover backlog is dropped, never carried over.
    int max_physics_steps = 5;
    /// Optional per-frame C++ hook, invoked after the script update and
    /// before the frame is flushed (e.g. to draw a scene graph).
    std::function<void(Engine &)> on_frame;
};

/// Host-owned engine: window + renderer + FakeLua state.
/// Game data lives in C++; scripts orchestrate per-frame logic then Reset().
class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    /// Create window, GL context, FakeLua state, compile entry script.
    bool Init(const EngineConfig &config);

    /// Run until the window closes or max_frames is reached (0 = run indefinitely).
    int Run(int max_frames = 0);

    void Shutdown();

    /// Set/replace the per-frame C++ hook (usable any time, including after Init).
    void SetFrameCallback(std::function<void(Engine &)> callback);

    // --- window control (safe no-ops in headless runs) ---
    /// Ask the host window to close after this frame; ends Run().
    void RequestQuit();
    /// Replace the OS title bar text.
    void SetWindowTitle(std::string_view title);
    /// Enter/leave borderless fullscreen on the primary monitor.
    void SetFullscreen(bool fullscreen);
    [[nodiscard]] bool IsFullscreen() const;
    [[nodiscard]] int WindowWidth() const;
    [[nodiscard]] int WindowHeight() const;
    /// Resize the OS window (logical points); the renderer follows on the
    /// next frame through the normal resize path.
    void SetWindowSize(int width, int height);
    void SetWindowResizable(bool resizable);
    [[nodiscard]] bool WindowResizable() const;
    void SetMinWindowSize(int width, int height);
    /// Cursor shape: 0 arrow, 1 ibeam, 2 crosshair, 3 hand, 4 hresize,
    /// 5 vresize.
    void SetCursorShape(int shape);

    [[nodiscard]] bool IsRunning() const;
    [[nodiscard]] double DeltaTime() const;
    /// Seconds since Init (sum of frame deltas).
    [[nodiscard]] double TimeElapsed() const;
    [[nodiscard]] std::uint64_t FrameIndex() const;

    /// Physics timestep actually used: the configured fixed step, or the
    /// frame delta when fixed stepping is disabled.
    [[nodiscard]] double PhysicsDt() const;
    /// Number of physics steps simulated during the current frame (0 when the
    /// accumulator had not reached a full step yet).
    [[nodiscard]] int PhysicsStepsLastFrame() const;
    /// Render interpolation factor [0,1] between the last two physics steps.
    [[nodiscard]] double InterpAlpha() const;

    Renderer &GetRenderer();
    [[nodiscard]] const Renderer &GetRenderer() const;

    /// Shared handle-based texture/atlas pool.
    ResourceManager &GetResources();
    [[nodiscard]] const ResourceManager &GetResources() const;

    /// Per-frame input snapshot, refreshed after event polling each frame.
    const Input &GetInput() const;
    /// Mutable handle for synthetic frames: `Input::InjectFrame` lets
    /// headless tests (and input replays) drive widgets that would otherwise
    /// need a real mouse and keyboard.
    Input &GetInput();

    /// Best-effort one-shot sound effect player (disabled on audio-less hosts).
    AudioEngine &GetAudio();
    [[nodiscard]] const AudioEngine &GetAudio() const;

    /// CPU particle pool, updated once per frame before the script runs.
    ParticleSystem &GetParticles();
    [[nodiscard]] const ParticleSystem &GetParticles() const;

    /// Built-in 2D physics world (auto-stepped once per frame).
    PhysicsWorld &GetPhysics();
    [[nodiscard]] const PhysicsWorld &GetPhysics() const;

    /// Frame-animation clip pool (auto-advanced once per frame).
    AnimationSystem &GetAnimations();
    [[nodiscard]] const AnimationSystem &GetAnimations() const;

    /// Loaded Tiled tile maps.
    TilemapLibrary &GetTilemaps();
    [[nodiscard]] const TilemapLibrary &GetTilemaps() const;

    /// Anchored UI widgets; click/hover state is refreshed per frame.
    UISystem &GetUI();
    [[nodiscard]] const UISystem &GetUI() const;

    /// Persistent save-slot key/value store (see save_dir).
    SaveStore &GetSaves();
    [[nodiscard]] const SaveStore &GetSaves() const;

    /// Runtime localization table (language packs + instant switching).
    I18n &GetI18n();
    [[nodiscard]] const I18n &GetI18n() const;

    /// Local achievement mirror with Steam sync (see achievements.h).
    Achievements &GetAchievements();
    [[nodiscard]] const Achievements &GetAchievements() const;

    /// Data-pack mod discovery/ordering (see mod.h).
    ModManager &GetMods();
    [[nodiscard]] const ModManager &GetMods() const;

    /// Deterministic random stream (seed it from a save for reproducible runs).
    Random &GetRandom();
    [[nodiscard]] const Random &GetRandom() const;

    /// Smoothed frames per second and the last frame's duration in ms.
    [[nodiscard]] double Fps() const;
    [[nodiscard]] double FrameMs() const;

    /// C++-owned fixed-slot entity database for scripts.
    EntityStore &GetEntities();
    [[nodiscard]] const EntityStore &GetEntities() const;

    [[nodiscard]] const std::string &SaveDir() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fake2d

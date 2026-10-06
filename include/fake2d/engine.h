#pragma once

#include "fake2d/animation.h"
#include "fake2d/audio.h"
#include "fake2d/entity_store.h"
#include "fake2d/input.h"
#include "fake2d/particle.h"
#include "fake2d/physics.h"
#include "fake2d/renderer.h"
#include "fake2d/resource_manager.h"
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
    /// Relative path to the FakeLua entry script (e.g. scripts/game.lua).
    std::string script_entry = "scripts/game.lua";
    /// Watch the entry script's mtime and hot-reload it on change.
    bool hot_reload = false;
    /// Directory (relative to the working directory) holding save slot JSONs.
    std::string save_dir = "saves";
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

    [[nodiscard]] bool IsRunning() const;
    [[nodiscard]] double DeltaTime() const;
    /// Seconds since Init (sum of frame deltas).
    [[nodiscard]] double TimeElapsed() const;
    [[nodiscard]] std::uint64_t FrameIndex() const;

    Renderer &GetRenderer();
    [[nodiscard]] const Renderer &GetRenderer() const;

    /// Shared handle-based texture/atlas pool.
    ResourceManager &GetResources();
    [[nodiscard]] const ResourceManager &GetResources() const;

    /// Per-frame input snapshot, refreshed after event polling each frame.
    const Input &GetInput() const;

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

    /// C++-owned fixed-slot entity database for scripts.
    EntityStore &GetEntities();
    [[nodiscard]] const EntityStore &GetEntities() const;

    [[nodiscard]] const std::string &SaveDir() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fake2d

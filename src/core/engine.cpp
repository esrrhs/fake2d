#include "fake2d/engine.h"
#include "fake2d/app_paths.h"
#include "fake2d/renderer.h"
#include "fake2d/script_host.h"

#include "platform/window.h"

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace fake2d {

struct Engine::Impl {
    EngineConfig config;
    platform::Window window;
    Renderer renderer;
    ScriptHost script;
    ResourceManager resources;
    Input input;
    AudioEngine audio;
    ParticleSystem particles;
    PhysicsWorld physics;
    AnimationSystem animations;
    SkeletonSystem skeletons;
    TilemapLibrary tilemaps;
    UISystem ui;
    SaveStore saves;
    EntityStore entities;
    I18n i18n;
    Achievements achievements;
    ModManager mods;
    Random random;
    bool running = false;
    /// Gameplay delta handed to scripts/systems (after time scale + hitstop).
    double delta_time = 0.0;
    double elapsed = 0.0;
    /// Global gameplay speed multiplier; hitstop temporarily forces it to 0.
    float time_scale = 1.0f;
    /// Remaining hit-stop time in real seconds.
    double hitstop_remaining = 0.0;
    double fps_smoothed = 0.0;
    double frame_ms = 0.0;
    std::uint64_t frame_index = 0;
    // Fixed-step accumulator: physics advances in whole `fixed_dt` steps so
    // simulation results never depend on the frame rate. `step_accumulator`
    // carries the leftover time into the next frame.
    double step_accumulator = 0.0;
    int last_physics_steps = 0;
    float interp_alpha = 1.0f;
    /// Demo the launcher asked to start next; consumed by the host entry point.
    std::string pending_relaunch;
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}

Engine::~Engine() {
    Shutdown();
}

bool Engine::Init(const EngineConfig &config) {
    impl_->config = config;
    if (impl_->config.save_dir.empty()) {
        // Platform-correct default instead of a CWD-relative "saves" folder.
        impl_->config.save_dir = AppPaths::SavesDir();
    }

    platform::WindowDesc desc;
    desc.title = impl_->config.title.c_str();
    desc.width = impl_->config.width;
    desc.height = impl_->config.height;
    desc.vsync = impl_->config.vsync;
    desc.headless = impl_->config.headless;
    desc.resizable = impl_->config.resizable;

    if (!impl_->window.Create(desc)) {
        return false;
    }
    if (impl_->config.min_width > 0 || impl_->config.min_height > 0) {
        impl_->window.SetMinSize(impl_->config.min_width,
                                 impl_->config.min_height);
    }
    if (!impl_->renderer.Init(impl_->window.Width(), impl_->window.Height())) {
        return false;
    }
    impl_->renderer.Resize(impl_->window.FramebufferWidth(),
                           impl_->window.FramebufferHeight(),
                           impl_->window.Width(), impl_->window.Height(),
                           impl_->window.ContentScale());
    impl_->input.Attach(impl_->window.Handle());
    if (!impl_->script.Init()) {
        return false;
    }
    impl_->script.BindEngine(this);

    // Audio is optional: a headless CI box without a device simply disables
    // playback and every Play() becomes a no-op.
    impl_->audio.Init();

    // UI reads the input snapshot and viewport every frame.
    impl_->ui.Bind(&impl_->input, &impl_->renderer.GetCamera(), &impl_->renderer);

    // Gameplay reads contacts once per frame, but the fixed-step loop may
    // step the world several times in a frame: accumulate so no event is
    // dropped between reads (cleared at the top of each frame).
    impl_->physics.SetEventAccumulation(true);

    // Optional localization: an explicit directory wins, otherwise a packaged
    // <resource root>/assets/lang is picked up automatically.
    std::string language_dir = impl_->config.localization_dir;
    if (language_dir.empty()) {
        std::error_code ec;
        const std::string candidate = AppPaths::Resolve("assets/lang");
        if (std::filesystem::is_directory(candidate, ec)) {
            language_dir = candidate;
        }
    }
    if (!language_dir.empty()) {
        impl_->i18n.LoadDirectory(language_dir, impl_->config.language);
    }

#if defined(FAKE2D_WITH_FAKELUA) && FAKE2D_WITH_FAKELUA
    if (!impl_->config.script_entry.empty()) {
        if (!impl_->script.CompileFile(impl_->config.script_entry)) {
            std::fprintf(stderr, "fake2d: warning: script entry compile failed, continuing without scripts\n");
        }
    }
#endif

    impl_->running = true;
    return true;
}

int Engine::Run(int max_frames) {
    if (!impl_->running) {
        return 1;
    }

    using clock = std::chrono::steady_clock;
    auto prev = clock::now();

    while (!impl_->window.ShouldClose()) {
        if (max_frames > 0 && impl_->frame_index >= static_cast<std::uint64_t>(max_frames)) {
            break;
        }
        const auto now = clock::now();
        const double real_dt = std::chrono::duration<double>(now - prev).count();
        prev = now;
        impl_->frame_ms = real_dt * 1000.0;
        if (real_dt > 0.0) {
            const double fps = 1.0 / real_dt;
            // ~10-frame smoothing so a single hitch does not make the read-out
            // jump around; the first frame seeds the average.
            impl_->fps_smoothed = impl_->fps_smoothed <= 0.0
                                      ? fps
                                      : impl_->fps_smoothed * 0.9 + fps * 0.1;
        }
        // Gameplay time: a hitstop burns real time while forcing dt to 0; otherwise
        // the configured time scale multiplies the frame delta. Everything that
        // auto-advances (physics, animations, particles, tiles, camera effects,
        // scripts) reads this value, so the whole world freezes together.
        double scaled_dt = real_dt * static_cast<double>(impl_->time_scale);
        if (impl_->hitstop_remaining > 0.0) {
            impl_->hitstop_remaining -= real_dt;
            if (impl_->hitstop_remaining < 0.0) {
                impl_->hitstop_remaining = 0.0;
            }
            scaled_dt = 0.0;
        }
        impl_->delta_time = scaled_dt;

        impl_->window.PollEvents();
        if (impl_->window.RefreshSize()) {
            // Live window resize / monitor DPI change (e.g. dragged between
            // Retina and external displays).
            impl_->renderer.Resize(impl_->window.FramebufferWidth(),
                                   impl_->window.FramebufferHeight(),
                                   impl_->window.Width(), impl_->window.Height(),
                                   impl_->window.ContentScale());
        }
        impl_->input.NewFrame(impl_->window.Handle());
        impl_->ui.NewFrame();
        impl_->elapsed += impl_->delta_time;
        // Custom shaders that declare `u_time` are stamped from this, so an
        // animated pass advances on its own instead of every script feeding
        // the uniform by hand.
        impl_->renderer.SetTime(static_cast<float>(impl_->elapsed));

        // Advance gameplay systems; scripts read results / emit per frame.
        impl_->renderer.GetCamera().UpdateEffects(static_cast<float>(impl_->delta_time));
        impl_->particles.Update(static_cast<float>(impl_->delta_time));
        impl_->animations.Update(static_cast<float>(impl_->delta_time));
        // Skeletons ride the same clock, so a rig and a frame clip on
        // the same character cannot drift apart over a long session.
        impl_->skeletons.Update(static_cast<float>(impl_->delta_time));
        // Animated tiles ride their own clock, like the animator and unlike
        // a script-fed uniform: water and torches loop with no bookkeeping.
        impl_->tilemaps.AdvanceAll(static_cast<float>(impl_->delta_time));

        // Physics: fixed-step accumulator (config.fixed_dt <= 0 keeps the
        // legacy single variable-dt step). Frames that do not reach a whole
        // step still refresh the contact buffers with a zero-dt pass, so
        // gameplay never sees a stale or replayed event set. Events raised by
        // every step of this frame accumulate until the next frame's clear,
        // so a multi-step frame never hides a began/ended from gameplay.
        impl_->physics.ClearFrameEvents();
        if (impl_->config.fixed_dt > 0.0f) {
            const double fixed = static_cast<double>(impl_->config.fixed_dt);
            // A stall (tab suspended, breakpoint) must not dump seconds of
            // simulation into one frame.
            impl_->step_accumulator += impl_->delta_time > 0.25 ? 0.25 : impl_->delta_time;
            int steps = 0;
            while (impl_->step_accumulator >= fixed &&
                   steps < impl_->config.max_physics_steps) {
                impl_->physics.Step(static_cast<float>(fixed));
                impl_->step_accumulator -= fixed;
                ++steps;
            }
            if (steps == 0) {
                impl_->physics.Step(0.0f);
            } else if (steps == impl_->config.max_physics_steps) {
                // Spiral-of-death guard: drop the backlog instead of
                // accumulating debt we can never pay back.
                impl_->step_accumulator = 0.0;
            }
            impl_->last_physics_steps = steps;
            impl_->interp_alpha = static_cast<float>(impl_->step_accumulator / fixed);
            impl_->physics.SetInterpAlpha(impl_->interp_alpha);
        } else {
            impl_->physics.Step(static_cast<float>(impl_->delta_time));
            impl_->last_physics_steps = 1;
            impl_->interp_alpha = 1.0f;
            impl_->physics.SetInterpAlpha(1.0f);
        }

        // Clear color cycles slightly so the skeleton window is visibly alive.
        const float t = static_cast<float>(impl_->frame_index) * 0.01f;
        const float r = 0.08f + 0.02f * (t - static_cast<int>(t));
        impl_->renderer.BeginFrame(r, 0.10f, 0.14f);

        // Script update then arena reset — FakeLua per-frame contract.
        impl_->script.CallUpdate(impl_->delta_time);
        impl_->script.ResetFrame();

        if (impl_->config.hot_reload) {
            impl_->script.PollHotReload();
        }
        if (impl_->config.on_frame) {
            impl_->config.on_frame(*this);
        }

        impl_->renderer.EndFrame();
        impl_->window.SwapBuffers();

        ++impl_->frame_index;
    }

    Shutdown();
    return 0;
}

double Engine::PhysicsDt() const {
    return impl_->config.fixed_dt > 0.0f
               ? static_cast<double>(impl_->config.fixed_dt)
               : impl_->delta_time;
}

int Engine::PhysicsStepsLastFrame() const {
    return impl_->last_physics_steps;
}

double Engine::InterpAlpha() const {
    return static_cast<double>(impl_->interp_alpha);
}

void Engine::Shutdown() {
    if (!impl_) {
        return;
    }
    impl_->running = false;
    impl_->particles.Clear();
    impl_->physics.Clear();
    impl_->animations.Clear();
    impl_->skeletons.Clear();
    impl_->tilemaps.Clear();
    impl_->audio.Shutdown();
    impl_->resources.Clear();
    impl_->script.Shutdown();
    impl_->renderer.Shutdown();
    impl_->window.Destroy();
}

void Engine::SetFrameCallback(std::function<void(Engine &)> callback) {
    impl_->config.on_frame = std::move(callback);
}

void Engine::RequestQuit() {
    impl_->window.SetShouldClose(true);
}

void Engine::RequestRelaunch(std::string_view demo) {
    impl_->pending_relaunch = std::string(demo);
    impl_->window.SetShouldClose(true);
}

std::string Engine::PendingRelaunch() const {
    return impl_->pending_relaunch;
}

void Engine::SetWindowTitle(std::string_view title) {
    impl_->window.SetTitle(std::string(title).c_str());
}

void Engine::SetFullscreen(bool fullscreen) {
    impl_->window.SetFullscreen(fullscreen);
}

bool Engine::IsFullscreen() const {
    return impl_ && impl_->window.IsFullscreen();
}

int Engine::WindowWidth() const {
    return impl_ ? impl_->window.Width() : 0;
}

int Engine::WindowHeight() const {
    return impl_ ? impl_->window.Height() : 0;
}

void Engine::SetWindowSize(int width, int height) {
    if (impl_) {
        impl_->window.SetSize(width, height);
    }
}

void Engine::SetWindowResizable(bool resizable) {
    if (impl_) {
        impl_->window.SetResizable(resizable);
    }
}

bool Engine::WindowResizable() const {
    return impl_ && impl_->window.Resizable();
}

void Engine::SetMinWindowSize(int width, int height) {
    if (impl_) {
        impl_->window.SetMinSize(width, height);
    }
}

void Engine::SetCursorShape(int shape) {
    if (impl_) {
        impl_->window.SetCursorShape(shape);
    }
}

bool Engine::IsRunning() const {
    return impl_ && impl_->running;
}

double Engine::DeltaTime() const {
    return impl_ ? impl_->delta_time : 0.0;
}

double Engine::TimeElapsed() const {
    return impl_ ? impl_->elapsed : 0.0;
}

void Engine::SetTimeScale(float scale) {
    if (!impl_) {
        return;
    }
    impl_->time_scale = std::clamp(scale, 0.0f, 100.0f);
}

float Engine::TimeScale() const {
    return impl_ ? impl_->time_scale : 1.0f;
}

void Engine::Hitstop(float seconds) {
    if (!impl_ || seconds <= 0.0f) {
        return;
    }
    // Overlapping impacts extend the freeze instead of shortening it.
    impl_->hitstop_remaining =
        std::max(impl_->hitstop_remaining, static_cast<double>(seconds));
}

const Input &Engine::GetInput() const {
    return impl_->input;
}

Input &Engine::GetInput() {
    return impl_->input;
}

std::uint64_t Engine::FrameIndex() const {
    return impl_ ? impl_->frame_index : 0;
}

Renderer &Engine::GetRenderer() {
    return impl_->renderer;
}

const Renderer &Engine::GetRenderer() const {
    return impl_->renderer;
}

ResourceManager &Engine::GetResources() {
    return impl_->resources;
}

const ResourceManager &Engine::GetResources() const {
    return impl_->resources;
}

AudioEngine &Engine::GetAudio() {
    return impl_->audio;
}

const AudioEngine &Engine::GetAudio() const {
    return impl_->audio;
}

ParticleSystem &Engine::GetParticles() {
    return impl_->particles;
}

const ParticleSystem &Engine::GetParticles() const {
    return impl_->particles;
}

PhysicsWorld &Engine::GetPhysics() {
    return impl_->physics;
}

const PhysicsWorld &Engine::GetPhysics() const {
    return impl_->physics;
}

SkeletonSystem &Engine::GetSkeletons() {
    return impl_->skeletons;
}

const SkeletonSystem &Engine::GetSkeletons() const {
    return impl_->skeletons;
}

AnimationSystem &Engine::GetAnimations() {
    return impl_->animations;
}

const AnimationSystem &Engine::GetAnimations() const {
    return impl_->animations;
}

TilemapLibrary &Engine::GetTilemaps() {
    return impl_->tilemaps;
}

const TilemapLibrary &Engine::GetTilemaps() const {
    return impl_->tilemaps;
}

UISystem &Engine::GetUI() {
    return impl_->ui;
}

const UISystem &Engine::GetUI() const {
    return impl_->ui;
}

SaveStore &Engine::GetSaves() {
    return impl_->saves;
}

I18n &Engine::GetI18n() {
    return impl_->i18n;
}

const I18n &Engine::GetI18n() const {
    return impl_->i18n;
}

Achievements &Engine::GetAchievements() {
    return impl_->achievements;
}

const Achievements &Engine::GetAchievements() const {
    return impl_->achievements;
}

ModManager &Engine::GetMods() {
    return impl_->mods;
}

const ModManager &Engine::GetMods() const {
    return impl_->mods;
}

Random &Engine::GetRandom() {
    return impl_->random;
}

const Random &Engine::GetRandom() const {
    return impl_->random;
}

double Engine::Fps() const {
    return impl_->fps_smoothed;
}

double Engine::FrameMs() const {
    return impl_->frame_ms;
}

const SaveStore &Engine::GetSaves() const {
    return impl_->saves;
}

EntityStore &Engine::GetEntities() {
    return impl_->entities;
}

const EntityStore &Engine::GetEntities() const {
    return impl_->entities;
}

const std::string &Engine::SaveDir() const {
    return impl_->config.save_dir;
}

} // namespace fake2d

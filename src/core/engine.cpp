#include "fake2d/engine.h"
#include "fake2d/renderer.h"
#include "fake2d/script_host.h"

#include "platform/window.h"

#include <chrono>
#include <cstdio>

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
    TilemapLibrary tilemaps;
    UISystem ui;
    SaveStore saves;
    EntityStore entities;
    bool running = false;
    double delta_time = 0.0;
    double elapsed = 0.0;
    std::uint64_t frame_index = 0;
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}

Engine::~Engine() {
    Shutdown();
}

bool Engine::Init(const EngineConfig &config) {
    impl_->config = config;

    platform::WindowDesc desc;
    desc.title = impl_->config.title.c_str();
    desc.width = impl_->config.width;
    desc.height = impl_->config.height;
    desc.vsync = impl_->config.vsync;
    desc.headless = impl_->config.headless;

    if (!impl_->window.Create(desc)) {
        return false;
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
        impl_->delta_time = std::chrono::duration<double>(now - prev).count();
        prev = now;

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

        // Advance gameplay systems once; scripts read results / emit per frame.
        impl_->renderer.GetCamera().UpdateEffects(static_cast<float>(impl_->delta_time));
        impl_->particles.Update(static_cast<float>(impl_->delta_time));
        impl_->physics.Step(static_cast<float>(impl_->delta_time));
        impl_->animations.Update(static_cast<float>(impl_->delta_time));

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

void Engine::Shutdown() {
    if (!impl_) {
        return;
    }
    impl_->running = false;
    impl_->particles.Clear();
    impl_->physics.Clear();
    impl_->animations.Clear();
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

void Engine::SetWindowTitle(std::string_view title) {
    impl_->window.SetTitle(std::string(title).c_str());
}

void Engine::SetFullscreen(bool fullscreen) {
    impl_->window.SetFullscreen(fullscreen);
}

bool Engine::IsFullscreen() const {
    return impl_ && impl_->window.IsFullscreen();
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

const Input &Engine::GetInput() const {
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

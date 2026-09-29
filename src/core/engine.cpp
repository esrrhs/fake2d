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
    bool running = false;
    double delta_time = 0.0;
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
    if (!impl_->script.Init()) {
        return false;
    }
    impl_->script.BindEngine(this);

    if (!impl_->config.script_entry.empty()) {
        if (!impl_->script.CompileFile(impl_->config.script_entry)) {
            std::fprintf(stderr, "fake2d: warning: script entry compile failed, continuing without scripts\n");
        }
    }

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

        // Clear color cycles slightly so the skeleton window is visibly alive.
        const float t = static_cast<float>(impl_->frame_index) * 0.01f;
        const float r = 0.08f + 0.02f * (t - static_cast<int>(t));
        impl_->renderer.BeginFrame(r, 0.10f, 0.14f);

        // Script update then arena reset — FakeLua per-frame contract.
        impl_->script.CallUpdate(impl_->delta_time);
        impl_->script.ResetFrame();

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
    impl_->script.Shutdown();
    impl_->renderer.Shutdown();
    impl_->window.Destroy();
}

bool Engine::IsRunning() const {
    return impl_ && impl_->running;
}

double Engine::DeltaTime() const {
    return impl_ ? impl_->delta_time : 0.0;
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

} // namespace fake2d

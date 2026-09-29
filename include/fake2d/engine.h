#pragma once

#include "fake2d/renderer.h"
#include "fake2d/version.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace fake2d {

struct EngineConfig {
    std::string title = "fake2d";
    int width = 1280;
    int height = 720;
    bool vsync = true;
    bool headless = false;
    /// Relative path to the FakeLua entry script (e.g. scripts/main.lua).
    std::string script_entry = "scripts/main.lua";
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

    [[nodiscard]] bool IsRunning() const;
    [[nodiscard]] double DeltaTime() const;
    [[nodiscard]] std::uint64_t FrameIndex() const;

    Renderer &GetRenderer();
    [[nodiscard]] const Renderer &GetRenderer() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fake2d

#pragma once

#include <string>
#include <string_view>

namespace fake2d {

class Engine;

/// Bridges FakeLua scripts and the C++ host.
/// Per-frame pattern: Call(update) → host render → State::Reset().
class ScriptHost {
public:
    ScriptHost();
    ~ScriptHost();

    ScriptHost(const ScriptHost &) = delete;
    ScriptHost &operator=(const ScriptHost &) = delete;

    bool Init();
    bool CompileFile(std::string_view path);
    /// Invoke Lua `update(dt)` if present. Returns false on script error.
    bool CallUpdate(double dt);
    void ResetFrame();
    void Shutdown();

    void BindEngine(Engine *engine);

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace fake2d

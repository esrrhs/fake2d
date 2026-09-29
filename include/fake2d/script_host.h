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
    /// Re-compile the entry script from disk. On a compile error the previous
    /// program keeps running. Returns true when a new program was loaded.
    bool ReloadFile();
    /// Check the entry script's mtime and reload it when the file changed.
    /// Returns true when a reload happened this call.
    bool PollHotReload();
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

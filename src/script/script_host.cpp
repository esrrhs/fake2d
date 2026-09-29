#include "fake2d/script_host.h"

#include "script_bindings.h"
#include "fakelua.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <stdexcept>

namespace fake2d {

struct ScriptHost::Impl {
    fakelua::State *state = nullptr;
    Engine *engine = nullptr;
    fakelua::JITType jit = fakelua::JIT_GCC;
    bool owns_state = false;
    bool script_ready = false;
    /// Backend availability learned from the first compile; later compiles
    /// (hot reload) skip straight to the last working configuration.
    bool interp_only = false;

    std::filesystem::path entry_path;
    std::filesystem::file_time_type entry_mtime{};
    bool has_mtime = false;
};

namespace {

void CompileIntoState(fakelua::State *state, const std::string &path, bool interp_only) {
    fakelua::CompileConfig cfg;
    cfg.debug_mode = false;
    // TCC is never used; GCC JIT is the default backend.
    cfg.disable_jit[fakelua::JIT_TCC] = true;
    if (interp_only) {
        cfg.disable_jit[fakelua::JIT_GCC] = true;
    }
    fakelua::CompileFile(state, path, cfg);
}

} // namespace

ScriptHost::ScriptHost() : impl_(new Impl) {}

ScriptHost::~ScriptHost() {
    Shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool ScriptHost::Init() {
    const fakelua::StateConfig cfg;
    impl_->state = fakelua::FakeluaNewState(cfg);
    impl_->owns_state = true;
    return impl_->state != nullptr;
}

void ScriptHost::BindEngine(Engine *engine) {
    impl_->engine = engine;
    RegisterScriptApi(impl_->state, engine);
}

bool ScriptHost::CompileFile(std::string_view path) {
    if (!impl_->state) {
        return false;
    }
    const std::string path_str(path);
    try {
        if (impl_->interp_only) {
            // Reuse the configuration that worked before.
            CompileIntoState(impl_->state, path_str, true);
        } else {
            try {
                CompileIntoState(impl_->state, path_str, false);
            } catch (const std::exception &) {
                // No usable GCC on this host; the interpreter backend is the
                // most portable fallback.
                std::fprintf(stderr, "fake2d: GCC JIT unavailable, using the interpreter backend\n");
                impl_->interp_only = true;
                impl_->jit = fakelua::JIT_INTERP;
                CompileIntoState(impl_->state, path_str, true);
            }
        }
        impl_->script_ready = true;

        impl_->entry_path = std::filesystem::path(path);
        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(impl_->entry_path, ec);
        impl_->has_mtime = !ec;
        if (!ec) {
            impl_->entry_mtime = mtime;
        }
        return true;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "fake2d: CompileFile(%s) failed: %s\n", path_str.c_str(), e.what());
        impl_->script_ready = false;
        return false;
    }
}

bool ScriptHost::ReloadFile() {
    if (!impl_->state || impl_->entry_path.empty()) {
        return false;
    }
    try {
        // Re-compile into the live state: game data lives in C++, so resetting
        // script globals on reload is by design.
        CompileIntoState(impl_->state, impl_->entry_path.string(), impl_->interp_only);
        impl_->script_ready = true;
        std::fprintf(stderr, "fake2d: hot-reloaded %s\n", impl_->entry_path.string().c_str());
        return true;
    } catch (const std::exception &e) {
        // Keep the previous program running; surface the compile error once.
        std::fprintf(stderr, "fake2d: hot-reload of %s failed (keeping previous script): %s\n",
                     impl_->entry_path.string().c_str(), e.what());
        return false;
    }
}

bool ScriptHost::PollHotReload() {
    if (!impl_->state || !impl_->has_mtime || impl_->entry_path.empty()) {
        return false;
    }

    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(impl_->entry_path, ec);
    if (ec || mtime == impl_->entry_mtime) {
        return false;
    }

    // Advance the recorded mtime even on failure so a broken edit does not
    // spam compile errors every frame; the next save retries.
    impl_->entry_mtime = mtime;
    return ReloadFile();
}

bool ScriptHost::CallUpdate(double dt) {
    if (!impl_->state || !impl_->script_ready) {
        return false;
    }
    try {
        int code = 0;
        fakelua::Call(impl_->state, impl_->jit, "update", code, dt);
        (void)code;
        return true;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "fake2d: update() failed: %s\n", e.what());
        return false;
    }
}

void ScriptHost::ResetFrame() {
    if (impl_->state) {
        fakelua::inter::Reset(impl_->state);
    }
}

void ScriptHost::Shutdown() {
    if (impl_ && impl_->owns_state && impl_->state) {
        fakelua::FakeluaDeleteState(impl_->state);
        impl_->state = nullptr;
        impl_->owns_state = false;
    }
}

} // namespace fake2d

#include "fake2d/script_host.h"

#include "fakelua.h"

#include <cstdio>
#include <stdexcept>

namespace fake2d {

struct ScriptHost::Impl {
    fakelua::State *state = nullptr;
    Engine *engine = nullptr;
    fakelua::JITType jit = fakelua::JIT_TCC;
    bool owns_state = false;
    bool script_ready = false;
};

ScriptHost::ScriptHost() : impl_(new Impl) {}

ScriptHost::~ScriptHost() {
    Shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool ScriptHost::Init() {
    const fakelua::StateConfig cfg;
    // Prefers TCC for fast iteration during early engine bring-up.
    impl_->state = fakelua::FakeluaNewState(cfg);
    impl_->owns_state = true;
    return impl_->state != nullptr;
}

bool ScriptHost::CompileFile(std::string_view path) {
    if (!impl_->state) {
        return false;
    }
    try {
        fakelua::CompileConfig cfg;
        cfg.debug_mode = false;
        fakelua::CompileFile(impl_->state, std::string(path), cfg);
        impl_->script_ready = true;
        return true;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "fake2d: CompileFile(%s) failed: %s\n", std::string(path).c_str(), e.what());
        impl_->script_ready = false;
        return false;
    }
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
        fakelua::Reset(impl_->state);
    }
}

void ScriptHost::Shutdown() {
    if (impl_ && impl_->owns_state && impl_->state) {
        fakelua::FakeluaDeleteState(impl_->state);
        impl_->state = nullptr;
        impl_->owns_state = false;
    }
}

void ScriptHost::BindEngine(Engine *engine) {
    impl_->engine = engine;
}

} // namespace fake2d

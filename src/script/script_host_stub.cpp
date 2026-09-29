#include "fake2d/script_host.h"

#include <cstdio>

namespace fake2d {

struct ScriptHost::Impl {
    Engine *engine = nullptr;
};

ScriptHost::ScriptHost() : impl_(new Impl) {}

ScriptHost::~ScriptHost() {
    Shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool ScriptHost::Init() {
    std::fprintf(stderr, "fake2d: built without FakeLua (FAKE2D_WITH_FAKELUA=OFF); scripts disabled\n");
    return true;
}

bool ScriptHost::CompileFile(std::string_view path) {
    (void)path;
    return false;
}

bool ScriptHost::ReloadFile() {
    return false;
}

bool ScriptHost::PollHotReload() {
    return false;
}

bool ScriptHost::CallUpdate(double dt) {
    (void)dt;
    return false;
}

void ScriptHost::ResetFrame() {}

void ScriptHost::Shutdown() {}

void ScriptHost::BindEngine(Engine *engine) {
    impl_->engine = engine;
}

} // namespace fake2d

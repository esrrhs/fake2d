#include "fake2d/script_host.h"
#include "fake2d/engine.h"

#include "fakelua.h"

#include <cstdio>
#include <functional>
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

void ScriptHost::BindEngine(Engine *engine) {
    impl_->engine = engine;
    if (!impl_->state || !impl_->engine) {
        return;
    }

    // Register 2D draw primitives
    fakelua::RegisterNativeFunction(
        impl_->state, "draw_quad", false,
        std::function<void(fakelua::State *, double, double, double, double, double, double, double, double)>(
            [this](fakelua::State * /*s*/, double x, double y, double w, double h, double r, double g, double b, double a) {
                if (impl_->engine) {
                    impl_->engine->GetRenderer().DrawQuad(
                        static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(w), static_cast<float>(h),
                        Color{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), static_cast<float>(a)}
                    );
                }
            }
        )
    );

    fakelua::RegisterNativeFunction(
        impl_->state, "draw_quad_rgb", false,
        std::function<void(fakelua::State *, double, double, double, double, double, double, double)>(
            [this](fakelua::State * /*s*/, double x, double y, double w, double h, double r, double g, double b) {
                if (impl_->engine) {
                    impl_->engine->GetRenderer().DrawQuad(
                        static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(w), static_cast<float>(h),
                        Color{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), 1.0f}
                    );
                }
            }
        )
    );

    // Register camera controls
    fakelua::RegisterNativeFunction(
        impl_->state, "camera_set_position", false,
        std::function<void(fakelua::State *, double, double)>(
            [this](fakelua::State * /*s*/, double x, double y) {
                if (impl_->engine) {
                    impl_->engine->GetRenderer().GetCamera().SetPosition(static_cast<float>(x), static_cast<float>(y));
                }
            }
        )
    );

    fakelua::RegisterNativeFunction(
        impl_->state, "camera_move", false,
        std::function<void(fakelua::State *, double, double)>(
            [this](fakelua::State * /*s*/, double dx, double dy) {
                if (impl_->engine) {
                    impl_->engine->GetRenderer().GetCamera().Move(static_cast<float>(dx), static_cast<float>(dy));
                }
            }
        )
    );

    fakelua::RegisterNativeFunction(
        impl_->state, "camera_set_zoom", false,
        std::function<void(fakelua::State *, double)>(
            [this](fakelua::State * /*s*/, double zoom) {
                if (impl_->engine) {
                    impl_->engine->GetRenderer().GetCamera().SetZoom(static_cast<float>(zoom));
                }
            }
        )
    );

    fakelua::RegisterNativeFunction(
        impl_->state, "camera_set_rotation", false,
        std::function<void(fakelua::State *, double)>(
            [this](fakelua::State * /*s*/, double rad) {
                if (impl_->engine) {
                    impl_->engine->GetRenderer().GetCamera().SetRotation(static_cast<float>(rad));
                }
            }
        )
    );
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

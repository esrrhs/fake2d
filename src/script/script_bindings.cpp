#include "script_bindings.h"

#include "fake2d/engine.h"
#include "fake2d/renderer.h"

#include <functional>
#include <string_view>

namespace fake2d {

namespace {

TextureHandle ResolveTexture(std::int64_t handle) {
    return handle > 0 ? static_cast<TextureHandle>(static_cast<std::uint64_t>(handle)) : kInvalidTextureHandle;
}

void RegisterDrawApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "draw_quad", false,
        std::function<void(fakelua::State *, double, double, double, double, double, double, double, double)>(
            [engine](fakelua::State *, double x, double y, double w, double h,
                     double r, double g, double b, double a) {
                engine->GetRenderer().DrawQuad(
                    static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), static_cast<float>(h),
                    Color{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), static_cast<float>(a)});
            }));

    fakelua::RegisterNativeFunction(
        state, "draw_quad_rgb", false,
        std::function<void(fakelua::State *, double, double, double, double, double, double, double)>(
            [engine](fakelua::State *, double x, double y, double w, double h,
                     double r, double g, double b) {
                engine->GetRenderer().DrawQuad(
                    static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), static_cast<float>(h),
                    Color{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), 1.0f});
            }));
}

void RegisterCameraApi(fakelua::State *state, Engine *engine) {
    Camera2D &camera = engine->GetRenderer().GetCamera();

    fakelua::RegisterNativeFunction(
        state, "camera_set_position", false,
        std::function<void(fakelua::State *, double, double)>(
            [engine](fakelua::State *, double x, double y) {
                engine->GetRenderer().GetCamera().SetPosition(static_cast<float>(x), static_cast<float>(y));
            }));

    fakelua::RegisterNativeFunction(
        state, "camera_move", false,
        std::function<void(fakelua::State *, double, double)>(
            [engine](fakelua::State *, double dx, double dy) {
                engine->GetRenderer().GetCamera().Move(static_cast<float>(dx), static_cast<float>(dy));
            }));

    fakelua::RegisterNativeFunction(
        state, "camera_set_zoom", false,
        std::function<void(fakelua::State *, double)>(
            [engine](fakelua::State *, double zoom) {
                engine->GetRenderer().GetCamera().SetZoom(static_cast<float>(zoom));
            }));

    fakelua::RegisterNativeFunction(
        state, "camera_set_rotation", false,
        std::function<void(fakelua::State *, double)>(
            [engine](fakelua::State *, double radians) {
                engine->GetRenderer().GetCamera().SetRotation(static_cast<float>(radians));
            }));

    fakelua::RegisterNativeFunction(
        state, "camera_x", false,
        std::function<double(fakelua::State *)>(
            [&camera](fakelua::State *) { return static_cast<double>(camera.Position().x); }));

    fakelua::RegisterNativeFunction(
        state, "camera_y", false,
        std::function<double(fakelua::State *)>(
            [&camera](fakelua::State *) { return static_cast<double>(camera.Position().y); }));

    fakelua::RegisterNativeFunction(
        state, "camera_zoom", false,
        std::function<double(fakelua::State *)>(
            [&camera](fakelua::State *) { return static_cast<double>(camera.Zoom()); }));

    fakelua::RegisterNativeFunction(
        state, "camera_rotation", false,
        std::function<double(fakelua::State *)>(
            [&camera](fakelua::State *) { return static_cast<double>(camera.Rotation()); }));

    fakelua::RegisterNativeFunction(
        state, "camera_screen_to_world_x", false,
        std::function<double(fakelua::State *, double, double)>(
            [&camera](fakelua::State *, double sx, double sy) {
                return static_cast<double>(camera.ScreenToWorld({static_cast<float>(sx), static_cast<float>(sy)}).x);
            }));

    fakelua::RegisterNativeFunction(
        state, "camera_screen_to_world_y", false,
        std::function<double(fakelua::State *, double, double)>(
            [&camera](fakelua::State *, double sx, double sy) {
                return static_cast<double>(camera.ScreenToWorld({static_cast<float>(sx), static_cast<float>(sy)}).y);
            }));

    fakelua::RegisterNativeFunction(
        state, "camera_world_to_screen_x", false,
        std::function<double(fakelua::State *, double, double)>(
            [&camera](fakelua::State *, double wx, double wy) {
                return static_cast<double>(camera.WorldToScreen({static_cast<float>(wx), static_cast<float>(wy)}).x);
            }));

    fakelua::RegisterNativeFunction(
        state, "camera_world_to_screen_y", false,
        std::function<double(fakelua::State *, double, double)>(
            [&camera](fakelua::State *, double wx, double wy) {
                return static_cast<double>(camera.WorldToScreen({static_cast<float>(wx), static_cast<float>(wy)}).y);
            }));
}

void RegisterSpriteApi(fakelua::State *state, Engine *engine) {
    auto *resources = &engine->GetResources();
    auto *renderer = &engine->GetRenderer();

    fakelua::RegisterNativeFunction(
        state, "sprite_load", false,
        std::function<std::int64_t(fakelua::State *, std::string_view)>(
            [resources](fakelua::State *, std::string_view path) -> std::int64_t {
                const TextureHandle handle = resources->LoadTexture(std::string(path));
                return handle == kInvalidTextureHandle
                           ? 0
                           : static_cast<std::int64_t>(static_cast<std::uint64_t>(handle));
            }));

    fakelua::RegisterNativeFunction(
        state, "sprite_free", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [resources](fakelua::State *, std::int64_t handle) {
                resources->Release(ResolveTexture(handle));
            }));

    fakelua::RegisterNativeFunction(
        state, "sprite_width", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [resources](fakelua::State *, std::int64_t handle) -> std::int64_t {
                const Texture2D *texture = resources->GetTexture(ResolveTexture(handle));
                return texture ? texture->Width() : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "sprite_height", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [resources](fakelua::State *, std::int64_t handle) -> std::int64_t {
                const Texture2D *texture = resources->GetTexture(ResolveTexture(handle));
                return texture ? texture->Height() : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "sprite_draw", false,
        std::function<void(fakelua::State *, std::int64_t, double, double, double, double)>(
            [resources, renderer](fakelua::State *, std::int64_t handle, double x, double y, double w, double h) {
                const Texture2D *texture = resources->GetTexture(ResolveTexture(handle));
                if (texture) {
                    renderer->DrawSprite(*texture, static_cast<float>(x), static_cast<float>(y),
                                         static_cast<float>(w), static_cast<float>(h));
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "sprite_draw_tinted", false,
        std::function<void(fakelua::State *, std::int64_t, double, double, double, double,
                           double, double, double, double)>(
            [resources, renderer](fakelua::State *, std::int64_t handle, double x, double y, double w, double h,
                                  double r, double g, double b, double a) {
                const Texture2D *texture = resources->GetTexture(ResolveTexture(handle));
                if (texture) {
                    renderer->DrawSprite(*texture, static_cast<float>(x), static_cast<float>(y),
                                         static_cast<float>(w), static_cast<float>(h),
                                         Color{static_cast<float>(r), static_cast<float>(g),
                                               static_cast<float>(b), static_cast<float>(a)});
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "sprite_draw_region", false,
        std::function<void(fakelua::State *, std::int64_t, double, double, double, double,
                           double, double, double, double)>(
            [resources, renderer](fakelua::State *, std::int64_t handle,
                                  double sx, double sy, double sw, double sh,
                                  double dx, double dy, double dw, double dh) {
                const Texture2D *texture = resources->GetTexture(ResolveTexture(handle));
                if (texture) {
                    renderer->DrawSprite(*texture,
                                         {static_cast<float>(sx), static_cast<float>(sy),
                                          static_cast<float>(sw), static_cast<float>(sh)},
                                         {static_cast<float>(dx), static_cast<float>(dy),
                                          static_cast<float>(dw), static_cast<float>(dh)});
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "sprite_draw_rotated", false,
        std::function<void(fakelua::State *, std::int64_t, double, double, double, double,
                           double, double, double, double, double, double, double)>(
            [resources, renderer](fakelua::State *, std::int64_t handle,
                                  double sx, double sy, double sw, double sh,
                                  double dx, double dy, double dw, double dh,
                                  double angle_rad, double ox, double oy) {
                const Texture2D *texture = resources->GetTexture(ResolveTexture(handle));
                if (texture) {
                    renderer->DrawSpriteRotated(*texture,
                                                {static_cast<float>(sx), static_cast<float>(sy),
                                                 static_cast<float>(sw), static_cast<float>(sh)},
                                                {static_cast<float>(dx), static_cast<float>(dy),
                                                 static_cast<float>(dw), static_cast<float>(dh)},
                                                static_cast<float>(angle_rad),
                                                {static_cast<float>(ox), static_cast<float>(oy)});
                }
            }));
}

void RegisterInputApi(fakelua::State *state, Engine *engine) {
    const Input *input = &engine->GetInput();

    fakelua::RegisterNativeFunction(
        state, "input_key_down", false,
        std::function<bool(fakelua::State *, std::string_view)>(
            [input](fakelua::State *, std::string_view key) { return input->KeyDown(key); }));

    fakelua::RegisterNativeFunction(
        state, "input_key_pressed", false,
        std::function<bool(fakelua::State *, std::string_view)>(
            [input](fakelua::State *, std::string_view key) { return input->KeyPressed(key); }));

    fakelua::RegisterNativeFunction(
        state, "input_key_released", false,
        std::function<bool(fakelua::State *, std::string_view)>(
            [input](fakelua::State *, std::string_view key) { return input->KeyReleased(key); }));

    fakelua::RegisterNativeFunction(
        state, "input_mouse_x", false,
        std::function<double(fakelua::State *)>(
            [input](fakelua::State *) { return static_cast<double>(input->MousePosition().x); }));

    fakelua::RegisterNativeFunction(
        state, "input_mouse_y", false,
        std::function<double(fakelua::State *)>(
            [input](fakelua::State *) { return static_cast<double>(input->MousePosition().y); }));

    fakelua::RegisterNativeFunction(
        state, "input_mouse_down", false,
        std::function<bool(fakelua::State *, std::int64_t)>(
            [input](fakelua::State *, std::int64_t button) { return input->MouseDown(button); }));

    fakelua::RegisterNativeFunction(
        state, "input_mouse_pressed", false,
        std::function<bool(fakelua::State *, std::int64_t)>(
            [input](fakelua::State *, std::int64_t button) { return input->MousePressed(button); }));

    fakelua::RegisterNativeFunction(
        state, "input_mouse_wheel", false,
        std::function<double(fakelua::State *)>(
            [input](fakelua::State *) { return input->MouseWheel(); }));
}

void RegisterTimeApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "time_delta", false,
        std::function<double(fakelua::State *)>(
            [engine](fakelua::State *) { return engine->DeltaTime(); }));

    fakelua::RegisterNativeFunction(
        state, "time_elapsed", false,
        std::function<double(fakelua::State *)>(
            [engine](fakelua::State *) { return engine->TimeElapsed(); }));

    fakelua::RegisterNativeFunction(
        state, "time_frame", false,
        std::function<std::int64_t(fakelua::State *)>(
            [engine](fakelua::State *) { return static_cast<std::int64_t>(engine->FrameIndex()); }));
}

void RegisterDebugApi(fakelua::State *state) {
    fakelua::RegisterNativeFunction(
        state, "log_number", false,
        std::function<void(fakelua::State *, double)>(
            [](fakelua::State *, double value) { std::fprintf(stderr, "[script] %g\n", value); }));
}

} // namespace

void RegisterScriptApi(fakelua::State *state, Engine *engine) {
    if (!state || !engine) {
        return;
    }
    RegisterDrawApi(state, engine);
    RegisterCameraApi(state, engine);
    RegisterSpriteApi(state, engine);
    RegisterInputApi(state, engine);
    RegisterTimeApi(state, engine);
    RegisterDebugApi(state);
}

} // namespace fake2d

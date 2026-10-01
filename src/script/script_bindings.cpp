#include "script_bindings.h"

#include "fake2d/animation.h"
#include "fake2d/engine.h"
#include "fake2d/particle.h"
#include "fake2d/physics.h"
#include "fake2d/renderer.h"
#include "fake2d/tilemap.h"
#include "fake2d/ui.h"

#include <functional>
#include <string_view>

namespace fake2d {

namespace {

TextureHandle ResolveTexture(std::int64_t handle) {
    return handle > 0 ? static_cast<TextureHandle>(static_cast<std::uint64_t>(handle)) : kInvalidTextureHandle;
}

BodyId ResolveBody(std::int64_t handle) {
    return static_cast<BodyId>(static_cast<std::uint64_t>(handle));
}

void RegisterPhysicsApi(fakelua::State *state, Engine *engine) {
    auto create_body = [engine](double x, double y, double hw, double hh, double radius) -> std::int64_t {
        BodyConfig cfg;
        cfg.position = {static_cast<float>(x), static_cast<float>(y)};
        cfg.half_extents = {static_cast<float>(hw), static_cast<float>(hh)};
        cfg.radius = static_cast<float>(radius);
        const BodyId id = engine->GetPhysics().CreateBody(cfg);
        return static_cast<std::int64_t>(static_cast<std::uint64_t>(id));
    };

    fakelua::RegisterNativeFunction(
        state, "phys_create_box", false,
        std::function<std::int64_t(fakelua::State *, double, double, double, double)>(
            [create_body](fakelua::State *, double x, double y, double hw, double hh) -> std::int64_t {
                return create_body(x, y, hw, hh, 0.0);
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_create_circle", false,
        std::function<std::int64_t(fakelua::State *, double, double, double)>(
            [create_body](fakelua::State *, double x, double y, double radius) -> std::int64_t {
                return create_body(x, y, radius, radius, radius);
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_destroy", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                engine->GetPhysics().DestroyBody(ResolveBody(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_set_static", false,
        std::function<void(fakelua::State *, std::int64_t, bool)>(
            [engine](fakelua::State *, std::int64_t id, bool is_static) {
                if (BodyConfig *b = engine->GetPhysics().Get(ResolveBody(id))) {
                    b->type = is_static ? BodyType::Static : BodyType::Dynamic;
                    if (is_static) {
                        b->velocity = {0.0f, 0.0f};
                    }
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_set_sensor", false,
        std::function<void(fakelua::State *, std::int64_t, bool)>(
            [engine](fakelua::State *, std::int64_t id, bool sensor) {
                if (BodyConfig *b = engine->GetPhysics().Get(ResolveBody(id))) {
                    b->sensor = sensor;
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_set_restitution", false,
        std::function<void(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double r) {
                if (BodyConfig *b = engine->GetPhysics().Get(ResolveBody(id))) {
                    b->restitution = static_cast<float>(r);
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_set_gravity_scale", false,
        std::function<void(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double scale) {
                if (BodyConfig *b = engine->GetPhysics().Get(ResolveBody(id))) {
                    b->gravity_scale = static_cast<float>(scale);
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_set_pos", false,
        std::function<void(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double x, double y) {
                if (BodyConfig *b = engine->GetPhysics().Get(ResolveBody(id))) {
                    b->position = {static_cast<float>(x), static_cast<float>(y)};
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_set_vel", false,
        std::function<void(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double vx, double vy) {
                if (BodyConfig *b = engine->GetPhysics().Get(ResolveBody(id))) {
                    b->velocity = {static_cast<float>(vx), static_cast<float>(vy)};
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_set_user", false,
        std::function<void(fakelua::State *, std::int64_t, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id, std::int64_t user) {
                if (BodyConfig *b = engine->GetPhysics().Get(ResolveBody(id))) {
                    b->user_id = user;
                }
            }));

    const auto getter = [engine](std::int64_t id, int field) -> double {
        const BodyConfig *b = engine->GetPhysics().Get(ResolveBody(id));
        if (b == nullptr) {
            return 0.0;
        }
        switch (field) {
            case 0: return b->position.x;
            case 1: return b->position.y;
            case 2: return b->velocity.x;
            case 3: return b->velocity.y;
            default: return 0.0;
        }
    };
    fakelua::RegisterNativeFunction(state, "phys_x", false,
        std::function<double(fakelua::State *, std::int64_t)>(
            [getter](fakelua::State *, std::int64_t id) { return getter(id, 0); }));
    fakelua::RegisterNativeFunction(state, "phys_y", false,
        std::function<double(fakelua::State *, std::int64_t)>(
            [getter](fakelua::State *, std::int64_t id) { return getter(id, 1); }));
    fakelua::RegisterNativeFunction(state, "phys_vx", false,
        std::function<double(fakelua::State *, std::int64_t)>(
            [getter](fakelua::State *, std::int64_t id) { return getter(id, 2); }));
    fakelua::RegisterNativeFunction(state, "phys_vy", false,
        std::function<double(fakelua::State *, std::int64_t)>(
            [getter](fakelua::State *, std::int64_t id) { return getter(id, 3); }));

    fakelua::RegisterNativeFunction(
        state, "phys_gravity", false,
        std::function<void(fakelua::State *, double, double)>(
            [engine](fakelua::State *, double gx, double gy) {
                engine->GetPhysics().SetGravity({static_cast<float>(gx), static_cast<float>(gy)});
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_contact_count", false,
        std::function<std::int64_t(fakelua::State *)>(
            [engine](fakelua::State *) -> std::int64_t {
                return static_cast<std::int64_t>(engine->GetPhysics().Contacts().size());
            }));

    const auto contact_user = [engine](std::int64_t index, bool b_side) -> std::int64_t {
        const auto &contacts = engine->GetPhysics().Contacts();
        if (index < 0 || static_cast<std::size_t>(index) >= contacts.size()) {
            return 0;
        }
        return b_side ? contacts[static_cast<std::size_t>(index)].user_b
                      : contacts[static_cast<std::size_t>(index)].user_a;
    };
    fakelua::RegisterNativeFunction(state, "phys_contact_user_a", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [contact_user](fakelua::State *, std::int64_t i) { return contact_user(i, false); }));
    fakelua::RegisterNativeFunction(state, "phys_contact_user_b", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [contact_user](fakelua::State *, std::int64_t i) { return contact_user(i, true); }));
}

void RegisterAnimationApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "anim_create", false,
        std::function<std::int64_t(fakelua::State *)>(
            [engine](fakelua::State *) -> std::int64_t {
                return engine->GetAnimations().Create();
            }));

    fakelua::RegisterNativeFunction(
        state, "anim_destroy", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                engine->GetAnimations().Destroy(static_cast<int>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "anim_frame", false,
        std::function<void(fakelua::State *, std::int64_t, std::int64_t,
                           double, double, double, double)>(
            [engine](fakelua::State *, std::int64_t id, std::int64_t tex,
                     double sx, double sy, double sw, double sh) {
                engine->GetAnimations().AddFrame(
                    static_cast<int>(id), ResolveTexture(tex),
                    {static_cast<float>(sx), static_cast<float>(sy),
                     static_cast<float>(sw), static_cast<float>(sh)});
            }));

    fakelua::RegisterNativeFunction(
        state, "anim_fps", false,
        std::function<void(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double fps) {
                engine->GetAnimations().SetFps(static_cast<int>(id), static_cast<float>(fps));
            }));

    fakelua::RegisterNativeFunction(
        state, "anim_loop", false,
        std::function<void(fakelua::State *, std::int64_t, bool)>(
            [engine](fakelua::State *, std::int64_t id, bool loop) {
                engine->GetAnimations().SetLoop(static_cast<int>(id), loop);
            }));

    fakelua::RegisterNativeFunction(
        state, "anim_play", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                engine->GetAnimations().Play(static_cast<int>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "anim_stop", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                engine->GetAnimations().Stop(static_cast<int>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "anim_finished", false,
        std::function<bool(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                return engine->GetAnimations().Finished(static_cast<int>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "anim_draw", false,
        std::function<void(fakelua::State *, std::int64_t, double, double, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double x, double y, double w, double h) {
                engine->GetAnimations().Draw(
                    static_cast<int>(id), engine->GetResources(),
                    engine->GetRenderer().GetSpriteBatch(),
                    {static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(w), static_cast<float>(h)});
            }));
}

void RegisterTilemapApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "map_load", false,
        std::function<std::int64_t(fakelua::State *, std::string_view)>(
            [engine](fakelua::State *, std::string_view path) -> std::int64_t {
                return engine->GetTilemaps().Load(std::string(path), engine->GetResources());
            }));

    fakelua::RegisterNativeFunction(
        state, "map_draw", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                if (const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id))) {
                    map->Draw(engine->GetRenderer());
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "map_solid", false,
        std::function<bool(fakelua::State *, std::int64_t, std::int64_t, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id, std::int64_t col, std::int64_t row) {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map != nullptr && map->SolidAt(static_cast<int>(col), static_cast<int>(row));
            }));

    fakelua::RegisterNativeFunction(
        state, "map_cols", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) -> std::int64_t {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map ? map->Cols() : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_rows", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) -> std::int64_t {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map ? map->Rows() : 0;
            }));
}

void RegisterUiApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "ui_panel", false,
        std::function<void(fakelua::State *, std::int64_t, double, double, double, double,
                           double, double, double, double)>(
            [engine](fakelua::State *, std::int64_t anchor, double ox, double oy,
                     double w, double h, double r, double g, double b, double a) {
                engine->GetUI().Panel(static_cast<UIAnchor>(anchor),
                                      static_cast<float>(ox), static_cast<float>(oy),
                                      static_cast<float>(w), static_cast<float>(h),
                                      Color{static_cast<float>(r), static_cast<float>(g),
                                            static_cast<float>(b), static_cast<float>(a)});
            }));

    fakelua::RegisterNativeFunction(
        state, "ui_label", false,
        std::function<void(fakelua::State *, std::int64_t, double, double,
                           std::string_view, double, double, double, double, double)>(
            [engine](fakelua::State *, std::int64_t anchor, double ox, double oy,
                     std::string_view text, double scale,
                     double r, double g, double b, double a) {
                engine->GetUI().Label(static_cast<UIAnchor>(anchor),
                                      static_cast<float>(ox), static_cast<float>(oy),
                                      text, static_cast<float>(scale),
                                      Color{static_cast<float>(r), static_cast<float>(g),
                                            static_cast<float>(b), static_cast<float>(a)});
            }));

    fakelua::RegisterNativeFunction(
        state, "ui_button", false,
        std::function<bool(fakelua::State *, std::string_view, std::int64_t,
                           double, double, double, double, std::string_view, double)>(
            [engine](fakelua::State *, std::string_view key, std::int64_t anchor,
                     double ox, double oy, double w, double h,
                     std::string_view text, double scale) -> bool {
                return engine->GetUI().Button(
                    key, static_cast<UIAnchor>(anchor),
                    static_cast<float>(ox), static_cast<float>(oy),
                    static_cast<float>(w), static_cast<float>(h),
                    text, static_cast<float>(scale));
            }));
}

void RegisterLineApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "draw_line", false,
        std::function<void(fakelua::State *, double, double, double, double,
                           double, double, double, double)>(
            [engine](fakelua::State *, double x1, double y1, double x2, double y2,
                     double r, double g, double b, double a) {
                engine->GetRenderer().DrawLine(
                    static_cast<float>(x1), static_cast<float>(y1),
                    static_cast<float>(x2), static_cast<float>(y2),
                    Color{static_cast<float>(r), static_cast<float>(g),
                          static_cast<float>(b), static_cast<float>(a)});
            }));

    fakelua::RegisterNativeFunction(
        state, "draw_rect_outline", false,
        std::function<void(fakelua::State *, double, double, double, double,
                           double, double, double, double)>(
            [engine](fakelua::State *, double x, double y, double w, double h,
                     double r, double g, double b, double a) {
                engine->GetRenderer().DrawRectOutline(
                    {static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(w), static_cast<float>(h)},
                    Color{static_cast<float>(r), static_cast<float>(g),
                          static_cast<float>(b), static_cast<float>(a)});
            }));

    fakelua::RegisterNativeFunction(
        state, "draw_circle_outline", false,
        std::function<void(fakelua::State *, double, double, double, std::int64_t,
                           double, double, double, double)>(
            [engine](fakelua::State *, double cx, double cy, double radius,
                     std::int64_t segments, double r, double g, double b, double a) {
                engine->GetRenderer().DrawCircleOutline(
                    static_cast<float>(cx), static_cast<float>(cy),
                    static_cast<float>(radius), static_cast<int>(segments),
                    Color{static_cast<float>(r), static_cast<float>(g),
                          static_cast<float>(b), static_cast<float>(a)});
            }));
}

void RegisterTextApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "draw_text", false,
        std::function<double(fakelua::State *, std::string_view, double, double, double,
                             double, double, double, double)>(
            [engine](fakelua::State *, std::string_view text, double x, double y, double scale,
                     double r, double g, double b, double a) -> double {
                return static_cast<double>(engine->GetRenderer().DrawText(
                    text, static_cast<float>(x), static_cast<float>(y), static_cast<float>(scale),
                    Color{static_cast<float>(r), static_cast<float>(g),
                          static_cast<float>(b), static_cast<float>(a)}));
            }));

    fakelua::RegisterNativeFunction(
        state, "text_width", false,
        std::function<double(fakelua::State *, std::string_view, double)>(
            [engine](fakelua::State *, std::string_view text, double scale) -> double {
                return static_cast<double>(
                    engine->GetRenderer().MeasureText(text, static_cast<float>(scale)));
            }));
}

void RegisterAudioApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "audio_play", false,
        std::function<void(fakelua::State *, std::string_view, double)>(
            [engine](fakelua::State *, std::string_view name, double volume) {
                engine->GetAudio().Play(name, static_cast<float>(volume));
            }));

    fakelua::RegisterNativeFunction(
        state, "audio_enabled", false,
        std::function<bool(fakelua::State *)>(
            [engine](fakelua::State *) { return engine->GetAudio().IsEnabled(); }));

    fakelua::RegisterNativeFunction(
        state, "audio_music", false,
        std::function<void(fakelua::State *, std::string_view, double)>(
            [engine](fakelua::State *, std::string_view name, double volume) {
                engine->GetAudio().PlayMusic(name, static_cast<float>(volume));
            }));

    fakelua::RegisterNativeFunction(
        state, "audio_music_stop", false,
        std::function<void(fakelua::State *)>(
            [engine](fakelua::State *) { engine->GetAudio().StopMusic(); }));
}

void RegisterParticleApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "part_create", false,
        std::function<std::int64_t(fakelua::State *)>(
            [engine](fakelua::State *) -> std::int64_t {
                return static_cast<std::int64_t>(engine->GetParticles().CreateEmitter());
            }));

    fakelua::RegisterNativeFunction(
        state, "part_set_lifetime", false,
        std::function<void(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double lo, double hi) {
                if (ParticleConfig *cfg = engine->GetParticles().Config(static_cast<int>(id))) {
                    cfg->lifetime_min = static_cast<float>(lo);
                    cfg->lifetime_max = static_cast<float>(hi);
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "part_set_speed", false,
        std::function<void(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double lo, double hi) {
                if (ParticleConfig *cfg = engine->GetParticles().Config(static_cast<int>(id))) {
                    cfg->speed_min = static_cast<float>(lo);
                    cfg->speed_max = static_cast<float>(hi);
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "part_set_direction", false,
        std::function<void(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double angle, double spread) {
                if (ParticleConfig *cfg = engine->GetParticles().Config(static_cast<int>(id))) {
                    cfg->angle = static_cast<float>(angle);
                    cfg->spread = static_cast<float>(spread);
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "part_set_size", false,
        std::function<void(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double start, double end) {
                if (ParticleConfig *cfg = engine->GetParticles().Config(static_cast<int>(id))) {
                    cfg->start_size = static_cast<float>(start);
                    cfg->end_size = static_cast<float>(end);
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "part_set_color", false,
        std::function<void(fakelua::State *, std::int64_t, double, double, double, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double r, double g, double b,
                     double a_start, double a_end) {
                if (ParticleConfig *cfg = engine->GetParticles().Config(static_cast<int>(id))) {
                    cfg->color = Color{static_cast<float>(r), static_cast<float>(g),
                                       static_cast<float>(b), 1.0f};
                    cfg->start_alpha = static_cast<float>(a_start);
                    cfg->end_alpha = static_cast<float>(a_end);
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "part_set_gravity", false,
        std::function<void(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double gx, double gy) {
                if (ParticleConfig *cfg = engine->GetParticles().Config(static_cast<int>(id))) {
                    cfg->gravity = {static_cast<float>(gx), static_cast<float>(gy)};
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "part_set_drag", false,
        std::function<void(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double drag) {
                if (ParticleConfig *cfg = engine->GetParticles().Config(static_cast<int>(id))) {
                    cfg->drag = static_cast<float>(drag);
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "part_set_spin", false,
        std::function<void(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double spin) {
                if (ParticleConfig *cfg = engine->GetParticles().Config(static_cast<int>(id))) {
                    cfg->spin = static_cast<float>(spin);
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "part_emit", false,
        std::function<void(fakelua::State *, std::int64_t, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, std::int64_t count, double x, double y) {
                engine->GetParticles().Emit(static_cast<int>(id), static_cast<int>(count),
                                            {static_cast<float>(x), static_cast<float>(y)});
            }));

    fakelua::RegisterNativeFunction(
        state, "part_draw", false,
        std::function<void(fakelua::State *)>(
            [engine](fakelua::State *) {
                engine->GetParticles().Draw(engine->GetRenderer().GetSpriteBatch());
            }));
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

void RegisterBatchApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "batch_set_sorted", false,
        std::function<void(fakelua::State *, bool)>(
            [engine](fakelua::State *, bool sorted) {
                engine->GetRenderer().SetSortedBatch(sorted);
            }));

    fakelua::RegisterNativeFunction(
        state, "draw_set_layer", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t layer) {
                engine->GetRenderer().SetDrawLayer(static_cast<int>(layer));
            }));

    fakelua::RegisterNativeFunction(
        state, "draw_set_z", false,
        std::function<void(fakelua::State *, double)>(
            [engine](fakelua::State *, double z) {
                engine->GetRenderer().SetDrawZ(static_cast<float>(z));
            }));

    fakelua::RegisterNativeFunction(
        state, "draw_set_blend", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t mode) {
                engine->GetRenderer().SetBlendMode(mode == 1 ? BlendMode::Additive
                                                            : BlendMode::Alpha);
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

    fakelua::RegisterNativeFunction(
        state, "camera_shake", false,
        std::function<void(fakelua::State *, double)>(
            [&camera](fakelua::State *, double trauma) {
                camera.AddTrauma(static_cast<float>(trauma));
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
    RegisterBatchApi(state, engine);
    RegisterLineApi(state, engine);
    RegisterTextApi(state, engine);
    RegisterCameraApi(state, engine);
    RegisterSpriteApi(state, engine);
    RegisterInputApi(state, engine);
    RegisterTimeApi(state, engine);
    RegisterAudioApi(state, engine);
    RegisterParticleApi(state, engine);
    RegisterPhysicsApi(state, engine);
    RegisterAnimationApi(state, engine);
    RegisterTilemapApi(state, engine);
    RegisterUiApi(state, engine);
    RegisterDebugApi(state);
}

} // namespace fake2d

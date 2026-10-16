#include "script_bindings.h"

#include "fake2d/animation.h"
#include "fake2d/audio.h"
#include "fake2d/engine.h"
#include "fake2d/particle.h"
#include "fake2d/physics.h"
#include "fake2d/renderer.h"
#include "fake2d/tilemap.h"
#include "fake2d/ui.h"

#include <cmath>
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

    // ---- Collision filtering (category / mask bitmasks) ---------------------
    // Creators with an explicit filter; the plain creators default to
    // category 1 / mask all (collide with everything).
    fakelua::RegisterNativeFunction(
        state, "phys_create_box_cat", false,
        std::function<std::int64_t(fakelua::State *, double, double, double, double,
                                   std::int64_t, std::int64_t)>(
            [engine](fakelua::State *, double x, double y, double hw, double hh,
                     std::int64_t category, std::int64_t mask) -> std::int64_t {
                BodyConfig cfg;
                cfg.position = {static_cast<float>(x), static_cast<float>(y)};
                cfg.half_extents = {static_cast<float>(hw), static_cast<float>(hh)};
                cfg.category = static_cast<std::uint16_t>(category);
                cfg.mask = static_cast<std::uint16_t>(mask);
                const BodyId id = engine->GetPhysics().CreateBody(cfg);
                return static_cast<std::int64_t>(static_cast<std::uint64_t>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_create_circle_cat", false,
        std::function<std::int64_t(fakelua::State *, double, double, double,
                                   std::int64_t, std::int64_t)>(
            [engine](fakelua::State *, double x, double y, double radius,
                     std::int64_t category, std::int64_t mask) -> std::int64_t {
                BodyConfig cfg;
                cfg.position = {static_cast<float>(x), static_cast<float>(y)};
                cfg.radius = static_cast<float>(radius);
                cfg.half_extents = {static_cast<float>(radius),
                                    static_cast<float>(radius)};
                cfg.category = static_cast<std::uint16_t>(category);
                cfg.mask = static_cast<std::uint16_t>(mask);
                const BodyId id = engine->GetPhysics().CreateBody(cfg);
                return static_cast<std::int64_t>(static_cast<std::uint64_t>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_set_category", false,
        std::function<void(fakelua::State *, std::int64_t, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id, std::int64_t category) {
                if (BodyConfig *b = engine->GetPhysics().Get(ResolveBody(id))) {
                    b->category = static_cast<std::uint16_t>(category);
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_set_mask", false,
        std::function<void(fakelua::State *, std::int64_t, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id, std::int64_t mask) {
                if (BodyConfig *b = engine->GetPhysics().Get(ResolveBody(id))) {
                    b->mask = static_cast<std::uint16_t>(mask);
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_category", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) -> std::int64_t {
                const BodyConfig *b = engine->GetPhysics().Get(ResolveBody(id));
                return b != nullptr ? static_cast<std::int64_t>(b->category) : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "phys_mask", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) -> std::int64_t {
                const BodyConfig *b = engine->GetPhysics().Get(ResolveBody(id));
                return b != nullptr ? static_cast<std::int64_t>(b->mask) : 0;
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

    // Interpolated render position (between the last two fixed steps); equals
    // phys_x / phys_y when interpolation is off or the frame ran no step.
    const auto smooth_getter = [engine](std::int64_t id, int field) -> double {
        const Vec2 p = engine->GetPhysics().SmoothPosition(ResolveBody(id));
        return static_cast<double>(field == 0 ? p.x : p.y);
    };
    fakelua::RegisterNativeFunction(state, "phys_smooth_x", false,
        std::function<double(fakelua::State *, std::int64_t)>(
            [smooth_getter](fakelua::State *, std::int64_t id) { return smooth_getter(id, 0); }));
    fakelua::RegisterNativeFunction(state, "phys_smooth_y", false,
        std::function<double(fakelua::State *, std::int64_t)>(
            [smooth_getter](fakelua::State *, std::int64_t id) { return smooth_getter(id, 1); }));

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

    // ---- Queries: ray casts -------------------------------------------------
    fakelua::RegisterNativeFunction(
        state, "phys_raycast", false,
        std::function<bool(fakelua::State *, double, double, double, double)>(
            [engine](fakelua::State *, double x1, double y1, double x2, double y2) -> bool {
                return engine->GetPhysics().RayCast(
                    {static_cast<float>(x1), static_cast<float>(y1)},
                    {static_cast<float>(x2), static_cast<float>(y2)});
            }));

    // Masked ray cast: only bodies whose category intersects `mask` are hit
    // (bitmask, e.g. 0x2 = category bit 1 only; 0xFFFF = everything).
    fakelua::RegisterNativeFunction(
        state, "phys_raycast_cat", false,
        std::function<bool(fakelua::State *, double, double, double, double, std::int64_t)>(
            [engine](fakelua::State *, double x1, double y1, double x2, double y2,
                     std::int64_t mask) -> bool {
                return engine->GetPhysics().RayCast(
                    {static_cast<float>(x1), static_cast<float>(y1)},
                    {static_cast<float>(x2), static_cast<float>(y2)},
                    static_cast<std::uint16_t>(mask));
            }));

    const auto ray_reader = [engine](int field) -> double {
        if (!engine->GetPhysics().HasRayHit()) {
            return 0.0;
        }
        const RayHit &hit = engine->GetPhysics().LastRayHit();
        switch (field) {
            case 0: return hit.point.x;
            case 1: return hit.point.y;
            case 2: return hit.normal.x;
            case 3: return hit.normal.y;
            case 4: return hit.distance;
            default: return static_cast<double>(hit.user_id);
        }
    };
    fakelua::RegisterNativeFunction(state, "phys_ray_hit_x", false,
        std::function<double(fakelua::State *)>(
            [ray_reader](fakelua::State *) { return ray_reader(0); }));
    fakelua::RegisterNativeFunction(state, "phys_ray_hit_y", false,
        std::function<double(fakelua::State *)>(
            [ray_reader](fakelua::State *) { return ray_reader(1); }));
    fakelua::RegisterNativeFunction(state, "phys_ray_hit_nx", false,
        std::function<double(fakelua::State *)>(
            [ray_reader](fakelua::State *) { return ray_reader(2); }));
    fakelua::RegisterNativeFunction(state, "phys_ray_hit_ny", false,
        std::function<double(fakelua::State *)>(
            [ray_reader](fakelua::State *) { return ray_reader(3); }));
    fakelua::RegisterNativeFunction(state, "phys_ray_hit_dist", false,
        std::function<double(fakelua::State *)>(
            [ray_reader](fakelua::State *) { return ray_reader(4); }));
    fakelua::RegisterNativeFunction(state, "phys_ray_hit_user", false,
        std::function<std::int64_t(fakelua::State *)>(
            [ray_reader](fakelua::State *) {
                return static_cast<std::int64_t>(ray_reader(5));
            }));

    // ---- Queries: shape overlaps (buffered) ---------------------------------
    fakelua::RegisterNativeFunction(
        state, "phys_overlap_circle", false,
        std::function<std::int64_t(fakelua::State *, double, double, double)>(
            [engine](fakelua::State *, double x, double y, double r) -> std::int64_t {
                return static_cast<std::int64_t>(engine->GetPhysics().OverlapCircle(
                    {static_cast<float>(x), static_cast<float>(y)},
                    static_cast<float>(r)));
            }));
    fakelua::RegisterNativeFunction(
        state, "phys_overlap_box", false,
        std::function<std::int64_t(fakelua::State *, double, double, double, double)>(
            [engine](fakelua::State *, double x, double y, double hw, double hh) -> std::int64_t {
                return static_cast<std::int64_t>(engine->GetPhysics().OverlapBox(
                    {static_cast<float>(x), static_cast<float>(y)},
                    {static_cast<float>(hw), static_cast<float>(hh)}));
            }));

    // Masked overlaps: only bodies whose category intersects `mask` count.
    fakelua::RegisterNativeFunction(
        state, "phys_overlap_circle_cat", false,
        std::function<std::int64_t(fakelua::State *, double, double, double, std::int64_t)>(
            [engine](fakelua::State *, double x, double y, double r,
                     std::int64_t mask) -> std::int64_t {
                return static_cast<std::int64_t>(engine->GetPhysics().OverlapCircle(
                    {static_cast<float>(x), static_cast<float>(y)},
                    static_cast<float>(r), static_cast<std::uint16_t>(mask)));
            }));
    fakelua::RegisterNativeFunction(
        state, "phys_overlap_box_cat", false,
        std::function<std::int64_t(fakelua::State *, double, double, double, double, std::int64_t)>(
            [engine](fakelua::State *, double x, double y, double hw, double hh,
                     std::int64_t mask) -> std::int64_t {
                return static_cast<std::int64_t>(engine->GetPhysics().OverlapBox(
                    {static_cast<float>(x), static_cast<float>(y)},
                    {static_cast<float>(hw), static_cast<float>(hh)},
                    static_cast<std::uint16_t>(mask)));
            }));
    fakelua::RegisterNativeFunction(state, "phys_overlap_id", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t i) -> std::int64_t {
                const BodyId id = i >= 0
                    ? engine->GetPhysics().OverlapBody(static_cast<std::size_t>(i))
                    : kInvalidBody;
                return static_cast<std::int64_t>(static_cast<std::uint64_t>(id));
            }));
    fakelua::RegisterNativeFunction(state, "phys_overlap_user", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t i) -> std::int64_t {
                return i >= 0
                    ? engine->GetPhysics().OverlapUser(static_cast<std::size_t>(i))
                    : 0;
            }));

    // ---- Contact lifecycle: stayed / ended ----------------------------------
    fakelua::RegisterNativeFunction(state, "phys_stay_count", false,
        std::function<std::int64_t(fakelua::State *)>(
            [engine](fakelua::State *) -> std::int64_t {
                return static_cast<std::int64_t>(
                    engine->GetPhysics().StayedContacts().size());
            }));
    fakelua::RegisterNativeFunction(state, "phys_end_count", false,
        std::function<std::int64_t(fakelua::State *)>(
            [engine](fakelua::State *) -> std::int64_t {
                return static_cast<std::int64_t>(
                    engine->GetPhysics().EndedContacts().size());
            }));

    const auto lifecycle_user = [engine](std::int64_t index, bool ended, bool b_side) -> std::int64_t {
        const auto &events = ended ? engine->GetPhysics().EndedContacts()
                                   : engine->GetPhysics().StayedContacts();
        if (index < 0 || static_cast<std::size_t>(index) >= events.size()) {
            return 0;
        }
        return b_side ? events[static_cast<std::size_t>(index)].user_b
                      : events[static_cast<std::size_t>(index)].user_a;
    };
    fakelua::RegisterNativeFunction(state, "phys_stay_user_a", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [lifecycle_user](fakelua::State *, std::int64_t i) {
                return lifecycle_user(i, false, false);
            }));
    fakelua::RegisterNativeFunction(state, "phys_stay_user_b", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [lifecycle_user](fakelua::State *, std::int64_t i) {
                return lifecycle_user(i, false, true);
            }));
    fakelua::RegisterNativeFunction(state, "phys_end_user_a", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [lifecycle_user](fakelua::State *, std::int64_t i) {
                return lifecycle_user(i, true, false);
            }));
    fakelua::RegisterNativeFunction(state, "phys_end_user_b", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [lifecycle_user](fakelua::State *, std::int64_t i) {
                return lifecycle_user(i, true, true);
            }));
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
        state, "anim_pause", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                engine->GetAnimations().Pause(static_cast<int>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "anim_resume", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                engine->GetAnimations().Resume(static_cast<int>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "anim_set_frame", false,
        std::function<void(fakelua::State *, std::int64_t, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id, std::int64_t index) {
                engine->GetAnimations().SetFrameIndex(
                    static_cast<int>(id), static_cast<int>(index));
            }));

    fakelua::RegisterNativeFunction(
        state, "anim_frame_index", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) -> std::int64_t {
                return engine->GetAnimations().FrameIndex(static_cast<int>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "anim_draw", false,
        std::function<void(fakelua::State *, double, double, double, double, double)>(
            [engine](fakelua::State *, double id, double x, double y, double w, double h) {
                // id is double on purpose: anim handles stored in EntityStore
                // slots arrive as floats and a long long param would reject
                // them (FakeluaToNativeLonglong).
                engine->GetAnimations().Draw(
                    static_cast<int>(id), engine->GetResources(),
                    engine->GetRenderer().GetSpriteBatch(),
                    {static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(w), static_cast<float>(h)});
            }));

    fakelua::RegisterNativeFunction(
        state, "anim_draw_flip", false,
        std::function<void(fakelua::State *, double, double, double, double, double,
                           bool, bool)>(
            [engine](fakelua::State *, double id, double x, double y, double w,
                     double h, bool flip_x, bool flip_y) {
                engine->GetAnimations().DrawFlipped(
                    static_cast<int>(id), engine->GetResources(),
                    engine->GetRenderer().GetSpriteBatch(),
                    {static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(w), static_cast<float>(h)},
                    flip_x, flip_y);
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

    // Whole map at a uniform scale with parallax ignored — the minimap bake.
    fakelua::RegisterNativeFunction(
        state, "map_draw_scaled", false,
        std::function<void(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double scale) {
                if (const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id))) {
                    map->DrawScaled(engine->GetRenderer(), static_cast<float>(scale));
                }
            }));

    // Cell indices/gids take double on the Lua side: math.floor keeps the
    // float type in FakeLua, so casts happen here at the native boundary.
    fakelua::RegisterNativeFunction(
        state, "map_solid", false,
        std::function<bool(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double col, double row) {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map != nullptr &&
                       map->SolidAt(static_cast<int>(col), static_cast<int>(row));
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

    fakelua::RegisterNativeFunction(
        state, "map_tilew", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) -> std::int64_t {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map ? map->TileWidth() : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_tileh", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) -> std::int64_t {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map ? map->TileHeight() : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_tile", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double col, double row) -> std::int64_t {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map ? static_cast<std::int64_t>(map->TileAt(static_cast<int>(col),
                                                                   static_cast<int>(row)))
                           : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_set_tile", false,
        std::function<void(fakelua::State *, std::int64_t, double, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double col, double row,
                     double gid) {
                if (gid < 0.0) {
                    return;
                }
                if (Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id))) {
                    map->SetTile(static_cast<int>(col), static_cast<int>(row),
                                 static_cast<std::uint32_t>(gid));
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "map_oneway", false,
        std::function<bool(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double col, double row) {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map != nullptr && map->OneWayAt(static_cast<int>(col),
                                                       static_cast<int>(row));
            }));

    fakelua::RegisterNativeFunction(
        state, "map_slope_dir", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double col,
                     double row) -> std::int64_t {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map ? map->SlopeDirAt(static_cast<int>(col),
                                             static_cast<int>(row))
                           : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_slope_rise", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double col,
                     double row) -> std::int64_t {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map ? map->SlopeRiseAt(static_cast<int>(col),
                                              static_cast<int>(row))
                           : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_ground_y", false,
        std::function<double(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double world_x,
                     double reach_y) -> double {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map ? static_cast<double>(
                                 map->GroundYAt(static_cast<float>(world_x),
                                                static_cast<float>(reach_y)))
                           : -1.0;
            }));

    // ---- grid path finding (A*, Phase 18) ----------------------------------
    // World endpoints snap to cells; results are buffered waypoints.
    fakelua::RegisterNativeFunction(
        state, "map_find_path", false,
        std::function<bool(fakelua::State *, std::int64_t, double, double, double, double,
                           bool)>(
            [engine](fakelua::State *, std::int64_t id, double sx, double sy,
                     double gx, double gy, bool diagonal) {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map != nullptr &&
                       map->FindPath(static_cast<float>(sx), static_cast<float>(sy),
                                     static_cast<float>(gx), static_cast<float>(gy),
                                     diagonal);
            }));

    fakelua::RegisterNativeFunction(
        state, "map_path_count", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) -> std::int64_t {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map ? static_cast<std::int64_t>(map->PathPointCount()) : 0;
            }));

    // Waypoint index is a double: indices usually come from a counter or an
    // EntityStore slot, and FakeLua passes those as floats (same reason
    // anim_draw takes its clip id as double).
    fakelua::RegisterNativeFunction(
        state, "map_path_x", false,
        std::function<double(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double i) -> double {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                if (!map || !std::isfinite(i) || i < 0.0) {
                    return 0.0;
                }
                return static_cast<double>(
                    map->PathPointAt(static_cast<std::size_t>(i)).x);
            }));

    fakelua::RegisterNativeFunction(
        state, "map_path_y", false,
        std::function<double(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double i) -> double {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                if (!map || !std::isfinite(i) || i < 0.0) {
                    return 0.0;
                }
                return static_cast<double>(
                    map->PathPointAt(static_cast<std::size_t>(i)).y);
            }));

    fakelua::RegisterNativeFunction(
        state, "map_clear_path", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                if (Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id))) {
                    map->ClearPath();
                }
            }));

    fakelua::RegisterNativeFunction(
        state, "map_layer_count", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) -> std::int64_t {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map ? static_cast<std::int64_t>(map->Layers().size()) : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_layer_parallax_x", false,
        std::function<double(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double idx) -> double {
                if (!std::isfinite(idx) || idx < 0.0) {
                    return 1.0;
                }
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                const auto i = static_cast<std::size_t>(idx);
                return (map && i < map->Layers().size())
                           ? static_cast<double>(map->Layers()[i].parallax_x)
                           : 1.0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_layer_parallax_y", false,
        std::function<double(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double idx) -> double {
                if (!std::isfinite(idx) || idx < 0.0) {
                    return 1.0;
                }
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                const auto i = static_cast<std::size_t>(idx);
                return (map && i < map->Layers().size())
                           ? static_cast<double>(map->Layers()[i].parallax_y)
                           : 1.0;
            }));

    // --- Tiled object layers (objectgroup) --------------------------------
    // Object layers are authored spawn/trigger data: points, rectangles,
    // ellipses and polylines with custom properties. They are never drawn.
    auto get_object = [engine](std::int64_t id, double layer,
                               double index) -> const MapObject * {
        if (!std::isfinite(layer) || layer < 0.0 || !std::isfinite(index) ||
            index < 0.0) {
            return nullptr;
        }
        const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
        if (map == nullptr) {
            return nullptr;
        }
        const auto *objects = map->ObjectsAt(static_cast<std::size_t>(layer));
        if (objects == nullptr) {
            return nullptr;
        }
        const auto i = static_cast<std::size_t>(index);
        return i < objects->size() ? &(*objects)[i] : nullptr;
    };

    fakelua::RegisterNativeFunction(
        state, "map_find_layer", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t,
                                   std::string_view)>(
            [engine](fakelua::State *, std::int64_t id,
                     std::string_view name) -> std::int64_t {
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map ? static_cast<std::int64_t>(map->FindLayer(name)) : -1;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_layer_type", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double idx) -> std::int64_t {
                if (!std::isfinite(idx) || idx < 0.0) {
                    return -1;
                }
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                return map ? static_cast<std::int64_t>(
                                 map->LayerTypeAt(static_cast<std::size_t>(idx)))
                           : -1;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_count", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double idx) -> std::int64_t {
                if (!std::isfinite(idx) || idx < 0.0) {
                    return 0;
                }
                const Tilemap *map = engine->GetTilemaps().Get(static_cast<int>(id));
                const auto *objects =
                    map ? map->ObjectsAt(static_cast<std::size_t>(idx)) : nullptr;
                return objects ? static_cast<std::int64_t>(objects->size()) : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_id", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t, double, double)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index) -> std::int64_t {
                const MapObject *obj = get_object(id, layer, index);
                return obj ? static_cast<std::int64_t>(obj->id) : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_name", false,
        std::function<std::string_view(fakelua::State *, std::int64_t, double,
                                       double)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index) -> std::string_view {
                const MapObject *obj = get_object(id, layer, index);
                return obj ? std::string_view{obj->name} : std::string_view{};
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_class", false,
        std::function<std::string_view(fakelua::State *, std::int64_t, double,
                                       double)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index) -> std::string_view {
                const MapObject *obj = get_object(id, layer, index);
                return obj ? std::string_view{obj->class_name} : std::string_view{};
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_x", false,
        std::function<double(fakelua::State *, std::int64_t, double, double)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index) -> double {
                const MapObject *obj = get_object(id, layer, index);
                return obj ? static_cast<double>(obj->x) : 0.0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_y", false,
        std::function<double(fakelua::State *, std::int64_t, double, double)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index) -> double {
                const MapObject *obj = get_object(id, layer, index);
                return obj ? static_cast<double>(obj->y) : 0.0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_w", false,
        std::function<double(fakelua::State *, std::int64_t, double, double)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index) -> double {
                const MapObject *obj = get_object(id, layer, index);
                return obj ? static_cast<double>(obj->width) : 0.0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_h", false,
        std::function<double(fakelua::State *, std::int64_t, double, double)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index) -> double {
                const MapObject *obj = get_object(id, layer, index);
                return obj ? static_cast<double>(obj->height) : 0.0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_rotation", false,
        std::function<double(fakelua::State *, std::int64_t, double, double)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index) -> double {
                const MapObject *obj = get_object(id, layer, index);
                return obj ? static_cast<double>(obj->rotation) : 0.0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_visible", false,
        std::function<bool(fakelua::State *, std::int64_t, double, double)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index) {
                const MapObject *obj = get_object(id, layer, index);
                // A bad reference reads as invisible so callers skip it.
                return obj != nullptr && obj->visible;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_shape", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t, double, double)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index) -> std::int64_t {
                const MapObject *obj = get_object(id, layer, index);
                return obj ? static_cast<std::int64_t>(obj->shape) : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_point_count", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t, double, double)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index) -> std::int64_t {
                const MapObject *obj = get_object(id, layer, index);
                return obj ? static_cast<std::int64_t>(obj->points.size()) : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_point_x", false,
        std::function<double(fakelua::State *, std::int64_t, double, double,
                             double)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index, double point) -> double {
                const MapObject *obj = get_object(id, layer, index);
                if (obj == nullptr || point < 0.0) {
                    return 0.0;
                }
                const auto p = static_cast<std::size_t>(point);
                return p < obj->points.size()
                           ? static_cast<double>(obj->points[p].x)
                           : 0.0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_point_y", false,
        std::function<double(fakelua::State *, std::int64_t, double, double,
                             double)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index, double point) -> double {
                const MapObject *obj = get_object(id, layer, index);
                if (obj == nullptr || point < 0.0) {
                    return 0.0;
                }
                const auto p = static_cast<std::size_t>(point);
                return p < obj->points.size()
                           ? static_cast<double>(obj->points[p].y)
                           : 0.0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_has_prop", false,
        std::function<bool(fakelua::State *, std::int64_t, double, double,
                           std::string_view)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index, std::string_view key) {
                const MapObject *obj = get_object(id, layer, index);
                return obj != nullptr && obj->HasProperty(key);
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_prop_bool", false,
        std::function<bool(fakelua::State *, std::int64_t, double, double,
                           std::string_view)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index, std::string_view key) {
                const MapObject *obj = get_object(id, layer, index);
                return obj != nullptr && obj->PropBool(key);
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_prop_num", false,
        std::function<double(fakelua::State *, std::int64_t, double, double,
                             std::string_view)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index, std::string_view key) -> double {
                const MapObject *obj = get_object(id, layer, index);
                return obj ? obj->PropNum(key) : 0.0;
            }));

    fakelua::RegisterNativeFunction(
        state, "map_obj_prop_str", false,
        std::function<std::string_view(fakelua::State *, std::int64_t, double,
                                       double, std::string_view)>(
            [get_object](fakelua::State *, std::int64_t id, double layer,
                         double index, std::string_view key)
                -> std::string_view {
                const MapObject *obj = get_object(id, layer, index);
                return obj ? obj->PropStr(key) : std::string_view{};
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

    // ---- widgets (Phase 20) -------------------------------------------------
    // Values stay in the script: ui_slider returns the updated value and
    // ui_checkbox returns true on the frame it was toggled, so the caller
    // owns the state (same contract as ui_button).
    fakelua::RegisterNativeFunction(
        state, "ui_slider", false,
        std::function<double(fakelua::State *, std::string_view, std::int64_t,
                             double, double, double, double, double, double, double)>(
            [engine](fakelua::State *, std::string_view key, std::int64_t anchor,
                     double ox, double oy, double w, double h, double value,
                     double min_value, double max_value) -> double {
                return engine->GetUI().Slider(
                    key, static_cast<UIAnchor>(anchor), static_cast<float>(ox),
                    static_cast<float>(oy), static_cast<float>(w),
                    static_cast<float>(h), static_cast<float>(value),
                    static_cast<float>(min_value), static_cast<float>(max_value));
            }));

    fakelua::RegisterNativeFunction(
        state, "ui_checkbox", false,
        std::function<bool(fakelua::State *, std::string_view, std::int64_t,
                           double, double, double, bool, std::string_view, double)>(
            [engine](fakelua::State *, std::string_view key, std::int64_t anchor,
                     double ox, double oy, double size, bool checked,
                     std::string_view label, double scale) -> bool {
                return engine->GetUI().Checkbox(
                    key, static_cast<UIAnchor>(anchor), static_cast<float>(ox),
                    static_cast<float>(oy), static_cast<float>(size), checked,
                    label, static_cast<float>(scale));
            }));

    // ---- widgets round 2 (Phase 25) ----------------------------------------
    // A progress bar is a pure draw: value clamped to 0..1 fills w from the
    // left, and there is no state to keep.
    fakelua::RegisterNativeFunction(
        state, "ui_bar", false,
        std::function<void(fakelua::State *, std::int64_t, double, double, double,
                           double, double, double, double, double, double)>(
            [engine](fakelua::State *, std::int64_t anchor, double ox, double oy,
                     double w, double h, double value, double r, double g,
                     double b, double a) {
                engine->GetUI().ProgressBar(
                    static_cast<UIAnchor>(anchor), static_cast<float>(ox),
                    static_cast<float>(oy), static_cast<float>(w),
                    static_cast<float>(h), static_cast<float>(value),
                    Color{static_cast<float>(r), static_cast<float>(g),
                          static_cast<float>(b), static_cast<float>(a)});
            }));

    // One radio row; the caller owns which index is selected (see the header).
    fakelua::RegisterNativeFunction(
        state, "ui_radio", false,
        std::function<bool(fakelua::State *, std::string_view, std::int64_t, double,
                           double, double, bool, std::string_view, double)>(
            [engine](fakelua::State *, std::string_view key, std::int64_t anchor,
                     double ox, double oy, double size, bool selected,
                     std::string_view label, double scale) -> bool {
                return engine->GetUI().RadioButton(
                    key, static_cast<UIAnchor>(anchor), static_cast<float>(ox),
                    static_cast<float>(oy), static_cast<float>(size), selected,
                    label, static_cast<float>(scale));
            }));

    // A combo is built from one header call plus one call per row: the
    // popup's open state lives in the script, so nothing about the list is
    // hidden inside the widget.
    fakelua::RegisterNativeFunction(
        state, "ui_combo", false,
        std::function<bool(fakelua::State *, std::string_view, std::int64_t, double,
                           double, double, double, std::string_view, bool, double)>(
            [engine](fakelua::State *, std::string_view key, std::int64_t anchor,
                     double ox, double oy, double w, double h,
                     std::string_view label, bool open, double scale) -> bool {
                return engine->GetUI().ComboBegin(
                    key, static_cast<UIAnchor>(anchor), static_cast<float>(ox),
                    static_cast<float>(oy), static_cast<float>(w),
                    static_cast<float>(h), label, open,
                    static_cast<float>(scale));
            }));

    // A titled panel dragged by its bar; the position lives in the script, so
    // the caller needs the per-frame mouse delta to move it.
    fakelua::RegisterNativeFunction(
        state, "ui_window", false,
        std::function<bool(fakelua::State *, std::string_view, std::int64_t, double,
                           double, double, double, std::string_view, double)>(
            [engine](fakelua::State *, std::string_view key, std::int64_t anchor,
                     double ox, double oy, double w, double h,
                     std::string_view title, double scale) -> bool {
                return engine->GetUI().Window(
                    key, static_cast<UIAnchor>(anchor), static_cast<float>(ox),
                    static_cast<float>(oy), static_cast<float>(w),
                    static_cast<float>(h), title, static_cast<float>(scale));
            }));

    // One tab of a tab bar; which tab is active is the caller's.
    fakelua::RegisterNativeFunction(
        state, "ui_tab", false,
        std::function<bool(fakelua::State *, std::string_view, std::int64_t, double,
                           double, double, double, std::string_view, bool, double)>(
            [engine](fakelua::State *, std::string_view key, std::int64_t anchor,
                     double ox, double oy, double w, double h,
                     std::string_view label, bool active, double scale) -> bool {
                return engine->GetUI().Tab(
                    key, static_cast<UIAnchor>(anchor), static_cast<float>(ox),
                    static_cast<float>(oy), static_cast<float>(w),
                    static_cast<float>(h), label, active,
                    static_cast<float>(scale));
            }));

    // A disclosure row; `open` is the caller's, so children are an if-block.
    fakelua::RegisterNativeFunction(
        state, "ui_tree", false,
        std::function<bool(fakelua::State *, std::string_view, std::int64_t, double,
                           double, double, double, std::string_view, bool, double)>(
            [engine](fakelua::State *, std::string_view key, std::int64_t anchor,
                     double ox, double oy, double w, double h,
                     std::string_view label, bool open, double scale) -> bool {
                return engine->GetUI().Tree(
                    key, static_cast<UIAnchor>(anchor), static_cast<float>(ox),
                    static_cast<float>(oy), static_cast<float>(w),
                    static_cast<float>(h), label, open,
                    static_cast<float>(scale));
            }));

    fakelua::RegisterNativeFunction(
        state, "ui_combo_item", false,
        std::function<bool(fakelua::State *, std::string_view, std::int64_t, double,
                           double, double, double, std::string_view, bool, double)>(
            [engine](fakelua::State *, std::string_view key, std::int64_t anchor,
                     double ox, double oy, double w, double h,
                     std::string_view label, bool selected, double scale) -> bool {
                return engine->GetUI().ComboItem(
                    key, static_cast<UIAnchor>(anchor), static_cast<float>(ox),
                    static_cast<float>(oy), static_cast<float>(w),
                    static_cast<float>(h), label, selected,
                    static_cast<float>(scale));
            }));

    // The field's text lives in the UI system (scripts cannot own a mutable
    // string), so read it back with ui_text_get and seed it with ui_text_set.
    fakelua::RegisterNativeFunction(
        state, "ui_text_input", false,
        std::function<bool(fakelua::State *, std::string_view, std::int64_t,
                           double, double, double, double, std::int64_t)>(
            [engine](fakelua::State *, std::string_view key, std::int64_t anchor,
                     double ox, double oy, double w, double h,
                     std::int64_t max_chars) -> bool {
                return engine->GetUI().TextInput(
                    key, static_cast<UIAnchor>(anchor), static_cast<float>(ox),
                    static_cast<float>(oy), static_cast<float>(w),
                    static_cast<float>(h), static_cast<std::size_t>(max_chars));
            }));

    fakelua::RegisterNativeFunction(
        state, "ui_text_get", false,
        std::function<std::string(fakelua::State *, std::string_view)>(
            [engine](fakelua::State *, std::string_view key) -> std::string {
                return std::string(engine->GetUI().TextValue(key));
            }));

    fakelua::RegisterNativeFunction(
        state, "ui_text_set", false,
        std::function<void(fakelua::State *, std::string_view, std::string_view)>(
            [engine](fakelua::State *, std::string_view key, std::string_view text) {
                engine->GetUI().SetTextValue(key, text);
            }));

    fakelua::RegisterNativeFunction(
        state, "ui_scroll", false,
        std::function<double(fakelua::State *, std::string_view, std::int64_t,
                             double, double, double, double, double, double)>(
            [engine](fakelua::State *, std::string_view key, std::int64_t anchor,
                     double ox, double oy, double w, double h, double content_h,
                     double scroll_y) -> double {
                return engine->GetUI().ScrollArea(
                    key, static_cast<UIAnchor>(anchor), static_cast<float>(ox),
                    static_cast<float>(oy), static_cast<float>(w),
                    static_cast<float>(h), static_cast<float>(content_h),
                    static_cast<float>(scroll_y));
            }));

    fakelua::RegisterNativeFunction(
        state, "ui_hovered", false,
        std::function<bool(fakelua::State *, std::string_view)>(
            [engine](fakelua::State *, std::string_view key) -> bool {
                return engine->GetUI().IsHovered(key);
            }));

    fakelua::RegisterNativeFunction(
        state, "ui_text_focused", false,
        std::function<std::string(fakelua::State *)>(
            [engine](fakelua::State *) -> std::string {
                return std::string(engine->GetUI().FocusedTextInput());
            }));

    fakelua::RegisterNativeFunction(
        state, "ui_set_origin", false,
        std::function<void(fakelua::State *, double, double)>(
            [engine](fakelua::State *, double x, double y) {
                engine->GetUI().SetWorldOrigin({static_cast<float>(x),
                                                static_cast<float>(y)});
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

    // Aligned, word-wrapped text. align: 0 left / 1 center / 2 right; the box
    // top-left is (x, y) and its width is wrap_w (0 = single line, aligned
    // around x). Returns the y just below the last line.
    fakelua::RegisterNativeFunction(
        state, "draw_text_ex", false,
        std::function<double(fakelua::State *, std::string_view, double, double, double,
                             double, double, double, double, std::int64_t, double)>(
            [engine](fakelua::State *, std::string_view text, double x, double y, double scale,
                     double r, double g, double b, double a, std::int64_t align,
                     double wrap_w) -> double {
                return static_cast<double>(engine->GetRenderer().DrawTextEx(
                    text, static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(scale), static_cast<int>(align),
                    static_cast<float>(wrap_w),
                    Color{static_cast<float>(r), static_cast<float>(g),
                          static_cast<float>(b), static_cast<float>(a)}));
            }));

    // Nine-slice panel: corners keep their source pixel size, edges/center
    // stretch (dialog frames at any size without blur).
    const auto nine_slice = [engine](fakelua::State *, std::int64_t handle, double sx,
                                     double sy, double sw,
                                     double sh, double dx, double dy, double dw, double dh,
                                     double cx, double cy, double r, double g, double b,
                                     double a) {
        const Texture2D *texture =
            engine->GetResources().GetTexture(ResolveTexture(handle));
        if (texture) {
            engine->GetRenderer().DrawNineSlice(
                *texture,
                Rect{static_cast<float>(sx), static_cast<float>(sy),
                     static_cast<float>(sw), static_cast<float>(sh)},
                Rect{static_cast<float>(dx), static_cast<float>(dy),
                     static_cast<float>(dw), static_cast<float>(dh)},
                static_cast<float>(cx), static_cast<float>(cy),
                Color{static_cast<float>(r), static_cast<float>(g),
                      static_cast<float>(b), static_cast<float>(a)});
        }
    };
    fakelua::RegisterNativeFunction(
        state, "draw_nine_slice", false,
        std::function<void(fakelua::State *, std::int64_t, double, double, double, double,
                           double, double, double, double, double, double, double, double,
                           double, double)>(nine_slice));
    // Untinted convenience overload.
    fakelua::RegisterNativeFunction(
        state, "draw_nine_slice_white", false,
        std::function<void(fakelua::State *, std::int64_t, double, double, double, double,
                           double, double, double, double, double, double)>(
            [nine_slice](fakelua::State *s, std::int64_t handle, double sx, double sy,
                         double sw, double sh, double dx, double dy, double dw, double dh,
                         double cx, double cy) {
                nine_slice(s, handle, sx, sy, sw, sh, dx, dy, dw, dh, cx, cy,
                           1.0, 1.0, 1.0, 1.0);
            }));

    // Scissor clip in logical points (top-left origin); cleared each frame.
    fakelua::RegisterNativeFunction(
        state, "draw_set_clip", false,
        std::function<void(fakelua::State *, double, double, double, double)>(
            [engine](fakelua::State *, double x, double y, double w, double h) {
                engine->GetRenderer().SetClipRect(Rect{
                    static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), static_cast<float>(h)});
            }));

    fakelua::RegisterNativeFunction(
        state, "draw_clear_clip", false,
        std::function<void(fakelua::State *)>([engine](fakelua::State *) {
            engine->GetRenderer().ClearClipRect();
        }));

    // ---- offscreen render targets (Phase 19) --------------------------------
    // Render into a target with rt_begin / rt_end, then blit it with rt_draw.
    // Inside a target, (0,0) is the target's own top-left and 1 unit = 1
    // pixel; rt_clear resets the bound surface (the screen when none is).
    fakelua::RegisterNativeFunction(
        state, "rt_create", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t w, std::int64_t h) -> std::int64_t {
                return engine->GetRenderer().GetRenderTargets().Create(
                    static_cast<int>(w), static_cast<int>(h));
            }));

    fakelua::RegisterNativeFunction(
        state, "rt_destroy", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                Renderer &r = engine->GetRenderer();
                // Destroying the bound surface would leave drawing pointed at
                // a deleted FBO; leave the target first.
                if (r.ActiveRenderTarget() == static_cast<int>(id)) {
                    r.EndRenderTarget();
                }
                r.GetRenderTargets().Destroy(static_cast<int>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "rt_valid", false,
        std::function<bool(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                const RenderTarget *t =
                    engine->GetRenderer().GetRenderTargets().Get(static_cast<int>(id));
                return t != nullptr && t->IsValid();
            }));

    fakelua::RegisterNativeFunction(
        state, "rt_width", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) -> std::int64_t {
                const RenderTarget *t =
                    engine->GetRenderer().GetRenderTargets().Get(static_cast<int>(id));
                return t != nullptr ? t->Width() : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "rt_height", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) -> std::int64_t {
                const RenderTarget *t =
                    engine->GetRenderer().GetRenderTargets().Get(static_cast<int>(id));
                return t != nullptr ? t->Height() : 0;
            }));

    fakelua::RegisterNativeFunction(
        state, "rt_begin", false,
        std::function<bool(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                return engine->GetRenderer().BeginRenderTarget(static_cast<int>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "rt_end", false,
        std::function<void(fakelua::State *)>([engine](fakelua::State *) {
            engine->GetRenderer().EndRenderTarget();
        }));

    fakelua::RegisterNativeFunction(
        state, "rt_clear", false,
        std::function<void(fakelua::State *, double, double, double, double)>(
            [engine](fakelua::State *, double r, double g, double b, double a) {
                engine->GetRenderer().ClearActiveTarget(Color{
                    static_cast<float>(r), static_cast<float>(g),
                    static_cast<float>(b), static_cast<float>(a)});
            }));

    fakelua::RegisterNativeFunction(
        state, "rt_draw", false,
        std::function<void(fakelua::State *, std::int64_t, double, double, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double x, double y, double w,
                     double h) {
                engine->GetRenderer().DrawRenderTarget(
                    static_cast<int>(id), static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), static_cast<float>(h));
            }));

    // Post-processing present: blit a target across the whole of whatever is
    // being drawn into (the screen, or another target for a ping-pong pass)
    // through a shader. id 0 = the window (never presentable), shader 0 =
    // no override.
    fakelua::RegisterNativeFunction(
        state, "rt_present", false,
        std::function<void(fakelua::State *, std::int64_t, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id, std::int64_t shader) {
                Shader *program = shader == 0 ? nullptr
                                              : engine->GetRenderer().GetShader(
                                                    static_cast<int>(shader));
                engine->GetRenderer().PresentRenderTarget(static_cast<int>(id),
                                                          program);
            }));

    // --- multi-pass chains (Phase 24) --------------------------------------
    // One ping-pong pass: src -> dst through an optional shader. Refuses
    // src == dst (a surface cannot be sampled while it is being drawn into),
    // the window as either side, and any nesting.
    fakelua::RegisterNativeFunction(
        state, "rt_copy", false,
        std::function<bool(fakelua::State *, std::int64_t, std::int64_t, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t src, std::int64_t dst,
                     std::int64_t shader) -> bool {
                Shader *program = shader == 0 ? nullptr
                                              : engine->GetRenderer().GetShader(
                                                    static_cast<int>(shader));
                return engine->GetRenderer().CopyRenderTarget(
                    static_cast<int>(src), static_cast<int>(dst), program);
            }));

    // Reallocate a target at a new size (contents lost). Half- and quarter
    // resolution passes are why this exists.
    fakelua::RegisterNativeFunction(
        state, "rt_resize", false,
        std::function<bool(fakelua::State *, std::int64_t, std::int64_t, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id, std::int64_t w,
                     std::int64_t h) -> bool {
                return engine->GetRenderer().ResizeRenderTarget(
                    static_cast<int>(id), static_cast<int>(w), static_cast<int>(h));
            }));

    // Point the bound target's canvas camera at a world offset: whole-world
    // draws (map_draw) then render the camera's view into the target, which
    // is what a post-processing pass needs. Returns false when no target is
    // bound.
    fakelua::RegisterNativeFunction(
        state, "rt_camera", false,
        std::function<bool(fakelua::State *, double, double)>(
            [engine](fakelua::State *, double x, double y) -> bool {
                return engine->GetRenderer().SetTargetCamera(
                    static_cast<float>(x), static_cast<float>(y));
            }));

    fakelua::RegisterNativeFunction(
        state, "rt_active", false,
        std::function<std::int64_t(fakelua::State *)>(
            [engine](fakelua::State *) -> std::int64_t {
                return engine->GetRenderer().ActiveRenderTarget();
            }));

    // --- mask sampling (Phase 26) ------------------------------------------
    // Read one pixel back out of a target as a single scalar — the query side
    // of an exploration mask, so "has this point been revealed?" is gameplay
    // state rather than pixels nobody can inspect. Returns -1 for anything
    // that is not a real sample (unknown id, out-of-bounds coordinates), which
    // keeps "0 = unexplored" from being confused with "no such target".
    fakelua::RegisterNativeFunction(
        state, "rt_sample", false,
        std::function<double(fakelua::State *, std::int64_t, std::int64_t,
                             std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id, std::int64_t x,
                     std::int64_t y) -> double {
                const fake2d::RenderTarget *target =
                    engine->GetRenderer().GetRenderTargets().Get(static_cast<int>(id));
                if (target == nullptr) {
                    return -1.0;
                }
                fake2d::Color px{};
                if (!target->ReadPixel(static_cast<int>(x), static_cast<int>(y), px)) {
                    return -1.0;
                }
                return static_cast<double>(px.r * 0.299f + px.g * 0.587f +
                                           px.b * 0.114f);
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

    // `fade` > 0 cross-fades: the incoming track ramps up while the outgoing
    // one ramps down, so switching never clicks. 0 cuts over.
    fakelua::RegisterNativeFunction(
        state, "audio_music", false,
        std::function<void(fakelua::State *, std::string_view, double, double)>(
            [engine](fakelua::State *, std::string_view name, double volume,
                     double fade) {
                engine->GetAudio().PlayMusic(name, static_cast<float>(volume),
                                             static_cast<float>(fade));
            }));

    fakelua::RegisterNativeFunction(
        state, "audio_music_stop", false,
        std::function<void(fakelua::State *, double)>(
            [engine](fakelua::State *, double fade) {
                engine->GetAudio().StopMusic(static_cast<float>(fade));
            }));

    fakelua::RegisterNativeFunction(
        state, "audio_music_fade", false,
        std::function<void(fakelua::State *, double, double)>(
            [engine](fakelua::State *, double volume, double seconds) {
                engine->GetAudio().FadeMusicTo(static_cast<float>(volume),
                                               static_cast<float>(seconds));
            }));

    // --- mixing (Phase 22) ---------------------------------------------------
    // Bus ids: 0 master, 1 sfx, 2 music. Every voice is multiplied by its
    // own bus and by master, so effective = voice * bus * master.
    fakelua::RegisterNativeFunction(
        state, "audio_bus_set", false,
        std::function<void(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t bus, double volume) {
                engine->GetAudio().SetBusVolume(static_cast<AudioBus>(bus),
                                                static_cast<float>(volume));
            }));

    fakelua::RegisterNativeFunction(
        state, "audio_bus_get", false,
        std::function<double(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t bus) -> double {
                return engine->GetAudio().BusVolume(static_cast<AudioBus>(bus));
            }));

    // Pause freezes the mix: silence out, and neither cursors nor fades
    // advance, so resuming picks up exactly where it stopped.
    fakelua::RegisterNativeFunction(
        state, "audio_pause", false,
        std::function<void(fakelua::State *, bool)>(
            [engine](fakelua::State *, bool paused) {
                engine->GetAudio().SetPaused(paused);
            }));

    fakelua::RegisterNativeFunction(
        state, "audio_paused", false,
        std::function<bool(fakelua::State *)>(
            [engine](fakelua::State *) { return engine->GetAudio().IsPaused(); }));

    fakelua::RegisterNativeFunction(
        state, "audio_music_playing", false,
        std::function<bool(fakelua::State *)>(
            [engine](fakelua::State *) {
                return engine->GetAudio().IsMusicPlaying();
            }));

    fakelua::RegisterNativeFunction(
        state, "audio_music_name", false,
        std::function<std::string(fakelua::State *)>(
            [engine](fakelua::State *) -> std::string {
                return std::string(engine->GetAudio().MusicName());
            }));

    // Loudest sample of the last mixed block — drives VU meters. Reads 0
    // when no device is consuming the mixer (headless).
    fakelua::RegisterNativeFunction(
        state, "audio_peak", false,
        std::function<double(fakelua::State *)>(
            [engine](fakelua::State *) -> double {
                return engine->GetAudio().Peak();
            }));

    // --- positional audio (Phase 23) ---------------------------------------
    // `audio_play_at` places a one-shot in the world; it is panned left/right
    // and attenuated by distance against the listener, and re-evaluated on
    // every audio_listener call, so sounds follow you as you walk past.
    fakelua::RegisterNativeFunction(
        state, "audio_play_at", false,
        std::function<void(fakelua::State *, std::string_view, double, double, double)>(
            [engine](fakelua::State *, std::string_view name, double volume,
                     double x, double y) {
                engine->GetAudio().PlayAt(name, static_cast<float>(volume),
                                          static_cast<float>(x),
                                          static_cast<float>(y));
            }));

    fakelua::RegisterNativeFunction(
        state, "audio_listener", false,
        std::function<void(fakelua::State *, double, double)>(
            [engine](fakelua::State *, double x, double y) {
                engine->GetAudio().SetListener(static_cast<float>(x),
                                               static_cast<float>(y));
            }));

    // Distance at which a placed voice goes silent (twice it reaches full
    // stereo separation). <= 0 is ignored.
    fakelua::RegisterNativeFunction(
        state, "audio_range", false,
        std::function<void(fakelua::State *, double)>(
            [engine](fakelua::State *, double max_distance) {
                engine->GetAudio().SetListenerRange(static_cast<float>(max_distance));
            }));

    fakelua::RegisterNativeFunction(
        state, "audio_peak_l", false,
        std::function<double(fakelua::State *)>(
            [engine](fakelua::State *) -> double {
                return engine->GetAudio().PeakLeft();
            }));

    fakelua::RegisterNativeFunction(
        state, "audio_peak_r", false,
        std::function<double(fakelua::State *)>(
            [engine](fakelua::State *) -> double {
                return engine->GetAudio().PeakRight();
            }));

    // --- decoded files & streamed tracks (Phase 29) -------------------------
    // `audio_load_file` decodes the whole file (WAV/FLAC/MP3/OGG-Vorbis,
    // whatever miniaudio reads) into a clip; `audio_stream` registers the same
    // file without decoding it and reads it in windows as it plays, which is
    // what a long music track wants. Both are played by the existing
    // `audio_music` / `audio_play` — a stream is a clip whose samples arrive
    // on demand.
    fakelua::RegisterNativeFunction(
        state, "audio_load_file", false,
        std::function<bool(fakelua::State *, std::string_view, std::string_view)>(
            [engine](fakelua::State *, std::string_view name, std::string_view path) {
                return engine->GetAudio().AddClipFile(name, std::string(path));
            }));

    fakelua::RegisterNativeFunction(
        state, "audio_stream", false,
        std::function<bool(fakelua::State *, std::string_view, std::string_view)>(
            [engine](fakelua::State *, std::string_view name, std::string_view path) {
                return engine->GetAudio().AddStream(name, std::string(path));
            }));

    fakelua::RegisterNativeFunction(
        state, "audio_clip_len", false,
        std::function<double(fakelua::State *, std::string_view)>(
            [engine](fakelua::State *, std::string_view name) -> double {
                return engine->GetAudio().ClipDuration(name);
            }));

    // Frames currently buffered: the whole clip for a decoded file, one window
    // for a stream — the tell that a track is really being streamed.
    fakelua::RegisterNativeFunction(
        state, "audio_clip_cached", false,
        std::function<double(fakelua::State *, std::string_view)>(
            [engine](fakelua::State *, std::string_view name) -> double {
                return static_cast<double>(engine->GetAudio().ResidentFrames(name));
            }));
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
                // 0 alpha (normal transparency), 1 additive (glow), 2 multiply
                // (masks: result = source * destination). Anything else falls
                // back to alpha rather than picking a random GL state.
                engine->GetRenderer().SetBlendMode(
                    mode == 1 ? BlendMode::Additive
                              : (mode == 2 ? BlendMode::Multiply : BlendMode::Alpha));
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

    // ---- Follow / bounds (Phase 16) ---------------------------------------
    // world rectangle the view may never leave; the camera centre is clamped
    // (or centred when the level is smaller than the view on that axis).
    fakelua::RegisterNativeFunction(
        state, "camera_set_bounds", false,
        std::function<void(fakelua::State *, double, double, double, double)>(
            [&camera](fakelua::State *, double x, double y, double w, double h) {
                camera.SetBounds(static_cast<float>(x), static_cast<float>(y),
                                 static_cast<float>(w), static_cast<float>(h));
            }));

    fakelua::RegisterNativeFunction(
        state, "camera_clear_bounds", false,
        std::function<void(fakelua::State *)>([&camera](fakelua::State *) {
            camera.ClearBounds();
        }));

    fakelua::RegisterNativeFunction(
        state, "camera_deadzone", false,
        std::function<void(fakelua::State *, double, double)>(
            [&camera](fakelua::State *, double hw, double hh) {
                camera.SetDeadzone(static_cast<float>(hw), static_cast<float>(hh));
            }));

    // SMB-style scroll lock: while following, the camera never moves left.
    fakelua::RegisterNativeFunction(
        state, "camera_forward_only", false,
        std::function<void(fakelua::State *, bool)>(
            [&camera](fakelua::State *, bool enabled) {
                camera.SetForwardOnlyX(enabled);
            }));

    fakelua::RegisterNativeFunction(
        state, "camera_smoothing", false,
        std::function<void(fakelua::State *, double)>(
            [&camera](fakelua::State *, double rate) {
                camera.SetFollowSmoothing(static_cast<float>(rate));
            }));

    // Smooth follow of a world point (deadzone + smoothing + bounds). The
    // engine supplies the frame delta, so scripts just pass the target.
    fakelua::RegisterNativeFunction(
        state, "camera_follow", false,
        std::function<void(fakelua::State *, double, double)>(
            [engine, &camera](fakelua::State *, double x, double y) {
                camera.Follow(static_cast<float>(x), static_cast<float>(y),
                              static_cast<float>(engine->DeltaTime()));
            }));

    fakelua::RegisterNativeFunction(
        state, "camera_snap", false,
        std::function<void(fakelua::State *, double, double)>(
            [&camera](fakelua::State *, double x, double y) {
                camera.SnapTo({static_cast<float>(x), static_cast<float>(y)});
            }));

    fakelua::RegisterNativeFunction(
        state, "camera_viewport_w", false,
        std::function<double(fakelua::State *)>(
            [&camera](fakelua::State *) {
                return static_cast<double>(camera.ViewportWidth());
            }));

    fakelua::RegisterNativeFunction(
        state, "camera_viewport_h", false,
        std::function<double(fakelua::State *)>(
            [&camera](fakelua::State *) {
                return static_cast<double>(camera.ViewportHeight());
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

    fakelua::RegisterNativeFunction(
        state, "sprite_draw_flip", false,
        std::function<void(fakelua::State *, std::int64_t, double, double, double, double,
                           double, double, double, double, double, double)>(
            [resources, renderer](fakelua::State *, std::int64_t handle,
                                  double sx, double sy, double sw, double sh,
                                  double dx, double dy, double dw, double dh,
                                  double flip_x, double flip_y) {
                const Texture2D *texture = resources->GetTexture(ResolveTexture(handle));
                if (texture) {
                    renderer->DrawSpriteFlipped(
                        *texture,
                        {static_cast<float>(sx), static_cast<float>(sy),
                         static_cast<float>(sw), static_cast<float>(sh)},
                        {static_cast<float>(dx), static_cast<float>(dy),
                         static_cast<float>(dw), static_cast<float>(dh)},
                        flip_x != 0.0, flip_y != 0.0);
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

    // Per-frame movement of the cursor. This is what a drag needs: a window,
    // a scrollbar or a node editor all move by the delta, not to the position.
    fakelua::RegisterNativeFunction(
        state, "input_mouse_dx", false,
        std::function<double(fakelua::State *)>(
            [input](fakelua::State *) { return static_cast<double>(input->MouseDelta().x); }));

    fakelua::RegisterNativeFunction(
        state, "input_mouse_dy", false,
        std::function<double(fakelua::State *)>(
            [input](fakelua::State *) { return static_cast<double>(input->MouseDelta().y); }));

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

    fakelua::RegisterNativeFunction(
        state, "input_pad_connected", false,
        std::function<bool(fakelua::State *, std::int64_t)>(
            [input](fakelua::State *, std::int64_t pad) {
                return input->GamepadConnected(static_cast<int>(pad));
            }));

    fakelua::RegisterNativeFunction(
        state, "input_pad_down", false,
        std::function<bool(fakelua::State *, std::int64_t, std::string_view)>(
            [input](fakelua::State *, std::int64_t pad, std::string_view button) {
                return input->GamepadButtonDown(static_cast<int>(pad), button);
            }));

    fakelua::RegisterNativeFunction(
        state, "input_pad_pressed", false,
        std::function<bool(fakelua::State *, std::int64_t, std::string_view)>(
            [input](fakelua::State *, std::int64_t pad, std::string_view button) {
                return input->GamepadButtonPressed(static_cast<int>(pad), button);
            }));

    fakelua::RegisterNativeFunction(
        state, "input_pad_released", false,
        std::function<bool(fakelua::State *, std::int64_t, std::string_view)>(
            [input](fakelua::State *, std::int64_t pad, std::string_view button) {
                return input->GamepadButtonReleased(static_cast<int>(pad), button);
            }));

    fakelua::RegisterNativeFunction(
        state, "input_pad_axis", false,
        std::function<double(fakelua::State *, std::int64_t, std::string_view)>(
            [input](fakelua::State *, std::int64_t pad, std::string_view axis) -> double {
                return static_cast<double>(input->GamepadAxis(static_cast<int>(pad), axis));
            }));

    fakelua::RegisterNativeFunction(
        state, "input_pad_name", false,
        std::function<std::string(fakelua::State *, std::int64_t)>(
            [input](fakelua::State *, std::int64_t pad) -> std::string {
                return std::string(input->GamepadName(static_cast<int>(pad)));
            }));
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

    // Fixed-step introspection: the physics step actually simulated, how many
    // steps ran this frame, and the render interpolation factor.
    fakelua::RegisterNativeFunction(
        state, "time_sim_dt", false,
        std::function<double(fakelua::State *)>(
            [engine](fakelua::State *) { return engine->PhysicsDt(); }));

    fakelua::RegisterNativeFunction(
        state, "time_steps", false,
        std::function<std::int64_t(fakelua::State *)>(
            [engine](fakelua::State *) {
                return static_cast<std::int64_t>(engine->PhysicsStepsLastFrame());
            }));

    fakelua::RegisterNativeFunction(
        state, "time_alpha", false,
        std::function<double(fakelua::State *)>(
            [engine](fakelua::State *) { return engine->InterpAlpha(); }));
}

void RegisterStorageApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "storage_load", false,
        std::function<bool(fakelua::State *, std::string_view)>(
            [engine](fakelua::State *, std::string_view slot) {
                return engine->GetSaves().LoadSlot(slot, engine->SaveDir());
            }));

    fakelua::RegisterNativeFunction(
        state, "storage_save", false,
        std::function<bool(fakelua::State *, std::string_view)>(
            [engine](fakelua::State *, std::string_view slot) {
                return engine->GetSaves().SaveSlot(slot, engine->SaveDir());
            }));

    fakelua::RegisterNativeFunction(
        state, "storage_set_num", false,
        std::function<void(fakelua::State *, std::string_view, double)>(
            [engine](fakelua::State *, std::string_view key, double v) {
                engine->GetSaves().SetNumber(key, v);
            }));

    fakelua::RegisterNativeFunction(
        state, "storage_set_str", false,
        std::function<void(fakelua::State *, std::string_view, std::string_view)>(
            [engine](fakelua::State *, std::string_view key, std::string_view v) {
                engine->GetSaves().SetString(key, v);
            }));

    fakelua::RegisterNativeFunction(
        state, "storage_get_num", false,
        std::function<double(fakelua::State *, std::string_view, double)>(
            [engine](fakelua::State *, std::string_view key, double fallback) -> double {
                return engine->GetSaves().GetNumber(key, fallback);
            }));

    fakelua::RegisterNativeFunction(
        state, "storage_get_str", false,
        std::function<std::string(fakelua::State *, std::string_view, std::string_view)>(
            [engine](fakelua::State *, std::string_view key,
                     std::string_view fallback) -> std::string {
                return engine->GetSaves().GetString(key, fallback);
            }));

    fakelua::RegisterNativeFunction(
        state, "storage_has", false,
        std::function<bool(fakelua::State *, std::string_view)>(
            [engine](fakelua::State *, std::string_view key) {
                return engine->GetSaves().Has(key);
            }));

    fakelua::RegisterNativeFunction(
        state, "storage_delete", false,
        std::function<void(fakelua::State *, std::string_view)>(
            [engine](fakelua::State *, std::string_view key) {
                engine->GetSaves().Erase(key);
            }));

    fakelua::RegisterNativeFunction(
        state, "storage_reset", false,
        std::function<void(fakelua::State *)>(
            [engine](fakelua::State *) { engine->GetSaves().Reset(); }));
}

void RegisterEntityApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "ent_create", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t tag) -> std::int64_t {
                return static_cast<std::int64_t>(
                    engine->GetEntities().Create(static_cast<std::int64_t>(tag)));
            }));

    fakelua::RegisterNativeFunction(
        state, "ent_destroy", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                engine->GetEntities().Destroy(static_cast<EntityStore::Id>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "ent_clear", false,
        std::function<void(fakelua::State *)>(
            [engine](fakelua::State *) { engine->GetEntities().Clear(); }));

    fakelua::RegisterNativeFunction(
        state, "ent_active", false,
        std::function<bool(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                return engine->GetEntities().Active(static_cast<EntityStore::Id>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "ent_tag", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) -> std::int64_t {
                return engine->GetEntities().Tag(static_cast<EntityStore::Id>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "ent_set_tag", false,
        std::function<void(fakelua::State *, std::int64_t, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id, std::int64_t tag) {
                engine->GetEntities().SetTag(static_cast<EntityStore::Id>(id), tag);
            }));

    fakelua::RegisterNativeFunction(
        state, "ent_num", false,
        std::function<double(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double slot) -> double {
                if (!std::isfinite(slot) || slot < 0.0) {
                    return 0.0;
                }
                return engine->GetEntities().GetNumber(
                    static_cast<EntityStore::Id>(id), static_cast<std::size_t>(slot));
            }));

    fakelua::RegisterNativeFunction(
        state, "ent_set_num", false,
        std::function<void(fakelua::State *, std::int64_t, double, double)>(
            [engine](fakelua::State *, std::int64_t id, double slot, double v) {
                if (!std::isfinite(slot) || slot < 0.0) {
                    return;
                }
                engine->GetEntities().SetNumber(static_cast<EntityStore::Id>(id),
                                                static_cast<std::size_t>(slot), v);
            }));

    fakelua::RegisterNativeFunction(
        state, "ent_str", false,
        std::function<std::string(fakelua::State *, std::int64_t, double)>(
            [engine](fakelua::State *, std::int64_t id, double slot) -> std::string {
                if (!std::isfinite(slot) || slot < 0.0) {
                    return {};
                }
                return engine->GetEntities().GetString(
                    static_cast<EntityStore::Id>(id), static_cast<std::size_t>(slot));
            }));

    fakelua::RegisterNativeFunction(
        state, "ent_set_str", false,
        std::function<void(fakelua::State *, std::int64_t, double, std::string_view)>(
            [engine](fakelua::State *, std::int64_t id, double slot,
                     std::string_view v) {
                if (!std::isfinite(slot) || slot < 0.0) {
                    return;
                }
                engine->GetEntities().SetString(static_cast<EntityStore::Id>(id),
                                                static_cast<std::size_t>(slot), v);
            }));

    fakelua::RegisterNativeFunction(
        state, "ent_count", false,
        std::function<std::int64_t(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t tag) -> std::int64_t {
                auto &store = engine->GetEntities();
                return tag < 0 ? static_cast<std::int64_t>(store.Count())
                               : static_cast<std::int64_t>(
                                     store.CountWithTag(static_cast<std::int64_t>(tag)));
            }));

    fakelua::RegisterNativeFunction(
        state, "ent_at", false,
        std::function<std::int64_t(fakelua::State *, double, std::int64_t)>(
            [engine](fakelua::State *, double index, std::int64_t tag) -> std::int64_t {
                if (!std::isfinite(index) || index < 0.0) {
                    return 0;
                }
                auto &store = engine->GetEntities();
                const auto idx = static_cast<std::size_t>(index);
                const auto id = tag < 0
                                    ? store.At(idx)
                                    : store.AtWithTag(idx, static_cast<std::int64_t>(tag));
                return static_cast<std::int64_t>(id);
            }));
}

void RegisterShaderApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "shader_load", false,
        std::function<std::int64_t(fakelua::State *, std::string_view, std::string_view)>(
            [engine](fakelua::State *, std::string_view vs, std::string_view fs) -> std::int64_t {
                return engine->GetRenderer().LoadShaderFromFile(std::string(vs), std::string(fs));
            }));

    fakelua::RegisterNativeFunction(
        state, "shader_destroy", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                engine->GetRenderer().DestroyShader(static_cast<int>(id));
            }));

    fakelua::RegisterNativeFunction(
        state, "shader_set_float", false,
        std::function<void(fakelua::State *, std::int64_t, std::string_view, double)>(
            [engine](fakelua::State *, std::int64_t id, std::string_view name, double v) {
                engine->GetRenderer().SetShaderFloat(static_cast<int>(id), name,
                                                     static_cast<float>(v));
            }));

    fakelua::RegisterNativeFunction(
        state, "shader_set_int", false,
        std::function<void(fakelua::State *, std::int64_t, std::string_view, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id, std::string_view name,
                     std::int64_t v) {
                engine->GetRenderer().SetShaderInt(static_cast<int>(id), name,
                                                   static_cast<int>(v));
            }));

    fakelua::RegisterNativeFunction(
        state, "shader_set_vec2", false,
        std::function<void(fakelua::State *, std::int64_t, std::string_view, double,
                           double)>(
            [engine](fakelua::State *, std::int64_t id, std::string_view name,
                     double x, double y) {
                engine->GetRenderer().SetShaderVec2(static_cast<int>(id), name,
                                                    static_cast<float>(x),
                                                    static_cast<float>(y));
            }));

    fakelua::RegisterNativeFunction(
        state, "shader_set_vec4", false,
        std::function<void(fakelua::State *, std::int64_t, std::string_view, double,
                           double, double, double)>(
            [engine](fakelua::State *, std::int64_t id, std::string_view name,
                     double r, double g, double b, double a) {
                engine->GetRenderer().SetShaderVec4(
                    static_cast<int>(id), name,
                    fake2d::Color{static_cast<float>(r), static_cast<float>(g),
                                  static_cast<float>(b), static_cast<float>(a)});
            }));

    fakelua::RegisterNativeFunction(
        state, "shader_set_vec2", false,
        std::function<void(fakelua::State *, std::int64_t, std::string_view, double, double)>(
            [engine](fakelua::State *, std::int64_t id, std::string_view name, double x,
                     double y) {
                engine->GetRenderer().SetShaderVec2(static_cast<int>(id), name,
                                                    static_cast<float>(x),
                                                    static_cast<float>(y));
            }));

    fakelua::RegisterNativeFunction(
        state, "shader_set_vec4", false,
        std::function<void(fakelua::State *, std::int64_t, std::string_view, double, double,
                           double, double)>(
            [engine](fakelua::State *, std::int64_t id, std::string_view name, double r,
                     double g, double b, double a) {
                engine->GetRenderer().SetShaderVec4(
                    static_cast<int>(id), name,
                    Color{static_cast<float>(r), static_cast<float>(g),
                          static_cast<float>(b), static_cast<float>(a)});
            }));

    fakelua::RegisterNativeFunction(
        state, "draw_use_shader", false,
        std::function<void(fakelua::State *, std::int64_t)>(
            [engine](fakelua::State *, std::int64_t id) {
                engine->GetRenderer().UseShaderById(static_cast<int>(id));
            }));
}

void RegisterWindowApi(fakelua::State *state, Engine *engine) {
    fakelua::RegisterNativeFunction(
        state, "window_quit", false,
        std::function<void(fakelua::State *)>(
            [engine](fakelua::State *) { engine->RequestQuit(); }));

    fakelua::RegisterNativeFunction(
        state, "window_set_title", false,
        std::function<void(fakelua::State *, std::string_view)>(
            [engine](fakelua::State *, std::string_view title) { engine->SetWindowTitle(title); }));

    fakelua::RegisterNativeFunction(
        state, "window_set_fullscreen", false,
        std::function<void(fakelua::State *, double)>(
            [engine](fakelua::State *, double fullscreen) {
                engine->SetFullscreen(fullscreen != 0.0);
            }));

    fakelua::RegisterNativeFunction(
        state, "window_fullscreen", false,
        std::function<bool(fakelua::State *)>(
            [engine](fakelua::State *) { return engine->IsFullscreen(); }));
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
    RegisterStorageApi(state, engine);
    RegisterEntityApi(state, engine);
    RegisterShaderApi(state, engine);
    RegisterWindowApi(state, engine);
    RegisterDebugApi(state);
}

} // namespace fake2d
